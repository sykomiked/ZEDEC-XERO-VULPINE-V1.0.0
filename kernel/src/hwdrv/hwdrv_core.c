/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwdrv_core.c — helpers, PCI scan, registry and driver matching shared by
 * every standard class driver. See hwdrv.h. */
#include "hwdrv.h"

/* ---- MODBIND DECLARATION — L4 devices -------------------------------------
 * Comment, not code, pending the ZXV_PROVIDES mechanism (see pci.c).
 *
 *   ZXV_PROVIDES(hw_class_drivers_ready)
 *   ZXV_REQUIRES()                 -- nothing: config space comes in through
 *                                     hw_plat_t, not through pci.c
 *   ZXV_BRINGUP(hw_probe_all)
 */

void hw_memset(void *dst, uint8_t v, uint32_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) dst;
    for (uint32_t i = 0; i < n; i++) d[i] = v;
}

void hw_memcpy(void *dst, const void *src, uint32_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) dst;
    const volatile uint8_t *s = (const volatile uint8_t *) src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

int hw_memeq(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    for (uint32_t i = 0; i < n; i++)
        if (x[i] != y[i]) return 0;
    return 1;
}

int hw_wait32(const hw_plat_t *p, volatile void *base, uint32_t off, uint32_t mask, uint32_t want,
              uint32_t timeout_us)
{
    uint32_t waited = 0;
    for (;;) {
        if ((hw_rd32(base, off) & mask) == want) return 1;
        if (waited >= timeout_us) return 0;
        p->delay_us(p->ctx, 10);
        waited += 10;
    }
}

/* ---- line builder ------------------------------------------------------- */
void hw_line_init(hw_line_t *l)
{
    l->n = 0;
    l->b[0] = 0;
}

void hw_line_c(hw_line_t *l, char c)
{
    if (l->n + 1 >= sizeof l->b) return;
    l->b[l->n++] = c;
    l->b[l->n] = 0;
}

void hw_line_s(hw_line_t *l, const char *s)
{
    while (s && *s) hw_line_c(l, *s++);
}

void hw_line_u(hw_line_t *l, uint64_t v)
{
    char t[24];
    int i = 0;
    do {
        t[i++] = (char) ('0' + v % 10);
        v /= 10;
    } while (v);
    while (i) hw_line_c(l, t[--i]);
}

void hw_line_x(hw_line_t *l, uint64_t v, uint32_t digits)
{
    static const char hex[] = "0123456789abcdef";
    if (digits == 0 || digits > 16) digits = 16;
    for (uint32_t i = digits; i > 0; i--) hw_line_c(l, hex[(v >> ((i - 1) * 4)) & 0xF]);
}

void hw_line_bdf(hw_line_t *l, uint32_t bdf)
{
    hw_line_x(l, (bdf >> 8) & 0xFF, 2);
    hw_line_c(l, ':');
    hw_line_x(l, (bdf >> 3) & 0x1F, 2);
    hw_line_c(l, '.');
    hw_line_x(l, bdf & 7, 1);
}

/* ---- PCI ------------------------------------------------------------------ */
#define CFG_ID       0x00
#define CFG_CMD      0x04
#define CFG_CLASS    0x08
#define CFG_HDR      0x0C
#define CFG_BAR0     0x10
#define CFG_CAP      0x34
#define CFG_IRQ      0x3C
#define CMD_IO       0x0001
#define CMD_MEM      0x0002
#define CMD_MASTER   0x0004
#define CMD_INTX_OFF 0x0400
#define STATUS_CAPS  0x0010

static void scan_bars(const hw_plat_t *p, hw_pci_dev_t *d)
{
    /* Bridges (header type 1) have two BARs, endpoints six. */
    uint32_t nbar = (d->header_type & 0x7F) == 1 ? 2 : ((d->header_type & 0x7F) == 0 ? 6 : 0);
    uint32_t cmd = p->cfg_read(p->ctx, d->bdf, CFG_CMD);
    /* Sizing writes all-ones into live BARs: switch decode off meanwhile so a
     * half-written BAR never claims a bus range. */
    p->cfg_write(p->ctx, d->bdf, CFG_CMD, cmd & ~(uint32_t) (CMD_IO | CMD_MEM));
    for (uint32_t i = 0; i < nbar; i++) {
        uint32_t off = CFG_BAR0 + i * 4;
        uint32_t lo = p->cfg_read(p->ctx, d->bdf, off);
        p->cfg_write(p->ctx, d->bdf, off, 0xFFFFFFFFu);
        uint32_t szlo = p->cfg_read(p->ctx, d->bdf, off);
        p->cfg_write(p->ctx, d->bdf, off, lo);
        if (lo & 1) { /* IO BAR */
            d->bar_flags[i] = HW_BAR_IO;
            d->bar[i] = lo & ~3u;
            uint32_t m = szlo & ~3u & 0xFFFFu;
            d->bar_size[i] = m ? ((~m + 1) & 0xFFFFu) : 0;
            continue;
        }
        uint64_t base = lo & ~0xFu, mask = szlo & ~0xFu;
        d->bar_flags[i] = (lo & 8) ? HW_BAR_PREFETCH : 0;
        if (((lo >> 1) & 3) == 2 && i + 1 < nbar) { /* 64-bit: next BAR is the high half */
            uint32_t hi = p->cfg_read(p->ctx, d->bdf, off + 4);
            p->cfg_write(p->ctx, d->bdf, off + 4, 0xFFFFFFFFu);
            uint32_t szhi = p->cfg_read(p->ctx, d->bdf, off + 4);
            p->cfg_write(p->ctx, d->bdf, off + 4, hi);
            base |= (uint64_t) hi << 32;
            mask |= (uint64_t) szhi << 32;
            d->bar_flags[i] |= HW_BAR_64;
            d->bar[i] = base;
            d->bar_size[i] = mask ? (~mask + 1) : 0;
            i++; /* the high half is not a BAR of its own */
            d->bar[i] = 0;
            d->bar_size[i] = 0;
            d->bar_flags[i] = 0;
            continue;
        }
        d->bar[i] = base;
        d->bar_size[i] = mask ? ((~mask + 1) & 0xFFFFFFFFu) : 0;
    }
    p->cfg_write(p->ctx, d->bdf, CFG_CMD, cmd);
}

uint32_t hw_pci_scan(const hw_plat_t *p, hw_pci_dev_t *out, uint32_t max)
{
    uint32_t n = 0;
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t dev = 0; dev < 32; dev++) {
            for (uint32_t fn = 0; fn < 8; fn++) {
                uint32_t bdf = HW_BDF(bus, dev, fn);
                uint32_t id = p->cfg_read(p->ctx, bdf, CFG_ID);
                if ((id & 0xFFFF) == 0xFFFF || (id & 0xFFFF) == 0) {
                    if (fn == 0) break; /* no function 0: no device */
                    continue;
                }
                uint32_t hdr = (p->cfg_read(p->ctx, bdf, CFG_HDR) >> 16) & 0xFF;
                if (n < max) {
                    hw_pci_dev_t *d = &out[n];
                    hw_memset(d, 0, sizeof *d);
                    d->bdf = bdf;
                    d->vendor = (uint16_t) id;
                    d->device = (uint16_t) (id >> 16);
                    uint32_t cls = p->cfg_read(p->ctx, bdf, CFG_CLASS);
                    d->rev = (uint8_t) cls;
                    d->progif = (uint8_t) (cls >> 8);
                    d->subclass = (uint8_t) (cls >> 16);
                    d->class_code = (uint8_t) (cls >> 24);
                    d->header_type = (uint8_t) hdr;
                    d->irq_line = (uint8_t) p->cfg_read(p->ctx, bdf, CFG_IRQ);
                    scan_bars(p, d);
                    n++;
                }
                if (fn == 0 && !(hdr & 0x80)) break; /* single-function device */
            }
        }
    }
    return n;
}

void hw_pci_enable(const hw_plat_t *p, const hw_pci_dev_t *d)
{
    uint32_t cmd = p->cfg_read(p->ctx, d->bdf, CFG_CMD) & 0xFFFF;
    cmd = (cmd | CMD_MEM | CMD_MASTER | CMD_INTX_OFF) & ~(uint32_t) CMD_IO;
    p->cfg_write(p->ctx, d->bdf, CFG_CMD, cmd);
}

uint32_t hw_pci_find_cap(const hw_plat_t *p, const hw_pci_dev_t *d, uint8_t id)
{
    uint32_t status = p->cfg_read(p->ctx, d->bdf, CFG_CMD) >> 16;
    if (!(status & STATUS_CAPS)) return 0;
    uint32_t off = p->cfg_read(p->ctx, d->bdf, CFG_CAP) & 0xFC;
    for (uint32_t guard = 0; off >= 0x40 && guard < 48; guard++) {
        uint32_t w = p->cfg_read(p->ctx, d->bdf, off);
        if ((w & 0xFF) == id) return off;
        off = (w >> 8) & 0xFC;
    }
    return 0;
}

volatile void *hw_pci_map_bar(const hw_plat_t *p, const hw_pci_dev_t *d, uint32_t i)
{
    if (i >= 6 || !d->bar[i] || !d->bar_size[i] || (d->bar_flags[i] & HW_BAR_IO)) return 0;
    return p->mmio_map(p->ctx, d->bar[i], d->bar_size[i]);
}

/* ---- registry ------------------------------------------------------------- */
int hw_reg_blk(hw_registry_t *r, hw_blk_t *b)
{
    if (r->n_blk >= HW_MAX_BLK) return -1;
    r->blk[r->n_blk++] = b;
    return 0;
}

int hw_reg_nic(hw_registry_t *r, hw_nic_t *n)
{
    if (r->n_nic >= HW_MAX_NIC) return -1;
    r->nic[r->n_nic++] = n;
    return 0;
}

int hw_reg_input(hw_registry_t *r, hw_input_t *in)
{
    if (r->n_input >= HW_MAX_INPUT) return -1;
    r->input[r->n_input++] = in;
    return 0;
}

int hw_reg_audio(hw_registry_t *r, hw_audio_t *a)
{
    if (r->n_audio >= HW_MAX_AUDIO) return -1;
    r->audio[r->n_audio++] = a;
    return 0;
}

/* ---- matching ------------------------------------------------------------- */
const hw_driver_t *hw_match(const hw_driver_t *tab, uint32_t n, const hw_pci_dev_t *d)
{
    for (uint32_t i = 0; i < n; i++) {
        if (!tab[i].ids) continue;
        for (const hw_id_t *id = tab[i].ids; id->vendor; id++)
            if (id->vendor == d->vendor && id->device == d->device) return &tab[i];
    }
    for (uint32_t i = 0; i < n; i++) {
        const hw_driver_t *t = &tab[i];
        if (t->ids) continue;
        if (t->class_code != d->class_code) continue;
        if (t->subclass != 0xFF && t->subclass != d->subclass) continue;
        if (t->progif != 0xFF && t->progif != d->progif) continue;
        return t;
    }
    return 0;
}
