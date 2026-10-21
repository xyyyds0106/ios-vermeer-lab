/* SPDX-License-Identifier: MIT */
#include "AdrenoCmdDb.h"
static const char names[AdrenoCmdDbResources][9]={"gfx.lvl","cx.lvl","mx.lvl","gmxc.lvl","SH0","MC0","ACV","CN0","mxc.lvl"};
const char *AdrenoCmdDbResourceName(unsigned i){return i<AdrenoCmdDbResources?names[i]:"?";}
static uint16_t u16(const uint8_t *p){return (uint16_t)p[0]|(uint16_t)p[1]<<8;}
static uint32_t u32(const uint8_t *p){return (uint32_t)u16(p)|(uint32_t)u16(p+2)<<16;}
static uint64_t key(const uint8_t *p){return (uint64_t)u32(p)|(uint64_t)u32(p+4)<<32;}
static int range(unsigned off,unsigned bytes,size_t size){return off<=size && bytes<=size-off;}
static int overlap(unsigned a,unsigned an,unsigned b,unsigned bn){return an && bn && a<b+bn && b<a+an;}
static unsigned fail(struct AdrenoCmdDb *r,unsigned error){r->status=error;r->complete=0;return error;}
unsigned AdrenoCmdDbParse(const uint8_t *image,size_t bytes,unsigned qualified_version,struct AdrenoCmdDb *r)
{
    if(!image || !r || bytes!=ADRENO_CMD_DB_BYTES)return AdrenoCmdDbArgument;
    uintptr_t a=(uintptr_t)image,b=(uintptr_t)r;
    if(a<=b?b-a<bytes:a-b<sizeof(*r))return AdrenoCmdDbArgument;
    *r=(struct AdrenoCmdDb){0};
    if(qualified_version!=1 || u32(image)!=1 || u32(image+4)!=0x0c0330db || (u32(image+140)&1))
        return fail(r,AdrenoCmdDbHeader);
    unsigned offsets[8]={0},counts[8]={0},aux[8]={0},types[8]={0},versions[8]={0},total=0,ended=0;
    for(unsigned i=0;i<8;i++) {
        const uint8_t *row=image+8+16*i;unsigned type=u16(row);
        if(!type){ended=1;continue;}
        if(ended || type>7)return fail(r,AdrenoCmdDbHeader);
        /* cmd-db may split one slave type across several directory rows. */
        types[i]=type;counts[i]=u16(row+6);offsets[i]=144u+u16(row+2);aux[i]=144u+u16(row+4);versions[i]=u16(row+8);
        if(counts[i]>ADRENO_CMD_DB_MAX_ENTRIES-total)return fail(r,AdrenoCmdDbBounds);
        total+=counts[i];
        if((offsets[i]&3u) || !range(offsets[i],24u*counts[i],bytes) || !range(aux[i],0,bytes))
            return fail(r,AdrenoCmdDbBounds);
        for(unsigned j=0;j<i;j++)if(overlap(offsets[i],24u*counts[i],offsets[j],24u*counts[j]))
            return fail(r,AdrenoCmdDbBounds);
        ++r->rows;
    }
    if(!r->rows)return fail(r,AdrenoCmdDbHeader);
    for(unsigned row=0;row<r->rows;row++)for(unsigned i=0;i<counts[row];i++) {
        const uint8_t *entry=image+offsets[row]+24*i;uint64_t id=key(entry);
        if(!id)return fail(r,AdrenoCmdDbHeader);
        for(unsigned j=0;j<r->entries;j++)if(r->seen[j]==id)return fail(r,AdrenoCmdDbDuplicate);
        r->seen[r->entries++]=id;
        unsigned length=u16(entry+20),at=aux[row]+u16(entry+22);
        if(!range(at,length,bytes))return fail(r,AdrenoCmdDbBounds);
        for(unsigned j=0;j<r->rows;j++)if(overlap(at,length,offsets[j],24u*counts[j]))
            return fail(r,AdrenoCmdDbBounds);
        for(unsigned target=0;target<AdrenoCmdDbResources;target++)if(id==key((const uint8_t *)names[target])) {
            struct AdrenoCmdDbResource *out=&r->resource[target];
            unsigned arc=target<4 || target==AdrenoCmdDbMxc,type=arc?3:5;
            uint32_t addr=u32(entry+16);
            if(types[row]!=type || !addr || (addr&3u) || ((addr>>16)&7u)!=type || addr>UINT32_MAX-16 ||
               !length || length>sizeof(out->aux) || (arc?(length&1u):length!=8))
                return fail(r,AdrenoCmdDbUnsupported);
            for(unsigned j=0;j<AdrenoCmdDbResources;j++)if((r->found_mask&(1u<<j)) && r->resource[j].address==addr)
                return fail(r,AdrenoCmdDbDuplicate);
            out->address=addr;out->version=(uint16_t)versions[row];out->aux_bytes=(uint16_t)length;
            for(unsigned j=0;j<length;j++)out->aux[j]=image[at+j];
            r->found_mask|=1u<<target;
        }
    }
    r->complete=1;
    r->status=(r->found_mask&0xf7u)==0xf7u?AdrenoCmdDbOK:AdrenoCmdDbMissing;
    return r->status;
}
