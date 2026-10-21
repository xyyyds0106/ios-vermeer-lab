#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_baseband_smem.h"

static void prepare(uint8_t *p,size_t size)
{
    memset(p,0,size);size_t toc=size-4096,part=256;
    memcpy(p+toc,"$TOC",4);vbb_put32(p+toc+4,1);vbb_put32(p+toc+8,1);
    vbb_put32(p+toc+32,(uint32_t)part);vbb_put32(p+toc+36,2048);
    vbb_put16(p+toc+46,1);vbb_put32(p+toc+48,32);
    memcpy(p+part,"$PRT",4);vbb_put16(p+part+6,1);vbb_put32(p+part+8,2048);
    vbb_put32(p+part+12,32+3*48);vbb_put32(p+part+16,2048);
    for(unsigned i=0;i<3;i++) {
        uint8_t *e=p+part+32+i*48;
        vbb_put16(e,0xa5a5);vbb_put16(e+2,(uint16_t)(478+i));vbb_put32(e+4,32);
    }
}
int main(void)
{
    uint8_t p[8192];struct vbb_smem s;
    prepare(p,sizeof(p));
    assert(vbb_smem_snapshot(p,sizeof(p),&s)==VBB_SMEM_OK);
    assert(s.partition_offset==256 && s.partition_size==2048 && s.entries==3);
    assert(s.descriptor.offset==304 && s.tx.offset==352 && s.rx.offset==400);
    assert(s.descriptor.size==32 && s.tx.size==32 && s.rx.size==32);
    vbb_put16(p+256+32+8,7);
    assert(vbb_smem_snapshot(p,sizeof(p),&s)==VBB_SMEM_OK && s.descriptor.size==25);
    vbb_put16(p+256+32+8,33);
    assert(vbb_smem_snapshot(p,sizeof(p),&s)==VBB_SMEM_BAD && !s.descriptor.size);
    prepare(p,sizeof(p));vbb_put32(p+256+16,1800);
    assert(vbb_smem_snapshot(p,sizeof(p),&s)==VBB_SMEM_CACHED);
    prepare(p,sizeof(p));vbb_put32(p+256+12,2049);
    assert(vbb_smem_snapshot(p,sizeof(p),&s)==VBB_SMEM_BAD);
    prepare(p,sizeof(p));vbb_put16(p+256+32+48+2,478);
    assert(vbb_smem_snapshot(p,sizeof(p),&s)==VBB_SMEM_BAD);
    prepare(p,sizeof(p));vbb_put16(p+4096+46,2);
    assert(vbb_smem_snapshot(p,sizeof(p),&s)==VBB_SMEM_NO_PARTITION);
    prepare(p,sizeof(p));vbb_put32(p+4096+8,2);
    memcpy(p+4096+80,p+4096+32,48);
    assert(vbb_smem_snapshot(p,sizeof(p),&s)==VBB_SMEM_BAD); /* overlapping partitions */
    prepare(p,sizeof(p));vbb_put32(p+4096+36,0xffffffff);
    assert(vbb_smem_snapshot(p,sizeof(p),&s)==VBB_SMEM_BAD);
    uint32_t rng=70;
    for(unsigned i=0;i<100000;i++) {
        prepare(p,sizeof(p));rng=rng*1664525u+1013904223u;
        size_t offset=rng%sizeof(p);rng=rng*1664525u+1013904223u;
        p[offset]^=(uint8_t)(1u<<(rng%8));
        int rc=vbb_smem_snapshot(p,sizeof(p),&s);
        if(rc==VBB_SMEM_OK || rc==VBB_SMEM_CACHED) {
            assert(s.partition_offset+s.partition_size<=sizeof(p)-4096);
            const struct vbb_smem_item items[]={s.descriptor,s.tx,s.rx};
            for(unsigned j=0;j<3;j++)if(items[j].size) {
                assert(items[j].offset>=s.partition_offset+32);
                assert(items[j].size<=s.partition_offset+s.partition_size-items[j].offset);
            }
        }
    }
    /* Small/truncated snapshots must fail before any partition-table access. */
    for(size_t n=0;n<8192;n+=17) {
        uint8_t *small=calloc(1,n?n:1);assert(small);
        assert(vbb_smem_snapshot(small,n,&s)==VBB_SMEM_BAD);free(small);
    }
    puts("Baseband SMEM snapshot bounds, item discovery and 100000 mutations: PASS");
    return 0;
}
