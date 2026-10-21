/* SPDX-License-Identifier: MIT */
#ifndef ADRENO_CMD_DB_H
#define ADRENO_CMD_DB_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ADRENO_CMD_DB_BYTES 131072u
#define ADRENO_CMD_DB_MAX_ENTRIES 1024u
enum {AdrenoCmdDbOK,AdrenoCmdDbArgument,AdrenoCmdDbHeader,AdrenoCmdDbBounds,
      AdrenoCmdDbDuplicate,AdrenoCmdDbUnsupported,AdrenoCmdDbMissing};
enum {AdrenoCmdDbGfx,AdrenoCmdDbCx,AdrenoCmdDbMx,AdrenoCmdDbGmxc,
      AdrenoCmdDbSh0,AdrenoCmdDbMc0,AdrenoCmdDbAcv,AdrenoCmdDbCn0,AdrenoCmdDbResources};
struct AdrenoCmdDbResource {uint32_t address;uint16_t version,aux_bytes;uint8_t aux[32];};
struct AdrenoCmdDb {
    unsigned status,complete,entries,rows,found_mask;
    struct AdrenoCmdDbResource resource[AdrenoCmdDbResources];
    uint64_t seen[ADRENO_CMD_DB_MAX_ENTRIES]; /* caller-retained scratch, not kernel stack */
};
/* Full fixed-size, immutable snapshot from the platform-owned native copy.
 * A sparse touch-only copy cannot establish gmxc absence. Explicit version1
 * qualification is independent of the untrusted header. Complete means all
 * rows/entries/aux bounds and unique IDs were checked; OK additionally needs
 * all seven required ARC/BCM resources. No voltage/GX/HFI readiness is implied.
 * Input/output must be disjoint. Failure never authorizes a partial config.
 */
unsigned AdrenoCmdDbParse(const uint8_t *,size_t,unsigned,struct AdrenoCmdDb *);
const char *AdrenoCmdDbResourceName(unsigned);
#ifdef __cplusplus
}
#endif
#endif
