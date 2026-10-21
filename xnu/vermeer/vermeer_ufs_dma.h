/* Guarded UFSHCI address encoding, shared with host tests.
 * Layout/capability reference: Linux v6.1 include/ufs/ufshci.h and
 * drivers/ufs/core/ufshcd.c: ufshcd_host_memory_configure/ufshcd_map_sg.
 * https://github.com/torvalds/linux/blob/v6.1/include/ufs/ufshci.h
 */
#ifndef VERMEER_UFS_DMA_H
#define VERMEER_UFS_DMA_H
#ifdef VERMEER_HOST_TEST
#include <stdint.h>
#else
#include <sys/types.h>
#endif
#define VU_DMA_SIZE 0x8000U
static inline uint32_t vu_lo(uint64_t pa) { return (uint32_t)pa; }
static inline uint32_t vu_hi(uint64_t pa) { return (uint32_t)(pa >> 32); }
static inline int vu_dma_allowed(uint64_t pa, int high_enabled, uint32_t cap, uint32_t smmu_id2)
{
    if (pa & 0x3fff) return 0;
    if (pa >= 0xc0000000ULL && pa <= 0xd0000000ULL - VU_DMA_SIZE) return 1;
    /* Require explicit opt-in, UFS 64AS and this handset's exact observed
     * SMMU ID2 (36-bit address interfaces). No arbitrary high DMA address.
     */
    return high_enabled == 1 && (cap & 0x01000000U) && smmu_id2 == 0x5111 &&
           pa >= 0x8c0000000ULL && pa <= 0x940000000ULL - VU_DMA_SIZE;
}
static inline void vu_dma_descriptors(volatile uint32_t *utrd,
                                      volatile uint32_t *prdt, uint64_t pa, unsigned length)
{
    utrd[4] = vu_lo(pa + 0x1000);
    utrd[5] = vu_hi(pa + 0x1000);
    if (length) {
        prdt[0] = vu_lo(pa + 0x4000);
        prdt[1] = vu_hi(pa + 0x4000);
        prdt[2] = 0;
        prdt[3] = length - 1;
    }
}
#endif
