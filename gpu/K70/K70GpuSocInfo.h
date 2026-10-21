/* SPDX-License-Identifier: MIT */
#ifndef K70_GPU_SOCINFO_H
#define K70_GPU_SOCINFO_H
#include <stddef.h>
#include <stdint.h>
/* Decode a stable private copy of SMEM item 137, NOT the whole SMEM area.
 * Layout: MiCode 53ef33ea drivers/soc/qcom/socinfo.c, formats 0.16..0.19.
 * GPU part type=1, each mapping is 3 little-endian words + 32-byte name.
 * No mapping/MMIO or silicon-ID fallback: board acquisition must independently
 * locate item 137 and establish stability. GMU revision 0x07020100 is a
 * catalog/firmware value and must never substitute for this actual chip ID.
 */
struct K70GpuSocInfo {uint32_t format,soc_id,gpu_chip_id,vulkan_id;};
static inline uint32_t k70_soc_le32(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static inline int K70GpuSocInfoDecode(const uint8_t *p,size_t bytes,struct K70GpuSocInfo *out)
{
    if(!p || !out || bytes<192 || bytes>65536)return 1;
    uintptr_t a=(uintptr_t)p,b=(uintptr_t)out;
    if(a<=b?b-a<bytes:a-b<sizeof(*out))return 1;
    uint32_t format=k70_soc_le32(p),header;
    if(format<16 || format>19)return 1;
    header=format==16?192:format==17?196:format==18?204:216;
    if(bytes<header)return 1;
    size_t at=k70_soc_le32(p+184),count=k70_soc_le32(p+188);
    if(!count || count>17 || (at&3) || at<header || at>bytes || count>(bytes-at)/44)return 1;
    uint32_t seen=0;struct K70GpuSocInfo r={format,k70_soc_le32(p+4),0,0};
    for(size_t i=0;i<count;i++) {
        const uint8_t *entry=p+at+44*i;uint32_t type=k70_soc_le32(entry);
        if(!type || type>=17 || (seen&(1u<<type)))return 1;
        seen|=1u<<type;
        if(type==1) {
            r.gpu_chip_id=k70_soc_le32(entry+4);r.vulkan_id=k70_soc_le32(entry+8);
            if(!r.gpu_chip_id || r.gpu_chip_id==UINT32_MAX)return 1;
        }
    }
    if(!r.soc_id || r.soc_id==UINT32_MAX || !r.gpu_chip_id)return 1;
    *out=r;return 0;
}
#endif
