/* Read-only BSD partition adapter based on reference0028, using the verified
 * vermeer transport. Compile this file INSTEAD of vermeer_ufs_probe.c.
 * Single command, 8KiB bounce buffer, permanent low-RAM DMA allocation.
 * No storage-writing opcode exists; FWRITE opens and strategy writes fail.
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
    bool ready, failed, owned, attempted;
    int bmajor, cmajor;
} vb;
static LCK_GRP_DECLARE(vermeer_ufs_grp, "vermeer_ufs");
static LCK_MTX_DECLARE(vermeer_ufs_lock, &vermeer_ufs_grp);

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
    wr(0x50, (uint32_t)dma_pa); wr(0x54, 0); wr(0x60, 1);
    if (rd(UFS + 0x50) != dma_pa || rd(UFS + 0x54) || rd(UFS + 0x60) != 1) return ENXIO;
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
static int vb_open(dev_t dev, int flags, __unused int type, __unused struct proc *p)
{
    if (minor(dev) || !vb.ready || vb.failed) return ENXIO;
    return (flags & FWRITE) ? EROFS : 0;
}
static int vb_close(__unused dev_t d, __unused int f, __unused int t, __unused struct proc *p)
{
    return 0;
}
static void vb_strategy(buf_t bp)
{
    uint64_t off;
    uint32_t count = buf_count(bp);
    caddr_t va = NULL;
    int error = 0;
    buf_setresid(bp, count);
    if (!(buf_flags(bp) & B_READ)) { error = EROFS; goto done; }
    if (!vb.ready || vb.failed || minor(buf_device(bp))) { error = ENXIO; goto done; }
    if (!count) goto done;
    if (buf_map(bp, &va)) { error = EIO; goto done; }
    lck_mtx_lock(&vermeer_ufs_lock);
    if (vg_io_bounds(buf_blkno(bp), vb.secsize, count, vb.blocks * 4096, &off)) {
        error = EINVAL;
        goto unlock;
    }
    for (unsigned copied = 0; copied < count;) {
        uint64_t pos = off + copied;
        unsigned skip = pos % 4096;
        unsigned want = MIN(count - copied, 8192 - skip);
        unsigned blocks = (skip + want + 4095) / 4096;
        error = vb_read_blocks(vb.first + pos / 4096, blocks);
        if (error) break;
        memcpy(va + copied, (const void *)(DATA + skip), want);
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
    case DKIOCGETBLOCKCOUNT: *n = vb.blocks * 4096 / vb.secsize; return 0;
    case DKIOCISWRITABLE: *f = 0; return 0;
    case DKIOCGETMAXBLOCKCOUNTREAD: *n = 8192 / vb.secsize; return 0;
    case DKIOCGETMAXBLOCKCOUNTWRITE: *n = 0; return 0;
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
{ return vb.ready && !vb.failed ? (int)MIN(vb.blocks * 4096 / vb.secsize, INT_MAX) : -1; }
static int vb_read(dev_t d, struct uio *u, __unused int f)
{ return physio(vb_strategy, NULL, d, B_READ, minphys, u, (int)vb.secsize); }
static int vb_write(__unused dev_t d, __unused struct uio *u, __unused int f)
{ return EROFS; }
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
    } else vb_restore();
    lck_mtx_unlock(&vermeer_ufs_lock);
    if (error) { printf("VERMEER BLOCK: init failed %d; RAM root retained\n", error); return error; }
    vb.bmajor = bdevsw_add(-1, &vb_bdevsw);
    if (vb.bmajor < 0) { vb_restore(); return ENXIO; }
    vb.cmajor = cdevsw_add_with_bdev(-1, &vb_cdevsw, vb.bmajor);
    if (vb.cmajor < 0) { vb_restore(); return ENXIO; }
    void *bn = devfs_make_node(makedev(vb.bmajor, 0), DEVFS_BLOCK, UID_ROOT, GID_OPERATOR, 0400, "vermeerufs0");
    void *cn = devfs_make_node(makedev(vb.cmajor, 0), DEVFS_CHAR, UID_ROOT, GID_OPERATOR, 0400, "rvermeerufs0");
    if (!bn || !cn) { vb_restore(); return ENXIO; }
    vb.ready = true;
    printf("VERMEER BLOCK: /dev/{r,}vermeerufs0 ready READ ONLY; keeping rd=md0\n");
    return 0;
}
#endif
