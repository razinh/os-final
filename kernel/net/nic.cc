#include "nic.h"
#include "spin_lock.h"
#include "lib/kstd.h"
#include "print.h"
#include "vmm.h"
#include "physmem.h"
#include "pcie.h"

namespace net {

// ring buffer constants
#define RING_SIZE 256
#define PACKET_SIZE 2048

// Scan PCI bus 0 for the ivshmem-plain device (vendor 0x1AF4, device 0x1110)
// and return the physical base address of its shared memory BAR.
// ivshmem-plain exposes the shmem at BAR 2 (QEMU >=5.x) or BAR 0 (older).
static uintptr_t find_ivshmem_phys() {
    for (uint8_t dev = 0; dev < 32; dev++) {
        uint32_t id = pci_config_read32(0, dev, 0, 0x00);
        if ((id & 0xFFFF) == 0xFFFF) continue;
        if ((id & 0xFFFF) != 0x1AF4 || (id >> 16) != 0x1110) continue;

        // Check BAR 2 first (modern QEMU), then BAR 0 (older QEMU)
        uint8_t bar_offsets[2] = {0x18, 0x10};
        for (int k = 0; k < 2; k++) {
            uint32_t bar = pci_config_read32(0, dev, 0, bar_offsets[k]);
            if (bar & 0x1) continue;           // I/O BAR, skip
            uintptr_t addr = (uintptr_t)(bar & ~0xFu);
            if ((bar >> 1) & 0x2) {            // 64-bit BAR: add high 32 bits
                uint32_t bar_hi = pci_config_read32(0, dev, 0, bar_offsets[k] + 4);
                addr |= ((uintptr_t)bar_hi << 32);
            }
            if (addr != 0) {
                KPRINT("[NIC] ivshmem BAR found at dev ? bar_off ?: addr ?\n",
                       Dec(dev), Dec(bar_offsets[k]), Dec(addr));
                return addr;
            }
        }
    }
    return 0;
}

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
    uintptr_t phys = find_ivshmem_phys();
    if (phys == 0) {
        KPRINT("[NIC] ERROR: ivshmem device not found on PCI bus 0\n");
        return;
    }

    // Map the ivshmem physical region into kernel virtual address space.
    // The kernel uses a Higher Half Direct Map (HHDM): VA = PA + Sys::hhdm_offset.
    // We call impl::map() for each page so the kernel page tables have valid
    // entries before we dereference mem_.
    const size_t    pages = (sizeof(SharedNICMemory) + FRAME_SIZE - 1) >> LOG_FRAME_SIZE;

    for (size_t i = 0; i < pages; i++) {
        PA pa(phys + i * FRAME_SIZE);
        VA va(pa); 
        impl::map(va.vpn(), pa.ppn(), false, true);
    }

    mem_ = reinterpret_cast<volatile SharedNICMemory*>(phys + Sys::hhdm_offset);

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