# Byte Sending Protocol

## Project Overview

This project implements the **Byte Sending Protocol** designed to transfer a sequence of bytes between a client and server over TCP or UDP connections. Additionally, for UDP, the protocol supports a retransmission mechanism. The client-server communication is packet-based, with each packet containing a payload of bytes (1 to 64,000 bytes). The protocol ensures packet ordering, error handling, and connection management, following strict timeouts and retransmission rules.

### Protocol Versions:
1. **TCP Version**: Ensures reliable byte transmission using the built-in guarantees of TCP.
2. **UDP Version**: Operates over UDP without retransmissions, offering faster, less reliable communication.
3. **UDP with Retransmissions**: Implements a custom mechanism for packet retransmissions when UDP is used, ensuring data reliability similar to TCP.

### Key Protocol Features:
- **Packet Sizes**: Byte packets range from 1 to 64,000 bytes.
- **Packet Types**: Several packet types control the communication: `CONN`, `CONACC`, `CONRJT`, `DATA`, `ACC`, `RJT`, and `RCVD`.
- **Timeout and Retransmissions**: Retransmission mechanism ensures reliable data transmission over UDP. If a packet is not acknowledged within a specified timeout (`MAX_WAIT`), it is resent. This continues up to `MAX_RETRANSMITS` times before terminating the connection.

## Communication Flow

1. **Connection Setup**:
   - The client sends a `CONN` packet to initiate the connection.
   - The server responds with either `CONACC` to accept the connection or `CONRJT` to reject it.
   - The client proceeds to send data only after receiving `CONACC`.

2. **Data Transmission**:
   - The client sends `DATA` packets containing the byte stream.
   - For UDP with retransmission, the server sends an `ACC` packet acknowledging the receipt of each `DATA` packet.
   - If the client does not receive an acknowledgment (`ACC`) within the `MAX_WAIT`, it resends the packet, retrying up to `MAX_RETRANSMITS` times.

3. **Connection Termination**:
   - When all data is transmitted, the server sends an `RCVD` packet to confirm receipt of the entire stream.
   - The client terminates the connection after receiving `RCVD`.

## Packet Format

Each packet consists of a header with fixed fields, followed by the payload (if applicable). The fields are transmitted in network byte order.

### Packet Types:

- **CONN**: Connection initiation (Client → Server)
  - Packet Type: 1
  - Session ID: 64 bits (random identifier)
  - Protocol ID: 8 bits (1 for TCP, 2 for UDP, 3 for UDP with retransmission)
  - Byte stream length: 64 bits (size of the data to be transmitted)

- **CONACC**: Connection accepted (Server → Client)
  - Packet Type: 2
  - Session ID: 64 bits

- **CONRJT**: Connection rejected (Server → Client)
  - Packet Type: 3
  - Session ID: 64 bits

- **DATA**: Data packet (Client → Server)
  - Packet Type: 4
  - Session ID: 64 bits
  - Packet Number: 64 bits (packet sequence number)
  - Data Length: 32 bits
  - Data: Variable-length payload

- **ACC**: Acknowledgment for data packet (Server → Client)
  - Packet Type: 5
  - Session ID: 64 bits
  - Packet Number: 64 bits

- **RJT**: Rejection of data packet (Server → Client)
  - Packet Type: 6
  - Session ID: 64 bits
  - Packet Number: 64 bits

- **RCVD**: Entire stream received (Server → Client)
  - Packet Type: 7
  - Session ID: 64 bits

## Client and Server Implementation

### Client:
- **Parameters**:
  - Protocol (`tcp`, `udp`, `udpr`)
  - Server address (IP or hostname)
  - Port number
- **Behavior**:
  - Reads the data to send from standard input.
  - Transmits data in `DATA` packets according to the protocol selected.
  - Implements retransmission (for `udpr`) when acknowledgments are not received within the timeout.
  - Terminates upon successful transmission or error.

### Server:
- **Parameters**:
  - Protocol (`tcp`, `udp`)
  - Port number
- **Behavior**:
  - Listens for incoming connections.
  - Processes incoming packets, checking session consistency and packet ordering.
  - Outputs received data to standard output once each packet is fully processed.
  - Only handles one connection at a time.
  
### Error Handling:
- Errors related to network issues or internal failures are reported to `stderr` with a prefix `ERROR:`. The program then exits or continues based on the error type.

## How to Build and Run

1. **Build**:
   - Run `make` in the root directory of the project. It will generate two binaries: `ppcbs` (server) and `ppcbc` (client).

2. **Run the Server**:
   ```bash
   ./bin/ppcbs [tcp|udp] <port>
   ```
   Example:
   ```bash
   ./bin/ppcbs tcp 8080
   ```

3. **Run the Client**:
   ```bash
   ./bin/ppcbc [tcp|udp|udpr] <server_address> <port> < <file>
   ```
   Example:
   ```bash
   ./bin/ppcbc tcp 127.0.0.1 8080 < sample.txt
   ```

4. **Testing**:
   - Connect two instances on different machines or virtual environments.
   - Send a sequence of bytes from the client to the server and verify the transmission is correct.

## Performance Testing

Performance of the protocol was evaluated under different network conditions, including varying:
- **Bandwidth**: Limit available network bandwidth and measure transmission speed.
- **Latency**: Introduce artificial delays and observe how retransmissions affect performance.
- **Packet Loss**: Simulate packet loss to test the reliability of the UDP retransmission mechanism.
- **File Size**: Test with different file sizes and packet lengths.

Results are documented, and the observations are plotted in graphs to show how different factors impact performance. These insights are included in a **report.pdf** file.

## Constants and Configuration

- `MAX_WAIT`: Maximum time to wait for a packet (in seconds).
- `MAX_RETRANSMITS`: Maximum number of retransmissions for UDP with retransmission.

These constants are declared in `protconst.h` and can be adjusted as needed.

## Conclusion

This project demonstrates an implementation of a custom byte stream transmission protocol using both TCP and UDP with and without retransmission. Through systematic testing and error handling, it ensures reliable and efficient data transmission under various network conditions.

--- 


