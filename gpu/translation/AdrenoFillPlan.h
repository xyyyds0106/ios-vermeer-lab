/* SPDX-License-Identifier: MIT
 * Original bounded lowering candidate, not a Metal/IOKit ABI or GPU submitter.
 */
#ifndef ADRENO_FILL_PLAN_H
#define ADRENO_FILL_PLAN_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { AdrenoFillBuffer=1, AdrenoFillMaxBytes=4096, AdrenoFillMaxWords=1027 };
enum { AdrenoFillMapped=1, AdrenoFillWritable=2, AdrenoFillLeaseHeld=4 };
typedef enum {
    AdrenoFillPlanned=0, AdrenoFillUnsupported, AdrenoFillInvalid,
    AdrenoFillResource, AdrenoFillRange, AdrenoFillCapacity
} AdrenoFillStatus;
/* Typed internal IR after an actual API/ABI decoder, not an Objective-C or
 * untrusted wire struct. Zero length and byte-unaligned ranges are unsupported.
 */
typedef struct {
    uint32_t version,operation,value,reserved;
    uint64_t buffer_id,generation,context_id,offset,bytes;
} AdrenoFillRequest;
/* Trusted resolved view from the future resource owner. Flags DO NOT acquire
 * or prove a hardware lease. Never construct this view from client GPU/physical
 * addresses. logical_bytes is bytes(), not CPU-page padding/ownedBytes().
 */
typedef struct {
    uint64_t buffer_id,generation,context_id,gpu_base,logical_bytes;
    uint32_t address_bits,flags;
} AdrenoFillView;
typedef struct {
    uint64_t destination,bytes;
    uint32_t packet_words,requires_completion;
} AdrenoFillPlan;
/* Emit one CP_MEM_WRITE (0x3d), with a 64-bit GPU address and repeated bytes.
 * Inputs, output words, plan and word_count must be disjoint caller-owned
 * accessible storage; inputs remain immutable for this call. No allocation,
 * hardware, fence, cache/visibility operation, shader, power or implicit submit.
 * Failure: *word_count=0; words/plan unchanged. Planned means ENCODED only.
 * Caller must hold/revalidate the real mapping/lease through submission and
 * observed hardware completion; no resource may be released based on this plan.
 */
AdrenoFillStatus AdrenoFillTranslate(const AdrenoFillRequest *,const AdrenoFillView *,
    uint32_t *words,size_t capacity,AdrenoFillPlan *,size_t *word_count);
#ifdef __cplusplus
}
#endif
#endif
