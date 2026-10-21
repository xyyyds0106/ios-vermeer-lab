/* SPDX-License-Identifier: MIT
 * Serialized K70 GPII0 SPI transport. Register/wire facts: pinned MiCode
 * 53ef33ea msm_gpi.c/msm_gpi_mmio.h. Original polling implementation.
 * No guessed IOVA, takeover/reset, fabricated event or implicit DMA release.
 */
#ifndef VERMEER_GPI_TRANSPORT_H
#define VERMEER_GPI_TRANSPORT_H
#include "vermeer_gpi.h"
enum { VGPT_TX,VGPT_RX,VGPT_TR,VGPT_RR,VGPT_ER,VGPT_PAGES };
enum { VGPT_CLEAN,VGPT_CLEAN_INVALIDATE,VGPT_INVALIDATE };
enum { VGPT_OK,VGPT_INVALID,VGPT_LEASE,VGPT_IO,VGPT_STATE,VGPT_TIMEOUT,
       VGPT_CACHE,VGPT_EVENT,VGPT_CLOCK,VGPT_RETIRED };
enum { VGPT_NEW,VGPT_STARTING,VGPT_READY,VGPT_TRANSFER,VGPT_QUARANTINE };
enum {VGPT_TRACE_POINTERS=1,VGPT_TRACE_COPY,VGPT_TRACE_CACHE_TX,VGPT_TRACE_CACHE_RX,
      VGPT_TRACE_CACHE_TR,VGPT_TRACE_CACHE_RR,VGPT_TRACE_RX_DB,VGPT_TRACE_TX_DB,
      VGPT_TRACE_POLL,VGPT_TRACE_EVENTS,VGPT_TRACE_RX_ACQUIRE,VGPT_TRACE_COMPLETE};
/* The exact CURRENT boot FDT must first match board_contract.h, including
 * GIC cells/parent and the COMPLETE Apps-SMMU/GPI/SE4 interrupts properties.
 * These are read-only GICD offsets; no mask/clear/routing operation exists.
 * Sources: GPII0 SPI279, SE4 SPI357, Apps global SPI65, context index bank+1. */
struct vgpt_irq_observation {unsigned source,intid,offset;uint32_t enabled,active;};
static inline int vgpt_irqs_masked(void *ctx,int (*read)(void *,unsigned,uint32_t *),
    unsigned bank,struct vgpt_irq_observation *out)
{
    static const unsigned context_spi[70]={97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,
        113,114,115,116,117,118,181,182,183,184,185,186,187,188,189,190,191,192,315,316,317,318,
        319,320,321,322,323,324,325,326,327,328,329,330,331,332,333,334,335,336,337,338,
        339,340,341,342,343,344,345,395,396,397,398,399};
    struct vgpt_irq_observation r={0};uint32_t typer=0;
    if(!read || !out || bank>70)return 0;
    r.offset=4;
    if(!read(ctx,4,&typer)){*out=r;return 0;}
    unsigned implemented=32*((typer&31)+1),spi[4]={279,357,65,bank<70?context_spi[bank]:0};
    for(unsigned i=0;i<(bank<70?4u:3u);i++) {
        r=(struct vgpt_irq_observation){.source=i+1,.intid=spi[i]+32};
        if(r.intid>=implemented){*out=r;return 0;}
        r.offset=0x100+4*(r.intid/32);
        if(!read(ctx,r.offset,&r.enabled)){*out=r;return 0;}
        r.offset=0x300+4*(r.intid/32);
        if(!read(ctx,r.offset,&r.active)){*out=r;return 0;}
        if((r.enabled|r.active)&(1u<<(r.intid%32))){*out=r;return 0;}
    }
    *out=(struct vgpt_irq_observation){0};return 1;
}
struct vgpt_page {void *cpu;uint64_t iova;};
struct vgpt_ops {
    void *ctx;
    int (*lease)(void *); /* live board, clocks, owned pages AND SIDb6 mapping */
    int (*read)(void *,unsigned,uint32_t *);
    int (*write)(void *,unsigned,uint32_t);
    int (*cache)(void *,void *,size_t,unsigned); /* synchronous whole CPU page */
    int (*now_us)(void *,uint64_t *); /* current monotonic time, read-only */
    int (*pause_us)(void *,unsigned);
    int (*serial_idle)(void *); /* 1 idle, 0 active, -1 fault */
    void (*checkpoint)(void *,unsigned); /* optional first-exchange RAM trace, no MMIO */
};
struct vgpt {
    struct vgpt_ops io;struct vgpt_page page[VGPT_PAGES];
    unsigned phase,active_phase,error,quarantined,hardware_touched,tx_wp,rx_wp,ev_rp;
    unsigned last_channel,last_code,last_status,last_length,events,commands;
    unsigned checkpoint,exchanges;
    unsigned verify_register;uint32_t verify_expected,verify_observed,verify_mask;
    uint64_t begin,last,budget,command_begin;unsigned command_budget;
};
static inline int vgpt_fail(struct vgpt *g,unsigned error)
{g->error=error;g->quarantined=1;g->phase=g->active_phase=VGPT_QUARANTINE;return 0;}
static inline int vgpt_guard(struct vgpt *g)
{
    uint64_t now;unsigned phase=g->phase;
    if(g->quarantined)return 0;
    if(phase<VGPT_STARTING || phase>VGPT_TRANSFER || phase!=g->active_phase)return vgpt_fail(g,VGPT_STATE);
    if(g->io.lease(g->io.ctx)!=1)return vgpt_fail(g,VGPT_LEASE);
    if(g->io.now_us(g->io.ctx,&now)!=1 || now<g->last)return vgpt_fail(g,VGPT_CLOCK);
    g->last=now;
    if(now-g->begin>=g->budget)return vgpt_fail(g,VGPT_TIMEOUT);
    if(g->command_budget && now-g->command_begin>=g->command_budget)return vgpt_fail(g,VGPT_TIMEOUT);
    if(g->quarantined || g->phase!=phase)return vgpt_fail(g,VGPT_RETIRED);
    return 1;
}
static inline int vgpt_rd(struct vgpt *g,unsigned reg,uint32_t *out)
{
    if(!vgpt_guard(g))return 0;
    if(g->io.read(g->io.ctx,reg,out)!=1)return vgpt_fail(g,VGPT_IO);
    return vgpt_guard(g);
}
static inline int vgpt_wr(struct vgpt *g,unsigned reg,uint32_t value)
{
    if(!vgpt_guard(g))return 0;
    g->hardware_touched=1;
    if(g->io.write(g->io.ctx,reg,value)!=1)return g->quarantined?0:vgpt_fail(g,VGPT_IO);
    return vgpt_guard(g);
}
static inline int vgpt_cache(struct vgpt *g,unsigned page,unsigned op)
{
    if(!vgpt_guard(g))return 0;
    if(g->io.cache(g->io.ctx,g->page[page].cpu,16384,op)!=1)return vgpt_fail(g,VGPT_CACHE);
    return vgpt_guard(g);
}
static inline int vgpt_pause(struct vgpt *g)
{
    if(!vgpt_guard(g))return 0;
    if(g->io.pause_us(g->io.ctx,20)!=1)return vgpt_fail(g,VGPT_IO);
    return vgpt_guard(g);
}
static inline int vgpt_mark(struct vgpt *g,unsigned checkpoint)
{
    g->checkpoint=checkpoint;
    if(!g->exchanges && g->io.checkpoint)g->io.checkpoint(g->io.ctx,checkpoint);
    return vgpt_guard(g);
}
static inline int vgpt_valid(const struct vgpt *g)
{
    if(!g || !g->io.lease || !g->io.read || !g->io.write || !g->io.cache ||
       !g->io.now_us || !g->io.pause_us || !g->io.serial_idle)return 0;
    for(unsigned i=0;i<VGPT_PAGES;i++) {
        uintptr_t cpu=(uintptr_t)g->page[i].cpu;uint64_t va=g->page[i].iova;
        if(!cpu || !va || ((cpu|va)&16383) || cpu>UINTPTR_MAX-16384 ||
           va>=(UINT64_C(1)<<40) || 16384>(UINT64_C(1)<<40)-va)return 0;
        for(unsigned j=0;j<i;j++)if(g->page[j].cpu==g->page[i].cpu || g->page[j].iova==va)return 0;
    }
    return 1;
}
static inline int vgpt_errors(struct vgpt *g)
{
    uint32_t global,general;
    if(!vgpt_rd(g,0x13100,&global) || !vgpt_rd(g,0x13118,&general))return 0;
    return !(global|general) || vgpt_fail(g,VGPT_EVENT);
}
static inline int vgpt_command(struct vgpt *g,unsigned channel,unsigned opcode,unsigned expected)
{
    if(channel>2 || opcode>1 || (channel==2 && opcode) || expected!=opcode+1)return vgpt_fail(g,VGPT_INVALID);
    unsigned event=channel==2,context=event?0x11000:0x10000+channel*0x80;
    unsigned irq=event?0x13094:0x13090,clear=event?0x130a4:0x130a0;
    uint32_t bit=1u<<(event?0:channel),pending,state;
    g->command_begin=g->last;g->command_budget=250000;
    if(!vgpt_rd(g,context,&state))return 0;
    if(((state>>20)&15)!=opcode)return vgpt_fail(g,VGPT_STATE);
    if(!vgpt_wr(g,clear,bit) || !vgpt_wr(g,event?0x13010:0x13008,
        (opcode<<24)|(event?0:channel)))return 0;
    ++g->commands;
    for(unsigned i=0;i<12500;i++) {
        if(!vgpt_errors(g) || !vgpt_rd(g,irq,&pending) || !vgpt_rd(g,context,&state))return 0;
        unsigned observed=(state>>20)&15;
        if(observed==expected) {
            /* Poll the real 0->1 allocation / 1->2 start transition. CPU
             * delivery is masked; completion does not require an IRQ latch
             * to propagate through that mask. No same-state "ACK" allowed. */
            if(!vgpt_wr(g,clear,bit))return 0;
            g->command_budget=0;return 1;
        }
        if(observed!=opcode)return vgpt_fail(g,VGPT_STATE);
        if(!vgpt_pause(g))return 0;
    }
    return vgpt_fail(g,VGPT_TIMEOUT);
}
static inline int vgpt_pointer(struct vgpt *g,unsigned reg,unsigned page,unsigned bytes,unsigned *index)
{
    uint32_t hi,lo,again;
    if(!vgpt_rd(g,reg+4,&hi) || !vgpt_rd(g,reg,&lo) || !vgpt_rd(g,reg+4,&again))return 0;
    uint64_t ptr=((uint64_t)hi<<32)|lo,base=g->page[page].iova;
    if(hi!=again || ptr<base || ptr-base>=bytes || ((ptr-base)&15))return vgpt_fail(g,VGPT_STATE);
    *index=(unsigned)((ptr-base)/16);return 1;
}
static inline int vgpt_expect(struct vgpt *g,unsigned reg,uint32_t expected,uint32_t mask)
{
    uint32_t observed;
    if(!vgpt_rd(g,reg,&observed))return 0;
    if((observed&mask)==(expected&mask))return 1;
    g->verify_register=reg;g->verify_expected=expected;g->verify_observed=observed;g->verify_mask=mask;
    return vgpt_fail(g,VGPT_STATE);
}
static inline int vgpt_context(struct vgpt *g,unsigned channel,unsigned state)
{
    if(channel>2 || state<1 || state>2)return vgpt_fail(g,VGPT_INVALID);
    unsigned event=channel==2,base=event?0x11000:0x10000+channel*0x80;
    unsigned page=event?VGPT_ER:channel?VGPT_RR:VGPT_TR;
    uint64_t addr=g->page[page].iova;
    uint32_t cfg=0x10000002|(event?0x10000:channel?0:8)|(state<<20);
    /* EE/CHID are read-only identity fields. Check every documented writable
     * CONFIG field plus real allocation/start state, length and both base
     * halves. Never read a write-only doorbell to verify it. */
    uint32_t mask=event?0xfff1000f:0xfff7c00f;
    return vgpt_expect(g,base,cfg,mask) && vgpt_expect(g,base+4,event?4096:1024,UINT32_MAX) &&
        vgpt_expect(g,base+8,(uint32_t)addr,UINT32_MAX) &&
        vgpt_expect(g,base+12,(uint32_t)(addr>>32),UINT32_MAX);
}
static inline int vgpt_start(struct vgpt *g)
{
    if(!vgpt_valid(g) || g->phase!=VGPT_NEW || g->quarantined)return 0;
    if(g->io.now_us(g->io.ctx,&g->last)!=1)return vgpt_fail(g,VGPT_CLOCK);
    g->begin=g->last;g->budget=1500000;g->phase=g->active_phase=VGPT_STARTING;
    /* Dedicated static GPII0 only: refuse allocated/started/error channels.
     * A platform lease must additionally exclude firmware/other SW users. */
    for(unsigned i=0;i<3;i++) {
        uint32_t cfg;unsigned off=i==2?0x11000:0x10000+0x80*i;
        if(!vgpt_rd(g,off,&cfg))return 0;
        if((cfg>>20)&15)return vgpt_fail(g,VGPT_STATE);
    }
    if(!vgpt_errors(g))return 0;
    for(unsigned p=0;p<VGPT_PAGES;p++) {
        if(!vgpt_guard(g))return 0;
        uint8_t *bytes=g->page[p].cpu;
        for(unsigned j=0;j<16384;j++)bytes[j]=0;
        if(!vgpt_cache(g,p,VGPT_CLEAN_INVALIDATE))return 0;
    }
    /* Source latches enabled for polling; GPI interrupt delivery is masked.
     * No GIC handler/route or unrelated GPI instance is changed. */
    static const unsigned settings[][2]={{0x13088,0},{0x13098,3},{0x1309c,1},
        {0x130b8,1},{0x13108,1},{0x13120,15},{0x13180,1},{0x13188,0},{0x1318c,0}};
    for(unsigned i=0;i<sizeof(settings)/sizeof(settings[0]);i++)
        if(!vgpt_wr(g,settings[i][0],settings[i][1]))return 0;
    if(!vgpt_command(g,2,0,1))return 0;
    uint64_t ev=g->page[VGPT_ER].iova;
    /* DT ev-factor=2 means 64 << 2 = 256 events, NOT 64*2. */
    const unsigned evregs[][2]={{0x11000,0x10010002},{0x11004,4096},
        {0x11008,(uint32_t)ev},{0x1100c,(uint32_t)(ev>>32)},
        {0x12104,(uint32_t)(ev>>32)},{0x11020,0},{0x11024,0},
        {0x11028,0},{0x1102c,0},{0x11030,0},{0x11034,0}};
    for(unsigned i=0;i<sizeof(evregs)/sizeof(evregs[0]);i++)
        if(!vgpt_wr(g,evregs[i][0],evregs[i][1]))return 0;
    if(!vgpt_context(g,2,1))return 0;
    if(!vgpt_wr(g,0x12100,(uint32_t)(ev+4096-16)))return 0;
    for(unsigned ch=0;ch<2;ch++) {
        if(!vgpt_command(g,ch,0,1))return 0;
        unsigned base=0x10000+ch*0x80,db=0x12000+ch*8;
        uint64_t addr=g->page[ch?VGPT_RR:VGPT_TR].iova;
        const unsigned reg[][2]={{base,0x10000002|(ch?0:8)}, {base+4,1024},
            {base+8,(uint32_t)addr},{base+12,(uint32_t)(addr>>32)},
            {db+4,(uint32_t)(addr>>32)},{base+0x60,(ch?0:0x10000)|0x14},
            {base+0x64,0},{base+0x68,0},{base+0x6c,0},{base+0x5c,1}};
        for(unsigned i=0;i<sizeof(reg)/sizeof(reg[0]);i++)if(!vgpt_wr(g,reg[i][0],reg[i][1]))return 0;
        if(!vgpt_context(g,ch,1) || !vgpt_expect(g,base+0x60,(ch?0:0x10000)|0x14,UINT32_MAX))return 0;
    }
    if(!vgpt_command(g,1,1,2) || !vgpt_command(g,0,1,2))return 0;
    if(!vgpt_context(g,0,2) || !vgpt_context(g,1,2) || !vgpt_context(g,2,1))return 0;
    g->phase=g->active_phase=VGPT_READY;return vgpt_guard(g);
}
static inline int vgpt_exchange(struct vgpt *g,const uint8_t *tx,uint8_t *rx,unsigned count,
    unsigned clock_source,unsigned divider)
{
    if(!vgpt_valid(g) || g->phase!=VGPT_READY || g->quarantined || !tx || !rx || !count || count>1032)return 0;
    uintptr_t input=(uintptr_t)tx,output=(uintptr_t)rx,self=(uintptr_t)g;
    if(input>UINTPTR_MAX-count || output>UINTPTR_MAX-count)return 0;
    if((input<self+sizeof(*g) && self<input+count) ||
       (output<self+sizeof(*g) && self<output+count))return 0;
    for(unsigned p=0;p<VGPT_PAGES;p++) {
        uintptr_t page=(uintptr_t)g->page[p].cpu;
        if((input<page+16384 && page<input+count) || (output<page+16384 && page<output+count))return 0;
    }
    uint64_t now;
    if(g->io.now_us(g->io.ctx,&now)!=1 || now<g->last)return vgpt_fail(g,VGPT_CLOCK);
    g->last=g->begin=now;g->budget=100000;g->phase=g->active_phase=VGPT_TRANSFER;
    struct vgp_spi_plan plan;
    if(!vgp_spi_plan(&plan,g->page[VGPT_TX].iova,g->page[VGPT_RX].iova,count,clock_source,divider))return vgpt_fail(g,VGPT_INVALID);
    if(!vgpt_context(g,0,2) || !vgpt_context(g,1,2) || !vgpt_context(g,2,1))return 0;
    if(!vgpt_mark(g,VGPT_TRACE_POINTERS))return 0;
    unsigned index;
    if(!vgpt_pointer(g,0x10010,VGPT_TR,1024,&index))return 0;
    if(index!=g->tx_wp)return vgpt_fail(g,VGPT_STATE);
    if(!vgpt_pointer(g,0x10090,VGPT_RR,1024,&index))return 0;
    if(index!=g->rx_wp)return vgpt_fail(g,VGPT_STATE);
    if(!vgpt_pointer(g,0x11010,VGPT_ER,4096,&index))return 0;
    if(index!=g->ev_rp)return vgpt_fail(g,VGPT_EVENT);
    if(!vgpt_mark(g,VGPT_TRACE_COPY))return 0;
    for(unsigned i=0;i<count;i++)((uint8_t *)g->page[VGPT_TX].cpu)[i]=tx[i];
    unsigned tx_end=(g->tx_wp+2)&63,rx_end=g->rx_wp;
    for(unsigned i=0;i<3;i++)((struct vgp_tre *)g->page[VGPT_TR].cpu)[(g->tx_wp+i)&63]=plan.tx[i];
    ((struct vgp_tre *)g->page[VGPT_RR].cpu)[g->rx_wp]=plan.rx;
    if(!vgpt_mark(g,VGPT_TRACE_CACHE_TX) || !vgpt_cache(g,VGPT_TX,VGPT_CLEAN) ||
       !vgpt_mark(g,VGPT_TRACE_CACHE_RX) || !vgpt_cache(g,VGPT_RX,VGPT_CLEAN_INVALIDATE) ||
       !vgpt_mark(g,VGPT_TRACE_CACHE_TR) || !vgpt_cache(g,VGPT_TR,VGPT_CLEAN) ||
       !vgpt_mark(g,VGPT_TRACE_CACHE_RR) || !vgpt_cache(g,VGPT_RR,VGPT_CLEAN))return 0;
    g->tx_wp=(g->tx_wp+3)&63;g->rx_wp=(g->rx_wp+1)&63;
    if(!vgpt_mark(g,VGPT_TRACE_RX_DB) || !vgpt_wr(g,0x12008,(uint32_t)(g->page[VGPT_RR].iova+16*g->rx_wp)) ||
       !vgpt_mark(g,VGPT_TRACE_TX_DB) || !vgpt_wr(g,0x12000,(uint32_t)(g->page[VGPT_TR].iova+16*g->tx_wp)))return 0;
    if(!vgpt_mark(g,VGPT_TRACE_POLL))return 0;
    unsigned done=0,processed=0;
    for(unsigned poll=0;poll<5000;poll++) {
        if(!vgpt_errors(g) || !vgpt_pointer(g,0x11010,VGPT_ER,4096,&index))return 0;
        if(index!=g->ev_rp) {
            if(!vgpt_mark(g,VGPT_TRACE_EVENTS))return 0;
            if(!vgpt_cache(g,VGPT_ER,VGPT_INVALIDATE))return 0;
            while(index!=g->ev_rp) {
                if(++processed>255)return vgpt_fail(g,VGPT_EVENT);
                struct vgp_tre event=((struct vgp_tre *)g->page[VGPT_ER].cpu)[g->ev_rp];
                unsigned ch=event.word[3]>>24,type=(event.word[3]>>16)&255,code=event.word[2]>>24;
                uint64_t pointer=((uint64_t)event.word[1]<<32)|event.word[0];
                g->last_channel=ch;g->last_code=code;g->last_status=event.word[3]&65535;
                g->last_length=event.word[2]&0xffffff;
                if(type!=0x22 || ch>1 || code!=2 || g->last_status || g->last_length!=count ||
                   (done&(1u<<ch)) || pointer!=g->page[ch?VGPT_RR:VGPT_TR].iova+16*(ch?rx_end:tx_end))
                    return vgpt_fail(g,VGPT_EVENT);
                done|=1u<<ch;++g->events;g->ev_rp=(g->ev_rp+1)&255;
            }
            if(!vgpt_wr(g,0x12100,(uint32_t)(g->page[VGPT_ER].iova+16*((g->ev_rp+255)&255))) ||
               !vgpt_wr(g,0x130c0,1))return 0;
        }
        if(done==3) {
            if(!vgpt_guard(g))return 0;
            int idle=g->io.serial_idle(g->io.ctx);
            if(idle!=0 && idle!=1)return vgpt_fail(g,VGPT_STATE);
            if(!vgpt_guard(g))return 0;
            if(idle) {
                if(!vgpt_pointer(g,0x10010,VGPT_TR,1024,&index))return 0;
                if(index!=g->tx_wp)return vgpt_fail(g,VGPT_STATE);
                if(!vgpt_pointer(g,0x10090,VGPT_RR,1024,&index))return 0;
                if(index!=g->rx_wp)return vgpt_fail(g,VGPT_STATE);
                if(!vgpt_mark(g,VGPT_TRACE_RX_ACQUIRE) || !vgpt_cache(g,VGPT_RX,VGPT_INVALIDATE) || !vgpt_guard(g))return 0;
                if(!vgpt_mark(g,VGPT_TRACE_COMPLETE))return 0;
                for(unsigned i=0;i<count;i++)rx[i]=((uint8_t *)g->page[VGPT_RX].cpu)[i];
                ++g->exchanges;g->phase=g->active_phase=VGPT_READY;return 1;
            }
        }
        if(!vgpt_pause(g))return 0;
    }
    return vgpt_fail(g,VGPT_TIMEOUT);
}
#endif
