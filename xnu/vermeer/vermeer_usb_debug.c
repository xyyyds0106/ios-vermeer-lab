/* Experimental K70 CDC ACM console. Opt-in: vermeer_usb=1.
 * Reuses ABL's PHY/clocks; never changes GCC, SMMU, PMIC or storage.
 * DWC3 register/endpoint protocol: Linux drivers/usb/dwc3, reference0073.
 * USB can read the RAM console ring and issue four bounded queries only.
 * All controller memory is permanent: a timeout never frees DMA memory.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <mach/thread_act.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <arm/caches_internal.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <pexpert/pexpert.h>
#include "vermeer_usb_protocol.h"

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern ppnum_t pmap_find_phys(pmap_t, addr64_t);
extern unsigned vermeer_debug_copy(uint64_t *, char *, unsigned);
extern uint64_t vermeer_debug_total(void);
extern uint64_t vermeer_debug_dropped(void);
extern int vermeer_debug_sample_request(void);
extern int vermeer_debug_frame_request(void);
extern void vermeer_debug_input_status(void);
extern int vermeer_debug_control_request(const char *,unsigned);
void vermeer_usb_debug_start(void);

#define GCTL 0xc110
#define GEVNTADRLO 0xc400
#define GEVNTADRHI 0xc404
#define GEVNTSIZ 0xc408
#define GEVNTCOUNT 0xc40c
#define DCFG 0xc700
#define DCTL 0xc704
#define DEVTEN 0xc708
#define DSTS 0xc70c
#define DALEPENA 0xc720
#define EPBASE(n) (0xc800+16*(n))
#define RUN (1u<<31)
#define RESET (1u<<30)
#define HALTED (1u<<22)
#define ACTIVE (1u<<10)
#define FORCE_REMOVE (1u<<11)
#define SET_CONFIG 1
#define SET_RESOURCE 2
#define STALL 4
#define CLEAR_STALL 5
#define START 6
#define END 8
#define START_CONFIG 9
#define T_NORMAL (1u<<4)
#define T_SETUP (2u<<4)
#define T_STATUS2 (3u<<4)
#define T_STATUS3 (4u<<4)
#define T_DATA (5u<<4)
#define HWO 1u
#define TRB_FLAGS (HWO|2u|(1u<<10)|(1u<<11))
#define EVENTS 0
#define EVENT_SIZE 4096
/* Separate cache lines: hardware never shares a dirty CPU-owned TRB line. */
#define SETUP 0x1200
#define DATA 0x1300
#define NOTIFY 0x1500
#define RX 0x1800
#define TX 0x2000
#define RX_SIZE 512
#define TX_SIZE 2048
#define DMA_SIZE 0x4000
static const unsigned trb_off[6]={0x1000,0x1040,0x1080,0x10c0,0x1100,0x1140};
static const char *const usb_strings[]={NULL,"Vermeer Lab","Vermeer XNU Debug","K70DBG004"};
static struct {
    vm_offset_t regs, dma, smmu;
    uint64_t pa, cursor, tx_bytes, rx_bytes;
    uint32_t pos, events, setups, mps;
    unsigned resource[6], requested[6];
    bool busy[6], failed, configured, dtr, notify;
    enum { SETUP_PHASE, DATA_PHASE, WAIT2, WAIT3, STATUS_PHASE } phase;
    bool data_in, set_line;
    uint8_t coding[7];
    char command[64];
    unsigned command_n;
    bool command_overflow;
    int sleep_channel;
} u;

static uint32_t read_reg(vm_offset_t base,unsigned off)
{
    uint32_t v=*(volatile uint32_t *)(base+off);
    __asm__ volatile("dmb oshld" ::: "memory");
    return v;
}
static uint32_t rd(unsigned off) { return read_reg(u.regs,off); }
static void wr(unsigned off,uint32_t v)
{
    *(volatile uint32_t *)(u.regs+off)=v;
    __asm__ volatile("dsb sy" ::: "memory");
}
static void sync_dma(unsigned off,unsigned size)
{
    flush_dcache64((addr64_t)(u.pa+off),size,TRUE);
    __asm__ volatile("dsb sy" ::: "memory");
}
static void fail(const char *why)
{
    if (!u.failed) printf("VERMEER USB: stopped: %s; permanent DMA retained\n",why);
    u.failed=true;
}
static int epcmd(unsigned ep,unsigned cmd,uint32_t p0,uint32_t p1,uint32_t p2)
{
    if (u.failed || ep>=6) return -1;
    wr(EPBASE(ep)+8,p0); wr(EPBASE(ep)+4,p1); wr(EPBASE(ep),p2);
    wr(EPBASE(ep)+12,cmd|ACTIVE);
    for (unsigned n=0;n<1000;n++) {
        uint32_t v=rd(EPBASE(ep)+12);
        if (!(v&ACTIVE)) {
            if ((v>>12)&15) { fail("endpoint command status"); return -1; }
            if ((cmd&15)==START) u.resource[ep]=(v>>16)&0x7f;
            return 0;
        }
        delay(1);
    }
    fail("endpoint command timeout"); return -1;
}
static void transfer(unsigned ep,unsigned buf,unsigned length,unsigned kind)
{
    if (u.failed) return;
    if (ep>=6 || u.busy[ep] || buf>DMA_SIZE || length>DMA_SIZE-buf) {
        fail("transfer ownership/bounds"); return;
    }
    unsigned off=trb_off[ep];
    uint64_t p=u.pa+buf,t=u.pa+off;
    volatile uint32_t *trb=(volatile uint32_t *)(u.dma+off);
    trb[0]=(uint32_t)p; trb[1]=(uint32_t)(p>>32);
    trb[2]=length; trb[3]=kind|TRB_FLAGS;
    u.requested[ep]=length;
    if (length) sync_dma(buf,length);
    sync_dma(off,16);
    u.busy[ep]=true;
    (void)epcmd(ep,START,(uint32_t)(t>>32),(uint32_t)t,0);
}
static int complete(unsigned ep)
{
    if (ep>=6 || !u.busy[ep]) { fail("unexpected completion"); return -1; }
    sync_dma(trb_off[ep],16);
    volatile uint32_t *t=(volatile uint32_t *)(u.dma+trb_off[ep]);
    unsigned left=t[2]&0xffffff;
    if ((t[3]&HWO) || (t[2]>>28) || left>u.requested[ep]) {
        fail("invalid DMA completion"); return -1;
    }
    u.busy[ep]=false;
    return (int)(u.requested[ep]-left);
}
static void setup_arm(void)
{
    u.phase=SETUP_PHASE;
    transfer(0,SETUP,8,T_SETUP);
}
static void stall(void)
{
    (void)epcmd(0,STALL,0,0,0);
    setup_arm();
}
static void receive_arm(void) { transfer(2,RX,RX_SIZE,T_NORMAL); }
static void stop_data(void)
{
    for (unsigned ep=2;ep<6;ep++) {
        if (u.busy[ep]) {
            if (!u.resource[ep] || epcmd(ep,END|FORCE_REMOVE|(u.resource[ep]<<16),0,0,0)) return;
            u.busy[ep]=false;
        }
    }
    wr(DALEPENA,3);
    u.configured=false; u.dtr=false; u.notify=false;
}
static void configure(void)
{
    if (u.configured || u.failed) return;
    if (epcmd(0,START_CONFIG|(2u<<16),0,0,0) ||
        epcmd(2,SET_CONFIG,(2u<<1)|(u.mps<<3),(1u<<8)|(2u<<25),0) ||
        epcmd(2,SET_RESOURCE,1,0,0) ||
        epcmd(3,SET_CONFIG,(2u<<1)|(u.mps<<3)|(1u<<17),(1u<<8)|(3u<<25),0) ||
        epcmd(3,SET_RESOURCE,1,0,0) ||
        epcmd(5,SET_CONFIG,(3u<<1)|(16u<<3)|(2u<<17),(1u<<8)|(8u<<16)|(5u<<25),0) ||
        epcmd(5,SET_RESOURCE,1,0,0)) return;
    wr(DALEPENA,3|(1u<<2)|(1u<<3)|(1u<<5));
    u.configured=true; receive_arm();
    printf("VERMEER USB: CDC ACM configured, bulk MPS %u\n",u.mps);
}
static unsigned string_desc(unsigned index,uint8_t *data)
{
    if (!index) { data[0]=4;data[1]=3;data[2]=9;data[3]=4; return 4; }
    if (index>=sizeof(usb_strings)/sizeof(usb_strings[0])) return 0;
    unsigned n=2;
    for (const char *s=usb_strings[index];*s && n<126;s++) { data[n++]=(uint8_t)*s;data[n++]=0; }
    data[0]=(uint8_t)n;data[1]=3;return n;
}
static void handle_setup(void)
{
    sync_dma(SETUP,8);
    const uint8_t *s=(const uint8_t *)(u.dma+SETUP);
    uint8_t *data=(uint8_t *)(u.dma+DATA);
    unsigned value=s[2]|(s[3]<<8),index=s[4]|(s[5]<<8),length=s[6]|(s[7]<<8),n=0;
    int kind=vusb_request(s[0],s[1],value,index,length);
    u.data_in=(s[0]&0x80)!=0;u.set_line=false;
    if (u.setups++<40) printf("VERMEER USB: setup %02x %02x %04x %04x len%u\n",s[0],s[1],value,index,length);
    switch (kind) {
    case VUSB_DESC:
        switch (value>>8) {
        case 1: if (value&255) break;memcpy(data,vusb_device,sizeof(vusb_device));n=sizeof(vusb_device);break;
        case 2: case 7:
            if (value&255) break;
            memcpy(data,vusb_config,sizeof(vusb_config));n=sizeof(vusb_config);
            data[1]=(uint8_t)(value>>8);
            /* Configuration and other-speed configuration have opposite MPS. */
            for (unsigned i=0;i<n;i+=data[i]) if (data[i+1]==5 && (data[i+3]&3)==2) {
                unsigned m=(value>>8)==7?(u.mps==512?64:512):u.mps;
                data[i+4]=(uint8_t)m;data[i+5]=(uint8_t)(m>>8);
            }
            break;
        case 3:n=string_desc(value&255,data);break;
        case 6:if (!(value&255)) { memcpy(data,vusb_qualifier,sizeof(vusb_qualifier));n=sizeof(vusb_qualifier); }break;
        default:break;
        }
        if (!n) { stall();return; }break;
    case VUSB_ADDRESS:wr(DCFG,(rd(DCFG)&~(127u<<3))|(value<<3));break;
    case VUSB_CONFIG:
        /* Reconfiguration while transfers are live is stopped, never recycled
         * before a controller reset drains stale completion events. */
        if (!value && u.configured) { fail("host deconfigured; reboot to reconnect"); return; }
        if (value) configure();break;
    case VUSB_GET_CONFIG:data[0]=u.configured?1:0;n=1;break;
    case VUSB_STATUS:data[0]=data[1]=0;n=2;break;
    case VUSB_INTERFACE:break;
    case VUSB_GET_INTERFACE:data[0]=0;n=1;break;
    case VUSB_SET_LINE:u.set_line=true;break;
    case VUSB_GET_LINE:memcpy(data,u.coding,7);n=7;break;
    case VUSB_CONTROL:
        if (u.configured) {
            bool old=u.dtr;u.dtr=(value&1)!=0;u.notify=true;
            if (u.dtr && !old) printf("VERMEER USB: debug connected; help/status/ping/replay/sample/frame/input (read only)\n");
        }break;
    case VUSB_BREAK:break;
    case VUSB_CLEAR_HALT:
        /* Active transfer replay cannot be made safe by CLEARSTALL alone. */
        if (u.busy[(index&15)*2+((index>>7)&1)]) { fail("clear-halt on active endpoint");return; }
        (void)epcmd((index&15)*2+((index>>7)&1),CLEAR_STALL,0,0,0);break;
    default:stall();return;
    }
    if (u.failed) return;
    if (!length) { u.phase=WAIT2;return; }
    u.phase=DATA_PHASE;
    if (u.data_in) transfer(1,DATA,n<length?n:length,T_DATA);
    else if (u.set_line) transfer(0,DATA,64,T_DATA);
    else stall();
}
static void command_line(void)
{
    u.command[u.command_n]=0;
    if (u.command_overflow) printf("VERMEER DEBUG: command too long\n");
    else if (!strcmp(u.command,"ping")) printf("VERMEER DEBUG: pong K70DBG004\n");
    else if (!strcmp(u.command,"status"))
        printf("VERMEER DEBUG: K70DBG004 events=%u tx=%llu rx=%llu log=%llu dropped=%llu\n",
            u.events,u.tx_bytes,u.rx_bytes,vermeer_debug_total(),vermeer_debug_dropped());
    else if (!strcmp(u.command,"replay")) {
        u.cursor=0;printf("VERMEER DEBUG: replay retained RAM log\n");
    } else if (!strcmp(u.command,"sample")) {
        printf("VERMEER DEBUG: sample %s\n",vermeer_debug_sample_request() ?
            "queued (coalesced; selected tasks only)" : "unavailable (disabled or limit reached)");
    } else if (!strcmp(u.command,"frame")) {
        if (!vermeer_debug_frame_request()) printf("VERMEER FRAME: unavailable\n");
    } else if (!strcmp(u.command,"input")) {
        vermeer_debug_input_status();
    } else if (vermeer_debug_control_request(u.command,u.command_n)) {
        /* Optional GUI-only control worker: queue-only callback, default off. */
    } else if (u.command_n) printf("VERMEER DEBUG: commands help status ping replay sample frame input; queries only\n");
    u.command_n=0;u.command_overflow=false;
}
static void receive_done(unsigned length)
{
    sync_dma(RX,RX_SIZE);u.rx_bytes+=length;
    const uint8_t *p=(const uint8_t *)(u.dma+RX);
    for (unsigned i=0;i<length;i++) {
        if (p[i]=='\r' || p[i]=='\n') command_line();
        else if (p[i]>=32 && p[i]<=126) {
            if (u.command_n<sizeof(u.command)-1) u.command[u.command_n++]=(char)p[i];
            else u.command_overflow=true;
        }
    }
    receive_arm();
}
static void transmit(void)
{
    if (!u.configured || !u.dtr || u.failed) return;
    if (u.notify && !u.busy[5]) {
        const uint8_t msg[10]={0xa1,0x20,0,0,0,0,2,0,3,0};
        memcpy((void *)(u.dma+NOTIFY),msg,sizeof(msg));u.notify=false;
        transfer(5,NOTIFY,sizeof(msg),T_NORMAL);
    }
    if (u.busy[3]) return;
    unsigned n=vermeer_debug_copy(&u.cursor,(char *)(u.dma+TX),TX_SIZE);
    /* Serial stream must have a short packet, with no padding/invented bytes. */
    if (n && !(n%u.mps)) { n--;u.cursor--; }
    if (n) { transfer(3,TX,n,T_NORMAL);u.tx_bytes+=n; }
}
static void event(uint32_t e)
{
    if (e&1) {
        if ((e&0xfe)!=0) { fail("unexpected device event format");return; }
        unsigned type=(e>>8)&15;
        if (type==1) {
            if (u.configured) { stop_data();fail("bus reset after configuration; reboot to reconnect");return; }
            wr(DCFG,rd(DCFG)&~(127u<<3));printf("VERMEER USB: bus reset\n");
        } else if (type==2) {
            unsigned speed=rd(DSTS)&7;
            if (speed!=0 && speed!=1 && speed!=3) { fail("unexpected non-USB2 speed");return; }
            u.mps=speed==0?512:64;
            (void)epcmd(0,SET_CONFIG,(64u<<3)|(2u<<30),(1u<<8)|(1u<<10),0);
            (void)epcmd(1,SET_CONFIG,(64u<<3)|(2u<<30),(1u<<8)|(1u<<10)|(1u<<25),0);
            printf("VERMEER USB: connected speed=%u\n",speed);
        } else if (!type && u.configured) fail("cable disconnect; reboot to reconnect");
        else if (type==9 || type==11) fail("controller/event overflow");
        return;
    }
    unsigned ep=(e>>1)&31,type=(e>>6)&15,status=(e>>12)&15;
    if (type==1) {
        int n=complete(ep);if (n<0) return;
        if (status&1) { fail("USB transfer bus error");return; }
        if (ep==2) { receive_done((unsigned)n);return; }
        if (ep>1) return;
        switch (u.phase) {
        case SETUP_PHASE:if (ep!=0 || n!=8) fail("setup length");else handle_setup();break;
        case DATA_PHASE:
            if (u.set_line) {
                if (ep!=0 || n!=7) { fail("line coding length");break; }
                sync_dma(DATA,64);memcpy(u.coding,(void *)(u.dma+DATA),7);
            }
            u.phase=WAIT3;break;
        case STATUS_PHASE:setup_arm();break;
        default:fail("control completion order");break;
        }
    } else if (ep<=1 && type==3 && status==2 && (u.phase==WAIT2 || u.phase==WAIT3)) {
        unsigned direction=u.phase==WAIT2?1:(u.data_in?0:1);
        if (ep!=direction) { fail("control status direction");return; }
        unsigned kind=u.phase==WAIT3?T_STATUS3:T_STATUS2;
        u.phase=STATUS_PHASE;transfer(ep,DATA,0,kind);
    }
}
static void usb_thread(void *arg __unused,wait_result_t result __unused)
{
    while (!u.failed) {
        unsigned count=rd(GEVNTCOUNT)&0xffff;
        if (count>EVENT_SIZE || (count&3)) { fail("event ring size/alignment");break; }
        if (count) {
            sync_dma(EVENTS,EVENT_SIZE);
            for (unsigned i=0;i<count && !u.failed;i+=4) {
                uint32_t e=*(volatile uint32_t *)(u.dma+u.pos);
                u.pos=(u.pos+4)%EVENT_SIZE;u.events++;
                event(e);
            }
            wr(GEVNTCOUNT,count);
        }
        transmit();
        uint64_t deadline;clock_interval_to_deadline(1,NSEC_PER_MSEC,&deadline);
        (void)assert_wait_deadline(&u.sleep_channel,THREAD_UNINT,deadline);
        thread_block(THREAD_CONTINUE_NULL);
    }
    wr(DEVTEN,0);wr(DCTL,rd(DCTL)&~RUN);
    thread_terminate(current_thread());
}
/* Require the exact known nonsecure SMMU layout and a unique identity route.
 * Never create mappings or weaken isolation just to make debug USB work. */
static bool identity(void)
{
    vm_offset_t s=u.smmu;
    if (read_reg(s,0x20)!=0x4c017e69 || read_reg(s,0x24)!=0x60000046 ||
        read_reg(s,0)!=0x00280406 || read_reg(s,0x28)!=0x5111) return false;
    unsigned matches=0;uint32_t route=0;
    for (unsigned i=0;i<105;i++) if (vusb_stream_match(read_reg(s,0x800+4*i))) {
        matches++;route=read_reg(s,0xc00+4*i);
    }
    if (matches!=1) { printf("VERMEER USB: SID40 matches %u\n",matches);return false; }
    unsigned cb=route&255;uint32_t cbar=0,sctlr=1;
    if (((route>>16)&3)==0) {
        if (cb>=70) return false;
        cbar=read_reg(s,0x1000+4*cb);sctlr=read_reg(s,0x80000+0x1000*cb);
    }
    printf("VERMEER USB: SID40 route=%08x cbar=%08x sctlr=%08x\n",route,cbar,sctlr);
    return vusb_route_identity(route,cbar,sctlr)!=0;
}
void vermeer_usb_debug_start(void)
{
    static bool attempted;
    int enabled=0;PE_parse_boot_argn("vermeer_usb",&enabled,sizeof(enabled));
    if (enabled!=1 || attempted) return;
    attempted=true;u.mps=512;u.coding[0]=0;u.coding[1]=0xc2;u.coding[2]=1;u.coding[6]=8;
    printf("VERMEER USB: CDC ACM debug004, RAM log and read-only commands\n");
    vm_offset_t gcc=ml_io_map(0x139000,0x1000);
    if (!gcc) return;
    uint32_t power=read_reg(gcc,4),master=read_reg(gcc,0x18),cfg=read_reg(gcc,0x88),aggre=read_reg(gcc,0x8c);
    printf("VERMEER USB: GCC power=%08x master=%08x cfg=%08x aggre=%08x\n",power,master,cfg,aggre);
    if (!(power&(1u<<31)) || ((master|cfg|aggre)&(1u<<31))) {
        printf("VERMEER USB: clocks/power unavailable; controller untouched\n");return;
    }
    u.regs=ml_io_map(0xa600000,0xe000);u.smmu=ml_io_map(0x15000000,0xd0000);
    if (!u.regs || !u.smmu) return;
    uint32_t id=rd(0xc120),hw=rd(0xc140);unsigned width=hw>>24;
    printf("VERMEER USB: id=%08x hwparams0=%08x awidth=%u gctl=%08x dctl=%08x dsts=%08x\n",
        id,hw,width,rd(GCTL),rd(DCTL),rd(DSTS));
    if ((id>>16)!=0x5533 && (id>>16)!=0x3331) return;
    if (((rd(GCTL)>>12)&3)!=2 || !identity()) {
        printf("VERMEER USB: device mode/identity guard refused; controller untouched\n");return;
    }
    if (kmem_alloc_contig(kernel_map,&u.dma,DMA_SIZE,PAGE_MASK,0,0,
        KMA_DATA|KMA_ZERO|KMA_PERMANENT,VM_KERN_MEMORY_IOKIT)!=KERN_SUCCESS) return;
    ppnum_t pn=pmap_find_phys(kernel_pmap,(addr64_t)u.dma);
    if (!pn) return;
    u.pa=ptoa_64(pn);
    if (!vusb_dma_allowed(u.pa,DMA_SIZE,width)) { printf("VERMEER USB: high DMA width/range guard refused\n");return; }
    for (unsigned off=0;off<DMA_SIZE;off+=PAGE_SIZE) {
        ppnum_t page=pmap_find_phys(kernel_pmap,(addr64_t)(u.dma+off));
        if (!page || ptoa_64(page)!=u.pa+off) return;
    }
    vm_offset_t qs=ml_io_map(0xa6f8800,0x100);
    if (!qs) return;
    /* First stop any old device DMA; soft reset must finish before replacing
     * descriptors. PHY configuration and global bus settings remain from ABL. */
    wr(DEVTEN,0);wr(DCTL,rd(DCTL)&~RUN);
    unsigned i;
    for (i=0;i<500 && !(rd(DSTS)&HALTED);i++) delay(1000);
    if (!(rd(DSTS)&HALTED)) { fail("controller halt timeout");return; }
    wr(DCTL,RESET);
    for (i=0;i<500 && (rd(DCTL)&RESET);i++) delay(1000);
    if (rd(DCTL)&RESET) { fail("controller reset timeout");return; }
    sync_dma(0,DMA_SIZE);
    wr(GEVNTADRLO,(uint32_t)u.pa);wr(GEVNTADRHI,(uint32_t)(u.pa>>32));
    if (rd(GEVNTADRHI)!=(uint32_t)(u.pa>>32)) { fail("event address readback");return; }
    wr(GEVNTSIZ,(1u<<31)|EVENT_SIZE);wr(GEVNTCOUNT,rd(GEVNTCOUNT)&0xffff);
    wr(DCFG,rd(DCFG)&~(7u|(127u<<3))); /* high speed only */
    if (epcmd(0,START_CONFIG,0,0,0) ||
        epcmd(0,SET_CONFIG,64u<<3,(1u<<8)|(1u<<10),0) || epcmd(0,SET_RESOURCE,1,0,0) ||
        epcmd(1,SET_CONFIG,64u<<3,(1u<<8)|(1u<<10)|(1u<<25),0) || epcmd(1,SET_RESOURCE,1,0,0)) return;
    wr(DALEPENA,3);setup_arm();if (u.failed) return;
    uint32_t hs=read_reg(qs,0x10),ss=read_reg(qs,0x30);
    *(volatile uint32_t *)(qs+0x10)=hs|(1u<<20)|(1u<<28);
    *(volatile uint32_t *)(qs+0x30)=ss|(1u<<24);
    __asm__ volatile("dsb sy" ::: "memory");
    wr(DEVTEN,7|(1u<<9)|(1u<<11));wr(DCTL,rd(DCTL)|RUN);
    for (i=0;i<500 && (rd(DSTS)&HALTED);i++) delay(1000);
    if (rd(DSTS)&HALTED) { fail("controller run timeout");return; }
    thread_t thread;
    if (kernel_thread_start(usb_thread,NULL,&thread)!=KERN_SUCCESS) { fail("poll thread creation");wr(DCTL,rd(DCTL)&~RUN);return; }
    thread_deallocate(thread);
    printf("VERMEER USB: running; DMA=%llx; Windows serial K70DBG004; awaiting enumeration\n",u.pa);
}
#endif
