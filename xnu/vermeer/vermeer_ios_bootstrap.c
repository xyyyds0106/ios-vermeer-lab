/* First-boot compatibility metadata for the explicitly approved experimental
 * root. This is not a real encrypted keybag or a hardware security provider.
 * Existing bags are never replaced. All I/O uses the guarded mounted volume.
 */
#include <pexpert/arm64/board_config.h>
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <pexpert/pexpert.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include <sys/vnode_internal.h>
#include <sys/vnode_if.h>
#include "vermeer_migration_transaction.h"

int vermeer_ios_prepare_root(void);
int vermeer_ios_mkdir(const char *path, vfs_context_t ctx);
int vermeer_ufs_ios_root_ready(void);
int vermeer_ios_mkdir_owned(const char *, uid_t, gid_t, mode_t, vfs_context_t);
int vermeer_ios_rename_exclusive(const char *, const char *, vfs_context_t);

static int root_directory(const char *path, vfs_context_t ctx)
{
    int error = vermeer_ios_mkdir(path, ctx);
    if (error && error != EEXIST) return error;
    vnode_t vp = NULLVP;
    error = vnode_lookup(path, VNODE_LOOKUP_NOFOLLOW, &vp, ctx);
    if (error) return error;
    struct vnode_attr va;
    VATTR_INIT(&va); VATTR_WANTED(&va, va_uid);
    error = vnode_getattr(vp, &va, ctx);
    if (!error && (!vnode_isdir(vp) || vnode_mount(vp) != vnode_mount(rootvnode) || va.va_uid)) error = EPERM;
    vnode_put(vp);
    return error;
}

/* This optional baseline is for the newly constructed, disposable 20H392 root.
 * It avoids upgrade plugins trying to install/demote system apps before their
 * backend services have passed DataMigration themselves. Preserve the original
 * preference inode and never change Setup or activation records here.
 */
#define VMB_DIR "/private/var/mobile/Library/Preferences"
static const char * const vmb_paths[VMB_SLOTS] = {
    VMB_DIR "/com.apple.migration.plist",
    VMB_DIR "/.com.apple.migration.vermeer-before-v1.plist",
    VMB_DIR "/.com.apple.migration.vermeer-stage-v1.plist",
    VMB_DIR "/.vermeer-migration-baseline-v1"
};

static int vmb_validate(vnode_t vp,enum vmb_slot slot,vfs_context_t ctx,off_t *size)
{
    struct vnode_attr va;
    VATTR_INIT(&va);VATTR_WANTED(&va,va_uid);VATTR_WANTED(&va,va_nlink);VATTR_WANTED(&va,va_data_size);
    int error=vnode_getattr(vp,&va,ctx);
    if (error) return error;
    if (!vnode_isreg(vp) || vnode_mount(vp)!=vnode_mount(rootvnode) || va.va_nlink!=1 ||
        va.va_data_size>1024*1024 || (va.va_uid!=0 && va.va_uid!=501) ||
        (slot==VMB_DONE && va.va_uid!=0) || (slot==VMB_STAGE && va.va_uid!=501)) return EPERM;
    if (size) *size=(off_t)va.va_data_size;
    return 0;
}

static int vmb_stat_file(void *opaque,enum vmb_slot slot)
{
    vfs_context_t ctx=(vfs_context_t)opaque;
    vnode_t vp=NULLVP;
    int error=vnode_lookup(vmb_paths[slot],VNODE_LOOKUP_NOFOLLOW,&vp,ctx);
    if (error) return error;
    error=vmb_validate(vp,slot,ctx,NULL);
    vnode_put(vp);return error;
}

static int vmb_matches_file(void *opaque,enum vmb_slot slot,const char *expected,unsigned length)
{
    vfs_context_t ctx=(vfs_context_t)opaque;
    char data[sizeof(vmb_plist)];
    if (length>sizeof(data)) return EINVAL;
    vnode_t vp=NULLVP;off_t size=0;
    int error=vnode_open(vmb_paths[slot],FREAD|O_NOFOLLOW,0,VNODE_LOOKUP_NOFOLLOW,&vp,ctx);
    if (error) return error;
    error=vmb_validate(vp,slot,ctx,&size);
    if (!error && size!=(off_t)length) error=VMB_DIFFERENT;
    int residual=0;
    if (!error) error=vn_rdwr(UIO_READ,vp,data,(int)length,0,UIO_SYSSPACE,0,
        vfs_context_ucred(ctx),&residual,current_proc());
    if (!error && residual) error=EIO;
    if (!error && memcmp(data,expected,length)) error=VMB_DIFFERENT;
    int close_error=vnode_close(vp,FREAD,ctx);
    return error ? error : close_error;
}

static int vmb_sync_directory(vfs_context_t ctx)
{
    vnode_t vp=NULLVP;
    int error=vnode_lookup(VMB_DIR,VNODE_LOOKUP_NOFOLLOW,&vp,ctx);
    if (error) return error;
    error=VNOP_FSYNC(vp,MNT_WAIT,ctx);vnode_put(vp);return error;
}

static int vmb_create_file(void *opaque,enum vmb_slot slot,const char *bytes,unsigned length)
{
    vfs_context_t ctx=(vfs_context_t)opaque;
    if (slot!=VMB_STAGE && slot!=VMB_DONE) return EPERM;
    vnode_t vp=NULLVP;
    int error=vnode_open(vmb_paths[slot],FWRITE|O_CREAT|O_EXCL|O_NOFOLLOW,0600,VNODE_LOOKUP_NOFOLLOW,&vp,ctx);
    if (error) return error;
    struct vnode_attr va;
    VATTR_INIT(&va);VATTR_SET(&va,va_uid,slot==VMB_DONE ? 0 : 501);
    VATTR_SET(&va,va_gid,slot==VMB_DONE ? 0 : 501);VATTR_SET(&va,va_mode,0600);
    error=vnode_setattr(vp,&va,ctx);
    int residual=0;
    if (!error) error=vn_rdwr(UIO_WRITE,vp,(caddr_t)bytes,(int)length,0,UIO_SYSSPACE,IO_SYNC,
        vfs_context_ucred(ctx),&residual,current_proc());
    if (!error && residual) error=EIO;
    if (!error) error=VNOP_FSYNC(vp,MNT_WAIT,ctx);
    int close_error=vnode_close(vp,FWRITE,ctx);
    if (!error) error=close_error;
    if (!error) error=vmb_sync_directory(ctx);
    return error;
}

static int vmb_rename_file(void *opaque,enum vmb_slot from,enum vmb_slot to)
{
    vfs_context_t ctx=(vfs_context_t)opaque;
    if (!((from==VMB_CURRENT && to==VMB_BACKUP) || (from==VMB_STAGE && to==VMB_CURRENT) ||
          (from==VMB_BACKUP && to==VMB_CURRENT))) return EPERM;
    int error=vermeer_ios_rename_exclusive(vmb_paths[from],vmb_paths[to],ctx);
    return error ? error : vmb_sync_directory(ctx);
}

static int vmb_contains(const char *data,unsigned length,const char *text)
{
    unsigned n=(unsigned)strlen(text);
    for (unsigned i=0;n<=length && i<=length-n;i++) if (!memcmp(data+i,text,n)) return 1;
    return 0;
}

static int vmb_prepare_directory(void *opaque)
{
    vfs_context_t ctx=(vfs_context_t)opaque;
    /* Pin the disposable root version before creating any metadata. The media
     * UUID, writable extent and HFS root checks already gate this caller.
     */
    vnode_t version=NULLVP;
    int error=vnode_open("/System/Library/CoreServices/SystemVersion.plist",FREAD|O_NOFOLLOW,0,VNODE_LOOKUP_NOFOLLOW,&version,ctx);
    if (error) return error;
    off_t size=0;char data[2048];
    error=vmb_validate(version,VMB_CURRENT,ctx,&size);
    if (!error && (size<=0 || size>(off_t)sizeof(data))) error=EINVAL;
    int residual=0;
    if (!error) error=vn_rdwr(UIO_READ,version,data,(int)size,0,UIO_SYSSPACE,0,vfs_context_ucred(ctx),&residual,current_proc());
    if (!error && (residual || !vmb_contains(data,(unsigned)size,"20H392") ||
                   !vmb_contains(data,(unsigned)size,"16.7.16"))) error=EINVAL;
    int close_error=vnode_close(version,FREAD,ctx);
    if (!error) error=close_error;
    if (error) return error;
    static const char * const dirs[]={"/private/var/mobile","/private/var/mobile/Library",VMB_DIR};
    for (unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++) {
        error=vermeer_ios_mkdir_owned(dirs[i],501,501,0700,ctx);
        if (error && error!=EEXIST) return error;
        vnode_t vp=NULLVP;
        error=vnode_lookup(dirs[i],VNODE_LOOKUP_NOFOLLOW,&vp,ctx);
        if (error) return error;
        struct vnode_attr va;VATTR_INIT(&va);VATTR_WANTED(&va,va_uid);
        error=vnode_getattr(vp,&va,ctx);
        if (!error && (!vnode_isdir(vp) || vnode_mount(vp)!=vnode_mount(rootvnode) ||
                      (va.va_uid!=0 && va.va_uid!=501))) error=EPERM;
        vnode_put(vp);if (error) return error;
    }
    return 0;
}

static int vermeer_prepare_migration(vfs_context_t ctx)
{
    uint32_t enabled=0;
    if (!PE_parse_boot_argn("vermeer_migration_baseline",&enabled,sizeof(enabled)) || enabled!=1) return 0;
    static const struct vmb_ops ops={vmb_prepare_directory,vmb_stat_file,vmb_matches_file,vmb_create_file,vmb_rename_file};
    int error=vmb_initialize(&ops,(void *)ctx);
    printf("VERMEER ROOT: experimental clean-root migration baseline: %d; original backup retained\n",error);
    /* Optional metadata failure leaves normal iOS migration enabled and the
     * original preference in its current/backup slot. Keep USB boot available.
     */
    return 0;
}

int vermeer_ios_prepare_root(void)
{
    uint32_t prepare = 0;
    if (!PE_parse_boot_argn("vermeer_prepare_root", &prepare, sizeof(prepare)) || prepare != 1) return 0;
    char rd[32] = {0};
    if (!PE_parse_boot_argn("rd", rd, sizeof(rd)) || strcmp(rd, "vermeerufs0") ||
        !vermeer_ufs_ios_root_ready() || !rootvnode ||
        !(vfs_flags(vnode_mount(rootvnode)) & MNT_ROOTFS) ||
        strcmp(vfs_statfs(vnode_mount(rootvnode))->f_fstypename, "hfs")) return EPERM;
    vfs_context_t ctx = vfs_context_kernel();
    int error = root_directory("/private/var/keybags", ctx);
    if (error) return error;
    vnode_t vp = NULLVP;
    const int mode = FWRITE | O_CREAT | O_EXCL | O_NOFOLLOW;
    error = vnode_open("/private/var/keybags/systembag.kb", mode, 0600, VNODE_LOOKUP_NOFOLLOW, &vp, ctx);
    if (error == EEXIST) {
        printf("VERMEER ROOT: existing systembag preserved\n");
        return vermeer_prepare_migration(ctx);
    }
    if (error) return error;
    /* keybagd reads this plist again for optional BackupKeyBagKeys even when
     * the built-in test AppleKeyStore already reports system handle0 loaded.
     * Match the reference lab schema with an explicit non-secret test marker.
     */
    static const char plist[] = "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
        "<key>KeyBagKeys</key><data>VkVSTUVFUg==</data></dict></plist>\n";
    int residual = 0;
    error = vn_rdwr(UIO_WRITE, vp, (caddr_t)plist, sizeof(plist)-1, 0,
        UIO_SYSSPACE, IO_SYNC, vfs_context_ucred(ctx), &residual, current_proc());
    if (!error && residual) error = EIO;
    if (!error) error = VNOP_FSYNC(vp, MNT_WAIT, ctx);
    int close_error = vnode_close(vp, FWRITE, ctx);
    if (!error) error = close_error;
    printf("VERMEER ROOT: first-boot test systembag prepared: %d\n", error);
    return error ? error : vermeer_prepare_migration(ctx);
}
#endif
