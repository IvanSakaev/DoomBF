// Assuming that windows is always little-endian

static uint16_t ibf_bswap16(uint16_t value) {
        return (uint16_t)((value >> 8) | (value << 8));
}

static uint32_t ibf_bswap32(uint32_t value) {
        return ((value & UINT32_C(0x000000ff)) << 24) |
               ((value & UINT32_C(0x0000ff00)) << 8)  |
               ((value & UINT32_C(0x00ff0000)) >> 8)  |
               ((value & UINT32_C(0xff000000)) >> 24);
}

#define htons ibf_bswap16
#define ntohs ibf_bswap16
#define htonl ibf_bswap32
#define ntohl ibf_bswap32
