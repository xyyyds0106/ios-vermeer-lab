/* SPDX-License-Identifier: MIT
 * K70 GPII0 observation and SPI TRE planning. No command/doorbell writes.
 * Register/packet facts: MiCode 53ef33ea msm_gpi_mmio.h, msm_gpi.h and
 * spi-msm-geni.c. Addresses are relative to DT gpi-top a00000, not ee_base.
 */
#ifndef VERMEER_GPI_H
#define VERMEER_GPI_H
#include <stdint.h>
#include <stddef.h>

enum { VGP_STABLE, VGP_IO, VGP_CHANGED, VGP_UNSAMPLED };
enum { VGP_WORDS=32, VGP_TRES=64, VGP_ELEMENT=16 };
struct vgp_snapshot {
    uint32_t word[VGP_WORDS],valid,changed,reason;
};
/* Two channels and one event context: config, length, base, RP, WP and
 * scratch0; followed by status-only IRQ and error registers. Never read a
 * command, doorbell, interrupt-clear or debug selector register. */
static inline int vgp_offset(unsigned index,unsigned *offset)
{
    if(!offset || index>=VGP_WORDS)return 0;
    if(index<27) {
        unsigned context=index/9,field=index%9;
        unsigned base=context==0?0x10000:context==1?0x10080:0x11000;
        *offset=field<8?base+4*field:context<2?base+0x60:0x13400;
    } else {
        static const unsigned status[5]={0x13080,0x13090,0x13094,0x13100,0x13200};
        *offset=status[index-27];
    }
    /* Vendor ee_base = mapped gpi-top - DT gpi-ee-offset(10000).
     * Thus channel0 is a10000, NOT a20000. All offsets stay in DT range. */
    return *offset<0x60000 && !(*offset&3);
}
static inline void vgp_collect(void *ctx,int (*read)(void *,unsigned,uint32_t *),
    struct vgp_snapshot *out)
{
    if(!out)return;
    *out=(struct vgp_snapshot){.reason=VGP_UNSAMPLED};
    if(!read)return;
    for(unsigned pass=0;pass<2;pass++)for(unsigned i=0;i<VGP_WORDS;i++) {
        unsigned offset;uint32_t value=0,bit=UINT32_C(1)<<i;
        if(!vgp_offset(i,&offset) || !read(ctx,offset,&value)) {
            out->reason=VGP_IO;return;
        }
        if(!pass)out->word[i]=value;
        else {
            out->valid|=bit; /* both reads completed; later fields remain invalid */
            if(out->word[i]!=value)out->changed|=bit;
        }
    }
    if(out->valid==UINT32_MAX)out->reason=out->changed?VGP_CHANGED:VGP_STABLE;
    /* Stable/unallocated is observation only, never a resource lease. */
}
struct vgp_tre {uint32_t word[4];};
struct vgp_spi_plan {struct vgp_tre tx[3],rx;unsigned tx_count,rx_count;};
static inline int vgp_spi_plan(struct vgp_spi_plan *out,uint64_t tx,uint64_t rx,
    unsigned bytes,unsigned clock_source,unsigned divider)
{
    if(!out)return 0;
    *out=(struct vgp_spi_plan){0};
    if(!bytes || bytes>1032 || !divider || divider>0xfff || clock_source>7 ||
       !tx || !rx || tx>UINT64_MAX-bytes || rx>UINT64_MAX-bytes ||
       (tx<rx+bytes && rx<tx+bytes))return 0;
    /* Fixed mode0, CS0, 8-bit words. CONFIG chains into full-duplex GO,
     * GO links RX and chains into TX DMA. TX completion requests IEOT.
     * Planning takes IOVAs; physical addresses are not implicitly IOVAs. */
    out->tx[0]=(struct vgp_tre){{0x03000004,0,(clock_source<<16)|divider,0x00220001}};
    out->tx[1]=(struct vgp_tre){{3,0,bytes,0x00200801}};
    out->tx[2]=(struct vgp_tre){{(uint32_t)tx,(uint32_t)(tx>>32),bytes,0x00100200}};
    out->rx=(struct vgp_tre){{(uint32_t)rx,(uint32_t)(rx>>32),bytes,0x00100000}};
    out->tx_count=3;out->rx_count=1;return 1;
}
#endif
