#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_baseband_commands.h"
#include "../xnu/vermeer/vermeer_baseband_observer.h"
static const char token[]="01000000000000000000000000000000";
static const char *const commands[]={
    "bb 01000000000000000000000000000000",
    "bh0 0200000000000000 01000000000000000000000000000000",
    "bh1 0000000000000000 01000000000000000000000000000000",
    "bh2 0000000000000000 01000000000000000000000000000000",
    "bh3 0000000000000000 01000000000000000000000000000000",
    "bg 01000000000000000000000000000000"};
struct harness {
    struct vbb_command_queue q;
    struct vbb_command_stage s;
    struct vbb_observer observer;
    unsigned starts;
};
static unsigned work(struct harness *h,uint64_t now)
{
    struct vbb_worker_request request;
    unsigned kind=vbb_command_worker_next(&h->s,&h->q,now,&request);
    if(kind==VBB_REQUEST_START) {
        h->starts++;
        assert(vbb_observer_start(&h->observer,request.token,request.image,now,100));
    } else if(kind==VBB_REQUEST_ACK) {
        assert(vbb_observer_ack(&h->observer,request.token,request.sequence,request.crc,now));
    } else if(kind==VBB_REQUEST_STOP)vbb_observer_disconnect(&h->observer);
    return kind;
}
static unsigned send(struct harness *h,const char *p,uint64_t now)
{
    (void)vbb_command_enqueue(&h->q,p,strlen(p),now);return work(h,now);
}
static int publish(void *ctx,const char *p,size_t n)
{
    (void)ctx;assert(n==264 && !memcmp(p,"\nVBDR1 ",7));return 1;
}
static void finish(struct harness *h)
{
    while(h->observer.state==VBB_OBS_PUBLISH) {
        vbb_observer_poll(&h->observer,100,publish,0);
        char run_hex[33],ack[62];
        for(unsigned i=0;i<16;i++)snprintf(run_hex+2*i,3,"%02x",h->observer.record.run[i]);
        assert(snprintf(ack,sizeof(ack),"ba %s %016llx %08x",run_hex,
            (unsigned long long)h->observer.record.sequence,vbb_u32(h->observer.frame+124))==61);
        assert(send(h,ack,100)==VBB_REQUEST_ACK);
    }
}
static void tests(void)
{
    struct harness h={0};
    /* USB enqueue cannot start or mutate worker-owned staging. */
    for(unsigned i=0;i<6;i++)assert(vbb_command_enqueue(&h.q,commands[i],strlen(commands[i]),100));
    assert(h.s.state==VBB_STAGE_EMPTY && !h.starts);
    for(unsigned i=0;i<6;i++)assert(work(&h,100)==(i==5?VBB_REQUEST_START:VBB_REQUEST_NONE));
    assert(h.starts==1 && h.observer.record.run[0]==1 && h.observer.record.image_sha256[0]==2);
    finish(&h);assert(h.observer.state==VBB_OBS_DONE && h.observer.synthetic_steps==5);
    assert(send(&h,commands[0],100)==VBB_REQUEST_STOP && h.starts==1);
    assert(!vbb_command_enqueue(&h.q,commands[0],35,100));
    for(unsigned n=0;n<5;n++) { /* incomplete fragments + GO */
        h=(struct harness){0};send(&h,commands[0],100);
        for(unsigned i=1;i<=n && i<4;i++)send(&h,commands[i],100);
        assert(send(&h,commands[5],100)==VBB_REQUEST_STOP && !h.starts && !h.observer.synthetic_steps);
    }
    h=(struct harness){0};send(&h,commands[0],100);send(&h,commands[2],100);
    assert(h.s.state==VBB_STAGE_BLOCKED && !h.starts);
    h=(struct harness){0};send(&h,commands[0],100);send(&h,commands[1],100);send(&h,commands[1],100);
    assert(h.s.state==VBB_STAGE_BLOCKED && !h.starts);
    h=(struct harness){0};send(&h,commands[0],100);
    char wrong[54];memcpy(wrong,commands[1],54);wrong[21]='f';send(&h,wrong,100);
    assert(h.s.error==VBB_CMD_TOKEN && !h.starts);
    h=(struct harness){0};send(&h,commands[0],100);assert(work(&h,2100)==VBB_REQUEST_STOP && !h.starts);
    h=(struct harness){0};for(unsigned i=0;i<6;i++)vbb_command_enqueue(&h.q,commands[i],strlen(commands[i]),100);
    assert(work(&h,2100)==VBB_REQUEST_STOP && !h.starts); /* queue residence counts */
    h=(struct harness){0};send(&h,commands[0],100);send(&h,commands[1],2099);
    assert(work(&h,2100)==VBB_REQUEST_STOP && !h.starts); /* fragments cannot extend TTL */
    h=(struct harness){0};send(&h,commands[0],100);assert(work(&h,99)==VBB_REQUEST_STOP);
    h=(struct harness){0};vbb_command_enqueue(&h.q,commands[0],35,101);
    assert(work(&h,100)==VBB_REQUEST_STOP && !h.starts);
    h=(struct harness){0};send(&h,commands[0],UINT64_MAX-1000);assert(!h.starts && h.s.error==VBB_CMD_CLOCK);
    h=(struct harness){0};for(unsigned i=0;i<6;i++)vbb_command_enqueue(&h.q,commands[i],strlen(commands[i]),100);
    vbb_command_fault(&h.q,VBB_CMD_DISCONNECTED);assert(work(&h,100)==VBB_REQUEST_STOP && !h.starts);
    h=(struct harness){0};for(unsigned i=0;i<8;i++)assert(vbb_command_enqueue(&h.q,commands[i%6],strlen(commands[i%6]),100));
    assert(!vbb_command_enqueue(&h.q,commands[0],35,100));assert(h.q.error==VBB_CMD_OVERFLOW);
    assert(work(&h,100)==VBB_REQUEST_STOP && !h.starts);
    /* Invalid input after a queued GO cancels that unclaimed start too. */
    h=(struct harness){0};for(unsigned i=0;i<6;i++)vbb_command_enqueue(&h.q,commands[i],strlen(commands[i]),100);
    assert(!vbb_command_enqueue(&h.q,"bb",2,100));assert(work(&h,100)==VBB_REQUEST_STOP && !h.starts);
    /* Queue stores values, not input pointers. */
    h=(struct harness){0};char source[36];memcpy(source,commands[0],36);
    assert(vbb_command_enqueue(&h.q,source,35,100));memset(source,'x',35);
    assert(work(&h,100)==VBB_REQUEST_NONE && h.s.token[0]==1);
    /* Request copies stay immutable after staging is consumed/poisoned. */
    struct vbb_command_queue q={0};struct vbb_command_stage s={0};struct vbb_worker_request r;
    for(unsigned i=0;i<6;i++){vbb_command_enqueue(&q,commands[i],strlen(commands[i]),100);vbb_command_worker_next(&s,&q,100,&r);}
    struct vbb_worker_request saved=r;
    assert(saved.kind==VBB_REQUEST_START);vbb_command_fault(&q,VBB_CMD_DISCONNECTED);
    vbb_command_worker_next(&s,&q,100,&r);assert(saved.token[0]==1 && saved.image[0]==2);
    struct vbb_command c;
    for(unsigned i=0;i<6;i++)for(unsigned n=0;n<strlen(commands[i]);n++) {
        assert(!vbb_command_parse(commands[i],n,0,&c) && !c.kind);
    }
    assert(!vbb_command_parse(commands[0],64,0,&c)); /* checked before reading bytes */
    assert(!vbb_command_parse("bb 00000000000000000000000000000000",35,0,&c));
    uint32_t rng=41;char fuzz[64];
    for(unsigned i=0;i<100000;i++) {
        for(unsigned j=0;j<64;j++){rng=1664525u*rng+1013904223u;fuzz[j]=(char)(rng>>24);}
        if(vbb_command_parse(fuzz,i%65,0,&c))assert(c.kind>=1 && c.kind<=4);
        else assert(!c.kind);
    }
    (void)token;
}
static int script(const char *path)
{
    FILE *file=fopen(path,"rb");if(!file)return 2;
    struct harness h={0};char line[128];uint64_t now=100;
    while(fgets(line,sizeof(line),file)) {
        size_t n=strlen(line);if(n && line[n-1]=='\n')line[--n]=0;
        if(!strcmp(line,"timeout"))now=2100;
        else if(!strcmp(line,"disconnect"))vbb_command_fault(&h.q,VBB_CMD_DISCONNECTED);
        else (void)vbb_command_enqueue(&h.q,line,n,now);
        (void)work(&h,now);
    }
    fclose(file);
    if(h.s.state==VBB_STAGE_COLLECT)(void)work(&h,2100);
    if(h.observer.state==VBB_OBS_PUBLISH)finish(&h);
    printf("starts=%u steps=%u stage=%u observer=%u error=%u\n",h.starts,h.observer.synthetic_steps,
           h.s.state,h.observer.state,h.s.error);
    return 0;
}
int main(int argc,char **argv)
{
    if(argc==3 && !strcmp(argv[1],"--script"))return script(argv[2]);
    tests();puts("Command staging: ordered immutable one-shot handoff/bounds/timeout/queue faults/100000 parser mutations PASS");return 0;
}
