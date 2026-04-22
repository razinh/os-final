#pragma once

namespace net {

// Initialize the full networking stack:
//   NIC → Ethernet → ARP + IP → TCP
// Call once from kernel_main before any network use.
void net_init();

} // namespace net
