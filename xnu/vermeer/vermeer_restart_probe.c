/* Default-off, GUI-only metadata worker. No PMIC transactions or reset.
 * Host must save BEFORE_PROBE and acknowledge nonce/sequence/CRC before any
 * core/config MMIO reads. Native SPMI write and reset paths remain unavailable.
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
#include "vermeer_restart_control.h"
#include "vermeer_spmi_restart.h"
extern vm_offset_t ml_io_map(vm_offset_t,vm_size_t);
extern void vermeer_debug_control_register(int (*)(const char *,unsigned));
void vermeer_restart_probe_start(void);
static struct {
    uint32_t _Atomic lock,pending;
    struct {unsigned kind;struct vrc_request request;} job;
    struct vrc_control control;
    struct vrs_backend backend;
    vm_offset_t core,config;
    bool mapped;
} restart_probe;
static uint64_t vrp_now(void)
{uint64_t ns;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/NSEC_PER_MSEC;}
static void vrp_sleep(unsigned ms)
{
    uint64_t deadline;clock_interval_to_deadline(ms,NSEC_PER_MSEC,&deadline);
    assert_wait_deadline((event_t)&restart_probe,THREAD_UNINT,deadline);thread_block(THREAD_CONTINUE_NULL);
}
static void vrp_token_text(const uint8_t *in,char *out)
{
    static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<16;i++){out[2*i]=hex[in[i]>>4];out[2*i+1]=hex[in[i]&15];}out[32]=0;
}
static int vrp_command(const char *text,unsigned n)
{
    unsigned kind=0;struct vrc_request request={{0},0,0};
    if(n>=3 && text[0]=='r' && text[1]=='p' && text[2]==' ') {
        if(!vrc_probe_token(text,n,request.token))goto malformed;
        kind=1;
    } else if(n>=3 && text[0]=='r' && text[1]=='a' && text[2]==' ') {
        char copy[64];
        if(n!=61)goto malformed;
        memcpy(copy,text,n);copy[1]='b'; /* shared bounded ACK fields, not a reset verb */
        if(!vrc_parse(copy,n,&request))goto malformed;
        kind=2;
    } else return 0;
    if(!os_atomic_cmpxchg(&restart_probe.lock,0,1,acquire))goto busy;
    if(os_atomic_load(&restart_probe.pending,relaxed)) {os_atomic_store(&restart_probe.lock,0,release);goto busy;}
    restart_probe.job.kind=kind;restart_probe.job.request=request;
    os_atomic_store(&restart_probe.pending,1,release);os_atomic_store(&restart_probe.lock,0,release);
    /* Only a bounded queue/copy on the USB thread. Worker polls every10ms;
     * no blocking lock, mapping, PMIC read, VFS call or reset here. */
    printf("VRC1 QUEUED kind=%u\n",kind);return 1;
malformed:printf("VRC1 REFUSED reason=malformed\n");return 1;
busy:printf("VRC1 REFUSED reason=queue-busy\n");return 1;
}
static int vrp_read(void *ctx,unsigned bank,unsigned off,uint32_t *out)
{
    (void)ctx;
    vm_offset_t base=bank==VRS_CORE?restart_probe.core:bank==VRS_CONFIG?restart_probe.config:0;
    unsigned limit=bank==VRS_CORE?0x3000:bank==VRS_CONFIG?0x4000:0;
    if(!base || !out || (off&3) || off>=limit)return 0;
    *out=*(volatile uint32_t *)(base+off);__asm__ volatile("dmb oshld":::"memory");return 1;
}
static int vrp_no_write(void *ctx,unsigned bank,unsigned off,uint32_t value)
{(void)ctx;(void)bank;(void)off;(void)value;return 0;}
static int vrp_pause(void *ctx){(void)ctx;delay(1);return 1;}
static vm_offset_t vrp_map(uint64_t pa,unsigned bytes)
{
    if(PAGE_SIZE!=16384)return 0;
    vm_offset_t offset=pa&PAGE_MASK;
    vm_size_t span=(offset+bytes+PAGE_MASK)&~(vm_size_t)PAGE_MASK;
    vm_offset_t base=ml_io_map((vm_offset_t)(pa&~(uint64_t)PAGE_MASK),span);
    return base?base+offset:0;
}
static void vrp_worker(void *arg,wait_result_t result)
{
    (void)arg;(void)result;
    for(;;) {
        if(restart_probe.control.state==VRC_WAIT_ACK && vrp_now()>=restart_probe.control.deadline) {
            restart_probe.control.state=VRC_FAILED;printf("VRC1 REFUSED reason=ack-timeout reads=0\n");
        }
        if(!os_atomic_cmpxchg(&restart_probe.lock,0,1,acquire)){vrp_sleep(10);continue;}
        if(!os_atomic_load(&restart_probe.pending,relaxed)) {
            os_atomic_store(&restart_probe.lock,0,release);vrp_sleep(10);continue;
        }
        unsigned kind=restart_probe.job.kind;struct vrc_request request=restart_probe.job.request;
        os_atomic_store(&restart_probe.pending,0,relaxed);os_atomic_store(&restart_probe.lock,0,release);
        if(kind==1) {
            if(restart_probe.backend.retired) {printf("VRC1 REFUSED reason=backend-retired\n");continue;}
            restart_probe.control=(struct vrc_control){.enabled=1};request.sequence=1;request.crc=vrc_probe_crc(request.token);
            if(!vrc_arm(&restart_probe.control,&request,vrp_now(),30000))continue;
            char token[33];vrp_token_text(request.token,token);
            printf("VRC1 BEFORE_PROBE token=%s seq=0000000000000001 crc=%08x core=0c400000+3000 cfg=0c42d000+4000\n",token,request.crc);
            continue;
        }
        if(!vrc_queue(&restart_probe.control,&request,vrp_now())) {
            printf("VRC1 REFUSED reason=ack-mismatch-or-stale reads=0\n");continue;
        }
        restart_probe.control.state=VRC_WORKING;
        if(!restart_probe.mapped) {
            restart_probe.core=vrp_map(0x0c400000,0x3000);restart_probe.config=vrp_map(0x0c42d000,0x4000);
            restart_probe.mapped=true;
            restart_probe.backend=(struct vrs_backend){.enabled=1,.read32=vrp_read,.write32=vrp_no_write,.pause_us=vrp_pause};
            memcpy(restart_probe.backend.regions,vrs_k70_regions,sizeof(vrs_k70_regions));
        }
        if(!restart_probe.core || !restart_probe.config) {
            restart_probe.backend.retired=1;restart_probe.control.state=VRC_FAILED;
            printf("VRC1 REFUSED reason=mapping-failed\n");continue;
        }
        int status=vrs_probe(&restart_probe.backend);
        char token[33];vrp_token_text(request.token,token);
        printf("VRC1 PROBE_RESULT token=%s status=%d version=%08x features=%08x apid=%u map=%08x owner=%08x retired=%u\n",
            token,status,restart_probe.backend.version,restart_probe.backend.features,restart_probe.backend.apid,
            restart_probe.backend.map,restart_probe.backend.owner,restart_probe.backend.retired);
        restart_probe.control.state=VRC_FAILED; /* consumed; never call reset worker */
        printf("VRC1 CAPABILITIES reason_write=unavailable shutdown=unavailable bootloader_reset=unavailable\n");
    }
}
void vermeer_restart_probe_start(void)
{
    static bool attempted;int enabled=0;DTEntry entry;const void *value=NULL;unsigned size=0;
    if(attempted || !PE_parse_boot_argn("vermeer_restart_probe",&enabled,sizeof(enabled)) || enabled!=1)return;
    attempted=true;
    if(SecureDTLookupEntry(NULL,"/",&entry)!=kSuccess || SecureDTGetProperty(entry,"physical-model",&value,&size)!=kSuccess ||
       size!=sizeof("Redmi K70") || memcmp(value,"Redmi K70",size))return;
    thread_t thread=THREAD_NULL;
    if(kernel_thread_start(vrp_worker,NULL,&thread)!=KERN_SUCCESS)return;
    thread_deallocate(thread);vermeer_debug_control_register(vrp_command);
    printf("VRC1 READY metadata-only rp/ra; reason writes/reset disabled; no reads before host ACK\n");
}
#endif
