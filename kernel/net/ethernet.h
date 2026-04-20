#pragma once

#include <stdint.h>
#include <stddef.h>

namespace net {

struct __attribute__((packed)) EthernetHeader {
    uint8_t  dest_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype; // 0x0800 for IPv4, 0x0806 for ARP
};

namespace ethernet {

constexpr uint16_t ETHERTYPE_IPV4 = 0x0800;
constexpr uint16_t ETHERTYPE_ARP  = 0x0806;
constexpr size_t ETHERNET_MAC_LEN = 6;

using ProtocolHandler = void (*)(const uint8_t* payload, size_t length);

// initialize the ethernet module
void init();

// send an ethernet frame
bool send(const uint8_t dest_mac[ETHERNET_MAC_LEN], uint16_t ethertype,
          const uint8_t* payload, size_t payload_len);

// parse and send one received Ethernet frame.
void handle(const uint8_t* frame, size_t frame_len);

// register upper-layer handlers
void set_ipv4_handler(ProtocolHandler handler);
void set_arp_handler(ProtocolHandler handler);

} // namespace ethernet

} // namespace net
