/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ahci.c — AHCI (Serial ATA) class driver (PCI class 01/06/01), polled.
 *
 * Follows AHCI 1.3.1 section 10 (software initialisation): BIOS/OS handoff,
 * GHC.AE, HBA reset, then per implemented port: stop the engines, give the
 * port a command list, a received-FIS area and one command table, enable FIS
 * receive, wait for the link and for the device to leave BSY/DRQ, check the
 * signature and start the command engine. ATA disks get IDENTIFY DEVICE and
 * become hw_blk_t sata0, sata1, ...; ATAPI drives, port multipliers and
 * enclosure bridges are left stopped with a note.
 *
 * Only command slot 0 is used, with one PRD pointing at a 16 KB bounce buffer
 * shared by every port of the HBA, so callers' buffers need no alignment.
 * Disks with 48-bit addressing use READ/WRITE DMA EXT, older ones READ/WRITE
 * DMA. A task-file error (PxIS.TFES or TFD.ERR) fails the request and recovers
 * the port: stop, clear SERR/IS, command-list override if BSY/DRQ stuck,
 * restart.
 *
 * No interrupts. Every register read that follows a write to a W1C register
 * comes after at least one delay_us, so the host fake device (which only acts
 * in delay_us) and real hardware read the same values.
 *
 * Verdicts: HW_T at least one disk registered; HW_F the HBA or every device
 * failed (reset timeout, BSY stuck, IDENTIFY error, DMA memory above 4 GB
 * without CAP.S64A); HW_B the HBA contradicts itself (no implemented ports,
 * version 0) or the only device reports an unusable geometry; HW_N no disks. */
#include "hwdrv.h"

#define HBA_CAP  0x00
#define HBA_GHC  0x04
#define HBA_PI   0x0C
#define HBA_VS   0x10
#define HBA_CAP2 0x24
#define HBA_BOHC 0x28

#define CAP_S64A (1u << 31)
#define CAP_SSS  (1u << 27)
#define CAP_SCLO (1u << 24)
#define CAP2_BOH 1u
#define BOHC_BOS 0x01u
#define BOHC_OOS 0x02u
#define BOHC_BB  0x10u
#define GHC_HR   0x01u
#define GHC_AE   (1u << 31)

#define PX(n, reg) (0x100u + (n) * 0x80u + (reg))
#define PX_CLB     0x00
#define PX_CLBU    0x04
#define PX_FB      0x08
#define PX_FBU     0x0C
#define PX_IS      0x10
#define PX_IE      0x14
#define PX_CMD     0x18
#define PX_TFD     0x20
#define PX_SIG     0x24
#define PX_SSTS    0x28
#define PX_SCTL    0x2C
#define PX_SERR    0x30
#define PX_CI      0x38

#define CMD_ST  0x0001u
#define CMD_SUD 0x0002u
#define CMD_POD 0x0004u
#define CMD_CLO 0x0008u
#define CMD_FRE 0x0010u
#define CMD_FR  0x4000u
#define CMD_CR  0x8000u

#define IS_TFES  (1u << 30)
#define IS_HBFS  (1u << 29)
#define IS_HBDS  (1u << 28)
#define IS_IFS   (1u << 27)
#define IS_FATAL (IS_TFES | IS_HBFS | IS_HBDS | IS_IFS)

#define TFD_ERR 0x01u
#define TFD_DRQ 0x08u
#define TFD_BSY 0x80u

#define SIG_ATA   0x00000101u
#define SIG_ATAPI 0xEB140101u
#define SIG_SEMB  0xC33C0101u
#define SIG_PM    0x96690101u

#define ATA_IDENTIFY      0xEC
#define ATA_READ_DMA      0xC8
#define ATA_WRITE_DMA     0xCA
#define ATA_READ_DMA_EXT  0x25
#define ATA_WRITE_DMA_EXT 0x35

#define BOUNCE 16384u

#define T_RESET  1000000u /* HBA reset, 1 s (spec) */
#define T_ENGINE 500000u  /* CR/FR clear, 500 ms (spec) */
#define T_LINK   50000u   /* PHY up after reset or COMRESET */
#define T_BSY    3000000u /* device ready after link up (spin-up) */
#define T_CMD    2000000u /* one command, at most 16 KB */

typedef struct ahci_hba {
    const hw_plat_t *p;
    volatile void *r;
    uint32_t cap;
    uint8_t *buf; /* bounce buffer, shared by the HBA's ports */
    uint64_t buf_phys;
} ahci_hba_t;

typedef struct ahci_port {
    ahci_hba_t *h;
    uint32_t n;
    uint8_t *cl, *fis, *ct;
    uint64_t cl_phys, fis_phys, ct_phys;
    int lba48;
    hw_blk_t blk;
} ahci_port_t;

static uint32_t g_sata_n; /* disks registered so far: sata0, sata1, ... */

static void put32(uint8_t *b, uint32_t off, uint32_t v)
{
    b[off] = (uint8_t) v;
    b[off + 1] = (uint8_t) (v >> 8);
    b[off + 2] = (uint8_t) (v >> 16);
    b[off + 3] = (uint8_t) (v >> 24);
}

static uint32_t idw(const uint8_t *id, uint32_t w)
{
    return (uint32_t) id[2 * w] | ((uint32_t) id[2 * w + 1] << 8);
}

/* Wait for a register after writing it: one delay first, so the device has
 * acted on the write before it is read back. */
static int wait_after(const hw_plat_t *p, volatile void *r, uint32_t off, uint32_t mask,
                      uint32_t want, uint32_t timeout_us)
{
    p->delay_us(p->ctx, 10);
    return hw_wait32(p, r, off, mask, want, timeout_us);
}

/* 1 when [phys, phys+size) is reachable: anywhere with CAP.S64A, else below
 * 4 GB. */
static int dma_ok(const ahci_hba_t *h, uint64_t phys, uint32_t size)
{
    if (h->cap & CAP_S64A) return 1;
    return ((phys + size - 1) >> 32) == 0;
}

/* Clear ST then FRE and wait for CR and FR to drop. 1 = stopped. */
static int port_stop(ahci_hba_t *h, uint32_t n)
{
    volatile void *r = h->r;
    hw_wr32(r, PX(n, PX_CMD), hw_rd32(r, PX(n, PX_CMD)) & ~CMD_ST);
    if (!wait_after(h->p, r, PX(n, PX_CMD), CMD_CR, 0, T_ENGINE)) return 0;
    hw_wr32(r, PX(n, PX_CMD), hw_rd32(r, PX(n, PX_CMD)) & ~CMD_FRE);
    return wait_after(h->p, r, PX(n, PX_CMD), CMD_FR, 0, T_ENGINE);
}

/* COMRESET through SCTL.DET, then wait for the link (DET = 3). */
static int port_comreset(ahci_hba_t *h, uint32_t n)
{
    volatile void *r = h->r;
    uint32_t sctl = hw_rd32(r, PX(n, PX_SCTL)) & ~0xFu;
    hw_wr32(r, PX(n, PX_SCTL), sctl | 1u);
    h->p->delay_us(h->p->ctx, 1000); /* at least 1 ms of COMRESET */
    hw_wr32(r, PX(n, PX_SCTL), sctl);
    return wait_after(h->p, r, PX(n, PX_SSTS), 0xF, 3, T_LINK);
}

/* After a task-file or host error: stop the command engine (which clears CI),
 * clear SERR and IS, get BSY/DRQ down (command list override, else COMRESET),
 * start again. */
static void port_recover(ahci_port_t *pt)
{
    ahci_hba_t *h = pt->h;
    volatile void *r = h->r;
    uint32_t n = pt->n;
    hw_wr32(r, PX(n, PX_CMD), hw_rd32(r, PX(n, PX_CMD)) & ~CMD_ST);
    wait_after(h->p, r, PX(n, PX_CMD), CMD_CR, 0, T_ENGINE);
    hw_wr32(r, PX(n, PX_SERR), 0xFFFFFFFFu);
    hw_wr32(r, PX(n, PX_IS), 0xFFFFFFFFu);
    h->p->delay_us(h->p->ctx, 10);
    if (hw_rd32(r, PX(n, PX_TFD)) & (TFD_BSY | TFD_DRQ)) {
        if (h->cap & CAP_SCLO) {
            hw_wr32(r, PX(n, PX_CMD), hw_rd32(r, PX(n, PX_CMD)) | CMD_CLO);
            wait_after(h->p, r, PX(n, PX_CMD), CMD_CLO, 0, T_ENGINE);
        }
        if (hw_rd32(r, PX(n, PX_TFD)) & (TFD_BSY | TFD_DRQ)) {
            port_comreset(h, n);
            hw_wr32(r, PX(n, PX_SERR), 0xFFFFFFFFu);
            wait_after(h->p, r, PX(n, PX_TFD), TFD_BSY | TFD_DRQ, 0, T_BSY);
        }
    }
    hw_wr32(r, PX(n, PX_CMD), hw_rd32(r, PX(n, PX_CMD)) | CMD_FRE | CMD_ST);
}

/* Issue one command in slot 0, data (bytes, at most BOUNCE) through the bounce
 * buffer. 0 = done, -1 = error or timeout (the port has been recovered). */
static int port_exec(ahci_port_t *pt, uint8_t cmd, uint64_t lba, uint32_t count, uint32_t bytes,
                     int write)
{
    ahci_hba_t *h = pt->h;
    volatile void *r = h->r;
    uint32_t n = pt->n;
    /* The device may still be busy (a previous command's tail, power
     * management): wait for it; only a port that stays stuck is recovered. */
    if (!hw_wait32(h->p, r, PX(n, PX_TFD), TFD_BSY | TFD_DRQ, 0, T_CMD) ||
        (hw_rd32(r, PX(n, PX_CI)) & 1u)) {
        port_recover(pt);
        if (!wait_after(h->p, r, PX(n, PX_TFD), TFD_BSY | TFD_DRQ, 0, T_CMD) ||
            (hw_rd32(r, PX(n, PX_CI)) & 1u))
            return -1;
    }
    /* command header 0: CFL 5 dwords, W, one PRD */
    put32(pt->cl, 0, 5u | (write ? (1u << 6) : 0) | (1u << 16));
    put32(pt->cl, 4, 0);
    put32(pt->cl, 8, (uint32_t) pt->ct_phys);
    put32(pt->cl, 12, (uint32_t) (pt->ct_phys >> 32));
    hw_memset(pt->ct, 0, 0x90);
    uint8_t *f = pt->ct; /* H2D register FIS */
    f[0] = 0x27;
    f[1] = 0x80; /* C: command register */
    f[2] = cmd;
    if (cmd == ATA_READ_DMA_EXT || cmd == ATA_WRITE_DMA_EXT) {
        f[4] = (uint8_t) lba;
        f[5] = (uint8_t) (lba >> 8);
        f[6] = (uint8_t) (lba >> 16);
        f[7] = 0x40; /* LBA mode */
        f[8] = (uint8_t) (lba >> 24);
        f[9] = (uint8_t) (lba >> 32);
        f[10] = (uint8_t) (lba >> 40);
        f[12] = (uint8_t) count;
        f[13] = (uint8_t) (count >> 8);
    } else if (cmd == ATA_READ_DMA || cmd == ATA_WRITE_DMA) {
        f[4] = (uint8_t) lba;
        f[5] = (uint8_t) (lba >> 8);
        f[6] = (uint8_t) (lba >> 16);
        f[7] = (uint8_t) (0x40 | ((lba >> 24) & 0xF));
        f[12] = (uint8_t) count; /* 256 is sent as 0 */
    }
    uint8_t *prd = pt->ct + 0x80;
    put32(prd, 0, (uint32_t) h->buf_phys);
    put32(prd, 4, (uint32_t) (h->buf_phys >> 32));
    put32(prd, 12, bytes - 1);
    hw_wr32(r, PX(n, PX_IS), 0xFFFFFFFFu);
    hw_wmb();
    hw_wr32(r, PX(n, PX_CI), 1u);
    for (uint32_t waited = 0;; waited += 10) {
        h->p->delay_us(h->p->ctx, 10);
        if (hw_rd32(r, PX(n, PX_IS)) & IS_FATAL) break;
        if (!(hw_rd32(r, PX(n, PX_CI)) & 1u)) {
            if (hw_rd32(r, PX(n, PX_TFD)) & (TFD_ERR | TFD_BSY | TFD_DRQ)) break;
            return 0;
        }
        if (waited >= T_CMD) break;
    }
    port_recover(pt);
    return -1;
}

static int ahci_rw(hw_blk_t *b, uint64_t lba, uint32_t count, uint8_t *u, int write)
{
    ahci_port_t *pt = (ahci_port_t *) b->drv;
    uint32_t per = BOUNCE / b->block_size;
    while (count) {
        uint32_t k = count < per ? count : per;
        uint32_t bytes = k * b->block_size;
        uint8_t op = pt->lba48 ? (write ? ATA_WRITE_DMA_EXT : ATA_READ_DMA_EXT)
                               : (write ? ATA_WRITE_DMA : ATA_READ_DMA);
        if (write) hw_memcpy(pt->h->buf, u, bytes);
        if (port_exec(pt, op, lba, k, bytes, write) != 0) return -1;
        if (!write) hw_memcpy(u, pt->h->buf, bytes);
        u += bytes;
        lba += k;
        count -= k;
    }
    return 0;
}

static int ahci_read(hw_blk_t *b, uint64_t lba, uint32_t n, void *buf)
{
    if (n == 0 || lba >= b->blocks || n > b->blocks - lba) return -1;
    return ahci_rw(b, lba, n, (uint8_t *) buf, 0);
}

static int ahci_write(hw_blk_t *b, uint64_t lba, uint32_t n, const void *buf)
{
    if (n == 0 || lba >= b->blocks || n > b->blocks - lba) return -1;
    return ahci_rw(b, lba, n, (uint8_t *) buf, 1);
}

/* Model number: IDENTIFY words 27..46, two ASCII bytes per word, high first. */
static void note_model(hw_line_t *note, const uint8_t *id)
{
    char m[40];
    for (uint32_t i = 0; i < 20; i++) {
        uint32_t w = idw(id, 27 + i);
        m[2 * i] = (char) (w >> 8);
        m[2 * i + 1] = (char) w;
    }
    uint32_t end = 40;
    while (end > 0 && (m[end - 1] == ' ' || m[end - 1] == 0)) end--;
    uint32_t start = 0;
    while (start < end && m[start] == ' ') start++;
    for (uint32_t i = start; i < end; i++)
        hw_line_c(note, (m[i] >= 0x20 && m[i] < 0x7F) ? m[i] : '?');
}

static void note_port(hw_line_t *note, uint32_t n, const char *what)
{
    hw_line_s(note, "; port ");
    hw_line_u(note, n);
    hw_line_c(note, ' ');
    hw_line_s(note, what);
}

enum { P_EMPTY, P_DISK, P_OTHER, P_HELD, P_FAIL, P_ABORT };

static int port_init(ahci_hba_t *h, uint32_t n, hw_registry_t *reg, hw_line_t *note)
{
    const hw_plat_t *p = h->p;
    volatile void *r = h->r;
    if (!port_stop(h, n)) {
        note_port(note, n, "engine will not stop");
        return P_FAIL;
    }
    ahci_port_t *pt = (ahci_port_t *) p->dma_alloc(p->ctx, sizeof(ahci_port_t), 64, 0);
    if (!pt) return P_ABORT;
    pt->h = h;
    pt->n = n;
    pt->cl = (uint8_t *) p->dma_alloc(p->ctx, 1024, 1024, &pt->cl_phys);
    pt->fis = (uint8_t *) p->dma_alloc(p->ctx, 256, 256, &pt->fis_phys);
    pt->ct = (uint8_t *) p->dma_alloc(p->ctx, 256, 128, &pt->ct_phys);
    if (!pt->cl || !pt->fis || !pt->ct) return P_ABORT;
    if (!dma_ok(h, pt->cl_phys, 1024) || !dma_ok(h, pt->fis_phys, 256) ||
        !dma_ok(h, pt->ct_phys, 256))
        return P_ABORT;
    hw_wr32(r, PX(n, PX_CLB), (uint32_t) pt->cl_phys);
    hw_wr32(r, PX(n, PX_CLBU), (uint32_t) (pt->cl_phys >> 32));
    hw_wr32(r, PX(n, PX_FB), (uint32_t) pt->fis_phys);
    hw_wr32(r, PX(n, PX_FBU), (uint32_t) (pt->fis_phys >> 32));
    hw_wr32(r, PX(n, PX_IE), 0);
    hw_wr32(r, PX(n, PX_SERR), 0xFFFFFFFFu);
    hw_wr32(r, PX(n, PX_IS), 0xFFFFFFFFu);
    uint32_t cmd = hw_rd32(r, PX(n, PX_CMD)) | CMD_FRE;
    if (h->cap & CAP_SSS) cmd |= CMD_SUD | CMD_POD; /* staggered spin-up: spin it up now */
    hw_wr32(r, PX(n, PX_CMD), cmd);

    /* Link: DET = 3. DET 1 (device seen, no PHY communication) gets one
     * COMRESET; DET 0 is an empty port. */
    if (!wait_after(p, r, PX(n, PX_SSTS), 0xF, 3, T_LINK)) {
        uint32_t det = hw_rd32(r, PX(n, PX_SSTS)) & 0xF;
        if (det == 0 || !port_comreset(h, n)) {
            port_stop(h, n);
            if (det != 0) note_port(note, n, "no link");
            return P_EMPTY;
        }
    }
    hw_wr32(r, PX(n, PX_SERR), 0xFFFFFFFFu); /* link-up diagnostics block the first FIS */
    if (!wait_after(p, r, PX(n, PX_TFD), TFD_BSY | TFD_DRQ, 0, T_BSY)) {
        port_stop(h, n);
        note_port(note, n, "device stays BSY");
        return P_FAIL;
    }
    uint32_t sig = hw_rd32(r, PX(n, PX_SIG));
    if (sig != SIG_ATA) {
        port_stop(h, n);
        if (sig == SIG_ATAPI)
            note_port(note, n, "ATAPI left alone");
        else if (sig == SIG_PM)
            note_port(note, n, "port multiplier left alone");
        else if (sig == SIG_SEMB)
            note_port(note, n, "enclosure bridge left alone");
        else {
            note_port(note, n, "signature ");
            hw_line_x(note, sig, 8);
            hw_line_s(note, " left alone");
        }
        return P_OTHER;
    }
    hw_wr32(r, PX(n, PX_IS), 0xFFFFFFFFu);
    hw_wr32(r, PX(n, PX_CMD), hw_rd32(r, PX(n, PX_CMD)) | CMD_ST);

    if (port_exec(pt, ATA_IDENTIFY, 0, 0, 512, 0) != 0) {
        port_stop(h, n);
        note_port(note, n, "IDENTIFY DEVICE failed");
        return P_FAIL;
    }
    const uint8_t *id = h->buf;
    if (!(idw(id, 49) & (1u << 9))) {
        port_stop(h, n);
        note_port(note, n, "disk without LBA");
        return P_HELD;
    }
    uint64_t sectors = idw(id, 60) | ((uint64_t) idw(id, 61) << 16);
    uint64_t s48 = idw(id, 100) | ((uint64_t) idw(id, 101) << 16) |
                   ((uint64_t) idw(id, 102) << 32) | ((uint64_t) idw(id, 103) << 48);
    pt->lba48 = (idw(id, 83) & (1u << 10)) && s48 != 0;
    if (pt->lba48) sectors = s48;
    uint32_t bs = 512;
    uint32_t w106 = idw(id, 106);
    if ((w106 & 0xC000u) == 0x4000u && (w106 & (1u << 12)))
        bs = (idw(id, 117) | (idw(id, 118) << 16)) * 2u;
    if (bs < 512 || bs > 4096 || (bs & (bs - 1))) {
        port_stop(h, n);
        note_port(note, n, "unsupported sector size ");
        hw_line_u(note, bs);
        return P_HELD;
    }
    hw_line_t model;
    hw_line_init(&model);
    note_model(&model, id);
    if (sectors == 0) {
        port_stop(h, n);
        note_port(note, n, model.b);
        hw_line_s(note, " has no media");
        return P_EMPTY;
    }
    hw_blk_t *b = &pt->blk;
    uint32_t num = g_sata_n, i = 0;
    b->name[i++] = 's';
    b->name[i++] = 'a';
    b->name[i++] = 't';
    b->name[i++] = 'a';
    if (num >= 10) b->name[i++] = (char) ('0' + (num / 10) % 10);
    b->name[i++] = (char) ('0' + num % 10);
    b->name[i] = 0;
    b->block_size = bs;
    b->blocks = sectors;
    b->drv = pt;
    b->read = ahci_read;
    b->write = ahci_write;
    hw_line_s(note, "; ");
    hw_line_s(note, b->name);
    hw_line_c(note, ' ');
    hw_line_s(note, model.b);
    hw_line_c(note, ' ');
    hw_line_u(note, sectors);
    hw_line_s(note, " x ");
    hw_line_u(note, bs);
    hw_line_s(note, " B");
    if (hw_reg_blk(reg, b) != 0) {
        hw_line_s(note, " (registry full)");
        return P_HELD;
    }
    g_sata_n++;
    return P_DISK;
}

static void note_hba(hw_line_t *note, const hw_pci_dev_t *d, uint32_t vs, uint32_t ports)
{
    static const struct {
        uint16_t vendor, device;
        const char *name;
    } known[] = {
        {0x8086, 0x2922, "ICH9 "},     {0x8086, 0x2829, "ICH8M "},    {0x8086, 0x3A22, "ICH10 "},
        {0x8086, 0x1C02, "6 Series "}, {0x8086, 0x8C02, "8 Series "}, {0x1022, 0x7801, "AMD "},
    };
    for (uint32_t i = 0; i < sizeof known / sizeof known[0]; i++)
        if (known[i].vendor == d->vendor && known[i].device == d->device)
            hw_line_s(note, known[i].name);
    hw_line_s(note, "AHCI ");
    hw_line_u(note, vs >> 16);
    hw_line_c(note, '.');
    hw_line_u(note, (vs >> 8) & 0xFF); /* 1.3.1 is 0x00010301 */
    if (vs & 0xFF) {
        hw_line_c(note, '.');
        hw_line_u(note, vs & 0xFF);
    }
    hw_line_c(note, ' ');
    hw_line_u(note, ports);
    hw_line_s(note, " ports");
}

hw_verdict_t ahci_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r, hw_line_t *note)
{
    volatile void *regs = hw_pci_map_bar(p, d, 5);
    if (!regs) {
        hw_line_s(note, "ABAR (BAR5) not assigned");
        return HW_N;
    }
    ahci_hba_t *h = (ahci_hba_t *) p->dma_alloc(p->ctx, sizeof(ahci_hba_t), 64, 0);
    if (!h) {
        hw_line_s(note, "out of DMA memory");
        return HW_F;
    }
    h->p = p;
    h->r = regs;
    hw_pci_enable(p, d);
    uint32_t cap = hw_rd32(regs, HBA_CAP), vs = hw_rd32(regs, HBA_VS);
    if (cap == 0xFFFFFFFFu) {
        hw_line_s(note, "registers read all-ones");
        return HW_F;
    }

    /* BIOS/OS handoff (10.6.3): request ownership, give firmware 25 ms, and
     * 2 s more while it reports itself busy. */
    if (hw_rd32(regs, HBA_CAP2) & CAP2_BOH) {
        uint32_t bohc = hw_rd32(regs, HBA_BOHC);
        hw_wr32(regs, HBA_BOHC, (bohc & (BOHC_BOS | BOHC_OOS)) | BOHC_OOS);
        if (!wait_after(p, regs, HBA_BOHC, BOHC_BOS, 0, 25000)) {
            if (hw_rd32(regs, HBA_BOHC) & BOHC_BB)
                hw_wait32(p, regs, HBA_BOHC, BOHC_BB, 0, 2000000);
            if (hw_rd32(regs, HBA_BOHC) & BOHC_BOS) hw_line_s(note, "firmware kept ownership; ");
        }
    }

    /* AHCI mode, then HBA reset (clears every port), then AHCI mode again. */
    hw_wr32(regs, HBA_GHC, hw_rd32(regs, HBA_GHC) | GHC_AE);
    hw_wr32(regs, HBA_GHC, GHC_AE | GHC_HR);
    if (!wait_after(p, regs, HBA_GHC, GHC_HR, 0, T_RESET)) {
        hw_line_s(note, "HBA reset timed out");
        return HW_F;
    }
    hw_wr32(regs, HBA_GHC, GHC_AE);
    if (!wait_after(p, regs, HBA_GHC, GHC_AE, GHC_AE, 1000)) {
        hw_line_s(note, "GHC.AE will not set");
        return HW_F;
    }
    h->cap = cap = hw_rd32(regs, HBA_CAP);
    uint32_t pi = hw_rd32(regs, HBA_PI);
    uint32_t ports = 0;
    for (uint32_t i = 0; i < 32; i++) ports += (pi >> i) & 1;
    note_hba(note, d, vs, ports);
    if (pi == 0 || (vs >> 16) == 0) {
        hw_line_s(note, ": no implemented ports or version 0");
        return HW_B;
    }
    h->buf = (uint8_t *) p->dma_alloc(p->ctx, BOUNCE, 4096, &h->buf_phys);
    if (!h->buf) {
        hw_line_s(note, "; out of DMA memory");
        return HW_F;
    }
    if (!dma_ok(h, h->buf_phys, BOUNCE)) {
        hw_line_s(note, "; DMA memory above 4 GB and no 64-bit addressing");
        return HW_F;
    }

    uint32_t disks = 0, fails = 0, held = 0;
    for (uint32_t n = 0; n < 32; n++) {
        if (!(pi & (1u << n))) continue;
        int s = port_init(h, n, r, note);
        if (s == P_ABORT) {
            hw_line_s(note, (h->cap & CAP_S64A) ? "; out of DMA memory"
                                                : "; out of DMA memory below 4 GB");
            return disks ? HW_T : HW_F;
        }
        disks += s == P_DISK;
        fails += s == P_FAIL;
        held += s == P_HELD;
    }
    if (disks) return HW_T;
    if (fails) return HW_F;
    if (held) return HW_B;
    hw_line_s(note, "; no disks");
    return HW_N;
}
