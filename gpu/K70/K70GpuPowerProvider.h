/* SPDX-License-Identifier: MIT */
#ifndef K70_GPU_POWER_PROVIDER_H
#define K70_GPU_POWER_PROVIDER_H
#include "K70GpuPower.h"
#include "../AdrenoFirmware/AdrenoCmdDb.h"
#include "../AdrenoFirmware/AdrenoHfiVotes.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Kernel-internal platform boundary, not an application/DT wire format.
 * Board registration must follow exact live K70 FDT and speed-bin validation.
 * Include off=0 in each enabled table; never truncate an OPP table to fit.
 * BCM order is SH0, MC0, ACV, using the pinned target catalog's policy.
 * ACD/IFPC are disabled in this initial polling profile. The board must
 * establish the required AOSS notification/disabled state, not assume it. */
struct K70GpuPowerPolicy {
    unsigned qualified,gpu_count,gmu_count,bandwidth_count;
    uint32_t gpu_hz[16],gpu_level[16],gmu_hz[4],gmu_level[4],bandwidth_kbps[16];
    /* Independently qualified common-level clock floor. The executor ceils
     * this in actual MX aux and merges required GPU/GMU HFI dependency
     * indices in the SAME MX table; it never compares rail common numbers. */
    uint32_t boot_mx_level;
    struct {uint16_t buswidth;uint8_t fixed;uint16_t perfmode;uint32_t perfmode_kbps;} bcm[3];
};
enum {
    K70PowerOwnGpu=1u,K70PowerOwnGmu=2u,K70PowerOwnSmmu=4u,
    K70PowerOwnClocks=8u,K70PowerOwnPas=16u,K70PowerOwnMicrocode=32u,
    K70PowerOwnDrv2=64u,K70PowerHoldAwake=128u,K70PowerIrqMasked=256u,
    K70PowerBoardQualified=512u,K70PowerRequired=1023u
};
enum {K70PowerRetainActive=1,K70PowerQuarantine=2};
struct K70GpuPowerAccess {
    void *cookie;uint64_t epoch;
    const uint8_t *cmd_db;size_t cmd_db_bytes;
    unsigned cmd_db_qualified_version,rsc_revision;
    const struct K70GpuPowerPolicy *policy;
    /* proof is bounded, read-only and performs no MMIO. It checks the SAME
     * epoch, actual exclusive GPU/GMU/SMMU/clock/PAS/carveout reservations,
     * DRV2 transaction lock, PM hold and permanent GIC IRQ mask ownership.
     * After finish(RETAIN), DRV2 means retained ACTIVE ownership; another
     * client's serialized transaction must not revoke the GPU's ACTIVE vote. */
    unsigned (*proof)(void *,uint64_t);
    int (*now_us)(void *,uint64_t *);
    /* Only DRV2-relative 32-bit accesses; offset range 0..0xffff. Callbacks
     * return zero on success and are ordered and software-bounded. Ordinary
     * native MMIO is not exception-contained: a dead bus can still abort/hang. */
    int (*read32)(void *,uint32_t,uint32_t *);
    int (*write32)(void *,uint32_t,uint32_t);
    int (*wait_us)(void *,unsigned);
    /* AFTER acknowledged ARC/BCM votes, establish actual GPUCC/GCC initial
     * access and AOSS prerequisites before clock-controller register I/O.
     * This is a real board operation, not a cached vote-ACK readiness flag.
     * Native ClockStart subsequently powers CX GDSC and verifies PWR_ON;
     * only after that may native code read GMU/GPU registers.
     * Do not turn GX on: the native driver checks GX off, then uses GMU HFI
     * and a fresh OOB ACK to transition it. */
    int (*prepare_domains)(void *,const struct K70GpuPowerRequest *);
    /* RETAIN releases only DRV2's transaction lock after real idle/ACK cleanup.
     * QUARANTINE permanently retains all uncertain votes/mappings/ownership.
     * Must work even when proof has just failed. No rollback/unvote/reset. */
    void (*finish)(void *,uint64_t,unsigned);
};
struct K70GpuPowerProvider {
    void *cookie;
    /* Obtain owned DRV2 AND whole GPU platform resources. Provide a wired,
     * immutable two-pass checked full cmd-db copy from THIS boot and a pinned
     * policy. Success=0. Failure must retain any indeterminate acquisitions.
     * Registration/begin are serialized kernel-worker operations. */
    int (*begin)(void *,struct K70GpuPowerAccess *);
};
enum {
    K70PowerOK,K70PowerArgument,K70PowerUnregistered,K70PowerReused,K70PowerBegin,
    K70PowerProof,K70PowerClock,K70PowerTimeout,K70PowerDatabase,K70PowerPolicy,
    K70PowerLayout,K70PowerConflict,K70PowerNoSlot,K70PowerIo,K70PowerVerify,
    K70PowerDomain,K70PowerRetired
};
struct K70GpuPowerResult {
    unsigned status,attempted,touched,retained,selected,ncpt,commands,acks,reads,writes;
    unsigned phase;uint32_t last_offset,last_value,irq,idle,command_status[8];
};
/* Strong implementation is provided by K70GpuPowerProvider.c. There is no
 * default provider or fallback success. Register once; failed acquisition is
 * single-attempt and retained until reboot. No release/retry API exists. */
int K70GpuPowerRegister(const struct K70GpuPowerProvider *);
const struct K70GpuPowerResult *K70GpuPowerDiagnostics(void);
/* Only valid after Acquire; returns the SAME verified policy/cmd-db-derived
 * HFI tables and selected indices used by the actual CPU power transaction.
 * Native integration must use these tables, not a separately guessed config. */
int K70GpuPowerConfiguration(const struct K70GpuPowerLease *,
    const AdrenoHfiStartupConfig **,unsigned *,unsigned *);
#ifdef __cplusplus
}
#endif
#endif
