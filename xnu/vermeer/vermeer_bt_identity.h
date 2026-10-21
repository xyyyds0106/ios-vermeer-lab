/* One bounded standard HCI Read Local Version query over an owned byte link.
 * No UART/MMIO, reset, power, firmware, baud change or vendor command exists
 * here. This is not an iOS Bluetooth service. The future transport must supply
 * nonblocking callbacks and prove all four preconditions before using it.
 */
#ifndef VERMEER_BT_IDENTITY_H
#define VERMEER_BT_IDENTITY_H
#include "vermeer_bt_h4.h"
#define VBT_ID_EXCLUSIVE 1u
#define VBT_ID_AWAKE 2u
#define VBT_ID_CLEAN_RX 4u
#define VBT_ID_COMMAND_CREDIT 8u
#define VBT_ID_REQUIRED 15u
#define VBT_ID_MAX_STEPS 4096u
struct vbt_identity_io {
    void *ctx;
    /* Serialized/nonblocking callbacks must not reinitialize this query.
     * Return bytes transferred, 0 if would block, -1 on permanent error.
     */
    int (*write)(void *, const uint8_t *, size_t);
    int (*read)(void *, uint8_t *, size_t);
    uint64_t (*now_us)(void *);
    unsigned (*preconditions)(void *);
};
enum vbt_identity_state { VBT_ID_FAILED=-1, VBT_ID_WAITING=0, VBT_ID_DONE=1 };
struct vbt_identity {
    struct vbt_identity_io link; /* pinned callbacks/context for the entire query */
    struct vbt_h4 h4;
    struct vbt_local_version candidate;
    uint64_t start, last;
    unsigned timeout_us, steps, tx_bytes, events, have_version, busy;
    enum vbt_identity_state state;
};
static inline int vbt_identity_begin(struct vbt_identity *s,
                                    const struct vbt_identity_io *io,
                                    unsigned timeout_us)
{
    if (!s) return 0;
    vbt_h4_init(&s->h4); s->steps=0; s->tx_bytes=0; s->events=0;
    s->have_version=0; s->busy=0; s->state=VBT_ID_FAILED;
    if (!io || !io->write || !io->read || !io->now_us || !io->preconditions ||
        !timeout_us || timeout_us>1000000u ||
        (io->preconditions(io->ctx)&VBT_ID_REQUIRED)!=VBT_ID_REQUIRED) return 0;
    s->link=*io;
    s->start=s->last=s->link.now_us(s->link.ctx); s->timeout_us=timeout_us;
    s->state=VBT_ID_WAITING;
    return 1;
}
static inline void vbt_identity_packet(void *ctx, unsigned type,
                                      const uint8_t *event, size_t n)
{
    struct vbt_identity *s=ctx;
    if (type!=4 || ++s->events>32 || n<2) { s->h4.failed=1; return; }
    if (event[0]==0x0e) {
        /* No other command may be outstanding on this dedicated query link. */
        if (s->have_version || !vbt_h4_local_version(event,n,&s->candidate)) {
            s->h4.failed=1; return;
        }
        s->have_version=1;
    } else if (event[0]==0x0f || event[0]==0x10) {
        /* Unexpected Command Status / Hardware Error retires the transaction. */
        s->h4.failed=1;
    }
}
/* One bounded poll: at most one write of <=4 bytes and one read of <=64 bytes.
 * Deadline/step budget remains active during short writes and unrelated
 * events. Never resend a prefix or issue an automatic reset after failure.
 * ready/awake/initial-clean-RX/initial-credit attestations describe the same
 * transaction's owned link and remain set until that ownership is revoked;
 * the transport must keep unexpected IBS handled outside this query or fail.
 */
static inline enum vbt_identity_state vbt_identity_step(struct vbt_identity *s)
{
    if (!s) return VBT_ID_FAILED;
    if (s->state!=VBT_ID_WAITING) return s->state;
    if (s->busy) goto fail;
    const struct vbt_identity_io *io=&s->link;
    s->busy=1;
    if (++s->steps>VBT_ID_MAX_STEPS ||
        (io->preconditions(io->ctx)&VBT_ID_REQUIRED)!=VBT_ID_REQUIRED ||
        s->state!=VBT_ID_WAITING) goto fail;
    uint64_t now=io->now_us(io->ctx);
    if (s->state!=VBT_ID_WAITING || now<s->last || now-s->start>=s->timeout_us) goto fail;
    s->last=now;
    if (s->tx_bytes<4) {
        static const uint8_t cmd[]={1,1,0x10,0};
        size_t left=4-s->tx_bytes;
        int n=io->write(io->ctx,cmd+s->tx_bytes,left);
        if (s->state!=VBT_ID_WAITING || n<0 || (size_t)n>left) goto fail;
        s->tx_bytes+=(unsigned)n;
        /* A short/full write may block long enough to expire the deadline or
         * revoke ownership. Check before either returning WAITING or reading
         * another byte; no subsequent transport I/O may follow that loss.
         */
        now=io->now_us(io->ctx);
        if (s->state!=VBT_ID_WAITING || now<s->last || now-s->start>=s->timeout_us ||
            (io->preconditions(io->ctx)&VBT_ID_REQUIRED)!=VBT_ID_REQUIRED ||
            s->state!=VBT_ID_WAITING) goto fail;
        s->last=now;
        if (s->tx_bytes<4) { s->busy=0; return s->state; }
    }
    uint8_t bytes[64];
    int n=io->read(io->ctx,bytes,sizeof(bytes));
    if (s->state!=VBT_ID_WAITING || n<0 || n>(int)sizeof(bytes)) goto fail;
    if (n && !vbt_h4_feed(&s->h4,bytes,(size_t)n,vbt_identity_packet,NULL,s)) goto fail;
    /* A callback can advance time as well as transfer bytes. Do not publish a
     * response which arrived after the overall deadline or after revocation.
     */
    now=io->now_us(io->ctx);
    if (now<s->last || now-s->start>=s->timeout_us ||
        (io->preconditions(io->ctx)&VBT_ID_REQUIRED)!=VBT_ID_REQUIRED ||
        s->state!=VBT_ID_WAITING) goto fail;
    s->last=now;
    if (s->have_version) s->state=VBT_ID_DONE;
    s->busy=0; return s->state;
fail:
    s->state=VBT_ID_FAILED; s->h4.failed=1; s->busy=0;
    return s->state;
}
static inline int vbt_identity_result(const struct vbt_identity *s,
                                      struct vbt_local_version *out)
{
    if (!s || !out || s->state!=VBT_ID_DONE) return 0;
    *out=s->candidate;
    return 1;
}
#endif
