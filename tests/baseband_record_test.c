#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_baseband_record.h"
static struct vbb_record initial(void)
{
    struct vbb_record r={0};r.sequence=1;r.state=VBB_REC_BEGIN;
    for(unsigned i=0;i<16;i++)r.run[i]=(uint8_t)(i+1);
    for(unsigned i=0;i<32;i++)r.image_sha256[i]=(uint8_t)(i+33);
    return r;
}
static void frame(const struct vbb_record *r)
{
    uint8_t p[128];assert(vbb_record_encode(r,p,sizeof(p)));
    fputs("VBBR1 ",stdout);for(unsigned i=0;i<128;i++)printf("%02x",p[i]);putchar('\n');
}
int main(int argc,char **argv)
{
    (void)argv;
    struct vbb_record r=initial(),out;uint8_t p[128],bad[128];
    assert(vbb_record_crc((const uint8_t *)"123456789",9)==0xcbf43926);
    assert(vbb_record_encode(&r,p,128) && vbb_record_decode(p,128,&out));
    assert(out.sequence==1 && !memcmp(out.run,r.run,16) && !memcmp(out.image_sha256,r.image_sha256,32));
    for(unsigned n=0;n<128;n++)assert(!vbb_record_decode(p,n,&out) && !out.sequence);
    assert(!vbb_record_decode(p,129,&out));
    for(unsigned i=0;i<128;i++)for(unsigned b=0;b<8;b++) {
        memcpy(bad,p,128);bad[i]^=(uint8_t)(1u<<b);
        assert(!vbb_record_decode(bad,128,&out) && !out.sequence);
    }
    memcpy(bad,p,128);bad[112]=1;vbb_put32(bad+124,vbb_record_crc(bad,124));
    assert(!vbb_record_decode(bad,128,&out));
    r.state=VBB_REC_FINISHED;r.sequence=2;r.phase=1;
    assert(!vbb_record_encode(&r,bad,128));
    for(unsigned i=0;i<128;i++)assert(!bad[i]);
    r.phase=5;assert(vbb_record_encode(&r,bad,128));
    r=initial();r.sequence=2;r.state=VBB_REC_BEFORE_READ;r.phase=1;r.address=0x81eff000;r.length=32;
    assert(vbb_record_encode(&r,bad,128));
    r.address=UINT64_MAX-7;assert(!vbb_record_valid(&r));
    r.address=0x81effff8;assert(!vbb_record_valid(&r));
    r.address=0x81eff000;r.length=7;assert(!vbb_record_valid(&r));
    r.length=32;r.flags=VBB_REC_EXCEPTION;assert(!vbb_record_valid(&r));
    r.state=VBB_REC_ABORT;r.esr=0x96000010;r.vector=4;r.far=0x81eff008;
    assert(!vbb_record_valid(&r));r.flags|=VBB_REC_FAR_VALID;
    assert(vbb_record_encode(&r,bad,128) && vbb_record_decode(bad,128,&out));
    assert(out.esr==r.esr && out.far==r.far && out.vector==4);
    r.vector=16;assert(!vbb_record_valid(&r));
    assert(!vbb_record_encode(NULL,bad,128));assert(!vbb_record_encode(&r,NULL,128));
    assert(!vbb_record_decode(NULL,128,&out));assert(!vbb_record_decode(p,128,NULL));
    uint32_t seed=17;
    for(unsigned i=0;i<100000;i++) {
        memcpy(bad,p,128);seed=1664525u*seed+1013904223u;unsigned pos=seed%124;
        seed=1664525u*seed+1013904223u;bad[pos]^=(uint8_t)(1+(seed%255));
        if(i&1)vbb_put32(bad+124,vbb_record_crc(bad,124));
        if(vbb_record_decode(bad,128,&out))assert(vbb_record_valid(&out));
        else assert(!out.sequence);
    }
    if(argc>1) {
        r=initial();frame(&r);
        r.sequence=2;r.state=VBB_REC_BEFORE_READ;r.phase=1;r.address=0x81eff000;r.length=32;frame(&r);
        r.sequence=3;r.state=VBB_REC_ABORT;r.esr=0x96000010;r.vector=4;
        r.flags=VBB_REC_EXCEPTION|VBB_REC_FAR_VALID;r.far=r.address;frame(&r);
    } else puts("Record codec: CRC, truncation, state constraints, exception and100000 mutations PASS");
    return 0;
}
