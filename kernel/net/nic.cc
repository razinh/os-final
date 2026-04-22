#include "nic.h"
#include "spin_lock.h"
#include "lib/kstd.h"
#include "print.h"

namespace net {

// physical address where QEMU maps shared memory
#define SHARED_MEM_PHYS_ADDR 0xFEBD0000UL

// ring buffer constants
#define RING_SIZE 256
#define PACKET_SIZE 2048

// static members
volatile SharedNICMemory* NIC::mem_ = nullptr;
NIC::ReceiveCallback NIC::receive_callback_ = nullptr;
uint64_t NIC::tx_count_ = 0;
uint64_t NIC::rx_count_ = 0;
uint64_t NIC::tx_drops_ = 0;

// locks for multicore safety
static SpinLock tx_lock;
static SpinLock rx_lock;

void NIC::init() {
    // Map shared memory
    mem_ = reinterpret_cast<volatile SharedNICMemory*>(SHARED_MEM_PHYS_ADDR);
    
    // Initialize ring pointers
    mem_->rx.head = 0;
    mem_->rx.tail = 0;
    mem_->tx.head = 0;
    mem_->tx.tail = 0;
    
    KPRINT("[NIC] Initialized\n");
}

bool NIC::send(const uint8_t* data, size_t length) {
    if (!mem_) {
        KPRINT("[NIC] Error: NIC not initialized\n");
        return false;
    }

    if (length > PACKET_SIZE - 2) {
        KPRINT("[NIC] Error: Packet too large to send (size: ? bytes)\n", length);
        return false;
    }

    tx_lock.lock();

    // check if TX ring has space
    uint32_t head = mem_->tx.head;
    uint32_t tail = mem_->tx.tail;
    if (head - tail >= RING_SIZE) {
        // ring is full, drop packet
        KPRINT("[NIC] TX ring full (head=?, tail=?), dropping packet\n", Dec(head), Dec(tail));
        tx_drops_++;
        tx_lock.unlock();
        return false;
    }

    // get slot to write to
    uint32_t slot = head % RING_SIZE;

    // write length (first 2 bytes)
    mem_->tx.packets[slot][0] = length & 0xFF;
    mem_->tx.packets[slot][1] = (length >> 8) & 0xFF;

    // write packet data
    for (uint16_t i = 0; i < length; i++) {
        mem_->tx.packets[slot][i + 2] = data[i];
    }

    // memory barrier: ensure packet is written before updating head
    asm volatile("mfence" ::: "memory");

    // update head
    mem_->tx.head = head + 1;
    tx_count_++;
    tx_lock.unlock();
    return true;
}

void NIC::poll() {
    if (!mem_) return;
    rx_lock.lock();

    // process all available packets
    while (mem_->rx.tail != mem_->rx.head) {
        uint32_t tail = mem_->rx.tail;
        uint32_t slot = tail % RING_SIZE;

        // read length
        uint16_t length = mem_->rx.packets[slot][0] | (mem_->rx.packets[slot][1] << 8);
        if (length > PACKET_SIZE - 2) {
            KPRINT("[NIC] Error: Received packet with invalid length (? bytes)\n", length);
            mem_->rx.tail = tail + 1;
            continue;
        }

        // copy to local buffer
        uint8_t local_packet[PACKET_SIZE];
        for (uint16_t i = 0; i < length; i++) {
            local_packet[i] = mem_->rx.packets[slot][i + 2];
        }
        
        // advance tail before callback to allow new packets to be received while processing
        mem_->rx.tail = tail + 1;
        rx_count_++;

        // release lock while processing callback to allow concurrency
        rx_lock.unlock();

        // deliver to network stack
        if (receive_callback_) {
            receive_callback_(local_packet, length);
        }

        rx_lock.lock();
    }

    rx_lock.unlock();
}

void NIC::set_receive_callback(ReceiveCallback callback) {
    receive_callback_ = callback;
}

} // namespace net