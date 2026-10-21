/* SPDX-License-Identifier: MIT */
#include "AdrenoFillPlan.h"
#include "../AdrenoPM4/AdrenoPM4.h"
static int width(unsigned bits)
{return bits==32 || bits==36 || bits==40 || bits==42 || bits==44 || bits==48;}
AdrenoFillStatus AdrenoFillTranslate(const AdrenoFillRequest *r,const AdrenoFillView *v,
    uint32_t *words,size_t capacity,AdrenoFillPlan *plan,size_t *word_count)
{
    if(!word_count)return AdrenoFillInvalid;
    *word_count=0;
    if(!r || !v || !words || !plan || capacity>SIZE_MAX/sizeof(*words))return AdrenoFillInvalid;
    if(r->version!=1 || r->operation!=AdrenoFillBuffer || r->reserved || r->value>255 ||
        !r->bytes || r->bytes>AdrenoFillMaxBytes || ((r->offset|r->bytes)&3u) || !width(v->address_bits))
        return AdrenoFillUnsupported;
    if(!r->buffer_id || !r->generation || !r->context_id ||
        r->buffer_id!=v->buffer_id || r->generation!=v->generation || r->context_id!=v->context_id ||
        v->flags!=(AdrenoFillMapped|AdrenoFillWritable|AdrenoFillLeaseHeld))return AdrenoFillResource;
    const uint64_t limit=UINT64_C(1)<<v->address_bits;
    if(!v->gpu_base || (v->gpu_base&3u) || !v->logical_bytes || v->gpu_base>=limit ||
        v->logical_bytes>limit-v->gpu_base || r->offset>v->logical_bytes ||
        r->bytes>v->logical_bytes-r->offset)return AdrenoFillRange;
    const uint64_t address=v->gpu_base+r->offset;
    const size_t data_words=(size_t)(r->bytes/4),needed=data_words+3;
    if(capacity<needed)return AdrenoFillCapacity;
    uint32_t header;
    if(AdrenoPM4Type7(0x3d,(uint32_t)data_words+2,&header)!=AdrenoPM4Success)
        return AdrenoFillUnsupported;
    const uint32_t pattern=r->value*UINT32_C(0x01010101);
    words[0]=header;words[1]=(uint32_t)address;words[2]=(uint32_t)(address>>32);
    for(size_t i=0;i<data_words;i++)words[i+3]=pattern;
    *plan=(AdrenoFillPlan){address,r->bytes,(uint32_t)needed,1};
    *word_count=needed;return AdrenoFillPlanned;
}
