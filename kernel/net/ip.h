#pragma once
#include <stdint.h>
#include "lib/kstd.h"
namespace net {

struct __attribute__((packed)) Ipv4Header {
    uint8_t ihl : 4;
    uint8_t version : 4;
    uint8_t tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t header_checksum;
    uint32_t src_ip; 
    uint32_t dest_ip;
};

namespace ip {

void init();

void send(uint32_t dest_ip, uint8_t protocol, const uint8_t* data, uint16_t length);

void handle(const uint8_t* payload, size_t len);

}
}
