/* SPDX-License-Identifier: MIT
 * Original bounded K70 CCI sensor-register read implementation.
 * Register/command facts: LineageOS sm8550-modules 92a9fde479d3,
 * cam_cci_core.c cam_cci_read(), cam_cci_dev.h, cam_cci_hwreg.h.
 * A read issues I2C address bytes: this is active hardware I/O, not a snapshot.
 * Caller serializes this object and holds controller/power/pin/clock leases.
 */
#ifndef VERMEER_CAMERA_CCI_H
#define VERMEER_CAMERA_CCI_H
#include <stdint.h>
#include <stddef.h>

enum { VCCI_OK, VCCI_ARGUMENT, VCCI_LEASE, VCCI_CLOCK, VCCI_IO,
       VCCI_BUSY, VCCI_LAYOUT, VCCI_TIMEOUT, VCCI_NACK, VCCI_PROTOCOL,
       VCCI_RETIRED };
/* These are provider attestations, not actions implemented by this module.
 * EXCLUSIVE includes both masters, queue0/1 and the IRQ handler. CONFIG means
 * verified CCI timing plus fitted sensor rails/reset/MCLK and ID-register tuple.
 */
enum { VCCI_BOARD=1, VCCI_MAPPING=2, VCCI_DOMAIN=4, VCCI_EXCLUSIVE=8,
       VCCI_CONFIG=16, VCCI_REQUIRED=31 };
struct vcci_io {
    void *ctx;
    uint64_t physical;
    size_t length;
    uint32_t verified_hw_version; /* actual qualified layout, never a guess */
    unsigned (*proof)(void *);
    int (*read)(void *, unsigned, uint32_t *); /* ordered, window-relative */
    int (*write)(void *, unsigned, uint32_t);
    int (*now_us)(void *, uint64_t *);
    int (*pause_us)(void *, unsigned); /* non-reentrant, bounded */
};
struct vcci {
    struct vcci_io io;
    uint64_t begin, last;
    unsigned error, active, retired, touched, started, polls;
};
struct vcci_request {
    unsigned slot, sid, address_bytes, count;
    uint32_t address; /* register address sent MSB first, 1 or 2 bytes */
};
struct vcci_result {
    uint8_t data[16];
    unsigned count, fifo_prefix; /* raw first FIFO byte; not a sensor ID */
};
static inline uint64_t vcci_slot_base(unsigned slot)
{
    return slot==0 || slot==2 ? UINT64_C(0xac15000) :
           slot==1 ? UINT64_C(0xac17000) :
           slot==3 ? UINT64_C(0xac16000) : 0;
}
static inline int vcci_fail(struct vcci *s, unsigned error)
{
    s->error=error; s->retired=1; return 0;
}
/* Initialize only on a fresh, owned controller. After a fault the platform
 * retains the mapping/domain and performs its own qualified recovery before
 * creating a fresh object. No automatic reset, retry, unlock or power-down.
 */
static inline int vcci_init(struct vcci *s, const struct vcci_io *io)
{
    if (!s || !io) return 0;
    *s=(struct vcci){0}; s->io=*io;
    if (!io->proof || !io->read || !io->write || !io->now_us || !io->pause_us ||
        io->length!=0x1000 || !io->verified_hw_version ||
        (io->physical!=vcci_slot_base(0) && io->physical!=vcci_slot_base(1) &&
         io->physical!=vcci_slot_base(3))) return vcci_fail(s,VCCI_ARGUMENT);
    return 1;
}
static inline int vcci_guard(struct vcci *s)
{
    uint64_t now;
    if (s->retired || !s->active) return 0;
    if ((s->io.proof(s->io.ctx)&VCCI_REQUIRED)!=VCCI_REQUIRED)
        return vcci_fail(s,VCCI_LEASE);
    if (s->retired) return 0;
    if (!s->io.now_us(s->io.ctx,&now) || now<s->last)
        return vcci_fail(s,VCCI_CLOCK);
    if (s->retired) return 0;
    s->last=now;
    if (now-s->begin>=20000) return vcci_fail(s,VCCI_TIMEOUT);
    if ((s->io.proof(s->io.ctx)&VCCI_REQUIRED)!=VCCI_REQUIRED)
        return vcci_fail(s,VCCI_LEASE);
    if (s->retired) return 0;
    return 1;
}
static inline int vcci_rd(struct vcci *s, unsigned off, uint32_t *value)
{
    if (!vcci_guard(s)) return 0;
    if (off>0xffc || (off&3) || !s->io.read(s->io.ctx,off,value))
        return vcci_fail(s,VCCI_IO);
    return vcci_guard(s);
}
static inline int vcci_wr(struct vcci *s, unsigned off, uint32_t value)
{
    if (!vcci_guard(s)) return 0;
    s->touched=1; /* failed writes may already have reached the controller */
    if (off>0xffc || (off&3) || !s->io.write(s->io.ctx,off,value))
        return vcci_fail(s,VCCI_IO);
    return vcci_guard(s);
}
/* Whole result is published only after RD_DONE, the post-UNLOCK REPORT,
 * empty queue/FIFO and final lease/deadline checks. On any failure out is
 * unchanged; already-started hardware must stay leased/quarantined by owner.
 */
static inline int vcci_read_register(struct vcci *s,
    const struct vcci_request *r, struct vcci_result *out)
{
    if (!s || !r || !out) return 0;
    if (s->retired) return 0;
    if (s->active) return vcci_fail(s,VCCI_RETIRED);
    if (r->slot>3 || vcci_slot_base(r->slot)!=s->io.physical ||
        r->sid<8 || r->sid>0x77 || !r->count || r->count>16 ||
        !r->address_bytes || r->address_bytes>2 ||
        (r->address_bytes==1 ? r->address>255 : r->address>65535)) {
        s->error=VCCI_ARGUMENT; return 0;
    }
    s->active=1; s->error=0; s->touched=0; s->started=0; s->polls=0;
    if (!s->io.now_us(s->io.ctx,&s->last)) { vcci_fail(s,VCCI_CLOCK); goto end; }
    s->begin=s->last;
    unsigned master=r->slot==2, q=0x400+master*0x200;
    unsigned datareg=0x118+master*0x100, levelreg=datareg+4;
    uint32_t version, value, irq0, irq1;
    struct vcci_result result={0};
    if (!vcci_rd(s,0,&version)) goto end;
    if (version!=s->io.verified_hw_version) { vcci_fail(s,VCCI_LAYOUT); goto end; }
    /* Neither master may carry someone else's queued work or RX data. */
    for (unsigned m=0;m<2;m++) {
        for (unsigned queue=0;queue<2;queue++) {
            if (!vcci_rd(s,0x304+m*0x200+queue*0x100,&value)) goto end;
            if (value) { vcci_fail(s,VCCI_BUSY); goto end; }
        }
        if (!vcci_rd(s,0x11c+m*0x100,&value)) goto end;
        if (value) { vcci_fail(s,VCCI_BUSY); goto end; }
    }
    if (!vcci_rd(s,0xc0c,&irq0) || !vcci_rd(s,0xc18,&irq1)) goto end;
    if (irq0 || irq1) { vcci_fail(s,VCCI_BUSY); goto end; }
    /* SET_PARAM with retries=0/id_map=0; LOCK; address write without STOP;
     * READ issues repeated START, then UNLOCK and REPORT(id=0, stop+irq).
     * Appending REPORT ensures a read completion alone cannot release a
     * master while its UNLOCK is still queued.
     */
    uint32_t address=0x0b|(r->address_bytes<<4);
    for (unsigned i=0;i<r->address_bytes;i++)
        address|=((r->address>>(8*(r->address_bytes-1-i)))&255)<<(8*(i+1));
    uint32_t commands[6]={1|(r->sid<<4),6,address,10|(r->count<<4),7,0x308};
    for (unsigned i=0;i<6;i++) if (!vcci_wr(s,q+0x10,commands[i])) goto end;
    if (!vcci_rd(s,q+4,&value)) goto end;
    if (value!=6) { vcci_fail(s,VCCI_PROTOCOL); goto end; }
    if (!vcci_wr(s,q,6)) goto end;
    s->started=1;
    if (!vcci_wr(s,8,1u<<(master*2+1))) goto end;
    uint32_t done=master?0x1000:1, report=master?0x100000:0x100;
    for (;;) {
        if (++s->polls>1000) { vcci_fail(s,VCCI_TIMEOUT); goto end; }
        if (!vcci_rd(s,0xc0c,&irq0) || !vcci_rd(s,0xc18,&irq1)) goto end;
        if (irq0&(master?0x60000000u:0x18000000u)) { vcci_fail(s,VCCI_NACK); goto end; }
        if ((irq0&~(done|report)) || irq1) { vcci_fail(s,VCCI_PROTOCOL); goto end; }
        if ((irq0&(done|report))==(done|report)) break;
        if (!vcci_guard(s)) goto end;
        if (!s->io.pause_us(s->io.ctx,20)) { vcci_fail(s,VCCI_IO); goto end; }
        if (!vcci_guard(s)) goto end;
    }
    if (!vcci_rd(s,q+4,&value)) goto end;
    if (value) { vcci_fail(s,VCCI_PROTOCOL); goto end; }
    if (!vcci_rd(s,levelreg,&value)) goto end;
    unsigned words=r->count/4+1;
    if (value!=words) { vcci_fail(s,VCCI_PROTOCOL); goto end; }
    for (unsigned i=0;i<words;i++) {
        if (!vcci_rd(s,datareg,&value)) goto end;
        for (unsigned b=0;b<4;b++) {
            unsigned index=i*4+b;
            if (!index) result.fifo_prefix=value&255;
            else if (index<=r->count) result.data[index-1]=(uint8_t)(value>>(b*8));
        }
    }
    if (!vcci_rd(s,levelreg,&value)) goto end;
    if (value) { vcci_fail(s,VCCI_PROTOCOL); goto end; }
    if (!vcci_wr(s,0xc08,done|report) || !vcci_wr(s,0xc00,1)) goto end;
    if (!vcci_guard(s)) goto end;
    result.count=r->count; *out=result;
end:
    s->active=0;
    return !s->retired && s->error==VCCI_OK;
}
#endif
