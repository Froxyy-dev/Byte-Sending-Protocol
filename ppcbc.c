#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/random.h>
#include <signal.h>

#include "err.h"
#include "ppcb-common.h"
#include "ppcb-tcp.h"
#include "ppcb-udp.h"
#include "ppcb-udpr.h"

uint64_t read_byte_sequence(char **byte_sequence) {
    uint64_t byte_sequence_length = 0, current_size = SEQUENCE_SIZE;

    while (fgets(*byte_sequence + byte_sequence_length,current_size - byte_sequence_length,stdin)) {
        byte_sequence_length += strlen(*byte_sequence + byte_sequence_length);

        if (byte_sequence_length + 1 >= current_size) {
            current_size *= 2;

            *byte_sequence = realloc(*byte_sequence, current_size);
            ASSERT_MALLOC(*byte_sequence);
        }
    }

    // Check if error while reading from file.
    if (feof(stdin) == 0) {
        fatal("fgets");
    }

    *byte_sequence = realloc(*byte_sequence, byte_sequence_length + 1);
    ASSERT_MALLOC(*byte_sequence);

    return byte_sequence_length;
}


int main(int argc, char *argv[]) {
    if (argc != 4) {
        fatal("usage: %s <protocol> <host> <port>\n", argv[0]);
    }

    // Ignore SIGPIPE signals, so they are delivered as normal errors.
    signal(SIGPIPE, SIG_IGN);

    // Processing protocol type.
    char const *protocol_str = argv[1];
    PPCB_Protocol selected_protocol;
    if (strcmp(protocol_str, "tcp") == 0) {
        selected_protocol = PPCB_TCP;
    }
    else if (strcmp(protocol_str, "udp") == 0) {
        selected_protocol = PPCB_UDP;
    }
    else if (strcmp(protocol_str, "udpr") == 0) {
        selected_protocol = PPCB_UDPR;
    }
    else {
        fatal("inappropriate protocol: %s", protocol_str);
    }

    uint16_t protocol_type = (selected_protocol == PPCB_TCP) ? SOCK_STREAM : SOCK_DGRAM;

    // Process server address.
    char const *host = argv[2];
    uint16_t port = read_port(argv[3]);
    struct sockaddr_in server_address = get_server_address(host, port, selected_protocol);

    // Read byte sequence.
    char *byte_sequence = (char *)malloc(SEQUENCE_SIZE * sizeof(char));
    ASSERT_MALLOC(byte_sequence);
    uint64_t byte_sequence_length = read_byte_sequence(&byte_sequence);

    // Create a socket.
    int socket_fd = socket(AF_INET, protocol_type, 0);
    if (socket_fd < 0) {
        sys_fatal("cannot create a socket");
    }

    // Get random session id.
    uint64_t session_id;
    if (getrandom(&session_id, sizeof(uint64_t), GRND_NONBLOCK) == -1) {
        sys_fatal("cannot get random bytes");
    }

    // Communicate with a server.
    if (selected_protocol == PPCB_TCP) {
        send_bytes_tcp(socket_fd, server_address, session_id, byte_sequence_length, byte_sequence);
    }
    else if (selected_protocol == PPCB_UDP) {
        send_bytes_udp(socket_fd, server_address, session_id, byte_sequence_length, byte_sequence);
    }
    else {
        send_bytes_udpr(socket_fd, server_address, session_id, byte_sequence_length, byte_sequence);
    }

    // Free allocated memory and close descriptors.
    free(byte_sequence);
    close(socket_fd);

    return 0;
}