/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ADRENO_HFI_STARTUP_H
#define ADRENO_HFI_STARTUP_H
#include "AdrenoHfi.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Modern A6xx/A7xx wire profile. Legacy and A8xx table formats are excluded. */
typedef struct {uint64_t hz;uint32_t vote;} AdrenoHfiPerformanceLevel;
typedef struct {
    unsigned gpu_count,gmu_count;
    AdrenoHfiPerformanceLevel gpu[16],gmu[4];
    unsigned bandwidth_count,cnoc_count,ddr_count;
    uint32_t cnoc_wait_mask,ddr_wait_mask;
    uint32_t cnoc_addresses[6],cnoc_data[2][6];
    uint32_t ddr_addresses[8],ddr_data[16][8];
    uint32_t acd_version,acd_enable_mask,acd_stride,acd_count,acd_data[32];
    unsigned enable_ifpc;
} AdrenoHfiStartupConfig;
typedef enum {
    AdrenoHfiStageNone,AdrenoHfiStagePerformance,AdrenoHfiStageBandwidth,
    AdrenoHfiStageAcdEnable,AdrenoHfiStageAcdTable,AdrenoHfiStageIfpc,
    AdrenoHfiStageCoreStart,AdrenoHfiStageStart,AdrenoHfiStageComplete
} AdrenoHfiStartupStage;
typedef enum {
    AdrenoHfiStartupSuccess,AdrenoHfiStartupInvalid,AdrenoHfiStartupBusy,
    AdrenoHfiStartupPoisoned,AdrenoHfiStartupClockError,AdrenoHfiStartupWaitError,
    AdrenoHfiStartupTimeout,AdrenoHfiStartupPlatformFault,AdrenoHfiStartupTransportError
} AdrenoHfiStartupStatus;
typedef struct {
    void *cookie;
    int (*now_us)(void *,uint64_t *); /* read-only current monotonic time */
    int (*wait_us)(void *,unsigned);
    int (*check_health)(void *); /* read-only real platform fault/ownership checks */
    unsigned polls_per_command,wait_interval_us;
    uint64_t command_timeout_us,total_timeout_us;
} AdrenoHfiStartupServices;
typedef struct {unsigned attempted,poisoned,completed;} AdrenoHfiStartup;
typedef struct {
    AdrenoHfiStartupStage stage;
    AdrenoHfiStatus transport_status;
    uint32_t firmware_error,wire_header,acknowledged_stages;
    unsigned published_commands,poll_reads,resources_must_retain;
    uint64_t elapsed_us;
} AdrenoHfiStartupResult;
/* Validate every input before any queue publication. Frequencies include the
 * zero/off entry, are strictly ascending, exactly representable in wire kHz.
 * Votes and command-db addresses are trusted platform inputs, never guessed
 * by this library. ACD entries are packed for each enabled GPU level; AOSS
 * notification and IFPC power prerequisites must already be established.
 * Unused fixed-capacity slots must be zero. This prevents silently ignoring
 * unsupported or malformed configuration. No input is authenticated here. */
AdrenoHfiStartupStatus AdrenoHfiStartupValidate(const AdrenoHfiStartupConfig *);
AdrenoHfiStartupStatus AdrenoHfiStartupValidateServices(const AdrenoHfiStartupServices *);
/* Payload excludes header. All output storage must be disjoint from inputs.
 * Encodes one selected stage; unused optional stages refuse. Failure leaves
 * payload/header outputs untouched except word_count=0. Capacity >=159. */
AdrenoHfiStartupStatus AdrenoHfiStartupEncode(const AdrenoHfiStartupConfig *,
    AdrenoHfiStartupStage,uint32_t *id,uint32_t *payload,size_t capacity,size_t *word_count);
/* Single attempt, serialized, caller-owned zero-initialized state. Requires
 * a real ready GMU, exclusive coherent HFI allocation and active native4K GMU
 * mappings. Only matching actual ACKs advance the startup sequence. All
 * commands and the whole sequence have monotonic-time AND iteration bounds.
 * Callback operations must themselves be bounded and must not re-enter this
 * API or mutate protocol state. Health must check a live exclusive platform
 * lease retained through each transport operation, not a cached approval.
 * Time and live phase are checked after callbacks and before final success.
 * On any attempted-sequence
 * failure the transport is poisoned; no replay, synthetic ACK or implicit
 * stop occurs. Even success does not authorize resource release or prove GPU
 * execution, power/coherence/SMMU readiness, shader submission or a desktop.
 * Input, state, services, transport, result and shared queues must remain
 * disjoint and pinned; config remains immutable throughout the sequence. */
AdrenoHfiStartupStatus AdrenoHfiStartupRun(AdrenoHfiStartup *,AdrenoHfi *,
    const AdrenoHfiStartupConfig *,const AdrenoHfiStartupServices *,AdrenoHfiStartupResult *);
#ifdef __cplusplus
}
#endif
#endif
