/* Actual ACTIVE-only touch voltage/mode/enable executor, with injected native
 * I/O. No guessed cmd-db addresses, no mapping or USB calls. A platform-wide
 * controller/IRQ/idle lease is mandatory for the ENTIRE invocation. Core tests
 * prove ordering/failure behavior, not PMIC or interconnect readiness.
 */
#ifndef VERMEER_RPMH_TOUCH_POWER_H
#define VERMEER_RPMH_TOUCH_POWER_H
#include "vermeer_rpmh_observe.h"
#define VRW_OWNER 64u
#define VRW_IDLE_HELD 128u
#define VRW_WRITE_AUTHORIZED 256u
#define VRW_IRQ_OWNER 512u
#define VRW_SEQUENCE_QUALIFIED 1024u
#define VRW_REQUIRED 2047u
/* Explicit experimental native-driver policy. Ordinary MMIO may terminate
 * XNU on external abort/SError or hang on an unresponsive bus. This bit never
 * asserts VRP_FAULT_CONTAINED; no recovery/retry/rollback is promised. */
#define VRW_FATAL_MMIO 2048u
static inline int vrw_proof_valid(unsigned p)
{
    return (p&(VRW_REQUIRED&~VRP_FAULT_CONTAINED))==(VRW_REQUIRED&~VRP_FAULT_CONTAINED) &&
        !!(p&(VRP_FAULT_CONTAINED|VRW_FATAL_MMIO));
}
#define VRW_ACK UINT32_C(0x10108)
#define VRW_AMC UINT32_C(0x10000)
#define VRW_TRIGGER UINT32_C(0x1000000)
enum { VRW_OK=0,VRW_GATED,VRW_ARGUMENT,VRW_REUSED,VRW_REVOKED,VRW_CLOCK,
       VRW_TIMEOUT,VRW_IO,VRW_CONFLICT,VRW_NO_SLOT,VRW_LAYOUT,VRW_VERIFY };
enum { VRW_NEW=0,VRW_PREFLIGHT,VRW_SELECT,VRW_ARM_IRQ,VRW_PROGRAM,VRW_FIRE,
       VRW_WAIT,VRW_CLEANUP,VRW_DONE,VRW_STOPPED,VRW_SETTLE };
struct vrw_io {
    void *ctx;
    unsigned (*proof)(void *);
    uint64_t (*epoch)(void *);
    uint64_t (*now_us)(void *);
    int (*read32)(void *,uint32_t offset,uint32_t *);
    int (*write32)(void *,uint32_t offset,uint32_t);
    void (*wait_us)(void *,unsigned); /* bounded thread-context delay, no I/O */
    unsigned iovdd_settle_us; /* platform-qualified sequence delay, <=50000 */
    /* I/O is relative to retained DRV2 mapping. Read/write callbacks must be
     * ordered and bounded at the software level. FAULT_CONTAINED and fatal
     * native MMIO are distinct policies; clocks cannot preempt a hung bus. */
};
struct vrw_state {
    unsigned used,phase,status,poisoned,touched,rail,completed_rails,selected;
    unsigned ncpt,first,stride,msg;
    uint32_t addresses[2],irq_original,command_status[3];
    uint32_t last_offset,last_value,reads,writes;
    unsigned failure_phase,last_read_valid;
    uint32_t last_read_value,last_irq,last_idle;
    uint64_t epoch,last,deadline;
};
static inline int vrw_fail(struct vrw_state *s,unsigned status)
{ if(s->phase!=VRW_STOPPED)s->failure_phase=s->phase;
  s->status=status;s->phase=VRW_STOPPED;return 0; }
static inline int vrw_ready(struct vrw_state *s,const struct vrw_io *io)
{
    if(!vrw_proof_valid(io->proof(io->ctx)) || io->epoch(io->ctx)!=s->epoch)
        return vrw_fail(s,VRW_REVOKED);
    uint64_t now=io->now_us(io->ctx);
    if(now<s->last)return vrw_fail(s,VRW_CLOCK);
    s->last=now;
    if(now>=s->deadline)return vrw_fail(s,VRW_TIMEOUT);
    return 1;
}
static inline int vrw_read(struct vrw_state *s,const struct vrw_io *io,uint32_t off,uint32_t *v)
{
    if((off&3u) || off>VRP_RSC_BYTES-4)return vrw_fail(s,VRW_ARGUMENT);
    if(!vrw_ready(s,io))return 0;
    ++s->reads;s->last_offset=off;s->last_read_valid=0;
    uint32_t value=0;
    if(io->read32(io->ctx,off,&value)!=1)return vrw_fail(s,VRW_IO);
    s->last_read_value=value;s->last_read_valid=1;
    if(!vrw_ready(s,io))return 0;
    *v=value;return 1;
}
static inline int vrw_write_allowed(const struct vrw_state *s,uint32_t off,uint32_t value)
{
    if(s->selected>=3)return 0;
    uint32_t base=0xd00+0x2a0*s->selected,bit=1u<<s->selected;
    if(off==0xd00)return 1; /* caller constructs read/modify/write under lease */
    if(off==0xd08)return s->phase==VRW_CLEANUP && value==bit;
    if(off==base+s->first)return value==0 || value==7;
    if(off==base+s->first+4)return value==0 || value==VRW_AMC || value==(VRW_AMC|VRW_TRIGGER);
    if(off==base+s->first+12)return value==0 || value==7;
    for(unsigned c=0;c<3;c++) {
        uint32_t cmd=base+s->msg+s->stride*c;
        unsigned delta=c==0?0:(c==1?8:4);
        uint32_t expected=c==0?(s->rail==0?1800u:3200u):(c==1?7u:1u);
        if(off==cmd)return value==VRW_ACK;
        if(off==cmd+4)return value==s->addresses[s->rail]+delta;
        if(off==cmd+8)return value==expected;
    }
    return 0;
}
static inline int vrw_write(struct vrw_state *s,const struct vrw_io *io,uint32_t off,uint32_t value)
{
    if(!vrw_write_allowed(s,off,value))return vrw_fail(s,VRW_ARGUMENT);
    if(!vrw_ready(s,io))return 0;
    /* Failed write is potentially applied. Never roll back or release its
     * mapping/slot on uncertainty; caller retains the lease until reboot. */
    s->poisoned=s->touched=1;s->last_offset=off;s->last_value=value;s->last_read_valid=0;++s->writes;
    if(io->write32(io->ctx,off,value)!=1)return vrw_fail(s,VRW_IO);
    return vrw_ready(s,io);
}
static inline int vrw_sync(struct vrw_state *s,const struct vrw_io *io,uint32_t off,uint32_t value)
{
    if(!vrw_write(s,io,off,value))return 0;
    for(unsigned i=0;i<128;i++) {
        uint32_t v;
        if(!vrw_read(s,io,off,&v))return 0;
        if(v==value)return 1;
    }
    return vrw_fail(s,VRW_VERIFY);
}
static inline int vrw_empty(struct vrw_state *s,const struct vrw_io *io,unsigned t,unsigned *empty)
{
    uint32_t b=0xd00+0x2a0*t,wait,control,idle,enable,irq;
    if(!vrw_read(s,io,b+s->first,&wait) || !vrw_read(s,io,b+s->first+4,&control) ||
       !vrw_read(s,io,b+s->first+8,&idle) || !vrw_read(s,io,b+s->first+12,&enable) ||
       !vrw_read(s,io,0xd04,&irq))return 0;
    if(idle==UINT32_MAX || irq==UINT32_MAX || (enable&~((1u<<s->ncpt)-1u)))return vrw_fail(s,VRW_LAYOUT);
    for(unsigned c=0;c<s->ncpt;c++)if(enable&(1u<<c)) {
        uint32_t addr;
        if(!vrw_read(s,io,b+s->msg+4+c*s->stride,&addr))return 0;
        for(unsigned r=0;r<2;r++)if(addr>=s->addresses[r] && addr-s->addresses[r]<16)
            return vrw_fail(s,VRW_CONFLICT);
    }
    *empty=idle!=0 && !wait && !control && !enable && !(irq&(1u<<t));
    return 1;
}
static inline int vrw_rail(struct vrw_state *s,const struct vrw_io *io)
{
    s->phase=VRW_SELECT;s->selected=UINT32_MAX;
    for(unsigned t=0;t<3;t++) {
        unsigned empty;
        if(!vrw_empty(s,io,t,&empty))return 0;
        if(empty && s->selected==UINT32_MAX)s->selected=t;
    }
    if(s->selected==UINT32_MAX)return vrw_fail(s,VRW_NO_SLOT);
    uint32_t b=0xd00+0x2a0*s->selected,bit=1u<<s->selected;unsigned empty;
    if(!vrw_read(s,io,0xd00,&s->irq_original) || s->irq_original==UINT32_MAX)
        return vrw_fail(s,s->status?s->status:VRW_LAYOUT);
    if(!vrw_empty(s,io,s->selected,&empty))return 0;
    if(!empty)return vrw_fail(s,VRW_CONFLICT);
    s->phase=VRW_ARM_IRQ;
    /* ACTIVE TCS completion is enabled at the RSC while the platform owner
     * keeps the GIC interrupt disabled. Poll the latched completion here;
     * masking it at the RSC and then requiring IRQ_STATUS can never finish
     * on hardware that gates that status with IRQ_ENABLE. Restore on success.
     */
    if(!vrw_sync(s,io,0xd00,s->irq_original|bit))return 0;
    if(!vrw_empty(s,io,s->selected,&empty))return 0;
    if(!empty)return vrw_fail(s,VRW_CONFLICT);
    s->phase=VRW_PROGRAM;
    for(unsigned c=0;c<3;c++) {
        uint32_t cmd=b+s->msg+c*s->stride,delta=c==0?0:(c==1?8:4);
        uint32_t value=c==0?(s->rail==0?1800u:3200u):(c==1?7u:1u);
        if(!vrw_sync(s,io,cmd,VRW_ACK) || !vrw_sync(s,io,cmd+4,s->addresses[s->rail]+delta) ||
           !vrw_sync(s,io,cmd+8,value))return 0;
    }
    if(!vrw_sync(s,io,b+s->first,7) || !vrw_sync(s,io,b+s->first+12,7) ||
       !vrw_sync(s,io,b+s->first+4,0) || !vrw_sync(s,io,b+s->first+4,VRW_AMC))return 0;
    s->phase=VRW_FIRE;
    if(!vrw_write(s,io,b+s->first+4,VRW_AMC|VRW_TRIGGER))return 0;
    s->phase=VRW_WAIT;
    unsigned done=0;
    for(unsigned i=0;i<100000;i++) {
        uint32_t irq,idle;
        if(!vrw_read(s,io,0xd04,&irq) || !vrw_read(s,io,b+s->first+8,&idle))return 0;
        s->last_irq=irq;s->last_idle=idle;
        if(irq==UINT32_MAX || idle==UINT32_MAX)return vrw_fail(s,VRW_IO);
        unsigned complete=1;
        for(unsigned c=0;c<3;c++) {
            if(!vrw_read(s,io,b+s->msg+12+c*s->stride,&s->command_status[c]))return 0;
            uint32_t v=s->command_status[c];
            complete&=v!=UINT32_MAX && (v&0x10100u)==0x10100u;
        }
        /* Require a fresh IRQ (preflight saw0), idle AND all ACK statuses;
         * stale command completion alone is not successful execution. */
        if((irq&bit) && idle && complete){done=1;break;}
        if(!vrw_ready(s,io))return 0;
        io->wait_us(io->ctx,1);
        if(!vrw_ready(s,io))return 0;
    }
    if(!done)return vrw_fail(s,VRW_TIMEOUT);
    s->completed_rails|=1u<<s->rail;
    s->phase=VRW_CLEANUP;
    if(!vrw_sync(s,io,b+s->first+4,VRW_AMC) || !vrw_sync(s,io,b+s->first+4,0) ||
       !vrw_sync(s,io,b+s->first+12,0) || !vrw_sync(s,io,b+s->first,0) ||
       !vrw_write(s,io,0xd08,bit))return 0;
    uint32_t irq,enabled;
    if(!vrw_read(s,io,0xd04,&irq))return 0;
    if(irq&bit)return vrw_fail(s,VRW_VERIFY);
    if(!vrw_read(s,io,0xd00,&enabled))return 0;
    if(enabled==UINT32_MAX)return vrw_fail(s,VRW_IO);
    if(!vrw_sync(s,io,0xd00,(enabled&~bit)|(s->irq_original&bit)))return 0;
    return 1;
}
/* Input DB must come from qualified live/copy-checked target-only collector
 * while the SAME lease/epoch is held. Caller cannot authorize from host fixture.
 * No voltage API exists: only DT-backed touch IOVDD1800mV, AVDD3200mV, HPM7.
 * ACTIVE-only: idle/suspend must remain inhibited while touch relies on votes.
 */
static inline unsigned vrw_touch_enable(struct vrw_state *s,const struct vrw_io *access,
    const struct vrp_result *db,unsigned qualified_revision)
{
    if(!s)return VRW_ARGUMENT;
    if(s->used)return VRW_REUSED;
    s->used=1;s->phase=VRW_PREFLIGHT;s->selected=UINT32_MAX;
    if(!access || !access->proof || !access->epoch || !access->now_us ||
       !access->read32 || !access->write32 || !access->wait_us || access->iovdd_settle_us>50000 ||
       !db || !db->complete || db->status ||
       db->db_version!=1 || db->db_valid!=3 || (db->db_flags&1u) ||
       !db->resource[0].valid || !db->resource[1].valid ||
       (qualified_revision!=0x207 && qualified_revision!=0x300)) {
        vrw_fail(s,VRW_GATED);return s->status;
    }
    const struct vrw_io io=*access;
    s->epoch=io.epoch(io.ctx);s->last=io.now_us(io.ctx);
    if(!s->epoch || s->last>UINT64_MAX-200000) {vrw_fail(s,VRW_CLOCK);return s->status;}
    s->deadline=s->last+200000;
    s->addresses[0]=db->resource[1].address;s->addresses[1]=db->resource[0].address;
    for(unsigned i=0;i<2;i++)if(!s->addresses[i] || (s->addresses[i]&3u) ||
        ((s->addresses[i]>>16)&7u)!=4 || s->addresses[i]>UINT32_MAX-16) {
        vrw_fail(s,VRW_LAYOUT);return s->status;
    }
    uint32_t lo=s->addresses[0]<s->addresses[1]?s->addresses[0]:s->addresses[1];
    uint32_t hi=s->addresses[0]>s->addresses[1]?s->addresses[0]:s->addresses[1];
    if(hi-lo<16){vrw_fail(s,VRW_LAYOUT);return s->status;}
    uint32_t id,solver,config;
    if(!vrw_read(s,&io,0,&id) || !vrw_read(s,&io,4,&solver) || !vrw_read(s,&io,12,&config))return s->status;
    s->ncpt=(config>>27)&31u;
    if(((id>>8)&0xffffu)!=qualified_revision || config==UINT32_MAX || solver==UINT32_MAX ||
       (solver&(1u<<24)) || ((config>>12)&63u)<8 || ((config>>12)&63u)>32 || s->ncpt<3 || s->ncpt>16) {
        vrw_fail(s,VRW_LAYOUT);return s->status;
    }
    s->first=qualified_revision==0x300?0x20:0x10;
    s->stride=qualified_revision==0x300?24:20;s->msg=qualified_revision==0x300?0x34:0x30;
    for(s->rail=0;s->rail<2;s->rail++) {
        if(!vrw_rail(s,&io))return s->status;
        if(s->rail==0 && io.iovdd_settle_us) {
            s->phase=VRW_SETTLE;
            if(!vrw_ready(s,&io))return s->status;
            io.wait_us(io.ctx,io.iovdd_settle_us);
            if(!vrw_ready(s,&io))return s->status;
        }
    }
    s->poisoned=0;s->status=VRW_OK;s->phase=VRW_DONE;return VRW_OK;
}
#endif
