/* SPDX-License-Identifier: MIT */
#ifndef K70_GMU_NATIVE_H
#define K70_GMU_NATIVE_H
#include "K70Gpu.h"
#include "K70Gmu.h"
#include "../NativeAdreno/NativeAdrenoGmuContext.h"
struct K70GmuNativeConfig {
    void *cookie;
    int (*lease)(void *);
    int (*now_us)(void *,uint64_t *);
    int (*wait_us)(void *,unsigned);
    K70GpuWindow windows[5],smmu;
    unsigned bank,asid,slot;
    uint32_t chip_id;
    const K70GmuProfile *profile;
    const uint8_t *firmware;size_t firmware_bytes;
    const AdrenoHfiStartupConfig *startup;
    uint32_t perf_index,bandwidth_index;
    uint64_t total_timeout_us;
};
struct K70GmuNativeResult {
    unsigned stage,ready,resources_must_retain;
    IOReturn status;
    NativeAdrenoGmuContextReport context;
    K70GmuStartupResult startup;
    uint32_t perf_firmware_error,oob_polls,gx_status;
    uint64_t elapsed_us;
};
/* Caller already holds the native platform power/clock and SMMU ownership
 * lease. Owns real allocations, GMU table programming, firmware upload, HFI
 * messages, a bounded performance vote, fresh OOB ACK and GX state. No weak
 * symbols, default firmware, default rail table or fabricated ACK. */
class K70GmuNative final {
    K70GmuNativeConfig c_{};K70GmuNativeResult *r_=nullptr;
    NativeAdrenoGmuContext context_;K70GmuDevice d_{};
    uint64_t start_=0,last_=0;bool used_=false,failed_=false;
    int guard();IOReturn fail(IOReturn);
    static int read(void *,unsigned,size_t,uint32_t *);
    static int write(void *,unsigned,size_t,uint32_t);
    static int barrier(void *);
    static int clock(void *,uint64_t *);
    static int wait(void *,unsigned);
    static int hash(void *,const void *,size_t,uint8_t *);
    static int sr32(void *,size_t,uint32_t *);
    static int sr64(void *,size_t,uint64_t *);
    static int sw32(void *,size_t,uint32_t);
    static int sw64(void *,size_t,uint64_t);
    int coreRead(uint32_t,uint32_t *);
    int coreWrite(uint32_t,uint32_t);
    int health();
public:
    K70GmuNative()=default;K70GmuNative(const K70GmuNative &)=delete;
    K70GmuNative &operator=(const K70GmuNative &)=delete;
    IOReturn run(const K70GmuNativeConfig *,K70GmuNativeResult *);
};
#endif
