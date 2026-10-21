/* SPDX-License-Identifier: MIT
 * Original K70 UART GPI byte transport for an already-owned, started GPII.
 * Facts: MiCode 53ef33eacdc4 msm_geni_serial.c, msm_gpi.h,
 * msm_gpi.c and msm_gpi_mmio.h. No dependency on touch's SPI transport.
 * Platform must establish power/firmware/baud/IBS, clocks, pins, GPI context
 * and SID0x436 IOVA mappings. This module submits real UART TREs/doorbells,
 * consumes completion events and synchronizes DMA. It does not establish
 * any of those leases, or implement the Apple Bluetooth user-client ABI.
 */
#ifndef VERMEER_BT_UART_GPI_H
#define VERMEER_BT_UART_GPI_H
#include <stdint.h>
#include <stddef.h>
#include "../vermeer_bt_identity.h"

enum { VBUG_TX, VBUG_RX, VBUG_TR, VBUG_RR, VBUG_ER, VBUG_PAGES };
enum { VBUG_CLEAN, VBUG_CLEAN_INVALIDATE, VBUG_INVALIDATE };
enum { VBUG_OK, VBUG_ARGUMENT, VBUG_LEASE, VBUG_CLOCK, VBUG_IO, VBUG_LAYOUT,
       VBUG_EVENT, VBUG_OVERFLOW, VBUG_TIMEOUT, VBUG_REENTRY, VBUG_CACHE };
enum { VBUG_BOARD=1, VBUG_MAP=2, VBUG_DOMAIN=4, VBUG_GPI=8, VBUG_IOVA=16,
       VBUG_AWAKE=32, VBUG_ROUTE=64, VBUG_REQUIRED=127 };
struct vbug_page { void *cpu; uint64_t iova; };
struct vbug_tre { uint32_t w[4]; };
struct vbug_io {
    void *ctx;
    uint64_t gpi_physical, uart_physical;
    size_t gpi_length, uart_length;
    unsigned gpii, divider, rx_fifo_depth;
    unsigned (*proof)(void *);
    int (*read)(void *, unsigned bank, unsigned offset, uint32_t *);
    int (*write)(void *, unsigned bank, unsigned offset, uint32_t);
    int (*cache)(void *, void *, size_t, unsigned); /* sync + ordered barrier */
    int (*now_us)(void *, uint64_t *);
};
struct vbug {
    struct vbug_io io;
    struct vbug_page page[VBUG_PAGES];
    uint8_t receive[4096];
    uint64_t last, tx_start;
    unsigned error, retired, busy, attached, dma_exposed;
    unsigned tx_wp, rx_wp, ev_rp, tx_pending, tx_end, tx_length, tx_polls;
    unsigned rx_pending, rx_end, rx_started, receive_head, receive_count;
    unsigned events;
};
static inline int vbug_fail(struct vbug *s,unsigned error)
{ s->error=error; s->retired=1; return 0; }
static inline int vbug_guard(struct vbug *s)
{
    uint64_t now;
    if (s->retired) return 0;
    if ((s->io.proof(s->io.ctx)&VBUG_REQUIRED)!=VBUG_REQUIRED)
        return vbug_fail(s,VBUG_LEASE);
    if (s->retired) return 0;
    if (!s->io.now_us(s->io.ctx,&now) || now<s->last)
        return vbug_fail(s,VBUG_CLOCK);
    if (s->retired) return 0;
    s->last=now;
    if (s->tx_pending && now-s->tx_start>=100000)
        return vbug_fail(s,VBUG_TIMEOUT);
    if ((s->io.proof(s->io.ctx)&VBUG_REQUIRED)!=VBUG_REQUIRED)
        return vbug_fail(s,VBUG_LEASE);
    if (s->retired) return 0;
    return 1;
}
static inline unsigned vbug_offset(const struct vbug *s,unsigned offset)
{ return offset+0x4000*s->io.gpii; }
static inline int vbug_rd(struct vbug *s,unsigned bank,unsigned off,uint32_t *value)
{
    if (!vbug_guard(s)) return 0;
    if (!bank) off=vbug_offset(s,off);
    if (!s->io.read(s->io.ctx,bank,off,value)) return vbug_fail(s,VBUG_IO);
    return vbug_guard(s);
}
static inline int vbug_wr(struct vbug *s,unsigned off,uint32_t value)
{
    if (!vbug_guard(s)) return 0;
    s->dma_exposed=1; /* even a failing doorbell may have reached hardware */
    if (!s->io.write(s->io.ctx,0,vbug_offset(s,off),value)) return vbug_fail(s,VBUG_IO);
    return vbug_guard(s);
}
static inline int vbug_sync(struct vbug *s,unsigned page,unsigned op)
{
    if (!vbug_guard(s)) return 0;
    if (!s->io.cache(s->io.ctx,s->page[page].cpu,16384,op)) return vbug_fail(s,VBUG_CACHE);
    return vbug_guard(s);
}
static inline int vbug_pointer(struct vbug *s,unsigned reg,unsigned page,unsigned bytes,unsigned *index)
{
    uint32_t lo,hi,again;
    if (!vbug_rd(s,0,reg+4,&hi) || !vbug_rd(s,0,reg,&lo) ||
        !vbug_rd(s,0,reg+4,&again)) return 0;
    uint64_t ptr=((uint64_t)hi<<32)|lo, base=s->page[page].iova;
    if (hi!=again || ptr<base || ptr-base>=bytes || ((ptr-base)&15))
        return vbug_fail(s,VBUG_LAYOUT);
    *index=(unsigned)((ptr-base)/16); return 1;
}
static inline struct vbug_tre vbug_config(const struct vbug *s,unsigned rx)
{
    /* 8N1, hardware CTS, no loopback/hunt; parity encoding4=none.
     * Source sets packing bit24. RX RFR is depth-2, stale=10*16 bits.
     */
    return (struct vbug_tre){{0x01000083,rx?((s->io.rx_fifo_depth-2)<<24)|160:0,
        s->io.divider,0x00220001}};
}
static inline void vbug_put(struct vbug *s,unsigned page,unsigned *wp,struct vbug_tre tre)
{ ((struct vbug_tre *)s->page[page].cpu)[*wp]=tre; *wp=(*wp+1)&63; }
static inline int vbug_queue_rx(struct vbug *s)
{
    if (!vbug_guard(s)) return 0;
    if (s->rx_pending) return vbug_fail(s,VBUG_EVENT);
    if (!vbug_sync(s,VBUG_RX,VBUG_CLEAN_INVALIDATE)) return 0;
    if (!s->rx_started) {
        vbug_put(s,VBUG_RR,&s->rx_wp,vbug_config(s,1));
        vbug_put(s,VBUG_RR,&s->rx_wp,(struct vbug_tre){{1,0,0,0x00200001}});
        s->rx_started=1;
    }
    s->rx_end=s->rx_wp;
    uint64_t iova=s->page[VBUG_RX].iova;
    vbug_put(s,VBUG_RR,&s->rx_wp,(struct vbug_tre){{(uint32_t)iova,
        (uint32_t)(iova>>32),2048,0x00100301}});
    if (!vbug_sync(s,VBUG_RR,VBUG_CLEAN)) return 0;
    s->rx_pending=1;
    return vbug_wr(s,0x12008,(uint32_t)(s->page[VBUG_RR].iova+16*s->rx_wp));
}
/* Fresh preconfigured empty rings only; never steals/takes over a context.
 * GPI0x800000 SID0x436 is saved K70 evidence; GPII1..5 are dynamic candidates.
 * Saved UART node has NO dmas property. ROUTE must therefore come from an
 * independently qualified live provider binding SE6 to this exclusive GPII;
 * mere mask membership is insufficient. No provider exists in current image.
 */
static inline int vbug_attach(struct vbug *s,const struct vbug_io *io,
    const struct vbug_page pages[VBUG_PAGES])
{
    if (!s || !io || !pages) return 0;
    *s=(struct vbug){0}; s->io=*io;
    if (!io->proof || !io->read || !io->write || !io->cache || !io->now_us ||
        io->gpi_physical!=0x800000 || io->gpi_length!=0x60000 ||
        io->uart_physical!=0x898000 || io->uart_length!=0x4000 ||
        io->gpii<1 || io->gpii>5 || !io->divider || io->divider>4095 ||
        io->rx_fifo_depth<3 || io->rx_fifo_depth>255) return vbug_fail(s,VBUG_ARGUMENT);
    for (unsigned i=0;i<VBUG_PAGES;i++) {
        uintptr_t cpu=(uintptr_t)pages[i].cpu; uint64_t va=pages[i].iova;
        if (!cpu || !va || ((cpu|va)&16383) || cpu>UINTPTR_MAX-16384 ||
            va>=(UINT64_C(1)<<40) || 16384>(UINT64_C(1)<<40)-va)
            return vbug_fail(s,VBUG_ARGUMENT);
        for (unsigned j=0;j<i;j++) if (pages[j].cpu==pages[i].cpu || pages[j].iova==va)
            return vbug_fail(s,VBUG_ARGUMENT);
        s->page[i]=pages[i];
    }
    if (!io->now_us(io->ctx,&s->last)) return vbug_fail(s,VBUG_CLOCK);
    s->busy=1;
    uint32_t v;
    if (!vbug_rd(s,1,0x68,&v)) goto fail;
    if (((v>>8)&255)!=2) { vbug_fail(s,VBUG_LAYOUT); goto fail; }
    if (!vbug_rd(s,1,0x64,&v)) goto fail;
    if (!(v&1)) { vbug_fail(s,VBUG_LAYOUT); goto fail; }
    for (unsigned ch=0;ch<3;ch++) {
        unsigned base=ch==2?0x11000:0x10000+ch*0x80;
        unsigned page=ch==2?VBUG_ER:ch==1?VBUG_RR:VBUG_TR;
        unsigned bytes=ch==2?4096:1024;
        if (!vbug_rd(s,0,base,&v)) goto fail;
        if ((v>>24)!=16 || ((v>>20)&15)!=(ch==2?1u:2u) ||
            (v&15)!=(ch==0?10u:2u) || (ch<2 && ((v>>14)&31))) {
            vbug_fail(s,VBUG_LAYOUT); goto fail;
        }
        if (!vbug_rd(s,0,base+4,&v)) goto fail;
        if (v!=bytes) { vbug_fail(s,VBUG_LAYOUT); goto fail; }
        uint32_t lo,hi;
        if (!vbug_rd(s,0,base+8,&lo) || !vbug_rd(s,0,base+12,&hi)) goto fail;
        if ((((uint64_t)hi<<32)|lo)!=s->page[page].iova) { vbug_fail(s,VBUG_LAYOUT); goto fail; }
        unsigned index;
        if (!vbug_pointer(s,base+0x10,page,bytes,&index)) goto fail;
        if (index) { vbug_fail(s,VBUG_LAYOUT); goto fail; }
        if (ch<2) {
            if (!vbug_pointer(s,base+0x18,page,bytes,&index)) goto fail;
            if (index) { vbug_fail(s,VBUG_LAYOUT); goto fail; }
            if (!vbug_rd(s,0,base+0x60,&v)) goto fail;
            if (v!=((ch?0u:0x10000u)|0x26)) { vbug_fail(s,VBUG_LAYOUT); goto fail; }
        }
    }
    if (!vbug_rd(s,0,0x13100,&v)) goto fail;
    if (v) { vbug_fail(s,VBUG_EVENT); goto fail; }
    if (!vbug_rd(s,0,0x13118,&v)) goto fail;
    if (v) { vbug_fail(s,VBUG_EVENT); goto fail; }
    s->attached=1;
    if (!vbug_queue_rx(s)) goto fail;
    s->busy=0; return 1;
fail:
    s->busy=0; return 0;
}
/* Serialized/nonblocking: bounded 32 events per call, preserves event order.
 * Completion pointer must match the one outstanding DMA descriptor; no
 * immediate-data, notification or invented success events are accepted.
 */
static inline int vbug_poll(struct vbug *s)
{
    if (!s || !s->attached || s->retired) return 0;
    if (s->busy) return vbug_fail(s,VBUG_REENTRY);
    s->busy=1;
    uint32_t v;
    if (!vbug_guard(s)) goto fail;
    if (s->tx_pending && ++s->tx_polls>4096) { vbug_fail(s,VBUG_TIMEOUT); goto fail; }
    if (!vbug_rd(s,0,0x13100,&v)) goto fail;
    if (v) { vbug_fail(s,VBUG_EVENT); goto fail; }
    if (!vbug_rd(s,0,0x13118,&v)) goto fail;
    if (v) { vbug_fail(s,VBUG_EVENT); goto fail; }
    unsigned producer;
    if (!vbug_pointer(s,0x11010,VBUG_ER,4096,&producer)) goto fail;
    if (producer!=s->ev_rp) {
        if (!vbug_sync(s,VBUG_ER,VBUG_INVALIDATE)) goto fail;
        unsigned events=0;
        while (producer!=s->ev_rp && events++<32) {
            struct vbug_tre event=((struct vbug_tre *)s->page[VBUG_ER].cpu)[s->ev_rp];
            unsigned ch=event.w[3]>>24,type=(event.w[3]>>16)&255;
            unsigned status=event.w[3]&65535,code=event.w[2]>>24,n=event.w[2]&0xffffff;
            uint64_t ptr=((uint64_t)event.w[1]<<32)|event.w[0];
            if (type!=0x22 || ch>1 || status || (code!=2 && code!=4)) {
                vbug_fail(s,VBUG_EVENT); goto fail;
            }
            if (!ch) {
                if (!s->tx_pending || code!=2 || n!=s->tx_length ||
                    ptr!=s->page[VBUG_TR].iova+16*s->tx_end) { vbug_fail(s,VBUG_EVENT); goto fail; }
                s->tx_pending=0;
            } else {
                if (!s->rx_pending || n>2048 ||
                    ptr!=s->page[VBUG_RR].iova+16*s->rx_end) { vbug_fail(s,VBUG_EVENT); goto fail; }
                if (n>4096-s->receive_count) { vbug_fail(s,VBUG_OVERFLOW); goto fail; }
                if (!vbug_sync(s,VBUG_RX,VBUG_INVALIDATE)) goto fail;
                if (!vbug_guard(s)) goto fail;
                for (unsigned i=0;i<n;i++)
                    s->receive[(s->receive_head+s->receive_count+i)&4095]=((uint8_t *)s->page[VBUG_RX].cpu)[i];
                s->receive_count+=n; s->rx_pending=0;
                if (!vbug_queue_rx(s)) goto fail;
            }
            s->ev_rp=(s->ev_rp+1)&255; ++s->events;
        }
        if (!vbug_wr(s,0x12100,(uint32_t)(s->page[VBUG_ER].iova+16*((s->ev_rp+255)&255))) ||
            !vbug_wr(s,0x130c0,1)) goto fail;
    }
    if (!vbug_guard(s)) goto fail;
    s->busy=0; return 1;
fail:
    s->busy=0; return 0;
}
/* Returns bytes copied/queued, 0 would-block, -1 retired. Once a TX prefix
 * has been submitted it is never retransmitted after a failure. RX stays
 * armed, so ALL pages/mappings/domains remain owned even after a completed
 * HCI query. Only platform-qualified channel stop/recovery can release them.
 */
static inline int vbug_write(void *ctx,const uint8_t *bytes,size_t n)
{
    struct vbug *s=ctx;
    if (!s || (!bytes && n) || n>4096) return -1;
    if (!vbug_poll(s)) return -1;
    if (!n || s->tx_pending) return 0;
    s->busy=1;
    if (!vbug_guard(s)) goto fail;
    for (size_t i=0;i<n;i++) ((uint8_t *)s->page[VBUG_TX].cpu)[i]=bytes[i];
    if (!vbug_sync(s,VBUG_TX,VBUG_CLEAN)) goto fail;
    vbug_put(s,VBUG_TR,&s->tx_wp,vbug_config(s,0));
    vbug_put(s,VBUG_TR,&s->tx_wp,(struct vbug_tre){{1,0,0,0x00200001}});
    s->tx_end=s->tx_wp;
    uint64_t va=s->page[VBUG_TX].iova;
    vbug_put(s,VBUG_TR,&s->tx_wp,(struct vbug_tre){{(uint32_t)va,(uint32_t)(va>>32),(uint32_t)n,0x00100200}});
    if (!vbug_sync(s,VBUG_TR,VBUG_CLEAN)) goto fail;
    s->tx_start=s->last; s->tx_pending=1; s->tx_length=(unsigned)n; s->tx_polls=0;
    if (!vbug_wr(s,0x12000,(uint32_t)(s->page[VBUG_TR].iova+16*s->tx_wp))) goto fail;
    s->busy=0; return (int)n;
fail:
    s->busy=0; return -1;
}
static inline int vbug_read(void *ctx,uint8_t *bytes,size_t n)
{
    struct vbug *s=ctx;
    if (!s || (!bytes && n) || n>4096) return -1;
    if (!vbug_poll(s)) return -1;
    unsigned count=s->receive_count;
    if (n<count) count=(unsigned)n;
    for (unsigned i=0;i<count;i++) bytes[i]=s->receive[(s->receive_head+i)&4095];
    s->receive_head=(s->receive_head+count)&4095; s->receive_count-=count;
    return (int)count;
}
/* Adapter for existing bounded standard version query. The owner supplies
 * initial clean RX + command credit after firmware/IBS initialization. */
static inline uint64_t vbug_identity_time(void *ctx)
{
    struct vbug *s=ctx;
    if (!vbug_guard(s)) return s->last;
    return s->last;
}
static inline unsigned vbug_identity_proof(void *ctx)
{
    struct vbug *s=ctx;
    return s->attached && !s->retired && vbug_guard(s)?VBT_ID_REQUIRED:0;
}
static inline int vbug_identity_link(struct vbug *s,struct vbt_identity_io *out,
    unsigned initial_clean_rx_and_credit)
{
    if (!s || !out || initial_clean_rx_and_credit!=1 || s->receive_count ||
        s->tx_pending || !s->attached || s->retired) return 0;
    *out=(struct vbt_identity_io){.ctx=s,.read=vbug_read,.write=vbug_write,
        .now_us=vbug_identity_time,.preconditions=vbug_identity_proof};
    return 1;
}
#endif
