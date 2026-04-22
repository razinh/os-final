#include "arp.h"
#include "ethernet.h"
#include "nic.h"
#include "net_util.h"
#include "print.h"
#include "lib/kstd.h"

namespace net {
namespace arp {

struct ArpEntry {
    uint32_t ip;       // host byte order
    uint8_t  mac[6];
    bool     valid;
};

static constexpr size_t TABLE_SIZE = 16;
static ArpEntry table[TABLE_SIZE];

static void table_insert(uint32_t ip, const uint8_t mac[6]) {
    for (size_t i = 0; i < TABLE_SIZE; i++) {
        if (table[i].valid && table[i].ip == ip) {
            memcpy(table[i].mac, mac, 6);
            return;
        }
    }
    for (size_t i = 0; i < TABLE_SIZE; i++) {
        if (!table[i].valid) {
            table[i] = { ip, {}, true };
            memcpy(table[i].mac, mac, 6);
            return;
        }
    }
    // Full: evict slot 0
    table[0] = { ip, {}, true };
    memcpy(table[0].mac, mac, 6);
}

void init() {
    for (size_t i = 0; i < TABLE_SIZE; i++) table[i].valid = false;
    ethernet::set_arp_handler(handle);
    KPRINT("[ARP] initialized (our IP: 10.0.2.15)\n");
}

void send_request(uint32_t target_ip) {
    ArpPacket pkt{};
    pkt.htype = hton16(1);
    pkt.ptype = hton16(0x0800);
    pkt.hlen  = 6;
    pkt.plen  = 4;
    pkt.oper  = hton16(1); // request

    memcpy(pkt.sha, NIC::MAC, 6);

    uint32_t my_ip_n = hton32(MY_IP);
    memcpy(pkt.spa, &my_ip_n, 4);

    memset(pkt.tha, 0, 6);
    uint32_t target_n = hton32(target_ip);
    memcpy(pkt.tpa, &target_n, 4);

    static constexpr uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    ethernet::send(bcast, ethernet::ETHERTYPE_ARP,
                   reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
}

void handle(const uint8_t* payload, size_t len) {
    if (len < sizeof(ArpPacket)) return;
    const auto* pkt = reinterpret_cast<const ArpPacket*>(payload);
    if (ntoh16(pkt->htype) != 1 || ntoh16(pkt->ptype) != 0x0800) return;

    uint32_t sender_ip_n;
    memcpy(&sender_ip_n, pkt->spa, 4);
    uint32_t sender_ip = ntoh32(sender_ip_n);

    // Learn the sender's MAC unconditionally
    table_insert(sender_ip, pkt->sha);

    // If this is a request targeting our IP, send a reply
    if (ntoh16(pkt->oper) == 1) {
        uint32_t target_ip_n;
        memcpy(&target_ip_n, pkt->tpa, 4);
        if (ntoh32(target_ip_n) == MY_IP) {
            ArpPacket reply{};
            reply.htype = hton16(1);
            reply.ptype = hton16(0x0800);
            reply.hlen  = 6;
            reply.plen  = 4;
            reply.oper  = hton16(2); // reply

            memcpy(reply.sha, NIC::MAC, 6);
            uint32_t my_ip_n = hton32(MY_IP);
            memcpy(reply.spa, &my_ip_n, 4);
            memcpy(reply.tha, pkt->sha, 6);
            memcpy(reply.tpa, pkt->spa, 4);

            ethernet::send(pkt->sha, ethernet::ETHERTYPE_ARP,
                           reinterpret_cast<const uint8_t*>(&reply), sizeof(reply));
        }
    }
}

bool lookup(uint32_t ip, uint8_t mac_out[6]) {
    for (size_t i = 0; i < TABLE_SIZE; i++) {
        if (table[i].valid && table[i].ip == ip) {
            memcpy(mac_out, table[i].mac, 6);
            return true;
        }
    }
    return false;
}

bool resolve(uint32_t ip, uint8_t mac_out[6]) {
    if (lookup(ip, mac_out)) return true;
    send_request(ip);
    for (int i = 0; i < 10000; i++) {
        NIC::poll();
        if (lookup(ip, mac_out)) return true;
    }
    KPRINT("[ARP] resolve timeout\n");
    return false;
}

} // namespace arp
} // namespace net

