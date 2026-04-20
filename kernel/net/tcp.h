#pragma once
#include <stdint.h>

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
    
    // Bitfields for flags and offsets
    uint8_t reserved1 : 4;
    uint8_t data_offset : 4; // Size of the TCP header in 32-bit words
    
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

} // namespace net
