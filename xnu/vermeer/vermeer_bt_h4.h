/* Original portable H4 framing core. No UART, power, firmware or IOKit access.
 * Wire layout references (not copied implementation):
 * Linux v6.1 include/net/bluetooth/hci.h; MiCode bsp-vermeer-t-oss
 * 53ef33eacdc4f7ac737451e4cfc91963d59e3956 drivers/bluetooth/hci_qca.c.
 * One serialized owner. Callbacks borrow bytes only until they return and
 * must not reenter or modify the receiver. Errors retire the stream; only a
 * transport reset with discarded RX bytes may initialize it again.
 */
#ifndef VERMEER_BT_H4_H
#define VERMEER_BT_H4_H
#include <stddef.h>
#include <stdint.h>

/* Local policy, not the controller's advertised ACL/ISO buffer capacity. */
#define VBT_H4_CAPACITY 4096u
struct vbt_h4 {
    uint8_t bytes[VBT_H4_CAPACITY]; /* HCI header + payload, excluding H4 type */
    size_t used, expected;
    unsigned type, header, failed, busy;
};
typedef void (*vbt_packet_fn)(void *, unsigned, const uint8_t *, size_t);
typedef void (*vbt_ibs_fn)(void *, unsigned);

static inline void vbt_h4_init(struct vbt_h4 *r)
{
    r->used=0; r->expected=0; r->type=0; r->header=0;
    r->failed=0; r->busy=0;
}

/* Returns 1 on success (including incomplete frames), 0 on retired stream.
 * The transport must treat UART overrun/framing errors or an incomplete-frame
 * timeout as a failure too. Never scan arbitrary payload for a new H4 type.
 * IBS is opt-in and recognized only BETWEEN packets. A callback reports the
 * control byte; the platform still owns clock votes, ACKs and sleep state.
 */
static inline int vbt_h4_feed(struct vbt_h4 *r, const uint8_t *src, size_t n,
                             vbt_packet_fn packet, vbt_ibs_fn ibs, void *ctx)
{
    if (!r) return 0;
    if (r->failed || r->busy || (!src && n) || !packet) {
        r->failed=1; return 0;
    }
    r->busy=1;
    for (size_t i=0; i<n; ++i) {
        unsigned b=src[i];
        if (!r->type) {
            if (b>=0xfcu && b<=0xfeu && ibs) {
                ibs(ctx,b);
                if (r->failed) break;
                continue;
            }
            /* Controller-to-host: ACL, SCO, Event, ISO. Commands are TX. */
            unsigned h=b==2 ? 4 : b==3 ? 3 : b==4 ? 2 : b==5 ? 4 : 0;
            if (!h) { r->failed=1; break; }
            r->type=b; r->header=h; r->used=0; r->expected=h;
            continue;
        }
        r->bytes[r->used++]=(uint8_t)b;
        if (r->used!=r->expected) continue;
        if (r->used==r->header) {
            size_t payload;
            if (r->type==4) payload=r->bytes[1];
            else if (r->type==3) payload=r->bytes[2];
            else {
                payload=(size_t)r->bytes[2] | (size_t)r->bytes[3]<<8;
                /* ISO_Data_Load_Length is 14 bits; bits 14..15 reserved. */
                if (r->type==5 && (payload & 0xc000u)) {
                    r->failed=1; break;
                }
            }
            if (payload>VBT_H4_CAPACITY-r->header) {
                r->failed=1; break;
            }
            r->expected=r->header+payload;
            if (payload) continue;
        }
        packet(ctx,r->type,r->bytes,r->used);
        if (r->failed) break;
        r->type=0; r->used=0; r->expected=0; r->header=0;
    }
    r->busy=0;
    return !r->failed;
}

/* Encode an HCI command including its H4 type. Returns 0 without writing on
 * invalid arguments/insufficient capacity. Input must not overlap output.
 * Command credits and controller readiness are the caller's responsibility.
 */
static inline size_t vbt_h4_command(uint8_t *dst, size_t capacity,
                                  uint16_t opcode, const uint8_t *args, size_t n)
{
    if (!dst || n>255 || capacity<n+4 || (!args && n) || !(opcode>>10)) return 0;
    dst[0]=1; dst[1]=(uint8_t)opcode; dst[2]=(uint8_t)(opcode>>8);
    dst[3]=(uint8_t)n;
    for (size_t i=0; i<n; ++i) dst[4+i]=args[i];
    return n+4;
}

struct vbt_local_version {
    uint16_t hci_revision, manufacturer, lmp_subversion;
    uint8_t hci_version, lmp_version, command_credits;
};

/* Successful Read Local Version Information (opcode 0x1001) complete event.
 * Input is the HCI event header+payload, excluding the H4 type. Return 0 and
 * preserve output for another opcode, failure status or malformed/trailing
 * data. These fields identify the reported controller revision; they do not
 * establish which DT candidate, firmware file or power sequence is correct.
 */
static inline int vbt_h4_local_version(const uint8_t *event, size_t n,
                                      struct vbt_local_version *version)
{
    if (!event || !version || n!=14 || event[0]!=0x0e || event[1]!=12 ||
        event[3]!=1 || event[4]!=0x10 || event[5]!=0) return 0;
    struct vbt_local_version v;
    v.command_credits=event[2]; v.hci_version=event[6];
    v.hci_revision=(uint16_t)((unsigned)event[7] | (unsigned)event[8]<<8);
    v.lmp_version=event[9];
    v.manufacturer=(uint16_t)((unsigned)event[10] | (unsigned)event[11]<<8);
    v.lmp_subversion=(uint16_t)((unsigned)event[12] | (unsigned)event[13]<<8);
    *version=v;
    return 1;
}
#endif
