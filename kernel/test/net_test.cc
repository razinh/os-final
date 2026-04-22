// Integration test for ARP → IP → TCP.
// Injects crafted Ethernet frames via ethernet::handle() to simulate a remote
// server, and inspects outgoing frames captured by mock_nic.cc.

#include "kernel/net/ethernet.h"
#include "kernel/net/arp.h"
#include "kernel/net/ip.h"
#include "kernel/net/tcp.h"
#include "kernel/net/nic.h"
#include "kernel/net/net_util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

using namespace net;

// Capture state exposed by mock_nic.cc
namespace net {
    extern uint8_t  g_sent_frame[2048];
    extern size_t   g_sent_len;
    extern bool     g_sent;
}

// ---- Test infrastructure --------------------------------------------------
static int tests_run = 0, tests_passed = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return; \
    } \
} while (0)

#define PASS() do { tests_passed++; printf("  pass\n"); } while (0)

static void reset() { net::g_sent = false; net::g_sent_len = 0; }

// ---- Network constants ----------------------------------------------------
// Server we pretend to talk to: 10.0.2.2 (QEMU gateway)
static constexpr uint32_t SERVER_IP   = 0x0A000202u;
static constexpr uint16_t SERVER_PORT = 80;
static constexpr uint8_t  SERVER_MAC[6] = {0x52,0x55,0x0A,0x00,0x02,0x02};

// Server ISN used in injected SYN-ACK
static constexpr uint32_t SERVER_ISN = 5000;

// ---- Frame field accessors ------------------------------------------------
// (Assumes IHL=5, no IP options — true for all frames we send.)

static const EthernetHeader* eth_of(const uint8_t* f) {
    return reinterpret_cast<const EthernetHeader*>(f);
}
static const Ipv4Header* ip_of(const uint8_t* f) {
    return reinterpret_cast<const Ipv4Header*>(f + sizeof(EthernetHeader));
}
static const TcpHeader* tcp_of(const uint8_t* f) {
    return reinterpret_cast<const TcpHeader*>(
        f + sizeof(EthernetHeader) + sizeof(Ipv4Header));
}
static const uint8_t* tcp_payload_of(const uint8_t* f) {
    return f + sizeof(EthernetHeader) + sizeof(Ipv4Header) + sizeof(TcpHeader);
}
static size_t tcp_payload_len_of(const uint8_t* f, size_t frame_len) {
    return frame_len - sizeof(EthernetHeader) - sizeof(Ipv4Header) - sizeof(TcpHeader);
}

// ---- Frame builders (for injection) ---------------------------------------

// Build an Ethernet-wrapped ARP reply into buf[].
static size_t make_arp_reply(uint8_t* buf,
                              uint32_t sender_ip,
                              const uint8_t sender_mac[6]) {
    auto* eth = reinterpret_cast<EthernetHeader*>(buf);
    memcpy(eth->dest_mac, NIC::MAC, 6);
    memcpy(eth->src_mac, sender_mac, 6);
    eth->ethertype = hton16(ethernet::ETHERTYPE_ARP);

    auto* a = reinterpret_cast<ArpPacket*>(buf + sizeof(EthernetHeader));
    a->htype = hton16(1);
    a->ptype = hton16(0x0800);
    a->hlen  = 6;
    a->plen  = 4;
    a->oper  = hton16(2); // reply

    memcpy(a->sha, sender_mac, 6);
    uint32_t sip_n = hton32(sender_ip);
    memcpy(a->spa, &sip_n, 4);

    memcpy(a->tha, NIC::MAC, 6);
    uint32_t my_n = hton32(arp::MY_IP);
    memcpy(a->tpa, &my_n, 4);

    return sizeof(EthernetHeader) + sizeof(ArpPacket);
}

// Build an Ethernet-wrapped ARP request for our IP into buf[].
static size_t make_arp_request_for_us(uint8_t* buf,
                                       uint32_t sender_ip,
                                       const uint8_t sender_mac[6]) {
    static const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    auto* eth = reinterpret_cast<EthernetHeader*>(buf);
    memcpy(eth->dest_mac, bcast, 6);
    memcpy(eth->src_mac, sender_mac, 6);
    eth->ethertype = hton16(ethernet::ETHERTYPE_ARP);

    auto* a = reinterpret_cast<ArpPacket*>(buf + sizeof(EthernetHeader));
    a->htype = hton16(1);
    a->ptype = hton16(0x0800);
    a->hlen  = 6;
    a->plen  = 4;
    a->oper  = hton16(1); // request

    memcpy(a->sha, sender_mac, 6);
    uint32_t sip_n = hton32(sender_ip);
    memcpy(a->spa, &sip_n, 4);

    memset(a->tha, 0, 6);
    uint32_t my_n = hton32(arp::MY_IP);
    memcpy(a->tpa, &my_n, 4);

    return sizeof(EthernetHeader) + sizeof(ArpPacket);
}

// Build an Ethernet + IPv4 + TCP frame into buf[].
// tcp_flags: F_FIN=0x01, F_SYN=0x02, F_RST=0x04, F_PSH=0x08, F_ACK=0x10
static size_t make_tcp_frame(uint8_t* buf,
                              uint32_t src_ip, uint16_t src_port,
                              uint32_t dst_ip, uint16_t dst_port,
                              uint32_t seq, uint32_t ack_val,
                              uint8_t  tcp_flags,
                              const uint8_t* payload, uint16_t plen,
                              const uint8_t src_mac[6]) {
    // --- TCP header ---
    uint16_t seg_len = (uint16_t)(sizeof(TcpHeader) + plen);
    uint8_t* p = buf + sizeof(EthernetHeader) + sizeof(Ipv4Header);
    auto* tcp = reinterpret_cast<TcpHeader*>(p);
    tcp->src_port        = hton16(src_port);
    tcp->dest_port       = hton16(dst_port);
    tcp->sequence_number = hton32(seq);
    tcp->ack_number      = hton32(ack_val);
    tcp->reserved1       = 0;
    tcp->data_offset     = 5;
    tcp->fin = (tcp_flags >> 0) & 1;
    tcp->syn = (tcp_flags >> 1) & 1;
    tcp->rst = (tcp_flags >> 2) & 1;
    tcp->psh = (tcp_flags >> 3) & 1;
    tcp->ack = (tcp_flags >> 4) & 1;
    tcp->urg = 0;
    tcp->reserved2       = 0;
    tcp->window_size     = hton16(8192);
    tcp->checksum        = 0;  // not verified on receive
    tcp->urgent_pointer  = 0;
    if (payload && plen) memcpy(p + sizeof(TcpHeader), payload, plen);

    // --- IPv4 header ---
    uint16_t ip_total = (uint16_t)(sizeof(Ipv4Header) + seg_len);
    auto* ip = reinterpret_cast<Ipv4Header*>(buf + sizeof(EthernetHeader));
    ip->ihl             = 5;
    ip->version         = 4;
    ip->tos             = 0;
    ip->total_length    = hton16(ip_total);
    ip->identification  = hton16(1);
    ip->flags_fragment  = 0;
    ip->ttl             = 64;
    ip->protocol        = 6;
    ip->header_checksum = 0;
    ip->src_ip          = hton32(src_ip);
    ip->dest_ip         = hton32(dst_ip);

    // --- Ethernet header ---
    auto* eth = reinterpret_cast<EthernetHeader*>(buf);
    memcpy(eth->dest_mac, NIC::MAC, 6);
    memcpy(eth->src_mac, src_mac, 6);
    eth->ethertype = hton16(ethernet::ETHERTYPE_IPV4);

    return sizeof(EthernetHeader) + ip_total;
}

// Inject an ARP reply from SERVER so that arp::lookup(SERVER_IP) succeeds.
static void seed_arp_table() {
    uint8_t frame[256];
    size_t len = make_arp_reply(frame, SERVER_IP, SERVER_MAC);
    ethernet::handle(frame, len);
}

// ---- ARP tests ------------------------------------------------------------

static void test_arp_learn_from_reply() {
    printf("ARP: learn MAC from injected reply\n");
    tests_run++;

    seed_arp_table();

    uint8_t mac[6];
    CHECK(arp::lookup(SERVER_IP, mac));
    CHECK(memcmp(mac, SERVER_MAC, 6) == 0);
    PASS();
}

static void test_arp_send_request() {
    printf("ARP: send_request transmits valid ARP request frame\n");
    tests_run++;
    reset();

    arp::send_request(SERVER_IP);

    CHECK(net::g_sent);
    CHECK(net::g_sent_len >= sizeof(EthernetHeader) + sizeof(ArpPacket));

    // Dest MAC must be broadcast
    const auto* eth = eth_of(net::g_sent_frame);
    static const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    CHECK(memcmp(eth->dest_mac, bcast, 6) == 0);
    CHECK(ntoh16(eth->ethertype) == ethernet::ETHERTYPE_ARP);

    const auto* a = reinterpret_cast<const ArpPacket*>(
        net::g_sent_frame + sizeof(EthernetHeader));
    CHECK(ntoh16(a->oper) == 1);       // request
    CHECK(ntoh16(a->htype) == 1);      // Ethernet
    CHECK(ntoh16(a->ptype) == 0x0800); // IPv4

    // Sender is us
    CHECK(memcmp(a->sha, NIC::MAC, 6) == 0);
    uint32_t spa; memcpy(&spa, a->spa, 4);
    CHECK(ntoh32(spa) == arp::MY_IP);

    // Target IP is the server
    uint32_t tpa; memcpy(&tpa, a->tpa, 4);
    CHECK(ntoh32(tpa) == SERVER_IP);
    PASS();
}

static void test_arp_reply_to_request() {
    printf("ARP: reply sent when we receive a who-has for our IP\n");
    tests_run++;
    reset();

    uint8_t frame[256];
    size_t len = make_arp_request_for_us(frame, SERVER_IP, SERVER_MAC);
    ethernet::handle(frame, len);

    CHECK(net::g_sent);
    const auto* a = reinterpret_cast<const ArpPacket*>(
        net::g_sent_frame + sizeof(EthernetHeader));
    CHECK(ntoh16(a->oper) == 2);  // reply
    CHECK(memcmp(a->sha, NIC::MAC, 6) == 0);

    uint32_t spa; memcpy(&spa, a->spa, 4);
    CHECK(ntoh32(spa) == arp::MY_IP);  // we replied with our IP

    // Reply is addressed to the requester
    CHECK(memcmp(a->tha, SERVER_MAC, 6) == 0);
    PASS();
}

// ---- TCP tests ------------------------------------------------------------

static void test_tcp_syn_sent() {
    printf("TCP: connect() sends SYN with correct flags and ports\n");
    tests_run++;
    seed_arp_table();
    reset();

    tcp_connect(SERVER_IP, SERVER_PORT);

    CHECK(net::g_sent);
    CHECK(net::g_sent_len >= sizeof(EthernetHeader) + sizeof(Ipv4Header) + sizeof(TcpHeader));
    CHECK(tcp_state() == TcpState::SYN_SENT);

    const auto* tcp = tcp_of(net::g_sent_frame);
    CHECK(tcp->syn == 1);
    CHECK(tcp->ack == 0);
    CHECK(ntoh16(tcp->dest_port) == SERVER_PORT);
    PASS();
}

static void test_tcp_handshake_completes() {
    printf("TCP: SYN-ACK completes handshake, ACK is sent back\n");
    tests_run++;
    seed_arp_table();

    // Send SYN
    tcp_connect(SERVER_IP, SERVER_PORT);
    // Our seq after SYN = 1001 (1000 + 1 for SYN)
    const auto* syn = tcp_of(net::g_sent_frame);
    uint16_t our_port = ntoh16(syn->src_port);

    // Inject SYN-ACK: server seq=SERVER_ISN, ack=1001
    reset();
    uint8_t frame[256];
    size_t len = make_tcp_frame(frame,
        SERVER_IP, SERVER_PORT,
        arp::MY_IP, our_port,
        SERVER_ISN, 1001,
        0x12,  // SYN(0x02) | ACK(0x10)
        nullptr, 0, SERVER_MAC);
    ethernet::handle(frame, len);

    CHECK(tcp_state() == TcpState::ESTABLISHED);
    CHECK(net::g_sent);  // ACK must have been sent

    const auto* ack = tcp_of(net::g_sent_frame);
    CHECK(ack->ack == 1);
    CHECK(ack->syn == 0);
    CHECK(ntoh32(ack->ack_number) == SERVER_ISN + 1);
    PASS();
}

static void test_tcp_send_data() {
    printf("TCP: send_data() emits PSH+ACK with correct seq and payload\n");
    tests_run++;
    seed_arp_table();

    // Establish connection
    tcp_connect(SERVER_IP, SERVER_PORT);
    const auto* syn = tcp_of(net::g_sent_frame);
    uint16_t our_port = ntoh16(syn->src_port);

    uint8_t frame[256];
    size_t len = make_tcp_frame(frame,
        SERVER_IP, SERVER_PORT, arp::MY_IP, our_port,
        SERVER_ISN, 1001, 0x12, nullptr, 0, SERVER_MAC);
    ethernet::handle(frame, len);  // complete handshake

    // Now send HTTP request
    reset();
    const char* req = "GET / HTTP/1.0\r\n\r\n";
    uint16_t req_len = (uint16_t)strlen(req);
    tcp_send_data(reinterpret_cast<const uint8_t*>(req), req_len);

    CHECK(net::g_sent);
    const auto* seg = tcp_of(net::g_sent_frame);
    CHECK(seg->psh == 1);
    CHECK(seg->ack == 1);
    CHECK(ntoh32(seg->sequence_number) == 1001u);
    CHECK(ntoh32(seg->ack_number) == SERVER_ISN + 1);

    // Payload must match
    size_t plen = tcp_payload_len_of(net::g_sent_frame, net::g_sent_len);
    CHECK(plen == req_len);
    CHECK(memcmp(tcp_payload_of(net::g_sent_frame), req, req_len) == 0);
    PASS();
}

static void test_tcp_receive_data() {
    printf("TCP: received data lands in rx buffer and ACK is sent\n");
    tests_run++;
    seed_arp_table();

    // Establish connection
    tcp_connect(SERVER_IP, SERVER_PORT);
    const auto* syn = tcp_of(net::g_sent_frame);
    uint16_t our_port = ntoh16(syn->src_port);

    uint8_t hs[256];
    size_t hs_len = make_tcp_frame(hs,
        SERVER_IP, SERVER_PORT, arp::MY_IP, our_port,
        SERVER_ISN, 1001, 0x12, nullptr, 0, SERVER_MAC);
    ethernet::handle(hs, hs_len);

    // Inject HTTP response data from server
    const char* resp = "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nHello";
    uint16_t resp_len = (uint16_t)strlen(resp);

    reset();
    uint8_t data_frame[512];
    size_t df_len = make_tcp_frame(data_frame,
        SERVER_IP, SERVER_PORT, arp::MY_IP, our_port,
        SERVER_ISN + 1, 1001,
        0x18,  // PSH(0x08) | ACK(0x10)
        reinterpret_cast<const uint8_t*>(resp), resp_len,
        SERVER_MAC);
    ethernet::handle(data_frame, df_len);

    // Data should be in rx buffer
    CHECK(tcp_rx_available() == (uint32_t)resp_len);

    // ACK should have been sent
    CHECK(net::g_sent);
    const auto* ack = tcp_of(net::g_sent_frame);
    CHECK(ack->ack == 1);
    CHECK(ntoh32(ack->ack_number) == SERVER_ISN + 1 + resp_len);

    // Read and verify the data
    char buf[512] = {};
    uint32_t n = tcp_rx_read(reinterpret_cast<uint8_t*>(buf), sizeof(buf));
    CHECK(n == (uint32_t)resp_len);
    CHECK(memcmp(buf, resp, resp_len) == 0);
    PASS();
}

static void test_tcp_close() {
    printf("TCP: close() sends FIN, remote FIN completes shutdown\n");
    tests_run++;
    seed_arp_table();

    // Establish
    tcp_connect(SERVER_IP, SERVER_PORT);
    const auto* syn = tcp_of(net::g_sent_frame);
    uint16_t our_port = ntoh16(syn->src_port);

    uint8_t hs[256];
    size_t hs_len = make_tcp_frame(hs,
        SERVER_IP, SERVER_PORT, arp::MY_IP, our_port,
        SERVER_ISN, 1001, 0x12, nullptr, 0, SERVER_MAC);
    ethernet::handle(hs, hs_len);

    // Send our FIN
    reset();
    tcp_close();
    CHECK(net::g_sent);
    CHECK(tcp_state() == TcpState::FIN_WAIT);

    const auto* fin_seg = tcp_of(net::g_sent_frame);
    CHECK(fin_seg->fin == 1);
    CHECK(fin_seg->ack == 1);

    // Server sends its FIN
    reset();
    uint8_t fin_frame[256];
    size_t fin_len = make_tcp_frame(fin_frame,
        SERVER_IP, SERVER_PORT, arp::MY_IP, our_port,
        SERVER_ISN + 1, 1002,
        0x11,  // FIN(0x01) | ACK(0x10)
        nullptr, 0, SERVER_MAC);
    ethernet::handle(fin_frame, fin_len);

    CHECK(tcp_state() == TcpState::CLOSED);
    CHECK(tcp_connection_closed());

    // Final ACK for server's FIN
    CHECK(net::g_sent);
    const auto* final_ack = tcp_of(net::g_sent_frame);
    CHECK(final_ack->ack == 1);
    CHECK(final_ack->fin == 0);
    PASS();
}

// ---- main -----------------------------------------------------------------

int main() {
    printf("Network Stack Integration Tests\n");
    printf("================================\n");

    // Initialize the stack (same order as net_init())
    NIC::init();
    ethernet::init();
    arp::init();
    ip::init();

    printf("\n-- ARP --\n");
    test_arp_learn_from_reply();
    test_arp_send_request();
    test_arp_reply_to_request();

    printf("\n-- TCP --\n");
    test_tcp_syn_sent();
    test_tcp_handshake_completes();
    test_tcp_send_data();
    test_tcp_receive_data();
    test_tcp_close();

    printf("\n%d / %d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
