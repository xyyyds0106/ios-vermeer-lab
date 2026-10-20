/* Bounded read-only primary GPT parser. UEFI 2.10 section 5.3:
 * https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html
 * Only 128-byte entries and <=8192 bytes of entry data are supported here.
 * Unsupported formats fail closed; this code never repairs a partition table.
 */
#ifndef VERMEER_GPT_H
#define VERMEER_GPT_H
#ifdef VERMEER_HOST_TEST
#include <stdint.h>
#else
#include <sys/types.h>
#endif

struct vermeer_gpt {
    uint64_t entries_lba, first_usable, last_usable, first, blocks;
    uint32_t entries_bytes, entries_crc;
};
static inline uint32_t vg_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline uint64_t vg_le64(const uint8_t *p)
{
    return vg_le32(p) | (uint64_t)vg_le32(p + 4) << 32;
}
static inline uint32_t vg_crc_byte(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (unsigned bit = 0; bit < 8; bit++)
        crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1)));
    return crc;
}
static inline uint32_t vg_crc(const uint8_t *p, unsigned bytes)
{
    uint32_t crc = ~0U;
    for (unsigned i = 0; i < bytes; i++) crc = vg_crc_byte(crc, p[i]);
    return ~crc;
}
static inline int vg_header(const uint8_t *h, uint64_t last_lba, struct vermeer_gpt *g)
{
    const char signature[] = "EFI PART";
    for (unsigned i = 0; i < 8; i++) if (h[i] != (uint8_t)signature[i]) return 1;
    unsigned size = vg_le32(h + 12);
    if (vg_le32(h + 8) != 0x10000 || size < 92 || size > 4096 || vg_le32(h + 20)) return 2;
    uint32_t crc = ~0U;
    for (unsigned i = 0; i < size; i++)
        crc = vg_crc_byte(crc, (i >= 16 && i < 20) ? 0 : h[i]);
    if (~crc != vg_le32(h + 16)) return 3;
    if (vg_le64(h + 24) != 1 || vg_le64(h + 32) != last_lba) return 4;
    g->first_usable = vg_le64(h + 40);
    g->last_usable = vg_le64(h + 48);
    g->entries_lba = vg_le64(h + 72);
    uint32_t n = vg_le32(h + 80), size_entry = vg_le32(h + 84);
    if (!n || n > 8192 / 128 || size_entry != 128) return 5;
    g->entries_bytes = n * 128;
    g->entries_crc = vg_le32(h + 88);
    uint64_t table_blocks = (g->entries_bytes + 4095U) / 4096;
    if (g->first_usable < 6 || g->first_usable > g->last_usable ||
        g->last_usable >= last_lba || g->entries_lba < 2 ||
        g->entries_lba >= g->first_usable ||
        table_blocks > g->first_usable - g->entries_lba ||
        table_blocks > last_lba - g->last_usable - 1) return 6;
    g->first = g->blocks = 0;
    return 0;
}
static inline int vg_userdata(const uint8_t *entries, struct vermeer_gpt *g)
{
    if (vg_crc(entries, g->entries_bytes) != g->entries_crc) return 7;
    unsigned matches = 0;
    for (unsigned off = 0; off < g->entries_bytes; off += 128) {
        const uint8_t *e = entries + off;
        unsigned used = 0;
        for (unsigned i = 0; i < 16; i++) used |= e[i];
        if (!used) continue;
        uint64_t first = vg_le64(e + 32), last = vg_le64(e + 40);
        if (first < g->first_usable || last > g->last_usable || first > last) return 8;
        /* All occupied partitions must be disjoint. n<=64 bounds this scan. */
        for (unsigned prev = 0; prev < off; prev += 128) {
            const uint8_t *p = entries + prev;
            unsigned occupied = 0;
            for (unsigned i = 0; i < 16; i++) occupied |= p[i];
            if (occupied && first <= vg_le64(p + 40) && vg_le64(p + 32) <= last) return 9;
        }
        const char name[] = "userdata";
        unsigned equal = 1;
        for (unsigned i = 0; i < sizeof(name); i++)
            if (e[56 + 2*i] != (uint8_t)name[i] || e[57 + 2*i]) equal = 0;
        if (equal) {
            if (++matches != 1) return 10;
            g->first = first;
            g->blocks = last - first + 1;
        }
    }
    return matches == 1 ? 0 : 11;
}
/* Negative or overflowing sector offsets must be rejected before multiplication. */
static inline int vg_io_bounds(int64_t sector, uint32_t secsize, uint32_t count,
                               uint64_t part_bytes, uint64_t *off)
{
    if (sector < 0 || !secsize || (uint64_t)sector > part_bytes / secsize) return 1;
    *off = (uint64_t)sector * secsize;
    return count > part_bytes - *off;
}
#endif
