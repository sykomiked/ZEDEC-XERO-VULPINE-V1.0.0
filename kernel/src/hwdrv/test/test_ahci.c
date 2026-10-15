/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_ahci.c — ahci.c against a register-level fake AHCI HBA.
 *
 * The fake implements what the driver uses: GHC.HR/AE, BIOS/OS handoff,
 * per-port PxCMD ST/CR and FRE/FR, staggered spin-up (SUD), SStatus/SControl
 * (COMRESET), W1C PxIS/PxSERR, the first D2H FIS with the device signature,
 * command list slot 0 with a command table and PRDT, IDENTIFY DEVICE,
 * READ/WRITE DMA (EXT), task-file errors that halt the port until ST is
 * cleared, and command-list override. Each port's disk is a store for the low
 * LBAs plus a generated pattern above, so 28- and 48-bit addresses can be
 * checked without gigabytes of memory. Knobs break it every way the driver
 * must survive. */
#define _GNU_SOURCE /* MAP_32BIT */
/* Apple arm64 reserves the whole low 4 GB of a process (__PAGEZERO), so the
 * checks that need a buffer below 4 GB cannot run there; Linux runs them. */
#if defined(__APPLE__) && defined(__aarch64__)
#define NO_LOW_MEMORY 1
#else
#define NO_LOW_MEMORY 0
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "hwfake.h"

#define NP     8
#define STORE  64 /* sectors backed by memory per disk */
#define K_NONE 0
#define K_ATA  1
#define K_SIG  2 /* some other signature (ATAPI, PM, ...) */

#define CAP_S64A (1u << 31)
#define CAP_SSS  (1u << 27)
#define CAP_SCLO (1u << 24)

typedef struct fport {
    int kind;
    uint32_t sig;
    uint64_t sectors;
    uint32_t bs;
    int lba48, no_lba, raw_bs, zero_s48;
    const char *model;
    uint8_t *store;
    /* knobs */
    int bsy_forever, cr_stuck, fr_stuck, cmd_never, identify_fail, det1, det1_forever;
    int fail_data_n, err_no_tfes, bsy_after_error, host_err; /* host_err: IS bit to raise */
    uint32_t bsy_us, cr_lag_us;
    uint64_t bsy_until, st_off_at;
    /* state */
    uint32_t is, serr, pub_is, pub_serr;
    int cr, fr, fis_done, halted, comreset_on, linked;
    uint64_t fis_at;
    /* observations */
    uint32_t cmds, data_cmds, reads, writes, last_cmd, last_count, recoveries, clo, comresets;
    uint64_t last_lba;
    uint8_t last_dev;
    int st_without_fre, st_while_bsy, st_while_clo, st_while_cr, bad_layout, cmd_while_bsy;
    uint32_t prdbc_last;
} fport_t;

typedef struct fahci {
    uint8_t *r;
    fport_t port[NP];
    int hr_stuck, ae_stuck_off, boh, bos_hold, bb_us;
    int in_reset;
    uint64_t hr_at, oos_at;
    uint32_t resets;
} fahci_t;

static uint32_t rd(fahci_t *f, uint32_t o)
{
    return fk_rd32(f->r + o);
}
static void wr(fahci_t *f, uint32_t o, uint32_t v)
{
    fk_wr32(f->r + o, v);
}
static uint32_t PO(uint32_t n, uint32_t reg)
{
    return 0x100 + n * 0x80 + reg;
}

static uint8_t pat(uint64_t lba, uint32_t off)
{
    if (lba == 1 && off < 8) return (uint8_t) "EFI PART"[off];
    return (uint8_t) (lba * 13 + off * 7 + (lba >> 24) * 5 + (lba >> 40) * 3 + 1);
}

static uint8_t *sector_ptr(fport_t *p, uint64_t lba)
{
    return lba < STORE ? p->store + lba * p->bs : 0;
}

static void port_reset(fport_t *p)
{
    p->is = p->serr = 0;
    p->cr = p->cr_stuck; /* a hung engine survives the HBA reset */
    p->fr = p->fr_stuck;
    p->fis_done = p->halted = p->comreset_on = p->linked = 0;
    p->fis_at = 0;
    p->st_off_at = 0;
}

static void hba_reset(fahci_t *f)
{
    for (uint32_t n = 0; n < NP; n++) {
        port_reset(&f->port[n]);
        wr(f, PO(n, 0x18), 0);
        wr(f, PO(n, 0x10), 0);
        wr(f, PO(n, 0x30), 0);
        wr(f, PO(n, 0x38), 0);
        wr(f, PO(n, 0x20), 0x7F);
        wr(f, PO(n, 0x24), 0xFFFFFFFFu);
        f->port[n].pub_is = f->port[n].pub_serr = 0;
    }
    f->resets++;
}

static void put_id_str(uint8_t *id, uint32_t w0, uint32_t words, const char *s)
{
    char t[64];
    memset(t, ' ', sizeof t);
    memcpy(t, s, strlen(s) < 2 * words ? strlen(s) : 2 * words);
    for (uint32_t i = 0; i < words; i++) {
        id[2 * (w0 + i)] = (uint8_t) t[2 * i + 1];
        id[2 * (w0 + i) + 1] = (uint8_t) t[2 * i];
    }
}

static void put_w(uint8_t *id, uint32_t w, uint32_t v)
{
    id[2 * w] = (uint8_t) v;
    id[2 * w + 1] = (uint8_t) (v >> 8);
}

static void identify(fport_t *p, uint32_t n, uint8_t *id)
{
    memset(id, 0, 512);
    put_w(id, 0, 0x0040);
    char model[48];
    snprintf(model, sizeof model, "ZXV FAKE DISK %u", n);
    put_id_str(id, 27, 20, p->model ? p->model : model);
    put_id_str(id, 10, 10, "SN0001");
    if (!p->no_lba) put_w(id, 49, 1u << 9);
    uint64_t s28 = p->sectors > 0x0FFFFFFFu ? 0x0FFFFFFFu : p->sectors;
    put_w(id, 60, (uint32_t) s28);
    put_w(id, 61, (uint32_t) (s28 >> 16));
    put_w(id, 83, (1u << 14) | (p->lba48 ? (1u << 10) : 0));
    put_w(id, 86, p->lba48 ? (1u << 10) : 0);
    if (p->lba48 && !p->zero_s48)
        for (uint32_t i = 0; i < 4; i++) put_w(id, 100 + i, (uint32_t) (p->sectors >> (16 * i)));
    uint32_t bs = p->raw_bs ? (uint32_t) p->raw_bs : p->bs;
    if (bs != 512) {
        put_w(id, 106, 0x4000 | (1u << 12));
        put_w(id, 117, (bs / 2) & 0xFFFF);
        put_w(id, 118, (bs / 2) >> 16);
    } else {
        put_w(id, 106, 0x4000);
    }
}

/* Scatter/gather between the PRDT and a linear buffer. */
static uint32_t prd_total(const uint8_t *ct, uint32_t prdtl)
{
    uint32_t t = 0;
    for (uint32_t i = 0; i < prdtl; i++) t += (fk_rd32(ct + 0x80 + 16 * i + 12) & 0x3FFFFF) + 1;
    return t;
}

static void prd_copy(const uint8_t *ct, uint32_t prdtl, uint8_t *lin, uint32_t bytes, int to_mem)
{
    uint32_t done = 0;
    for (uint32_t i = 0; i < prdtl && done < bytes; i++) {
        const uint8_t *e = ct + 0x80 + 16 * i;
        uint8_t *m = (uint8_t *) fk_ptr(fk_rd64(e));
        uint32_t len = (fk_rd32(e + 12) & 0x3FFFFF) + 1;
        if (len > bytes - done) len = bytes - done;
        if (to_mem)
            memcpy(m, lin + done, len);
        else
            memcpy(lin + done, m, len);
        done += len;
    }
}

static void fail_cmd(fport_t *p, fahci_t *f, uint32_t n, uint32_t err)
{
    if (p->host_err) { /* host bus / interface fatal error: TFD untouched */
        p->is |= 1u << p->host_err;
        p->halted = 1;
        return;
    }
    if (p->err_no_tfes) { /* completes with ERR in the task file, no TFES */
        wr(f, PO(n, 0x20), 0x51 | (err << 8));
        wr(f, PO(n, 0x38), rd(f, PO(n, 0x38)) & ~1u);
        return;
    }
    wr(f, PO(n, 0x20), (p->bsy_after_error ? 0xD1 : 0x51) | (err << 8));
    p->is |= 1u << 30;   /* TFES */
    p->serr |= 1u << 11; /* ERR.E */
    p->halted = 1;
}

static void exec_slot0(fahci_t *f, uint32_t n)
{
    fport_t *p = &f->port[n];
    uint64_t clb = fk_rd64(f->r + PO(n, 0x00));
    uint8_t *hdr = (uint8_t *) fk_ptr(clb);
    uint32_t dw0 = fk_rd32(hdr), cfl = dw0 & 0x1F, w = (dw0 >> 6) & 1, prdtl = dw0 >> 16;
    uint64_t ctba = fk_rd64(hdr + 8);
    p->cmds++;
    if (rd(f, PO(n, 0x20)) & 0x88) p->cmd_while_bsy = 1;
    if (fk_rd32(hdr + 4) != 0) p->bad_layout = 1; /* PRDBC starts at 0 */
    if (clb % 1024 || ctba % 128 || cfl != 5 || prdtl == 0 || !ctba) {
        p->bad_layout = 1;
        fail_cmd(p, f, n, 0x04);
        return;
    }
    const uint8_t *ct = (const uint8_t *) fk_ptr(ctba), *fis = ct;
    if (fis[0] != 0x27 || !(fis[1] & 0x80)) {
        p->bad_layout = 1;
        fail_cmd(p, f, n, 0x04);
        return;
    }
    for (uint32_t i = 0; i < prdtl; i++) {
        const uint8_t *e = ct + 0x80 + 16 * i;
        if ((fk_rd64(e) & 1) || !(fk_rd32(e + 12) & 1)) { /* word aligned, even count */
            p->bad_layout = 1;
            fail_cmd(p, f, n, 0x04);
            return;
        }
    }
    uint32_t cmd = fis[2], total = prd_total(ct, prdtl);
    p->last_cmd = cmd;
    p->last_dev = fis[7];
    static uint8_t lin[1 << 20];
    if (cmd == 0xEC) {
        if (p->kind != K_ATA || p->identify_fail || w || total < 512) {
            fail_cmd(p, f, n, 0x04);
            return;
        }
        identify(p, n, lin);
        prd_copy(ct, prdtl, lin, 512, 1);
        p->prdbc_last = 512;
    } else if (cmd == 0x25 || cmd == 0x35 || cmd == 0xC8 || cmd == 0xCA) {
        int ext = cmd == 0x25 || cmd == 0x35, wr_cmd = cmd == 0x35 || cmd == 0xCA;
        uint64_t lba;
        uint32_t count;
        if (ext) {
            lba = (uint64_t) fis[4] | ((uint64_t) fis[5] << 8) | ((uint64_t) fis[6] << 16) |
                  ((uint64_t) fis[8] << 24) | ((uint64_t) fis[9] << 32) |
                  ((uint64_t) fis[10] << 40);
            count = fis[12] | ((uint32_t) fis[13] << 8);
            if (!count) count = 65536;
        } else {
            lba = (uint64_t) fis[4] | ((uint64_t) fis[5] << 8) | ((uint64_t) fis[6] << 16) |
                  ((uint64_t) (fis[7] & 0xF) << 24);
            count = fis[12] ? fis[12] : 256;
            if (fis[8] | fis[9] | fis[10] | fis[13]) p->bad_layout = 1;
        }
        p->data_cmds++;
        p->last_lba = lba;
        p->last_count = count;
        if ((ext && !p->lba48) || !(fis[7] & 0x40) || (uint32_t) w != (uint32_t) wr_cmd ||
            total != count * p->bs || total > sizeof lin) {
            fail_cmd(p, f, n, 0x04);
            return;
        }
        if (lba + count > p->sectors) {
            fail_cmd(p, f, n, 0x10); /* IDNF */
            return;
        }
        if (p->fail_data_n && p->data_cmds == (uint32_t) p->fail_data_n) {
            fail_cmd(p, f, n, 0x40); /* UNC */
            return;
        }
        if (wr_cmd) {
            prd_copy(ct, prdtl, lin, total, 0);
            for (uint32_t s = 0; s < count; s++) {
                uint8_t *d = sector_ptr(p, lba + s);
                if (d) memcpy(d, lin + s * p->bs, p->bs);
            }
            p->writes++;
        } else {
            for (uint32_t s = 0; s < count; s++) {
                uint8_t *d = sector_ptr(p, lba + s);
                for (uint32_t i = 0; i < p->bs; i++)
                    lin[s * p->bs + i] = d ? d[i] : pat(lba + s, i);
            }
            prd_copy(ct, prdtl, lin, total, 1);
            p->reads++;
        }
        p->prdbc_last = total;
    } else {
        fail_cmd(p, f, n, 0x04);
        return;
    }
    fk_wr32(hdr + 4, p->prdbc_last);
    wr(f, PO(n, 0x20), 0x50);
    p->is |= 1u; /* DHRS */
    wr(f, PO(n, 0x38), rd(f, PO(n, 0x38)) & ~1u);
}

static void port_tick(fahci_t *f, uint32_t n, uint64_t now, int sss)
{
    fport_t *p = &f->port[n];
    uint32_t m;
    /* W1C: whatever the driver wrote over the published value clears bits */
    if ((m = rd(f, PO(n, 0x10))) != p->pub_is) p->is &= ~m;
    if ((m = rd(f, PO(n, 0x30))) != p->pub_serr) p->serr &= ~m;
    uint32_t cmd = rd(f, PO(n, 0x18));
    /* COMRESET through SControl.DET */
    uint32_t sdet = rd(f, PO(n, 0x2C)) & 0xF;
    if (sdet == 1 && !p->comreset_on) {
        p->comreset_on = 1;
        p->comresets++;
    } else if (sdet == 0 && p->comreset_on) {
        p->comreset_on = 0;
        p->linked = 0;
        p->fis_done = 0;
        p->fis_at = 0;
        p->halted = 0;
        if (!p->det1_forever) p->det1 = 0;
        wr(f, PO(n, 0x20), 0x80);
    }
    uint32_t det = 0;
    if (p->kind != K_NONE && (!sss || (cmd & 2)) && !p->comreset_on)
        det = (p->det1 || p->det1_forever) ? 1 : 3;
    if (det == 3 && !p->linked) {
        p->linked = 1;
        p->serr |= 1u << 26; /* DIAG.X: holds the first FIS until cleared */
    }
    wr(f, PO(n, 0x28), det ? (det | 0x110) : 0);
    /* FIS receive and the first D2H register FIS */
    if (cmd & 0x10) {
        uint64_t fb = fk_rd64(f->r + PO(n, 0x08));
        if (!fb || fb % 256) p->bad_layout = 1;
        p->fr = 1;
        if (det == 3 && !p->fis_done && !(p->serr & (1u << 26))) {
            if (!p->fis_at) p->fis_at = now + p->bsy_us + 20;
            if (!p->bsy_forever && now >= p->fis_at) {
                p->fis_done = 1;
                wr(f, PO(n, 0x24), p->sig);
                wr(f, PO(n, 0x20), p->kind == K_ATA ? 0x50 : 0x00);
                if (fb && fb % 256 == 0) ((uint8_t *) fk_ptr(fb))[0x40] = 0x34;
            } else {
                wr(f, PO(n, 0x20), 0x80);
            }
        }
    } else if (!p->fr_stuck) {
        p->fr = 0;
    }
    if (p->bsy_until) { /* busy for a while, then ready again */
        wr(f, PO(n, 0x20), now < p->bsy_until ? 0x80 : 0x50);
        if (now >= p->bsy_until) p->bsy_until = 0;
    }
    /* command list override */
    if ((cmd & 0x9) == 0x9 && !p->cr) p->st_while_clo = 1;
    if (cmd & 0x8) {
        p->clo++;
        wr(f, PO(n, 0x20), rd(f, PO(n, 0x20)) & ~0x88u);
        cmd &= ~0x8u;
    }
    if (cmd & 1) {
        if (p->st_off_at) p->st_while_cr = 1; /* restarted before CR dropped */
        if (!p->cr) {
            if (!(cmd & 0x10)) p->st_without_fre = 1;
            if (rd(f, PO(n, 0x20)) & 0x88) p->st_while_bsy = 1;
        }
        p->cr = 1;
        p->st_off_at = 0;
    } else if (p->cr) {
        if (p->halted) p->recoveries++;
        p->halted = 0;
        wr(f, PO(n, 0x38), 0);
        if (!p->st_off_at) p->st_off_at = now;
        if (!p->cr_stuck && now >= p->st_off_at + p->cr_lag_us) {
            p->cr = 0;
            p->st_off_at = 0;
        }
    }
    cmd = (cmd & ~0xC000u) | (p->cr ? 0x8000u : 0) | (p->fr ? 0x4000u : 0);
    wr(f, PO(n, 0x18), cmd);
    if ((cmd & 1) && p->cr && !p->halted && !p->cmd_never && (rd(f, PO(n, 0x38)) & 1))
        exec_slot0(f, n);
    wr(f, PO(n, 0x10), p->is);
    p->pub_is = p->is;
    wr(f, PO(n, 0x30), p->serr);
    p->pub_serr = p->serr;
}

static void tick(fake_dev_t *d, uint64_t now)
{
    fahci_t *f = (fahci_t *) d->state;
    uint32_t ghc = rd(f, 0x04);
    if (ghc & 1) {
        if (!f->in_reset) {
            f->in_reset = 1;
            f->hr_at = now + 100;
        }
        if (!f->hr_stuck && now >= f->hr_at) {
            hba_reset(f);
            f->in_reset = 0;
            ghc = 0; /* HR clears AE too (CAP.SAM is 0) */
        }
    }
    if (f->ae_stuck_off) ghc &= ~(1u << 31);
    wr(f, 0x04, ghc);
    if (f->boh) {
        uint32_t b = rd(f, 0x28);
        if (b & 2) { /* OS requested ownership */
            if (!f->oos_at) f->oos_at = now + 500;
            if (f->bb_us && now < f->oos_at + (uint64_t) f->bb_us)
                b |= 0x10;
            else
                b &= ~0x10u;
            if (!f->bos_hold && now >= f->oos_at + (uint64_t) f->bb_us) b &= ~1u;
        }
        wr(f, 0x28, b);
    }
    int sss = (rd(f, 0x00) & CAP_SSS) != 0;
    if (f->in_reset) return;
    for (uint32_t n = 0; n < NP; n++)
        if (rd(f, 0x0C) & (1u << n)) port_tick(f, n, now, sss);
}

static fahci_t *g_last;

static fahci_t *add_ahci(fake_plat_t *fp, uint32_t bdf, uint32_t nports)
{
    fake_dev_t *d = fake_dev_new(fp, bdf, 0x8086, 0x2922, 0x01, 0x06, 0x01);
    fahci_t *f = (fahci_t *) calloc(1, sizeof *f);
    f->r = fake_dev_bar(d, 5, 0x2000);
    wr(f, 0x00, CAP_S64A | CAP_SCLO | (31u << 8) | (nports - 1));
    wr(f, 0x0C, (1u << nports) - 1);
    wr(f, 0x10, 0x00010000);
    for (uint32_t n = 0; n < NP; n++) {
        wr(f, PO(n, 0x20), 0x7F);
        wr(f, PO(n, 0x24), 0xFFFFFFFFu);
        wr(f, PO(n, 0x14), 0x7DC0007Fu); /* firmware left interrupts enabled */
        wr(f, PO(n, 0x2C), 0x300);       /* SControl: IPM transitions disabled */
        f->port[n].bs = 512;
    }
    d->state = f;
    d->tick = tick;
    g_last = f;
    return f;
}

static void add_disk(fahci_t *f, uint32_t n, uint64_t sectors, uint32_t bs, int lba48)
{
    fport_t *p = &f->port[n];
    p->kind = K_ATA;
    p->sig = 0x00000101;
    p->sectors = sectors;
    p->bs = bs;
    p->lba48 = lba48;
    p->store = (uint8_t *) calloc(STORE, bs);
    for (uint64_t s = 0; s < STORE; s++)
        for (uint32_t i = 0; i < bs; i++) p->store[s * bs + i] = pat(s, i);
}

static void add_other(fahci_t *f, uint32_t n, uint32_t sig)
{
    f->port[n].kind = K_SIG;
    f->port[n].sig = sig;
}

static void free_ahci(fahci_t *f)
{
    for (uint32_t n = 0; n < NP; n++) free(f->port[n].store);
    free(f);
}

static hw_verdict_t bind_dev(fake_plat_t *fp, uint32_t idx, hw_registry_t *r, hw_line_t *note)
{
    hw_pci_dev_t devs[4];
    uint32_t n = hw_pci_scan(&fp->plat, devs, 4);
    CHECK(n > idx);
    /* BAR5 is the last BAR, so the scan can only see 32 bits of it; the fake's
     * register memory lives above 4 GB on the host. */
    if (fp->devs[idx]->bar_mem[5])
        devs[idx].bar[5] = (uint64_t) (uintptr_t) fp->devs[idx]->bar_mem[5];
    hw_line_init(note);
    return ahci_bind(&fp->plat, &devs[idx], r, note);
}

static hw_verdict_t bind_one(fake_plat_t *fp, hw_registry_t *r, hw_line_t *note)
{
    return bind_dev(fp, 0, r, note);
}

/* 00:1f.0, an ISA bridge marked multi-function, so 00:1f.2 is scanned. */
static void add_lpc(fake_plat_t *fp)
{
    fake_dev_t *d = fake_dev_new(fp, HW_BDF(0, 0x1f, 0), 0x8086, 0x2918, 0x06, 0x01, 0x00);
    d->cfg[3] |= 0x00800000u;
}

static int name_num(const hw_blk_t *b)
{
    if (strncmp(b->name, "sata", 4) != 0) return -1;
    return atoi(b->name + 4);
}

static void test_happy_path(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    add_lpc(&fp);
    fahci_t *f = add_ahci(&fp, HW_BDF(0, 0x1f, 2), 6);
    add_disk(f, 0, 2048, 512, 1);
    add_other(f, 2, 0xEB140101);
    add_disk(f, 3, 512, 4096, 1);
    add_disk(f, 5, 0x0FFFFFFF, 512, 0); /* LBA28 only, 128 GB */
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_dev(&fp, 1, &r, &note) == HW_T);
    CHECK(strstr(note.b, "ICH9 AHCI 1.0 6 ports") == note.b);
    CHECK(strstr(note.b, "ZXV FAKE DISK 0 2048 x 512 B") != 0);
    CHECK(strstr(note.b, "ZXV FAKE DISK 3 512 x 4096 B") != 0);
    CHECK(strstr(note.b, "ZXV FAKE DISK 5 268435455 x 512 B") != 0);
    CHECK(strstr(note.b, "port 2 ATAPI left alone") != 0);
    CHECK(strstr(note.b, "port 1") == 0 && strstr(note.b, "port 4") == 0);
    CHECK(strstr(note.b, "no disks") == 0);
    CHECK(r.n_blk == 3);
    int n0 = name_num(r.blk[0]);
    CHECK(n0 >= 0);
    CHECK(name_num(r.blk[1]) == n0 + 1 && name_num(r.blk[2]) == n0 + 2);
    CHECK(strstr(note.b, r.blk[0]->name) != 0);
    /* controller state */
    CHECK(f->resets == 1);
    CHECK(rd(f, 0x04) & (1u << 31));
    CHECK((fp.devs[1]->cfg[1] & 0x0406) == 0x0406);
    CHECK((fp.devs[0]->cfg[1] & 0x0006) == 0);
    for (uint32_t n = 0; n < 6; n++) {
        uint32_t cmd = rd(f, PO(n, 0x18));
        int disk = n == 0 || n == 3 || n == 5;
        CHECK(((cmd & 0x11) == 0x11) == disk);
        CHECK((cmd & 0xC000) == (disk ? 0xC000u : 0));
        CHECK(!f->port[n].bad_layout && !f->port[n].st_without_fre && !f->port[n].st_while_bsy);
        if (n == 1 || n == 4) continue;
        CHECK(rd(f, PO(n, 0x00)) % 1024 == 0 && rd(f, PO(n, 0x00)) != 0);
        CHECK(rd(f, PO(n, 0x08)) % 256 == 0);
        CHECK(fk_rd64(f->r + PO(n, 0x00)) != fk_rd64(f->r + PO(n, 0x08)));
        CHECK(rd(f, PO(n, 0x14)) == 0); /* PxIE */
    }
    CHECK(f->port[2].cmds == 0); /* ATAPI: no command ever issued */
    CHECK(f->port[0].last_cmd == 0xEC && f->port[0].cmds == 1);

    hw_blk_t *b = r.blk[0];
    CHECK(b->block_size == 512 && b->blocks == 2048);
    uint8_t buf[48 * 512];
    CHECK(b->read(b, 1, 1, buf) == 0);
    CHECK(memcmp(buf, "EFI PART", 8) == 0);
    CHECK(f->port[0].last_cmd == 0x25 && f->port[0].last_lba == 1 && f->port[0].last_count == 1);
    CHECK(f->port[0].last_dev == 0x40);
    CHECK(fk_rd32(fk_ptr(fk_rd64(f->r + PO(0, 0x00))) + 4) == 512); /* PRDBC */
    /* 40 sectors = 32 (16 KB bounce) + 8 */
    uint32_t r0 = f->port[0].reads;
    CHECK(b->read(b, 10, 40, buf) == 0);
    CHECK(f->port[0].reads - r0 == 2);
    CHECK(f->port[0].last_lba == 42 && f->port[0].last_count == 8);
    CHECK(memcmp(buf, f->port[0].store + 10 * 512, 40 * 512) == 0);
    CHECK(b->read(b, 3, 3, buf + 1) == 0);
    CHECK(memcmp(buf + 1, f->port[0].store + 3 * 512, 3 * 512) == 0);
    /* reads past the memory store come from the pattern */
    CHECK(b->read(b, 2000, 2, buf) == 0);
    CHECK(buf[0] == pat(2000, 0) && buf[511] == pat(2000, 511) && buf[512] == pat(2001, 0));
    uint8_t w[9 * 512];
    for (uint32_t i = 0; i < sizeof w; i++) w[i] = (uint8_t) (0x5A ^ (i * 3));
    CHECK(b->write(b, 40, 9, w) == 0);
    CHECK(f->port[0].last_cmd == 0x35 && f->port[0].writes == 1);
    CHECK(memcmp(f->port[0].store + 40 * 512, w, sizeof w) == 0);
    memset(buf, 0, sizeof buf);
    CHECK(b->read(b, 40, 9, buf) == 0);
    CHECK(memcmp(buf, w, sizeof w) == 0);
    /* bounds: refused before the device sees anything */
    uint32_t cmds = f->port[0].cmds;
    CHECK(b->read(b, 2048, 1, buf) != 0);
    CHECK(b->read(b, 2047, 2, buf) != 0);
    CHECK(b->read(b, 0, 0, buf) != 0);
    CHECK(b->write(b, ~0ULL, 1, buf) != 0);
    CHECK(b->write(b, 2040, 9, buf) != 0);
    CHECK(f->port[0].cmds == cmds);
    CHECK(b->read(b, 2047, 1, buf) == 0);
    for (int i = 0; i < 50; i++) CHECK(b->read(b, (uint64_t) (i * 37 % 2048), 1, buf) == 0);
    CHECK(buf[5] == pat(49 * 37 % 2048, 5) || (49 * 37 % 2048) < STORE);
    CHECK(f->port[0].recoveries == 0 && rd(f, PO(0, 0x30)) == 0);

    /* 4 KB logical sectors: four per bounce buffer */
    hw_blk_t *b3 = r.blk[1];
    CHECK(b3->block_size == 4096 && b3->blocks == 512);
    static uint8_t big[6 * 4096];
    uint32_t r3 = f->port[3].reads;
    CHECK(b3->read(b3, 1, 6, big) == 0);
    CHECK(f->port[3].reads - r3 == 2);
    CHECK(f->port[3].last_lba == 5 && f->port[3].last_count == 2);
    CHECK(memcmp(big, f->port[3].store + 4096, 6 * 4096) == 0);
    CHECK(memcmp(big, "EFI PART", 8) == 0);
    memset(big, 0x77, 4096);
    CHECK(b3->write(b3, 7, 1, big) == 0);
    CHECK(f->port[3].store[7 * 4096] == 0x77 && f->port[3].store[8 * 4096 - 1] == 0x77);

    /* 28-bit disk: READ/WRITE DMA, LBA bits 24..27 in the device byte */
    hw_blk_t *b5 = r.blk[2];
    CHECK(b5->blocks == 0x0FFFFFFF);
    CHECK(b5->read(b5, 0x0ABCDEF1, 2, buf) == 0);
    CHECK(f->port[5].last_cmd == 0xC8);
    CHECK(f->port[5].last_lba == 0x0ABCDEF1 && f->port[5].last_count == 2);
    CHECK(f->port[5].last_dev == 0x4A);
    CHECK(buf[0] == pat(0x0ABCDEF1, 0) && buf[512 + 9] == pat(0x0ABCDEF2, 9));
    CHECK(b5->read(b5, 0x0FFFFFFE, 1, buf) == 0);
    CHECK(f->port[5].last_dev == 0x4F);
    memset(buf, 0x31, 512);
    CHECK(b5->write(b5, 2, 1, buf) == 0);
    CHECK(f->port[5].last_cmd == 0xCA && f->port[5].store[2 * 512 + 100] == 0x31);
    /* 32 sectors in one LBA28 command (count byte 32) */
    CHECK(b5->read(b5, 0, 32, buf) == 0);
    CHECK(f->port[5].last_count == 32);
    fake_plat_free(&fp);
    free_ahci(f);
}

static void test_lba48_high(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fahci_t *f = add_ahci(&fp, HW_BDF(0, 2, 0), 1);
    add_disk(f, 0, 1ULL << 41, 512, 1); /* 1 PB: every LBA byte in use */
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_one(&fp, &r, &note) == HW_T);
    CHECK(strstr(note.b, "2199023255552 x 512 B") != 0);
    CHECK(strstr(note.b, "AHCI 1.0 1 ports") != 0);
    hw_blk_t *b = r.blk[0];
    uint8_t buf[512];
    uint64_t lba = 0x01A2B3C4D5E6ULL;
    CHECK(b->read(b, lba, 1, buf) == 0);
    CHECK(f->port[0].last_lba == lba);
    CHECK(buf[0] == pat(lba, 0) && buf[300] == pat(lba, 300));
    CHECK(b->read(b, (1ULL << 41) - 1, 1, buf) == 0);
    CHECK(b->read(b, 1ULL << 41, 1, buf) != 0);
    fake_plat_free(&fp);
    free_ahci(f);
}

static void test_errors_and_recovery(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fahci_t *f = add_ahci(&fp, HW_BDF(0, 2, 0), 1);
    add_disk(f, 0, 256, 512, 1);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_one(&fp, &r, &note) == HW_T);
    hw_blk_t *b = r.blk[0];
    fport_t *p = &f->port[0];
    uint8_t buf[512];

    /* TFES on a read: -1 at once, the port is stopped (CR given time to
     * drop), SERR/IS cleared and restarted */
    p->fail_data_n = (int) p->data_cmds + 1;
    p->cr_lag_us = 300;
    uint64_t t0 = fp.now_us;
    CHECK(b->read(b, 5, 1, buf) == -1);
    CHECK(fp.now_us - t0 < 5000);
    CHECK(p->recoveries == 1);
    CHECK(!p->st_while_cr);
    p->cr_lag_us = 0;
    fp.plat.delay_us(fp.plat.ctx, 10);
    CHECK((rd(f, PO(0, 0x18)) & 0x8011) == 0x8011);
    CHECK(rd(f, PO(0, 0x10)) == 0 && rd(f, PO(0, 0x30)) == 0);
    CHECK(p->clo == 0 && p->comresets == 0);
    CHECK(b->read(b, 5, 1, buf) == 0);
    CHECK(buf[0] == pat(5, 0) && buf[511] == pat(5, 511));
    /* a failure in the second chunk of a multi-command read */
    static uint8_t big[64 * 512];
    p->fail_data_n = (int) p->data_cmds + 2;
    CHECK(b->read(b, 0, 64, big) == -1);
    CHECK(p->recoveries == 2);
    CHECK(b->read(b, 0, 64, big) == 0);
    CHECK(memcmp(big, p->store, 64 * 512) == 0);
    /* write error */
    p->fail_data_n = (int) p->data_cmds + 1;
    CHECK(b->write(b, 9, 1, buf) == -1);
    CHECK(p->recoveries == 3);

    /* host bus fatal, host bus data, interface fatal: fail at once, recover */
    for (int bit = 27; bit <= 29; bit++) {
        p->host_err = bit;
        p->fail_data_n = (int) p->data_cmds + 1;
        uint32_t rec = p->recoveries;
        t0 = fp.now_us;
        CHECK(b->read(b, 4, 1, buf) == -1);
        CHECK(fp.now_us - t0 < 5000 && p->recoveries == rec + 1);
        p->host_err = 0;
        CHECK(b->read(b, 4, 1, buf) == 0 && buf[0] == pat(4, 0));
    }

    /* ERR in the task file without TFES (CI cleared): still an error */
    p->err_no_tfes = 1;
    p->fail_data_n = (int) p->data_cmds + 1;
    CHECK(b->read(b, 6, 1, buf) == -1);
    p->err_no_tfes = 0;
    CHECK(b->read(b, 6, 1, buf) == 0 && buf[0] == pat(6, 0));

    /* the device stays BSY after the error: command list override */
    p->bsy_after_error = 1;
    p->fail_data_n = (int) p->data_cmds + 1;
    CHECK(b->read(b, 7, 1, buf) == -1);
    CHECK(p->clo == 1);
    p->bsy_after_error = 0;
    CHECK(b->read(b, 7, 1, buf) == 0 && buf[0] == pat(7, 0));
    CHECK(!p->st_while_bsy);

    /* a command that never completes times out and the port recovers */
    p->cmd_never = 1;
    t0 = fp.now_us;
    CHECK(b->read(b, 8, 1, buf) == -1);
    CHECK(fp.now_us - t0 >= 2000000 && fp.now_us - t0 < 3000000);
    p->cmd_never = 0;
    CHECK(b->read(b, 8, 1, buf) == 0 && buf[0] == pat(8, 0));
    /* a range the device refuses (IDNF) */
    p->sectors = 100; /* the device shrank: the driver still believes 256 */
    CHECK(b->read(b, 150, 1, buf) == -1);
    p->sectors = 256;
    CHECK(b->read(b, 150, 1, buf) == 0);
    /* the device goes BSY between commands: wait it out, never issue into it */
    p->bsy_until = fp.now_us + 500;
    wr(f, PO(0, 0x20), 0x80);
    uint32_t clo0 = p->clo, rec0 = p->recoveries;
    CHECK(b->read(b, 151, 1, buf) == 0 && buf[0] == pat(151, 0));
    CHECK(!p->cmd_while_bsy && p->clo == clo0 && p->recoveries == rec0);
    CHECK(p->bsy_until == 0);
    /* stuck BSY past the command timeout: the port is recovered (CLO) and
     * the command still goes through */
    p->bsy_until = fp.now_us + 2100000;
    wr(f, PO(0, 0x20), 0x80);
    CHECK(b->read(b, 152, 1, buf) == 0 && buf[0] == pat(152, 0));
    CHECK(p->clo == clo0 + 1 && !p->cmd_while_bsy);
    CHECK(!p->bad_layout && !p->st_while_clo && !p->st_while_cr);
    fake_plat_free(&fp);
    free_ahci(f);

    /* without SCLO, a port stuck BSY after an error gets a COMRESET */
    fake_plat_init(&fp);
    f = add_ahci(&fp, HW_BDF(0, 2, 0), 1);
    wr(f, 0x00, rd(f, 0x00) & ~CAP_SCLO);
    add_disk(f, 0, 256, 512, 1);
    memset(&r, 0, sizeof r);
    CHECK(bind_one(&fp, &r, &note) == HW_T);
    b = r.blk[0];
    p = &f->port[0];
    p->bsy_after_error = 1;
    p->fail_data_n = (int) p->data_cmds + 1;
    CHECK(b->read(b, 7, 1, buf) == -1);
    CHECK(p->clo == 0 && p->comresets == 1);
    CHECK(!p->st_while_bsy);
    CHECK(rd(f, PO(0, 0x2C)) == 0x300);
    p->bsy_after_error = 0;
    CHECK(b->read(b, 7, 1, buf) == 0 && buf[0] == pat(7, 0));
    fake_plat_free(&fp);
    free_ahci(f);
}

typedef struct {
    const char *what;
    void (*setup)(fahci_t *f);
    hw_verdict_t want;
    const char *note;
    uint32_t disks;
} fault_t;

static void s_disk(fahci_t *f)
{
    add_disk(f, 0, 128, 512, 1);
}
static void k_hr(fahci_t *f)
{
    s_disk(f);
    f->hr_stuck = 1;
}
static void k_ae(fahci_t *f)
{
    s_disk(f);
    f->ae_stuck_off = 1;
}
static void k_ones(fahci_t *f)
{
    s_disk(f);
    memset(f->r, 0xFF, 0x20);
}
static void k_pi0(fahci_t *f)
{
    s_disk(f);
    wr(f, 0x0C, 0);
}
static void k_vs0(fahci_t *f)
{
    s_disk(f);
    wr(f, 0x10, 0);
}
static void k_empty(fahci_t *f)
{
    (void) f;
}
static void k_bsy(fahci_t *f)
{
    s_disk(f);
    f->port[0].bsy_forever = 1;
}
static void k_atapi(fahci_t *f)
{
    add_other(f, 0, 0xEB140101);
}
static void k_pm(fahci_t *f)
{
    add_other(f, 0, 0x96690101);
}
static void k_semb(fahci_t *f)
{
    add_other(f, 0, 0xC33C0101);
}
static void k_sig(fahci_t *f)
{
    add_other(f, 0, 0x12345678);
}
static void k_ident(fahci_t *f)
{
    s_disk(f);
    f->port[0].identify_fail = 1;
}
static void k_nolba(fahci_t *f)
{
    s_disk(f);
    f->port[0].no_lba = 1;
}
static void k_bs520(fahci_t *f)
{
    s_disk(f);
    f->port[0].raw_bs = 520;
}
static void k_bs8k(fahci_t *f)
{
    s_disk(f);
    f->port[0].raw_bs = 8192;
}
static void k_bs256(fahci_t *f)
{
    s_disk(f);
    f->port[0].raw_bs = 256;
}
static void k_zero(fahci_t *f)
{
    add_disk(f, 0, 0, 512, 1);
}
static void k_cr(fahci_t *f)
{
    s_disk(f);
    f->port[0].cr_stuck = 1;
    f->port[0].cr = 1;
}
static void k_fr(fahci_t *f)
{
    s_disk(f);
    f->port[0].fr_stuck = 1;
    f->port[0].fr = 1;
}
static void k_det1(fahci_t *f)
{
    s_disk(f);
    f->port[0].det1 = 1;
}
static void k_det1x(fahci_t *f)
{
    s_disk(f);
    f->port[0].det1_forever = 1;
}
static void k_sss(fahci_t *f)
{
    s_disk(f);
    wr(f, 0x00, rd(f, 0x00) | CAP_SSS);
}
static void k_slow(fahci_t *f)
{
    s_disk(f);
    f->port[0].bsy_us = 2500000;
}
static void k_boh(fahci_t *f)
{
    s_disk(f);
    wr(f, 0x24, 1);
    wr(f, 0x28, 1);
    f->boh = 1;
}
static void k_bohbb(fahci_t *f)
{
    k_boh(f);
    f->bb_us = 100000;
}
static void k_bohx(fahci_t *f)
{
    k_boh(f);
    f->bos_hold = 1;
}
static void k_two(fahci_t *f)
{
    wr(f, 0x0C, 3);
    wr(f, 0x00, (rd(f, 0x00) & ~0x1Fu) | 1);
    f->port[0].kind = K_ATA;
    add_disk(f, 0, 64, 512, 1);
    f->port[0].bsy_forever = 1;
    add_disk(f, 1, 96, 512, 1);
}
static void k_sparse(fahci_t *f) /* PI bits 1 and 6 of 8 */
{
    wr(f, 0x0C, 0x42);
    wr(f, 0x00, (rd(f, 0x00) & ~0x1Fu) | 7);
    add_disk(f, 6, 64, 512, 1);
}

static void test_faults(void)
{
    static const fault_t faults[] = {
        {"HBA reset stuck", k_hr, HW_F, "HBA reset timed out", 0},
        {"AE will not set", k_ae, HW_F, "GHC.AE will not set", 0},
        {"all-ones registers", k_ones, HW_F, "all-ones", 0},
        {"no implemented ports", k_pi0, HW_B, "no implemented ports", 0},
        {"version 0", k_vs0, HW_B, "version 0", 0},
        {"no devices", k_empty, HW_N, "AHCI 1.0 1 ports; no disks", 0},
        {"BSY never clears", k_bsy, HW_F, "port 0 device stays BSY", 0},
        {"ATAPI only", k_atapi, HW_N, "port 0 ATAPI left alone; no disks", 0},
        {"port multiplier", k_pm, HW_N, "port multiplier left alone", 0},
        {"enclosure bridge", k_semb, HW_N, "enclosure bridge left alone", 0},
        {"odd signature", k_sig, HW_N, "signature 12345678 left alone", 0},
        {"IDENTIFY aborted", k_ident, HW_F, "port 0 IDENTIFY DEVICE failed", 0},
        {"no LBA", k_nolba, HW_B, "disk without LBA", 0},
        {"520-byte sectors", k_bs520, HW_B, "unsupported sector size 520", 0},
        {"8 KB sectors", k_bs8k, HW_B, "unsupported sector size 8192", 0},
        {"256-byte sectors", k_bs256, HW_B, "unsupported sector size 256", 0},
        {"no media", k_zero, HW_N, "ZXV FAKE DISK 0 has no media", 0},
        {"CR stuck", k_cr, HW_F, "port 0 engine will not stop", 0},
        {"FR stuck", k_fr, HW_F, "port 0 engine will not stop", 0},
        {"DET 1 until COMRESET", k_det1, HW_T, "128 x 512 B", 1},
        {"DET 1 forever", k_det1x, HW_N, "port 0 no link; no disks", 0},
        {"staggered spin-up", k_sss, HW_T, "128 x 512 B", 1},
        {"slow spin-up", k_slow, HW_T, "128 x 512 B", 1},
        {"BIOS handoff", k_boh, HW_T, "128 x 512 B", 1},
        {"BIOS busy", k_bohbb, HW_T, "128 x 512 B", 1},
        {"BIOS keeps it", k_bohx, HW_T, "firmware kept ownership; ICH9", 1},
        {"one of two BSY", k_two, HW_T, "port 0 device stays BSY; sata", 1},
        {"sparse PI", k_sparse, HW_T, "AHCI 1.0 2 ports; sata", 1},
    };
    for (uint32_t i = 0; i < sizeof faults / sizeof faults[0]; i++) {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fahci_t *f = add_ahci(&fp, HW_BDF(0, 4, 0), 1);
        faults[i].setup(f);
        hw_registry_t r;
        memset(&r, 0, sizeof r);
        hw_line_t note;
        hw_verdict_t v = bind_one(&fp, &r, &note);
        if (v != faults[i].want || !strstr(note.b, faults[i].note))
            printf("  fault '%s': verdict %d note '%s'\n", faults[i].what, v, note.b);
        CHECK(v == faults[i].want);
        CHECK(strstr(note.b, faults[i].note) != 0);
        CHECK(r.n_blk == faults[i].disks);
        if (r.n_blk) {
            uint8_t buf[512];
            hw_blk_t *b = r.blk[0];
            CHECK(b->read(b, 1, 1, buf) == 0);
            CHECK(memcmp(buf, "EFI PART", 8) == 0);
        }
        if (faults[i].setup == k_sss) CHECK(rd(f, PO(0, 0x18)) & 2);
        if (faults[i].setup == k_boh || faults[i].setup == k_bohbb) {
            CHECK((rd(f, 0x28) & 3) == 2);
            CHECK(strstr(note.b, "kept ownership") == 0);
        }
        if (faults[i].setup == k_det1)
            CHECK(f->port[0].comresets == 1 && rd(f, PO(0, 0x2C)) == 0x300);
        if (v != HW_T && faults[i].setup != k_hr && faults[i].setup != k_cr &&
            faults[i].setup != k_fr)
            CHECK((rd(f, PO(0, 0x18)) & 0x11) == 0); /* whatever failed is left stopped */
        if (faults[i].setup == k_sparse) {
            CHECK(rd(f, PO(0, 0x00)) == 0 && rd(f, PO(0, 0x14)) == 0x7DC0007Fu);
            CHECK(rd(f, PO(1, 0x00)) != 0 && (rd(f, PO(1, 0x18)) & 0x11) == 0);
            CHECK((rd(f, PO(6, 0x18)) & 0x11) == 0x11);
        }
        if (faults[i].setup == k_atapi) CHECK((rd(f, PO(0, 0x18)) & 0x11) == 0);
        if (faults[i].setup == k_bsy) CHECK((rd(f, PO(0, 0x18)) & 0x11) == 0);
        for (uint32_t n = 0; n < NP; n++) CHECK(!f->port[n].bad_layout);
        fake_plat_free(&fp);
        free_ahci(f);
    }
}

static void test_no_s64a(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fahci_t *f = add_ahci(&fp, HW_BDF(0, 2, 0), 1);
    wr(f, 0x00, rd(f, 0x00) & ~CAP_S64A);
    add_disk(f, 0, 128, 512, 1);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    hw_verdict_t v = bind_one(&fp, &r, &note);
    /* host heap pointers are the "bus addresses": above 4 GB on any 64-bit
     * host, so this must refuse */
    void *probe = malloc(16);
    int high = ((uint64_t) (uintptr_t) probe >> 32) != 0;
    free(probe);
    CHECK(high);
    CHECK(v == HW_F);
    CHECK(strstr(note.b, "above 4 GB") != 0);
    CHECK(r.n_blk == 0);
    CHECK(f->port[0].cmds == 0);
    fake_plat_free(&fp);
    free_ahci(f);
}

static void test_no_bar_registry_and_dma(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fake_dev_new(&fp, HW_BDF(0, 5, 0), 0x8086, 0x2922, 0x01, 0x06, 0x01);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_one(&fp, &r, &note) == HW_N);
    CHECK(strstr(note.b, "BAR5") != 0);
    fake_plat_free(&fp);

    /* registry already full */
    fake_plat_init(&fp);
    fahci_t *f = add_ahci(&fp, HW_BDF(0, 2, 0), 1);
    add_disk(f, 0, 128, 512, 1);
    memset(&r, 0, sizeof r);
    r.n_blk = HW_MAX_BLK;
    CHECK(bind_one(&fp, &r, &note) == HW_B);
    CHECK(strstr(note.b, "registry full") != 0);
    CHECK(r.n_blk == HW_MAX_BLK);
    fake_plat_free(&fp);
    free_ahci(f);

    /* every allocation failing in turn: hba, bounce, port, CL, FIS, CT */
    for (uint32_t k = 1; k <= 6; k++) {
        fake_plat_init(&fp);
        f = add_ahci(&fp, HW_BDF(0, 6, 0), 1);
        add_disk(f, 0, 128, 512, 1);
        fp.dma_fail_after = k;
        memset(&r, 0, sizeof r);
        CHECK(bind_one(&fp, &r, &note) == HW_F);
        CHECK(strstr(note.b, "out of DMA memory") != 0);
        CHECK(r.n_blk == 0);
        fake_plat_free(&fp);
        free_ahci(f);
    }
    /* the seventh allocation is the second port's: the first disk stays */
    fake_plat_init(&fp);
    f = add_ahci(&fp, HW_BDF(0, 6, 0), 2);
    add_disk(f, 0, 128, 512, 1);
    add_disk(f, 1, 128, 512, 1);
    fp.dma_fail_after = 7;
    memset(&r, 0, sizeof r);
    CHECK(bind_one(&fp, &r, &note) == HW_T);
    CHECK(strstr(note.b, "out of DMA memory") != 0);
    CHECK(r.n_blk == 1);
    fake_plat_free(&fp);
    free_ahci(f);
}

/* Allocator wrapper: fail exactly one call, and/or serve a range of calls
 * from memory below 4 GB (for HBAs without 64-bit addressing). */
static void *(*g_real_alloc)(void *, uint32_t, uint32_t, uint64_t *);
static uint32_t g_calls, g_fail_only, g_high_call;
static uint8_t *g_low; /* when set, every call but g_high_call is served below 4 GB */
static uint32_t g_low_used;

static void *wrap_alloc(void *ctx, uint32_t size, uint32_t align, uint64_t *phys)
{
    g_calls++;
    if (g_calls == g_fail_only) return 0;
    if (g_low && g_calls != g_high_call) {
        uint32_t at = (g_low_used + align - 1) & ~(align - 1);
        if (at + size > (1u << 20)) return 0;
        g_low_used = at + size;
        memset(g_low + at, 0, size);
        if (phys) *phys = (uint64_t) (uintptr_t) (g_low + at);
        return g_low + at;
    }
    return g_real_alloc(ctx, size, align, phys);
}

static void wrap_init(fake_plat_t *fp, uint32_t fail_only, uint32_t high_call)
{
    g_real_alloc = fp->plat.dma_alloc;
    fp->plat.dma_alloc = wrap_alloc;
    g_calls = 0;
    g_fail_only = fail_only;
    g_high_call = high_call;
    g_low_used = 0;
}

static void test_alloc_paths(void)
{
    /* exactly one allocation fails (the ones after it would succeed) */
    for (uint32_t k = 1; k <= 6; k++) {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fahci_t *f = add_ahci(&fp, HW_BDF(0, 6, 0), 1);
        add_disk(f, 0, 128, 512, 1);
        wrap_init(&fp, k, 0);
        hw_registry_t r;
        memset(&r, 0, sizeof r);
        hw_line_t note;
        CHECK(bind_one(&fp, &r, &note) == HW_F);
        CHECK(strstr(note.b, "out of DMA memory") != 0);
        CHECK(r.n_blk == 0 && f->port[0].cmds == 0);
        fake_plat_free(&fp);
        free_ahci(f);
    }
    /* no S64A: works with every DMA buffer below 4 GB; refuses as soon as
     * one is above. Calls: 1 HBA state, 2 bounce, 3 port state, 4 command
     * list, 5 FIS area, 6 command table (1 and 3 are never DMA targets). */
#if NO_LOW_MEMORY
    printf("SKIP no-S64A DMA checks: this host maps nothing below 4 GB\n");
    return;
#endif
    g_low = (uint8_t *) mmap(0, 1u << 20, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    CHECK(g_low != MAP_FAILED);
    if (g_low == MAP_FAILED) {
        g_low = 0;
        return;
    }
    for (uint32_t high = 0; high <= 6; high++) {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fahci_t *f = add_ahci(&fp, HW_BDF(0, 6, 0), 1);
        wr(f, 0x00, rd(f, 0x00) & ~CAP_S64A);
        add_disk(f, 0, 128, 512, 1);
        wrap_init(&fp, 0, high);
        hw_registry_t r;
        memset(&r, 0, sizeof r);
        hw_line_t note;
        hw_verdict_t v = bind_one(&fp, &r, &note);
        if (high == 0 || high == 1 || high == 3) {
            CHECK(v == HW_T);
            CHECK(r.n_blk == 1);
            CHECK(rd(f, PO(0, 0x04)) == 0 && rd(f, PO(0, 0x0C)) == 0); /* CLBU, FBU */
            uint8_t buf[512];
            if (r.n_blk)
                CHECK(r.blk[0]->read(r.blk[0], 1, 1, buf) == 0 && memcmp(buf, "EFI PART", 8) == 0);
        } else {
            CHECK(v == HW_F);
            CHECK(strstr(note.b, "4 GB") != 0);
            CHECK(r.n_blk == 0 && f->port[0].cmds == 0);
        }
        fake_plat_free(&fp);
        free_ahci(f);
    }
    munmap(g_low, 1u << 20);
    g_low = 0;
}

static void test_identify_details(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fahci_t *f = add_ahci(&fp, HW_BDF(0, 2, 0), 4);
    wr(f, 0x10, 0x00010301); /* AHCI 1.3.1 */
    add_disk(f, 0, 64, 512, 1);
    f->port[0].model = "   Odd\x01Model";
    add_disk(f, 1, 70, 512, 1);
    f->port[1].model = "0123456789012345678901234567890123456789";
    add_disk(f, 2, 80, 512, 1);
    f->port[2].zero_s48 = 1; /* 48-bit feature set without a 48-bit count */
    add_disk(f, 3, 90, 512, 1);
    f->port[3].model = "";
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_one(&fp, &r, &note) == HW_T);
    CHECK(strstr(note.b, "AHCI 1.3.1 4 ports; sata") == note.b + 5);
    CHECK(strstr(note.b, " Odd?Model 64 x 512 B") != 0);
    CHECK(strstr(note.b, " 0123456789012345678901234567890123456789 70") != 0 ||
          note.n >= sizeof note.b - 1);
    CHECK(r.n_blk == 4);
    CHECK(r.blk[2]->blocks == 80);
    uint8_t buf[512];
    CHECK(r.blk[2]->read(r.blk[2], 3, 1, buf) == 0 && f->port[2].last_cmd == 0xC8);
    CHECK(r.blk[3]->blocks == 90);
    fake_plat_free(&fp);
    free_ahci(f);
}

/* Names past sata9 take two digits. */
static void test_many_names(void)
{
    int expect = -1;
    for (int round = 0; round < 3; round++) {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fahci_t *f = add_ahci(&fp, HW_BDF(0, 2, 0), 6);
        for (uint32_t n = 0; n < 6; n++) add_disk(f, n, 32 + n, 512, 1);
        hw_registry_t r;
        memset(&r, 0, sizeof r);
        hw_line_t note;
        CHECK(bind_one(&fp, &r, &note) == HW_T);
        CHECK(r.n_blk == 6);
        for (uint32_t i = 0; i < r.n_blk; i++) {
            int num = name_num(r.blk[i]);
            char want[16];
            snprintf(want, sizeof want, "sata%d", num);
            CHECK(strcmp(r.blk[i]->name, want) == 0);
            if (expect >= 0) CHECK(num == expect);
            expect = num + 1;
            CHECK(num < 100);
        }
        fake_plat_free(&fp);
        free_ahci(f);
    }
}

static void test_two_hbas(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fahci_t *a = add_ahci(&fp, HW_BDF(0, 2, 0), 1);
    add_disk(a, 0, 64, 512, 1);
    fahci_t *b = add_ahci(&fp, HW_BDF(0, 3, 0), 2);
    add_disk(b, 1, 80, 512, 1);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_dev(&fp, 0, &r, &note) == HW_T);
    CHECK(bind_dev(&fp, 1, &r, &note) == HW_T);
    CHECK(r.n_blk == 2);
    CHECK(name_num(r.blk[1]) == name_num(r.blk[0]) + 1);
    CHECK(r.blk[0]->blocks == 64 && r.blk[1]->blocks == 80);
    uint8_t buf[512];
    CHECK(r.blk[1]->read(r.blk[1], 1, 1, buf) == 0 && memcmp(buf, "EFI PART", 8) == 0);
    CHECK(b->port[1].reads == 1 && a->port[0].reads == 0);
    CHECK(r.blk[0]->read(r.blk[0], 2, 1, buf) == 0 && buf[0] == pat(2, 0));
    CHECK(a->port[0].reads == 1);
    fake_plat_free(&fp);
    free_ahci(a);
    free_ahci(b);
}

/* The whole probe path: driver table match by class, boot lines, self-test. */
static void test_probe_and_selftest(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    add_lpc(&fp);
    fahci_t *f = add_ahci(&fp, HW_BDF(0, 0x1f, 2), 6);
    add_disk(f, 0, 102400, 512, 1);
    /* hw_probe_all scans by itself and BAR5 is a 32-bit BAR there: move the
     * register window below 4 GB (8 KB aligned, as its size requires). */
#if NO_LOW_MEMORY
    uint8_t *low = (uint8_t *) MAP_FAILED; /* the probe-all half needs a 32-bit BAR address */
    printf("SKIP probe-all with a 32-bit BAR: this host maps nothing below 4 GB\n");
#else
    uint8_t *low = (uint8_t *) mmap(0, 0x4000, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    CHECK(low != MAP_FAILED);
#endif
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    if (low != MAP_FAILED) {
        uint8_t *win = (uint8_t *) (((uintptr_t) low + 0x1FFF) & ~(uintptr_t) 0x1FFF);
        memcpy(win, f->r, 0x2000);
        f->r = win;
        fp.devs[1]->bar_mem[5] = win;
        fp.devs[1]->cfg[9] = (uint32_t) (uintptr_t) win; /* 32-bit memory BAR */
        fp.devs[1]->cfg[10] = 0;
        hw_probe_stats_t st = hw_probe_all(&fp.plat, &r);
        CHECK(st.functions == 2 && st.unclaimed == 1);
        CHECK(st.bound == 1 && st.held == 0 && st.failed == 0);
        CHECK(fake_log_has(&fp,
                           "[DRIVER ONLINE] ahci 00:1f.2 8086:2922 ICH9 AHCI 1.0 6 ports; sata"));
        CHECK(fake_log_has(&fp, "ZXV FAKE DISK 0 102400 x 512 B"));
        CHECK(fake_log_has(&fp, "1 bound, 0 failed, 0 held, 1 without a class driver"));
        CHECK(r.n_blk == 1);
        CHECK(hw_selftest_all(&fp.plat, &r) == 1);
        CHECK(fake_log_has(&fp, "[SELFTEST OK] sata"));
        CHECK(fake_log_has(&fp, "read LBA 1: GPT header 'EFI PART'; 102400 blocks of 512"));
        munmap(low, 0x4000);
    }
    fake_plat_free(&fp);
    free_ahci(f);

    fake_plat_init(&fp);
    add_lpc(&fp);
    f = add_ahci(&fp, HW_BDF(0, 0x1f, 2), 6);
    add_disk(f, 0, 102400, 512, 1);
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_dev(&fp, 1, &r, &note) == HW_T);
    CHECK(strstr(note.b, "ICH9 AHCI 1.0 6 ports; sata") != 0);
    CHECK(strstr(note.b, "ZXV FAKE DISK 0 102400 x 512 B") != 0);
    CHECK(hw_selftest_all(&fp.plat, &r) == 1);
    CHECK(fake_log_has(&fp, "read LBA 1: GPT header 'EFI PART'; 102400 blocks of 512"));
    fake_plat_free(&fp);
    free_ahci(f);
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    test_happy_path();
    test_lba48_high();
    test_errors_and_recovery();
    test_faults();
    test_no_s64a();
    test_no_bar_registry_and_dma();
    test_two_hbas();
    test_probe_and_selftest();
    test_alloc_paths();
    test_identify_details();
    test_many_names();
    (void) g_last;
    printf("test_ahci: %d checks, %d failures\n", fk_checks, fk_failures);
    return fk_failures ? 1 : 0;
}
