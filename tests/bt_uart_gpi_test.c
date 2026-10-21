#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/media/vermeer_bt_uart_gpi.h"

static _Alignas(16384) uint8_t memory[VBUG_PAGES][16384];
struct mock {
    struct vbug *s;
    uint32_t regs[0x60000/4],uart[0x4000/4];
    unsigned proof,ops,fault,revoke,delay,reenter,auto_tx,tx_bells,rx_bells;
    unsigned events,cache_fault,backwards;
    uint64_t now;
};
static unsigned proof(void *p) {return ((struct mock *)p)->proof;}
static int time_us(void *p,uint64_t *out) {
    struct mock *m=p;*out=m->now;return 1;
}
static int tick(struct mock *m) {
    unsigned at=++m->ops;
    if(at==m->revoke)m->proof=0;
    if(at==m->delay)m->now+=100000;
    if(at==m->backwards)m->now=0;
    if(at==m->reenter)assert(!vbug_poll(m->s));
    return at!=m->fault;
}
static int read32(void *p,unsigned bank,unsigned off,uint32_t *v) {
    struct mock *m=p;assert(off<(!bank?0x60000u:0x4000u) && !(off&3));
    if(!tick(m))return 0;
    *v=bank?m->uart[off/4]:m->regs[off/4];return 1;
}
static void event(struct mock *m,unsigned ch,unsigned code,unsigned n,uint64_t ptr,unsigned status) {
    struct vbug *s=m->s; unsigned base=vbug_offset(s,0x11000);
    unsigned index=(unsigned)((m->regs[(base+0x10)/4]-s->page[VBUG_ER].iova)/16);
    ((struct vbug_tre *)memory[VBUG_ER])[index]=(struct vbug_tre){{(uint32_t)ptr,(uint32_t)(ptr>>32),
        (code<<24)|n,(ch<<24)|0x220000|status}};
    index=(index+1)&255;m->regs[(base+0x10)/4]=(uint32_t)(s->page[VBUG_ER].iova+16*index);
    ++m->events;
}
static int write32(void *p,unsigned bank,unsigned off,uint32_t v) {
    struct mock *m=p;struct vbug *s=m->s;assert(!bank);if(!tick(m))return 0;
    unsigned rel=off-0x4000*s->io.gpii;
    assert(rel==0x12000 || rel==0x12008 || rel==0x12100 || rel==0x130c0);
    m->regs[off/4]=v;
    if(rel==0x12000) {
        ++m->tx_bells;
        /* Hardware model checks actual submitted UART TREs and bytes. */
        struct vbug_tre *r=(struct vbug_tre *)memory[VBUG_TR];
        unsigned first=(s->tx_end+62)&63,go=(s->tx_end+63)&63;
        assert(r[first].w[0]==0x1000083 && r[first].w[1]==0 && r[first].w[2]==3 && r[first].w[3]==0x220001);
        assert(r[go].w[0]==1 && r[go].w[3]==0x200001);
        assert(r[s->tx_end].w[2]==s->tx_length && r[s->tx_end].w[3]==0x100200);
        if(m->auto_tx)event(m,0,2,s->tx_length,s->page[VBUG_TR].iova+16*s->tx_end,0);
    } else if(rel==0x12008) {
        ++m->rx_bells;
        struct vbug_tre *r=(struct vbug_tre *)memory[VBUG_RR];
        assert(r[s->rx_end].w[2]==2048 && r[s->rx_end].w[3]==0x100301);
    }
    return 1;
}
static int cache(void *p,void *cpu,size_t bytes,unsigned op) {
    struct mock *m=p; assert(bytes==16384 && op<=2);
    unsigned page=VBUG_PAGES;for(unsigned i=0;i<VBUG_PAGES;i++)if(cpu==memory[i])page=i;
    assert(page<VBUG_PAGES); if(!tick(m))return 0;
    return !m->cache_fault;
}
static void init(struct vbug *s,struct mock *m,unsigned gpii) {
    memset(m,0,sizeof(*m));memset(memory,0,sizeof(memory));m->s=s;m->proof=127;m->now=10;
    struct vbug_io io={.ctx=m,.gpi_physical=0x800000,.gpi_length=0x60000,
        .uart_physical=0x898000,.uart_length=0x4000,.gpii=gpii,.divider=3,.rx_fifo_depth=16,
        .proof=proof,.read=read32,.write=write32,.cache=cache,.now_us=time_us};
    struct vbug_page pages[VBUG_PAGES];
    for(unsigned i=0;i<VBUG_PAGES;i++)pages[i]=(struct vbug_page){memory[i],0x400000+16384*i};
    m->uart[0x68/4]=0x200;m->uart[0x64/4]=1;
    for(unsigned ch=0;ch<3;ch++) {
        unsigned base=(ch==2?0x11000:0x10000+ch*0x80)+0x4000*gpii;
        unsigned page=ch==2?VBUG_ER:ch?VBUG_RR:VBUG_TR;
        m->regs[base/4]=ch==2?0x10110002:ch?0x10200002:0x1020000a;
        m->regs[(base+4)/4]=ch==2?4096:1024;
        m->regs[(base+8)/4]=(uint32_t)pages[page].iova;
        m->regs[(base+0x10)/4]=(uint32_t)pages[page].iova;
        m->regs[(base+0x18)/4]=(uint32_t)pages[page].iova;
        m->regs[(base+0x60)/4]=(ch?0:0x10000)|0x26;
    }
    assert(vbug_attach(s,&io,pages));
}
static void receive(struct mock *m,const uint8_t *data,unsigned n,unsigned code) {
    assert(n<=2048);memcpy(memory[VBUG_RX],data,n);
    event(m,1,code,n,m->s->page[VBUG_RR].iova+16*m->s->rx_end,0);
}
int main(void) {
    static struct mock m;struct vbug s; uint8_t out[4096];
    static const uint8_t command[]={1,1,0x10,0};
    static const uint8_t version[]={4,14,12,1,1,0x10,0,11,0x34,0x12,11,0x1d,0,0x78,0x56};
    unsigned total;
    for(unsigned gpii=1;gpii<=5;gpii++) {
        init(&s,&m,gpii); m.auto_tx=1;
        assert(vbug_write(&s,command,4)==4 && m.tx_bells==1);
        receive(&m,version,sizeof(version),2);
        assert(vbug_read(&s,out,3)==3 && !memcmp(out,version,3));
        assert(vbug_read(&s,out,sizeof(out))==12 && !memcmp(out,version+3,12));
        assert(!s.tx_pending && m.rx_bells==2 && s.events==2);
    }
    /* Real bounded identity state machine through DMA pages/TREs/event ring. */
    for(unsigned split=0;split<=sizeof(version);split++) {
        init(&s,&m,1);m.auto_tx=1;struct vbt_identity_io io;struct vbt_identity query;
        assert(!vbug_identity_link(&s,&io,0));assert(vbug_identity_link(&s,&io,1));
        assert(vbt_identity_begin(&query,&io,100000));
        assert(vbt_identity_step(&query)==VBT_ID_WAITING);
        assert(!memcmp(memory[VBUG_TX],command,4));
        receive(&m,version,split,4);
        assert(vbt_identity_step(&query)==(split==sizeof(version)?VBT_ID_DONE:VBT_ID_WAITING));
        if(split<sizeof(version)) {
            receive(&m,version+split,sizeof(version)-split,2);
            assert(vbt_identity_step(&query)==VBT_ID_DONE);
        }
        struct vbt_local_version v;assert(vbt_identity_result(&query,&v));
        assert(v.manufacturer==0x1d && v.hci_revision==0x1234 && v.lmp_subversion==0x5678);
        assert(m.tx_bells==1 && s.rx_pending && s.dma_exposed);
    }
    init(&s,&m,1);assert(vbug_write(&s,command,4)==4);receive(&m,version,15,2);
    unsigned before=m.ops;assert(vbug_poll(&s));total=m.ops-before;
    /* Every MMIO/cache boundary in completion/RX requeue fails closed. */
    for(unsigned kind=0;kind<5;kind++)for(unsigned at=1;at<=total;at++) {
        init(&s,&m,1);assert(vbug_write(&s,command,4)==4);receive(&m,version,15,2);
        before=m.ops;
        if(kind==0)m.fault=before+at;else if(kind==1)m.revoke=before+at;
        else if(kind==2)m.delay=before+at;else if(kind==3)m.reenter=before+at;
        else m.backwards=before+at;
        assert(!vbug_poll(&s) && s.retired && m.ops==before+at);
        assert(vbug_write(&s,command,4)==-1 && vbug_read(&s,out,4)==-1 && m.ops==before+at);
    }
    for(unsigned malformed=0;malformed<7;malformed++) {
        init(&s,&m,1);assert(vbug_write(&s,command,4)==4);
        unsigned ch=malformed==0?2:0, code=malformed==1?16:2;
        unsigned n=malformed==2?3:4,status=malformed==3?1:0;
        uint64_t ptr=s.page[VBUG_TR].iova+16*s.tx_end+(malformed==4?16:0);
        event(&m,ch,code,n,ptr,status);
        if(malformed==5)event(&m,0,2,4,ptr,0);
        if(malformed==6)((struct vbug_tre *)memory[VBUG_ER])[0].w[3]^=0x10000;
        assert(!vbug_poll(&s) && s.retired);
    }
    init(&s,&m,1);assert(vbug_write(&s,command,4)==4);
    m.now+=100000;assert(!vbug_poll(&s) && s.error==VBUG_TIMEOUT && m.tx_bells==1);
    init(&s,&m,1);assert(vbug_write(&s,command,4)==4);
    for(unsigned i=0;i<4096;i++)assert(vbug_poll(&s));
    assert(!vbug_poll(&s) && s.error==VBUG_TIMEOUT);
    /* Ring wrapping and variable EOT/EOB length; no duplicate RX publication. */
    init(&s,&m,5);m.auto_tx=1;
    for(unsigned i=0;i<300;i++) {
        assert(vbug_write(&s,command,4)==4);receive(&m,version,15,i&1?2:4);
        assert(vbug_read(&s,out,4096)==15 && !memcmp(out,version,15));
    }
    assert(s.events==600 && m.tx_bells==300 && m.rx_bells==301);
    puts("BT UART GPI: 5 instances, HCI identity every split, ring wrap, malformed events, all callback fault boundaries PASS");
    return 0;
}
