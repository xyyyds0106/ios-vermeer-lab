/* Host tests execute the same parser and I/O bounds helper as the driver. */
#define VERMEER_HOST_TEST 1
#include "../xnu/vermeer/vermeer_gpt.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

static uint8_t h[4096], e[4096];
static void p32(uint8_t *p, uint32_t n)
{ for (unsigned i = 0; i < 4; i++) p[i] = n >> (8*i); }
static void p64(uint8_t *p, uint64_t n)
{ p32(p, n); p32(p+4, n >> 32); }
static void hcrc(void)
{ p32(h+16, 0); p32(h+16, vg_crc(h, vg_le32(h+12))); }
static void ecrc(void)
{ p32(h+88, vg_crc(e, sizeof(e))); hcrc(); }
static void entry(unsigned index, const char *name, uint64_t first, uint64_t last)
{
    uint8_t *p = e + index * 128;
    memset(p, 0, 128); p[0] = 1; p[16] = index + 1;
    p64(p+32, first); p64(p+40, last);
    for (unsigned i = 0; name[i]; i++) p[56+2*i] = name[i];
}
static void fixture(void)
{
    memset(h, 0, sizeof(h)); memset(e, 0, sizeof(e));
    memcpy(h, "EFI PART", 8); p32(h+8, 0x10000); p32(h+12, 92);
    p64(h+24, 1); p64(h+32, 61842431); p64(h+40, 6); p64(h+48, 61842426);
    p64(h+72, 2); p32(h+80, 32); p32(h+84, 128);
    entry(0, "example", 6, 12); entry(28, "userdata", 2570304, 61842426);
    ecrc();
}
static int parse(void)
{
    struct vermeer_gpt g;
    int rc = vg_header(h, 61842431, &g);
    if (!rc) rc = vg_userdata(e, &g);
    return rc;
}
int main(int argc, char **argv)
{
    struct vermeer_gpt g;
    fixture(); assert(!parse()); assert(!vg_header(h, 61842431, &g));
    assert(!vg_userdata(e, &g) && g.first == 2570304 && g.blocks == 59272123);
    h[0] ^= 1; assert(parse()); fixture(); h[60] ^= 1; assert(parse());
    fixture(); e[30] ^= 1; assert(parse());
    fixture(); p32(h+12, 4097); assert(parse());
    fixture(); p32(h+12, 91); assert(parse());
    fixture(); p32(h+80, 0); hcrc(); assert(parse());
    fixture(); p32(h+80, UINT32_MAX); hcrc(); assert(parse());
    fixture(); p32(h+84, 256); hcrc(); assert(parse());
    fixture(); p64(h+24, 2); hcrc(); assert(parse());
    fixture(); p64(h+32, 61842430); hcrc(); assert(parse());
    fixture(); p64(h+72, UINT64_MAX); hcrc(); assert(parse());
    fixture(); p64(h+72, 6); hcrc(); assert(parse());
    fixture(); p64(h+48, 61842430); hcrc(); assert(parse());
    fixture(); entry(28, "userdata", 0, 61842426); ecrc(); assert(parse());
    fixture(); entry(28, "userdata", 2570304, 61842427); ecrc(); assert(parse());
    fixture(); entry(28, "userdata", 2570304, 2570303); ecrc(); assert(parse());
    fixture(); entry(28, "not-userdata", 2570304, 61842426); ecrc(); assert(parse());
    fixture(); entry(1, "userdata", 13, 14); ecrc(); assert(parse());
    fixture(); entry(1, "overlap", 12, 14); ecrc(); assert(parse());
    fixture(); memset(e+28*128, 0, 16); ecrc(); assert(parse());
    uint64_t off, bytes = 59272123ULL * 4096;
    assert(!vg_io_bounds(0, 512, 8192, bytes, &off) && !off);
    assert(!vg_io_bounds(1, 512, 512, bytes, &off) && off == 512);
    assert(!vg_io_bounds(bytes/512, 512, 0, bytes, &off));
    assert(vg_io_bounds(bytes/512, 512, 512, bytes, &off));
    assert(vg_io_bounds(-1, 512, 512, bytes, &off));
    assert(vg_io_bounds(INT64_MAX, 4096, 4096, bytes, &off));
    assert(vg_io_bounds(0, 0, 1, bytes, &off));
    assert(!vg_io_bounds(bytes/4096-1, 4096, 4096, bytes, &off));
    assert(vg_io_bounds(bytes/4096-1, 4096, 4097, bytes, &off));
    if (argc == 2) {
        /* Optional private live Android GPT. No dumps or identifiers in repo. */
        FILE *f = fopen(argv[1], "rb"); assert(f);
        assert(!fseek(f, 4096, SEEK_SET)); assert(fread(h, 1, 4096, f) == 4096);
        assert(!vg_header(h, 61842431, &g));
        assert(g.entries_bytes <= sizeof(e));
        assert(!fseek(f, (long)g.entries_lba * 4096, SEEK_SET));
        assert(fread(e, 1, g.entries_bytes, f) == g.entries_bytes); fclose(f);
        assert(!vg_userdata(e, &g)); assert(g.first == 2570304 && g.blocks == 59272123);
    }
    puts("GPT corruption/range/overlap/duplicate and I/O overflow tests: PASS");
    return 0;
}
