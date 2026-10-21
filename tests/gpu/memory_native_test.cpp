/* SPDX-License-Identifier: MIT
 * Executes the production allocation/lifetime code with injected VM failures.
 * ARM cache instructions and actual DMA require separate on-device validation.
 */
#include "NativeAdrenoMemory.h"
#include "NativeAdrenoGranules.h"
#include <cassert>
#include <cstdio>
#include <string>

static std::vector<NativeAdrenoPhysicalSpan> attributes;
static std::vector<bool> attribute_modes;
IOReturn NativeAdrenoQueryCpuNative(NativeAdrenoCpuState *out) {
    *out={};out->kernel_page_bytes=out->kernel_map_page_bytes=fixture.page_bytes;
    return fixture.cpu_status;
}
IOReturn NativeAdrenoMaintainOwnedPages(void *p,size_t n,NativeAdrenoCacheOperation op) {
    assert(p && n && !(((uintptr_t)p|n)&4095));
    bool owned=false;
    for(auto *d:fixture.live)if((uintptr_t)p>=(uintptr_t)d->raw &&
        (uintptr_t)p-(uintptr_t)d->raw<=d->bytes && n<=d->bytes-((uintptr_t)p-(uintptr_t)d->raw))owned=true;
    assert(owned);fixture.cache_operations.push_back(op);
    return ++fixture.cache_calls==fixture.cache_fail_call?kIOReturnIOError:kIOReturnSuccess;
}
IOReturn NativeAdrenoMaintainPhysicalPages(uint64_t p,size_t n,NativeAdrenoCacheOperation) {
    assert(p && n && !((p|n)&4095));++fixture.physical_calls;fixture.physical_bytes+=n;return 0;
}
IOReturn NativeAdrenoSetPagesCacheMode(const NativeAdrenoPhysicalSpan *spans,size_t count,bool wc) {
    assert(spans && count);
    for(size_t i=0;i<count;i++) {
        assert(spans[i].bytes && !((spans[i].physical|spans[i].bytes)&(fixture.page_bytes-1)));
        attributes.push_back(spans[i]);attribute_modes.push_back(wc);
    }
    return 0;
}
IOReturn NativeAdrenoReassertWriteCombined(const NativeAdrenoPhysicalSpan *s,size_t n,uint32_t *reset) {
    assert(reset);*reset=0;return NativeAdrenoSetPagesCacheMode(s,n,true);
}
static void reset(size_t page) {
    assert(fixture.live.empty() && fixture.metadata.empty() && !fixture.imports_live);
    fixture=MemoryFixture{};fixture.page_bytes=page;attributes.clear();attribute_modes.clear();
}
static void discardQuarantinedFixture() {
    // Test process only; production intentionally retains uncertain allocations.
    while(!fixture.live.empty())fixture.live.back()->release();
    while(!fixture.metadata.empty())fixtureMetadataFree(fixture.metadata.back());
}
static void layouts() {
    for(size_t page:{4096,16384}) {
        uint64_t tcr=page==4096?(UINT64_C(2)<<30):((UINT64_C(1)<<30)|(2<<14));
        assert(NativeAdrenoCpuGranuleValid(page,page,tcr,1,0x80000000));
        assert(!NativeAdrenoCpuGranuleValid(page,page,tcr,0,0x80000000));
        assert(!NativeAdrenoCpuGranuleValid(page,page,tcr,1,0));
        assert(!NativeAdrenoCpuGranuleValid(page,page,tcr^(1<<14),1,0x80000000));
        for(size_t n=4096;n<=1024*1024;n+=4096) {
            size_t owned=0;
            assert(NativeAdrenoAllocationLayout(n,n,page,&owned));
            assert(owned>=n && owned-n<page && !(owned&(page-1)));
        }
    }
    size_t owned=99;
    assert(!NativeAdrenoAllocationLayout(0,4096,16384,&owned) && !owned);
    assert(!NativeAdrenoAllocationLayout(4097,8192,16384,&owned));
    assert(!NativeAdrenoAllocationLayout(8192,4096,16384,&owned));
    assert(!NativeAdrenoAllocationLayout(4096,4096,65536,&owned));
}
static void allocationMatrix() {
    for(size_t page:{4096,16384})for(size_t n:{4096,8192,12288,16384,20480,65536})
    for(unsigned bits:{32,36,40,42,44,48})for(auto mode:{Contiguous,ScatterUnlessContiguous}) {
        reset(page);fixture.mode=mode;NativeAdrenoMemory m;
        assert(m.allocate(n,bits,mode==Contiguous,n)==0);
        assert(m.bytes()==n && m.ownedBytes()==((n+page-1)&~(page-1)));
        size_t exposed=0;
        for(size_t i=0;i<m.spanCount();i++) {
            assert(m.spans()[i].offset==exposed);exposed+=m.spans()[i].bytes;
        }
        assert(exposed==n);
        for(size_t i=0;i<m.ownedBytes();i++)assert(!((unsigned char*)m.mapping())[i]);
        assert(m.makeWriteCombined()==0);
        size_t changed=0;for(auto s:attributes)changed+=s.bytes;
        assert(changed==m.ownedBytes());
        assert(m.publishForDevice(n,4096,true)==kIOReturnBadArgument); // hidden padding
        assert(m.holdForDevice()==0 && m.release()==kIOReturnBusy);
        assert(m.allocate(n,bits,false,n)==kIOReturnBusy);
        assert(m.retireAfterQuiescence()==0 && m.release()==0);
        assert(!attribute_modes.back() && !m.backed() && !m.ownedBytes());
    }
    reset(16384);
}
static void failures() {
    for(size_t page:{4096,16384})for(unsigned fault=0;fault<11;fault++) {
        reset(page);NativeAdrenoMemory m;
        if(fault==0)fixture.allocation_fail=true;
        if(fault==1)fixture.prepare_fail=true;
        if(fault==2)fixture.null_mapping=true;
        if(fault==3)fixture.unaligned_mapping=true;
        if(fault==4)fixture.wrong_length=true;
        if(fault==5)fixture.metadata_fail=true;
        if(fault>=6 && fault<=9)fixture.mode=(SegmentMode)(ZeroPhysical+fault-6);
        if(fault==10)fixture.cache_fail_call=1;
        assert(m.allocate(2*page,32,true,2*page)!=0);
        assert(!m.backed() && fixture.live.empty() && fixture.metadata.empty());
    }
    reset(16384);NativeAdrenoMemory m;
    assert(m.allocate(4096,40,false,4096)==0);
    fixture.complete_fail=true;
    assert(m.release()!=0 && m.quarantined() && m.backed() && !fixture.released);
    assert(m.release()==kIOReturnBusy && m.holdForDevice()==kIOReturnNotReady);
    discardQuarantinedFixture();reset(16384);
}
static void imports() {
    for(size_t page:{4096,16384}) {
        reset(page);NativeAdrenoMemory m;
        assert(m.importTask((void*)2,0x40001000,12288,40,12288)==0);
        assert(m.bytes()==12288 && m.ownedBytes()==0 && !m.mapping());
        assert(m.makeWriteCombined()==kIOReturnNotReady && attributes.empty());
        assert(m.allocate(4096,40,false,4096)==kIOReturnBusy);
        assert(m.publishForDevice(4096,4096,false)==0 && fixture.physical_bytes==4096);
        assert(m.release()==0);
        fixture.write_protected_imports=true;
        assert(m.importTask((void*)2,0x40001000,12288,40,12288)==0 && m.readOnly());
        assert(m.publishForDevice(0,4096,false)==0);
        unsigned calls=fixture.physical_calls;
        assert(m.publishForDevice(0,4096,true)==kIOReturnNotPermitted);
        assert(m.acquireDeviceWrites(0,4096)==kIOReturnNotPermitted);
        assert(fixture.physical_calls==calls && m.release()==0);
    }
    reset(16384);
}
static void poolTest() {
    reset(16384);
    assert(NativeAdrenoMemory::reservePool(40,65536)==0);
    NativeAdrenoMemory live;
    assert(live.allocate(4096,40,false,4096,true)==0);
    unsigned calls=fixture.cache_calls;
    assert(NativeAdrenoMemory::writeCombinePool()==kIOReturnBusy);
    assert(fixture.cache_calls==calls && attributes.empty());
    assert(live.release()==0);
    assert(NativeAdrenoMemory::writeCombinePool()==0);
    NativeAdrenoMemory a,b,c;
    assert(a.allocate(4096,40,false,4096,true)==0);
    assert(b.allocate(4096,40,false,4096,true)==0);
    assert(b.spans()[0].physical-a.spans()[0].physical==16384);
    auto pa=a.spans()[0].physical;
    assert(a.bytes()==4096 && a.ownedBytes()==16384 && a.spans()[0].bytes==4096);
    assert(a.makeWriteCombined()==0 && a.release()==0);
    assert(c.allocate(12288,40,false,12288,true)==0 && c.spans()[0].physical==pa);
    assert(b.release()==0 && c.release()==0 && fixture.metadata.empty());
    // Pool has permanent kernel lifetime; release test doubles only at process exit.
    discardQuarantinedFixture();
}
static void poolQuarantine() {
    reset(16384);fixture.wrong_length=true;fixture.complete_fail=true;
    assert(NativeAdrenoMemory::reservePool(40,65536)!=0 && !fixture.released);
    assert(NativeAdrenoMemory::reservePool(40,65536)==kIOReturnBusy);
    assert(NativeAdrenoMemory::writeCombinePool()==kIOReturnNotReady);
    discardQuarantinedFixture();
}
int main(int argc,char **argv) {
    if(argc==2 && std::string(argv[1])=="pool-quarantine")poolQuarantine();
    else {layouts();allocationMatrix();failures();imports();poolTest();}
    std::puts("PASS: native CPU pages, GPU spans, cache ownership, imports and retirement");
}
