#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_bt_h4.h"

struct sink { unsigned packets, controls, type, control; size_t n; uint8_t data[4096]; };
static void packet(void *p, unsigned type, const uint8_t *data, size_t n)
{
    struct sink *s=p;
    assert(n<=sizeof(s->data));
    s->packets++; s->type=type; s->n=n; memcpy(s->data,data,n);
}
static void control(void *p, unsigned byte)
{
    struct sink *s=p;
    assert(byte>=0xfc && byte<=0xfe); s->controls++; s->control=byte;
}
static void reenter(void *p, unsigned t, const uint8_t *data, size_t n)
{
    (void)t; (void)data; (void)n;
    assert(!vbt_h4_feed(p,NULL,0,reenter,NULL,p));
}
static void splits(const uint8_t *wire, size_t n, unsigned type)
{
    for (size_t split=0; split<=n; ++split) {
        struct vbt_h4 r; struct sink s={0}; vbt_h4_init(&r);
        assert(vbt_h4_feed(&r,wire,split,packet,control,&s));
        assert(vbt_h4_feed(&r,wire+split,n-split,packet,control,&s));
        assert(s.packets==1 && s.type==type && s.n==n-1);
        assert(!memcmp(s.data,wire+1,n-1) && !r.type && !s.controls);
    }
    struct vbt_h4 r; struct sink s={0}; vbt_h4_init(&r);
    for (size_t i=0;i<n;++i) {
        assert(vbt_h4_feed(&r,wire+i,1,packet,control,&s));
        assert(s.packets==(i==n-1));
    }
}
int main(void)
{
    const uint8_t event[]={4,0x0e,4,1,3,0x0c,0}; /* Reset complete */
    const uint8_t acl[]={2,1,0,4,0,0xfc,0xfd,0xfe,4};
    const uint8_t sco[]={3,1,0,3,0xfc,0xfd,0xfe};
    const uint8_t iso[]={5,1,0,2,0,0xfd,0xfe};
    const uint8_t empty[]={4,0xff,0};
    splits(event,sizeof(event),4); splits(acl,sizeof(acl),2);
    splits(sco,sizeof(sco),3); splits(iso,sizeof(iso),5);
    splits(empty,sizeof(empty),4);
    struct vbt_h4 r; struct sink s={0}; vbt_h4_init(&r);
    const uint8_t stream[]={0xfd,4,0xff,0,0xfc,4,0xff,1,0xfe,0xfe};
    assert(vbt_h4_feed(&r,stream,sizeof(stream),packet,control,&s));
    assert(s.packets==2 && s.controls==3 && s.data[2]==0xfe);
    /* All event payload lengths, fragmented at every boundary. */
    uint8_t buf[VBT_H4_CAPACITY+1]; memset(buf,0xfd,sizeof(buf));
    buf[0]=4; buf[1]=0xff;
    for (unsigned n=0;n<=255;++n) { buf[2]=(uint8_t)n; splits(buf,n+3,4); }
    /* Capacity boundary: exact largest local ACL is accepted, next is not. */
    memset(buf,0xfe,sizeof(buf)); buf[0]=2; buf[1]=1; buf[2]=0;
    unsigned len=VBT_H4_CAPACITY-4;
    buf[3]=(uint8_t)len; buf[4]=(uint8_t)(len>>8);
    vbt_h4_init(&r); s=(struct sink){0};
    assert(vbt_h4_feed(&r,buf,sizeof(buf),packet,control,&s));
    assert(s.packets==1 && s.n==VBT_H4_CAPACITY);
    ++buf[3]; vbt_h4_init(&r); s=(struct sink){0};
    assert(!vbt_h4_feed(&r,buf,sizeof(buf),packet,control,&s));
    assert(!s.packets && !vbt_h4_feed(&r,event,sizeof(event),packet,control,&s));
    /* Exhaust all possible ACL advertised lengths without supplying payload. */
    for (unsigned n=0;n<65536;++n) {
        buf[3]=(uint8_t)n; buf[4]=(uint8_t)(n>>8);
        vbt_h4_init(&r); s=(struct sink){0};
        assert(vbt_h4_feed(&r,buf,5,packet,control,&s)==(n<=len));
        assert(s.packets==(n==0));
    }
    const uint8_t reserved_iso[]={5,1,0,0,0x40};
    vbt_h4_init(&r); assert(!vbt_h4_feed(&r,reserved_iso,5,packet,control,&s));
    const uint8_t bad[]={0,1,6,0xff,0xfc,0xfd,0xfe};
    for (unsigned i=0;i<sizeof(bad);++i) {
        vbt_h4_init(&r); assert(!vbt_h4_feed(&r,bad+i,1,packet,NULL,&s));
    }
    vbt_h4_init(&r); assert(!vbt_h4_feed(&r,NULL,1,packet,control,&s));
    vbt_h4_init(&r); assert(vbt_h4_feed(&r,NULL,0,packet,NULL,&s));
    assert(!vbt_h4_feed(&r,event,sizeof(event),reenter,NULL,&r));
    uint8_t dst[260]; memset(dst,0xa5,sizeof(dst));
    assert(!vbt_h4_command(dst,3,0x0c03,NULL,0) && dst[0]==0xa5);
    assert(!vbt_h4_command(dst,sizeof(dst),0,NULL,0));
    assert(!vbt_h4_command(dst,sizeof(dst),0x0c03,NULL,1));
    assert(!vbt_h4_command(dst,sizeof(dst),0x0c03,buf,256));
    assert(vbt_h4_command(dst,sizeof(dst),0x0c03,NULL,0)==4);
    assert(!memcmp(dst,"\x01\x03\x0c\x00",4));
    assert(vbt_h4_command(dst,sizeof(dst),0xfc01,buf,255)==259);
    assert(dst[3]==255 && !memcmp(dst+4,buf,255));
    uint8_t version_event[]={0x0e,12,1,1,0x10,0,12,0x34,0x12,12,0x1d,0,0x78,0x56};
    struct vbt_local_version v={0};
    assert(vbt_h4_local_version(version_event,sizeof(version_event),&v));
    assert(v.hci_version==12 && v.lmp_version==12 && v.command_credits==1);
    assert(v.hci_revision==0x1234 && v.manufacturer==0x1d && v.lmp_subversion==0x5678);
    struct vbt_local_version saved=v;
    for (size_t n=0;n<sizeof(version_event);++n)
        assert(!vbt_h4_local_version(version_event,n,&v));
    assert(!vbt_h4_local_version(version_event,sizeof(version_event)+1,&v));
    version_event[5]=1; assert(!vbt_h4_local_version(version_event,sizeof(version_event),&v));
    version_event[5]=0; version_event[4]=0;
    assert(!vbt_h4_local_version(version_event,sizeof(version_event),&v));
    assert(v.hci_version==saved.hci_version && v.lmp_version==saved.lmp_version);
    assert(v.hci_revision==saved.hci_revision && v.manufacturer==saved.manufacturer);
    assert(v.lmp_subversion==saved.lmp_subversion && v.command_credits==saved.command_credits);
    puts("Bluetooth H4 fragmentation/IBS/capacity/retirement/command bounds: PASS");
    return 0;
}
