#include "net_init.h"
#include "nic.h"
#include "ethernet.h"
#include "arp.h"
#include "ip.h"
#include "print.h"

namespace net
{
    void net_init()
    {
        NIC::init();
        ethernet::init();
        arp::init();
        ip::init();
        KPRINT("[NET] stack ready\n");
    }
}
