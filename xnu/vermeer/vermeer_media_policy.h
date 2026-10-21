/* Integrated iOS test: writes can only target the prepared16GiB HFSX volume
 * inside this handset's independently inventoried userdata partition.
 * This file performs no I/O. The diagnostic profiles contain no write opcode.
 */
#ifndef VERMEER_MEDIA_POLICY_H
#define VERMEER_MEDIA_POLICY_H
#ifdef VERMEER_HOST_TEST
#include <stdint.h>
#else
#include <sys/types.h>
#endif
#define VI_FIRST 2570304ULL
#define VI_PART_BLOCKS 59272123ULL
#define VI_ROOT_BLOCKS 4194304ULL
static inline uint32_t vi_be32(volatile const uint8_t *p)
{ return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline int vi_uuid(const char *s, uint64_t *value)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < 16; i++) {
        unsigned c = (unsigned char)s[i], n;
        if (c >= '0' && c <= '9') n = c - '0';
        else if (c >= 'a' && c <= 'f') n = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') n = c - 'A' + 10;
        else return 1;
        v = (v << 4) | n;
    }
    if (s[16] || !v) return 1;
    *value = v; return 0;
}
static inline int vi_hfs_root(volatile const uint8_t *block, uint64_t uuid)
{
    volatile const uint8_t *h = block + 1024;
    uint64_t id = (uint64_t)vi_be32(h + 104) << 32 | vi_be32(h + 108);
    uint32_t journal = vi_be32(h + 12);
    return h[0] == 'H' && h[1] == 'X' && h[2] == 0 && h[3] == 5 &&
        (vi_be32(h + 4) & 0x2000) && vi_be32(h + 40) == 4096 &&
        vi_be32(h + 44) == VI_ROOT_BLOCKS && journal && journal < VI_ROOT_BLOCKS &&
        uuid && id == uuid;
}
static inline int vi_write_allowed(int enabled, uint64_t first, uint64_t blocks,
                                    uint64_t lba, unsigned count)
{
    return enabled == 1 && first == VI_FIRST && blocks == VI_PART_BLOCKS &&
        count && count <= 2 && lba >= first && lba - first < VI_ROOT_BLOCKS &&
        count <= VI_ROOT_BLOCKS - (lba - first);
}
#endif
