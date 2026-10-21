#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../gpu/AdrenoFirmware/AdrenoCmdDb.h"
static uint8_t image[ADRENO_CMD_DB_BYTES];
static struct AdrenoCmdDb result;
static void p16(unsigned at,unsigned n){image[at]=(uint8_t)n;image[at+1]=(uint8_t)(n>>8);}
static void p32(unsigned at,uint32_t n){p16(at,n&65535);p16(at+2,n>>16);}
static void fixture(unsigned gmxc)
{
    memset(image,0,sizeof(image));p32(0,1);p32(4,0x0c0330db);
    /* Independent synthetic ARC/BCM rows, not fitted K70 values. */
    p16(8,3);p16(10,0);p16(12,512);p16(14,gmxc?4:3);p16(16,0x100);
    p16(24,5);p16(26,128);p16(28,768);p16(30,4);p16(32,0x300);
    for(unsigned i=0;i<8;i++) {
        if(i==3 && !gmxc)continue;
        unsigned row=i<4?0:1,index=i<4?i:i-4;
        unsigned at=144+(row?128:0)+24*index,aux=144+(row?768:512)+32*index;
        const char *name=AdrenoCmdDbResourceName(i);memcpy(image+at,name,strlen(name));
        p32(at+16,(row?0x50000u:0x30000u)+16*i);
        p16(at+20,8);p16(at+22,32*index);
        p16(aux,0);p16(aux+2,128);p16(aux+4,256);p16(aux+6,512);
    }
}
static unsigned parse(void){return AdrenoCmdDbParse(image,sizeof(image),1,&result);}
static void split_arc_fixture(void)
{
    fixture(1);
    /* Split ARC into independent rows, retaining row-relative aux offsets. */
    memcpy(image+40,image+24,16);
    p16(14,1);
    p16(24,3);p16(26,24);p16(28,512);p16(30,3);p16(32,0x101);
}
int main(void)
{
    fixture(1);assert(parse()==AdrenoCmdDbOK && result.complete && result.entries==8 && result.found_mask==255);
    assert(result.resource[AdrenoCmdDbMx].address==0x30020 && result.resource[AdrenoCmdDbCx].aux[3]==0);
    /* MXC is a separate ARC clock rail; it is never the optional GMxC. */
    p16(40,3);p16(42,224);p16(44,1024);p16(46,1);p16(48,0x102);
    memcpy(image+144+224,"mxc.lvl",8);p32(144+224+16,0x30100);p16(144+224+20,8);
    p16(144+1024+2,64);p16(144+1024+4,128);p16(144+1024+6,256);
    assert(parse()==AdrenoCmdDbOK && result.complete && result.entries==9 && result.found_mask==511);
    assert(result.resource[AdrenoCmdDbMxc].address==0x30100 && result.resource[AdrenoCmdDbGmxc].address==0x30030);
    p32(144+224+16,0x50100);assert(parse()==AdrenoCmdDbUnsupported && !result.complete);
    split_arc_fixture();
    assert(parse()==AdrenoCmdDbOK && result.complete && result.rows==3 && result.entries==8 && result.found_mask==255);
    assert(result.resource[AdrenoCmdDbGfx].version==0x100 && result.resource[AdrenoCmdDbCx].version==0x101);
    assert(result.resource[AdrenoCmdDbMx].address==0x30020 && result.resource[AdrenoCmdDbGmxc].aux[7]==2);
    split_arc_fixture();memcpy(image+144+24,image+144,8);assert(parse()==AdrenoCmdDbDuplicate && !result.complete);
    split_arc_fixture();p16(26,0);assert(parse()==AdrenoCmdDbBounds && !result.complete);
    fixture(0);assert(parse()==AdrenoCmdDbOK && result.complete && result.entries==7 && result.found_mask==0xf7);
    fixture(0);memcpy(image+144+128,"unknown",8);
    assert(parse()==AdrenoCmdDbMissing && result.complete && !(result.found_mask&(1u<<AdrenoCmdDbSh0)));
    fixture(1);p32(0,2);assert(parse()==AdrenoCmdDbHeader && !result.complete);
    fixture(1);assert(AdrenoCmdDbParse(image,sizeof(image),0,&result)==AdrenoCmdDbHeader);
    fixture(1);p32(140,1);assert(parse()==AdrenoCmdDbHeader);
    fixture(1);p16(14,1025);assert(parse()==AdrenoCmdDbBounds);
    fixture(1);p16(26,0);assert(parse()==AdrenoCmdDbBounds);
    fixture(1);p16(26,65532);p16(30,1024);assert(parse()==AdrenoCmdDbBounds);
    fixture(1);p16(24,3);assert(parse()==AdrenoCmdDbUnsupported);
    fixture(1);memcpy(image+144+24,image+144,8);assert(parse()==AdrenoCmdDbDuplicate);
    fixture(1);p16(12,0);assert(parse()==AdrenoCmdDbBounds);
    fixture(1);p16(164,34);assert(parse()==AdrenoCmdDbUnsupported);
    fixture(1);p32(160,0x40000);assert(parse()==AdrenoCmdDbUnsupported);
    fixture(1);p32(144+24+16,0x30000);assert(parse()==AdrenoCmdDbDuplicate);
    fixture(1);p16(8,0);assert(parse()==AdrenoCmdDbHeader);
    fixture(1);p16(144+128+20,6);assert(parse()==AdrenoCmdDbUnsupported);
    fixture(1);memset(image+144+24,0,8);assert(parse()==AdrenoCmdDbHeader);
    fixture(1);assert(AdrenoCmdDbParse(image,sizeof(image)-1,1,&result)==AdrenoCmdDbArgument);
    /* Failed validation must never leave a previously successful complete flag. */
    fixture(1);assert(parse()==AdrenoCmdDbOK);p16(14,65535);assert(parse()!=AdrenoCmdDbOK && !result.complete);
    uint32_t seed=1;
    for(unsigned i=0;i<10000;i++) {
        fixture(i&1);seed=seed*1664525u+1013904223u;unsigned at=seed%1024;
        image[at]^=(uint8_t)(1u<<(seed>>29));unsigned status=parse();
        assert(status==result.status && result.entries<=ADRENO_CMD_DB_MAX_ENTRIES);
        if(status==AdrenoCmdDbOK)assert(result.complete && (result.found_mask&0xf7u)==0xf7u);
    }
    puts("GPU cmd-db: repeated slave rows, full rows/entries/aux validation, required resources, optional gmxc absence, duplicates/bounds and 10000 mutations PASS");
    return 0;
}
