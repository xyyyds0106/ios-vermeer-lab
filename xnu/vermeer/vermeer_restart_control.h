/* SPDX-License-Identifier: MIT
 * Offline, default-off bootloader-reset queue policy. No USB registration,
 * threads, MMIO, VFS calls, reset or guessed firmware destination here.
 * The future USB callback may parse/queue only. A separate serialized worker
 * runs the bounded platform callbacks after host durable-save acknowledgement.
 */
#ifndef VERMEER_RESTART_CONTROL_H
#define VERMEER_RESTART_CONTROL_H
#include <stdint.h>
#include <stddef.h>
enum { VRC_IDLE,VRC_WAIT_ACK,VRC_QUEUED,VRC_WORKING,VRC_FAILED,VRC_RESET_RETURNED };
struct vrc_request {uint8_t token[16];uint64_t sequence;uint32_t crc;};
struct vrc_control {
    unsigned enabled,state;
    struct vrc_request expected;
    uint64_t started,deadline,last_time;
    int error;
};
struct vrc_ops {
    void *ctx;
    /* Read-only monotonic uptime in milliseconds. Neither this nor the lease
     * proof callback may map/write registers or change authorization/state. */
    uint64_t (*now_ms)(void *);
    /* A native implementation must prove live board/lease/firmware contract,
     * not infer them from an access_proven boolean. None is registered yet. */
    int (*platform_ready)(void *);
    int (*shutdown_sync)(void *); /* worker only, bounded; quiesce DMA + sync root */
    int (*program_reason)(void *); /* fixed SDAM reason only; completion/readback */
    int (*reset)(void *); /* firmware-qualified basic PSCI SMC; must not return */
};
static inline int vrc_hex(char c)
{
    if(c>='0' && c<='9')return c-'0';
    if(c>='a' && c<='f')return c-'a'+10;
    if(c>='A' && c<='F')return c-'A'+10;
    return -1;
}
static inline int vrc_probe_token(const char *text,size_t n,uint8_t token[16])
{
    if(!text || !token || n!=35 || text[0]!='r' || text[1]!='p' || text[2]!=' ')return 0;
    uint8_t result[16];unsigned nonzero=0;
    for(unsigned i=0;i<16;i++) {
        int a=vrc_hex(text[3+2*i]),b=vrc_hex(text[4+2*i]);if(a<0 || b<0)return 0;
        result[i]=(uint8_t)((a<<4)|b);nonzero|=result[i];
    }
    if(!nonzero)return 0;
    for(unsigned i=0;i<16;i++)token[i]=result[i];return 1;
}
static inline uint32_t vrc_probe_crc(const uint8_t token[16])
{
    /* Canonical fixed metadata-only request. Source/image identity stays in
     * the host launch manifest; a nonce echo is correlation, not attestation. */
    static const uint8_t schema[]="VRC1 K70 CORE=c400000+3000 CFG=c42d000+4000 PPID=071 EE=0";
    uint32_t crc=0xffffffffu;
    for(unsigned i=0;i<sizeof(schema)-1+16;i++) {
        crc^=i<sizeof(schema)-1?schema[i]:token[i-(sizeof(schema)-1)];
        for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));
    }
    return ~crc;
}
static inline int vrc_parse(const char *text,size_t n,struct vrc_request *out)
{
    /* rb +128-bit token +64-bit terminal sequence +32-bit terminal CRC is61
     * characters, within the existing63-character parser. No memory address. */
    if(!text || !out || n!=61 || text[0]!='r' || text[1]!='b' || text[2]!=' ' ||
       text[35]!=' ' || text[52]!=' ')return 0;
    struct vrc_request value={{0},0,0};unsigned nonzero=0;
    for(unsigned i=0;i<16;i++) {
        int a=vrc_hex(text[3+2*i]),b=vrc_hex(text[4+2*i]);
        if(a<0 || b<0)return 0;
        value.token[i]=(uint8_t)((a<<4)|b);nonzero|=value.token[i];
    }
    for(unsigned i=36;i<52;i++) {int h=vrc_hex(text[i]);if(h<0)return 0;value.sequence=(value.sequence<<4)|(unsigned)h;}
    for(unsigned i=53;i<61;i++) {int h=vrc_hex(text[i]);if(h<0)return 0;value.crc=(value.crc<<4)|(unsigned)h;}
    if(!nonzero || !value.sequence)return 0;
    *out=value;return 1;
}
static inline int vrc_arm(struct vrc_control *s,const struct vrc_request *terminal,uint64_t now,uint64_t ttl)
{
    if(!s || !s->enabled || s->state!=VRC_IDLE || !terminal || !terminal->sequence ||
       !ttl || ttl>60000 || now>UINT64_MAX-ttl)return 0;
    unsigned nonzero=0;for(unsigned i=0;i<16;i++)nonzero|=terminal->token[i];
    if(!nonzero)return 0;
    /* Producer binds this terminal to the immutable run/image identity. Host
     * must durably save it before sending the matching acknowledgement. */
    s->expected=*terminal;s->started=s->last_time=now;s->deadline=now+ttl;s->state=VRC_WAIT_ACK;return 1;
}
static inline int vrc_queue(struct vrc_control *s,const struct vrc_request *ack,uint64_t now)
{
    if(!s || !s->enabled || s->state!=VRC_WAIT_ACK || !ack)return 0;
    unsigned same=ack->sequence==s->expected.sequence && ack->crc==s->expected.crc;
    for(unsigned i=0;i<16;i++)same&=ack->token[i]==s->expected.token[i];
    if(!same || now<s->started || now>=s->deadline) {s->state=VRC_FAILED;s->error=-1;return 0;}
    s->last_time=now;s->state=VRC_QUEUED;return 1;
}
static inline int vrc_live_guard(struct vrc_control *s,const struct vrc_ops *ops)
{
    if(!s->enabled || s->state!=VRC_WORKING){s->error=-4;return 0;}
    uint64_t now=ops->now_ms(ops->ctx);
    if(now<s->last_time || now>=s->deadline){s->error=-5;return 0;}
    s->last_time=now;
    if(ops->platform_ready(ops->ctx)!=1){s->error=-3;return 0;}
    /* Bound time spent obtaining the proof as well. A native lease remains
     * held exclusively through the next destructive operation; a boolean
     * snapshot alone cannot provide concurrency safety. */
    now=ops->now_ms(ops->ctx);
    if(!s->enabled || s->state!=VRC_WORKING){s->error=-4;return 0;}
    if(now<s->last_time || now>=s->deadline){s->error=-5;return 0;}
    s->last_time=now;return 1;
}
static inline int vrc_worker(struct vrc_control *s,const struct vrc_ops *ops)
{
    if(!s || !s->enabled || s->state!=VRC_QUEUED)return 0;
    s->state=VRC_WORKING; /* consume before any callback; no automatic retry */
    s->error=-2;
    if(!ops || !ops->now_ms || !ops->platform_ready || !ops->shutdown_sync || !ops->program_reason || !ops->reset ||
       !vrc_live_guard(s,ops) || ops->shutdown_sync(ops->ctx)!=1 || !vrc_live_guard(s,ops) ||
       ops->program_reason(ops->ctx)!=1 || !vrc_live_guard(s,ops)) {s->state=VRC_FAILED;return 0;}
    /* Returning from a reset request is not success and not Fastboot proof.
     * Actual destination is established by the host's later USB enumeration. */
    s->error=ops->reset(ops->ctx);s->state=VRC_RESET_RETURNED;return 0;
}
#endif
