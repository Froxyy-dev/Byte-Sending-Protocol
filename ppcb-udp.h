#ifndef PPCB_UDP_H
#define PPCB_UDP_H

#include <inttypes.h>

void send_bytes_udp(
        int                   socket_fd,
        struct sockaddr_in    server_address,
        uint64_t              session_id,
        uint64_t              byte_sequence_length,
        char*                 byte_sequence
);

void handle_connection_udp(
        int                 socket_fd,
        struct sockaddr_in  client_address,
        uint64_t            session_id,
        uint64_t            byte_sequence_length,
        char                *buffer
);

#endif // PPCB_UDP_H
