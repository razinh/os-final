#include "tcp.h"
#include "ip.h"
#include "arp.h"
#include "net_util.h"
#include "print.h"
#include "machine.h"

namespace net {

// ---- Receive buffer -------------------------------------------------------
static constexpr uint32_t RX_BUF_SIZE = 65536;
static uint8_t  rx_buf[RX_BUF_SIZE];
static uint32_t rx_head = 0;   // next write position (mod RX_BUF_SIZE)
static uint32_t rx_tail = 0;   // next read position  (mod RX_BUF_SIZE)
static bool     rx_closed = false;

// ---- Single connection state ----------------------------------------------
struct TcpConn {
    TcpState state       = TcpState::CLOSED;
    uint16_t local_port  = 0;
    uint16_t remote_port = 0;
    uint32_t remote_ip   = 0;  // host byte order
    uint32_t seq_num     = 0;
    uint32_t ack_num     = 0;
};
static TcpConn conn;

// ---- Flags bitmask --------------------------------------------------------
static constexpr uint8_t F_FIN = 0x01;
static constexpr uint8_t F_SYN = 0x02;
static constexpr uint8_t F_RST = 0x04;
static constexpr uint8_t F_PSH = 0x08;
static constexpr uint8_t F_ACK = 0x10;

// ---- TCP checksum (pseudo-header + segment) -------------------------------
static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                              const uint8_t* seg, uint16_t seg_len) {
    struct __attribute__((packed)) PseudoHdr {
        uint32_t src; uint32_t dst;
        uint8_t  zero; uint8_t proto; uint16_t len;
    };
    PseudoHdr ph;
    ph.src   = hton32(src_ip);
    ph.dst   = hton32(dst_ip);
    ph.zero  = 0;
    ph.proto = 6;
    ph.len   = hton16(seg_len);

    // Accumulate pseudo-header + segment
    uint32_t sum = 0;
    auto acc = [&](const uint8_t* p, size_t n) {
        while (n > 1) { sum += (uint32_t)((p[0] << 8) | p[1]); p += 2; n -= 2; }
        if (n & 1) sum += (uint32_t)(p[0] << 8);
    };
    acc(reinterpret_cast<const uint8_t*>(&ph), sizeof(ph));
    acc(seg, seg_len);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

// ---- Build and transmit a TCP segment -------------------------------------
static void send_segment(uint8_t flags,
                         const uint8_t* data, uint16_t data_len) {
    uint16_t seg_len = (uint16_t)(sizeof(TcpHeader) + data_len);
    uint8_t  buf[2048];

    auto* hdr            = reinterpret_cast<TcpHeader*>(buf);
    hdr->src_port        = hton16(conn.local_port);
    hdr->dest_port       = hton16(conn.remote_port);
    hdr->sequence_number = hton32(conn.seq_num);
    hdr->ack_number      = hton32(conn.ack_num);
    hdr->reserved1       = 0;
    hdr->data_offset     = 5;  // 20-byte header, no options
    hdr->fin             = (flags >> 0) & 1;
    hdr->syn             = (flags >> 1) & 1;
    hdr->rst             = (flags >> 2) & 1;
    hdr->psh             = (flags >> 3) & 1;
    hdr->ack             = (flags >> 4) & 1;
    hdr->urg             = 0;
    hdr->reserved2       = 0;
    hdr->window_size     = hton16(8192);
    hdr->urgent_pointer  = 0;
    hdr->checksum        = 0;

    if (data && data_len > 0) memcpy(buf + sizeof(TcpHeader), data, data_len);

    hdr->checksum = tcp_checksum(arp::MY_IP, conn.remote_ip, buf, seg_len);
    ip::send(conn.remote_ip, 6, buf, seg_len);
}

// ---- Public API -----------------------------------------------------------

void tcp_connect(uint32_t dest_ip, uint16_t dest_port) {
    conn.state       = TcpState::CLOSED;
    conn.remote_ip   = dest_ip;
    conn.remote_port = dest_port;
    conn.local_port  = 49152;
    conn.seq_num     = 1000;
    conn.ack_num     = 0;
    rx_head = rx_tail = 0;
    rx_closed = false;

    conn.state = TcpState::SYN_SENT;
    send_segment(F_SYN, nullptr, 0);
    conn.seq_num++;  // SYN consumes one sequence number
    KPRINT("[TCP] SYN sent\n");
}

void tcp_send_data(const uint8_t* data, uint16_t length) {
    if (conn.state != TcpState::ESTABLISHED) {
        KPRINT("[TCP] send_data: not connected\n");
        return;
    }
    send_segment(F_PSH | F_ACK, data, length);
    conn.seq_num += length;
}

void tcp_close() {
    if (conn.state == TcpState::ESTABLISHED) {
        conn.state = TcpState::FIN_WAIT;
        send_segment(F_FIN | F_ACK, nullptr, 0);
        conn.seq_num++;
        KPRINT("[TCP] FIN sent\n");
    }
}

void tcp_receive(const uint8_t* segment, uint16_t length, uint32_t src_ip) {
    if (length < sizeof(TcpHeader)) return;
    if (src_ip != conn.remote_ip) return;

    const auto* hdr = reinterpret_cast<const TcpHeader*>(segment);
    if (ntoh16(hdr->dest_port) != conn.local_port) return;

    uint16_t hdr_bytes = (uint16_t)(hdr->data_offset * 4);
    if (hdr_bytes < sizeof(TcpHeader) || hdr_bytes > length) return;

    const uint8_t* payload     = segment + hdr_bytes;
    uint16_t       payload_len = (uint16_t)(length - hdr_bytes);
    uint32_t       seq         = ntoh32(hdr->sequence_number);

    // --- SYN_SENT: waiting for SYN-ACK ---
    if (conn.state == TcpState::SYN_SENT) {
        if (hdr->syn && hdr->ack) {
            conn.ack_num = seq + 1;
            conn.seq_num = ntoh32(hdr->ack_number);
            conn.state   = TcpState::ESTABLISHED;
            send_segment(F_ACK, nullptr, 0);
            KPRINT("[TCP] ESTABLISHED\n");
        }
        return;
    }

    // --- ESTABLISHED: data transfer ---
    if (conn.state == TcpState::ESTABLISHED) {
        if (hdr->rst) {
            KPRINT("[TCP] RST received\n");
            conn.state = TcpState::CLOSED;
            rx_closed  = true;
            return;
        }

        if (payload_len > 0) {
            uint32_t space = RX_BUF_SIZE - (rx_head - rx_tail);
            uint32_t n     = (payload_len < space) ? payload_len : (uint16_t)space;
            for (uint32_t i = 0; i < n; i++)
                rx_buf[(rx_head + i) % RX_BUF_SIZE] = payload[i];
            rx_head += n;
        }

        conn.ack_num = seq + payload_len;
        // Always ACK (even zero-payload keepalives)
        if (payload_len > 0 || hdr->fin == 0)
            send_segment(F_ACK, nullptr, 0);

        if (hdr->fin) {
            conn.ack_num = seq + payload_len + 1;
            send_segment(F_ACK, nullptr, 0);
            conn.state = TcpState::TIME_WAIT;
            rx_closed  = true;
            KPRINT("[TCP] FIN received, closing\n");
        }
        return;
    }

    // --- FIN_WAIT: waiting for remote FIN ---
    if (conn.state == TcpState::FIN_WAIT) {
        if (hdr->fin) {
            conn.ack_num = seq + 1;
            send_segment(F_ACK, nullptr, 0);
            conn.state = TcpState::CLOSED;
            rx_closed  = true;
            KPRINT("[TCP] connection closed\n");
        }
    }
}

TcpState tcp_state()            { return conn.state; }
bool     tcp_connection_closed(){ return rx_closed;  }

uint32_t tcp_rx_available() {
    return rx_head - rx_tail;
}

uint32_t tcp_rx_read(uint8_t* buffer, uint32_t length) {
    uint32_t avail   = rx_head - rx_tail;
    uint32_t to_read = (length < avail) ? length : avail;
    for (uint32_t i = 0; i < to_read; i++)
        buffer[i] = rx_buf[(rx_tail + i) % RX_BUF_SIZE];
    rx_tail += to_read;
    return to_read;
}

} // namespace net
