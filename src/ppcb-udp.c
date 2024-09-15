#include <endian.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <stdbool.h>

#include "ppcb-udp.h"
#include "err.h"
#include "ppcb-common.h"


/// UDP CLIENT HELPER FUNCTIONS ///

static void client_receives_RESPONSE(
        int                     socket_fd,
        struct sockaddr_in      server_address,
        uint64_t                session_id,
        char                    *buffer,
        PPCB_Packet_id          waiting_for
) {
    struct sockaddr_in receive_address;
    ssize_t received_length;
    char *error_message = (waiting_for == PPCB_CONACC) ? "receiving CONACC" : "receiving RCVD";

    do {
        received_length = receive_packet_udp(socket_fd, &receive_address, buffer, false);
        if (received_length < 0) {
            sys_fatal("recvfrom");
        }
        else if (received_length == 0) {
            sys_fatal("timeout");
        }
    } while (different_addresses(server_address, receive_address));

    if ((size_t)received_length != sizeof(PPCB_RESPONSE_packet)) {
        fatal(error_message);
    }

    // Validating return packet.
    PPCB_RESPONSE_packet data_received;
    memcpy(&data_received, buffer, sizeof(PPCB_RESPONSE_packet));
    validate_response_packet(&data_received, waiting_for, session_id);
}

static void client_initialise_connection(
        int                   socket_fd,
        struct sockaddr_in    server_address,
        uint64_t              session_id,
        uint64_t              byte_sequence_length,
        char                  *buffer
) {
    // Establishing a connection.
    PPCB_CONN_packet data_to_send;
    set_CONN(&data_to_send, session_id,PPCB_UDP, byte_sequence_length);
    ssize_t sent_length = send_packet_udp(socket_fd, server_address,
                                          sizeof(PPCB_CONN_packet), &data_to_send);
    validate_send(sent_length, sizeof(PPCB_CONN_packet), true, PPCB_UDP, "sending CONN");

    client_receives_RESPONSE(socket_fd, server_address, session_id, buffer, PPCB_CONACC);
}

static void client_send_bytes_to_server(
        int                   socket_fd,
        struct sockaddr_in    server_address,
        uint64_t              session_id,
        char*                 byte_sequence,
        uint64_t              byte_sequence_length,
        char                  *buffer
) {
    // Data exchange.
    uint64_t bytes_send = 0, packet_number = 0;
    uint32_t max_size = min(PACKET_SIZE, MAX_PACKET_SIZE);

    while (bytes_send < byte_sequence_length) {
        uint32_t current_send = min((uint64_t)max_size, byte_sequence_length - bytes_send);
        size_t message_length = sizeof(PPCB_DATA_packet) + current_send;

        // Coping packet to the buffer.
        PPCB_DATA_packet data_packet;
        set_DATA(&data_packet, session_id, packet_number, current_send);
        memcpy(buffer, &data_packet, sizeof(PPCB_DATA_packet));
        memcpy(buffer + sizeof(PPCB_DATA_packet), byte_sequence + bytes_send, current_send);

        // Sending packet
        ssize_t sent_length = send_packet_udp(socket_fd, server_address, message_length, buffer);
        validate_send(sent_length, message_length, true, PPCB_UDP, "sending DATA");

        bytes_send += (uint64_t) current_send;
        packet_number++;
    }
}

/// UDP CLIENT FUNCTION ///

void send_bytes_udp(
        int                   socket_fd,
        struct sockaddr_in    server_address,
        uint64_t              session_id,
        uint64_t              byte_sequence_length,
        char*                 byte_sequence
) {
    static char buffer[BUFFER_SIZE];

    client_initialise_connection(socket_fd, server_address, session_id, byte_sequence_length, buffer);

    client_send_bytes_to_server(socket_fd, server_address, session_id, byte_sequence,
                                byte_sequence_length, buffer);

    client_receives_RESPONSE(socket_fd, server_address, session_id, buffer, PPCB_RCVD);
}

/// UDP SERVER HELPER FUNCTIONS ///

static bool server_receive_bytes(
        int                 socket_fd,
        struct sockaddr_in  client_address,
        uint64_t            session_id,
        uint64_t            byte_sequence_length,
        char                *buffer
) {
    uint64_t bytes_received = 0, packet_number = 0;
    struct sockaddr_in receive_address;

    while (bytes_received < byte_sequence_length) {
        ssize_t received_length = receive_packet_udp(socket_fd, &receive_address, buffer, false);

        if (received_length < 0) {
            sys_error("recvfrom");
            return false;
        }
        else if (received_length == 0) {
            sys_error("timeout");
            return false;
        }

        uint8_t packet_id;
        memcpy(&packet_id, buffer, sizeof(uint8_t));

        // First we need to check if this is a correct client.
        if (different_addresses(client_address, receive_address)) {
            if (packet_id == PPCB_CONN) {
                server_sends_RESPONSE_udp(socket_fd, receive_address, 0, PPCB_UDP, PPCB_CONRJT);
            }
            else if (packet_id == PPCB_DATA) {
                server_sends_RJT_udp(socket_fd, receive_address, 0, packet_number, PPCB_UDP);
            }
            continue;
        }

        if (packet_id != PPCB_DATA || (size_t)received_length < sizeof(PPCB_DATA_packet)) {
            error("invalid DATA");
            if (packet_id == PPCB_DATA) {
                server_sends_RJT_udp(socket_fd, client_address, session_id, packet_number, PPCB_UDP);
            }
            return false;
        }

        PPCB_DATA_packet data_packet;
        memcpy(&data_packet, buffer, sizeof(PPCB_DATA_packet));

        data_packet.packet_number = be64toh(data_packet.packet_number);
        data_packet.packet_byte_sequence_length = be32toh(data_packet.packet_byte_sequence_length);
        size_t message_length = sizeof(PPCB_DATA_packet) + data_packet.packet_byte_sequence_length;

        if ((size_t) received_length != message_length ||
            !validate_data_packet(&data_packet, PPCB_UDP, session_id, packet_number,
                                  bytes_received, byte_sequence_length)
        ) {
            error("invalid DATA");
            server_sends_RJT_udp(socket_fd, client_address, session_id, packet_number, PPCB_UDP);

            return false;
        }

        printf("%.*s", data_packet.packet_byte_sequence_length, buffer + sizeof(PPCB_DATA_packet));
        fflush(stdout);

        bytes_received += (uint64_t) data_packet.packet_byte_sequence_length;
        packet_number++;
    }

    return true;
}

/// UDP SERVER FUNCTION ///

void handle_connection_udp(
        int                 socket_fd,
        struct sockaddr_in  client_address,
        uint64_t            session_id,
        uint64_t            byte_sequence_length,
        char                *buffer
) {
    // Sending CONACC to client.
    PPCB_RESPONSE_packet data_to_send;
    set_RESPONSE(&data_to_send, PPCB_CONACC, session_id);
    ssize_t sent_length = send_packet_udp(socket_fd, client_address,
                                          sizeof(PPCB_RESPONSE_packet), &data_to_send);
    if (!validate_send(sent_length, sizeof(PPCB_RESPONSE_packet), false,
                       PPCB_UDP, "sending CONACC")) {
        return;
    }

    if (!server_receive_bytes(socket_fd, client_address, session_id, byte_sequence_length, buffer)) {
        return;
    }

    server_sends_RESPONSE_udp(socket_fd, client_address, session_id, PPCB_UDP, PPCB_RCVD);
}