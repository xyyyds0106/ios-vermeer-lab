/* Receive-side QRTR adapter candidate. No SMEM/GLINK/physical I/O.
 * TX QMI framing/credit, namespace lookup, HELLO and MPSS lifecycle still belong
 * to the future native provider. Do not attach directly to an SMEM FIFO.
 */
#ifndef VERMEER_CELLULAR_QRTR_H
#define VERMEER_CELLULAR_QRTR_H
#include "vermeer_cellular.h"

/* Return 1 only after copying ALL 52 bytes to the native control TX queue.
 * Return 0/negative for failure. Caller serializes; callback must not reenter.
 * Native queue must own its copy, preserve ordering and report later link loss.
 */
typedef int (*vcell_qrtr_emit)(void *arg,const uint8_t *frame,size_t length);

static inline void vcell_qrtr_resume(const struct vbb_packet *p,uint8_t out[52])
{
    for(unsigned i=0;i<52;i++)out[i]=0;
    vbb_put32(out,1);vbb_put32(out+4,VBB_RESUME_TX);
    vbb_put32(out+8,p->dst_node);vbb_put32(out+12,p->dst_port);
    vbb_put32(out+20,20);vbb_put32(out+24,p->src_node);vbb_put32(out+28,p->src_port);
    vbb_put32(out+32,VBB_RESUME_TX);vbb_put32(out+36,p->dst_node);vbb_put32(out+40,p->dst_port);
}
/* Only a verified, point-to-point modem QRTR route is supported. A namespace
 * which forwards server announcements through another node needs its own
 * provenance adapter, not removal of these checks. Registry changes and QMI
 * responses are never published if the required RESUME_TX cannot be queued.
 */
static inline int vcell_qrtr_receive(struct vcell *c,uint64_t generation,
    const uint8_t *wire,size_t length,uint64_t now,vcell_qrtr_emit emit,void *arg)
{
    if(!c || generation!=c->generation)return VCELL_STALE;
    int rc=vcell_tick(c,now);if(rc!=VCELL_OK)return rc;
    struct vbb_packet p;
    if(!vbb_qrtr_decode(wire,length,&p))return VCELL_PROTOCOL;
    if(p.src_node!=c->remote_node)return VCELL_STALE;
    if(p.type==VBB_DATA) {
        if(p.dst_node!=c->local_node || p.dst_port!=c->local_port)return VCELL_STALE;
        int known=0;
        for(unsigned i=0;i<VCELL_SERVICES;i++)
            if(c->present[i] && c->servers[i].port==p.src_port)known=1;
        if(!known)return VCELL_NO_SERVICE;
        if(p.confirm_rx) {
            uint8_t ack[52];vcell_qrtr_resume(&p,ack);
            if(!emit || emit(arg,ack,sizeof(ack))!=1) {
                vcell_disconnect(c,VCELL_TRANSPORT);return c->completion.status;
            }
        }
        return vcell_receive(c,generation,p.src_node,p.src_port,p.payload,p.size,now);
    }
    if(p.src_port!=VBB_CTRL || p.dst_port!=VBB_CTRL ||
       (p.dst_node!=c->local_node && p.dst_node!=VBB_BCAST))return VCELL_STALE;
    if(p.confirm_rx)return VCELL_PROTOCOL; /* supported control packets never request credit */
    if(p.type==VBB_NEW_SERVER || p.type==VBB_DEL_SERVER) {
        struct vbb_server s;
        if(!vbb_qrtr_server(&p,&s))return VCELL_PROTOCOL;
        return vcell_server_event(c,generation,&s,p.type==VBB_DEL_SERVER,now);
    }
    if(p.type==VBB_BYE) {
        if(p.size!=20 || vbb_u32(p.payload)!=VBB_BYE)return VCELL_PROTOCOL;
        vcell_disconnect(c,VCELL_OFFLINE);return c->completion.status;
    }
    return VCELL_NO_SERVICE; /* HELLO/lookup/RESUME_TX belong to native routing */
}
#endif
