/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* amlfake.c — fake hardware for the AML interpreter host tests (amlfake.h). */
#include "amlfake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static amlfake_t *g_fk; /* the log hook carries no context */

static void fk_logline(const char *s)
{
    if (g_fk && g_fk->nlog < 512) {
        snprintf(g_fk->log[g_fk->nlog], 160, "%s", s);
        g_fk->nlog++;
    }
    if (getenv("AML_VERBOSE")) printf("    | %s\n", s);
}

static volatile void *fk_map(void *ctx, uint64_t phys, uint64_t size)
{
    amlfake_t *f = (amlfake_t *) ctx;
    f->map_calls++;
    if (f->map_fail) return 0;
    for (uint32_t i = 0; i < f->nwin; i++) {
        fk_win_t *w = &f->win[i];
        if (phys >= w->phys && phys - w->phys <= w->len && size <= w->len - (phys - w->phys))
            return w->buf + (phys - w->phys);
    }
    return 0;
}

static void logio(amlfake_t *f, uint32_t port, uint32_t width, uint32_t v, int write)
{
    if (f->niolog < 1024) {
        f->iolog[f->niolog].port = port;
        f->iolog[f->niolog].width = width;
        f->iolog[f->niolog].val = v;
        f->iolog[f->niolog].write = (uint8_t) write;
        f->niolog++;
    } else {
        f->io_dropped++;
    }
}

static uint32_t raw_rd(amlfake_t *f, uint32_t port, uint32_t width)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < width / 8; i++) v |= (uint32_t) f->io[(port + i) & 0xFFFF] << (8 * i);
    return v;
}

static void raw_wr(amlfake_t *f, uint32_t port, uint32_t width, uint32_t v)
{
    for (uint32_t i = 0; i < width / 8; i++) f->io[(port + i) & 0xFFFF] = (uint8_t) (v >> (8 * i));
}

static int is_w1c(uint32_t port)
{
    return (port >= FK_PM1A_EVT && port < FK_PM1A_EVT + 2) ||
           (port >= FK_PM1B_EVT && port < FK_PM1B_EVT + 2) || (port >= FK_GPE0 && port < FK_GPE0 + 4) || port == 0xB031;
}

static uint32_t fk_ior(void *ctx, uint32_t port, uint32_t width)
{
    amlfake_t *f = (amlfake_t *) ctx;
    uint32_t v;
    if (port == FK_EC_CMD) {
        v = f->ec_dead ? 0x02 : (uint32_t) (f->ec_obf ? 1 : 0) | (f->ec_query ? 0x20 : 0);
    } else if (port == FK_EC_DATA) {
        v = f->ec_out;
        f->ec_obf = 0;
    } else if (port == 0x71 || port == 0x73) {
        v = f->cmos[f->cmos_idx[port == 0x73] | (port == 0x73 ? 0x80 : 0)];
    } else if (port == 0x501) {
        v = f->idx_file[f->idx_sel];
    } else if (port == 0x610) {
        v = f->bank_reg[f->bank_sel & 3][0];
    } else {
        v = raw_rd(f, port, width);
    }
    logio(f, port, width, v, 0);
    return v;
}

static void ec_cmd(amlfake_t *f, uint8_t v)
{
    if (f->ec_dead) return;
    f->ec_state = v == 0x80 ? 1 : v == 0x81 ? 2 : 0;
    if (v == 0x84) {
        f->ec_out = f->ec_query;
        f->ec_query = 0;
        f->ec_obf = 1;
    }
}

static void ec_data(amlfake_t *f, uint8_t v)
{
    if (f->ec_dead) return;
    if (f->ec_state == 1) {
        f->ec_out = f->ec_ram[v];
        f->ec_obf = 1;
        f->ec_reads++;
        f->ec_state = 0;
    } else if (f->ec_state == 2) {
        f->ec_addr = v;
        f->ec_state = 3;
    } else if (f->ec_state == 3) {
        f->ec_ram[f->ec_addr] = v;
        f->ec_writes++;
        f->ec_state = 0;
    }
}

static void fk_iow(void *ctx, uint32_t port, uint32_t width, uint32_t v)
{
    amlfake_t *f = (amlfake_t *) ctx;
    logio(f, port, width, v, 1);
    if (port == FK_EC_CMD) {
        ec_cmd(f, (uint8_t) v);
    } else if (port == FK_EC_DATA) {
        ec_data(f, (uint8_t) v);
    } else if (port == 0x70 || port == 0x72) {
        f->cmos_idx[port == 0x72] = (uint8_t) (v & 0x7F);
    } else if (port == 0x71 || port == 0x73) {
        f->cmos[f->cmos_idx[port == 0x73] | (port == 0x73 ? 0x80 : 0)] = (uint8_t) v;
    } else if (port == 0x500) {
        f->idx_sel = (uint8_t) v;
    } else if (port == 0x501) {
        f->idx_file[f->idx_sel] = (uint8_t) v;
    } else if (port == 0x600) {
        f->bank_sel = (uint8_t) v;
    } else if (port == 0x610) {
        f->bank_reg[f->bank_sel & 3][0] = (uint8_t) v;
    } else if (is_w1c(port)) {
        uint32_t cur = raw_rd(f, port, width);
        raw_wr(f, port, width, cur & ~v);
    } else if (port == FK_SMI_CMD) {
        raw_wr(f, port, width, v);
        if (f->sci_en_after_smi) f->io[FK_PM1A_CNT] |= 1;
    } else if (port == FK_PM1A_CNT || port == FK_PM1B_CNT) {
        raw_wr(f, port, width, v & ~0x2000u);
        if (v & 0x2000u) {
            f->sleep_writes++;
            if (port == FK_PM1A_CNT) f->last_slp_typ_a = (v >> 10) & 7;
            else f->last_slp_typ_b = (v >> 10) & 7;
            if (f->wake_after_sleep) f->io[FK_PM1A_EVT + 1] |= 0x80; /* WAK_STS */
        }
    } else {
        raw_wr(f, port, width, v);
    }
}

static fk_pci_t *find_pci(amlfake_t *f, uint32_t seg, uint32_t bus, uint32_t dev, uint32_t fn)
{
    for (uint32_t i = 0; i < f->npci; i++)
        if (f->pci[i].seg == seg && f->pci[i].bus == bus && f->pci[i].dev == dev &&
            f->pci[i].fn == fn)
            return &f->pci[i];
    return 0;
}

static uint32_t fk_pcir(void *ctx, uint32_t seg, uint32_t bus, uint32_t dev, uint32_t fn,
                        uint32_t off, uint32_t width)
{
    fk_pci_t *p = find_pci((amlfake_t *) ctx, seg, bus, dev, fn);
    uint32_t v = 0;
    if (!p) return width == 32 ? 0xFFFFFFFFu : (1u << width) - 1;
    for (uint32_t i = 0; i < width / 8; i++) v |= (uint32_t) p->cfg[(off + i) & 0xFF] << (8 * i);
    return v;
}

static void fk_pciw(void *ctx, uint32_t seg, uint32_t bus, uint32_t dev, uint32_t fn, uint32_t off,
                    uint32_t width, uint32_t v)
{
    fk_pci_t *p = find_pci((amlfake_t *) ctx, seg, bus, dev, fn);
    if (!p) return;
    for (uint32_t i = 0; i < width / 8; i++) p->cfg[(off + i) & 0xFF] = (uint8_t) (v >> (8 * i));
}

static void fk_delay(void *ctx, uint32_t us)
{
    amlfake_t *f = (amlfake_t *) ctx;
    f->now_us += us;
    f->delays++;
}

static uint64_t fk_timer(void *ctx)
{
    return ((amlfake_t *) ctx)->now_us * 10;
}

static int fk_gl(void *ctx, int acquire)
{
    amlfake_t *f = (amlfake_t *) ctx;
    if (acquire) {
        if (f->gl_fail) return 0;
        f->gl_acq++;
        return 1;
    }
    f->gl_rel++;
    return 1;
}

void fk_init(amlfake_t *f)
{
    memset(f, 0, sizeof *f);
    g_fk = f;
    f->plat.ctx = f;
    f->plat.mmio_map = fk_map;
    f->plat.io_read = fk_ior;
    f->plat.io_write = fk_iow;
    f->plat.pci_read = fk_pcir;
    f->plat.pci_write = fk_pciw;
    f->plat.delay_us = fk_delay;
    f->plat.timer_100ns = fk_timer;
    f->plat.global_lock = fk_gl;
    f->plat.log = fk_logline;
}

void fk_window(amlfake_t *f, uint64_t phys, void *buf, uint64_t len)
{
    if (f->nwin < 6) {
        f->win[f->nwin].phys = phys;
        f->win[f->nwin].len = len;
        f->win[f->nwin].buf = (uint8_t *) buf;
        f->nwin++;
    }
}

fk_pci_t *fk_pci_add(amlfake_t *f, uint32_t seg, uint32_t bus, uint32_t dev, uint32_t fn)
{
    if (f->npci >= 8) return 0;
    fk_pci_t *p = &f->pci[f->npci++];
    memset(p, 0, sizeof *p);
    p->seg = seg;
    p->bus = bus;
    p->dev = dev;
    p->fn = fn;
    return p;
}

int fk_log_has(const amlfake_t *f, const char *s)
{
    return fk_log_count(f, s) > 0;
}

uint32_t fk_log_count(const amlfake_t *f, const char *s)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < f->nlog; i++)
        if (strstr(f->log[i], s)) n++;
    return n;
}

void fk_log_clear(amlfake_t *f)
{
    f->nlog = 0;
}

uint32_t fk_io_count(const amlfake_t *f, uint32_t port, uint32_t width, int write)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < f->niolog; i++)
        if (f->iolog[i].port == port && f->iolog[i].width == width && f->iolog[i].write == write)
            n++;
    return n;
}

uint32_t fk_table(uint8_t *out, uint32_t max, const char sig[4], uint8_t rev, const uint8_t *body,
                  uint32_t blen)
{
    uint32_t len = 36 + blen;
    if (len > max) return 0;
    memset(out, 0, 36);
    memcpy(out, sig, 4);
    out[4] = (uint8_t) len;
    out[5] = (uint8_t) (len >> 8);
    out[6] = (uint8_t) (len >> 16);
    out[7] = (uint8_t) (len >> 24);
    out[8] = rev;
    memcpy(out + 10, "ZXVFK ", 6);
    memcpy(out + 16, "AMLTEST ", 8);
    memcpy(out + 36, body, blen);
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum = (uint8_t) (sum + out[i]);
    out[9] = (uint8_t) (0u - sum);
    return len;
}
