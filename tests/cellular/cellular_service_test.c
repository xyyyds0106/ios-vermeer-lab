/* Every packet/endpoint below is SYNTHETIC. No sockets, phone or radio backend. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../xnu/vermeer/cellular/vermeer_cellular.h"

static const uint32_t instances[5]={0x101,0x202,0x303,0x404,0x505};
static void fixture(struct vcell *c)
{
    vcell_init(c);
    assert(vcell_connect(c,1,20,600,7,instances,100)==VCELL_OK);
    for(unsigned i=0;i<5;i++) {
        struct vbb_server s={vcell_service(i),instances[i],7,80+i};
        assert(vcell_server_event(c,1,&s,0,100)==VCELL_OK);
    }
}
/* Fixture builder independent of the request encoder. */
static size_t field(uint8_t *p,size_t at,uint8_t type,const uint8_t *value,size_t n)
{
    assert(at+n+3<=256);p[at]=type;p[at+1]=(uint8_t)n;p[at+2]=(uint8_t)(n>>8);
    memcpy(p+at+3,value,n);return at+3+n;
}
static size_t response(uint8_t *p,const struct vcell_request *r,uint16_t error)
{
    const uint8_t status[]={error?1:0,0,(uint8_t)error,(uint8_t)(error>>8)};
    p[0]=2;p[1]=(uint8_t)r->transaction;p[2]=(uint8_t)(r->transaction>>8);
    p[3]=(uint8_t)r->message;p[4]=(uint8_t)(r->message>>8);p[5]=7;p[6]=0;
    return field(p,7,2,status,4);
}
static int deliver(struct vcell *c,const struct vcell_request *r,uint8_t *p,size_t n,uint64_t now)
{
    p[5]=(uint8_t)(n-7);p[6]=(uint8_t)((n-7)>>8);
    return vcell_receive(c,r->generation,r->endpoint.node,r->endpoint.port,p,n,now);
}
static struct vcell_request query(struct vcell *c,unsigned op,uint64_t now)
{
    struct vcell_request r;
    assert(vcell_prepare(c,op,NULL,0,0,now,1000,&r)==VCELL_OK);
    assert(vcell_send_complete(c,r.generation,r.transaction,1,now)==VCELL_OK);return r;
}
static void one_byte_query(struct vcell *c,unsigned op,uint8_t value,uint64_t now)
{
    struct vcell_request r=query(c,op,now);uint8_t p[256];size_t n=response(p,&r,0);
    n=field(p,n,1,&value,1);assert(deliver(c,&r,p,n,now)==VCELL_OK);
}
static void nas_query(struct vcell *c,uint8_t reg,uint8_t ps,int roam,uint64_t now)
{
    struct vcell_request r=query(c,VCELL_QUERY_NAS,now);uint8_t p[256];size_t n=response(p,&r,0);
    const uint8_t serving[]={reg,1,ps,2,1,8};n=field(p,n,1,serving,sizeof(serving));
    if(roam>=0){uint8_t value=(uint8_t)roam;n=field(p,n,0x10,&value,1);}
    const uint8_t plmn[]={0xcc,1,1,0,4,'T','e','s','t'};n=field(p,n,0x12,plmn,sizeof(plmn));
    assert(deliver(c,&r,p,n,now)==VCELL_OK);
}
static struct vcell_policy policy(int roaming)
{
    struct vcell_policy p={0};p.data_enabled=1;p.allow_roaming=(uint8_t)roaming;
    p.ip_family=6;p.apn_length=12;memcpy(p.apn,"test.example",13);return p;
}
static void ready(struct vcell *c,uint64_t now)
{
    struct vcell_policy p=policy(0);assert(vcell_set_policy(c,&p,now)==VCELL_OK);
    one_byte_query(c,VCELL_QUERY_DMS,0,now);nas_query(c,1,1,1,now);
    one_byte_query(c,VCELL_QUERY_WDS,1,now);
}
static void sent(struct vcell *c,struct vcell_request *r,unsigned op,uint64_t now)
{
    assert(vcell_prepare(c,op,"12345",5,1,now,1000,r)==VCELL_OK);
    assert(vcell_send_complete(c,r->generation,r->transaction,1,now)==VCELL_OK);
}
static void handle_response(struct vcell *c,const struct vcell_request *r,uint64_t now)
{
    uint8_t p[256];size_t n=response(p,r,0);const uint8_t handle[]={0x78,0x56,0x34,0x12};
    n=field(p,n,1,handle,4);assert(deliver(c,r,p,n,now)==VCELL_OK);
}

static void registry_cases(void)
{
    struct vcell c;struct vcell_request r;vcell_init(&c);
    assert(vcell_prepare(&c,VCELL_QUERY_DMS,NULL,0,0,0,1000,&r)==VCELL_OFFLINE);
    assert(!r.size && !c.policy.data_enabled && !c.policy.allow_roaming);
    assert(vcell_connect(&c,0,20,600,7,instances,0)==VCELL_INVALID);
    assert(vcell_connect(&c,1,20,VBB_CTRL,7,instances,0)==VCELL_INVALID);
    assert(vcell_connect(&c,1,20,600,20,instances,0)==VCELL_INVALID);
    assert(vcell_connect(&c,1,20,600,7,instances,100)==VCELL_OK);
    assert(vcell_prepare(&c,VCELL_QUERY_DMS,NULL,0,0,100,10,&r)==VCELL_NO_SERVICE);
    struct vbb_server s={VBB_DMS,instances[1],7,81};
    assert(vcell_server_event(&c,0,&s,0,100)==VCELL_STALE);
    s.instance++;assert(vcell_server_event(&c,1,&s,0,100)==VCELL_NO_SERVICE);s.instance--;
    s.node=1;assert(vcell_server_event(&c,1,&s,0,100)==VCELL_INVALID);s.node=7;
    assert(vcell_server_event(&c,1,&s,0,100)==VCELL_OK);
    assert(vcell_server_event(&c,1,&s,0,100)==VCELL_OK); /* exact duplicate */
    s.port++;assert(vcell_server_event(&c,1,&s,1,100)==VCELL_STALE);s.port--;
    struct vcell_request old=query(&c,VCELL_QUERY_DMS,100);
    assert(vcell_server_event(&c,1,&s,1,101)==VCELL_NO_SERVICE);
    assert(!c.bound && !c.pending && !c.uncertain);
    assert(vcell_connect(&c,1,20,600,7,instances,101)==VCELL_INVALID);
    assert(vcell_connect(&c,2,20,600,7,instances,101)==VCELL_OK);
    assert(vcell_server_event(&c,2,&s,0,101)==VCELL_OK);
    r=query(&c,VCELL_QUERY_DMS,101);assert(r.transaction!=old.transaction);
    uint8_t p[256];size_t n=response(p,&old,0);const uint8_t mode=0;n=field(p,n,1,&mode,1);
    assert(deliver(&c,&old,p,n,101)==VCELL_STALE);
    old.generation=2;assert(deliver(&c,&old,p,n,101)==VCELL_STALE);assert(c.pending);
    n=response(p,&r,0);n=field(p,n,1,&mode,1);assert(deliver(&c,&r,p,n,101)==VCELL_OK);
    s.port=82;assert(vcell_server_event(&c,2,&s,0,102)==VCELL_PROTOCOL);assert(!c.bound);
    fixture(&c);s=(struct vbb_server){VCELL_VOICE,instances[4],7,80};
    assert(vcell_server_event(&c,1,&s,0,100)==VCELL_PROTOCOL);assert(!c.bound);
}

static void query_and_match_cases(void)
{
    struct vcell c;fixture(&c);struct vcell_request r=query(&c,VCELL_QUERY_DMS,100);
    const uint8_t golden[]={0,1,0,0x2d,0,0,0};assert(!memcmp(r.bytes,golden,7));
    uint8_t p[256];size_t n=response(p,&r,94);
    assert(deliver(&c,&r,p,n,100)==VCELL_MODEM);
    assert(c.completion.result==1 && c.completion.modem_error==94 && !c.dms_valid && c.bound);
    ready(&c,101);assert(c.dms_valid && c.wds_valid && c.nas.valid);
    assert(c.nas.roaming==VCELL_HOME && c.nas.mcc==460 && c.nas.mnc==1);
    r=query(&c,VCELL_QUERY_NAS,102);assert(!c.nas.valid);n=response(p,&r,0);
    const uint8_t serving[]={1,1,1,2,0};n=field(p,n,1,serving,sizeof(serving));
    assert(vcell_receive(&c,0,7,82,p,n,102)==VCELL_STALE);
    assert(vcell_receive(&c,1,1,82,p,n,102)==VCELL_STALE);
    assert(vcell_receive(&c,1,7,81,p,n,102)==VCELL_STALE);
    p[1]++;assert(deliver(&c,&r,p,n,102)==VCELL_STALE);p[1]--;
    p[3]++;assert(deliver(&c,&r,p,n,102)==VCELL_STALE);p[3]--;
    p[0]=4;assert(deliver(&c,&r,p,n,102)==VCELL_STALE);p[0]=2;
    assert(c.pending && !c.nas.valid);assert(deliver(&c,&r,p,n,102)==VCELL_OK);
    assert(c.nas.roaming==VCELL_ROAM_UNKNOWN && !c.nas.plmn_valid);
    assert(deliver(&c,&r,p,n,102)==VCELL_STALE);
    nas_query(&c,1,1,0,103);assert(c.nas.roaming==VCELL_ROAMING);
    nas_query(&c,3,2,9,104);assert(c.nas.registration==3 && c.nas.roaming==VCELL_ROAM_UNKNOWN);
    r=query(&c,VCELL_QUERY_UIM,105);n=response(p,&r,0);const uint8_t cards[9]={0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0};
    n=field(p,n,0x10,cards,9);assert(deliver(&c,&r,p,n,105)==VCELL_OK);
    assert(!vcell_data_eligible(&c,105)); /* wire result never grants SIM/network readiness */
    r=query(&c,VCELL_QUERY_NAS,106);n=response(p,&r,0);
    assert(deliver(&c,&r,p,n,106)==VCELL_PROTOCOL);assert(!c.bound && !c.nas.valid);
    for(unsigned op=VCELL_QUERY_WDS;op<=VCELL_QUERY_UIM;op++) {
        fixture(&c);r=query(&c,op,100);n=response(p,&r,0);
        const uint8_t bad[]={255,255,255,255,255};n=field(p,n,op==VCELL_QUERY_UIM?0x10:1,bad,5);
        assert(deliver(&c,&r,p,n,100)==VCELL_PROTOCOL);assert(!c.bound);
    }
}

static void policy_cases(void)
{
    struct vcell_policy p=policy(0);assert(vcell_policy_valid(&p));
    const char *bad[]={"", ".test", "test.", "test..x", "-test", "test-", "te_st", "te/st", "te st"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        struct vcell_policy b=p;memset(b.apn,0,sizeof(b.apn));strcpy(b.apn,bad[i]);
        b.apn_length=(uint8_t)strlen(bad[i]);assert(!vcell_policy_valid(&b));
    }
    p.apn_length=101;assert(!vcell_policy_valid(&p));p=policy(0);
    p.apn[0]=(char)0xff;assert(!vcell_policy_valid(&p));p=policy(0);
    p.ip_family=8;assert(!vcell_policy_valid(&p));p=policy(0);
    p.data_enabled=2;assert(!vcell_policy_valid(&p));p=policy(0);
    p.apn[12]='x';assert(!vcell_policy_valid(&p));
    p=policy(0);memset(p.apn,'a',100);p.apn[63]='.';p.apn[100]=0;p.apn_length=100;
    assert(vcell_policy_valid(&p));p.apn[63]='a';assert(!vcell_policy_valid(&p));
    struct vcell c;fixture(&c);struct vcell_request r;
    assert(vcell_prepare(&c,VCELL_DATA_START,NULL,0,1,100,10,&r)==VCELL_POLICY);
    ready(&c,100);assert(vcell_data_eligible(&c,100));assert(!vcell_user_plane_allowed(&c,100));
    assert(!vcell_data_eligible(&c,99));assert(!vcell_data_eligible(&c,5100));
    assert(vcell_prepare(&c,VCELL_DATA_START,NULL,0,0,100,10,&r)==VCELL_POLICY);
    nas_query(&c,1,1,0,101);
    assert(vcell_prepare(&c,VCELL_DATA_START,NULL,0,1,101,10,&r)==VCELL_POLICY);
    p=policy(1);assert(vcell_set_policy(&c,&p,101)==VCELL_OK);
    assert(vcell_data_eligible(&c,101));nas_query(&c,1,1,-1,102);
    assert(!vcell_data_eligible(&c,102));nas_query(&c,2,1,1,103);
    assert(!vcell_data_eligible(&c,103));nas_query(&c,1,2,1,104);
    assert(!vcell_data_eligible(&c,104));nas_query(&c,1,1,1,105);
    one_byte_query(&c,VCELL_QUERY_DMS,3,105);assert(!vcell_data_eligible(&c,105));
    one_byte_query(&c,VCELL_QUERY_DMS,0,106);
    assert(vcell_prepare(&c,VCELL_DATA_START,NULL,0,1,106,10,&r)==VCELL_OK);
    p.data_enabled=0;assert(vcell_set_policy(&c,&p,106)==VCELL_OK);
    assert(!c.pending && c.completion.status==VCELL_NOT_SENT);
    struct vcell_request lease;
    assert(vcell_pending_request(&c,r.generation,r.transaction,106,&lease)==VCELL_STALE && !lease.size);
    assert(vcell_send_complete(&c,r.generation,r.transaction,1,106)==VCELL_STALE);
    fixture(&c);ready(&c,100);p=policy(0);
    memset(p.apn,'a',100);p.apn[63]='.';p.apn[100]=0;p.apn_length=100;
    assert(vcell_set_policy(&c,&p,100)==VCELL_OK);
    assert(vcell_prepare(&c,VCELL_DATA_START,NULL,0,1,100,10,&r)==VCELL_OK);
    assert(r.size==118 && r.size<=VCELL_REQUEST_CAP && r.bytes[7]==0x14 && r.bytes[8]==100);
}

static void data_cases(void)
{
    struct vcell c;fixture(&c);ready(&c,100);struct vcell_request r;
    sent(&c,&r,VCELL_DATA_START,100);
    const uint8_t golden[]={0,4,0,0x20,0,23,0,0x14,12,0,'t','e','s','t','.','e','x','a','m','p','l','e',0x19,1,0,6,0x33,1,0,0};
    assert(r.size==sizeof(golden) && !memcmp(r.bytes,golden,sizeof(golden)));
    handle_response(&c,&r,100);assert(c.handle_known && c.data_handle==0x12345678 && !c.wds_valid);
    assert(!vcell_user_plane_allowed(&c,100));one_byte_query(&c,VCELL_QUERY_WDS,2,101);
    assert(vcell_user_plane_allowed(&c,101));assert(!vcell_user_plane_allowed(&c,5100));
    assert(vcell_stop_needed(&c,5100));
    nas_query(&c,1,1,0,102);assert(c.stop_required && vcell_stop_needed(&c,102));
    assert(!vcell_user_plane_allowed(&c,102));sent(&c,&r,VCELL_DATA_STOP,102);
    const uint8_t stop[]={0,7,0,0x21,0,11,0,1,4,0,0x78,0x56,0x34,0x12,0x10,1,0,1};
    assert(r.size==sizeof(stop) && !memcmp(r.bytes,stop,sizeof(stop)));
    uint8_t p[256];size_t n=response(p,&r,94);assert(deliver(&c,&r,p,n,102)==VCELL_MODEM);
    assert(c.handle_known && c.stop_required);sent(&c,&r,VCELL_DATA_STOP,103);
    n=response(p,&r,0);assert(deliver(&c,&r,p,n,103)==VCELL_OK);
    assert(!c.handle_known && !c.stop_required && !c.wds_valid);
    fixture(&c);ready(&c,100);sent(&c,&r,VCELL_DATA_START,100);
    struct vcell_policy off=policy(0);off.data_enabled=0;
    assert(vcell_set_policy(&c,&off,101)==VCELL_OK && c.pending && c.stop_required);
    handle_response(&c,&r,101);assert(c.handle_known && vcell_stop_needed(&c,101));
    assert(!vcell_user_plane_allowed(&c,101));sent(&c,&r,VCELL_DATA_STOP,101);
    n=response(p,&r,0);assert(deliver(&c,&r,p,n,101)==VCELL_OK);
    fixture(&c);ready(&c,100);sent(&c,&r,VCELL_DATA_START,100);handle_response(&c,&r,100);
    one_byte_query(&c,VCELL_QUERY_WDS,1,101);assert(!c.handle_known && !c.data_handle);
    fixture(&c);ready(&c,100);one_byte_query(&c,VCELL_QUERY_WDS,2,101);
    assert(vcell_prepare(&c,VCELL_DATA_START,NULL,0,1,101,10,&r)==VCELL_STALE);
    assert(!c.handle_known); /* another client's connection cannot be stopped */
    fixture(&c);ready(&c,100);sent(&c,&r,VCELL_DATA_START,100);
    n=response(p,&r,0);assert(deliver(&c,&r,p,n,100)==VCELL_UNCERTAIN);
    assert(c.uncertain && !c.handle_known); /* missing handle is not start success */
}

static void voice_cases(void)
{
    struct vcell c;fixture(&c);ready(&c,100);struct vcell_request r;
    const char *bad[]={"", "+", "123;", "12#", "12*", "1+2", " 123", "1\n2"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)
        assert(vcell_prepare(&c,VCELL_DIAL,bad[i],strlen(bad[i]),1,100,10,&r)==VCELL_INVALID);
    assert(vcell_prepare(&c,VCELL_DIAL,NULL,1,1,100,10,&r)==VCELL_INVALID);
    assert(vcell_prepare(&c,VCELL_DIAL,"12345",33,1,100,10,&r)==VCELL_INVALID);
    assert(vcell_prepare(&c,VCELL_HANGUP,NULL,0,1,100,10,&r)==VCELL_STALE);
    sent(&c,&r,VCELL_DIAL,100);
    const uint8_t golden[]={0,4,0,0x20,0,8,0,1,5,0,'1','2','3','4','5'};
    assert(r.size==sizeof(golden) && !memcmp(r.bytes,golden,sizeof(golden)));
    uint8_t p[256];size_t n=response(p,&r,0);const uint8_t id=5;n=field(p,n,0x10,&id,1);
    assert(deliver(&c,&r,p,n,100)==VCELL_OK && c.call_id_known && c.call_id==5);
    assert(vcell_prepare(&c,VCELL_DIAL,"123",3,1,100,10,&r)==VCELL_BUSY);
    sent(&c,&r,VCELL_HANGUP,101);
    const uint8_t hangup[]={0,5,0,0x21,0,4,0,1,1,0,5};
    assert(r.size==sizeof(hangup) && !memcmp(r.bytes,hangup,sizeof(hangup)));
    n=response(p,&r,0);n=field(p,n,0x10,&id,1);assert(deliver(&c,&r,p,n,101)==VCELL_OK);
    assert(!c.call_id_known);
    fixture(&c);ready(&c,100);sent(&c,&r,VCELL_DIAL,100);
    n=response(p,&r,94);assert(deliver(&c,&r,p,n,100)==VCELL_MODEM);
    assert(!c.call_id_known && !c.uncertain && c.bound);
    sent(&c,&r,VCELL_DIAL,101);n=response(p,&r,0);
    assert(deliver(&c,&r,p,n,101)==VCELL_UNCERTAIN && c.uncertain);
}

static void lifecycle_cases(void)
{
    struct vcell c;struct vcell_request r,copy;uint8_t p[256];
    fixture(&c);assert(vcell_prepare(&c,VCELL_QUERY_DMS,NULL,0,0,100,10,&r)==VCELL_OK);
    assert(vcell_pending_request(&c,1,r.transaction,100,&copy)==VCELL_OK);
    r.bytes[0]=99;assert(copy.bytes[0]==0 && c.request.bytes[0]==0);r=copy;
    size_t n=response(p,&r,0);const uint8_t mode=0;n=field(p,n,1,&mode,1);
    assert(deliver(&c,&r,p,n,100)==VCELL_STALE); /* response before acceptance */
    assert(vcell_prepare(&c,VCELL_QUERY_WDS,NULL,0,0,100,10,&copy)==VCELL_BUSY);
    assert(vcell_send_complete(&c,1,r.transaction,0,100)==VCELL_NOT_SENT);
    assert(!c.pending && c.bound && !c.uncertain);
    r=query(&c,VCELL_QUERY_DMS,101);assert(r.transaction==2);
    assert(vcell_send_complete(&c,1,r.transaction,1,101)==VCELL_STALE);
    assert(vcell_tick(&c,1100)==VCELL_OK);
    assert(vcell_tick(&c,1101)==VCELL_TIMEOUT && !c.bound && !c.uncertain);
    fixture(&c);ready(&c,100);sent(&c,&r,VCELL_DIAL,100);
    assert(vcell_tick(&c,1100)==VCELL_UNCERTAIN && c.uncertain && !c.pending);
    assert(vcell_connect(&c,2,20,600,7,instances,1101)==VCELL_OK);
    assert(vcell_prepare(&c,VCELL_DIAL,"123",3,1,1101,10,&r)==VCELL_UNCERTAIN);
    assert(!vcell_data_eligible(&c,1101)); /* no automatic retry after reconnect */
    fixture(&c);ready(&c,100);
    assert(vcell_prepare(&c,VCELL_DATA_START,NULL,0,1,100,10,&r)==VCELL_OK);
    assert(vcell_send_complete(&c,1,r.transaction,-1,100)==VCELL_UNCERTAIN && c.uncertain);
    fixture(&c);assert(vcell_prepare(&c,VCELL_QUERY_DMS,NULL,0,0,100,10,&r)==VCELL_OK);
    assert(vcell_send_complete(&c,1,r.transaction,-1,100)==VCELL_TRANSPORT && !c.uncertain);
    fixture(&c);ready(&c,100);sent(&c,&r,VCELL_DATA_START,100);handle_response(&c,&r,100);
    vcell_disconnect(&c,VCELL_OFFLINE);assert(c.uncertain && !c.handle_known && !c.nas.valid);
    fixture(&c);ready(&c,100);sent(&c,&r,VCELL_DIAL,100);
    struct vbb_server s={VCELL_VOICE,instances[4],7,84};
    assert(vcell_server_event(&c,1,&s,1,100)==VCELL_UNCERTAIN);
    fixture(&c);r=query(&c,VCELL_QUERY_DMS,100);assert(vcell_tick(&c,99)==VCELL_STALE && !c.bound);
    fixture(&c);c.next_transaction=UINT16_MAX;r=query(&c,VCELL_QUERY_DMS,100);
    assert(r.transaction==UINT16_MAX);n=response(p,&r,94);assert(deliver(&c,&r,p,n,100)==VCELL_MODEM);
    assert(vcell_prepare(&c,VCELL_QUERY_DMS,NULL,0,0,100,10,&r)==VCELL_EXHAUSTED);
    assert(!r.size && !c.pending);
    assert(vcell_prepare(&c,0,NULL,0,0,100,10,&r)==VCELL_INVALID);
    assert(vcell_prepare(&c,VCELL_QUERY_DMS,NULL,0,0,100,0,&r)==VCELL_INVALID);
    assert(vcell_prepare(&c,VCELL_QUERY_DMS,NULL,0,0,UINT64_MAX,10,&r)==VCELL_INVALID);
    fixture(&c);ready(&c,100);
    assert(vcell_prepare(&c,VCELL_DATA_START,NULL,0,1,100,10000,&r)==VCELL_OK);
    assert(vcell_pending_request(&c,1,r.transaction,5100,&copy)==VCELL_POLICY);
    assert(!c.pending && !c.uncertain && !copy.size); /* freshness rechecked before send */
    fixture(&c);ready(&c,100);
    assert(vcell_prepare(&c,VCELL_DIAL,"12345",5,1,100,10000,&r)==VCELL_OK);
    assert(vcell_pending_request(&c,1,r.transaction,5100,&copy)==VCELL_STALE && !c.uncertain);
    fixture(&c);ready(&c,100);
    assert(vcell_prepare(&c,VCELL_DATA_START,NULL,0,1,100,10000,&r)==VCELL_OK);
    assert(vcell_send_complete(&c,1,r.transaction,1,5100)==VCELL_UNCERTAIN && c.uncertain);
    fixture(&c);ready(&c,100);
    assert(vcell_prepare(&c,VCELL_DIAL,"12345",5,1,100,10,&r)==VCELL_OK);
    assert(vcell_send_complete(&c,1,r.transaction,1,110)==VCELL_UNCERTAIN && c.uncertain);
}

static void each_mutation_failure(void)
{
    for(unsigned op=VCELL_DIAL;op<=VCELL_DATA_STOP;op++) {
        for(unsigned failure=0;failure<4;failure++) {
            struct vcell c;fixture(&c);ready(&c,100);struct vcell_request r;
            uint8_t p[256];size_t n;
            if(op==VCELL_HANGUP) {
                sent(&c,&r,VCELL_DIAL,100);n=response(p,&r,0);const uint8_t id=5;
                n=field(p,n,0x10,&id,1);assert(deliver(&c,&r,p,n,100)==VCELL_OK);
            }
            if(op==VCELL_DATA_STOP) {sent(&c,&r,VCELL_DATA_START,100);handle_response(&c,&r,100);}
            assert(vcell_prepare(&c,op,"12345",5,1,100,1000,&r)==VCELL_OK);
            if(failure==0) {
                assert(vcell_send_complete(&c,1,r.transaction,-1,100)==VCELL_UNCERTAIN);
            } else {
                assert(vcell_send_complete(&c,1,r.transaction,1,100)==VCELL_OK);
                if(failure==1)assert(vcell_tick(&c,1100)==VCELL_UNCERTAIN);
                if(failure==2) {
                    n=response(p,&r,94);assert(deliver(&c,&r,p,n,100)==VCELL_MODEM);
                    assert(c.bound && !c.uncertain && c.completion.modem_error==94);
                }
                if(failure==3) {
                    /* Matching corrupt result TLV; cannot prove a failed mutation. */
                    n=response(p,&r,0);p[10]=2;
                    assert(deliver(&c,&r,p,n,100)==VCELL_UNCERTAIN);
                }
            }
            assert(!c.pending);
            if(failure!=2)assert(c.uncertain && !c.bound);
        }
    }
}

static uint32_t random_word(uint32_t *s) { *s=*s*1664525u+1013904223u;return *s; }
static void malformed_cases(void)
{
    uint32_t seed=0xc311;
    for(unsigned it=0;it<100000;it++) {
        struct vcell c;fixture(&c);unsigned op=1+random_word(&seed)%4;
        struct vcell_request r=query(&c,op,100);uint8_t template_bytes[256];
        size_t full=response(template_bytes,&r,0);
        const uint8_t value[]={1,1,1,2,1,8};full=field(template_bytes,full,1,value,6);
        template_bytes[5]=(uint8_t)(full-7);
        size_t n=random_word(&seed)%257;uint8_t *bytes=malloc(n?n:1);assert(bytes);
        for(size_t i=0;i<n;i++)bytes[i]=(uint8_t)(random_word(&seed)>>24);
        if(n>=full && it%2==0){memcpy(bytes,template_bytes,full);bytes[random_word(&seed)%n]^=(uint8_t)(1u<<(random_word(&seed)%8));}
        (void)vcell_receive(&c,1,7,r.endpoint.port,bytes,n,100);
        assert(!c.call_id_known && !c.handle_known && !c.uncertain);
        assert(!c.pending || c.request.transaction==r.transaction);
        free(bytes);
    }
    /* Every truncation of a matched valid query is safely rejected. */
    for(size_t cut=0;cut<18;cut++) {
        struct vcell c;fixture(&c);struct vcell_request r=query(&c,VCELL_QUERY_DMS,100);
        uint8_t p[256];size_t n=response(p,&r,0);const uint8_t mode=0;n=field(p,n,1,&mode,1);
        p[5]=(uint8_t)(n-7);assert(n==18);
        assert(vcell_receive(&c,1,7,r.endpoint.port,p,cut,100)==VCELL_PROTOCOL);
        assert(!c.dms_valid && !c.bound);
    }
}
int main(void)
{
    registry_cases();query_and_match_cases();policy_cases();data_cases();voice_cases();
    lifecycle_cases();each_mutation_failure();malformed_cases();
    puts("SYNTHETIC ONLY: registry, QMI queries/dial/hangup/data wire, policy, lifecycle, 100000 malformed packets: PASS");
    puts("Hardware modem/SIM/registration/IMS/audio/internet/roaming capability proven: NO");
    return 0;
}
