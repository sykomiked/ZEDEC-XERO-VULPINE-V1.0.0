/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwfake.c — host platform and fake PCI config space (see hwfake.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hwfake.h"

int fk_checks, fk_failures;

static void *g_allocs[4096];
static uint32_t g_nallocs;

static void *track(void *p)
{
    if (p && g_nallocs < sizeof g_allocs / sizeof g_allocs[0]) g_allocs[g_nallocs++] = p;
    return p;
}

static fake_dev_t *find(fake_plat_t *fp, uint32_t bdf)
{
    for (uint32_t i = 0; i < fp->n; i++)
        if (fp->devs[i]->bdf == bdf) return fp->devs[i];
    return 0;
}

/* Which BAR (and which half) a config offset addresses: returns the BAR index
 * owning it, *hi = 1 for the upper dword of a 64-bit BAR. -1 if none. */
static int bar_at(const fake_dev_t *d, uint32_t off, int *hi)
{
    if (off < 0x10 || off >= 0x28) return -1;
    uint32_t i = (off - 0x10) / 4;
    if (d->bar_size[i]) {
        *hi = 0;
        return (int) i;
    }
    if (i > 0 && d->bar_size[i - 1] && d->bar_kind[i - 1] == FAKE_BAR_MEM64) {
        *hi = 1;
        return (int) i - 1;
    }
    return -1;
}

static uint32_t cfg_read(void *ctx, uint32_t bdf, uint32_t off)
{
    fake_dev_t *d = find((fake_plat_t *) ctx, bdf);
    if (!d || off > 0xFC) return 0xFFFFFFFFu;
    return d->cfg[off / 4];
}

static void cfg_write(void *ctx, uint32_t bdf, uint32_t off, uint32_t val)
{
    fake_dev_t *d = find((fake_plat_t *) ctx, bdf);
    if (!d || off > 0xFC) return;
    int hi, b = bar_at(d, off, &hi);
    if (b >= 0) {
        uint64_t mask = ~(d->bar_size[b] - 1);
        if (hi)
            d->cfg[off / 4] = val & (uint32_t) (mask >> 32);
        else if (d->bar_kind[b] == FAKE_BAR_IO)
            d->cfg[off / 4] = (val & (uint32_t) mask & ~3u) | 1;
        else if (d->bar_kind[b] == FAKE_BAR_MEM32)
            d->cfg[off / 4] = val & (uint32_t) mask & ~0xFu;
        else
            d->cfg[off / 4] = (val & (uint32_t) mask & ~0xFu) | 0x4; /* 64-bit memory */
        return;
    }
    if (off >= 0x10 && off < 0x28) return; /* unimplemented BAR: hardwired to 0 */
    if (off == 0x04) {                     /* command: low half writable, status read-only */
        if (d->cmd_locked) return;
        d->cfg[1] = (d->cfg[1] & 0xFFFF0000u) | (val & 0xFFFF);
        return;
    }
    if (off == 0x00 || off == 0x08) return; /* ids and class are read-only */
    d->cfg[off / 4] = val;
}

static volatile void *mmio_map(void *ctx, uint64_t phys, uint64_t size)
{
    (void) ctx;
    (void) size;
    return (volatile void *) (uintptr_t) phys;
}

static void *dma_alloc(void *ctx, uint32_t size, uint32_t align, uint64_t *phys)
{
    fake_plat_t *fp = (fake_plat_t *) ctx;
    fp->dma_calls++;
    if (fp->dma_fail_after && fp->dma_calls >= fp->dma_fail_after) return 0;
    if (fp->dma_fail_one && fp->dma_calls == fp->dma_fail_one) return 0;
    if (align < 64) align = 64;
    size_t sz = ((size_t) size + align - 1) / align * align;
    void *p = track(aligned_alloc(align, sz ? sz : align));
    if (!p) return 0;
    memset(p, 0, sz ? sz : align);
    fp->dma_bytes += size;
    if (phys) *phys = (uint64_t) (uintptr_t) p;
    return p;
}

static fake_plat_t *g_fp; /* the log hook gets no ctx */

static void delay_us(void *ctx, uint32_t us)
{
    fake_plat_t *fp = (fake_plat_t *) ctx;
    fp->now_us += us;
    for (uint32_t i = 0; i < fp->n; i++)
        if (fp->devs[i]->tick) fp->devs[i]->tick(fp->devs[i], fp->now_us);
}

static void log_line(const char *s)
{
    if (getenv("HWFAKE_VERBOSE")) printf("  | %s\n", s);
    if (!g_fp || g_fp->nlog >= 64) return;
    snprintf(g_fp->log[g_fp->nlog++], 160, "%s", s);
}

void fake_plat_init(fake_plat_t *fp)
{
    memset(fp, 0, sizeof *fp);
    fp->plat.ctx = fp;
    fp->plat.cfg_read = cfg_read;
    fp->plat.cfg_write = cfg_write;
    fp->plat.mmio_map = mmio_map;
    fp->plat.dma_alloc = dma_alloc;
    fp->plat.delay_us = delay_us;
    fp->plat.log = log_line;
    g_fp = fp;
}

fake_dev_t *fake_dev_new(fake_plat_t *fp, uint32_t bdf, uint16_t vendor, uint16_t device,
                         uint8_t cls, uint8_t sub, uint8_t progif)
{
    fake_dev_t *d = (fake_dev_t *) track(calloc(1, sizeof *d));
    d->bdf = bdf;
    d->cfg[0] = ((uint32_t) device << 16) | vendor;
    d->cfg[1] = 0x00100000u; /* status: capabilities list present */
    d->cfg[2] = ((uint32_t) cls << 24) | ((uint32_t) sub << 16) | ((uint32_t) progif << 8) | 1;
    d->cfg[0x34 / 4] = 0; /* no capabilities unless a test adds them */
    fp->devs[fp->n++] = d;
    return d;
}

uint8_t *fake_dev_bar(fake_dev_t *d, uint32_t i, uint64_t size)
{
    /* Aligned to its own size, as a real BAR is, so the sizing probe's
     * restore write keeps the address intact. */
    size_t sz = size < 4096 ? 4096 : (size_t) size;
    uint8_t *m = (uint8_t *) track(aligned_alloc(sz, sz));
    memset(m, 0, sz);
    d->bar_mem[i] = m;
    d->bar_size[i] = size;
    d->bar_kind[i] = FAKE_BAR_MEM64;
    uint64_t a = (uint64_t) (uintptr_t) m;
    d->cfg[4 + i] = ((uint32_t) a & ~0xFu) | 0x4;
    d->cfg[5 + i] = (uint32_t) (a >> 32);
    return m;
}

void fake_dev_bar_at(fake_dev_t *d, uint32_t i, uint64_t addr, uint64_t size, uint8_t kind)
{
    d->bar_mem[i] = 0;
    d->bar_size[i] = size;
    d->bar_kind[i] = kind;
    if (kind == FAKE_BAR_IO) {
        d->cfg[4 + i] = ((uint32_t) addr & ~3u) | 1;
    } else if (kind == FAKE_BAR_MEM32) {
        d->cfg[4 + i] = (uint32_t) addr & ~0xFu;
    } else {
        d->cfg[4 + i] = ((uint32_t) addr & ~0xFu) | 0x4;
        d->cfg[5 + i] = (uint32_t) (addr >> 32);
    }
}

void fake_plat_free(fake_plat_t *fp)
{
    (void) fp;
    for (uint32_t i = 0; i < g_nallocs; i++) free(g_allocs[i]);
    g_nallocs = 0;
    g_fp = 0;
}

int fake_log_has(const fake_plat_t *fp, const char *s)
{
    for (uint32_t i = 0; i < fp->nlog; i++)
        if (strstr(fp->log[i], s)) return 1;
    return 0;
}
