#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_touch_iommu.h"
enum {NORMAL,IO_FAIL,CLEAN_FAIL,TLB_STUCK,READBACK_BAD,LEASE_LOST,CLOCK_BACKWARD};
struct fixture {
    struct vti v;uint32_t reg[0xd0000/4];uint64_t now;
    unsigned mode,calls,fail_at,reads,writes,cleaned;
};
static int guard(void *ctx)
{struct fixture *f=ctx;return !(f->mode==LEASE_LOST && f->calls>=f->fail_at);}
static int io_fail(struct fixture *f)
{return ++f->calls==f->fail_at && f->mode==IO_FAIL;}
static int rd32(void *ctx,unsigned off,uint32_t *out)
{
    struct fixture *f=ctx;assert(!(off&3) && off<sizeof(f->reg));++f->reads;
    if(io_fail(f))return 0;
    *out=f->reg[off/4];
    if(f->mode==TLB_STUCK && off==0x80000+4096*f->v.bank+0x7f4)*out=1;
    return 1;
}
static int rd64(void *ctx,unsigned off,uint64_t *out)
{
    struct fixture *f=ctx;assert(!(off&7) && off+8<=sizeof(f->reg));++f->reads;
    if(io_fail(f))return 0;
    *out=((uint64_t)f->reg[(off+4)/4]<<32)|f->reg[off/4];return 1;
}
static int allowed(struct fixture *f,unsigned off,unsigned wide)
{
    unsigned bank=f->v.bank,slot=f->v.slot,cb=0x80000+4096*bank;
    assert(bank<70 && slot<105);
    if(wide)return off==cb+0x20 || off==cb+0x28;
    if(off==0x800+4*slot || off==0xc00+4*slot || off==0x1000+4*bank || off==0x1800+4*bank)return 1;
    const unsigned fields[]={0,0x10,0x30,0x38,0x3c,0x58,0x618,0x7f0};
    for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++)if(off==cb+fields[i])return 1;
    return 0;
}
static int wr32(void *ctx,unsigned off,uint32_t value)
{
    struct fixture *f=ctx;assert(allowed(f,off,0));++f->writes;
    assert(f->cleaned);if(io_fail(f))return 0;
    if(off==0x80000+4096*f->v.bank+0x58)f->reg[off/4]&=~value;
    else f->reg[off/4]=value;
    return 1;
}
static int wr64(void *ctx,unsigned off,uint64_t value)
{
    struct fixture *f=ctx;assert(allowed(f,off,1));++f->writes;
    if(io_fail(f))return 0;
    if(f->mode==READBACK_BAD)value^=0x1000;
    f->reg[off/4]=(uint32_t)value;f->reg[(off+4)/4]=(uint32_t)(value>>32);return 1;
}
static int clean(void *ctx,void *cpu,size_t bytes)
{
    struct fixture *f=ctx;assert(cpu==f->v.table_cpu && bytes==16384);++f->cleaned;
    if(io_fail(f) || f->mode==CLEAN_FAIL)return 0;
    return 1;
}
static int now_us(void *ctx,uint64_t *value)
{
    struct fixture *f=ctx;
    if(f->mode==CLOCK_BACKWARD && f->calls>=f->fail_at)*value=f->now-1;
    else *value=++f->now;
    return 1;
}
static int pause_us(void *ctx,unsigned us)
{struct fixture *f=ctx;assert(us==20);f->now+=us;return 1;}
static struct fixture *make(unsigned mode)
{
    struct fixture *f=calloc(1,sizeof(*f));assert(f);f->mode=mode;
    f->v.io=(struct vti_ops){f,guard,rd32,rd64,wr32,wr64,clean,now_us,pause_us};
    f->v.table_cpu=aligned_alloc(16384,16384);assert(f->v.table_cpu);memset(f->v.table_cpu,0xaa,16384);
    f->v.table_pa=UINT64_C(0x8c2000000);
    for(unsigned i=0;i<5;i++)f->v.physical[i]=UINT64_C(0x8c1000000)+16384*i;
    f->reg[0x20/4]=0x4c017e69;f->reg[0x24/4]=0x60000046;f->reg[0x28/4]=0x5111;f->reg[0]=0x280406;
    /* Preserve unrelated USB/UFS routes and both ASIDs of an enabled bank. */
    f->reg[0x800/4]=0x80000040;f->reg[0xc00/4]=0x10000;
    f->reg[0x804/4]=0x80000060;f->reg[0xc04/4]=0x10000;
    f->reg[0x80000/4]=1;f->reg[0x80024/4]=1u<<16;
    f->reg[0x8002c/4]=2u<<16;
    /* A disabled bank69 is still referenced by someone else's stream. */
    f->reg[0x808/4]=0x800000a5;f->reg[0xc08/4]=69;
    return f;
}
static void destroy(struct fixture *f)
{free(f->v.table_cpu);free(f);}
static void verify_tables(struct fixture *f)
{
    uint64_t *table=f->v.table_cpu;uint64_t base=f->v.table_pa;
    assert(f->v.tables.table_pages==3 && f->v.tables.leaf_pages==20 && f->v.tables.start_level==1);
    assert(table[0]==(base+4096+3));assert(table[512]==(base+8192+3));
    assert(!table[1024+255] && !table[1024+276]);
    for(unsigned page=0;page<20;page++) {
        uint64_t leaf=table[1024+256+page];unsigned extent=page/4;
        assert((leaf&UINT64_C(0x0000fffffffff000))==f->v.physical[extent]+4096*(page%4));
        assert((leaf&3)==3 && (leaf&(UINT64_C(3)<<53))==(UINT64_C(3)<<53));
        assert(!!(leaf&(1u<<7))==!(extent==1 || extent==4));
    }
}
static void normal(void)
{
    struct fixture *f=make(NORMAL);assert(vti_prepare(&f->v));
    assert(f->v.bank==68 && f->v.asid==3 && f->v.slot==3 && f->v.attached && !f->v.retired);
    assert(f->reg[0x800/4]==0x80000040 && f->reg[0xc00/4]==0x10000);
    assert(f->reg[0x804/4]==0x80000060 && f->reg[0xc04/4]==0x10000);
    assert(f->reg[0xc08/4]==69 && f->reg[0x80000/4]==1);
    assert(f->reg[0x80c/4]==0x800000b6 && f->reg[0xc0c/4]==68);
    assert(!(f->v.image.sctlr&0x40));verify_tables(f);
    unsigned reads=f->reads,writes=f->writes;assert(!vti_prepare(&f->v));assert(reads==f->reads && writes==f->writes);
    destroy(f);
    f=make(NORMAL);f->reg[0x80c/4]=0x800000b6;f->reg[0xc0c/4]=0x20000;
    assert(vti_prepare(&f->v) && f->v.slot==3);destroy(f);
}
static void rejects(void)
{
    for(unsigned type=0;type<4;type++)if(type!=2) {
        struct fixture *f=make(NORMAL);f->reg[0x80c/4]=0x800000b6;f->reg[0xc0c/4]=type<<16;
        assert(!vti_prepare(&f->v) && !f->writes && f->v.error==VTI_BUSY);destroy(f);
    }
    struct fixture *f=make(NORMAL);f->reg[0x80c/4]=0x80ff00b0;
    assert(!vti_prepare(&f->v) && !f->writes);destroy(f);
    f=make(NORMAL);f->v.physical[1]=f->v.table_pa;
    assert(!vti_prepare(&f->v) && !f->writes && !f->cleaned);destroy(f);
    f=make(NORMAL);f->reg[0x24/4]^=1;
    assert(!vti_prepare(&f->v) && !f->writes);destroy(f);
    f=make(NORMAL);for(unsigned i=0;i<70;i++)f->reg[(0x80000+4096*i)/4]=1;
    assert(!vti_prepare(&f->v) && !f->writes);destroy(f);
}
static void failures(void)
{
    struct fixture *f=make(NORMAL);assert(vti_prepare(&f->v));unsigned calls=f->calls;destroy(f);
    for(unsigned i=1;i<=calls;i++) {
        f=make(IO_FAIL);f->fail_at=i;
        assert(!vti_prepare(&f->v) && f->v.retired && f->calls==i);
        unsigned reads=f->reads,writes=f->writes;assert(!vti_prepare(&f->v));assert(reads==f->reads && writes==f->writes);destroy(f);
    }
    const unsigned modes[]={CLEAN_FAIL,TLB_STUCK,READBACK_BAD,LEASE_LOST,CLOCK_BACKWARD};
    for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);i++) {
        f=make(modes[i]);f->fail_at=10;
        assert(!vti_prepare(&f->v) && f->v.retired && !f->v.attached);
        if(f->v.touched)assert(f->reg[(0xc00+4*f->v.slot)/4]==0x20000);
        destroy(f);
    }
}
int main(void)
{normal();rejects();failures();puts("Touch IOMMU: owned SIDb6 4K tables/context, unrelated routes intact, foreign mapping refusal and every IO failure stop PASS");}
