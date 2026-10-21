/* SPDX-License-Identifier: MIT
 * Real K70 native ACTIVE-only touch backend, default-off experimental driver.
 * Ordinary MMIO can panic/hang XNU. No SEA/SError recovery is claimed and the
 * FAULT_CONTAINED bit is NEVER set on the real provider. No rollback/retry.
 * Sole DRV2 software owner: integration must route every client/IRQ through
 * this module. Firmware's other DRVs are not modified or claimed here.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <machine/machine_routines.h>
#include <machine/atomic.h>
#include <arm/memory_types.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <vm/pmap.h>
#include "vermeer_rpmh_board.h"
#include "vermeer_rpmh_board_dt.h"
#include "vermeer_rpmh_touch_native.h"
#include "AdrenoCmdDb.h"
#include "vermeer_touch_power_if.h"
/* BSD's build does not expose the MACH/PEXPERT-private declaration in
 * arm/machine_routines.h. Match the actual kernel function's full signature;
 * an implicit int return would truncate the mapped virtual address. */
#if !defined(PEXPERT_KERNEL_PRIVATE) && !defined(MACH_KERNEL_PRIVATE)
extern vm_offset_t io_map(vm_map_offset_t,vm_size_t,unsigned int,vm_prot_t,bool);
#endif
/* Saved DT: TLMM f000000 + register bank100000 + GPIO24*1000. Mapping is
 * one native16KB page; only GPIO24 config and in/out are accessed. */
#define VRB_RESET_PA UINT64_C(0x0f118000)
static struct {
    uint32_t _Atomic configured,owner,retired,awake,ready,started,irq_masked;
    uint64_t _Atomic generation;
    uint64_t _Atomic active_epoch[VRB_MEDIA+1];
    vm_offset_t rsc,db,reset,gic;
    unsigned reset_touched;
    struct vrb_controller_lease touch_lease;
    struct vrw_native_state touch;
    uint8_t metadata[VRP_DB_BYTES]; /* sparse directory COPY, not GPU cmd-db */
    struct vrp_event last_event;
    /* Preserved before every potentially fatal native access; default XNU
     * exception reporting supplies ESR/FAR if an abort occurs. No hook retry. */
    volatile struct {uint64_t pa,epoch,sequence;uint32_t write,value,returned;} note;
} vrb;
static uint32_t vrb_load(vm_offset_t,uint64_t);
int vermeer_rpmh_pm_inhibited(void)
{return os_atomic_load(&vrb.awake,acquire)!=0;}
int vermeer_rpmh_controller_claim(unsigned client,struct vrb_controller_lease *lease)
{
    if(!lease || client<VRB_TOUCH || client>VRB_MEDIA ||
       !os_atomic_load(&vrb.configured,acquire) || os_atomic_load(&vrb.retired,acquire) ||
       !os_atomic_cmpxchg(&vrb.owner,0,client,acq_rel))return 0;
    if(os_atomic_load(&vrb.retired,acquire))return 0;
    /* Hold becomes visible before any mapping or hardware access. The strong
     * hook stops CPU idle and terminates a demand-sleep attempt before CPU
     * power-down. The PM assertion also prevents normal system idle sleep. */
    os_atomic_store(&vrb.awake,1,release);
    if(vermeer_rpmh_pm_hook_version()!=1 || !vermeer_rpmh_system_idle_hold() || os_atomic_load(&vrb.retired,acquire)) {
        os_atomic_store(&vrb.retired,1,release);return 0;
    }
    uint64_t epoch=os_atomic_load(&vrb.generation,relaxed)+1;
    if(!epoch){os_atomic_store(&vrb.retired,1,release);return 0;}
    os_atomic_store(&vrb.generation,epoch,release);
    /* No RPMh interrupt handler is installed by this polling backend. Its
     * physical SPI5/INTID37 MUST already be disabled and inactive at GICD.
     * Do not steal/mask an enabled IRQ, clear pending IRQs or alter routing.
     * All future DRV2 clients/IRQ users share this permanent polling owner. */
    if(!vrb.gic)vrb.gic=io_map(0x17100000,PAGE_SIZE,VM_WIMG_IO,VM_PROT_READ,false);
    if(os_atomic_load(&vrb.retired,acquire))return 0;
    if(!vrb.gic || (vrb_load(vrb.gic+0x104,0x17100104)&(1u<<5)) || os_atomic_load(&vrb.retired,acquire) ||
       (vrb_load(vrb.gic+0x304,0x17100304)&(1u<<5))) {
        os_atomic_store(&vrb.retired,1,release);return 0;
    }
    if(os_atomic_load(&vrb.retired,acquire))return 0;
    os_atomic_store(&vrb.irq_masked,1,release);
    *lease=(struct vrb_controller_lease){epoch,client};return 1;
}
int vermeer_rpmh_controller_valid(const struct vrb_controller_lease *l)
{
    return l && l->client>=VRB_TOUCH && l->client<=VRB_MEDIA && l->epoch && os_atomic_load(&vrb.configured,acquire) &&
        !os_atomic_load(&vrb.retired,acquire) && os_atomic_load(&vrb.irq_masked,acquire) && vermeer_rpmh_pm_inhibited() &&
        os_atomic_load(&vrb.owner,acquire)==l->client &&
        os_atomic_load(&vrb.generation,acquire)==l->epoch;
}
void vermeer_rpmh_controller_finish(const struct vrb_controller_lease *l,unsigned disposition)
{
    if(!vermeer_rpmh_controller_valid(l))return;
    if(disposition>VRB_RETAIN_ACTIVE) {os_atomic_store(&vrb.retired,1,release);return;}
    if(disposition==VRB_RETAIN_ACTIVE)os_atomic_store(&vrb.active_epoch[l->client],l->epoch,release);
    os_atomic_store(&vrb.owner,0,release); /* mappings/awake hold never released */
}
int vermeer_rpmh_active_valid(const struct vrb_controller_lease *l)
{
    return l && l->client>=VRB_TOUCH && l->client<=VRB_MEDIA && l->epoch &&
        os_atomic_load(&vrb.configured,acquire) && !os_atomic_load(&vrb.retired,acquire) &&
        vermeer_rpmh_pm_inhibited() && os_atomic_load(&vrb.active_epoch[l->client],acquire)==l->epoch;
}
void vermeer_rpmh_active_quarantine(const struct vrb_controller_lease *l)
{
    if(vermeer_rpmh_active_valid(l))os_atomic_store(&vrb.retired,1,release);
    /* No rollback/unmap/unvote/reset/IRQ writes, even if another client owns
     * an in-flight transaction. Its next proof check observes retirement. */
}
static unsigned vrb_proof(void *ctx)
{
    (void)ctx;
    if(!vermeer_rpmh_controller_valid(&vrb.touch_lease) || !vrb.rsc || !vrb.db || !vrb.reset)return 0;
    return (VRW_REQUIRED&~VRP_FAULT_CONTAINED)|VRW_FATAL_MMIO;
}
static uint64_t vrb_epoch(void *ctx)
{(void)ctx;return os_atomic_load(&vrb.generation,acquire);}
static uint64_t vrb_now(void *ctx)
{(void)ctx;uint64_t ns;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/NSEC_PER_USEC;}
static void vrb_note(uint64_t pa,unsigned write,uint32_t value)
{
    vrb.note.pa=pa;vrb.note.epoch=vrb_epoch(NULL);vrb.note.write=write;
    vrb.note.value=value;vrb.note.returned=0;vrb.note.sequence++;
    __asm__ volatile("dsb sy":::"memory");
}
static uint32_t vrb_load(vm_offset_t va,uint64_t pa)
{
    vrb_note(pa,0,0);uint32_t v=*(volatile uint32_t *)va;
    __asm__ volatile("dmb oshld":::"memory");vrb.note.returned=1;return v;
}
static void vrb_store(vm_offset_t va,uint64_t pa,uint32_t value)
{
    vrb_note(pa,1,value);*(volatile uint32_t *)va=value;
    __asm__ volatile("dsb sy":::"memory");vrb.note.returned=1;
}
static int vrb_read(void *ctx,uint32_t off,uint32_t *v)
{
    (void)ctx;if(!v || !vrb_proof(NULL) || (off&3u) || off>VRP_RSC_BYTES-4)return 0;
    *v=vrb_load(vrb.rsc+off,VRP_RSC_BASE+off);return 1;
}
static int vrb_write(void *ctx,uint32_t off,uint32_t v)
{
    (void)ctx;
    /* The real executor additionally enforces the selected TCS and values. */
    if(!vrb_proof(NULL) || !vrw_write_allowed(&vrb.touch.executor,off,v))return 0;
    vrb_store(vrb.rsc+off,VRP_RSC_BASE+off,v);return 1;
}
static int vrb_db_read(void *ctx,uint32_t off,uint32_t n,uint8_t *dst)
{
    (void)ctx;
    if(!dst || !n || n>144 || (off&3u) || (n&3u) ||
       !vrp_range(off,n,(uint32_t)VRP_DB_BYTES) || !vrb_proof(NULL))return 0;
    for(unsigned i=0;i<n;i+=4) {
        if(!vrb_proof(NULL))return 0;
        uint32_t value=vrb_load(vrb.db+off+i,VRP_DB_BASE+off+i);
        for(unsigned k=0;k<4;k++)dst[i+k]=(uint8_t)(value>>(8*k));
    }
    return 1;
}
static int vrb_record(void *ctx,const struct vrp_event *e)
{(void)ctx;if(!e || !vrb_proof(NULL))return 0;vrb.last_event=*e;return 1;}
/* Optional GPU dependency capture under the SAME live platform lease.
 * This only reads the qualified fixed reserved command-DB window. It does
 * not vote GPU power, touch GPU registers or publish an accelerator. */
static uint8_t vrb_gpu_db_copy[ADRENO_CMD_DB_BYTES];
static struct AdrenoCmdDb vrb_gpu_db;
static unsigned vrb_gpu_db_used;
static unsigned vrb_gpu_db_copy_status;
static void vrb_gpu_db_probe(void)
{
    int enabled=0;
    if(!PE_parse_boot_argn("vermeer_gpu_probe",&enabled,sizeof(enabled)) || enabled!=1 || vrb_gpu_db_used)return;
    vrb_gpu_db_used=1;
    uint64_t start=vrb_now(NULL),last=start;uint8_t check[128];unsigned error=0;
    for(unsigned pass=0;pass<2 && !error;pass++)for(unsigned off=0;off<ADRENO_CMD_DB_BYTES;off+=sizeof(check)) {
        uint64_t now=vrb_now(NULL);
        if(now<last || now-start>=5000000 || !vrb_proof(NULL)){error=100;break;}
        last=now;
        uint8_t *dst=pass?check:vrb_gpu_db_copy+off;
        if(!vrb_db_read(NULL,off,sizeof(check),dst)){error=101;break;}
        now=vrb_now(NULL);
        if(now<last || now-start>=5000000 || !vrb_proof(NULL)){error=100;break;}
        last=now;
        if(pass && memcmp(check,vrb_gpu_db_copy+off,sizeof(check))){error=102;break;}
    }
    vrb_gpu_db_copy_status=error;
    if(error){printf("VERMEER GPU DB: copy refused status=%u; no GPU I/O\n",error);return;}
    unsigned status=AdrenoCmdDbParse(vrb_gpu_db_copy,sizeof(vrb_gpu_db_copy),1,&vrb_gpu_db);
    printf("VERMEER GPU DB: status=%u complete=%u rows=%u entries=%u found=%03x stable-two-pass=1 gpu-enabled=0\n",
        status,vrb_gpu_db.complete,vrb_gpu_db.rows,vrb_gpu_db.entries,vrb_gpu_db.found_mask);
    if(status!=AdrenoCmdDbOK)for(unsigned i=0;i<8;i++) {
        /* Bounded retained-RAM diagnostics, including empty directory rows.
         * A parse rejection never causes additional controller/DB MMIO. */
        const uint8_t *p=vrb_gpu_db_copy+8+16*i;uint16_t field[5];
        for(unsigned k=0;k<5;k++)field[k]=(uint16_t)p[2*k]|(uint16_t)p[2*k+1]<<8;
        printf("VERMEER GPU DB ROW: index=%u type=%u entries=%u entry-offset=%u aux-offset=%u version=%04x\n",
            i,field[0],field[3],field[1],field[2],field[4]);
    }
    if(!vrb_gpu_db.complete)return;
    for(unsigned i=0;i<AdrenoCmdDbResources;i++)if(vrb_gpu_db.found_mask&(1u<<i)) {
        const struct AdrenoCmdDbResource *r=&vrb_gpu_db.resource[i];uint32_t words[8]={0};
        for(unsigned k=0;k<r->aux_bytes;k++)words[k/4]|=(uint32_t)r->aux[k]<<((k%4)*8);
        printf("VERMEER GPU DB: name=%s addr=%08x version=%04x aux-bytes=%u aux=%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x\n",
            AdrenoCmdDbResourceName(i),r->address,r->version,r->aux_bytes,
            words[0],words[1],words[2],words[3],words[4],words[5],words[6],words[7]);
    }
}
static void vrb_wait(void *ctx,unsigned us)
{(void)ctx;if(us<=50000)delay(us);}
static int vrb_reset_assert(void *ctx)
{
    (void)ctx;if(!vrb_proof(NULL))return 0;
    uint32_t cfg=vrb_load(vrb.reset,VRB_RESET_PA);
    if(cfg==UINT32_MAX)return 0;
    /* DT pmx_ts_active: GPIO function,8mA,pull-up. Write only the owned pad.
     * Latch low before enabling output; preserve unrelated upper config bits. */
    vrb.reset_touched=1;
    vrb_store(vrb.reset+4,VRB_RESET_PA+4,0);
    uint32_t expected=(cfg&~0x3ffu)|0x2c3u;
    vrb_store(vrb.reset,VRB_RESET_PA,expected);
    return vrb_load(vrb.reset,VRB_RESET_PA)==expected &&
        !(vrb_load(vrb.reset+4,VRB_RESET_PA+4)&3u);
}
static void vrb_finish(void *ctx,unsigned disposition)
{
    (void)ctx;
    /* A reset assertion is a real hardware change too; a later pre-vote
     * failure must not release ownership as if nothing had happened. */
    if(disposition!=VRW_KEEP_TOUCH_IDLE_HOLD && vrb.reset_touched)disposition=VRW_QUARANTINE;
    if(disposition==VRW_KEEP_TOUCH_IDLE_HOLD)return; /* release after reset high */
    vermeer_rpmh_controller_finish(&vrb.touch_lease,disposition);
}
static int vrb_begin(void *ctx,struct vrw_native_lease *l)
{
    (void)ctx;
    if(!l || PAGE_SIZE!=16384 || !vermeer_rpmh_controller_claim(VRB_TOUCH,&vrb.touch_lease))return 0;
    /* Permanent exact-window mappings. cmd-db uses Normal noncacheable/WC,
     * matching Linux MEMREMAP_WC, and read-only CPU permissions. No DB writes. */
    vrb.rsc=io_map(VRP_RSC_BASE,VRP_RSC_BYTES,VM_WIMG_IO,VM_PROT_READ|VM_PROT_WRITE,false);
    vrb.db=io_map(VRP_DB_BASE,VRP_DB_BYTES,VM_WIMG_WCOMB,VM_PROT_READ,false);
    vrb.reset=io_map(VRB_RESET_PA,PAGE_SIZE,VM_WIMG_IO,VM_PROT_READ|VM_PROT_WRITE,false);
    if(!vrb.rsc || !vrb.db || !vrb.reset) {
        vermeer_rpmh_controller_finish(&vrb.touch_lease,VRB_UNCERTAIN);return 0;
    }
    *l=(struct vrw_native_lease){
        .io={NULL,vrb_proof,vrb_epoch,vrb_now,vrb_read,vrb_write,vrb_wait,18000},
        .rsc_start=VRP_RSC_BASE,.rsc_bytes=VRP_RSC_BYTES,.db_start=VRP_DB_BASE,.db_bytes=VRP_DB_BYTES,
        .generation=vrb.touch_lease.epoch,.read_db=vrb_db_read,.record=vrb_record,.finish=vrb_finish,
        .metadata_copy=vrb.metadata,.metadata_bytes=sizeof(vrb.metadata),.prepare_touch=vrb_reset_assert};
    return 1;
}
static int vrb_touch_acquire(void *ctx)
{
    (void)ctx;
    if(!os_atomic_cmpxchg(&vrb.started,0,1,acq_rel))return os_atomic_load(&vrb.ready,acquire)!=0;
    static const struct vrw_native_provider provider={NULL,vrb_begin};
    unsigned result=vrw_native_touch_init(&vrb.touch,&provider);
    if(result!=VRW_OK) {
        printf("VERMEER RPMH: stopped status=%u phase=%u rails=%u retained=%u\n",result,
            vrb.touch.executor.phase,vrb.touch.executor.completed_rails,os_atomic_load(&vrb.retired,relaxed));
        const struct vrw_state *s=&vrb.touch.executor;
        /* Retained RAM only: never probe a failed/quarantined controller. */
        printf("VERMEER RPMH: failed-phase=%u rail=%u tcs=%u reads=%u writes=%u offset=%08x read-valid=%u read=%08x write=%08x\n",
            s->failure_phase,s->rail,s->selected,s->reads,s->writes,s->last_offset,
            s->last_read_valid,s->last_read_value,s->last_value);
        printf("VERMEER RPMH: irq=%08x idle=%08x cmd=%08x/%08x/%08x elapsed-deadline=%llu/%llu\n",
            s->last_irq,s->last_idle,s->command_status[0],s->command_status[1],s->command_status[2],
            (unsigned long long)s->last,(unsigned long long)s->deadline);
        return 0;
    }
    if(!vrb_proof(NULL))goto failed;
    /* Reference9916 brl_power_on: IOVDD,3ms+15ms supply sequencing,AVDD,
     * reset high,100ms reset delay. Extra absent enable GPIOs are never used.
     * This is an explicit experimental board policy, not a fitted-IC proof.
     * SPI brl_reset_after/IC readiness remain the touch transport's job. */
    vrb_store(vrb.reset+4,VRB_RESET_PA+4,2);
    delay(100000);
    if(!vrb_proof(NULL) || (vrb_load(vrb.reset+4,VRB_RESET_PA+4)&3u)!=3u ||
       (vrb_load(vrb.reset,VRB_RESET_PA)&0x3ffu)!=0x2c3u)goto failed;
    vrb_gpu_db_probe();
    if(!vrb_proof(NULL))goto failed;
    os_atomic_store(&vrb.ready,1,release);
    vermeer_rpmh_controller_finish(&vrb.touch_lease,VRB_RETAIN_ACTIVE);
    printf("VERMEER RPMH: two touch rail ACKs; reset high; ACTIVE idle hold retained\n");return 1;
failed:
    vermeer_rpmh_controller_finish(&vrb.touch_lease,VRB_UNCERTAIN);return 0;
}
static int vrb_touch_valid(void *ctx)
{
    (void)ctx;return os_atomic_load(&vrb.ready,acquire) && vermeer_rpmh_active_valid(&vrb.touch_lease);
}
int vermeer_rpmh_touch_start(const void *fdt,size_t bytes)
{
    int enabled=0;DTEntry entry;const void *value=NULL;unsigned size=0;
    if(!PE_parse_boot_argn("vermeer_rpmh",&enabled,sizeof(enabled)) || enabled!=1 ||
       PAGE_SIZE!=16384 || vermeer_rpmh_pm_hook_version()!=1 ||
       SecureDTLookupEntry(NULL,"/",&entry)!=kSuccess ||
       SecureDTGetProperty(entry,"physical-model",&value,&size)!=kSuccess ||
       size!=sizeof("Redmi K70") || memcmp(value,"Redmi K70",size) || vrb_dt_check(fdt,bytes))return 0;
    /* All contract checks precede registration, mappings and native I/O. */
    if(!os_atomic_cmpxchg(&vrb.configured,0,1,acq_rel))return 0;
    static const struct vtp_ops ops={NULL,vrb_touch_acquire,vrb_touch_valid};
    if(!vermeer_touch_power_register(&ops)) {
        os_atomic_store(&vrb.retired,1,release);return 0;
    }
    return 1;
}
int vermeer_rpmh_touch_start_from_adt(void)
{
    DTEntry entry;const void *fdt=NULL;unsigned bytes=0;
    if(SecureDTLookupEntry(NULL,"/chosen",&entry)!=kSuccess ||
       SecureDTGetProperty(entry,"vermeer,boot-fdt",&fdt,&bytes)!=kSuccess)return 0;
    return vermeer_rpmh_touch_start(fdt,bytes);
}
#endif
