/* Opt-in XNU UFS read self-test, adapted from verified K70 probe010.
 * Uses a permanent, contiguous 32KiB allocation in tested RAM; physical DMA
 * descriptors and kernel virtual pointers are kept distinct. Explicit cache
 * maintenance surrounds each request. Diagnostic builds have no disk writes.
 * The IOS wrapper alone adds bounded volume writes and cache synchronization;
 * no erase/format/partition command exists. This self-test only reads LUN0.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <kern/clock.h>
#include <machine/machine_routines.h>
#include <arm/caches_internal.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <pexpert/pexpert.h>
#include "vermeer_ufs_response.h"
#include "vermeer_ufs_dma.h"
#if defined(VERMEER_UFS_IOS_PROFILE)
#include "vermeer_media_policy.h"
static int ios_media_writable;
static uint64_t ios_media_first, ios_media_blocks;
#endif

extern ppnum_t pmap_find_phys(pmap_t pmap, addr64_t va);
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
int vermeer_ufs_probe(void);

static vm_offset_t ufs_regs, smmu_regs, dma_va;
static uint64_t dma_pa;
static int high_dma_enabled;
#define R(r) ({ uint64_t v; __asm__ volatile("mrs %0, " r : "=r"(v)); v; })
#define UFS ufs_regs
#define SMMU smmu_regs
#define DMA dma_va
#define COMMAND (DMA + 0x1000)
#define DATA (DMA + 0x4000)
#define DMA_END (DATA + 8192)
#define DMA_SIZE VU_DMA_SIZE
#define EXPECTED_LAST_LBA 61842431U
#define EXPECTED_CRC32 0x45fbee46U

static void text(unsigned row, const char *s)
{
    (void)row;
    printf("VERMEER UFS: %s\n", s);
}
static void hex(unsigned row, const char *s, uint64_t value)
{
    (void)row;
    printf("VERMEER UFS: %s%016llx\n", s, (unsigned long long)value);
}
static void dma_sync(void)
{
    flush_dcache64((addr64_t)dma_pa, DMA_SIZE, TRUE);
}
static int init_mappings(void)
{
    static bool attempted;
    if (attempted) return EBUSY;
    attempted = true;
    ufs_regs = ml_io_map(0x01d84000, 0x3000);
    smmu_regs = ml_io_map(0x15000000, 0x90000);
    if (!ufs_regs || !smmu_regs) return ENOMEM;
    PE_parse_boot_argn("vermeer_ufs_highdma", &high_dma_enabled, sizeof(high_dma_enabled));
    if (kmem_alloc_contig(kernel_map, &dma_va, DMA_SIZE, PAGE_MASK, 0, 0,
        KMA_DATA | KMA_ZERO | KMA_PERMANENT, VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS)
        return ENOMEM;
    ppnum_t first = pmap_find_phys(kernel_pmap, (addr64_t)dma_va);
    if (!first) return ENOMEM;
    dma_pa = ptoa_64(first);
    if (!vu_dma_allowed(dma_pa, high_dma_enabled,
        *(volatile uint32_t *)ufs_regs, *(volatile uint32_t *)(smmu_regs + 0x28))) return ENXIO;
    for (unsigned off = 0; off < DMA_SIZE; off += PAGE_SIZE) {
        ppnum_t pn = pmap_find_phys(kernel_pmap, (addr64_t)(dma_va + off));
        if (!pn || ptoa_64(pn) != dma_pa + off) return ENXIO;
    }
    return 0;
}

static struct ufs_sense last_sense;

static uint32_t rd(uintptr_t p) { return *(volatile uint32_t *)p; }
static void wr(unsigned off, uint32_t v)
{
    *(volatile uint32_t *)(UFS + off) = v;
    __asm__ volatile("dsb sy" ::: "memory");
}
static uint64_t ticks(void)
{
    uint64_t v;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v));
    return v;
}
static int wait_register(unsigned off, uint32_t mask, uint32_t expected, unsigned seconds)
{
    uint64_t end = ticks() + seconds * R("cntfrq_el0");
    do {
        if ((rd(UFS + off) & mask) == expected) return 0;
        __asm__ volatile("yield");
    } while (ticks() < end);
    return 1;
}
static void zero(uintptr_t p, unsigned n)
{
    for (unsigned i = 0; i < n; i++) *(volatile uint8_t *)(p + i) = 0;
}
static uint32_t be32(uintptr_t p)
{
    volatile const uint8_t *b = (volatile const uint8_t *)p;
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}
static uint32_t vermeer_ufs_crc32(uintptr_t p, unsigned n)
{
    uint32_t crc = ~0U;
    for (unsigned i = 0; i < n; i++) {
        crc ^= *(volatile uint8_t *)(p + i);
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1)));
    }
    return ~crc;
}

/* Validate that the exact nonsecure UFS route is unchanged, global matching
 * has only one match, context0 stage1 is disabled and stage2 bypasses.
 * This does not itself prove working DMA: the byte comparison below does.
 */
static int identity_candidate(void)
{
    if (rd(SMMU + 0x20) != 0x4c017e69 || rd(SMMU + 0x24) != 0x60000046 ||
        rd(SMMU) != 0x00280406 || rd(SMMU + 0x800) != 0x80000060 ||
        rd(SMMU + 0xc00) != 0) return 1;
    unsigned matches = 0;
    for (unsigned i = 0; i < 105; i++) {
        uint32_t smr = rd(SMMU + 0x800 + 4 * i);
        unsigned mask = (smr >> 16) & 0x7fff;
        if ((smr & (1u << 31)) && !((smr ^ 0x60) & ~mask & 0xffff)) matches++;
    }
    if (matches != 1 || ((rd(SMMU + 0x1000) >> 16) & 3) != 1 ||
        (rd(SMMU + 0x80000) & 1)) return 2;
    /* Static buffer range must stay inside independently tested System RAM;
     * never overlap the executing image or live FDT/bootloader request list.
     */
    if (!vu_dma_allowed(dma_pa, high_dma_enabled, rd(UFS), rd(SMMU + 0x28))) return 3;
    return 0;
}

/* Opcode/length allowlist prevents accidental storage-mutating commands.
 * On timeout, stop: never ring a second request on an outstanding transfer.
 */
static int scsi_at(uint8_t opcode, unsigned length, uint32_t lba,
                   uint32_t *ocs, uint32_t *status)
{
    static uint8_t tag;
    bool write = false, sync = false;
#if defined(VERMEER_UFS_IOS_PROFILE)
    write = opcode == 0x2a && length && length <= 8192 && !(length % 4096) &&
        vi_write_allowed(ios_media_writable, ios_media_first, ios_media_blocks, lba, length / 4096);
    sync = opcode == 0x35 && !length && !lba && ios_media_writable == 1;
#endif
    last_sense.response = last_sense.key = last_sense.asc = last_sense.ascq = 0xff;
    if (!(write || sync || (opcode == 0 && length == 0) || (opcode == 0x1b && length == 0) ||
          (opcode == 0x25 && length == 8) ||
          (opcode == 0x28 &&
#if defined(VERMEER_UFS_BLOCK)
           length && length <= 8192 && !(length % 4096) &&
           lba <= EXPECTED_LAST_LBA && length / 4096 <= EXPECTED_LAST_LBA + 1U - lba
#else
           length == 8192 && lba == 0
#endif
          ))) return 10;
    if (rd(UFS + 0x58) || rd(UFS + 0x50) != vu_lo(dma_pa) || rd(UFS + 0x54) != vu_hi(dma_pa) ||
        rd(UFS + 0x60) != 1) return 11;
    zero(DMA, 32);
    zero(COMMAND, 1040);
    if (!write) zero(DATA, 8192);
    volatile uint8_t *cmd = (volatile uint8_t *)COMMAND;
    cmd[0] = 1; /* UPIU_COMMAND */
    cmd[1] = length ? (write ? 0x20 : 0x40) : 0;
    uint8_t lun = opcode == 0x1b ? 0xd0 : 0;
    cmd[2] = lun; /* Power command only to device WLUN; data only LUN0. */
    cmd[3] = ++tag;
    cmd[12] = length >> 24;
    cmd[13] = length >> 16;
    cmd[14] = length >> 8;
    cmd[15] = length;
    cmd[16] = opcode;
    if (opcode == 0x1b) cmd[20] = 0x10; /* Active power mode, as Linux. */
    if (opcode == 0x28 || write) {
        cmd[18] = lba >> 24;
        cmd[19] = lba >> 16;
        cmd[20] = lba >> 8;
        cmd[21] = lba;
        cmd[23] = (length / 4096) >> 8;
        cmd[24] = length / 4096;
    }
    volatile uint32_t *utrd = (volatile uint32_t *)DMA;
    utrd[0] = (1U << 28) | ((length ? (write ? 1U : 2U) : 0U) << 25) | (1U << 24);
    utrd[2] = 15;
    vu_dma_descriptors(utrd, (volatile uint32_t *)(COMMAND + 1024), dma_pa, length);
    utrd[6] = (128U << 16) | 128U; /* 512-byte response at +512. */
    if (length) {
        utrd[7] = (256U << 16) | 1U; /* One PRDT entry at +1024. */
    }
    dma_sync();
    __asm__ volatile("dsb sy" ::: "memory");
    wr(0x58, 1);
    if (wait_register(0x58, 1, 0, opcode == 0x1b || write || sync ? 10 : 1)) return 1;
    __asm__ volatile("dsb sy" ::: "memory");
    dma_sync();
    *ocs = utrd[2] & 255;
    volatile uint8_t *resp = (volatile uint8_t *)(COMMAND + 512);
    *status = resp[7];
    return ufs_decode_response_lun(resp, tag, lun, *ocs, &last_sense);
}

static int scsi(uint8_t opcode, unsigned length, uint32_t *ocs, uint32_t *status)
{
    return scsi_at(opcode, length, 0, ocs, status);
}

static void result(unsigned row, const char *label, int rc, uint32_t ocs, uint32_t status)
{
    hex(row, label, ((uint64_t)(unsigned)rc << 32) | (ocs << 8) | status);
}

int vermeer_ufs_probe(void)
{
    int init_error = init_mappings();
    if (init_error) return init_error;
    text(0, "XNU VERMEER - UFS READ SELFTEST 001");
    text(1, "READ ONLY - NO DISK WRITE/ERASE");
    text(35, "NO PARTITIONS WRITTEN - NOT IOS");
    hex(3, "DMA PHYSICAL BUFFER: ", dma_pa);
    int guard = identity_candidate();
    hex(4, "IDENTITY GUARD (0=OK): ", guard);
    if (guard) {
        text(6, "MEMORY/MAPPING GUARD FAILED - STOP");
        return ENXIO;
    }
    if ((rd(UFS + 0x30) & 15) != 15 || rd(UFS + 0x34) != 1 ||
        rd(UFS + 0x58) || rd(UFS + 0x78) || rd(UFS + 0x24)) {
        text(6, "HOST NOT READY/IDLE - STOP");
        return ENXIO;
    }
    uint32_t oldlo = rd(UFS + 0x50), oldhi = rd(UFS + 0x54), oldrun = rd(UFS + 0x60);
    /* Zero the entire new request list before making it visible. */
    zero(DMA, DMA_END - DMA);
    __asm__ volatile("dsb sy" ::: "memory");
    wr(0x60, 0);
    if (wait_register(0x60, 1, 0, 1)) {
        text(6, "TRANSFER LIST DID NOT STOP");
        return ENXIO;
    }
    wr(0x50, vu_lo(dma_pa));
    wr(0x54, vu_hi(dma_pa));
    wr(0x60, 1);
    if (rd(UFS + 0x50) != vu_lo(dma_pa) || rd(UFS + 0x54) != vu_hi(dma_pa) || rd(UFS + 0x60) != 1) {
        text(6, "OWN TRANSFER LIST NOT ACCEPTED - STOP");
        return ENXIO;
    }
    text(6, "OWN DESCRIPTORS CONFIRMED");
    uint32_t ocs = 0xff, status = 0xff;
    int rc = 0;
    for (unsigned attempt = 0; attempt < 3; attempt++) {
        rc = scsi(0, 0, &ocs, &status);
        result(8 + attempt, "TUR RC/OCS/STATUS: ", rc, ocs, status);
        hex(11, "RESPONSE/KEY/ASC/ASCQ: ",
            (uint32_t)last_sense.response << 24 | (uint32_t)last_sense.key << 16 |
            (uint32_t)last_sense.asc << 8 | last_sense.ascq);
        if (!ufs_retry_tur(rc, status, &last_sense)) break;
        uint64_t until = ticks() + R("cntfrq_el0") / 10;
        while (ticks() < until) __asm__ volatile("yield");
    }
    /* The handset explicitly reported NOT READY/04/02: initialization
     * required. Request Active on device WLUN, never a data-LUN write.
     */
    if (rc == 5 && status == 2 && last_sense.key == 2 &&
        last_sense.asc == 4 && last_sense.ascq == 2) {
        text(22, "REQUESTING UFS ACTIVE POWER MODE");
        for (unsigned attempt = 0; attempt < 3; attempt++) {
            rc = scsi(0x1b, 0, &ocs, &status);
            result(24 + attempt, "ACTIVE RC/OCS/STATUS: ", rc, ocs, status);
            hex(27, "ACTIVE RESPONSE/KEY/ASC/Q: ",
                (uint32_t)last_sense.response << 24 | (uint32_t)last_sense.key << 16 |
                (uint32_t)last_sense.asc << 8 | last_sense.ascq);
            if (!(rc == 5 && status == 2 && last_sense.key == 6)) break;
        }
        if (rc) goto done;
        for (unsigned attempt = 0; attempt < 5; attempt++) {
            rc = scsi(0, 0, &ocs, &status);
            result(10, "ACTIVE TUR RC/OCS/STATUS: ", rc, ocs, status);
            if (!ufs_retry_tur(rc, status, &last_sense)) break;
            uint64_t until = ticks() + R("cntfrq_el0") / 10;
            while (ticks() < until) __asm__ volatile("yield");
        }
    }
    if (rc) goto done;
    rc = scsi(0x25, 8, &ocs, &status);
    result(12, "CAPACITY RC/OCS/STATUS: ", rc, ocs, status);
    if (rc) goto done;
    hex(13, "LAST LOGICAL BLOCK: ", be32(DATA));
    hex(14, "LOGICAL BLOCK BYTES: ", be32(DATA + 4));
    if (be32(DATA) != EXPECTED_LAST_LBA || be32(DATA + 4) != 4096) {
        text(16, "CAPACITY DOES NOT MATCH ANDROID - STOP");
        rc = 12;
        goto done;
    }
    rc = scsi(0x28, 8192, &ocs, &status);
    result(16, "READ RC/OCS/STATUS: ", rc, ocs, status);
    if (rc) goto done;
    unsigned differences = vermeer_ufs_crc32(DATA, 8192) != EXPECTED_CRC32;
    hex(18, "BYTES READ: ", 8192);
    hex(19, "CRC CHECK FAILED (0=OK): ", differences);
    hex(20, "READ CRC32: ", vermeer_ufs_crc32(DATA, 8192));
    hex(21, "ANDROID CRC32: ", EXPECTED_CRC32);
    text(23, differences ? "UFS READ: DATA MISMATCH" : "XNU UFS READ: PASS - ANDROID CRC MATCH");
    rc = differences ? 13 : 0;
done:
    hex(29, "SMMU CONTEXT FSR: ", rd(SMMU + 0x80058));
    hex(30, "FINAL RESULT (0=OK): ", rc);
    if (!rd(UFS + 0x58)) {
        wr(0x60, 0);
        if (!wait_register(0x60, 1, 0, 1)) {
            wr(0x50, oldlo);
            wr(0x54, oldhi);
            wr(0x60, oldrun);
            text(32, "FIRMWARE TRANSFER LIST RESTORED");
        }
    } else {
        text(32, "PENDING DMA - STOP UNTIL RESET");
    }
    return rc;
}
#endif
