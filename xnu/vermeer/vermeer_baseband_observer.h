/* Single-owner, dry-run-only observer. No reader, MMIO, USB, reboot or thread
 * API exists here. USB callbacks must queue ACKs to the worker, not race it.
 * VBDR magic distinguishes synthetic data from real VBBR evidence, including
 * inside the CRC-protected binary frame. Zero-initialize the context once.
 */
#ifndef VERMEER_BASEBAND_OBSERVER_H
#define VERMEER_BASEBAND_OBSERVER_H
#include "vermeer_baseband_record.h"
#define VBB_OBSERVER_LINE_SIZE 264u /* leading LF +263-byte record line */
#define VBB_OBSERVER_ACK_SIZE 61u  /* ba TOKEN32 SEQ16 CRC8, excluding LF */
enum { VBB_OBS_IDLE,VBB_OBS_PUBLISH,VBB_OBS_WAIT_ACK,VBB_OBS_DONE,VBB_OBS_STOPPED };
enum { VBB_OBS_NO_ERROR,VBB_OBS_TIMEOUT,VBB_OBS_CLOCK,VBB_OBS_DISCONNECTED,
       VBB_OBS_INVALID,VBB_OBS_PUBLISH_ERROR };
struct vbb_observer {
    struct vbb_record record;
    uint8_t frame[VBB_RECORD_SIZE];
    char line[VBB_OBSERVER_LINE_SIZE];
    uint64_t deadline,last_now,timeout;
    unsigned state,error,synthetic_steps;
};
/* Return1 iff all bytes were appended exactly once;0 iff none were appended
 * (e.g. lock contention); negative means fatal. Never use printf or split the
 * frame. All console writers and this callback must share the same ring lock.
 * The callback copies the bytes synchronously and must not reenter the worker.
 */
typedef int (*vbb_observer_publish_fn)(void *,const char *,size_t);
static inline void vbb_observer_stop(struct vbb_observer *s,unsigned error)
{ s->state=VBB_OBS_STOPPED;if(!s->error)s->error=error; }
static inline int vbb_observer_pack(struct vbb_observer *s)
{
    if(!vbb_record_encode(&s->record,s->frame,sizeof(s->frame)))return 0;
    vbb_put32(s->frame,UINT32_C(0x52444256)); /* VBDR */
    vbb_put32(s->frame+124,vbb_record_crc(s->frame,124));
    s->line[0]='\n';
    const char prefix[]="VBDR1 ";
    for(unsigned i=0;i<6;i++)s->line[i+1]=prefix[i];
    const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<128;i++) {
        s->line[7+2*i]=digits[s->frame[i]>>4];s->line[8+2*i]=digits[s->frame[i]&15];
    }
    s->line[263]='\n';return 1;
}
static inline int vbb_observer_deadline(struct vbb_observer *s,uint64_t now)
{
    if(UINT64_MAX-now<s->timeout){vbb_observer_stop(s,VBB_OBS_CLOCK);return 0;}
    s->deadline=now+s->timeout;return 1;
}
static inline int vbb_observer_start(struct vbb_observer *s,const uint8_t run[16],
    const uint8_t image[32],uint64_t now,uint64_t timeout)
{
    if(!s || s->state!=VBB_OBS_IDLE || !run || !image || !timeout || timeout>60000 ||
       UINT64_MAX-now<timeout)return 0;
    s->record=(struct vbb_record){0};s->record.state=VBB_REC_BEGIN;s->record.sequence=1;
    for(unsigned i=0;i<16;i++)s->record.run[i]=run[i];
    for(unsigned i=0;i<32;i++)s->record.image_sha256[i]=image[i];
    s->timeout=timeout;s->last_now=now;s->deadline=now+timeout;
    if(!vbb_observer_pack(s)){vbb_observer_stop(s,VBB_OBS_INVALID);return 0;}
    s->state=VBB_OBS_PUBLISH;return 1;
}
static inline int vbb_observer_time(struct vbb_observer *s,uint64_t now)
{
    if(s->state!=VBB_OBS_PUBLISH && s->state!=VBB_OBS_WAIT_ACK)return 0;
    if(now<s->last_now){vbb_observer_stop(s,VBB_OBS_CLOCK);return 0;}
    s->last_now=now;
    if(now<s->deadline)return 1;
    /* Terminal acknowledgement timeout stops; never emit after a terminal. */
    if(s->record.state==VBB_REC_FINISHED || s->record.state==VBB_REC_REFUSED) {
        vbb_observer_stop(s,VBB_OBS_TIMEOUT);return 0;
    }
    /* No BEGIN reached the host, or a synthetic step completed but READ_OK
     * could not be published: leave an incomplete trace, not a false refusal
     * claiming that the already-completed step never happened. */
    if(s->state==VBB_OBS_PUBLISH && (s->record.state==VBB_REC_BEGIN ||
                                    s->record.state==VBB_REC_READ_OK)) {
        vbb_observer_stop(s,VBB_OBS_TIMEOUT);return 0;
    }
    /* A published BEFORE_READ may time out without a synthetic step. Keep its
     * phase and emit REFUSED, never invent READ_OK or a hardware exception. */
    if(s->state==VBB_OBS_WAIT_ACK)s->record.sequence++;
    s->record.state=VBB_REC_REFUSED;s->record.result=3;
    s->record.address=0;s->record.length=0;s->error=VBB_OBS_TIMEOUT;
    if(!vbb_observer_deadline(s,now) || !vbb_observer_pack(s)) {
        vbb_observer_stop(s,VBB_OBS_INVALID);return 0;
    }
    s->state=VBB_OBS_PUBLISH;return 0;
}
static inline void vbb_observer_poll(struct vbb_observer *s,uint64_t now,
                                    vbb_observer_publish_fn publish,void *ctx)
{
    if(!s)return;
    (void)vbb_observer_time(s,now);
    if(s->state!=VBB_OBS_PUBLISH)return;
    if(!publish){vbb_observer_stop(s,VBB_OBS_PUBLISH_ERROR);return;}
    int rc=publish(ctx,s->line,sizeof(s->line));
    if(rc==1)s->state=VBB_OBS_WAIT_ACK;
    else if(rc!=0)vbb_observer_stop(s,VBB_OBS_PUBLISH_ERROR);
}
static inline int vbb_observer_ack(struct vbb_observer *s,const uint8_t run[16],
                                  uint64_t sequence,uint32_t crc,uint64_t now)
{
    if(!s || !vbb_observer_time(s,now) || s->state!=VBB_OBS_WAIT_ACK || !run ||
       sequence!=s->record.sequence || crc!=vbb_u32(s->frame+124))return 0;
    for(unsigned i=0;i<16;i++)if(run[i]!=s->record.run[i])return 0;
    if(s->record.state==VBB_REC_FINISHED){s->state=VBB_OBS_DONE;return 1;}
    if(s->record.state==VBB_REC_REFUSED){s->state=VBB_OBS_STOPPED;return 1;}
    s->record.sequence++;
    if(s->record.state==VBB_REC_BEFORE_READ) {
        /* Deliberately no callback and no physical-pointer dereference. */
        s->synthetic_steps++;s->record.state=VBB_REC_READ_OK;
    } else if(s->record.state==VBB_REC_READ_OK && s->record.phase==5) {
        s->record.state=VBB_REC_FINISHED;s->record.address=0;s->record.length=0;
    } else {
        s->record.state=VBB_REC_BEFORE_READ;s->record.phase++;
        switch(s->record.phase) {
        case 1:s->record.address=UINT64_C(0x81eff000);s->record.length=32;break;
        case 2:s->record.address=UINT64_C(0x81eff020);s->record.length=96;break;
        case 3:s->record.address=UINT64_C(0x81eff000);s->record.length=128;break;
        default:s->record.address=UINT64_C(0x81d01000);s->record.length=32;break;
        }
    }
    if(!vbb_observer_deadline(s,now) || !vbb_observer_pack(s)) {
        vbb_observer_stop(s,VBB_OBS_INVALID);return 0;
    }
    s->state=VBB_OBS_PUBLISH;return 1;
}
static inline int vbb_observer_hex(char c)
{
    if(c>='0' && c<='9')return c-'0';
    if(c>='a' && c<='f')return c-'a'+10;
    return -1;
}
static inline int vbb_observer_ack_line(struct vbb_observer *s,const char *p,size_t n,uint64_t now)
{
    if(!p || n!=VBB_OBSERVER_ACK_SIZE || p[0]!='b' || p[1]!='a' || p[2]!=' ' ||
       p[35]!=' ' || p[52]!=' ')return 0;
    uint8_t run[16];uint64_t sequence=0;uint32_t crc=0;
    for(unsigned i=0;i<16;i++) {
        int a=vbb_observer_hex(p[3+i*2]),b=vbb_observer_hex(p[4+i*2]);
        if(a<0 || b<0)return 0;
        run[i]=(uint8_t)((unsigned)a*16+(unsigned)b);
    }
    for(unsigned i=36;i<52;i++) {
        int v=vbb_observer_hex(p[i]);if(v<0)return 0;sequence=(sequence<<4)|(unsigned)v;
    }
    for(unsigned i=53;i<61;i++) {
        int v=vbb_observer_hex(p[i]);if(v<0)return 0;crc=(crc<<4)|(unsigned)v;
    }
    return vbb_observer_ack(s,run,sequence,crc,now);
}
static inline void vbb_observer_disconnect(struct vbb_observer *s)
{
    if(s && s->state!=VBB_OBS_DONE && s->state!=VBB_OBS_STOPPED)
        vbb_observer_stop(s,VBB_OBS_DISCONNECTED);
}
/* Adapter helper for the integration owner. append is infallible while locked
 * and copies the complete line into an owned ring; try_lock fails without
 * changes. The same lock must protect all other writers. No format calls. */
struct vbb_observer_sink {
    int (*try_lock)(void *);
    void (*append_locked)(void *,const char *,size_t);
    void (*unlock)(void *);
    void *context;
};
static inline int vbb_observer_publish_atomic(void *arg,const char *line,size_t n)
{
    struct vbb_observer_sink *sink=arg;
    if(!sink || !sink->try_lock || !sink->append_locked || !sink->unlock ||
       !line || n!=VBB_OBSERVER_LINE_SIZE)return -1;
    if(!sink->try_lock(sink->context))return 0;
    sink->append_locked(sink->context,line,n);
    sink->unlock(sink->context);return 1;
}
#endif
