#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_touch_route.h"
static uint32_t regs[0xd0000/4];
static unsigned reads,context_reads;
static uint32_t read_reg(void *ctx,unsigned off)
{
    assert(ctx==regs && !(off&3) && off<sizeof(regs));
    reads++;if(off>=0x1000)context_reads++;
    return regs[off/4];
}
static void make(void)
{
    memset(regs,0,sizeof(regs));reads=context_reads=0;
    regs[0x20/4]=0x4c017e69;regs[0x24/4]=0x60000046;regs[0x28/4]=0x5111;regs[0]=0x280406;
    regs[0x800/4]=0x800000a3;regs[0xc00/4]=0x10000;
}
int main(void)
{
    struct vtr_route r;
    make();assert(vtr_identity(regs,read_reg,&r) && r.reason==VTR_OK && r.matches==1 && !context_reads);
    for(unsigned bank=0;bank<70;bank++) {
        make();regs[0xc00/4]=bank;regs[(0x1000+4*bank)/4]=1u<<16;
        assert(vtr_identity(regs,read_reg,&r) && context_reads==2);
        regs[(0x80000+0x1000*bank)/4]=1;
        assert(!vtr_identity(regs,read_reg,&r) && r.reason==VTR_TRANSLATED);
    }
    for(unsigned bank=70;bank<256;bank++) {
        make();regs[0xc00/4]=bank;
        assert(!vtr_identity(regs,read_reg,&r) && r.reason==VTR_CONTEXT && !context_reads);
    }
    for(unsigned i=0;i<105;i++) {
        make();regs[0x800/4]=0;
        regs[(0x800+4*i)/4]=0x800000a3;regs[(0xc00+4*i)/4]=0x10000;
        assert(vtr_identity(regs,read_reg,&r) && r.slot==i);
    }
    make();regs[0x804/4]=0x800000a3;
    assert(!vtr_identity(regs,read_reg,&r) && r.matches==2 && !context_reads);
    make();regs[0x804/4]=0x80ff00a0; /* masked second match */
    assert(!vtr_identity(regs,read_reg,&r) && r.matches==2);
    make();regs[0x800/4]=0x80000040;
    assert(!vtr_identity(regs,read_reg,&r) && !r.matches);
    for(unsigned off=0x20;off<=0x28;off+=4) {
        make();regs[off/4]^=1;assert(!vtr_identity(regs,read_reg,&r) && reads==4 && !context_reads);
    }
    make();regs[0]^=1;assert(!vtr_identity(regs,read_reg,&r) && reads==4);
    make();regs[0xc00/4]=0x20000;assert(!vtr_identity(regs,read_reg,&r));
    make();regs[0xc00/4]=0;assert(!vtr_identity(regs,read_reg,&r));
    for(unsigned cfg=0;cfg<1024;cfg++)for(unsigned io=0;io<4;io++) {
        int expected=!(cfg&0x3c) && (io&1) && (!(cfg&0x200) || (io&2));
        assert(vtr_reset_high(cfg,io)==expected);
    }
    assert(vtr_dma_range(UINT64_C(0x8c0000000),32768,16384));
    assert(vtr_dma_range(UINT64_C(0x940000000)-32768,32768,16384));
    assert(!vtr_dma_range(UINT64_C(0x940000000)-16384,32768,16384));
    assert(!vtr_dma_range(UINT64_MAX-16383,32768,16384));
    assert(!vtr_dma_range(UINT64_C(0x8c0000000)+1,32768,16384));
    assert(!vtr_dma_range(UINT64_C(0x8c0000000),8192,4096));
    assert(vtr_may_restore(0,0,0));
    assert(vtr_may_restore(2,0,2)); /* unrelated status bits */
    assert(!vtr_may_restore(1,0,0)); /* inherited DMA with idle command */
    assert(!vtr_may_restore(0,1,0)); /* our failed DMA with idle command */
    assert(!vtr_may_restore(0,0,1));
    assert(!vtr_may_restore(0,0,0x1000));
    puts("Touch route: unique SIDa3, SMMU geometry/stages/bounds, GPIO pad/latch, native DMA ranges PASS");
}
