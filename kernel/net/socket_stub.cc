// Kernel socket API implementation.
// Wires socket_api.h down into the TCP layer.
// Single-connection: only one socket may be open at a time.

#include "socket_api.h"
#include "tcp.h"
#include "nic.h"

namespace {

// Parse "a.b.c.d" into a host-byte-order uint32_t.
static uint32_t parse_ip(const char* s) {
    uint32_t result = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t octet = 0;
        while (*s >= '0' && *s <= '9') { octet = octet * 10 + (uint32_t)(*s - '0'); s++; }
        result = (result << 8) | (octet & 0xFF);
        if (*s == '.') s++;
    }
    return result;
}

} // namespace

int socket_create(SocketType /*type*/) {
    return 0;  // single connection, fd is always 0
}

int socket_connect(int /*sockfd*/, const char* host, uint16_t port) {
    using namespace net;
    uint32_t ip = parse_ip(host);
    tcp_connect(ip, port);

    // Poll NIC until ESTABLISHED or CLOSED (simple timeout)
    for (int i = 0; i < 1000000; i++) {
        NIC::poll();
        TcpState s = tcp_state();
        if (s == TcpState::ESTABLISHED) return 0;
        if (s == TcpState::CLOSED)      return -1;
    }
    return -1;  // timeout
}

int socket_send(int /*sockfd*/, const void* data, size_t length) {
    if (length == 0) return 0;
    if (length > 0xFFFF) return -1;
    net::tcp_send_data(static_cast<const uint8_t*>(data), (uint16_t)length);
    return (int)length;
}

int socket_recv(int /*sockfd*/, void* buffer, size_t length) {
    using namespace net;
    // Poll until data is available or the connection closes
    while (tcp_rx_available() == 0) {
        if (tcp_connection_closed()) return 0;  // EOF
        NIC::poll();
    }
    uint32_t n = tcp_rx_read(static_cast<uint8_t*>(buffer), (uint32_t)length);
    return (int)n;
}

void socket_close(int /*sockfd*/) {
    net::tcp_close();
}

