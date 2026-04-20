// Mock NIC implementation for standalone ethernet layer tests.
// Replaces nic.cc so ethernet.cc can be compiled and tested outside the
// kernel build.  Kernel debug stubs (puts, putch, etc.) live in
// stub_kernel.cc to avoid the stdio.h / debug.h return-type conflict.

#include "kernel/net/nic.h"
#include <string.h>

namespace net {

// ---- Captured TX state (inspected by tests) ----
uint8_t  g_sent_frame[2048];
size_t   g_sent_len   = 0;
bool     g_sent       = false;

// ---- Static member definitions ----
volatile SharedNICMemory* NIC::mem_              = nullptr;
NIC::ReceiveCallback      NIC::receive_callback_ = nullptr;
uint64_t                  NIC::tx_count_         = 0;
uint64_t                  NIC::rx_count_         = 0;
uint64_t                  NIC::tx_drops_         = 0;

void NIC::init() {}

bool NIC::send(const uint8_t* data, size_t length) {
    g_sent = true;
    g_sent_len = length;
    memcpy(g_sent_frame, data, length);
    tx_count_++;
    return true;
}

void NIC::poll() {}

void NIC::set_receive_callback(ReceiveCallback cb) {
    receive_callback_ = cb;
}

} // namespace net
