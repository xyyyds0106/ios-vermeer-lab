/* Single-owner polled GENI SPI transport; register facts from MiCode's
 * bsp-vermeer-t-oss 53ef33eacdc4f7ac737451e4cfc91963d59e3956.
 * The platform must establish powered, clocked, SPI-firmware hardware first.
 * No DMA, unbounded wait, firmware loader, allocation or interrupt handler.
 */
#ifndef VERMEER_GENI_SPI_H
#define VERMEER_GENI_SPI_H
#include <stdint.h>
#define VG_STATUS 0x40u
#define VG_CMD 0x600u
#define VG_CTRL 0x604u
#define VG_IRQ 0x610u
#define VG_CLEAR 0x618u
#define VG_TX 0x700u
#define VG_RX 0x780u
#define VG_TX_COUNT 0x800u
#define VG_RX_COUNT 0x804u
#define VG_ERRORS (0x0eu | (3u<<24) | (3u<<28))
struct vg_bus {
    void *ctx;
    uint32_t (*read)(void *,unsigned);
    void (*write)(void *,unsigned,uint32_t);
    void (*pause)(void *); /* bounded 20us on hardware */
    unsigned tx_depth,rx_depth,failed;
    unsigned phase;uint32_t last_irq,last_status;
};
static inline uint32_t vg_pack(unsigned start)
{
    uint32_t a=((start*8+7)<<5)|0x1eu;
    uint32_t b=(((start+1)*8+7)<<5)|0x1eu;
    if(start==2)b|=1u;
    return a|(b<<10);
}
static inline uint32_t vg_rd(struct vg_bus *b,unsigned r){return b->read(b->ctx,r);}
static inline void vg_wr(struct vg_bus *b,unsigned r,uint32_t v){b->write(b->ctx,r,v);}
static inline int vg_idle(struct vg_bus *b){return !(vg_rd(b,VG_STATUS)&0x1001u);}
static inline void vg_abort(struct vg_bus *b)
{
    /* A timeout/error permanently retires this transport. Do not reuse a
     * sequencer whose cancel/abort completion might arrive late.
     */
    b->failed=1;
    b->last_status=vg_rd(b,VG_STATUS);
    if(vg_idle(b))return;
    vg_wr(b,VG_CTRL,4);
    for(unsigned i=0;i<100;i++) {
        if(vg_idle(b))return;
        b->pause(b->ctx);
    }
    vg_wr(b,VG_CTRL,2);
    for(unsigned i=0;i<100;i++) {
        if(vg_idle(b))return;
        b->pause(b->ctx);
    }
}
static inline int vg_control(struct vg_bus *b,unsigned op)
{
    if(b->failed || (op!=8 && op!=9) || !vg_idle(b))return 0;
    b->phase=op==8?1:3;
    vg_wr(b,VG_CLEAR,0xffffffffu);vg_wr(b,VG_CMD,op<<27);
    for(unsigned i=0;i<500;i++) {
        uint32_t irq=vg_rd(b,VG_IRQ);
        b->last_irq=irq;
        if(irq&VG_ERRORS)break;
        if((irq&1) && vg_idle(b)){vg_wr(b,VG_CLEAR,irq);return 1;}
        b->pause(b->ctx);
    }
    vg_abort(b);return 0;
}
static inline int vg_transfer(struct vg_bus *b,const uint8_t *tx,uint8_t *rx,unsigned n)
{
    if(!b || !tx || !rx || !n || n>1032 || !b->read || !b->write || !b->pause ||
       b->tx_depth<4 || b->tx_depth>256 || b->rx_depth<4 || b->rx_depth>256 || b->failed)return 0;
    if(!vg_idle(b))return 0;
    /* Do not inherit stale FIFO contents from another owner. */
    if(vg_rd(b,VG_TX_COUNT) || (vg_rd(b,VG_RX_COUNT)&0x1ffffffu))return 0;
    if(!vg_control(b,8))return 0;
    b->phase=2;
    vg_wr(b,0x26c,n);vg_wr(b,0x270,n);vg_wr(b,VG_CLEAR,0xffffffffu);
    /* Linux's full-duplex FIFO operation is TX_ONLY(1)|RX_ONLY(2), with
     * FRAGMENTATION retaining CS until the explicit deassert command.
     */
    vg_wr(b,VG_CMD,(3u<<27)|4u);
    unsigned sent=0,received=0;int ok=0;
    for(unsigned i=0;i<2500;i++) {
        uint32_t irq=vg_rd(b,VG_IRQ);
        b->last_irq=irq;
        if(irq&VG_ERRORS)break;
        unsigned used=vg_rd(b,VG_TX_COUNT)&0xfffffffu;
        if(used>b->tx_depth)break;
        unsigned room=b->tx_depth-used;
        while(room-- && sent<n) {
            uint32_t word=0;
            for(unsigned j=0;j<4 && sent<n;j++,sent++)word|=(uint32_t)tx[sent]<<(8*j);
            vg_wr(b,VG_TX,word);
        }
        uint32_t status=vg_rd(b,VG_RX_COUNT);unsigned words=status&0x1ffffffu;
        if(words>b->rx_depth)break;
        for(unsigned w=0;w<words;w++) {
            unsigned valid=4;
            if(w+1==words && (status&0x80000000u)) {
                unsigned last=(status>>28)&7u;
                if(last>4){vg_abort(b);return 0;}
                if(last)valid=last;
            }
            uint32_t word=vg_rd(b,VG_RX);
            if(valid>n-received){vg_abort(b);return 0;}
            for(unsigned j=0;j<valid;j++)rx[received++]=(uint8_t)(word>>(8*j));
        }
        if(irq&1) {
            ok=sent==n && received==n && vg_idle(b);
            vg_wr(b,VG_CLEAR,irq);break;
        }
        /* Consume only FIFO level notifications; never acknowledge DONE
         * using a later sample before its RX data has been drained.
         */
        vg_wr(b,VG_CLEAR,irq&((1u<<26)|(1u<<27)|(1u<<30)));
        b->pause(b->ctx);
    }
    if(!ok){vg_abort(b);return 0;}
    return vg_control(b,9);
}
#endif
