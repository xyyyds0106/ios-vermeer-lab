/* Real ring/fence implementation, with software MMIO/cache boundary peers. */
#include "NativeAdrenoRing.h"
#include <cassert>
#include <cstdio>
#include <cstring>
static uint32_t words[8192],fences[1024];
static uint64_t ticks;static unsigned acquired,mode,writes,fail_write;
static int wr(void *,uint32_t,uint32_t){++writes;return writes==fail_write;}
static int rd(void *,uint32_t,uint32_t *v){*v=mode==7?17:0;return 0;}
static int barrier(void *){return 0;}
static int publish(void *,const volatile uint32_t *,size_t){return mode==6;}
static int acquire(void *,volatile uint32_t *,size_t){++acquired;
    if(mode==1){ticks=100;fences[1]=1;} /* Slow acquire cannot create late success. */
    if(mode==2)ticks=0; /* Backwards clock after the callback. */
    return mode==5;}
static uint64_t now(void *){return ticks;}
static int idle(void *){if(mode!=3)++ticks;return 0;}
int main(){
    NativeAdrenoRingIo io{nullptr,wr,rd,barrier};NativeAdrenoRingCache cache{nullptr,publish,acquire};
    NativeAdrenoRingClock clock{nullptr,now,idle};
    for(mode=0;mode<=7;++mode){
        ticks=mode==2?10:0;acquired=writes=fail_write=0;memset(words,0,sizeof words);memset(fences,0,sizeof fences);
        NativeAdrenoRing r;IOReturn attached=r.attach(&io,&cache,words,sizeof words,0x200000,fences,sizeof fences,0x300000);
        if(mode==7){assert(attached && r.poisoned() && !acquired);continue;}
        assert(!attached);
        uint32_t packet[]={0x703d8003,0x308000,0,1},fence=0;
        IOReturn s=r.submit(packet,4,&fence);
        if(mode==6){assert(s && !fence && r.poisoned());continue;}
        assert(!s && fence==1 && r.outstanding()==1);
        if(mode==0)fences[1]=1;
        if(mode==4)fences[1]=2;
        NativeAdrenoRingFence result=r.wait(fence,&clock,100,nullptr);
        if(mode==0)assert(result==NativeAdrenoRingFenceComplete && !r.outstanding());
        else if(mode==2 || mode==4 || mode==5)assert(result==NativeAdrenoRingFencePoisoned && r.outstanding()==1);
        else assert(result==NativeAdrenoRingFenceTimeout && r.outstanding()==1);
        if(mode==3)assert(acquired==1048576); /* Frozen time remains finite. */
        if(r.poisoned()){unsigned old=acquired;assert(r.wait(fence,&clock,100,nullptr)==NativeAdrenoRingFencePoisoned && acquired==old);}
    }
    puts("PASS: production ring real/future/stale fences, cold RPTR, late acquire, backwards/frozen clock, cache/commit failures and no retirement on uncertainty; software peer only");
}
