/* SPDX-License-Identifier: MIT */
#include "K70GpuPowerProvider.h"
/* Pinned MiCode 53ef33ea rpmh-rsc.c: DRV2 tcs-offset0xd00, three ACTIVE
 * TCSs, 0x2a0 stride, qualified revisions2.7/3.0 only. ARC sends the aux
 * table index (rpmh-regulator.c/rpmhpd.c), never the common level or mV.
 * BCM data and HFI tables share AdrenoHfiVotes' target-qualified arithmetic.
 * No GPU, GMU, GDSC, SCM, clock or IRQ-controller MMIO is issued here. */
static struct {
    unsigned registration,attempt,active,retired;
    struct K70GpuPowerProvider provider;
    struct K70GpuPowerAccess access;
    struct AdrenoCmdDb db;
    AdrenoHfiStartupConfig startup;
    struct K70GpuPowerResult result;
    uint32_t address[8],data[8],wait_mask,irq_original;
    unsigned first,stride,msg,perf_index,bandwidth_index;
    uint64_t began,last;
} power;
static int error(unsigned status){power.result.status=status;return (int)status;}
static int proof(void)
{
    return power.access.epoch && power.access.proof &&
        (power.access.proof(power.access.cookie,power.access.epoch)&K70PowerRequired)==K70PowerRequired;
}
static int live(void)
{
    uint64_t now;
    if(!proof())return error(K70PowerProof);
    if(power.access.now_us(power.access.cookie,&now) || now<power.last)return error(K70PowerClock);
    power.last=now;
    if(!proof())return error(K70PowerProof);
    return now-power.began>=200000?error(K70PowerTimeout):0;
}
static int rd(uint32_t off,uint32_t *v)
{
    if(!v || (off&3u) || off>0xfffc)return error(K70PowerArgument);
    if(live())return (int)power.result.status;
    power.result.last_offset=off;++power.result.reads;
    if(power.access.read32(power.access.cookie,off,v))return error(K70PowerIo);
    power.result.last_value=*v;return live();
}
static int allowed(uint32_t off,uint32_t value)
{
    unsigned slot=power.result.selected;
    if(slot>=3)return 0;
    uint32_t base=0xd00+0x2a0*slot,mask=(1u<<power.result.commands)-1u;
    if(off==0xd00)return 1; /* only selected bit modified by caller */
    if(off==0xd08)return power.result.phase==4 && value==(1u<<slot);
    if(off==base+power.first)return !value || value==power.wait_mask;
    if(off==base+power.first+4)return !value || value==0x10000 || value==0x1010000;
    if(off==base+power.first+12)return !value || value==mask;
    for(unsigned i=0;i<power.result.commands;++i) {
        uint32_t at=base+power.msg+power.stride*i;
        if(off==at)return value==0x10108;
        if(off==at+4)return value==power.address[i];
        if(off==at+8)return value==power.data[i];
    }
    return 0;
}
static int wr(uint32_t off,uint32_t value)
{
    if(!allowed(off,value))return error(K70PowerArgument);
    if(live())return (int)power.result.status;
    power.result.touched=1;power.result.last_offset=off;power.result.last_value=value;++power.result.writes;
    if(power.access.write32(power.access.cookie,off,value))return error(K70PowerIo);
    return live();
}
static int sync(uint32_t off,uint32_t value)
{
    if(wr(off,value))return (int)power.result.status;
    for(unsigned i=0;i<128;++i){uint32_t v;if(rd(off,&v))return (int)power.result.status;if(v==value)return 0;}
    return error(K70PowerVerify);
}
static int arc_index(const struct AdrenoCmdDbResource *r,uint32_t level,uint32_t *out)
{
    uint32_t levels[2]={0,level},votes[2];
    if(!level || AdrenoHfiArcVotes(levels,2,r->aux,r->aux_bytes,r->aux,r->aux_bytes,votes,2))return 1;
    *out=votes[1]&255u;return !*out;
}
static int append(uint32_t address,uint32_t data,unsigned wait)
{
    unsigned n=power.result.commands;
    if(n>=8)return error(K70PowerPolicy);
    for(unsigned i=0;i<n;++i)if(power.address[i]==address)return error(K70PowerPolicy);
    power.address[n]=address;power.data[n]=data;
    if(wait)power.wait_mask|=1u<<n;
    ++power.result.commands;return 0;
}
static int build(const struct K70GpuPowerRequest *r)
{
    const struct K70GpuPowerPolicy *p=power.access.policy;
    struct AdrenoCmdDb *db=&power.db;AdrenoHfiStartupConfig *c=&power.startup;
    if(AdrenoCmdDbParse(power.access.cmd_db,power.access.cmd_db_bytes,
        power.access.cmd_db_qualified_version,db)!=AdrenoCmdDbOK || !db->complete)return error(K70PowerDatabase);
    if(!p || p->qualified!=1 || p->gpu_count<2 || p->gpu_count>16 || p->gmu_count<2 || p->gmu_count>4 ||
       p->bandwidth_count<2 || p->bandwidth_count>16 || p->gpu_level[0] || p->gmu_level[0] ||
       r->mx_level!=p->boot_mx_level || r->gmu_hz!=220000000 || r->gpu_hz!=220000000)
        return error(K70PowerPolicy);
    unsigned gpu=0,gmu=0,bw=0;
    for(unsigned i=0;i<16;++i) {
        if(i<p->gpu_count){c->gpu[i].hz=p->gpu_hz[i];if(i && p->gpu_hz[i]==r->gpu_hz)gpu=i;}
        else if(p->gpu_hz[i] || p->gpu_level[i])return error(K70PowerPolicy);
        if(i<p->bandwidth_count){if(i && p->bandwidth_kbps[i]==r->bandwidth_kbps)bw=i;}
        else if(p->bandwidth_kbps[i])return error(K70PowerPolicy);
    }
    for(unsigned i=0;i<4;++i) {
        if(i<p->gmu_count){c->gmu[i].hz=p->gmu_hz[i];if(i && p->gmu_hz[i]==r->gmu_hz)gmu=i;}
        else if(p->gmu_hz[i] || p->gmu_level[i])return error(K70PowerPolicy);
    }
    if(!gpu || !gmu || !bw || !r->gfx_level || !r->cx_level || !r->mx_level || r->gfx_level!=p->gpu_level[gpu] ||
       r->cx_level!=p->gmu_level[gmu])
        return error(K70PowerPolicy);
    c->gpu_count=p->gpu_count;c->gmu_count=p->gmu_count;
    unsigned secondary=(db->found_mask&(1u<<AdrenoCmdDbGmxc))?AdrenoCmdDbGmxc:AdrenoCmdDbMx;
    const struct AdrenoCmdDbResource *gfx=&db->resource[AdrenoCmdDbGfx],*cx=&db->resource[AdrenoCmdDbCx],
        *mx=&db->resource[AdrenoCmdDbMx],*sec=&db->resource[secondary];
    uint32_t votes[16],index;
    if(AdrenoHfiArcVotes(p->gpu_level,p->gpu_count,gfx->aux,gfx->aux_bytes,sec->aux,sec->aux_bytes,votes,16))
        return error(K70PowerPolicy);
    for(unsigned i=0;i<p->gpu_count;++i)c->gpu[i].vote=votes[i];
    if(AdrenoHfiArcVotes(p->gmu_level,p->gmu_count,cx->aux,cx->aux_bytes,mx->aux,mx->aux_bytes,votes,16))
        return error(K70PowerPolicy);
    for(unsigned i=0;i<p->gmu_count;++i)c->gmu[i].vote=votes[i];
    AdrenoHfiBcm bcms[3]={0};
    for(unsigned i=0;i<3;++i) {
        const struct AdrenoCmdDbResource *entry=&db->resource[AdrenoCmdDbSh0+i];
        bcms[i].address=entry->address;bcms[i].buswidth=p->bcm[i].buswidth;bcms[i].fixed=p->bcm[i].fixed;
        bcms[i].perfmode=p->bcm[i].perfmode;bcms[i].perfmode_kbps=p->bcm[i].perfmode_kbps;
        if(AdrenoHfiBcmAux(&bcms[i],entry->aux,entry->aux_bytes))return error(K70PowerPolicy);
    }
    if(AdrenoHfiBuildBandwidth(p->bandwidth_kbps,p->bandwidth_count,bcms,3,
        db->resource[AdrenoCmdDbCn0].address,c) || AdrenoHfiStartupValidate(c))return error(K70PowerPolicy);
    power.perf_index=gpu;power.bandwidth_index=bw;
    /* Raise dependencies first. ACTIVE-only CPU votes stay held across HFI
     * boot and GX/OOB; HFI votes from the GMU do not remove this CPU floor. */
    /* MX's common levels need not equal the primary CX/GFX levels. First
     * ceil the qualified clock floor in MX's own table, then merge indices
     * in THAT table with actual HFI dependency indices. Linux a6xx_gmu's
     * dep_votes_init separately ceils the GPU level in MX; its secondary
     * ARC builder may saturate when a primary level exceeds secondary max.
     * Comparing common numbers across rails would wrongly reject that case. */
    uint32_t dep_index;
    if(arc_index(mx,r->mx_level,&index) || arc_index(mx,r->gfx_level,&dep_index))return error(K70PowerPolicy);
    if(index<dep_index)index=dep_index;
    dep_index=(c->gmu[gmu].vote>>8)&255u;
    if(index<dep_index)index=dep_index;
    if(secondary==AdrenoCmdDbMx) {
        dep_index=(c->gpu[gpu].vote>>8)&255u;
        if(index<dep_index)index=dep_index;
    }
    if(index>=mx->aux_bytes/2 || !(mx->aux[2*index] | mx->aux[2*index+1]) || append(mx->address,index,1))
        return error(K70PowerPolicy);
    if(arc_index(cx,r->cx_level,&index) || append(cx->address,index,1))return error(K70PowerPolicy);
    if(secondary==AdrenoCmdDbGmxc) {
        index=(c->gpu[gpu].vote>>8)&255u;
        if(!index || index>=sec->aux_bytes/2 || !(sec->aux[2*index] | sec->aux[2*index+1]) ||
           append(sec->address,index,1))return error(K70PowerPolicy);
    }
    if(arc_index(gfx,r->gfx_level,&index) || append(gfx->address,index,1) ||
        append(c->cnoc_addresses[0],c->cnoc_data[1][0],1))return error(K70PowerPolicy);
    for(unsigned i=0;i<3;++i)if(append(c->ddr_addresses[i],c->ddr_data[bw][i],(c->ddr_wait_mask>>i)&1u))
        return (int)power.result.status;
    return 0;
}
static int empty(unsigned slot,unsigned *out)
{
    uint32_t base=0xd00+0x2a0*slot,wait,control,idle,enable,irq;
    if(rd(base+power.first,&wait) || rd(base+power.first+4,&control) || rd(base+power.first+8,&idle) ||
       rd(base+power.first+12,&enable) || rd(0xd04,&irq))return (int)power.result.status;
    if(idle==UINT32_MAX || irq==UINT32_MAX || (enable>>power.result.ncpt))return error(K70PowerLayout);
    for(unsigned i=0;i<power.result.ncpt;++i)if(enable&(1u<<i)) {
        uint32_t address;if(rd(base+power.msg+power.stride*i+4,&address))return (int)power.result.status;
        for(unsigned j=0;j<power.result.commands;++j)if(address==power.address[j])return error(K70PowerConflict);
    }
    *out=idle && !wait && !control && !enable && !(irq&(1u<<slot));return 0;
}
static int execute(void)
{
    uint32_t id,solver,config;
    power.result.phase=1;
    if(rd(0,&id) || rd(4,&solver) || rd(12,&config))return (int)power.result.status;
    power.result.ncpt=(config>>27)&31u;
    if(((id>>8)&0xffff)!=power.access.rsc_revision || config==UINT32_MAX || solver==UINT32_MAX ||
       (solver&(1u<<24)) || ((config>>12)&63u)<8 || ((config>>12)&63u)>32 ||
       power.result.ncpt<power.result.commands || power.result.ncpt>16)return error(K70PowerLayout);
    power.first=power.access.rsc_revision==0x300?0x20:0x10;
    power.stride=power.access.rsc_revision==0x300?24:20;
    power.msg=power.access.rsc_revision==0x300?0x34:0x30;
    power.result.selected=UINT32_MAX;
    for(unsigned i=0;i<3;++i){unsigned free;if(empty(i,&free))return (int)power.result.status;
        if(free && power.result.selected==UINT32_MAX)power.result.selected=i;}
    if(power.result.selected==UINT32_MAX)return error(K70PowerNoSlot);
    uint32_t base=0xd00+0x2a0*power.result.selected,bit=1u<<power.result.selected,
        mask=(1u<<power.result.commands)-1u;unsigned free;
    if(rd(0xd00,&power.irq_original))return (int)power.result.status;
    if(power.irq_original==UINT32_MAX)return error(K70PowerLayout);
    if(empty(power.result.selected,&free))return (int)power.result.status;
    if(!free)return error(K70PowerConflict);
    if(sync(0xd00,power.irq_original|bit) || empty(power.result.selected,&free))return (int)power.result.status;
    if(!free)return error(K70PowerConflict);
    power.result.phase=2;
    for(unsigned i=0;i<power.result.commands;++i){uint32_t at=base+power.msg+power.stride*i;
        if(sync(at,0x10108) || sync(at+4,power.address[i]) || sync(at+8,power.data[i]))return (int)power.result.status;}
    if(sync(base+power.first,power.wait_mask) || sync(base+power.first+12,mask) || sync(base+power.first+4,0) ||
       sync(base+power.first+4,0x10000) || wr(base+power.first+4,0x1010000))return (int)power.result.status;
    power.result.phase=3;unsigned done=0;
    for(unsigned poll=0;poll<100000;++poll) {
        if(rd(0xd04,&power.result.irq) || rd(base+power.first+8,&power.result.idle))return (int)power.result.status;
        if(power.result.irq==UINT32_MAX || power.result.idle==UINT32_MAX)return error(K70PowerIo);
        unsigned acks=0;
        for(unsigned i=0;i<power.result.commands;++i) {
            uint32_t status;if(rd(base+power.msg+power.stride*i+12,&status))return (int)power.result.status;
            power.result.command_status[i]=status;
            if(status!=UINT32_MAX && (status&0x10100u)==0x10100u)acks|=1u<<i;
        }
        power.result.acks=acks;
        if((power.result.irq&bit) && power.result.idle && acks==mask){done=1;break;}
        if(live())return (int)power.result.status;
        if(power.access.wait_us(power.access.cookie,1))return error(K70PowerIo);
        if(live())return (int)power.result.status;
    }
    if(!done)return error(K70PowerTimeout);
    power.result.phase=4;
    if(sync(base+power.first+4,0x10000) || sync(base+power.first+4,0) || sync(base+power.first+12,0) ||
       sync(base+power.first,0) || wr(0xd08,bit))return (int)power.result.status;
    uint32_t irq,enabled;
    if(rd(0xd04,&irq) || rd(0xd00,&enabled))return (int)power.result.status;
    if((irq&bit) || enabled==UINT32_MAX)return error(K70PowerVerify);
    return sync(0xd00,(enabled&~bit)|(power.irq_original&bit));
}
int K70GpuPowerRegister(const struct K70GpuPowerProvider *p)
{
    if(!p || !p->begin)return K70PowerArgument;
    unsigned zero=0;
    if(!__atomic_compare_exchange_n(&power.registration,&zero,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return K70PowerReused;
    power.provider=*p;__atomic_store_n(&power.registration,2,__ATOMIC_RELEASE);return 0;
}
int K70GpuPowerAcquire(const struct K70GpuPowerRequest *request,struct K70GpuPowerLease *lease)
{
    if(!request || !lease)return K70PowerArgument;
    *lease=(struct K70GpuPowerLease){0};
    if(__atomic_load_n(&power.registration,__ATOMIC_ACQUIRE)!=2)return K70PowerUnregistered;
    unsigned zero=0;
    if(!__atomic_compare_exchange_n(&power.attempt,&zero,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return K70PowerReused;
    power.result.attempted=1;
    if(power.provider.begin(power.provider.cookie,&power.access))return error(K70PowerBegin);
    struct K70GpuPowerAccess *a=&power.access;
    *lease=(struct K70GpuPowerLease){a->epoch,a->cmd_db,a->cmd_db_bytes,a->cmd_db_qualified_version};
    if(!a->epoch || !a->cmd_db || !a->proof || !a->now_us || !a->read32 || !a->write32 || !a->wait_us ||
       !a->prepare_domains || !a->finish || (a->rsc_revision!=0x207 && a->rsc_revision!=0x300))error(K70PowerArgument);
    else if(!proof())error(K70PowerProof);
    else if(a->now_us(a->cookie,&power.began))error(K70PowerClock);
    else {
        power.last=power.began;
        if(!live() && !build(request) && !live() && !execute() && !live()) {
            power.result.phase=5;
            if(a->prepare_domains(a->cookie,request))error(K70PowerDomain);
            else if(!live()) {
                a->finish(a->cookie,a->epoch,K70PowerRetainActive);
                if(proof()) {
                    power.result.retained=1;power.result.phase=6;
                    __atomic_store_n(&power.active,1,__ATOMIC_RELEASE);return 0;
                }
                error(K70PowerProof);
            }
        }
    }
    __atomic_store_n(&power.retired,1,__ATOMIC_RELEASE);
    if(a->finish)a->finish(a->cookie,a->epoch,K70PowerQuarantine);
    return (int)power.result.status;
}
int K70GpuPowerValidate(const struct K70GpuPowerLease *lease)
{
    if(!lease || !__atomic_load_n(&power.active,__ATOMIC_ACQUIRE) || __atomic_load_n(&power.retired,__ATOMIC_ACQUIRE) ||
       lease->epoch!=power.access.epoch || lease->cmd_db!=power.access.cmd_db || lease->cmd_db_bytes!=power.access.cmd_db_bytes ||
       lease->cmd_db_qualified_version!=power.access.cmd_db_qualified_version)return K70PowerRetired;
    return proof()?0:K70PowerProof;
}
void K70GpuPowerQuarantine(const struct K70GpuPowerLease *lease)
{
    if(!lease || !__atomic_load_n(&power.attempt,__ATOMIC_ACQUIRE) || lease->epoch!=power.access.epoch)return;
    __atomic_store_n(&power.retired,1,__ATOMIC_RELEASE);
    if(power.access.finish)power.access.finish(power.access.cookie,power.access.epoch,K70PowerQuarantine);
}
int K70GpuPowerConfiguration(const struct K70GpuPowerLease *lease,const AdrenoHfiStartupConfig **out,unsigned *perf,unsigned *bw)
{
    if(!out || !perf || !bw)return K70PowerArgument;
    *out=0;
    int status=K70GpuPowerValidate(lease);if(status)return status;
    *out=&power.startup;*perf=power.perf_index;*bw=power.bandwidth_index;return 0;
}
const struct K70GpuPowerResult *K70GpuPowerDiagnostics(void){return &power.result;}
