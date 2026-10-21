/* Host allocation/cache boundaries only; all lifecycle/VM code is production. */
#include "NativeAdrenoMemory.h"
IOReturn NativeAdrenoQueryCpuNative(NativeAdrenoCpuState *s){*s={};s->kernel_page_bytes=16384;s->kernel_map_page_bytes=16384;return fixture.cpu_status;}
IOReturn NativeAdrenoQueryCpu4K(NativeAdrenoCpuState *s){return NativeAdrenoQueryCpuNative(s);}
IOReturn NativeAdrenoMaintainOwnedPages(void *,size_t,NativeAdrenoCacheOperation){++fixture.cache_calls;return fixture.cache_calls==fixture.cache_fail_call?kIOReturnIOError:0;}
IOReturn NativeAdrenoMaintainPhysicalPages(uint64_t,size_t,NativeAdrenoCacheOperation){return 0;}
IOReturn NativeAdrenoSetPagesCacheMode(const NativeAdrenoPhysicalSpan *,size_t,bool){return 0;}
IOReturn NativeAdrenoReassertWriteCombined(const NativeAdrenoPhysicalSpan *,size_t,uint32_t *n){*n=0;return 0;}
