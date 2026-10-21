/* SPDX-License-Identifier: MIT
 * K70 port: CPU ownership granules and GPU translation granules are distinct.
 */
#ifndef NATIVE_ADRENO_GRANULES_H
#define NATIVE_ADRENO_GRANULES_H
#include <stdint.h>
#include <stddef.h>
enum { NativeAdrenoGpuPage=4096, NativeAdrenoMaxAllocation=256u*1024u*1024u };
static inline int NativeAdrenoCpuGranuleValid(uint64_t page,uint64_t map_page,
    uint64_t tcr,uint64_t sctlr,uint64_t ttbr1)
{
    if(page!=map_page || (page!=4096 && page!=16384) || !(sctlr&1))return 0;
    unsigned tg0=(unsigned)(tcr>>14)&3,tg1=(unsigned)(tcr>>30)&3;
    if(page==4096 ? (tg0!=0 || tg1!=2) : (tg0!=2 || tg1!=1))return 0;
    return (ttbr1 & UINT64_C(0x0000ffffffffffff) & ~(page-1))!=0;
}
static inline int NativeAdrenoAllocationLayout(size_t requested,size_t budget,
    size_t cpu_page,size_t *owned)
{
    if(!owned)return 0;
    *owned=0;
    if((cpu_page!=4096 && cpu_page!=16384) || !requested || (requested&4095) ||
       !budget || budget>NativeAdrenoMaxAllocation || requested>budget ||
       requested>SIZE_MAX-(cpu_page-1))return 0;
    size_t result=(requested+cpu_page-1)&~(cpu_page-1);
    if(result>NativeAdrenoMaxAllocation)return 0;
    *owned=result;return 1;
}
#endif
