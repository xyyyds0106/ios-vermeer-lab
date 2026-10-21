#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_touch_clock.h"
struct fixture {unsigned calls,reads,fail,mode;uint64_t now;struct vtc_dfs_snapshot s;};
static int valid(void *ctx)
{struct fixture *f=ctx;return !(f->mode==1 && ++f->calls==f->fail);}
static int now_us(void *ctx,uint64_t *out)
{
    struct fixture *f=ctx;
    if(f->mode==3 && ++f->calls==f->fail)return 0;
    if(f->mode==4 && ++f->calls==f->fail){*out=0;return 1;}
    if(f->mode==5 && ++f->calls==f->fail)f->now+=20000;
    *out=++f->now;return 1;
}
static int rd(void *ctx,unsigned off,uint32_t *out)
{
    struct fixture *f=ctx;assert(off==0x14 || (off>=0x1c && off<=0x38 && !(off&3)));
    ++f->reads;
    if(f->mode==2 && f->reads==f->fail)return 0;
    *out=off==0x14?f->s.control:f->s.cfg[(off-0x1c)/4];
    if(f->mode==6 && f->reads==f->fail)*out^=0x100;
    return 1;
}
static void dfs(void)
{
    struct vtc_dfs_snapshot s={.control=0x21};struct vtc_gpi_clock p={0},before;
    for(unsigned i=0;i<8;i++)s.cfg[i]=0x100101;
    before=p;assert(!vtc_dfs_choose(&s,&p) && !memcmp(&p,&before,sizeof p));
    for(unsigned level=0;level<8;level++)for(unsigned hid=0;hid<2;hid++) {
        s.cfg[level]=0x100000|hid;assert(vtc_dfs_choose(&s,&p));
        assert(p.level==level && p.divider==128 && p.source_hz==19200000 && p.spi_hz==150000);
        assert(vtc_dfs_live(&s,&p,0x2f,s.cfg[level]));
        assert(!vtc_dfs_live(&s,&p,0x31,s.cfg[level])); /* unsupported level8 */
        assert(!vtc_dfs_live(&s,&p,0x20,s.cfg[level]));
        assert(!vtc_dfs_live(&s,&p,0x21,s.cfg[level]^0x100));
        for(unsigned bit=0;bit<32;bit++)if(bit!=20 && bit!=0) {
            s.cfg[level]=0x100000|(1u<<bit);before=p;
            assert(!vtc_dfs_choose(&s,&p) && !memcmp(&p,&before,sizeof p));
        }
        s.cfg[level]=0x100101;
    }
    struct vtc_dfs_snapshot output,sentinel;memset(&sentinel,0xa5,sizeof sentinel);
    struct fixture f={.now=100,.s=s};struct vtc_dfs_ops ops={&f,valid,rd,now_us};
    assert(!vtc_dfs_collect(&ops,&output) && f.reads==18 && !memcmp(&output,&s,sizeof s));
    for(unsigned mode=1;mode<=6;mode++) {
        unsigned limit=mode==1?73:mode==2?18:mode<=5?37:18;
        for(unsigned at=(mode==4 || mode==5)?2:1;at<=limit;at++) {
            f=(struct fixture){.now=100,.s=s,.mode=mode,.fail=at};output=sentinel;
            assert(vtc_dfs_collect(&ops,&output)!=VTC_DFS_OK);
            assert(!memcmp(&output,&sentinel,sizeof output));
        }
    }
    f=(struct fixture){.now=100,.s=s};f.s.control=0;output=sentinel;
    assert(vtc_dfs_collect(&ops,&output)==VTC_DFS_CHANGED && f.reads==1);
    assert(vtc_dfs_collect(NULL,&output)==VTC_DFS_ARGUMENT);
    puts("AP DFS: actual XO level at every index, no PLL/MND fallback, stable bounded two-pass reads and all lease/IO/clock/change cutoffs PASS");
}
int main(void)
{
    assert(vtc_ready(VTC_VOTES,0,0,0,0,0));
    assert(vtc_ready(VTC_VOTES,0,0,0x80000002u,0x80000002u,0));
    assert(vtc_ready(VTC_VOTES,0,0,0x80000002u,0,0));
    assert(!vtc_ready(VTC_VOTES,0,0,0x80000000u,0,0));
    assert(!vtc_ready(VTC_VOTES,0,0,0,0x80000000u,0));
    assert(!vtc_ready(VTC_VOTES,0x80000002u,0,0,0,0));
    assert(!vtc_ready(VTC_VOTES,0,0x80000002u,0,0,0));
    /* HWCG interpretation applies ONLY to the two AHB branches. */
    assert(!vtc_ready(VTC_VOTES,0,0,0x80000002u,0x80000002u,0x80000002u));
    for(unsigned bit=0;bit<32;bit++)if(VTC_VOTES&(1u<<bit))
        assert(!vtc_ready(VTC_VOTES&~(1u<<bit),0,0,2,2,0));
    assert(vtc_ready(0xffffffffu,0,0,2,2,0));
    dfs();puts("Clock votes, hardware-gated AHB idle and strict SE4 readiness: PASS");
}
