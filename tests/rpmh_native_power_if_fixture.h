/* Test boundary only. The actual native object uses the touch owner's header.
 * Runner checks ABI declaration equality if the canonical header is present. */
#ifndef VERMEER_TOUCH_POWER_IF_H
#define VERMEER_TOUCH_POWER_IF_H
struct vtp_ops {
    void *ctx;
    int (*acquire)(void *);
    int (*valid)(void *);
};
int vermeer_touch_power_register(const struct vtp_ops *);
#endif
