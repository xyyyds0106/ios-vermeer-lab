/* SPDX-License-Identifier: MIT */
#ifndef K70_GPU_POWER_H
#define K70_GPU_POWER_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Implemented by the native RPMh/platform owner, not by a weak GPU stub.
 * Levels are actual K70 enabled-OPP RPMh levels, never mV or table indices.
 * GMU HFI tables additionally need gfx.lvl/mx.lvl/cx.lvl/[gmxc.lvl] ARC aux
 * and SH0/MC0/ACV/CN0 BCM addresses+aux from the complete qualified cmd-db.
 * Keep ACTIVE votes, the controller ownership and suspend/idle inhibitor
 * while GMU/GPU rely on them; no guessed SLEEP/WAKE TCS or rollback. */
struct K70GpuPowerRequest {
    uint32_t cx_level,mx_level,gfx_level;
    uint32_t gmu_hz,gpu_hz,bandwidth_kbps;
};
struct K70GpuPowerLease {
    uint64_t epoch;
    const uint8_t *cmd_db;size_t cmd_db_bytes;
    uint32_t cmd_db_qualified_version;
};
/* Return 0 only after real completed votes/readiness under an exclusive GPU,
 * GMU and GPU-SMMU platform lease. On failure retain indeterminate resources.
 * Acquire must not claim GX active merely because the CX vote completed: the
 * GPU driver performs real GMU HFI performance/OOB and checks GX power bits.
 * Validate is bounded/no MMIO and checks the epoch/idle/power ownership.
 * Quarantine never releases uncertain resources or writes a guessed reset. */
int K70GpuPowerAcquire(const struct K70GpuPowerRequest *,struct K70GpuPowerLease *);
int K70GpuPowerValidate(const struct K70GpuPowerLease *);
void K70GpuPowerQuarantine(const struct K70GpuPowerLease *);
#ifdef __cplusplus
}
#endif
#endif
