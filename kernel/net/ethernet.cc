#include "ethernet.h"

#include "nic.h"
#include "print.h"
#include "lib/kstd.h"

namespace net {
namespace ethernet {

static ProtocolHandler ipv4_handler = nullptr;
static ProtocolHandler arp_handler = nullptr;

static uint16_t ntoh16(uint16_t x) {
    return static_cast<uint16_t>((x >> 8) | (x << 8));
}

static bool is_broadcast_mac(const uint8_t mac[ETHERNET_MAC_LEN]) {
    // FF:FF:FF:FF:FF:FF == broadcast address
    for (size_t i = 0; i < ETHERNET_MAC_LEN; i++) {
        if (mac[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

void init() {
    NIC::set_receive_callback(handle);
}

bool send(const uint8_t dest_mac[ETHERNET_MAC_LEN], uint16_t ethertype,
          const uint8_t* payload, size_t payload_len) {
    if (!dest_mac || (!payload && payload_len != 0)) {
        return false;
    }

    if (payload_len > (2048 - sizeof(EthernetHeader))) {
        KPRINT("[ETH] Payload too large (? bytes)\n", Dec(payload_len));
        return false;
    }

    uint8_t frame[2048];
    auto* header = reinterpret_cast<EthernetHeader*>(frame);

    memcpy(header->dest_mac, dest_mac, ETHERNET_MAC_LEN);
    memcpy(header->src_mac, NIC::MAC, ETHERNET_MAC_LEN);
    header->ethertype = ntoh16(ethertype);

    if (payload_len > 0) {
        memcpy(frame + sizeof(EthernetHeader), payload, payload_len);
    }

    return NIC::send(frame, sizeof(EthernetHeader) + payload_len);
}

void handle(const uint8_t* frame, size_t frame_len) {
    if (!frame || frame_len < sizeof(EthernetHeader))
        return;

    const auto* header = reinterpret_cast<const EthernetHeader*>(frame);
    const uint8_t* payload = frame + sizeof(EthernetHeader);
    const size_t payload_len = frame_len - sizeof(EthernetHeader);


    if (memcmp(header->dest_mac, NIC::MAC, ETHERNET_MAC_LEN) != 0 &&
        !is_broadcast_mac(header->dest_mac)) {
        return;
    }

    const uint16_t ethertype = ntoh16(header->ethertype);
    if(ethertype ==  0x0800) {
        if (ipv4_handler) {
            ipv4_handler(payload, payload_len);
        }
    } else if(ethertype == 0x0806) {
        if (arp_handler) {
            arp_handler(payload, payload_len);
        }
    }
}


void set_ipv4_handler(ProtocolHandler handler) {
    ipv4_handler = handler;
}

void set_arp_handler(ProtocolHandler handler) {
    arp_handler = handler;
}

}
}
