/* SPDX-License-Identifier: MIT */
#ifndef K70_ZAP_H
#define K70_ZAP_H
#include "K70Gpu.h"
struct K70ZapServices {
    void *cookie;
    int (*lease)(void *);
    int (*now_us)(void *,uint64_t *);
    int (*wait_us)(void *,unsigned);
};
struct K70ZapResult {
    unsigned used,stage,authenticated,resources_must_retain;
    int64_t smc_status[3];uint64_t pas_status[3];
    unsigned resumes,busy_retries;
};
/* Real PAS13 INIT_IMAGE/MEM_SETUP/AUTH_AND_RESET. Requires exclusive SCM/PAS
 * and K70 reserved GPU microcode region0x9b09a000+0x2000 under the platform
 * lease. Official SoC firmware is a candidate; only TZ success authenticates.
 * No cache-attribute change of the neighbouring part of a native16K page.
 * One attempt, resources retained on every path, no PAS shutdown guess. */
class K70Zap final {
    NativeAdrenoMemory metadata_;K70ZapResult *r_=nullptr;K70ZapServices io_{};
    uint64_t began_=0,last_=0;bool failed_=false;
    int guard();
    int smc(unsigned command,uint64_t arginfo,uint64_t a,uint64_t b,uint64_t c,unsigned index);
public:
    K70Zap()=default;K70Zap(const K70Zap &)=delete;K70Zap &operator=(const K70Zap &)=delete;
    IOReturn run(const uint8_t *,size_t,const NativeAdrenoFirmwareRules *,const K70ZapServices *,K70ZapResult *);
};
#endif
