/* Offline-tested record codec for a future observable diagnostic. No MMIO,
 * USB, shared publication, exception recovery, reset or retention mechanism.
 * Callers serialize access and compare run/image identity at the receiver.
 */
#ifndef VERMEER_BASEBAND_RECORD_H
#define VERMEER_BASEBAND_RECORD_H
#include "vermeer_baseband_protocol.h"
#define VBB_RECORD_SIZE 128u
enum { VBB_REC_BEGIN=1,VBB_REC_BEFORE_READ=2,VBB_REC_READ_OK=3,
       VBB_REC_FINISHED=4,VBB_REC_ABORT=5,VBB_REC_REFUSED=6 };
enum { VBB_REC_EXCEPTION=1,VBB_REC_FAR_VALID=2 };
struct vbb_record {
    uint64_t sequence,address,esr,far;
    uint8_t run[16],image_sha256[32];
    uint32_t state,phase,length,result,vector,flags;
};
static inline uint64_t vbb_rec_u64(const uint8_t *p)
{ return (uint64_t)vbb_u32(p)|((uint64_t)vbb_u32(p+4)<<32); }
static inline void vbb_rec_put64(uint8_t *p,uint64_t v)
{ vbb_put32(p,(uint32_t)v);vbb_put32(p+4,(uint32_t)(v>>32)); }
static inline uint32_t vbb_record_crc(const uint8_t *p,size_t n)
{
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<n;i++) {
        crc^=p[i];
        for(unsigned b=0;b<8;b++)crc=(crc>>1)^((0u-(crc&1))&UINT32_C(0xedb88320));
    }
    return ~crc;
}
static inline int vbb_record_valid(const struct vbb_record *r)
{
    if(!r || !r->sequence || r->state<1 || r->state>6 || r->phase>5 ||
       r->length>4096 || r->result>5 || (r->flags&~3u))return 0;
    unsigned run=0,hash=0;
    for(unsigned i=0;i<16;i++)run|=r->run[i];
    for(unsigned i=0;i<32;i++)hash|=r->image_sha256[i];
    if(!run || !hash)return 0;
    if(r->state==VBB_REC_BEGIN)
        return r->sequence==1 && !r->phase && !r->address && !r->length &&
            !r->result && !r->esr && !r->far && !r->vector && !r->flags;
    if(r->sequence==1)return 0;
    if(r->state==VBB_REC_FINISHED || r->state==VBB_REC_REFUSED) {
        if(r->address || r->length || r->esr || r->far || r->vector || r->flags)return 0;
        /* A metadata success requires the final partition-header recheck. */
        if(r->state==VBB_REC_FINISHED)return r->phase && (r->result || r->phase==5);
        return r->result!=0;
    }
    if(!r->phase || r->result || !r->length || (r->address&7) || (r->length&7) ||
       r->address<UINT64_C(0x81d00000) || r->address>=UINT64_C(0x81f00000) ||
       r->length>UINT64_C(0x81f00000)-r->address)return 0;
    if(r->state!=VBB_REC_ABORT)return !r->esr && !r->far && !r->vector && !r->flags;
    return (r->flags&VBB_REC_EXCEPTION) && r->vector<16 &&
        ((r->flags&VBB_REC_FAR_VALID) || !r->far);
}
/* Exactly 128 little-endian bytes; reserved bytes are zero. CRC is corruption
 * detection, not authentication. Failing encode clears the caller's frame. */
static inline int vbb_record_encode(const struct vbb_record *r,uint8_t *p,size_t n)
{
    if(!p || n!=VBB_RECORD_SIZE)return 0;
    for(size_t i=0;i<n;i++)p[i]=0;
    if(!vbb_record_valid(r))return 0;
    vbb_put32(p,UINT32_C(0x52424256));vbb_put16(p+4,1);vbb_put16(p+6,VBB_RECORD_SIZE);
    vbb_rec_put64(p+8,r->sequence);
    for(unsigned i=0;i<16;i++)p[16+i]=r->run[i];
    for(unsigned i=0;i<32;i++)p[32+i]=r->image_sha256[i];
    vbb_put32(p+64,r->state);vbb_put32(p+68,r->phase);vbb_rec_put64(p+72,r->address);
    vbb_put32(p+80,r->length);vbb_put32(p+84,r->result);vbb_rec_put64(p+88,r->esr);
    vbb_rec_put64(p+96,r->far);vbb_put32(p+104,r->vector);vbb_put32(p+108,r->flags);
    vbb_put32(p+124,vbb_record_crc(p,124));return 1;
}
static inline int vbb_record_decode(const uint8_t *p,size_t n,struct vbb_record *out)
{
    struct vbb_record r={0};
    if(!out)return 0;
    *out=r;
    if(!p || n!=VBB_RECORD_SIZE || vbb_u32(p)!=UINT32_C(0x52424256) ||
       vbb_u16(p+4)!=1 || vbb_u16(p+6)!=VBB_RECORD_SIZE ||
       vbb_u32(p+124)!=vbb_record_crc(p,124))return 0;
    for(unsigned i=112;i<124;i++)if(p[i])return 0;
    r.sequence=vbb_rec_u64(p+8);
    for(unsigned i=0;i<16;i++)r.run[i]=p[16+i];
    for(unsigned i=0;i<32;i++)r.image_sha256[i]=p[32+i];
    r.state=vbb_u32(p+64);r.phase=vbb_u32(p+68);r.address=vbb_rec_u64(p+72);
    r.length=vbb_u32(p+80);r.result=vbb_u32(p+84);r.esr=vbb_rec_u64(p+88);
    r.far=vbb_rec_u64(p+96);r.vector=vbb_u32(p+104);r.flags=vbb_u32(p+108);
    if(!vbb_record_valid(&r))return 0;
    *out=r;return 1;
}
#endif
