#include <endian.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdbool.h>

#include "ppcb-tcp.h"
#include "err.h"
#include "ppcb-common.h"
#include "protconst.h"


/// COMMUNICATION FUNCTIONS ///

static ssize_t send_packet_tcp(
        int         socket_fd,
        size_t      data_length,
        void        *data
) {
    return writen(socket_fd, data, data_length);
}

static ssize_t receive_packet_tcp(
        int         client_fd,
        size_t      data_length,
        void        *data
) {
    // Set timeouts for the client socket.
    struct timeval to = {.tv_sec = MAX_WAIT, .tv_usec = 0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);

    ssize_t read_length = readn(client_fd, data, data_length);

    if (read_length < 0) {
        if (errno != EAGAIN) {
            return -1;
        }
        return 0;
    }
    return read_length;
}

/// TCP CLIENT HELPER FUNCTIONS ///

static void client_receives_RESPONSE(
        int                 socket_fd,
        uint64_t            session_id,
        PPCB_Packet_id      waiting_for
) {
    char *error_message = (waiting_for == PPCB_CONACC) ? "receiving CONACC" : "receiving RCVD";

    PPCB_RESPONSE_packet data_received;
    ssize_t received_length = receive_packet_tcp(socket_fd, sizeof(PPCB_RESPONSE_packet),
                                                 &data_received);
    validate_receive(received_length, sizeof(PPCB_RESPONSE_packet), true,
                     PPCB_TCP, error_message);
    validate_response_packet(&data_received, waiting_for, session_id);
}


static void client_initialise_connection(
    int                     socket_fd,
    struct sockaddr_in      server_address,
    uint64_t                session_id,
    uint64_t                byte_sequence_length
) {
    // Connect to the server.
    if (connect(socket_fd, (struct sockaddr *) &server_address,
                (socklen_t) sizeof(server_address)) < 0) {
        sys_fatal("connect");
    }

    // Establishing a connection.
    PPCB_CONN_packet data_to_send;
    set_CONN(&data_to_send, session_id,PPCB_TCP, byte_sequence_length);
    ssize_t sent_length = send_packet_tcp(socket_fd, sizeof(PPCB_CONN_packet), &data_to_send);
    validate_send(sent_length, sizeof(PPCB_CONN_packet), true, PPCB_TCP, "sending CONN");

    client_receives_RESPONSE(socket_fd, session_id, PPCB_CONACC);
}


static void client_send_bytes_to_server(
        int                   socket_fd,
        uint64_t              session_id,
        uint64_t              byte_sequence_length,
        char*                 byte_sequence
) {
    static char buffer[BUFFER_SIZE];

    // Data exchange.
    ssize_t sent_length;
    uint64_t bytes_send = 0, packet_number = 0;
    uint32_t max_size = min(PACKET_SIZE, MAX_PACKET_SIZE);

    while (bytes_send < byte_sequence_length) {
        uint32_t current_send = min((uint64_t)max_size, byte_sequence_length - bytes_send);
        uint32_t message_length = sizeof(PPCB_DATA_packet) + current_send;

        // Coping packet to the buffer.
        PPCB_DATA_packet data_packet;
        set_DATA(&data_packet, session_id, packet_number, current_send);
        memcpy(buffer, &data_packet, sizeof(PPCB_DATA_packet));
        memcpy(buffer + sizeof(PPCB_DATA_packet), byte_sequence + bytes_send, current_send);

        // Sending packet.
        sent_length = send_packet_tcp(socket_fd, message_length, buffer);
        validate_send(sent_length, message_length, true, PPCB_TCP, "sending DATA");

        bytes_send += (uint64_t) current_send;
        packet_number++;
    }
}

/// TCP CLIENT FUNCTION ///

void send_bytes_tcp(
        int                   socket_fd,
        struct sockaddr_in    server_address,
        uint64_t              session_id,
        uint64_t              byte_sequence_length,
        char*                 byte_sequence
) {
    client_initialise_connection(socket_fd, server_address, session_id, byte_sequence_length);

    client_send_bytes_to_server(socket_fd, session_id, byte_sequence_length, byte_sequence);

    client_receives_RESPONSE(socket_fd, session_id, PPCB_RCVD);
}

/// TCP SERVER HELPER FUNCTIONS ///

static void server_sends_RJT_tcp(
        int         client_fd,
        uint64_t    session_id,
        uint64_t    packet_number
) {
    PPCB_PACKET_RESPONSE_packet reject_packet;
    set_PACKET_RESPONSE(&reject_packet, PPCB_RJT, session_id, packet_number);
    ssize_t sent_length = send_packet_tcp(client_fd, sizeof(PPCB_PACKET_RESPONSE_packet),
                                          &reject_packet);
    validate_send(sent_length, sizeof(PPCB_PACKET_RESPONSE_packet), false, PPCB_TCP, "sending RJT");
}

static void server_sends_RESPONSE(
        int             socket_fd,
        uint64_t        session_id,
        PPCB_Packet_id  sending
) {
    char *error_message = (sending == PPCB_CONRJT) ? "sending CONRJT" : "sending RCVD";
    PPCB_RESPONSE_packet data_response;
    set_RESPONSE(&data_response, sending, session_id);
    ssize_t sent_length = send_packet_tcp(socket_fd, sizeof(PPCB_RESPONSE_packet), &data_response);
    validate_send(sent_length, sizeof(PPCB_RESPONSE_packet), false, PPCB_TCP, error_message);
}

static bool server_receive_bytes(
        int         client_fd,
        uint64_t    session_id,
        uint64_t    byte_sequence_length,
        char        *buffer
) {
    uint64_t bytes_received = 0, packet_number = 0;
    ssize_t received_length;

    while (bytes_received < byte_sequence_length) {
        PPCB_DATA_packet data_packet;
        received_length = receive_packet_tcp(client_fd, sizeof(PPCB_DATA_packet), &data_packet);
        if (!validate_receive(received_length, sizeof(PPCB_DATA_packet), false,
                              PPCB_TCP,"receiving DATA")) {
            return false;
        }

        data_packet.packet_number = be64toh(data_packet.packet_number);
        data_packet.packet_byte_sequence_length = be32toh(data_packet.packet_byte_sequence_length);

        if (data_packet.id != PPCB_DATA ||
            !validate_data_packet(&data_packet, PPCB_TCP, session_id, packet_number,
                                  bytes_received, byte_sequence_length)
            ) {
            error("invalid DATA");
            if (data_packet.id == PPCB_DATA) {
                server_sends_RJT_tcp(client_fd, session_id, packet_number);
            }

            return false;
        }

        received_length = receive_packet_tcp(client_fd, data_packet.packet_byte_sequence_length,
                                             buffer + sizeof(PPCB_DATA_packet));
        if (!validate_receive(received_length, data_packet.packet_byte_sequence_length,
                              false, PPCB_TCP, "receiving DATA")) {
            error("invalid DATA");
            server_sends_RJT_tcp(client_fd, session_id, packet_number);
            return false;
        }

        printf("%.*s", data_packet.packet_byte_sequence_length, buffer + sizeof(PPCB_DATA_packet));
        fflush(stdout);

        bytes_received += (uint64_t) data_packet.packet_byte_sequence_length;
        packet_number++;
    }

    return true;
}

/// TCP SERVER FUNCTION ///

void handle_connection_tcp(
        int     client_fd,
        char    *buffer
) {
    // Receiving CONN packet.
    PPCB_CONN_packet data_received;
    ssize_t sent_length;
    ssize_t received_length = receive_packet_tcp(client_fd, sizeof(PPCB_CONN_packet), &data_received);
    if (!validate_receive(received_length, sizeof(PPCB_CONN_packet), false,
                          PPCB_TCP,"receiving CONN")) {
        return;
    }

    uint64_t session_id = data_received.session_id;
    uint64_t byte_sequence_length = be64toh(data_received.byte_sequence_length);

    if (data_received.id != PPCB_CONN || data_received.protocol_id != PPCB_TCP ||
        byte_sequence_length == 0) {
        error("invalid CONN");

        if (data_received.id == PPCB_CONN) {
            server_sends_RESPONSE(client_fd, session_id, PPCB_CONRJT);
        }

        return;
    }

    // Responding to client.
    PPCB_RESPONSE_packet data_to_send;
    set_RESPONSE(&data_to_send, PPCB_CONACC, session_id);
    sent_length = send_packet_tcp(client_fd, sizeof(PPCB_RESPONSE_packet), &data_to_send);
    if (!validate_send(sent_length, sizeof(PPCB_RESPONSE_packet), false,
                       PPCB_TCP, "sending CONACC")) {
        return;
    }

    if (!server_receive_bytes(client_fd, session_id, byte_sequence_length, buffer)) {
        return;
    }

    server_sends_RESPONSE(client_fd, session_id, PPCB_RCVD);
}
