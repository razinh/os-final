#include "net_init.h"
#include "nic.h"
#include "ethernet.h"
#include "arp.h"
#include "ip.h"
#include "print.h"

namespace net {

void net_init() {
    NIC::init();       // map shared memory, zero ring buffers
    ethernet::init();  // register NIC receive callback → ethernet::handle
    arp::init();       // register ethernet ARP handler → arp::handle
    ip::init();        // register ethernet IPv4 handler → ip::handle
    // TCP has no global init; state is reset per tcp_connect() call.
    KPRINT("[NET] stack ready\n");
}

} // namespace net
