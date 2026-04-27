#pragma once
#include <stdint.h>
#include "lib/kstd.h"

namespace net
{

    enum class TcpState
    {
        CLOSED,
        SYN_SENT,
        ESTABLISHED,
        FIN_WAIT,
        TIME_WAIT
    };

    struct __attribute__((packed)) TcpHeader
    {
        uint16_t src_port;
        uint16_t dest_port;
        uint32_t sequence_number;
        uint32_t ack_number;
        uint8_t reserved1 : 4;
        uint8_t data_offset : 4;
        uint8_t fin : 1;
        uint8_t syn : 1;
        uint8_t rst : 1;
        uint8_t psh : 1;
        uint8_t ack : 1;
        uint8_t urg : 1;
        uint8_t reserved2 : 2;
        uint16_t window_size;
        uint16_t checksum;
        uint16_t urgent_pointer;
    };

    void tcp_connect(uint32_t dest_ip, uint16_t dest_port);

    void tcp_send_data(const uint8_t *data, uint16_t length);

    void tcp_close();

    void tcp_receive(const uint8_t *segment, uint16_t length, uint32_t src_ip);

    TcpState tcp_state();

    // number of bytes in the rx.
    uint32_t tcp_rx_available();

    // read up to length bytes from the receive buffer, returns the bytes copied.
    uint32_t tcp_rx_read(uint8_t *buffer, uint32_t length);

    bool tcp_connection_closed();

}
