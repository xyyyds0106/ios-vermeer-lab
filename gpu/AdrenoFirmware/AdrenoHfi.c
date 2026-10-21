/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* HFI wire facts from the source pinned by the umbrella. No Linux runtime. */
#include "AdrenoHfi.h"
#if !defined(__aarch64__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Modern GMU HFI transport requires little-endian AArch64
#endif
#define WORDS 1024u
static void acquire(void){__asm__ volatile("dmb oshld":::"memory");}
static void publish(void){__asm__ volatile("dsb oshst":::"memory");}
static void complete(void){__asm__ volatile("dmb osh":::"memory");}
static AdrenoHfiStatus poison(AdrenoHfi *h,AdrenoHfiStatus status)
{h->poisoned=1;return status;}
static AdrenoHfiStatus valid(AdrenoHfi *h,unsigned q,volatile uint32_t **header,
    volatile uint32_t **data,uint32_t *read,uint32_t *write)
{
    if(!h || !h->shared || !h->notify || !h->iova || (h->iova&4095) ||
        ((uintptr_t)h->shared&4095))return AdrenoHfiInvalid;
    if(h->poisoned)return AdrenoHfiPoisoned;
    volatile uint32_t *m=h->shared;acquire();
    if(m[0]!=0 || m[1]!=168 || m[2]!=6 || m[3]!=12 || m[4]!=3 || m[5]!=3)
        return poison(h,AdrenoHfiCorrupt);
    volatile uint32_t *v=m+6+12*q;
    if(v[0]!=1 || v[1]!=h->iova+(q+1)*4096 || v[2]!=(0xa00|q) ||
        v[3]!=WORDS || v[4]!=0)return poison(h,AdrenoHfiCorrupt);
    *read=v[10];*write=v[11];acquire();
    if(*read>=WORDS || *write>=WORDS || (*read&3) || (*write&3))return poison(h,AdrenoHfiCorrupt);
    *header=v;*data=m+(q+1)*WORDS;return AdrenoHfiSuccess;
}
AdrenoHfiStatus AdrenoHfiInitializeStopped(AdrenoHfi *h,volatile uint32_t *shared,
    size_t bytes,uint64_t iova,void *cookie,int (*notify)(void *))
{
    if(!h || !shared || !notify || bytes!=16384 || ((uintptr_t)shared&4095) ||
        (uintptr_t)shared>UINTPTR_MAX-16383 || !iova || (iova&4095) || iova>UINT32_MAX-16383)
        return AdrenoHfiInvalid;
    for(unsigned i=0;i<4096;++i)shared[i]=0;
    shared[1]=168;shared[2]=6;shared[3]=12;shared[4]=3;shared[5]=3;
    for(unsigned q=0;q<3;++q) {
        volatile uint32_t *v=shared+6+12*q;
        v[0]=1;v[1]=(uint32_t)iova+(q+1)*4096;v[2]=0xa00|q;v[3]=WORDS;
        v[6]=1;v[7]=1;v[8]=1;
    }
    publish();
    *h=(AdrenoHfi){shared,(uint32_t)iova,0,0,0,cookie,notify,0,0,0,0};
    return AdrenoHfiSuccess;
}
AdrenoHfiStatus AdrenoHfiSend(AdrenoHfi *h,unsigned id,const uint32_t *payload,
    size_t words,uint32_t *wire_header)
{
    if(!wire_header || id>255 || words>254 || (words && !payload))return AdrenoHfiInvalid;
    volatile uint32_t *v,*data;uint32_t read,write;
    AdrenoHfiStatus status=valid(h,0,&v,&data,&read,&write);
    if(status!=AdrenoHfiSuccess)return status;
    if(h->pending_header)return AdrenoHfiBusy;
    unsigned count=(unsigned)words+1,padded=(count+3)&~3u;
    if(((read-write-1)&(WORDS-1))<padded)return AdrenoHfiNoSpace;
    uint32_t sequence=(h->sequence+1)%4095;
    uint32_t header=(sequence<<20)|(count<<8)|id;
    data[write]=header;
    for(unsigned i=0;i<words;++i)data[(write+1+i)&(WORDS-1)]=payload[i];
    for(unsigned i=count;i<padded;++i)data[(write+i)&(WORDS-1)]=UINT32_C(0xfafafafa);
    publish();v[11]=(write+padded)&(WORDS-1);publish();
    h->pending_header=header;h->sequence=sequence;*wire_header=header;
    if(h->notify(h->cookie))return poison(h,AdrenoHfiNotifyError);
    return AdrenoHfiSuccess;
}
AdrenoHfiStatus AdrenoHfiPoll(AdrenoHfi *h,uint32_t *payload,size_t capacity,
    size_t *payload_words,uint32_t *error)
{
    if(!payload || capacity<16 || !payload_words || !error)return AdrenoHfiInvalid;
    *payload_words=0;*error=0;
    volatile uint32_t *v,*data;uint32_t read,write;
    AdrenoHfiStatus status=valid(h,1,&v,&data,&read,&write);
    if(status!=AdrenoHfiSuccess)return status;
    if(!h->pending_header)return AdrenoHfiInvalid;
    /* A ring holds at most WORDS/4 packets; each pass consumes one. */
    for(unsigned pass=0;pass<WORDS/4;++pass) {
        if(read==write) {v[8]=1;publish();return AdrenoHfiEmpty;}
        uint32_t packet[19];uint32_t header=data[read];unsigned count=(header>>8)&255;
        unsigned available=(write-read)&(WORDS-1),padded=(count+3)&~3u;
        if(!count || count>19 || padded>available)return poison(h,AdrenoHfiCorrupt);
        for(unsigned i=0;i<count;++i)packet[i]=data[(read+i)&(WORDS-1)];
        complete();
        const int firmware_error=(header&255)==100;
        const int match=!firmware_error && count>=3 && (packet[1]>>20)==(h->pending_header>>20);
        if(!match) {
            if(count<2)return poison(h,AdrenoHfiCorrupt);
            if(!h->skipped++) {h->skipped_header=header;h->skipped_ret=packet[1];h->skipped_code=count>2?packet[2]:0;}
            read=(read+padded)&(WORDS-1);v[10]=read;publish();
            continue;
        }
        if(packet[2]) {*error=packet[2];return poison(h,AdrenoHfiFirmwareError);}
        /* Copy only the checked matching response, never padding. This is not
         * cryptographic authentication of firmware or its response. */
        for(unsigned i=3;i<count;++i)payload[i-3]=packet[i];
        complete();v[10]=(read+padded)&(WORDS-1);publish();
        h->pending_header=0;*payload_words=count-3;return AdrenoHfiSuccess;
    }
    return poison(h,AdrenoHfiCorrupt);
}
AdrenoHfiStatus AdrenoHfiExchange(AdrenoHfi *h,unsigned id,const uint32_t *input,
    size_t words,unsigned budget,int (*wait_once)(void *),uint32_t *output,
    size_t capacity,size_t *count,uint32_t *error)
{
    if(!budget || budget>1048576 || !output || capacity<16 || !count || !error)
        return AdrenoHfiInvalid;
    *count=0;*error=0;uint32_t header;
    AdrenoHfiStatus status=AdrenoHfiSend(h,id,input,words,&header);
    if(status!=AdrenoHfiSuccess)return status;
    for(unsigned i=0;i<budget;++i) {
        status=AdrenoHfiPoll(h,output,capacity,count,error);
        if(status!=AdrenoHfiEmpty)return status;
        if(i+1<budget && wait_once && wait_once(h->cookie))return poison(h,AdrenoHfiWaitError);
    }
    return poison(h,AdrenoHfiTimeout);
}
