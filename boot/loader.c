/*
 * Milestone 4: act as iBoot for xnu.
 *
 * mkpayload.py already resolved the kernel's chained fixups and built the
 * ADT, so this only has to: find the payload in the ABL ramdisk, copy the
 * segments to phys_base + (vmaddr - virt_base), place the ADT and
 * boot_args behind the kernel, and jump to the entry point with the MMU
 * and caches off, x0 = boot_args (physical).
 */
#include <stdint.h>
#include "console.h"
#include "fdt.h"
#include "loader.h"
#include "ram_guard.h"
#include "boot_fdt.h"

#define BOOT_LINE_LENGTH	608
#define PAGE_16K		0x4000UL

struct payload_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[];
};

/* pexpert/pexpert/arm64/boot.h, Revision/Version 2 */
struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags, memSizeActual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == 0x2a8, "payload header layout");
_Static_assert(__builtin_offsetof(struct boot_args, Video) == 40, "boot_args layout");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine) == 108, "boot_args layout");
_Static_assert(sizeof(struct boot_args) == 736, "boot_args layout");

/* MMU off: everything is Device memory, so copy in aligned 8-byte words. */
static void copy64(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	volatile const uint64_t *s = (volatile const uint64_t *)src;
	for (uint64_t i = 0; i < (n + 7) / 8; i++)
		d[i] = s[i];
}

static void zero64(uintptr_t dst, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	for (uint64_t i = 0; i < n / 8; i++)
		d[i] = 0;
}

static int magic_ok(const char *m)
{
	static const char want[] = "SWEETXNU";
	for (int i = 0; i < 8; i++)
		if (m[i] != want[i])
			return 0;
	return 1;
}

typedef void (*xnu_entry_t)(uint64_t boot_args_pa);
extern char _head[], _end[];
extern const char embedded_payload_begin[], embedded_payload_end[];

static int overlaps(uint64_t start, uint64_t end, uint64_t lo, uint64_t hi)
{
	return start < hi && end > lo;
}

const char *loader_boot(uintptr_t dtb)
{
	uint64_t rd_start = (uintptr_t)embedded_payload_begin;
	uint64_t rd_end = (uintptr_t)embedded_payload_end;
	con_hex2("PAYLOAD", "lo=", rd_start, "hi=", rd_end);
	if ((rd_start & 7) || rd_end <= rd_start || rd_end - rd_start > 0x10000000)
		return "invalid embedded payload range";
	if (rd_end - rd_start < sizeof(struct payload))
		return "embedded payload too small";

	const struct payload *p = (const struct payload *)rd_start;
	if (!magic_ok(p->magic))
		return "no SWEETXNU payload";
	if (p->version != 1 || (p->adt_off & 7) || (rd_start & 7))
		return "bad payload header";
	uint64_t available = rd_end - rd_start;
	if (!p->nsegs || p->nsegs > 128 ||
	    sizeof(*p) + p->nsegs * sizeof(struct payload_seg) > available ||
	    p->adt_off > available || p->adt_size > available - p->adt_off)
		return "payload bounds invalid";
	int highram = p->phys_base == HIGH_RAM_LO && p->mem_size == HIGH_RAM_HI - HIGH_RAM_LO;
	if (!highram && (p->phys_base != 0xc0000000 || p->mem_size != 0x10000000))
		return "RAM profile mismatch";
	if (highram) {
		/* Fail before copying any segment into high RAM. The guard was tested
		 * under ASan/UBSan and probe011 checked this physical range on K70.
		 */
		if (dtb < 0xb5d00000 || dtb >= 0xb5f00000 || (dtb & 7) ||
		    ram_guard((const uint8_t *)dtb, 0xb5f00000 - dtb, HIGH_RAM_LO, HIGH_RAM_HI))
			return "high RAM live FDT guard refused";
	}
	if (p->kernel_end < p->virt_base || p->kernel_end - p->virt_base > p->mem_size)
		return "kernel end outside RAM";
	uint64_t lo = p->phys_base, hi = lo + p->mem_size;
	if (overlaps((uintptr_t)_head, (uintptr_t)_end, lo, hi) ||
	    overlaps(dtb, dtb + 0x100000, lo, hi))
		return "shim or FDT overlaps kernel RAM";
	int entry_valid = 0;
	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		if (s->vmaddr < p->virt_base || s->vmaddr - p->virt_base > p->mem_size ||
		    s->vmsize > p->mem_size - (s->vmaddr - p->virt_base) ||
		    s->file_size > s->vmsize || s->file_off > available ||
		    s->file_size > available - s->file_off || (s->file_off & 7) || (s->vmaddr & 7))
			return "segment bounds invalid";
		if (p->entry_va >= s->vmaddr && p->entry_va - s->vmaddr < s->file_size)
			entry_valid = 1;
	}
	if (!entry_valid)
		return "entry outside loaded segments";

	uint64_t pa_of_va_base = p->phys_base - p->virt_base;	/* pa = va + this */
	con_hex("PHYS", p->phys_base);
	con_hex("MEMSIZE", p->mem_size);
	con_hex("VIRT", p->virt_base);
	con_hex("ENTRY", p->entry_va);

	/* The payload must not overlap where the kernel goes. */
	if (rd_end > p->phys_base && rd_start < p->phys_base + p->mem_size)
		return "ramdisk overlaps kernel memory";
	uint64_t final_data = p->kernel_end - p->virt_base;
	if (p->adt_size > p->mem_size - final_data ||
	    p->mem_size - final_data - p->adt_size < 2 * PAGE_16K)
		return "ADT/boot args exceed RAM";

	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uintptr_t dst = s->vmaddr + pa_of_va_base;
		copy64(dst, rd_start + s->file_off, s->file_size);
		if (s->vmsize > s->file_size)
			zero64(dst + ((s->file_size + 7) & ~7UL), s->vmsize - ((s->file_size + 7) & ~7UL));
	}
	con_hex("SEGS", p->nsegs);
	con_hex("DATAEND", p->kernel_end + pa_of_va_base);

	/* ADT, then boot_args, behind the kernel; everything below topOfKernelData is ours. */
	uintptr_t adt_pa = p->kernel_end + pa_of_va_base;
	copy64(adt_pa, rd_start + p->adt_off, p->adt_size);
	size_t boot_fdt_offset;
	int boot_fdt_present = boot_fdt_slot((const uint8_t *)adt_pa, p->adt_size, &boot_fdt_offset);
	if (boot_fdt_present < 0)
		return "invalid boot FDT ADT slot";
	if (boot_fdt_present) {
		/* highram's complete live FDT guard has already checked the actual
		 * ABL pointer and its hard physical mapping bound, before RAM writes.
		 */
		if (!highram || boot_fdt_bridge((uint8_t *)adt_pa, p->adt_size,
		    (const uint8_t *)dtb, 0xb5f00000 - dtb) != 1)
			return "live boot FDT copy refused";
	}
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	uintptr_t top = ba_pa + PAGE_16K;

	zero64(ba_pa, PAGE_16K);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = p->phys_base;
	ba->memSize = p->mem_size;
	ba->topOfKernelData = top;
	ba->Video.v_baseAddr = FB_BASE;
	ba->Video.v_display = 1;
	ba->Video.v_rowBytes = FB_STRIDE;
	ba->Video.v_width = FB_W;
	ba->Video.v_height = FB_H;
	ba->Video.v_depth = 32;
	ba->deviceTreeP = adt_pa - pa_of_va_base;	/* xnu reads it with the MMU on: a VA */
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (int i = 0; i < BOOT_LINE_LENGTH; i++)
		ba->CommandLine[i] = p->cmdline[i];
	ba->memSizeActual = p->mem_size;
	con_hex("BOOTARG", ba_pa);
	con_hex("TOP", top);

	uintptr_t entry_pa = p->entry_va + pa_of_va_base;
	con_hex("JUMP", entry_pa);
	con_str("XNU", "jumping (fbcon clears screen)");

	/* Everything was written with the MMU and caches off; drop any stale I-cache lines too. */
	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	((xnu_entry_t)entry_pa)(ba_pa);
	return "xnu returned";
}
