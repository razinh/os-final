#pragma once

#include <stdint.h>
#include "lib/kstd.h"

namespace net {

// Shared memory structure
struct SharedNICMemory {
    // RX ring (Network to Kernel)
    struct {
        volatile uint32_t head; // helper writes 
        volatile uint32_t tail; // kernel reads from here
        uint8_t padding[56]; // cache line padding
        uint8_t packets[256][2048];
    } rx;

    // TX ring (Kernel to Network)
    struct {
        volatile uint32_t head; // kernel writes 
        volatile uint32_t tail; // helper reads from here
        uint8_t padding[56]; // cache line padding
        uint8_t packets[256][2048];
    } tx;
} __attribute__((packed));

class NIC {
public:
    // initialize the NIC
    static void init();

    // send a packet
    static bool send(const uint8_t* data, size_t length);

    // poll for received packets
    static void poll();
    
    // set callback for received packets
    using ReceiveCallback = void (*)(const uint8_t* data, size_t length);
    static void set_receive_callback(ReceiveCallback callback);

    // stats
    static uint64_t tx_count() { return tx_count_; }
    static uint64_t rx_count() { return rx_count_; }
    static uint64_t tx_drops() { return tx_drops_; }

    // fake mac address
    static constexpr uint8_t MAC[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};

private:
    static volatile SharedNICMemory* mem_;
    static ReceiveCallback receive_callback_;
    static uint64_t tx_count_;
    static uint64_t rx_count_;
    static uint64_t tx_drops_;
};

} // namespace net