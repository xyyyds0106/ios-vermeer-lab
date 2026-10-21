/* SPDX-License-Identifier: MIT. Kernel-only, pinned board driver interfaces. */
#ifndef VERMEER_RPMH_BOARD_H
#define VERMEER_RPMH_BOARD_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Thread context, before vermeer_touch_start. Input is a wired immutable copy
 * of THIS boot's Linux FDT. No physical pointers, host dump or arbitrary DT.
 * Requires vermeer_rpmh=1 and the separately reviewed PM hook candidate.
 * Registers the static vtp_ops; acquire executes on the touch worker later. */
int vermeer_rpmh_touch_start(const void *,size_t);
/* Optional loader bridge: /chosen/vermeer,boot-fdt contains the complete
 * copied FDT bytes, not a PA. Absence fails before mapping or device I/O. */
int vermeer_rpmh_touch_start_from_adt(void);
enum { VRB_TOUCH=1,VRB_GPU=2,VRB_MEDIA=3 };
enum { VRB_CLEAN=0,VRB_RETAIN_ACTIVE=1,VRB_UNCERTAIN=2 };
struct vrb_controller_lease {uint64_t epoch;uint32_t client;};
/* All native DRV2 clients AND any RPMh IRQ consumer must use this owner.
 * Nonblocking try-claim: a busy/quarantined controller is never stolen.
 * Polling-only IRQ coordination: GIC INTID37 must be disabled/inactive; the
 * provider checks it read-only and refuses an enabled or active IRQ. No IRQ
 * registration/global masking/clear/reroute is performed. Native clients may
 * not enable this IRQ while this permanent polling owner exists.
 * CLEAN/RETAIN release the transaction lock; awake hold remains until reboot.
 * UNCERTAIN keeps lock, epoch, mappings and PM hold until reboot. */
int vermeer_rpmh_controller_claim(unsigned,struct vrb_controller_lease *);
int vermeer_rpmh_controller_valid(const struct vrb_controller_lease *);
void vermeer_rpmh_controller_finish(const struct vrb_controller_lease *,unsigned);
/* Retained ACTIVE ownership survives releasing the transaction lock and
 * other clients' transactions. Suitable for GPU Validate's no-MMIO boundary.
 * Quarantine invalidates all client leases and permanently blocks new I/O. */
int vermeer_rpmh_active_valid(const struct vrb_controller_lease *);
void vermeer_rpmh_active_quarantine(const struct vrb_controller_lease *);
/* Used by the independent CPU PM hook. No allocation/MMIO/blocking. */
int vermeer_rpmh_pm_inhibited(void);
/* Strong symbol supplied by the CPU PM hook candidate, never a weak stub. */
unsigned vermeer_rpmh_pm_hook_version(void);
int vermeer_rpmh_system_idle_hold(void);
#ifdef __cplusplus
}
#endif
#endif
