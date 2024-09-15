#include <endian.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <stdbool.h>

#include "ppcb-udpr.h"
#include "err.h"
#include "ppcb-common.h"
#include "protconst.h"


/// UDPR CLIENT HELPER FUNCTIONS ///

static void client_initialise_connection(
        int                 socket_fd,
        struct sockaddr_in  server_address,
        uint64_t            session_id,
        uint64_t            byte_sequence_length,
        char                *buffer
) {
    struct sockaddr_in receive_address;
    PPCB_CONN_packet data_to_send;
    set_CONN(&data_to_send, session_id, PPCB_UDPR, byte_sequence_length);

    ssize_t received_length, sent_length;

    for (size_t transmit = 0; transmit < MAX_RETRANSMITS + 1; transmit++) {
        sent_length = send_packet_udp(socket_fd, server_address,
                                       sizeof(PPCB_CONN_packet), &data_to_send);
        validate_send(sent_length, sizeof(PPCB_CONN_packet), true, PPCB_UDPR, "sending CONN");

        do {
            received_length = receive_packet_udp(socket_fd, &receive_address, buffer, false);
            if (received_length < 0) {
                sys_fatal("recvfrom");
            } else if (received_length == 0) {
                break; // timeout
            }
        } while (different_addresses(server_address, receive_address));

        if (received_length == 0) {
            continue; // timeout
        }

        if ((size_t) received_length != sizeof(PPCB_RESPONSE_packet)) {
            fatal("receiving CONACC");
        }

        // Validating return packet.
        PPCB_RESPONSE_packet data_received;
        memcpy(&data_received, buffer, sizeof(PPCB_RESPONSE_packet));
        validate_response_packet(&data_received, PPCB_CONACC, session_id);

        return;
    }

    fatal("didn't receive CONACC after retransmission");
}

bool client_receives_packet(
        int                 socket_fd,
        struct sockaddr_in  server_address,
        uint64_t            session_id,
        uint64_t            packet_number,
        char                *buffer,
        PPCB_Packet_id      confirming_packet
) {
    struct sockaddr_in receive_address;
    char *waiting_for = (confirming_packet == PPCB_ACC) ? "ACC" : "RCVD";

    for (;;) {
        ssize_t received_length = receive_packet_udp(socket_fd, &receive_address, buffer, false);

        if (received_length < 0) {
            sys_fatal("recvfrom");
        } else if (received_length == 0) {
            return false; // timeout
        }

        if (different_addresses(receive_address, server_address)) {
            continue; // Reset timer and wait for another packet.
        }

        uint8_t packet_id;
        memcpy(&packet_id, buffer, sizeof(uint8_t));

        // Check if we received previous CONACC.
        if (packet_id == PPCB_CONACC && received_length == sizeof(PPCB_RESPONSE_packet)) {
            PPCB_RESPONSE_packet response_packet;
            memcpy(&response_packet, buffer, sizeof(PPCB_RESPONSE_packet));
            validate_response_packet(&response_packet, PPCB_CONACC, session_id);
        } // Check if we received previous ACC.
        else if (packet_id == PPCB_ACC && received_length == sizeof(PPCB_PACKET_RESPONSE_packet)) {
            PPCB_PACKET_RESPONSE_packet response_packet;
            memcpy(&response_packet, buffer, sizeof(PPCB_PACKET_RESPONSE_packet));
            response_packet.packet_number = be64toh(response_packet.packet_number);

            if (response_packet.session_id != session_id) {
                fatal("incorrect session id");
            }
            if (response_packet.packet_number < packet_number) {
                continue; // previous ACC packet
            }
            if (response_packet.packet_number == packet_number && confirming_packet == PPCB_ACC) {
                return true; // We got packet we were waiting for
            }

            fatal("receiving %s", waiting_for);
        } // Check if we received correct packet.
        else if (packet_id == PPCB_RCVD && confirming_packet == PPCB_RCVD
                && (size_t)received_length == sizeof(PPCB_RESPONSE_packet)) {

            PPCB_RESPONSE_packet response_packet;
            memcpy(&response_packet, buffer, sizeof(PPCB_RESPONSE_packet));
            validate_response_packet(&response_packet, PPCB_RCVD, session_id);

            return true;
        } // Unknown packet id.
        else {
            fatal("receiving %s", waiting_for);
        }
    }

    return false;
}

static void client_send_bytes_to_server(
        int                 socket_fd,
        struct sockaddr_in  server_address,
        uint64_t            session_id,
        uint64_t            packet_number,
        uint32_t            current_send,
        uint32_t            bytes_send,
        char                *byte_sequence,
        char                *buffer,
        char                *send_buffer
) {
    PPCB_DATA_packet data_to_send;
    set_DATA(&data_to_send, session_id, packet_number, current_send);

    memcpy(send_buffer, &data_to_send, sizeof(PPCB_DATA_packet));
    memcpy(send_buffer + sizeof(PPCB_DATA_packet), byte_sequence + bytes_send, current_send);

    size_t message_length = current_send + sizeof(PPCB_DATA_packet);
    ssize_t sent_length;
    bool received_packet;

    for (ssize_t transmit = 0; transmit < MAX_RETRANSMITS + 1; transmit++) {
        sent_length = send_packet_udp(socket_fd, server_address, message_length, send_buffer);
        validate_send(sent_length, message_length, true, PPCB_UDPR, "sending DATA");

        received_packet = client_receives_packet(socket_fd, server_address, session_id, packet_number,
                                                 buffer, PPCB_ACC);
        if (!received_packet) {
            continue;
        }

        return;
    }

    fatal("didn't receive ACC after retransmissions");
}

/// UDPR CLIENT FUNCTION ///

void send_bytes_udpr(
        int                   socket_fd,
        struct sockaddr_in    server_address,
        uint64_t              session_id,
        uint64_t              byte_sequence_length,
        char*                 byte_sequence
) {
    static char buffer[BUFFER_SIZE], send_buffer[BUFFER_SIZE];

    client_initialise_connection(socket_fd, server_address, session_id, byte_sequence_length, buffer);

    // Data exchange.
    uint64_t bytes_send = 0, packet_number = 0;
    uint32_t max_size = min(MAX_PACKET_SIZE, PACKET_SIZE);

    while (bytes_send < byte_sequence_length) {
        uint32_t current_send = min(max_size, byte_sequence_length - bytes_send);

        client_send_bytes_to_server(socket_fd, server_address, session_id, packet_number,
                                    current_send, bytes_send, byte_sequence,  buffer, send_buffer);

        bytes_send += current_send;
        packet_number++;
    }

    if (!client_receives_packet(socket_fd, server_address, session_id, packet_number, buffer,
                                PPCB_RCVD)) {
        fatal("didn't receive RCVD");
    }
}

/// UDPR SERVER HELPER FUNCTIONS ///

ssize_t server_sends_packet(
        int                 socket_fd,
        struct sockaddr_in  client_address,
        uint64_t            session_id,
        uint64_t            packet_number,
        PPCB_Packet_id      confirming_packet
) {
    if (confirming_packet == PPCB_ACC) {
        PPCB_PACKET_RESPONSE_packet data_to_send;
        set_PACKET_RESPONSE(&data_to_send, PPCB_ACC, session_id, packet_number);
        return send_packet_udp(socket_fd, client_address,
                               sizeof(PPCB_PACKET_RESPONSE_packet), &data_to_send);
    }
    else {
        PPCB_RESPONSE_packet data_to_send;
        set_RESPONSE(&data_to_send, confirming_packet, session_id);

        return send_packet_udp(socket_fd, client_address,
                               sizeof(PPCB_RESPONSE_packet), &data_to_send);
    }
}

static bool validate_CONN_packet(
        uint64_t    session_id,
        uint8_t     packet_id,
        uint64_t    byte_sequence_length,
        char        *buffer
) {
    PPCB_CONN_packet conn_packet;
    memcpy(&conn_packet, buffer, sizeof(PPCB_CONN_packet));

    conn_packet.byte_sequence_length = be64toh(conn_packet.byte_sequence_length);

    if (packet_id != PPCB_CONN || conn_packet.session_id != session_id ||
        conn_packet.protocol_id != PPCB_UDPR ||
        conn_packet.byte_sequence_length != byte_sequence_length) {
        return false;
    }

    return true;
}

ssize_t server_receives_packet(
        int                 socket_fd,
        struct sockaddr_in  client_address,
        uint64_t            session_id,
        uint64_t            packet_number,
        uint64_t            byte_sequence_length,
        uint64_t            bytes_received,
        char                *buffer
) {
    struct sockaddr_in receive_address;

    for (;;) {
        ssize_t received_length = receive_packet_udp(socket_fd, &receive_address, buffer, false);

        if (received_length < 0) {
            sys_error("recvfrom");
            return -1;
        } else if (received_length == 0) {
            return 0; // timeout
        }

        uint8_t packet_id;
        memcpy(&packet_id, buffer, sizeof(uint8_t));

        // First we need to check if this is a correct client.
        if (different_addresses(receive_address, client_address)) {
            if (packet_id == PPCB_CONN) {
                server_sends_RESPONSE_udp(socket_fd, receive_address, 0, PPCB_UDPR, PPCB_CONRJT);
            }
            else if (packet_id == PPCB_DATA) {
                server_sends_RJT_udp(socket_fd, receive_address, 0, packet_number, PPCB_UDPR);
            }
            continue;
        }

        // We might receive previous CONN.
        if ((size_t) received_length == sizeof(PPCB_CONN_packet)) {
            if (!validate_CONN_packet(session_id, packet_id, byte_sequence_length, buffer)) {
                error("invalid CONN");
                return -1;
            }

            continue;
        }

        // Now we check if this is DATA.
        if (packet_id != PPCB_DATA || (size_t) received_length < sizeof(PPCB_DATA_packet)) {
            error("invalid DATA");
            if (packet_id == PPCB_DATA) {
                server_sends_RJT_udp(socket_fd, client_address, session_id, packet_number, PPCB_UDPR);
            }
            return -1;
        }

        PPCB_DATA_packet data_packet;
        memcpy(&data_packet, buffer, sizeof(PPCB_DATA_packet));

        data_packet.packet_number = be64toh(data_packet.packet_number);
        data_packet.packet_byte_sequence_length = be32toh(data_packet.packet_byte_sequence_length);
        size_t message_length = sizeof(PPCB_DATA_packet) + data_packet.packet_byte_sequence_length;

        if ((size_t) received_length != message_length ||
            !validate_data_packet(&data_packet,PPCB_UDPR, session_id, packet_number,
                                  bytes_received, byte_sequence_length)) {

            error("invalid DATA");
            server_sends_RJT_udp(socket_fd, client_address, session_id, packet_number, PPCB_UDPR);
            return -1;
        }

        if (data_packet.packet_number < packet_number) {
            continue; // Got previous DATA
        } // Got waited for DATA

        if (data_packet.packet_byte_sequence_length > byte_sequence_length - bytes_received) {
            error("invalid DATA");
            server_sends_RJT_udp(socket_fd, client_address, session_id, packet_number, PPCB_UDPR);

            return -1;
        }

        return data_packet.packet_byte_sequence_length;
    }

    return 0;
}

ssize_t exchange_server(
        int                 socket_fd,
        struct sockaddr_in  client_address,
        uint64_t            session_id,
        uint64_t            packet_number,
        PPCB_Packet_id      confirming_packet,
        uint64_t            byte_sequence_length,
        uint64_t            bytes_received,
        char                *buffer
) {
    char *sending_error;
    size_t expected_length;
    if (confirming_packet == PPCB_ACC) {
        sending_error = "sending ACC";
        expected_length = sizeof(PPCB_PACKET_RESPONSE_packet);
    }
    else {
        sending_error = "sending CONACC";
        expected_length = sizeof(PPCB_RESPONSE_packet);
    }

    for (ssize_t transmit = 0; transmit < MAX_RETRANSMITS + 1; transmit++) {
        ssize_t sent_length = server_sends_packet(socket_fd, client_address, session_id,
                                                  packet_number, confirming_packet);
        validate_send(sent_length, expected_length, false, PPCB_UDPR, sending_error);

        ssize_t received_length = server_receives_packet(socket_fd, client_address, session_id,
                                                 packet_number + (confirming_packet == PPCB_ACC),
                                                 byte_sequence_length, bytes_received, buffer);

        if (received_length == -1) {
            return -1; // error occurred
        } else if (received_length == 0) {
            continue; // timeout
        }

        printf("%.*s", (int) received_length, buffer + sizeof(PPCB_DATA_packet));
        fflush(stdout);
        return received_length;
    }

    error("didn't receive DATA after retransmissions");
    return -1;
}

/// UDPR SERVER FUNCTION ///

void handle_connection_udpr(
        int                 socket_fd,
        struct sockaddr_in  client_address,
        uint64_t            session_id,
        uint64_t            byte_sequence_length,
        char                *buffer
) {
    uint64_t bytes_received = 0, packet_number = 0;
    ssize_t received_length = exchange_server(socket_fd, client_address, session_id,
                                              packet_number, PPCB_CONACC, byte_sequence_length,
                                              bytes_received, buffer);

    if (received_length < 0) {
        return;
    }

    bytes_received += (uint64_t) received_length;

    while (bytes_received < byte_sequence_length) {
        received_length = exchange_server(socket_fd,  client_address, session_id,
                                          packet_number,PPCB_ACC, byte_sequence_length,
                                          bytes_received, buffer);

        if (received_length < 0) {
            return;
        }

        bytes_received += (uint64_t) received_length;
        packet_number++;
    }

    // Servers sends ACC once.
    ssize_t sent_length = server_sends_packet(socket_fd, client_address, session_id,
                                              packet_number, PPCB_ACC);
    validate_send(sent_length, sizeof(PPCB_PACKET_RESPONSE_packet), false, PPCB_UDPR, "sending ACC");

    // Server sends RCVD once.
    sent_length = server_sends_packet(socket_fd, client_address, session_id,
                                              packet_number, PPCB_RCVD);
    validate_send(sent_length, sizeof(PPCB_RESPONSE_packet), false, PPCB_UDPR, "sending RCVD");
}
