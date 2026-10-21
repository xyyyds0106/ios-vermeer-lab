#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../gpu/K70/K70GpuBoardPolicy.h"
static uint8_t image[0x200000],original[0x200000];
static size_t bytes;
static struct K70GpuBoardPolicy result;
static struct K70GpuBoardQualification proof={1,1,{16,519,0x43050c01,0},0,3};
static uint32_t be(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void put(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static int parse(void){return K70GpuBoardPolicyBuild(image,bytes,&proof,&result);}
static void reset(void){memcpy(image,original,bytes);memset(&result,0x5a,sizeof(result));}
/* Locate a property in the saved host TEST fixture. Production does its own
 * bounded parse and never loads this path or assumes the test qualifications. */
static uint8_t *property(const char *wanted,const char *name)
{
    size_t at=be(image+8),strings=be(image+12),used=0,lens[64];unsigned depth=0;char path[512];
    for(;;) {
        uint32_t t=be(image+at);at+=4;
        if(t==1){size_t start=at;while(image[at])++at;lens[depth]=used;
            if(!depth){path[0]='/';used=1;}else{if(depth>1)path[used++]='/';memcpy(path+used,image+start,at-start);used+=at-start;}
            path[used]=0;++depth;at=(at+4)&~(size_t)3;
        }else if(t==2){used=lens[--depth];path[used]=0;}
        else if(t==3){unsigned n=be(image+at),no=be(image+at+4);at+=8;
            if(!strcmp(path,wanted) && !strcmp((char *)image+strings+no,name))return image+at;
            at=(at+n+3)&~(size_t)3;
        }else if(t==9)return NULL;else assert(t==4);
    }
}
static void refused(unsigned expected)
{
    struct K70GpuBoardPolicy before=result;unsigned status=(unsigned)parse();
    if(status!=expected)fprintf(stderr,"expected %u, got %u\n",expected,status);
    assert(status==expected && !memcmp(&before,&result,sizeof(result)));
}
int main(int argc,char **argv)
{
    assert(argc==2);FILE *input=fopen(argv[1],"rb");assert(input);
    bytes=fread(original,1,sizeof(original),input);assert(bytes>40 && !ferror(input) && feof(input));fclose(input);
    const char *gpu="/soc/qcom,kgsl-3d0@3d00000";
    for(unsigned bin=0;bin<2;++bin) {
        reset();proof.speed_bin=bin;proof.sku_code=bin?0:3;assert(!parse());
        assert(result.power.qualified && result.power.gpu_count==(bin?9:10) && result.power.gmu_count==3);
        assert(result.power.gpu_hz[1]==220000000 && result.power.gpu_level[1]==52);
        assert(result.power.gpu_hz[result.power.gpu_count-1]==(bin?680000000:719000000));
        assert(result.power.gmu_hz[1]==220000000 && result.power.gmu_level[1]==64);
        assert(result.request.mx_level==56 && result.request.cx_level==64 && result.request.gfx_level==52);
        assert(result.request.bandwidth_kbps==2136718 && result.power.bandwidth_count==10);
        assert(result.power.bcm[0].buswidth==16 && result.power.bcm[1].buswidth==4);
        assert(result.power.bcm[2].fixed && result.power.bcm[2].perfmode==8 && result.power.bcm[2].perfmode_kbps==16500000);
        assert(result.chip_id==proof.soc.gpu_chip_id && result.smmu_actlr==0x32b && result.mxc_supply_present);
        assert(result.zap_base==0x9b09a000 && result.zap_bytes==8192);
    }
    proof.speed_bin=0;proof.sku_code=3;
    reset();proof.speed_bin_qualified=0;refused(K70BoardSpeedBin);proof.speed_bin_qualified=1;
    reset();proof.speed_bin=2;refused(K70BoardSpeedBin);proof.speed_bin=0;
    reset();proof.sku_code=0;refused(K70BoardSku);proof.sku_code=3;
    reset();proof.socinfo_qualified=0;refused(K70BoardChip);proof.socinfo_qualified=1;
    reset();proof.soc.gpu_chip_id=0;refused(K70BoardChip);proof.soc.gpu_chip_id=0x43050c01;
    reset();put(property(gpu,"reg"),0x3d10000);refused(K70BoardContract);
    reset();memcpy(property(gpu,"status"),"no",3);refused(K70BoardStatus);
    reset();put(property("/soc/clock-controller@3d90000","vdd_mx-supply"),39);refused(K70BoardContract);
    reset();put(property("/soc/kgsl-smmu@3da0000","qcom,actlr")+8,0);refused(K70BoardContract);
    reset();put(property("/reserved-memory/gpu_micro_code_region@9b09a000","reg")+12,4096);refused(K70BoardContract);
    reset();put(property(gpu,"qcom,bus-table-ddr")+4,0);refused(K70BoardOpp);
    reset();put(property("/soc/qcom,gmu@3d69000","qcom,gmu-freq-table")+4,0);refused(K70BoardOpp);
    reset();put(property("/soc/qcom,kgsl-3d0@3d00000/qcom,gpu-pwrlevel-bins/qcom,gpu-pwrlevels-0/qcom,gpu-pwrlevel@8","qcom,bus-freq"),10);refused(K70BoardOpp);
    reset();uint8_t *row_acd=property("/soc/qcom,kgsl-3d0@3d00000/qcom,gpu-pwrlevel-bins/qcom,gpu-pwrlevels-0/qcom,gpu-pwrlevel@8","qcom,acd-level");
    put(row_acd-4,be(property(gpu,"status")-4));refused(K70BoardStatus); /* status on selected OPP child */
    reset();assert(K70GpuBoardPolicyBuild(image,bytes-1,&proof,&result)==K70BoardFdt);
    /* Header corruption cannot escape bounds or publish a partial policy. */
    for(unsigned i=0;i<40;++i){reset();image[i]^=0x80;struct K70GpuBoardPolicy before=result;
        int status=parse();if(status)assert(!memcmp(&before,&result,sizeof(result)));}
    puts("K70 live-FDT policy: both bins, exact resource/supply/SID/ACTLR/IRQ guards, full OPP extraction, unknown proofs refused and malformed bounds PASS (host fixture only)");
    return 0;
}
