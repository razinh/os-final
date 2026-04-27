#pragma once
#include <stdint.h>
#include "lib/kstd.h"

namespace net
{

    static inline uint16_t hton16(uint16_t x)
    {
        return (uint16_t)((x >> 8) | (x << 8));
    }

    static inline uint32_t hton32(uint32_t x)
    {
        return ((x & 0xFF000000u) >> 24) | ((x & 0x00FF0000u) >> 8) | ((x & 0x0000FF00u) << 8) | ((x & 0x000000FFu) << 24);
    }

    static inline uint16_t ntoh16(uint16_t x) { return hton16(x); }
    static inline uint32_t ntoh32(uint32_t x) { return hton32(x); }

    // Caller must zero any checksum field before calling.
    static inline uint16_t inet_checksum(const void *data, size_t len)
    {
        const uint8_t *p = static_cast<const uint8_t *>(data);
        uint32_t sum = 0;
        while (len > 1)
        {
            sum += (uint32_t)((p[0] << 8) | p[1]);
            p += 2;
            len -= 2;
        }
        if (len & 1)
            sum += (uint32_t)(p[0] << 8);
        while (sum >> 16)
            sum = (sum & 0xFFFF) + (sum >> 16);
        return (uint16_t)~sum;
    }

}
