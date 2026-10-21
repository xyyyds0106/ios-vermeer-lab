/* Synthetic RX/credit ordering; no live transport callback exists. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../xnu/vermeer/cellular/vermeer_cellular_qrtr.h"

static const uint32_t instances[5]={1,2,3,4,5};
static size_t frame(uint8_t *out,uint32_t type,uint32_t src_port,uint32_t dst_port,
    const uint8_t *payload,size_t size,int confirm)
{
    size_t n=32+((size+3)&~(size_t)3);memset(out,0,n);
    vbb_put32(out,1);vbb_put32(out+4,type);vbb_put32(out+8,7);vbb_put32(out+12,src_port);
    vbb_put32(out+16,(uint32_t)confirm);vbb_put32(out+20,(uint32_t)size);
    vbb_put32(out+24,20);vbb_put32(out+28,dst_port);memcpy(out+32,payload,size);return n;
}
static void announce(struct vcell *c,uint8_t *wire)
{
    uint8_t server[]={4,0,0,0,2,0,0,0,2,0,0,0,7,0,0,0,81,0,0,0};
    size_t n=frame(wire,VBB_NEW_SERVER,VBB_CTRL,VBB_CTRL,server,20,0);
    assert(vcell_qrtr_receive(c,c->generation,wire,n,100,NULL,NULL)==VCELL_OK);
}
struct credit { struct vcell *c; unsigned calls; int result; uint8_t saved[52]; };
static int emit(void *arg,const uint8_t *bytes,size_t n)
{
    struct credit *a=arg;assert(n==52);assert(a->c->pending && !a->c->dms_valid);
    a->calls++;memcpy(a->saved,bytes,n);return a->result;
}
static struct vcell_request start(struct vcell *c,uint8_t *wire)
{
    vcell_init(c);assert(vcell_connect(c,1,20,600,7,instances,100)==VCELL_OK);announce(c,wire);
    struct vcell_request r;assert(vcell_prepare(c,VCELL_QUERY_DMS,NULL,0,0,100,10,&r)==VCELL_OK);
    assert(vcell_send_complete(c,1,r.transaction,1,100)==VCELL_OK);return r;
}
int main(void)
{
    struct vcell c;uint8_t wire[512];struct vcell_request r=start(&c,wire);
    const uint8_t response[]={2,1,0,0x2d,0,11,0,2,4,0,0,0,0,0,1,1,0,0};
    size_t n=frame(wire,VBB_DATA,81,600,response,sizeof(response),1);
    struct credit ack={&c,0,1,{0}};
    assert(vcell_qrtr_receive(&c,1,wire,n,100,emit,&ack)==VCELL_OK);
    assert(ack.calls==1 && c.dms_valid && !c.pending);
    const uint8_t expected[]={1,0,0,0,7,0,0,0,20,0,0,0,0x58,2,0,0,0,0,0,0,20,0,0,0,7,0,0,0,81,0,0,0,
        7,0,0,0,20,0,0,0,0x58,2,0,0,0,0,0,0,0,0,0,0};
    assert(!memcmp(ack.saved,expected,52));
    struct vbb_packet packet;assert(vbb_qrtr_decode(ack.saved,52,&packet));
    assert(packet.type==VBB_RESUME_TX && packet.dst_node==7 && packet.dst_port==81 && !packet.confirm_rx);
    for(int result=-1;result<=0;result++) {
        r=start(&c,wire);n=frame(wire,VBB_DATA,81,600,response,sizeof(response),1);
        ack=(struct credit){&c,0,result,{0}};
        assert(vcell_qrtr_receive(&c,1,wire,n,100,emit,&ack)==VCELL_TRANSPORT);
        assert(ack.calls==1 && !c.dms_valid && !c.bound);
    }
    r=start(&c,wire);n=frame(wire,VBB_DATA,81,600,response,sizeof(response),1);
    assert(vcell_qrtr_receive(&c,1,wire,n,100,NULL,NULL)==VCELL_TRANSPORT && !c.dms_valid);
    r=start(&c,wire);n=frame(wire,VBB_DATA,81,600,response,sizeof(response),0);
    assert(vcell_qrtr_receive(&c,1,wire,n,100,NULL,NULL)==VCELL_OK && c.dms_valid);
    r=start(&c,wire);n=frame(wire,VBB_DATA,81,600,response,sizeof(response),1);
    ack=(struct credit){&c,0,1,{0}};
    assert(vcell_qrtr_receive(&c,0,wire,n,100,emit,&ack)==VCELL_STALE);
    wire[8]=8;assert(vcell_qrtr_receive(&c,1,wire,n,100,emit,&ack)==VCELL_STALE);wire[8]=7;
    wire[28]=0;assert(vcell_qrtr_receive(&c,1,wire,n,100,emit,&ack)==VCELL_STALE);wire[28]=0x58;
    wire[12]=90;assert(vcell_qrtr_receive(&c,1,wire,n,100,emit,&ack)==VCELL_NO_SERVICE);
    assert(!ack.calls && c.pending && !c.dms_valid);
    /* A stale transaction is still consumed for credit but cannot publish QMI state. */
    wire[12]=81;wire[33]=2;
    assert(vcell_qrtr_receive(&c,1,wire,n,100,emit,&ack)==VCELL_STALE && ack.calls==1);
    wire[33]=1;
    /* QRTR v3 padded payload, same QMI contract. */
    memmove(wire+16,wire+32,20);memset(wire,0,16);
    wire[0]=3;wire[1]=VBB_DATA;wire[2]=1;vbb_put32(wire+4,18);
    vbb_put16(wire+8,7);vbb_put16(wire+10,81);vbb_put16(wire+12,20);vbb_put16(wire+14,600);
    assert(vcell_qrtr_receive(&c,1,wire,36,100,emit,&ack)==VCELL_OK && ack.calls==2);
    /* Server source provenance and destination are checked before registry mutation. */
    vcell_init(&c);assert(vcell_connect(&c,1,20,600,7,instances,100)==VCELL_OK);announce(&c,wire);
    wire[8]=8;assert(vcell_qrtr_receive(&c,1,wire,52,100,NULL,NULL)==VCELL_STALE);
    wire[8]=7;wire[16]=1;assert(vcell_qrtr_receive(&c,1,wire,52,100,NULL,NULL)==VCELL_PROTOCOL);
    wire[16]=0;wire[44]=8;assert(vcell_qrtr_receive(&c,1,wire,52,100,NULL,NULL)==VCELL_INVALID);
    const uint8_t bye[20]={3};n=frame(wire,VBB_BYE,VBB_CTRL,VBB_CTRL,bye,20,0);
    assert(vcell_qrtr_receive(&c,1,wire,n,100,NULL,NULL)==VCELL_OFFLINE && !c.bound);
    for(size_t cut=0;cut<52;cut++) {
        r=start(&c,wire);n=frame(wire,VBB_DATA,81,600,response,sizeof(response),1);
        assert(n==52);ack=(struct credit){&c,0,1,{0}};
        assert(vcell_qrtr_receive(&c,1,wire,cut,100,emit,&ack)==VCELL_PROTOCOL);
        assert(!ack.calls && !c.dms_valid);
    }
    uint32_t seed=0xace1;
    for(unsigned it=0;it<100000;it++) {
        r=start(&c,wire);seed=seed*1664525u+1013904223u;size_t length=seed%257;
        uint8_t *bytes=malloc(length?length:1);assert(bytes);
        for(size_t i=0;i<length;i++){seed=seed*1664525u+1013904223u;bytes[i]=(uint8_t)(seed>>24);}
        ack=(struct credit){&c,0,1,{0}};
        (void)vcell_qrtr_receive(&c,1,bytes,length,100,emit,&ack);
        assert(!c.dms_valid && !c.handle_known && !c.call_id_known);free(bytes);
    }
    (void)r;
    puts("SYNTHETIC ONLY: QRTR v1/v3 routes, RESUME_TX before publication, failure, restart, 100000 malformed frames: PASS");
    return 0;
}
