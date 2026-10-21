#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../gpu/K70/K70GpuSocInfo.h"
static uint8_t image[512];
static void put(unsigned at,uint32_t v){for(unsigned i=0;i<4;i++)image[at+i]=(uint8_t)(v>>(8*i));}
static void fixture(unsigned format)
{memset(image,0,sizeof(image));put(0,format);put(4,519);put(184,256);put(188,2);
 put(256,2);put(300,1);put(304,0x43050a01);put(308,0x7000400);}
int main(void)
{
    struct K70GpuSocInfo out={0},before;
    for(unsigned f=16;f<=19;f++) {
        fixture(f);assert(!K70GpuSocInfoDecode(image,sizeof(image),&out));
        assert(out.format==f && out.soc_id==519 && out.gpu_chip_id==0x43050a01 && out.vulkan_id==0x7000400);
    }
    before=out;
    for(unsigned n=0;n<344;n++)assert(K70GpuSocInfoDecode(image,n,&out));
    assert(!memcmp(&out,&before,sizeof(out)));
    const unsigned bad[][2]={{0,15},{0,20},{184,188},{184,257},{184,508},{188,18},
        {188,0},{256,17},{256,1},{300,0},{300,16},{304,0},{304,UINT32_MAX},{4,0}};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        fixture(19);put(bad[i][0],bad[i][1]);assert(K70GpuSocInfoDecode(image,sizeof(image),&out));
        assert(!memcmp(&out,&before,sizeof(out)));
    }
    fixture(19);assert(K70GpuSocInfoDecode(image,sizeof(image),(struct K70GpuSocInfo *)(void *)(image+32)));
    assert(K70GpuSocInfoDecode(0,sizeof(image),&out));
    assert(K70GpuSocInfoDecode(image,sizeof(image),0));
    puts("GPU socinfo: actual item137 part ID, supported layouts, full bounds/duplicate/alias checks; no fabricated chip identity PASS");
}
