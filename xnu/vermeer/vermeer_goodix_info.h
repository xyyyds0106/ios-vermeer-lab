/* Berlin-D IC information wire format. No firmware/configuration upload.
 * Reference: Xiaomi9916 protocol at d92dbc093c08af885d8a808a170a715c1c024312.
 * Decode variable tables with a bounded cursor, never native structure casts.
 */
#ifndef VERMEER_GOODIX_INFO_H
#define VERMEER_GOODIX_INFO_H
#include "vermeer_goodix_packet.h"
struct vt_info { uint32_t event; uint16_t max_x,max_y; };
static inline uint32_t vt_le32(const uint8_t *p)
{ return vt_le16(p)|((uint32_t)vt_le16(p+2)<<16); }
static inline int vt_info_decode(const uint8_t *p,unsigned n,struct vt_info *out)
{
    if(!p || !out || n<181 || n>=1024 || vt_le16(p)!=n || !vt_checksum(p,n))return 0;
    unsigned pos=2+16+10+4;
    const unsigned limits[5]={5,10,10,10,8};
    for(unsigned i=0;i<5;i++) {
        if(pos>=n-2)return 0;
        unsigned count=p[pos++];
        if(count>limits[i] || count>(n-2-pos)/2)return 0;
        pos+=count*2;
    }
    /* 122-byte misc block, 20-byte other block, then checksum/extensions. */
    if(pos>n-2 || n-2-pos<142)return 0;
    struct vt_info info={vt_le32(p+pos+44),
        (uint16_t)vt_le16(p+pos+122+16),(uint16_t)vt_le16(p+pos+122+18)};
    if(vt_le16(p+pos+48)!=8 || vt_le16(p+pos+50)!=8 ||
       info.event<0x10000 || info.event>0x1fffff-90 ||
       !info.max_x || !info.max_y)return 0;
    *out=info;return 1;
}
static inline int vt_version_9916(const uint8_t *p,unsigned n)
{
    return p && n==28 && vt_checksum(p,n) &&
           p[10]=='9' && p[11]=='9' && p[12]=='1' && p[13]=='6';
}
#endif
