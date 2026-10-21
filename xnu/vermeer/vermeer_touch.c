/* Experimental K70 GT9916 / GENI SPI SE4 physical input.
 * Only --ios-gui + vermeer_touch=1 + exact physical-model can enter here.
 * Fixed DT/official MiCode clock and pin contract; no PMIC/reset writes,
 * firmware/config upload, storage access or arbitrary debug MMIO command.
 * Reuses controller firmware/power left by ABL; absent identity stops safely.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <mach/thread_act.h>
#include <machine/machine_routines.h>
#include <machine/atomic.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include "vermeer_geni_spi.h"
#include "vermeer_touch_route.h"
#include "vermeer_goodix_info.h"
#include "vermeer_touch_clock.h"
extern vm_offset_t ml_io_map(vm_offset_t,vm_size_t);
extern void sweet_hid_touch(int,int,int,int);
extern int sweet_hid_touch_ready(void);
extern void vermeer_debug_input_register(void (*)(void));
void vermeer_touch_start(void);

#define VT_SE 0x00a90000u
#define VT_GCC 0x00100000u
#define VT_GPIO 0x0f100000u /* TLMM base f000000 + Kalama REG_BASE100000 */
#define VT_VOTE 0x52008u
#define VT_CLOCKS VTC_VOTES
#define VT_RCG 0x184f0u
/* Wrapper1 core2x/core/master/slave AHB, SE4. Never SE3. */
static const unsigned vt_halt[5]={0x23294,0x23284,0x2327c,0x23280,0x184e4};
static const unsigned vt_offsets[]={0x20,0x24,0x28,0x48,0x7c,0x224,0x22c,
    0x230,0x24c,0x250,0x254,0x258,0x25c,0x260,0x264,0x268,0x274,0x278,
    0x284,0x288,0x614,0x644,0x80c,0x810,0x814,0xe18,0xe1c,0xe30};
#define VT_NREGS (sizeof(vt_offsets)/sizeof(vt_offsets[0]))
static struct {
    vm_offset_t se,gcc,gpio,smmu;
    uint32_t vote_added,pins[4],saved[VT_NREGS];
    bool pins_saved,engine_saved,root_forced;
    struct vg_bus bus;
    struct vt_info info;
    struct vt_frame contacts;
    unsigned _Atomic stage;
    uint64_t _Atomic reports,bad,transactions,emitted;
    uint32_t _Atomic last_header;
    uint32_t _Atomic clock_vote,clock_master,clock_slave,clock_se,clock_root,clock_cfg,clock_dfs;
    uint32_t _Atomic clock_core2x,clock_core;
    uint32_t _Atomic spi_phase,spi_irq,spi_status;
    uint32_t _Atomic reset_cfg,reset_io,irq_cfg,irq_io;
    uint32_t _Atomic route_reason,route_matches,route_slot,route_smr,route_s2cr,route_cbar,route_sctlr;
    uint32_t _Atomic route_id0,route_id1,route_id2,route_cr0;
    uint32_t _Atomic dma_tx_status,dma_rx_status,gsi_events,irq_route,geni_status;
    uint32_t _Atomic gpi_reason,gpi_matches,gpi_slot,gpi_smr,gpi_s2cr,gpi_cbar,gpi_sctlr;
    uint32_t _Atomic inherited_dma_mode,inherited_tx_in,inherited_rx_in;
    uint8_t tx[1032],rx[1032];
} touch={.route_reason=VTR_UNSAMPLED,.gpi_reason=VTR_UNSAMPLED};
enum { VT_WAIT=1,VT_CLOCK,VT_FIRMWARE,VT_IDENTITY,VT_READY,VT_STOPPED };
static uint32_t vt_rd(vm_offset_t base,unsigned reg)
{
    uint32_t v=*(volatile uint32_t *)(base+reg);
    __asm__ volatile("dmb oshld" ::: "memory");return v;
}
static void vt_wr(vm_offset_t base,unsigned reg,uint32_t v)
{
    *(volatile uint32_t *)(base+reg)=v;
    __asm__ volatile("dsb sy" ::: "memory");
}
static uint32_t vt_read(void *ctx,unsigned reg)
{ (void)ctx;return vt_rd(touch.se,reg); }
static void vt_write(void *ctx,unsigned reg,uint32_t v)
{ (void)ctx;vt_wr(touch.se,reg,v); }
static void vt_pause(void *ctx){(void)ctx;delay(20);}
static void vt_sleep(unsigned ms)
{
    uint64_t deadline;clock_interval_to_deadline(ms,NSEC_PER_MSEC,&deadline);
    assert_wait_deadline((event_t)&touch,THREAD_UNINT,deadline);
    thread_block(THREAD_CONTINUE_NULL);
}
static void vt_status(void)
{
    printf("VERMEER TOUCH: stage=%u reports=%llu bad=%llu spi=%llu emitted=%llu header=%08x source=physical-spi\n",
        os_atomic_load(&touch.stage,relaxed),os_atomic_load(&touch.reports,relaxed),
        os_atomic_load(&touch.bad,relaxed),os_atomic_load(&touch.transactions,relaxed),
        os_atomic_load(&touch.emitted,relaxed),os_atomic_load(&touch.last_header,relaxed));
    printf("VERMEER TOUCH: clock-snapshot vote=%08x core2x=%08x core=%08x master=%08x slave=%08x se4=%08x root=%08x cfg=%08x dfs=%08x\n",
        os_atomic_load(&touch.clock_vote,relaxed),os_atomic_load(&touch.clock_core2x,relaxed),
        os_atomic_load(&touch.clock_core,relaxed),os_atomic_load(&touch.clock_master,relaxed),
        os_atomic_load(&touch.clock_slave,relaxed),os_atomic_load(&touch.clock_se,relaxed),
        os_atomic_load(&touch.clock_root,relaxed),os_atomic_load(&touch.clock_cfg,relaxed),os_atomic_load(&touch.clock_dfs,relaxed));
    printf("VERMEER TOUCH: spi-snapshot phase=%u irq=%08x status=%08x\n",
        os_atomic_load(&touch.spi_phase,relaxed),os_atomic_load(&touch.spi_irq,relaxed),os_atomic_load(&touch.spi_status,relaxed));
    printf("VERMEER TOUCH: gpio-snapshot reset-cfg=%08x reset-io=%08x irq-cfg=%08x irq-io=%08x\n",
        os_atomic_load(&touch.reset_cfg,relaxed),os_atomic_load(&touch.reset_io,relaxed),
        os_atomic_load(&touch.irq_cfg,relaxed),os_atomic_load(&touch.irq_io,relaxed));
    printf("VERMEER TOUCH: SIDa3 layout=%08x/%08x/%08x cr0=%08x reason=%u matches=%u slot=%u smr=%08x s2cr=%08x cbar=%08x sctlr=%08x\n",
        os_atomic_load(&touch.route_id0,relaxed),os_atomic_load(&touch.route_id1,relaxed),
        os_atomic_load(&touch.route_id2,relaxed),os_atomic_load(&touch.route_cr0,relaxed),
        os_atomic_load(&touch.route_reason,relaxed),os_atomic_load(&touch.route_matches,relaxed),
        os_atomic_load(&touch.route_slot,relaxed),os_atomic_load(&touch.route_smr,relaxed),
        os_atomic_load(&touch.route_s2cr,relaxed),os_atomic_load(&touch.route_cbar,relaxed),os_atomic_load(&touch.route_sctlr,relaxed));
    printf("VERMEER TOUCH: inherited transport tx-status=%08x rx-status=%08x mode=%08x tx-in=%08x rx-in=%08x gsi=%08x irq-route=%08x status=%08x\n",
        os_atomic_load(&touch.dma_tx_status,relaxed),os_atomic_load(&touch.dma_rx_status,relaxed),
        os_atomic_load(&touch.inherited_dma_mode,relaxed),os_atomic_load(&touch.inherited_tx_in,relaxed),
        os_atomic_load(&touch.inherited_rx_in,relaxed),os_atomic_load(&touch.gsi_events,relaxed),
        os_atomic_load(&touch.irq_route,relaxed),os_atomic_load(&touch.geni_status,relaxed));
    printf("VERMEER TOUCH: SIDb6 GPI reason=%u matches=%u slot=%u smr=%08x s2cr=%08x cbar=%08x sctlr=%08x\n",
        os_atomic_load(&touch.gpi_reason,relaxed),os_atomic_load(&touch.gpi_matches,relaxed),
        os_atomic_load(&touch.gpi_slot,relaxed),os_atomic_load(&touch.gpi_smr,relaxed),
        os_atomic_load(&touch.gpi_s2cr,relaxed),os_atomic_load(&touch.gpi_cbar,relaxed),os_atomic_load(&touch.gpi_sctlr,relaxed));
}
static int vt_reset_ready(void)
{
    uint32_t cfg=vt_rd(touch.gpio,24*0x1000),io=vt_rd(touch.gpio,24*0x1000+4);
    os_atomic_store(&touch.reset_cfg,cfg,relaxed);os_atomic_store(&touch.reset_io,io,relaxed);
    os_atomic_store(&touch.irq_cfg,vt_rd(touch.gpio,25*0x1000),relaxed);
    os_atomic_store(&touch.irq_io,vt_rd(touch.gpio,25*0x1000+4),relaxed);
    return vtr_reset_high(cfg,io);
}
static uint32_t vt_smmu_read(void *ctx,unsigned reg)
{(void)ctx;return vt_rd(touch.smmu,reg);}
static int vt_route_ready(void)
{
    struct vtr_route r;
    int ok=vtr_identity(NULL,vt_smmu_read,&r);
#define VT_ROUTE_SAVE(field) os_atomic_store(&touch.route_##field,r.field,relaxed)
    VT_ROUTE_SAVE(reason);VT_ROUTE_SAVE(matches);VT_ROUTE_SAVE(slot);VT_ROUTE_SAVE(smr);
    VT_ROUTE_SAVE(s2cr);VT_ROUTE_SAVE(cbar);VT_ROUTE_SAVE(sctlr);VT_ROUTE_SAVE(id0);
    VT_ROUTE_SAVE(id1);VT_ROUTE_SAVE(id2);VT_ROUTE_SAVE(cr0);
#undef VT_ROUTE_SAVE
    return ok;
}
static void vt_gpi_route_snapshot(void)
{
    struct vtr_route r;(void)vtr_identity_sid(NULL,vt_smmu_read,0xb6,&r);
#define VT_GPI_SAVE(field) os_atomic_store(&touch.gpi_##field,r.field,relaxed)
    VT_GPI_SAVE(reason);VT_GPI_SAVE(matches);VT_GPI_SAVE(slot);VT_GPI_SAVE(smr);
    VT_GPI_SAVE(s2cr);VT_GPI_SAVE(cbar);VT_GPI_SAVE(sctlr);
#undef VT_GPI_SAVE
}
static int vt_transfer(unsigned n)
{
    int ok=vg_transfer(&touch.bus,touch.tx,touch.rx,n);
    os_atomic_store(&touch.spi_phase,touch.bus.phase,relaxed);os_atomic_store(&touch.spi_irq,touch.bus.last_irq,relaxed);
    os_atomic_store(&touch.spi_status,touch.bus.last_status,relaxed);
    return ok;
}
static void vt_emit_hid(void *ctx,unsigned id,int down,unsigned x,unsigned y)
{
    (void)ctx;sweet_hid_touch((int)id,down,(int)x,(int)y);
    os_atomic_inc(&touch.emitted,relaxed);
}
static int vt_spi_read(uint32_t addr,uint8_t *out,unsigned n)
{
    if(!n || n>1024)return 0;
    bzero(touch.tx,n+8);touch.tx[0]=0xf1;
    for(unsigned i=0;i<4;i++)touch.tx[1+i]=(uint8_t)(addr>>(24-i*8));
    touch.tx[5]=touch.tx[6]=touch.tx[7]=0xff;
    int ok=vt_transfer(n+8);
    if(!ok)return 0;
    bcopy(touch.rx+8,out,n);os_atomic_inc(&touch.transactions,relaxed);return 1;
}
static int vt_ack_event(void)
{
    /* The only controller data write: clear the verified event status byte.
     * info.event is accepted only after GT9916 identity and IC-info checks.
     */
    uint32_t addr=touch.info.event;touch.tx[0]=0xf0;
    for(unsigned i=0;i<4;i++)touch.tx[i+1]=(uint8_t)(addr>>(24-i*8));
    touch.tx[5]=0;
    int ok=vt_transfer(6);
    if(!ok)return 0;
    os_atomic_inc(&touch.transactions,relaxed);return 1;
}
static int vt_clock_enable(void)
{
    touch.gcc=ml_io_map(VT_GCC,0x54000);
    if(!touch.gcc)return 0;
    uint32_t before=vt_rd(touch.gcc,VT_VOTE);
    touch.vote_added=VT_CLOCKS&~before;
    vt_wr(touch.gcc,VT_VOTE,before|VT_CLOCKS);
    for(unsigned i=0;i<1000;i++) {
        uint32_t votes=vt_rd(touch.gcc,VT_VOTE),core2x=vt_rd(touch.gcc,vt_halt[0]),core=vt_rd(touch.gcc,vt_halt[1]);
        uint32_t master=vt_rd(touch.gcc,vt_halt[2]),slave=vt_rd(touch.gcc,vt_halt[3]),se=vt_rd(touch.gcc,vt_halt[4]);
        uint32_t cmd=vt_rd(touch.gcc,VT_RCG);
        os_atomic_store(&touch.clock_vote,votes,relaxed);os_atomic_store(&touch.clock_master,master,relaxed);
        os_atomic_store(&touch.clock_core2x,core2x,relaxed);os_atomic_store(&touch.clock_core,core,relaxed);
        os_atomic_store(&touch.clock_slave,slave,relaxed);os_atomic_store(&touch.clock_se,se,relaxed);
        os_atomic_store(&touch.clock_root,cmd,relaxed);os_atomic_store(&touch.clock_cfg,vt_rd(touch.gcc,VT_RCG+4),relaxed);
        os_atomic_store(&touch.clock_dfs,vt_rd(touch.gcc,VT_RCG+0x14),relaxed);
        if(vtc_ready(votes,core2x,core,master,slave,se)){vt_status();return 1;}
        /* Match clk_rcg2_set_force_enable when SE4's root is parked. This
         * requests the existing root only: no rate, parent, DFS or PLL edit.
         * The bit is undone on failure after the engine is quiescent.
         */
        if(i==100 && (votes&VT_CLOCKS)==VT_CLOCKS && (se&0x80000000u) &&
           (cmd&0x80000000u) && !(cmd&3u)) {
            touch.root_forced=true;vt_wr(touch.gcc,VT_RCG,cmd|2u);
        }
        delay(10);
    }
    printf("VERMEER TOUCH: SE4/AHB clock vote did not become ready\n");return 0;
}
static int vt_engine_setup(void)
{
    touch.se=ml_io_map(VT_SE,0x4000);touch.gpio=ml_io_map(VT_GPIO,0x30000);
    if(!touch.se || !touch.gpio)return 0;
    touch.bus=(struct vg_bus){.ctx=NULL,.read=vt_read,.write=vt_write,.pause=vt_pause};
    unsigned proto=(vt_rd(touch.se,0x68)>>8)&255u;
    uint32_t tx=vt_rd(touch.se,0xe24),rx=vt_rd(touch.se,0xe28);
    touch.bus.tx_depth=(tx>>16)&255u;touch.bus.rx_depth=(rx>>16)&255u;
    uint32_t interface=vt_rd(touch.se,0x64);
    uint32_t inherited_dma_mode=vt_rd(touch.se,0x258);
    uint32_t gsi_events=vt_rd(touch.se,0xe18),status=vt_rd(touch.se,VG_STATUS);
    os_atomic_store(&touch.gsi_events,gsi_events,relaxed);os_atomic_store(&touch.geni_status,status,relaxed);
    os_atomic_store(&touch.irq_route,vt_rd(touch.se,0xe1c),relaxed);
    os_atomic_store(&touch.dma_tx_status,vt_rd(touch.se,0xc40),relaxed);
    os_atomic_store(&touch.dma_rx_status,vt_rd(touch.se,0xd40),relaxed);
    os_atomic_store(&touch.spi_status,status,relaxed);
    os_atomic_store(&touch.inherited_dma_mode,inherited_dma_mode,relaxed);
    os_atomic_store(&touch.inherited_tx_in,vt_rd(touch.se,0xc54),relaxed);
    os_atomic_store(&touch.inherited_rx_in,vt_rd(touch.se,0xd54),relaxed);
    printf("VERMEER TOUCH: SE4 firmware=%u TX=%08x RX=%08x reset=%08x interface=%08x\n",
        proto,tx,rx,vt_rd(touch.gpio,24*0x1000+4),interface);
    if(proto!=1 || ((tx>>24)&63)!=32 || ((rx>>24)&63)!=32 ||
       touch.bus.tx_depth<4 || touch.bus.rx_depth<4 || !vg_idle(&touch.bus))return 0;
    unsigned transport=vtr_transport(proto,interface,inherited_dma_mode,gsi_events,status);
    if(transport==VTR_TRANSPORT_GPI_REQUIRED) {
        touch.smmu=ml_io_map(0x15000000,0xd0000);
        if(!touch.smmu)return 0;
        /* qcom,spi-geni downstream requires GPI when FIFO is disabled.
         * K70 DT maps its provider a00000 via SIDb6; SIDa3 is wrapper DMA.
         * Observe both routes; never substitute internal DMA or add bypass. */
        (void)vt_route_ready();vt_gpi_route_snapshot();(void)vt_reset_ready();
        printf("VERMEER TOUCH: GPI required by K70 vendor contract; internal DMA forbidden, GPI backend unavailable\n");
        return 0;
    }
    if(transport!=VTR_TRANSPORT_FIFO) {printf("VERMEER TOUCH: inherited DMA/GPI state not owned; FIFO refused\n");return 0;}
    /* ABL must already have the controller out of reset. Do not power-cycle
     * unknown rails or substitute an unverified reset/firmware sequence.
     */
    if(!vt_reset_ready()) {printf("VERMEER TOUCH: inherited reset pad not high; power/reset setup required\n");return 0;}
    for(unsigned i=0;i<VT_NREGS;i++)touch.saved[i]=vt_rd(touch.se,vt_offsets[i]);
    touch.engine_saved=true;
    for(unsigned i=0;i<4;i++) {
        unsigned reg=(44+i)*0x1000;touch.pins[i]=vt_rd(touch.gpio,reg);
        /* Exact active DT: ALT1, 6mA; pulldown MISO/MOSI/SCLK, pullup CS. */
        uint32_t value=(touch.pins[i]&~0x3ffu)|4u|(2u<<6)|(i==3?3u:1u);
        vt_wr(touch.gpio,reg,value);
    }
    touch.pins_saved=true;
    vt_wr(touch.se,0x614,0);vt_wr(touch.se,0x644,0);vt_wr(touch.se,0xe1c,0);
    vt_wr(touch.se,0xe18,0);vt_wr(touch.se,0x258,0); /* FIFO only; no internal DMA/GPI takeover */
    vt_wr(touch.se,0x28,vt_rd(touch.se,0x28)|0x7f);
    vt_wr(touch.se,0xe30,vt_rd(touch.se,0xe30)|0xf);
    vt_wr(touch.se,0x24,0x7f);vt_wr(touch.se,0x20,1);
    vt_wr(touch.se,0x224,0);vt_wr(touch.se,0x22c,0);vt_wr(touch.se,0x230,0);
    vt_wr(touch.se,0x24c,0);vt_wr(touch.se,0x250,0);vt_wr(touch.se,0x25c,0);
    vt_wr(touch.se,0x268,4);vt_wr(touch.se,0x254,0);
    /* MSB-first 4x8 packing, vectors start7,15,23,31; final stop bit. */
    vt_wr(touch.se,0x260,vg_pack(0));vt_wr(touch.se,0x264,vg_pack(2));
    vt_wr(touch.se,0x284,vg_pack(0));vt_wr(touch.se,0x288,vg_pack(2));
    vt_wr(touch.se,0x274,0);vt_wr(touch.se,0x278,0);
    vt_wr(touch.se,0x80c,0);vt_wr(touch.se,0x810,0);vt_wr(touch.se,0x814,touch.bus.rx_depth-2);
    /* Raw status latches enabled, controller delivery disabled for polling. */
    vt_wr(touch.se,0x614,0x7eu|VG_ERRORS|1u|(1u<<26)|(1u<<27)|(1u<<30));
    /* Retain the existing GCC root/DFS/PLL configuration and use a large
     * internal divider (128). No clock-rate or regulator programming.
     */
    vt_wr(touch.se,0x7c,0);vt_wr(touch.se,0x48,(128u<<4)|1u);
    vg_wr(&touch.bus,VG_CLEAR,0xffffffffu);
    return 1;
}
static void vt_restore(void)
{
    /* If abort did not quiesce, leave clocks/registers retained until reboot.
     * Disabling an active engine could turn a recoverable error into a fault.
     */
    if(os_atomic_load(&touch.gsi_events,relaxed) || !vtr_may_restore(os_atomic_load(&touch.inherited_dma_mode,relaxed),0,
        touch.se?vt_rd(touch.se,VG_STATUS):0)) {
        printf("VERMEER TOUCH: unowned DMA or uncertain engine retained; no register/pin/clock restore\n");return;
    }
    if(touch.engine_saved) {
        vt_wr(touch.se,0x20,1); /* inactive chip select after a cancelled transfer */
        vg_wr(&touch.bus,VG_CLEAR,0xffffffffu);vt_wr(touch.se,0x648,0xffffffffu);
        for(unsigned i=0;i<VT_NREGS;i++)vt_wr(touch.se,vt_offsets[i],touch.saved[i]);
    }
    if(touch.pins_saved)for(unsigned i=0;i<4;i++)vt_wr(touch.gpio,(44+i)*0x1000,touch.pins[i]);
    if(touch.gcc && touch.root_forced)vt_wr(touch.gcc,VT_RCG,vt_rd(touch.gcc,VT_RCG)&~2u);
    if(touch.gcc && touch.vote_added)
        vt_wr(touch.gcc,VT_VOTE,vt_rd(touch.gcc,VT_VOTE)&~touch.vote_added);
}
static int vt_identify(void)
{
    uint8_t version[28],length[2],data[1024];
    if(!vt_spi_read(0x10014,version,sizeof(version)) || !vt_version_9916(version,sizeof(version))) {
        printf("VERMEER TOUCH: GT9916 identity unavailable; no controller data write\n");return 0;
    }
    if(!vt_spi_read(0x10070,length,2))return 0;
    unsigned n=vt_le16(length);
    if(n<181 || n>=sizeof(data) || !vt_spi_read(0x10070,data,n) || !vt_info_decode(data,n,&touch.info)) {
        printf("VERMEER TOUCH: IC-info guard refused; no controller data write\n");return 0;
    }
    printf("VERMEER TOUCH: GT9916 verified, event=%08x axes=%ux%u\n",
        touch.info.event,touch.info.max_x,touch.info.max_y);return 1;
}
static void vt_thread(void *arg,wait_result_t result)
{
    (void)arg;(void)result;
    /* The HID service may be registered by IOKit after Apple userspace starts. */
    for(unsigned i=0;i<1800 && !sweet_hid_touch_ready();i++)vt_sleep(100);
    if(!sweet_hid_touch_ready())goto stopped;
    os_atomic_store(&touch.stage,VT_CLOCK,relaxed);
    if(!vt_clock_enable())goto stopped;
    os_atomic_store(&touch.stage,VT_FIRMWARE,relaxed);
    if(!vt_engine_setup())goto stopped;
    os_atomic_store(&touch.stage,VT_IDENTITY,relaxed);
    if(!vt_identify())goto stopped;
    os_atomic_store(&touch.stage,VT_READY,relaxed);
    printf("VERMEER TOUCH: physical GT9916 input ready (SPI FIFO, no firmware upload)\n");
    unsigned consecutive_bad=0;
    for(;;) {
        uint8_t packet[90];
        if(!vt_spi_read(touch.info.event,packet,8))break;
        os_atomic_store(&touch.last_header,vt_le32(packet),relaxed);
        if(!(packet[0]&0x80u)){vt_sleep(8);continue;}
        if(!vt_checksum(packet,8))goto bad_packet;
        unsigned count=packet[2]&15u;
        if(count>10)goto bad_packet;
        unsigned n=count?8+count*8+2:8;
        if(n>8 && !vt_spi_read(touch.info.event+8,packet+8,n-8))break;
        struct vt_frame frame;
        if(vt_decode(packet,n,touch.info.max_x,touch.info.max_y,&frame)!=VT_TOUCH)goto bad_packet;
        /* Commit a fully verified frame only after the acknowledgement
         * completed; a failed transaction must not create stuck contacts.
         */
        if(!vt_ack_event())break;
        vt_apply(&touch.contacts,&frame,vt_emit_hid,NULL);
        os_atomic_inc(&touch.reports,relaxed);consecutive_bad=0;vt_sleep(8);continue;
bad_packet:
        os_atomic_inc(&touch.bad,relaxed);
        if(++consecutive_bad>=3)break;
        vt_sleep(8);
    }
stopped:
    vt_release_all(&touch.contacts,vt_emit_hid,NULL);
    vt_restore();os_atomic_store(&touch.stage,VT_STOPPED,relaxed);vt_status();
    thread_terminate(current_thread());
}
void vermeer_touch_start(void)
{
    int enabled=0;DTEntry entry;const void *value=NULL;unsigned size=0;
    if(!PE_parse_boot_argn("vermeer_touch",&enabled,sizeof(enabled)) || enabled!=1)return;
    if(SecureDTLookupEntry(NULL,"/",&entry)!=kSuccess ||
       SecureDTGetProperty(entry,"physical-model",&value,&size)!=kSuccess ||
       size!=sizeof("Redmi K70") || memcmp(value,"Redmi K70",size)) {
        printf("VERMEER TOUCH: physical-model guard refused\n");return;
    }
    os_atomic_store(&touch.stage,VT_WAIT,relaxed);vermeer_debug_input_register(vt_status);
    thread_t thread=THREAD_NULL;
    kern_return_t result=kernel_thread_start(vt_thread,NULL,&thread);
    if(result==KERN_SUCCESS)thread_deallocate(thread);
    else os_atomic_store(&touch.stage,VT_STOPPED,relaxed);
}
#endif
