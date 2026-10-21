#define VERMEER_HOST_TEST 1
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../xnu/vermeer/vermeer_baseband_observer.h"
#include "../xnu/vermeer/vermeer_log_ring.h"
static uint8_t run[16]={1},image[32]={2};
static unsigned published;
static int busy,emit;
static int publish(void *ctx,const char *line,size_t n)
{
    (void)ctx;assert(n==264 && line[0]=='\n' && line[263]=='\n');
    assert(!memcmp(line+1,"VBDR1 ",6));
    if(busy)return busy<0?-1:0;
    published++;if(emit){assert(fwrite(line,1,n,stdout)==n);fflush(stdout);}return 1;
}
static void start(struct vbb_observer *s)
{
    *s=(struct vbb_observer){0};published=0;busy=0;
    assert(vbb_observer_start(s,run,image,100,100));
}
static void ack_text(const struct vbb_observer *s,char line[62])
{
    char token[33];for(unsigned i=0;i<16;i++)snprintf(token+i*2,3,"%02x",run[i]);
    assert(snprintf(line,62,"ba %s %016llx %08x",token,
        (unsigned long long)s->record.sequence,vbb_u32(s->frame+124))==61);
}
static void ack(struct vbb_observer *s,uint64_t now)
{
    char line[62];ack_text(s,line);assert(vbb_observer_ack_line(s,line,61,now));
    assert(!vbb_observer_ack_line(s,line,61,now)); /* duplicate cannot advance */
}
static void before_first(struct vbb_observer *s)
{
    start(s);vbb_observer_poll(s,100,publish,0);ack(s,100);
    vbb_observer_poll(s,100,publish,0);
    assert(s->record.state==VBB_REC_BEFORE_READ && !s->synthetic_steps);
}
static void states(void)
{
    struct vbb_observer s;start(&s);
    assert(!vbb_observer_ack(&s,run,1,vbb_u32(s.frame+124),100));
    while(s.state!=VBB_OBS_DONE) {
        vbb_observer_poll(&s,100,publish,0);
        assert(s.state==VBB_OBS_WAIT_ACK);
        struct vbb_record decoded;
        assert(!vbb_record_decode(s.frame,128,&decoded)); /* no real evidence */
        ack(&s,100);
    }
    assert(s.synthetic_steps==5 && published==12 && !s.error);
    vbb_observer_poll(&s,1000,publish,0);assert(published==12);
    assert(!vbb_observer_start(&s,run,image,1000,100));
    before_first(&s);uint8_t wrong[16];memcpy(wrong,run,16);wrong[3]^=1;
    assert(!vbb_observer_ack(&s,wrong,2,vbb_u32(s.frame+124),100));
    assert(!vbb_observer_ack(&s,run,1,vbb_u32(s.frame+124),100));
    assert(!vbb_observer_ack(&s,run,3,vbb_u32(s.frame+124),100));
    assert(!vbb_observer_ack(&s,run,2,vbb_u32(s.frame+124)^1,100));
    assert(!s.synthetic_steps && s.state==VBB_OBS_WAIT_ACK);
    vbb_observer_poll(&s,200,publish,0);
    assert(s.record.state==VBB_REC_REFUSED && s.record.phase==1 && s.record.sequence==3);
    assert(s.state==VBB_OBS_WAIT_ACK && !s.synthetic_steps);ack(&s,200);
    assert(s.state==VBB_OBS_STOPPED && s.error==VBB_OBS_TIMEOUT && published==3);
    before_first(&s);assert(!vbb_observer_ack(&s,run,2,vbb_u32(s.frame+124),200));
    assert(!s.synthetic_steps && s.record.state==VBB_REC_REFUSED);
    vbb_observer_poll(&s,200,publish,0);vbb_observer_poll(&s,300,publish,0);
    assert(s.state==VBB_OBS_STOPPED && published==3);
    start(&s);busy=1;vbb_observer_poll(&s,100,publish,0);
    assert(!published && s.state==VBB_OBS_PUBLISH);
    vbb_observer_poll(&s,200,publish,0);assert(s.state==VBB_OBS_STOPPED && !published);
    before_first(&s);ack(&s,100);assert(s.synthetic_steps==1);
    busy=1;vbb_observer_poll(&s,100,publish,0);vbb_observer_poll(&s,200,publish,0);
    assert(s.state==VBB_OBS_STOPPED && s.record.state==VBB_REC_READ_OK && published==2);
    start(&s);busy=-1;vbb_observer_poll(&s,100,publish,0);
    assert(s.state==VBB_OBS_STOPPED && s.error==VBB_OBS_PUBLISH_ERROR);
    start(&s);vbb_observer_poll(&s,99,publish,0);assert(s.error==VBB_OBS_CLOCK && !published);
    before_first(&s);vbb_observer_disconnect(&s);ack_text(&s,(char[62]){0});
    vbb_observer_poll(&s,100,publish,0);
    assert(s.state==VBB_OBS_STOPPED && s.error==VBB_OBS_DISCONNECTED && !s.synthetic_steps);
    s=(struct vbb_observer){0};assert(!vbb_observer_start(&s,run,image,0,0));
    assert(!vbb_observer_start(&s,run,image,0,60001));
    assert(!vbb_observer_start(&s,run,image,UINT64_MAX-50,100));
    assert(!vbb_observer_start(&s,(uint8_t[16]){0},image,0,100));
    before_first(&s);char line[62];ack_text(&s,line);
    for(unsigned n=0;n<61;n++)assert(!vbb_observer_ack_line(&s,line,n,100));
    assert(!vbb_observer_ack_line(&s,line,62,100));
    uint32_t seed=19;
    for(unsigned i=0;i<100000;i++) {
        char bad[61];memcpy(bad,line,61);seed=seed*1664525u+1013904223u;
        unsigned pos=seed%61;seed=seed*1664525u+1013904223u;bad[pos]^=(char)(1+seed%127);
        assert(!vbb_observer_ack_line(&s,bad,61,100));
        assert(!s.synthetic_steps && s.record.state==VBB_REC_BEFORE_READ);
    }
}
static struct vl_ring ring;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned appends;
static int try_lock(void *ctx){(void)ctx;return pthread_mutex_trylock(&lock)==0;}
static void append_locked(void *ctx,const char *p,size_t n)
{ (void)ctx;assert(n==264);appends++;vl_append(&ring,p,(unsigned)n); }
static void unlock(void *ctx){(void)ctx;assert(!pthread_mutex_unlock(&lock));}
static struct vbb_observer_sink sink={try_lock,append_locked,unlock,0};
static char sample[264];
static void *writer(void *arg)
{
    unsigned kind=*(unsigned *)arg;
    for(unsigned i=0;i<1000;i++) {
        if(!kind)while(!vbb_observer_publish_atomic(&sink,sample,264)){}
        else {
            assert(!pthread_mutex_lock(&lock));
            vl_append(&ring,"cpu log fragment",16);assert(!pthread_mutex_unlock(&lock));
        }
    }
    return 0;
}
static void ring_test(void)
{
    struct vbb_observer s;start(&s);memcpy(sample,s.line,264);
    uint64_t pos=VL_RING_SIZE-13;ring.total=pos;
    assert(!pthread_mutex_lock(&lock));
    assert(vbb_observer_publish_atomic(&sink,s.line,264)==0 && ring.total==pos && !appends);
    assert(!pthread_mutex_unlock(&lock));
    pthread_t a,b;unsigned zero=0,one=1;
    assert(!pthread_create(&a,0,writer,&zero));assert(!pthread_create(&b,0,writer,&one));
    assert(!pthread_join(a,0));assert(!pthread_join(b,0));assert(appends==1000);
    static char collected[300000];unsigned used=0;
    while(pos<ring.total) {
        unsigned chunk=1+used%127;
        assert(used+chunk<sizeof(collected));
        used+=vl_copy(&ring,&pos,collected+used,chunk);
    }
    unsigned frames=0;
    for(unsigned i=0;i+264<=used;i++)if(!memcmp(collected+i,"\nVBDR1 ",7)) {
        assert(!memcmp(collected+i,sample,264));frames++;i+=263;
    }
    assert(frames==1000); /* every atomic record survived wrap and other writers */
}
static int interactive(void)
{
    alarm(20);struct vbb_observer s;uint64_t now=100;start(&s);emit=1;
    for(;;) {
        vbb_observer_poll(&s,now,publish,0);
        if(s.state==VBB_OBS_DONE || s.state==VBB_OBS_STOPPED)break;
        char input[80];if(!fgets(input,sizeof(input),stdin))return 2;
        size_t n=strlen(input);if(n && input[n-1]=='\n')input[--n]=0;
        if(!strcmp(input,"timeout"))now=s.deadline;
        else if(!strcmp(input,"disconnect"))vbb_observer_disconnect(&s);
        else (void)vbb_observer_ack_line(&s,input,n,now);
    }
    printf("OBSERVER END state=%u steps=%u error=%u\n",s.state,s.synthetic_steps,s.error);
    return 0;
}
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--interactive"))return interactive();
    states();ring_test();
    puts("Dry observer: ACK/timeout/disconnect/100000 mutations/atomic ring concurrency PASS");return 0;
}
