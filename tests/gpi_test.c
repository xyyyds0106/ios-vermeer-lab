#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_gpi.h"
struct fixture {unsigned reads,fail,change;};
static int sample(void *ctx,unsigned offset,uint32_t *value)
{
    struct fixture *f=ctx;unsigned i=f->reads++;
    unsigned expected;assert(vgp_offset(i%32,&expected) && expected==offset);
    /* No writes API; test every single read failure in both passes. */
    if(i==f->fail)return 0;
    *value=offset^(i==f->change?0x100000u:0);return 1;
}
static void snapshots(void)
{
    unsigned seen[32],off;
    for(unsigned i=0;i<32;i++) {
        assert(vgp_offset(i,&off));seen[i]=off;
        for(unsigned j=0;j<i;j++)assert(off!=seen[j]);
        assert(off>=0x10000 && off<=0x13400);
    }
    assert(seen[0]==0x10000 && seen[9]==0x10080 && seen[18]==0x11000);
    assert(seen[8]==0x10060 && seen[17]==0x100e0 && seen[26]==0x13400);
    assert(!vgp_offset(32,&off) && !vgp_offset(~0u,&off) && !vgp_offset(0,NULL));
    struct vgp_snapshot s;struct fixture f={.fail=~0u,.change=~0u};
    vgp_collect(&f,sample,&s);assert(f.reads==64 && s.reason==VGP_STABLE && s.valid==UINT32_MAX && !s.changed);
    for(unsigned i=0;i<64;i++) {
        f=(struct fixture){.fail=i,.change=~0u};vgp_collect(&f,sample,&s);
        uint32_t valid=i<32?0:(UINT32_C(1)<<(i-32))-1;
        assert(f.reads==i+1 && s.reason==VGP_IO && s.valid==valid);
    }
    for(unsigned i=0;i<32;i++) {
        f=(struct fixture){.fail=~0u,.change=32+i};vgp_collect(&f,sample,&s);
        assert(s.reason==VGP_CHANGED && s.changed==(UINT32_C(1)<<i));
    }
    vgp_collect(NULL,NULL,&s);assert(s.reason==VGP_UNSAMPLED && !s.valid);
}
static void plans(void)
{
    struct vgp_spi_plan p;
    for(unsigned n=1;n<=1032;n++) {
        assert(vgp_spi_plan(&p,UINT64_C(0x123456780000),UINT64_C(0xabcdef120000),n,2,128));
        assert(p.tx_count==3 && p.rx_count==1 && sizeof(p.tx[0])==16);
        assert(p.tx[0].word[0]==0x03000004 && p.tx[0].word[2]==0x20080 && p.tx[0].word[3]==0x220001);
        assert(p.tx[1].word[0]==3 && p.tx[1].word[2]==n && p.tx[1].word[3]==0x200801);
        assert(p.tx[2].word[0]==0x56780000 && p.tx[2].word[1]==0x1234 && p.tx[2].word[2]==n && p.tx[2].word[3]==0x100200);
        assert(p.rx.word[0]==0xef120000 && p.rx.word[1]==0xabcd && p.rx.word[2]==n && p.rx.word[3]==0x100000);
    }
    const uint64_t pairs[][2]={{0,0x2000},{0x1000,0},{UINT64_MAX-5,0x2000},
        {0x1000,UINT64_MAX-5},{0x1000,0x1001},{0x1001,0x1000},{0x1000,0x1000}};
    struct vgp_spi_plan zero={0};
    for(unsigned i=0;i<sizeof(pairs)/sizeof(pairs[0]);i++) {
        memset(&p,0xff,sizeof(p));assert(!vgp_spi_plan(&p,pairs[i][0],pairs[i][1],8,0,1));
        assert(!memcmp(&p,&zero,sizeof(p)));
    }
    assert(!vgp_spi_plan(&p,0x1000,0x2000,0,0,1));
    assert(!vgp_spi_plan(&p,0x1000,0x2000,1033,0,1));
    assert(!vgp_spi_plan(&p,0x1000,0x2000,8,0,0));
    assert(!vgp_spi_plan(&p,0x1000,0x2000,8,0,0x1000));
    assert(!vgp_spi_plan(&p,0x1000,0x2000,8,8,1));
    assert(vgp_spi_plan(&p,0x1000,0x1008,8,7,0xfff));
}
int main(void)
{snapshots();plans();puts("GPI: fixed EE-relative read-only snapshots, failed/unstable reads and full-duplex TRE planning PASS");}
