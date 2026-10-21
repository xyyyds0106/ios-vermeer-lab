/* K70 baseband protocol foundation. Original bounded wire-format helpers.
 * Transport-independent: no MMIO, firmware loading, sockets or device state.
 * Wire definitions are documented in BASEBAND.md. Buffers remain caller-owned.
 * QRTR v1 and v3 without optional headers are supported. This is not QMUX:
 * QRTR service endpoints carry a seven-byte QMI header, including a 16-bit txn.
 */
#ifndef VERMEER_BASEBAND_PROTOCOL_H
#define VERMEER_BASEBAND_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

#define VBB_CTRL UINT32_C(0xfffffffe)
#define VBB_BCAST UINT32_C(0xffffffff)
#define VBB_MAX_PAYLOAD 65536u
enum { VBB_DATA=1, VBB_HELLO=2, VBB_BYE=3, VBB_NEW_SERVER=4,
       VBB_DEL_SERVER=5, VBB_DEL_CLIENT=6, VBB_RESUME_TX=7,
       VBB_EXIT=8, VBB_PING=9, VBB_NEW_LOOKUP=10, VBB_DEL_LOOKUP=11 };
enum { VBB_WDS=1, VBB_DMS=2, VBB_NAS=3, VBB_UIM=11 };

static inline uint16_t vbb_u16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1]<<8); }
static inline uint32_t vbb_u32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static inline void vbb_put16(uint8_t *p,uint16_t v)
{ p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static inline void vbb_put32(uint8_t *p,uint32_t v)
{ p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24); }

struct vbb_packet {
    uint32_t type,src_node,src_port,dst_node,dst_port,confirm_rx;
    const uint8_t *payload;
    size_t size;
};
/* Failure clears the output. Aligned wire length includes padding; size does not.
 * Nonzero confirm_rx obliges a future transport to send RESUME_TX after consuming
 * the packet. A decoder alone does not implement flow control or discovery.
 */
static inline int vbb_qrtr_decode(const uint8_t *p,size_t n,struct vbb_packet *out)
{
    struct vbb_packet v={0};size_t header;
    if(!out)return 0;
    *out=v;
    if(!p || n<16 || n%4)return 0;
    if(p[0]==1) {
        if(n<32 || vbb_u32(p)!=1)return 0;
        header=32;v.type=vbb_u32(p+4);v.src_node=vbb_u32(p+8);
        v.src_port=vbb_u32(p+12);v.confirm_rx=vbb_u32(p+16);
        v.size=vbb_u32(p+20);v.dst_node=vbb_u32(p+24);v.dst_port=vbb_u32(p+28);
        if(v.confirm_rx>1)return 0;
    } else if(p[0]==3) {
        if(p[2]>1 || p[3])return 0; /* optional headers need a separate audited format */
        header=16;v.type=p[1];v.confirm_rx=p[2];v.size=vbb_u32(p+4);
        v.src_node=vbb_u16(p+8);v.src_port=vbb_u16(p+10);
        v.dst_node=vbb_u16(p+12);v.dst_port=vbb_u16(p+14);
        if(v.src_port==0xfffe)v.src_port=VBB_CTRL;
        if(v.dst_port==0xfffe)v.dst_port=VBB_CTRL;
    } else return 0;
    if(v.dst_port==0xffff)v.dst_port=VBB_CTRL; /* legacy control destination */
    if(v.type<VBB_DATA || v.type>VBB_DEL_LOOKUP || !v.size || v.size>VBB_MAX_PAYLOAD ||
       n-header!=((v.size+3)&~(size_t)3))return 0;
    if(v.type!=VBB_DATA && v.type!=VBB_RESUME_TX && v.dst_port!=VBB_CTRL)return 0;
    v.payload=p+header;*out=v;return 1;
}

struct vbb_server { uint32_t service,instance,node,port; };
/* Fully decode an announcement before a caller considers changing its table.
 * Endpoint identity is not inferred from the service ID or the SMEM host ID.
 */
static inline int vbb_qrtr_server(const struct vbb_packet *p,struct vbb_server *out)
{
    struct vbb_server v={0};
    if(!out)return 0;
    *out=v;
    if(!p || !p->payload || p->size!=20 ||
       (p->type!=VBB_NEW_SERVER && p->type!=VBB_DEL_SERVER) ||
       p->dst_port!=VBB_CTRL || vbb_u32(p->payload)!=p->type)return 0;
    v.service=vbb_u32(p->payload+4);v.instance=vbb_u32(p->payload+8);
    v.node=vbb_u32(p->payload+12);v.port=vbb_u32(p->payload+16);
    if(v.node==VBB_BCAST || v.port==VBB_CTRL || v.port==VBB_BCAST)return 0;
    *out=v;return 1;
}

struct vbb_qmi {
    uint8_t type;
    uint16_t transaction,message;
    const uint8_t *tlvs;
    size_t size;
};
/* Validate the entire TLV sequence before publishing any field. Duplicate types
 * are rejected in this bring-up subset rather than choosing first/last silently.
 */
static inline int vbb_qmi_decode(const uint8_t *p,size_t n,struct vbb_qmi *out)
{
    struct vbb_qmi v={0};uint8_t seen[32]={0};size_t at=7;
    if(!out)return 0;
    *out=v;
    if(!p || n<7 || n>VBB_MAX_PAYLOAD || (p[0]!=0 && p[0]!=2 && p[0]!=4) ||
       n-7!=vbb_u16(p+5))return 0;
    while(at<n) {
        if(n-at<3)return 0;
        unsigned type=p[at],len=vbb_u16(p+at+1),bit=1u<<(type&7);
        if(seen[type>>3]&bit || len>n-at-3)return 0;
        seen[type>>3]|=(uint8_t)bit;at+=3+len;
    }
    v.type=p[0];v.transaction=vbb_u16(p+1);v.message=vbb_u16(p+3);
    v.tlvs=p+7;v.size=n-7;*out=v;return 1;
}

/* Input must be the immutable view returned by vbb_qmi_decode. Still bounds-check
 * iteration so malformed caller state cannot cause an out-of-bounds length read.
 */
static inline const uint8_t *vbb_qmi_tlv(const struct vbb_qmi *v,uint8_t type,size_t *length)
{
    if(!length)return NULL;
    *length=0;
    if(!v || !v->tlvs || v->size>VBB_MAX_PAYLOAD)return NULL;
    for(size_t at=0;at<v->size;) {
        if(v->size-at<3)return NULL;
        size_t n=vbb_u16(v->tlvs+at+1);
        if(n>v->size-at-3)return NULL;
        if(v->tlvs[at]==type){*length=n;return v->tlvs+at+3;}
        at+=3+n;
    }
    return NULL;
}

/* Only these four read-only messages can be built. Service discovery, transport
 * endpoint ownership, timeout and restart generation checks belong to the caller.
 * No dial, attach, APN/profile write, PIN attempt or roaming-policy write exists.
 */
static inline uint16_t vbb_query_message(uint32_t service)
{
    switch(service) {
    case VBB_WDS:return 0x22; /* packet service status */
    case VBB_DMS:return 0x2d; /* operating mode */
    case VBB_NAS:return 0x24; /* serving system */
    case VBB_UIM:return 0x2f; /* card status */
    default:return 0;
    }
}
static inline size_t vbb_qmi_query(uint32_t service,uint16_t transaction,uint8_t *out,size_t cap)
{
    uint16_t message=vbb_query_message(service);
    if(!out || cap<7 || !message || !transaction)return 0;
    out[0]=0;vbb_put16(out+1,transaction);vbb_put16(out+3,message);vbb_put16(out+5,0);
    return 7;
}

/* Returns a validated response even for modem errors, which are preserved.
 * A successful decode is not a successful QMI operation: result must equal zero.
 */
static inline int vbb_qmi_response(const uint8_t *p,size_t n,uint16_t transaction,
    uint16_t message,struct vbb_qmi *out,uint16_t *result,uint16_t *error)
{
    struct vbb_qmi v={0};size_t length;const uint8_t *status;
    if(out)*out=v;
    if(result)*result=UINT16_MAX;
    if(error)*error=UINT16_MAX;
    if(!out || !result || !error || !transaction || !vbb_qmi_decode(p,n,&v) ||
       v.type!=2 || v.transaction!=transaction || v.message!=message)return 0;
    status=vbb_qmi_tlv(&v,2,&length);
    if(!status || length!=4)return 0;
    uint16_t r=vbb_u16(status),e=vbb_u16(status+2);
    if(r>1 || (r==0 && e!=0) || (r==1 && e==0))return 0;
    *result=r;*error=e;*out=v;return 1;
}
#endif
