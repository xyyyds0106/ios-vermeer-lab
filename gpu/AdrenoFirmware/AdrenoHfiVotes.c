/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "AdrenoHfiVotes.h"
static uint16_t le16(const uint8_t *p){return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t le32(const uint8_t *p){return le16(p)|(uint32_t)le16(p+2)<<16;}
static int rail(const uint8_t *p,size_t bytes)
{
    if(!p || !bytes || (bytes&1) || bytes>512 || le16(p))return 0;
    unsigned padding=0;uint16_t last=0;
    for(size_t i=2;i<bytes;i+=2) {
        uint16_t value=le16(p+i);
        if(!value)padding=1;
        else if(padding || value<last)return 0;
        last=value;
    }
    return 1;
}
AdrenoHfiStartupStatus AdrenoHfiArcVotes(const uint32_t *levels,size_t count,
    const uint8_t *primary,size_t primary_bytes,const uint8_t *secondary,size_t secondary_bytes,
    uint32_t *votes,size_t capacity)
{
    if(!levels || !votes || !count || count>16 || capacity<count || levels[0] ||
        !rail(primary,primary_bytes) || !rail(secondary,secondary_bytes))return AdrenoHfiStartupInvalid;
    uint32_t result[16];
    for(size_t i=0;i<count;++i) {
        if(levels[i]>UINT16_MAX || (i && levels[i]<levels[i-1]))return AdrenoHfiStartupInvalid;
        size_t p=0,s=0;
        while(p<primary_bytes/2 && le16(primary+2*p)<levels[i])++p;
        if(p==primary_bytes/2)return AdrenoHfiStartupInvalid;
        for(size_t j=0;j<secondary_bytes/2;++j) {
            uint16_t value=le16(secondary+2*j);
            if(value>=levels[i]) {s=j;break;}
            if(value)s=j;
        }
        result[i]=(uint32_t)le16(primary+2*p)<<16 | (uint32_t)s<<8 | (uint32_t)p;
    }
    for(size_t i=0;i<count;++i)votes[i]=result[i];
    return AdrenoHfiStartupSuccess;
}
AdrenoHfiStartupStatus AdrenoHfiBcmAux(AdrenoHfiBcm *b,const uint8_t *p,size_t bytes)
{
    if(!b || !p || bytes!=8 || p[7])return AdrenoHfiStartupInvalid;
    b->unit=le32(p);b->width=le16(p+4);b->vcd=p[6];return AdrenoHfiStartupSuccess;
}
static int address(uint32_t a){return a && !(a&3);}
AdrenoHfiStartupStatus AdrenoHfiBuildBandwidth(const uint32_t *kbps,size_t count,
    const AdrenoHfiBcm *bcms,size_t n,uint32_t cn0,AdrenoHfiStartupConfig *c)
{
    if(!c || !kbps || !bcms || !count || count>16 || kbps[0] || !n || n>3 || !address(cn0))
        return AdrenoHfiStartupInvalid;
    for(size_t i=1;i<count;++i)if(kbps[i]<=kbps[i-1])return AdrenoHfiStartupInvalid;
    for(size_t j=0;j<n;++j) {
        const AdrenoHfiBcm *b=bcms+j;
        if(!address(b->address) || b->address==cn0 || b->fixed>1 || b->perfmode>0x3fff ||
            (!b->fixed && (!b->unit || !b->width || !b->buswidth || b->perfmode || b->perfmode_kbps)))
            return AdrenoHfiStartupInvalid;
        for(size_t k=0;k<j;++k)if(b->address==bcms[k].address)return AdrenoHfiStartupInvalid;
    }
    /* All input validation is finished before changing caller storage. */
    c->bandwidth_count=(unsigned)count;c->cnoc_count=1;c->ddr_count=(unsigned)n;
    c->cnoc_wait_mask=1;c->ddr_wait_mask=0;
    for(size_t i=0;i<6;++i) {
        c->cnoc_addresses[i]=i?0:cn0;c->cnoc_data[0][i]=i?0:0x40000000;
        c->cnoc_data[1][i]=i?0:0x60000001;
    }
    for(size_t j=0;j<8;++j) {
        c->ddr_addresses[j]=j<n?bcms[j].address:0;
        unsigned commit=j<n && (j+1==n || bcms[j].vcd!=bcms[j+1].vcd);
        if(commit)c->ddr_wait_mask|=1u<<j;
        for(size_t i=0;i<16;++i) {
            uint32_t data=0;
            if(i<count && j<n) {
                const AdrenoHfiBcm *b=bcms+j;data=commit?0x40000000:0;
                if(kbps[i]) {
                    data|=0x20000000;
                    if(b->fixed) {
                        if(b->perfmode_kbps && kbps[i]>=b->perfmode_kbps)data|=b->perfmode;
                    } else {
                        uint64_t peak=(uint64_t)kbps[i]*b->width/b->buswidth;
                        peak=peak*1000/b->unit;
                        uint32_t vote=peak>0x3fff?0x3fff:peak?(uint32_t)peak:1;
                        data|=vote<<14|vote;
                    }
                }
            }
            c->ddr_data[i][j]=data;
        }
    }
    return AdrenoHfiStartupSuccess;
}
