#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_baseband_protocol.h"

static const uint8_t announcement[]={
    1,0,0,0, 4,0,0,0, 7,0,0,0, 0xfe,0xff,0xff,0xff,
    0,0,0,0, 20,0,0,0, 1,0,0,0, 0xfe,0xff,0xff,0xff,
    4,0,0,0, 3,0,0,0, 1,2,0,0, 7,0,0,0, 0x40,0,0,0
};
static const uint8_t response[]={
    2,0x34,0x12,0x24,0,7,0, 2,4,0,0,0,0,0
};

static void qrtr_cases(void)
{
    struct vbb_packet p;struct vbb_server s;uint8_t data[sizeof(announcement)];
    assert(vbb_qrtr_decode(announcement,sizeof(announcement),&p));
    assert(p.src_node==7 && p.dst_node==1 && p.size==20 && p.dst_port==VBB_CTRL);
    assert(vbb_qrtr_server(&p,&s));
    assert(s.service==VBB_NAS && s.instance==513 && s.node==7 && s.port==0x40);
    for(size_t i=0;i<sizeof(announcement);i++) {
        assert(!vbb_qrtr_decode(announcement,i,&p));assert(!p.payload && !p.size);
    }
    memcpy(data,announcement,sizeof(data));data[1]=1;
    assert(!vbb_qrtr_decode(data,sizeof(data),&p));
    memcpy(data,announcement,sizeof(data));data[16]=2;
    assert(!vbb_qrtr_decode(data,sizeof(data),&p));
    memcpy(data,announcement,sizeof(data));data[28]=0;
    assert(!vbb_qrtr_decode(data,sizeof(data),&p));
    memcpy(data,announcement,sizeof(data));memset(data+20,0xff,4);
    assert(!vbb_qrtr_decode(data,sizeof(data),&p));
    memcpy(data,announcement,sizeof(data));data[32]=VBB_DEL_SERVER;
    assert(vbb_qrtr_decode(data,sizeof(data),&p));
    assert(!vbb_qrtr_server(&p,&s) && !s.service);
    data[4]=VBB_DEL_SERVER;
    assert(vbb_qrtr_decode(data,sizeof(data),&p) && vbb_qrtr_server(&p,&s));
    /* Protocol v3 has 16-bit endpoints and a seven-byte QMI payload padded to 8. */
    uint8_t v3[]={3,1,1,0, 7,0,0,0, 7,0,0x40,0, 1,0,0x22,0, 0,1,0,0x24,0,0,0,0xa5};
    assert(vbb_qrtr_decode(v3,sizeof(v3),&p));
    assert(p.confirm_rx==1 && p.size==7 && p.payload==v3+16 && p.src_node==7);
    v3[3]=4;assert(!vbb_qrtr_decode(v3,sizeof(v3),&p));v3[3]=0;
    v3[2]=2;assert(!vbb_qrtr_decode(v3,sizeof(v3),&p));v3[2]=0;
    v3[14]=0xfe;v3[15]=0xff;
    assert(vbb_qrtr_decode(v3,sizeof(v3),&p) && p.dst_port==VBB_CTRL);
    v3[14]=0xff;
    assert(vbb_qrtr_decode(v3,sizeof(v3),&p) && p.dst_port==VBB_CTRL);
    assert(!vbb_qrtr_decode(NULL,0,&p));assert(!vbb_qrtr_decode(v3,sizeof(v3),NULL));
}

static void qmi_cases(void)
{
    const uint32_t services[]={VBB_WDS,VBB_DMS,VBB_NAS,VBB_UIM};
    const uint8_t ids[]={0x22,0x2d,0x24,0x2f};
    uint8_t out[10],data[32];struct vbb_qmi q;uint16_t result,error;size_t length;
    for(unsigned i=0;i<4;i++) {
        memset(out,0xa5,sizeof(out));
        assert(vbb_qmi_query(services[i],0x1234,out+1,7)==7);
        assert(out[0]==0xa5 && out[8]==0xa5 && out[9]==0xa5);
        const uint8_t golden[]={0,0x34,0x12,ids[i],0,0,0};
        assert(!memcmp(out+1,golden,7));
        assert(vbb_qmi_decode(out+1,7,&q) && q.message==ids[i] && q.transaction==0x1234);
        for(size_t n=0;n<7;n++) {
            memset(out,0xa5,sizeof(out));
            assert(!vbb_qmi_query(services[i],1,out,n));
            for(unsigned j=0;j<sizeof(out);j++)assert(out[j]==0xa5);
        }
    }
    for(unsigned i=0;i<256;i++)
        if(i!=VBB_WDS && i!=VBB_DMS && i!=VBB_NAS && i!=VBB_UIM)
            assert(!vbb_qmi_query(i,1,out,sizeof(out)));
    assert(!vbb_qmi_query(VBB_NAS,0,out,sizeof(out)));
    assert(vbb_qmi_response(response,sizeof(response),0x1234,0x24,&q,&result,&error));
    assert(!result && !error);
    assert(vbb_qmi_tlv(&q,2,&length) && length==4);
    assert(!vbb_qmi_tlv(&q,0x10,&length) && !length);
    for(size_t n=0;n<sizeof(response);n++) {
        assert(!vbb_qmi_response(response,n,0x1234,0x24,&q,&result,&error));
        assert(!q.tlvs && result==UINT16_MAX && error==UINT16_MAX);
    }
    assert(!vbb_qmi_response(response,sizeof(response),1,0x24,&q,&result,&error));
    assert(!vbb_qmi_response(response,sizeof(response),0x1234,0x22,&q,&result,&error));
    memcpy(data,response,sizeof(response));data[10]=1;data[12]=94;
    assert(vbb_qmi_response(data,sizeof(response),0x1234,0x24,&q,&result,&error));
    assert(result==1 && error==94); /* failure is retained, never reported online */
    data[10]=0;
    assert(!vbb_qmi_response(data,sizeof(response),0x1234,0x24,&q,&result,&error));
    memcpy(data,response,sizeof(response));data[0]=4;
    assert(vbb_qmi_decode(data,sizeof(response),&q));
    assert(!vbb_qmi_response(data,sizeof(response),0x1234,0x24,&q,&result,&error));
    /* Unknown optional TLVs remain available, but truncated and duplicate types fail. */
    memcpy(data,response,sizeof(response));data[5]=11;data[14]=0x99;data[15]=1;data[16]=0;data[17]=0x42;
    assert(vbb_qmi_decode(data,18,&q));
    assert(vbb_qmi_tlv(&q,0x99,&length)[0]==0x42 && length==1);
    data[14]=2;assert(!vbb_qmi_decode(data,18,&q));
    data[14]=0x99;data[15]=2;assert(!vbb_qmi_decode(data,18,&q));
    data[15]=1;data[5]=12;data[18]=0;
    assert(!vbb_qmi_decode(data,19,&q)); /* malformed trailing TLV */
    assert(!vbb_qmi_decode(NULL,0,&q));assert(!vbb_qmi_decode(response,sizeof(response),NULL));
}

static uint32_t random_word(uint32_t *state)
{ *state=*state*1664525u+1013904223u;return *state; }

static void malformed_cases(void)
{
    uint32_t seed=0x4b3730;
    for(unsigned iteration=0;iteration<100000;iteration++) {
        size_t n=random_word(&seed)%257;
        uint8_t *p=malloc(n?n:1);assert(p);
        for(size_t i=0;i<n;i++)p[i]=(uint8_t)(random_word(&seed)>>24);
        if(n>=sizeof(announcement) && iteration%3==0) {
            memcpy(p,announcement,sizeof(announcement));
            p[random_word(&seed)%n]^=(uint8_t)(1u<<(random_word(&seed)%8));
        }
        if(n>=sizeof(response) && iteration%3==1) {
            memcpy(p,response,sizeof(response));
            p[random_word(&seed)%n]^=(uint8_t)(1u<<(random_word(&seed)%8));
        }
        struct vbb_packet packet;struct vbb_server server;struct vbb_qmi q;
        uint16_t result,error;
        if(vbb_qrtr_decode(p,n,&packet)) {
            assert(packet.payload>=p && packet.payload+packet.size<=p+n);
            (void)vbb_qrtr_server(&packet,&server);
        }
        if(vbb_qmi_decode(p,n,&q)) {
            for(unsigned type=0;type<256;type++) {
                size_t length;const uint8_t *value=vbb_qmi_tlv(&q,(uint8_t)type,&length);
                if(value)assert(value>=p && value+length<=p+n);
            }
        }
        (void)vbb_qmi_response(p,n,0x1234,0x24,&q,&result,&error);
        free(p);
    }
    /* Exact cap, length overflow and unaligned input are independently checked. */
    uint8_t *p=calloc(1,32+VBB_MAX_PAYLOAD+4);assert(p);
    memcpy(p,announcement,32);vbb_put32(p+4,VBB_DATA);vbb_put32(p+20,VBB_MAX_PAYLOAD);
    struct vbb_packet packet;
    assert(vbb_qrtr_decode(p,32+VBB_MAX_PAYLOAD,&packet));
    vbb_put32(p+20,VBB_MAX_PAYLOAD+1);
    assert(!vbb_qrtr_decode(p,36+VBB_MAX_PAYLOAD,&packet));
    memcpy(p+1,announcement,sizeof(announcement));
    assert(vbb_qrtr_decode(p+1,sizeof(announcement),&packet));
    free(p);
}

int main(void)
{
    qrtr_cases();qmi_cases();malformed_cases();
    puts("Baseband QRTR/QMI wire bounds, queries, responses and 100000 malformed inputs: PASS");
    return 0;
}
