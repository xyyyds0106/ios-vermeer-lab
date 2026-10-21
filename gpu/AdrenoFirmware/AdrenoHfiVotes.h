/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ADRENO_HFI_VOTES_H
#define ADRENO_HFI_VOTES_H
#include "AdrenoHfiStartup.h"
#ifdef __cplusplus
extern "C" {
#endif
/* CPU-side generation only, no RPMh transaction or power transition.
 * Input levels are from exact enabled OPPs (including off=0), not frequencies
 * or voltages. Rail byte spans are original little-endian cmd-db aux data.
 * Caller selects actual primary/secondary rails (including GMxC discovery).
 * Rail profile: off at entry zero, ascending active levels (duplicates allowed),
 * optional zero padding at the end. Other profiles explicitly refuse.
 * Supports <=256 rail entries, <=16 OPPs; no truncation or guessed fallback.
 * Output remains unchanged on failure. Inputs/outputs must be disjoint and
 * immutable during calls. Successful arithmetic does not authenticate inputs. */
AdrenoHfiStartupStatus AdrenoHfiArcVotes(const uint32_t *levels,size_t level_count,
    const uint8_t *primary,size_t primary_bytes,const uint8_t *secondary,size_t secondary_bytes,
    uint32_t *votes,size_t capacity);
typedef struct {
    uint32_t address,unit;
    uint16_t width,buswidth;
    uint8_t vcd,fixed;
    uint16_t perfmode;
    uint32_t perfmode_kbps;
} AdrenoHfiBcm;
/* Decode exactly eight bytes of BCM aux data; address/buswidth/fixed/perfmode
 * are separately supplied source/platform policy and are preserved. */
AdrenoHfiStartupStatus AdrenoHfiBcmAux(AdrenoHfiBcm *,const uint8_t *,size_t);
/* A7xx BCM arithmetic and modern HFI bandwidth layout. Off=0, then strictly
 * ascending KB/s OPP bandwidths, <=16. Up to three platform-ordered BCMs;
 * CN0 address must come from command-db. No source fallback address exists.
 * Unit/width come from BCM aux; buswidth/fixed/perfmode from target catalog.
 * Fills only bandwidth-related config fields, preserving performance/features.
 * Failure leaves the entire config unchanged. Success requires no HFI/device.
 * Does not validate actual rail/BCM ownership, limits, speed bin or power. */
AdrenoHfiStartupStatus AdrenoHfiBuildBandwidth(const uint32_t *kbps,size_t level_count,
    const AdrenoHfiBcm *bcms,size_t bcm_count,uint32_t cn0_address,AdrenoHfiStartupConfig *);
#ifdef __cplusplus
}
#endif
#endif
