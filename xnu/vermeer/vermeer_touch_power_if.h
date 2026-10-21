/* SPDX-License-Identifier: MIT
 * In-kernel integration boundary for the K70 RPMh/reset implementation.
 * The provider owns real rail votes/reset sequencing and retains them through
 * reboot, including partial/failed acquisitions. No synthetic readiness flag.
 * Static immutable callbacks/ctx must remain pinned. No register/debug API.
 */
#ifndef VERMEER_TOUCH_POWER_IF_H
#define VERMEER_TOUCH_POWER_IF_H
struct vtp_ops {
    void *ctx;
    int (*acquire)(void *); /* actual IOVDD/AVDD votes, settle and reset release */
    int (*valid)(void *); /* live retained lease/fault/readiness; read-only, bounded, nonblocking */
};
int vermeer_touch_power_register(const struct vtp_ops *);
#endif
