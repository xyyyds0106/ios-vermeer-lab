#define VERMEER_HOST_TEST 1
#include "../xnu/vermeer/vermeer_media_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char b[4096];
static void be32(unsigned off, uint32_t v)
{ for (unsigned i = 0; i < 4; i++) b[1024+off+i] = v >> (24-8*i); }
int main(void)
{
    assert(vi_write_allowed(1, VI_FIRST, VI_PART_BLOCKS, VI_FIRST, 2));
    assert(vi_write_allowed(1, VI_FIRST, VI_PART_BLOCKS, VI_FIRST+VI_ROOT_BLOCKS-1, 1));
    assert(!vi_write_allowed(1, VI_FIRST, VI_PART_BLOCKS, VI_FIRST+VI_ROOT_BLOCKS-1, 2));
    assert(!vi_write_allowed(1, VI_FIRST, VI_PART_BLOCKS, VI_FIRST-1, 1));
    assert(!vi_write_allowed(0, VI_FIRST, VI_PART_BLOCKS, VI_FIRST, 1));
    assert(!vi_write_allowed(2, VI_FIRST, VI_PART_BLOCKS, VI_FIRST, 1));
    assert(!vi_write_allowed(1, VI_FIRST+1, VI_PART_BLOCKS, VI_FIRST+1, 1));
    assert(!vi_write_allowed(1, VI_FIRST, VI_PART_BLOCKS-1, VI_FIRST, 1));
    assert(!vi_write_allowed(1, VI_FIRST, VI_PART_BLOCKS, UINT64_MAX, 2));
    assert(!vi_write_allowed(1, VI_FIRST, VI_PART_BLOCKS, VI_FIRST, 0));
    assert(!vi_write_allowed(1, VI_FIRST, VI_PART_BLOCKS, VI_FIRST, 3));
    uint64_t uuid; assert(!vi_uuid("0123456789AbCdEf", &uuid) && uuid == 0x0123456789abcdefULL);
    assert(vi_uuid("0000000000000000", &uuid)); assert(vi_uuid("0123", &uuid));
    assert(vi_uuid("0123456789abcdef0", &uuid)); assert(vi_uuid("0123456789abcdeg", &uuid));
    uuid = 0x123456789abcdef0ULL;
    b[1024] = 'H'; b[1025] = 'X'; b[1027] = 5;
    be32(4, 0x80002100); be32(12, 129); be32(40, 4096); be32(44, VI_ROOT_BLOCKS);
    be32(104, uuid >> 32); be32(108, uuid);
    assert(vi_hfs_root(b, uuid)); assert(!vi_hfs_root(b, uuid ^ 1));
    be32(4, 0x100); assert(!vi_hfs_root(b, uuid)); be32(4, 0x2000);
    be32(12, VI_ROOT_BLOCKS); assert(!vi_hfs_root(b, uuid)); be32(12, 129);
    be32(40, 512); assert(!vi_hfs_root(b, uuid)); be32(40, 4096);
    be32(44, VI_ROOT_BLOCKS+1); assert(!vi_hfs_root(b, uuid));
    puts("Integrated root UUID/journal/geometry and write-boundary tests PASS");
}
