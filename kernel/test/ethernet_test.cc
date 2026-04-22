#include "kernel/net/ethernet.h"
#include "kernel/net/nic.h"
#include "lib/kstd.h"

using namespace net;
using namespace net::ethernet;

// Capture state exposed by mock_nic.cc
namespace net {
    extern uint8_t  g_sent_frame[2048];
    extern size_t   g_sent_len;
    extern bool     g_sent;
}

// ---- Test helpers ----

static int tests_run    = 0;
static int tests_passed = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s  (line %d)\n", #cond, __LINE__); \
        return; \
    } \
} while (0)

#define PASS() do { \
    tests_passed++; \
    printf("  pass\n"); \
} while (0)

// Byte-swap a 16-bit value (mirrors ethernet.cc's ntoh16)
static uint16_t swap16(uint16_t x) {
    return (uint16_t)((x >> 8) | (x << 8));
}

// Handler capture state
static bool     g_ipv4_called = false;
static bool     g_arp_called  = false;
static uint8_t  g_ipv4_payload[2048];
static size_t   g_ipv4_len    = 0;
static uint8_t  g_arp_payload[2048];
static size_t   g_arp_len     = 0;

static void ipv4_handler(const uint8_t* payload, size_t len) {
    g_ipv4_called = true;
    g_ipv4_len = len;
    memcpy(g_ipv4_payload, payload, len);
}

static void arp_handler(const uint8_t* payload, size_t len) {
    g_arp_called = true;
    g_arp_len = len;
    memcpy(g_arp_payload, payload, len);
}

static void reset() {
    net::g_sent     = false;
    net::g_sent_len = 0;
    g_ipv4_called   = false;
    g_arp_called    = false;
    g_ipv4_len      = 0;
    g_arp_len       = 0;
    set_ipv4_handler(nullptr);
    set_arp_handler(nullptr);
}

// Build a minimal Ethernet frame into buf[] addressed to dest_mac with the
// given ethertype and optional payload.  Returns total frame length.
static size_t make_frame(uint8_t* buf, const uint8_t dest[6],
                         uint16_t ethertype,
                         const uint8_t* payload, size_t plen) {
    auto* hdr = reinterpret_cast<EthernetHeader*>(buf);
    memcpy(hdr->dest_mac, dest, 6);
    memset(hdr->src_mac, 0x11, 6);
    hdr->ethertype = swap16(ethertype);
    if (payload && plen)
        memcpy(buf + sizeof(EthernetHeader), payload, plen);
    return sizeof(EthernetHeader) + plen;
}

// ---- Tests ----

static void test_send_builds_frame() {
    printf("send: correct header and payload\n");
    tests_run++;
    reset();

    const uint8_t dest[6]    = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    const uint8_t payload[]  = {0x01, 0x02, 0x03, 0x04};

    bool ok = send(dest, ETHERTYPE_IPV4, payload, sizeof(payload));

    CHECK(ok);
    CHECK(net::g_sent);
    CHECK(net::g_sent_len == sizeof(EthernetHeader) + sizeof(payload));

    const auto* hdr = reinterpret_cast<const EthernetHeader*>(net::g_sent_frame);
    CHECK(memcmp(hdr->dest_mac, dest, 6) == 0);
    CHECK(memcmp(hdr->src_mac, NIC::MAC, 6) == 0);
    // ethertype should be stored in network byte order
    CHECK(swap16(hdr->ethertype) == ETHERTYPE_IPV4);
    CHECK(memcmp(net::g_sent_frame + sizeof(EthernetHeader), payload, sizeof(payload)) == 0);
    PASS();
}

static void test_send_zero_length_payload() {
    printf("send: null payload with zero length\n");
    tests_run++;
    reset();

    const uint8_t dest[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    bool ok = send(dest, ETHERTYPE_ARP, nullptr, 0);

    CHECK(ok);
    CHECK(net::g_sent);
    CHECK(net::g_sent_len == sizeof(EthernetHeader));
    PASS();
}

static void test_send_null_dest_rejected() {
    printf("send: null dest_mac is rejected\n");
    tests_run++;
    reset();

    bool ok = send(nullptr, ETHERTYPE_IPV4, nullptr, 0);
    CHECK(!ok);
    CHECK(!net::g_sent);
    PASS();
}

static void test_send_null_payload_nonzero_len_rejected() {
    printf("send: null payload with non-zero length is rejected\n");
    tests_run++;
    reset();

    const uint8_t dest[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    bool ok = send(dest, ETHERTYPE_IPV4, nullptr, 10);
    CHECK(!ok);
    CHECK(!net::g_sent);
    PASS();
}

static void test_send_oversized_payload_rejected() {
    printf("send: oversized payload is rejected\n");
    tests_run++;
    reset();

    const uint8_t dest[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t big[2048] = {};
    bool ok = send(dest, ETHERTYPE_IPV4, big, sizeof(big));
    CHECK(!ok);
    CHECK(!net::g_sent);
    PASS();
}

static void test_handle_dispatches_ipv4() {
    printf("handle: IPv4 frame dispatched to IPv4 handler\n");
    tests_run++;
    reset();
    set_ipv4_handler(ipv4_handler);

    const uint8_t data[] = {0x45, 0x00, 0x00, 0x14};
    uint8_t frame[sizeof(EthernetHeader) + sizeof(data)];
    size_t len = make_frame(frame, NIC::MAC, ETHERTYPE_IPV4, data, sizeof(data));

    handle(frame, len);

    CHECK(g_ipv4_called);
    CHECK(!g_arp_called);
    CHECK(g_ipv4_len == sizeof(data));
    CHECK(memcmp(g_ipv4_payload, data, sizeof(data)) == 0);
    PASS();
}

static void test_handle_dispatches_arp() {
    printf("handle: ARP frame dispatched to ARP handler\n");
    tests_run++;
    reset();
    set_arp_handler(arp_handler);

    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t frame[sizeof(EthernetHeader) + sizeof(data)];
    size_t len = make_frame(frame, NIC::MAC, ETHERTYPE_ARP, data, sizeof(data));

    handle(frame, len);

    CHECK(g_arp_called);
    CHECK(!g_ipv4_called);
    CHECK(g_arp_len == sizeof(data));
    CHECK(memcmp(g_arp_payload, data, sizeof(data)) == 0);
    PASS();
}

static void test_handle_broadcast_accepted() {
    printf("handle: broadcast dest MAC accepted\n");
    tests_run++;
    reset();
    set_arp_handler(arp_handler);

    const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t data[]   = {0x01, 0x02};
    uint8_t frame[sizeof(EthernetHeader) + sizeof(data)];
    size_t len = make_frame(frame, bcast, ETHERTYPE_ARP, data, sizeof(data));

    handle(frame, len);

    CHECK(g_arp_called);
    PASS();
}

static void test_handle_foreign_mac_dropped() {
    printf("handle: frame for a different MAC is dropped\n");
    tests_run++;
    reset();
    set_ipv4_handler(ipv4_handler);
    set_arp_handler(arp_handler);

    const uint8_t other[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    uint8_t frame[sizeof(EthernetHeader) + 4];
    size_t len = make_frame(frame, other, ETHERTYPE_IPV4, nullptr, 0);
    // zero out the payload area
    memset(frame + sizeof(EthernetHeader), 0, 4);
    len = sizeof(EthernetHeader) + 4;

    handle(frame, len);

    CHECK(!g_ipv4_called);
    CHECK(!g_arp_called);
    PASS();
}

static void test_handle_truncated_frame_dropped() {
    printf("handle: frame shorter than EthernetHeader is dropped\n");
    tests_run++;
    reset();
    set_ipv4_handler(ipv4_handler);

    uint8_t frame[sizeof(EthernetHeader) - 1];
    memset(frame, 0, sizeof(frame));

    handle(frame, sizeof(frame));

    CHECK(!g_ipv4_called);
    PASS();
}

static void test_handle_null_frame_dropped() {
    printf("handle: null frame pointer is dropped\n");
    tests_run++;
    reset();
    set_ipv4_handler(ipv4_handler);

    handle(nullptr, 64);

    CHECK(!g_ipv4_called);
    PASS();
}

static void test_handle_unknown_ethertype_dropped() {
    printf("handle: unknown ethertype is silently dropped\n");
    tests_run++;
    reset();
    set_ipv4_handler(ipv4_handler);
    set_arp_handler(arp_handler);

    uint8_t frame[sizeof(EthernetHeader) + 4];
    size_t len = make_frame(frame, NIC::MAC, 0x86DD /* IPv6 */, nullptr, 0);
    memset(frame + sizeof(EthernetHeader), 0, 4);
    len = sizeof(EthernetHeader) + 4;

    handle(frame, len);

    CHECK(!g_ipv4_called);
    CHECK(!g_arp_called);
    PASS();
}

int main() {
    printf("Ethernet Layer Test Suite\n");
    printf("=========================\n");

    init(); // registers ethernet::handle as the NIC receive callback

    test_send_builds_frame();
    test_send_zero_length_payload();
    test_send_null_dest_rejected();
    test_send_null_payload_nonzero_len_rejected();
    test_send_oversized_payload_rejected();
    test_handle_dispatches_ipv4();
    test_handle_dispatches_arp();
    test_handle_broadcast_accepted();
    test_handle_foreign_mac_dropped();
    test_handle_truncated_frame_dropped();
    test_handle_null_frame_dropped();
    test_handle_unknown_ethertype_dropped();

    printf("\n%d / %d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
