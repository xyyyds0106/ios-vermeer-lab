/* Startup control-flow regression against production serialization/control.
 * The HFI queue boundary is injected: this never emulates GPU execution. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../gpu/AdrenoFirmware/AdrenoHfiStartup.h"
enum {NORMAL,SLOW_HEALTH,SLOW_FINAL_HEALTH,SLOW_SEND,REVOKE_SEND,
    BACKWARD_HEALTH,CHANGE_PHASE,WAIT_EXPIRES,WAIT_FAIL,HEALTH_FAIL};
struct fixture {
    uint64_t now;unsigned mode,health,sends,polls,waits,fired;
    AdrenoHfiStartup *state;
};
static int now_us(void *ctx,uint64_t *out)
{*out=((struct fixture *)ctx)->now;return 0;}
static int health(void *ctx)
{
    struct fixture *f=ctx;++f->health;
    if(f->mode==SLOW_HEALTH && f->health==2)f->now+=100;
    if(f->mode==SLOW_FINAL_HEALTH && f->polls==4 && !f->fired++)f->now+=1000;
    if(f->mode==BACKWARD_HEALTH && f->health==2)f->now--;
    if(f->mode==CHANGE_PHASE && f->health==2)f->state->completed=1;
    return f->mode==HEALTH_FAIL || (f->mode==REVOKE_SEND && f->sends);
}
static int wait_us(void *ctx,unsigned interval)
{
    struct fixture *f=ctx;assert(interval==1);++f->waits;
    f->now+=f->mode==WAIT_EXPIRES?100:1;
    return f->mode==WAIT_FAIL;
}
AdrenoHfiStatus AdrenoHfiSend(AdrenoHfi *h,unsigned id,const uint32_t *p,size_t n,uint32_t *wire)
{
    struct fixture *f=h->cookie;(void)p;(void)n;
    static const unsigned ids[]={4,3,14,10};
    assert(f->sends<4 && id==ids[f->sends]);++f->sends;
    h->pending_header=*wire=(f->sends<<20)|id;
    if(f->mode==SLOW_SEND)f->now+=100;
    return AdrenoHfiSuccess;
}
AdrenoHfiStatus AdrenoHfiPoll(AdrenoHfi *h,uint32_t *out,size_t cap,size_t *n,uint32_t *error)
{
    struct fixture *f=h->cookie;(void)out;assert(cap==16);++f->polls;*n=0;*error=0;
    if(f->mode==WAIT_EXPIRES || f->mode==WAIT_FAIL)return AdrenoHfiEmpty;
    h->pending_header=0;return AdrenoHfiSuccess;
}
static AdrenoHfiStartupConfig config(void)
{
    AdrenoHfiStartupConfig c={0};c.gpu_count=c.gmu_count=2;
    c.gpu[1].hz=c.gmu[1].hz=1000;
    c.bandwidth_count=c.cnoc_count=c.ddr_count=1;
    c.cnoc_addresses[0]=0x100;c.ddr_addresses[0]=0x200;return c;
}
static void run(unsigned mode,int diagnostic)
{
    AdrenoHfiStartup state={0};struct fixture f={.now=1000,.mode=mode,.state=&state};
    AdrenoHfi h={.cookie=&f};AdrenoHfiStartupResult r;
    AdrenoHfiStartupServices s={&f,now_us,wait_us,health,4,1,100,1000};
    AdrenoHfiStartupConfig c=config();
    AdrenoHfiStartupStatus status=AdrenoHfiStartupRun(&state,&h,&c,&s,&r);
    if(diagnostic) {
        printf("mode=%u status=%u sends=%u polls=%u completed=%u elapsed=%llu\n",
            mode,status,f.sends,f.polls,state.completed,(unsigned long long)r.elapsed_us);return;
    }
    assert(r.resources_must_retain);
    if(mode==NORMAL) {
        assert(status==AdrenoHfiStartupSuccess && state.completed && !state.poisoned);
        assert(f.sends==4 && f.polls==4 && r.stage==AdrenoHfiStageComplete);return;
    }
    assert(status!=AdrenoHfiStartupSuccess && state.poisoned && h.poisoned);
    assert(r.stage!=AdrenoHfiStageComplete);
    if(mode==SLOW_HEALTH || mode==BACKWARD_HEALTH || mode==CHANGE_PHASE || mode==HEALTH_FAIL)
        assert(!f.sends && !f.polls);
    if(mode==SLOW_SEND || mode==REVOKE_SEND)assert(f.sends==1 && !f.polls);
    if(mode==SLOW_FINAL_HEALTH)assert(f.sends==4 && f.polls==4 && !state.completed);
    if(mode==WAIT_EXPIRES || mode==WAIT_FAIL)assert(f.sends==1 && f.polls==1 && f.waits==1);
    if(mode==SLOW_HEALTH || mode==SLOW_SEND || mode==SLOW_FINAL_HEALTH || mode==WAIT_EXPIRES)
        assert(status==AdrenoHfiStartupTimeout);
    if(mode==BACKWARD_HEALTH)assert(status==AdrenoHfiStartupClockError);
    if(mode==REVOKE_SEND || mode==CHANGE_PHASE || mode==HEALTH_FAIL)
        assert(status==AdrenoHfiStartupPlatformFault);
    if(mode==WAIT_FAIL)assert(status==AdrenoHfiStartupWaitError);
    unsigned sent=f.sends,polls=f.polls;
    assert(AdrenoHfiStartupRun(&state,&h,&c,&s,&r)==AdrenoHfiStartupPoisoned);
    assert(f.sends==sent && f.polls==polls);
}
int main(int argc,char **argv)
{
    (void)argv;
    for(unsigned mode=0;mode<=HEALTH_FAIL;mode++)run(mode,argc>1);
    if(argc==1)puts("HFI startup: callback deadlines, monotonic time, health revocation, phase changes and no retry PASS");
}
