#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_spmi_restart.h"
#include "../xnu/vermeer/vermeer_restart_control.h"
struct mock {
    uint32_t version,features,map[1024],owner[1024],cmd[2],data;
    uint8_t reason,size;
    unsigned reads,writes,reason_writes,pauses,calls,fail_call,status,readback_bad;
    unsigned gate_calls,sync_calls,reset_calls,gate_fail,sync_fail;
    uint64_t now;
    unsigned revoke_in_program,disable_in_program,state_in_program,slow_sync,slow_program,back_in_program,disable_in_sync;
    struct vrc_control *control;
    struct vrs_backend *backend;
};
static int read32(void *ctx,unsigned bank,unsigned off,uint32_t *out)
{
    struct mock *m=ctx;m->reads++;
    if(++m->calls==m->fail_call)return 0;
    if(bank==VRS_CORE) {
        if(off==0)*out=m->version;
        else if(off==4)*out=m->features;
        else {assert(off>=0x2000 && off<0x3000);*out=m->map[(off-0x2000)/4];}
    } else if(bank==VRS_CONFIG) {assert(off<0x1000);*out=m->owner[off/4];}
    else {
        unsigned reg=off-(bank==VRS_CHANNEL?m->backend->apid*0x1000:m->backend->apid*0x20);
        if(reg==8)*out=m->status;
        else {
            assert(bank==VRS_OBSERVER && reg==0x18);
            unsigned address=(m->cmd[0]>>4)&255;
            assert(address==0x44 || address==0x48);
            *out=address==0x44?m->size:(m->reason^(m->reason_writes && m->readback_bad?2:0));
        }
    }
    return 1;
}
static int write32(void *ctx,unsigned bank,unsigned off,uint32_t value)
{
    struct mock *m=ctx;m->writes++;
    if(++m->calls==m->fail_call)return 0;
    assert(bank==VRS_OBSERVER || bank==VRS_CHANNEL);
    unsigned reg=off-(bank==VRS_CHANNEL?m->backend->apid*0x1000:m->backend->apid*0x20);
    if(bank==VRS_CHANNEL && reg==0x10) {assert(value<=255);m->data=value;return 1;}
    assert(reg==0);
    if(bank==VRS_OBSERVER) {assert(value==(1u<<27|0x440u) || value==(1u<<27|0x480u));m->cmd[0]=value;}
    else {assert(value==0x480);m->reason=(uint8_t)m->data;m->reason_writes++;m->cmd[1]=value;}
    return 1;
}
static int pause_us(void *ctx){((struct mock *)ctx)->pauses++;return 1;}
static void make(struct mock *m,struct vrs_backend *b)
{
    *m=(struct mock){.version=0x70000000,.features=8,.size=1,.status=1,.backend=b,.now=130};
    m->map[3]=0x7100;
    *b=(struct vrs_backend){.ctx=m,.read32=read32,.write32=write32,.pause_us=pause_us};
    memcpy(b->regions,vrs_k70_regions,sizeof(b->regions));
}
static uint64_t now_ms(void *ctx){return ((struct mock *)ctx)->now;}
static int gate(void *ctx){struct mock *m=ctx;return m->revoke_in_program==2 || ++m->gate_calls==m->gate_fail?0:1;}
static int sync_root(void *ctx){struct mock *m=ctx;m->sync_calls++;
    if(m->slow_sync)m->now=2000;
    if(m->disable_in_sync)m->control->enabled=0;
    return !m->sync_fail;}
static int program(void *ctx){struct mock *m=ctx;int result=vrs_program_bootloader(m->backend)==1;
    if(m->slow_program)m->now=2000;
    if(m->back_in_program)m->now=100;
    if(m->revoke_in_program)m->revoke_in_program=2;
    if(m->disable_in_program)m->control->enabled=0;
    if(m->state_in_program)m->control->state=VRC_IDLE;
    return result;}
static int reset(void *ctx){((struct mock *)ctx)->reset_calls++;return -99;}
static struct vrc_ops ops(struct mock *m)
{return (struct vrc_ops){m,now_ms,gate,sync_root,program,reset};}
static struct vrc_request terminal(void)
{
    struct vrc_request r={{0},7,0xaabbccdd};r.token[0]=1;return r;
}
static void queue(struct vrc_control *s)
{
    struct vrc_request r=terminal();*s=(struct vrc_control){.enabled=1};
    assert(vrc_arm(s,&r,100,1000) && vrc_queue(s,&r,120));
}
int main(void)
{
    struct mock m;struct vrs_backend b;struct vrc_control s;struct vrc_request r=terminal();
    make(&m,&b);assert(vrs_program_bootloader(&b)==VRS_DISABLED && !m.calls);
    uint32_t value=0;assert(vrs_read(&b,VRS_CORE,0,&value)==VRS_DISABLED && !m.calls);
    for(unsigned raw=0;raw<256;raw++) {
        make(&m,&b);b.enabled=1;m.reason=(uint8_t)raw;
        assert(vrs_program_bootloader(&b)==1 && m.reason==(uint8_t)((raw&1)|4));
        assert(b.old_byte==raw && b.new_byte==m.reason && m.reason_writes==1);
        unsigned calls=m.calls;assert(vrs_program_bootloader(&b)==VRS_RETIRED && m.calls==calls);
    }
    /* Alternate mappings for another EE are permitted; multiple APSS-owned
     * mappings are refused, never pick an arbitrary channel. */
    make(&m,&b);b.enabled=1;m.map[5]=0x7100;m.owner[5]=2;assert(vrs_probe(&b)==1 && b.apid==3);
    for(unsigned apid=0;apid<1024;apid++) {
        make(&m,&b);b.enabled=1;m.features=1024;m.map[3]=0;m.map[apid]=0x7100;
        assert(vrs_program_bootloader(&b)==1 && b.apid==apid && m.reason_writes==1);
    }
    make(&m,&b);b.enabled=1;
    assert(vrs_write(&b,VRS_CORE,0,0)==VRS_IO && !m.calls);
    make(&m,&b);b.enabled=1;
    assert(vrs_write(&b,VRS_CHANNEL,0x10,0x02)==VRS_IO && !m.calls);
    for(unsigned fault=0;fault<8;fault++) {
        make(&m,&b);b.enabled=1;
        if(fault==0)m.version=0x50000000;
        if(fault==1)m.version=0x80000000;
        if(fault==2)m.features=1025;
        if(fault==3)m.features=0;
        if(fault==4)m.owner[3]=1;
        if(fault==5)m.map[3]=0;
        if(fault==6)m.map[5]=0x7100;
        if(fault==7)b.regions[VRS_CORE].base=0xdead0000;
        queue(&s);struct vrc_ops o=ops(&m);
        assert(!vrc_worker(&s,&o) && s.state==VRC_FAILED && !m.writes && !m.reset_calls);
    }
    make(&m,&b);b.enabled=1;assert(vrs_program_bootloader(&b)==1);unsigned count=m.calls;
    for(unsigned fail=1;fail<=count;fail++) {
        make(&m,&b);b.enabled=1;m.fail_call=fail;queue(&s);struct vrc_ops o=ops(&m);
        assert(!vrc_worker(&s,&o) && s.state==VRC_FAILED && !m.reset_calls);
        unsigned calls=m.calls;assert(vrs_program_bootloader(&b)==VRS_RETIRED && m.calls==calls);
    }
    for(unsigned fault=0;fault<7;fault++) {
        make(&m,&b);b.enabled=1;
        if(fault==0)m.status=0;
        if(fault==1)m.status=3;
        if(fault==2)m.status=5;
        if(fault==3)m.status=9;
        if(fault==4)m.readback_bad=1;
        if(fault==5)m.gate_fail=2;
        if(fault==6)m.sync_fail=1;
        queue(&s);struct vrc_ops o=ops(&m);
        assert(!vrc_worker(&s,&o) && s.state==VRC_FAILED && !m.reset_calls && m.pauses<=1000);
        if(fault==0)assert(m.pauses==1000 && !m.reason_writes);
        if(fault==4)assert(m.reason_writes==1); /* changed but no reset; no retry */
    }
    memset(&s,0,sizeof(s));assert(!vrc_arm(&s,&r,100,1000));
    s.enabled=1;assert(!vrc_arm(&s,&r,UINT64_MAX,100));
    for(unsigned bad=0;bad<6;bad++) {
        s=(struct vrc_control){.enabled=1};assert(vrc_arm(&s,&r,100,1000));
        struct vrc_request ack=r;uint64_t now=120;
        if(bad==0)ack.token[0]^=2;
        if(bad==1)ack.sequence++;
        if(bad==2)ack.crc++;
        if(bad==3)now=99;
        if(bad==4)now=1100;
        if(bad==5)now=UINT64_MAX;
        assert(!vrc_queue(&s,&ack,now) && s.state==VRC_FAILED);
    }
    for(unsigned stale=0;stale<2;stale++) {
        make(&m,&b);b.enabled=1;queue(&s);struct vrc_ops o=ops(&m);
        m.now=stale?1100:119;assert(!vrc_worker(&s,&o) && !m.calls && !m.reset_calls);
    }
    make(&m,&b);b.enabled=1;queue(&s);struct vrc_ops o=ops(&m);
    assert(!vrc_worker(&s,&o) && s.state==VRC_RESET_RETURNED && m.reset_calls==1);
    assert(!vrc_worker(&s,&o) && m.reset_calls==1);
    for(unsigned fault=0;fault<7;fault++) {
        make(&m,&b);b.enabled=1;queue(&s);m.control=&s;o=ops(&m);
        if(fault==0)m.revoke_in_program=1;
        if(fault==1)m.disable_in_program=1;
        if(fault==2)m.state_in_program=1;
        if(fault==3)m.slow_sync=1;
        if(fault==4)m.slow_program=1;
        if(fault==5)m.back_in_program=1;
        if(fault==6)m.disable_in_sync=1;
        assert(!vrc_worker(&s,&o) && s.state==VRC_FAILED && !m.reset_calls);
        if(fault==3 || fault==6)assert(!m.reason_writes);
        unsigned calls=m.calls;assert(!vrc_worker(&s,&o) && m.calls==calls && !m.reset_calls);
    }
    char text[64]="rb 01000000000000000000000000000000 0000000000000007 aabbccdd";
    struct vrc_request parsed;assert(strlen(text)==61 && vrc_parse(text,61,&parsed));
    assert(!memcmp(parsed.token,r.token,16) && parsed.sequence==r.sequence && parsed.crc==r.crc);
    for(unsigned i=0;i<61;i++) {
        char old=text[i];text[i]='!';assert(!vrc_parse(text,61,&parsed));text[i]=old;
        assert(!vrc_parse(text,i,&parsed));
    }
    uint8_t probe[16];
    assert(vrc_probe_token("rp 01000000000000000000000000000000",35,probe));
    assert(!memcmp(probe,r.token,16));
    assert(!vrc_probe_token("rp 00000000000000000000000000000000",35,probe));
    assert(!vrc_probe_token("rp 01000000000000000000000000000000 ",36,probe));
    assert(!vrc_probe_token("rp 01!00000000000000000000000000000",35,probe));
    puts("Restart candidate: default-off, v7/EE0 ownership, all256 bit-preservation inputs, every IO failure, timeout/readback, ACK and one-shot reset policy PASS");
}
