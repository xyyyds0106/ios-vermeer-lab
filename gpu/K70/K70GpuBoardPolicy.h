/* SPDX-License-Identifier: MIT */
#ifndef K70_GPU_BOARD_POLICY_H
#define K70_GPU_BOARD_POLICY_H
#include "K70GpuPowerProvider.h"
#include "K70GpuSocInfo.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Trusted board-worker inputs, independently obtained during THIS boot.
 * Decode a stable SMEM item137 copy with K70GpuSocInfoDecode. A GMU catalog
 * revision is not an actual GPU chip ID. Neither compatible nor a saved DT
 * selects a speed bin. The owner must prove the fuse/firmware-selected bin
 * and its SKU code. Zero/unknown qualification always refuses. The pinned
 * GMU220MHz and PLL1 at440MHz need MX LOW_SVS_D1=56. The real power provider
 * must merge it with HFI dependency indices from actual command-db aux.
 * This parser does not authenticate these external proofs itself. */
struct K70GpuBoardQualification {
    unsigned socinfo_qualified,speed_bin_qualified;
    struct K70GpuSocInfo soc;
    uint32_t speed_bin,sku_code;
};
struct K70GpuBoardPolicy {
    struct K70GpuPowerPolicy power;
    struct K70GpuPowerRequest request;
    uint32_t chip_id,speed_bin,sku_code,smmu_actlr;
    uint64_t zap_base,zap_bytes;
    /* GPUCC has an additional mxc.lvl supply; it is not gmxc.lvl.
     * A validated DT relationship does not establish its active voltage.
     * The board/clock owner must qualify any needed MXC vote separately. */
    uint32_t mxc_supply_present;
};
enum {K70BoardOK,K70BoardArgument,K70BoardFdt,K70BoardContract,K70BoardStatus,
      K70BoardChip,K70BoardSpeedBin,K70BoardSku,K70BoardOpp,K70BoardDependency};
/* Pure bounded parser, no mapping/MMIO/allocation. The caller supplies a
 * wired immutable copy of the CURRENT boot FDT and disjoint output/proofs.
 * Exact fixed-board resource relationships are checked against reviewed K70
 * facts; OPP/bandwidth values are read from the input, never copied from the
 * saved host DT. Full selected-bin tables are retained, including off=0.
 * Success only qualifies data: no device ownership, power or AOSS state is
 * proven. The power owner must still establish all native provider proofs.
 * ACD/IFPC remain disabled and require real board/AOSS qualification.
 * Failure preserves caller output. */
int K70GpuBoardPolicyBuild(const uint8_t *,size_t,const struct K70GpuBoardQualification *,
    struct K70GpuBoardPolicy *);
#ifdef __cplusplus
}
#endif
#endif
