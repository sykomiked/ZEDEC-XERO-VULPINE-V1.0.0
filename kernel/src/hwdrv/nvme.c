/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* nvme.c — NVM Express class driver (PCI class 01/08/02), polled.
 *
 * Follows NVMe base spec 1.4 section 7.6.1 (controller initialisation): reset,
 * admin queues, Identify controller and namespace 1, one I/O queue pair. Data
 * moves through a two-page bounce buffer described by PRP1/PRP2, so a command
 * carries at most 8 KB and callers' buffers need no alignment. No interrupts:
 * completions are found by the phase tag.
 *
 * Contradiction (HW_B): the class code says NVMe but CAP says the controller
 * does not support the NVM command set, or VS reports major version 0. */
#include "hwdrv.h"

#define REG_CAP  0x00
#define REG_VS   0x08
#define REG_CC   0x14
#define REG_CSTS 0x1C
#define REG_AQA  0x24
#define REG_ASQ  0x28
#define REG_ACQ  0x30

#define CC_EN     1u
#define CC_IOSQES (6u << 16)
#define CC_IOCQES (4u << 20)
#define CSTS_RDY  1u
#define CSTS_CFS  2u

#define QDEPTH 16
#define PAGE   4096u

typedef struct nvme_dev {
    const hw_plat_t *p;
    volatile void *r;
    uint32_t dstrd; /* doorbell stride in bytes */
    uint32_t timeout_us;
    /* queue 0 = admin, 1 = I/O */
    uint8_t *sq[2], *cq[2];
    uint64_t sq_phys[2], cq_phys[2];
    uint16_t sq_tail[2], cq_head[2], cid;
    uint8_t phase[2];
    uint8_t *buf; /* 2-page bounce buffer */
    uint64_t buf_phys;
    uint32_t nsid;
    hw_blk_t blk;
} nvme_dev_t;

static uint32_t g_nvme_n; /* controllers bound so far: names nvme0, nvme1, ... */

static void put32(uint8_t *b, uint32_t off, uint32_t v)
{
    b[off] = (uint8_t) v;
    b[off + 1] = (uint8_t) (v >> 8);
    b[off + 2] = (uint8_t) (v >> 16);
    b[off + 3] = (uint8_t) (v >> 24);
}

static uint32_t get32(const volatile uint8_t *b, uint32_t off)
{
    return (uint32_t) b[off] | ((uint32_t) b[off + 1] << 8) | ((uint32_t) b[off + 2] << 16) |
           ((uint32_t) b[off + 3] << 24);
}

/* Submit one command on queue q and wait for its completion. Returns the
 * 15-bit status (0 = success) or -1 on timeout. */
static int nvme_cmd(nvme_dev_t *n, uint32_t q, uint32_t opc, uint32_t nsid, uint64_t prp1,
                    uint64_t prp2, uint32_t c10, uint32_t c11, uint32_t c12)
{
    uint8_t *e = n->sq[q] + (uint32_t) n->sq_tail[q] * 64;
    uint16_t cid = ++n->cid;
    hw_memset(e, 0, 64);
    put32(e, 0, opc | ((uint32_t) cid << 16));
    put32(e, 4, nsid);
    put32(e, 24, (uint32_t) prp1);
    put32(e, 28, (uint32_t) (prp1 >> 32));
    put32(e, 32, (uint32_t) prp2);
    put32(e, 36, (uint32_t) (prp2 >> 32));
    put32(e, 40, c10);
    put32(e, 44, c11);
    put32(e, 48, c12);
    n->sq_tail[q] = (uint16_t) ((n->sq_tail[q] + 1) % QDEPTH);
    hw_wmb();
    hw_wr32(n->r, 0x1000 + (2 * q) * n->dstrd, n->sq_tail[q]);

    const volatile uint8_t *c = n->cq[q] + (uint32_t) n->cq_head[q] * 16;
    for (uint32_t waited = 0;; waited += 10) {
        uint32_t dw3 = get32(c, 12);
        if (((dw3 >> 16) & 1) == n->phase[q]) {
            n->cq_head[q] = (uint16_t) ((n->cq_head[q] + 1) % QDEPTH);
            if (n->cq_head[q] == 0) n->phase[q] ^= 1;
            hw_wr32(n->r, 0x1000 + (2 * q + 1) * n->dstrd, n->cq_head[q]);
            if ((dw3 & 0xFFFF) != cid) return -1;
            return (int) ((dw3 >> 17) & 0x7FFF);
        }
        if (waited >= n->timeout_us) return -1;
        n->p->delay_us(n->p->ctx, 10);
    }
}

static int nvme_rw(hw_blk_t *b, uint64_t lba, uint32_t count, void *buf, int write)
{
    nvme_dev_t *n = (nvme_dev_t *) b->drv;
    uint8_t *u = (uint8_t *) buf;
    uint32_t per = (2 * PAGE) / b->block_size;
    while (count) {
        uint32_t k = count < per ? count : per;
        uint32_t bytes = k * b->block_size;
        if (write) hw_memcpy(n->buf, u, bytes);
        uint64_t prp2 = bytes > PAGE ? n->buf_phys + PAGE : 0;
        int s = nvme_cmd(n, 1, write ? 0x01 : 0x02, n->nsid, n->buf_phys, prp2, (uint32_t) lba,
                         (uint32_t) (lba >> 32), k - 1);
        if (s != 0) return -1;
        if (!write) hw_memcpy(u, n->buf, bytes);
        u += bytes;
        lba += k;
        count -= k;
    }
    return 0;
}

static int nvme_read(hw_blk_t *b, uint64_t lba, uint32_t n, void *buf)
{
    if (lba >= b->blocks || n > b->blocks - lba) return -1;
    return nvme_rw(b, lba, n, buf, 0);
}

static int nvme_write(hw_blk_t *b, uint64_t lba, uint32_t n, const void *buf)
{
    if (lba >= b->blocks || n > b->blocks - lba) return -1;
    return nvme_rw(b, lba, n, (void *) buf, 1);
}

static int alloc_queue(nvme_dev_t *n, uint32_t q)
{
    n->sq[q] = (uint8_t *) n->p->dma_alloc(n->p->ctx, QDEPTH * 64, PAGE, &n->sq_phys[q]);
    n->cq[q] = (uint8_t *) n->p->dma_alloc(n->p->ctx, QDEPTH * 16, PAGE, &n->cq_phys[q]);
    n->sq_tail[q] = n->cq_head[q] = 0;
    n->phase[q] = 1;
    return n->sq[q] && n->cq[q];
}

static void note_model(hw_line_t *note, const uint8_t *id)
{
    /* Model number: bytes 24..63, ASCII, space padded. */
    uint32_t end = 63;
    while (end >= 24 && (id[end] == ' ' || id[end] == 0)) end--;
    for (uint32_t i = 24; i <= end && i < 64; i++)
        hw_line_c(note, (id[i] >= 0x20 && id[i] < 0x7F) ? (char) id[i] : '?');
}

hw_verdict_t nvme_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r, hw_line_t *note)
{
    volatile void *regs = hw_pci_map_bar(p, d, 0);
    if (!regs) {
        hw_line_s(note, "BAR0 not assigned");
        return HW_N;
    }
    /* Driver state comes from the platform allocator too (zeroed); there is
     * no kernel heap contract the drivers could rely on instead. */
    nvme_dev_t *n = (nvme_dev_t *) p->dma_alloc(p->ctx, sizeof(nvme_dev_t), 64, 0);
    if (!n) {
        hw_line_s(note, "out of DMA memory");
        return HW_F;
    }
    n->p = p;
    n->r = regs;
    hw_pci_enable(p, d);
    uint64_t cap = hw_rd64(n->r, REG_CAP);
    uint32_t vs = hw_rd32(n->r, REG_VS);
    if (cap == 0xFFFFFFFFFFFFFFFFULL) {
        hw_line_s(note, "registers read all-ones");
        return HW_F;
    }
    if (!((cap >> 37) & 1) || (vs >> 16) == 0) {
        hw_line_s(note, "CAP lacks the NVM command set or VS is 0");
        return HW_B;
    }
    if (((cap >> 48) & 0xF) != 0) { /* MPSMIN above 4 KB: our PRPs are 4 KB */
        hw_line_s(note, "minimum page size above 4 KB");
        return HW_B;
    }
    n->dstrd = 4u << ((cap >> 32) & 0xF);
    uint32_t to = (uint32_t) ((cap >> 24) & 0xFF); /* units of 500 ms */
    n->timeout_us = (to ? to : 1) * 500000u;
    if (n->timeout_us > 10000000u) n->timeout_us = 10000000u;
    uint32_t mqes = (uint32_t) (cap & 0xFFFF) + 1;
    if (mqes < QDEPTH) {
        hw_line_s(note, "queues shorter than 16 entries");
        return HW_B;
    }

    /* 1. disable and wait for not-ready */
    hw_wr32(n->r, REG_CC, hw_rd32(n->r, REG_CC) & ~CC_EN);
    if (!hw_wait32(p, n->r, REG_CSTS, CSTS_RDY, 0, n->timeout_us)) {
        hw_line_s(note, "reset timed out");
        return HW_F;
    }
    /* 2. admin queues */
    if (!alloc_queue(n, 0) || !alloc_queue(n, 1)) {
        hw_line_s(note, "out of DMA memory");
        return HW_F;
    }
    n->buf = (uint8_t *) p->dma_alloc(p->ctx, 2 * PAGE, PAGE, &n->buf_phys);
    if (!n->buf) {
        hw_line_s(note, "out of DMA memory");
        return HW_F;
    }
    hw_wr32(n->r, REG_AQA, ((QDEPTH - 1u) << 16) | (QDEPTH - 1u));
    hw_wr64(n->r, REG_ASQ, n->sq_phys[0]);
    hw_wr64(n->r, REG_ACQ, n->cq_phys[0]);
    /* 3. enable: 4 KB pages, round robin, 64-byte SQ / 16-byte CQ entries */
    hw_wr32(n->r, REG_CC, CC_IOSQES | CC_IOCQES | CC_EN);
    if (!hw_wait32(p, n->r, REG_CSTS, CSTS_RDY | CSTS_CFS, CSTS_RDY, n->timeout_us)) {
        hw_line_s(note, (hw_rd32(n->r, REG_CSTS) & CSTS_CFS) ? "controller fatal status"
                                                             : "enable timed out");
        return HW_F;
    }
    /* 4. Identify controller (CNS 1) */
    if (nvme_cmd(n, 0, 0x06, 0, n->buf_phys, 0, 1, 0, 0) != 0) {
        hw_line_s(note, "Identify controller failed");
        return HW_F;
    }
    note_model(note, n->buf);
    /* 5. I/O completion queue 1, then submission queue 1 (physically
     * contiguous, interrupts off) */
    if (nvme_cmd(n, 0, 0x05, 0, n->cq_phys[1], 0, ((QDEPTH - 1u) << 16) | 1, 1, 0) != 0 ||
        nvme_cmd(n, 0, 0x01, 0, n->sq_phys[1], 0, ((QDEPTH - 1u) << 16) | 1, (1u << 16) | 1, 0) !=
            0) {
        hw_line_s(note, "; I/O queue creation failed");
        return HW_F;
    }
    /* 6. Identify namespace 1 (CNS 0) */
    n->nsid = 1;
    if (nvme_cmd(n, 0, 0x06, 1, n->buf_phys, 0, 0, 0, 0) != 0) {
        hw_line_s(note, "; no namespace 1");
        return HW_N;
    }
    uint64_t nsze = (uint64_t) get32(n->buf, 0) | ((uint64_t) get32(n->buf, 4) << 32);
    uint32_t flbas = n->buf[26] & 0xF;
    uint32_t lbads = (get32(n->buf, 128 + 4 * flbas) >> 16) & 0xFF;
    if (nsze == 0) {
        hw_line_s(note, "; namespace 1 is empty");
        return HW_N;
    }
    if (lbads < 9 || lbads > 12) {
        hw_line_s(note, "; unsupported block size");
        return HW_B;
    }
    hw_blk_t *b = &n->blk;
    b->name[0] = 'n';
    b->name[1] = 'v';
    b->name[2] = 'm';
    b->name[3] = 'e';
    b->name[4] = (char) ('0' + g_nvme_n % 10);
    b->name[5] = 0;
    b->block_size = 1u << lbads;
    b->blocks = nsze;
    b->drv = n;
    b->read = nvme_read;
    b->write = nvme_write;
    hw_line_s(note, ", ns1 ");
    hw_line_u(note, nsze);
    hw_line_s(note, " x ");
    hw_line_u(note, b->block_size);
    hw_line_s(note, " B as ");
    hw_line_s(note, b->name);
    if (hw_reg_blk(r, b) != 0) return HW_N;
    g_nvme_n++;
    return HW_T;
}
