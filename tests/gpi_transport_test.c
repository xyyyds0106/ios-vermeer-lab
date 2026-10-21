#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_gpi_transport.h"
#include "../xnu/vermeer/vermeer_rpmh_board_contract.h"
enum {HEALTHY,NO_COMMAND_IRQ,NO_EVENTS,ERROR_EVENT,SHORT_EVENT,WRONG_POINTER,
      DUPLICATE_EVENT,WRONG_CHANNEL,WRONG_TYPE,ACTIVE_SERIAL,REVOKE_TX_DB,SLOW_CALLBACK,
      IO_FAILURE,CACHE_FAILURE,REVERSE_EVENTS,LEASE_BOUNDARY,BACKWARD_TIME,PHASE_CHANGE,
      CACHE_REVOKE,WAIT_FAIL,BAD_PRODUCER,BAD_CHANNEL_RP,SLOW_COMMAND,NO_COMMAND_CHANGE,WRONG_COMMAND_STATE,
      TRACE_REVOKE,TRACE_TIMEOUT,IGNORE_WRITE,WRITE_GUARD_TIMEOUT,
      NOP_NO_PROGRESS,NOP_ERROR,NOP_RX_MOVED,NOP_EVENT_MOVED,NOP_BAD_RP};
struct fixture {
    struct vgpt g;uint32_t reg[0x14000/4];uint64_t now;
    unsigned mode,reads,writes,cache_count,fail_at,callbacks,ev_prod,rx_doorbell,revoked;
    unsigned clean[5],invalid[5],transfers;
    unsigned trace_count,trace_last,trace_fail;
    unsigned ignore_register;
    unsigned nops;
};
static unsigned page_for(struct fixture *f,void *cpu)
{for(unsigned i=0;i<5;i++)if(f->g.page[i].cpu==cpu)return i;assert(0);return 0;}
static int lease(void *ctx)
{
    struct fixture *f=ctx;
    if(f->revoked || (f->mode==LEASE_BOUNDARY && f->callbacks>=f->fail_at))return 0;
    if(f->mode==REVOKE_TX_DB && f->transfers)return 0;
    return 1;
}
static int time_us(void *ctx,uint64_t *value)
{struct fixture *f=ctx;*value=f->mode==BACKWARD_TIME && f->callbacks>=f->fail_at?f->now-1:f->now;return 1;}
static int fail(struct fixture *f)
{
    ++f->callbacks;
    if(f->mode==PHASE_CHANGE && f->callbacks==f->fail_at)f->g.phase=VGPT_READY;
    if(f->mode==SLOW_COMMAND)f->now+=250000;
    if(f->mode==SLOW_CALLBACK && f->transfers)f->now+=100000;
    return f->mode==IO_FAILURE && f->callbacks==f->fail_at;
}
static int rd(void *ctx,unsigned off,uint32_t *value)
{
    struct fixture *f=ctx;assert(!(off&3) && off<sizeof(f->reg));++f->reads;
    if(fail(f))return 0;
    *value=f->reg[off/4];return 1;
}
static void pointer(struct fixture *f,unsigned reg,uint64_t value)
{f->reg[reg/4]=(uint32_t)value;f->reg[(reg+4)/4]=(uint32_t)(value>>32);}
static void emit(struct fixture *f,unsigned ch,unsigned index,unsigned bytes)
{
    struct vgp_tre *events=f->g.page[VGPT_ER].cpu;
    uint64_t ptr=f->g.page[ch?VGPT_RR:VGPT_TR].iova+16*index;
    struct vgp_tre e={{(uint32_t)ptr,(uint32_t)(ptr>>32),(2u<<24)|bytes,(ch<<24)|(0x22u<<16)}};
    if(f->mode==ERROR_EVENT)e.word[2]=(16u<<24)|bytes;
    if(f->mode==SHORT_EVENT)e.word[2]--;
    if(f->mode==WRONG_POINTER)e.word[0]+=16;
    if(f->mode==WRONG_CHANNEL)e.word[3]|=2u<<24;
    if(f->mode==WRONG_TYPE)e.word[3]^=1u<<16;
    events[f->ev_prod]=e;f->ev_prod=(f->ev_prod+1)&255;
}
static void execute(struct fixture *f,uint32_t tx_db)
{
    struct vgpt *g=&f->g;
    unsigned tw=(tx_db-(uint32_t)g->page[VGPT_TR].iova)/16;
    unsigned old=(tw+64-3)&63,rx=(g->rx_wp+63)&63;
    struct vgp_tre *ring=g->page[VGPT_TR].cpu,*rr=g->page[VGPT_RR].cpu;
    const struct vgp_tre *cfg=&ring[old],*go=&ring[(old+1)&63],*dma=&ring[(old+2)&63];
    unsigned bytes=dma->word[2];assert(bytes && bytes<=1032 && tw<64);
    assert(cfg->word[0]==0x03000004 && cfg->word[2]==128 && cfg->word[3]==0x220001);
    assert(go->word[0]==3 && go->word[2]==bytes && go->word[3]==0x200801);
    assert(dma->word[3]==0x100200 && rr[rx].word[3]==0x100000 && rr[rx].word[2]==bytes);
    assert(f->clean[VGPT_TX] && f->clean[VGPT_TR] && f->clean[VGPT_RR] && f->clean[VGPT_RX]);
    assert(f->rx_doorbell==1);f->rx_doorbell=0;++f->transfers;
    if(f->mode==NO_EVENTS || f->mode==WAIT_FAIL)return;
    for(unsigned i=0;i<bytes;i++)((uint8_t *)g->page[VGPT_RX].cpu)[i]=((uint8_t *)g->page[VGPT_TX].cpu)[i]^0x5a;
    if(f->mode==REVERSE_EVENTS){emit(f,1,rx,bytes);emit(f,0,(old+2)&63,bytes);}
    else {emit(f,0,(old+2)&63,bytes);emit(f,f->mode==DUPLICATE_EVENT?0:1,f->mode==DUPLICATE_EVENT?(old+2)&63:rx,bytes);}
    pointer(f,0x10010,g->page[VGPT_TR].iova+tw*16);
    pointer(f,0x10090,g->page[VGPT_RR].iova+g->rx_wp*16);
    pointer(f,0x11010,g->page[VGPT_ER].iova+f->ev_prod*16);
    if(f->mode==BAD_PRODUCER)pointer(f,0x11010,g->page[VGPT_ER].iova+4096);
    if(f->mode==BAD_CHANNEL_RP)pointer(f,0x10010,g->page[VGPT_TR].iova+16*((tw+1)&63));
}
static int wr(void *ctx,unsigned off,uint32_t value)
{
    struct fixture *f=ctx;assert(!(off&3) && off<sizeof(f->reg));++f->writes;
    if(fail(f))return 0;
    if(f->mode==IGNORE_WRITE && off==f->ignore_register)return 1;
    if(f->mode==WRITE_GUARD_TIMEOUT && off==0x12008) {
        f->now+=100000;return vgpt_guard(&f->g);
    }
    if(off==0x130a0)f->reg[0x13090/4]&=~value;
    else if(off==0x130a4)f->reg[0x13094/4]&=~value;
    else if(off==0x13008 || off==0x13010) {
        unsigned ev=off==0x13010,ch=value&255,op=value>>24;
        assert(ch<2 && op<2);
        unsigned base=ev?0x11000:0x10000+0x80*ch;
        if(f->mode!=NO_COMMAND_CHANGE)f->reg[base/4]=(f->reg[base/4]&~0xf00000u)|
            (f->mode==WRONG_COMMAND_STATE?15u:op?2u:1u)<<20;
        if(f->mode!=NO_COMMAND_IRQ)f->reg[(ev?0x13094:0x13090)/4]|=1u<<(ev?0:ch);
        if(op)pointer(f,base+0x10,((uint64_t)f->reg[(base+12)/4]<<32)|f->reg[(base+8)/4]);
    } else {
        if(off==0x10000 || off==0x10080 || off==0x11000)value|=1u<<20; /* RO state */
        f->reg[off/4]=value;
        if(off==0x1100c)pointer(f,0x11010,((uint64_t)value<<32)|f->reg[0x11008/4]);
        if(off==0x12008)++f->rx_doorbell;
        if(off==0x12000) {
            unsigned index=(value-(uint32_t)f->g.page[VGPT_TR].iova)/16;
            struct vgp_tre last=((struct vgp_tre *)f->g.page[VGPT_TR].cpu)[(index+63)&63];
            if(last.word[3]==0x00010000) {
                assert(index==1 && !last.word[0] && !last.word[1] && !last.word[2]);
                assert(f->clean[VGPT_TR] && !f->rx_doorbell && !f->transfers && !f->g.nop_verified);
                ++f->nops;
                if(f->mode!=NOP_NO_PROGRESS)pointer(f,0x10010,f->g.page[VGPT_TR].iova+(f->mode==NOP_BAD_RP?32:16));
                if(f->mode==NOP_ERROR)f->reg[0x13100/4]=1;
                if(f->mode==NOP_RX_MOVED)pointer(f,0x10090,f->g.page[VGPT_RR].iova+16);
                if(f->mode==NOP_EVENT_MOVED)pointer(f,0x11010,f->g.page[VGPT_ER].iova+16);
            } else execute(f,value);
        }
    }
    return 1;
}
static int cache(void *ctx,void *cpu,size_t n,unsigned op)
{
    struct fixture *f=ctx;assert(n==16384 && op<=2);unsigned p=page_for(f,cpu);
    ++f->cache_count;
    if(f->mode==CACHE_REVOKE && f->cache_count==f->fail_at)f->revoked=1;
    if(f->mode==CACHE_FAILURE && f->cache_count==f->fail_at)return 0;
    if(op==VGPT_CLEAN || op==VGPT_CLEAN_INVALIDATE)++f->clean[p];
    if(op==VGPT_INVALIDATE)++f->invalid[p];
    return 1;
}
static int pause_us(void *ctx,unsigned us)
{struct fixture *f=ctx;assert(us==20);f->now+=us;return f->mode!=WAIT_FAIL;}
static int idle(void *ctx)
{return ((struct fixture *)ctx)->mode!=ACTIVE_SERIAL;}
static void checkpoint(void *ctx,unsigned point)
{
    struct fixture *f=ctx;assert(point==f->g.checkpoint && !f->g.exchanges);
    assert(point>=1 && point<=VGPT_TRACE_COMPLETE);++f->trace_count;f->trace_last=point;
    if(point==VGPT_TRACE_RX_DB)assert(!f->rx_doorbell && !f->transfers);
    if(point==VGPT_TRACE_TX_DB)assert(f->rx_doorbell==1 && !f->transfers);
    if(point==f->trace_fail){
        if(f->mode==TRACE_REVOKE)f->revoked=1;
        if(f->mode==TRACE_TIMEOUT)f->now+=100000;
    }
}
static struct fixture *make(unsigned mode)
{
    struct fixture *f=calloc(1,sizeof(*f));assert(f);f->mode=mode;f->now=1000;
    f->g.io=(struct vgpt_ops){f,lease,rd,wr,cache,time_us,pause_us,idle,checkpoint};
    for(unsigned i=0;i<5;i++) {
        f->g.page[i].cpu=aligned_alloc(16384,16384);assert(f->g.page[i].cpu);
        f->g.page[i].iova=UINT64_C(0x8c1000000)+i*16384;
    }
    return f;
}
static void destroy(struct fixture *f)
{for(unsigned i=0;i<5;i++)free(f->g.page[i].cpu);free(f);}
static void healthy(void)
{
    uint8_t tx[1032],rx[1032];for(unsigned i=0;i<1032;i++)tx[i]=(uint8_t)i;
    const unsigned modes[]={HEALTHY,REVERSE_EVENTS,NO_COMMAND_IRQ};
    for(unsigned m=0;m<sizeof(modes)/sizeof(modes[0]);m++) {
        struct fixture *f=make(modes[m]);assert(vgpt_start(&f->g));assert(f->g.commands==5);
        assert(f->reg[0x11004/4]==4096 && f->reg[0x10004/4]==1024);
        for(unsigned n=1;n<=1032;n++) {
            memset(rx,0xcc,sizeof(rx));assert(vgpt_exchange(&f->g,tx,rx,n,0,128));
            for(unsigned i=0;i<n;i++)assert(rx[i]==(uint8_t)(tx[i]^0x5a));
            for(unsigned i=n;i<1032;i++)assert(rx[i]==0xcc);
        }
        assert(!f->g.quarantined && f->g.events==2064 && f->invalid[VGPT_RX]==1032);
        assert(f->trace_count==12 && f->trace_last==VGPT_TRACE_COMPLETE && f->g.exchanges==1032);
        destroy(f);
    }
}
struct irq_fixture {uint32_t reg[0x400/4];unsigned calls,fail_at;};
static int irq_read(void *ctx,unsigned at,uint32_t *out)
{
    struct irq_fixture *f=ctx;assert(at==4 || (at>=0x100 && at<0x400 && !(at&3)));
    if(++f->calls==f->fail_at)return 0;
    *out=f->reg[at/4];return 1;
}
static uint32_t be32(const uint8_t *p)
{return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static const struct vrb_rule *irq_rule(const char *path)
{
    for(size_t i=0;i<VRB_RULE_COUNT;i++)if(!strcmp(vrb_rules[i].path,path) && !strcmp(vrb_rules[i].name,"interrupts"))return &vrb_rules[i];
    assert(0);return NULL;
}
static void irq_contract(void)
{
    const struct vrb_rule *apps=irq_rule("/soc/apps-smmu@15000000");
    const struct vrb_rule *gpi=irq_rule("/soc/qcom,gpi-dma@a00000");
    const struct vrb_rule *se=irq_rule("/soc/qcom,qupv3_1_geni_se@ac0000/spi@a90000");
    assert(apps->size>=71*12 && gpi->size==12*12 && se->size==12);
    unsigned fixed[3]={be32(gpi->bytes+4)+32,be32(se->bytes+4)+32,be32(apps->bytes+4)+32};
    for(unsigned bank=0;bank<=70;bank++) {
        struct irq_fixture f={0};f.reg[1]=31;struct vgpt_irq_observation r;
        assert(vgpt_irqs_masked(&f,irq_read,bank,&r) && !r.source);
        unsigned calls=f.calls;
        for(unsigned source=0;source<(bank<70?4u:3u);source++)for(unsigned active=0;active<2;active++) {
            unsigned id=source<3?fixed[source]:be32(apps->bytes+12*(bank+1)+4)+32;
            memset(&f,0,sizeof f);f.reg[1]=31;
            f.reg[((active?0x300:0x100)+4*(id/32))/4]=1u<<(id%32);
            assert(!vgpt_irqs_masked(&f,irq_read,bank,&r));
            assert(r.source==source+1 && r.intid==id && f.calls==1+2*(source+1));
        }
        for(unsigned i=1;i<=calls;i++) {
            memset(&f,0,sizeof f);f.reg[1]=31;f.fail_at=i;
            assert(!vgpt_irqs_masked(&f,irq_read,bank,&r) && f.calls==i);
        }
    }
    struct irq_fixture f={0};struct vgpt_irq_observation r;
    assert(!vgpt_irqs_masked(&f,irq_read,70,&r) && f.calls==1 && r.intid==311);
    f.calls=0;assert(!vgpt_irqs_masked(&f,irq_read,71,&r) && !f.calls);
    /* A trace callback cannot bypass the real lease/time guards, even at
     * the final cache-acquire boundary; caller RX stays unpublished. */
    for(unsigned mode=TRACE_REVOKE;mode<=TRACE_TIMEOUT;mode++)for(unsigned point=1;point<=12;point++) {
        struct fixture *g=make(mode);uint8_t tx[8]={0},rx[8];memset(rx,0xcc,sizeof rx);
        assert(vgpt_start(&g->g));g->trace_fail=point;
        assert(!vgpt_exchange(&g->g,tx,rx,8,0,128) && g->g.quarantined);
        for(unsigned i=0;i<8;i++)assert(rx[i]==0xcc);
        assert(g->trace_last==point && !g->g.exchanges);destroy(g);
    }
}
static void faults(void)
{
    uint8_t tx[8]={0},rx[8];
    const unsigned modes[]={NO_EVENTS,ERROR_EVENT,SHORT_EVENT,WRONG_POINTER,DUPLICATE_EVENT,
        WRONG_CHANNEL,WRONG_TYPE,ACTIVE_SERIAL,REVOKE_TX_DB,SLOW_CALLBACK,WAIT_FAIL,BAD_PRODUCER,BAD_CHANNEL_RP};
    for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);i++) {
        struct fixture *f=make(modes[i]);assert(vgpt_start(&f->g));memset(rx,0xcc,sizeof(rx));
        assert(!vgpt_exchange(&f->g,tx,rx,8,0,128) && f->g.quarantined);
        for(unsigned j=0;j<8;j++)assert(rx[j]==0xcc);
        unsigned writes=f->writes,reads=f->reads;
        assert(!vgpt_exchange(&f->g,tx,rx,8,0,128) && !vgpt_start(&f->g));
        assert(writes==f->writes && reads==f->reads);destroy(f);
    }
    struct fixture *reference=make(HEALTHY);assert(vgpt_start(&reference->g));
    unsigned callbacks=reference->callbacks;destroy(reference);
    for(unsigned i=1;i<=callbacks;i++) {
        struct fixture *f=make(IO_FAILURE);f->fail_at=i;
        assert(!vgpt_start(&f->g) && f->g.quarantined && f->callbacks==i);destroy(f);
    }
    for(unsigned i=1;i<=5;i++) {
        struct fixture *f=make(CACHE_FAILURE);f->fail_at=i;
        assert(!vgpt_start(&f->g) && f->g.quarantined && !f->writes);destroy(f);
    }
    struct fixture *f=make(HEALTHY);f->reg[0x10000/4]=2u<<20;
    assert(!vgpt_start(&f->g) && !f->writes);destroy(f);
    f=make(HEALTHY);f->g.page[1].iova=f->g.page[0].iova;
    assert(!vgpt_start(&f->g) && !f->reads && !f->writes);destroy(f);
    for(unsigned mode=NO_COMMAND_CHANGE;mode<=WRONG_COMMAND_STATE;mode++) {
        f=make(mode);assert(!vgpt_start(&f->g) && f->g.quarantined);
        assert(f->g.error==(mode==NO_COMMAND_CHANGE?VGPT_TIMEOUT:VGPT_STATE));destroy(f);
    }
    for(unsigned c=0;c<3;c++)for(unsigned state=1;state<16;state++) {
        f=make(HEALTHY);f->reg[(c==2?0x11000:0x10000+0x80*c)/4]=state<<20;
        assert(!vgpt_start(&f->g) && !f->writes);destroy(f);
    }
    f=make(HEALTHY);assert(vgpt_start(&f->g));f->mode=SLOW_COMMAND;
    /* Direct command helper must stop after a single callback crosses its
     * 250ms deadline, even though the 1.5s startup budget has time remaining. */
    f->g.phase=f->g.active_phase=VGPT_STARTING;f->g.begin=f->g.last=f->now;f->g.budget=1500000;
    unsigned before=f->callbacks;assert(!vgpt_command(&f->g,0,0,1));
    assert(f->g.error==VGPT_TIMEOUT && f->callbacks==before+1);destroy(f);
}
static void transfer_boundaries(void)
{
    uint8_t tx[8]={0},rx[8];
    struct fixture *f=make(HEALTHY);assert(vgpt_start(&f->g));f->callbacks=f->cache_count=0;
    assert(vgpt_exchange(&f->g,tx,rx,8,0,128));unsigned calls=f->callbacks,caches=f->cache_count;destroy(f);
    const unsigned faults[]={IO_FAILURE,LEASE_BOUNDARY,BACKWARD_TIME,PHASE_CHANGE};
    for(unsigned m=0;m<sizeof(faults)/sizeof(faults[0]);m++)for(unsigned i=1;i<=calls;i++) {
        f=make(HEALTHY);assert(vgpt_start(&f->g));f->callbacks=0;f->mode=faults[m];f->fail_at=i;
        memset(rx,0xcc,sizeof(rx));assert(!vgpt_exchange(&f->g,tx,rx,8,0,128) && f->g.quarantined);
        assert(f->callbacks==i);for(unsigned j=0;j<8;j++)assert(rx[j]==0xcc);
        unsigned reads=f->reads,writes=f->writes;
        assert(!vgpt_exchange(&f->g,tx,rx,8,0,128));assert(f->reads==reads && f->writes==writes);destroy(f);
    }
    for(unsigned mode=CACHE_FAILURE;mode<=CACHE_REVOKE;mode+=CACHE_REVOKE-CACHE_FAILURE)
    for(unsigned i=1;i<=caches;i++) {
        f=make(HEALTHY);assert(vgpt_start(&f->g));f->cache_count=0;f->mode=mode;f->fail_at=i;
        memset(rx,0xcc,sizeof(rx));assert(!vgpt_exchange(&f->g,tx,rx,8,0,128) && f->g.quarantined);
        assert(f->cache_count==i);for(unsigned j=0;j<8;j++)assert(rx[j]==0xcc);destroy(f);
    }
    f=make(HEALTHY);assert(vgpt_start(&f->g));unsigned reads=f->reads,writes=f->writes;
    assert(!vgpt_exchange(&f->g,f->g.page[VGPT_ER].cpu,rx,8,0,128));
    assert(!vgpt_exchange(&f->g,tx,f->g.page[VGPT_RX].cpu,8,0,128));
    assert(!vgpt_exchange(&f->g,tx,(void *)&f->g,8,0,128));
    assert(f->reads==reads && f->writes==writes && !f->g.quarantined);destroy(f);
}
static void context_readback(void)
{
    /* A posted write returning success does not establish hardware state.
     * Drop each nonzero context/base/scratch write and prohibit DMA. */
    const unsigned ignored[]={0x11000,0x11004,0x11008,0x1100c,
        0x10000,0x10004,0x10008,0x1000c,0x10060,
        0x10080,0x10084,0x10088,0x1008c,0x100e0};
    for(unsigned i=0;i<sizeof ignored/sizeof ignored[0];i++) {
        struct fixture *f=make(IGNORE_WRITE);f->ignore_register=ignored[i];
        assert(!vgpt_start(&f->g) && f->g.quarantined && f->g.error==VGPT_STATE);
        assert(f->g.verify_register==ignored[i] && f->g.verify_mask && !f->rx_doorbell && !f->transfers);
        destroy(f);
    }
    const unsigned fields[]={0,4,8,12};uint8_t tx[8]={0},rx[8];
    for(unsigned c=0;c<3;c++)for(unsigned i=0;i<4;i++) {
        unsigned base=c==2?0x11000:0x10000+c*0x80,off=base+fields[i];
        uint32_t mask=i?UINT32_MAX:c==2?0xfff1000f:0xfff7c00f;
        for(unsigned bit=0;bit<32;bit++)if(mask&(1u<<bit)) {
            struct fixture *f=make(HEALTHY);assert(vgpt_start(&f->g));
            f->reg[off/4]^=1u<<bit;unsigned writes=f->writes;memset(rx,0xcc,sizeof rx);
            assert(!vgpt_exchange(&f->g,tx,rx,sizeof tx,0,128) && f->g.quarantined);
            assert(f->g.verify_register==off && f->writes==writes && !f->rx_doorbell && !f->transfers);
            for(unsigned n=0;n<sizeof rx;n++)assert(rx[n]==0xcc);
            destroy(f);
        }
    }
    struct fixture *f=make(HEALTHY);assert(vgpt_start(&f->g));
    f->reg[0x10000/4]|=0x30;f->reg[0x10080/4]|=0x130;f->reg[0x11000/4]|=0x30;
    assert(vgpt_exchange(&f->g,tx,rx,sizeof tx,0,128));destroy(f);
    f=make(HEALTHY);assert(vgpt_start(&f->g));f->mode=WRITE_GUARD_TIMEOUT;
    memset(rx,0xcc,sizeof rx);
    assert(!vgpt_exchange(&f->g,tx,rx,sizeof tx,0,128) && f->g.error==VGPT_TIMEOUT);
    assert(f->g.quarantined && !f->rx_doorbell && !f->transfers);
    for(unsigned i=0;i<sizeof rx;i++)assert(rx[i]==0xcc);
    destroy(f);
}
static void nop_probe(void)
{
    struct fixture *f=make(HEALTHY);uint8_t tx[8]={0},rx[8];
    assert(vgpt_start(&f->g));memset(f->g.page[VGPT_TX].cpu,0x5c,16384);memset(f->g.page[VGPT_RX].cpu,0xa7,16384);
    f->callbacks=f->cache_count=0;unsigned writes=f->writes;
    assert(vgpt_probe_nop(&f->g) && f->g.nop_verified && f->g.phase==VGPT_READY);
    unsigned calls=f->callbacks;assert(f->cache_count==1 && f->writes==writes+1 && f->nops==1);
    assert(!f->transfers && !f->rx_doorbell && !f->g.events && !f->g.exchanges && f->g.tx_wp==1);
    for(unsigned i=0;i<16384;i++) {
        assert(((uint8_t *)f->g.page[VGPT_TX].cpu)[i]==0x5c);
        assert(((uint8_t *)f->g.page[VGPT_RX].cpu)[i]==0xa7);
    }
    writes=f->writes;unsigned reads=f->reads;
    assert(!vgpt_probe_nop(&f->g) && f->writes==writes && f->reads==reads);
    assert(vgpt_exchange(&f->g,tx,rx,8,0,128) && f->g.tx_wp==4 && f->g.exchanges==1);destroy(f);
    for(unsigned mode=NOP_NO_PROGRESS;mode<=NOP_BAD_RP;mode++) {
        f=make(HEALTHY);assert(vgpt_start(&f->g));f->mode=mode;
        assert(!vgpt_probe_nop(&f->g) && f->g.quarantined && !f->g.nop_verified && f->nops==1);
        assert(!f->transfers && !f->rx_doorbell);
        writes=f->writes;reads=f->reads;
        assert(!vgpt_probe_nop(&f->g) && !vgpt_exchange(&f->g,tx,rx,8,0,128));
        assert(f->writes==writes && f->reads==reads);destroy(f);
    }
    const unsigned faults[]={IO_FAILURE,LEASE_BOUNDARY,BACKWARD_TIME,PHASE_CHANGE};
    for(unsigned mode=0;mode<sizeof faults/sizeof faults[0];mode++)for(unsigned i=1;i<=calls;i++) {
        f=make(HEALTHY);assert(vgpt_start(&f->g));f->callbacks=0;f->mode=faults[mode];f->fail_at=i;
        assert(!vgpt_probe_nop(&f->g) && f->g.quarantined && !f->g.nop_verified && f->callbacks==i);
        assert(!f->transfers && !f->rx_doorbell);destroy(f);
    }
    for(unsigned mode=CACHE_FAILURE;mode<=CACHE_REVOKE;mode+=CACHE_REVOKE-CACHE_FAILURE) {
        f=make(HEALTHY);assert(vgpt_start(&f->g));f->mode=mode;f->cache_count=0;f->fail_at=1;
        assert(!vgpt_probe_nop(&f->g) && f->g.quarantined && !f->g.nop_verified && !f->nops);destroy(f);
    }
}
int main(void)
{healthy();irq_contract();faults();transfer_boundaries();context_readback();nop_probe();puts("GPI transport: isolated NOP real-pointer lifecycle and every failure boundary; context write/readback and every live-field mutation; command/doorbell lifecycle, ring wrap, completions, IRQ contract, checkpoints and permanent quarantine PASS");}
