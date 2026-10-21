/* Bounded FDT guard for one sparse high-RAM test. No allocation or I/O.
 * Supports the handset's 2-address-cell/2-size-cell flat memory layout.
 * Unknown cell sizes, malformed input and overlapping fixed reservations fail.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define HIGH_RAM_LO UINT64_C(0x8c0000000)
#define HIGH_RAM_HI UINT64_C(0x940000000)

static uint32_t rg_be32(const uint8_t *p)
{ return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t rg_be64(const uint8_t *p)
{ return (uint64_t)rg_be32(p) << 32 | rg_be32(p + 4); }
static int rg_overlap(uint64_t a, uint64_t n, uint64_t lo, uint64_t hi)
{ return n && (n > UINT64_MAX - a || (a < hi && a + n > lo)); }
static int rg_name(const uint8_t *p, size_t n, const char *s)
{
    size_t i = 0;
    while (s[i] && i < n && p[i] == (uint8_t)s[i]) i++;
    return !s[i] && i == n;
}
static int rg_memory_name(const uint8_t *p, size_t n)
{
    return (n == 6 || (n > 7 && p[6] == '@')) && rg_name(p, 6, "memory");
}
/* Return zero only after the complete tree and reserve map are checked.
 * available is a hard mapping limit, not a trusted header length.
 */
static int ram_guard(const uint8_t *f, size_t available, uint64_t lo, uint64_t hi)
{
    if (available < 40 || rg_be32(f) != 0xd00dfeed || lo >= hi) return 1;
    uint32_t total = rg_be32(f + 4), so = rg_be32(f + 8), strings = rg_be32(f + 12);
    uint32_t reserve = rg_be32(f + 16), sn = rg_be32(f + 32), sz = rg_be32(f + 36);
    if (rg_be32(f + 20) < 17 || rg_be32(f + 24) > 17 || total > available ||
        total < 40 || so < 40 || strings < 40 || reserve < 40 ||
        (so & 3) || (reserve & 7) || so > total || sz > total - so ||
        strings > total || sn > total - strings || reserve > total - 16) return 2;
    /* The reserve map must terminate before another section begins. */
    size_t rlimit = total;
    if (so > reserve && so < rlimit) rlimit = so;
    if (strings > reserve && strings < rlimit) rlimit = strings;
    size_t r = reserve;
    for (;;) {
        if (r > rlimit || rlimit - r < 16) return 3;
        uint64_t base = rg_be64(f + r), size = rg_be64(f + r + 8);
        r += 16;
        if (!base && !size) break;
        if (rg_overlap(base, size, lo, hi)) return 4;
    }
    size_t p = so, end = so + sz;
    int depth = 0, in_memory = 0, in_reserved = 0, covered = 0;
    int root_ac = 0, root_sc = 0, res_ac = 0, res_sc = 0, roots = 0;
    while (p < end) {
        if (end - p < 4) return 5;
        uint32_t token = rg_be32(f + p); p += 4;
        if (token == 1) {
            size_t start = p;
            while (p < end && f[p]) p++;
            if (p == end || depth >= 64 || (!depth && roots++)) return 6;
            size_t n = p - start;
            if (depth == 1) {
                in_memory = rg_memory_name(f + start, n);
                in_reserved = rg_name(f + start, n, "reserved-memory");
                if (in_reserved) { res_ac = root_ac; res_sc = root_sc; }
            }
            p = (p + 4) & ~(size_t)3; depth++;
            if (p > end) return 7;
        } else if (token == 2) {
            if (!depth) return 8;
            if (--depth == 1) in_memory = in_reserved = 0;
        } else if (token == 3) {
            if (!depth || end - p < 8) return 9;
            uint32_t len = rg_be32(f + p), no = rg_be32(f + p + 4); p += 8;
            if (len > end - p || no >= sn) return 10;
            size_t nn = 0;
            while (nn < sn - no && f[strings + no + nn]) nn++;
            if (nn == sn - no) return 11;
            const uint8_t *name = f + strings + no;
            int ac = rg_name(name, nn, "#address-cells"), sc = rg_name(name, nn, "#size-cells");
            if ((depth == 1 || (depth == 2 && in_reserved)) && (ac || sc)) {
                if (len != 4 || rg_be32(f + p) != 2) return 12;
                if (depth == 1) { if (ac) root_ac = 2; else root_sc = 2; }
                else { if (ac) res_ac = 2; else res_sc = 2; }
            }
            if (depth == 2 && in_reserved && rg_name(name, nn, "ranges") && len) return 13;
            if (rg_name(name, nn, "reg") &&
                ((depth == 2 && in_memory) || (depth == 3 && in_reserved))) {
                if (root_ac != 2 || root_sc != 2 || !len || len % 16 ||
                    (in_reserved && (res_ac != 2 || res_sc != 2))) return 14;
                for (size_t i = 0; i < len; i += 16) {
                    uint64_t base = rg_be64(f + p + i), size = rg_be64(f + p + i + 8);
                    if (size > UINT64_MAX - base) return 15;
                    if (in_reserved && rg_overlap(base, size, lo, hi)) return 16;
                    if (in_memory && base <= lo && base + size >= hi) covered = 1;
                }
            }
            p = (p + len + 3) & ~(size_t)3;
            if (p > end) return 17;
        } else if (token == 9) {
            return depth || roots != 1 || !covered || root_ac != 2 || root_sc != 2 ? 18 : 0;
        } else if (token != 4) return 19;
    }
    return 20;
}
