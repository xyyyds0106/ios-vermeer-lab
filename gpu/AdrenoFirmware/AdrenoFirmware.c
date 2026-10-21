/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "AdrenoFirmware.h"

static uint32_t le32(const uint8_t *p)
{
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
         (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

AdrenoFirmwareResult AdrenoFirmwarePlanGmu(
    const uint8_t *firmware, size_t bytes,
    const AdrenoFirmwareRegion *regions, size_t region_count,
    AdrenoFirmwareBlock *blocks, size_t capacity, size_t *count)
{
  if (!count) return AdrenoFirmwareInvalid;
  *count = 0;
  if (!firmware || !regions || !blocks || !region_count || region_count > 64)
    return AdrenoFirmwareInvalid;
  if (bytes < 16) return AdrenoFirmwareTruncated;
  for (size_t i = 0; i < region_count; ++i) {
    const AdrenoFirmwareRegion *a = regions + i;
    if (!a->bytes || (a->address & 3) || (a->bytes & 3) ||
        a->bytes > UINT64_C(0x100000000) - a->address || a->identifier == UINT32_MAX)
      return AdrenoFirmwareInvalid;
    for (size_t j = 0; j < i; ++j) {
      const AdrenoFirmwareRegion *b = regions + j;
      if (a->identifier == b->identifier ||
          ((uint64_t)a->address < (uint64_t)b->address + b->bytes &&
           (uint64_t)b->address < (uint64_t)a->address + a->bytes))
        return AdrenoFirmwareInvalid;
    }
  }
  for (unsigned pass = 0; pass < 2; ++pass) {
    size_t offset = 0, n = 0;
    while (offset < bytes) {
      if (bytes - offset < 16) return AdrenoFirmwareTruncated;
      uint32_t address = le32(firmware + offset);
      uint32_t length = le32(firmware + offset + 4);
      uint32_t identifier = UINT32_MAX;
      if (length > bytes - offset - 16) return AdrenoFirmwareTruncated;
      if (length) {
        if ((address & 3) || (length & 3) ||
            length > UINT64_C(0x100000000) - address)
          return AdrenoFirmwareInvalid;
        for (size_t i = 0; i < region_count; ++i) {
          if (address >= regions[i].address && length <= regions[i].bytes &&
              (uint64_t)address - regions[i].address <= regions[i].bytes - length) {
            identifier = regions[i].identifier;
            break;
          }
        }
        if (identifier == UINT32_MAX) return AdrenoFirmwareUnmapped;
      }
      if (n == capacity) return AdrenoFirmwareNoSpace;
      if (pass) {
        blocks[n].address = address;
        blocks[n].bytes = length;
        blocks[n].type = le32(firmware + offset + 8);
        blocks[n].value = le32(firmware + offset + 12);
        blocks[n].payload_offset = offset + 16;
        blocks[n].region_identifier = identifier;
      }
      n++;
      offset += 16 + (size_t)length;
    }
    if (pass) *count = n;
  }
  return AdrenoFirmwareSuccess;
}
