/* SPDX-License-Identifier: MIT */
#include "K70GpuClock.h"
/* Pinned MiCode 53ef33ea gpucc-kalama.c and gcc-kalama.c; LUCID_OLE sequence
 * cross-checked with pinned public Linux/UniXNU GpuPowerBoard. Register units
 * are byte offsets, GCC physical origin0x100000/GPUCC0x3d90000. */
static int live(struct K70ClockState *s,const struct K70ClockIo *io)
{
    uint64_t t;
    if(s->poisoned || io->now_us(io->cookie,&t) || t<s->last)return s->poisoned=1;
    s->last=t;
    if(t-s->began>=50000 || io->lease(io->cookie))return s->poisoned=1;
    if(io->now_us(io->cookie,&t) || t<s->last)return s->poisoned=1;
    s->last=t;return t-s->began>=50000?(s->poisoned=1):0;
}
static int rd(struct K70ClockState *s,const struct K70ClockIo *io,unsigned w,size_t at,uint32_t *v)
{return live(s,io)||io->read32(io->cookie,w,at,v)||live(s,io);}
static int wr(struct K70ClockState *s,const struct K70ClockIo *io,unsigned w,size_t at,uint32_t v)
{return live(s,io)||io->write32(io->cookie,w,at,v)||live(s,io);}
static int wait(struct K70ClockState *s,const struct K70ClockIo *io,unsigned us)
{return live(s,io)||io->wait_us(io->cookie,us)||live(s,io);}
static int fence(struct K70ClockState *s,const struct K70ClockIo *io)
{return live(s,io)||io->barrier(io->cookie)||live(s,io);}
static int update(struct K70ClockState *s,const struct K70ClockIo *io,unsigned w,size_t at,uint32_t clear,uint32_t set)
{uint32_t v;return rd(s,io,w,at,&v)||wr(s,io,w,at,(v&~clear)|set)||fence(s,io);}
static int poll(struct K70ClockState *s,const struct K70ClockIo *io,unsigned w,size_t at,uint32_t mask,uint32_t expected,unsigned us)
{
    uint64_t began=s->last;
    for(unsigned i=0;i<us+1;++i){
        uint32_t v;
        if(rd(s,io,w,at,&v) || s->last-began>=us)return 1;
        if((v&mask)==expected)return 0;
        if(wait(s,io,1))return 1;
    }
    return 1;
}
static int branch(struct K70ClockState *s,const struct K70ClockIo *io,unsigned w,size_t at,unsigned delay_only)
{return update(s,io,w,at,0,1)||wait(s,io,10)||
    (delay_only?0:poll(s,io,w,at,UINT32_C(1)<<31,0,500));}
static int pll(struct K70ClockState *s,const struct K70ClockIo *io,size_t at,uint32_t l,uint32_t alpha,unsigned enable)
{
    uint32_t mode,op,actual_l,actual_alpha;
    if(rd(s,io,1,at,&mode)||rd(s,io,1,at+4,&op))return 1;
    if((mode&1) && (op&1)){
        /* Never silently treat a different live PLL rate as our boot clock. */
        return rd(s,io,1,at+0x10,&actual_l)||rd(s,io,1,at+0x14,&actual_alpha)||
            (actual_l&0xffff)!=l || actual_alpha!=alpha || !(mode&(UINT32_C(1)<<31));
    }
    const size_t offsets[]={0x10,0x14,0x20,0x24,0x28,0x1c,0x30,0x34,0x38};
    const uint32_t values[]={l|0x44440000,alpha,0x20485699,0x00182261,0x82aa299c,5,3,0x9000,0x34};
    for(unsigned i=0;i<sizeof offsets/sizeof *offsets;++i)
        if(wr(s,io,1,at+offsets[i],values[i]))return 1;
    if(update(s,io,1,at,1,0)||wr(s,io,1,at+4,0)||update(s,io,1,at,0,4))return 1;
    if(!enable)return 0;
    return wr(s,io,1,at+4,1)||fence(s,io)||poll(s,io,1,at,UINT32_C(1)<<31,UINT32_C(1)<<31,1500)||
        update(s,io,1,at+0x18,0,7)||update(s,io,1,at,0,1);
}
static int rcg(struct K70ClockState *s,const struct K70ClockIo *io,size_t at,unsigned select,unsigned div)
{
    if(update(s,io,1,at,0,2)||poll(s,io,1,at,UINT32_C(1)<<31,0,500)||
       update(s,io,1,at+4,0x10371f,(select<<8)|div)||update(s,io,1,at,0,1)||
       poll(s,io,1,at,1,0,500)||update(s,io,1,at,2,0))return 1;
    uint32_t v;
    return rd(s,io,1,at+4,&v)||((v&0x10371f)!=((select<<8)|div));
}
int K70GpuClockStart(struct K70ClockState *s,const struct K70ClockIo *io)
{
    if(!s||!io||!io->lease||!io->now_us||!io->wait_us||!io->read32||!io->write32||!io->barrier)return 1;
    if(s->used)return 1;
    s->used=1;
    if(io->now_us(io->cookie,&s->began))goto failed;
    s->last=s->began;
    s->phase=1;
    if(live(s,io)||branch(s,io,0,0x71004,1))goto failed;
    s->phase=2;
    /* gdsc_enable CX: SW_COLLAPSE clear, actual HWCTRL PWR_ON poll, then
     * retention. GX remains entirely under the real GMU OOB protocol. */
    if(update(s,io,1,0x9108,1,0)||wait(s,io,1)||poll(s,io,1,0x953c,UINT32_C(1)<<31,UINT32_C(1)<<31,2000)||
       update(s,io,1,0x9108,0,(1u<<14)|(1u<<13))||wait(s,io,1)||
       update(s,io,1,0x9108,0,1u<<11)||wait(s,io,1)||branch(s,io,1,0x911c,1)||branch(s,io,1,0x7000,0))goto failed;
    s->phase=3;
    if(update(s,io,0,0x52000,0,(1u<<15)|(1u<<16))||wait(s,io,10)||
       pll(s,io,0,0x1e,0xbaaa,0)||pll(s,io,0x1000,0x16,0xeaaa,1))goto failed;
    s->phase=4;
    if(rcg(s,io,0x93ec,5,5)||rcg(s,io,0x9318,3,3))goto failed;
    s->phase=5;
    const size_t cc[]={0x9004,0x900c,0x911c,0x913c,0x9144,0x9148};
    for(unsigned i=0;i<sizeof cc/sizeof *cc;++i)if(branch(s,io,1,cc[i],cc[i]==0x911c))goto failed;
    if(branch(s,io,0,0x71154,0)||branch(s,io,0,0x71010,0)||branch(s,io,0,0x71018,0)||live(s,io))goto failed;
    s->phase=6;s->ready=1;return 0;
failed:s->poisoned=1;s->ready=0;return 1;
}
