/* Production clock writer over a software register peer; no device access. */
#include "../../gpu/K70/K70GpuClock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct peer {uint32_t gcc[0x72000/4],cc[0xa000/4];unsigned calls,fail,after,mode;uint64_t now;};
static struct peer p;
static int event(void){if(p.fail && ++p.calls==p.fail)return 1;if(p.fail && p.calls>p.fail)++p.after;return 0;}
static int lease(void *ctx){assert(ctx==&p);return p.mode==5;}
static int now(void *ctx,uint64_t *v){assert(ctx==&p);*v=p.now;return 0;}
static int wait(void *ctx,unsigned us){assert(ctx==&p);if(p.mode!=6)p.now+=us;return 0;}
static int read(void *ctx,unsigned w,size_t at,uint32_t *v)
{assert(ctx==&p && w<2 && !(at&3) && at<(w?sizeof p.cc:sizeof p.gcc));if(event())return 1;*v=(w?p.cc:p.gcc)[at/4];return 0;}
static int write(void *ctx,unsigned w,size_t at,uint32_t v)
{
    assert(ctx==&p && w<2 && !(at&3) && at<(w?sizeof p.cc:sizeof p.gcc));if(event())return 1;
    if(w && at==0x1004 && v==1 && p.mode!=1)p.cc[0x1000/4]|=1u<<31;
    if(w && (at==0x9318 || at==0x93ec) && p.mode!=2)v&=~1u;
    (w?p.cc:p.gcc)[at/4]=v;return 0;
}
static int barrier(void *ctx){assert(ctx==&p);return event();}
static struct K70ClockIo io={&p,lease,now,wait,read,write,barrier};
static void reset(unsigned mode){memset(&p,0,sizeof p);p.mode=mode;p.cc[0x953c/4]=1u<<31;if(mode==3)p.cc[0x953c/4]=0;}
int main(void)
{
    reset(0);struct K70ClockState s={0};assert(!K70GpuClockStart(&s,&io)&&s.ready&&s.phase==6);
    assert((p.cc[0x931c/4]&0x10371f)==0x303 && (p.cc[0x93f0/4]&0x10371f)==0x505);
    reset(0);s=(struct K70ClockState){0};p.fail=~0u;assert(!K70GpuClockStart(&s,&io));unsigned calls=p.calls;
    for(unsigned fail=1;fail<=calls;++fail){reset(0);s=(struct K70ClockState){0};p.fail=fail;
        assert(K70GpuClockStart(&s,&io)&&s.poisoned&&!s.ready&&!p.after);
        unsigned before=p.calls;assert(K70GpuClockStart(&s,&io)&&p.calls==before);
    }
    for(unsigned mode=1;mode<=5;++mode){reset(mode);s=(struct K70ClockState){0};
        if(mode==4){p.cc[0x1000/4]=0x80000001;p.cc[0x1004/4]=1;p.cc[0x1010/4]=0x99;}
        assert(K70GpuClockStart(&s,&io)&&!s.ready&&s.poisoned);
    }
    reset(6);p.cc[0x953c/4]=0;s=(struct K70ClockState){0};assert(K70GpuClockStart(&s,&io)&&!s.ready&&s.poisoned);
    printf("PASS: real clock writer sequence, %u I/O fault cutoffs, replay, CX/PLL/RCG timeouts, wrong live rate, revoked lease and frozen time; software peer only\n",calls);
}
