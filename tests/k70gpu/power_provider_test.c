#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Reset only the private static instance between independent synthetic boots;
 * production exports no reset/retry entry. All public operations remain real. */
#include "../../gpu/K70/K70GpuPowerProvider.c"
static struct {
    uint8_t db[ADRENO_CMD_DB_BYTES];uint32_t regs[0x10000/4];
    struct K70GpuPowerPolicy policy;
    unsigned revision,proof,active,quarantine,prepared,triggered,ops,fail_at,fail_hit;
    unsigned no_irq,no_ack,stuck_readback,revoke_after_finish,domain_fail;
    unsigned revoke_at,time_calls,regress_at,jump_at;
    uint64_t now;
} hw;
static void p16(unsigned at,unsigned n){hw.db[at]=(uint8_t)n;hw.db[at+1]=(uint8_t)(n>>8);}
static void p32(unsigned at,uint32_t n){p16(at,n&65535);p16(at+2,n>>16);}
static unsigned first(void){return hw.revision==0x300?0x20:0x10;}
static unsigned msg(void){return hw.revision==0x300?0x34:0x30;}
static unsigned stride(void){return hw.revision==0x300?24:20;}
static int boundary(void){assert(!hw.fail_hit);++hw.ops;if(hw.revoke_at==hw.ops)hw.proof=0;
    if(hw.fail_at==hw.ops){hw.fail_hit=1;return 1;}return 0;}
static unsigned owns(void *p,uint64_t epoch){(void)p;assert(epoch==7);return hw.proof;}
static int now(void *p,uint64_t *out){(void)p;++hw.time_calls;
    if(hw.jump_at==hw.time_calls)hw.now+=200000;
    *out=hw.regress_at==hw.time_calls?0:++hw.now;return 0;}
static int read32(void *p,uint32_t off,uint32_t *out)
{
    (void)p;if(boundary())return 1;*out=hw.regs[off/4];
    if(hw.stuck_readback && hw.ops>20 && off==0xd00)*out^=1;
    return 0;
}
static int write32(void *p,uint32_t off,uint32_t value)
{
    (void)p;int fail=boundary();hw.regs[off/4]=value;
    if(off==0xd00+first()+4 && value==0x1010000) {
        assert(hw.prepared==0);++hw.triggered;
        unsigned mask=hw.regs[(0xd00+first()+12)/4];
        /* Actual encoded commands, not provider's internal command arrays. */
        unsigned count=0;for(unsigned i=0;i<16;i++)if(mask&(1u<<i)) {
            unsigned at=0xd00+msg()+stride()*i;
            assert(hw.regs[at/4]==0x10108);
            assert(hw.regs[(at+4)/4] && !(hw.regs[(at+4)/4]&3));
            hw.regs[(at+12)/4]=(hw.no_ack && !i)?0:0x10100;++count;
        }
        assert(count==7 || count==8);
        if(!hw.no_irq && (hw.regs[0xd00/4]&1))hw.regs[0xd04/4]|=1;
        hw.regs[(0xd00+first()+8)/4]=1;
    }
    if(off==0xd08)hw.regs[0xd04/4]&=~value;
    return fail;
}
static int wait_us(void *p,unsigned us){(void)p;if(boundary())return 1;hw.now+=us;return 0;}
static int prepare(void *p,const struct K70GpuPowerRequest *r)
{
    (void)p;assert(r->gpu_hz==220000000);assert(hw.triggered==1);
    assert(!hw.regs[0xd04/4] && !hw.regs[(0xd00+first()+12)/4]);
    ++hw.prepared;return hw.domain_fail;
}
static void finish(void *p,uint64_t epoch,unsigned mode)
{
    (void)p;assert(epoch==7);
    if(mode==K70PowerRetainActive){assert(hw.prepared==1);++hw.active;if(hw.revoke_after_finish)hw.proof=0;}
    else {assert(mode==K70PowerQuarantine);++hw.quarantine;hw.proof=0;}
}
static int begin(void *p,struct K70GpuPowerAccess *a)
{
    (void)p;*a=(struct K70GpuPowerAccess){.epoch=7,.cmd_db=hw.db,.cmd_db_bytes=sizeof(hw.db),
        .cmd_db_qualified_version=1,.rsc_revision=hw.revision,.policy=&hw.policy,.proof=owns,.now_us=now,
        .read32=read32,.write32=write32,.wait_us=wait_us,.prepare_domains=prepare,.finish=finish};return 0;
}
static struct K70GpuPowerRequest request={128,128,128,220000000,220000000,200000};
static struct K70GpuPowerProvider provider={0,begin};
static void fixture(unsigned gmxc,unsigned revision)
{
    memset(&power,0,sizeof(power));memset(&hw,0,sizeof(hw));hw.revision=revision;hw.proof=K70PowerRequired;
    p32(0,1);p32(4,0x0c0330db);
    p16(8,3);p16(10,0);p16(12,512);p16(14,gmxc?4:3);p16(16,0x100);
    p16(24,5);p16(26,128);p16(28,768);p16(30,4);p16(32,0x300);
    for(unsigned i=0;i<8;i++) {
        if(i==3 && !gmxc)continue;
        unsigned row=i>=4,index=row?i-4:i,at=144+(row?128:0)+24*index,aux=144+(row?768:512)+32*index;
        const char *name=AdrenoCmdDbResourceName(i);memcpy(hw.db+at,name,strlen(name));
        p32(at+16,(row?0x50000u:0x30000u)+16*i);p16(at+20,8);p16(at+22,32*index);
        if(row){p32(aux,1000);p16(aux+4,i==4?16:4);hw.db[aux+6]=i==6?2:1;}
        else{p16(aux,0);p16(aux+2,i==3?64:128);p16(aux+4,i==3?128:256);p16(aux+6,512);}
    }
    struct K70GpuPowerPolicy *p=&hw.policy;p->qualified=1;p->gpu_count=2;p->gmu_count=2;p->bandwidth_count=2;
    p->gpu_hz[1]=p->gmu_hz[1]=220000000;p->gpu_level[1]=p->gmu_level[1]=128;
    p->bandwidth_kbps[1]=200000;p->boot_mx_level=128;
    p->bcm[0].buswidth=16;p->bcm[1].buswidth=4;p->bcm[2].fixed=1;
    p->bcm[2].perfmode=8;p->bcm[2].perfmode_kbps=16500000;
    hw.regs[0]=(revision<<8)|2;hw.regs[12/4]=(16u<<27)|(8u<<12);
    for(unsigned i=0;i<3;++i)hw.regs[(0xd00+0x2a0*i+first()+8)/4]=1;
    assert(K70GpuPowerRegister(&provider)==0);
}
static int acquire(struct K70GpuPowerLease *l){return K70GpuPowerAcquire(&request,l);}
static void different_rails(unsigned gmxc)
{
    fixture(gmxc,0x300);
    request=(struct K70GpuPowerRequest){64,56,52,220000000,220000000,200000};
    hw.policy.gpu_level[1]=52;hw.policy.gmu_level[1]=64;hw.policy.boot_mx_level=56;
    p16(144+512+2,52);p16(144+512+32+2,64);
    p16(144+512+64+2,56);p16(144+512+64+4,60);p16(144+512+64+6,0);
    p16(144+512+96+2,16);p16(144+512+96+4,32);p16(144+512+96+6,0);
}
int main(void)
{
    struct K70GpuPowerLease l;const AdrenoHfiStartupConfig *c;unsigned perf,bw,total_ops=0;
    assert(K70GpuPowerAcquire(&request,&l)==K70PowerUnregistered);
    for(unsigned revision=0;revision<2;++revision)for(unsigned gmxc=0;gmxc<2;++gmxc) {
        fixture(gmxc,revision?0x300:0x207);assert(!acquire(&l));total_ops=hw.ops;
        assert(hw.prepared==1 && hw.active==1 && !hw.quarantine && hw.triggered==1);
        assert(power.result.commands==7+gmxc && power.result.acks==((1u<<(7+gmxc))-1));
        unsigned at=0xd00+msg();assert(hw.regs[(at+4)/4]==0x30020 && hw.regs[(at+8)/4]==1); /* MX index, not128 */
        at+=stride();assert(hw.regs[(at+4)/4]==0x30010 && hw.regs[(at+8)/4]==1);
        assert(!K70GpuPowerConfiguration(&l,&c,&perf,&bw));
        assert(perf==1 && bw==1 && c->gpu[1].vote==(gmxc?0x800201u:0x800101u) && c->gmu[1].vote==0x800101);
        if(gmxc){at+=stride();assert(hw.regs[(at+4)/4]==0x30030 && hw.regs[(at+8)/4]==2);}
        assert(c->ddr_addresses[0]==0x50040 && c->cnoc_addresses[0]==0x50070);
        /* Non-commit SH0 must not wait before MC0 commits their shared VCD. */
        assert(c->ddr_wait_mask==6 && c->ddr_data[1][0]==0x2fffffffu && c->ddr_data[1][1]==0x6fffffffu);
        unsigned ops=hw.ops;assert(!K70GpuPowerValidate(&l) && hw.ops==ops);
        assert(K70GpuPowerRegister(&provider)==K70PowerReused && acquire(&l)==K70PowerReused);
        /* Acquire deliberately clears an output lease on reuse; use saved identity. */
        l=(struct K70GpuPowerLease){7,hw.db,sizeof(hw.db),1};K70GpuPowerQuarantine(&l);
        assert(hw.quarantine==1 && K70GpuPowerValidate(&l));
    }
    for(unsigned fault=1;fault<=total_ops;++fault) {
        fixture(1,0x300);hw.fail_at=fault;assert(acquire(&l));
        assert(hw.fail_hit && hw.quarantine==1 && !hw.active && !hw.prepared && K70GpuPowerValidate(&l));
    }
    for(unsigned fault=1;fault<=total_ops;++fault) {
        fixture(1,0x300);hw.revoke_at=fault;assert(acquire(&l)==K70PowerProof);
        assert(hw.ops==fault && hw.quarantine==1 && !hw.active && !hw.prepared);
    }
    fixture(1,0x300);hw.no_irq=1;assert(acquire(&l)==K70PowerTimeout && hw.quarantine==1 && !hw.prepared);
    fixture(1,0x300);hw.no_ack=1;assert(acquire(&l)==K70PowerTimeout && hw.quarantine==1 && !hw.prepared);
    fixture(1,0x300);hw.proof&=~K70PowerOwnSmmu;assert(acquire(&l)==K70PowerProof && hw.ops==0);
    fixture(1,0x300);hw.policy.qualified=0;assert(acquire(&l)==K70PowerPolicy && hw.ops==0);
    fixture(1,0x300);hw.policy.gpu_level[1]=129;assert(acquire(&l)==K70PowerPolicy && hw.ops==0);
    fixture(1,0x300);hw.policy.bandwidth_kbps[1]=199999;assert(acquire(&l)==K70PowerPolicy && hw.ops==0);
    fixture(1,0x300);p16(144+512+4,64);assert(acquire(&l)==K70PowerPolicy && hw.ops==0);
    fixture(1,0x300);p32(4,0);assert(acquire(&l)==K70PowerDatabase && hw.ops==0);
    fixture(1,0x300);hw.domain_fail=1;assert(acquire(&l)==K70PowerDomain && hw.quarantine==1 && !hw.active);
    fixture(1,0x300);hw.revoke_after_finish=1;assert(acquire(&l)==K70PowerProof && hw.quarantine==1);
    fixture(1,0x300);hw.regs[(0xd00+first()+12)/4]=1;hw.regs[(0xd00+msg()+4)/4]=0x30020;
    assert(acquire(&l)==K70PowerConflict && !power.result.writes);
    fixture(1,0x300);hw.regs[0xd04/4]=7;assert(acquire(&l)==K70PowerNoSlot && !power.result.writes);
    fixture(1,0x300);hw.stuck_readback=1;assert(acquire(&l)==K70PowerVerify && hw.quarantine==1);
    fixture(1,0x300);hw.regress_at=3;assert(acquire(&l)==K70PowerClock && hw.ops==0);
    fixture(1,0x300);hw.jump_at=2;assert(acquire(&l)==K70PowerTimeout && hw.ops==0);
    /* Independent rail scales: primaryCX64 exceeds MX's maximum60. HFI's
     * legitimate secondary saturation needs MX index2; clock floor56 alone
     * would select index1. Acquisition merges actual MX indices, not levels.
     * GMxC also saturates at32 for primaryGFX52 when present. */
    for(unsigned gmxc=0;gmxc<2;++gmxc) {
        different_rails(gmxc);assert(!acquire(&l));
        unsigned at=0xd00+msg();assert(hw.regs[(at+4)/4]==0x30020 && hw.regs[(at+8)/4]==2);
        assert(!K70GpuPowerConfiguration(&l,&c,&perf,&bw));
        assert(c->gmu[1].vote==0x400201 && c->gpu[1].vote==(gmxc?0x340201u:0x340101u));
        if(gmxc){at+=2*stride();assert(hw.regs[(at+4)/4]==0x30030 && hw.regs[(at+8)/4]==2);}
    }
    different_rails(1);p16(144+512+64+2,48);p16(144+512+64+4,0);
    assert(acquire(&l)==K70PowerPolicy && hw.ops==0); /* actual MX cannot cover clock */
    different_rails(1);p16(144+512+32+2,60);p16(144+512+32+4,0);p16(144+512+32+6,0);
    assert(acquire(&l)==K70PowerPolicy && hw.ops==0); /* primaryCX cannot satisfy its OPP */
    request=(struct K70GpuPowerRequest){128,128,128,220000000,220000000,200000};
    puts("K70 real power provider: ARC/BCM wire votes, shared HFI config, 2.7/3.0, GMxC discovery, all I/O failures, timeout/conflict/quarantine PASS");
    return 0;
}
