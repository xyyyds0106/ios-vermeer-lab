/* TLMM-only CDC observation candidate. No mapping, writes, UART, PMIC, CCI,
 * firmware or input injection. An actual platform owner supplies an ordered,
 * nonblocking read callback for an ALREADY accessible retained device mapping.
 * Single serialized worker; callbacks must not reenter/reinitialize collection.
 */
#ifndef VERMEER_MEDIA_TLMM_H
#define VERMEER_MEDIA_TLMM_H
#include <stddef.h>
#include <stdint.h>
#define VMT_MODEL_PROVEN 1u
#define VMT_DT_PROVEN 2u
#define VMT_DEVICE_MAPPING 4u
#define VMT_DOMAIN_HELD 8u
#define VMT_MAPPING_HELD 16u
#define VMT_REQUIRED 31u
#define VMT_TLMM_BASE UINT64_C(0x0f000000)
#define VMT_TLMM_SIZE UINT64_C(0x01000000)
#define VMT_BT_FIELDS 0x00ffu
#define VMT_CAMERA_FIELDS 0xff00u
#define VMT_NO_FAILURE 0xffffffffu
#define VMT_RECORD_CAPACITY 512u
enum vmt_status { VMT_OK=0, VMT_GATED=1, VMT_BOUNDS=2, VMT_READ_FAILED=3,
                  VMT_REVOKED=4, VMT_CLOCK_FAILED=5, VMT_BAD_ARGUMENT=6 };
static const unsigned vmt_offsets[16]={
    0x153000,0x153004,0x153000,0x153004,0x151000,0x151004,0x152000,0x152004,
    0x176000,0x176004,0x178000,0x178004,0x175000,0x175004,0x177000,0x177004
};
static const char *const vmt_names[16]={
    "strap_cfg_a","strap_io_a","strap_cfg_b","strap_io_b",
    "bt_enable_cfg","bt_enable_io","bt_sw_cfg","bt_sw_io",
    "camera0_reset_cfg","camera0_reset_io","camera1_reset_cfg","camera1_reset_io",
    "camera2_reset_cfg","camera2_reset_io","camera3_reset_cfg","camera3_reset_io"
};
struct vmt_access {
    uint64_t physical_start, bytes;
    void *ctx;
    /* Offset is relative to the supplied mapping, not the full TLMM base.
     * Return 1 with a value on success; all other results stop the snapshot.
     */
    int (*read32)(void *,unsigned,uint32_t *);
    unsigned (*proof)(void *); /* published ownership/proof bits, no hardware I/O */
    uint64_t (*now_us)(void *); /* monotonic time, no hardware ownership change */
};
struct vmt_record {
    uint64_t sequence,start_us,end_us;
    uint32_t requested,valid,proof,status,failed_index,time_valid;
    uint32_t values[16];
};
static inline unsigned vmt_capture(const struct vmt_access *access,
                                   uint32_t requested,uint64_t sequence,
                                   struct vmt_record *out)
{
    if (!out) return VMT_BAD_ARGUMENT;
    *out=(struct vmt_record){0};out->sequence=sequence;out->requested=requested;
    out->status=VMT_BAD_ARGUMENT;out->failed_index=VMT_NO_FAILURE;
    if (!access || !access->now_us || !sequence || !requested || (requested&~0xffffu))
        return out->status;
    const struct vmt_access a=*access; /* pin callback/context and window */
    out->start_us=a.now_us(a.ctx);out->time_valid=1;
    if (!a.read32 || !a.proof) goto finish;
    out->proof=a.proof(a.ctx);
    if ((out->proof&VMT_REQUIRED)!=VMT_REQUIRED) {out->status=VMT_GATED;goto finish;}
    if (a.physical_start<VMT_TLMM_BASE || !a.bytes || (a.physical_start&3u) ||
        a.physical_start-VMT_TLMM_BASE>=VMT_TLMM_SIZE ||
        a.bytes>VMT_TLMM_SIZE-(a.physical_start-VMT_TLMM_BASE)) {
        out->status=VMT_BOUNDS;goto finish;
    }
    /* Preflight ALL requested addresses before the first device read. The
     * existing touch mapping f100000+30000 DOES NOT cover BT/camera GPIOs.
     */
    for (unsigned i=0;i<16;++i) {
        if (!(requested&(1u<<i))) continue;
        uint64_t pa=VMT_TLMM_BASE+vmt_offsets[i];
        if (a.bytes<4 || pa<a.physical_start || pa-a.physical_start>a.bytes-4) {
            out->status=VMT_BOUNDS;goto finish;
        }
    }
    out->status=VMT_OK;
    for (unsigned i=0;i<16;++i) {
        if (!(requested&(1u<<i))) continue;
        out->failed_index=i;
        out->proof=a.proof(a.ctx);
        if ((out->proof&VMT_REQUIRED)!=VMT_REQUIRED) {out->status=VMT_REVOKED;break;}
        uint32_t value;
        unsigned offset=(unsigned)(VMT_TLMM_BASE+vmt_offsets[i]-a.physical_start);
        if (a.read32(a.ctx,offset,&value)!=1) {out->status=VMT_READ_FAILED;break;}
        out->proof=a.proof(a.ctx);
        if ((out->proof&VMT_REQUIRED)!=VMT_REQUIRED) {out->status=VMT_REVOKED;break;}
        out->values[i]=value;out->valid|=1u<<i;out->failed_index=VMT_NO_FAILURE;
    }
finish:
    out->end_us=a.now_us(a.ctx);out->time_valid|=2;
    if (out->end_us<out->start_us) out->status=VMT_CLOCK_FAILED;
    return out->status;
}
struct vmt_text { char bytes[VMT_RECORD_CAPACITY]; size_t n; unsigned failed; };
static inline void vmt_char(struct vmt_text *b,char c)
{
    if (b->n>=sizeof(b->bytes)-1) b->failed=1;
    else b->bytes[b->n++]=c;
}
static inline void vmt_literal(struct vmt_text *b,const char *s)
{for (;*s;++s)vmt_char(b,*s);}
static inline void vmt_hex(struct vmt_text *b,uint64_t value,unsigned digits)
{for (unsigned i=digits;i>0;--i)vmt_char(b,"0123456789abcdef"[(value>>((i-1)*4))&15u]);}
/* One complete VMT1 line, with missing values represented by '-' sentinels.
 * The caller prints the line verbatim into the existing console ring. Return
 * the length excluding NUL, or 0 without changing output on insufficient room.
 */
static inline size_t vmt_encode(const struct vmt_record *r,char *out,size_t capacity)
{
    if (!r || !out) return 0;
    struct vmt_text b={.n=0,.failed=0};
    vmt_literal(&b,"VMT1 seq=");vmt_hex(&b,r->sequence,16);
    vmt_literal(&b," start=");
    if (r->time_valid&1u)vmt_hex(&b,r->start_us,16);else vmt_literal(&b,"----------------");
    vmt_literal(&b," end=");
    if (r->time_valid&2u)vmt_hex(&b,r->end_us,16);else vmt_literal(&b,"----------------");
    vmt_literal(&b," status=");vmt_hex(&b,r->status,8);
    vmt_literal(&b," proof=");vmt_hex(&b,r->proof,8);
    vmt_literal(&b," req=");vmt_hex(&b,r->requested,8);
    vmt_literal(&b," valid=");vmt_hex(&b,r->valid,8);
    vmt_literal(&b," fail=");vmt_hex(&b,r->failed_index,8);
    vmt_literal(&b," uart=skipped data=");
    for (unsigned i=0;i<16;++i) {
        if (i)vmt_char(&b,',');
        if (r->valid&(1u<<i))vmt_hex(&b,r->values[i],8);
        else vmt_literal(&b,"--------");
    }
    vmt_char(&b,'\n');
    if (b.failed || capacity<=b.n) return 0;
    b.bytes[b.n]=0;
    for (size_t i=0;i<=b.n;++i)out[i]=b.bytes[i];
    return b.n;
}
#endif
