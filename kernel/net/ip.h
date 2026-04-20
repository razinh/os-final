#pragma once
#include <stdint.h>

namespace net {

struct __attribute__((packed)) Ipv4Header {
    uint8_t  ihl : 4;        // Internet Header Length
    uint8_t  version : 4;    // IPv4 = 4
    uint8_t  tos;            // Type of Service
    uint16_t total_length;   // Length of header + data
    uint16_t identification; 
    uint16_t flags_fragment; // Flags and Fragment Offset
    uint8_t  ttl;            // Time to Live
    uint8_t  protocol;       // 6 for TCP, 17 for UDP
    uint16_t header_checksum;
    uint32_t src_ip;
    uint32_t dest_ip;
};

} // namespace net
