#define VERMEER_HOST_TEST 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_sample_images.h"
#define BASE 0x100000000ULL
static unsigned char memory[8192];
struct fixture { unsigned calls,rows,change; char path[VS_PATH_SIZE]; };
static int read_memory(void *opaque,uint64_t address,void *out,unsigned size)
{
    struct fixture *f=opaque;
    f->calls++;
    if (address<BASE || address>BASE+sizeof(memory) || size>BASE+sizeof(memory)-address) return 0;
    memcpy(out,memory+(address-BASE),size);
    if (f->change && address==BASE && f->calls>1) ((struct vs_infos64 *)out)->count++;
    return 1;
}
static void emit(void *opaque,uint64_t base,const char *path)
{
    struct fixture *f=opaque;
    assert(base>=BASE && base<0x180000000ULL);
    assert(strlen(path)<VS_PATH_SIZE);
    strcpy(f->path,path);f->rows++;
}
static void initialize(unsigned count)
{
    memset(memory,0,sizeof(memory));
    struct vs_infos64 info={17,count,BASE+32};
    memcpy(memory,&info,sizeof(info));
    for (unsigned i=0;i<count && i<128;i++) {
        struct vs_image64 image={BASE+i*4096,BASE+4096,0};
        memcpy(memory+32+i*sizeof(image),&image,sizeof(image));
    }
    strcpy((char *)memory+4096,"/System/Library/Migration/test\nplugin");
}
int main(void)
{
    _Static_assert(sizeof(struct vs_infos64)==16,"dyld prefix ABI");
    _Static_assert(sizeof(struct vs_image64)==24,"dyld entry ABI");
    assert(!vs_user_range(UINT64_MAX-4,16));
    assert(!vs_user_range(VS_USER_END-8,16));
    assert(!vs_user_range(0,16));
    initialize(2);struct fixture f={0};
    struct vs_image64 cache={0x190000000ULL,BASE+4096,0};
    memcpy(memory+32+sizeof(cache),&cache,sizeof(cache));
    assert(vs_images(BASE,read_memory,emit,&f)==1 && f.rows==1);
    assert(!strcmp(f.path,"/System/Library/Migration/test?plugin"));
    initialize(128);f=(struct fixture){0};
    assert(vs_images(BASE,read_memory,emit,&f)==VS_MAX_PRIVATE && f.rows==VS_MAX_PRIVATE);
    initialize(VS_MAX_IMAGES+1);f=(struct fixture){0};
    assert(vs_images(BASE,read_memory,emit,&f)==-1 && f.calls==1 && !f.rows);
    initialize(1);struct vs_infos64 *info=(void *)memory;
    info->array=UINT64_MAX-7;f=(struct fixture){0};
    assert(vs_images(BASE,read_memory,emit,&f)==-1 && f.calls==1);
    initialize(1);info->array=BASE+33;f=(struct fixture){0};
    assert(vs_images(BASE,read_memory,emit,&f)==-1 && f.calls==1);
    initialize(1);f=(struct fixture){.change=1};
    assert(vs_images(BASE,read_memory,emit,&f)==-2);
    initialize(1);memset(memory+4096,'x',VS_PATH_SIZE+100);f=(struct fixture){0};
    assert(vs_images(BASE,read_memory,emit,&f)==1 && strlen(f.path)==VS_PATH_SIZE-1);
    initialize(1);struct vs_image64 invalid={BASE,UINT64_MAX-2,0};
    memcpy(memory+32,&invalid,sizeof(invalid));f=(struct fixture){0};
    assert(vs_images(BASE,read_memory,emit,&f)==1 && !f.path[0] && f.calls==3);
    initialize(1);info->array=BASE+sizeof(memory)-8;f=(struct fixture){0};
    assert(vs_images(BASE,read_memory,emit,&f)==-1 && !f.rows);
    f=(struct fixture){0};
    assert(vs_images(BASE+1,read_memory,emit,&f)==-1 && !f.calls);
    puts("Bounded dyld maps: malformed pointers/counts, races, truncation and row limits PASS");
}
