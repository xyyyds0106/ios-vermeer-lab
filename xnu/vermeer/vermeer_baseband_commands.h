/* Bounded dry-observer command handoff; no device/file I/O or observer calls.
 * One USB producer and one worker share ONE caller-owned lock around every
 * queue/staging operation, including fault/disconnect and START acceptance.
 * Only the worker touches staging. Zero once per boot; no reset/reuse API.
 * Returned requests are value copies, never pointers into the input/queue.
 */
#ifndef VERMEER_BASEBAND_COMMANDS_H
#define VERMEER_BASEBAND_COMMANDS_H
#include "vermeer_baseband_protocol.h"
#define VBB_COMMAND_LIMIT 63u
#define VBB_COMMAND_SLOTS 8u
#define VBB_STAGE_TIMEOUT_MS 2000u
enum { VBB_CMD_BEGIN=1,VBB_CMD_HASH,VBB_CMD_GO,VBB_CMD_ACK };
enum { VBB_STAGE_EMPTY,VBB_STAGE_COLLECT,VBB_STAGE_USED,VBB_STAGE_BLOCKED };
enum { VBB_CMD_OK,VBB_CMD_INVALID,VBB_CMD_OVERFLOW,VBB_CMD_DISCONNECTED,
       VBB_CMD_TIMEOUT,VBB_CMD_CLOCK,VBB_CMD_ORDER,VBB_CMD_TOKEN,VBB_CMD_REPLAY };
enum { VBB_REQUEST_NONE,VBB_REQUEST_START,VBB_REQUEST_ACK,VBB_REQUEST_STOP };
struct vbb_command {
    uint8_t token[16],part[8];
    uint64_t received_ms,sequence;
    uint32_t crc;
    unsigned kind,index;
};
struct vbb_command_queue {
    struct vbb_command slots[VBB_COMMAND_SLOTS];
    uint64_t last_received;
    unsigned head,count,error,clock_seen;
};
struct vbb_command_stage {
    uint8_t token[16],image[32];
    uint64_t deadline,last_now;
    unsigned state,next_part,error,clock_seen;
};
struct vbb_worker_request {
    uint8_t token[16],image[32];
    uint64_t sequence;
    uint32_t crc;
    unsigned kind;
};
static inline int vbb_cmd_hex(char c)
{
    if(c>='0' && c<='9')return c-'0';
    if(c>='a' && c<='f')return c-'a'+10;
    return -1;
}
static inline int vbb_cmd_bytes(const char *p,uint8_t *out,unsigned bytes)
{
    for(unsigned i=0;i<bytes;i++) {
        int a=vbb_cmd_hex(p[i*2]),b=vbb_cmd_hex(p[i*2+1]);
        if(a<0 || b<0)return 0;
        out[i]=(uint8_t)((unsigned)a*16+(unsigned)b);
    }
    return 1;
}
/* Complete line WITHOUT CR/LF. Caller must route only baseband commands here;
 * an upstream truncation/overflow must call fault, never enqueue a valid prefix.
 * Invalid parse clears out and never retains a pointer to caller memory. */
static inline int vbb_command_parse(const char *p,size_t n,uint64_t now,struct vbb_command *out)
{
    struct vbb_command c={0};const char *token=0;
    if(!out)return 0;
    *out=c;
    if(!p || n>VBB_COMMAND_LIMIT)return 0;
    if(n==35 && p[0]=='b' && p[2]==' ' && (p[1]=='b' || p[1]=='g')) {
        c.kind=p[1]=='b'?VBB_CMD_BEGIN:VBB_CMD_GO;token=p+3;
    } else if(n==53 && p[0]=='b' && p[1]=='h' && p[2]>='0' && p[2]<='3' &&
              p[3]==' ' && p[20]==' ') {
        c.kind=VBB_CMD_HASH;c.index=(unsigned)(p[2]-'0');token=p+21;
        if(!vbb_cmd_bytes(p+4,c.part,8))return 0;
    } else if(n==61 && p[0]=='b' && p[1]=='a' && p[2]==' ' && p[35]==' ' && p[52]==' ') {
        c.kind=VBB_CMD_ACK;token=p+3;
        for(unsigned i=36;i<52;i++) {
            int v=vbb_cmd_hex(p[i]);if(v<0)return 0;c.sequence=(c.sequence<<4)|(unsigned)v;
        }
        for(unsigned i=53;i<61;i++) {
            int v=vbb_cmd_hex(p[i]);if(v<0)return 0;c.crc=(c.crc<<4)|(unsigned)v;
        }
        if(!c.sequence)return 0;
    } else return 0;
    if(!vbb_cmd_bytes(token,c.token,16))return 0;
    unsigned nonzero=0;for(unsigned i=0;i<16;i++)nonzero|=c.token[i];
    if(!nonzero)return 0;
    c.received_ms=now;*out=c;return 1;
}
/* Sticky failure cancels all queued commands, including an unclaimed GO. */
static inline void vbb_command_fault(struct vbb_command_queue *q,unsigned error)
{
    if(!q)return;
    if(!q->error)q->error=error?error:VBB_CMD_INVALID;
    q->head=q->count=0;
    for(unsigned i=0;i<VBB_COMMAND_SLOTS;i++)q->slots[i]=(struct vbb_command){0};
}
static inline int vbb_command_enqueue(struct vbb_command_queue *q,const char *line,size_t n,uint64_t now)
{
    if(!q || q->error)return 0;
    struct vbb_command c;
    if(!vbb_command_parse(line,n,now,&c)){vbb_command_fault(q,VBB_CMD_INVALID);return 0;}
    if(q->clock_seen && now<q->last_received){vbb_command_fault(q,VBB_CMD_CLOCK);return 0;}
    if(q->count==VBB_COMMAND_SLOTS){vbb_command_fault(q,VBB_CMD_OVERFLOW);return 0;}
    q->slots[(q->head+q->count)%VBB_COMMAND_SLOTS]=c;q->count++;
    q->last_received=now;q->clock_seen=1;return 1;
}
static inline unsigned vbb_command_stop(struct vbb_command_stage *s,struct vbb_command_queue *q,
                                       struct vbb_worker_request *out,unsigned error)
{
    if(!s->error)s->error=error;
    s->state=VBB_STAGE_BLOCKED;s->next_part=0;
    for(unsigned i=0;i<16;i++)s->token[i]=0;
    for(unsigned i=0;i<32;i++)s->image[i]=0;
    vbb_command_fault(q,error);out->kind=VBB_REQUEST_STOP;return out->kind;
}
/* Call regularly even when the queue is empty. Caller holds the common lock
 * until it has copied/accepted START into its idle observer on the worker.
 * A later disconnect cancels future work, not a start already linearized before
 * that disconnect. Drain STOP before observer poll/ACK; never run on USB thread.
 */
static inline unsigned vbb_command_worker_next(struct vbb_command_stage *s,
    struct vbb_command_queue *q,uint64_t now,struct vbb_worker_request *out)
{
    if(!s || !q || !out)return VBB_REQUEST_STOP;
    *out=(struct vbb_worker_request){0};
    if(q->error || s->state==VBB_STAGE_BLOCKED)
        return vbb_command_stop(s,q,out,q->error?q->error:s->error);
    if(s->clock_seen && now<s->last_now)return vbb_command_stop(s,q,out,VBB_CMD_CLOCK);
    s->last_now=now;s->clock_seen=1;
    if(s->state==VBB_STAGE_COLLECT && now>=s->deadline)
        return vbb_command_stop(s,q,out,VBB_CMD_TIMEOUT);
    if(!q->count)return VBB_REQUEST_NONE;
    struct vbb_command c=q->slots[q->head];
    q->slots[q->head]=(struct vbb_command){0};q->head=(q->head+1)%VBB_COMMAND_SLOTS;q->count--;
    if(c.received_ms>now)return vbb_command_stop(s,q,out,VBB_CMD_CLOCK);
    if(c.kind==VBB_CMD_BEGIN) {
        if(s->state!=VBB_STAGE_EMPTY)return vbb_command_stop(s,q,out,VBB_CMD_REPLAY);
        if(UINT64_MAX-c.received_ms<VBB_STAGE_TIMEOUT_MS)
            return vbb_command_stop(s,q,out,VBB_CMD_CLOCK);
        s->deadline=c.received_ms+VBB_STAGE_TIMEOUT_MS;
        if(now>=s->deadline)return vbb_command_stop(s,q,out,VBB_CMD_TIMEOUT);
        for(unsigned i=0;i<16;i++)s->token[i]=c.token[i];
        s->state=VBB_STAGE_COLLECT;return VBB_REQUEST_NONE;
    }
    if(s->state!=VBB_STAGE_COLLECT && s->state!=VBB_STAGE_USED)
        return vbb_command_stop(s,q,out,VBB_CMD_ORDER);
    for(unsigned i=0;i<16;i++)if(c.token[i]!=s->token[i])
        return vbb_command_stop(s,q,out,VBB_CMD_TOKEN);
    if(c.kind==VBB_CMD_ACK && s->state==VBB_STAGE_USED) {
        for(unsigned i=0;i<16;i++)out->token[i]=c.token[i];
        out->sequence=c.sequence;out->crc=c.crc;out->kind=VBB_REQUEST_ACK;return out->kind;
    }
    if(s->state==VBB_STAGE_USED)return vbb_command_stop(s,q,out,VBB_CMD_REPLAY);
    if(c.kind==VBB_CMD_HASH) {
        if(c.index!=s->next_part)return vbb_command_stop(s,q,out,VBB_CMD_ORDER);
        for(unsigned i=0;i<8;i++)s->image[8*c.index+i]=c.part[i];
        s->next_part++;return VBB_REQUEST_NONE;
    }
    if(c.kind!=VBB_CMD_GO || s->next_part!=4)
        return vbb_command_stop(s,q,out,VBB_CMD_ORDER);
    unsigned nonzero=0;for(unsigned i=0;i<32;i++)nonzero|=s->image[i];
    if(!nonzero)return vbb_command_stop(s,q,out,VBB_CMD_INVALID);
    for(unsigned i=0;i<16;i++)out->token[i]=s->token[i];
    for(unsigned i=0;i<32;i++)out->image[i]=s->image[i];
    s->state=VBB_STAGE_USED;out->kind=VBB_REQUEST_START;return out->kind;
}
#endif
