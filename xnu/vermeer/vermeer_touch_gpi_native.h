/* SPDX-License-Identifier: MIT
 * Private include of vermeer_touch.c: real XNU allocation/cache/MMIO binding.
 * Retained kernel mappings/pages, only fixed GPII0 and SIDb6 register writes.
 */
#ifndef VERMEER_TOUCH_GPI_NATIVE_H
#define VERMEER_TOUCH_GPI_NATIVE_H
static const struct vtp_ops *_Atomic vtg_power_provider;
static unsigned _Atomic vtg_owner;
static vm_offset_t vtg_gic;
static unsigned vtg_irq_retired;
static struct vgpt_irq_observation vtg_irq_last;
static int vtg_irq_read(void *ctx,unsigned off,uint32_t *out)
{
    (void)ctx;if(!vtg_gic || !out || (off&3) || off>=0x10000)return 0;
    *out=vt_rd(vtg_gic,off);return 1;
}
static int vtg_irqs_ready(void)
{
    if(vtg_irq_retired || !vtg_gic || !touch.power_held || !touch.power ||
       touch.power->valid(touch.power->ctx)!=1)return 0;
    /* That registered board lease exists only after checking this boot's
     * full fixed FDT IRQ contract, not merely the saved host reference. */
    unsigned bank=70;
    if(touch.identity_mapping && !((touch.identity_route.s2cr>>16)&3))bank=touch.identity_route.s2cr&255;
    else if(touch.iommu.scan_phase>=4)bank=touch.iommu.bank;
    if(vgpt_irqs_masked(NULL,vtg_irq_read,bank,&vtg_irq_last))return 1;
    vtg_irq_retired=1;
    printf("VERMEER TOUCH IRQ: refused source=%u INTID=%u GICD-offset=%x enabled=%08x active=%08x; no mask/clear/route writes\n",
        vtg_irq_last.source,vtg_irq_last.intid,vtg_irq_last.offset,vtg_irq_last.enabled,vtg_irq_last.active);
    return 0;
}
int vermeer_touch_power_register(const struct vtp_ops *ops)
{
    if(!ops || !ops->acquire || !ops->valid || os_atomic_load(&vtg_owner,acquire))return 0;
    return os_atomic_cmpxchg(&vtg_power_provider,(const struct vtp_ops *)NULL,ops,release);
}
static void vtg_status(void)
{
    if(touch.transport.verify_mask)printf("VERMEER GPI READBACK: reg=%05x expected=%08x observed=%08x mask=%08x; DMA refused\n",
        touch.transport.verify_register,touch.transport.verify_expected,touch.transport.verify_observed,touch.transport.verify_mask);
    os_atomic_store(&touch.gpi_phase,touch.transport.phase,relaxed);
    os_atomic_store(&touch.gpi_error,touch.transport.error,relaxed);
    os_atomic_store(&touch.gpi_events,touch.transport.events,relaxed);
    os_atomic_store(&touch.gpi_commands,touch.transport.commands,relaxed);
    os_atomic_store(&touch.gpi_retired,touch.transport.quarantined,relaxed);
    os_atomic_store(&touch.iommu_error,touch.iommu.error,relaxed);
    os_atomic_store(&touch.iommu_mode,touch.identity_mapping?1:touch.iommu.attached?2:0,relaxed);
    unsigned bank=touch.identity_mapping?(((touch.identity_route.s2cr>>16)&3)?UINT32_MAX:
        touch.identity_route.s2cr&255):touch.iommu.attached?touch.iommu.bank:UINT32_MAX;
    os_atomic_store(&touch.iommu_bank,bank,relaxed);
    os_atomic_store(&touch.iommu_slot,touch.identity_mapping?touch.identity_route.slot:
        touch.iommu.attached?touch.iommu.slot:UINT32_MAX,relaxed);
    os_atomic_store(&touch.gpi_last_channel,touch.transport.last_channel,relaxed);
    os_atomic_store(&touch.gpi_last_code,touch.transport.last_code,relaxed);
    os_atomic_store(&touch.gpi_last_status,touch.transport.last_status,relaxed);
    os_atomic_store(&touch.gpi_last_length,touch.transport.last_length,relaxed);
}
static int vtg_clock_ready(void)
{
    return touch.gcc && vtc_ready(vt_rd(touch.gcc,VT_VOTE),vt_rd(touch.gcc,vt_halt[0]),
        vt_rd(touch.gcc,vt_halt[1]),vt_rd(touch.gcc,vt_halt[2]),vt_rd(touch.gcc,vt_halt[3]),
        vt_rd(touch.gcc,vt_halt[4]));
}
static int vtg_pages(void)
{
    if(PAGE_SIZE!=16384 || !touch.dma_va || (touch.dma_va&PAGE_MASK) ||
       (touch.dma_pa&PAGE_MASK) || touch.dma_pa<UINT64_C(0x8c0000000) ||
       touch.dma_pa>UINT64_C(0x940000000)-6*PAGE_SIZE)return 0;
    for(unsigned i=0;i<6;i++) {
        ppnum_t pn=pmap_find_phys(kernel_pmap,(addr64_t)(touch.dma_va+i*PAGE_SIZE));
        if(!pn || ptoa_64(pn)!=touch.dma_pa+i*PAGE_SIZE)return 0;
    }
    return 1;
}
static int vtg_now(void *ctx,uint64_t *out)
{(void)ctx;uint64_t ns;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);*out=ns/NSEC_PER_USEC;return 1;}
static int vtg_pause(void *ctx,unsigned us)
{(void)ctx;if(us!=20)return 0;delay(us);return 1;}
static int vtg_dfs_owned(void *ctx)
{
    (void)ctx;return touch.gpi_claimed && touch.power_held && touch.power &&
        touch.power->valid(touch.power->ctx)==1 && vtg_clock_ready() && vtg_irqs_ready();
}
static int vtg_dfs_read(void *ctx,unsigned off,uint32_t *out)
{
    (void)ctx;if(!out || (off!=0x14 && (off<0x1c || off>0x38 || (off&3))) || !vtg_dfs_owned(NULL))return 0;
    *out=vt_rd(touch.gcc,VT_RCG+off);return 1;
}
static int vtg_dfs_valid(void)
{
    if(!touch.dfs_ready)return 0;
    uint32_t control,selected;
    if(!vtg_dfs_read(NULL,0x14,&control) || !vtg_dfs_read(NULL,0x1c+4*touch.gpi_clock.level,&selected))return 0;
    return vtc_dfs_live(&touch.dfs,&touch.gpi_clock,control,selected);
}
static int vtg_dfs_prepare(void)
{
    printf("VERMEER TOUCH PHASE: AP DFS snapshot begin; no serial clock-selector read\n");
    if(touch.manual_mode==2)vt_sleep(200);
    const struct vtc_dfs_ops io={NULL,vtg_dfs_owned,vtg_dfs_read,vtg_now};
    int status=vtc_dfs_collect(&io,&touch.dfs);
    if(status){printf("VERMEER TOUCH CLOCK: DFS snapshot refused status=%d\n",status);return 0;}
    printf("VERMEER TOUCH CLOCK: DFS control=%08x cfg=%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x\n",
        touch.dfs.control,touch.dfs.cfg[0],touch.dfs.cfg[1],touch.dfs.cfg[2],touch.dfs.cfg[3],
        touch.dfs.cfg[4],touch.dfs.cfg[5],touch.dfs.cfg[6],touch.dfs.cfg[7]);
    if(!vtc_dfs_choose(&touch.dfs,&touch.gpi_clock)) {
        printf("VERMEER TOUCH CLOCK: no qualified XO DFS entry; no guessed selector or PLL change\n");return 0;
    }
    touch.dfs_ready=true;
    printf("VERMEER TOUCH PHASE: AP DFS qualified level=%u source-hz=%u divider=%u spi-hz=%u\n",
        touch.gpi_clock.level,touch.gpi_clock.source_hz,touch.gpi_clock.divider,touch.gpi_clock.spi_hz);
    return vtg_dfs_valid();
}
static int vtg_cache(void *ctx,void *cpu,size_t bytes,unsigned op)
{
    (void)ctx;uintptr_t p=(uintptr_t)cpu;
    if(!vtg_pages() || bytes!=PAGE_SIZE || op>2 || (p&PAGE_MASK) ||
       p<touch.dma_va || p-touch.dma_va>=6*PAGE_SIZE)return 0;
    disable_preemption();uint64_t ctr;__asm__ volatile("mrs %0, ctr_el0":"=r"(ctr));
    size_t line=(size_t)4<<((ctr>>16)&15);
    if(line>4096){enable_preemption();return 0;}
    __asm__ volatile("dsb sy":::"memory");
    for(uintptr_t at=p;at<p+bytes;at+=line) {
        if(op==VGPT_CLEAN)__asm__ volatile("dc cvac, %0"::"r"(at):"memory");
        else if(op==VGPT_CLEAN_INVALIDATE)__asm__ volatile("dc civac, %0"::"r"(at):"memory");
        else __asm__ volatile("dc ivac, %0"::"r"(at):"memory");
    }
    __asm__ volatile("dsb sy":::"memory");enable_preemption();return 1;
}
static int vtg_power_valid(void)
{
    return (!touch.power_held || (touch.power && touch.power->valid(touch.power->ctx)==1)) && vt_reset_ready();
}
static int vtg_quiet(void *ctx)
{
    (void)ctx;
    if(!touch.gpi || !touch.se || !vtg_clock_ready() || !vtg_power_valid() || !vtg_irqs_ready() ||
       (touch.dfs_ready && !vtg_dfs_valid()) || (touch.dma_va && !vtg_pages()) || (vt_rd(touch.se,VG_STATUS)&0x1001))return 0;
    /* Pinned provider assigns Apps EE static mask1 + dynamic mask1e.
     * max-num-gpii=12 is NOT permission to read other EEs' instances5..11.
     * All five assigned instances must be unallocated with NO retained rings.
     * Native XNU has one provider owner; no other driver may allocate here. */
    unsigned ee=vt_rd(touch.gpi,0x10000)&0xf0;
    for(unsigned n=0;n<5;n++)for(unsigned c=0;c<3;c++) {
        unsigned off=(c==2?0x11000:0x10000+c*0x80)+0x4000*n;
        uint32_t cfg=vt_rd(touch.gpi,off);
        if((cfg&0xf0)!=ee || (cfg&~0x1ff0u))return 0;
        for(unsigned field=4;field<=0x1c;field+=4)if(vt_rd(touch.gpi,off+field))return 0;
    }
    return 1;
}
static int vtg_sread32(void *ctx,unsigned off,uint32_t *out)
{
    (void)ctx;if(!touch.smmu || !out || (off&3) || off>=0xd0000 || !vtg_clock_ready() || !vtg_pages())return 0;
    *out=vt_rd(touch.smmu,off);return 1;
}
static int vtg_sread64(void *ctx,unsigned off,uint64_t *out)
{
    (void)ctx;if(!touch.smmu || !out || (off&7) || off>0xd0000-8 || !vtg_clock_ready() || !vtg_pages())return 0;
    *out=*(volatile uint64_t *)(touch.smmu+off);__asm__ volatile("dmb oshld":::"memory");return 1;
}
static int vtg_swrite32(void *ctx,unsigned off,uint32_t value)
{
    (void)ctx;struct vti *v=&touch.iommu;unsigned cb=0x80000+4096*v->bank;
    if(v->slot>=105 || v->bank>=70 || !vtg_quiet(NULL))return 0;
    int allowed=off==0x800+4*v->slot || off==0xc00+4*v->slot ||
        off==0x1000+4*v->bank || off==0x1800+4*v->bank;
    const unsigned fields[]={0,4,0x10,0x30,0x38,0x3c,0x58,0x618,0x7f0};
    for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++)allowed|=off==cb+fields[i];
    if(!allowed || (off==cb+4 && value!=(v->bank_actlr&~2u)))return 0;
    vt_wr(touch.smmu,off,value);return 1;
}
static int vtg_swrite64(void *ctx,unsigned off,uint64_t value)
{
    (void)ctx;unsigned cb=0x80000+4096*touch.iommu.bank;
    if(touch.iommu.bank>=70 || (off!=cb+0x20 && off!=cb+0x28) || !vtg_quiet(NULL))return 0;
    *(volatile uint64_t *)(touch.smmu+off)=value;__asm__ volatile("dsb sy":::"memory");return 1;
}
static int vtg_clean_tables(void *ctx,void *cpu,size_t bytes)
{
    if(cpu!=(void *)(touch.dma_va+5*PAGE_SIZE))return 0;
    return vtg_cache(ctx,cpu,bytes,VGPT_CLEAN);
}
static int vtg_route_live(void)
{
    uint32_t value;
    if(touch.identity_mapping) {
        struct vtr_route *r=&touch.identity_route;
        if(vt_rd(touch.smmu,0)!=r->cr0 || vt_rd(touch.smmu,0x800+4*r->slot)!=r->smr ||
           vt_rd(touch.smmu,0xc00+4*r->slot)!=r->s2cr)return 0;
        if(((r->s2cr>>16)&3)==0) {
            unsigned bank=r->s2cr&255;
            if(vt_rd(touch.smmu,0x1000+4*bank)!=r->cbar || vt_rd(touch.smmu,0x80000+4096*bank)!=r->sctlr)return 0;
        }
        return 1;
    }
    struct vti *v=&touch.iommu;unsigned cb=0x80000+4096*v->bank;uint64_t wide;
    if(!v->attached || v->retired || vt_rd(touch.smmu,0)!=0x280406 ||
       vt_rd(touch.smmu,0x48) || vt_rd(touch.smmu,0x800+4*v->slot)!=0x800000b6 ||
       vt_rd(touch.smmu,0xc00+4*v->slot)!=v->bank ||
       (vt_rd(touch.smmu,cb)&~0x1000u)!=(v->image.sctlr&~0x1000u))return 0;
    uint32_t fsr=vt_rd(touch.smmu,cb+0x58);os_atomic_store(&touch.iommu_fsr,fsr,relaxed);
    if(fsr&0xc00001feu){v->retired=1;v->error=VTI_READBACK;return 0;}
    const unsigned check[][2]={{0x1000+4*v->bank,v->image.cbar},{0x1800+4*v->bank,v->image.cba2r},
        {cb+4,v->bank_actlr&~2u},{cb+0x30,v->image.tcr},{cb+0x38,v->image.mair0},{cb+0x3c,v->image.mair1}};
    for(unsigned i=0;i<sizeof(check)/sizeof(check[0]);i++) {
        if(!vtg_sread32(NULL,check[i][0],&value) || value!=check[i][1])return 0;
    }
    if(!vtg_sread32(NULL,cb+0x10,&value) || (value&~0x60u)!=(v->image.tcr2&~0x60u))return 0;
    return vtg_sread64(NULL,cb+0x20,&wide) && wide==v->image.ttbr0 &&
        vtg_sread64(NULL,cb+0x28,&wide) && wide==v->image.ttbr1;
}
static int vtg_lease(void *ctx)
{
    (void)ctx;
    return touch.gpi_claimed && touch.gpi && touch.se && touch.smmu && vtg_clock_ready() &&
        vtg_pages() && vtg_power_valid() && vtg_irqs_ready() && vtg_dfs_valid() && ((vt_rd(touch.se,0x68)>>8)&255)==1 &&
        (vt_rd(touch.se,0x64)&1) && vtg_route_live();
}
static int vtg_read(void *ctx,unsigned off,uint32_t *out)
{(void)ctx;if(!out || off>=0x60000 || (off&3) || !vtg_lease(NULL))return 0;*out=vt_rd(touch.gpi,off);return 1;}
static int vtg_write(void *ctx,unsigned off,uint32_t value)
{
    (void)ctx;if(!vtg_lease(NULL))return 0;int allowed=0;
    const unsigned registers[]={0x12000,0x12004,0x12008,0x1200c,0x12100,0x12104,
        0x13008,0x13010,0x13088,0x13098,0x1309c,0x130a0,0x130a4,0x130b8,0x130c0,
        0x13108,0x13120,0x13180,0x13188,0x1318c};
    for(unsigned i=0;i<sizeof(registers)/sizeof(registers[0]);i++)allowed|=off==registers[i];
    for(unsigned ch=0;ch<2;ch++) {
        unsigned base=0x10000+0x80*ch;
        allowed|=(off>=base && off<=base+12 && !(off&3)) || off==base+0x5c ||
            (off>=base+0x60 && off<=base+0x6c && !(off&3));
    }
    allowed|=(off>=0x11000 && off<=0x11034 && !(off&3));
    if(!allowed)return 0;
    int trace=touch.manual_mode==2 && !touch.transport.exchanges && (off==0x12000 || off==0x12008);
    if(trace) {
        printf("VERMEER GPI DOORBELL: store-begin reg=%05x value=%08x\n",off,value);
        /* Let the independent 1ms USB worker export this boundary. This time
         * counts against the unchanged 100ms exchange deadline. Revalidate
         * the full live lease after scheduling and before the MMIO store. */
        vt_sleep(10);if(!vgpt_guard(&touch.transport))return 0;
    }
    vt_wr(touch.gpi,off,value);
    if(trace) {
        printf("VERMEER GPI DOORBELL: store-complete reg=%05x value=%08x\n",off,value);
        vt_sleep(10);
    }
    return 1;
}
static int vtg_idle(void *ctx)
{(void)ctx;if(!vtg_lease(NULL))return -1;return !(vt_rd(touch.se,VG_STATUS)&0x1001);}
/* Registered RPMh provider owns its own GPIO/MMIO and full GPU DB snapshot.
 * This helper intentionally needs no touch SE/GPIO/SMMU mappings, so manual
 * mode can capture power/DB evidence before the new touch mapping path. */
static int vtg_acquire_registered_power(void)
{
    if(touch.power_held)return touch.power && touch.power->valid(touch.power->ctx)==1;
    touch.power=os_atomic_load(&vtg_power_provider,acquire);
    if(!touch.power || touch.power->acquire(touch.power->ctx)!=1 || touch.power->valid(touch.power->ctx)!=1) {
        printf("VERMEER TOUCH: real RPMh/reset lease acquisition failed; resources retained\n");return 0;
    }
    touch.power_held=true;return 1;
}
static void vtg_checkpoint(void *ctx,unsigned checkpoint)
{
    (void)ctx;
    /* The generic engine calls this only for its first exchange. RAM log
     * only: no device read, delay, IRQ mutation or deadline extension. */
    printf("VERMEER GPI CHECKPOINT: first-exchange=%u\n",checkpoint);
}
static int vtg_prepare(void)
{
    if(!touch.power_held)touch.power=os_atomic_load(&vtg_power_provider,acquire);
    if(touch.power && !touch.power_held) {
        printf("VERMEER TOUCH PHASE: power/GPUdb begin\n");
        if(!vtg_acquire_registered_power())return 0;
        printf("VERMEER TOUCH PHASE: power/GPUdb complete\n");
    }
    if(!vt_reset_ready()) {
        printf("VERMEER TOUCH: GPIO24 reset remains low; no working RPMh/reset provider registered\n");return 0;
    }
    if(!touch.power_held || !touch.power || touch.power->valid(touch.power->ctx)!=1) {
        printf("VERMEER TOUCH: GPI needs the current-FDT-checked RPMh lease for IRQ qualification\n");return 0;
    }
    if(!vtg_gic)vtg_gic=ml_io_map(0x17100000,0x10000);
    if(!vtg_irqs_ready())return 0;
    if(!touch.gpi)touch.gpi=ml_io_map(0x00a00000,0x60000);
    if(!vtg_quiet(NULL) || !os_atomic_cmpxchg(&vtg_owner,0,1,acq_rel)) {
        printf("VERMEER TOUCH: GPI provider not exclusively quiescent; no takeover\n");return 0;
    }
    touch.gpi_claimed=true;
    for(unsigned i=0;i<4;i++) {
        touch.pins[i]=vt_rd(touch.gpio,(44+i)*0x1000);
        if(!vtr_pin_owned(touch.pins[i])) {
            printf("VERMEER TOUCH: GPIO%u assigned to external eGPIO controller; no pin writes\n",44+i);return 0;
        }
    }
    if(!vtg_dfs_prepare())return 0;
    if(PAGE_SIZE!=16384 || kmem_alloc_contig(kernel_map,&touch.dma_va,6*PAGE_SIZE,PAGE_MASK,0,0,
        KMA_DATA|KMA_ZERO|KMA_PERMANENT,VM_KERN_MEMORY_IOKIT)!=KERN_SUCCESS)return 0;
    ppnum_t pn=pmap_find_phys(kernel_pmap,(addr64_t)touch.dma_va);if(!pn)return 0;
    touch.dma_pa=ptoa_64(pn);if(!vtg_pages())return 0;
    struct vtr_route route;
    touch.identity_mapping=vtr_identity_sid(NULL,vt_smmu_read,0xb6,&route);
    if(touch.identity_mapping)touch.identity_route=route;
    else {
        touch.iommu=(struct vti){.io={NULL,vtg_quiet,vtg_sread32,vtg_sread64,vtg_swrite32,
            vtg_swrite64,vtg_clean_tables,vtg_now,vtg_pause},
            .table_pa=touch.dma_pa+5*PAGE_SIZE,.table_cpu=(void *)(touch.dma_va+5*PAGE_SIZE)};
        for(unsigned i=0;i<5;i++)touch.iommu.physical[i]=touch.dma_pa+i*PAGE_SIZE;
        /* The supported GUI profile has one CPU and polled UFS/USB drivers.
         * Keep configuration selection/publication on that CPU without a
         * scheduler handoff. Multi-CPU needs a shared apps-SMMU config lock;
         * a driver-local flag cannot establish that cross-driver exclusion. */
        if(ml_get_cpu_count()!=1) {
            printf("VERMEER TOUCH: SIDb6 attach needs shared SMMU configuration lock on multi-CPU kernels\n");return 0;
        }
        printf("VERMEER TOUCH PHASE: SMMU attach begin; first owned register writes next\n");
        if(touch.manual_mode==2)vt_sleep(200);
        disable_preemption();int mapped=vti_prepare(&touch.iommu);enable_preemption();
        if(!mapped) {
            const struct vti *v=&touch.iommu;
            vtg_status();printf("VERMEER TOUCH: SIDb6 owned 4K mapping failed reason=%u; all pages retained\n",v->error);
            printf("VERMEER IOMMU: phase=%u index=%u conflict=%u slot=%u bank=%u touched=%u valid-routes=%u\n",
                v->scan_phase,v->scan_index,v->conflict,v->slot,v->bank,v->touched,v->valid_routes);
            printf("VERMEER IOMMU: enabled=%u reserved-disabled=%u unsafe-actlr=%u first-bank=%u actlr=%08x smr=%08x s2cr=%08x\n",
                v->enabled_banks,v->reserved_banks,v->unsafe_actlr_banks,v->first_unsafe_bank,
                v->first_unsafe_actlr,v->conflict_smr,v->conflict_s2cr);
            return 0;
        }
        printf("VERMEER TOUCH PHASE: SMMU attach complete bank=%u slot=%u\n",touch.iommu.bank,touch.iommu.slot);
    }
    touch.transport=(struct vgpt){.io={NULL,vtg_lease,vtg_read,vtg_write,vtg_cache,vtg_now,vtg_pause,vtg_idle,vtg_checkpoint}};
    for(unsigned i=0;i<5;i++)touch.transport.page[i]=(struct vgpt_page){(void *)(touch.dma_va+i*PAGE_SIZE),
        touch.identity_mapping?touch.dma_pa+i*PAGE_SIZE:0x100000+i*PAGE_SIZE};
    printf("VERMEER GPI DMA: pa=%llx table-pa=%llx ttbr0=%llx asid=%u cbar=%08x tcr=%08x tcr2=%08x sctlr=%08x\n",
        (unsigned long long)touch.dma_pa,(unsigned long long)touch.iommu.table_pa,
        (unsigned long long)touch.iommu.image.ttbr0,touch.iommu.asid,touch.iommu.image.cbar,
        touch.iommu.image.tcr,touch.iommu.image.tcr2,touch.iommu.image.sctlr);
    printf("VERMEER TOUCH PHASE: GPI start begin\n");
    if(touch.manual_mode==2)vt_sleep(200);
    if(!vgpt_start(&touch.transport)){vtg_status();return 0;}
    printf("VERMEER GPI READBACK: allocated and started contexts verified; DMA not submitted\n");
    printf("VERMEER TOUCH PHASE: GPI start complete\n");
    for(unsigned i=0;i<4;i++) {
        unsigned reg=(44+i)*0x1000;
        if(vt_rd(touch.gpio,reg)!=touch.pins[i])goto failed;
        if(!vtg_lease(NULL))goto failed;
        vt_wr(touch.gpio,reg,(touch.pins[i]&~0x3ffu)|4u|(2u<<6)|(i==3?3u:1u));
    }
    touch.pins_saved=true;
    /* Vendor GENI_GPI_DMA setup, only after the GPI/SID resources are owned.
     * Firmware executes the SPI GO; no internal-DMA length/start registers. */
    const unsigned reg[][2]={{0x618,UINT32_MAX},{0x648,UINT32_MAX},{0xc44,UINT32_MAX},
        {0xd44,UINT32_MAX},{0xe1c,0},{0x644,vt_rd(touch.se,0x644)&~1u},
        {0x614,vt_rd(touch.se,0x614)&~((1u<<0)|(1u<<26)|(1u<<27)|(1u<<30))},
        {0x258,1},{0xe18,vt_rd(touch.se,0xe18)|15u}};
    for(unsigned i=0;i<sizeof(reg)/sizeof(reg[0]);i++) {
        if(!vtg_lease(NULL))goto failed;vt_wr(touch.se,reg[i][0],reg[i][1]);
    }
    touch.use_gpi=true;vtg_status();
    printf("VERMEER TOUCH: GPI SPI execution enabled; mapping=%s bank=%u, GT9916 identity next\n",
        touch.identity_mapping?"inherited identity":"owned 4K IOVA",touch.iommu.bank);
    return 1;
failed:
    (void)vgpt_fail(&touch.transport,VGPT_LEASE);vtg_status();return 0;
}
#endif
