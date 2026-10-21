/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Independent serialization from the pinned modern HFI wire contract. */
#include "AdrenoHfiStartup.h"
static int zero(const uint32_t *v,size_t n)
{for(size_t i=0;i<n;++i)if(v[i])return 0;return 1;}
static int levels(const AdrenoHfiPerformanceLevel *v,unsigned n,unsigned max)
{
    if(n<2 || n>max || v[0].hz)return 0;
    for(unsigned i=0;i<n;++i)
        if(v[i].hz%1000 || v[i].hz/1000>UINT32_MAX || (i && v[i].hz<=v[i-1].hz))return 0;
    for(unsigned i=n;i<max;++i)if(v[i].hz || v[i].vote)return 0;
    return 1;
}
static int addresses(const uint32_t *v,unsigned n,unsigned max)
{
    if(!n || n>max)return 0;
    for(unsigned i=0;i<n;++i) {
        if(!v[i] || (v[i]&3))return 0;
        for(unsigned j=0;j<i;++j)if(v[i]==v[j])return 0;
    }
    return zero(v+n,max-n);
}
AdrenoHfiStartupStatus AdrenoHfiStartupValidate(const AdrenoHfiStartupConfig *c)
{
    if(!c || !levels(c->gpu,c->gpu_count,16) || !levels(c->gmu,c->gmu_count,4) ||
        !c->bandwidth_count || c->bandwidth_count>16 || c->enable_ifpc>1 ||
        !addresses(c->cnoc_addresses,c->cnoc_count,6) ||
        !addresses(c->ddr_addresses,c->ddr_count,8))return AdrenoHfiStartupInvalid;
    if(c->cnoc_wait_mask>>c->cnoc_count || c->ddr_wait_mask>>c->ddr_count)return AdrenoHfiStartupInvalid;
    for(unsigned i=0;i<c->cnoc_count;++i)
        for(unsigned j=0;j<c->ddr_count;++j)if(c->cnoc_addresses[i]==c->ddr_addresses[j])return AdrenoHfiStartupInvalid;
    for(unsigned i=0;i<2;++i)if(!zero(c->cnoc_data[i]+c->cnoc_count,6-c->cnoc_count))return AdrenoHfiStartupInvalid;
    for(unsigned i=0;i<16;++i)
        if(!zero(c->ddr_data[i]+(i<c->bandwidth_count?c->ddr_count:0),
            8-(i<c->bandwidth_count?c->ddr_count:0)))return AdrenoHfiStartupInvalid;
    if(!c->acd_enable_mask) {
        if(c->acd_version || c->acd_stride || c->acd_count || !zero(c->acd_data,32))return AdrenoHfiStartupInvalid;
    } else {
        if(c->acd_version!=1 || c->acd_stride!=1 || (c->acd_enable_mask&1) ||
            c->acd_enable_mask>>c->gpu_count)return AdrenoHfiStartupInvalid;
        unsigned count=0;
        for(uint32_t bits=c->acd_enable_mask;bits;bits>>=1)count+=bits&1;
        if(c->acd_count!=count || !zero(c->acd_data+count,32-count))return AdrenoHfiStartupInvalid;
    }
    return AdrenoHfiStartupSuccess;
}
AdrenoHfiStartupStatus AdrenoHfiStartupEncode(const AdrenoHfiStartupConfig *c,
    AdrenoHfiStartupStage stage,uint32_t *id,uint32_t *p,size_t capacity,size_t *count)
{
    if(!count)return AdrenoHfiStartupInvalid;
    *count=0;
    if(!id || !p || capacity<159 || AdrenoHfiStartupValidate(c)!=AdrenoHfiStartupSuccess ||
        stage<AdrenoHfiStagePerformance || stage>AdrenoHfiStageStart ||
        ((stage==AdrenoHfiStageAcdEnable || stage==AdrenoHfiStageAcdTable) && !c->acd_enable_mask) ||
        (stage==AdrenoHfiStageIfpc && !c->enable_ifpc))return AdrenoHfiStartupInvalid;
    size_t n=0;unsigned message=0;
    switch(stage) {
    case AdrenoHfiStagePerformance:
        message=4;n=58;
        for(size_t i=0;i<n;++i)p[i]=0;
        p[0]=c->gpu_count;p[1]=c->gmu_count;
        for(unsigned i=0;i<c->gpu_count;++i) {
            p[2+3*i]=c->gpu[i].vote;p[3+3*i]=UINT32_MAX;p[4+3*i]=(uint32_t)(c->gpu[i].hz/1000);
        }
        for(unsigned i=0;i<c->gmu_count;++i) {
            p[50+2*i]=c->gmu[i].vote;p[51+2*i]=(uint32_t)(c->gmu[i].hz/1000);
        }
        break;
    case AdrenoHfiStageBandwidth:
        message=3;n=159;
        p[0]=c->bandwidth_count;p[1]=c->cnoc_count;p[2]=c->ddr_count;
        p[3]=c->cnoc_wait_mask;p[4]=c->ddr_wait_mask;
        for(unsigned i=0;i<6;++i)p[5+i]=c->cnoc_addresses[i];
        for(unsigned i=0;i<12;++i)p[11+i]=c->cnoc_data[i/6][i%6];
        for(unsigned i=0;i<8;++i)p[23+i]=c->ddr_addresses[i];
        for(unsigned i=0;i<128;++i)p[31+i]=c->ddr_data[i/8][i%8];
        break;
    case AdrenoHfiStageAcdEnable:message=11;n=3;p[0]=12;p[1]=1;p[2]=0;break;
    case AdrenoHfiStageAcdTable:
        message=7;n=36;p[0]=c->acd_version;p[1]=c->acd_enable_mask;p[2]=c->acd_stride;p[3]=c->acd_count;
        for(unsigned i=0;i<32;++i)p[4+i]=c->acd_data[i];
        break;
    case AdrenoHfiStageIfpc:message=11;n=3;p[0]=9;p[1]=1;p[2]=0x1680;break;
    case AdrenoHfiStageCoreStart:message=14;n=1;p[0]=0;break;
    case AdrenoHfiStageStart:message=10;n=0;break;
    default:return AdrenoHfiStartupInvalid;
    }
    *id=message;*count=n;return AdrenoHfiStartupSuccess;
}
AdrenoHfiStartupStatus AdrenoHfiStartupValidateServices(const AdrenoHfiStartupServices *s)
{
    if(!s || !s->now_us || !s->wait_us || !s->check_health ||
        !s->polls_per_command || s->polls_per_command>1048576 ||
        !s->wait_interval_us || s->wait_interval_us>1000000 ||
        !s->command_timeout_us || s->command_timeout_us>10000000 ||
        s->total_timeout_us<s->command_timeout_us || s->total_timeout_us>60000000)
        return AdrenoHfiStartupInvalid;
    return AdrenoHfiStartupSuccess;
}
static AdrenoHfiStartupStatus clock_check(const AdrenoHfiStartupServices *s,
    uint64_t begin,uint64_t command,uint64_t *last,AdrenoHfiStartupResult *r)
{
    uint64_t now;
    if(s->now_us(s->cookie,&now) || now<*last)return AdrenoHfiStartupClockError;
    *last=now;r->elapsed_us=now-begin;
    if(now-begin>=s->total_timeout_us || now-command>=s->command_timeout_us)return AdrenoHfiStartupTimeout;
    return AdrenoHfiStartupSuccess;
}
AdrenoHfiStartupStatus AdrenoHfiStartupRun(AdrenoHfiStartup *state,AdrenoHfi *h,
    const AdrenoHfiStartupConfig *c,const AdrenoHfiStartupServices *s,AdrenoHfiStartupResult *r)
{
    if(!r)return AdrenoHfiStartupInvalid;
    *r=(AdrenoHfiStartupResult){0};r->resources_must_retain=1;
    if(!state || !h || AdrenoHfiStartupValidateServices(s)!=AdrenoHfiStartupSuccess ||
        AdrenoHfiStartupValidate(c)!=AdrenoHfiStartupSuccess)return AdrenoHfiStartupInvalid;
    if(state->poisoned || h->poisoned)return AdrenoHfiStartupPoisoned;
    if(state->attempted || state->completed || h->pending_header)return AdrenoHfiStartupBusy;
    uint64_t begin,last,command;
    if(s->now_us(s->cookie,&begin))return AdrenoHfiStartupClockError;
    last=begin;
    if(s->check_health(s->cookie))return AdrenoHfiStartupPlatformFault;
    state->attempted=1;
    AdrenoHfiStartupStatus status=AdrenoHfiStartupSuccess;
    uint32_t payload[159],output[16],id;size_t words,returned;
    for(unsigned step=AdrenoHfiStagePerformance;step<=AdrenoHfiStageStart;++step) {
        if((step==AdrenoHfiStageAcdEnable || step==AdrenoHfiStageAcdTable) && !c->acd_enable_mask)continue;
        if(step==AdrenoHfiStageIfpc && !c->enable_ifpc)continue;
        r->stage=(AdrenoHfiStartupStage)step;command=last;
        status=clock_check(s,begin,command,&last,r);if(status)goto failed;
        if(s->check_health(s->cookie)) {status=AdrenoHfiStartupPlatformFault;goto failed;}
        status=AdrenoHfiStartupEncode(c,r->stage,&id,payload,159,&words);if(status)goto failed;
        r->wire_header=0;
        r->transport_status=AdrenoHfiSend(h,id,payload,words,&r->wire_header);
        if(r->wire_header)++r->published_commands;
        if(r->transport_status!=AdrenoHfiSuccess) {status=AdrenoHfiStartupTransportError;goto failed;}
        unsigned acknowledged=0;
        for(unsigned i=0;i<s->polls_per_command;++i) {
            status=clock_check(s,begin,command,&last,r);if(status)goto failed;
            if(s->check_health(s->cookie)) {status=AdrenoHfiStartupPlatformFault;goto failed;}
            ++r->poll_reads;
            r->transport_status=AdrenoHfiPoll(h,output,16,&returned,&r->firmware_error);
            if(r->transport_status==AdrenoHfiSuccess) {
                status=clock_check(s,begin,command,&last,r);if(status)goto failed;
                if(s->check_health(s->cookie)) {status=AdrenoHfiStartupPlatformFault;goto failed;}
                r->acknowledged_stages|=1u<<step;acknowledged=1;break;
            }
            if(r->transport_status!=AdrenoHfiEmpty) {status=AdrenoHfiStartupTransportError;goto failed;}
            if(i+1<s->polls_per_command && s->wait_us(s->cookie,s->wait_interval_us)) {
                status=AdrenoHfiStartupWaitError;goto failed;
            }
        }
        if(!acknowledged) {status=AdrenoHfiStartupTimeout;goto failed;}
    }
    state->completed=1;r->stage=AdrenoHfiStageComplete;return AdrenoHfiStartupSuccess;
failed:
    state->poisoned=1;h->poisoned=1;return status;
}
