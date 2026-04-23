// nic_helper.c — Userspace TCP proxy for the ivshmem kernel NIC.
//
// Reads Ethernet frames from the TX ring in /tmp/nic_shmem, processes them,
// and injects synthesized frames into the RX ring. Uses only POSIX TCP sockets
// — no raw sockets, no root privileges required.
//
// Handles:
//   ARP who-has 10.0.2.2  → inject ARP reply
//   TCP SYN               → open real POSIX socket to destination, inject SYN-ACK
//   TCP PSH (data)        → send via POSIX socket, read response, inject PSH+FIN
//   TCP FIN               → clean up socket, inject FIN-ACK

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>

// ---- Ring / packet constants (must match kernel nic.h) ----------------------

#define RING_SIZE   256
#define PACKET_SIZE 2048

struct shared_nic_memory {
    struct {
        volatile uint32_t head;   // helper writes (RX: network→kernel)
        volatile uint32_t tail;   // kernel reads
        uint8_t padding[56];
        uint8_t packets[RING_SIZE][PACKET_SIZE];
    } rx;
    struct {
        volatile uint32_t head;   // kernel writes (TX: kernel→network)
        volatile uint32_t tail;   // helper reads
        uint8_t padding[56];
        uint8_t packets[RING_SIZE][PACKET_SIZE];
    } tx;
} __attribute__((packed));

// ---- IP / MAC constants (must match kernel arp.h) ---------------------------

#define MY_IP      0x0A00020Fu   // 10.0.2.15 — guest
#define GATEWAY_IP 0x0A000202u   // 10.0.2.2  — gateway the kernel ARPs for

// Guest NIC MAC (must match kernel NIC::MAC)
static const uint8_t GUEST_MAC[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
// Fake gateway MAC returned in ARP replies
static const uint8_t GW_MAC[6]    = {0x52, 0x55, 0x0A, 0x00, 0x02, 0x02};

// ---- Frame layout -----------------------------------------------------------

#define ETH_LEN 14   // Ethernet header
#define IP_LEN  20   // IPv4 header (no options)
#define TCP_LEN 20   // TCP header (no options)

// ---- Globals ----------------------------------------------------------------

static volatile int running = 1;
static struct shared_nic_memory *nic;

static void sig_handler(int s) { (void)s; running = 0; }

// ---- Byte-order helpers (avoid endian.h portability issues) -----------------

static uint16_t bswap16(uint16_t x) {
    return (uint16_t)((x >> 8) | (x << 8));
}
static uint32_t bswap32(uint32_t x) {
    return ((x & 0xFF000000u) >> 24) | ((x & 0x00FF0000u) >> 8)
         | ((x & 0x0000FF00u) << 8)  | ((x & 0x000000FFu) << 24);
}

// ---- RX ring injection ------------------------------------------------------

static void inject_rx(const uint8_t *pkt, size_t len) {
    uint32_t head = nic->rx.head;
    if (head - nic->rx.tail >= RING_SIZE) {
        fprintf(stderr, "[RX] ring full, dropping packet\n");
        return;
    }
    uint32_t slot = head % RING_SIZE;
    nic->rx.packets[slot][0] = (uint8_t)(len & 0xFF);
    nic->rx.packets[slot][1] = (uint8_t)((len >> 8) & 0xFF);
    memcpy(nic->rx.packets[slot] + 2, pkt, len);
    __sync_synchronize();   // memory fence before advancing head
    nic->rx.head = head + 1;
}

// ---- Frame builders ---------------------------------------------------------

static void build_eth(uint8_t *frame, uint16_t ethertype,
                      const uint8_t dst[6], const uint8_t src[6]) {
    memcpy(frame,     dst, 6);
    memcpy(frame + 6, src, 6);
    frame[12] = (uint8_t)(ethertype >> 8);
    frame[13] = (uint8_t)(ethertype & 0xFF);
}

static void build_ip(uint8_t *ip, uint8_t proto,
                     uint32_t src_h, uint32_t dst_h, uint16_t payload_len) {
    uint16_t total = (uint16_t)(IP_LEN + payload_len);
    ip[0]  = 0x45;                         // version=4, IHL=5
    ip[1]  = 0;
    ip[2]  = (uint8_t)(total >> 8);
    ip[3]  = (uint8_t)(total & 0xFF);
    ip[4]  = ip[5] = 0;                    // ID
    ip[6]  = ip[7] = 0;                    // flags / fragment
    ip[8]  = 64;                            // TTL
    ip[9]  = proto;
    ip[10] = ip[11] = 0;                   // checksum (kernel doesn't validate)
    uint32_t s = bswap32(src_h), d = bswap32(dst_h);
    memcpy(ip + 12, &s, 4);
    memcpy(ip + 16, &d, 4);
}

// flags byte: FIN=bit0, SYN=bit1, RST=bit2, PSH=bit3, ACK=bit4
static void build_tcp(uint8_t *tcp, uint16_t sport, uint16_t dport,
                      uint32_t seq_h, uint32_t ack_h, uint8_t flags) {
    uint16_t sp = bswap16(sport), dp = bswap16(dport);
    uint32_t sq = bswap32(seq_h),  ak = bswap32(ack_h);
    memcpy(tcp,      &sp, 2);
    memcpy(tcp + 2,  &dp, 2);
    memcpy(tcp + 4,  &sq, 4);
    memcpy(tcp + 8,  &ak, 4);
    tcp[12] = (uint8_t)((TCP_LEN / 4) << 4);   // data offset = 5
    tcp[13] = flags;
    uint16_t win = bswap16(8192);
    memcpy(tcp + 14, &win, 2);
    tcp[16] = tcp[17] = tcp[18] = tcp[19] = 0; // checksum, urgent
}

static void inject_tcp(uint32_t src_ip, uint16_t src_port,
                       uint32_t dst_ip, uint16_t dst_port,
                       uint32_t seq_h,  uint32_t ack_h, uint8_t flags,
                       const uint8_t *payload, uint16_t plen) {
    uint8_t frame[2048];
    uint16_t tcp_payload = plen;
    build_eth(frame, 0x0800, GUEST_MAC, GW_MAC);
    build_ip(frame + ETH_LEN, 6, src_ip, dst_ip,
             (uint16_t)(TCP_LEN + tcp_payload));
    build_tcp(frame + ETH_LEN + IP_LEN,
              src_port, dst_port, seq_h, ack_h, flags);
    if (payload && plen)
        memcpy(frame + ETH_LEN + IP_LEN + TCP_LEN, payload, plen);
    inject_rx(frame, (size_t)(ETH_LEN + IP_LEN + TCP_LEN + plen));
}

// ---- ARP reply --------------------------------------------------------------

static void inject_arp_reply(void) {
    uint8_t frame[60];
    memset(frame, 0, sizeof(frame));

    // Ethernet header: gateway → guest
    memcpy(frame,     GUEST_MAC, 6);
    memcpy(frame + 6, GW_MAC,    6);
    frame[12] = 0x08; frame[13] = 0x06;  // ARP

    // ARP payload at offset 14 (matches struct ArpPacket layout)
    uint8_t *a = frame + 14;
    a[0] = 0; a[1] = 1;        // htype = Ethernet
    a[2] = 0x08; a[3] = 0x00;  // ptype = IPv4
    a[4] = 6; a[5] = 4;        // hlen, plen
    a[6] = 0; a[7] = 2;        // oper = reply

    memcpy(a + 8, GW_MAC, 6);                  // sha: sender MAC = GW_MAC
    uint32_t gw_n = bswap32(GATEWAY_IP);
    memcpy(a + 14, &gw_n, 4);                  // spa: sender IP = 10.0.2.2
    memcpy(a + 18, GUEST_MAC, 6);              // tha: target MAC = guest MAC
    uint32_t my_n = bswap32(MY_IP);
    memcpy(a + 24, &my_n, 4);                  // tpa: target IP = 10.0.2.15

    inject_rx(frame, 60);
    printf("[ARP] Replied: 10.0.2.2 is at GW_MAC\n");
    fflush(stdout);
}

// ---- TCP connection state ---------------------------------------------------

static int      conn_sock   = -1;
static int      conn_active = 0;
static uint32_t srv_ip      = 0;
static uint16_t srv_port    = 0;
static uint16_t cli_port    = 0;
static uint32_t cli_seq     = 0;   // last SEQ seen from client
static uint32_t srv_seq     = 10000; // our ISN for server side

// ---- Packet handler ---------------------------------------------------------

static void handle_pkt(const uint8_t *pkt, size_t len) {
    if (len < ETH_LEN) return;

    uint16_t et = (uint16_t)((pkt[12] << 8) | pkt[13]);

    // ---- ARP ----------------------------------------------------------------
    if (et == 0x0806) {
        if (len < (size_t)(ETH_LEN + 28)) return;
        const uint8_t *a = pkt + ETH_LEN;
        uint16_t oper = (uint16_t)((a[6] << 8) | a[7]);
        if (oper != 1) return;  // only handle ARP requests
        uint32_t tpa;
        memcpy(&tpa, a + 24, 4);
        if (bswap32(tpa) == GATEWAY_IP)
            inject_arp_reply();
        return;
    }

    // ---- IPv4 / TCP ---------------------------------------------------------
    if (et != 0x0800) return;
    if (len < (size_t)(ETH_LEN + IP_LEN)) return;

    const uint8_t *ip = pkt + ETH_LEN;
    if ((ip[0] >> 4) != 4) return;
    uint8_t ihl = (uint8_t)((ip[0] & 0xF) * 4);
    if (ihl < IP_LEN || len < (size_t)(ETH_LEN + ihl + TCP_LEN)) return;
    if (ip[9] != 6) return;  // TCP only

    uint32_t src_ip_n, dst_ip_n;
    memcpy(&src_ip_n, ip + 12, 4);
    memcpy(&dst_ip_n, ip + 16, 4);
    uint32_t src_ip = bswap32(src_ip_n);
    uint32_t dst_ip = bswap32(dst_ip_n);

    const uint8_t *tcp = pkt + ETH_LEN + ihl;
    uint16_t sport = (uint16_t)((tcp[0] << 8) | tcp[1]);
    uint16_t dport = (uint16_t)((tcp[2] << 8) | tcp[3]);
    uint32_t seq;
    memcpy(&seq, tcp + 4, 4);
    seq = bswap32(seq);

    uint8_t tcp_off = (uint8_t)((tcp[12] >> 4) * 4);
    uint8_t flags   = tcp[13];

    int is_syn = (flags & 0x02) && !(flags & 0x10);
    int is_psh = (flags & 0x08) != 0;
    int is_fin = (flags & 0x01) != 0;

    size_t data_start = (size_t)(ETH_LEN + ihl + tcp_off);
    uint16_t plen = (data_start < len) ? (uint16_t)(len - data_start) : 0;
    const uint8_t *payload = pkt + data_start;

    // ---- SYN: open real TCP connection --------------------------------------
    if (is_syn && !conn_active) {
        cli_port = sport;
        cli_seq  = seq;
        srv_ip   = dst_ip;
        srv_port = dport;

        if (conn_sock >= 0) { close(conn_sock); conn_sock = -1; }

        conn_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (conn_sock < 0) { perror("[TCP] socket"); return; }

        // 15-second timeout for both connect (via SO_SNDTIMEO) and recv
        struct timeval tv = {15, 0};
        setsockopt(conn_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(conn_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(dport);
        addr.sin_addr.s_addr = htonl(dst_ip);

        printf("[TCP] Connecting to %u.%u.%u.%u:%u ...\n",
            (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
            (dst_ip >> 8)  & 0xFF,  dst_ip & 0xFF, dport);
        fflush(stdout);

        if (connect(conn_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("[TCP] connect failed");
            close(conn_sock); conn_sock = -1;
            return;
        }
        printf("[TCP] Connected\n");

        conn_active = 1;
        srv_seq     = 10000;

        // Inject SYN-ACK back to the kernel
        inject_tcp(dst_ip, dport, src_ip, sport,
                   srv_seq, cli_seq + 1,
                   0x12,           // SYN + ACK
                   NULL, 0);
        srv_seq++;
        printf("[TCP] SYN-ACK injected\n");
        fflush(stdout);

    // ---- PSH: forward HTTP data, inject response ----------------------------
    } else if (conn_active && is_psh && plen > 0) {
        printf("[TCP] Forwarding %u bytes to real server\n", plen);
        fflush(stdout);

        ssize_t sent = send(conn_sock, payload, plen, 0);
        if (sent < 0) { perror("[TCP] send"); return; }

        cli_seq = seq + plen;

        // ACK the client's data segment
        inject_tcp(srv_ip, srv_port, src_ip, cli_port,
                   srv_seq, cli_seq,
                   0x10,           // ACK
                   NULL, 0);

        // Read the full server response (loop until connection closes)
        static uint8_t resp[16384];
        ssize_t total = 0;
        while (total < (ssize_t)(sizeof(resp) - 1)) {
            ssize_t n = recv(conn_sock, resp + total,
                             sizeof(resp) - (size_t)total - 1, 0);
            if (n <= 0) break;
            total += n;
        }
        printf("[TCP] Received %zd bytes from server\n", total);

        if (total > 0) {
            inject_tcp(srv_ip, srv_port, src_ip, cli_port,
                       srv_seq, cli_seq,
                       0x18,       // PSH + ACK
                       resp, (uint16_t)total);
            srv_seq += (uint32_t)total;
        }

        // FIN from server (signals end of response to kernel)
        inject_tcp(srv_ip, srv_port, src_ip, cli_port,
                   srv_seq, cli_seq,
                   0x11,           // FIN + ACK
                   NULL, 0);
        srv_seq++;
        printf("[TCP] FIN injected\n");
        fflush(stdout);

        close(conn_sock); conn_sock = -1;
        conn_active = 0;

    // ---- FIN: kernel closing connection ------------------------------------
    } else if (conn_active && is_fin) {
        cli_seq = seq + 1;
        inject_tcp(srv_ip, srv_port, src_ip, cli_port,
                   srv_seq, cli_seq,
                   0x11,           // FIN + ACK
                   NULL, 0);
        if (conn_sock >= 0) { close(conn_sock); conn_sock = -1; }
        conn_active = 0;
        printf("[TCP] Guest closed connection\n");
        fflush(stdout);
    }
    // Pure ACKs and other segments are ignored
}

// ---- Main -------------------------------------------------------------------

int main(void) {
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    int fd = open("/tmp/nic_shmem", O_RDWR);
    if (fd < 0) { perror("open /tmp/nic_shmem"); return 1; }

    nic = mmap(NULL, sizeof(*nic), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (nic == MAP_FAILED) { perror("mmap"); return 1; }
    close(fd);

    printf("[nic_helper] Ready. Bridging /tmp/nic_shmem to real network.\n");
    fflush(stdout);

    while (running) {
        while (nic->tx.tail != nic->tx.head) {
            uint32_t tail = nic->tx.tail;
            uint32_t slot = tail % RING_SIZE;
            uint16_t plen = (uint16_t)(nic->tx.packets[slot][0] |
                                       (nic->tx.packets[slot][1] << 8));
            if (plen > 0 && plen <= PACKET_SIZE - 2)
                handle_pkt(nic->tx.packets[slot] + 2, plen);
            __sync_synchronize();
            nic->tx.tail = tail + 1;
        }
        usleep(100);  // 100 µs poll interval
    }

    if (conn_sock >= 0) close(conn_sock);
    munmap((void *)nic, sizeof(*nic));
    printf("[nic_helper] Stopped.\n");
    return 0;
}
