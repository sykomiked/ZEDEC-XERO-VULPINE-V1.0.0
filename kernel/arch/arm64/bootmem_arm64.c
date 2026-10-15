/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bootmem_arm64.c — arm64 early memory from the firmware map.
 *
 * The arm64 kernel kept about 230 MB in BSS (two vino ledgers, the triple
 * ledger, the scanout and PRISM buffers, the RAM disk, the console machines),
 * so its fixed window ran from 0x40080000 to past 0x4e900000 whatever the
 * board kept there. Those buffers now come from ranges the firmware called
 * usable, past __kernel_end and inside the 1-4 GB identity map arm64_mmu.c
 * builds. Evidence rules: zxv_bootmem.h. */
#include "bootmem_arm64.h"

#include "../zxv_bootmem.h"
#include "zxv_bootinfo.h"

extern void uart_puts(const char *s);
extern void uart_put_dec(uint64_t v);

#define RAM_LO       0x40000000ULL  /* first RAM GB on every board profile */
#define IDENTITY_TOP 0x100000000ULL /* arm64_mmu.c maps VA 1-4 GB 1:1 */
#define FDT_MAX      0x200000u      /* a device tree bigger than 2 MB is refused */
#define LEGACY_TOP   0x4F000000ULL  /* where the 232 MB BSS used to end, rounded */

/* The stack sits inside BSS on arm64 (linker.ld), so the image's end is the
 * floor. */
extern char __kernel_end[];

static zxb_t g_bm;
static const zxh_t *g_handoff;

const zxh_t *zxv_handoff(void)
{
    return g_handoff;
}

uint32_t bootmem_arm64_init(uint64_t dtb)
{
    uint64_t floor = zxb_up((uint64_t) (uintptr_t) __kernel_end);
    const zxh_t *h = (const zxh_t *) (uintptr_t) (ZXV_BOOTINFO_ADDR + ZXH_OFFSET);
    const char *src = "none";

    if (zxh_accept(h) == 1) {
        g_handoff = h;
        zxb_from_handoff(&g_bm, h, floor, IDENTITY_TOP);
        src = "handoff v2 map";
    } else if (dtb >= RAM_LO && dtb + FDT_MAX <= IDENTITY_TOP && !(dtb & 7u)) {
        /* Inside mapped RAM and aligned, so reading the header is safe; the
         * parser then proves it is a device tree or takes nothing. */
        if (zxb_from_fdt(&g_bm, (const uint8_t *) (uintptr_t) dtb, FDT_MAX, floor, IDENTITY_TOP))
            src = "device tree";
    }
    if (g_bm.n == 0 && !g_handoff) {
        /* NEITHER: no map and no device tree (QEMU -kernel with an ELF image
         * passes x0 = 0 and loads no tree below a kernel at 0x40080000). The
         * kernel's BSS already claimed up to LEGACY_TOP before these buffers
         * left it, so this claims nothing new, and it says so. */
        zxb_add(&g_bm, floor, LEGACY_TOP - floor, floor, LEGACY_TOP);
        src = "former BSS window (no firmware map, no device tree)";
    }
    uart_puts("[BOOTMEM] ");
    uart_put_dec(g_bm.n);
    uart_puts(" usable ranges past the kernel from the ");
    uart_puts(src);
    uart_puts(", ");
    uart_put_dec(zxb_remaining(&g_bm) >> 20);
    uart_puts(" MiB");
    if (g_bm.dropped) uart_puts(" (some ranges dropped: table full)");
    uart_puts("\n");
    return g_bm.n;
}

void *bootmem_arm64_take(uint64_t bytes, const char *what)
{
    uint64_t pa = zxb_take(&g_bm, bytes);
    if (!pa) {
        uart_puts("[BOOTMEM] no usable memory for ");
        uart_puts(what);
        uart_puts("\n");
        return 0;
    }
    uint64_t *p = (uint64_t *) (uintptr_t) pa;
    for (uint64_t i = 0; i < zxb_up(bytes) / 8u; i++) p[i] = 0;
    return p;
}

/* Shared sources built with ZXV_BOOT_TAKE (zxv_shell.c, game_runner.c) take
 * their large state through this instead of BSS. */
void *zxv_boot_take(uint64_t bytes, const char *what)
{
    return bootmem_arm64_take(bytes, what);
}
