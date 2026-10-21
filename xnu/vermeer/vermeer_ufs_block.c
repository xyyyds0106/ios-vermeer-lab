/* BSD partition adapter based on reference0028, using the verified
 * vermeer transport. Compile this file INSTEAD of vermeer_ufs_probe.c.
 * Single command, 8KiB bounce buffer, permanent low-RAM DMA allocation.
 * Diagnostics compile without a write opcode. The separate IOS wrapper permits
 * writes only after exact GPT, HFSX geometry/UUID and explicit boot-arg guards.
 */
#define VERMEER_UFS_BLOCK 1
#include "vermeer_ufs_probe.c"
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <miscfs/devfs/devfs.h>
#include <kern/locks.h>
#include "vermeer_gpt.h"

int vermeer_ufs_block_init(void);
static struct {
    uint64_t first, blocks;
    uint32_t secsize, oldlo, oldhi, oldrun;
    bool ready, failed, owned, attempted, writable, root_valid;
    int bmajor, cmajor;
} vb;
static LCK_GRP_DECLARE(vermeer_ufs_grp, "vermeer_ufs");
static LCK_MTX_DECLARE(vermeer_ufs_lock, &vermeer_ufs_grp);

static uint64_t vb_device_bytes(void)
{
    /* The integrated HFS volume is16GiB inside a larger userdata partition.
     * Present the volume size, so HFS alternate headers stay in that volume.
     * Keep the full GPT geometry separately for physical write guards.
     */
    return vb.root_valid ? 4194304ULL * 4096 : vb.blocks * 4096;
}

static void vb_restore(void)
{
    if (!vb.owned) return;
    if (rd(UFS + 0x58)) {
        printf("VERMEER BLOCK: DMA pending; device disabled until reset\n");
        vb.failed = true;
        return;
    }
    wr(0x60, 0);
    if (wait_register(0x60, 1, 0, 1)) return;
    wr(0x50, vb.oldlo);
    wr(0x54, vb.oldhi);
    wr(0x60, vb.oldrun);
    vb.owned = false;
    printf("VERMEER BLOCK: firmware list restored\n");
}
static int vb_take(void)
{
    if (identity_candidate() || (rd(UFS + 0x30) & 15) != 15 ||
        rd(UFS + 0x34) != 1 || rd(UFS + 0x58) || rd(UFS + 0x78) || rd(UFS + 0x24)) return ENXIO;
    vb.oldlo = rd(UFS + 0x50); vb.oldhi = rd(UFS + 0x54); vb.oldrun = rd(UFS + 0x60);
    zero(DMA, DMA_SIZE);
    dma_sync();
    wr(0x60, 0);
    vb.owned = true;
    if (wait_register(0x60, 1, 0, 1)) return ENXIO;
    wr(0x50, vu_lo(dma_pa)); wr(0x54, vu_hi(dma_pa)); wr(0x60, 1);
    if (rd(UFS + 0x50) != vu_lo(dma_pa) || rd(UFS + 0x54) != vu_hi(dma_pa) || rd(UFS + 0x60) != 1) return ENXIO;
    return 0;
}
static int vb_read_blocks(uint64_t lba, unsigned count)
{
    if (vb.failed) return EIO;
    if (!count || count > 2 || lba > EXPECTED_LAST_LBA ||
        count > EXPECTED_LAST_LBA + 1ULL - lba) return EINVAL;
    uint32_t ocs = 0xff, status = 0xff;
    int rc = scsi_at(0x28, count * 4096, (uint32_t)lba, &ocs, &status);
    if (rc) {
        printf("VERMEER BLOCK: read error rc=%d ocs=%x status=%x; disabling reads\n", rc, ocs, status);
        vb.failed = true;
        /* Never clear a timeout or submit more commands to pending DMA. */
        vb_restore();
        return EIO;
    }
    return 0;
}
static int vb_write_blocks(uint64_t lba, unsigned count)
{
#if defined(VERMEER_UFS_IOS_PROFILE)
    if (!vb.writable || vb.failed || !vi_write_allowed(ios_media_writable, vb.first, vb.blocks, lba, count)) return EROFS;
    uint32_t ocs = 0xff, status = 0xff;
    int rc = scsi_at(0x2a, count * 4096, (uint32_t)lba, &ocs, &status);
    if (rc) {
        printf("VERMEER ROOT: write error rc=%d ocs=%x status=%x; disabling device\n", rc, ocs, status);
        vb.failed = true; ios_media_writable = 0; vb_restore(); return EIO;
    }
    return 0;
#else
    (void)lba; (void)count; return EROFS;
#endif
}
static int vb_sync(void)
{
#if defined(VERMEER_UFS_IOS_PROFILE)
    if (vb.failed) return EIO;
    if (vb.writable) {
        uint32_t ocs = 0xff, status = 0xff;
        int rc = scsi_at(0x35, 0, 0, &ocs, &status);
        if (rc) { vb.failed = true; ios_media_writable = 0; vb_restore(); return EIO; }
    }
#endif
    return 0;
}
static int vb_open(dev_t dev, int flags, __unused int type, __unused struct proc *p)
{
    if (minor(dev) || !vb.ready || vb.failed) return ENXIO;
    return (flags & FWRITE) && !vb.writable ? EROFS : 0;
}
static int vb_close(__unused dev_t d, __unused int f, __unused int t, __unused struct proc *p)
{
    lck_mtx_lock(&vermeer_ufs_lock);
    int error = vb_sync();
    lck_mtx_unlock(&vermeer_ufs_lock);
    return error;
}
static void vb_strategy(buf_t bp)
{
    uint64_t off;
    uint32_t count = buf_count(bp);
    caddr_t va = NULL;
    int error = 0;
    buf_setresid(bp, count);
    bool read = (buf_flags(bp) & B_READ) != 0;
    if (!read && !vb.writable) { error = EROFS; goto done; }
    /* physio's B_PHYS b_datap is a USER address. buf_map returns that address
     * unchanged for a non-cluster buffer; it does not create a kernel mapping.
     * Raw character reads below use uiomove instead. Never memcpy B_PHYS here.
     */
    if (buf_flags(bp) & B_PHYS) { error = EINVAL; goto done; }
    if (!vb.ready || vb.failed || minor(buf_device(bp))) { error = ENXIO; goto done; }
    if (!count) goto done;
    if (buf_map(bp, &va)) { error = EIO; goto done; }
    lck_mtx_lock(&vermeer_ufs_lock);
    if (vg_io_bounds(buf_blkno(bp), vb.secsize, count, vb_device_bytes(), &off)) {
        error = EINVAL;
        goto unlock;
    }
    for (unsigned copied = 0; copied < count;) {
        uint64_t pos = off + copied;
        unsigned skip = pos % 4096;
        unsigned want = MIN(count - copied, 8192 - skip);
        unsigned blocks = (skip + want + 4095) / 4096;
        /* Partial writes preserve untouched bytes of the physical4KiB block. */
        if (read || skip || want % 4096) error = vb_read_blocks(vb.first + pos / 4096, blocks);
        if (error) break;
        if (read) memcpy(va + copied, (const void *)(DATA + skip), want);
        else {
            memcpy((void *)(DATA + skip), va + copied, want);
            error = vb_write_blocks(vb.first + pos / 4096, blocks);
            if (error) break;
        }
        copied += want;
        buf_setresid(bp, count - copied);
    }
unlock:
    lck_mtx_unlock(&vermeer_ufs_lock);
    buf_unmap(bp);
done:
    if (error) buf_seterror(bp, error);
    buf_biodone(bp);
}
static int vb_ioctl(dev_t dev, u_long cmd, caddr_t data, __unused int flags,
                    __unused struct proc *p, bool ischar)
{
    if (minor(dev) || !vb.ready || vb.failed) return ENXIO;
    uint32_t *f = (uint32_t *)data;
    uint64_t *n = (uint64_t *)data;
    switch (cmd) {
    case DKIOCGETBLOCKSIZE: *f = vb.secsize; return 0;
    case DKIOCSETBLOCKSIZE:
        /* Physical layout is fixed; only HFS's initial logical-size negotiation.
         * VFS may call this before the first strategy request. No media mutation.
         */
        if (ischar) return ENODEV;
        if (*f < 512 || *f > 4096 || (*f & (*f - 1))) return EINVAL;
        lck_mtx_lock(&vermeer_ufs_lock);
        vb.secsize = *f;
        lck_mtx_unlock(&vermeer_ufs_lock);
        return 0;
    case DKIOCGETPHYSICALBLOCKSIZE: *f = 4096; return 0;
    case DKIOCGETBLOCKCOUNT: *n = vb_device_bytes() / vb.secsize; return 0;
    case DKIOCISWRITABLE: *f = vb.writable; return 0;
    case DKIOCGETMAXBLOCKCOUNTREAD: *n = 8192 / vb.secsize; return 0;
    case DKIOCGETMAXBLOCKCOUNTWRITE: *n = vb.writable ? 8192 / vb.secsize : 0; return 0;
    case DKIOCSYNCHRONIZECACHE: {
        lck_mtx_lock(&vermeer_ufs_lock);
        int error = vb_sync();
        lck_mtx_unlock(&vermeer_ufs_lock);
        return error;
    }
    case DKIOCGETMAXSEGMENTCOUNTREAD:
    case DKIOCGETMAXSEGMENTCOUNTWRITE: *n = 1; return 0;
    default: return ENOTTY;
    }
}
static int vb_bioctl(dev_t d, u_long c, caddr_t a, int f, struct proc *p)
{ return vb_ioctl(d, c, a, f, p, false); }
static int vb_cioctl(dev_t d, u_long c, caddr_t a, int f, struct proc *p)
{ return vb_ioctl(d, c, a, f, p, true); }
static int vb_psize(__unused dev_t d)
{
    /* XNU specfs set_blocksize() stores d_psize's return in v_specsize.
     * This callback is a sector BYTE SIZE here, not a partition sector count.
     * Returning the count makes spec_read reject v_specsize > PAGE_SIZE.
     * Geometry remains available separately through DKIOCGETBLOCKCOUNT.
     */
    return vb.ready && !vb.failed ? (int)vb.secsize : -1;
}
static int vb_read(dev_t d, struct uio *u, __unused int f)
{
    if (minor(d) || !vb.ready || vb.failed) return ENXIO;
    int64_t offset = uio_offset(u), residual = uio_resid(u);
    uint64_t part_bytes = vb_device_bytes();
    if (offset < 0 || residual < 0 || (uint64_t)offset > part_bytes) return EINVAL;
    int error = 0;
    lck_mtx_lock(&vermeer_ufs_lock);
    while (uio_resid(u) > 0 && (uint64_t)uio_offset(u) < part_bytes) {
        uint64_t pos = (uint64_t)uio_offset(u);
        unsigned skip = pos % 4096;
        unsigned want = (unsigned)MIN((uint64_t)uio_resid(u), 8192 - skip);
        want = (unsigned)MIN((uint64_t)want, part_bytes - pos);
        unsigned blocks = (skip + want + 4095) / 4096;
        error = vb_read_blocks(vb.first + pos / 4096, blocks);
        if (error) break;
        /* uiomove uses the UIO address-space tag and copyout fault recovery.
         * It updates the file offset/residual only for bytes actually copied.
         */
        error = uiomove((caddr_t)(DATA + skip), (int)want, u);
        if (error) break;
    }
    lck_mtx_unlock(&vermeer_ufs_lock);
    return error;
}
static int vb_write(dev_t d, struct uio *u, __unused int f)
{
    if (!vb.writable) return EROFS;
    if (minor(d) || !vb.ready || vb.failed) return ENXIO;
    int64_t offset = uio_offset(u), residual = uio_resid(u);
    uint64_t limit = 4194304ULL * 4096;
    if (offset < 0 || residual < 0 || (uint64_t)offset > limit ||
        (uint64_t)residual > limit - (uint64_t)offset) return EINVAL;
    int error = 0;
    lck_mtx_lock(&vermeer_ufs_lock);
    while (uio_resid(u) > 0) {
        uint64_t pos = (uint64_t)uio_offset(u);
        unsigned skip = pos % 4096;
        unsigned want = (unsigned)MIN((uint64_t)uio_resid(u), 8192 - skip);
        unsigned blocks = (skip + want + 4095) / 4096;
        if (skip || want % 4096) error = vb_read_blocks(vb.first + pos / 4096, blocks);
        if (error) break;
        error = uiomove((caddr_t)(DATA + skip), (int)want, u);
        if (error) break;
        error = vb_write_blocks(vb.first + pos / 4096, blocks);
        if (error) break;
    }
    lck_mtx_unlock(&vermeer_ufs_lock);
    return error;
}
static const struct bdevsw vb_bdevsw = {
    .d_open = vb_open, .d_close = vb_close, .d_strategy = vb_strategy,
    .d_ioctl = vb_bioctl, .d_dump = eno_dump, .d_psize = vb_psize, .d_type = D_DISK,
};
static const struct cdevsw vb_cdevsw = {
    .d_open = vb_open, .d_close = vb_close, .d_read = vb_read, .d_write = vb_write,
    .d_ioctl = vb_cioctl, .d_stop = eno_stop, .d_reset = eno_reset, .d_ttys = NULL,
    .d_select = eno_select, .d_mmap = eno_mmap, .d_strategy = eno_strat,
    .d_reserved_1 = eno_getc, .d_reserved_2 = eno_putc, .d_type = D_DISK,
};
int vermeer_ufs_block_init(void)
{
    if (vb.attempted) return EBUSY;
    vb.attempted = true;
    /* This also allocates/maps memory, activates UFS and checks the LUN0 CRC.
     * It restores the firmware list before the partition adapter takes over.
     */
    int error = vermeer_ufs_probe();
    if (error) return error;
    vb.secsize = 512;
    lck_mtx_lock(&vermeer_ufs_lock);
    error = vb_take();
    struct vermeer_gpt g;
    if (!error) error = vb_read_blocks(1, 1);
    if (!error) error = vg_header((const uint8_t *)DATA, EXPECTED_LAST_LBA, &g);
    if (!error) error = vb_read_blocks(g.entries_lba, (g.entries_bytes + 4095) / 4096);
    if (!error) error = vg_userdata((const uint8_t *)DATA, &g);
    /* Require the independent Android inventory, not just a name match. */
    if (!error && (g.first != 2570304 || g.blocks != 59272123)) error = ENXIO;
    if (!error) {
        vb.first = g.first; vb.blocks = g.blocks;
        printf("VERMEER BLOCK: GPT CRC/bounds verified; userdata first=%llu blocks=%llu\n", vb.first, vb.blocks);
    }
#if defined(VERMEER_UFS_IOS_PROFILE)
    if (!error) {
        char uuid_text[17] = {0}; uint64_t uuid = 0; int rw = 0;
        PE_parse_boot_argn("vermeer_root_uuid", uuid_text, sizeof(uuid_text));
        PE_parse_boot_argn("vermeer_ufs_rw", &rw, sizeof(rw));
        error = vb_read_blocks(vb.first, 1);
        if (!error && (vi_uuid(uuid_text, &uuid) || !vi_hfs_root((const uint8_t *)DATA, uuid))) error = EINVAL;
        if (!error) {
            vb.root_valid = true;
            vb.writable = rw == 1;
            ios_media_first = vb.first; ios_media_blocks = vb.blocks;
            ios_media_writable = vb.writable;
            printf("VERMEER ROOT: prepared16GiB journaled HFSX UUID matched; %s\n", vb.writable ? "READ-WRITE" : "READ ONLY");
        }
    }
#endif
    if (error) vb_restore();
    lck_mtx_unlock(&vermeer_ufs_lock);
    if (error) { printf("VERMEER BLOCK: init failed %d; RAM root retained\n", error); return error; }
    vb.bmajor = bdevsw_add(-1, &vb_bdevsw);
    if (vb.bmajor < 0) { vb_restore(); return ENXIO; }
    vb.cmajor = cdevsw_add_with_bdev(-1, &vb_cdevsw, vb.bmajor);
    if (vb.cmajor < 0) { vb_restore(); return ENXIO; }
    void *bn = devfs_make_node(makedev(vb.bmajor, 0), DEVFS_BLOCK, UID_ROOT, GID_OPERATOR, vb.writable ? 0600 : 0400, "vermeerufs0");
    void *cn = devfs_make_node(makedev(vb.cmajor, 0), DEVFS_CHAR, UID_ROOT, GID_OPERATOR, vb.writable ? 0600 : 0400, "rvermeerufs0");
    if (!bn || !cn) { vb_restore(); return ENXIO; }
    vb.ready = true;
    printf("VERMEER BLOCK: /dev/{r,}vermeerufs0 ready %s\n", vb.writable ? "READ-WRITE" : "READ ONLY");
    return 0;
}
#if defined(VERMEER_UFS_IOS_PROFILE)
int vermeer_ufs_ios_rootdev(dev_t *dev);
int vermeer_ufs_ios_rootdev(dev_t *dev)
{
    int error = vb.ready ? 0 : vermeer_ufs_block_init();
    if (error) return error;
    if (!vb.root_valid || vb.failed) return ENXIO;
    *dev = makedev(vb.bmajor, 0); return 0;
}
#endif
#endif
