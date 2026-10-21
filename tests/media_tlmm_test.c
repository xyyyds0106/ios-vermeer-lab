#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_media_tlmm.h"
struct owner { unsigned flags,reads,fail_at,revoke_at,zero,clock_calls; uint64_t base; };
static uint64_t now(void *ctx){struct owner *o=ctx;return 100+o->clock_calls++;}
static uint64_t backwards(void *ctx){struct owner *o=ctx;return 100-o->clock_calls++;}
static unsigned proof(void *ctx){return ((struct owner *)ctx)->flags;}
static int read32(void *ctx,unsigned offset,uint32_t *value)
{
    struct owner *o=ctx;++o->reads;
    uint64_t pa=o->base+offset;
    unsigned i;
    for(i=0;i<16;++i)if(pa==VMT_TLMM_BASE+vmt_offsets[i])break;
    assert(i<16); /* Any invented register, including UART, fails the model. */
    if(o->reads==o->fail_at)return 0;
    if(o->reads==o->revoke_at)o->flags=0;
    *value=o->zero?0:(uint32_t)pa;return 1;
}
static struct vmt_access access(struct owner *o)
{
    struct vmt_access a={.physical_start=o->base,.bytes=0x30000,.ctx=o,
                        .read32=read32,.proof=proof,.now_us=now};return a;
}
int main(int argc,char **argv)
{
    (void)argv;
    struct owner o={.flags=31,.base=0x0f150000};struct vmt_access a=access(&o);
    struct vmt_record r;char line[VMT_RECORD_CAPACITY];
    assert(vmt_capture(&a,0xffff,1,&r)==VMT_OK && o.reads==16 && r.valid==0xffff);
    assert(r.start_us==100 && r.end_us==101 && r.failed_index==VMT_NO_FAILURE);
    for(unsigned i=0;i<16;++i)assert(vmt_names[i] && r.values[i]==VMT_TLMM_BASE+vmt_offsets[i]);
    size_t n=vmt_encode(&r,line,sizeof(line));assert(n && n<sizeof(line) && line[n-1]=='\n');
    if(argc>1)fputs(line,stdout); /* Pipeline fixture only; no device I/O. */
    for(size_t cap=0;cap<=n;++cap){
        char small[VMT_RECORD_CAPACITY];memset(small,'!',sizeof(small));
        assert(!vmt_encode(&r,small,cap) && small[0]=='!');
    }
    for(unsigned mask=0;mask<31;++mask){
        o=(struct owner){.flags=mask,.base=0x0f150000};a=access(&o);
        assert(vmt_capture(&a,VMT_BT_FIELDS,2,&r)==VMT_GATED && !o.reads && !r.valid);
        assert(vmt_encode(&r,line,sizeof(line)) && strstr(line,"data=--------,"));
    }
    o=(struct owner){.flags=31,.base=0x0f100000};a=access(&o);
    assert(vmt_capture(&a,VMT_BT_FIELDS,3,&r)==VMT_BOUNDS && !o.reads);
    o=(struct owner){.flags=31,.base=0x0f150000};a=access(&o);a.bytes=0x4000;
    assert(vmt_capture(&a,VMT_BT_FIELDS,4,&r)==VMT_OK && o.reads==8 && r.valid==255);
    o.reads=0;
    assert(vmt_capture(&a,0xffff,5,&r)==VMT_BOUNDS && !o.reads && !r.valid);
    o=(struct owner){.flags=31,.base=0x0f151000};a=access(&o);a.bytes=0x2007;
    assert(vmt_capture(&a,VMT_BT_FIELDS,51,&r)==VMT_BOUNDS && !o.reads);
    a.bytes=0x2008; /* Last input register ends EXACTLY at mapping end. */
    assert(vmt_capture(&a,VMT_BT_FIELDS,52,&r)==VMT_OK && o.reads==8);
    o=(struct owner){.flags=31,.base=0x00898000};a=access(&o);a.bytes=0x4000;
    assert(vmt_capture(&a,VMT_BT_FIELDS,53,&r)==VMT_BOUNDS && !o.reads);
    o=(struct owner){.flags=31,.base=0x0f150001};a=access(&o);
    assert(vmt_capture(&a,VMT_BT_FIELDS,54,&r)==VMT_BOUNDS && !o.reads);
    o=(struct owner){.flags=31,.base=0x0f174000};a=access(&o);a.bytes=0x8000;
    assert(vmt_capture(&a,VMT_CAMERA_FIELDS,6,&r)==VMT_OK && o.reads==8 && r.valid==0xff00);
    for(unsigned fail=1;fail<=16;++fail){
        o=(struct owner){.flags=31,.base=0x0f150000,.fail_at=fail};a=access(&o);
        assert(vmt_capture(&a,0xffff,7,&r)==VMT_READ_FAILED && o.reads==fail);
        assert(r.valid==((1u<<(fail-1))-1) && r.failed_index==fail-1);
        o=(struct owner){.flags=31,.base=0x0f150000,.revoke_at=fail};a=access(&o);
        assert(vmt_capture(&a,0xffff,8,&r)==VMT_REVOKED && o.reads==fail);
        assert(r.valid==((1u<<(fail-1))-1) && r.failed_index==fail-1);
    }
    o=(struct owner){.flags=31,.base=0x0f150000,.zero=1};a=access(&o);
    assert(vmt_capture(&a,VMT_BT_FIELDS,9,&r)==VMT_OK);
    assert(vmt_encode(&r,line,sizeof(line)) && strstr(line,"data=00000000,00000000,"));
    assert(strstr(line,"--------")); /* Camera missing while BT zeros are valid. */
    o=(struct owner){.flags=31,.base=0x0f150000};a=access(&o);a.now_us=backwards;
    assert(vmt_capture(&a,VMT_BT_FIELDS,10,&r)==VMT_CLOCK_FAILED);
    o=(struct owner){.flags=31,.base=0x0f150000};a=access(&o);
    assert(vmt_capture(&a,0x10000,11,&r)==VMT_BAD_ARGUMENT && !o.reads);
    a.physical_start=UINT64_MAX-3;
    assert(vmt_capture(&a,VMT_BT_FIELDS,12,&r)==VMT_BOUNDS && !o.reads);
    a=access(&o);a.bytes=UINT64_MAX;
    assert(vmt_capture(&a,VMT_BT_FIELDS,13,&r)==VMT_BOUNDS && !o.reads);
    puts("TLMM-only CDC capture bounds/proofs/revocation/validity/encoding: PASS");
    return 0;
}
