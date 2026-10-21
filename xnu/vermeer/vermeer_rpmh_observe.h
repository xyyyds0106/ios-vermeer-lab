/* K70 read-only RPMh metadata candidate. No mapping, MMIO dereference, writes,
 * transport, IRQ handling or exception recovery. Caller owns one serialized
 * worker and a retained, fault-contained provider. Never call Linux probe here.
 * Zero session once per boot/job slot. No retry/reset API; callbacks must not
 * reenter or mutate pinned inputs. No raw cmd-db bytes escape in result/events.
 */
#ifndef VERMEER_RPMH_OBSERVE_H
#define VERMEER_RPMH_OBSERVE_H
#include <stdint.h>
#include <stddef.h>
#define VRP_RSC_BASE UINT64_C(0x17a20000)
#define VRP_RSC_BYTES UINT64_C(0x10000)
#define VRP_DB_BASE UINT64_C(0x81c60000)
#define VRP_DB_BYTES UINT64_C(0x20000)
#define VRP_BOARD 1u
#define VRP_LIVE_DT 2u
#define VRP_MAPPING_HELD 4u
#define VRP_ATTRIBUTES 8u
#define VRP_AWAKE_LEASE 16u
#define VRP_FAULT_CONTAINED 32u
#define VRP_REQUIRED 63u
#define VRP_MAX_ENTRIES 1024u
#define VRP_TIMEOUT_US UINT64_C(10000000)
enum { VRP_RSC=1, VRP_DB=2 };
enum { VRP_OK=0,VRP_ARGUMENT,VRP_USED,VRP_GATED,VRP_BOUNDS,VRP_VERSION,
       VRP_GEOMETRY,VRP_CORRUPT,VRP_MISSING,VRP_DUPLICATE,VRP_CHANGED,
       VRP_READ_FAILED,VRP_LOG_FAILED,VRP_REVOKED,VRP_CLOCK,VRP_TIMEOUT };
enum { VRP_BEFORE=1,VRP_AFTER,VRP_END };
enum { VRP_BOOTSTRAP=1,VRP_IRQ,VRP_TCS,VRP_DB_HEADER,VRP_DB_ENTRY,
       VRP_DB_TARGET_RECHECK,VRP_DB_HEADER_RECHECK };
struct vrp_event {
    uint64_t sequence,physical;
    uint32_t kind,job,phase,length,read_index,status;
};
struct vrp_access {
    uint64_t rsc_start,rsc_bytes,db_start,db_bytes;
    /* Policies are independently qualified by platform owner, NOT copied from
     * the observed header/ID. Zero permits bootstrap discovery only. */
    uint32_t rsc_revision; /* 0, 0x0207 or 0x0300 (major<<8 | minor) */
    uint32_t db_version;   /* 0 or 1; no other format implemented */
    void *ctx;
    unsigned (*proof)(void *); /* no I/O; includes stable lease */
    uint64_t (*now_us)(void *); /* no I/O/ownership changes */
    int (*emit)(void *,const struct vrp_event *); /* whole preserved record or0 */
    int (*read)(void *,unsigned region,uint32_t offset,uint32_t bytes,uint8_t *);
};
struct vrp_resource {
    uint32_t valid,address,priority[2],aux_length,aux_version;
    /* Index0=ldob2, index1=ldob12. No strings or generic resource output. */
};
struct vrp_result {
    uint32_t status,complete,reads;
    uint32_t bootstrap_valid,id,solver,config;
    uint32_t irq_valid,irq_enable,irq_status,tcs_valid[8],tcs[8][4];
    uint32_t db_valid,db_version,db_flags;
    struct vrp_resource resource[2];
};
struct vrp_session {
    unsigned used,status,job,phase,reads;
    uint64_t start,last,deadline,event_sequence,last_physical;
    uint32_t last_length;
};
static inline uint16_t vrp_u16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0]|(uint16_t)p[1]<<8); }
static inline uint32_t vrp_u32(const uint8_t *p)
{ return (uint32_t)vrp_u16(p)|(uint32_t)vrp_u16(p+2)<<16; }
static inline int vrp_equal(const uint8_t *a,const uint8_t *b,unsigned n)
{ for(unsigned i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1; }
static inline int vrp_range(uint32_t off,uint32_t n,uint32_t size)
{ return off<=size && n<=size-off; }
static inline int vrp_fail(struct vrp_session *s,unsigned error)
{ s->status=error;return 0; }
static inline int vrp_ready(struct vrp_session *s,const struct vrp_access *a)
{
    if((a->proof(a->ctx)&VRP_REQUIRED)!=VRP_REQUIRED)return vrp_fail(s,VRP_REVOKED);
    uint64_t now=a->now_us(a->ctx);
    if(now<s->last)return vrp_fail(s,VRP_CLOCK);
    s->last=now;
    if(now>=s->deadline)return vrp_fail(s,VRP_TIMEOUT);
    return 1;
}
static inline int vrp_emit(struct vrp_session *s,const struct vrp_access *a,unsigned kind)
{
    struct vrp_event e={++s->event_sequence,s->last_physical,kind,s->job,
        s->phase,s->last_length,s->reads+(kind==VRP_BEFORE),s->status};
    if(a->emit(a->ctx,&e)!=1)return vrp_fail(s,VRP_LOG_FAILED);
    return 1;
}
/* Private primitive: only fixed-phase collectors below select addresses.
 * Provider reads relative to the EXACT qualified physical resource start.
 * The CPU mapping may be page-rounded, but its extra bytes are never allowed.
 */
static inline int vrp_read(struct vrp_session *s,const struct vrp_access *a,
    unsigned phase,uint32_t off,uint32_t n,uint8_t *dst)
{
    uint32_t limit=s->job==VRP_RSC?(uint32_t)VRP_RSC_BYTES:(uint32_t)VRP_DB_BYTES;
    if(!vrp_range(off,n,limit) || !n || n>144 || (off&3u) || (n&3u))
        return vrp_fail(s,VRP_BOUNDS);
    s->phase=phase;s->last_physical=(s->job==VRP_RSC?VRP_RSC_BASE:VRP_DB_BASE)+off;
    s->last_length=n;
    if(!vrp_ready(s,a) || !vrp_emit(s,a,VRP_BEFORE) || !vrp_ready(s,a))return 0;
    ++s->reads;
    int ok=a->read(a->ctx,s->job,off,n,dst);
    if(ok!=1) {
        vrp_fail(s,VRP_READ_FAILED);
        (void)vrp_emit(s,a,VRP_AFTER);
        return 0;
    }
    if(!vrp_ready(s,a))return 0; /* discard values if lease/time failed */
    if(!vrp_emit(s,a,VRP_AFTER) || !vrp_ready(s,a))return 0;
    return 1;
}
static inline int vrp_word(struct vrp_session *s,const struct vrp_access *a,
    unsigned phase,uint32_t off,uint32_t *value)
{
    uint8_t b[4];
    if(!vrp_read(s,a,phase,off,4,b))return 0;
    *value=vrp_u32(b);return 1;
}
static inline int vrp_rsc(struct vrp_session *s,const struct vrp_access *a,struct vrp_result *r)
{
    if(!vrp_word(s,a,VRP_BOOTSTRAP,0,&r->id))return 0;
    r->bootstrap_valid|=1;
    if(!vrp_word(s,a,VRP_BOOTSTRAP,4,&r->solver))return 0;
    r->bootstrap_valid|=2;
    if(!vrp_word(s,a,VRP_BOOTSTRAP,12,&r->config))return 0;
    r->bootstrap_valid|=4;
    if(!a->rsc_revision)return vrp_fail(s,VRP_GATED);
    unsigned observed=(r->id>>8)&0xffffu;
    if(observed!=a->rsc_revision || (observed!=0x207 && observed!=0x300))
        return vrp_fail(s,VRP_VERSION);
    unsigned count=(r->config>>12)&63u,ncpt=(r->config>>27)&31u;
    if(r->config==UINT32_MAX || r->solver==UINT32_MAX || count<8 || count>32 ||
       !ncpt || ncpt>16)return vrp_fail(s,VRP_GEOMETRY);
    if(!vrp_word(s,a,VRP_IRQ,0xd00,&r->irq_enable))return 0;
    r->irq_valid|=1;
    if(!vrp_word(s,a,VRP_IRQ,0xd04,&r->irq_status))return 0;
    r->irq_valid|=2;
    unsigned first=observed==0x300?0x20:0x10;
    for(unsigned i=0;i<8;i++)for(unsigned j=0;j<4;j++) {
        if(!vrp_word(s,a,VRP_TCS,0xd00+0x2a0*i+first+4*j,&r->tcs[i][j]))return 0;
        r->tcs_valid[i]|=1u<<j;
    }
    return 1;
}
static inline int vrp_db(struct vrp_session *s,const struct vrp_access *a,struct vrp_result *r)
{
    uint8_t bootstrap[8],header[144],check[144],entry[24],again[24];
    uint32_t offsets[8],sizes[8],aux[8];unsigned rows=0,total=0;
    if(!vrp_read(s,a,VRP_BOOTSTRAP,0,8,bootstrap))return 0;
    r->db_version=vrp_u32(bootstrap);r->db_valid=1;
    if(vrp_u32(bootstrap+4)!=0x0c0330dbu)return vrp_fail(s,VRP_CORRUPT);
    if(!a->db_version)return vrp_fail(s,VRP_GATED);
    if(a->db_version!=1 || r->db_version!=1)return vrp_fail(s,VRP_VERSION);
    if(!vrp_read(s,a,VRP_DB_HEADER,0,144,header))return 0;
    if(!vrp_equal(header,bootstrap,8))return vrp_fail(s,VRP_CHANGED);
    r->db_flags=vrp_u32(header+140);r->db_valid|=2;
    if(r->db_flags&1u)return vrp_fail(s,VRP_GATED);
    for(unsigned i=0;i<8;i++) {
        const uint8_t *row=header+8+i*16;
        if(!vrp_u16(row))break;
        unsigned count=vrp_u16(row+6);
        if(count>VRP_MAX_ENTRIES-total)return vrp_fail(s,VRP_BOUNDS);
        total+=count;offsets[i]=144u+vrp_u16(row+2);sizes[i]=24u*count;
        aux[i]=144u+vrp_u16(row+4);
        if((offsets[i]&3u) || !vrp_range(offsets[i],sizes[i],(uint32_t)VRP_DB_BYTES) ||
           !vrp_range(aux[i],0,(uint32_t)VRP_DB_BYTES))return vrp_fail(s,VRP_BOUNDS);
        for(unsigned j=0;j<i;j++)if(sizes[i] && sizes[j] &&
            offsets[i]<offsets[j]+sizes[j] && offsets[j]<offsets[i]+sizes[i])
            return vrp_fail(s,VRP_CORRUPT);
        ++rows;
    }
    static const uint8_t names[2][8]={{'l','d','o','b','2',0,0,0},{'l','d','o','b','1','2',0,0}};
    for(unsigned i=0;i<rows;i++)for(uint32_t at=offsets[i];at<offsets[i]+sizes[i];at+=24) {
        if(!vrp_read(s,a,VRP_DB_ENTRY,at,24,entry))return 0;
        for(unsigned k=0;k<2;k++)if(vrp_equal(entry,names[k],8)) {
            if(r->resource[k].valid)return vrp_fail(s,VRP_DUPLICATE);
            uint32_t addr=vrp_u32(entry+16),length=vrp_u16(entry+20);
            uint32_t auxoff=aux[i]+vrp_u16(entry+22);
            if(vrp_u16(header+8+i*16)!=4 || !addr || (addr&3u) ||
               ((addr>>16)&7u)!=4 || addr>UINT32_MAX-8 ||
               !vrp_range(auxoff,length,(uint32_t)VRP_DB_BYTES))return vrp_fail(s,VRP_CORRUPT);
            for(unsigned j=0;j<rows;j++)if(length && sizes[j] &&
                auxoff<offsets[j]+sizes[j] && offsets[j]<auxoff+length)
                return vrp_fail(s,VRP_CORRUPT);
            if(!vrp_read(s,a,VRP_DB_TARGET_RECHECK,at,24,again))return 0;
            if(!vrp_equal(entry,again,24))return vrp_fail(s,VRP_CHANGED);
            r->resource[k]=(struct vrp_resource){1,addr,{vrp_u32(entry+8),vrp_u32(entry+12)},
                length,vrp_u16(header+8+i*16+8)};
        }
    }
    if(!vrp_read(s,a,VRP_DB_HEADER_RECHECK,0,144,check))return 0;
    if(!vrp_equal(header,check,144))return vrp_fail(s,VRP_CHANGED);
    if(!r->resource[0].valid || !r->resource[1].valid)return vrp_fail(s,VRP_MISSING);
    return 1;
}
/* result must be disjoint from session/access/provider backing memory.
 * Complete means an observation finished, never voltage/enable/ownership proof.
 * DB summary is usable only with complete==1 AND status==VRP_OK. Equal rereads
 * detect some changes; they do not establish firmware coherence or a lock.
 */
static inline unsigned vrp_collect(struct vrp_session *s,const struct vrp_access *access,
                                  unsigned job,struct vrp_result *r)
{
    if(!s || !r)return VRP_ARGUMENT;
    if(s->used)return VRP_USED; /* preserve first result on reentry/replay */
    s->used=1;s->status=VRP_ARGUMENT;s->job=job;
    *r=(struct vrp_result){0};r->status=VRP_ARGUMENT;
    if(job!=VRP_RSC && job!=VRP_DB)return r->status;
    if(!access || !access->proof || !access->now_us || !access->emit || !access->read) {
        s->status=r->status=VRP_GATED;return r->status;
    }
    const struct vrp_access a=*access;
    if((job==VRP_RSC && (a.rsc_start!=VRP_RSC_BASE || a.rsc_bytes!=VRP_RSC_BYTES)) ||
       (job==VRP_DB && (a.db_start!=VRP_DB_BASE || a.db_bytes!=VRP_DB_BYTES))) {
        s->status=VRP_BOUNDS;goto end;
    }
    if((a.proof(a.ctx)&VRP_REQUIRED)!=VRP_REQUIRED) {s->status=VRP_GATED;goto end;}
    s->start=s->last=a.now_us(a.ctx);
    if(s->start>UINT64_MAX-VRP_TIMEOUT_US) {s->status=VRP_CLOCK;goto end;}
    s->deadline=s->start+VRP_TIMEOUT_US;s->status=VRP_OK;
    r->complete=job==VRP_RSC?(unsigned)vrp_rsc(s,&a,r):(unsigned)vrp_db(s,&a,r);
end:
    if(!vrp_emit(s,&a,VRP_END))r->complete=0;
    r->status=s->status;r->reads=s->reads;
    if(r->status!=VRP_OK)r->complete=0;
    return r->status;
}
#endif
