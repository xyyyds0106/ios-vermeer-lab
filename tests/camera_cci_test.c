#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/media/vermeer_camera_cci.h"
struct mock {
    struct vcci *driver;
    unsigned master, ops, fault, revoke, delay, proof, queued, running, words, read_words;
    unsigned error_irq, never_done, wrong_version, stale, bad_level, reenter;
    uint32_t commands[6];
    uint64_t now;
};
static unsigned proof(void *p) { return ((struct mock *)p)->proof; }
static int time_us(void *p,uint64_t *out) { *out=((struct mock *)p)->now; return 1; }
static int pause_us(void *p,unsigned us) { ((struct mock *)p)->now+=us; return 1; }
static int tick(struct mock *m) {
    m->ops++;
    if(m->ops==m->revoke)m->proof=0;
    if(m->ops==m->delay)m->now+=20000;
    if(m->reenter && m->ops==m->reenter) {
        struct vcci_request r={.slot=0,.sid=0x20,.address_bytes=2,.address=0x1234,.count=2};
        struct vcci_result out;
        assert(!vcci_read_register(m->driver,&r,&out));
    }
    return m->ops!=m->fault;
}
static int read32(void *p,unsigned off,uint32_t *out) {
    struct mock *m=p; if(!tick(m))return 0; *out=0;
    if(off==0)*out=m->wrong_version?1:0x10060000;
    else if(off==0xc0c) {
        if(m->running && !m->never_done)*out=m->error_irq?0x08000000u<<(m->master?2:0):
            m->master?0x101000:0x101;
        else if(m->stale)*out=1;
    } else if(off==0x404+m->master*0x200)*out=m->running?0:m->queued;
    else if(off==0x11c+m->master*0x100)*out=m->running?
        m->bad_level?99:m->words-m->read_words:0;
    else if(off==0x118+m->master*0x100) {
        unsigned i=m->read_words++;
        for(unsigned b=0;b<4;b++)*out|=((i*4+b)&255)<<(b*8);
    }
    return 1;
}
static int write32(void *p,unsigned off,uint32_t value) {
    struct mock *m=p; if(!tick(m))return 0;
    if(off==0x410+m->master*0x200) { assert(m->queued<6); m->commands[m->queued++]=value; }
    else if(off==0x400+m->master*0x200)assert(value==6 && m->queued==6);
    else if(off==8) { assert(value==(1u<<(m->master*2+1))); m->running=1; }
    else if(off==0xc08)assert(value==(m->master?0x101000u:0x101u));
    else if(off==0xc00)assert(value==1);
    else assert(0);
    return 1;
}
static void init(struct vcci *s,struct mock *m,unsigned slot,unsigned count) {
    memset(m,0,sizeof(*m)); m->driver=s; m->proof=31; m->master=slot==2; m->words=count/4+1;
    struct vcci_io io={.ctx=m,.physical=vcci_slot_base(slot),.length=0x1000,
        .verified_hw_version=0x10060000,.proof=proof,.read=read32,.write=write32,
        .now_us=time_us,.pause_us=pause_us};
    assert(vcci_init(s,&io));
}
int main(void) {
    struct vcci s; struct mock m; struct vcci_result out, original;
    memset(&original,0xa5,sizeof(original));
    unsigned total=0;
    for(unsigned slot=0;slot<4;slot++)for(unsigned count=1;count<=16;count++) {
        init(&s,&m,slot,count);
        struct vcci_request r={.slot=slot,.sid=0x20,.address_bytes=2,.address=0x1234,.count=count};
        out=original; assert(vcci_read_register(&s,&r,&out));
        assert(out.count==count && out.fifo_prefix==0);
        for(unsigned i=0;i<count;i++)assert(out.data[i]==i+1);
        assert(m.commands[0]==0x201 && m.commands[1]==6 && m.commands[2]==0x34122b &&
            m.commands[3]==(10|(count<<4)) && m.commands[4]==7 && m.commands[5]==0x308);
        total=m.ops;
        for(unsigned kind=0;kind<4;kind++)for(unsigned at=1;at<=total;at++) {
            init(&s,&m,slot,count); out=original;
            if(kind==0)m.fault=at; else if(kind==1)m.revoke=at;
            else if(kind==2)m.delay=at; else m.reenter=at;
            assert(!vcci_read_register(&s,&r,&out));
            assert(m.ops==at && !memcmp(&out,&original,sizeof(out)) && s.retired);
            assert(!vcci_read_register(&s,&r,&out) && m.ops==at);
        }
    }
    struct vcci_request r={.slot=0,.sid=0x20,.address_bytes=1,.address=0xff,.count=2};
    for(unsigned kind=0;kind<6;kind++) {
        init(&s,&m,0,2); out=original;
        if(kind==0)m.wrong_version=1; if(kind==1)m.stale=1;
        if(kind==2)m.error_irq=1; if(kind==3)m.never_done=1;
        if(kind==4)m.bad_level=1; if(kind==5)m.proof=0;
        assert(!vcci_read_register(&s,&r,&out) && !memcmp(&out,&original,sizeof(out)));
        if(kind<2 || kind==5)assert(!s.touched);
    }
    init(&s,&m,0,2); r.slot=3; assert(!vcci_read_register(&s,&r,&out) && !m.ops);
    puts("camera CCI: 64 slot/length cases, every MMIO failure/revocation/deadline/reentry boundary PASS");
    return 0;
}
