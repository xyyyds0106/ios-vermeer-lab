/* Host model of the real native module. The runner substitutes ONLY kernel
 * services + the three terminal MMIO/barrier primitives in a scratch copy.
 * All mapping, ownership, FDT gate, runtime parser, votes and reset flow below
 * are compiled from the actual production C file. No hardware is accessed. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <setjmp.h>
#define ARM64_BOARD_CONFIG_SWEET 1
typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;
typedef unsigned vm_prot_t;
typedef void *DTEntry;
enum {PAGE_SIZE=16384,VM_WIMG_IO=7,VM_WIMG_WCOMB=6,VM_PROT_READ=1,VM_PROT_WRITE=2,kSuccess=0,NSEC_PER_USEC=1000};
#define os_atomic_load(p,o) atomic_load_explicit(p,memory_order_##o)
#define os_atomic_store(p,v,o) atomic_store_explicit(p,v,memory_order_##o)
#define os_atomic_cmpxchg(p,a,b,o) ({__typeof__(atomic_load(p)) old=(a);atomic_compare_exchange_strong_explicit(p,&old,b,memory_order_##o,memory_order_relaxed);})
struct vtp_ops;
static struct {
    uint32_t rsc[0x10000/4],gpio[PAGE_SIZE/4],gic[PAGE_SIZE/4];uint8_t db[0x20000];
    const struct vtp_ops *ops;
    uint64_t now;
    unsigned enabled,hook,pm,register_fail,maps,reads,writes,gpio_writes,pm_calls;
    unsigned trap_at,noirq,stuck_reset,malformed_map;
    unsigned rsc_writes,triggered;
    const uint8_t *boot_fdt;unsigned boot_fdt_bytes;
    jmp_buf fault;
} hw;
static vm_offset_t io_map(uint64_t pa,vm_size_t size,unsigned attr,vm_prot_t prot,int unmappable)
{
    ++hw.maps;assert(!unmappable);
    if(pa==0x17a20000) {assert(size==0x10000 && attr==VM_WIMG_IO && prot==3);return (uintptr_t)hw.rsc;}
    if(pa==0x81c60000) {assert(size==0x20000 && attr==VM_WIMG_WCOMB && prot==1);return (uintptr_t)hw.db;}
    if(pa==0x17100000) {assert(size==PAGE_SIZE && attr==VM_WIMG_IO && prot==1);return (uintptr_t)hw.gic;}
    assert(pa==0xf118000 && size==PAGE_SIZE && attr==VM_WIMG_IO && prot==3);
    return hw.malformed_map?0:(uintptr_t)hw.gpio;
}
static int PE_parse_boot_argn(const char *key,void *out,size_t bytes)
{assert(!strcmp(key,"vermeer_rpmh") && bytes==sizeof(int));*(int *)out=(int)hw.enabled;return 1;}
static int SecureDTLookupEntry(void *base,const char *path,DTEntry *entry)
{(void)base;assert(!strcmp(path,"/") || !strcmp(path,"/chosen"));*entry=(void *)path;return kSuccess;}
static int SecureDTGetProperty(DTEntry entry,const char *name,const void **value,unsigned *bytes)
{
    (void)entry;
    if(!strcmp(name,"vermeer,boot-fdt") && hw.boot_fdt) {
        *value=hw.boot_fdt;*bytes=hw.boot_fdt_bytes;return kSuccess;
    }
    if(strcmp(name,"physical-model"))return 1;
    *value="Redmi K70";*bytes=sizeof("Redmi K70");return kSuccess;
}
static uint64_t mach_absolute_time(void){return hw.now*1000;}
static void absolutetime_to_nanoseconds(uint64_t value,uint64_t *out){*out=value;}
static void delay(unsigned us){hw.now+=us;}
static uint32_t model_load(vm_offset_t,uint64_t);
static void model_store(vm_offset_t,uint64_t,uint32_t);
#include "board_native_model.c"
unsigned vermeer_rpmh_pm_hook_version(void){return hw.hook;}
int vermeer_rpmh_system_idle_hold(void){++hw.pm_calls;return hw.pm;}
int vermeer_touch_power_register(const struct vtp_ops *ops)
{assert(ops && ops->acquire && ops->valid);if(hw.register_fail || hw.ops)return 0;hw.ops=ops;return 1;}
static unsigned first(void){return ((hw.rsc[0]>>8)&0xffff)==0x300?0x20:0x10;}
static unsigned msg(void){return first()==0x20?0x34:0x30;}
static unsigned stride(void){return first()==0x20?24:20;}
static void before_access(void)
{
    ++hw.now;
    assert(vermeer_rpmh_pm_inhibited() && vrb.owner>=VRB_TOUCH && vrb.owner<=VRB_MEDIA && !vrb.retired);
    if(hw.trap_at && hw.reads+hw.writes==hw.trap_at)longjmp(hw.fault,1);
}
static uint32_t model_load(vm_offset_t va,uint64_t pa)
{
    ++hw.reads;before_access();
    if(pa>=0x17a20000 && pa<0x17a30000) {
        assert(va==(uintptr_t)hw.rsc+pa-0x17a20000);return hw.rsc[(pa-0x17a20000)/4];
    }
    if(pa>=0x81c60000 && pa<0x81c80000) {
        assert(va==(uintptr_t)hw.db+pa-0x81c60000);
        return vrp_u32(hw.db+(pa-0x81c60000));
    }
    if(pa==0x17100104 || pa==0x17100304) {
        assert(va==(uintptr_t)hw.gic+pa-0x17100000);return hw.gic[(pa-0x17100000)/4];
    }
    assert(pa==VRB_RESET_PA || pa==VRB_RESET_PA+4);
    assert(va==(uintptr_t)hw.gpio+pa-VRB_RESET_PA);return hw.gpio[(pa-VRB_RESET_PA)/4];
}
static void model_store(vm_offset_t va,uint64_t pa,uint32_t value)
{
    ++hw.writes;before_access();
    if(pa==VRB_RESET_PA || pa==VRB_RESET_PA+4) {
        assert(va==(uintptr_t)hw.gpio+pa-VRB_RESET_PA);++hw.gpio_writes;
        if(pa==VRB_RESET_PA)hw.gpio[0]=value;
        else hw.gpio[1]=value?(hw.stuck_reset?2u:3u):0;
        return;
    }
    assert(pa>=0x17a20000 && pa<0x17a30000 && va==(uintptr_t)hw.rsc+pa-0x17a20000);
    unsigned off=(unsigned)(pa-0x17a20000);++hw.rsc_writes;
    assert(vrw_write_allowed(&vrb.touch.executor,off,value));
    if(off==0xd08)hw.rsc[0xd04/4]&=~value;else hw.rsc[off/4]=value;
    for(unsigned t=0;t<3;t++)if(off==0xd00+t*0x2a0+first()+4 && value==(VRW_AMC|VRW_TRIGGER)) {
        unsigned b=0xd00+t*0x2a0,rail=hw.triggered++;assert(rail<2);
        assert(hw.gpio[1]==0); /* reset remains asserted throughout both votes */
        for(unsigned c=0;c<3;c++) {
            unsigned at=b+msg()+c*stride();
            assert(hw.rsc[at/4]==VRW_ACK);
            assert(hw.rsc[(at+4)/4]==(rail?0x40100u:0x40200u)+(c==0?0:(c==1?8:4)));
            assert(hw.rsc[(at+8)/4]==(c==0?(rail?3200u:1800u):(c==1?7u:1u)));
            hw.rsc[(at+12)/4]=0x10100;
        }
        if(!hw.noirq)hw.rsc[0xd04/4]|=1u<<t;
        hw.rsc[(b+first()+8)/4]=1;
    }
}
static void p16(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void p32(uint8_t *p,unsigned v){p16(p,v&65535);p16(p+2,v>>16);}
static void reset(unsigned revision)
{
    memset(&hw,0,sizeof(hw));memset(&vrb,0,sizeof(vrb));
    hw.enabled=hw.hook=hw.pm=1;hw.now=100;
    hw.rsc[0]=(revision>>8)<<16|(revision&255)<<8;hw.rsc[3]=(16u<<27)|(8u<<12);
    hw.rsc[0xd00/4]=0xab;
    for(unsigned t=0;t<8;t++)hw.rsc[(0xd00+t*0x2a0+first()+8)/4]=1;
    p32(hw.db,1);p32(hw.db+4,0x0c0330db);p16(hw.db+8,4);p16(hw.db+12,48);p16(hw.db+14,2);
    memcpy(hw.db+144,"ldob2",5);p32(hw.db+160,0x40100);
    memcpy(hw.db+168,"ldob12",6);p32(hw.db+184,0x40200);
}
static uint8_t *read_fdt(const char *path,size_t *bytes)
{
    FILE *fp=fopen(path,"rb");assert(fp);assert(!fseek(fp,0,SEEK_END));
    long n=ftell(fp);assert(n>40 && n<=0x200000);rewind(fp);
    uint8_t *buf=malloc((size_t)n);assert(buf && fread(buf,1,(size_t)n,fp)==(size_t)n);
    fclose(fp);*bytes=(size_t)n;return buf;
}
int main(int argc,char **argv)
{
    assert(argc==2);size_t bytes;uint8_t *fdt=read_fdt(argv[1],&bytes);
    for(unsigned rev=0;rev<2;rev++) {
        reset(rev?0x300:0x207);
        hw.boot_fdt=fdt;hw.boot_fdt_bytes=(unsigned)bytes;
        assert(vermeer_rpmh_touch_start_from_adt()==1 && !hw.maps && !hw.writes);
        assert(hw.ops && hw.ops->acquire(NULL)==1 && hw.ops->valid(NULL)==1);
        assert(hw.maps==4 && hw.rsc_writes==42 && hw.gpio_writes==3 && hw.triggered==2 &&
            vrb.touch.executor.completed_rails==3 && !vrb.owner && vrb.awake && !vrb.retired);
        unsigned total=hw.reads+hw.writes;
        assert(hw.ops->acquire(NULL)==1 && hw.ops->valid(NULL)==1 && hw.reads+hw.writes==total);
        struct vrb_controller_lease gpu;
        /* Claim's IRQ-only reads also run for a GPU owner. */
        assert(vermeer_rpmh_controller_claim(VRB_GPU,&gpu)==1 && vermeer_rpmh_controller_valid(&gpu));
        struct vrb_controller_lease other;assert(!vermeer_rpmh_controller_claim(VRB_MEDIA,&other));
        vermeer_rpmh_controller_finish(&gpu,VRB_RETAIN_ACTIVE);
        assert(vermeer_rpmh_active_valid(&gpu) && hw.ops->valid(NULL) && !vrb.owner);
        unsigned access_count=hw.reads+hw.writes;
        vermeer_rpmh_active_quarantine(&gpu);
        assert(!hw.ops->valid(NULL) && !vermeer_rpmh_active_valid(&gpu) && vrb.retired && vrb.awake);
        assert(access_count==hw.reads+hw.writes); /* no MMIO from validate/quarantine */
    }
    reset(0x300);hw.enabled=0;assert(!vermeer_rpmh_touch_start(fdt,bytes) && !hw.maps && !hw.ops);
    reset(0x300);hw.hook=0;assert(!vermeer_rpmh_touch_start(fdt,bytes) && !hw.maps && !hw.ops);
    reset(0x300);assert(!vermeer_rpmh_touch_start(fdt,bytes-1) && !hw.maps && !hw.ops);
    reset(0x300);assert(!vermeer_rpmh_touch_start_from_adt() && !hw.ops);
    reset(0x300);hw.register_fail=1;assert(!vermeer_rpmh_touch_start(fdt,bytes) && !hw.maps);
    reset(0x300);hw.gic[0x104/4]=1u<<5;assert(vermeer_rpmh_touch_start(fdt,bytes));
    assert(!hw.ops->acquire(NULL) && !hw.writes && vrb.retired && hw.maps==1);
    reset(0x300);hw.gic[0x304/4]=1u<<5;assert(vermeer_rpmh_touch_start(fdt,bytes));
    assert(!hw.ops->acquire(NULL) && !hw.writes && vrb.retired && hw.maps==1);
    reset(0x300);assert(vermeer_rpmh_touch_start(fdt,bytes));hw.pm=0;
    assert(!hw.ops->acquire(NULL) && vrb.retired && vrb.owner==VRB_TOUCH && !hw.maps && !hw.writes);
    reset(0x400);assert(vermeer_rpmh_touch_start(fdt,bytes));
    assert(!hw.ops->acquire(NULL) && !hw.writes && !hw.triggered && vrb.awake);
    reset(0x300);hw.db[144]='x';assert(vermeer_rpmh_touch_start(fdt,bytes));
    assert(!hw.ops->acquire(NULL) && !hw.writes && !hw.triggered);
    reset(0x300);hw.malformed_map=1;assert(vermeer_rpmh_touch_start(fdt,bytes));
    assert(!hw.ops->acquire(NULL) && !hw.writes && vrb.retired && vrb.owner==VRB_TOUCH);
    reset(0x300);hw.stuck_reset=1;assert(vermeer_rpmh_touch_start(fdt,bytes));
    assert(!hw.ops->acquire(NULL) && hw.triggered==2 && vrb.retired && vrb.owner==VRB_TOUCH && !vrb.ready);
    reset(0x300);hw.noirq=1;assert(vermeer_rpmh_touch_start(fdt,bytes));
    assert(!hw.ops->acquire(NULL) && hw.triggered==1 && vrb.retired && vrb.owner==VRB_TOUCH && hw.gpio_writes==2);
    /* Model terminal CPU exceptions at every actual MMIO boundary. They cannot
     * become returned recoverable I/O errors or trigger a second attempt. */
    reset(0x300);assert(vermeer_rpmh_touch_start(fdt,bytes));assert(hw.ops->acquire(NULL));
    unsigned boundaries=hw.reads+hw.writes;
    for(unsigned at=1;at<=boundaries;at++) {
        reset(0x300);assert(vermeer_rpmh_touch_start(fdt,bytes));hw.trap_at=at;
        if(!setjmp(hw.fault)){(void)hw.ops->acquire(NULL);assert(0);}
        assert(vrb.owner==VRB_TOUCH && vrb.awake && !vrb.note.returned && !vrb.ready);
        unsigned total=hw.reads+hw.writes;assert(total==at);
        assert(!hw.ops->acquire(NULL) && total==hw.reads+hw.writes);
    }
    uint32_t seed=1;
    for(unsigned trial=0;trial<1000;trial++) {
        seed=seed*1664525u+1013904223u;size_t pos=seed%bytes;uint8_t old=fdt[pos];
        fdt[pos]^=(uint8_t)(1u<<(seed>>29));(void)vrb_dt_check(fdt,bytes);fdt[pos]=old;
    }
    assert(!vrb_dt_check(fdt,bytes));free(fdt);
    printf("Actual native board flow: %u terminal MMIO boundaries, 1000 DT mutations; maps/attrs/DB/TCS/rails/reset/PM ownership PASS\n",boundaries);return 0;
}
