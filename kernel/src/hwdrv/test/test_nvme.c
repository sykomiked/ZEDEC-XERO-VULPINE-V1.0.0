/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_nvme.c — nvme.c against a register-level fake NVMe controller.
 *
 * The fake implements what the driver uses: CC.EN/CSTS.RDY handshake, admin
 * and one I/O queue pair read from host "DMA" memory through doorbells,
 * Identify controller/namespace, Create I/O CQ/SQ, Read and Write with
 * PRP1/PRP2, and phase-tagged completions. Knobs let a test break it in each
 * way the driver must survive. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hwfake.h"

#define BLOCKS 64

typedef struct fnvme {
    uint8_t *r;
    uint8_t disk[BLOCKS * 512];
    uint64_t sq[3], cq[3];
    uint32_t sq_size[3], cq_size[3];
    uint32_t sq_head[3], cq_tail[3];
    uint8_t phase[3];
    uint32_t last_db[3];
    uint64_t rdy_at; /* RDY follows EN after this time (0 = immediately) */
    int en_seen;
    /* fault knobs */
    int never_ready, fatal_on_enable, fail_opcode, nsze_zero, lbads, wrong_cid, no_css;
    int drop_opcode;               /* never complete this opcode */
    int ns_missing;                /* Identify namespace 1 fails */
    const uint8_t *model;          /* 40 raw model bytes (0 = "ZXV Fake NVMe" space padded) */
    uint32_t fail_status, fail_io; /* status for fail_opcode; status for every I/O */
    uint64_t nsze;                 /* namespace size reported (0 = BLOCKS) */
    /* what the driver programmed, for exact checks */
    uint32_t aqa, cc, cqdb[2], strict;
    uint64_t last_slba;
    uint32_t reads, writes, cmds;
} fnvme_t;

static uint32_t rd(fnvme_t *f, uint32_t o)
{
    return fk_rd32(f->r + o);
}
static void wr(fnvme_t *f, uint32_t o, uint32_t v)
{
    fk_wr32(f->r + o, v);
}

static void complete(fnvme_t *f, uint32_t q, uint16_t cid, uint32_t status)
{
    uint8_t *c = (uint8_t *) fk_ptr(f->cq[q]) + f->cq_tail[q] * 16;
    memset(c, 0, 16);
    fk_wr32(c + 8, f->sq_head[q]);
    fk_wr32(c + 12, (uint32_t) (f->wrong_cid ? cid + 1 : cid) | ((uint32_t) f->phase[q] << 16) |
                        (status << 17));
    if (++f->cq_tail[q] == f->cq_size[q]) {
        f->cq_tail[q] = 0;
        f->phase[q] ^= 1;
    }
}

static void xfer(fnvme_t *f, const uint8_t *e, int write)
{
    uint64_t slba = fk_rd64(e + 40);
    uint32_t nlb = (fk_rd32(e + 48) & 0xFFFF) + 1;
    uint32_t bytes = nlb * 512;
    uint8_t *p1 = (uint8_t *) fk_ptr(fk_rd64(e + 24)), *p2 = (uint8_t *) fk_ptr(fk_rd64(e + 32));
    uint32_t first = bytes > 4096 ? 4096 : bytes;
    uint8_t *disk = f->disk + (slba % BLOCKS) * 512;
    if (write) {
        memcpy(disk, p1, first);
        if (bytes > first) memcpy(disk + first, p2, bytes - first);
        f->writes++;
    } else {
        memcpy(p1, disk, first);
        if (bytes > first) memcpy(p2, disk + first, bytes - first);
        f->reads++;
    }
}

static uint32_t exec(fnvme_t *f, uint32_t q, const uint8_t *e)
{
    uint32_t dw0 = fk_rd32(e), opc = dw0 & 0xFF;
    f->cmds++;
    if ((int) opc == f->fail_opcode) return f->fail_status ? f->fail_status : 0x2;
    /* Strict about what a careful driver leaves zero: PRP2 unless the
     * transfer crosses a page, CDW11/12 for Identify. QEMU is more lenient. */
    uint64_t prp1 = fk_rd64(e + 24), prp2 = fk_rd64(e + 32);
    if (prp1 & 0xFFF) f->strict++;
    if (q == 0) {
        uint8_t *buf = (uint8_t *) fk_ptr(fk_rd64(e + 24));
        uint32_t c10 = fk_rd32(e + 40), c11 = fk_rd32(e + 44);
        switch (opc) {
        case 0x06: /* Identify */
            memset(buf, 0, 4096);
            if (prp2 || c11 || fk_rd32(e + 48)) {
                f->strict++;
                return 0x2;
            }
            if ((c10 & 0xFF) == 1) {
                if (fk_rd32(e + 4) != 0) {
                    f->strict++;
                    return 0xB;
                }
                memcpy(buf + 4, "FAKE0001", 8);
                memcpy(buf + 24, "ZXV Fake NVMe", 13);
                memset(buf + 37, ' ', 64 - 37);
                if (f->model) memcpy(buf + 24, f->model, 40);
                buf[64] = 'X'; /* byte after the field must not leak in */
            } else if ((c10 & 0xFF) == 0) {
                if (fk_rd32(e + 4) != 1 || f->ns_missing) return 0xB; /* invalid namespace */
                fk_wr64(buf, f->nsze_zero ? 0 : (f->nsze ? f->nsze : BLOCKS));
                buf[26] = 1; /* LBA format 1 */
                fk_wr32(buf + 128 + 4, (uint32_t) (f->lbads ? f->lbads : 9) << 16);
            }
            return 0;
        case 0x05: { /* create I/O CQ */
            uint32_t id = c10 & 0xFFFF;
            if (id != 1 || c11 != 1 || (c10 >> 16) + 1 > 64) { /* PC, no IEN */
                f->strict++;
                return 0x2;
            }
            f->cq[1] = fk_rd64(e + 24);
            f->cq_size[1] = (c10 >> 16) + 1;
            f->phase[1] = 1;
            return 0;
        }
        case 0x01: { /* create I/O SQ */
            uint32_t id = c10 & 0xFFFF;
            if (id != 1 || c11 != ((1u << 16) | 1) || !f->cq[1] || (c10 >> 16) + 1 > 64) {
                f->strict++;
                return 0x2;
            }
            f->sq[1] = fk_rd64(e + 24);
            f->sq_size[1] = (c10 >> 16) + 1;
            return 0;
        }
        }
        return 0x1; /* invalid opcode */
    }
    if (fk_rd32(e + 4) != 1) return 0xB;
    uint64_t slba = fk_rd64(e + 40);
    uint32_t nlb = (fk_rd32(e + 48) & 0xFFFF) + 1;
    uint64_t ns = f->nsze ? f->nsze : BLOCKS;
    f->last_slba = slba;
    if (slba + nlb > ns || (slba % BLOCKS) + nlb > BLOCKS) return 0x80; /* LBA out of range */
    if ((nlb * 512 > 4096) != (prp2 != 0) || (prp2 & 0xFFF)) f->strict++;
    if (f->fail_io) return f->fail_io;
    if (opc == 0x02)
        xfer(f, e, 0);
    else if (opc == 0x01)
        xfer(f, e, 1);
    else
        return 0x1;
    return 0;
}

static void tick(fake_dev_t *d, uint64_t now)
{
    fnvme_t *f = (fnvme_t *) d->state;
    uint32_t cc = rd(f, 0x14);
    if (cc & 1) {
        if (!f->en_seen) {
            f->en_seen = 1;
            f->rdy_at = now + 200;
            f->sq[0] = fk_rd64(f->r + 0x28);
            f->cq[0] = fk_rd64(f->r + 0x30);
            f->aqa = rd(f, 0x24);
            f->cc = cc;
            f->sq_size[0] = (f->aqa & 0xFFF) + 1;
            f->cq_size[0] = ((f->aqa >> 16) & 0xFFF) + 1;
            uint32_t mqes = (uint32_t) (fk_rd64(f->r) & 0xFFFF) + 1;
            if (f->sq_size[0] < 2 || f->cq_size[0] < 2 || f->sq_size[0] > mqes ||
                f->cq_size[0] > mqes || (cc & 0x00FFFFF0u) != 0x00460000u)
                f->fatal_on_enable = 1; /* a controller may fail on a bad config */
            f->phase[0] = 1;
            f->sq_head[0] = f->cq_tail[0] = 0;
        }
        if (f->fatal_on_enable)
            wr(f, 0x1C, 2);
        else if (!f->never_ready && now >= f->rdy_at)
            wr(f, 0x1C, 1);
    } else {
        f->en_seen = 0;
        wr(f, 0x1C, 0);
    }
    if (!(rd(f, 0x1C) & 1)) return;
    for (uint32_t q = 0; q < 2; q++) {
        if (!f->sq[q]) continue;
        uint32_t stride = 4u << ((fk_rd64(f->r) >> 32) & 0xF);
        uint32_t tail = rd(f, 0x1000 + 2 * q * stride);
        f->cqdb[q] = rd(f, 0x1000 + (2 * q + 1) * stride);
        while (f->sq_head[q] != tail) {
            const uint8_t *e = (const uint8_t *) fk_ptr(f->sq[q]) + f->sq_head[q] * 64;
            uint16_t cid = (uint16_t) (fk_rd32(e) >> 16);
            f->sq_head[q] = (f->sq_head[q] + 1) % f->sq_size[q];
            if ((int) (fk_rd32(e) & 0xFF) == f->drop_opcode && q == 0) continue;
            uint32_t st = exec(f, q, e);
            complete(f, q, cid, st);
        }
    }
}

static fnvme_t *add_nvme(fake_plat_t *fp, uint32_t bdf)
{
    fake_dev_t *d = fake_dev_new(fp, bdf, 0x1b36, 0x0010, 0x01, 0x08, 0x02);
    fnvme_t *f = (fnvme_t *) calloc(1, sizeof *f);
    f->r = fake_dev_bar(d, 0, 0x4000);
    f->fail_opcode = -1;
    f->drop_opcode = -1;
    /* CAP: MQES 63, CQR, TO 2 (1 s), DSTRD 0, CSS bit 37 (NVM), MPSMIN 0 */
    fk_wr64(f->r, 63ULL | (1ULL << 16) | (2ULL << 24) | (1ULL << 37));
    wr(f, 0x08, 0x00010400); /* 1.4 */
    for (uint32_t i = 0; i < BLOCKS * 512; i++) f->disk[i] = (uint8_t) (i * 7 + i / 512);
    memcpy(f->disk + 512, "EFI PART", 8);
    d->state = f;
    d->tick = tick;
    return f;
}

static hw_verdict_t bind_one(fake_plat_t *fp, hw_registry_t *r, hw_line_t *note)
{
    hw_pci_dev_t devs[4];
    uint32_t n = hw_pci_scan(&fp->plat, devs, 4);
    CHECK(n == 1);
    hw_line_init(note);
    return nvme_bind(&fp->plat, &devs[0], r, note);
}

static void test_happy_path(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fnvme_t *f = add_nvme(&fp, HW_BDF(0, 3, 0));
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_one(&fp, &r, &note) == HW_T);
    CHECK(r.n_blk == 1);
    hw_blk_t *b = r.blk[0];
    char want[80];
    snprintf(want, sizeof want, "ZXV Fake NVMe, ns1 64 x 512 B as %s", b->name);
    CHECK(strcmp(note.b, want) == 0);
    CHECK(b->block_size == 512 && b->blocks == BLOCKS);
    CHECK(strlen(b->name) == 5 && strncmp(b->name, "nvme", 4) == 0 && b->name[4] >= '0' &&
          b->name[4] <= '9');
    /* exactly what the driver programmed */
    CHECK(f->aqa == 0x000F000F);
    CHECK(f->cc == 0x00460001);
    CHECK(f->strict == 0);
    CHECK(f->sq_size[1] == 16 && f->cq_size[1] == 16);
    CHECK(f->cmds == 4);              /* identify ctrl, create CQ, create SQ, identify ns */
    fp.plat.delay_us(fp.plat.ctx, 1); /* let the fake sample the doorbells */
    CHECK(f->cqdb[0] == 4);           /* admin CQ head doorbell follows completions */
    /* bus mastering and memory decode on, INTx off */
    CHECK((fp.devs[0]->cfg[1] & 0x0406) == 0x0406);

    uint8_t buf[20 * 512];
    static uint8_t buf2[BLOCKS * 512];
    CHECK(b->read(b, 1, 1, buf) == 0);
    CHECK(memcmp(buf, "EFI PART", 8) == 0);
    /* 20 blocks span three 8 KB chunks: 16 + 4 */
    uint32_t reads0 = f->reads;
    CHECK(b->read(b, 10, 20, buf) == 0);
    CHECK(f->reads - reads0 == 2);
    CHECK(memcmp(buf, f->disk + 10 * 512, 20 * 512) == 0);
    /* odd alignment of the caller's buffer */
    CHECK(b->read(b, 3, 3, buf + 1) == 0);
    CHECK(memcmp(buf + 1, f->disk + 3 * 512, 3 * 512) == 0);
    /* write then read back; compare against a separate copy so a write that
     * silently became a read (filling w from the disk) cannot pass */
    uint8_t w[9 * 512], keep[9 * 512];
    for (uint32_t i = 0; i < sizeof w; i++) w[i] = keep[i] = (uint8_t) (0xA5 ^ i);
    uint32_t writes0 = f->writes;
    CHECK(b->write(b, 40, 9, w) == 0);
    CHECK(f->writes - writes0 == 1); /* 9 blocks fit one 8 KB command */
    CHECK(memcmp(w, keep, sizeof w) == 0);
    CHECK(memcmp(f->disk + 40 * 512, keep, sizeof w) == 0);
    memset(buf, 0, sizeof buf);
    CHECK(b->read(b, 40, 9, buf) == 0);
    CHECK(memcmp(buf, w, sizeof w) == 0);
    /* bounds: the driver refuses before the device sees it */
    uint32_t cmds = f->cmds;
    CHECK(b->read(b, BLOCKS, 1, buf) != 0);
    CHECK(b->read(b, BLOCKS - 1, 2, buf) != 0);
    CHECK(b->write(b, ~0ULL, 1, buf) != 0);
    CHECK(f->cmds == cmds);
    CHECK(b->read(b, BLOCKS - 1, 1, buf) == 0);
    CHECK(b->read(b, BLOCKS - 2, 2, buf) == 0);
    CHECK(b->read(b, BLOCKS - 2, 3, buf) != 0);
    CHECK(b->write(b, BLOCKS - 1, 1, keep) == 0);
    CHECK(b->write(b, BLOCKS - 1, 2, keep) != 0);
    CHECK(b->write(b, BLOCKS, 1, keep) != 0);
    CHECK(b->read(b, 0, BLOCKS, buf2) == 0); /* the whole disk, 8 commands */
    CHECK(memcmp(buf2, f->disk, sizeof buf2) == 0);
    /* exactly one page: no PRP2 (the fake counts any violation) */
    CHECK(b->read(b, 8, 8, buf) == 0 && f->strict == 0);
    CHECK(b->read(b, 8, 9, buf) == 0 && f->strict == 0);
    /* many commands wrap both rings and flip the phase tag several times */
    for (int i = 0; i < 100; i++) CHECK(b->read(b, (uint64_t) (i % BLOCKS), 1, buf) == 0);
    CHECK(memcmp(buf, f->disk + (99 % BLOCKS) * 512, 512) == 0);
    CHECK(f->strict == 0);
    /* I/O errors reach the caller, whatever the status value */
    f->fail_io = 0x1;
    CHECK(b->read(b, 1, 1, buf) == -1);
    CHECK(b->write(b, 1, 1, buf) == -1);
    f->fail_io = 0x4000;
    CHECK(b->read(b, 1, 1, buf) == -1);
    f->fail_io = 0;
    CHECK(b->read(b, 1, 1, buf) == 0);
    fake_plat_free(&fp);
    free(f);
}

typedef struct {
    const char *what;
    void (*breakit)(fnvme_t *f);
    hw_verdict_t want;
    const char *note;
} fault_t;

static void k_never_ready(fnvme_t *f)
{
    f->never_ready = 1;
}
static void k_fatal(fnvme_t *f)
{
    f->fatal_on_enable = 1;
}
static void k_ident(fnvme_t *f)
{
    f->fail_opcode = 0x06;
}
static void k_cq(fnvme_t *f)
{
    f->fail_opcode = 0x05;
}
static void k_sq(fnvme_t *f)
{
    f->fail_opcode = 0x01;
}
static void k_nsze(fnvme_t *f)
{
    f->nsze_zero = 1;
}
static void k_lbads(fnvme_t *f)
{
    f->lbads = 16;
}
static void k_cid(fnvme_t *f)
{
    f->wrong_cid = 1;
}
static void k_css(fnvme_t *f)
{
    fk_wr64(f->r, fk_rd64(f->r) & ~(1ULL << 37));
}
static void k_vs0(fnvme_t *f)
{
    wr(f, 0x08, 0);
}
static void k_mps(fnvme_t *f)
{
    fk_wr64(f->r, fk_rd64(f->r) | (1ULL << 48));
}
static void k_mqes(fnvme_t *f)
{
    fk_wr64(f->r, (fk_rd64(f->r) & ~0xFFFFULL) | 7);
}
static void k_ones(fnvme_t *f)
{
    memset(f->r, 0xFF, 16);
}
static void k_ident1(fnvme_t *f)
{
    f->fail_opcode = 0x06;
    f->fail_status = 1;
}
static void k_drop_ident(fnvme_t *f)
{
    f->drop_opcode = 0x06;
}
static void k_drop_cq(fnvme_t *f)
{
    f->drop_opcode = 0x05;
}
static void k_identns(fnvme_t *f)
{
    f->ns_missing = 1;
}
static void k_mqes14(fnvme_t *f)
{
    fk_wr64(f->r, (fk_rd64(f->r) & ~0xFFFFULL) | 14);
}
static void k_lbads12(fnvme_t *f)
{
    f->lbads = 12;
}
static void k_lbads8(fnvme_t *f)
{
    f->lbads = 8;
}
static void k_lbads13(fnvme_t *f)
{
    f->lbads = 13;
}
static void k_mqes15(fnvme_t *f)
{
    fk_wr64(f->r, (fk_rd64(f->r) & ~0xFFFFULL) | 15);
}
static void k_dstrd2(fnvme_t *f)
{
    fk_wr64(f->r, fk_rd64(f->r) | (2ULL << 32));
}
static void k_stuck_rdy(fnvme_t *f)
{
    wr(f, 0x1C, 1);
    wr(f, 0x14, 1);
    f->en_seen = 1;
    f->never_ready = 0;
}

static void test_faults(void)
{
    static const fault_t faults[] = {
        {"never ready", k_never_ready, HW_F, "enable timed out"},
        {"fatal status", k_fatal, HW_F, "controller fatal status"},
        {"identify fails", k_ident, HW_F, "Identify controller failed"},
        {"create CQ fails", k_cq, HW_F, "I/O queue creation failed"},
        {"create SQ fails", k_sq, HW_F, "I/O queue creation failed"},
        {"empty namespace", k_nsze, HW_N, "namespace 1 is empty"},
        {"64 KB blocks", k_lbads, HW_B, "unsupported block size"},
        {"wrong CID", k_cid, HW_F, "Identify controller failed"},
        {"identify status 1", k_ident1, HW_F, "Identify controller failed"},
        {"identify never completes", k_drop_ident, HW_F, "Identify controller failed"},
        {"create CQ never completes", k_drop_cq, HW_F, "I/O queue creation failed"},
        {"identify ns fails", k_identns, HW_N, "no namespace 1"},
        {"512 B blocks ok, 4 KB ok", k_lbads12, HW_T, "x 4096 B"},
        {"256 B blocks", k_lbads8, HW_B, "unsupported block size"},
        {"8 KB blocks", k_lbads13, HW_B, "unsupported block size"},
        {"queues of exactly 16", k_mqes15, HW_T, "ns1"},
        {"queues of 15", k_mqes14, HW_B, "shorter than 16"},
        {"doorbell stride 16", k_dstrd2, HW_T, "ns1"},
        {"no NVM command set", k_css, HW_B, "NVM command set"},
        {"VS 0", k_vs0, HW_B, "VS is 0"},
        {"MPSMIN 8 KB", k_mps, HW_B, "page size"},
        {"short queues", k_mqes, HW_B, "shorter than 16"},
        {"all-ones registers", k_ones, HW_F, "all-ones"},
        {"RDY stuck after disable", k_stuck_rdy, HW_T, ""},
    };
    for (uint32_t i = 0; i < sizeof faults / sizeof faults[0]; i++) {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fnvme_t *f = add_nvme(&fp, HW_BDF(0, 4, 0));
        faults[i].breakit(f);
        hw_registry_t r;
        memset(&r, 0, sizeof r);
        hw_line_t note;
        hw_verdict_t v = bind_one(&fp, &r, &note);
        if (v != faults[i].want || !strstr(note.b, faults[i].note))
            printf("  fault '%s': verdict %d note '%s'\n", faults[i].what, v, note.b);
        CHECK(v == faults[i].want);
        CHECK(strstr(note.b, faults[i].note) != 0);
        CHECK((v == HW_T) == (r.n_blk == 1));
        fake_plat_free(&fp);
        free(f);
    }
}

/* Every controller wait is bounded by CAP.TO (500 ms units, 0 taken as 1,
 * capped at 10 s), and a command that never completes costs the same. */
static void test_timeouts(void)
{
    static const struct {
        uint32_t to;
        uint64_t lo, hi;
    } t[] = {{0, 500000, 500100},    {1, 500000, 500100},      {2, 1000000, 1000100},
             {19, 9500000, 9500100}, {20, 10000000, 10000100}, {200, 10000000, 10000100}};
    for (uint32_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        for (int drop = 0; drop < 2; drop++) {
            fake_plat_t fp;
            fake_plat_init(&fp);
            fnvme_t *f = add_nvme(&fp, HW_BDF(0, 7, 0));
            fk_wr64(f->r, (fk_rd64(f->r) & ~(0xFFULL << 24)) | ((uint64_t) t[i].to << 24));
            if (drop)
                f->drop_opcode = 0x06;
            else
                f->never_ready = 1;
            hw_registry_t r;
            memset(&r, 0, sizeof r);
            hw_line_t note;
            CHECK(bind_one(&fp, &r, &note) == HW_F);
            uint64_t spent = fp.now_us - (drop ? 200 : 0); /* RDY comes after 200 us */
            if (spent < t[i].lo || spent > t[i].hi)
                printf("  TO %u drop %d: %llu us\n", t[i].to, drop, (unsigned long long) spent);
            CHECK(spent >= t[i].lo && spent <= t[i].hi);
            fake_plat_free(&fp);
            free(f);
        }
    }
}

/* A namespace past 2^32 blocks: the upper SLBA dword must reach the device. */
static void test_big_namespace(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fnvme_t *f = add_nvme(&fp, HW_BDF(0, 8, 0));
    f->nsze = 0x300000000ULL;
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_one(&fp, &r, &note) == HW_T);
    CHECK(strstr(note.b, "ns1 12884901888 x 512 B") != 0);
    hw_blk_t *b = r.blk[0];
    CHECK(b->blocks == 0x300000000ULL);
    uint8_t buf[512];
    uint64_t lba = 0x200000000ULL + BLOCKS + 1; /* maps to fake block 1 */
    CHECK(b->read(b, lba, 1, buf) == 0);
    CHECK(f->last_slba == lba);
    CHECK(memcmp(buf, "EFI PART", 8) == 0);
    CHECK(b->read(b, 0x2FFFFFFFFULL, 1, buf) == 0);
    CHECK(f->last_slba == 0x2FFFFFFFFULL);
    CHECK(b->read(b, 0x300000000ULL, 1, buf) != 0);
    fake_plat_free(&fp);
    free(f);
}

/* Model number: trailing spaces and NULs trimmed, control bytes shown as '?',
 * all 40 bytes used when full, nothing past byte 63. */
static void test_model(void)
{
    static const struct {
        const char raw[41];
        const char *want;
    } m[] = {
        {"AB\001C\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
         "AB?C, ns1"},
        {"                                        ", ", ns1"},
        {"0123456789012345678901234567890123456789",
         "0123456789012345678901234567890123456789, ns1"},
        {"x \177~                                  ", "x ?~, ns1"},
        {"  lead and trail                        ", "  lead and trail, ns1"},
        {"\037                                       ", "?, ns1"},
    };
    for (uint32_t i = 0; i < sizeof m / sizeof m[0]; i++) {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fnvme_t *f = add_nvme(&fp, HW_BDF(0, 10, 0));
        f->model = (const uint8_t *) m[i].raw;
        hw_registry_t r;
        memset(&r, 0, sizeof r);
        hw_line_t note;
        CHECK(bind_one(&fp, &r, &note) == HW_T);
        if (strncmp(note.b, m[i].want, strlen(m[i].want)) != 0)
            printf("  model %u: '%s'\n", i, note.b);
        CHECK(strncmp(note.b, m[i].want, strlen(m[i].want)) == 0);
        fake_plat_free(&fp);
        free(f);
    }
}

/* A second controller gets the next name. */
static void test_names(void)
{
    char first[16] = {0};
    for (int i = 0; i < 2; i++) {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fnvme_t *f = add_nvme(&fp, HW_BDF(0, 9, 0));
        hw_registry_t r;
        memset(&r, 0, sizeof r);
        hw_line_t note;
        CHECK(bind_one(&fp, &r, &note) == HW_T);
        if (i == 0)
            strcpy(first, r.blk[0]->name);
        else
            CHECK(r.blk[0]->name[4] == (first[4] == '9' ? '0' : first[4] + 1));
        fake_plat_free(&fp);
        free(f);
    }
}

static void test_no_bar_and_dma_exhaustion(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fake_dev_new(&fp, HW_BDF(0, 5, 0), 0x1b36, 0x0010, 0x01, 0x08, 0x02); /* no BAR */
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_one(&fp, &r, &note) == HW_N);
    CHECK(strstr(note.b, "BAR0") != 0);
    fake_plat_free(&fp);

    for (uint32_t k = 1; k <= 5; k++) {
        fake_plat_init(&fp);
        fnvme_t *f = add_nvme(&fp, HW_BDF(0, 6, 0));
        fp.dma_fail_after = k;
        memset(&r, 0, sizeof r);
        CHECK(bind_one(&fp, &r, &note) == HW_F);
        CHECK(strstr(note.b, "DMA") != 0);
        CHECK(r.n_blk == 0);
        fake_plat_free(&fp);
        free(f);
    }
}

int main(void)
{
    test_happy_path();
    test_faults();
    test_no_bar_and_dma_exhaustion();
    test_timeouts();
    test_big_namespace();
    test_names();
    test_model();
    printf("test_nvme: %d checks, %d failures\n", fk_checks, fk_failures);
    return fk_failures ? 1 : 0;
}
