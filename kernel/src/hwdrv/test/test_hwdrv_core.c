/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_hwdrv_core.c — the shared core: helpers, line builder, PCI scan and BAR
 * sizing, capability walk, registry, driver matching, probe logging, and the
 * boot self-tests (disk GPT read, DHCP discover/offer, key echo, audio DMA). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hwfake.h"

/* ---- helpers --------------------------------------------------------------- */
static void test_helpers(void)
{
    uint8_t a[16], b[16];
    hw_memset(a, 0x5A, sizeof a);
    for (int i = 0; i < 16; i++) CHECK(a[i] == 0x5A);
    hw_memset(a, 0, 0);
    CHECK(a[0] == 0x5A);
    for (int i = 0; i < 16; i++) b[i] = (uint8_t) i;
    hw_memcpy(a, b, 16);
    CHECK(memcmp(a, b, 16) == 0);
    CHECK(hw_memeq(a, b, 16));
    a[15] ^= 1;
    CHECK(!hw_memeq(a, b, 16));
    CHECK(hw_memeq(a, b, 15));
    a[0] ^= 1;
    CHECK(!hw_memeq(a, b, 1));
    CHECK(hw_memeq(a, b, 0));

    uint8_t r[16] = {0};
    hw_wr32(r, 4, 0x11223344);
    CHECK(hw_rd32(r, 4) == 0x11223344);
    CHECK(hw_rd16(r, 4) == 0x3344 && hw_rd16(r, 6) == 0x1122);
    CHECK(hw_rd8(r, 7) == 0x11);
    hw_wr16(r, 0, 0xBEEF);
    CHECK(hw_rd8(r, 0) == 0xEF && hw_rd8(r, 1) == 0xBE);
    hw_wr8(r, 2, 0x7F);
    CHECK(hw_rd8(r, 2) == 0x7F);
    hw_wr64(r, 8, 0x0123456789ABCDEFULL);
    CHECK(hw_rd32(r, 8) == 0x89ABCDEF && hw_rd32(r, 12) == 0x01234567);
    CHECK(hw_rd64(r, 8) == 0x0123456789ABCDEFULL);
}

static void test_line(void)
{
    hw_line_t l;
    hw_line_init(&l);
    CHECK(l.n == 0 && l.b[0] == 0);
    hw_line_s(&l, "a");
    hw_line_u(&l, 0);
    hw_line_c(&l, ' ');
    hw_line_u(&l, 18446744073709551615ULL);
    CHECK(strcmp(l.b, "a0 18446744073709551615") == 0);
    hw_line_init(&l);
    hw_line_x(&l, 0xAB, 4);
    hw_line_c(&l, '|');
    hw_line_x(&l, 0x1234, 2); /* only the low digits */
    hw_line_c(&l, '|');
    hw_line_x(&l, 0xF, 0); /* 0 digits means 16 */
    CHECK(strcmp(l.b, "00ab|34|000000000000000f") == 0);
    hw_line_init(&l);
    hw_line_x(&l, 0x123456789ABCDEF0ULL, 17); /* clamps to 16 */
    CHECK(strcmp(l.b, "123456789abcdef0") == 0);
    hw_line_init(&l);
    hw_line_bdf(&l, HW_BDF(0x1A, 0x1F, 7));
    CHECK(strcmp(l.b, "1a:1f.7") == 0);
    hw_line_init(&l);
    hw_line_bdf(&l, HW_BDF(0, 3, 0));
    CHECK(strcmp(l.b, "00:03.0") == 0);
    hw_line_s(&l, 0); /* NULL is ignored */
    CHECK(l.n == 7);
    /* never overflows; keeps a terminator */
    hw_line_init(&l);
    for (int i = 0; i < 400; i++) hw_line_c(&l, 'x');
    CHECK(l.n == sizeof l.b - 1);
    CHECK(l.b[sizeof l.b - 1] == 0);
    CHECK(strlen(l.b) == sizeof l.b - 1);
}

/* ---- wait ------------------------------------------------------------------ */
typedef struct {
    uint8_t *r;
    uint64_t at;
} flip_t;
static void flip_tick(fake_dev_t *d, uint64_t now)
{
    flip_t *f = (flip_t *) d->state;
    if (now >= f->at) fk_wr32(f->r, 0x10);
}

static void test_wait(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fake_dev_t *d = fake_dev_new(&fp, HW_BDF(0, 1, 0), 0x1234, 0x5678, 0xFF, 0, 0);
    flip_t f = {fake_dev_bar(d, 0, 4096), 500};
    d->state = &f;
    d->tick = flip_tick;
    CHECK(!hw_wait32(&fp.plat, f.r, 0, 0x10, 0x10, 100));
    CHECK(fp.now_us >= 100 && fp.now_us < 500);
    CHECK(hw_wait32(&fp.plat, f.r, 0, 0x10, 0x10, 10000));
    CHECK(fp.now_us >= 500 && fp.now_us < 600);
    uint64_t t = fp.now_us;
    CHECK(hw_wait32(&fp.plat, f.r, 0, 0x10, 0x10, 0)); /* already met: no delay */
    CHECK(fp.now_us == t);
    CHECK(!hw_wait32(&fp.plat, f.r, 0, 0x10, 0, 0)); /* not met, zero timeout */
    CHECK(fp.now_us == t);
    fake_plat_free(&fp);
}

/* ---- PCI scan ---------------------------------------------------------------- */
static void test_scan(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    /* 00:00.0 single function; a stray 00:00.1 must NOT be listed */
    fake_dev_t *host = fake_dev_new(&fp, HW_BDF(0, 0, 0), 0x8086, 0x29C0, 0x06, 0x00, 0x00);
    fake_dev_new(&fp, HW_BDF(0, 0, 1), 0x8086, 0xDEAD, 0x06, 0x80, 0x00);
    /* 00:02.0 multi-function with fn 0 and fn 3 */
    fake_dev_t *mf0 = fake_dev_new(&fp, HW_BDF(0, 2, 0), 0x1af4, 0x1000, 0x02, 0x00, 0x00);
    mf0->cfg[3] |= 0x00800000u;
    fake_dev_t *mf3 = fake_dev_new(&fp, HW_BDF(0, 2, 3), 0x1af4, 0x1001, 0x01, 0x00, 0x00);
    /* function 1 of a device without function 0 is unreachable */
    fake_dev_new(&fp, HW_BDF(0, 5, 1), 0x1111, 0x2222, 0x01, 0x00, 0x00);
    /* vendor 0 (some broken bridges) is skipped */
    fake_dev_new(&fp, HW_BDF(0, 6, 0), 0x0000, 0x0000, 0x01, 0x00, 0x00);
    /* a bridge (header 1) with two BARs, on bus 0; an endpoint on bus 3 */
    fake_dev_t *br = fake_dev_new(&fp, HW_BDF(0, 0x1c, 0), 0x8086, 0x2940, 0x06, 0x04, 0x00);
    br->cfg[3] |= 0x00010000u;
    fake_dev_bar_at(br, 0, 0xFEB00000, 0x1000, FAKE_BAR_MEM32);
    fake_dev_bar_at(br, 2, 0xFEC00000, 0x1000, FAKE_BAR_MEM32); /* beyond a bridge's 2 */
    fake_dev_t *ep = fake_dev_new(&fp, HW_BDF(3, 0, 0), 0x10ec, 0x8168, 0x02, 0x00, 0x00);
    fake_dev_bar_at(ep, 0, 0xE000, 0x100, FAKE_BAR_IO);
    fake_dev_bar_at(ep, 2, 0x380000000000ULL, 0x4000, FAKE_BAR_MEM64);
    fake_dev_bar_at(ep, 4, 0xFE000000, 0x100000, FAKE_BAR_MEM32);
    ep->cfg[0x3C / 4] = 0x0000010B;                 /* IRQ 11 */
    ep->cfg[2] = (ep->cfg[2] & 0xFFFFFF00u) | 0x15; /* revision */
    mf3->cfg[1] |= 0x0005; /* io + master already on: restored after sizing */

    hw_pci_dev_t devs[16];
    uint32_t n = hw_pci_scan(&fp.plat, devs, 16);
    CHECK(n == 5);
    CHECK(devs[0].bdf == HW_BDF(0, 0, 0) && devs[0].vendor == 0x8086 && devs[0].device == 0x29C0);
    CHECK(devs[0].class_code == 0x06 && devs[0].subclass == 0 && devs[0].progif == 0);
    CHECK(devs[1].bdf == HW_BDF(0, 2, 0) && devs[1].header_type == 0x80);
    CHECK(devs[2].bdf == HW_BDF(0, 2, 3) && devs[2].device == 0x1001);
    CHECK(devs[3].bdf == HW_BDF(0, 0x1c, 0) && (devs[3].header_type & 0x7F) == 1);
    CHECK(devs[3].bar[0] == 0xFEB00000 && devs[3].bar_size[0] == 0x1000);
    CHECK(devs[3].bar_size[2] == 0 && devs[3].bar[2] == 0);
    const hw_pci_dev_t *e = &devs[4];
    CHECK(e->bdf == HW_BDF(3, 0, 0) && e->vendor == 0x10ec && e->device == 0x8168);
    CHECK(e->rev == 0x15 && e->irq_line == 11);
    CHECK(e->bar_flags[0] == HW_BAR_IO && e->bar[0] == 0xE000 && e->bar_size[0] == 0x100);
    CHECK(e->bar_flags[2] == HW_BAR_64 && e->bar[2] == 0x380000000000ULL);
    CHECK(e->bar_size[2] == 0x4000);
    CHECK(e->bar[3] == 0 && e->bar_size[3] == 0 && e->bar_flags[3] == 0);
    CHECK(e->bar_flags[4] == 0 && e->bar[4] == 0xFE000000 && e->bar_size[4] == 0x100000);
    CHECK(e->bar_size[1] == 0 && e->bar_size[5] == 0);
    /* sizing restored every BAR and the command word */
    CHECK(ep->cfg[4] == (0xE000u | 1));
    CHECK(ep->cfg[6] == 0x4 && ep->cfg[7] == 0x3800);
    CHECK(ep->cfg[8] == 0xFE000000u);
    CHECK((mf3->cfg[1] & 0xFFFF) == 0x0005);
    (void) host;

    /* max truncates but the walk still counts nothing past it */
    hw_pci_dev_t two[2];
    CHECK(hw_pci_scan(&fp.plat, two, 2) == 2);
    CHECK(two[1].bdf == HW_BDF(0, 2, 0));
    CHECK(hw_pci_scan(&fp.plat, two, 0) == 0);

    /* enable: memory + master + INTx off, IO off, status untouched */
    hw_pci_enable(&fp.plat, &devs[2]);
    CHECK((mf3->cfg[1] & 0xFFFF) == 0x0406);
    CHECK((mf3->cfg[1] >> 16) == 0x0010);

    /* map: IO BAR, empty BAR, out of range all refuse; memory maps */
    CHECK(hw_pci_map_bar(&fp.plat, e, 0) == 0);
    CHECK(hw_pci_map_bar(&fp.plat, e, 3) == 0);
    CHECK(hw_pci_map_bar(&fp.plat, e, 6) == 0);
    CHECK(hw_pci_map_bar(&fp.plat, e, 2) == (volatile void *) (uintptr_t) 0x380000000000ULL);
    hw_pci_dev_t z = *e;
    z.bar_size[4] = 0;
    CHECK(hw_pci_map_bar(&fp.plat, &z, 4) == 0);
    fake_plat_free(&fp);
}

static void test_caps(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fake_dev_t *d = fake_dev_new(&fp, HW_BDF(0, 4, 0), 0x8086, 0x10D3, 0x02, 0x00, 0x00);
    d->cfg[0x34 / 4] = 0x40;
    d->cfg[0x40 / 4] = 0x00005001; /* PM -> 0x50 */
    d->cfg[0x50 / 4] = 0x0000E005; /* MSI -> 0xE0 */
    d->cfg[0xE0 / 4] = 0x00000010; /* PCIe, end */
    hw_pci_dev_t dev;
    CHECK(hw_pci_scan(&fp.plat, &dev, 1) == 1);
    CHECK(hw_pci_find_cap(&fp.plat, &dev, 0x01) == 0x40);
    CHECK(hw_pci_find_cap(&fp.plat, &dev, 0x05) == 0x50);
    CHECK(hw_pci_find_cap(&fp.plat, &dev, 0x10) == 0xE0);
    CHECK(hw_pci_find_cap(&fp.plat, &dev, 0x11) == 0);
    /* a loop in the list ends */
    d->cfg[0xE0 / 4] = 0x00004010;
    CHECK(hw_pci_find_cap(&fp.plat, &dev, 0x11) == 0);
    /* pointer below 0x40 ends the walk */
    d->cfg[0x34 / 4] = 0x3C;
    CHECK(hw_pci_find_cap(&fp.plat, &dev, 0x01) == 0);
    /* no capabilities bit: not walked at all */
    d->cfg[0x34 / 4] = 0x40;
    d->cfg[1] &= ~0x00100000u;
    CHECK(hw_pci_find_cap(&fp.plat, &dev, 0x01) == 0);
    /* low two pointer bits are reserved and masked */
    d->cfg[1] |= 0x00100000u;
    d->cfg[0x34 / 4] = 0x43;
    CHECK(hw_pci_find_cap(&fp.plat, &dev, 0x01) == 0x40);
    fake_plat_free(&fp);
}

/* ---- registry and matching --------------------------------------------------- */
static void test_registry(void)
{
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_blk_t b;
    hw_nic_t n;
    hw_input_t in;
    hw_audio_t a;
    for (int i = 0; i < HW_MAX_BLK; i++) CHECK(hw_reg_blk(&r, &b) == 0);
    CHECK(hw_reg_blk(&r, &b) == -1 && r.n_blk == HW_MAX_BLK);
    for (int i = 0; i < HW_MAX_NIC; i++) CHECK(hw_reg_nic(&r, &n) == 0);
    CHECK(hw_reg_nic(&r, &n) == -1 && r.n_nic == HW_MAX_NIC);
    for (int i = 0; i < HW_MAX_INPUT; i++) CHECK(hw_reg_input(&r, &in) == 0);
    CHECK(hw_reg_input(&r, &in) == -1 && r.n_input == HW_MAX_INPUT);
    for (int i = 0; i < HW_MAX_AUDIO; i++) CHECK(hw_reg_audio(&r, &a) == 0);
    CHECK(hw_reg_audio(&r, &a) == -1 && r.n_audio == HW_MAX_AUDIO);
    CHECK(r.blk[0] == &b && r.nic[3] == &n && r.input[0] == &in && r.audio[1] == &a);
}

static hw_verdict_t bind_dummy(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r,
                               hw_line_t *note)
{
    (void) p;
    (void) d;
    (void) r;
    (void) note;
    return HW_T;
}

static void test_match(void)
{
    static const hw_id_t ids[] = {{0x8086, 0x100E}, {0x8086, 0x10D3}, {0, 0}};
    static const hw_id_t ids2[] = {{0x10ec, 0x8168}, {0, 0}};
    const hw_driver_t tab[] = {
        {"classnet", 0x02, 0x00, 0xFF, 0, bind_dummy},   {"e1k", 0x02, 0x00, 0xFF, ids, bind_dummy},
        {"rtl", 0x02, 0x00, 0xFF, ids2, bind_dummy},     {"nvme", 0x01, 0x08, 0x02, 0, bind_dummy},
        {"anystorage", 0x01, 0xFF, 0xFF, 0, bind_dummy},
    };
    hw_pci_dev_t d;
    memset(&d, 0, sizeof d);
    d.vendor = 0x8086;
    d.device = 0x10D3;
    d.class_code = 0x02;
    CHECK(hw_match(tab, 5, &d) == &tab[1]); /* specific id beats an earlier class row */
    d.vendor = 0x10ec;
    d.device = 0x8168;
    CHECK(hw_match(tab, 5, &d) == &tab[2]);
    d.device = 0x8139;
    CHECK(hw_match(tab, 5, &d) == &tab[0]); /* falls back to class */
    d.class_code = 0x01;
    d.subclass = 0x08;
    d.progif = 0x02;
    CHECK(hw_match(tab, 5, &d) == &tab[3]);
    d.progif = 0x01;
    CHECK(hw_match(tab, 5, &d) == &tab[4]); /* progif mismatch, wildcard row */
    d.subclass = 0x06;
    CHECK(hw_match(tab, 5, &d) == &tab[4]);
    CHECK(hw_match(tab, 3, &d) == 0);
    d.class_code = 0x0C;
    CHECK(hw_match(tab, 5, &d) == 0);
    CHECK(hw_match(tab, 0, &d) == 0);
    /* an id row does not match by class alone */
    const hw_driver_t only_ids[] = {{"e1k", 0x02, 0x00, 0xFF, ids, bind_dummy}};
    d.class_code = 0x02;
    d.subclass = 0;
    d.vendor = 0x1234;
    CHECK(hw_match(only_ids, 1, &d) == 0);

    /* the built-in table: classes route to the right drivers */
    memset(&d, 0, sizeof d);
    d.class_code = 0x01, d.subclass = 0x08, d.progif = 0x02;
    CHECK(strcmp(hw_match(hw_drivers, hw_n_drivers, &d)->name, "nvme") == 0);
    d.subclass = 0x06, d.progif = 0x01;
    CHECK(strcmp(hw_match(hw_drivers, hw_n_drivers, &d)->name, "ahci") == 0);
    d.progif = 0x00; /* IDE-mode SATA is not AHCI */
    CHECK(hw_match(hw_drivers, hw_n_drivers, &d) == 0);
    d.class_code = 0x0C, d.subclass = 0x03, d.progif = 0x30;
    CHECK(strcmp(hw_match(hw_drivers, hw_n_drivers, &d)->name, "xhci") == 0);
    d.progif = 0x20; /* EHCI is not claimed */
    CHECK(hw_match(hw_drivers, hw_n_drivers, &d) == 0);
    d.class_code = 0x04, d.subclass = 0x03, d.progif = 0x00;
    CHECK(strcmp(hw_match(hw_drivers, hw_n_drivers, &d)->name, "hda") == 0);
    d.class_code = 0x02, d.subclass = 0x00, d.vendor = 0x8086, d.device = 0x100E;
    CHECK(strcmp(hw_match(hw_drivers, hw_n_drivers, &d)->name, "e1000") == 0);
}

/* ---- probe -------------------------------------------------------------------- */
static void test_probe(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fake_dev_new(&fp, HW_BDF(0, 0, 0), 0x8086, 0x29C0, 0x06, 0x00, 0x00); /* unclaimed */
    fake_dev_new(&fp, HW_BDF(0, 3, 0), 0x1b36, 0x0010, 0x01, 0x08, 0x02); /* NVMe, no BAR */
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_probe_stats_t st = hw_probe_all(&fp.plat, &r);
    CHECK(st.functions == 2 && st.unclaimed == 1 && st.held == 1);
    CHECK(st.bound == 0 && st.failed == 0);
    CHECK(fake_log_has(&fp, "[DRIVER HELD] nvme 00:03.0 1b36:0010 BAR0 not assigned"));
    CHECK(fake_log_has(&fp, "[OK] class drivers: 2 PCI functions, 0 bound, 0 failed, 1 held, 1 "
                            "without a class driver"));
    CHECK(fp.nlog == 2);
    fake_plat_free(&fp);
}

/* ---- self-tests --------------------------------------------------------------- */
typedef struct {
    hw_blk_t b;
    int fail;
    int gpt;
    uint64_t last_lba;
} tblk_t;

static int tblk_read(hw_blk_t *b, uint64_t lba, uint32_t n, void *buf)
{
    tblk_t *t = (tblk_t *) b;
    t->last_lba = lba;
    if (t->fail) return -1;
    memset(buf, 0, n * b->block_size);
    if (t->gpt) memcpy(buf, "EFI PART", 8);
    return 0;
}

static void test_selftest_blk(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    tblk_t d[4];
    memset(d, 0, sizeof d);
    const char *names[] = {"gptdisk", "rawdisk", "baddisk", "nomedia"};
    for (int i = 0; i < 4; i++) {
        strcpy(d[i].b.name, names[i]);
        d[i].b.block_size = 512;
        d[i].b.blocks = 100;
        d[i].b.read = tblk_read;
    }
    d[0].gpt = 1;
    d[2].fail = 1;
    d[3].b.blocks = 1;
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    for (int i = 0; i < 4; i++) hw_reg_blk(&r, &d[i].b);
    CHECK(hw_selftest_all(&fp.plat, &r) == 2);
    CHECK(fake_log_has(&fp, "[SELFTEST OK] gptdisk read LBA 1: GPT header 'EFI PART'; 100 "
                            "blocks of 512"));
    CHECK(fake_log_has(&fp, "[SELFTEST OK] rawdisk read LBA 1: no GPT header"));
    CHECK(fake_log_has(&fp, "[SELFTEST FAILED] baddisk read LBA 1 failed"));
    CHECK(fake_log_has(&fp, "[SELFTEST NONE] nomedia no readable media: 1 blocks"));
    CHECK(d[0].last_lba == 1 && d[3].last_lba == 0);
    /* block sizes the buffer cannot hold are not read */
    fp.nlog = 0;
    memset(&r, 0, sizeof r);
    d[0].b.block_size = 8192;
    d[0].last_lba = 7;
    hw_reg_blk(&r, &d[0].b);
    CHECK(hw_selftest_all(&fp.plat, &r) == 0);
    CHECK(d[0].last_lba == 7);
    d[0].b.block_size = 0;
    CHECK(hw_selftest_all(&fp.plat, &r) == 0);
    d[0].b.block_size = 4096;
    CHECK(hw_selftest_all(&fp.plat, &r) == 1);
    fake_plat_free(&fp);
}

/* A NIC whose wire is a tiny DHCP server: it answers the discover after a few
 * polls, optionally with the wrong xid, a non-offer, or never. */
typedef struct {
    hw_nic_t n;
    uint8_t tx[600];
    uint32_t txlen, sends, polls;
    int mode; /* 0 answer, 1 never, 2 wrong xid first, 3 send fails, 4 junk frames */
    int pending;
} tnic_t;

static int tnic_send(hw_nic_t *n, const void *f, uint32_t len)
{
    tnic_t *t = (tnic_t *) n;
    if (t->mode == 3) return -1;
    memcpy(t->tx, f, len < sizeof t->tx ? len : sizeof t->tx);
    t->txlen = len;
    t->sends++;
    t->pending = 1;
    return 0;
}

static uint32_t make_offer(uint8_t *f, const uint8_t *disc, uint32_t xid_xor)
{
    memset(f, 0, 342);
    memcpy(f, disc + 6, 6);
    f[12] = 0x08;
    uint8_t *ip = f + 14;
    ip[0] = 0x45;
    ip[9] = 17;
    ip[12] = 10, ip[13] = 0, ip[14] = 2, ip[15] = 2;
    uint8_t *udp = ip + 20;
    udp[1] = 67;
    udp[3] = 68;
    uint8_t *b = udp + 8;
    const uint8_t *db = disc + 14 + 20 + 8;
    b[0] = 2;
    memcpy(b + 4, db + 4, 4);
    b[7] ^= (uint8_t) xid_xor;
    b[16] = 10, b[17] = 0, b[18] = 2, b[19] = 15;
    return 342;
}

static int tnic_recv(hw_nic_t *n, void *buf, uint32_t max)
{
    tnic_t *t = (tnic_t *) n;
    (void) max;
    if (!t->pending || t->mode == 1) return 0;
    if (++t->polls < 3) return 0;
    if (t->mode == 4 && t->polls < 6) { /* runts, ARP and a non-UDP IPv4 frame */
        uint8_t *f = (uint8_t *) buf;
        memset(f, 0, 64);
        if (t->polls == 4) f[12] = 0x08, f[13] = 0x06;
        if (t->polls == 5) f[12] = 0x08, f[14] = 0x45, f[23] = 6;
        return t->polls == 3 ? 20 : 64;
    }
    if (t->mode == 2 && t->sends == 1) {
        t->pending = 0;
        return (int) make_offer((uint8_t *) buf, t->tx, 1);
    }
    t->pending = 0;
    return (int) make_offer((uint8_t *) buf, t->tx, 0);
}

static uint16_t csum(const uint8_t *h, uint32_t n)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i + 1 < n; i += 2) s += ((uint32_t) h[i] << 8) | h[i + 1];
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (uint16_t) s;
}

static void test_selftest_nic(void)
{
    static const uint8_t mac[6] = {0x52, 0x54, 0x00, 0x5a, 0x58, 0x01};
    for (int mode = 0; mode <= 4; mode++) {
        fake_plat_t fp;
        fake_plat_init(&fp);
        tnic_t t;
        memset(&t, 0, sizeof t);
        t.mode = mode;
        strcpy(t.n.name, "eth9");
        memcpy(t.n.mac, mac, 6);
        t.n.send = tnic_send;
        t.n.recv = tnic_recv;
        hw_registry_t r;
        memset(&r, 0, sizeof r);
        hw_reg_nic(&r, &t.n);
        uint32_t ok = hw_selftest_all(&fp.plat, &r);
        if (mode == 0 || mode == 4) {
            CHECK(ok == 1);
            CHECK(t.sends == 1);
            CHECK(fake_log_has(&fp, "[SELFTEST OK] eth9 DHCP offer received; mac "
                                    "52:54:00:5a:58:01; offered 10.0.2.15 by 10.0.2.2"));
        } else if (mode == 1) {
            CHECK(ok == 0);
            CHECK(t.sends == 3); /* three tries */
            CHECK(fp.now_us >= 3000000 && fp.now_us < 3100000);
            CHECK(fake_log_has(&fp, "[SELFTEST NONE] eth9 no DHCP offer"));
        } else if (mode == 2) {
            CHECK(ok == 1);
            CHECK(t.sends == 2); /* the wrong-xid reply is ignored */
        } else {
            CHECK(ok == 0);
            CHECK(fake_log_has(&fp, "[SELFTEST FAILED] eth9 transmit refused"));
        }
        if (mode == 3) {
            fake_plat_free(&fp);
            continue;
        }
        /* the discover itself */
        const uint8_t *f = t.tx;
        CHECK(t.txlen == 342);
        CHECK(memcmp(f, "\xff\xff\xff\xff\xff\xff", 6) == 0);
        CHECK(memcmp(f + 6, mac, 6) == 0);
        CHECK(f[12] == 0x08 && f[13] == 0x00);
        const uint8_t *ip = f + 14;
        CHECK(ip[0] == 0x45 && ip[9] == 17 && ip[8] == 64);
        CHECK(((ip[2] << 8) | ip[3]) == 328);
        CHECK(csum(ip, 20) == 0xFFFF); /* header checksum valid */
        CHECK(memcmp(ip + 16, "\xff\xff\xff\xff", 4) == 0);
        CHECK(memcmp(ip + 12, "\0\0\0\0", 4) == 0);
        const uint8_t *udp = ip + 20;
        CHECK(udp[0] == 0 && udp[1] == 68 && udp[2] == 0 && udp[3] == 67);
        CHECK(((udp[4] << 8) | udp[5]) == 308);
        const uint8_t *b = udp + 8;
        CHECK(b[0] == 1 && b[1] == 1 && b[2] == 6 && b[10] == 0x80);
        CHECK(memcmp(b + 28, mac, 6) == 0);
        CHECK(b[236] == 99 && b[237] == 130 && b[238] == 83 && b[239] == 99);
        CHECK(b[240] == 53 && b[241] == 1 && b[242] == 1 && b[243] == 255);
        uint32_t xid =
            ((uint32_t) b[4] << 24) | ((uint32_t) b[5] << 16) | ((uint32_t) b[6] << 8) | b[7];
        CHECK(xid == (0x5A585600u ^ (0x58u << 8) ^ 0x01u));
        fake_plat_free(&fp);
    }
}

typedef struct {
    hw_input_t in;
    const char *keys;
    uint32_t i, polls;
} tin_t;

static int tin_poll(hw_input_t *in, hw_key_t *ev)
{
    tin_t *t = (tin_t *) in;
    t->polls++;
    if (!t->keys[t->i]) return 0;
    char c = t->keys[t->i++];
    ev->usage = 4;
    ev->ascii = (uint8_t) (c == '#' ? 0 : c);
    ev->mods = 0;
    ev->pressed = c != '-';
    return 1;
}

static void test_selftest_input(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    tin_t a = {{"usbkbd0", 0, tin_poll}, "zx-v#", 0, 0};
    tin_t b = {{"idle", 0, tin_poll}, "", 0, 0};
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_reg_input(&r, &a.in);
    hw_reg_input(&r, &b.in);
    CHECK(hw_selftest_all(&fp.plat, &r) == 2);
    /* '-' is a release, '#' a press with no ASCII: 4 presses, "zxv" echoed */
    CHECK(fake_log_has(&fp, "[SELFTEST OK] usbkbd0 polling; 4 key press(es): \"zxv\""));
    CHECK(fake_log_has(&fp, "[SELFTEST OK] idle polling; 0 key press(es)"));
    CHECK(!fake_log_has(&fp, "idle polling; 0 key press(es): "));
    CHECK(fp.now_us >= 4000000 && fp.now_us < 4100000); /* 2 s each */
    fake_plat_free(&fp);
}

typedef struct {
    hw_audio_t a;
    int fail;
    uint32_t start_after, frames, calls;
    int16_t first, period_neg;
} taud_t;
static fake_plat_t *g_afp;
static uint64_t g_t0;

static int taud_play(hw_audio_t *a, const int16_t *f, uint32_t n)
{
    taud_t *t = (taud_t *) a;
    if (t->fail) return -1;
    t->frames = n;
    t->first = f[0];
    t->period_neg = f[2 * 32];
    g_t0 = g_afp->now_us;
    return 0;
}

static uint32_t taud_pos(hw_audio_t *a)
{
    taud_t *t = (taud_t *) a;
    t->calls++;
    return g_afp->now_us - g_t0 >= t->start_after ? 512 : 0;
}

static void test_selftest_audio(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    g_afp = &fp;
    taud_t ok = {{"hda0", 1, 0, taud_play, taud_pos}, 0, 5000, 0, 0, 0, 0};
    taud_t stuck = {{"hda1", 1, 0, taud_play, taud_pos}, 0, 900000000, 0, 0, 0, 0};
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_reg_audio(&r, &ok.a);
    hw_reg_audio(&r, &stuck.a);
    CHECK(hw_selftest_all(&fp.plat, &r) == 1);
    CHECK(ok.frames == 4800 && ok.first == 1200 && ok.period_neg == -1200);
    CHECK(ok.calls >= 5 && ok.calls <= 6);
    CHECK(stuck.calls == 300);
    CHECK(fake_log_has(&fp, "[SELFTEST OK] hda0 output stream DMA position 512 bytes"));
    CHECK(fake_log_has(&fp, "[SELFTEST FAILED] hda1 output stream DMA position 0 bytes"));
    fp.nlog = 0;
    ok.fail = 1;
    memset(&r, 0, sizeof r);
    hw_reg_audio(&r, &ok.a);
    CHECK(hw_selftest_all(&fp.plat, &r) == 0);
    CHECK(fake_log_has(&fp, "[SELFTEST FAILED] hda0 stream did not start"));
    fake_plat_free(&fp);
}

int main(void)
{
    test_helpers();
    test_line();
    test_wait();
    test_scan();
    test_caps();
    test_registry();
    test_match();
    test_probe();
    test_selftest_blk();
    test_selftest_nic();
    test_selftest_input();
    test_selftest_audio();
    printf("test_hwdrv_core: %d checks, %d failures\n", fk_checks, fk_failures);
    return fk_failures ? 1 : 0;
}
