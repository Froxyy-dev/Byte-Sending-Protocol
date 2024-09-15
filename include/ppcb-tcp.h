#ifndef PPCB_TCP_H
#define PPCB_TCP_H


void send_bytes_tcp(
        int                   socket_fd,
        struct sockaddr_in    server_address,
        uint64_t              session_id,
        uint64_t              byte_sequence_length,
        char*                 byte_sequence
);

#define QUEUE_LENGTH  5

void handle_connection_tcp(
        int     client_fd,
        char    *buffer
);

#endif // PPCB_TCP_H
