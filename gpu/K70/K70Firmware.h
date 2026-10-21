/* SPDX-License-Identifier: MIT */
#ifndef K70_FIRMWARE_H
#define K70_FIRMWARE_H
#include "../NativeAdreno/NativeAdrenoFirmware.h"
struct K70FirmwareBundle {
    NativeAdrenoFirmwareView gmu,sqe,zap;
    NativeAdrenoFirmwareRules gmu_rules,sqe_rules,zap_rules;
};
/* Strong local assembly definitions from prepare_firmware.py. A missing
 * firmware object fails linking; no weak zero-filled default exists. Full
 * hashes are checked before giving a view. Byte identity alone is not board,
 * GMU protocol, SCM/PAS or hardware-execution qualification. */
IOReturn K70GpuOfficialFirmware(K70FirmwareBundle *);
#endif
