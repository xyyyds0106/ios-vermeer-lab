#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_geni_dma.h"
static _Alignas(16384) uint8_t tx_page[16384],rx_page[16384];
struct sim {
    struct vg_dma *dma;
    unsigned tick,active,started,master,ts,rs,writes,cancels,authorized,cache_calls;
    unsigned fault,cache_fail,deny,mode,n;
    uint32_t tx_low,tx_high,rx_low,rx_high;
};
static uint32_t rd(void *ctx,unsigned r)
{
    struct sim *s=ctx;
    if(r==VG_STATUS)return s->active;
    if(r==VG_IRQ)return s->master;
    if(r==VGD_TX+VGD_STAT)return s->ts;
    if(r==VGD_RX+VGD_STAT)return s->rs;
    if(r==0x258)return s->mode;
    return 0;
}
static void wr(void *ctx,unsigned r,uint32_t v)
{
    struct sim *s=ctx;++s->writes;
    if(r==VG_CLEAR)s->master&=~v;
    if(r==VGD_TX+VGD_CLEAR)s->ts&=~v;
    if(r==VGD_RX+VGD_CLEAR)s->rs&=~v;
    if(r==VGD_TX)s->tx_low=v;
    if(r==VGD_TX+4)s->tx_high=v;
    if(r==VGD_RX)s->rx_low=v;
    if(r==VGD_RX+4)s->rx_high=v;
    if(r==VGD_RX+12)s->n=v;
    if(r==VGD_TX+12){assert(s->n==v);s->started=1;}
    if(r==VG_CTRL){++s->cancels;if(s->fault!=4)s->active=0;}
    if(r==VG_CMD) {
        unsigned op=v>>27;
        if(op==8 || op==9) {
            unsigned fail=(op==8 && s->fault==8) || (op==9 && s->fault==9);
            s->master=fail?0:1;s->active=fail?1:0;
        }
        else {assert(op==3 && (v&4));s->active=1;}
    }
}
static void pause_bus(void *ctx)
{
    struct sim *s=ctx;
    if(!s->started)return;
    ++s->tick;
    if(s->tick==1 && s->fault!=1)s->ts=1;
    if(s->tick==3 && s->fault!=2) {
        s->rs=1;
        for(unsigned i=0;i<s->n;i++)s->dma->rx[i]=s->dma->tx[i]^0x5a;
    }
    if(s->tick==5) {
        if(s->fault!=3)s->master=1;
        if(s->fault!=4)s->active=0;
    }
    if(s->fault==5)s->ts=4;
    if(s->fault==6)s->rs=4;
    if(s->fault==7)s->master=8;
}
static int authorize(void *ctx,uint64_t tx,uint64_t rx,size_t page)
{
    struct sim *s=ctx;++s->authorized;
    assert(tx==s->dma->tx_pa && rx==s->dma->rx_pa && page==s->dma->page_bytes);
    return s->deny?-1:1;
}
static int cache(void *ctx,void *p,size_t n)
{
    struct sim *s=ctx;
    assert(n==s->dma->page_bytes);
    assert(p==((s->cache_calls==0)?(void*)tx_page:(void*)rx_page));
    return ++s->cache_calls!=s->cache_fail;
}
static void make(struct sim *s,struct vg_bus *b,struct vg_dma *d,size_t page)
{
    *s=(struct sim){.dma=d,.mode=1};
    *b=(struct vg_bus){.ctx=s,.read=rd,.write=wr,.pause=pause_bus};
    *d=(struct vg_dma){.tx=tx_page,.rx=rx_page,.tx_pa=UINT64_C(0x8c0000000),
        .rx_pa=UINT64_C(0x8c0004000),.page_bytes=page,.address_bits=36,.ctx=s,
        .authorize=authorize,.clean=cache,.clean_invalidate=cache,.invalidate=cache};
}
int main(void)
{
    uint8_t tx[1032],rx[1032];
    for(unsigned i=0;i<sizeof(tx);i++)tx[i]=(uint8_t)(i*31+7);
    struct sim s;struct vg_bus b;struct vg_dma d;
    for(size_t page=4096;page<=16384;page*=4)for(unsigned n=1;n<=sizeof(tx);n++) {
        make(&s,&b,&d,page);memset(rx,0xa5,sizeof(rx));
        /* Stale DONE must not make the new operation complete. */
        s.ts=s.rs=1;
        assert(vgd_transfer(&b,&d,tx,rx,n));
        for(unsigned i=0;i<n;i++)assert(rx[i]==(uint8_t)(tx[i]^0x5a));
        if(n<sizeof(rx))assert(rx[n]==0xa5);
        assert(s.tick==5 && s.cache_calls==3 && s.authorized==1 && !b.failed && !d.quarantined);
        assert((((uint64_t)s.tx_high<<32)|s.tx_low)==d.tx_pa);
        assert((((uint64_t)s.rx_high<<32)|s.rx_low)==d.rx_pa);
    }
    for(unsigned fault=1;fault<=9;fault++) {
        make(&s,&b,&d,16384);s.fault=fault;memset(rx,0xa5,sizeof(rx));
        assert(!vgd_transfer(&b,&d,tx,rx,1032));
        for(unsigned i=0;i<sizeof(rx);i++)assert(rx[i]==0xa5);
        assert(d.quarantined && b.failed && s.tick<=2700 && s.cache_calls==2);
        unsigned writes=s.writes;
        assert(!vgd_transfer(&b,&d,tx,rx,1) && s.writes==writes);
    }
    for(unsigned fail=1;fail<=3;fail++) {
        make(&s,&b,&d,16384);s.cache_fail=fail;memset(rx,0xa5,sizeof(rx));
        assert(!vgd_transfer(&b,&d,tx,rx,1) && rx[0]==0xa5);
        if(fail<3)assert(!s.writes && !d.quarantined);
        else assert(d.quarantined && b.failed);
    }
    for(unsigned invalid=0;invalid<12;invalid++) {
        make(&s,&b,&d,16384);
        if(invalid==0)s.deny=1;
        if(invalid==1)s.mode=0;
        if(invalid==2)d.rx=d.tx;
        if(invalid==3)d.rx_pa=d.tx_pa;
        if(invalid==4)d.tx_pa++;
        if(invalid==5)d.tx_pa=UINT64_C(1)<<36;
        if(invalid==6)d.page_bytes=8192;
        if(invalid==7)d.clean=NULL;
        if(invalid==8)d.authorize=NULL;
        if(invalid==9)d.address_bits=64;
        if(invalid==10)d.quarantined=1;
        if(invalid==11)s.active=1;
        assert(!vgd_transfer(&b,&d,tx,rx,1) && !s.writes && !s.cache_calls);
    }
    make(&s,&b,&d,16384);
    assert(!vgd_transfer(&b,&d,tx,rx,0) && !s.writes);
    assert(!vgd_transfer(&b,&d,tx,rx,1033) && !s.writes);
    assert(!vgd_transfer(NULL,&d,tx,rx,1));
    puts("GENI DMA: lengths 1..1032, 4K/16K pages, high PA, staggered completion, cache failures, retirement PASS");
}
