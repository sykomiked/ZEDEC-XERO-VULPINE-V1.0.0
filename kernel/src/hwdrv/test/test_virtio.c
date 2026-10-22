/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_virtio.c — the virtio-pci transport and the vblk, vscsi and vnet
 * drivers against fake virtio devices (fvirtio.h for the transport; a SCSI
 * target, a block device and a NIC cabled to nicpeer.h here). Knobs break
 * each device in the ways a driver must survive; the fake counts every
 * transport rule the driver breaks, and every happy path checks that count
 * stays zero. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fvirtio.h"
#include "nicpeer.h"
#include "../virtio_pci.h"

#define BLOCKS 64

static void disk_fill(uint8_t *d, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t) (i * 7 + i / 512);
    memcpy(d + 512, "EFI PART", 8);
}

static hw_pci_dev_t g_dev[8];

static const hw_pci_dev_t *scan_first(fake_plat_t *fp)
{
    uint32_t n = hw_pci_scan(&fp->plat, g_dev, 8);
    CHECK(n >= 1);
    return &g_dev[0];
}

/* ===================================================================== blk */
typedef struct fblk {
    fvirtio_t *f;
    uint8_t disk[BLOCKS * 512];
    uint32_t reqs, reads, writes, maxlen;
    uint64_t last_sector;
    int ioerr;
    int no_status; /* complete requests without writing the status byte */
} fblk_t;

static int32_t blk_serve(fvirtio_t *f, uint32_t q, fv_desc_t *d, uint32_t n)
{
    fblk_t *b = (fblk_t *) f->dev_state;
    if (q != 0 || n != 3 || d[0].w || d[0].len != 16 || !d[2].w || d[2].len != 1) {
        f->strict++;
        return 0;
    }
    uint32_t type = fk_rd32(d[0].p);
    uint64_t sector = fk_rd64(d[0].p + 8);
    b->reqs++;
    b->last_sector = sector;
    if (d[1].len > b->maxlen) b->maxlen = d[1].len;
    if (d[1].len % 512 || d[1].w != (type == 0)) f->strict++;
    if (fk_rd32(d[0].p + 4)) f->strict++; /* reserved */
    if (b->ioerr) {
        d[2].p[0] = 1;
        return 1;
    }
    if (b->no_status) return 0;
    for (uint32_t i = 0; i < d[1].len; i++) {
        uint8_t *at = &b->disk[(sector * 512 + i) % sizeof b->disk];
        if (type == 0)
            d[1].p[i] = *at;
        else
            *at = d[1].p[i];
    }
    if (type == 0)
        b->reads++;
    else
        b->writes++;
    d[2].p[0] = 0;
    return (int32_t) (type == 0 ? d[1].len + 1 : 1);
}

static void blk_tick(fake_dev_t *d, uint64_t now)
{
    (void) now;
    fv_tick((fvirtio_t *) d->state);
}

static fblk_t *add_blk(fake_plat_t *fp, uint64_t features, uint16_t qmax, uint64_t capacity,
                       uint32_t size_max)
{
    const uint16_t qm[FV_QN] = {qmax, 0, 0, 0};
    fblk_t *b = (fblk_t *) calloc(1, sizeof *b);
    b->f = fv_new(fp, HW_BDF(0, 4, 0), 0x1042, 0x01, 0x80, features, qm, blk_tick);
    b->f->serve = blk_serve;
    b->f->dev_state = b;
    fk_wr64(b->f->r + FV_DEVCFG, capacity);
    fk_wr32(b->f->r + FV_DEVCFG + 8, size_max);
    disk_fill(b->disk, sizeof b->disk);
    return b;
}

/* Every disk a driver registers is named prefix + one digit, the digit one
 * past the previous disk's (mod 10), across the whole run. */
static void name_next(const char *name, const char *prefix, int *last)
{
    size_t n = strlen(prefix);
    CHECK(strncmp(name, prefix, n) == 0 && strlen(name) == n + 1);
    int dgt = name[n] - '0';
    CHECK(dgt >= 0 && dgt <= 9);
    if (*last >= 0) CHECK(dgt == (*last + 1) % 10);
    *last = dgt;
}

static int g_blk_last = -1, g_vsd_last = -1;

static hw_verdict_t bind_blk(fake_plat_t *fp, hw_registry_t *r, hw_line_t *note)
{
    memset(r, 0, sizeof *r);
    hw_line_init(note);
    hw_verdict_t v = vblk_bind(&fp->plat, scan_first(fp), r, note);
    if (v == HW_T) name_next(r->blk[0]->name, "vblk", &g_blk_last);
    return v;
}

static void test_blk_happy(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fblk_t *b = add_blk(&fp, (1ull << 1) | (1ull << 9), 128, BLOCKS, 4096);
    hw_registry_t r;
    hw_line_t note;
    CHECK(bind_blk(&fp, &r, &note) == HW_T);
    CHECK(r.n_blk == 1);
    hw_blk_t *k = r.blk[0];
    char want[80];
    snprintf(want, sizeof want, "virtio disk, 64 x 512 B as %s", k->name);
    CHECK(strcmp(note.b, want) == 0);
    CHECK(strncmp(k->name, "vblk", 4) == 0 && strlen(k->name) == 5);
    CHECK(k->block_size == 512 && k->blocks == BLOCKS);
    /* the transport: VERSION_1 and SIZE_MAX taken, bit 9 (offered, not
     * wanted) left alone; status ACK|DRIVER|FEATURES_OK|DRIVER_OK; the queue
     * cut to 64 from the device's 128; bus mastering and memory decode on */
    CHECK(b->f->taken == ((1ull << 32) | (1ull << 1)));
    CHECK(b->f->status == 0x0F);
    CHECK(b->f->q[0].size == 64 && b->f->q[0].enable == 1);
    CHECK((fp.devs[0]->cfg[1] & 0x0006) == 0x0006);
    CHECK(b->f->r[0x10] == 0xFF && b->f->r[0x11] == 0xFF); /* no MSI-X vector */
    uint8_t buf[20 * 512];
    static uint8_t whole[BLOCKS * 512];
    CHECK(k->read(k, 1, 1, buf) == 0);
    CHECK(memcmp(buf, "EFI PART", 8) == 0);
    CHECK(b->last_sector == 1);
    /* size_max 4096: 20 sectors are three requests of 8, 8, 4 */
    uint32_t r0 = b->reqs;
    CHECK(k->read(k, 10, 20, buf) == 0);
    CHECK(b->reqs - r0 == 3 && b->maxlen == 4096);
    CHECK(memcmp(buf, b->disk + 10 * 512, 20 * 512) == 0);
    CHECK(k->read(k, 3, 3, buf + 1) == 0);
    CHECK(memcmp(buf + 1, b->disk + 3 * 512, 3 * 512) == 0);
    uint8_t w[9 * 512], keep[9 * 512];
    for (uint32_t i = 0; i < sizeof w; i++) w[i] = keep[i] = (uint8_t) (0x5A ^ i);
    uint32_t w0 = b->writes;
    CHECK(k->write(k, 40, 9, w) == 0);
    CHECK(b->writes - w0 == 2);
    CHECK(memcmp(b->disk + 40 * 512, keep, sizeof keep) == 0);
    memset(buf, 0, sizeof buf);
    CHECK(k->read(k, 40, 9, buf) == 0 && memcmp(buf, keep, sizeof keep) == 0);
    /* bounds: refused before the device sees anything */
    uint32_t rq = b->reqs;
    CHECK(k->read(k, BLOCKS, 1, buf) != 0);
    CHECK(k->read(k, BLOCKS - 1, 2, buf) != 0);
    CHECK(k->write(k, ~0ull, 1, buf) != 0);
    CHECK(b->reqs == rq);
    CHECK(k->read(k, BLOCKS - 1, 1, buf) == 0);
    CHECK(k->read(k, 0, BLOCKS, whole) == 0 && memcmp(whole, b->disk, sizeof whole) == 0);
    /* many requests wrap the 64-entry rings */
    for (int i = 0; i < 150; i++) CHECK(k->read(k, (uint64_t) (i % BLOCKS), 1, buf) == 0);
    CHECK(memcmp(buf, b->disk + (149 % BLOCKS) * 512, 512) == 0);
    CHECK(b->f->q[0].kicks == b->reqs);
    CHECK(b->f->strict == 0);
    /* device I/O error reaches the caller */
    b->ioerr = 1;
    CHECK(k->read(k, 1, 1, buf) == -1);
    CHECK(k->write(k, 1, 1, buf) == -1);
    b->ioerr = 0;
    CHECK(k->read(k, 1, 1, buf) == 0);
    fake_plat_free(&fp);
    free(b->f);
    free(b);
}

static void test_blk_shapes(void)
{
    hw_registry_t r;
    hw_line_t note;
    uint8_t buf[4096];
    /* no SIZE_MAX: one request carries up to 64 KB */
    {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fblk_t *b = add_blk(&fp, 0, 16, BLOCKS, 0);
        CHECK(bind_blk(&fp, &r, &note) == HW_T);
        CHECK(b->f->q[0].size == 16);
        static uint8_t big[BLOCKS * 512];
        CHECK(r.blk[0]->read(r.blk[0], 0, BLOCKS, big) == 0);
        CHECK(b->reqs == 1 && b->maxlen == BLOCKS * 512);
        CHECK(b->f->strict == 0);
        fake_plat_free(&fp);
        free(b->f);
        free(b);
    }
    /* read-only: writes refused before any request */
    {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fblk_t *b = add_blk(&fp, 1ull << 5, 64, BLOCKS, 0);
        CHECK(bind_blk(&fp, &r, &note) == HW_T);
        CHECK(strstr(note.b, ", read-only as vblk") != 0);
        CHECK(r.blk[0]->write(r.blk[0], 1, 1, buf) == -1);
        CHECK(b->reqs == 0);
        CHECK(r.blk[0]->read(r.blk[0], 1, 1, buf) == 0 && b->reqs == 1);
        fake_plat_free(&fp);
        free(b->f);
        free(b);
    }
    /* capacity 0: no medium, the device reset */
    {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fblk_t *b = add_blk(&fp, 0, 64, 0, 0);
        CHECK(bind_blk(&fp, &r, &note) == HW_N);
        CHECK(strcmp(note.b, "no medium (capacity 0)") == 0);
        CHECK(r.n_blk == 0 && b->f->status == 0);
        fake_plat_free(&fp);
        free(b->f);
        free(b);
    }
    /* capacity past 64-bit byte addressing: held */
    {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fblk_t *b = add_blk(&fp, 0, 64, (~0ull >> 9) + 1, 0);
        CHECK(bind_blk(&fp, &r, &note) == HW_B);
        CHECK(r.n_blk == 0 && b->f->status == 0);
        fake_plat_free(&fp);
        free(b->f);
        free(b);
    }
    /* the largest capacity that still fits is taken */
    {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fblk_t *b = add_blk(&fp, 0, 64, ~0ull >> 9, 0);
        CHECK(bind_blk(&fp, &r, &note) == HW_T);
        CHECK(r.blk[0]->blocks == ~0ull >> 9);
        fake_plat_free(&fp);
        free(b->f);
        free(b);
    }
    /* size_max under a sector: held */
    {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fblk_t *b = add_blk(&fp, 1ull << 1, 64, BLOCKS, 511);
        CHECK(bind_blk(&fp, &r, &note) == HW_B);
        CHECK(strcmp(note.b, "a largest segment under one sector") == 0);
        fake_plat_free(&fp);
        free(b->f);
        free(b);
    }
    /* size_max exactly one sector: one sector per request */
    {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fblk_t *b = add_blk(&fp, 1ull << 1, 64, BLOCKS, 512);
        CHECK(bind_blk(&fp, &r, &note) == HW_T);
        CHECK(r.blk[0]->read(r.blk[0], 0, 3, buf) == 0 && b->reqs == 3 && b->maxlen == 512);
        fake_plat_free(&fp);
        free(b->f);
        free(b);
    }
}

/* Transport faults, through vblk. */
static void test_transport_faults(void)
{
    hw_registry_t r;
    hw_line_t note;
    struct {
        const char *what;
        int knob;
        hw_verdict_t want;
        const char *note;
    } t[] = {
        {"no VERSION_1", 1, HW_F, "does not offer VIRTIO_F_VERSION_1"},
        {"refuses features", 2, HW_F, "refused the features offered back to it"},
        {"reset never completes", 3, HW_F, "reset did not complete"},
        {"no queue 0", 4, HW_F, "a virtqueue it needs is missing"},
        {"queue of 1", 5, HW_F, "a virtqueue too short to use"},
        {"no capability list", 6, HW_N, "no capability list (a legacy-only virtio device)"},
        {"no vendor caps", 7, HW_N, "no modern virtio structures in a memory BAR"},
        {"cap past the BAR", 8, HW_N, "no modern virtio structures in a memory BAR"},
        {"no device cfg cap", 9, HW_N, "no modern virtio structures in a memory BAR"},
        {"DMA exhausted", 10, HW_F, "out of DMA memory"},
        {"common cfg too short", 11, HW_N, "no modern virtio structures in a memory BAR"},
    };
    for (uint32_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fblk_t *b = add_blk(&fp, 0, t[i].knob == 4 ? 0 : t[i].knob == 5 ? 1 : 64, BLOCKS, 0);
        fake_dev_t *d = fp.devs[0];
        switch (t[i].knob) {
        case 1:
            b->f->offered &= ~(1ull << 32);
            break;
        case 2:
            b->f->refuse = 1;
            break;
        case 3:
            b->f->stuck_reset = 1;
            b->f->status = 0x0F; /* left running by the firmware */
            b->f->r[0x14] = 0x0F;
            break;
        case 6:
            d->cfg[1] &= ~0x00100000u;
            break;
        case 7:
            d->cfg[0x40 / 4] = (d->cfg[0x40 / 4] & ~0xFFu) | 0x11; /* MSI-X id */
            d->cfg[0x54 / 4] = (d->cfg[0x54 / 4] & ~0xFFu) | 0x11;
            d->cfg[0x6C / 4] = (d->cfg[0x6C / 4] & ~0xFFu) | 0x11;
            d->cfg[0x80 / 4] = (d->cfg[0x80 / 4] & ~0xFFu) | 0x11;
            break;
        case 8:
            d->cfg[0x40 / 4 + 2] = FV_BAR - 0x10;
            break; /* common runs past BAR 4 */
        case 9:
            d->cfg[0x6C / 4] &= 0xFFFFu;
            d->cfg[0x6C / 4] |= 16u << 16 | 3u << 24;
            d->cfg[0x80 / 4] = (d->cfg[0x80 / 4] & 0x00FFFFFFu) | (3u << 24); /* two ISR */
            break;
        case 10:
            fp.dma_fail_after = 2;
            break;
        case 11:
            d->cfg[0x40 / 4 + 3] = 0x37;
            break;
        }
        hw_verdict_t v = bind_blk(&fp, &r, &note);
        if (v != t[i].want || strncmp(note.b, t[i].note, strlen(t[i].note)) != 0)
            printf("  fault '%s': verdict %d note '%s'\n", t[i].what, v, note.b);
        CHECK(v == t[i].want);
        CHECK(strncmp(note.b, t[i].note, strlen(t[i].note)) == 0);
        CHECK(r.n_blk == 0);
        if (t[i].knob == 1 || t[i].knob == 2) CHECK(b->f->status & 0x80); /* FAILED */
        fake_plat_free(&fp);
        free(b->f);
        free(b);
    }
    /* a device that never answers: the request times out, the caller sees -1 */
    fake_plat_t fp;
    fake_plat_init(&fp);
    fblk_t *b = add_blk(&fp, 0, 64, BLOCKS, 0);
    CHECK(bind_blk(&fp, &r, &note) == HW_T);
    b->f->deaf = 1;
    uint8_t buf[512];
    uint64_t t0 = fp.now_us;
    CHECK(r.blk[0]->read(r.blk[0], 1, 1, buf) == -1);
    CHECK(fp.now_us - t0 == 5000000); /* 5 s in 10 us polls, not one poll more */
    CHECK(b->f->q[0].kicks == 1 && b->reqs == 0);
    fake_plat_free(&fp);
    free(b->f);
    free(b);
}

static void test_blk_more(void)
{
    hw_registry_t r;
    hw_line_t note;
    uint8_t buf[512];
    fake_plat_t fp;
    /* every byte of the header's sector field reaches the device */
    fake_plat_init(&fp);
    fblk_t *b = add_blk(&fp, 0, 64, 1ull << 40, 0);
    CHECK(bind_blk(&fp, &r, &note) == HW_T);
    CHECK(r.blk[0]->read(r.blk[0], 0x0123456789ull, 1, buf) == 0 &&
          b->last_sector == 0x0123456789ull);
    CHECK(r.blk[0]->write(r.blk[0], 0xFEDCBA9876ull, 1, buf) == 0 &&
          b->last_sector == 0xFEDCBA9876ull);
    CHECK(r.blk[0]->read(r.blk[0], (1ull << 40) - 1, 1, buf) == 0);
    CHECK(r.blk[0]->read(r.blk[0], 1ull << 40, 0, buf) == -1);
    CHECK(b->f->strict == 0);
    fake_plat_free(&fp);
    free(b->f);
    free(b);
    /* a capacity whose top byte is set: held */
    fake_plat_init(&fp);
    b = add_blk(&fp, 0, 64, 0x0100000000000000ull, 0);
    CHECK(bind_blk(&fp, &r, &note) == HW_B);
    fake_plat_free(&fp);
    free(b->f);
    free(b);
    /* 200 sectors: 64 KB then the rest, with no SIZE_MAX and with sizes whose
     * upper bytes are set (each still above the bounce buffer) */
    const uint32_t sm[3][2] = {{0, 0}, {1u << 1, 0x00010200}, {1u << 1, 0x01000200}};
    for (uint32_t i = 0; i < 3; i++) {
        fake_plat_init(&fp);
        b = add_blk(&fp, sm[i][0], 64, 1024, sm[i][1]);
        CHECK(bind_blk(&fp, &r, &note) == HW_T);
        static uint8_t big[200 * 512];
        CHECK(r.blk[0]->read(r.blk[0], 3, 200, big) == 0);
        CHECK(b->reqs == 2 && b->maxlen == 65536);
        CHECK(memcmp(big, b->disk + 3 * 512, 61 * 512) == 0);
        fake_plat_free(&fp);
        free(b->f);
        free(b);
    }
    /* a device that completes without writing the status byte */
    fake_plat_init(&fp);
    b = add_blk(&fp, 0, 64, BLOCKS, 0);
    CHECK(bind_blk(&fp, &r, &note) == HW_T);
    b->no_status = 1;
    CHECK(r.blk[0]->read(r.blk[0], 1, 1, buf) == -1);
    fake_plat_free(&fp);
    free(b->f);
    free(b);
    /* each of the driver's own allocations failing alone, before any queue */
    for (uint32_t n = 1; n <= 4; n++) {
        fake_plat_init(&fp);
        b = add_blk(&fp, 0, 64, BLOCKS, 0);
        fp.dma_fail_one = n;
        CHECK(bind_blk(&fp, &r, &note) == HW_F && strcmp(note.b, "out of DMA memory") == 0);
        CHECK(b->f->q[0].enable == 0 && r.n_blk == 0);
        fake_plat_free(&fp);
        free(b->f);
        free(b);
    }
}

/* The transport API directly, and the shapes a device may come in. */
static void test_transport_shapes(void)
{
    hw_registry_t r;
    hw_line_t note;
    /* queue sizes: the largest power of two up to 64 the device allows, at
     * least 4 */
    const struct {
        uint16_t max, want;
    } qs[] = {{256, 64}, {64, 64}, {48, 32}, {32, 32}, {17, 16}, {8, 8}, {4, 4}, {3, 0}, {2, 0}};
    for (uint32_t i = 0; i < sizeof qs / sizeof qs[0]; i++) {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fblk_t *b = add_blk(&fp, 0, qs[i].max, BLOCKS, 0);
        hw_verdict_t v = bind_blk(&fp, &r, &note);
        if (qs[i].want) {
            CHECK(v == HW_T && b->f->q[0].size == qs[i].want);
            uint8_t buf[512];
            for (int k = 0; k < 3 * qs[i].want; k++)
                CHECK(r.blk[0]->read(r.blk[0], 1, 1, buf) == 0);
            CHECK(b->f->strict == 0);
        } else {
            CHECK(v == HW_F && strcmp(note.b, "a virtqueue too short to use") == 0);
        }
        fake_plat_free(&fp);
        free(b->f);
        free(b);
    }
    /* a reset that takes five polls to finish is waited for */
    {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fblk_t *b = add_blk(&fp, 0, 64, BLOCKS, 0);
        b->f->slow_reset = 5;
        b->f->status = 0x0F;
        b->f->r[0x14] = 0x0F;
        CHECK(bind_blk(&fp, &r, &note) == HW_T);
        CHECK(b->f->resets == 1);
        fake_plat_free(&fp);
        free(b->f);
        free(b);
    }
    /* one that never finishes is given up on after 100000 polls of 1 us */
    {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fblk_t *b = add_blk(&fp, 0, 64, BLOCKS, 0);
        b->f->stuck_reset = 1;
        b->f->status = 0x0F;
        b->f->r[0x14] = 0x0F;
        CHECK(bind_blk(&fp, &r, &note) == HW_F);
        CHECK(fp.now_us == 100001);
        fake_plat_free(&fp);
        free(b->f);
        free(b);
    }
    /* capability chain quirks: pointers with their low two bits set, a
     * structure that ends exactly at the end of its BAR, and a capability
     * naming BAR 6 (there is none) ahead of the real one */
    {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fblk_t *b = add_blk(&fp, 0, 64, BLOCKS, 0);
        fake_dev_t *d = fp.devs[0];
        d->cfg[0x34 / 4] = 0x43;                                         /* -> 0x40 */
        d->cfg[0x40 / 4] = (d->cfg[0x40 / 4] & ~0xFF00u) | (0x57u << 8); /* -> 0x54 */
        d->cfg[0x80 / 4 + 2] = FV_BAR - 0x100;                           /* device cfg at the end */
        memmove(b->f->r + FV_BAR - 0x100, b->f->r + FV_DEVCFG, 16);
        /* a common-config capability in BAR 6 first in the chain: ignored */
        d->cfg[0x34 / 4] = 0xC0;
        fv_cap(d, 0xC0, 0x40, 1, 0, 0x38);
        d->cfg[0xC4 / 4] = 6;
        CHECK(bind_blk(&fp, &r, &note) == HW_T);
        CHECK(r.blk[0]->blocks == BLOCKS);
        uint8_t buf[512];
        CHECK(r.blk[0]->read(r.blk[0], 1, 1, buf) == 0 && memcmp(buf, "EFI PART", 8) == 0);
        CHECK(b->f->strict == 0);
        fake_plat_free(&fp);
        free(b->f);
        free(b);
    }
    /* out of DMA memory part way through a queue's rings */
    for (uint32_t n = 5; n <= 7; n++) {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fblk_t *b = add_blk(&fp, 0, 64, BLOCKS, 0);
        fp.dma_fail_after = n;
        CHECK(bind_blk(&fp, &r, &note) == HW_F && strcmp(note.b, "out of DMA memory") == 0);
        CHECK(b->f->q[0].enable == 0);
        fake_plat_free(&fp);
        free(b->f);
        free(b);
    }
    /* the API's own results */
    {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fblk_t *b = add_blk(&fp, (1ull << 3) | (1ull << 40), 64, BLOCKS, 0);
        const hw_pci_dev_t *d = scan_first(&fp);
        vpci_t v;
        hw_line_init(&note);
        CHECK(vpci_attach(&v, &fp.plat, d, &note) == 0);
        CHECK(v.notify_mult == FV_MULT);
        /* only what is both wanted and offered, plus VERSION_1 */
        CHECK(vpci_negotiate(&v, (1ull << 0) | (1ull << 3) | (1ull << 41), &note) == 0);
        CHECK(v.features == ((1ull << 32) | (1ull << 3)) && b->f->taken == v.features);
        CHECK(vpci_negotiate(&v, (1ull << 40), &note) == 0);
        CHECK(v.features == ((1ull << 32) | (1ull << 40)) && b->f->taken == v.features);
        CHECK(vpci_queue(&v, 0, 1, &note) == -1); /* queue 1 absent */
        CHECK(vpci_queue(&v, 0, 0, &note) == 0 && v.q[0].depth == 64 && v.q[0].index == 0);
        b->f->refuse = 1;
        CHECK(vpci_negotiate(&v, 0, &note) == -1);
        b->f->refuse = 0;
        b->f->offered = 0;
        CHECK(vpci_negotiate(&v, 0, &note) == -1);
        b->f->stuck_reset = 1;
        CHECK(vpci_negotiate(&v, 0, &note) == -1);
        fp.devs[0]->cfg[1] &= ~0x00100000u;
        CHECK(vpci_attach(&v, &fp.plat, d, &note) == -1);
        fp.devs[0]->cfg[1] |= 0x00100000u;
        fp.devs[0]->cfg[0x40 / 4 + 3] = 0x37;
        CHECK(vpci_attach(&v, &fp.plat, d, &note) == -1);
        fake_plat_free(&fp);
        free(b->f);
        free(b);
    }
}

/* ==================================================================== scsi */
#define NT 8

typedef struct fscsi {
    fvirtio_t *f;
    uint8_t disk[BLOCKS * 512];
    uint32_t bs[NT];
    uint64_t blocks[NT];
    uint8_t type[NT]; /* 0xFF = nothing at that target */
    uint8_t ua[NT];   /* unit attention pending */
    uint32_t not_ready, cmds, inquiries, rc16, reads, writes, maxlen;
    uint8_t last_op;
    uint64_t last_lba;
    uint32_t last_count;
    int fail_io, bad_response, fixed_cdb, desc_sense;
    uint32_t resid; /* residual reported on every good read/write */
    int fail_op;    /* this opcode fails with CHECK CONDITION, fail_key */
    uint8_t fail_key;
    uint8_t status_all; /* nonzero: every command ends with this SCSI status */
    uint8_t sense_rc;   /* nonzero: the sense response code (else 0x70/0x72) */
    uint32_t sense_len; /* nonzero: the sense length reported (else 18) */
} fscsi_t;

static void scsi_resp(uint8_t *resp, uint8_t status, uint8_t response, uint32_t resid, uint8_t key,
                      int desc)
{
    memset(resp, 0, 108);
    fk_wr32(resp + 4, resid);
    resp[10] = status;
    resp[11] = response;
    if (status == 2) {
        fk_wr32(resp, 18);
        if (desc) {
            resp[12] = 0x72;
            resp[13] = key;
        } else {
            resp[12] = 0x70;
            resp[14] = key;
        }
    }
}

/* CHECK CONDITION with sense key `key`, in the format the knobs ask for. */
static void scsi_check(const fscsi_t *s, uint8_t *resp, uint8_t key)
{
    scsi_resp(resp, 2, 0, 0, 0, 0);
    fk_wr32(resp, s->sense_len ? s->sense_len : 18);
    uint8_t rc = s->sense_rc ? s->sense_rc : s->desc_sense ? 0x72 : 0x70;
    resp[12] = rc;
    if ((rc & 0x7E) == 0x72)
        resp[13] = key;
    else
        resp[14] = key;
}

/* Bytes a CDB must leave zero (reserved, control, and for INQUIRY the
 * EVPD/page fields): 1 = must be zero. */
static int cdb_dirty(const uint8_t *cdb)
{
    static const uint8_t inq[6] = {0, 1, 1, 1, 0, 1}, tur[6] = {0, 1, 1, 1, 1, 1},
                         rc10[10] = {0, 1, 1, 1, 1, 1, 1, 1, 1, 1},
                         rw10[10] = {0, 1, 0, 0, 0, 0, 1, 0, 0, 1},
                         rc16[16] = {0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1},
                         rw16[16] = {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1};
    const uint8_t *m = 0;
    uint32_t n = 0;
    switch (cdb[0]) {
    case 0x12:
        m = inq, n = 6;
        break;
    case 0x00:
        m = tur, n = 6;
        break;
    case 0x25:
        m = rc10, n = 10;
        break;
    case 0x28:
    case 0x2A:
        m = rw10, n = 10;
        break;
    case 0x9E:
        m = rc16, n = 16;
        break;
    case 0x88:
    case 0x8A:
        m = rw16, n = 16;
        break;
    default:
        return 0;
    }
    for (uint32_t i = 0; i < n; i++)
        if (m[i] && cdb[i]) return 1;
    for (uint32_t i = n; i < 32; i++) /* nothing past the CDB's own length */
        if (cdb[i]) return 1;
    return 0;
}

static uint32_t be(const uint8_t *b, uint32_t n)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++) v = (v << 8) | b[i];
    return v;
}

static void put_be(uint8_t *b, uint64_t v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) b[n - 1 - i] = (uint8_t) (v >> (8 * i));
}

static uint8_t cdb_op(const uint8_t *req)
{
    return req[19];
}

static int32_t scsi_serve(fvirtio_t *f, uint32_t q, fv_desc_t *d, uint32_t n)
{
    fscsi_t *s = (fscsi_t *) f->dev_state;
    if (q != 2 || n < 2 || d[0].w || d[0].len != 51) {
        f->strict++;
        return 0;
    }
    uint8_t *req = d[0].p, *cdb = req + 19, *resp = 0, *data = 0;
    uint32_t dlen = 0;
    int data_in = 0;
    if (n == 2 && d[1].w && d[1].len == 108) {
        resp = d[1].p;
    } else if (n == 3 && !d[1].w && d[2].w && d[2].len == 108) {
        data = d[1].p, dlen = d[1].len, resp = d[2].p;
    } else if (n == 3 && d[1].w && d[1].len == 108 && d[2].w) {
        resp = d[1].p, data = d[2].p, dlen = d[2].len, data_in = 1;
    } else {
        f->strict++;
        return 0;
    }
    s->cmds++;
    if (cdb_op(req) == 0x12) s->inquiries++;
    if (req[0] != 1 || req[2] != 0x40 || req[3] != 0) f->strict++; /* LUN 0, flat */
    for (uint32_t i = 4; i < 8; i++)
        if (req[i]) f->strict++;
    if (req[16] || req[17] || req[18]) f->strict++; /* task attr, prio, crn */
    if (cdb_dirty(cdb)) f->strict++;
    uint32_t t = req[1];
    if (t >= NT || s->type[t] == 0xFF) {
        scsi_resp(resp, 0, 3, 0, 0, 0);
        return 108;
    }
    if (s->bad_response) {
        scsi_resp(resp, 0, 9, 0, 0, 0);
        return 108;
    }
    uint8_t op = cdb[0];
    if (s->status_all) {
        scsi_resp(resp, s->status_all, 0, 0, 0, 0);
        return 108;
    }
    if (op != 0x12 && s->ua[t]) {
        s->ua[t]--;
        scsi_check(s, resp, 6);
        return 108;
    }
    if (op == s->fail_op) {
        scsi_check(s, resp, s->fail_key);
        return 108;
    }
    switch (op) {
    case 0x12: { /* INQUIRY */
        uint8_t inq[36] = {0};
        inq[0] = s->type[t];
        inq[4] = 31;
        memcpy(inq + 8, "ZXV     FAKE SCSI DISK  ", 24);
        uint32_t k = cdb[4] < 36 ? cdb[4] : 36;
        if (!data_in || dlen != cdb[4])
            f->strict++;
        else
            memcpy(data, inq, k);
        scsi_resp(resp, 0, 0, 0, 0, 0);
        return (int32_t) (108 + k);
    }
    case 0x00: /* TEST UNIT READY */
        if (data) f->strict++;
        if (s->not_ready) {
            s->not_ready--;
            scsi_check(s, resp, 2);
        } else {
            scsi_resp(resp, 0, 0, 0, 0, 0);
        }
        return 108;
    case 0x25: { /* READ CAPACITY (10) */
        uint64_t last = s->blocks[t] - 1;
        if (!data_in || dlen != 8)
            f->strict++;
        else {
            put_be(data, last > 0xFFFFFFFFull ? 0xFFFFFFFFull : last, 4);
            put_be(data + 4, s->bs[t], 4);
        }
        scsi_resp(resp, 0, 0, 0, 0, 0);
        return 116;
    }
    case 0x9E: /* READ CAPACITY (16) */
        s->rc16++;
        if (cdb[1] != 0x10 || !data_in || dlen != 32 || be(cdb + 10, 4) != 32)
            f->strict++;
        else {
            memset(data, 0, 32);
            put_be(data, s->blocks[t] - 1, 8);
            put_be(data + 8, s->bs[t], 4);
        }
        scsi_resp(resp, 0, 0, 0, 0, 0);
        return 140;
    case 0x28:
    case 0x2A:
    case 0x88:
    case 0x8A: {
        int wr = op == 0x2A || op == 0x8A;
        uint64_t lba;
        uint32_t cnt;
        if (op == 0x28 || op == 0x2A) {
            lba = be(cdb + 2, 4);
            cnt = be(cdb + 7, 2);
        } else {
            lba = ((uint64_t) be(cdb + 2, 4) << 32) | be(cdb + 6, 4);
            cnt = be(cdb + 10, 4);
        }
        s->last_op = op, s->last_lba = lba, s->last_count = cnt;
        uint32_t bytes = cnt * s->bs[t];
        if (dlen > s->maxlen) s->maxlen = dlen;
        if (!data || dlen != bytes || data_in == wr || lba + cnt > s->blocks[t]) {
            f->strict++;
            scsi_resp(resp, 2, 0, 0, 5, 0);
            return 108;
        }
        if (s->fail_io) {
            scsi_check(s, resp, 3);
            return 108;
        }
        for (uint32_t i = 0; i < bytes; i++) {
            uint8_t *at = &s->disk[(lba * s->bs[t] + i) % sizeof s->disk];
            if (wr)
                *at = data[i];
            else
                data[i] = *at;
        }
        if (wr)
            s->writes++;
        else
            s->reads++;
        scsi_resp(resp, 0, 0, s->resid, 0, 0);
        return (int32_t) (108 + (wr ? 0 : bytes));
    }
    }
    scsi_resp(resp, 2, 0, 0, 5, 0); /* illegal request */
    return 108;
}

static void scsi_cfg(fscsi_t *s, uint32_t nq, uint32_t max_sectors, uint16_t max_target)
{
    uint8_t *c = s->f->r + FV_DEVCFG;
    fk_wr32(c, nq);
    fk_wr32(c + 4, 126);
    fk_wr32(c + 8, max_sectors);
    fk_wr32(c + 12, 128);
    fk_wr32(c + 16, 8);
    fk_wr32(c + 20, 96);
    fk_wr32(c + 24, 32);
    c[28] = 0, c[29] = 0;
    c[30] = (uint8_t) max_target, c[31] = (uint8_t) (max_target >> 8);
    fk_wr32(c + 32, 16383);
}

static void scsi_tick(fake_dev_t *d, uint64_t now)
{
    (void) now;
    fvirtio_t *f = (fvirtio_t *) d->state;
    fscsi_t *s = (fscsi_t *) f->dev_state;
    if (s->fixed_cdb) fk_wr32(f->r + FV_DEVCFG + 24, 16);
    fv_tick(f);
}

static fscsi_t *add_scsi(fake_plat_t *fp, uint16_t device)
{
    const uint16_t qm[FV_QN] = {16, 16, 16, 0};
    fscsi_t *s = (fscsi_t *) calloc(1, sizeof *s);
    s->f = fv_new(fp, HW_BDF(0, 5, 0), device, 0x01, 0x00, 0, qm, scsi_tick);
    s->fail_op = -1;
    s->f->serve = scsi_serve;
    s->f->dev_state = s;
    memset(s->type, 0xFF, sizeof s->type);
    s->type[0] = 0;
    s->bs[0] = 512;
    s->blocks[0] = BLOCKS;
    scsi_cfg(s, 1, 0xFFFF, 255);
    disk_fill(s->disk, sizeof s->disk);
    return s;
}

static hw_verdict_t bind_scsi(fake_plat_t *fp, hw_registry_t *r, hw_line_t *note)
{
    memset(r, 0, sizeof *r);
    hw_line_init(note);
    hw_verdict_t v = vscsi_bind(&fp->plat, scan_first(fp), r, note);
    for (uint32_t i = 0; v == HW_T && i < r->n_blk; i++)
        name_next(r->blk[i]->name, "vsd", &g_vsd_last);
    return v;
}

static void free_scsi(fake_plat_t *fp, fscsi_t *s)
{
    fake_plat_free(fp);
    free(s->f);
    free(s);
}

static void test_scsi_happy(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fscsi_t *s = add_scsi(&fp, 0x1048);
    /* a disk at 0, a CD-ROM at 1, a 4 KB-sector disk at 2; all three come
     * out of reset with a unit attention, and the first TEST UNIT READY
     * finds the disk still spinning up twice */
    s->type[1] = 0x05, s->bs[1] = 2048, s->blocks[1] = 10;
    s->type[2] = 0x00, s->bs[2] = 4096, s->blocks[2] = 8;
    s->ua[0] = s->ua[1] = s->ua[2] = 1;
    s->not_ready = 2;
    hw_registry_t r;
    hw_line_t note;
    CHECK(bind_scsi(&fp, &r, &note) == HW_T);
    CHECK(r.n_blk == 2);
    hw_blk_t *a = r.blk[0], *b = r.blk[1];
    char want[160];
    snprintf(want, sizeof want,
             "virtio SCSI disk 64 x 512 B at target 0 as %s; 8 x 4096 B at target 2 as %s", a->name,
             b->name);
    CHECK(strcmp(note.b, want) == 0);
    if (strcmp(note.b, want) != 0) printf("  note '%s'\n", note.b);
    CHECK(strncmp(a->name, "vsd", 3) == 0 && strncmp(b->name, "vsd", 3) == 0);
    CHECK(strcmp(a->name, b->name) != 0);
    CHECK(a->block_size == 512 && a->blocks == BLOCKS);
    CHECK(b->block_size == 4096 && b->blocks == 8);
    CHECK(s->inquiries == NT); /* every target 0..7 asked */
    CHECK(s->f->q[2].size == 16 && s->f->q[2].enable && !s->f->q[0].enable);
    CHECK(s->f->status == 0x0F && s->f->taken == (1ull << 32));
    uint8_t buf[20 * 512];
    CHECK(a->read(a, 1, 1, buf) == 0 && memcmp(buf, "EFI PART", 8) == 0);
    CHECK(s->last_op == 0x28 && s->last_lba == 1 && s->last_count == 1);
    CHECK(a->read(a, 10, 20, buf) == 0);
    CHECK(memcmp(buf, s->disk + 10 * 512, 20 * 512) == 0);
    CHECK(a->read(a, 2, 3, buf + 1) == 0 && memcmp(buf + 1, s->disk + 2 * 512, 3 * 512) == 0);
    uint8_t w[3 * 512], keep[3 * 512];
    for (uint32_t i = 0; i < sizeof w; i++) w[i] = keep[i] = (uint8_t) (0xC3 ^ i);
    CHECK(a->write(a, 50, 3, w) == 0 && s->last_op == 0x2A && s->last_count == 3);
    CHECK(memcmp(s->disk + 50 * 512, keep, sizeof keep) == 0);
    memset(buf, 0, sizeof buf);
    CHECK(a->read(a, 50, 3, buf) == 0 && memcmp(buf, keep, sizeof keep) == 0);
    /* the 4 KB disk: block 1 is bytes 4096.. of the shared backing store */
    CHECK(b->read(b, 1, 2, buf) == 0 && s->last_count == 2);
    CHECK(memcmp(buf, s->disk + 4096, 8192) == 0);
    /* bounds, before the device sees anything */
    uint32_t c0 = s->cmds;
    CHECK(a->read(a, BLOCKS, 1, buf) != 0);
    CHECK(a->read(a, BLOCKS - 1, 2, buf) != 0);
    CHECK(b->write(b, 8, 1, buf) != 0);
    CHECK(s->cmds == c0);
    CHECK(a->read(a, BLOCKS - 1, 1, buf) == 0);
    /* one command moves up to 64 KB */
    static uint8_t whole[BLOCKS * 512];
    c0 = s->cmds;
    CHECK(a->read(a, 0, BLOCKS, whole) == 0 && s->cmds - c0 == 1);
    CHECK(memcmp(whole, s->disk, sizeof whole) == 0);
    for (int i = 0; i < 100; i++) CHECK(a->read(a, (uint64_t) (i % BLOCKS), 1, buf) == 0);
    CHECK(s->f->strict == 0);
    /* a unit attention mid-run is retried; a medium error is not */
    s->ua[0] = 1;
    CHECK(a->read(a, 1, 1, buf) == 0);
    s->fail_io = 1;
    c0 = s->cmds;
    CHECK(a->read(a, 1, 1, buf) == -1 && s->cmds - c0 == 1);
    CHECK(a->write(a, 1, 1, buf) == -1);
    s->fail_io = 0;
    s->resid = 0x01000000; /* a short transfer is an error, however large */
    CHECK(a->read(a, 1, 1, buf) == -1);
    s->resid = 1;
    CHECK(a->read(a, 1, 1, buf) == -1);
    s->resid = 0;
    s->bad_response = 1;
    CHECK(a->read(a, 1, 1, buf) == -1);
    s->bad_response = 0;
    /* a unit attention that never clears: retried a bounded number of times */
    s->ua[0] = 100;
    c0 = s->cmds;
    CHECK(a->read(a, 1, 1, buf) == -1 && s->cmds - c0 == 8);
    s->ua[0] = 0;
    /* descriptor-format sense is understood too */
    s->desc_sense = 1;
    s->ua[0] = 1;
    CHECK(a->read(a, 1, 1, buf) == 0);
    CHECK(s->f->strict == 0);
    free_scsi(&fp, s);
}

static void test_scsi_big(void)
{
    /* past 2^32 blocks: READ CAPACITY (16), then READ (16) for LBAs past
     * 2^32 - 1 and READ (10) below */
    fake_plat_t fp;
    fake_plat_init(&fp);
    fscsi_t *s = add_scsi(&fp, 0x1004);
    s->blocks[0] = 0x100000010ull;
    hw_registry_t r;
    hw_line_t note;
    CHECK(bind_scsi(&fp, &r, &note) == HW_T);
    hw_blk_t *a = r.blk[0];
    CHECK(s->rc16 == 1 && a->blocks == 0x100000010ull);
    uint8_t buf[3 * 512];
    CHECK(a->read(a, 0xFFFFFFFEull, 1, buf) == 0 && s->last_op == 0x28);
    CHECK(a->read(a, 0xFFFFFFFFull, 1, buf) == 0 && s->last_op == 0x28);
    CHECK(a->read(a, 0xFFFFFFFFull, 2, buf) == 0 && s->last_op == 0x88);
    CHECK(s->last_lba == 0xFFFFFFFFull && s->last_count == 2);
    CHECK(a->write(a, 0x100000005ull, 3, buf) == 0 && s->last_op == 0x8A);
    CHECK(s->last_lba == 0x100000005ull);
    CHECK(a->read(a, 0x10000000Full, 1, buf) == 0);
    CHECK(a->read(a, 0x100000010ull, 1, buf) != 0);
    CHECK(s->f->strict == 0);
    free_scsi(&fp, s);
    /* exactly 2^32 blocks: READ CAPACITY (10) reports FFFFFFFFh, so (16) */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1004);
    s->blocks[0] = 0x100000000ull;
    CHECK(bind_scsi(&fp, &r, &note) == HW_T);
    CHECK(s->rc16 == 1 && r.blk[0]->blocks == 0x100000000ull);
    free_scsi(&fp, s);
    /* one block fewer still fits READ CAPACITY (10) */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1004);
    s->blocks[0] = 0xFFFFFFFFull;
    CHECK(bind_scsi(&fp, &r, &note) == HW_T);
    CHECK(s->rc16 == 0 && r.blk[0]->blocks == 0xFFFFFFFFull);
    free_scsi(&fp, s);
    /* max_sectors 16: 8 KB per command */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    scsi_cfg(s, 1, 16, 255);
    CHECK(bind_scsi(&fp, &r, &note) == HW_T);
    static uint8_t whole[BLOCKS * 512];
    uint32_t c0 = s->cmds;
    CHECK(r.blk[0]->read(r.blk[0], 0, BLOCKS, whole) == 0 && s->cmds - c0 == 4);
    CHECK(s->maxlen == 8192 && memcmp(whole, s->disk, sizeof whole) == 0);
    free_scsi(&fp, s);
}

static void test_scsi_faults(void)
{
    hw_registry_t r;
    hw_line_t note;
    fake_plat_t fp;
    /* no disk anywhere */
    fake_plat_init(&fp);
    fscsi_t *s = add_scsi(&fp, 0x1048);
    s->type[0] = 0xFF;
    CHECK(bind_scsi(&fp, &r, &note) == HW_N && strcmp(note.b, "no disk on the bus") == 0);
    free_scsi(&fp, s);
    /* only a CD-ROM */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    s->type[0] = 0x05;
    CHECK(bind_scsi(&fp, &r, &note) == HW_N && r.n_blk == 0);
    free_scsi(&fp, s);
    /* a qualifier saying the LUN is not attached */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    s->type[0] = 0x20;
    CHECK(bind_scsi(&fp, &r, &note) == HW_N && r.n_blk == 0);
    free_scsi(&fp, s);
    /* block sizes it cannot use: 1000, 256, 8192 */
    const uint32_t odd[3] = {1000, 256, 8192};
    for (uint32_t i = 0; i < 3; i++) {
        fake_plat_init(&fp);
        s = add_scsi(&fp, 0x1048);
        s->bs[0] = odd[i];
        CHECK(bind_scsi(&fp, &r, &note) == HW_N);
        CHECK(strcmp(note.b, "only disks with a block size it cannot use") == 0);
        free_scsi(&fp, s);
    }
    /* 4096-byte blocks but max_sectors 4 (2 KB a command): unusable too */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    s->bs[0] = 4096;
    scsi_cfg(s, 1, 4, 255);
    CHECK(bind_scsi(&fp, &r, &note) == HW_N);
    free_scsi(&fp, s);
    /* max_target 0: target 0 only */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    s->type[3] = 0, s->bs[3] = 512, s->blocks[3] = 4;
    scsi_cfg(s, 1, 0xFFFF, 0);
    CHECK(bind_scsi(&fp, &r, &note) == HW_T && r.n_blk == 1 && s->inquiries == 1);
    free_scsi(&fp, s);
    /* max_target 3: targets 0..3 */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    s->type[3] = 0, s->bs[3] = 512, s->blocks[3] = 4;
    scsi_cfg(s, 1, 0xFFFF, 3);
    CHECK(bind_scsi(&fp, &r, &note) == HW_T && r.n_blk == 2 && s->inquiries == 4);
    free_scsi(&fp, s);
    /* more disks than the controller keeps: four registered */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    for (uint32_t t = 0; t < NT; t++) s->type[t] = 0, s->bs[t] = 512, s->blocks[t] = 4;
    CHECK(bind_scsi(&fp, &r, &note) == HW_T && r.n_blk == 4 && s->inquiries == 4);
    free_scsi(&fp, s);
    /* no request queue */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    scsi_cfg(s, 0, 0xFFFF, 255);
    CHECK(bind_scsi(&fp, &r, &note) == HW_B);
    CHECK(strcmp(note.b, "a SCSI controller with no request queue") == 0);
    CHECK(s->f->status == 0);
    free_scsi(&fp, s);
    /* a CDB size it will not change */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    s->fixed_cdb = 1;
    fk_wr32(s->f->r + FV_DEVCFG + 24, 16);
    CHECK(bind_scsi(&fp, &r, &note) == HW_B);
    CHECK(strcmp(note.b, "will not take a 32-byte CDB and 96-byte sense") == 0);
    free_scsi(&fp, s);
    /* a sense size it does change: taken */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    fk_wr32(s->f->r + FV_DEVCFG + 20, 252);
    CHECK(bind_scsi(&fp, &r, &note) == HW_T);
    CHECK(fk_rd32(s->f->r + FV_DEVCFG + 20) == 96 && fk_rd32(s->f->r + FV_DEVCFG + 24) == 32);
    free_scsi(&fp, s);
    /* a controller that never answers */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    s->f->deaf = 1;
    CHECK(bind_scsi(&fp, &r, &note) == HW_F);
    CHECK(strcmp(note.b, "a command to target 0 failed or timed out") == 0);
    CHECK(s->f->status == 0);
    free_scsi(&fp, s);
    /* every response a transport failure */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    s->bad_response = 1;
    CHECK(bind_scsi(&fp, &r, &note) == HW_F);
    free_scsi(&fp, s);
    /* no request queue in the transport (queue 2 absent) */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    s->f->q[2].max = 0;
    fv_reset(s->f);
    CHECK(bind_scsi(&fp, &r, &note) == HW_F);
    CHECK(strcmp(note.b, "a virtqueue it needs is missing") == 0);
    free_scsi(&fp, s);
    /* out of DMA memory for the bounce buffer */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    fp.dma_fail_after = 4;
    CHECK(bind_scsi(&fp, &r, &note) == HW_F && strcmp(note.b, "out of DMA memory") == 0);
    free_scsi(&fp, s);
    /* a full block registry: nothing registered, nothing claimed */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    memset(&r, 0, sizeof r);
    r.n_blk = HW_MAX_BLK;
    hw_line_init(&note);
    CHECK(vscsi_bind(&fp.plat, scan_first(&fp), &r, &note) == HW_N);
    CHECK(r.n_blk == HW_MAX_BLK);
    free_scsi(&fp, s);
}

static void test_scsi_more(void)
{
    hw_registry_t r;
    hw_line_t note;
    fake_plat_t fp;
    fscsi_t *s;
    uint8_t buf[512];
    static uint8_t big[200 * 512];
    /* every byte of LBAs and capacities, both CDB sizes */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    s->blocks[0] = 0x123456800ull;
    s->bs[0] = 512;
    CHECK(bind_scsi(&fp, &r, &note) == HW_T && r.blk[0]->blocks == 0x123456800ull);
    CHECK(r.blk[0]->read(r.blk[0], 0x12345678ull, 1, buf) == 0);
    CHECK(s->last_op == 0x28 && s->last_lba == 0x12345678ull);
    CHECK(r.blk[0]->write(r.blk[0], 0x123456789ull, 1, buf) == 0);
    CHECK(s->last_op == 0x8A && s->last_lba == 0x123456789ull);
    CHECK(r.blk[0]->read(r.blk[0], 0x123456800ull, 0, buf) == -1);
    CHECK(s->f->strict == 0);
    free_scsi(&fp, s);
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    s->blocks[0] = 0x12345679ull;
    s->bs[0] = 4096;
    CHECK(bind_scsi(&fp, &r, &note) == HW_T);
    CHECK(r.blk[0]->blocks == 0x12345679ull && r.blk[0]->block_size == 4096 && s->rc16 == 0);
    free_scsi(&fp, s);
    /* a READ (10) of 128 blocks: the count's high byte is 0 */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    s->blocks[0] = 1024;
    CHECK(bind_scsi(&fp, &r, &note) == HW_T);
    uint32_t c0 = s->cmds;
    CHECK(r.blk[0]->read(r.blk[0], 0, 128, big) == 0 && s->cmds - c0 == 1 && s->last_count == 128);
    CHECK(r.blk[0]->read(r.blk[0], 0, 200, big) == 0 && s->cmds - c0 == 3 && s->maxlen == 65536);
    CHECK(s->f->strict == 0);
    free_scsi(&fp, s);
    /* max_sectors 0 (no limit stated), 0xFFFF, 128 and 127 */
    const uint32_t ms[4][2] = {{0, 65536}, {0xFFFF, 65536}, {128, 65536}, {127, 65024}};
    for (uint32_t i = 0; i < 4; i++) {
        fake_plat_init(&fp);
        s = add_scsi(&fp, 0x1048);
        s->blocks[0] = 1024;
        scsi_cfg(s, 1, ms[i][0], 255);
        CHECK(bind_scsi(&fp, &r, &note) == HW_T);
        c0 = s->cmds;
        CHECK(r.blk[0]->read(r.blk[0], 0, 200, big) == 0 && s->cmds - c0 == 2);
        CHECK(s->maxlen == ms[i][1]);
        free_scsi(&fp, s);
    }
    /* a 4 KB disk whose controller moves exactly 4 KB a command */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    s->bs[0] = 4096, s->blocks[0] = 8;
    scsi_cfg(s, 1, 8, 255);
    CHECK(bind_scsi(&fp, &r, &note) == HW_T && r.blk[0]->block_size == 4096);
    free_scsi(&fp, s);
    /* each step of the probe failing outright (no retry): no disk */
    const struct {
        int op;
        uint8_t key;
        uint64_t blocks;
        uint32_t cmds;
    } pf[] = {{0x12, 5, BLOCKS, 8},
              {0x00, 3, BLOCKS, 9},
              {0x25, 5, BLOCKS, 10},
              {0x9E, 5, 0x100000010ull, 11}};
    for (uint32_t i = 0; i < sizeof pf / sizeof pf[0]; i++) {
        fake_plat_init(&fp);
        s = add_scsi(&fp, 0x1048);
        s->fail_op = pf[i].op, s->fail_key = pf[i].key, s->blocks[0] = pf[i].blocks;
        CHECK(bind_scsi(&fp, &r, &note) == HW_N && r.n_blk == 0);
        CHECK(strcmp(note.b, "no disk on the bus") == 0);
        CHECK(s->cmds == pf[i].cmds);
        free_scsi(&fp, s);
    }
    /* a capacity of zero blocks: READ CAPACITY (16) says all ones, no disk */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    s->blocks[0] = 0;
    CHECK(bind_scsi(&fp, &r, &note) == HW_N && s->rc16 == 1);
    free_scsi(&fp, s);
    /* BUSY on every command is a failed controller, not an empty bus */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    s->status_all = 0x08;
    CHECK(bind_scsi(&fp, &r, &note) == HW_F);
    CHECK(strcmp(note.b, "a command to target 0 failed or timed out") == 0);
    free_scsi(&fp, s);
    /* sense formats: which ones a unit attention is recognised in */
    const struct {
        int desc;
        uint8_t rc;
        uint32_t len;
        int ok;
    } sf[] = {{0, 0, 3, 1},    {0, 0, 2, 0},    {1, 0, 2, 1},    {1, 0, 1, 0},
              {0, 0x71, 0, 1}, {1, 0x73, 0, 1}, {0, 0x01, 0, 0}, {0, 0x7F, 0, 0}};
    for (uint32_t i = 0; i < sizeof sf / sizeof sf[0]; i++) {
        fake_plat_init(&fp);
        s = add_scsi(&fp, 0x1048);
        CHECK(bind_scsi(&fp, &r, &note) == HW_T);
        s->desc_sense = sf[i].desc, s->sense_rc = sf[i].rc, s->sense_len = sf[i].len;
        s->ua[0] = 1;
        c0 = s->cmds;
        uint64_t t0 = fp.now_us;
        int rr = r.blk[0]->read(r.blk[0], 1, 1, buf);
        if ((rr == 0) != sf[i].ok) printf("  sense case %u: read %d\n", i, rr);
        CHECK((rr == 0) == sf[i].ok);
        CHECK(s->cmds - c0 == (sf[i].ok ? 2u : 1u));
        if (sf[i].ok) CHECK(fp.now_us - t0 == 1020); /* 10 us, 1 ms pause, 10 us */
        free_scsi(&fp, s);
    }
    /* a medium error in descriptor format is not retried either */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    CHECK(bind_scsi(&fp, &r, &note) == HW_T);
    s->desc_sense = 1, s->fail_io = 1;
    c0 = s->cmds;
    CHECK(r.blk[0]->read(r.blk[0], 1, 1, buf) == -1 && s->cmds - c0 == 1);
    free_scsi(&fp, s);
    /* features the device offers (INOUT, HOTPLUG, CHANGE, T10_PI): none taken */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    s->f->offered |= 0xF;
    CHECK(bind_scsi(&fp, &r, &note) == HW_T && s->f->taken == (1ull << 32));
    free_scsi(&fp, s);
    /* queue counts with only a high byte set are still queues */
    const uint32_t nq[3] = {0x100, 0x10000, 0x1000000};
    for (uint32_t i = 0; i < 3; i++) {
        fake_plat_init(&fp);
        s = add_scsi(&fp, 0x1048);
        scsi_cfg(s, nq[i], 0xFFFF, 255);
        CHECK(bind_scsi(&fp, &r, &note) == HW_T);
        free_scsi(&fp, s);
    }
    /* sizes past one byte are rewritten whole */
    fake_plat_init(&fp);
    s = add_scsi(&fp, 0x1048);
    fk_wr32(s->f->r + FV_DEVCFG + 20, 0x1FC);
    fk_wr32(s->f->r + FV_DEVCFG + 24, 0x01000020);
    CHECK(bind_scsi(&fp, &r, &note) == HW_T);
    CHECK(fk_rd32(s->f->r + FV_DEVCFG + 20) == 96 && fk_rd32(s->f->r + FV_DEVCFG + 24) == 32);
    free_scsi(&fp, s);
    /* each of the driver's own allocations failing alone, before any queue */
    for (uint32_t n = 1; n <= 4; n++) {
        fake_plat_init(&fp);
        s = add_scsi(&fp, 0x1048);
        fp.dma_fail_one = n;
        CHECK(bind_scsi(&fp, &r, &note) == HW_F && strcmp(note.b, "out of DMA memory") == 0);
        CHECK(s->f->q[2].enable == 0);
        free_scsi(&fp, s);
    }
}

/* ===================================================================== net */
typedef struct fnet {
    fvirtio_t *f;
    peer_t peer;
    uint32_t tx, rx, hdr_bad;
    int over; /* report the next frame one byte longer than its buffer */
} fnet_t;

static int32_t net_serve(fvirtio_t *f, uint32_t q, fv_desc_t *d, uint32_t n)
{
    fnet_t *e = (fnet_t *) f->dev_state;
    if (q == 1) {
        if (n != 1 || d[0].w || d[0].len < 12 + 14) {
            f->strict++;
            return 0;
        }
        for (uint32_t i = 0; i < 12; i++)
            if (d[0].p[i]) e->hdr_bad++;
        e->tx++;
        peer_on_tx(&e->peer, d[0].p + 12, d[0].len - 12);
        return 0;
    }
    if (q != 0 || n != 1 || !d[0].w) {
        f->strict++;
        return 0;
    }
    peer_frame_t *fr = peer_front(&e->peer);
    if (!fr) return -1;
    if (fr->len + 12 > d[0].len) {
        f->strict++;
        peer_pop(&e->peer);
        return -1;
    }
    memset(d[0].p, 0, 12);
    d[0].p[10] = 1; /* num_buffers */
    memcpy(d[0].p + 12, fr->b, fr->len);
    uint32_t len = fr->len;
    if (fr->err) len = 5; /* a runt the driver must drop */
    if (e->over) {
        e->over = 0;
        len = d[0].len - 12 + 1;
    }
    peer_pop(&e->peer);
    e->rx++;
    return (int32_t) (len + 12);
}

static void net_tick(fake_dev_t *d, uint64_t now)
{
    (void) now;
    fvirtio_t *f = (fvirtio_t *) d->state;
    fv_tick(f);
    fnet_t *e = (fnet_t *) f->dev_state;
    if ((f->status & 4) && f->q[0].enable && e->peer.count && !f->deaf) fv_serve_queue(f, 0);
}

static const uint8_t mac0[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};

static fnet_t *add_net(fake_plat_t *fp, uint64_t features, uint16_t rxmax, uint16_t status)
{
    const uint16_t qm[FV_QN] = {rxmax, 256, 0, 0};
    fnet_t *e = (fnet_t *) calloc(1, sizeof *e);
    e->f = fv_new(fp, HW_BDF(0, 3, 0), 0x1041, 0x02, 0x00, features, qm, net_tick);
    e->f->serve = net_serve;
    e->f->dev_state = e;
    memcpy(e->f->r + FV_DEVCFG, mac0, 6);
    e->f->r[FV_DEVCFG + 6] = (uint8_t) status;
    return e;
}

static hw_verdict_t bind_net(fake_plat_t *fp, hw_registry_t *r, hw_line_t *note)
{
    memset(r, 0, sizeof *r);
    hw_line_init(note);
    return vnet_bind(&fp->plat, scan_first(fp), r, note);
}

static void free_net(fake_plat_t *fp, fnet_t *e)
{
    fake_plat_free(fp);
    free(e->f);
    free(e);
}

#define F_MAC    (1ull << 5)
#define F_STATUS (1ull << 16)

static void test_net_happy(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fnet_t *e = add_net(&fp, F_MAC | F_STATUS | (1ull << 0), 256, 1);
    hw_registry_t r;
    hw_line_t note;
    CHECK(bind_net(&fp, &r, &note) == HW_T);
    CHECK(strcmp(note.b, "virtio NIC 52:54:00:12:34:56 link up as eth0") == 0);
    CHECK(r.n_nic == 1);
    hw_nic_t *n = r.nic[0];
    CHECK(memcmp(n->mac, mac0, 6) == 0 && n->link_up == 1);
    CHECK(n->on_pci == 1 && n->bdf == HW_BDF(0, 3, 0));
    CHECK(e->f->taken == ((1ull << 32) | F_MAC | F_STATUS)); /* CSUM (bit 0) not taken */
    CHECK(e->f->q[0].size == 64 && e->f->q[1].size == 64);
    CHECK(e->f->status == 0x0F);
    /* sixteen receive buffers posted, the device told */
    uint8_t *av = (uint8_t *) fk_ptr(e->f->q[0].avail);
    CHECK((av[2] | (av[3] << 8)) == 16);
    fp.plat.delay_us(fp.plat.ctx, 1); /* let the fake see the notification */
    CHECK(e->f->q[0].kicks == 1);
    /* the boot self-test: DHCPDISCOVER out, the peer's offer back */
    CHECK(hw_selftest_all(&fp.plat, &r) == 1);
    CHECK(e->peer.discovers == 1 && e->peer.offers == 1);
    CHECK(fake_log_has(&fp, "[SELFTEST OK] eth0"));
    /* frames out and echoed back, byte for byte, many times round the rings */
    uint8_t f[1514], g[2048];
    for (uint32_t i = 0; i < 200; i++) {
        uint32_t len = 14 + (i * 37) % 1500;
        peer_mk(f, peer_mac, mac0, len, i);
        CHECK(n->send(n, f, len) == 0);
        int got = n->recv(n, g, sizeof g);
        CHECK(got == (int) len);
        CHECK(memcmp(g + 12, f + 12, len - 12) == 0 && memcmp(g, mac0, 6) == 0);
    }
    CHECK(n->recv(n, g, sizeof g) == 0);
    CHECK(e->hdr_bad == 0 && e->f->strict == 0);
    /* the largest frame, and one byte past it */
    peer_mk(f, peer_mac, mac0, 1514, 9);
    CHECK(n->send(n, f, 1514) == 0);
    CHECK(n->recv(n, g, sizeof g) == 1514);
    CHECK(n->send(n, f, 1515) == -1);
    CHECK(n->send(n, f, 13) == -1);
    CHECK(n->send(n, 0, 60) == -1);
    /* a frame longer than the caller's buffer is cut to it */
    peer_mk(f, peer_mac, mac0, 100, 3);
    CHECK(n->send(n, f, 100) == 0);
    CHECK(n->recv(n, g, 40) == 40 && memcmp(g + 12, f + 12, 28) == 0);
    CHECK(n->recv(n, g, 0) == 0 && n->recv(n, 0, 10) == 0);
    /* a runt is dropped and the next frame still arrives */
    peer_mk(f, peer_mac, mac0, 60, 4);
    peer_push(&e->peer, f, 60, 1);
    peer_push(&e->peer, f, 60, 0);
    fp.plat.delay_us(fp.plat.ctx, 1);
    CHECK(n->recv(n, g, sizeof g) == 60);
    CHECK(n->recv(n, g, sizeof g) == 0);
    /* more frames waiting than buffers posted: all arrive, in order */
    for (uint32_t i = 0; i < 40; i++) {
        peer_mk(f, mac0, peer_mac, 64, i);
        peer_push(&e->peer, f, 64, 0);
    }
    uint32_t in = 0;
    for (uint32_t spin = 0; spin < 200 && in < 40; spin++) {
        fp.plat.delay_us(fp.plat.ctx, 1);
        int got = n->recv(n, g, sizeof g);
        if (got) {
            peer_mk(f, mac0, peer_mac, 64, in);
            CHECK(got == 64 && memcmp(g, f, 64) == 0);
            in++;
        }
    }
    CHECK(in == 40);
    CHECK(e->f->strict == 0);
    /* the seal: phy_off resets the device, then PCI decode and DMA go off */
    uint32_t resets = e->f->resets;
    CHECK(hw_nic_power_off(&fp.plat, n) == HW_NIC_OFF_PCI);
    CHECK(e->f->resets == resets + 1 && e->f->status == 0 && !e->f->q[0].enable);
    CHECK((fp.devs[0]->cfg[1] & 0x6) == 0 && n->link_up == 0);
    free_net(&fp, e);
}

static void test_net_shapes(void)
{
    hw_registry_t r;
    hw_line_t note;
    fake_plat_t fp;
    /* no MAC feature: its own locally administered address */
    fake_plat_init(&fp);
    fnet_t *e = add_net(&fp, F_STATUS, 256, 1);
    CHECK(bind_net(&fp, &r, &note) == HW_T);
    CHECK(strcmp(note.b, "virtio NIC 02:5a:58:56:00:00 (its own: the device has none) link up "
                         "as eth0") == 0);
    if (strncmp(note.b, "virtio NIC 02", 13) != 0) printf("  note '%s'\n", note.b);
    free_net(&fp, e);
    /* the second NIC's own address and name follow the registry */
    fake_plat_init(&fp);
    e = add_net(&fp, 0, 256, 0);
    memset(&r, 0, sizeof r);
    r.n_nic = 1;
    hw_line_init(&note);
    CHECK(vnet_bind(&fp.plat, scan_first(&fp), &r, &note) == HW_T);
    CHECK(r.nic[1]->mac[5] == 1 && strcmp(r.nic[1]->name, "eth1") == 0);
    CHECK(r.nic[1]->link_up == 1); /* no STATUS feature: up, whatever the field says */
    free_net(&fp, e);
    /* STATUS says the cable is out */
    fake_plat_init(&fp);
    e = add_net(&fp, F_MAC | F_STATUS, 256, 0);
    CHECK(bind_net(&fp, &r, &note) == HW_T);
    CHECK(strcmp(note.b, "virtio NIC 52:54:00:12:34:56 link down as eth0") == 0);
    CHECK(r.nic[0]->link_up == 0);
    free_net(&fp, e);
    /* a group address: held */
    fake_plat_init(&fp);
    e = add_net(&fp, F_MAC, 256, 1);
    e->f->r[FV_DEVCFG] = 0x01;
    CHECK(bind_net(&fp, &r, &note) == HW_B && r.n_nic == 0 && e->f->status == 0);
    free_net(&fp, e);
    /* a receive queue too short for sixteen buffers */
    fake_plat_init(&fp);
    e = add_net(&fp, F_MAC, 8, 1);
    CHECK(bind_net(&fp, &r, &note) == HW_F);
    CHECK(strcmp(note.b, "a receive queue shorter than 16") == 0);
    free_net(&fp, e);
    /* exactly sixteen is enough */
    fake_plat_init(&fp);
    e = add_net(&fp, F_MAC, 16, 1);
    CHECK(bind_net(&fp, &r, &note) == HW_T);
    free_net(&fp, e);
    /* no transmit queue */
    fake_plat_init(&fp);
    e = add_net(&fp, F_MAC, 64, 1);
    e->f->q[1].max = 0;
    fv_reset(e->f);
    CHECK(bind_net(&fp, &r, &note) == HW_F);
    CHECK(strcmp(note.b, "a virtqueue it needs is missing") == 0);
    free_net(&fp, e);
    /* a full NIC table */
    fake_plat_init(&fp);
    e = add_net(&fp, F_MAC, 64, 1);
    memset(&r, 0, sizeof r);
    r.n_nic = HW_MAX_NIC;
    hw_line_init(&note);
    CHECK(vnet_bind(&fp.plat, scan_first(&fp), &r, &note) == HW_N);
    CHECK(fp.dma_calls == 0);
    free_net(&fp, e);
    /* out of DMA memory for the buffers */
    fake_plat_init(&fp);
    e = add_net(&fp, F_MAC, 64, 1);
    fp.dma_fail_after = 2;
    CHECK(bind_net(&fp, &r, &note) == HW_F && strcmp(note.b, "out of DMA memory") == 0);
    free_net(&fp, e);
    /* a device that stops taking frames: send gives up */
    fake_plat_init(&fp);
    e = add_net(&fp, F_MAC, 64, 1);
    CHECK(bind_net(&fp, &r, &note) == HW_T);
    e->f->deaf = 1;
    uint8_t f[64];
    peer_mk(f, peer_mac, mac0, 64, 1);
    uint64_t t0 = fp.now_us;
    CHECK(r.nic[0]->send(r.nic[0], f, 64) == -1);
    CHECK(fp.now_us - t0 == 1000000);
    free_net(&fp, e);
}

static void test_net_more(void)
{
    hw_registry_t r;
    hw_line_t note;
    fake_plat_t fp;
    uint8_t f[2048], g[2048];
    fake_plat_init(&fp);
    fnet_t *e = add_net(&fp, F_MAC | F_STATUS, 64, 1);
    CHECK(bind_net(&fp, &r, &note) == HW_T);
    hw_nic_t *n = r.nic[0];
    /* a call with no room takes nothing: the frame waits for the next one */
    peer_mk(f, mac0, peer_mac, 80, 7);
    peer_push(&e->peer, f, 80, 0);
    fp.plat.delay_us(fp.plat.ctx, 1);
    CHECK(n->recv(n, g, 0) == 0);
    CHECK(n->recv(n, 0, sizeof g) == 0);
    CHECK(n->recv(n, g, sizeof g) == 80 && memcmp(g, f, 80) == 0);
    /* the shortest frame, and one byte under it */
    peer_mk(f, mac0, peer_mac, 14, 1);
    peer_push(&e->peer, f, 13, 0);
    peer_push(&e->peer, f, 14, 0);
    fp.plat.delay_us(fp.plat.ctx, 1);
    CHECK(n->recv(n, g, sizeof g) == 14);
    /* a frame filling its buffer exactly, and one the device says is longer */
    peer_mk(f, mac0, peer_mac, 2036, 2);
    peer_push(&e->peer, f, 2036, 0);
    fp.plat.delay_us(fp.plat.ctx, 1);
    CHECK(n->recv(n, g, sizeof g) == 2036 && memcmp(g, f, 2036) == 0);
    e->over = 1;
    peer_push(&e->peer, f, 100, 0);
    peer_push(&e->peer, f, 60, 0);
    fp.plat.delay_us(fp.plat.ctx, 1);
    CHECK(n->recv(n, g, sizeof g) == 60);
    /* room for a little more than the frame: the frame's length, no more */
    peer_mk(f, mac0, peer_mac, 100, 3);
    peer_push(&e->peer, f, 100, 0);
    fp.plat.delay_us(fp.plat.ctx, 1);
    CHECK(n->recv(n, g, 105) == 100);
    CHECK(e->f->strict == 0);
    /* phy_off itself: MAC stopped, no PHY to power down */
    CHECK(n->phy_off(n) == 1 && n->link_up == 0 && e->f->status == 0);
    free_net(&fp, e);
    /* a device that never takes frames: each send times out, and once every
     * transmit descriptor is stuck with the device, sends are refused */
    fake_plat_init(&fp);
    e = add_net(&fp, F_MAC, 64, 1);
    CHECK(bind_net(&fp, &r, &note) == HW_T);
    e->f->deaf = 1;
    peer_mk(f, peer_mac, mac0, 64, 1);
    for (int i = 0; i < 64; i++) CHECK(r.nic[0]->send(r.nic[0], f, 64) == -1);
    CHECK(r.nic[0]->send(r.nic[0], f, 64) == -2);
    free_net(&fp, e);
    /* each of the driver's own allocations failing alone, before any queue */
    for (uint32_t k = 1; k <= 3; k++) {
        fake_plat_init(&fp);
        e = add_net(&fp, F_MAC, 64, 1);
        fp.dma_fail_one = k;
        CHECK(bind_net(&fp, &r, &note) == HW_F && strcmp(note.b, "out of DMA memory") == 0);
        CHECK(e->f->q[0].enable == 0 && r.n_nic == 0);
        free_net(&fp, e);
    }
}

/* ==================================================================== probe */
static void test_probe(void)
{
    hw_pci_dev_t d;
    memset(&d, 0, sizeof d);
    d.vendor = 0x1AF4;
    struct {
        uint16_t id;
        const char *drv;
    } t[] = {{0x1000, "vnet"}, {0x1041, "vnet"},  {0x1001, "vblk"},
             {0x1042, "vblk"}, {0x1004, "vscsi"}, {0x1048, "vscsi"}};
    for (uint32_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        d.device = t[i].id;
        const hw_driver_t *m = hw_match(hw_drivers, hw_n_drivers, &d);
        CHECK(m && strcmp(m->name, t[i].drv) == 0);
    }
    /* other virtio devices (GPU, console, entropy) are not claimed by these */
    const uint16_t other[] = {0x1050, 0x1003, 0x1005, 0x1043, 0x1044};
    for (uint32_t i = 0; i < sizeof other / sizeof other[0]; i++) {
        d.device = other[i];
        d.class_code = 0x07;
        const hw_driver_t *m = hw_match(hw_drivers, hw_n_drivers, &d);
        CHECK(!m || (strncmp(m->name, "v", 1) != 0));
    }
    /* a whole probe: a virtio-scsi disk and a virtio NIC, as a cloud VM has */
    fake_plat_t fp;
    fake_plat_init(&fp);
    fscsi_t *s = add_scsi(&fp, 0x1048);
    const uint16_t qm[FV_QN] = {256, 256, 0, 0};
    fnet_t *e = (fnet_t *) calloc(1, sizeof *e);
    e->f = fv_new(&fp, HW_BDF(0, 3, 0), 0x1041, 0x02, 0x00, F_MAC | F_STATUS, qm, net_tick);
    e->f->serve = net_serve;
    e->f->dev_state = e;
    memcpy(e->f->r + FV_DEVCFG, mac0, 6);
    e->f->r[FV_DEVCFG + 6] = 1;
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_probe_stats_t st = hw_probe_all(&fp.plat, &r);
    CHECK(st.functions == 2 && st.bound == 2 && st.failed == 0);
    CHECK(r.n_blk == 1 && r.n_nic == 1);
    CHECK(fake_log_has(&fp, "[DRIVER ONLINE] vscsi 00:05.0 1af4:1048 virtio SCSI disk 64 x 512"));
    CHECK(fake_log_has(&fp, "[DRIVER ONLINE] vnet 00:03.0 1af4:1041 virtio NIC 52:54:00:12:34:56"));
    CHECK(hw_selftest_all(&fp.plat, &r) == 2);
    CHECK(fake_log_has(&fp, "read LBA 1: GPT header 'EFI PART'"));
    CHECK(s->f->strict == 0 && e->f->strict == 0);
    fake_plat_free(&fp);
    free(s->f);
    free(s);
    free(e->f);
    free(e);
}

int main(void)
{
    test_blk_happy();
    test_blk_shapes();
    test_blk_more();
    test_transport_faults();
    test_transport_shapes();
    test_scsi_happy();
    test_scsi_big();
    test_scsi_faults();
    test_scsi_more();
    test_net_happy();
    test_net_shapes();
    test_net_more();
    test_probe();
    printf("test_virtio: %d checks, %d failures\n", fk_checks, fk_failures);
    return fk_failures ? 1 : 0;
}
