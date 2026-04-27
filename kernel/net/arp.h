#pragma once
#include <stdint.h>
#include "lib/kstd.h"

namespace net {

struct __attribute__((packed)) ArpPacket {
    uint16_t htype;   // 1 = Ethernet
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;    // 1 = request, 2 = reply
    uint8_t sha[6];
    uint8_t spa[4];
    uint8_t tha[6];
    uint8_t tpa[4];
};

namespace arp {

constexpr uint32_t MY_IP = 0x0A00020Fu;
constexpr uint32_t MY_GATEWAY = 0x0A000202u;
constexpr uint32_t MY_NETMASK = 0xFFFFFF00u;

void init();

void send_request(uint32_t target_ip);

void handle(const uint8_t* payload, size_t len);

bool lookup(uint32_t ip, uint8_t mac_out[6]);

bool resolve(uint32_t ip, uint8_t mac_out[6]);

}
}
