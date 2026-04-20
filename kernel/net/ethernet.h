#pragma once

#include <stdint.h>

namespace net {

struct __attribute__((packed)) EthernetHeader {
    uint8_t  dest_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype; // 0x0800 for IPv4, 0x0806 for ARP
};

} // namespace net
