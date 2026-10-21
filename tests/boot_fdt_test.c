#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../boot/boot_fdt.h"

static void le(uint8_t *p,uint32_t v)
{for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void be(uint8_t *p,uint32_t v)
{for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(24-8*i));}
static size_t property(uint8_t *p,const char *name,const void *value,unsigned bytes)
{
    memset(p,0,36+((bytes+3u)&~3u));memcpy(p,name,strlen(name));le(p+32,bytes);
    if(value)memcpy(p+36,value,bytes);
    return 36+((bytes+3u)&~3u);
}
static size_t template(uint8_t *p,unsigned markers,unsigned wrong_node)
{
    memset(p,0,3*BOOT_FDT_CAPACITY);le(p,1);le(p+4,1);size_t at=8;
    at+=property(p+at,"name","device-tree",12);
    le(p+at,1+markers);le(p+at+4,0);at+=8;
    at+=property(p+at,"name",wrong_node?"other!":"chosen",7);
    for(unsigned i=0;i<markers;i++)at+=property(p+at,"vermeer,boot-fdt",NULL,BOOT_FDT_CAPACITY);
    return at;
}
static void fdt(uint8_t *p)
{
    memset(p,0,72);be(p,0xd00dfeed);be(p+4,72);be(p+8,56);be(p+12,72);
    be(p+16,40);be(p+20,17);be(p+24,16);be(p+36,16);
    be(p+56,1);be(p+64,2);be(p+68,9);
}
int main(void)
{
    uint8_t *a=malloc(3*BOOT_FDT_CAPACITY),*saved=malloc(3*BOOT_FDT_CAPACITY);
    assert(a && saved);uint8_t source[72];fdt(source);size_t slot=0;
    size_t n=template(a,1,0);assert(boot_fdt_slot(a,n,&slot)==1 && slot+BOOT_FDT_CAPACITY==n);
    memcpy(saved,a,n);assert(boot_fdt_bridge(a,n,source,sizeof(source))==1);
    assert(!memcmp(a,saved,slot) && !memcmp(a+slot,source,72));
    for(size_t i=slot+72;i<n;i++)assert(!a[i]);
    assert(boot_fdt_bridge(a,n,source,72)==-1); /* no replay/host-prefilled slot */
    for(unsigned i=0;i<72;i++) {
        memcpy(a,saved,n);assert(boot_fdt_bridge(a,n,source,i)==-1 && !memcmp(a,saved,n));
    }
    const unsigned fields[]={0,4,8,12,20,24,32,36};
    for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++) {
        uint8_t bad[72];memcpy(bad,source,72);be(bad+fields[i],UINT32_MAX);
        memcpy(a,saved,n);assert(boot_fdt_bridge(a,n,bad,72)==-1 && !memcmp(a,saved,n));
    }
    for(unsigned i=0;i<256;i++) {
        size_t short_n=i<128?i:n-(i-127);
        memcpy(a,saved,n);assert(boot_fdt_bridge(a,short_n,source,72)==-1 && !memcmp(a,saved,n));
    }
    for(size_t i=0;i<slot;i++) {
        memcpy(a,saved,n);a[i]^=0x80;uint8_t before=a[i];
        int result=boot_fdt_bridge(a,n,source,72);
        assert(a[i]==before);
        if(result==1)assert(!memcmp(a+slot,source,72));
        else for(size_t j=slot;j<n;j++)assert(!a[j]);
    }
    memcpy(a,saved,n);a[slot+BOOT_FDT_CAPACITY-1]=1;
    assert(boot_fdt_bridge(a,n,source,72)==-1);
    for(unsigned i=0;i<72;i++)assert(a[slot+i]==0);
    n=template(a,2,0);assert(boot_fdt_slot(a,n,&slot)==-1);
    n=template(a,1,1);assert(boot_fdt_slot(a,n,&slot)==-1);
    n=template(a,0,0);slot=123;assert(boot_fdt_slot(a,n,&slot)==0 && slot==123);
    assert(boot_fdt_bridge(a,n,NULL,0)==0); /* legacy payload remains bootable */
    /* Deep/malformed counters cannot overflow the explicit parse stack. */
    memset(a,0,65*8);for(unsigned i=0;i<65;i++)le(a+i*8+4,1);
    assert(boot_fdt_slot(a,65*8,&slot)==-1);
    free(saved);free(a);
    puts("Boot FDT: bounded full ADT scan, live copy, zero tail, malformed/truncated/duplicate/prefilled refusal and legacy compatibility PASS");
}
