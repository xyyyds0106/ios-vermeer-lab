/* SPDX-License-Identifier: MIT
 * Fill a reserved Apple-DT property with THIS boot's Linux FDT. The payload
 * carries only zeroes; a saved host DT cannot substitute for the ABL input.
 * No allocation, physical discovery, device register access or publication.
 */
#ifndef VERMEER_BOOT_FDT_H
#define VERMEER_BOOT_FDT_H
#include <stddef.h>
#include <stdint.h>
#define BOOT_FDT_CAPACITY 0x200000u
static inline uint32_t bf_le32(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static inline uint32_t bf_be32(const uint8_t *p)
{return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static inline int bf_name(const uint8_t *p,size_t n,const char *s)
{
    size_t i=0;while(i<n && s[i] && p[i]==(uint8_t)s[i])i++;
    return i<n && !s[i] && !p[i];
}
/* 1 found, 0 legacy ADT without slot, -1 malformed/ambiguous template.
 * Parse the entire ADT before permitting any write; depth and byte bounds
 * apply independently of untrusted node/property counters.
 */
static inline int boot_fdt_slot(const uint8_t *adt,size_t bytes,size_t *offset)
{
    if(!adt || !offset || bytes<8 || bytes>0x1000000)return -1;
    /* Every pushed depth is assigned before use. Avoid a zero-filled array:
     * the freestanding boot shim has no libc memset implementation. */
    uint32_t remaining[64];remaining[0]=1;unsigned depth=0,found=0,chosen_count=0;
    size_t at=0,slot=0;
    for(;;) {
        if(!remaining[depth]){if(!depth)break;depth--;continue;}
        remaining[depth]--;
        if(at>bytes || bytes-at<8)return -1;
        unsigned props=bf_le32(adt+at),children=bf_le32(adt+at+4);
        at+=8;
        if(props>(bytes-at)/36 || children>(bytes-at)/8)return -1;
        unsigned names=0,chosen=0,markers=0;size_t candidate=0;
        for(unsigned p=0;p<props;p++) {
            if(at>bytes || bytes-at<36)return -1;
            const uint8_t *name=adt+at;uint32_t raw=bf_le32(adt+at+32);
            size_t length=raw&0x7fffffffu;at+=36;
            if(length>bytes-at)return -1;
            size_t padded=(length+3)&~(size_t)3;
            if(padded>bytes-at)return -1;
            if(bf_name(name,32,"name")) {
                if(++names!=1 || (raw>>31))return -1;
                chosen=length==7 && bf_name(adt+at,length,"chosen");
            }
            if(bf_name(name,32,"vermeer,boot-fdt")) {
                if(++markers!=1 || raw!=BOOT_FDT_CAPACITY)return -1;
                candidate=at;
            }
            at+=padded;
        }
        if(depth==1 && chosen && ++chosen_count!=1)return -1;
        if(markers) {
            if(depth!=1 || !chosen || found++)return -1;
            slot=candidate;
        }
        if(children){if(depth==63)return -1;remaining[++depth]=children;}
    }
    if(at!=bytes)return -1;
    if(found)*offset=slot;
    return found?1:0;
}
static inline int boot_fdt_bridge(uint8_t *adt,size_t bytes,const uint8_t *fdt,size_t available)
{
    size_t slot=0;int result=boot_fdt_slot(adt,bytes,&slot);
    if(result!=1)return result;
    if(!fdt || available<40 || bf_be32(fdt)!=0xd00dfeed)return -1;
    size_t total=bf_be32(fdt+4),structure=bf_be32(fdt+8),strings=bf_be32(fdt+12);
    size_t structure_bytes=bf_be32(fdt+36),string_bytes=bf_be32(fdt+32);
    if(total<40 || total>available || total>BOOT_FDT_CAPACITY ||
       structure<40 || (structure&3) || structure>total || structure_bytes>total-structure ||
       strings<40 || strings>total || string_bytes>total-strings ||
       bf_be32(fdt+20)!=17 || bf_be32(fdt+24)>17)return -1;
    uintptr_t a=(uintptr_t)adt,f=(uintptr_t)fdt;
    if(bytes>UINTPTR_MAX-a || total>UINTPTR_MAX-f || (a<f+total && f<a+bytes))return -1;
    for(size_t i=0;i<BOOT_FDT_CAPACITY;i++)if(adt[slot+i])return -1;
    for(size_t i=0;i<total;i++)adt[slot+i]=fdt[i];
    return 1;
}
#endif
