/* Concrete native-adapter lifecycle. The GPU/platform owner supplies begin()
 * with retained mappings and fault-contained I/O, not a table of guessed rails.
 * This calls cmd-db qualification and the REAL writer, not a simulation.
 * No provider is installed here: default remains GATED with no hardware I/O.
 */
#ifndef VERMEER_RPMH_TOUCH_NATIVE_H
#define VERMEER_RPMH_TOUCH_NATIVE_H
#include "vermeer_rpmh_touch_power.h"
enum { VRW_RELEASE_CLEAN=0,VRW_KEEP_TOUCH_IDLE_HOLD=1,VRW_QUARANTINE=2 };
struct vrw_native_lease {
    struct vrw_io io; /* iovdd_settle_us is reviewed board policy, not guessed */
    uint64_t rsc_start,rsc_bytes,db_start,db_bytes;
    uint64_t generation;
    int (*read_db)(void *,uint32_t offset,uint32_t bytes,uint8_t *);
    int (*record)(void *,const struct vrp_event *); /* preserve typed init evidence */
    /* CLEAN: release init lease. KEEP: release controller lock but retain
     * CPU-idle/suspend inhibit while touch relies on ACTIVE-only votes.
     * QUARANTINE: retain uncertain TCS/mappings/state until reboot. No writes,
     * resets, guessed rollback or retry allowed in finish. Must not fail. */
    void (*finish)(void *,unsigned disposition);
};
struct vrw_native_provider {
    void *ctx;
    /* On success owns the shared RPMh controller/IRQ lock and idle lease.
     * On failure cleans up internally and supplies no access. */
    int (*begin)(void *,struct vrw_native_lease *);
};
struct vrw_native_state {
    unsigned used,status,disposition;
    struct vrp_session db_session;
    struct vrp_session bootstrap_session;
    struct vrp_result bootstrap;
    struct vrp_result db;
    struct vrw_state executor;
};
struct vrw_native_context { struct vrw_native_lease lease; };
static inline unsigned vrw_native_proof(void *p)
{
    struct vrw_native_lease *l=&((struct vrw_native_context *)p)->lease;
    unsigned bits=l->io.proof(l->io.ctx);
    if((bits&VRW_REQUIRED)!=VRW_REQUIRED || l->io.epoch(l->io.ctx)!=l->generation)return 0;
    return bits;
}
static inline uint64_t vrw_native_clock(void *p)
{ struct vrw_io *io=&((struct vrw_native_context *)p)->lease.io;return io->now_us(io->ctx); }
static inline uint64_t vrw_native_epoch(void *p)
{ struct vrw_native_lease *l=&((struct vrw_native_context *)p)->lease;return l->io.epoch(l->io.ctx)==l->generation?l->generation:0; }
static inline int vrw_native_read32(void *p,uint32_t off,uint32_t *v)
{ struct vrw_io *io=&((struct vrw_native_context *)p)->lease.io;return io->read32(io->ctx,off,v); }
static inline int vrw_native_write32(void *p,uint32_t off,uint32_t v)
{ struct vrw_io *io=&((struct vrw_native_context *)p)->lease.io;return io->write32(io->ctx,off,v); }
static inline void vrw_native_wait(void *p,unsigned us)
{ struct vrw_io *io=&((struct vrw_native_context *)p)->lease.io;io->wait_us(io->ctx,us); }
static inline int vrw_native_dbread(void *p,unsigned region,uint32_t off,uint32_t n,uint8_t *dst)
{
    struct vrw_native_lease *l=&((struct vrw_native_context *)p)->lease;
    if(!vrw_native_proof(p))return 0;
    if(region==VRP_DB && vrp_range(off,n,(uint32_t)VRP_DB_BYTES))return l->read_db(l->io.ctx,off,n,dst);
    if(region!=VRP_RSC || n!=4 || (off!=0 && off!=4 && off!=12))return 0;
    uint32_t v;
    if(l->io.read32(l->io.ctx,off,&v)!=1)return 0;
    for(unsigned i=0;i<4;i++)dst[i]=(uint8_t)(v>>(8*i));
    return 1;
}
static inline int vrw_native_record(void *p,const struct vrp_event *event)
{ struct vrw_native_lease *l=&((struct vrw_native_context *)p)->lease;return l->record(l->io.ctx,event); }
static inline unsigned vrw_native_touch_init(struct vrw_native_state *s,const struct vrw_native_provider *provider)
{
    if(!s)return VRW_ARGUMENT;
    if(s->used)return VRW_REUSED;
    s->used=1;s->status=VRW_GATED;
    if(!provider || !provider->begin)return s->status;
    struct vrw_native_context c={0};
    if(provider->begin(provider->ctx,&c.lease)!=1)return s->status;
    struct vrw_native_lease *l=&c.lease;struct vrw_io *io=&l->io;
    if(!l->finish)return s->status; /* broken provider owns its unreturned lease */
    if(!io->proof || !io->epoch || !io->now_us || !io->read32 || !io->write32 || !io->wait_us ||
       io->iovdd_settle_us>50000 ||
       !l->read_db || !l->record || !l->generation || l->rsc_start!=VRP_RSC_BASE ||
       l->rsc_bytes!=VRP_RSC_BYTES || l->db_start!=VRP_DB_BASE || l->db_bytes!=VRP_DB_BYTES ||
       !vrw_native_proof(&c))goto finish;
    /* Driver initialization qualifies real ID against fixed source-supported
     * layouts. No caller-supplied expected revision or resource address. The
     * standalone observer API retains its original external-policy contract. */
    struct vrp_access a={l->rsc_start,l->rsc_bytes,l->db_start,l->db_bytes,
        0,1,&c,
        vrw_native_proof,vrw_native_clock,vrw_native_record,vrw_native_dbread};
    unsigned boot=vrp_collect(&s->bootstrap_session,&a,VRP_RSC,&s->bootstrap);
    if(boot!=VRP_GATED || s->bootstrap.bootstrap_valid!=7) {
        s->status=boot==VRP_READ_FAILED?VRW_IO:(boot==VRP_REVOKED?VRW_REVOKED:
            (boot==VRP_TIMEOUT?VRW_TIMEOUT:(boot==VRP_CLOCK?VRW_CLOCK:VRW_GATED)));
        if(boot==VRP_READ_FAILED || boot==VRP_REVOKED)s->disposition=VRW_QUARANTINE;
        goto finish;
    }
    unsigned revision=(s->bootstrap.id>>8)&0xffffu;
    if(revision!=0x207 && revision!=0x300){s->status=VRW_LAYOUT;goto finish;}
    unsigned result=vrp_collect(&s->db_session,&a,VRP_DB,&s->db);
    if(result!=VRP_OK) {
        s->status=result==VRP_READ_FAILED?VRW_IO:(result==VRP_REVOKED?VRW_REVOKED:
            (result==VRP_TIMEOUT?VRW_TIMEOUT:(result==VRP_CLOCK?VRW_CLOCK:VRW_GATED)));
        if(result==VRP_READ_FAILED || result==VRP_REVOKED)s->disposition=VRW_QUARANTINE;
        goto finish;
    }
    /* Qualification and execution must be in the same generation. The core
     * pins io.epoch again but may not silently accept a replaced lease. */
    if(!vrw_native_proof(&c)){s->status=VRW_REVOKED;s->disposition=VRW_QUARANTINE;goto finish;}
    struct vrw_io bounded={&c,vrw_native_proof,vrw_native_epoch,vrw_native_clock,
        vrw_native_read32,vrw_native_write32,vrw_native_wait,io->iovdd_settle_us};
    s->status=vrw_touch_enable(&s->executor,&bounded,&s->db,revision);
    if(s->executor.poisoned || s->status==VRW_IO || s->status==VRW_REVOKED)s->disposition=VRW_QUARANTINE;
    else if(s->status==VRW_OK)s->disposition=VRW_KEEP_TOUCH_IDLE_HOLD;
finish:
    l->finish(io->ctx,s->disposition);return s->status;
}
#endif
