#pragma once
#include <stdint.h>
#include "lib/kstd.h"

namespace net {

struct __attribute__((packed)) ArpPacket {
    uint16_t htype;   // 1 = Ethernet
    uint16_t ptype;   // 0x0800 = IPv4
    uint8_t  hlen;    // 6
    uint8_t  plen;    // 4
    uint16_t oper;    // 1 = request, 2 = reply
    uint8_t  sha[6];  // sender hardware address
    uint8_t  spa[4];  // sender protocol address (IP)
    uint8_t  tha[6];  // target hardware address
    uint8_t  tpa[4];  // target protocol address (IP)
};

namespace arp {

// Our IP address in host byte order (10.0.2.15 — QEMU guest default)
constexpr uint32_t MY_IP = 0x0A00020Fu;

void init();

// Send an ARP who-has request for target_ip (host byte order)
void send_request(uint32_t target_ip);

// Called by ethernet layer with raw ARP payload
void handle(const uint8_t* payload, size_t len);

// Non-blocking table lookup. Returns true and fills mac_out if known.
bool lookup(uint32_t ip, uint8_t mac_out[6]);

// Blocking: send request + poll NIC until reply or timeout.
bool resolve(uint32_t ip, uint8_t mac_out[6]);

} // namespace arp
} // namespace net
