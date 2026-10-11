/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* efifb_con.c — mirror the serial console onto the UEFI GOP framebuffer.
 * See efifb_con.h for the handoff contract. */
#include "efifb_con.h"
#include "vbe.h"

static vbe_state_t g_fb;
static int g_on, g_col, g_row, g_cols, g_rows;

#define FG 0x00D0D0D0u /* grey: same value in RGBx and BGRx, so no swap */
#define BG 0x00000000u

/* The boot map identity-maps only the first 1 GB; a GOP framebuffer normally
 * sits higher (OVMF q35: 0x80000000, real PCs: often 0xC0000000+ or above
 * 4 GB). Map each 1 GB slot the framebuffer touches with 2 MB pages through a
 * fresh L2 table, the way ring3.c edits the live tables. Slots below 512 GB
 * only (PML4[0]); anything else stays serial-only. */
static uint64_t g_fb_l2[2][512] __attribute__((aligned(4096)));

static int map_fb(uint64_t base, uint64_t size)
{
    uint64_t end = base + size - 1;
    if (end >> 39) return 0;
    uint64_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *l4 = (uint64_t *) (cr3 & ~0xFFFULL);
    if (!(l4[0] & 1)) return 0;
    uint64_t *l3 = (uint64_t *) (l4[0] & 0x000FFFFFFFFFF000ULL);
    int used = 0;
    for (uint64_t gb = base >> 30; gb <= (end >> 30); gb++) {
        if (l3[gb] & 1) continue; /* already mapped (first 1 GB) */
        if (used == 2) return 0;
        uint64_t *l2 = g_fb_l2[used++];
        for (int i = 0; i < 512; i++)
            l2[i] = ((gb << 30) + ((uint64_t) i << 21)) | 0x83; /* P|RW|PS */
        l3[gb] = (uint64_t) (uintptr_t) l2 | 0x03;
    }
    __asm__ __volatile__("mov %%cr3, %%rax\n\t mov %%rax, %%cr3" : : : "rax", "memory");
    return 1;
}

int efifb_init(void)
{
    volatile zxv_x86_bootinfo_t *bi = (volatile zxv_x86_bootinfo_t *) ZXV_X86_BOOTINFO_ADDR;
    if (bi->magic != ZXV_BOOTINFO_MAGIC) return 0;
    uint64_t want = bi->magic ^ bi->fb ^ bi->width ^ bi->height ^ bi->stride;
    if (bi->check != want || !bi->fb || bi->width < 320 || bi->height < 200 ||
        bi->stride < bi->width || bi->stride > 16384 || bi->height > 16384)
        return 0;
    uint64_t bytes = (uint64_t) bi->stride * bi->height * 4;
    if (!map_fb(bi->fb, bytes)) return 0;
    /* vbe_state rows are `width` pixels wide; use the scanline stride as the
     * width so each row lands where the firmware put it. */
    vbe_init_fb(&g_fb, (uint16_t) bi->stride, (uint16_t) bi->height, 32, (uintptr_t) bi->fb);
    g_cols = (int) (bi->width / 8);
    g_rows = (int) (bi->height / 16);
    vbe_fill_rect(&g_fb, 0, 0, (int32_t) bi->width, (int32_t) bi->height, BG);
    g_col = g_row = 0;
    g_on = 1;
    return 1;
}

int efifb_active(void)
{
    return g_on;
}

/* Wrap to the top instead of scrolling: reading back a framebuffer is very
 * slow on real hardware, so the line ahead of the cursor is cleared instead. */
static void newline(void)
{
    g_col = 0;
    if (++g_row >= g_rows) g_row = 0;
    vbe_fill_rect(&g_fb, 0, g_row * 16, g_cols * 8, 16, BG);
}

void efifb_putc(char c)
{
    if (!g_on) return;
    uint8_t u = (uint8_t) c;
    if (u == '\n') {
        newline();
        return;
    }
    if (u == '\r') {
        g_col = 0;
        return;
    }
    if (u >= 0x80 && u < 0xC0) return; /* UTF-8 continuation byte */
    if (u >= 0xC0)
        c = '-'; /* one cell per non-ASCII char */
    else if (u < 0x20)
        return;
    /* font8x16's lowercase glyphs are unreliable; fold a..z to A..Z, as
     * vbe_draw_text_ex does. */
    if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
    if (g_col >= g_cols) newline();
    vbe_draw_char(&g_fb, g_col * 8, g_row * 16, c, FG, BG);
    g_col++;
}
