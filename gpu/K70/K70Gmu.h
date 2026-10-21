/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef K70_GMU_H
#define K70_GMU_H
#include "AdrenoHfiStartup.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {K70GmuCore,K70GmuRscc,K70GmuPdc,K70GmuGpu,K70GmuCxMisc,K70GmuWindows};
typedef enum {
    K70GmuSuccess,K70GmuArgument,K70GmuFirmware,K70GmuBusy,
    K70GmuIoError,K70GmuStateMismatch,K70GmuTimeout,K70GmuClockError,
    K70GmuPoisoned,K70GmuFault,K70GmuProtocolError
} K70GmuStatus;
typedef struct {
    void *cookie;
    size_t bytes[K70GmuWindows];
    int (*read32)(void *,unsigned window,size_t byte_offset,uint32_t *);
    int (*write32)(void *,unsigned window,size_t byte_offset,uint32_t);
    int (*barrier)(void *);
} K70GmuIo;
typedef struct {
    void *cookie;
    int (*sha256)(void *,const void *,size_t,uint8_t digest[32]);
    int (*now_us)(void *,uint64_t *);
    int (*wait_us)(void *,unsigned);
} K70GmuServices;
typedef struct {volatile uint32_t *cpu;size_t bytes;uint64_t iova;} K70GmuMemory;
/* Private immutable qualification contract; no firmware bytes in Git.
 * Identity must come from this board's actual qcom,chipid or socinfo GPU part
 * identity, independently of the compatibility match. For gen7-2-1 the GMU
 * GENERAL10 value is ADRENO_GMU_REV(0x070201), i.e. 0x07020100; this revision
 * value is distinct from the actual SoC GPU part chip_id.
 * setup is derived from pinned public A740 RSC/PDC source and board resources.
 * Qualification must establish the exact full firmware digest/block layout.
 * A public reference image alone is not board qualification. */
typedef struct {unsigned window;size_t byte_offset;uint32_t value;} K70GmuSetupWrite;
typedef struct {
    uint32_t qualified,chip_id,gmu_chip_id,firmware_blocks;
    size_t firmware_bytes;uint8_t sha256[32];
    const K70GmuSetupWrite *setup;size_t setup_count;
} K70GmuProfile;
typedef struct {
    uint32_t chip_id;
    const K70GmuProfile *profile;
    const uint8_t *firmware;size_t firmware_bytes;
    K70GmuMemory icache,dummy,debug,log,hfi;
    unsigned poll_budget;
} K70GmuConfig;
typedef struct {
    K70GmuIo io;K70GmuServices services;AdrenoHfi hfi;
    uint32_t poisoned,boot_attempted;
    AdrenoHfiStartup startup;
} K70GmuDevice;
typedef struct {
    uint32_t phase,write_attempts,read_attempts,poll_reads;
    uint32_t complete_hash_verified,memory_changed,reset_verified;
    uint32_t firmware_init_result,hfi_status,firmware_ready_observed,hfi_ready_observed;
    uint32_t ao_fault_status,cm3_fault_status;
    uint32_t resources_must_retain;
    uint8_t complete_sha256[32];
} K70GmuResult;

/* Board-specific COLD start through firmware/HFI controller readiness.
 * This is NOT a power/clock, SMMU ownership or production boot entry point.
 * Caller must establish exclusive device/stream ownership; hold CX/GX,
 * core/hub clocks at the verified A740 boot rates, RPMh/interconnect votes,
 * fault interrupts and secure/platform prerequisites; install actual native4K
 * GMU SMMU mappings; prove coherent CPU/GMU visibility and pin all storage.
 * The GMU must already be held in reset and RSC idle, with retention cleared.
 * This function verifies those readable states before mutating anything.
 * It does not read silicon identity; chip_id must be from trusted discovery.
 * It validates the complete pinned firmware hash before register writes,
 * installs RSC/PDC setup and all payloads, verifies TCM payload readback,
 * initializes HFI memory, configures/releases CM3 and polls fresh FW/HFI
 * status with elapsed-time AND iteration bounds. No substituted ACK exists.
 * The required hash/time/wait services must perform real operations.
 * Memory extents are byte sizes, mapped with native4K pages, never 16K pages.
 * Config/input/device/result/IO/services and all storage must be disjoint,
 * immutable where applicable and retained for the device's lifetime.
 * Virtual extent checks do not prove physical ownership or alias isolation;
 * inspect actual CPU/GPU tables independently. A device allows one attempt.
 * Use ColdStart below to also exchange the modern HFI startup messages.
 * Debug draining, GPU submission and fault recovery are separate operations.
 * Any failure after mutation attempts reset assertion and poisons the session.
 * Even verified reset does NOT drain SMMU/bus traffic or authorize release;
 * resources_must_retain stays true on all paths. No automatic retry is safe. */
/* Result readiness fields are observations, never current-running assertions:
 * a later failure may already have reasserted reset. */
K70GmuStatus K70GmuLoadAndStartHfi(K70GmuDevice *,const K70GmuConfig *,K70GmuResult *);

typedef struct {
    unsigned polls_per_command,wait_interval_us;
    uint64_t command_timeout_us,total_timeout_us;
} K70GmuStartupLimits;
typedef struct {
    K70GmuResult load;
    AdrenoHfiStartupResult protocol;
    AdrenoHfiStartupStatus protocol_status;
    K70GmuStatus health_status;
} K70GmuStartupResult;
/* Full cold firmware/HFI startup sequence, under all LoadAndStartHfi platform
 * preconditions above. Validate protocol/limits before any hardware mutation,
 * then load firmware and exchange every required command with actual ACKs.
 * Health checks reread reset/HFI status and fatal registers around commands.
 * Supplied OPP/ARC/BCM/ACD data must be platform-derived; this function neither
 * invents command-db values nor sets power, clocks, SMMU or AOSS policy.
 * Any protocol failure after the load attempts reset and permanently poisons
 * the device. A verified reset still does not authorize releasing mappings.
 * Success covers firmware startup exchanges, NOT SQE/ZAP/GPU initialization,
 * shader execution, submission, presentation or a complete hardware driver. */
K70GmuStatus K70GmuColdStart(K70GmuDevice *,const K70GmuConfig *,
    const AdrenoHfiStartupConfig *,const K70GmuStartupLimits *,K70GmuStartupResult *);

typedef struct {volatile unsigned char *cpu;uint64_t physical;size_t bytes;} K70GmuMmioWindow;
typedef struct {K70GmuMmioWindow windows[K70GmuWindows];} K70GmuMmio;
/* Bind only already owned/powered Device mappings of exact pinned DT ranges.
 * Uses direct AArch64 volatile 32-bit I/O + dsb sy. No map/allocation/power,
 * pointer ownership proof or external-abort recovery is provided. */
K70GmuStatus K70GmuBindMmio(K70GmuMmio *,K70GmuIo *);
#ifdef __cplusplus
}
#endif
#endif
