/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bootmem_x86.c — x86_64 early memory from the firmware map.
 *
 * The large buffers that used to sit in BSS (and so in a fixed window over
 * whatever firmware kept at 1-91 MB) are taken from here instead: ranges the
 * firmware called usable, past the kernel and its boot stack, inside the 1 GB identity map
 * boot.asm builds. See zxv_bootmem.h for the evidence rules. */
#include "bootmem_x86.h"

#include "../zxv_bootmem.h"
#include "efifb_con.h"
#include "uart_16550.h"

#define MB1_MAGIC    0x2BADB002u
#define MB1_F_MMAP   (1u << 6)
#define IDENTITY_TOP 0x40000000ULL /* boot.asm maps the low 1 GB */

/* The boot stack sits after __kernel_end (linker64.ld), so the floor is the
 * stack's top, not the kernel's end. */
extern char __stack_top[];

static zxb_t g_bm;
static const zxh_t *g_handoff;

const zxh_t *zxv_handoff(void)
{
    return g_handoff;
}

static uint32_t rd32(uint64_t a)
{
    return *(const volatile uint32_t *) (uintptr_t) a;
}

uint32_t bootmem_x86_init(uint32_t mb_magic, uint64_t mb_info)
{
    uint64_t floor = zxb_up((uint64_t) (uintptr_t) __stack_top);
    const zxh_t *h = (const zxh_t *) (uintptr_t) (ZXV_X86_BOOTINFO_ADDR + ZXH_OFFSET);
    const char *src = "none";

    if (zxh_accept(h) == 1) {
        g_handoff = h;
        zxb_from_handoff(&g_bm, h, floor, IDENTITY_TOP);
        src = "handoff v2 map";
    } else if (mb_magic == MB1_MAGIC && mb_info && mb_info < IDENTITY_TOP &&
               (rd32(mb_info) & MB1_F_MMAP)) {
        uint32_t len = rd32(mb_info + 44u);
        uint64_t map = rd32(mb_info + 48u);
        if (map && map + len <= IDENTITY_TOP)
            zxb_from_multiboot1(&g_bm, (const uint8_t *) (uintptr_t) map, len, floor, IDENTITY_TOP);
        /* The loader's own structures stay untouched. */
        zxb_exclude(&g_bm, mb_info, mb_info + 88u);
        zxb_exclude(&g_bm, map, map + len);
        src = "multiboot map";
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

void *bootmem_x86_take(uint64_t bytes, const char *what)
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

/* The shared RAM disk's store (kernel/src/blockdev/ramdisk.c built with
 * ZXV_RAMDISK_PROVIDED). */
uint8_t *zxv_ramdisk_backing(uint32_t bytes)
{
    return bootmem_x86_take(bytes, "the RAM disk");
}
