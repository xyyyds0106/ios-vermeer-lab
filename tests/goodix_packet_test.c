#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_goodix_packet.h"
#include "../xnu/vermeer/vermeer_goodix_info.h"
static unsigned emitted,ups,downs;
static void emit(void *ctx,unsigned id,int down,unsigned x,unsigned y)
{
    (void)ctx;assert(id<10 && x<1440 && y<3200);emitted++;if(down)downs++;else ups++;
}
static void checksum(uint8_t *p,unsigned n)
{
    unsigned sum=0;for(unsigned i=0;i<n-2;i++)sum+=p[i];p[n-2]=(uint8_t)sum;p[n-1]=(uint8_t)(sum>>8);
}
static void seal(uint8_t *p)
{
    checksum(p,8);if(p[2]&15)checksum(p+8,(p[2]&15)*8+2);
}
static void point(uint8_t *p,unsigned i,unsigned id,unsigned x,unsigned y)
{
    uint8_t *r=p+8+8*i;r[0]=(uint8_t)(id<<4);r[2]=(uint8_t)x;r[3]=(uint8_t)(x>>8);r[4]=(uint8_t)y;r[5]=(uint8_t)(y>>8);
}
int main(void)
{
    /* Two variable scan tables and independently supplied native axes. */
    uint8_t info[187]={0};info[0]=187;
    info[32]=2;info[37]=1;
    unsigned misc=43;
    info[misc+44]=0x40;info[misc+46]=1;
    info[misc+48]=8;info[misc+50]=8;
    info[misc+138]=0x38;info[misc+139]=4;
    info[misc+140]=0x60;info[misc+141]=9;
    checksum(info,sizeof(info));struct vt_info ic={0},prior;
    assert(vt_info_decode(info,sizeof(info),&ic));
    assert(ic.event==0x10040 && ic.max_x==1080 && ic.max_y==2400);prior=ic;
    for(unsigned n=0;n<sizeof(info);n++)assert(!vt_info_decode(info,n,&ic));
    assert(!memcmp(&ic,&prior,sizeof(ic)));
    info[32]=6;checksum(info,sizeof(info));assert(!vt_info_decode(info,sizeof(info),&ic));
    info[32]=2;info[misc+48]=9;checksum(info,sizeof(info));assert(!vt_info_decode(info,sizeof(info),&ic));
    uint8_t version[28]={0};memcpy(version+10,"9916",4);checksum(version,28);
    assert(vt_version_9916(version,28));version[13]='5';checksum(version,28);assert(!vt_version_9916(version,28));
    uint8_t p[98]={0x80,0,2};point(p,0,1,1439,3199);point(p,1,8,0,0);seal(p);
    struct vt_frame output={0},state={0},saved;
    assert(vt_decode(p,26,1439,3199,&output)==VT_TOUCH);
    assert(output.point[1].x==1439 && output.point[1].y==3199 && output.point[8].x==0);
    vt_apply(&state,&output,emit,0);assert(downs==2 && !ups);
    vt_apply(&state,&output,emit,0);assert(emitted==2);
    saved=output;
    for(unsigned size=0;size<26;size++)assert(vt_decode(p,size,1439,3199,&output)==VT_BAD);
    assert(!memcmp(&saved,&output,sizeof(saved)));
    p[8+8]=0x10;seal(p);assert(vt_decode(p,26,1439,3199,&output)==VT_BAD); /* duplicateID */
    p[8+8]=0xf0;seal(p);assert(vt_decode(p,26,1439,3199,&output)==VT_BAD);
    point(p,1,8,65535,0);seal(p);assert(vt_decode(p,26,1439,3199,&output)==VT_BAD);
    point(p,1,8,0,0);seal(p);p[8]^=1;assert(vt_decode(p,26,1439,3199,&output)==VT_BAD);
    assert(!memcmp(&saved,&output,sizeof(saved))); /* no partial frame escapes */
    p[8]^=1;seal(p);p[8]|=3;seal(p);assert(vt_decode(p,26,1439,3199,&output)==VT_BAD);
    p[2]=0;seal(p);assert(vt_decode(p,8,1439,3199,&output)==VT_TOUCH);
    vt_apply(&state,&output,emit,0);assert(ups==2);vt_release_all(&state,emit,0);assert(ups==2);
    p[2]=1;point(p,0,0,32767,65535);seal(p);
    assert(vt_decode(p,18,32767,65535,&output)==VT_TOUCH);
    assert(output.point[0].x==1439 && output.point[0].y==3199);
    vt_apply(&state,&output,emit,0);vt_release_all(&state,emit,0);assert(downs==3 && ups==3);
    assert(vt_decode(p,18,0,65535,&output)==VT_BAD);assert(vt_decode(p,18,65536,65535,&output)==VT_BAD);
    uint32_t rng=51;
    uint8_t random_info[1024];
    for(unsigned i=0;i<50000;i++) {
        for(unsigned j=0;j<sizeof(random_info);j++){rng=rng*1664525u+1013904223u;random_info[j]=(uint8_t)(rng>>24);}
        unsigned n=181+rng%(1024-181);
        random_info[0]=(uint8_t)n;random_info[1]=(uint8_t)(n>>8);checksum(random_info,n);
        if(vt_info_decode(random_info,n,&ic))assert(ic.max_x && ic.max_y && ic.event>=0x10000 && ic.event<=0x1fffff-90);
    }
    for(unsigned i=0;i<100000;i++) {
        for(unsigned j=0;j<98;j++){rng=rng*1664525u+1013904223u;p[j]=(uint8_t)(rng>>24);}
        unsigned n=rng%99;
        int result=vt_decode(p,n,65535,65535,&output);
        if(result==VT_TOUCH)for(unsigned j=0;j<10;j++)assert(output.point[j].x<1440 && output.point[j].y<3200);
    }
    puts("Goodix9916 checksums, complete frames, resolution bounds, multitouch releases and malformed packets: PASS");
    return 0;
}
