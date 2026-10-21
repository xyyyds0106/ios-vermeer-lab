#define VERMEER_HOST_TEST 1
#define VL_RING_SIZE 32
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_log_ring.h"
#include "../xnu/vermeer/vermeer_usb_protocol.h"

int main(void)
{
    struct vl_ring r={0};char history[20000],out[64];uint32_t seed=123;
    for (unsigned i=0;i<sizeof(history);i++) history[i]=(char)(i*37);
    unsigned written=0;
    for (unsigned step=0;step<300;step++) {
        seed=seed*1664525u+1013904223u;
        unsigned n=seed%67;
        vl_append(&r,history+written,n);written+=n;
        assert(r.total==written);
        uint64_t pos=0;
        unsigned first=written>VL_RING_SIZE?written-VL_RING_SIZE:0;
        unsigned got=0;
        while ((n=vl_copy(&r,&pos,out,7))) {
            assert(n<=7 && got+n<=VL_RING_SIZE);
            assert(!memcmp(out,history+first+got,n));got+=n;
        }
        assert(got==written-first && pos==written);
        pos=UINT64_MAX;
        n=vl_copy(&r,&pos,out,sizeof(out));
        assert(n==written-first && !memcmp(out,history+first,n));
    }
    /* Parse complete CDC function and count both interfaces and all endpoints. */
    assert(sizeof(vusb_device)==18 && vusb_device[4]==2 && vusb_device[5]==2);
    assert(sizeof(vusb_config)==(vusb_config[2]|(vusb_config[3]<<8)));
    unsigned iface=0,eps=0,mask=0,unions=0;
    for (unsigned i=0;i<sizeof(vusb_config);i+=vusb_config[i]) {
        assert(vusb_config[i]>=2 && i+vusb_config[i]<=sizeof(vusb_config));
        if (vusb_config[i+1]==4) iface++;
        if (vusb_config[i+1]==5) {
            eps++;unsigned ep=vusb_config[i+2];
            assert(ep==1 || ep==0x81 || ep==0x82);
            unsigned bit=ep==1?1:ep==0x81?2:4;assert(!(mask&bit));mask|=bit;
        }
        if (vusb_config[i+1]==0x24 && vusb_config[i+2]==6) {
            unions++;assert(vusb_config[i+3]==0 && vusb_config[i+4]==1);
        }
    }
    assert(iface==2 && eps==3 && mask==7 && unions==1);
    assert(vusb_request(0x21,0x20,0,0,7)==VUSB_SET_LINE);
    for (unsigned i=0;i<65536;i++) if (i!=7) assert(vusb_request(0x21,0x20,0,0,i)==VUSB_BAD);
    assert(vusb_request(0x21,0x20,0,1,7)==VUSB_BAD);
    assert(vusb_request(0x80,6,0x100,0,65535)==VUSB_DESC);
    assert(vusb_request(0x80,6,0xff00,0,64)==VUSB_BAD);
    assert(vusb_request(0x21,0x22,3,0,0)==VUSB_CONTROL);
    assert(vusb_request(0x21,0x22,4,0,0)==VUSB_BAD);
    assert(vusb_request(0x40,1,0,0,0)==VUSB_BAD);
    assert(vusb_dma_allowed(0x8c0000000ULL,0x4000,36));
    assert(vusb_dma_allowed(0x93fffc000ULL,0x4000,36));
    assert(!vusb_dma_allowed(0x940000000ULL,0x4000,36));
    assert(!vusb_dma_allowed(0x8c0000000ULL,0x4000,32));
    assert(!vusb_dma_allowed(0x8c0000001ULL,0x4000,64));
    assert(!vusb_dma_allowed(UINT64_MAX-0x3fff,0x4000,64));
    assert(!vusb_dma_allowed(0xc7400000ULL,0x4000,64));
    assert(!vusb_dma_allowed(0x8c0000000ULL,0x8000,64));
    assert(vusb_stream_match(0x80000040));assert(!vusb_stream_match(0x80000060));
    assert(!vusb_stream_match(0x40));assert(vusb_stream_match(0x80010041));
    assert(vusb_route_identity(0x10000,0,1));
    assert(vusb_route_identity(0,0x10000,0));
    assert(!vusb_route_identity(0,0x10000,1));
    assert(!vusb_route_identity(0,0,0));assert(!vusb_route_identity(0x20000,0,0));
    puts("USB RAM ring, CDC descriptors, request bounds, high DMA and identity routing: PASS");
    return 0;
}
