// Integration test: HttpClient GET and POST through the full kernel network stack.
//
// Uses sim_nic.cc as the NIC layer, which automatically simulates a TCP server:
// it injects SYN-ACK during connect and injects an HTTP response during recv.
// The real ethernet, ARP, IP, TCP, socket_stub, and http layers all run unchanged.

#include "kernel/net/http.h"
#include "kernel/net/ethernet.h"
#include "kernel/net/arp.h"
#include "kernel/net/ip.h"
#include "kernel/net/nic.h"
#include "kernel/net/net_util.h"
#include <stdio.h>
#include <string.h>

using namespace net;

// Declared in sim_nic.cc
extern void sim_nic_reset(const char* response, size_t len);

static int tests_run    = 0;
static int tests_passed = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return; \
    } \
} while (0)

#define PASS() do { tests_passed++; printf("  pass\n"); } while (0)

// Simulated server — must match the constants in sim_nic.cc
static const char*        SERVER_HOST = "10.0.2.2";
static constexpr uint16_t SERVER_PORT = 80;

// Pre-seed the ARP table so tcp_connect() doesn't need to ARP-resolve the server.
static void seed_arp_table() {
    static constexpr uint32_t SERVER_IP  = 0x0A000202u;
    static constexpr uint8_t  SERVER_MAC[6] = {0x52,0x55,0x0A,0x00,0x02,0x02};

    uint8_t frame[sizeof(EthernetHeader) + sizeof(ArpPacket)];

    auto* eth = reinterpret_cast<EthernetHeader*>(frame);
    memcpy(eth->dest_mac, NIC::MAC, 6);
    memcpy(eth->src_mac,  SERVER_MAC, 6);
    eth->ethertype = hton16(ethernet::ETHERTYPE_ARP);

    auto* a = reinterpret_cast<ArpPacket*>(frame + sizeof(EthernetHeader));
    a->htype = hton16(1);
    a->ptype = hton16(0x0800);
    a->hlen  = 6;
    a->plen  = 4;
    a->oper  = hton16(2);  // reply
    memcpy(a->sha, SERVER_MAC, 6);
    uint32_t sip = hton32(SERVER_IP);
    memcpy(a->spa, &sip, 4);
    memcpy(a->tha, NIC::MAC, 6);
    uint32_t my_n = hton32(arp::MY_IP);
    memcpy(a->tpa, &my_n, 4);

    ethernet::handle(frame, sizeof(frame));
}

// ---- Test: GET ----

static const char GET_RESPONSE[] =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "Hello, World!";

static void test_http_get() {
    printf("HTTP GET through full stack returns 200 OK\n");
    tests_run++;

    sim_nic_reset(GET_RESPONSE, sizeof(GET_RESPONSE) - 1);

    char buf[4096] = {};
    int len = HttpClient::get(SERVER_HOST, SERVER_PORT, "/",
                              buf, sizeof(buf) - 1);
    CHECK(len > 0);

    HttpResponse resp;
    CHECK(resp.parse(buf, (size_t)len));
    CHECK(resp.status_code() == 200);
    CHECK(resp.body_length() == 13);
    CHECK(memcmp(resp.body(), "Hello, World!", 13) == 0);
    PASS();
}

// ---- Test: POST ----

static const char POST_RESPONSE[] =
    "HTTP/1.0 201 Created\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 15\r\n"
    "\r\n"
    "{\"status\":\"ok\"}";

static void test_http_post() {
    printf("HTTP POST through full stack returns 201 Created\n");
    tests_run++;

    sim_nic_reset(POST_RESPONSE, sizeof(POST_RESPONSE) - 1);

    const char* body = "key=value";
    char buf[4096] = {};
    int len = HttpClient::post(SERVER_HOST, SERVER_PORT, "/submit",
                               body, strlen(body),
                               buf, sizeof(buf) - 1);
    CHECK(len > 0);

    HttpResponse resp;
    CHECK(resp.parse(buf, (size_t)len));
    CHECK(resp.status_code() == 201);
    CHECK(memcmp(resp.body(), "{\"status\":\"ok\"}", 15) == 0);
    PASS();
}

// ---- main ----

int main() {
    printf("HTTP Stack Integration Tests\n");
    printf("============================\n");

    NIC::init();
    ethernet::init();
    arp::init();
    ip::init();

    // Inject a fake ARP reply so the kernel knows the server's MAC.
    seed_arp_table();

    printf("\n-- GET --\n");
    test_http_get();

    printf("\n-- POST --\n");
    test_http_post();

    printf("\n%d / %d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
