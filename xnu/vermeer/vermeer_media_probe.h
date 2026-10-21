/* Optional next-build CDC worker contract. No UART or automatic mappings.
 * A provider and its context live permanently. Successful begin borrows an
 * accessible mapping/domain lease through capture; end releases that borrow.
 * Failed begin must clean up any partial acquisition itself.
 */
#ifndef VERMEER_MEDIA_PROBE_H
#define VERMEER_MEDIA_PROBE_H
#include "vermeer_media_tlmm.h"
struct vmp_provider {
    void *ctx;
    int (*begin)(void *,unsigned,struct vmt_access *); /* requested field mask */
    void (*end)(void *);
};
static inline unsigned vmp_request_mask(const char *s,size_t n)
{
    if(!s)return 0;
    static const char *const commands[]={"media bt","media camera","media all"};
    static const unsigned lengths[]={8,12,9},masks[]={VMT_BT_FIELDS,VMT_CAMERA_FIELDS,0xffff};
    for(unsigned k=0;k<3;k++) {
        if(n!=lengths[k])continue;
        unsigned i=0;for(;i<n && s[i]==commands[k][i];i++);
        if(i==n)return masks[k];
    }
    return 0;
}
static inline int vmp_no_read(void *ctx,unsigned offset,uint32_t *out)
{(void)ctx;(void)offset;(void)out;return 0;}
static inline unsigned vmp_no_proof(void *ctx){(void)ctx;return 0;}
static inline unsigned vmp_capture(const struct vmp_provider *provider,
                                   unsigned requested,uint64_t sequence,
                                   uint64_t (*now_us)(void *),void *clock_ctx,
                                   struct vmt_record *record)
{
    struct vmt_access fallback={.ctx=clock_ctx,.read32=vmp_no_read,
                                .proof=vmp_no_proof,.now_us=now_us};
    if(!record || !now_us || !sequence || !requested || (requested&~0xffffu))
        return vmt_capture(&fallback,requested,sequence,record);
    if(!provider || !provider->begin || !provider->end)
        return vmt_capture(&fallback,requested,sequence,record);
    /* Pin the provider before invoking it. It must not modify/reenter the
     * worker. Only a complete, successful borrow requires an end callback.
     */
    const struct vmp_provider p=*provider;
    struct vmt_access access={0};
    if(p.begin(p.ctx,requested,&access)!=1)
        return vmt_capture(&fallback,requested,sequence,record);
    unsigned result=vmt_capture(&access,requested,sequence,record);
    p.end(p.ctx);
    return result;
}
/* Native symbols supplied by vermeer_media_probe.c after reviewed integration. */
int vermeer_media_probe_bind(const struct vmp_provider *);
void vermeer_media_probe_start(void);
#endif
