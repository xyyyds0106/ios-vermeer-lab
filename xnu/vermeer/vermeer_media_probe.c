/* Unlinked next-build candidate. Opt-in worker, TLMM only, no provider by
 * default. USB parses/queues; no device read or mapping occurs on that thread.
 * Requires the separate unapplied media hook patch, never register over the
 * existing restart/baseband control callback.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <mach/thread_act.h>
#include <machine/atomic.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include "vermeer_media_probe.h"
extern void vermeer_debug_media_register(int (*)(const char *,unsigned));
static uint32_t _Atomic media_pending,media_started;
static const struct vmp_provider *_Atomic media_provider;
static uint64_t media_sequence; /* serialized worker only; never wraps */
static uint64_t media_now_us(void *ctx)
{
    (void)ctx;uint64_t ns;
    absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;
}
int vermeer_media_probe_bind(const struct vmp_provider *provider)
{
    if(!provider || !provider->begin || !provider->end)return 0;
    /* The owner publishes an immutable permanent provider. No replacement,
     * freeing, mapping creation or hardware access is performed here.
     */
    return os_atomic_cmpxchg(&media_provider,NULL,provider,release);
}
static int media_request(const char *text,unsigned n)
{
    unsigned mask=vmp_request_mask(text,n);
    if(!mask || !os_atomic_load(&media_started,acquire))return 0;
    if(!os_atomic_cmpxchg(&media_pending,0,mask,release))
        printf("VMT1_QUEUE busy\n");
    else printf("VMT1_QUEUE queued mask=%04x\n",mask);
    return 1;
}
static void media_sleep(void)
{
    uint64_t deadline;clock_interval_to_deadline(20,NSEC_PER_MSEC,&deadline);
    assert_wait_deadline((event_t)&media_pending,THREAD_UNINT,deadline);
    thread_block(THREAD_CONTINUE_NULL);
}
static void media_worker(void *arg,wait_result_t result)
{
    (void)arg;(void)result;
    for(;;) {
        unsigned requested=os_atomic_xchg(&media_pending,0,acquire);
        if(!requested){media_sleep();continue;}
        if(media_sequence==UINT64_MAX){printf("VMT1_QUEUE sequence-exhausted\n");continue;}
        const struct vmp_provider *provider=os_atomic_load(&media_provider,acquire);
        struct vmt_record record;char line[VMT_RECORD_CAPACITY];
        (void)vmp_capture(provider,requested,++media_sequence,media_now_us,NULL,&record);
        if(vmt_encode(&record,line,sizeof(line)))printf("%s",line);
        else printf("VMT1_QUEUE encode-failed\n");
    }
}
void vermeer_media_probe_start(void)
{
    static bool attempted;int enabled=0;DTEntry entry;const void *value=NULL;unsigned size=0;
    if(attempted || !PE_parse_boot_argn("vermeer_media_probe",&enabled,sizeof(enabled)) || enabled!=1)return;
    attempted=true;
    if(SecureDTLookupEntry(NULL,"/",&entry)!=kSuccess ||
       SecureDTGetProperty(entry,"physical-model",&value,&size)!=kSuccess ||
       size!=sizeof("Redmi K70") || memcmp(value,"Redmi K70",size))return;
    thread_t thread=THREAD_NULL;
    if(kernel_thread_start(media_worker,NULL,&thread)!=KERN_SUCCESS)return;
    thread_deallocate(thread);os_atomic_store(&media_started,1,release);
    vermeer_debug_media_register(media_request);
    printf("VMT1_QUEUE ready; TLMM-only; no provider means gated samples; UART/CCI unavailable\n");
}
#endif
