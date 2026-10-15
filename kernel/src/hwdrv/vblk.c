/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vblk.c — virtio-blk over the PCI transport (VIRTIO 1.1, section 5.2),
 * polled, one request at a time.
 *
 * The disk of a KVM cloud VM. Requests are in 512-byte sectors whatever the
 * device's own block size, so the disk registers as 512-byte blocks. Each
 * request is three descriptors: the 16-byte header, a bounce buffer of up to
 * 64 KB (callers' buffers need no alignment; less when the device sets
 * VIRTIO_BLK_F_SIZE_MAX), and the status byte. A device that offers
 * VIRTIO_BLK_F_RO refuses writes here before any request.
 *
 * Contradiction (HW_B): a capacity of zero sectors from a device that says it
 * is a disk is "nothing to drive" (HW_N), but a capacity that does not fit
 * 64-bit byte addressing is held. */
#include "virtio_pci.h"

#define F_SIZE_MAX 1u /* largest data segment, config offset 8 */
#define F_RO       5u /* read-only device */

#define T_IN  0u
#define T_OUT 1u

#define SECTOR  512u
#define BOUNCE  65536u
#define TIMEOUT 5000000u /* 5 s per request */

typedef struct vblk {
    vpci_t v;
    uint8_t *hdr, *buf, *st;
    uint8_t ro;
    uint32_t per; /* sectors one request may move */
    hw_blk_t blk;
} vblk_t;

const hw_id_t vblk_ids[] = {{VPCI_VENDOR, 0x1001}, {VPCI_VENDOR, 0x1042}, {0, 0}};

static uint32_t g_vblk_n;

static void put32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t) v;
    b[1] = (uint8_t) (v >> 8);
    b[2] = (uint8_t) (v >> 16);
    b[3] = (uint8_t) (v >> 24);
}

/* One request of k sectors at lba. 0 = done and status OK. */
static int request(vblk_t *b, uint32_t type, uint64_t lba, uint32_t k)
{
    put32(b->hdr, type);
    put32(b->hdr + 4, 0);
    put32(b->hdr + 8, (uint32_t) lba);
    put32(b->hdr + 12, (uint32_t) (lba >> 32));
    b->st[0] = 0xFF;
    vring_buf_t bufs[3] = {
        {b->hdr, 16, false}, {b->buf, k * SECTOR, type == T_IN}, {b->st, 1, true}};
    if (vring_add(&b->v.q[0].vr, bufs, 3) < 0) return -1;
    vpci_kick(&b->v, 0);
    uint32_t len;
    if (vpci_wait(&b->v, 0, &len, TIMEOUT) < 0) return -1;
    return b->st[0] == 0 ? 0 : -1;
}

static int vblk_rw(hw_blk_t *hb, uint64_t lba, uint32_t n, void *buf, int write)
{
    vblk_t *b = (vblk_t *) hb->drv;
    if (lba >= hb->blocks || n > hb->blocks - lba) return -1;
    if (write && b->ro) return -1;
    uint8_t *u = (uint8_t *) buf;
    while (n) {
        uint32_t k = n < b->per ? n : b->per;
        if (write) hw_memcpy(b->buf, u, k * SECTOR);
        if (request(b, write ? T_OUT : T_IN, lba, k)) return -1;
        if (!write) hw_memcpy(u, b->buf, k * SECTOR);
        u += k * SECTOR;
        lba += k;
        n -= k;
    }
    return 0;
}

static int vblk_read(hw_blk_t *hb, uint64_t lba, uint32_t n, void *buf)
{
    return vblk_rw(hb, lba, n, buf, 0);
}

static int vblk_write(hw_blk_t *hb, uint64_t lba, uint32_t n, const void *buf)
{
    return vblk_rw(hb, lba, n, (void *) buf, 1);
}

hw_verdict_t vblk_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r, hw_line_t *note)
{
    vblk_t *b = (vblk_t *) p->dma_alloc(p->ctx, sizeof(vblk_t), 64, 0);
    if (!b) {
        hw_line_s(note, "out of DMA memory");
        return HW_F;
    }
    if (vpci_attach(&b->v, p, d, note)) return HW_N;
    if (vpci_negotiate(&b->v, (1ull << F_RO) | (1ull << F_SIZE_MAX), note)) return HW_F;
    b->hdr = (uint8_t *) p->dma_alloc(p->ctx, 16, 16, 0);
    b->buf = (uint8_t *) p->dma_alloc(p->ctx, BOUNCE, 4096, 0);
    b->st = (uint8_t *) p->dma_alloc(p->ctx, 1, 1, 0);
    if (!b->hdr || !b->buf || !b->st) {
        hw_line_s(note, "out of DMA memory");
        return HW_F;
    }
    if (vpci_queue(&b->v, 0, 0, note)) return HW_F;
    vpci_ready(&b->v);
    uint8_t cap[8];
    vpci_cfg_read(&b->v, 0, cap, 8);
    uint64_t sectors = 0;
    for (int i = 7; i >= 0; i--) sectors = (sectors << 8) | cap[i];
    if (sectors == 0) {
        hw_line_s(note, "no medium (capacity 0)");
        vpci_reset(&b->v);
        return HW_N;
    }
    if (sectors > (~0ull >> 9)) {
        hw_line_s(note, "a capacity past 64-bit byte addressing");
        vpci_reset(&b->v);
        return HW_B;
    }
    b->ro = (uint8_t) ((b->v.features >> F_RO) & 1);
    b->per = BOUNCE / SECTOR;
    if ((b->v.features >> F_SIZE_MAX) & 1) {
        uint8_t sm[4];
        vpci_cfg_read(&b->v, 8, sm, 4);
        uint32_t max = (uint32_t) sm[0] | ((uint32_t) sm[1] << 8) | ((uint32_t) sm[2] << 16) |
                       ((uint32_t) sm[3] << 24);
        if (max < SECTOR) {
            hw_line_s(note, "a largest segment under one sector");
            vpci_reset(&b->v);
            return HW_B;
        }
        if (max / SECTOR < b->per) b->per = max / SECTOR;
    }
    hw_blk_t *hb = &b->blk;
    hw_memcpy(hb->name, "vblk", 4);
    hb->name[4] = (char) ('0' + g_vblk_n % 10);
    hb->name[5] = 0;
    hb->block_size = SECTOR;
    hb->blocks = sectors;
    hb->drv = b;
    hb->read = vblk_read;
    hb->write = vblk_write;
    hw_line_s(note, "virtio disk, ");
    hw_line_u(note, sectors);
    hw_line_s(note, " x 512 B");
    if (b->ro) hw_line_s(note, ", read-only");
    hw_line_s(note, " as ");
    hw_line_s(note, hb->name);
    if (hw_reg_blk(r, hb) != 0) return HW_N;
    g_vblk_n++;
    return HW_T;
}
