#define VERMEER_HOST_TEST 1
#include "../xnu/vermeer/vermeer_ufs_dma.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    const uint32_t cap = 0x3587031f;
    assert(vu_dma_allowed(0xc0000000, 0, cap, 0x5111));
    assert(vu_dma_allowed(0xcfff8000, 0, cap, 0x5111));
    assert(!vu_dma_allowed(0xcfffc000, 1, cap, 0x5111));
    assert(!vu_dma_allowed(0xbfffc000, 1, cap, 0x5111));
    assert(!vu_dma_allowed(0xc0000001, 1, cap, 0x5111));
    assert(!vu_dma_allowed(0x8c0000000ULL, 0, cap, 0x5111));
    assert(!vu_dma_allowed(0x8c0000000ULL, 2, cap, 0x5111));
    assert(!vu_dma_allowed(0x8c0000000ULL, 1, cap & ~0x01000000U, 0x5111));
    assert(!vu_dma_allowed(0x8c0000000ULL, 1, cap, 0x5110));
    assert(vu_dma_allowed(0x8c0000000ULL, 1, cap, 0x5111));
    assert(vu_dma_allowed(0x93fff8000ULL, 1, cap, 0x5111));
    assert(!vu_dma_allowed(0x93fffc000ULL, 1, cap, 0x5111));
    assert(!vu_dma_allowed(0x8bfffc000ULL, 1, cap, 0x5111));
    assert(!vu_dma_allowed(0xffffffffffffc000ULL, 1, cap, 0x5111));
    uint32_t u[8] = {0}, d[4] = {0};
    vu_dma_descriptors(u, d, 0x8c1234000ULL, 8192);
    assert(u[4] == 0xc1235000 && u[5] == 8);
    assert(d[0] == 0xc1238000 && d[1] == 8 && d[2] == 0 && d[3] == 8191);
    vu_dma_descriptors(u, d, 0xc7400000ULL, 8);
    assert(u[4] == 0xc7401000 && u[5] == 0);
    assert(d[0] == 0xc7404000 && d[1] == 0 && d[3] == 7);
    /* Carry into the high word, even though this address is not permitted DMA. */
    vu_dma_descriptors(u, d, 0xfffff000ULL, 4096);
    assert(u[4] == 0 && u[5] == 1 && d[0] == 0x3000 && d[1] == 1);
    memset(d, 0xa5, sizeof(d)); vu_dma_descriptors(u, d, 0x8c0000000ULL, 0);
    assert(u[4] == 0xc0001000 && u[5] == 8);
    for (unsigned i = 0; i < 4; i++) assert(d[i] == 0xa5a5a5a5);
    puts("UFS DMA opt-in/range/capability/64-bit descriptor tests PASS");
}
