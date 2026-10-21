/* SPDX-License-Identifier: MIT
 * Native XNU kernel-control UART service for iOS16's existing BM3UART
 * non-Skywalk path. Evidence: retained iOS16.7.16/20H392 bluetoothd
 * 100066bdc(open), 100066e50(TIOCGETA), 1000671ac(TIOCSETA),
 * 1000676a4(write fallback), 100067a9c(read fallback).
 * Default com.apple.uart.sk.bluetooth REQUIRES a Skywalk UUID/channel and
 * is deliberately not claimed. Explicit daemon UART parameter must select
 * com.apple.uart.k70.bluetooth,BAUD. Selection/startup gate is not integrated.
 * One permanent owner; callbacks only copy bytes. Worker alone touches GPI.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kern_control.h>
#include <sys/kpi_mbuf.h>
#include <sys/termios.h>
#include <sys/ttycom.h>
#include <kern/locks.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <mach/thread_act.h>
#include <machine/atomic.h>
#include "vermeer_bt_uart_service.h"

_Static_assert(sizeof(struct termios)==72,"iOS16 ARM64 termios ABI");
_Static_assert(TIOCGETA==0x40487413u,"observed iOS16 get option");
_Static_assert(TIOCSETA==0x80487414u,"observed iOS16 set option");
static LCK_GRP_DECLARE(vbus_group,"k70-bt-uart");
static LCK_MTX_DECLARE(vbus_lock,&vbus_group);
static struct {
    struct vbug *transport; kern_ctl_ref control;
    struct vbug_io provider;
    struct termios termios;
    uint8_t tx[8192],rx[2048];
    unsigned tx_head,tx_count,rx_count,unit,baud,connected,started,attempted;
    uint32_t _Atomic retired;
} vbus;
int vermeer_bt_uart_service_live(void)
{ return !os_atomic_load(&vbus.retired,acquire); }
static void vbus_fail(void)
{ os_atomic_store(&vbus.retired,1,release); }
static unsigned vbus_proof(void *ctx)
{
    (void)ctx;
    if(!vermeer_bt_uart_service_live())return 0;
    unsigned proof=vbus.provider.proof(vbus.provider.ctx);
    return vermeer_bt_uart_service_live()?proof:0;
}
static int vbus_read32(void *ctx,unsigned bank,unsigned off,uint32_t *out)
{ (void)ctx;return vbus.provider.read(vbus.provider.ctx,bank,off,out); }
static int vbus_write32(void *ctx,unsigned bank,unsigned off,uint32_t value)
{ (void)ctx;return vbus.provider.write(vbus.provider.ctx,bank,off,value); }
static int vbus_cache(void *ctx,void *cpu,size_t len,unsigned op)
{ (void)ctx;return vbus.provider.cache(vbus.provider.ctx,cpu,len,op); }
static int vbus_time(void *ctx,uint64_t *now)
{ (void)ctx;return vbus.provider.now_us(vbus.provider.ctx,now); }
static errno_t vbus_connect(kern_ctl_ref ref,struct sockaddr_ctl *address,void **info)
{
    (void)ref;errno_t error=0;
    lck_mtx_lock(&vbus_lock);
    if (!vbus.started || !vermeer_bt_uart_service_live()) error=ENXIO;
    else if (vbus.connected) error=EBUSY;
    else { vbus.connected=1;vbus.unit=address->sc_unit;*info=&vbus; }
    lck_mtx_unlock(&vbus_lock);return error;
}
static errno_t vbus_disconnect(kern_ctl_ref ref,u_int32_t unit,void *info)
{
    (void)ref;(void)info;
    lck_mtx_lock(&vbus_lock);
    if (vbus.connected && unit==vbus.unit) {
        vbus_fail();vbus.connected=0;vbus.tx_count=0;
    }
    lck_mtx_unlock(&vbus_lock);
    /* Never release DMA, reset firmware or permit stale-stream reconnect. */
    return 0;
}
static errno_t vbus_send(kern_ctl_ref ref,u_int32_t unit,void *info,mbuf_t mbuf,int flags)
{
    (void)ref;(void)info;errno_t error=0;size_t size=mbuf_pkthdr_len(mbuf);
    lck_mtx_lock(&vbus_lock);
    if (!vbus.connected || unit!=vbus.unit || !vermeer_bt_uart_service_live()) error=ENXIO;
    else if (flags || !size || size>4096) error=EMSGSIZE;
    else if (size>sizeof(vbus.tx)-vbus.tx_count) error=ENOBUFS;
    else {
        /* Copy before committing count. mbuf_copydata can fail; no partial
         * datagram reaches GPI. Wrap split is bounded by one socket record.
         */
        unsigned tail=(vbus.tx_head+vbus.tx_count)&8191;
        size_t first=MIN(size,sizeof(vbus.tx)-tail);
        error=mbuf_copydata(mbuf,0,first,vbus.tx+tail);
        if (!error && first<size) error=mbuf_copydata(mbuf,first,size-first,vbus.tx);
        if (!error) vbus.tx_count+=(unsigned)size;
    }
    lck_mtx_unlock(&vbus_lock);
    mbuf_freem(mbuf);return error;
}
static errno_t vbus_getopt(kern_ctl_ref ref,u_int32_t unit,void *info,int option,void *data,size_t *len)
{
    (void)ref;(void)info;errno_t error=0;
    lck_mtx_lock(&vbus_lock);
    if (!vbus.connected || unit!=vbus.unit || !vermeer_bt_uart_service_live()) error=ENXIO;
    else if ((unsigned)option!=TIOCGETA) error=ENOPROTOOPT;
    else if (data && *len<sizeof(vbus.termios)) error=EMSGSIZE;
    else { if(data)memcpy(data,&vbus.termios,sizeof(vbus.termios));*len=sizeof(vbus.termios); }
    lck_mtx_unlock(&vbus_lock);return error;
}
static errno_t vbus_setopt(kern_ctl_ref ref,u_int32_t unit,void *info,int option,void *data,size_t len)
{
    (void)ref;(void)info;errno_t error=0;
    lck_mtx_lock(&vbus_lock);
    if (!vbus.connected || unit!=vbus.unit || !vermeer_bt_uart_service_live()) error=ENXIO;
    else if ((unsigned)option!=TIOCSETA) error=ENOPROTOOPT;
    else if (!data || len!=sizeof(struct termios)) error=EINVAL;
    else {
        const struct termios *t=data;
        if (t->c_ispeed!=vbus.baud || t->c_ospeed!=vbus.baud ||
            (t->c_cflag&CSIZE)!=CS8 || (t->c_cflag&(PARENB|CSTOPB)) ||
            (t->c_iflag&(ISTRIP|INLCR|IGNCR|ICRNL|IXON|IXOFF)) ||
            (t->c_oflag&OPOST) || (t->c_lflag&(ECHO|ICANON|ISIG|IEXTEN))) error=EINVAL;
        else vbus.termios=*t;
    }
    lck_mtx_unlock(&vbus_lock);return error;
}
static void vbus_sleep(void)
{
    uint64_t deadline;clock_interval_to_deadline(1,NSEC_PER_MSEC,&deadline);
    assert_wait_deadline((event_t)&vbus,THREAD_UNINT,deadline);
    thread_block(THREAD_CONTINUE_NULL);
}
static void vbus_pump(void)
{
    uint8_t tx[2048];
        unsigned count=0,unit=0,active;
        lck_mtx_lock(&vbus_lock);
        active=vbus.connected && vermeer_bt_uart_service_live();
        if(active) {
            unit=vbus.unit;count=MIN(vbus.tx_count,sizeof(tx));
            for(unsigned i=0;i<count;i++)tx[i]=vbus.tx[(vbus.tx_head+i)&8191];
        }
        lck_mtx_unlock(&vbus_lock);
        if(!active)return;
        /* No socket lock or service mutex around device or socket callbacks.
         * Disconnect flips the live proof, so next GPI boundary retires I/O.
         */
        if(!vbug_poll(vbus.transport)){vbus_fail();return;}
        if(count) {
            int n=vbug_write(vbus.transport,tx,count);
            if(n<0){vbus_fail();return;}
            if(n) {
                lck_mtx_lock(&vbus_lock);
                if(vbus.connected && vermeer_bt_uart_service_live()) {
                    vbus.tx_head=(vbus.tx_head+(unsigned)n)&8191;vbus.tx_count-=(unsigned)n;
                }
                lck_mtx_unlock(&vbus_lock);
            }
        }
        if(!vbus.rx_count) {
            int n=vbug_read(vbus.transport,vbus.rx,sizeof(vbus.rx));
            if(n<0){vbus_fail();return;}
            vbus.rx_count=(unsigned)n;
        }
        if(vbus.rx_count && vermeer_bt_uart_service_live()) {
            int error=ctl_enqueuedata(vbus.control,unit,vbus.rx,vbus.rx_count,CTL_DATA_EOR);
            if(!error)vbus.rx_count=0;
            else if(error!=ENOBUFS){vbus_fail();return;}
            /* ENOBUFS retains bytes in this permanent spool; never discards
             * or reports delivered data. Further overflow retires GPI. */
        }
}
static void vbus_worker(void *arg,wait_result_t result)
{
    (void)arg;(void)result;
    for(;;){vbus_pump();vbus_sleep();}
}
int vermeer_bt_uart_service_start(struct vbug *transport,unsigned baud)
{
    if (!transport || !transport->attached || transport->retired || !baud) return EINVAL;
    lck_mtx_lock(&vbus_lock);
    if(vbus.attempted){lck_mtx_unlock(&vbus_lock);return EBUSY;}
    vbus.attempted=1;vbus.transport=transport;vbus.baud=baud;
    /* Permanent serialized ownership transfer from platform to worker. Wrap
     * every callback so asynchronous disconnect revokes the GPI proof without
     * relying on a platform convention or changing the provider's context.
     */
    vbus.provider=transport->io;
    transport->io.ctx=&vbus;transport->io.proof=vbus_proof;
    transport->io.read=vbus_read32;transport->io.write=vbus_write32;
    transport->io.cache=vbus_cache;transport->io.now_us=vbus_time;
    lck_mtx_unlock(&vbus_lock);
    struct kern_ctl_reg reg={0};
    strlcpy(reg.ctl_name,VERMEER_BT_UART_CONTROL,sizeof(reg.ctl_name));
    reg.ctl_flags=CTL_FLAG_PRIVILEGED;reg.ctl_sendsize=8192;reg.ctl_recvsize=8192;
    reg.ctl_connect=vbus_connect;reg.ctl_disconnect=vbus_disconnect;
    reg.ctl_send=vbus_send;reg.ctl_getopt=vbus_getopt;reg.ctl_setopt=vbus_setopt;
    vbus.termios.c_cflag=CS8|CREAD|CLOCAL|CCTS_OFLOW|CRTS_IFLOW;
    vbus.termios.c_ispeed=vbus.termios.c_ospeed=baud;
    errno_t error=ctl_register(&reg,&vbus.control);
    if(error){vbus_fail();return error;}
    thread_t thread=THREAD_NULL;
    if(kernel_thread_start(vbus_worker,NULL,&thread)!=KERN_SUCCESS) {
        vbus_fail();(void)ctl_deregister(vbus.control);return EIO;
    }
    thread_deallocate(thread);
    lck_mtx_lock(&vbus_lock);vbus.started=1;lck_mtx_unlock(&vbus_lock);
    return 0;
}
#endif
