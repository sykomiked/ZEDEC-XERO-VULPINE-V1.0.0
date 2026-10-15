/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fvirtio.h — a register-level fake virtio 1.x PCI device, the transport half
 * shared by the virtio driver tests (test_virtio.c).
 *
 * BAR 4 (16 KB) holds the four structures the vendor capabilities point at:
 * common configuration at 0x0000, ISR at 0x1000, device configuration at
 * 0x2000, notifications at 0x3000 (multiplier 8, queue i's offset i). The
 * fake reacts only in tick(), as every fake here does:
 *   - feature words follow DFSELECT; GF is stored into the word GFSELECT names;
 *   - QSELECT loads that queue's registers; with QSELECT unchanged, the
 *     registers the driver wrote are taken into the queue;
 *   - STATUS 0 is a reset; FEATURES_OK is cleared again when the driver took
 *     a feature the device did not offer or left VERSION_1 out;
 *   - a notification is a write over the area's 0xFFFF fill; the queue it
 *     names is then served: each available chain is walked, checked (no
 *     device-readable buffer after a device-writable one, every address
 *     non-zero, the chain no longer than the queue) and handed to the
 *     device's serve(), which returns the bytes it wrote, or -1 to keep the
 *     chain for later (a receive buffer with no frame to put in it yet).
 * Any rule the driver breaks counts in `strict`. */
#ifndef ZXV_FVIRTIO_H
#define ZXV_FVIRTIO_H

#include <string.h>
#include "hwfake.h"

#define FV_COMMON 0x0000u
#define FV_ISR    0x1000u
#define FV_DEVCFG 0x2000u
#define FV_NOTIFY 0x3000u
#define FV_BAR    0x4000u
#define FV_QN     4u
#define FV_MULT   8u

typedef struct fv_desc {
    uint8_t *p;
    uint32_t len;
    uint8_t w; /* device-writable */
} fv_desc_t;

typedef struct fv_queue {
    uint16_t max, size, enable, notify_off;
    uint64_t desc, avail, used;
    uint16_t last_avail;
    uint32_t kicks, served;
} fv_queue_t;

struct fvirtio;
typedef int32_t (*fv_serve_t)(struct fvirtio *f, uint32_t q, fv_desc_t *d, uint32_t n);

typedef struct fvirtio {
    uint8_t *r;
    fake_dev_t *dev;
    uint64_t offered, taken;
    fv_queue_t q[FV_QN];
    uint16_t sel;
    uint8_t status;
    fv_serve_t serve;
    void *dev_state;
    uint32_t strict, resets;
    /* fault knobs */
    int no_v1;           /* VERSION_1 not offered */
    int refuse;          /* FEATURES_OK never sticks */
    int stuck_reset;     /* STATUS never reads back 0 after a reset */
    int deaf;            /* notifications are ignored */
    uint32_t slow_reset; /* STATUS reads back 0 only this many ticks after a reset */
    uint32_t reset_left;
} fvirtio_t;

static inline uint8_t *fv_c(fvirtio_t *f)
{
    return f->r + FV_COMMON;
}

static inline void fv_load_queue(fvirtio_t *f)
{
    uint8_t *c = fv_c(f);
    uint16_t s = f->sel;
    if (s >= FV_QN) {
        memset(c + 0x18, 0, 0x38 - 0x18);
        return;
    }
    fv_queue_t *q = &f->q[s];
    c[0x18] = (uint8_t) q->size, c[0x19] = (uint8_t) (q->size >> 8);
    c[0x1A] = 0xFF, c[0x1B] = 0xFF;
    c[0x1C] = (uint8_t) q->enable, c[0x1D] = 0;
    c[0x1E] = (uint8_t) q->notify_off, c[0x1F] = 0;
    fk_wr64(c + 0x20, q->desc);
    fk_wr64(c + 0x28, q->avail);
    fk_wr64(c + 0x30, q->used);
}

static inline void fv_reset(fvirtio_t *f)
{
    f->resets++;
    f->taken = 0;
    for (uint32_t i = 0; i < FV_QN; i++) {
        fv_queue_t *q = &f->q[i];
        q->size = q->max;
        q->enable = 0;
        q->desc = q->avail = q->used = 0;
        q->last_avail = 0;
    }
    f->sel = 0;
    fv_c(f)[0x16] = 0;
    fv_c(f)[0x17] = 0;
    fv_load_queue(f);
    f->status = 0;
}

/* Serve queue qi: every chain the driver has made available. */
static inline void fv_serve_queue(fvirtio_t *f, uint32_t qi)
{
    fv_queue_t *q = &f->q[qi];
    if (!q->enable || !q->size || !q->desc || !q->avail || !q->used) {
        f->strict++;
        return;
    }
    if (!(f->status & 4)) f->strict++; /* notified before DRIVER_OK */
    uint8_t *av = (uint8_t *) fk_ptr(q->avail), *us = (uint8_t *) fk_ptr(q->used);
    uint8_t *dt = (uint8_t *) fk_ptr(q->desc);
    for (uint32_t guard = 0; guard < 4096; guard++) {
        uint16_t aidx = (uint16_t) (av[2] | (av[3] << 8));
        if (q->last_avail == aidx) return;
        uint32_t slot = (uint32_t) (q->last_avail % q->size);
        uint16_t head = (uint16_t) (av[4 + 2 * slot] | (av[5 + 2 * slot] << 8));
        fv_desc_t d[FV_QN * 16];
        uint32_t n = 0;
        uint16_t i = head;
        int seen_w = 0;
        for (;;) {
            if (i >= q->size || n >= q->size || n >= sizeof d / sizeof d[0]) {
                f->strict++;
                return;
            }
            uint8_t *e = dt + 16u * i;
            uint64_t addr = fk_rd64(e);
            uint32_t len = fk_rd32(e + 8);
            uint16_t fl = (uint16_t) (e[12] | (e[13] << 8));
            uint16_t nx = (uint16_t) (e[14] | (e[15] << 8));
            if (!addr || !len) f->strict++;
            if (fl & 2)
                seen_w = 1;
            else if (seen_w)
                f->strict++; /* readable after writable */
            d[n].p = (uint8_t *) fk_ptr(addr);
            d[n].len = len;
            d[n].w = (uint8_t) ((fl >> 1) & 1);
            n++;
            if (!(fl & 1)) break;
            i = nx;
        }
        int32_t wrote = f->serve(f, qi, d, n);
        if (wrote < 0) return; /* kept for later */
        q->last_avail++;
        q->served++;
        uint16_t uidx = (uint16_t) (us[2] | (us[3] << 8));
        uint8_t *ue = us + 4 + 8u * (uidx % q->size);
        fk_wr32(ue, head);
        fk_wr32(ue + 4, (uint32_t) wrote);
        uidx++;
        us[2] = (uint8_t) uidx;
        us[3] = (uint8_t) (uidx >> 8);
    }
}

static inline void fv_tick(fvirtio_t *f)
{
    uint8_t *c = fv_c(f);
    /* features */
    uint32_t dsel = fk_rd32(c + 0x00);
    fk_wr32(c + 0x04, dsel < 2 ? (uint32_t) (f->offered >> (32 * dsel)) : 0);
    uint32_t gsel = fk_rd32(c + 0x08);
    if (gsel < 2) {
        f->taken &= ~(0xFFFFFFFFull << (32 * gsel));
        f->taken |= (uint64_t) fk_rd32(c + 0x0C) << (32 * gsel);
    }
    /* status */
    uint8_t st = c[0x14];
    if (st == 0 && f->status != 0) {
        fv_reset(f);
        if (f->stuck_reset || f->slow_reset) c[0x14] = 1;
        f->reset_left = f->slow_reset;
        return;
    }
    if (f->status == 0 && st == 1 && (f->stuck_reset || f->reset_left)) {
        if (f->reset_left && --f->reset_left == 0) c[0x14] = 0;
        return;
    }
    if ((st & 8) && !(f->status & 8)) {
        int bad = (f->taken & ~f->offered) || !((f->taken >> 32) & 1) || f->refuse;
        if (bad) {
            st = (uint8_t) (st & ~8u);
            c[0x14] = st;
        }
    }
    f->status = st;
    /* queues */
    uint16_t sel = (uint16_t) (c[0x16] | (c[0x17] << 8));
    if (sel != f->sel) {
        f->sel = sel;
        fv_load_queue(f);
    } else if (sel < FV_QN) {
        fv_queue_t *q = &f->q[sel];
        uint16_t size = (uint16_t) (c[0x18] | (c[0x19] << 8));
        if (q->max && size != q->size) {
            if (size == 0 || size > q->max || (size & (size - 1))) f->strict++;
            q->size = size;
        }
        q->desc = fk_rd64(c + 0x20);
        q->avail = fk_rd64(c + 0x28);
        q->used = fk_rd64(c + 0x30);
        if (c[0x1C] && !q->enable) {
            if (!q->max) f->strict++;
            q->enable = 1;
        }
    }
    /* notifications */
    for (uint32_t i = 0; i < FV_QN; i++) {
        uint8_t *nb = f->r + FV_NOTIFY + FV_MULT * i;
        uint16_t v = (uint16_t) (nb[0] | (nb[1] << 8));
        if (v == 0xFFFF) continue;
        nb[0] = nb[1] = 0xFF;
        if (v != i) f->strict++; /* the value written is the queue index */
        f->q[i].kicks++;
        if (!f->deaf) fv_serve_queue(f, i);
    }
}

/* Add the vendor capability at `at` (chained to `next`). */
static inline void fv_cap(fake_dev_t *d, uint32_t at, uint32_t next, uint32_t type, uint32_t off,
                          uint32_t len)
{
    d->cfg[at / 4] = 0x09u | (next << 8) | (16u << 16) | (type << 24);
    d->cfg[at / 4 + 1] = 4; /* BAR 4 */
    d->cfg[at / 4 + 2] = off;
    d->cfg[at / 4 + 3] = len;
    if (type == 2) d->cfg[at / 4 + 4] = FV_MULT; /* notify_off_multiplier */
}

/* A fake virtio PCI function with queues of `max` entries each (0 = the
 * queue does not exist). */
static inline fvirtio_t *fv_new(fake_plat_t *fp, uint32_t bdf, uint16_t device, uint8_t cls,
                                uint8_t sub, uint64_t features, const uint16_t qmax[FV_QN],
                                void (*tick)(fake_dev_t *, uint64_t))
{
    fake_dev_t *d = fake_dev_new(fp, bdf, 0x1AF4, device, cls, sub, 0);
    fvirtio_t *f = (fvirtio_t *) calloc(1, sizeof *f);
    f->dev = d;
    f->r = fake_dev_bar(d, 4, FV_BAR);
    f->offered = features | (1ull << 32);
    d->cfg[0x34 / 4] = 0x40;
    fv_cap(d, 0x40, 0x54, 1, FV_COMMON, 0x38);
    fv_cap(d, 0x54, 0x6C, 2, FV_NOTIFY, FV_MULT * FV_QN);
    fv_cap(d, 0x6C, 0x80, 3, FV_ISR, 4);
    fv_cap(d, 0x80, 0x00, 4, FV_DEVCFG, 0x100);
    memset(f->r + FV_NOTIFY, 0xFF, FV_MULT * FV_QN);
    for (uint32_t i = 0; i < FV_QN; i++) {
        f->q[i].max = qmax[i];
        f->q[i].notify_off = (uint16_t) i;
    }
    fv_reset(f);
    f->resets = 0;
    d->state = f;
    d->tick = tick;
    return f;
}

#endif
