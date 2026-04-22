#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <arpa/inet.h>
#include <signal.h>

#define RING_SIZE 256
#define PACKET_SIZE 2048

struct shared_nic_memory {
    struct {
        volatile uint32_t head;
        volatile uint32_t tail;
        uint8_t padding[56];
        uint8_t packets[RING_SIZE][PACKET_SIZE];
    } rx;

    struct {
        volatile uint32_t head;
        volatile uint32_t tail;
        uint8_t padding[56];
        uint8_t packets[RING_SIZE][PACKET_SIZE];
    } tx;
} __attribute__((packed));

static volatile int running = 1;

void signal_handler(int sig) {
    printf("Received signal %d, shutting down...\n", sig);
    running = 0;
}

int main(int argc, char* argv[]) {
    const char* shmem_path = "/tmp/nic_shmem";
    const char* interface = "tap0"; // TODO change to our interface

    if (argc > 1) {
        interface = argv[1];
    }

    printf("NIC Helper starting...\n");
    printf("Shared memory: %s\n", shmem_path);
    printf("Network interface: %s\n", interface);

    // Handle Ctrl+C for graceful shutdown
    signal(SIGINT, signal_handler);

    // Open shared memory
    int fd = open(shmem_path, O_RDWR);
    if (fd < 0) {
        perror("open");
        printf("Make sure QEMU created %s\n", shmem_path);
        return 1;
    }

    // Map shared memory
    struct shared_nic_memory* nic = mmap(NULL, sizeof(*nic), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (nic == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    printf("Shared memory mapped at %p\n", (void*)nic);

    // Create raw socket
    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    // Bind to network interface
    struct sockaddr_ll addr = {0};
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_ALL);
    addr.sll_ifindex = if_nametoindex(interface);

    if (addr.sll_ifindex == 0) {
        fprintf(stderr, "Interface %s not found\n", interface);
        fprintf(stderr, "Available interfaces:\n");
        system("ip link show | grep '^[0-9]' | cut -d: -f2");
        return 1;
    }

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    printf("Bound to interface %s (index %d)\n", interface, addr.sll_ifindex);
    printf("Polling...\n\n");

    uint64_t rx_total = 0;
    uint64_t tx_total = 0;

    while (running) {
        // RECEIVE: Network to Kernel
        uint8_t packet[PACKET_SIZE];
        ssize_t len = recv(sock, packet, PACKET_SIZE, MSG_DONTWAIT);

        if (len > 0) {
            uint32_t head = nic->rx.head;
            uint32_t tail = nic->rx.tail;

            if (head - tail < RING_SIZE) {
                uint32_t slot = head % RING_SIZE;
                nic->rx.packets[slot][0] = len & 0xFF;
                nic->rx.packets[slot][1] = (len >> 8) & 0xFF;
                // Copy packet data
                memcpy(nic->rx.packets[slot] + 2, packet, len);
                nic->rx.head++;
                rx_total++;
                printf("[RX] %zd bytes to kernel (total: %lu)\n", len, rx_total);
            }
        }

        // TRASMIT: Kernel to Network
        while (nic->tx.tail != nic->tx.head) {
            uint32_t tail = nic->tx.tail;
            uint32_t slot = tail % RING_SIZE;

            uint16_t length = nic->tx.packets[slot][0] | (nic->tx.packets[slot][1] << 8);
            if (length > PACKET_SIZE - 2) {
                fprintf(stderr, "[TX] Invalid packet length: %u\n", length);
                nic->tx.tail = tail + 1;
                continue;
            }

            ssize_t sent = send(sock, nic->tx.packets[slot] + 2, length, 0);

            if (sent < 0) {
                tx_total++;
                printf("[TX] %zd bytes to network (total: %lu)\n", sent, tx_total);
            } else if (sent < 0) {
                perror("send");
            }

            nic->tx.tail = tail + 1;
        }
        usleep(1000); // sleep 1ms
    }

    printf("\nStats:\n");
    printf("    RX: %lu packets\n", rx_total);
    printf("    TX: %lu packets\n", tx_total);
    munmap((void*)nic, sizeof(*nic));
    close(sock);
    close(fd);

    return 0;
}