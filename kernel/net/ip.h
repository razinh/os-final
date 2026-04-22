#pragma once
#include <stdint.h>
#include "lib/kstd.h"
namespace net {

struct __attribute__((packed)) Ipv4Header {
    uint8_t  ihl : 4;        // Internet Header Length (in 32-bit words)
    uint8_t  version : 4;    // IPv4 = 4
    uint8_t  tos;
    uint16_t total_length;   // header + data, network byte order
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t  ttl;
    uint8_t  protocol;       // 6 = TCP, 17 = UDP
    uint16_t header_checksum;
    uint32_t src_ip;         // network byte order
    uint32_t dest_ip;        // network byte order
};

namespace ip {

// Register as ethernet's IPv4 handler. Call after ethernet::init().
void init();

// Send an IPv4 packet. dest_ip is in host byte order.
void send(uint32_t dest_ip, uint8_t protocol,
          const uint8_t* data, uint16_t length);

// Called by ethernet layer with raw IPv4 payload.
void handle(const uint8_t* payload, size_t len);

} // namespace ip
} // namespace net
