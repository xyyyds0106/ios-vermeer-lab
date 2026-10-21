/* SPDX-License-Identifier: MIT */
#ifndef K70_GPU_CLOCK_H
#define K70_GPU_CLOCK_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {K70ClockGcc,K70ClockGpucc};
struct K70ClockIo {
    void *cookie;
    int (*lease)(void *);
    int (*now_us)(void *,uint64_t *);
    int (*wait_us)(void *,unsigned);
    int (*read32)(void *,unsigned,size_t,uint32_t *);
    int (*write32)(void *,unsigned,size_t,uint32_t);
    int (*barrier)(void *);
};
struct K70ClockState {unsigned used,poisoned,ready,phase;uint64_t began,last;};
/* Exclusive GPU clock ownership and completed CX/MX power votes required.
 * Real Kalama LUCID_OLE PLL1 + GMU220MHz/HUB200MHz boot setup. No GX claim.
 * Retain all votes/mappings on every path; a timeout never authorizes undo.
 * No repeated attempt, guessed frequency fallback or cached-ready success. */
int K70GpuClockStart(struct K70ClockState *,const struct K70ClockIo *);
#ifdef __cplusplus
}
#endif
#endif
