#pragma once
#include <stdint.h>
#include "lib/kstd.h"

namespace net {

enum class TcpState {
    CLOSED,
    SYN_SENT,
    ESTABLISHED,
    FIN_WAIT,
    TIME_WAIT
};

struct __attribute__((packed)) TcpHeader {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t sequence_number;
    uint32_t ack_number;

    // Byte 12: reserved (low nibble) | data_offset (high nibble)
    uint8_t reserved1   : 4;
    uint8_t data_offset : 4;  // header length in 32-bit words

    // Byte 13: flags (LSB = FIN)
    uint8_t fin : 1;
    uint8_t syn : 1;
    uint8_t rst : 1;
    uint8_t psh : 1;
    uint8_t ack : 1;
    uint8_t urg : 1;
    uint8_t reserved2 : 2;

    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_pointer;
};

// Start a TCP connection (sends SYN). dest_ip is host byte order.
void tcp_connect(uint32_t dest_ip, uint16_t dest_port);

// Send data over an established connection.
void tcp_send_data(const uint8_t* data, uint16_t length);

// Send FIN to initiate graceful close.
void tcp_close();

// Called by the IP layer when a TCP segment arrives.
void tcp_receive(const uint8_t* segment, uint16_t length, uint32_t src_ip);

// Current connection state.
TcpState tcp_state();

// Number of bytes waiting in the receive buffer.
uint32_t tcp_rx_available();

// Read up to 'length' bytes from the receive buffer. Returns bytes copied.
uint32_t tcp_rx_read(uint8_t* buffer, uint32_t length);

// True once the remote side has sent FIN or RST.
bool tcp_connection_closed();

} // namespace net
