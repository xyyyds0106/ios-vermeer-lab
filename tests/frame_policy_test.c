#define VERMEER_HOST_TEST 1
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "../xnu/vermeer/vermeer_frame_policy.h"
struct surface { unsigned refs; };
static void release(struct vframe_refs *r)
{
    for(unsigned i=0;i<VFRAME_LAYERS;i++) if(r->slot[i]) {
        struct surface *s=r->slot[i];assert(s->refs);--s->refs;r->slot[i]=0;
    }
}
int main(void)
{
    struct surface a={1},b={1};
    struct vframe_refs pending={0},incoming={0},old={0},presenting={0};
    incoming.slot[0]=&a;++a.refs;vframe_replace(&pending,&incoming,&old);release(&old);
    --a.refs;assert(a.refs==1); /* User drops the surface before the slow panel copy. */
    vframe_take(&pending,&presenting);assert(!pending.slot[0] && a.refs==1);
    incoming.slot[0]=&b;++b.refs;vframe_replace(&pending,&incoming,&old);release(&old);
    assert(presenting.slot[0]==&a && a.refs==1); /* New submit cannot invalidate in-flight copy. */
    release(&presenting);assert(!a.refs);
    /* Dropping a pending frame releases it; a failed surface lookup is NULL. */
    vframe_replace(&pending,&incoming,&old);release(&old);assert(b.refs==1);
    vframe_take(&pending,&presenting);release(&presenting);assert(b.refs==1);
    struct vframe_copy c;
#define CLIP(sx,sy,dx,dy,w,h) vframe_clip(&c,sx,sy,dx,dy,w,h,1125,2436,4608,0,11239424,1440,3200,5760,18432000,157,382)
    assert(CLIP(0,0,0,0,1125,2436));
    assert(c.src==0 && c.dst==382*5760ULL+157*4 && c.bytes==4500 && c.rows==2436);
    assert(CLIP(0,0,-200,0,1125,2436));assert(c.src==43*4 && c.dst==382*5760ULL && c.bytes==(1125-43)*4);
    assert(CLIP(-20,-10,0,0,100,100));assert(c.src==0 && c.dst==392*5760ULL+177*4 && c.bytes==80*4 && c.rows==90);
    assert(!CLIP(0,0,INT_MIN,INT_MIN,INT_MAX,INT_MAX));
    assert(!CLIP(INT_MAX,0,0,0,1,1));assert(!CLIP(0,0,0,0,0,1));
    assert(!vframe_clip(&c,0,0,0,0,1,1,1,1,4,UINT64_MAX,4,1,1,4,4,0,0));
    assert(!vframe_clip(&c,0,0,0,0,1,1,2,1,4,0,4,1,1,4,4,0,0));
    assert(!vframe_clip(&c,0,0,0,0,1,2,1,2,4,0,7,1,2,4,8,0,0));
    uint32_t rng=71;
    for(unsigned i=0;i<200000;i++) {
        int32_t p[6];
        for(unsigned j=0;j<6;j++){rng=rng*1664525u+1013904223u;p[j]=(int32_t)(rng%14000)-7000;}
        if(CLIP(p[0],p[1],p[2],p[3],p[4],p[5])) {
            assert(c.bytes && c.bytes<=4500 && c.rows && c.rows<=2436);
            assert(c.src+(c.rows-1)*4608ULL+c.bytes<=11239424);
            assert(c.dst+(c.rows-1)*5760ULL+c.bytes<=18432000);
        }
    }
    puts("Pending/in-flight surface lifetime, clipped scroll geometry and copy bounds: PASS");
    return 0;
}
