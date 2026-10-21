/* K70 Berlin-D wire packet parser, independent of MMIO and firmware loading.
 * Protocol reference: Xiaomi 9916 driver compatible xiaomi,vermeer-spi at
 * Air-Alex/kernel_xiaomi_sm8550-modules d92dbc093c08af885d8a808a170a715c1c024312.
 * The 143999/319999 Android axes are virtual precision, not physical pixels.
 */
#ifndef VERMEER_GOODIX_PACKET_H
#define VERMEER_GOODIX_PACKET_H
#include <stdint.h>
#define VT_CONTACTS 10
#define VT_PACKET_MAX 98
enum { VT_BAD=-1,VT_OTHER=0,VT_TOUCH=1 };
struct vt_point { uint16_t x,y;uint8_t active; };
struct vt_frame { struct vt_point point[VT_CONTACTS]; };
static inline unsigned vt_le16(const uint8_t *p) { return p[0]|((unsigned)p[1]<<8); }
static inline int vt_checksum(const uint8_t *p,unsigned length)
{
    if(length<2)return 0;
    unsigned sum=0;for(unsigned i=0;i<length-2;i++)sum+=p[i];
    return (sum&65535u)==vt_le16(p+length-2);
}
static inline int vt_decode(const uint8_t *p,unsigned length,unsigned max_x,unsigned max_y,struct vt_frame *out)
{
    if(!p || !out || length<8 || length>VT_PACKET_MAX || !max_x || !max_y ||
       max_x>65535 || max_y>65535 || !vt_checksum(p,8))return VT_BAD;
    if(!(p[0]&0x80))return VT_OTHER;
    unsigned count=p[2]&15u;
    if(count>VT_CONTACTS || (count && (length<8+count*8+2 || !vt_checksum(p+8,count*8+2))))return VT_BAD;
    struct vt_frame result={0};
    for(unsigned i=0;i<count;i++) {
        const uint8_t *r=p+8+8*i;
        unsigned id=r[0]>>4,type=r[0]&15u,x=vt_le16(r+2),y=vt_le16(r+4);
        /* Stylus packets have another record layout. Do not turn them into
         * finger contacts or partially publish a malformed multi-touch frame.
         */
        if(id>=VT_CONTACTS || (type!=0 && type!=2) || result.point[id].active || x>max_x || y>max_y)return VT_BAD;
        result.point[id].active=1;
        result.point[id].x=(uint16_t)(((uint32_t)x*1439u+max_x/2)/max_x);
        result.point[id].y=(uint16_t)(((uint32_t)y*3199u+max_y/2)/max_y);
    }
    *out=result;return VT_TOUCH;
}
typedef void (*vt_emit)(void *,unsigned,int,unsigned,unsigned);
static inline void vt_apply(struct vt_frame *state,const struct vt_frame *frame,vt_emit emit,void *ctx)
{
    for(unsigned i=0;i<VT_CONTACTS;i++)if(state->point[i].active && !frame->point[i].active)
        emit(ctx,i,0,state->point[i].x,state->point[i].y);
    for(unsigned i=0;i<VT_CONTACTS;i++)if(frame->point[i].active &&
       (!state->point[i].active || frame->point[i].x!=state->point[i].x || frame->point[i].y!=state->point[i].y))
        emit(ctx,i,1,frame->point[i].x,frame->point[i].y);
    *state=*frame;
}
static inline void vt_release_all(struct vt_frame *state,vt_emit emit,void *ctx)
{
    const struct vt_frame empty={0};vt_apply(state,&empty,emit,ctx);
}
#endif
