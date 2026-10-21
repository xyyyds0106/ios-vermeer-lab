/* Runs the actual native callback/worker source with explicit XNU API mocks.
 * Real ARM64/16KB XNU compile separately checks declarations/ABI; mock structs
 * are not evidence that kernel linkage or daemon selection works.
 */
#define main vbug_datapath_test_main
#include "bt_uart_gpi_test.c"
#undef main
#include <errno.h>
#include <stdlib.h>
#define ARM64_BOARD_CONFIG_SWEET 1
typedef int errno_t;
typedef unsigned int u_int32_t;
typedef void *kern_ctl_ref;
typedef void *thread_t;
typedef int wait_result_t;
typedef void *event_t;
struct sockaddr_ctl {unsigned sc_unit;};
struct termios {uint64_t c_iflag,c_oflag,c_cflag,c_lflag;uint8_t c_cc[20];uint32_t pad;uint64_t c_ispeed,c_ospeed;};
#define TIOCGETA 0x40487413u
#define TIOCSETA 0x80487414u
#define CSIZE 0x300u
#define CS8 0x300u
#define CREAD 0x800u
#define CLOCAL 0x8000u
#define PARENB 0x1000u
#define CSTOPB 0x400u
#define CCTS_OFLOW 0x10000u
#define CRTS_IFLOW 0x20000u
#define ISTRIP 0x20u
#define INLCR 0x40u
#define IGNCR 0x80u
#define ICRNL 0x100u
#define IXON 0x200u
#define IXOFF 0x400u
#define OPOST 1u
#define ECHO 8u
#define ICANON 0x100u
#define ISIG 0x80u
#define IEXTEN 0x400u
#define LCK_GRP_DECLARE(name,description) int name
#define LCK_MTX_DECLARE(name,group) int name
#define acquire 0
#define release 0
#define os_atomic_load(ptr,order) (*(ptr))
#define os_atomic_store(ptr,value,order) (*(ptr)=(value))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define THREAD_UNINT 0
#define NSEC_PER_MSEC 1000000
#define THREAD_CONTINUE_NULL NULL
#define THREAD_NULL NULL
#define KERN_SUCCESS 0
#define CTL_DATA_EOR 2u
#define CTL_FLAG_PRIVILEGED 1u
struct mbuf {const uint8_t *bytes;size_t length;unsigned copy_fail,freed;};
typedef struct mbuf *mbuf_t;
struct kern_ctl_reg {
 char ctl_name[96];unsigned ctl_flags,ctl_sendsize,ctl_recvsize;
 errno_t (*ctl_connect)(kern_ctl_ref,struct sockaddr_ctl *,void **);
 errno_t (*ctl_disconnect)(kern_ctl_ref,unsigned,void *);
 errno_t (*ctl_send)(kern_ctl_ref,unsigned,void *,mbuf_t,int);
 errno_t (*ctl_getopt)(kern_ctl_ref,unsigned,void *,int,void *,size_t *);
 errno_t (*ctl_setopt)(kern_ctl_ref,unsigned,void *,int,void *,size_t);
};
static unsigned held,registrations,socket_error,socket_count,threads;
static struct kern_ctl_reg registered;
static uint8_t socket_data[4096];
static void lck_mtx_lock(int *lock){(void)lock;assert(!held);held=1;}
static void lck_mtx_unlock(int *lock){(void)lock;assert(held);held=0;}
static size_t mbuf_pkthdr_len(mbuf_t m){return m->length;}
static errno_t mbuf_copydata(mbuf_t m,size_t off,size_t n,void *dst){
 assert(off<=m->length && n<=m->length-off);if(m->copy_fail)return EFAULT;memcpy(dst,m->bytes+off,n);return 0;
}
static void mbuf_freem(mbuf_t m){assert(!m->freed);m->freed=1;}
static size_t strlcpy(char *d,const char *s,size_t n){size_t len=strlen(s);assert(len<n);memcpy(d,s,len+1);return len;}
static errno_t ctl_register(struct kern_ctl_reg *reg,kern_ctl_ref *out){registered=*reg;++registrations;*out=&registered;return 0;}
static errno_t ctl_deregister(kern_ctl_ref ref){assert(ref==&registered);return 0;}
static errno_t ctl_enqueuedata(kern_ctl_ref ref,unsigned unit,void *data,size_t len,unsigned flags){
 assert(!held && ref==&registered && unit==1 && flags==CTL_DATA_EOR && len<=4096);
 if(socket_error)return (int)socket_error;memcpy(socket_data,data,len);socket_count=(unsigned)len;return 0;
}
static int kernel_thread_start(void (*fn)(void *,int),void *arg,thread_t *thread){(void)fn;(void)arg;*thread=&registered;++threads;return KERN_SUCCESS;}
static void thread_deallocate(thread_t thread){assert(thread==&registered);}
static void clock_interval_to_deadline(unsigned a,unsigned b,uint64_t *d){*d=(uint64_t)a*b;}
static void assert_wait_deadline(event_t event,int type,uint64_t d){(void)event;(void)type;(void)d;}
static void thread_block(void *x){(void)x;}
#include VBUS_SOURCE
static int disconnecting_read(void *ctx,unsigned bank,unsigned off,uint32_t *out) {
    int result=read32(ctx,bank,off,out);
    assert(vbus_disconnect(&registered,1,&vbus)==0);
    return result;
}

static void reset_service(struct vbug *s,struct mock *m) {
    memset(&vbus,0,sizeof(vbus));held=0;socket_error=0;socket_count=0;
    init(s,m,1);m->auto_tx=1;
    assert(vermeer_bt_uart_service_start(s,2400000)==0);
    assert(registered.ctl_flags==CTL_FLAG_PRIVILEGED && !strcmp(registered.ctl_name,VERMEER_BT_UART_CONTROL));
    struct sockaddr_ctl address={.sc_unit=1};void *info=NULL;
    assert(registered.ctl_connect(&registered,&address,&info)==0 && info==&vbus);
}
int main(void) {
    static struct mock m;struct vbug s;uint8_t command[]={1,1,0x10,0};
    uint8_t version[]={4,14,12,1,1,0x10,0,11,0x34,0x12,11,0x1d,0,0x78,0x56};
    reset_service(&s,&m);
    struct sockaddr_ctl address={.sc_unit=2};void *info=NULL;
    assert(registered.ctl_connect(&registered,&address,&info)==EBUSY);
    assert(vermeer_bt_uart_service_start(&s,2400000)==EBUSY);
    struct termios t;size_t len=sizeof(t);
    assert(registered.ctl_getopt(&registered,1,&vbus,(int)TIOCGETA,&t,&len)==0 && len==72);
    assert(t.c_ospeed==2400000 && t.c_ispeed==2400000);
    assert(registered.ctl_setopt(&registered,1,&vbus,(int)TIOCSETA,&t,sizeof(t))==0);
    t.c_ospeed=115200;
    assert(registered.ctl_setopt(&registered,1,&vbus,(int)TIOCSETA,&t,sizeof(t))==EINVAL);
    t.c_ospeed=2400000;t.c_iflag=IXON;
    assert(registered.ctl_setopt(&registered,1,&vbus,(int)TIOCSETA,&t,sizeof(t))==EINVAL);
    assert(registered.ctl_getopt(&registered,1,&vbus,0x40105412,&t,&len)==ENOPROTOOPT);
    struct mbuf mbuf={command,4,0,0};
    assert(registered.ctl_send(&registered,1,&vbus,&mbuf,0)==0 && mbuf.freed && vbus.tx_count==4);
    vbus_pump();assert(m.tx_bells==1 && !memcmp(memory[VBUG_TX],command,4) && !vbus.tx_count);
    receive(&m,version,sizeof(version),2);socket_error=ENOBUFS;vbus_pump();
    assert(vbus.rx_count==sizeof(version) && !socket_count && vermeer_bt_uart_service_live());
    socket_error=0;vbus_pump();
    assert(!vbus.rx_count && socket_count==sizeof(version) && !memcmp(socket_data,version,sizeof(version)));
    unsigned ops=m.ops;assert(registered.ctl_disconnect(&registered,1,&vbus)==0);
    vbus_pump();assert(m.ops==ops && !vermeer_bt_uart_service_live());
    assert(vbug_poll(&s)==0 && m.ops==ops); /* wrapper revokes before MMIO */
    assert(registered.ctl_connect(&registered,&address,&info)==ENXIO);
    /* Copy failure/queue pressure never commits partial client bytes. */
    reset_service(&s,&m);mbuf=(struct mbuf){command,4,1,0};
    assert(registered.ctl_send(&registered,1,&vbus,&mbuf,0)==EFAULT && mbuf.freed && !vbus.tx_count);
    uint8_t block[4096];memset(block,0x5a,sizeof(block));
    for(unsigned i=0;i<2;i++) {mbuf=(struct mbuf){block,sizeof(block),0,0};assert(!vbus_send(&registered,1,&vbus,&mbuf,0));}
    mbuf=(struct mbuf){command,4,0,0};assert(vbus_send(&registered,1,&vbus,&mbuf,0)==ENOBUFS && vbus.tx_count==8192);
    for(unsigned i=0;i<5;i++)vbus_pump();assert(!vbus.tx_count && m.tx_bells==4);
    /* Socket failure closes the proof; persistent RX DMA is quarantined. */
    receive(&m,version,sizeof(version),4);socket_error=EINVAL;vbus_pump();
    assert(!vermeer_bt_uart_service_live());ops=m.ops;vbus_pump();assert(m.ops==ops);
    reset_service(&s,&m);vbus.provider.read=disconnecting_read;ops=m.ops;
    vbus_pump();assert(!vermeer_bt_uart_service_live() && s.retired && m.ops==ops+1);
    vbus_pump();assert(m.ops==ops+1);
    assert(!held && registrations==threads);
    puts("Native UART service: actual callback/pump source, termios, mbuf failures, backpressure, GPI/socket flow and disconnect lease PASS");
    return 0;
}
