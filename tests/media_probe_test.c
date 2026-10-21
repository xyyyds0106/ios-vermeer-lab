#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_media_probe.h"
struct owner {unsigned begin,end,reads,borrow,fail,flags;};
static uint64_t now_us(void *ctx){(void)ctx;return 100;}
static unsigned proof(void *ctx){return ((struct owner *)ctx)->flags;}
static int read32(void *ctx,unsigned offset,uint32_t *out)
{struct owner *o=ctx;assert(o->borrow);assert(offset<0x4000);++o->reads;
 if(o->reads==o->fail)return 0;*out=0;return 1;}
static int begin(void *ctx,unsigned requested,struct vmt_access *a)
{
    struct owner *o=ctx;++o->begin;
    assert(requested && !(requested&~0xffffu));
    if(!o->flags)return 0;
    assert(!o->borrow);o->borrow=1;
    *a=(struct vmt_access){.physical_start=0x0f150000,.bytes=0x4000,.ctx=o,
                           .read32=read32,.proof=proof,.now_us=now_us};return 1;
}
static void end(void *ctx){struct owner *o=ctx;assert(o->borrow);o->borrow=0;++o->end;}
int main(void)
{
    assert(vmp_request_mask("media bt",8)==VMT_BT_FIELDS);
    assert(vmp_request_mask("media camera",12)==VMT_CAMERA_FIELDS);
    assert(vmp_request_mask("media all",9)==0xffff);
    const char *bad[]={"media bt ","media","media uart","media cci","MEDIA BT","media bt 0xf151000"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(!vmp_request_mask(bad[i],strlen(bad[i])));
    struct vmt_record record;
    assert(vmp_capture(NULL,VMT_BT_FIELDS,1,now_us,NULL,&record)==VMT_GATED);
    assert(!record.valid && record.start_us==100);
    struct owner o={0};struct vmp_provider provider={&o,begin,end};
    assert(vmp_capture(&provider,VMT_BT_FIELDS,2,now_us,NULL,&record)==VMT_GATED);
    assert(o.begin==1 && !o.reads && !o.borrow && !o.end);
    o=(struct owner){.flags=31};
    assert(vmp_capture(&provider,VMT_BT_FIELDS,3,now_us,NULL,&record)==VMT_OK);
    assert(o.begin==1 && o.end==1 && o.reads==8 && !o.borrow && record.valid==255);
    for(unsigned fail=1;fail<=8;fail++){
        o=(struct owner){.flags=31,.fail=fail};
        assert(vmp_capture(&provider,VMT_BT_FIELDS,4,now_us,NULL,&record)==VMT_READ_FAILED);
        assert(o.begin==1 && o.end==1 && o.reads==fail && !o.borrow);
    }
    o=(struct owner){.flags=31};
    assert(vmp_capture(&provider,VMT_CAMERA_FIELDS,5,now_us,NULL,&record)==VMT_BOUNDS);
    assert(o.begin==1 && o.end==1 && !o.reads && !o.borrow);
    o=(struct owner){.flags=31};
    assert(vmp_capture(&provider,0x10000,6,now_us,NULL,&record)==VMT_BAD_ARGUMENT);
    assert(!o.begin && !o.end && !o.reads);
    puts("Media worker candidate: fixed commands, gated default, borrowed lease release on every capture path PASS");
}
