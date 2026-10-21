#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_bt_identity.h"
#include "../xnu/vermeer/vermeer_bt_observe.h"

struct wire {
    uint8_t tx[4], rx[128]; size_t sent, received, length;
    unsigned flags, write_size, read_size, writes, reads;
    int write_error, read_error; uint64_t now, read_cost, write_cost;
    unsigned revoke_write, backwards_write;
    struct vbt_identity *reenter;
};
static int write_bytes(void *ctx,const uint8_t *data,size_t n)
{
    struct wire *w=ctx; ++w->writes;
    if(w->write_error) return w->write_error;
    if(n>w->write_size)n=w->write_size;
    assert(w->sent+n<=sizeof(w->tx));
    memcpy(w->tx+w->sent,data,n);w->sent+=n;
    w->now+=w->write_cost;
    if(w->revoke_write)w->flags=0;
    if(w->backwards_write)--w->now;
    return (int)n;
}
static int read_bytes(void *ctx,uint8_t *data,size_t n)
{
    struct wire *w=ctx;++w->reads;w->now+=w->read_cost;
    if(w->reenter)assert(vbt_identity_step(w->reenter)==VBT_ID_FAILED);
    if(w->read_error)return w->read_error;
    if(n>w->read_size)n=w->read_size;
    if(n>w->length-w->received)n=w->length-w->received;
    memcpy(data,w->rx+w->received,n);w->received+=n;return (int)n;
}
static uint64_t now_us(void *ctx){return ((struct wire *)ctx)->now;}
static unsigned ready(void *ctx){return ((struct wire *)ctx)->flags;}
static struct vbt_identity_io callbacks(struct wire *w)
{
    struct vbt_identity_io io={w,write_bytes,read_bytes,now_us,ready};return io;
}
static struct wire fixture(void)
{
    struct wire w={0};w.flags=15;w.write_size=4;w.read_size=64;
    const uint8_t event[]={4,0x0e,12,1,1,0x10,0,12,0x34,0x12,12,0x1d,0,0x78,0x56};
    memcpy(w.rx,event,sizeof(event));w.length=sizeof(event);return w;
}
static enum vbt_identity_state run(struct vbt_identity *s,struct wire *w)
{
    enum vbt_identity_state state;
    do {state=vbt_identity_step(s);++w->now;} while(state==VBT_ID_WAITING);
    return state;
}
struct reader { unsigned count, stop; };
static int read_register(void *ctx,unsigned bank,unsigned offset,uint32_t *out)
{
    struct reader *r=ctx;
    if(++r->count==r->stop)return 0;
    assert(bank<=1);
    if(bank==0)assert(offset>=0x151000 && offset<=0x153004 && !(offset&3u));
    else assert(offset<0x4000 && offset!=0x780 && offset!=0x700 && offset!=0x618);
    *out=0;return 1;
}
int main(void)
{
    for(unsigned tx=1;tx<=4;++tx)for(unsigned rx=1;rx<=64;++rx){
        struct wire w=fixture();w.write_size=tx;w.read_size=rx;
        struct vbt_identity_io io=callbacks(&w);struct vbt_identity s;
        assert(vbt_identity_begin(&s,&io,100000));
        /* Changing the caller's callback struct cannot redirect this query. */
        io.ctx=NULL;io.write=NULL;
        struct vbt_local_version v={0};assert(!vbt_identity_result(&s,&v));
        assert(run(&s,&w)==VBT_ID_DONE && vbt_identity_result(&s,&v));
        assert(w.sent==4 && !memcmp(w.tx,"\x01\x01\x10\x00",4));
        assert(v.manufacturer==0x1d && v.hci_revision==0x1234 && v.lmp_subversion==0x5678);
        unsigned calls=w.writes+w.reads;
        assert(vbt_identity_step(&s)==VBT_ID_DONE && w.writes+w.reads==calls);
    }
    for(unsigned flags=0;flags<15;++flags){
        struct wire w=fixture();w.flags=flags;
        struct vbt_identity_io io=callbacks(&w);struct vbt_identity s;
        assert(!vbt_identity_begin(&s,&io,100000));assert(!w.writes && !w.reads);
    }
    for(unsigned fault=0;fault<12;++fault){
        struct wire w=fixture();struct vbt_identity s;struct vbt_identity_io io=callbacks(&w);
        assert(vbt_identity_begin(&s,&io,100));
        switch(fault){
        case 0:w.write_error=-1;break;
        case 1:w.write_error=5;break;
        case 2:w.read_error=-1;break;
        case 3:w.read_error=65;break;
        case 4:w.rx[6]=1;break; /* nonzero HCI status */
        case 5:w.rx[5]=0;break; /* other opcode */
        case 6:w.now=100;break; /* deadline before I/O */
        case 7:w.read_cost=100;break; /* deadline during read */
        case 8:w.flags=14;break; /* ownership revoked */
        case 9:w.rx[0]=0xfd;break; /* unhandled IBS */
        case 10:w.reenter=&s;break;
        case 11:w.rx[1]=0x10;break; /* hardware error */
        }
        assert(run(&s,&w)==VBT_ID_FAILED);
        struct vbt_local_version v={.manufacturer=0xaaaa};
        assert(!vbt_identity_result(&s,&v) && v.manufacturer==0xaaaa);
        unsigned calls=w.writes+w.reads;
        assert(vbt_identity_step(&s)==VBT_ID_FAILED && w.writes+w.reads==calls);
    }
    /* Revocation/deadline/backwards time DURING a short or complete write
     * must fail before returning WAITING or issuing the first read.
     */
    for(unsigned chunk=1;chunk<=4;++chunk)for(unsigned fault=0;fault<3;++fault){
        struct wire w=fixture();struct vbt_identity s;struct vbt_identity_io io=callbacks(&w);
        w.now=10;w.write_size=chunk;
        assert(vbt_identity_begin(&s,&io,100));
        if(fault==0)w.revoke_write=1;
        else if(fault==1)w.write_cost=100;
        else w.backwards_write=1;
        assert(vbt_identity_step(&s)==VBT_ID_FAILED);
        assert(w.sent==chunk && w.writes==1 && !w.reads);
        assert(vbt_identity_step(&s)==VBT_ID_FAILED && w.writes==1 && !w.reads);
    }
    {struct wire w=fixture();struct vbt_identity s;struct vbt_identity_io io=callbacks(&w);
     w.write_size=0;assert(vbt_identity_begin(&s,&io,1000000));
     for(unsigned i=0;i<VBT_ID_MAX_STEPS;++i)assert(vbt_identity_step(&s)==VBT_ID_WAITING);
     assert(vbt_identity_step(&s)==VBT_ID_FAILED && !w.reads && !w.sent);}
    {struct wire w=fixture();struct vbt_identity s;struct vbt_identity_io io=callbacks(&w);
     w.read_size=0;w.now=10;assert(vbt_identity_begin(&s,&io,100));
     assert(vbt_identity_step(&s)==VBT_ID_WAITING);w.now=9;
     assert(vbt_identity_step(&s)==VBT_ID_FAILED);}
    {struct wire w=fixture();struct vbt_identity s;struct vbt_identity_io io=callbacks(&w);
     memmove(w.rx+3,w.rx,w.length);memcpy(w.rx,"\x04\xff\x00",3);w.length+=3;
     assert(vbt_identity_begin(&s,&io,100));assert(run(&s,&w)==VBT_ID_DONE);}
    {struct wire w=fixture();struct vbt_identity s;struct vbt_identity_io io=callbacks(&w);
     memcpy(w.rx+w.length,w.rx,w.length);w.length*=2;
     assert(vbt_identity_begin(&s,&io,100));assert(run(&s,&w)==VBT_ID_FAILED);}
    {struct wire w=fixture();struct vbt_identity s;struct vbt_identity_io io=callbacks(&w);
     assert(!vbt_identity_begin(&s,&io,0));assert(!vbt_identity_begin(&s,&io,1000001));}
    {struct wire w=fixture();struct vbt_identity s;struct vbt_identity_io io=callbacks(&w);
     w.length=0;assert(vbt_identity_begin(&s,&io,10));
     assert(run(&s,&w)==VBT_ID_FAILED && w.sent==4 && w.writes==1);}
    {struct wire w=fixture();struct vbt_identity s;struct vbt_identity_io io=callbacks(&w);
     w.write_size=1;assert(vbt_identity_begin(&s,&io,2));
     assert(run(&s,&w)==VBT_ID_FAILED && w.sent==2 && !w.reads);}
    {struct wire w=fixture();struct vbt_identity s;struct vbt_identity_io io=callbacks(&w);
     for(unsigned i=0;i<33;++i)memcpy(w.rx+3*i,"\x04\xff\x00",3);
     w.length=99;assert(vbt_identity_begin(&s,&io,100));
     assert(run(&s,&w)==VBT_ID_FAILED && w.sent==4);}
    struct vbt_observation obs;vbt_observation_init(&obs);struct reader reader={0};
    for(unsigned i=0;i<VBT_OBS_COUNT;++i)assert(vbt_obs_names[i] && vbt_obs_names[i][0]);
    assert(!vbt_observe_bank(&obs,0,0,read_register,&reader) && !reader.count);
    assert(vbt_strap_hint(&obs)==VBT_STRAP_UNKNOWN);
    assert(vbt_uart_observed_mode(&obs)==VBT_UART_UNKNOWN);
    assert(vbt_observe_bank(&obs,0,1,read_register,&reader) && reader.count==8);
    assert(vbt_strap_hint(&obs)==VBT_STRAP_KIWI);
    obs.values[VBT_STRAP_IO_A]=obs.values[VBT_STRAP_IO_B]=1;
    assert(vbt_strap_hint(&obs)==VBT_STRAP_QCA6490);
    obs.values[VBT_STRAP_IO_B]=0;assert(vbt_strap_hint(&obs)==VBT_STRAP_UNKNOWN);
    obs.values[VBT_STRAP_IO_B]=1;
    obs.values[VBT_STRAP_CFG_A]=obs.values[VBT_STRAP_CFG_B]=1u<<9;
    assert(vbt_strap_hint(&obs)==VBT_STRAP_UNKNOWN);
    obs.values[VBT_STRAP_CFG_A]=obs.values[VBT_STRAP_CFG_B]=1u<<2;
    assert(vbt_strap_hint(&obs)==VBT_STRAP_UNKNOWN);
    reader.count=0;reader.stop=3;
    assert(!vbt_observe_bank(&obs,1,1,read_register,&reader) && reader.count==3);
    assert(vbt_uart_observed_mode(&obs)==VBT_UART_UNKNOWN);
    reader.count=0;reader.stop=0;
    assert(vbt_observe_bank(&obs,1,1,read_register,&reader));
    assert(vbt_uart_observed_mode(&obs)==VBT_UART_OTHER_PROTOCOL);
    obs.values[VBT_FW_M]=obs.values[VBT_FW_S]=2u<<8;
    assert(vbt_uart_observed_mode(&obs)==VBT_UART_FIFO_OBSERVED);
    obs.values[VBT_DMA_MODE]=1;assert(vbt_uart_observed_mode(&obs)==VBT_UART_SE_DMA_OBSERVED);
    obs.values[VBT_IF_DISABLE]=1;assert(vbt_uart_observed_mode(&obs)==VBT_UART_GSI_REQUIRED);
    assert(!vbt_observe_bank(&obs,1,0,read_register,&reader));
    assert(vbt_uart_observed_mode(&obs)==VBT_UART_UNKNOWN);
    for(unsigned fail=1;fail<=VBT_OBS_COUNT-8;++fail){
        vbt_observation_init(&obs);reader=(struct reader){.stop=fail};
        assert(!vbt_observe_bank(&obs,1,1,read_register,&reader));
        assert(reader.count==fail);
        for(unsigned i=8;i<VBT_OBS_COUNT;++i)
            assert(!!(obs.valid&(1u<<i))==(i-8<fail-1));
    }
    obs.valid=0x3fffffffu;
    assert(!vbt_observe_bank(&obs,0,1,NULL,NULL) && (obs.valid&255u)==0);
    puts("Bluetooth identity short-I/O/deadline/ownership/faults and RO register plan: PASS");
    return 0;
}
