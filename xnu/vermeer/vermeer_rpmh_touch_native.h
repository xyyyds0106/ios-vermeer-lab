/* Concrete native-adapter lifecycle. The GPU/platform owner supplies begin()
 * with retained mappings and explicit contained/fatal I/O policy.
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
    /* Fatal native MMIO stages only bounded directory/target metadata into
     * this retained ordinary kernel RAM before running the contained COPY
     * parser. Never return this sparse buffer as a complete GPU cmd-db. */
    uint8_t *metadata_copy;
    size_t metadata_bytes;
    int (*prepare_touch)(void *); /* optional real reset assertion, before votes */
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
    if(!vrw_proof_valid(bits) || l->io.epoch(l->io.ctx)!=l->generation)return 0;
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
/* The ordinary native driver accepts terminal MMIO faults explicitly. The
 * original observer remains contained-only. This staging pass never passes a
 * fatal MMIO callback to it: copy_read below reads retained kernel RAM only. */
struct vrw_stage { struct vrw_native_context *c;uint64_t last,deadline,sequence;unsigned reads,status; };
static inline int vrw_stage_fail(struct vrw_stage *s,unsigned error)
{s->status=error;return 0;}
static inline int vrw_stage_ready(struct vrw_stage *s)
{
    if(!vrw_native_proof(s->c))return vrw_stage_fail(s,VRW_REVOKED);
    uint64_t now=vrw_native_clock(s->c);
    if(now<s->last)return vrw_stage_fail(s,VRW_CLOCK);
    if(now>=s->deadline)return vrw_stage_fail(s,VRW_TIMEOUT);
    s->last=now;return 1;
}
static inline int vrw_stage_read(struct vrw_stage *s,unsigned region,unsigned off,unsigned n,uint8_t *dst)
{
    struct vrw_native_lease *l=&s->c->lease;
    unsigned limit=region==VRP_DB?(unsigned)VRP_DB_BYTES:(unsigned)VRP_RSC_BYTES;
    if(!dst || !n || n>144 || (off&3) || (n&3) || !vrp_range(off,n,limit))return vrw_stage_fail(s,VRW_ARGUMENT);
    if(!vrw_stage_ready(s))return 0;
    struct vrp_event e={++s->sequence,(region==VRP_DB?VRP_DB_BASE:VRP_RSC_BASE)+off,
        VRP_BEFORE,region,VRP_BOOTSTRAP,n,s->reads+1,VRP_OK};
    if(l->record(l->io.ctx,&e)!=1)return vrw_stage_fail(s,VRW_IO);
    if(!vrw_stage_ready(s))return 0;
    ++s->reads;int ok;
    if(region==VRP_DB)ok=l->read_db(l->io.ctx,off,n,dst);
    else {
        uint32_t v=0;ok=n==4?l->io.read32(l->io.ctx,off,&v):0;
        if(ok==1)for(unsigned i=0;i<4;i++)dst[i]=(uint8_t)(v>>(8*i));
    }
    if(ok!=1)return vrw_stage_fail(s,VRW_IO);
    if(!vrw_stage_ready(s))return 0;
    e.kind=VRP_AFTER;e.sequence=++s->sequence;
    if(l->record(l->io.ctx,&e)!=1)return vrw_stage_fail(s,VRW_IO);
    return vrw_stage_ready(s);
}
static inline unsigned vrw_copy_proof(void *p)
{
    unsigned bits=vrw_native_proof(p);
    /* This callback authorizes COPY reads only; it never authorizes MMIO. */
    return bits?(bits|VRP_FAULT_CONTAINED):0;
}
static inline int vrw_copy_read(void *p,unsigned region,uint32_t off,uint32_t n,uint8_t *out)
{
    struct vrw_native_lease *l=&((struct vrw_native_context *)p)->lease;
    if(region!=VRP_DB || !vrw_native_proof(p) || !vrp_range(off,n,(uint32_t)VRP_DB_BYTES))return 0;
    for(unsigned i=0;i<n;i++)out[i]=l->metadata_copy[off+i];
    return 1;
}
static inline unsigned vrw_native_fatal_collect(struct vrw_native_state *state,struct vrw_native_context *c)
{
    struct vrw_native_lease *l=&c->lease;
    if(!l->metadata_copy || l->metadata_bytes!=VRP_DB_BYTES)return VRW_GATED;
    uint64_t now=vrw_native_clock(c);
    if(now>UINT64_MAX-VRP_TIMEOUT_US)return VRW_CLOCK;
    struct vrw_stage s={c,now,now+VRP_TIMEOUT_US,0,0,VRW_OK};uint8_t word[4];
    uint32_t *fields[3]={&state->bootstrap.id,&state->bootstrap.solver,&state->bootstrap.config};
    const unsigned offsets[3]={0,4,12};
    for(unsigned i=0;i<3;i++) {
        if(!vrw_stage_read(&s,VRP_RSC,offsets[i],4,word))return s.status;
        *fields[i]=vrp_u32(word);state->bootstrap.bootstrap_valid|=1u<<i;
    }
    unsigned revision=(state->bootstrap.id>>8)&0xffffu;
    unsigned count=(state->bootstrap.config>>12)&63u,ncpt=(state->bootstrap.config>>27)&31u;
    if((revision!=0x207 && revision!=0x300) || state->bootstrap.solver==UINT32_MAX ||
       (state->bootstrap.solver&(1u<<24)) || state->bootstrap.config==UINT32_MAX ||
       count<8 || count>32 || ncpt<3 || ncpt>16)return VRW_LAYOUT;
    uint8_t *copy=l->metadata_copy,header[144],again[144];
    if(!vrw_stage_read(&s,VRP_DB,0,8,header))return s.status;
    if(vrp_u32(header)!=1 || vrp_u32(header+4)!=0x0c0330dbu)return VRW_LAYOUT;
    if(!vrw_stage_read(&s,VRP_DB,0,144,again))return s.status;
    if(!vrp_equal(header,again,8) || (vrp_u32(again+140)&1u))return VRW_LAYOUT;
    for(unsigned i=0;i<144;i++)copy[i]=header[i]=again[i];
    unsigned total=0,found=0;
    for(unsigned row=0;row<8;row++) {
        const uint8_t *r=header+8+row*16;if(!vrp_u16(r))break;
        unsigned n=vrp_u16(r+6),off=144u+vrp_u16(r+2);
        if(n>VRP_MAX_ENTRIES-total || (off&3u) || !vrp_range(off,n*24,(unsigned)VRP_DB_BYTES))return VRW_LAYOUT;
        total+=n;
        for(unsigned j=0;j<row;j++) {
            unsigned before=144u+vrp_u16(header+10+j*16),bytes=24u*vrp_u16(header+14+j*16);
            if(n && bytes && off<before+bytes && before<off+n*24)return VRW_LAYOUT;
        }
        for(unsigned i=0;i<n;i++) {
            unsigned at=off+24*i;
            if(!vrw_stage_read(&s,VRP_DB,at,24,copy+at))return s.status;
            static const uint8_t names[2][8]={{'l','d','o','b','2',0,0,0},{'l','d','o','b','1','2',0,0}};
            if(vrp_equal(copy+at,names[0],8) || vrp_equal(copy+at,names[1],8)) {
                unsigned bit=vrp_equal(copy+at,names[0],8)?1u:2u;
                if(found&bit)return VRW_LAYOUT;
                found|=bit;
                if(!vrw_stage_read(&s,VRP_DB,at,24,again))return s.status;
                if(!vrp_equal(copy+at,again,24))return VRW_LAYOUT;
            }
        }
    }
    if(!vrw_stage_read(&s,VRP_DB,0,144,again))return s.status;
    if(!vrp_equal(header,again,144))return VRW_LAYOUT;
    struct vrp_access a={l->rsc_start,l->rsc_bytes,l->db_start,l->db_bytes,0,1,c,
        vrw_copy_proof,vrw_native_clock,vrw_native_record,vrw_copy_read};
    unsigned result=vrp_collect(&state->db_session,&a,VRP_DB,&state->db);
    return result==VRP_OK?VRW_OK:(result==VRP_REVOKED?VRW_REVOKED:
        (result==VRP_TIMEOUT?VRW_TIMEOUT:(result==VRP_CLOCK?VRW_CLOCK:
        (result==VRP_READ_FAILED || result==VRP_LOG_FAILED?VRW_IO:VRW_LAYOUT))));
}
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
    if(!(vrw_native_proof(&c)&VRP_FAULT_CONTAINED)) {
        s->status=vrw_native_fatal_collect(s,&c);
        if(s->status!=VRW_OK) {
            if(s->status==VRW_IO || s->status==VRW_REVOKED)s->disposition=VRW_QUARANTINE;
            goto finish;
        }
        goto execute;
    }
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
execute:
    if(!vrw_native_proof(&c)){s->status=VRW_REVOKED;s->disposition=VRW_QUARANTINE;goto finish;}
    /* Recheck the entire bootstrap in the same lease before any reset/vote.
     * The executor performs its own final version/geometry check again. */
    {
        uint64_t now=vrw_native_clock(&c);
        if(now>UINT64_MAX-200000){s->status=VRW_CLOCK;goto finish;}
        struct vrw_stage check={&c,now,now+200000,0,0,VRW_OK};uint8_t bytes[4];
        const unsigned offsets[3]={0,4,12};
        const uint32_t expected[3]={s->bootstrap.id,s->bootstrap.solver,s->bootstrap.config};
        for(unsigned i=0;i<3;i++) {
            if(!vrw_stage_read(&check,VRP_RSC,offsets[i],4,bytes)) {
                s->status=check.status;s->disposition=VRW_QUARANTINE;goto finish;
            }
            if(vrp_u32(bytes)!=expected[i]){s->status=VRW_LAYOUT;goto finish;}
        }
    }
    if(l->prepare_touch && l->prepare_touch(io->ctx)!=1) {
        s->status=VRW_IO;s->disposition=VRW_QUARANTINE;goto finish;
    }
    struct vrw_io bounded={&c,vrw_native_proof,vrw_native_epoch,vrw_native_clock,
        vrw_native_read32,vrw_native_write32,vrw_native_wait,io->iovdd_settle_us};
    s->status=vrw_touch_enable(&s->executor,&bounded,&s->db,(s->bootstrap.id>>8)&0xffffu);
    if(s->executor.poisoned || s->status==VRW_IO || s->status==VRW_REVOKED)s->disposition=VRW_QUARANTINE;
    else if(s->status==VRW_OK)s->disposition=VRW_KEEP_TOUCH_IDLE_HOLD;
finish:
    l->finish(io->ctx,s->disposition);return s->status;
}
#endif
