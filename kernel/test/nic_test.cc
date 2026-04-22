#include "net/nic.h"
#include "kernel/lib/kstd.h"

using namespace net;

// Callback for received packets
void on_receive(const uint8_t* data, size_t length) {
    printf("[TEST] Received %u bytes:\n", length);
    printf("  ");
    for (size_t i = 0; i < length && i < 64; i++) {
        printf("%02x ", data[i]);
        if ((i + 1) % 16 == 0) printf("\n  ");
    }
    printf("\n");
}

void test_nic() {
    printf("=== NIC Test ===\n\n");
    
    // Initialize
    NIC::init();
    NIC::set_receive_callback(on_receive);
    
    // Send a test packet (Ethernet broadcast)
    uint8_t test_packet[] = {
        // Destination MAC (broadcast)
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        // Source MAC
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        // EtherType (IPv4)
        0x08, 0x00,
        // Payload
        'H', 'e', 'l', 'l', 'o', '!',
    };
    
    printf("Sending test packet...\n");
    if (NIC::send(test_packet, sizeof(test_packet))) {
        printf("✓ Packet queued\n");
    } else {
        printf("✗ Send failed\n");
    }
    
    // Poll for received packets (simulate main loop)
    printf("\nPolling for packets (10 iterations)...\n");
    for (int i = 0; i < 10; i++) {
        NIC::poll();
        volatile int dummy = 0;
        for (int j = 0; j < 1000000; j++) {
            dummy = j; 
        }
        (void)dummy;
    }
    
    printf("\nStats:\n");
    printf("  TX: %lu packets, %lu drops\n", NIC::tx_count(), NIC::tx_drops());
    printf("  RX: %lu packets\n", NIC::rx_count());
    
    printf("\n✓ NIC test complete\n");
}

// If testing standalone
#ifndef KERNEL_BUILD
int main() {
    test_nic();
    return 0;
}
#endif