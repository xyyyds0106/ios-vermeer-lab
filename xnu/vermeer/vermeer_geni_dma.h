/* SPDX-License-Identifier: MIT
 * K70 GENI internal-DMA transfer core. Register facts: pinned MiCode GENI
 * source (53ef33eacdc4f7ac737451e4cfc91963d59e3956). Board integration pending.
 * No allocation, power changes, SMMU bypass or implicit buffer retirement.
 */
#ifndef VERMEER_GENI_DMA_H
#define VERMEER_GENI_DMA_H
#include "vermeer_geni_spi.h"
#include <stddef.h>
#define VGD_TX 0xc30u
#define VGD_RX 0xd30u
#define VGD_STAT 0x10u
#define VGD_CLEAR 0x14u
#define VGD_ENABLE 0x1cu
#define VGD_ERROR 4u
struct vg_dma {
    uint8_t *tx,*rx;
    uint64_t tx_pa,rx_pa;
    size_t page_bytes;
    unsigned address_bits,quarantined;
    void *ctx;
    /* Must verify retained exclusive pages, this board's live DMA stream
     * translation AND powered/deasserted-reset controller before each use.
     * The core never creates that authorization from a DT address alone. */
    int (*authorize)(void *,uint64_t,uint64_t,size_t);
    /* Synchronous PoC operations with DSB on the issuing CPU. Whole dedicated
     * CPU pages are passed. All callbacks return exactly 1 on success. */
    int (*clean)(void *,void *,size_t);
    int (*clean_invalidate)(void *,void *,size_t);
    int (*invalidate)(void *,void *,size_t);
    uint32_t last_tx,last_rx,last_master;
};
static inline int vgd_valid(const struct vg_dma *d)
{
    if(!d || d->quarantined || !d->tx || !d->rx || !d->authorize ||
       !d->clean || !d->clean_invalidate || !d->invalidate ||
       (d->page_bytes!=4096 && d->page_bytes!=16384) ||
       (d->address_bits!=32 && d->address_bits!=36 && d->address_bits!=40))return 0;
    const size_t p=d->page_bytes;
    if((((uintptr_t)d->tx|(uintptr_t)d->rx|d->tx_pa|d->rx_pa)&(p-1)) ||
       (uintptr_t)d->tx>UINTPTR_MAX-p || (uintptr_t)d->rx>UINTPTR_MAX-p ||
       !d->tx_pa || !d->rx_pa)return 0;
    const uint64_t limit=UINT64_C(1)<<d->address_bits;
    if(d->tx_pa>=limit || p>limit-d->tx_pa || d->rx_pa>=limit || p>limit-d->rx_pa)return 0;
    /* Native-page alignment makes distinct pages disjoint. */
    return d->tx!=d->rx && d->tx_pa!=d->rx_pa;
}
static inline int vgd_transfer(struct vg_bus *b,struct vg_dma *d,
    const uint8_t *tx,uint8_t *rx,unsigned n)
{
    if(!b || !b->read || !b->write || !b->pause || b->failed || !vgd_valid(d) ||
       !tx || !rx || !n || n>1032 || n>d->page_bytes)return 0;
    if(!vg_idle(b) || !(vg_rd(b,0x258)&1u) ||
       d->authorize(d->ctx,d->tx_pa,d->rx_pa,d->page_bytes)!=1)return 0;
    for(unsigned i=0;i<n;i++){d->tx[i]=tx[i];d->rx[i]=0;}
    if(d->clean(d->ctx,d->tx,d->page_bytes)!=1 ||
       d->clean_invalidate(d->ctx,d->rx,d->page_bytes)!=1)return 0;
    if(!vg_control(b,8)) {d->quarantined=1;return 0;}
    b->phase=4;
    vg_wr(b,VGD_TX+VGD_CLEAR,0xffffffffu);vg_wr(b,VGD_RX+VGD_CLEAR,0xffffffffu);
    vg_wr(b,VG_CLEAR,0xffffffffu);
    vg_wr(b,0x26c,n);vg_wr(b,0x270,n);
    vg_wr(b,VGD_TX,(uint32_t)d->tx_pa);vg_wr(b,VGD_TX+4,(uint32_t)(d->tx_pa>>32));
    vg_wr(b,VGD_RX,(uint32_t)d->rx_pa);vg_wr(b,VGD_RX+4,(uint32_t)(d->rx_pa>>32));
    vg_wr(b,VGD_TX+8,1);vg_wr(b,VGD_RX+8,0);
    vg_wr(b,VGD_TX+VGD_ENABLE,7);vg_wr(b,VGD_RX+VGD_ENABLE,7);
    /* Full duplex is TX_ONLY|RX_ONLY (3); retain CS for explicit deassert. */
    vg_wr(b,VG_CMD,(3u<<27)|4u);
    vg_wr(b,VGD_RX+12,n);vg_wr(b,VGD_TX+12,n);
    unsigned tx_done=0,rx_done=0,master_done=0;
    for(unsigned i=0;i<2500;i++) {
        uint32_t ts=vg_rd(b,VGD_TX+VGD_STAT),rs=vg_rd(b,VGD_RX+VGD_STAT),ms=vg_rd(b,VG_IRQ);
        d->last_tx=ts;d->last_rx=rs;d->last_master=ms;b->last_irq=ms;
        if((ts|rs)&VGD_ERROR || (ms&VG_ERRORS))break;
        tx_done|=ts&1;rx_done|=rs&1;master_done|=ms&1;
        vg_wr(b,VGD_TX+VGD_CLEAR,ts);vg_wr(b,VGD_RX+VGD_CLEAR,rs);vg_wr(b,VG_CLEAR,ms);
        if(tx_done && rx_done && master_done && vg_idle(b)) {
            if(!vg_control(b,9))break;
            if(d->invalidate(d->ctx,d->rx,d->page_bytes)!=1)break;
            for(unsigned j=0;j<n;j++)rx[j]=d->rx[j];
            return 1;
        }
        b->pause(b->ctx);
    }
    /* An idle command bit alone cannot prove DMA has stopped. Keep both pages
     * and their stream mapping until reboot; never copy partial RX to caller. */
    d->quarantined=1;vg_abort(b);return 0;
}
#endif
