#ifndef PPCB_COMMON_H
#define PPCB_COMMON_H

#include <inttypes.h>
#include <stddef.h>
#include <sys/types.h>
#include <stdbool.h>

#define MAX_PACKET_SIZE 64000
#define PACKET_SIZE 64000
#define SEQUENCE_SIZE 16
#define BUFFER_SIZE 64500

/// ENUMS ///

typedef enum {
    PPCB_TCP     = 1, 
    PPCB_UDP     = 2, 
    PPCB_UDPR    = 3
} PPCB_Protocol;

typedef enum {
    PPCB_CONN      = 1, 
    PPCB_CONACC    = 2, 
    PPCB_CONRJT    = 3,
    PPCB_DATA      = 4,
    PPCB_ACC       = 5,
    PPCB_RJT       = 6,
    PPCB_RCVD      = 7
} PPCB_Packet_id;

/// PACKET STRUCTS ///

typedef struct __attribute__((__packed__)) {
    uint8_t     id;
    uint64_t    session_id;
    uint8_t     protocol_id;
    uint64_t    byte_sequence_length;
} PPCB_CONN_packet;

typedef struct __attribute__((__packed__)) {
    uint8_t     id;
    uint64_t    session_id;
} PPCB_RESPONSE_packet;

typedef struct __attribute__((__packed__)) {
    uint8_t     id;
    uint64_t    session_id;
    uint64_t    packet_number;
    uint32_t    packet_byte_sequence_length;
} PPCB_DATA_packet;


typedef struct __attribute__((__packed__)) {
    uint8_t     id;
    uint64_t    session_id;
    uint64_t    packet_number;
} PPCB_PACKET_RESPONSE_packet;

/// PACKET FUNCTIONS ///

void set_CONN(
        PPCB_CONN_packet    *packet,
        uint64_t            session_id,
        uint8_t             protocol_id,
        uint64_t            byte_sequence_length
);

void set_RESPONSE(
        PPCB_RESPONSE_packet    *packet,
        uint8_t                 packet_id,
        uint64_t                session_id
);

void set_DATA(
        PPCB_DATA_packet    *packet,
        uint64_t            session_id,
        uint64_t            packet_number,
        uint32_t            packet_byte_sequence_length
);

void set_PACKET_RESPONSE(
        PPCB_PACKET_RESPONSE_packet     *packet,
        uint8_t                         packet_id,
        uint64_t                        session_id,
        uint64_t                        packet_number
);

/// SENDING UDP PACKETS ///

ssize_t send_packet_udp(
        int                 socket_fd,
        struct sockaddr_in  server_address,
        size_t              data_length,
        void                *buffer
);

ssize_t receive_packet_udp(
        int                   socket_fd,
        struct sockaddr_in    *receive_address,
        void                  *buffer,
        bool                  connection_initialize
);

void server_sends_RESPONSE_udp(
        int                 socket_fd,
        struct sockaddr_in  client_address,
        uint64_t            session_id,
        PPCB_Protocol       protocol,
        PPCB_Packet_id      sending
);

void server_sends_RJT_udp(
        int                 socket_fd,
        struct sockaddr_in  client_address,
        uint64_t            session_id,
        uint64_t            packet_number,
        PPCB_Protocol       protocol
);

/// COMPARE ADDRESS ///

bool different_addresses(
        struct sockaddr_in  lhs,
        struct sockaddr_in  rhs
);

/// VALIDATING FUNCTIONS ///

bool validate_data_packet(
        PPCB_DATA_packet    *packet,
        PPCB_Protocol       protocol,
        uint64_t            session_id,
        uint64_t            packet_number,
        uint64_t            bytes_received,
        uint64_t            byte_sequence_length
);

bool validate_send(
        ssize_t         sent_length,
        size_t          expected_length,
        bool            terminate,
        PPCB_Protocol   protocol,
        const char      *error_message
);

bool validate_receive(
        ssize_t         received_length,
        size_t          expected_length,
        bool            terminate,
        PPCB_Protocol   protocol,
        const char      *error_message
);

void validate_response_packet(
        PPCB_RESPONSE_packet    *packet,
        PPCB_Packet_id          expected_id,
        uint64_t                expected_session_id
);

/// FUNCTIONS FROM LABS ///

uint16_t read_port(
        char const    *string
);

struct sockaddr_in get_server_address(
        char const       *host,
        uint16_t         port,
        PPCB_Protocol    selected_protocol
);

ssize_t readn(
        int       fd,
        void      *vptr,
        size_t    n
);

ssize_t writen(
        int           fd,
        const void    *vptr,
        size_t        n
);


/// CUSTOM MIN FUNCTION ///
#define min(a, b) (((a) < (b)) ? (a) : (b))

#endif // PPCB_COMMON_H
