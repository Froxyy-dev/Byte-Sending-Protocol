#include <sys/types.h>
#include <sys/socket.h>
#include <endian.h>
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdbool.h>

#include "ppcb-common.h"
#include "err.h"
#include "ppcb-tcp.h"
#include "ppcb-udp.h"
#include "ppcb-udpr.h"


void setup_tcp_server(
        int socket_fd,
        struct sockaddr_in server_address,
        char *buffer
) {
    // Switch the socket to listening.
    if (listen(socket_fd, QUEUE_LENGTH) < 0) {
        sys_fatal("listen");
    }

    // Find out what port the server is actually listening on.
    socklen_t length = (socklen_t) sizeof server_address;
    if (getsockname(socket_fd,(struct sockaddr *) &server_address, &length) < 0) {
        sys_fatal("getsockname");
    }

    for (;;) {
        struct sockaddr_in client_address;

        int client_fd = accept(socket_fd, (struct sockaddr *) &client_address,
                               &((socklen_t) {sizeof(client_address)}));
        if (client_fd < 0) {
            sys_fatal("accept");
        }

        handle_connection_tcp(client_fd, buffer);
        close(client_fd);
    }
}

void setup_udp_server(
        int socket_fd,
        char *buffer
) {
    ssize_t received_length;

    struct sockaddr_in client_address;

    for (;;) {
        received_length = receive_packet_udp(socket_fd, &client_address, buffer, true);
        if (!validate_receive(received_length, sizeof(PPCB_CONN_packet),
                              false, PPCB_UDP, "receiving CONN")) {
            continue;
        }

        PPCB_CONN_packet data_received;
        memcpy(&data_received, buffer, sizeof(PPCB_CONN_packet));

        uint8_t packet_id = data_received.id;
        uint8_t protocol_id = data_received.protocol_id;
        uint64_t session_id = data_received.session_id;
        uint64_t byte_sequence_length = be64toh(data_received.byte_sequence_length);

        if (packet_id != PPCB_CONN || (protocol_id != PPCB_UDP && protocol_id != PPCB_UDPR) ||
            byte_sequence_length == 0) {

            error("invalid CONN");
            if (packet_id == PPCB_CONN) {
                server_sends_RESPONSE_udp(socket_fd, client_address, session_id, PPCB_UDP,
                                          PPCB_CONRJT);
            }

            continue;
        }

        if (protocol_id == PPCB_UDP) {
            handle_connection_udp(socket_fd, client_address, session_id,
                                  byte_sequence_length, buffer);
        } else {
            handle_connection_udpr(socket_fd, client_address, session_id,
                                   byte_sequence_length, buffer);
        }
    }
}


int main(int argc, char *argv[]) {
    if (argc != 3) {
        fatal("usage: %s <protocol> <port>", argv[0]);
    }

    char const *protocol_str = argv[1];
    PPCB_Protocol selected_protocol;

    if (strcmp(protocol_str, "tcp") == 0) {
        selected_protocol = PPCB_TCP;
    } else if (strcmp(protocol_str, "udp") == 0) {
        selected_protocol = PPCB_UDP;
    } else {
        fatal("inappropriate protocol: %s", protocol_str);
    }

    uint16_t protocol_type = (selected_protocol == PPCB_TCP) ? SOCK_STREAM : SOCK_DGRAM;
    uint16_t port = read_port(argv[2]);

    // Ignore SIGPIPE signals, so they are delivered as normal errors.
    signal(SIGPIPE, SIG_IGN);

    // Create a socket.
    int socket_fd = socket(AF_INET, protocol_type, 0);
    if (socket_fd < 0) {
        sys_fatal("cannot create a socket");
    }

    // Bind the socket to a concrete address.
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;                    // IPv4
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);     // Listening on all interfaces.
    server_address.sin_port = htons(port);

    if (bind(socket_fd, (struct sockaddr *) &server_address,
            (socklen_t) sizeof server_address) < 0) {
        sys_fatal("bind");
    }

    static char buffer[BUFFER_SIZE];

    if (selected_protocol == PPCB_TCP) {
        setup_tcp_server(socket_fd, server_address, buffer);
    } else {
        setup_udp_server(socket_fd, buffer);
    }

    close(socket_fd);
    return 0;
}
