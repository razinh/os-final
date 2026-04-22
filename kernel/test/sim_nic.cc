// Simulated-server NIC for HTTP stack integration tests.
// NIC::poll() drives the TCP handshake automatically and injects HTTP responses,
// so HttpClient can be exercised against the real kernel network stack without
// a live network.

#include "kernel/net/nic.h"
#include "kernel/net/ethernet.h"
#include "kernel/net/arp.h"
#include "kernel/net/ip.h"
#include "kernel/net/tcp.h"
#include "kernel/net/net_util.h"
#include <string.h>

namespace net {

// ---- Captured TX state (readable by tests) ----
uint8_t  g_sent_frame[2048];
size_t   g_sent_len   = 0;
bool     g_sent       = false;

// ---- Static member definitions ----
volatile SharedNICMemory* NIC::mem_              = nullptr;
NIC::ReceiveCallback      NIC::receive_callback_ = nullptr;
uint64_t                  NIC::tx_count_         = 0;
uint64_t                  NIC::rx_count_         = 0;
uint64_t                  NIC::tx_drops_         = 0;

} // namespace net

// ---- Simulated server constants (must match what the test passes to HttpClient) ----
static constexpr uint32_t SIM_SERVER_IP   = 0x0A000202u;  // 10.0.2.2
static constexpr uint16_t SIM_SERVER_PORT = 80;
static constexpr uint8_t  SIM_SERVER_MAC[6] = {0x52,0x55,0x0A,0x00,0x02,0x02};
static constexpr uint32_t SIM_SERVER_ISN  = 5000;

// ---- Per-test simulation state ----
static bool        sim_saw_syn       = false;
static bool        sim_sent_response = false;
static uint16_t    sim_client_port   = 0;
static uint32_t    sim_client_isn    = 0;
static const char* sim_response      = nullptr;
static size_t      sim_response_len  = 0;

// ---- TCP frame builder ----
// flags: FIN=bit0 SYN=bit1 RST=bit2 PSH=bit3 ACK=bit4
static size_t build_tcp_frame(uint8_t* buf,
    uint32_t src_ip,  uint16_t src_port,
    uint32_t dst_ip,  uint16_t dst_port,
    uint32_t seq,     uint32_t ack_val,
    uint8_t  flags,
    const uint8_t* payload, uint16_t plen,
    const uint8_t  src_mac[6])
{
    using namespace net;
    static constexpr size_t ETH = sizeof(EthernetHeader);
    static constexpr size_t IP  = sizeof(Ipv4Header);
    static constexpr size_t TCP = sizeof(TcpHeader);

    auto* tcp = reinterpret_cast<TcpHeader*>(buf + ETH + IP);
    tcp->src_port        = hton16(src_port);
    tcp->dest_port       = hton16(dst_port);
    tcp->sequence_number = hton32(seq);
    tcp->ack_number      = hton32(ack_val);
    tcp->reserved1       = 0;
    tcp->data_offset     = 5;
    tcp->fin             = (flags >> 0) & 1;
    tcp->syn             = (flags >> 1) & 1;
    tcp->rst             = (flags >> 2) & 1;
    tcp->psh             = (flags >> 3) & 1;
    tcp->ack             = (flags >> 4) & 1;
    tcp->urg             = 0;
    tcp->reserved2       = 0;
    tcp->window_size     = hton16(8192);
    tcp->checksum        = 0;
    tcp->urgent_pointer  = 0;
    if (payload && plen)
        memcpy(buf + ETH + IP + TCP, payload, plen);

    uint16_t ip_total = (uint16_t)(IP + TCP + plen);
    auto* iph = reinterpret_cast<Ipv4Header*>(buf + ETH);
    iph->ihl             = 5;
    iph->version         = 4;
    iph->tos             = 0;
    iph->total_length    = hton16(ip_total);
    iph->identification  = hton16(1);
    iph->flags_fragment  = 0;
    iph->ttl             = 64;
    iph->protocol        = 6;
    iph->header_checksum = 0;
    iph->src_ip          = hton32(src_ip);
    iph->dest_ip         = hton32(dst_ip);

    auto* eth = reinterpret_cast<EthernetHeader*>(buf);
    memcpy(eth->dest_mac, NIC::MAC, 6);
    memcpy(eth->src_mac, src_mac, 6);
    eth->ethertype = hton16(ethernet::ETHERTYPE_IPV4);

    return ETH + ip_total;
}

// ---- Public reset: call before each test, supply the HTTP response to serve ----
void sim_nic_reset(const char* response, size_t len) {
    sim_response      = response;
    sim_response_len  = len;
    sim_saw_syn       = false;
    sim_sent_response = false;
    sim_client_port   = 0;
    sim_client_isn    = 0;
    net::g_sent       = false;
    net::g_sent_len   = 0;
}

// ---- NIC implementation ----
namespace net {

void NIC::init() {}

bool NIC::send(const uint8_t* data, size_t length) {
    size_t cap = (length < sizeof(g_sent_frame)) ? length : sizeof(g_sent_frame);
    memcpy(g_sent_frame, data, cap);
    g_sent_len = cap;
    g_sent     = true;
    tx_count_++;
    return true;
}

// Called in a tight loop by socket_connect() and socket_recv().
// Inspects the last transmitted frame and injects server-side TCP frames.
void NIC::poll() {
    if (!g_sent) return;

    static constexpr size_t ETH = sizeof(EthernetHeader);
    static constexpr size_t IP  = sizeof(Ipv4Header);
    static constexpr size_t TCP = sizeof(TcpHeader);

    const auto* tcp_hdr = reinterpret_cast<const TcpHeader*>(
        g_sent_frame + ETH + IP);

    if (!sim_saw_syn && tcp_hdr->syn && !tcp_hdr->ack) {
        // Client sent SYN — record ephemeral port and ISN, then inject SYN-ACK.
        sim_client_port = ntoh16(tcp_hdr->src_port);
        sim_client_isn  = ntoh32(tcp_hdr->sequence_number);
        g_sent = false;  // clear before injecting so the ACK we send back sets g_sent cleanly

        uint8_t frame[256];
        size_t len = build_tcp_frame(frame,
            SIM_SERVER_IP, SIM_SERVER_PORT,
            arp::MY_IP,    sim_client_port,
            SIM_SERVER_ISN, sim_client_isn + 1,
            0x12,  // SYN + ACK
            nullptr, 0, SIM_SERVER_MAC);
        ethernet::handle(frame, len);
        sim_saw_syn = true;

    } else if (sim_saw_syn && !sim_sent_response && tcp_hdr->psh) {
        // Client sent HTTP request data — inject HTTP response then FIN.
        size_t hdr_total  = ETH + IP + TCP;
        uint32_t req_len  = (g_sent_len > hdr_total)
                            ? (uint32_t)(g_sent_len - hdr_total) : 0;
        uint32_t ack_for_client = ntoh32(tcp_hdr->sequence_number) + req_len;
        g_sent = false;

        // Inject response data segment.
        uint8_t data_frame[4096];
        size_t df_len = build_tcp_frame(data_frame,
            SIM_SERVER_IP, SIM_SERVER_PORT,
            arp::MY_IP,    sim_client_port,
            SIM_SERVER_ISN + 1, ack_for_client,
            0x18,  // PSH + ACK
            reinterpret_cast<const uint8_t*>(sim_response),
            (uint16_t)sim_response_len,
            SIM_SERVER_MAC);
        ethernet::handle(data_frame, df_len);

        // Inject FIN to signal end of response.
        uint8_t fin_frame[128];
        size_t fl_len = build_tcp_frame(fin_frame,
            SIM_SERVER_IP, SIM_SERVER_PORT,
            arp::MY_IP,    sim_client_port,
            SIM_SERVER_ISN + 1 + (uint32_t)sim_response_len, ack_for_client,
            0x11,  // FIN + ACK
            nullptr, 0, SIM_SERVER_MAC);
        ethernet::handle(fin_frame, fl_len);

        sim_sent_response = true;
    }
    // Otherwise (pure ACK, FIN-ACK from our side, etc.) — ignore.
}

void NIC::set_receive_callback(ReceiveCallback cb) {
    receive_callback_ = cb;
}

} // namespace net
