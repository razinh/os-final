#include "ip.h"
#include "arp.h"
#include "ethernet.h"
#include "net_util.h"
#include "tcp.h"
#include "print.h"
#include "machine.h"

namespace net {
namespace ip {

static uint16_t ip_id = 1;

void init() {
    ethernet::set_ipv4_handler(handle);
    KPRINT("[IP] initialized\n");
}

void send(uint32_t dest_ip, uint8_t protocol,
          const uint8_t* data, uint16_t length) {
    uint8_t dest_mac[6];
    uint32_t arp_target = ((dest_ip & arp::MY_NETMASK) == (arp::MY_IP & arp::MY_NETMASK)) ? dest_ip : arp::MY_GATEWAY;
    if (!arp::resolve(arp_target, dest_mac)) {
        KPRINT("[IP] ARP resolve failed, dropping\n");
        return;
    }

    uint16_t total = (uint16_t)(sizeof(Ipv4Header) + length);
    uint8_t  pkt[2048];
    if (total > sizeof(pkt)) {
        KPRINT("[IP] packet too large\n");
        return;
    }

    auto* hdr = reinterpret_cast<Ipv4Header*>(pkt);
    hdr->version = 4;
    hdr->ihl = 5;
    hdr->tos = 0;
    hdr->total_length = hton16(total);
    hdr->identification = hton16(ip_id++);
    hdr->flags_fragment = 0;
    hdr->ttl = 64;
    hdr->protocol = protocol;
    hdr->header_checksum = 0;
    hdr->src_ip = hton32(arp::MY_IP);
    hdr->dest_ip = hton32(dest_ip);
    hdr->header_checksum = inet_checksum(hdr, sizeof(Ipv4Header));

    memcpy(pkt + sizeof(Ipv4Header), data, length);
    ethernet::send(dest_mac, ethernet::ETHERTYPE_IPV4, pkt, total);
}

void handle(const uint8_t* payload, size_t len) {
    if (len < sizeof(Ipv4Header)) 
        return;
    const auto* hdr = reinterpret_cast<const Ipv4Header*>(payload);
    if (hdr->version != 4) 
        return;

    uint16_t hdr_len = (uint16_t)(hdr->ihl * 4);
    if (hdr_len < sizeof(Ipv4Header) || hdr_len > len) 
        return;

    uint16_t total = ntoh16(hdr->total_length);
    if (total > len) 
        return;

    uint32_t src_ip = ntoh32(hdr->src_ip);
    const uint8_t* segment = payload + hdr_len;
    uint16_t seg_len = (uint16_t)(total - hdr_len);

    if (hdr->protocol == 6) {
        tcp_receive(segment, seg_len, src_ip);
    }
}

}
}