#include "tcp.h"
#include <stdint.h>

namespace net {

// Placeholder for IP send function
void ip_send(uint32_t dest_ip, uint8_t protocol, uint8_t* data, uint16_t length) {
    // To be implemented
}

struct TcpConnection {
    TcpState state = TcpState::CLOSED;
    uint16_t local_port = 0;
    uint16_t remote_port = 0;
    uint32_t remote_ip = 0;
    uint32_t sequence_number = 0;
    uint32_t ack_number = 0;
};

// Simplified, single-connection TCP implementation
static TcpConnection current_connection;

uint16_t tcp_calculate_checksum() {
    return 0; // Placeholder
}

void tcp_connect(uint32_t dest_ip, uint16_t dest_port) {
    current_connection.state = TcpState::SYN_SENT;
    current_connection.remote_ip = dest_ip;
    current_connection.remote_port = dest_port;
    current_connection.local_port = 12345; // Arbitrary dynamic port
    current_connection.sequence_number = 1000; // Arbitrary starting sequence number
    current_connection.ack_number = 0;

    TcpHeader header{};
    header.src_port = current_connection.local_port;
    header.dest_port = current_connection.remote_port;
    header.sequence_number = current_connection.sequence_number;
    header.ack_number = 0;
    
    // TCP header length is 20 bytes by default. Data offset is measured in 32-bit words (20 / 4 = 5).
    header.data_offset = 5; 
    header.syn = 1;
    header.window_size = 8192; // Default window size
    header.checksum = tcp_calculate_checksum();

    // 6 is the standard IP protocol number for TCP
    ip_send(dest_ip, 6, reinterpret_cast<uint8_t*>(&header), sizeof(TcpHeader));
}

void tcp_send_data(const uint8_t* data, uint16_t length) {
    // TODO: Wrap the data in a TcpHeader with PSH and ACK flags set, 
    // update sequence numbers, and call ip_send() to transmit.
}

void tcp_close() {
    // TODO: Send a TcpHeader with the FIN flag set to 1, 
    // and change current_connection.state appropriately (e.g. FIN_WAIT).
}

void tcp_receive(uint8_t* packet, uint16_t length) {
    if (length < sizeof(TcpHeader)) {
        return; // Too small to be a valid TCP packet
    }

    TcpHeader* header = reinterpret_cast<TcpHeader*>(packet);

    if (current_connection.state == TcpState::SYN_SENT) {
        // Check for SYN-ACK
        if (header->syn == 1 && header->ack == 1) {
            // Read server's sequence number and set our ack_number to that + 1
            current_connection.ack_number = header->sequence_number + 1;
            
            // SYN consumes a sequence number, so we increment ours
            current_connection.sequence_number += 1;
            
            // Move to ESTABLISHED state
            current_connection.state = TcpState::ESTABLISHED;

            // Send a final ACK back
            TcpHeader ack_header{};
            ack_header.src_port = current_connection.local_port;
            ack_header.dest_port = current_connection.remote_port;
            ack_header.sequence_number = current_connection.sequence_number;
            ack_header.ack_number = current_connection.ack_number;
            ack_header.data_offset = 5;
            ack_header.ack = 1;
            ack_header.window_size = 8192;
            ack_header.checksum = tcp_calculate_checksum();

            ip_send(current_connection.remote_ip, 6, reinterpret_cast<uint8_t*>(&ack_header), sizeof(TcpHeader));
        }
    } else if (current_connection.state == TcpState::ESTABLISHED) {
        // TODO: Calculate the payload offset using header->data_offset.
        // Extract the raw HTTP text and place it in the socket receive buffer.
        // Update acknowledgment numbers and send an ACK back.
    }

    if (header->fin == 1) {
        // TODO: Send a final ACK matching this FIN sequence number, 
        // and change current_connection.state to CLOSED.
    }
}

} // namespace net
