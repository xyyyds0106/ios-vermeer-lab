/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ADRENO_FIRMWARE_H
#define ADRENO_FIRMWARE_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t address;
  uint64_t bytes;
  uint32_t identifier;
} AdrenoFirmwareRegion;

typedef struct {
  uint32_t address, bytes, type, value;
  size_t payload_offset;
  uint32_t region_identifier;
} AdrenoFirmwareBlock;

typedef enum {
  AdrenoFirmwareSuccess,
  AdrenoFirmwareInvalid,
  AdrenoFirmwareTruncated,
  AdrenoFirmwareUnmapped,
  AdrenoFirmwareNoSpace
} AdrenoFirmwareResult;

/* No output entries are changed on failure; *count is then zero.
 * Zero-size metadata stays opaque, with region_identifier UINT32_MAX.
 * Success validates structure/ranges, not firmware authenticity or execution.
 */
#ifdef __cplusplus
extern "C" {
#endif
AdrenoFirmwareResult AdrenoFirmwarePlanGmu(
    const uint8_t *firmware, size_t bytes,
    const AdrenoFirmwareRegion *regions, size_t region_count,
    AdrenoFirmwareBlock *blocks, size_t capacity, size_t *count);
#ifdef __cplusplus
}
#endif
#endif
