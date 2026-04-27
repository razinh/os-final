#pragma once

namespace net
{
    // NIC → Ethernet → ARP + IP → TCP
    // Call once from kernel_main before any network use.
    void net_init();

}
