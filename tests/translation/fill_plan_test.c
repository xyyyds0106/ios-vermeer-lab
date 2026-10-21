#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../gpu/translation/AdrenoFillPlan.h"
#include "../../gpu/AdrenoPM4/AdrenoPM4.h"
static unsigned pop(uint32_t v){unsigned n=0;for(;v;v>>=1)n+=v&1u;return n;}
/* Independent SOFTWARE oracle for packet semantics. Does not submit to a GPU. */
static void model(const uint32_t *words,size_t n,uint64_t base,uint8_t *memory,size_t bytes)
{
    uint32_t h=words[0],count=h&0x3fff,opcode=(h>>16)&127;
    assert((h>>28)==7 && !(h&0x0f004000u) && opcode==0x3d && count>=3 && n==count+1u);
    assert(((pop(count)+((h>>15)&1u))&1u)==1);
    assert(((pop(opcode)+((h>>23)&1u))&1u)==1);
    uint64_t address=words[1]|(uint64_t)words[2]<<32;
    assert(address>=base && address-base<=bytes && (n-3)*4<=bytes-(address-base));
    size_t at=(size_t)(address-base);
    for(size_t i=3;i<n;i++)for(unsigned b=0;b<4;b++)memory[at++]=(uint8_t)(words[i]>>(8*b));
}
static AdrenoFillRequest request(void)
{return (AdrenoFillRequest){1,AdrenoFillBuffer,0x5a,0,1,3,7,0,4};}
static AdrenoFillView view(void)
{return (AdrenoFillView){1,3,7,UINT64_C(0x123400000),8192,40,7};}
static void refused(AdrenoFillRequest r,AdrenoFillView v,size_t capacity,AdrenoFillStatus expected)
{
    uint32_t words[AdrenoFillMaxWords],before[AdrenoFillMaxWords];AdrenoFillPlan plan,old;size_t n=99;
    memset(words,0xa5,sizeof(words));memcpy(before,words,sizeof(words));memset(&plan,0xa5,sizeof(plan));old=plan;
    assert(AdrenoFillTranslate(&r,&v,words,capacity,&plan,&n)==expected && !n);
    assert(!memcmp(words,before,sizeof(words)) && !memcmp(&plan,&old,sizeof(plan)));
}
int main(void)
{
    const unsigned lengths[]={4,8,12,64,4092,4096},offsets[]={0,4,128,4096};
    for(unsigned byte=0;byte<256;byte++)for(unsigned i=0;i<6;i++)for(unsigned j=0;j<4;j++){
        AdrenoFillRequest r=request();AdrenoFillView v=view();r.value=byte;r.bytes=lengths[i];r.offset=offsets[j];
        uint32_t words[AdrenoFillMaxWords];AdrenoFillPlan plan;size_t n=0;uint8_t memory[8192];memset(memory,0xa5,sizeof(memory));
        assert(AdrenoFillTranslate(&r,&v,words,AdrenoFillMaxWords,&plan,&n)==AdrenoFillPlanned);
        assert(plan.destination==v.gpu_base+r.offset && plan.bytes==r.bytes && plan.requires_completion==1 && plan.packet_words==n);
        AdrenoPM4Packet decoded;size_t packets=0;
        assert(AdrenoPM4Plan(words,n,&decoded,1,&packets)==AdrenoPM4Success && packets==1);
        assert(decoded.type==7 && decoded.identifier==0x3d && decoded.payload_words==r.bytes/4+2 && !decoded.header_word);
        model(words,n,v.gpu_base,memory,sizeof(memory));
        for(size_t p=0;p<sizeof(memory);p++)assert(memory[p]==(p>=r.offset && p-r.offset<r.bytes?byte:0xa5));
    }
    AdrenoFillRequest r=request();AdrenoFillView v=view();r.bytes=4096;
    for(size_t cap=0;cap<AdrenoFillMaxWords;cap++)refused(r,v,cap,AdrenoFillCapacity);
    for(unsigned fault=0;fault<19;fault++){
        r=request();v=view();AdrenoFillStatus status;
        switch(fault){
        case 0:r.version=2;status=AdrenoFillUnsupported;break;
        case 1:r.operation=2;status=AdrenoFillUnsupported;break;
        case 2:r.value=256;status=AdrenoFillUnsupported;break;
        case 3:r.reserved=1;status=AdrenoFillUnsupported;break;
        case 4:r.bytes=0;status=AdrenoFillUnsupported;break;
        case 5:r.bytes=4100;status=AdrenoFillUnsupported;break;
        case 6:r.offset=1;status=AdrenoFillUnsupported;break;
        case 7:r.bytes=3;status=AdrenoFillUnsupported;break;
        case 8:r.buffer_id=2;status=AdrenoFillResource;break;
        case 9:r.generation=2;status=AdrenoFillResource;break;
        case 10:r.context_id=8;status=AdrenoFillResource;break;
        case 11:v.flags=3;status=AdrenoFillResource;break;
        case 12:v.flags=15;status=AdrenoFillResource;break;
        case 13:r.offset=8192;status=AdrenoFillRange;break;
        case 14:v.gpu_base=UINT64_MAX-3;status=AdrenoFillRange;break;
        case 15:v.logical_bytes=UINT64_MAX;status=AdrenoFillRange;break;
        case 16:v.gpu_base++;status=AdrenoFillRange;break;
        case 17:v.address_bits=49;status=AdrenoFillUnsupported;break;
        default:v.gpu_base=0;status=AdrenoFillRange;break;
        }
        refused(r,v,AdrenoFillMaxWords,status);
    }
    /* Logical bytes, not native CPU backing: a 4KiB BO never exposes padding. */
    r=request();v=view();v.logical_bytes=4096;r.offset=4096;
    refused(r,v,AdrenoFillMaxWords,AdrenoFillRange);
    /* Last DWORD below a 32-bit IOVA limit is valid; crossing it is not. */
    r=request();v=view();v.address_bits=32;v.gpu_base=UINT64_C(0xfffffffc);v.logical_bytes=4;
    uint32_t words[4];AdrenoFillPlan plan;size_t n=0;
    assert(AdrenoFillTranslate(&r,&v,words,4,&plan,&n)==AdrenoFillPlanned && words[1]==0xfffffffc && !words[2]);
    assert(words[0]==0x703d8003 && words[3]==0x5a5a5a5a);
    v.logical_bytes=8;refused(r,v,4,AdrenoFillRange);
    const unsigned widths[]={32,36,40,42,44,48};
    for(unsigned i=0;i<6;i++){
        r=request();v=view();v.address_bits=widths[i];v.gpu_base=(UINT64_C(1)<<widths[i])-4;v.logical_bytes=4;
        assert(AdrenoFillTranslate(&r,&v,words,4,&plan,&n)==AdrenoFillPlanned);
        assert((words[1]|(uint64_t)words[2]<<32)==v.gpu_base);
        r.offset=4;refused(r,v,4,AdrenoFillRange);
    }
    r=request();v=view();
    assert(AdrenoFillTranslate(NULL,&v,words,4,&plan,&n)==AdrenoFillInvalid && !n);
    assert(AdrenoFillTranslate(&r,NULL,words,4,&plan,&n)==AdrenoFillInvalid && !n);
    assert(AdrenoFillTranslate(&r,&v,NULL,4,&plan,&n)==AdrenoFillInvalid && !n);
    assert(AdrenoFillTranslate(&r,&v,words,4,NULL,&n)==AdrenoFillInvalid && !n);
    assert(AdrenoFillTranslate(&r,&v,words,SIZE_MAX,&plan,&n)==AdrenoFillInvalid && !n);
    assert(AdrenoFillTranslate(&r,&v,words,4,&plan,NULL)==AdrenoFillInvalid);
    puts("Fill lowering: 6144 real PM4 decoder + independent memory-oracle cases; capacity, identity, range, six address widths and invalid inputs PASS; no GPU execution");
}
