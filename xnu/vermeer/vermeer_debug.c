/* Console mirror: best effort, any context, never waits or writes storage. */
#include <sys/types.h>
#include <machine/atomic.h>
#include "vermeer_log_ring.h"
static struct vl_ring log_ring;
static uint32_t _Atomic log_lock;
static uint64_t _Atomic dropped;
typedef int (*vermeer_sample_request_fn)(void);
static vermeer_sample_request_fn _Atomic sample_request;
void vermeer_debug_append(const char *data, int size);
unsigned vermeer_debug_copy(uint64_t *pos, char *out, unsigned max);
uint64_t vermeer_debug_total(void);
uint64_t vermeer_debug_dropped(void);
void vermeer_debug_sample_register(vermeer_sample_request_fn);
int vermeer_debug_sample_request(void);
void vermeer_debug_append(const char *data, int size)
{
    if (size<=0) return;
    if (!os_atomic_cmpxchg(&log_lock,0,1,acquire)) {
        os_atomic_add(&dropped,(uint64_t)size,relaxed); return;
    }
    vl_append(&log_ring,data,(unsigned)size);
    os_atomic_store(&log_lock,0,release);
}
unsigned vermeer_debug_copy(uint64_t *pos, char *out, unsigned max)
{
    if (!os_atomic_cmpxchg(&log_lock,0,1,acquire)) return 0;
    unsigned n=vl_copy(&log_ring,pos,out,max);
    os_atomic_store(&log_lock,0,release);
    return n;
}
uint64_t vermeer_debug_total(void)
{
    if (!os_atomic_cmpxchg(&log_lock,0,1,acquire)) return 0;
    uint64_t n=log_ring.total;
    os_atomic_store(&log_lock,0,release);
    return n;
}
uint64_t vermeer_debug_dropped(void) { return os_atomic_load(&dropped,relaxed); }
/* The optional GUI sampler owns this callback. USB only queues work; it
 * never walks another task's VM or blocks the controller polling thread.
 * The callback lives in the kernel permanently, including after unregister.
 */
void vermeer_debug_sample_register(vermeer_sample_request_fn callback)
{
    os_atomic_store(&sample_request,callback,release);
}
int vermeer_debug_sample_request(void)
{
    vermeer_sample_request_fn callback=os_atomic_load(&sample_request,acquire);
    return callback ? callback() : 0;
}
