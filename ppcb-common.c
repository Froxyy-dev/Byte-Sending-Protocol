#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "ppcb-common.h"
#include "err.h"
#include "protconst.h"


/// PACKET FUNCTIONS ///

void set_CONN(
        PPCB_CONN_packet    *packet,
        uint64_t            session_id,
        uint8_t             protocol_id,
        uint64_t            byte_sequence_length
) {
    *packet = (PPCB_CONN_packet) {
            .id                             = PPCB_CONN,
            .session_id                     = session_id,
            .protocol_id                    = protocol_id,
            .byte_sequence_length           = htobe64(byte_sequence_length)
    };
}

void set_RESPONSE(
        PPCB_RESPONSE_packet    *packet,
        uint8_t                 packet_id,
        uint64_t                session_id
) {
    *packet = (PPCB_RESPONSE_packet) {
        .id                             = packet_id, 
        .session_id                     = session_id
    };
}

void set_DATA(
        PPCB_DATA_packet    *packet,
        uint64_t            session_id,
        uint64_t            packet_number,
        uint32_t            packet_byte_sequence_length
) {
    *packet = (PPCB_DATA_packet) {
        .id                             = PPCB_DATA, 
        .session_id                     = session_id, 
        .packet_number                  = htobe64(packet_number),
        .packet_byte_sequence_length    = htobe32(packet_byte_sequence_length)
    };
}

void set_PACKET_RESPONSE(
        PPCB_PACKET_RESPONSE_packet     *packet,
        uint8_t                         packet_id,
        uint64_t                        session_id,
        uint64_t                        packet_number
) {
    *packet = (PPCB_PACKET_RESPONSE_packet) {
        .id                             = packet_id, 
        .session_id                     = session_id, 
        .packet_number                  = htobe64(packet_number),
    };
}

/// SENDING UDP PACKETS ///

ssize_t send_packet_udp(
        int                 socket_fd,
        struct sockaddr_in  server_address,
        size_t              data_length,
        void                *buffer
) {
    int send_flags = 0;
    socklen_t address_length = (socklen_t) sizeof(server_address);
    return sendto(socket_fd, buffer, data_length, send_flags,
                                    (struct sockaddr *) &server_address, address_length);
}

ssize_t receive_packet_udp(
        int                   socket_fd,
        struct sockaddr_in    *receive_address,
        void                  *buffer,
        bool                  connection_initialize
) {
    // Set timeouts for the client socket.
    struct timeval to = {.tv_sec = (connection_initialize ? 0 : MAX_WAIT), .tv_usec = 0};
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);

    size_t max_length = BUFFER_SIZE;

    int receive_flags = 0;
    socklen_t address_length = (socklen_t) sizeof(*receive_address);
    ssize_t read_length = recvfrom(socket_fd, buffer, max_length, receive_flags,
                                       (struct sockaddr *) receive_address, &address_length);

    if (read_length < 0) {
        if (errno != EAGAIN) {
            return -1;
        }
        return 0;
    }
    return read_length;
}

void server_sends_RESPONSE_udp(
        int                 socket_fd,
        struct sockaddr_in  client_address,
        uint64_t            session_id,
        PPCB_Protocol       protocol,
        PPCB_Packet_id      sending
) {
    char *error_message = (sending == PPCB_CONRJT) ? "sending CONRJT" : "sending RCVD";
    PPCB_RESPONSE_packet data_response;
    set_RESPONSE(&data_response, sending, session_id);

    ssize_t sent_length = send_packet_udp(socket_fd, client_address,
                                          sizeof(PPCB_RESPONSE_packet), &data_response);
    validate_send(sent_length, sizeof(PPCB_RESPONSE_packet), false, protocol, error_message);
}


void server_sends_RJT_udp(
        int                 socket_fd,
        struct sockaddr_in  client_address,
        uint64_t            session_id,
        uint64_t            packet_number,
        PPCB_Protocol       protocol
) {
    PPCB_PACKET_RESPONSE_packet reject_packet;
    set_PACKET_RESPONSE(&reject_packet, PPCB_RJT, session_id, packet_number);
    ssize_t sent_length = send_packet_udp(socket_fd, client_address,
                                          sizeof(PPCB_PACKET_RESPONSE_packet),&reject_packet);
    validate_send(sent_length, sizeof(PPCB_PACKET_RESPONSE_packet), false, protocol, "sending RJT");
}

/// COMPARE ADDRESS ///

bool different_addresses(
        struct sockaddr_in  lhs,
        struct sockaddr_in  rhs
) {
    return (lhs.sin_addr.s_addr != rhs.sin_addr.s_addr) || (lhs.sin_port != rhs.sin_port);
}

/// VALIDATING FUNCTIONS ///

bool validate_data_packet(
        PPCB_DATA_packet    *packet,
        PPCB_Protocol       protocol,
        uint64_t            session_id,
        uint64_t            packet_number,
        uint64_t            bytes_received,
        uint64_t            byte_sequence_length
) {
    if (packet->session_id != session_id ||
        packet->packet_byte_sequence_length < 1 ||
        packet->packet_byte_sequence_length > MAX_PACKET_SIZE) {
        return false;
    }

    if (protocol == PPCB_UDPR) {
        if (packet->packet_number > packet_number) {
            return false;
        }
    }
    else if (packet->packet_number != packet_number ||
             packet->packet_byte_sequence_length > byte_sequence_length - bytes_received) {
        return false;
    }

    return true;
}

bool validate_send(
        ssize_t         sent_length,
        size_t          expected_length,
        bool            terminate,
        PPCB_Protocol   protocol,
        const char      *error_message
) {
    const char *sent_error = (protocol == PPCB_TCP) ? "writen" : "sendto";
    if (sent_length <= 0) {
        if (terminate) {
            sys_fatal(sent_error);
        }

        sys_error(sent_error);
        return false;
    }
    if ((size_t)sent_length != expected_length) {
        if (terminate) {
            fatal(error_message);
        }

        error(error_message);
        return false;
    }

    return true;
}

bool validate_receive(
        ssize_t         received_length,
        size_t          expected_length,
        bool            terminate,
        PPCB_Protocol   protocol,
        const char      *error_message
) {
    const char *sent_error = (protocol == PPCB_TCP) ? "readn" : "recvfrom";

    if (received_length < 0) {
        if (terminate) {
            sys_fatal(sent_error);
        }
        sys_error(sent_error);
        return false;
    }
    else if (received_length == 0) {
        if (terminate) {
            sys_fatal("timeout");
        }
        sys_error("timeout");
        return false;
    }
    else if ((size_t)received_length != expected_length) {
        if (terminate) {
            fatal(error_message);
        }

        error(error_message);
        return false;
    }

    return true;
}

void validate_response_packet(
        PPCB_RESPONSE_packet    *packet,
        PPCB_Packet_id          expected_id,
        uint64_t                expected_session_id
) {
    if (packet->id != expected_id) {
        fatal("incorrect packet id");
    }
    if (packet->session_id != expected_session_id) {
        fatal("incorrect session id");
    }
}

/// FUNCTIONS FROM LABS ///

uint16_t read_port(
        char const    *string
) {
    char *endptr;
    errno = 0;
    unsigned long port = strtoul(string, &endptr, 10);
    if (errno != 0 || *endptr != 0 || port == 0 || port > UINT16_MAX) {
        fatal("%s is not a valid port number", string);
    }
    return (uint16_t) port;
}

struct sockaddr_in get_server_address(
        char const       *host,
        uint16_t         port,
        PPCB_Protocol    selected_protocol
) {
    bool selected_tcp = (selected_protocol == PPCB_TCP);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = selected_tcp ? SOCK_STREAM : SOCK_DGRAM;
    hints.ai_protocol = selected_tcp ? IPPROTO_TCP : IPPROTO_UDP;

    struct addrinfo *address_result;
    int errcode = getaddrinfo(host, NULL, &hints, &address_result);
    if (errcode != 0) {
        fatal("getaddrinfo: %s", gai_strerror(errcode));
    }

    struct sockaddr_in send_address;
    send_address.sin_family = AF_INET;   // IPv4
    send_address.sin_addr.s_addr =       // IP address
            ((struct sockaddr_in *) (address_result->ai_addr))->sin_addr.s_addr;
    send_address.sin_port = htons(port); // port from the command line

    freeaddrinfo(address_result);

    return send_address;
}

// Following two functions come from Stevens' "UNIX Network Programming" book.

// Read n bytes from a descriptor. Use in place of read() when fd is a stream socket.
ssize_t readn(
        int       fd,
        void      *vptr,
        size_t    n
) {
    ssize_t nleft, nread;
    char *ptr;

    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
        if ((nread = read(fd, ptr, nleft)) < 0)
            return nread;     // When error, return < 0.
        else if (nread == 0)
            break;            // EOF

        nleft -= nread;
        ptr += nread;
    }
    return n - nleft;         // return >= 0
}

// Write n bytes to a descriptor.
ssize_t writen(
        int           fd,
        const void    *vptr,
        size_t        n
) {
    ssize_t nleft, nwritten;
    const char *ptr;

    ptr = vptr;               // Can't do pointer arithmetic on void*.
    nleft = n;
    while (nleft > 0) {
        if ((nwritten = write(fd, ptr, nleft)) <= 0)
            return nwritten;  // error

        nleft -= nwritten;
        ptr += nwritten;
    }
    return n;
}
