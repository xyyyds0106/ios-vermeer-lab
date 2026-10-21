#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_rpmh_touch_native.h"
struct fake {
    uint32_t r[0x4000];uint8_t db[0x20000];
    uint64_t now,generation;unsigned proof,rev,reads,writes,ops,dbreads;
    unsigned failread,failwrite,revoke,late,backwards,epochchange;
    unsigned noirq,incomplete,allones,noidle,triggered,finish_count,finish_mode;
    unsigned qualify_rev,qualify_db,begins,begin_fail;
    unsigned delayed,pending_tcs,stuck_write,change_id;
    unsigned revoke_settle,late_settle,settle;
    unsigned faildb,change_target,prepare_failed,prepare_calls;
    unsigned revoke_copy,timeout_copy,no_copy;
    uint64_t first_trigger;
    uint64_t ready_at;
    uint32_t addr[2],write_off[128],write_val[128];
    struct vrw_native_state *state;
};
static struct fake f;
static uint8_t staged_db[0x20000];
static void p16(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void p32(uint8_t *p,uint32_t v){p16(p,v&65535);p16(p+2,v>>16);}
static unsigned first(void){return f.rev==0x300?0x20:0x10;}
static unsigned msg(void){return f.rev==0x300?0x34:0x30;}
static unsigned stride(void){return f.rev==0x300?24:20;}
static unsigned bits(void *p){return ((struct fake *)p)->proof;}
static uint64_t epoch(void *p){return ((struct fake *)p)->generation;}
static uint64_t time_us(void *p){return ((struct fake *)p)->now;}
static void wait_us(void *p,unsigned n)
{
    ((struct fake *)p)->now+=n;
    if(n>1 && f.revoke_settle)f.proof=0;
    if(n>1 && f.late_settle)f.now+=300000;
    if(f.delayed && f.pending_tcs<3 && f.now>=f.ready_at) {
        unsigned b=0xd00+f.pending_tcs*0x2a0;
        if(!f.noirq && (f.r[0xd00/4]&(1u<<f.pending_tcs)))f.r[0xd04/4]|=1u<<f.pending_tcs;
        f.r[(b+first()+8)/4]=1;
        for(unsigned c=0;c<3;c++)f.r[(b+msg()+12+c*stride())/4]=0x10100;
        f.pending_tcs=99;
    }
}
static void effect(void)
{
    ++f.ops;++f.now;
    if(f.revoke==f.ops)f.proof=0;
    if(f.epochchange==f.ops)++f.generation;
    if(f.late==f.ops)f.now+=20000000;
    if(f.backwards==f.ops)f.now=0;
    if(f.state)assert(vrw_native_touch_init(f.state,NULL)==VRW_REUSED);
}
static int rd(void *p,uint32_t off,uint32_t *value)
{
    (void)p;assert(vrw_proof_valid(f.proof));assert(!(off&3) && off<0x10000);
    ++f.reads;*value=f.r[off/4];effect();return f.failread!=f.reads;
}
static int wr(void *p,uint32_t off,uint32_t value)
{
    (void)p;assert(vrw_proof_valid(f.proof));assert(!(off&3) && off<0x10000);
    assert(f.writes<128);f.write_off[f.writes]=off;f.write_val[f.writes]=value;++f.writes;
    /* No sleep/wake/fast-path, DRV0/1 or PMIC writes. */
    assert(off==0xd00 || off==0xd08 || (off>=0xd00 && off<0xd00+3*0x2a0));
    if(off==0xd08)f.r[0xd04/4]&=~value;
    else if(f.stuck_write!=f.writes)f.r[off/4]=value;
    for(unsigned t=0;t<3;t++)if(off==0xd00+t*0x2a0+first()+4 && value==(VRW_AMC|VRW_TRIGGER)) {
        unsigned rail=f.triggered++;assert(rail<2);
        if(!rail)f.first_trigger=f.now;else assert(f.now-f.first_trigger>=f.settle);
        unsigned b=0xd00+t*0x2a0;
        for(unsigned c=0;c<3;c++) {
            unsigned cmd=b+msg()+c*stride();
            assert(f.r[cmd/4]==VRW_ACK);
            assert(f.r[(cmd+4)/4]==f.addr[rail]+(c==0?0:(c==1?8:4)));
            assert(f.r[(cmd+8)/4]==(c==0?(rail==0?1800u:3200u):(c==1?7u:1u)));
            f.r[(cmd+12)/4]=f.allones?UINT32_MAX:(f.incomplete?0x100u:0x10100u);
        }
        if(!f.noirq && (f.r[0xd00/4]&(1u<<t)))f.r[0xd04/4]|=1u<<t;
        f.r[(b+first()+8)/4]=f.noidle?0:1;
        if(f.delayed) {
            f.pending_tcs=t;f.ready_at=f.now+50;f.r[0xd04/4]&=~(1u<<t);f.r[(b+first()+8)/4]=0;
            for(unsigned c=0;c<3;c++)f.r[(b+msg()+12+c*stride())/4]=0;
        }
    }
    effect();return f.failwrite!=f.writes;
}
static int dbread(void *p,uint32_t off,uint32_t n,uint8_t *dst)
{
    (void)p;assert(off<=sizeof(f.db) && n<=sizeof(f.db)-off);++f.dbreads;
    memcpy(dst,f.db+off,n);if(f.change_target && off==144)f.db[160]^=4;
    effect();return f.faildb!=f.dbreads;
}
static int record(void *p,const struct vrp_event *e)
{
    (void)p;
    if(f.state)assert(vrw_native_touch_init(f.state,NULL)==VRW_REUSED);
    if(f.change_id && e->job==VRP_DB && e->kind==VRP_END)f.r[0]=0x40000;
    if(e->phase==VRP_DB_HEADER && e->kind==VRP_BEFORE) {
        if(f.revoke_copy)f.proof=0;
        if(f.timeout_copy)f.now+=20000000;
    }
    return 1;
}
static void finish(void *p,unsigned mode){(void)p;++f.finish_count;f.finish_mode=mode;}
static int prepare(void *p){(void)p;++f.prepare_calls;return !f.prepare_failed;}
static int begin(void *p,struct vrw_native_lease *l)
{
    (void)p;++f.begins;if(f.begin_fail)return 0;
    *l=(struct vrw_native_lease){
        .io={&f,bits,epoch,time_us,rd,wr,wait_us,f.settle},
        .rsc_start=VRP_RSC_BASE,.rsc_bytes=VRP_RSC_BYTES,.db_start=VRP_DB_BASE,.db_bytes=VRP_DB_BYTES,
        .generation=f.generation,.read_db=dbread,.record=record,.finish=finish,
        .metadata_copy=staged_db,.metadata_bytes=sizeof(staged_db),.prepare_touch=prepare};
    if(f.no_copy)l->metadata_copy=NULL;
    return 1;
}
static void reset(unsigned rev)
{
    memset(&f,0,sizeof(f));f.proof=VRW_REQUIRED;f.generation=11;f.rev=rev;
    f.qualify_rev=rev;f.qualify_db=1;f.now=100;
    f.pending_tcs=99;
    f.settle=18000; /* synthetic board-policy fixture, not a K70 qualification */
    f.addr[0]=0x40200;f.addr[1]=0x40100;
    f.r[0]=(rev>>8)<<16|(rev&255)<<8;f.r[3]=(16u<<27)|(8u<<12);
    f.r[0xd00/4]=0xab;
    for(unsigned t=0;t<8;t++)f.r[(0xd00+t*0x2a0+first()+8)/4]=1;
    p32(f.db,1);p32(f.db+4,0x0c0330db);p16(f.db+8,4);p16(f.db+12,48);p16(f.db+14,2);
    memcpy(f.db+144,"ldob2",5);p32(f.db+160,f.addr[1]);
    memcpy(f.db+168,"ldob12",6);p32(f.db+184,f.addr[0]);
}
static unsigned run(struct vrw_native_state *s)
{
    *s=(struct vrw_native_state){0};f.state=s;
    struct vrw_native_provider provider={&f,begin};
    unsigned result=vrw_native_touch_init(s,&provider);
    assert(f.finish_count==1 || f.begin_fail);
    unsigned writes=f.writes,reads=f.reads,begins=f.begins;
    assert(vrw_native_touch_init(s,&provider)==VRW_REUSED);
    assert(writes==f.writes && reads==f.reads && begins==f.begins);
    return result;
}
static void healthy(void)
{
    struct vrw_native_state s;
    for(unsigned i=0;i<2;i++) {
        reset(i?0x300:0x207);
        uint32_t foreign[5*0x2a0/4];memcpy(foreign,&f.r[(0xd00+3*0x2a0)/4],sizeof(foreign));
        assert(run(&s)==VRW_OK);assert(s.executor.completed_rails==3 && f.triggered==2);
        assert(s.executor.phase==VRW_DONE && !s.executor.poisoned && f.writes==42);
        assert(f.finish_mode==VRW_KEEP_TOUCH_IDLE_HOLD && f.r[0xd00/4]==0xab);
        assert(!memcmp(foreign,&f.r[(0xd00+3*0x2a0)/4],sizeof(foreign)));
    }
    reset(0x300);f.r[(0xd00+first()+4)/4]=1; /* occupied0 -> use1 */
    assert(run(&s)==VRW_OK && s.executor.selected==1);
    for(unsigned i=0;i<f.writes;i++)assert(f.write_off[i]==0xd00 || f.write_off[i]==0xd08 || f.write_off[i]>=0xd00+0x2a0);
    reset(0x300);f.delayed=1;assert(run(&s)==VRW_OK && f.triggered==2);
    reset(0x300);f.r[0xd00/4]=0;
    assert(run(&s)==VRW_OK && f.triggered==2 && f.r[0xd00/4]==0);
}
static void guards(void)
{
    struct vrw_native_state s={0};assert(vrw_native_touch_init(&s,NULL)==VRW_GATED);
    for(unsigned bit=1;bit<2048;bit<<=1) {
        reset(0x300);f.proof^=bit;assert(run(&s)==VRW_GATED && !f.reads && !f.writes && !f.dbreads);
    }
    reset(0x300);f.r[0]=0;assert(run(&s)==VRW_LAYOUT && !f.writes && !f.dbreads);
    reset(0x300);p32(f.db,2);assert(run(&s)==VRW_GATED && !f.writes && f.dbreads==1);
    reset(0x300);f.settle=50001;assert(run(&s)==VRW_GATED && !f.writes && !f.reads);
    reset(0x300);f.db[144]='x';assert(run(&s)==VRW_GATED && !f.writes && f.reads==3);
    reset(0x300);p32(f.db+184,0x40104);assert(run(&s)==VRW_LAYOUT && !f.writes);
    reset(0x300);f.r[0]=0x40000;assert(run(&s)==VRW_LAYOUT && !f.writes);
    reset(0x300);f.change_id=1;assert(run(&s)==VRW_LAYOUT && !f.writes);
    reset(0x300);f.r[1]=1u<<24;assert(run(&s)==VRW_LAYOUT && !f.writes);
    reset(0x300);f.r[3]=(2u<<27)|(8u<<12);assert(run(&s)==VRW_LAYOUT && !f.writes);
    reset(0x300);
    for(unsigned t=0;t<3;t++)f.r[(0xd00+t*0x2a0+first()+4)/4]=1;
    assert(run(&s)==VRW_NO_SLOT && !f.writes);
    reset(0x300);f.r[(0xd00+first()+12)/4]=1;f.r[(0xd00+msg()+4)/4]=f.addr[0]+4;
    assert(run(&s)==VRW_CONFLICT && !f.writes);
}
static void failures(void)
{
    struct vrw_native_state s;reset(0x300);assert(run(&s)==VRW_OK);
    unsigned reads=f.reads,writes=f.writes,ops=f.ops;
    for(unsigned i=1;i<=writes;i++) {
        reset(0x300);f.failwrite=i;
        assert(run(&s)==VRW_IO && f.writes==i && s.executor.poisoned && f.finish_mode==VRW_QUARANTINE);
    }
    reset(0x300);f.stuck_write=2;
    assert(run(&s)==VRW_VERIFY && f.writes==2 && s.executor.poisoned && f.finish_mode==VRW_QUARANTINE);
    reset(0x300);f.revoke_settle=1;
    assert(run(&s)==VRW_REVOKED && f.triggered==1 && s.executor.completed_rails==1 && s.executor.poisoned);
    reset(0x300);f.late_settle=1;
    assert(run(&s)==VRW_TIMEOUT && f.triggered==1 && s.executor.completed_rails==1 && s.executor.poisoned);
    for(unsigned i=1;i<=reads;i++) {
        reset(0x300);f.failread=i;
        assert(run(&s)==VRW_IO && f.reads==i && f.finish_mode==VRW_QUARANTINE);
    }
    for(unsigned i=1;i<=ops;i++) {
        reset(0x300);f.revoke=i;assert(run(&s)==VRW_REVOKED && f.ops==i);
        reset(0x300);f.epochchange=i;assert(run(&s)==VRW_REVOKED && f.ops==i);
        reset(0x300);f.late=i;assert(run(&s)==VRW_TIMEOUT && f.ops==i);
        reset(0x300);f.backwards=i;assert(run(&s)==VRW_CLOCK && f.ops==i);
    }
    for(unsigned fail=0;fail<4;fail++) {
        reset(0x300);
        if(fail==0)f.noirq=1;else if(fail==1)f.incomplete=1;else if(fail==2)f.noidle=1;else f.allones=1;
        assert(run(&s)==VRW_TIMEOUT && f.triggered==1 && s.executor.completed_rails==0);
        assert(s.executor.failure_phase==VRW_WAIT && s.executor.last_read_valid);
        assert(s.executor.poisoned && f.finish_mode==VRW_QUARANTINE);
        for(unsigned i=0;i<f.writes;i++)assert(f.write_off[i]!=0xd08); /* no false cleanup */
    }
    printf("Fault positions: %u reads, %u writes, %u callback boundaries; all PASS\n",reads,writes,ops);
}
static void fatal_native(void)
{
    struct vrw_native_state s;
    const unsigned fatal=(VRW_REQUIRED&~VRP_FAULT_CONTAINED)|VRW_FATAL_MMIO;
    for(unsigned rev=0;rev<2;rev++) {
        reset(rev?0x300:0x207);f.proof=fatal;
        assert(run(&s)==VRW_OK && f.triggered==2 && f.prepare_calls==1);
        assert(!(f.proof&VRP_FAULT_CONTAINED) && f.finish_mode==VRW_KEEP_TOUCH_IDLE_HOLD);
    }
    reset(0x300);f.proof=fatal;assert(run(&s)==VRW_OK);
    unsigned reads=f.reads,dbreads=f.dbreads,ops=f.ops;
    for(unsigned at=1;at<=reads;at++) {
        reset(0x300);f.proof=fatal;f.failread=at;
        assert(run(&s)==VRW_IO && f.reads==at && f.finish_mode==VRW_QUARANTINE);
    }
    for(unsigned at=1;at<=dbreads;at++) {
        reset(0x300);f.proof=fatal;f.faildb=at;
        assert(run(&s)==VRW_IO && f.dbreads==at && !f.writes && f.finish_mode==VRW_QUARANTINE);
    }
    for(unsigned at=1;at<=ops;at++) {
        reset(0x300);f.proof=fatal;f.revoke=at;
        assert(run(&s)!=VRW_OK && f.ops==at);
        reset(0x300);f.proof=fatal;f.epochchange=at;
        assert(run(&s)!=VRW_OK && f.ops==at);
    }
    reset(0x300);f.proof=fatal;f.prepare_failed=1;
    assert(run(&s)==VRW_IO && f.prepare_calls==1 && !f.writes && f.finish_mode==VRW_QUARANTINE);
    reset(0x300);f.proof=fatal;f.change_target=1;
    assert(run(&s)==VRW_LAYOUT && !f.writes && !f.prepare_calls);
    reset(0x300);f.proof=fatal;p32(f.db,2);
    assert(run(&s)==VRW_LAYOUT && !f.writes && f.dbreads==1);
    reset(0x300);f.proof=fatal;p16(f.db+10,0xfff0);p16(f.db+14,1024);
    assert(run(&s)==VRW_LAYOUT && !f.writes && !f.prepare_calls);
    reset(0x300);f.proof=fatal;f.change_id=1;
    assert(run(&s)==VRW_LAYOUT && !f.writes);
    reset(0x300);f.proof=fatal;f.no_copy=1;
    assert(run(&s)==VRW_GATED && !f.reads && !f.dbreads && !f.writes);
    reset(0x300);f.proof=fatal;f.revoke_copy=1;
    assert(run(&s)==VRW_REVOKED && f.dbreads==7 && !f.writes && !f.prepare_calls);
    reset(0x300);f.proof=fatal;f.timeout_copy=1;
    assert(run(&s)==VRW_TIMEOUT && f.dbreads==7 && !f.writes && !f.prepare_calls);
    reset(0x300);f.proof=fatal;p16(f.db+12,1024*24);p16(f.db+14,1024);
    assert(run(&s)==VRW_OK && f.dbreads==1029 && s.db.complete);
    reset(0x300);f.proof=fatal;memcpy(f.db+168,"ldob2\0\0\0",8);
    assert(run(&s)==VRW_LAYOUT && !f.writes && !f.prepare_calls);
    printf("Fatal native policy/COPY parser: %u live DB reads, every read/epoch/revocation boundary PASS\n",dbreads);
}
int main(void)
{
    healthy();guards();failures();fatal_native();
    puts("RPMh actual touch executor/native adapter: two layouts, actual command/trigger/ACK/cleanup, gates, ownership, timeout quarantine PASS");
    return 0;
}
