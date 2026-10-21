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

int vermeer_ios_prepare_root(void);
int vermeer_ios_mkdir(const char *path, vfs_context_t ctx);
int vermeer_ufs_ios_root_ready(void);

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
        return 0;
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
    return error;
}
#endif
