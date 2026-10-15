/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* virtio_pci.c — the virtio 1.x PCI transport (virtio_pci.h).
 *
 * The register writes that select something (a feature word, a queue) are
 * each followed by delay_us(1): a real device answers at once, and the host
 * fake device (src/hwdrv/test) answers from delay_us, as every fake here
 * does. */
#include "virtio_pci.h"

#define CAP_VENDOR 0x09u
#define CFG_COMMON 1u
#define CFG_NOTIFY 2u
#define CFG_DEVICE 4u

static void settle(const vpci_t *v)
{
    v->p->delay_us(v->p->ctx, 1);
}

int vpci_attach(vpci_t *v, const hw_plat_t *p, const hw_pci_dev_t *d, hw_line_t *note)
{
    hw_memset(v, 0, sizeof *v);
    v->p = p;
    if (!(p->cfg_read(p->ctx, d->bdf, 0x04) & (0x10u << 16))) { /* status: capability list */
        hw_line_s(note, "no capability list (a legacy-only virtio device)");
        return -1;
    }
    volatile uint8_t *mapped[6] = {0};
    uint32_t at = p->cfg_read(p->ctx, d->bdf, 0x34) & 0xFC;
    for (uint32_t hops = 0; at && hops < 48; hops++) {
        uint32_t w0 = p->cfg_read(p->ctx, d->bdf, at);
        uint32_t next = (w0 >> 8) & 0xFC;
        if ((w0 & 0xFF) == CAP_VENDOR) {
            uint32_t type = (w0 >> 24) & 0xFF;
            uint32_t bar = p->cfg_read(p->ctx, d->bdf, at + 4) & 0xFF;
            uint32_t off = p->cfg_read(p->ctx, d->bdf, at + 8);
            uint32_t len = p->cfg_read(p->ctx, d->bdf, at + 12);
            volatile uint8_t *base = 0;
            if (bar < 6 && d->bar_size[bar] && (uint64_t) off + len <= d->bar_size[bar]) {
                if (!mapped[bar]) mapped[bar] = (volatile uint8_t *) hw_pci_map_bar(p, d, bar);
                base = mapped[bar];
            }
            if (base) {
                volatile void *s = base + off;
                if (type == CFG_COMMON && !v->common && len >= 0x38) v->common = s;
                if (type == CFG_NOTIFY && !v->notify_base) {
                    v->notify_base = s;
                    v->notify_mult = p->cfg_read(p->ctx, d->bdf, at + 16);
                }
                if (type == CFG_DEVICE && !v->devcfg) v->devcfg = s;
            }
        }
        at = next;
    }
    if (!v->common || !v->notify_base || !v->devcfg) {
        hw_line_s(note, "no modern virtio structures in a memory BAR (legacy-only device)");
        return -1;
    }
    hw_pci_enable(p, d);
    return 0;
}

static void status_or(vpci_t *v, uint8_t bits)
{
    hw_wr8(v->common, VPCI_STATUS, (uint8_t) (hw_rd8(v->common, VPCI_STATUS) | bits));
    settle(v);
}

void vpci_reset(vpci_t *v)
{
    hw_wr8(v->common, VPCI_STATUS, 0);
    settle(v);
    /* the device has finished resetting when status reads back 0 */
    for (uint32_t i = 0; i < 100000 && hw_rd8(v->common, VPCI_STATUS); i++) settle(v);
}

int vpci_negotiate(vpci_t *v, uint64_t want, hw_line_t *note)
{
    vpci_reset(v);
    if (hw_rd8(v->common, VPCI_STATUS)) {
        hw_line_s(note, "reset did not complete");
        return -1;
    }
    status_or(v, VPCI_S_ACK);
    status_or(v, VPCI_S_DRIVER);
    uint64_t offered = 0;
    for (uint32_t w = 0; w < 2; w++) {
        hw_wr32(v->common, VPCI_DFSELECT, w);
        settle(v);
        offered |= (uint64_t) hw_rd32(v->common, VPCI_DF) << (32 * w);
    }
    if (!((offered >> VPCI_F_VERSION_1) & 1)) {
        status_or(v, VPCI_S_FAILED);
        hw_line_s(note, "does not offer VIRTIO_F_VERSION_1");
        return -1;
    }
    v->features = (offered & want) | (1ull << VPCI_F_VERSION_1);
    for (uint32_t w = 0; w < 2; w++) {
        hw_wr32(v->common, VPCI_GFSELECT, w);
        settle(v);
        hw_wr32(v->common, VPCI_GF, (uint32_t) (v->features >> (32 * w)));
        settle(v);
    }
    status_or(v, VPCI_S_FEATURES_OK);
    if (!(hw_rd8(v->common, VPCI_STATUS) & VPCI_S_FEATURES_OK)) {
        status_or(v, VPCI_S_FAILED);
        hw_line_s(note, "refused the features offered back to it");
        return -1;
    }
    hw_wr16(v->common, VPCI_MSIX, VPCI_NO_VECTOR);
    return 0;
}

int vpci_queue(vpci_t *v, uint32_t slot, uint16_t index, hw_line_t *note)
{
    vpci_queue_t *q = &v->q[slot];
    hw_wr16(v->common, VPCI_QSELECT, index);
    settle(v);
    uint32_t max = hw_rd16(v->common, VPCI_QSIZE);
    if (max == 0) {
        hw_line_s(note, "a virtqueue it needs is missing");
        return -1;
    }
    uint32_t depth = VRING_MAX_DEPTH;
    while (depth > max) depth >>= 1;
    if (depth < 4) {
        hw_line_s(note, "a virtqueue too short to use");
        return -1;
    }
    uint64_t pd, pa, pu;
    vring_desc_t *desc =
        (vring_desc_t *) v->p->dma_alloc(v->p->ctx, sizeof(vring_desc_t) * depth, 16, &pd);
    vring_avail_t *avail =
        (vring_avail_t *) v->p->dma_alloc(v->p->ctx, sizeof(vring_avail_t), 2, &pa);
    vring_used_t *used = (vring_used_t *) v->p->dma_alloc(v->p->ctx, sizeof(vring_used_t), 4, &pu);
    if (!desc || !avail || !used || !vring_init(&q->vr, desc, avail, used, (uint16_t) depth)) {
        hw_line_s(note, "out of DMA memory");
        return -1;
    }
    q->index = index;
    q->depth = (uint16_t) depth;
    hw_wr16(v->common, VPCI_QSIZE, (uint16_t) depth);
    hw_wr16(v->common, VPCI_QMSIX, VPCI_NO_VECTOR);
    hw_wr64(v->common, VPCI_QDESC, pd);
    hw_wr64(v->common, VPCI_QDRIVER, pa);
    hw_wr64(v->common, VPCI_QDEVICE, pu);
    uint32_t noff = hw_rd16(v->common, VPCI_QNOTIFY);
    q->notify = (volatile uint8_t *) v->notify_base + noff * v->notify_mult;
    hw_wmb();
    hw_wr16(v->common, VPCI_QENABLE, 1);
    settle(v);
    return 0;
}

void vpci_ready(vpci_t *v)
{
    status_or(v, VPCI_S_DRIVER_OK);
}

void vpci_kick(vpci_t *v, uint32_t slot)
{
    hw_wmb();
    hw_wr16(v->q[slot].notify, 0, v->q[slot].index);
}

void vpci_cfg_read(vpci_t *v, uint32_t off, void *out, uint32_t n)
{
    uint8_t *o = (uint8_t *) out;
    for (uint32_t tries = 0; tries < 16; tries++) {
        uint8_t g = hw_rd8(v->common, VPCI_GEN);
        for (uint32_t i = 0; i < n; i++) o[i] = hw_rd8(v->devcfg, off + i);
        if (hw_rd8(v->common, VPCI_GEN) == g) return;
    }
}

int32_t vpci_wait(vpci_t *v, uint32_t slot, uint32_t *len, uint32_t timeout_us)
{
    for (uint32_t waited = 0;; waited += 10) {
        int32_t id = vring_get_used(&v->q[slot].vr, len);
        if (id >= 0) return id;
        if (waited >= timeout_us) return -1;
        v->p->delay_us(v->p->ctx, 10);
    }
}
