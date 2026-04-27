// Kernel socket API implementation.
// Wires socket_api.h down into the TCP layer.
// Single-connection: only one socket may be open at a time.

#include "socket_api.h"
#include "tcp.h"
#include "nic.h"

namespace {

// Converts a dotted-decimal IPv4 string into a host-byte-order uint32_t by accumulating each decimal octet and 
// packing it into the result from the most-significant byte downward.
static uint32_t parse_ip(const char* s) {
    uint32_t result = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t octet = 0;
        while (*s >= '0' && *s <= '9') { 
            octet = octet * 10 + (uint32_t)(*s - '0'); s++; 
        }
        result = (result << 8) | (octet & 0xFF);
        if (*s == '.') s++;
    }
    return result;
}

} // namespace

// Single-connection TCB implementation because the file descriptor is always 0 since only one TCP connection may 
// exist at a time, so no allocation is needed when creating the socket.
int socket_create(SocketType /*type*/) {
    return 0;
}

// Connecting has the hpst get parsed as a 32-bit IP address and the three-way TCP handshake is initiated with
// spinning because of the million polls until the NIC reaches ESTABLISHED or CLOSED, else its a timeout.
int socket_connect(int /*sockfd*/, const char* host, uint16_t port) {
    using namespace net;
    uint32_t ip = parse_ip(host);
    tcp_connect(ip, port);
    for (int i = 0; i < 1000000; i++) {
        NIC::poll();
        TcpState s = tcp_state();
        if (s == TcpState::ESTABLISHED) {
            return 0;
        }
        if (s == TcpState::CLOSED) {
            return -1;
        }
    }
    return -1;
}

// Sending has the data get passed into the TCP send pass, with a no-op check for zero-length rejection and
// payloads larger than 65535B since the length has to fix in a uint16_t, and the number of bytes queued is returned.
int socket_send(int /*sockfd*/, const void* data, size_t length) {
    if (length == 0) {
        return 0;
    }
    if (length > 0xFFFF) {
        return -1;
    }
    net::tcp_send_data(static_cast<const uint8_t*>(data), (uint16_t)length);
    return (int)length;
}

// Recieving has a polling loop block until the TCP recieve ring has any sort of data or the connection closes as an
// EOF, and then once the data is available, just read it into the buffer and return the count,
int socket_recv(int /*sockfd*/, void* buffer, size_t length) {
    using namespace net;
    while (tcp_rx_available() == 0) {
        if (tcp_connection_closed()) {
            return 0;
        }
        NIC::poll();
    }
    uint32_t n = tcp_rx_read(static_cast<uint8_t*>(buffer), (uint32_t)length);
    return (int)n;
}

// Closing the socket has the TCP close by draining the sending buffer and the four-way FIN handshake in the TCP layer
// is completed.
void socket_close(int /*sockfd*/) {
    net::tcp_close();
}

