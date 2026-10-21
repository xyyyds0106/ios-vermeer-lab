/* Isolated, allocation-free cellular service candidate. No hardware/backend.
 * Initialize once per boot; serialize every API call on one native worker.
 * See CELLULAR-SERVICE.md for the REQUIRED native QRTR and policy contracts.
 */
#ifndef VERMEER_CELLULAR_H
#define VERMEER_CELLULAR_H
#include "../vermeer_baseband_protocol.h"

#define VCELL_VOICE 9u
#define VCELL_SERVICES 5u
#define VCELL_REQUEST_CAP 128u
#define VCELL_FRESH_MS UINT64_C(5000)
enum vcell_operation { VCELL_QUERY_WDS=1, VCELL_QUERY_DMS, VCELL_QUERY_NAS,
    VCELL_QUERY_UIM, VCELL_DIAL, VCELL_HANGUP, VCELL_DATA_START, VCELL_DATA_STOP };
enum vcell_error { VCELL_OK=0, VCELL_INVALID=-1, VCELL_OFFLINE=-2,
    VCELL_BUSY=-3, VCELL_NO_SERVICE=-4, VCELL_POLICY=-5, VCELL_STALE=-6,
    VCELL_TIMEOUT=-7, VCELL_TRANSPORT=-8, VCELL_PROTOCOL=-9,
    VCELL_MODEM=-10, VCELL_UNCERTAIN=-11, VCELL_NOT_SENT=-12,
    VCELL_EXHAUSTED=-13 };
enum vcell_roaming { VCELL_ROAM_UNKNOWN=0, VCELL_HOME=1, VCELL_ROAMING=2 };

struct vcell_policy {
    uint8_t data_enabled, allow_roaming, ip_family;
    uint8_t apn_length;
    char apn[101]; /* explicit ASCII APN, no credentials or carrier defaults */
};
struct vcell_nas {
    uint8_t valid, registration, cs_attach, ps_attach, network, roaming;
    uint8_t plmn_valid; /* numeric MNC has no digit-width evidence */
    uint16_t mcc, mnc;
    uint64_t at_ms;
};
struct vcell_request {
    uint64_t generation;
    struct vbb_server endpoint;
    uint16_t transaction, message;
    uint8_t operation;
    size_t size;
    uint8_t bytes[VCELL_REQUEST_CAP]; /* owned copy; never log number/APN */
};
struct vcell_completion {
    uint8_t operation;
    uint16_t transaction, result, modem_error;
    int status;
};
struct vcell {
    uint64_t generation, last_ms, deadline;
    uint32_t local_node, local_port, remote_node, next_transaction;
    uint32_t instances[VCELL_SERVICES];
    struct vbb_server servers[VCELL_SERVICES];
    uint8_t present[VCELL_SERVICES], bound, pending, sent;
    uint8_t uncertain; /* sticky: unresolved mutation/resource after loss */
    uint8_t dms_valid, operating_mode, wds_valid, packet_status;
    uint64_t dms_at, wds_at;
    uint8_t call_id_known, call_id, handle_known, stop_required;
    uint32_t data_handle;
    struct vcell_policy policy;
    struct vcell_nas nas;
    struct vcell_request request;
    struct vcell_completion completion;
};

static inline uint32_t vcell_service(unsigned i)
{
    const uint32_t ids[VCELL_SERVICES]={VBB_WDS,VBB_DMS,VBB_NAS,VBB_UIM,VCELL_VOICE};
    return i<VCELL_SERVICES?ids[i]:0;
}
static inline int vcell_index(uint32_t service)
{
    for(unsigned i=0;i<VCELL_SERVICES;i++)if(vcell_service(i)==service)return (int)i;
    return -1;
}
static inline int vcell_mutation(unsigned op) { return op>=VCELL_DIAL && op<=VCELL_DATA_STOP; }
static inline void vcell_erase_request(struct vcell *c)
{
    struct vcell_request empty={0};c->request=empty;c->pending=0;c->sent=0;c->deadline=0;
}
static inline void vcell_init(struct vcell *c)
{
    if(c){struct vcell empty={0};*c=empty;c->next_transaction=1;}
}
/* No stop/hangup is sent here. Loss of a known resource or possibly delivered
 * mutation latches uncertainty across reconnect; it cannot be cleared by API.
 */
static inline void vcell_disconnect(struct vcell *c,int reason)
{
    if(!c)return;
    if(c->handle_known || c->call_id_known ||
       (c->pending && c->sent && vcell_mutation(c->request.operation)))c->uncertain=1;
    if(c->pending) {
        c->completion.operation=c->request.operation;
        c->completion.transaction=c->request.transaction;
        c->completion.result=UINT16_MAX;c->completion.modem_error=UINT16_MAX;
    }
    c->completion.status=c->uncertain?VCELL_UNCERTAIN:reason;
    c->bound=0;c->dms_valid=0;c->wds_valid=0;c->nas.valid=0;
    c->handle_known=0;c->data_handle=0;c->call_id_known=0;c->call_id=0;
    c->stop_required=0;
    for(unsigned i=0;i<VCELL_SERVICES;i++) {
        struct vbb_server empty={0};c->servers[i]=empty;c->present[i]=0;
    }
    vcell_erase_request(c);
}
static inline int vcell_tick(struct vcell *c,uint64_t now)
{
    if(!c)return VCELL_INVALID;
    if(now<c->last_ms){vcell_disconnect(c,VCELL_STALE);return VCELL_STALE;}
    c->last_ms=now;
    if(c->pending && now>=c->deadline) {
        vcell_disconnect(c,VCELL_TIMEOUT);return c->completion.status;
    }
    return c->bound?VCELL_OK:VCELL_OFFLINE;
}
/* Transport supplies an independently verified peer and full instance values.
 * Binding a transport is NOT a modem/SIM/network-ready state. Never reset this
 * context on reconnect: generations increase and transaction IDs never wrap.
 */
static inline int vcell_connect(struct vcell *c,uint64_t generation,
    uint32_t local_node,uint32_t local_port,uint32_t remote_node,
    const uint32_t instances[VCELL_SERVICES],uint64_t now)
{
    if(!c || !instances || !generation || generation<=c->generation || c->bound ||
       now<c->last_ms || !local_port || local_port>=VBB_CTRL ||
       local_node>=VBB_CTRL || remote_node>=VBB_CTRL || local_node==remote_node ||
       !c->next_transaction)return VCELL_INVALID;
    c->generation=generation;c->last_ms=now;c->local_node=local_node;
    c->local_port=local_port;c->remote_node=remote_node;c->bound=1;
    for(unsigned i=0;i<VCELL_SERVICES;i++)c->instances[i]=instances[i];
    return VCELL_OK;
}
static inline int vcell_policy_valid(const struct vcell_policy *p)
{
    if(!p || p->data_enabled>1 || p->allow_roaming>1 ||
       (p->ip_family!=4 && p->ip_family!=6) || !p->apn_length ||
       p->apn_length>100 || p->apn[p->apn_length])return 0;
    unsigned label=0;
    for(unsigned i=0;i<p->apn_length;i++) {
        unsigned ch=(uint8_t)p->apn[i];
        if(ch=='.') {if(!label || p->apn[i-1]=='-')return 0;label=0;continue;}
        if(!((ch>='a' && ch<='z') || (ch>='A' && ch<='Z') ||
             (ch>='0' && ch<='9') || ch=='-') || (!label && ch=='-') || ++label>63)return 0;
    }
    return label && p->apn[p->apn_length-1]!='-';
}
static inline int vcell_fresh(uint8_t valid,uint64_t at,uint64_t now)
{ return valid && now>=at && now-at<VCELL_FRESH_MS; }
/* Gate for a future user-plane adapter. It MUST close on every false result,
 * including snapshot expiry, unknown roaming, policy change and disconnect.
 * true is policy eligibility, never evidence of internet reachability.
 */
static inline int vcell_data_eligible(const struct vcell *c,uint64_t now)
{
    return c && now>=c->last_ms && c->bound && !c->uncertain && !c->stop_required &&
        c->policy.data_enabled && vcell_policy_valid(&c->policy) &&
        vcell_fresh(c->dms_valid,c->dms_at,now) && c->operating_mode==0 &&
        vcell_fresh(c->nas.valid,c->nas.at_ms,now) &&
        c->nas.registration==1 && c->nas.ps_attach==1 &&
        (c->nas.roaming==VCELL_HOME ||
         (c->nas.roaming==VCELL_ROAMING && c->policy.allow_roaming));
}
static inline int vcell_user_plane_allowed(const struct vcell *c,uint64_t now)
{
    return vcell_data_eligible(c,now) && c->handle_known &&
        vcell_fresh(c->wds_valid,c->wds_at,now) && c->packet_status==2;
}
static inline int vcell_stop_needed(const struct vcell *c,uint64_t now)
{ return c && c->handle_known && !vcell_data_eligible(c,now); }
static inline int vcell_set_policy(struct vcell *c,const struct vcell_policy *p,uint64_t now)
{
    if(!c || !vcell_policy_valid(p))return VCELL_INVALID;
    int rc=vcell_tick(c,now);if(rc!=VCELL_OK && rc!=VCELL_OFFLINE)return rc;
    /* Cancel an unsent request. A sent start may race the new policy; keep
     * waiting to learn its handle, but close the gate and require a stop. */
    if(c->pending && c->request.operation==VCELL_DATA_START) {
        if(!c->sent) {c->completion.status=VCELL_NOT_SENT;vcell_erase_request(c);}
        else c->stop_required=1;
    }
    if(c->handle_known)c->stop_required=1; /* APN/IP changes also need teardown */
    c->policy=*p;return VCELL_OK;
}
static inline void vcell_invalidate_query(struct vcell *c,unsigned op)
{
    if(op==VCELL_QUERY_DMS)c->dms_valid=0;
    if(op==VCELL_QUERY_WDS)c->wds_valid=0;
    if(op==VCELL_QUERY_NAS)c->nas.valid=0;
}
static inline size_t vcell_tlv(uint8_t *out,size_t at,uint8_t type,const uint8_t *p,size_t n)
{
    out[at]=type;vbb_put16(out+at+1,(uint16_t)n);
    for(size_t i=0;i<n;i++)out[at+3+i]=p[i];
    return at+3+n;
}
/* Pure preparation; NEVER sends. permit=1 is required per mutating request;
 * it must come from the integration owner's separate user authorization.
 * The output is cleared on failure. A prepared buffer is a value copy.
 */
static inline int vcell_prepare(struct vcell *c,unsigned op,const char *number,
    size_t number_length,int permit,uint64_t now,uint32_t timeout_ms,struct vcell_request *out)
{
    struct vcell_request r={0};uint32_t service;uint16_t message;
    if(!out)return VCELL_INVALID;
    *out=r;
    if(!c || op<VCELL_QUERY_WDS || op>VCELL_DATA_STOP || !timeout_ms ||
       timeout_ms>60000 || now>UINT64_MAX-timeout_ms)return VCELL_INVALID;
    int rc=vcell_tick(c,now);if(rc!=VCELL_OK)return rc;
    if(c->pending)return VCELL_BUSY;
    if(vcell_mutation(op) && permit!=1)return VCELL_POLICY;
    if(vcell_mutation(op) && c->uncertain)return VCELL_UNCERTAIN;
    switch(op) {
    case VCELL_QUERY_WDS:service=VBB_WDS;message=0x22;break;
    case VCELL_QUERY_DMS:service=VBB_DMS;message=0x2d;break;
    case VCELL_QUERY_NAS:service=VBB_NAS;message=0x24;break;
    case VCELL_QUERY_UIM:service=VBB_UIM;message=0x2f;break;
    case VCELL_DIAL:
        service=VCELL_VOICE;message=0x20;
        if(c->call_id_known)return VCELL_BUSY;
        if(!vcell_fresh(c->dms_valid,c->dms_at,now) || c->operating_mode!=0 ||
           !vcell_fresh(c->nas.valid,c->nas.at_ms,now) || c->nas.registration!=1)return VCELL_STALE;
        if(!number || !number_length || number_length>32)return VCELL_INVALID;
        for(size_t i=0;i<number_length;i++)
            if(!((number[i]>='0' && number[i]<='9') || (i==0 && number[i]=='+' && number_length>1)))return VCELL_INVALID;
        r.size=vcell_tlv(r.bytes,7,1,(const uint8_t *)number,number_length);break;
    case VCELL_HANGUP:
        service=VCELL_VOICE;message=0x21;
        if(!c->call_id_known)return VCELL_STALE;
        r.size=vcell_tlv(r.bytes,7,1,&c->call_id,1);break;
    case VCELL_DATA_START: {
        service=VBB_WDS;message=0x20;
        if(c->handle_known)return VCELL_BUSY;
        if(!vcell_data_eligible(c,now))return VCELL_POLICY;
        if(!vcell_fresh(c->wds_valid,c->wds_at,now) || c->packet_status!=1)return VCELL_STALE;
        r.size=vcell_tlv(r.bytes,7,0x14,(const uint8_t *)c->policy.apn,c->policy.apn_length);
        r.size=vcell_tlv(r.bytes,r.size,0x19,&c->policy.ip_family,1);
        const uint8_t off=0;r.size=vcell_tlv(r.bytes,r.size,0x33,&off,1);break;
    }
    case VCELL_DATA_STOP: {
        service=VBB_WDS;message=0x21;
        if(!c->handle_known)return VCELL_STALE;
        uint8_t handle[4];vbb_put32(handle,c->data_handle);
        r.size=vcell_tlv(r.bytes,7,1,handle,4);
        const uint8_t off=1;r.size=vcell_tlv(r.bytes,r.size,0x10,&off,1);break;
    }
    default:return VCELL_INVALID;
    }
    int slot=vcell_index(service);
    if(!c->present[slot])return VCELL_NO_SERVICE;
    if(c->next_transaction>UINT16_MAX)return VCELL_EXHAUSTED;
    r.generation=c->generation;r.endpoint=c->servers[slot];r.operation=(uint8_t)op;
    r.transaction=(uint16_t)c->next_transaction++;r.message=message;
    if(!r.size)r.size=7;
    r.bytes[0]=0;vbb_put16(r.bytes+1,r.transaction);vbb_put16(r.bytes+3,message);
    vbb_put16(r.bytes+5,(uint16_t)(r.size-7));
    c->request=r;c->pending=1;c->sent=0;c->deadline=now+timeout_ms;
    c->completion=(struct vcell_completion){(uint8_t)op,r.transaction,UINT16_MAX,UINT16_MAX,VCELL_BUSY};
    vcell_invalidate_query(c,op);
    if(op==VCELL_DATA_STOP)c->stop_required=1;
    *out=r;return VCELL_OK;
}
/* Revalidate and copy under the native common lock immediately before send;
 * retain that lock through native atomic-copy and send_complete. */
static inline int vcell_pending_request(struct vcell *c,uint64_t generation,
    uint16_t transaction,uint64_t now,struct vcell_request *out)
{
    if(!out)return VCELL_INVALID;
    *out=(struct vcell_request){0};
    if(!c || generation!=c->generation)return VCELL_STALE;
    int rc=vcell_tick(c,now);if(rc!=VCELL_OK)return rc;
    if(!c->pending || c->sent || transaction!=c->request.transaction)return VCELL_STALE;
    if(c->request.operation==VCELL_DATA_START &&
       (!vcell_data_eligible(c,now) || !vcell_fresh(c->wds_valid,c->wds_at,now) || c->packet_status!=1)) {
        c->completion.status=VCELL_POLICY;vcell_erase_request(c);return VCELL_POLICY;
    }
    if(c->request.operation==VCELL_DIAL &&
       (!vcell_fresh(c->dms_valid,c->dms_at,now) || c->operating_mode!=0 ||
        !vcell_fresh(c->nas.valid,c->nas.at_ms,now) || c->nas.registration!=1)) {
        c->completion.status=VCELL_STALE;vcell_erase_request(c);return VCELL_STALE;
    }
    *out=c->request;return VCELL_OK;
}
/* Native send result: 1=whole datagram copied/accepted, 0=definitely no bytes
 * accepted (including EAGAIN), -1=delivery uncertain. No automatic retries.
 * Caller must validate epoch and claim the prepared request under the same lock
 * used by disconnect/policy before sending. A retained output is not a lease.
 */
static inline int vcell_send_complete(struct vcell *c,uint64_t generation,
    uint16_t transaction,int accepted,uint64_t now)
{
    if(!c || !c->bound || generation!=c->generation || !c->pending ||
       c->sent || transaction!=c->request.transaction)return VCELL_STALE;
    if(accepted!=0 && vcell_mutation(c->request.operation) &&
       (now<c->last_ms || now>=c->deadline)) {
        c->sent=1;vcell_disconnect(c,now<c->last_ms?VCELL_STALE:VCELL_TIMEOUT);
        return c->completion.status;
    }
    int rc=vcell_tick(c,now);if(rc!=VCELL_OK)return rc;
    if(accepted==1){
        struct vcell_request checked;
        rc=vcell_pending_request(c,generation,transaction,now,&checked);
        if(rc!=VCELL_OK) {
            /* A provider which sent without a current claim cannot retract
             * the bytes. Retain an uncertain outcome instead of retrying. */
            c->uncertain=1;vcell_disconnect(c,VCELL_UNCERTAIN);return VCELL_UNCERTAIN;
        }
        c->sent=1;return VCELL_OK;
    }
    if(accepted==0){c->completion.status=VCELL_NOT_SENT;vcell_erase_request(c);return VCELL_NOT_SENT;}
    c->sent=1;vcell_disconnect(c,VCELL_TRANSPORT);return c->completion.status;
}
/* Strict supported NAS subset. Unknown/missing roaming remains unknown, never
 * inferred from PLMN. Legacy Current PLMN does not preserve the MNC digit count.
 */
static inline int vcell_decode_nas(const struct vbb_qmi *q,uint64_t now,struct vcell_nas *out)
{
    struct vcell_nas n={0};size_t len;const uint8_t *p=vbb_qmi_tlv(q,1,&len);
    if(!p || len<5 || len!=5u+p[4] || p[0]>4 || p[1]>2 || p[2]>2 || p[3]>2)return 0;
    n.registration=p[0];n.cs_attach=p[1];n.ps_attach=p[2];n.network=p[3];
    p=vbb_qmi_tlv(q,0x10,&len);
    if(p){if(len!=1)return 0;n.roaming=p[0]==0?VCELL_ROAMING:p[0]==1?VCELL_HOME:VCELL_ROAM_UNKNOWN;}
    p=vbb_qmi_tlv(q,0x12,&len);
    if(p) {
        if(len<5 || len!=5u+p[4] || vbb_u16(p)>999 || vbb_u16(p+2)>999)return 0;
        n.plmn_valid=1;n.mcc=vbb_u16(p);n.mnc=vbb_u16(p+2);
    }
    n.valid=1;n.at_ms=now;*out=n;return 1;
}
/* Registry events MUST originate in a native QRTR namespace that has validated
 * the remote route and consumed confirm_rx. Only the five exact configured
 * service+instance pairs are retained. No fallback to first service/SMEM host.
 */
static inline int vcell_server_event(struct vcell *c,uint64_t generation,
    const struct vbb_server *s,int removed,uint64_t now)
{
    if(!c || !s || generation!=c->generation)return VCELL_STALE;
    int rc=vcell_tick(c,now);if(rc!=VCELL_OK)return rc;
    int i=vcell_index(s->service);
    if(i<0 || s->instance!=c->instances[i])return VCELL_NO_SERVICE;
    if(s->node!=c->remote_node || !s->port || s->port>=VBB_CTRL || (removed!=0 && removed!=1))return VCELL_INVALID;
    if(removed) {
        if(!c->present[i] || c->servers[i].port!=s->port)return VCELL_STALE;
        vcell_disconnect(c,VCELL_NO_SERVICE);return c->completion.status;
    }
    for(unsigned j=0;j<VCELL_SERVICES;j++)if(j!=(unsigned)i && c->present[j] &&
        c->servers[j].port==s->port){vcell_disconnect(c,VCELL_PROTOCOL);return VCELL_PROTOCOL;}
    if(c->present[i] && c->servers[i].port!=s->port) {
        vcell_disconnect(c,VCELL_PROTOCOL);return c->completion.status;
    }
    c->servers[i]=*s;c->present[i]=1;return VCELL_OK;
}
/* Called only AFTER the native QRTR layer checks destination/route, consumes
 * confirm_rx (RESUME_TX), and dispatches to this local client. Source identity
 * and generation are checked again here. No state is published before that
 * native consumption point. Native flow control/restarts remain prerequisites.
 */
static inline int vcell_receive(struct vcell *c,uint64_t generation,uint32_t node,
    uint32_t port,const uint8_t *bytes,size_t size,uint64_t now)
{
    if(!c || generation!=c->generation)return VCELL_STALE;
    int rc=vcell_tick(c,now);if(rc!=VCELL_OK)return rc;
    if(!c->pending || !c->sent || node!=c->request.endpoint.node ||
       port!=c->request.endpoint.port)return VCELL_STALE;
    struct vbb_qmi q;uint16_t result,error;
    if(!vbb_qmi_decode(bytes,size,&q)) {vcell_disconnect(c,VCELL_PROTOCOL);return c->completion.status;}
    /* Indications are deliberately not interpreted as responses. */
    if(q.type!=2 || q.transaction!=c->request.transaction || q.message!=c->request.message)return VCELL_STALE;
    if(!vbb_qmi_response(bytes,size,c->request.transaction,c->request.message,&q,&result,&error)) {
        vcell_disconnect(c,VCELL_PROTOCOL);return c->completion.status;
    }
    c->completion.result=result;c->completion.modem_error=error;
    if(result) {
        if(c->request.operation==VCELL_DATA_START)c->stop_required=0;
        c->completion.status=VCELL_MODEM;vcell_erase_request(c);return VCELL_MODEM;
    }
    unsigned op=c->request.operation;size_t len;const uint8_t *p;
    int valid=1;
    switch(op) {
    case VCELL_QUERY_DMS:
        p=vbb_qmi_tlv(&q,1,&len);valid=p && len==1;
        if(valid){c->operating_mode=p[0];c->dms_valid=1;c->dms_at=now;}break;
    case VCELL_QUERY_WDS:
        p=vbb_qmi_tlv(&q,1,&len);valid=p && len==1 && p[0]<=4;
        if(valid){c->packet_status=p[0];c->wds_valid=1;c->wds_at=now;
            if(p[0]==1){c->handle_known=0;c->data_handle=0;c->stop_required=0;}}
        break;
    case VCELL_QUERY_NAS:valid=vcell_decode_nas(&q,now,&c->nas);break;
    case VCELL_QUERY_UIM:
        /* Wire result only; card/application/slot semantics are unsupported.
         * No SIM-ready bit or permission to attempt a PIN is synthesized. */
        p=vbb_qmi_tlv(&q,0x10,&len);valid=p && len>=9;break;
    case VCELL_DIAL:
        p=vbb_qmi_tlv(&q,0x10,&len);valid=p && len==1;
        if(valid){c->call_id=p[0];c->call_id_known=1;}break;
    case VCELL_HANGUP:
        p=vbb_qmi_tlv(&q,0x10,&len);valid=p && len==1 && p[0]==c->call_id;
        if(valid){c->call_id_known=0;c->call_id=0;}break;
    case VCELL_DATA_START:
        p=vbb_qmi_tlv(&q,1,&len);valid=p && len==4 && vbb_u32(p)!=0 && vbb_u32(p)!=UINT32_MAX;
        if(valid){c->data_handle=vbb_u32(p);c->handle_known=1;c->wds_valid=0;}break;
    case VCELL_DATA_STOP:c->handle_known=0;c->data_handle=0;c->stop_required=0;c->wds_valid=0;break;
    default:valid=0;break;
    }
    if(!valid){vcell_disconnect(c,VCELL_PROTOCOL);return c->completion.status;}
    c->completion.status=VCELL_OK;vcell_erase_request(c);
    if(c->handle_known && !vcell_data_eligible(c,now))c->stop_required=1;
    return VCELL_OK;
}
#endif
