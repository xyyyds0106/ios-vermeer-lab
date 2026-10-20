/* Storage-free handoff to a separately validated vermeer-early kernel. */
#include <stdint.h>
#include "console.h"
#include "loader.h"
#define R(r) ({ uint64_t v; __asm__ volatile("mrs %0, " r : "=r"(v)); v; })

static void reset_after(unsigned seconds)
{
    uint64_t end = R("cntpct_el0") + R("cntfrq_el0") * seconds;
    while (R("cntpct_el0") < end) __asm__ volatile("yield");
    register uint64_t x0 __asm__("x0") = 0x84000009;
    register uint64_t x1 __asm__("x1") = 0;
    register uint64_t x2 __asm__("x2") = 0;
    register uint64_t x3 __asm__("x3") = 0;
    __asm__ volatile("dsb sy; smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
                     : : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
                         "x12", "x13", "x14", "x15", "x16", "x17", "memory");
    for (;;) __asm__ volatile("wfe");
}

void probe_exception(uint64_t slot, uint64_t *regs)
{
    (void)regs;
    __asm__ volatile("msr daifset, #15");
    con_str("EXCEPT", "vermeer shim exception (before XNU)");
    con_hex("SLOT", slot);
    con_hex("ESR", R("esr_el1"));
    con_hex("ELR", R("elr_el1"));
    con_hex("FAR", R("far_el1"));
    reset_after(90);
}

void boot_main(uintptr_t fdt)
{
    con_clear(BLUE);
    con_str("VERMEER", "XNU early boot shim - NOT iOS");
    con_hex("IMAGE", (uintptr_t)boot_main);
    con_hex("FDT", fdt);
    con_str("LOADER", loader_boot(fdt));
    con_str("RESET", "in 90s; no partitions written");
    reset_after(90);
}
