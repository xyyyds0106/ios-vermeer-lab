#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_rpmh_observe.h"
struct mock {
    uint8_t db[0x20000],rsc[0x10000];
    uint64_t now;unsigned proof,reads,events,befores,afters,ends,last_phase;
    unsigned fail_read,fail_emit,revoke_before,revoke_read,late_before,late_read;
    unsigned backwards,change_entry,change_header,reenter;
    struct vrp_session *session;struct vrp_access *access;struct vrp_result *result;
};
static struct mock m;
static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){put16(p,v&65535);put16(p+2,v>>16);}
static unsigned proof(void *p){return ((struct mock *)p)->proof;}
static uint64_t now(void *p){return ((struct mock *)p)->now;}
static int emit(void *p,const struct vrp_event *e)
{
    struct mock *x=p;++x->events;
    assert(e->sequence==x->events);
    if(e->kind==VRP_BEFORE) {
        ++x->befores;x->last_phase=e->phase;
        assert(e->read_index==x->reads+1);
        if(x->revoke_before==x->befores)x->proof=0;
        if(x->late_before==x->befores)x->now=VRP_TIMEOUT_US;
        if(x->backwards==x->befores)x->now=0;
    } else if(e->kind==VRP_AFTER) {++x->afters;assert(e->read_index==x->reads);}
    else {assert(e->kind==VRP_END);++x->ends;}
    if(x->reenter) {
        unsigned count=x->reads;
        assert(vrp_collect(x->session,x->access,e->job,x->result)==VRP_USED);
        assert(count==x->reads);
    }
    return x->fail_emit!=x->events;
}
static int read_bytes(void *p,unsigned region,uint32_t offset,uint32_t n,uint8_t *dst)
{
    struct mock *x=p;assert(x->proof==VRP_REQUIRED);assert(x->now<VRP_TIMEOUT_US+100);
    assert(x->befores==x->reads+1);++x->reads;
    assert(!(offset&3u) && !(n&3u));
    if(region==VRP_DB) {
        assert(offset<=sizeof(x->db) && n<=sizeof(x->db)-offset);
        assert((n==8 && offset==0) || (n==144 && offset==0) || n==24);
        if(x->change_entry && x->last_phase==VRP_DB_TARGET_RECHECK)x->db[offset+16]^=4;
        if(x->change_header && x->last_phase==VRP_DB_HEADER_RECHECK)x->db[8+8]^=1;
        memcpy(dst,x->db+offset,n);
    } else {
        assert(region==VRP_RSC && n==4);
        int allowed=offset==0 || offset==4 || offset==12 || offset==0xd00 || offset==0xd04;
        unsigned first=((vrp_u32(x->rsc)>>16)&255)==3?0x20:0x10;
        for(unsigned t=0;t<8;t++)for(unsigned j=0;j<4;j++)
            if(offset==0xd00+0x2a0*t+first+4*j)allowed=1;
        assert(allowed);assert(offset!=0xd08);
        memcpy(dst,x->rsc+offset,n);
    }
    if(x->revoke_read==x->reads)x->proof=0;
    if(x->late_read==x->reads)x->now=VRP_TIMEOUT_US;
    return x->fail_read!=x->reads;
}
static struct vrp_access reset(unsigned revision)
{
    memset(&m,0,sizeof(m));m.proof=VRP_REQUIRED;
    put32(m.rsc,(revision>>8)<<16 | (revision&255)<<8);
    put32(m.rsc+12,(16u<<27)|(8u<<12));
    put32(m.db,1);put32(m.db+4,0x0c0330db);
    put16(m.db+8,4);put16(m.db+10,0);put16(m.db+12,72);put16(m.db+14,3);put16(m.db+16,0x100);
    memcpy(m.db+144,"ldob2",5);put32(m.db+160,0x40100);
    memcpy(m.db+168,"PRIVATE!",8);put32(m.db+184,0xdeadbeef);
    memcpy(m.db+192,"ldob12",6);put32(m.db+208,0x40200);
    struct vrp_access a={VRP_RSC_BASE,VRP_RSC_BYTES,VRP_DB_BASE,VRP_DB_BYTES,
        revision,1,&m,proof,now,emit,read_bytes};
    return a;
}
static unsigned run(struct vrp_access *a,unsigned job,struct vrp_result *r)
{
    struct vrp_session s={0};m.session=&s;m.access=a;m.result=r;
    unsigned status=vrp_collect(&s,a,job,r);
    assert(status==r->status && m.reads==r->reads && m.ends==1);
    unsigned reads=m.reads;assert(vrp_collect(&s,a,job,r)==VRP_USED);assert(reads==m.reads);
    return status;
}
static void normal(void)
{
    struct vrp_result r;
    for(unsigned rev=0;rev<2;rev++) {
        struct vrp_access a=reset(rev?0x300:0x207);m.reenter=1;
        assert(run(&a,VRP_RSC,&r)==VRP_OK && r.complete && r.reads==37);
        assert(r.bootstrap_valid==7 && r.irq_valid==3);
        for(unsigned t=0;t<8;t++)assert(r.tcs_valid[t]==15);
    }
    struct vrp_access a=reset(0x300);m.reenter=1;
    assert(run(&a,VRP_DB,&r)==VRP_OK && r.complete && r.reads==8);
    assert(r.resource[0].address==0x40100 && r.resource[1].address==0x40200);
    assert(r.resource[0].aux_version==0x100 && r.db_valid==3);
    const uint8_t *bytes=(const uint8_t *)&r;
    for(size_t i=0;i+8<=sizeof(r);i++)assert(memcmp(bytes+i,"PRIVATE!",8));
    assert(r.resource[0].address!=0xdeadbeef && r.resource[1].address!=0xdeadbeef);
    a=reset(0x300);put16(m.db+14,1024);put16(m.db+12,24576);
    assert(run(&a,VRP_DB,&r)==VRP_OK && r.complete && r.reads==1029);
}
static void failures(void)
{
    struct vrp_result r;struct vrp_access a;
    struct vrp_session absent={0};
    assert(vrp_collect(&absent,NULL,VRP_RSC,&r)==VRP_GATED && !r.reads && !r.complete);
    a=reset(0x300);a.read=NULL;absent=(struct vrp_session){0};
    assert(vrp_collect(&absent,&a,VRP_DB,&r)==VRP_GATED && !m.reads && !m.events);
    assert(vrp_collect(&absent,&a,VRP_DB,&r)==VRP_USED);
    for(unsigned job=VRP_RSC;job<=VRP_DB;job++) {
        unsigned reads=job==VRP_RSC?37:8;
        for(unsigned bit=1;bit<64;bit<<=1) {
            a=reset(0x300);m.proof^=bit;
            assert(run(&a,job,&r)==VRP_GATED && !r.reads);
        }
        for(unsigned i=1;i<=reads;i++) {
            a=reset(0x300);m.fail_read=i;
            assert(run(&a,job,&r)==VRP_READ_FAILED && r.reads==i && !r.complete);
            a=reset(0x300);m.revoke_before=i;
            assert(run(&a,job,&r)==VRP_REVOKED && r.reads==i-1 && !r.complete);
            a=reset(0x300);m.revoke_read=i;
            assert(run(&a,job,&r)==VRP_REVOKED && r.reads==i && !r.complete);
            a=reset(0x300);m.late_before=i;
            assert(run(&a,job,&r)==VRP_TIMEOUT && r.reads==i-1 && !r.complete);
            a=reset(0x300);m.late_read=i;
            assert(run(&a,job,&r)==VRP_TIMEOUT && r.reads==i && !r.complete);
        }
        for(unsigned i=1;i<=reads*2+1;i++) {
            a=reset(0x300);m.fail_emit=i;
            assert(run(&a,job,&r)==VRP_LOG_FAILED && !r.complete);
            assert(r.reads<=(i+1)/2);
        }
        a=reset(0x300);m.now=100;m.backwards=1;
        assert(run(&a,job,&r)==VRP_CLOCK && !r.reads);
        a=reset(0x300);m.now=UINT64_MAX;
        assert(run(&a,job,&r)==VRP_CLOCK && !r.reads);
    }
    a=reset(0x300);a.rsc_start+=4;
    assert(run(&a,VRP_RSC,&r)==VRP_BOUNDS && !r.reads);
    a=reset(0x300);a.db_bytes=UINT64_MAX;
    assert(run(&a,VRP_DB,&r)==VRP_BOUNDS && !r.reads);
    a=reset(0x300);a.rsc_bytes--;
    assert(run(&a,VRP_RSC,&r)==VRP_BOUNDS && !r.reads);
    a=reset(0x300);a.db_start-=4;
    assert(run(&a,VRP_DB,&r)==VRP_BOUNDS && !r.reads);
    a=reset(0x300);a.rsc_revision=0;
    assert(run(&a,VRP_RSC,&r)==VRP_GATED && r.reads==3 && !r.irq_valid);
    a=reset(0x300);a.db_version=0;
    assert(run(&a,VRP_DB,&r)==VRP_GATED && r.reads==1 && !r.resource[0].valid);
    a=reset(0x300);a.rsc_revision=0x207;
    assert(run(&a,VRP_RSC,&r)==VRP_VERSION && r.reads==3);
    a=reset(0x400);
    assert(run(&a,VRP_RSC,&r)==VRP_VERSION && r.reads==3);
    a=reset(0x300);put32(m.rsc+12,(7u<<12)|(16u<<27));
    assert(run(&a,VRP_RSC,&r)==VRP_GEOMETRY && r.reads==3);
    a=reset(0x300);put32(m.rsc+12,UINT32_MAX);
    assert(run(&a,VRP_RSC,&r)==VRP_GEOMETRY && r.reads==3);
    a=reset(0x300);put32(m.db,2);
    assert(run(&a,VRP_DB,&r)==VRP_VERSION && r.reads==1);
    a=reset(0x300);m.db[4]=0;
    assert(run(&a,VRP_DB,&r)==VRP_CORRUPT && r.reads==1);
    a=reset(0x300);m.db[140]=1;
    assert(run(&a,VRP_DB,&r)==VRP_GATED && r.reads==2);
    a=reset(0x300);put16(m.db+14,1025);
    assert(run(&a,VRP_DB,&r)==VRP_BOUNDS && r.reads==2);
    a=reset(0x300);put16(m.db+10,1);
    assert(run(&a,VRP_DB,&r)==VRP_BOUNDS && r.reads==2);
    a=reset(0x300);put16(m.db+24,3);put16(m.db+30,2);
    assert(run(&a,VRP_DB,&r)==VRP_CORRUPT && r.reads==2);
    a=reset(0x300);memcpy(m.db+168,m.db+144,24);
    assert(run(&a,VRP_DB,&r)==VRP_DUPLICATE && !r.complete);
    a=reset(0x300);m.db[144]='x';
    assert(run(&a,VRP_DB,&r)==VRP_MISSING && !r.complete);
    a=reset(0x300);put16(m.db+8,3);
    assert(run(&a,VRP_DB,&r)==VRP_CORRUPT && !r.complete);
    a=reset(0x300);put32(m.db+160,0x50100);
    assert(run(&a,VRP_DB,&r)==VRP_CORRUPT && !r.complete);
    a=reset(0x300);put16(m.db+164,4);put16(m.db+12,0);
    assert(run(&a,VRP_DB,&r)==VRP_CORRUPT && !r.complete);
    a=reset(0x300);m.change_entry=1;
    assert(run(&a,VRP_DB,&r)==VRP_CHANGED && !r.complete);
    a=reset(0x300);m.change_header=1;
    assert(run(&a,VRP_DB,&r)==VRP_CHANGED && !r.complete);
}
static void mutate(void)
{
    uint32_t rng=0x1234;struct vrp_result r;
    for(unsigned i=0;i<10000;i++) {
        struct vrp_access a=reset(0x300);
        rng=rng*1664525u+1013904223u;
        unsigned at=(rng>>8)%216;
        rng=rng*1664525u+1013904223u;m.db[at]^=(uint8_t)(1u<<(rng%8));
        (void)run(&a,VRP_DB,&r);
        assert(r.reads<=VRP_MAX_ENTRIES+5);
        if(r.complete)assert(r.status==VRP_OK && r.resource[0].valid && r.resource[1].valid);
    }
}
int main(void)
{
    normal();failures();mutate();
    puts("RPMh observer: v2.7/v3.0, allowlist, all read/log failure positions, lease/time gates, redaction, replay, 10000 mutations PASS");
    return 0;
}
