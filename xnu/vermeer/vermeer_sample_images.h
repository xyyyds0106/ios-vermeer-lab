/* Read-only, bounded dyld64 image enumeration for the selected-task sampler.
 * ABI: apple-oss-distributions/dyld, libdyld/dyld_process_info_internal.h
 * (dyld_all_image_infos_64 prefix and dyld_image_info_64). No direct user
 * pointer dereference: the caller must supply a fault-safe target-map reader.
 */
#ifndef VERMEER_SAMPLE_IMAGES_H
#define VERMEER_SAMPLE_IMAGES_H
#ifdef VERMEER_HOST_TEST
#include <stdint.h>
#else
#include <sys/types.h>
#endif
#define VS_MAX_IMAGES 4096u
#define VS_MAX_PRIVATE 64u
#define VS_PATH_SIZE 160u
#define VS_USER_END 0x8000000000ULL
struct vs_infos64 { uint32_t version, count; uint64_t array; };
struct vs_image64 { uint64_t base, path, modified; };
typedef int (*vs_reader)(void *,uint64_t,void *,unsigned);
typedef void (*vs_emitter)(void *,uint64_t,const char *);
static inline int vs_user_range(uint64_t address,uint64_t size)
{
    return address>=0x100000000ULL && address<VS_USER_END && size && size<=VS_USER_END-address;
}
/* Returns emitted rows, -1 for unavailable/invalid metadata, -2 if dyld
 * changed its list during the snapshot. Rows are diagnostic, best effort.
 */
static inline int vs_images(uint64_t info,vs_reader read,vs_emitter emit,void *context)
{
    struct vs_infos64 first,last;
    unsigned emitted=0;
    if ((info&7) || !vs_user_range(info,sizeof(first)) ||
        !read(context,info,&first,sizeof(first)) || !first.version || first.version>64 ||
        first.count>VS_MAX_IMAGES || (first.array&7) ||
        (first.count && !vs_user_range(first.array,(uint64_t)first.count*sizeof(struct vs_image64)))) return -1;
    for (unsigned i=0;i<first.count && emitted<VS_MAX_PRIVATE;i++) {
        struct vs_image64 image;
        if (!read(context,first.array+(uint64_t)i*sizeof(image),&image,sizeof(image))) return -1;
        /* Shared-cache images are symbolicated with the separate cache slide.
         * Capture the main executable and dynamically loaded private plugins.
         */
        if (image.base<0x100000000ULL || image.base>=0x180000000ULL) continue;
        char path[VS_PATH_SIZE];
        unsigned n;
        for (n=0;n<VS_PATH_SIZE-1;n++) {
            char c;
            if (!vs_user_range(image.path,n+1) || !read(context,image.path+n,&c,1)) break;
            if (!c) break;
            path[n]=(c>=' ' && c<='~') ? c : '?';
        }
        path[n]=0;
        emit(context,image.base,path);
        emitted++;
    }
    if (!read(context,info,&last,sizeof(last)) || last.version!=first.version ||
        last.count!=first.count || last.array!=first.array) return -2;
    return (int)emitted;
}
#endif
