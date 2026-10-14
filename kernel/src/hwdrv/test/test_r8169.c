/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_r8169.c — r8169.c against a register-level fake RTL8169/8168/8101.
 *
 * This fake is the ONLY thing r8169.c has been run against: QEMU has no
 * model of these chips. It follows the public datasheet register layout:
 * ChipCmd reset self-clearing, IDR0-5 loaded at reset, OWN-bit descriptor
 * rings with EOR wrap, TxPoll.NPQ kicking transmit, receive lengths that
 * include the CRC, the RxConfig accept bits as the address filter, and
 * PHYstatus for link. Like several real 8168 revisions it does NOT pad
 * short frames, so the driver has to. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hwfake.h"
#include "nicpeer.h"

enum {
    IDR0 = 0x00,
    MAR0 = 0x08,
    TNPDS = 0x20,
    CHIPCMD = 0x37,
    TXPOLL = 0x38,
    INTRMASK = 0x3C,
    INTRSTAT = 0x3E,
    TXCONFIG = 0x40,
    RXCONFIG = 0x44,
    CFG9346 = 0x50,
    PHYSTATUS = 0x6C,
    RXMAXSIZE = 0xDA,
    CPLUSCMD = 0xE0,
    RDSAR = 0xE4,
    MAXTXPKT = 0xEC,
};
enum { K_8169, K_8168, K_8101 };
#define OWN (1u << 31)
#define EOR (1u << 30)
#define FS  (1u << 29)
#define LS  (1u << 28)
#define RES (1u << 21)
#define XID 0x2C800000u /* chip version bits in TxConfig, read-only */

typedef struct frtl {
    uint8_t *r;
    int kind;
    uint8_t mac[6];
    peer_t peer;
    int reset_stuck, zero_mac, unplugged, tx_dd_never, ones, rx_err_next, split_next;
    uint64_t link_at;
    uint32_t txi, rxi; /* the chip's own ring positions */
    uint32_t resets, kicks, tx_frames, rx_frames, rx_filtered, rx_full, violations, short_tx;
    uint32_t last_tx_len;
} frtl_t;

static uint32_t rd32(frtl_t *f, uint32_t o)
{
    return fk_rd32(f->r + o);
}
static void wr32(frtl_t *f, uint32_t o, uint32_t v)
{
    fk_wr32(f->r + o, v);
}
static uint32_t rd16(frtl_t *f, uint32_t o)
{
    return (uint32_t) f->r[o] | ((uint32_t) f->r[o + 1] << 8);
}
static void wr16(frtl_t *f, uint32_t o, uint32_t v)
{
    f->r[o] = (uint8_t) v;
    f->r[o + 1] = (uint8_t) (v >> 8);
}

static void do_reset(frtl_t *f)
{
    f->resets++;
    memset(f->r, 0, 256);
    if (!f->zero_mac) memcpy(f->r + IDR0, f->mac, 6);
    memset(f->r + MAR0, 0xFF, 8);
    wr16(f, CPLUSCMD, 0x0060); /* RX VLAN strip + RX checksum on: driver clears */
    wr16(f, INTRMASK, 0xFFFF);
    wr16(f, INTRSTAT, 0x00A5);
    wr32(f, TXCONFIG, XID);
    f->txi = f->rxi = 0;
}

static int link_ok(frtl_t *f, uint64_t now)
{
    return !f->unplugged && now >= f->link_at;
}

static int rx_accept(frtl_t *f, const uint8_t *fr)
{
    static const uint8_t bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint32_t rc = rd32(f, RXCONFIG);
    if (!memcmp(fr, bc, 6)) return (rc >> 3) & 1;
    return ((rc >> 1) & 1) && !memcmp(fr, f->r + IDR0, 6);
}

static uint8_t *ring_at(frtl_t *f, uint32_t off)
{
    return (uint8_t *) fk_ptr(((uint64_t) rd32(f, off + 4) << 32) | rd32(f, off));
}

static void do_tx(frtl_t *f)
{
    if (!(f->r[TXPOLL] & 0x40)) return;
    if (f->tx_dd_never) return; /* the DMA engine never picks the kick up */
    f->r[TXPOLL] = 0;
    f->kicks++;
    if (!(f->r[CHIPCMD] & 0x04)) return;
    uint8_t *ring = ring_at(f, TNPDS);
    for (uint32_t guard = 0; guard < 64; guard++) {
        uint8_t *d = ring + f->txi * 16;
        uint32_t o1 = fk_rd32(d);
        if (!(o1 & OWN)) return;
        if ((o1 & (FS | LS)) != (FS | LS)) f->violations++;
        uint32_t len = o1 & 0x3FFF;
        uint8_t fr[PEER_FRAME];
        memset(fr, 0, sizeof fr);
        if (len > 1514)
            f->violations++;
        else
            memcpy(fr, fk_ptr(fk_rd64(d + 8)), len);
        if (len < 60) f->short_tx++; /* this chip sends runts as they are */
        f->last_tx_len = len;
        f->tx_frames++;
        peer_on_tx(&f->peer, fr, len);
        fk_wr32(d, o1 & ~OWN);
        if (o1 & EOR)
            f->txi = 0;
        else if (++f->txi >= 16) { /* past the driver's 16: EOR missing */
            f->violations++;
            f->txi = 0;
        }
    }
}

static int put_rx(frtl_t *f, uint8_t *ring, const uint8_t *src, uint32_t len, uint32_t flags)
{
    uint8_t *d = ring + f->rxi * 16;
    uint32_t o1 = fk_rd32(d);
    if (!(o1 & OWN)) return 0;
    uint32_t cap = o1 & 0x3FFF;
    if (len > cap) {
        f->violations++;
        len = cap;
    }
    memcpy(fk_ptr(fk_rd64(d + 8)), src, len);
    fk_wr32(d, (o1 & EOR) | flags | len);
    if (o1 & EOR)
        f->rxi = 0;
    else if (++f->rxi >= 32) {
        f->violations++;
        f->rxi = 0;
    }
    return 1;
}

static void do_rx(frtl_t *f)
{
    if (!(f->r[CHIPCMD] & 0x08)) return;
    uint8_t *ring = ring_at(f, RDSAR);
    peer_frame_t *pf;
    while ((pf = peer_front(&f->peer)) != 0) {
        uint32_t o1 = fk_rd32(ring + f->rxi * 16);
        uint32_t nx = (o1 & EOR) ? 0 : f->rxi + 1;
        if (!(o1 & OWN) || (f->split_next && !(fk_rd32(ring + nx * 16) & OWN))) {
            f->rx_full++;
            return;
        }
        if (!rx_accept(f, pf->b)) {
            f->rx_filtered++;
            peer_pop(&f->peer);
            continue;
        }
        uint8_t buf[PEER_FRAME + 4];
        uint32_t len = pf->len;
        memcpy(buf, pf->b, len);
        memset(buf + len, 0xCC, 4); /* the CRC is always in the buffer */
        len += 4;
        uint32_t err = pf->err ? RES : 0;
        if (f->rx_err_next) {
            err = RES;
            f->rx_err_next = 0;
        }
        if (f->split_next) {
            f->split_next = 0;
            put_rx(f, ring, buf, len / 2, FS);
            put_rx(f, ring, buf + len / 2, len - len / 2, LS);
        } else {
            put_rx(f, ring, buf, len, FS | LS | err);
        }
        f->rx_frames++;
        peer_pop(&f->peer);
    }
}

static void tick(fake_dev_t *d, uint64_t now)
{
    frtl_t *f = (frtl_t *) d->state;
    if (f->ones) {
        memset(f->r, 0xFF, 256);
        return;
    }
    if (f->r[CHIPCMD] & 0x10) {
        if (f->reset_stuck) return;
        do_reset(f);
    }
    wr32(f, TXCONFIG, (rd32(f, TXCONFIG) & ~0x7C800000u) | XID);
    f->r[PHYSTATUS] = link_ok(f, now) ? 0x02 | 0x01 | 0x10 : 0;
    if (!link_ok(f, now)) return;
    do_tx(f);
    do_rx(f);
}

static const uint8_t mac_a[6] = {0x00, 0xE0, 0x4C, 0x68, 0x00, 0x01};

static frtl_t *add_rtl(fake_plat_t *fp, uint32_t bdf, uint16_t did, int kind)
{
    fake_dev_t *d = fake_dev_new(fp, bdf, 0x10EC, did, 0x02, 0x00, 0x00);
    frtl_t *f = (frtl_t *) calloc(1, sizeof *f);
    /* 8169: registers in BAR1; 8168/8101: BAR2, with an MSI-X table BAR4 */
    f->r = fake_dev_bar(d, kind == K_8169 ? 1 : 2, 256);
    if (kind != K_8169) fake_dev_bar(d, 4, 0x4000);
    f->kind = kind;
    memcpy(f->mac, mac_a, 6);
    f->mac[5] = (uint8_t) (bdf >> 3);
    do_reset(f);
    f->resets = 0;
    d->state = f;
    d->tick = tick;
    return f;
}

static hw_pci_dev_t g_dev[4];

static hw_verdict_t bind_one(fake_plat_t *fp, hw_registry_t *r, hw_line_t *note)
{
    uint32_t n = hw_pci_scan(&fp->plat, g_dev, 4);
    CHECK(n == 1);
    hw_line_init(note);
    return r8169_bind(&fp->plat, &g_dev[0], r, note);
}

typedef struct env {
    fake_plat_t fp;
    hw_registry_t r;
    hw_line_t note;
    frtl_t *f;
    hw_verdict_t v;
} env_t;

static env_t *env_new(uint16_t did, int kind, void (*breakit)(frtl_t *))
{
    env_t *e = (env_t *) calloc(1, sizeof *e);
    fake_plat_init(&e->fp);
    e->f = add_rtl(&e->fp, HW_BDF(1, 0, 0), did, kind);
    e->f->mac[5] = 0x42;
    do_reset(e->f);
    e->f->resets = 0;
    if (breakit) breakit(e->f);
    e->v = bind_one(&e->fp, &e->r, &e->note);
    return e;
}

static void wait_us(fake_plat_t *fp, uint32_t us)
{
    fp->plat.delay_us(fp->plat.ctx, us);
}

static void env_free(env_t *e)
{
    fake_plat_free(&e->fp);
    free(e->f);
    free(e);
}

static void test_ids(void)
{
    static const uint16_t want[] = {0x8169, 0x8167, 0x8168, 0x8161, 0x8136};
    uint32_t n = 0;
    for (const hw_id_t *id = r8169_ids; id->vendor; id++) {
        n++;
        CHECK(id->vendor == 0x10EC);
        CHECK(id->device != 0x8139); /* the RTL8139 is a different design */
        for (const hw_id_t *j = id + 1; j->vendor; j++) CHECK(j->device != id->device);
    }
    CHECK(n == sizeof want / sizeof want[0]);
    hw_pci_dev_t d;
    memset(&d, 0, sizeof d);
    d.vendor = 0x10EC;
    d.class_code = 2;
    for (uint32_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        d.device = want[i];
        const hw_driver_t *m = hw_match(hw_drivers, hw_n_drivers, &d);
        CHECK(m && m->bind == r8169_bind && strcmp(m->name, "r8169") == 0);
    }
    d.device = 0x8139;
    const hw_driver_t *m = hw_match(hw_drivers, hw_n_drivers, &d);
    CHECK(!m || m->bind != r8169_bind);
}

typedef struct model {
    uint16_t did;
    int kind;
    const char *name;
} model_t;

static void test_bind_models(void)
{
    static const model_t ms[] = {
        {0x8169, K_8169, "RTL8169"},      {0x8167, K_8169, "RTL8169SC"},
        {0x8168, K_8168, "RTL8168/8111"}, {0x8161, K_8168, "RTL8168/8111"},
        {0x8136, K_8101, "RTL8101/8102"},
    };
    for (uint32_t i = 0; i < sizeof ms / sizeof ms[0]; i++) {
        env_t *e = env_new(ms[i].did, ms[i].kind, 0);
        frtl_t *f = e->f;
        char want[96];
        snprintf(want, sizeof want, "%s 00:e0:4c:68:00:42 link up as eth0", ms[i].name);
        if (strcmp(e->note.b, want)) printf("  note '%s' want '%s'\n", e->note.b, want);
        CHECK(e->v == HW_T);
        CHECK(strcmp(e->note.b, want) == 0);
        CHECK(e->r.n_nic == 1);
        hw_nic_t *n = e->r.nic[0];
        CHECK(strcmp(n->name, "eth0") == 0 && n->link_up == 1);
        CHECK(memcmp(n->mac, f->mac, 6) == 0);
        CHECK(f->resets == 1);
        CHECK((e->fp.devs[0]->cfg[1] & 0x0407) == 0x0406);
        CHECK(rd16(f, INTRMASK) == 0);
        CHECK((rd16(f, CPLUSCMD) & 0x60) == 0);
        CHECK(rd16(f, RXMAXSIZE) == 2048);
        CHECK(f->r[MAXTXPKT] == 0x3F);
        CHECK(f->r[CFG9346] == 0x00);
        CHECK(f->r[CHIPCMD] == 0x0C);
        uint32_t rxc = rd32(f, RXCONFIG);
        CHECK(rxc == ((7u << 8) | 8u | 2u | (ms[i].kind == K_8169 ? 7u << 13 : 1u << 15)));
        CHECK((rd32(f, TXCONFIG) & ~0x7C800000u) == ((3u << 24) | (7u << 8)));
        CHECK(rd32(f, MAR0) == 0 && rd32(f, MAR0 + 4) == 0);
        uint8_t *tr = ring_at(f, TNPDS), *rr = ring_at(f, RDSAR);
        CHECK(((uintptr_t) tr & 255) == 0 && ((uintptr_t) rr & 255) == 0);
        int rx_ok = 1, tx_ok = 1;
        for (uint32_t k = 0; k < 32; k++) {
            uint32_t o1 = fk_rd32(rr + k * 16);
            if (o1 != (OWN | (k == 31 ? EOR : 0) | 2048)) rx_ok = 0;
            if (fk_rd64(rr + k * 16 + 8) != fk_rd64(rr + 8) + 2048ULL * k) rx_ok = 0;
        }
        for (uint32_t k = 0; k < 16; k++)
            if (fk_rd32(tr + k * 16) != (k == 15 ? EOR : 0)) tx_ok = 0;
        CHECK(rx_ok && tx_ok);
        env_free(e);
    }
}

static void test_echo_and_wrap(uint16_t did, int kind)
{
    env_t *e = env_new(did, kind, 0);
    frtl_t *f = e->f;
    CHECK(e->v == HW_T);
    hw_nic_t *n = e->r.nic[0];
    uint8_t tx[1600], rx[2048];
    int ok = 1;
    for (uint32_t k = 0; k < 100; k++) {
        uint32_t len = 60 + (k * 97) % (1514 - 60 + 1);
        if (k == 1) len = 1514;
        peer_mk(tx, peer_mac, n->mac, len, k);
        if (n->send(n, tx, len) != 0) ok = 0;
        int got = 0;
        for (int t = 0; t < 10 && !got; t++) {
            got = n->recv(n, rx, sizeof rx);
            if (!got) wait_us(&e->fp, 100);
        }
        if (got != (int) len || memcmp(rx, n->mac, 6) || memcmp(rx + 6, peer_mac, 6) ||
            memcmp(rx + 12, tx + 12, len - 12))
            ok = 0;
    }
    CHECK(ok);
    CHECK(f->tx_frames == 100 && f->rx_frames == 100 && f->kicks == 100);
    CHECK(f->violations == 0);
    /* short frame: the driver pads with zeros (the chip would send a runt) */
    peer_mk(tx, peer_mac, n->mac, 42, 7);
    CHECK(n->send(n, tx, 42) == 0);
    CHECK(f->last_tx_len == 60 && f->short_tx == 0);
    int z = 1;
    for (uint32_t i = 42; i < 60; i++) z &= f->peer.last[i] == 0;
    CHECK(z);
    wait_us(&e->fp, 10);
    CHECK(n->recv(n, rx, sizeof rx) == 60);
    CHECK(n->recv(n, rx, sizeof rx) == 0);
    uint32_t before = f->tx_frames;
    CHECK(n->send(n, tx, 0) == -1);
    CHECK(n->send(n, tx, 1515) == -1);
    CHECK(n->send(n, 0, 60) == -1);
    CHECK(f->tx_frames == before);
    CHECK(n->send(n, tx, 60) == 0);
    wait_us(&e->fp, 10);
    CHECK(n->recv(n, rx, 0) == 0);
    CHECK(n->recv(n, 0, 64) == 0);
    memset(rx, 0, sizeof rx);
    CHECK(n->recv(n, rx, 20) == 20);
    CHECK(memcmp(rx, n->mac, 6) == 0 && rx[20] == 0);
    CHECK(n->recv(n, rx, sizeof rx) == 0);
    env_free(e);
}

static void test_rx_full_and_errors(void)
{
    env_t *e = env_new(0x8168, K_8168, 0);
    frtl_t *f = e->f;
    hw_nic_t *n = e->r.nic[0];
    uint8_t fr[200], rx[2048];
    /* all 32 descriptors are the chip's (no tail register): 32 fit */
    for (uint32_t k = 0; k < 50; k++) {
        peer_mk(fr, n->mac, peer_mac, 64 + k, k);
        peer_push(&f->peer, fr, 64 + k, 0);
    }
    wait_us(&e->fp, 10);
    CHECK(f->rx_frames == 32 && f->rx_full > 0 && f->peer.count == 18);
    int order = 1;
    for (uint32_t k = 0; k < 50; k++) {
        int got = n->recv(n, rx, sizeof rx);
        if (!got) {
            wait_us(&e->fp, 10);
            got = n->recv(n, rx, sizeof rx);
        }
        peer_mk(fr, n->mac, peer_mac, 64 + k, k);
        if (got != (int) (64 + k) || memcmp(rx, fr, 64 + k)) order = 0;
    }
    CHECK(order);
    CHECK(f->violations == 0 && f->peer.count == 0);

    peer_mk(fr, n->mac, peer_mac, 100, 1);
    f->rx_err_next = 1;
    peer_push(&f->peer, fr, 100, 0);
    wait_us(&e->fp, 10);
    f->split_next = 1;
    peer_push(&f->peer, fr, 100, 0);
    wait_us(&e->fp, 10);
    peer_mk(fr, n->mac, peer_mac, 77, 2);
    peer_push(&f->peer, fr, 77, 0);
    wait_us(&e->fp, 10);
    CHECK(n->recv(n, rx, sizeof rx) == 77);
    CHECK(memcmp(rx, fr, 77) == 0);
    CHECK(n->recv(n, rx, sizeof rx) == 0);
    static const uint8_t other[6] = {0x02, 0, 0, 0, 0, 9};
    static const uint8_t bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    peer_mk(fr, other, peer_mac, 64, 3);
    peer_push(&f->peer, fr, 64, 0);
    peer_mk(fr, bc, peer_mac, 65, 4);
    peer_push(&f->peer, fr, 65, 0);
    wait_us(&e->fp, 10);
    CHECK(f->rx_filtered == 1);
    CHECK(n->recv(n, rx, sizeof rx) == 65);
    CHECK(n->recv(n, rx, sizeof rx) == 0);
    CHECK(f->violations == 0);
    env_free(e);
}

static void k_reset(frtl_t *f)
{
    f->reset_stuck = 1;
}
static void k_ones(frtl_t *f)
{
    f->ones = 1;
    memset(f->r, 0xFF, 256);
}
static void k_zero(frtl_t *f)
{
    f->zero_mac = 1;
}
static void k_group(frtl_t *f)
{
    f->mac[0] = 0x01;
}
static void k_unplug(frtl_t *f)
{
    f->unplugged = 1;
}
static void k_late(frtl_t *f)
{
    f->link_at = 2000000;
}
static void k_too_late(frtl_t *f)
{
    f->link_at = 3000000;
}

typedef struct fault {
    const char *what;
    uint16_t did;
    int kind;
    void (*breakit)(frtl_t *);
    hw_verdict_t want;
    const char *note;
} fault_t;

static void test_faults(void)
{
    static const fault_t fs[] = {
        {"reset stuck", 0x8168, K_8168, k_reset, HW_F, "RTL8168/8111 reset timed out"},
        {"all-ones", 0x8169, K_8169, k_ones, HW_F, "RTL8169 registers read all-ones"},
        {"zero MAC", 0x8168, K_8168, k_zero, HW_F, "RTL8168/8111 no MAC address"},
        {"group MAC", 0x8136, K_8101, k_group, HW_B, "01:e0:4c:68:00:42 has the group bit set"},
        {"unplugged", 0x8168, K_8168, k_unplug, HW_T, "00:e0:4c:68:00:42 link down as eth0"},
        {"link after 2 s", 0x8168, K_8168, k_late, HW_T, "link up as eth0"},
        {"link after 3 s", 0x8168, K_8168, k_too_late, HW_T, "link down as eth0"},
    };
    for (uint32_t i = 0; i < sizeof fs / sizeof fs[0]; i++) {
        env_t *e = env_new(fs[i].did, fs[i].kind, fs[i].breakit);
        if (e->v != fs[i].want || !strstr(e->note.b, fs[i].note))
            printf("  fault '%s': verdict %d note '%s'\n", fs[i].what, e->v, e->note.b);
        CHECK(e->v == fs[i].want);
        CHECK(strstr(e->note.b, fs[i].note) != 0);
        CHECK((e->v == HW_T) == (e->r.n_nic == 1));
        CHECK(e->fp.now_us < 2600000u);
        env_free(e);
    }
}

static void test_tx_stall_and_link_down(void)
{
    env_t *e = env_new(0x8168, K_8168, 0);
    hw_nic_t *n = e->r.nic[0];
    e->f->tx_dd_never = 1;
    uint8_t tx[64];
    peer_mk(tx, peer_mac, n->mac, 64, 0);
    uint64_t t0 = e->fp.now_us;
    CHECK(n->send(n, tx, 64) == -1);
    CHECK(e->fp.now_us - t0 >= 50000 && e->fp.now_us - t0 < 60000);
    /* OWN is per slot: all 16 can be outstanding, the 17th finds slot 0 busy */
    for (int k = 1; k < 16; k++) CHECK(n->send(n, tx, 64) == -1);
    t0 = e->fp.now_us;
    CHECK(n->send(n, tx, 64) == -2);
    CHECK(e->fp.now_us == t0);
    e->f->tx_dd_never = 0;
    wait_us(&e->fp, 10);
    CHECK(e->f->tx_frames == 16);
    CHECK(n->send(n, tx, 64) == 0);
    CHECK(e->f->tx_frames == 17);
    CHECK(e->f->violations == 0);
    env_free(e);

    e = env_new(0x8168, K_8168, k_unplug);
    n = e->r.nic[0];
    CHECK(n->link_up == 0);
    CHECK(n->send(n, tx, 64) == 0);
    CHECK(e->f->tx_frames == 0);
    CHECK(hw_selftest_all(&e->fp.plat, &e->r) == 0);
    CHECK(fake_log_has(&e->fp, "[SELFTEST NONE] eth0 no DHCP offer"));
    e->f->unplugged = 0;
    wait_us(&e->fp, 10);
    CHECK(e->f->tx_frames >= 1);
    CHECK(n->send(n, tx, 64) == 0);
    CHECK(n->link_up == 1);
    env_free(e);
}

static void test_no_bar_unknown_full_dma(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fake_dev_new(&fp, HW_BDF(1, 0, 0), 0x10EC, 0x8168, 2, 0, 0);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_one(&fp, &r, &note) == HW_N);
    CHECK(strcmp(note.b, "no memory BAR") == 0);
    fake_plat_free(&fp);

    /* a BAR smaller than the register file is not the register window */
    fake_plat_init(&fp);
    fake_dev_t *d0 = fake_dev_new(&fp, HW_BDF(1, 0, 0), 0x10EC, 0x8168, 2, 0, 0);
    fake_dev_bar(d0, 0, 128);
    memset(&r, 0, sizeof r);
    CHECK(bind_one(&fp, &r, &note) == HW_N);
    fake_plat_free(&fp);

    env_t *e = env_new(0x8168, K_8168, 0);
    hw_pci_dev_t d = g_dev[0];
    d.device = 0x8139;
    hw_line_init(&e->note);
    CHECK(r8169_bind(&e->fp.plat, &d, &e->r, &e->note) == HW_N);
    CHECK(strcmp(e->note.b, "unknown device id") == 0);
    e->r.n_nic = HW_MAX_NIC;
    d.device = 0x8168;
    hw_line_init(&e->note);
    CHECK(r8169_bind(&e->fp.plat, &d, &e->r, &e->note) == HW_N);
    CHECK(strstr(e->note.b, "NIC table full") != 0);
    env_free(e);

    for (uint32_t k = 1; k <= 5; k++) {
        fake_plat_init(&fp);
        frtl_t *f = add_rtl(&fp, HW_BDF(1, 0, 0), 0x8168, K_8168);
        fp.dma_fail_after = k;
        memset(&r, 0, sizeof r);
        CHECK(bind_one(&fp, &r, &note) == HW_F);
        CHECK(strstr(note.b, "out of DMA memory") != 0);
        CHECK(r.n_nic == 0);
        fake_plat_free(&fp);
        free(f);
    }
}

static void test_probe_and_selftest(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    frtl_t *a = add_rtl(&fp, HW_BDF(1, 0, 0), 0x8168, K_8168);
    frtl_t *b = add_rtl(&fp, HW_BDF(2, 0, 0), 0x8169, K_8169);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_probe_stats_t st = hw_probe_all(&fp.plat, &r);
    CHECK(st.functions == 2 && st.bound == 2 && st.failed == 0 && st.held == 0);
    CHECK(fake_log_has(&fp, "[DRIVER ONLINE] r8169 01:00.0 10ec:8168 RTL8168/8111 "
                            "00:e0:4c:68:00:20 link up as eth0"));
    CHECK(fake_log_has(&fp, "[DRIVER ONLINE] r8169 02:00.0 10ec:8169 RTL8169 "
                            "00:e0:4c:68:00:40 link up as eth1"));
    CHECK(hw_selftest_all(&fp.plat, &r) == 2);
    CHECK(fake_log_has(&fp, "[SELFTEST OK] eth0 DHCP offer received; mac 00:e0:4c:68:00:20; "
                            "offered 10.0.2.15 by 10.0.2.2"));
    CHECK(fake_log_has(&fp, "[SELFTEST OK] eth1 DHCP offer received"));
    CHECK(a->peer.discovers == 1 && b->peer.discovers == 1);
    CHECK(a->violations == 0 && b->violations == 0);
    fake_plat_free(&fp);
    free(a);
    free(b);
}

int main(void)
{
    test_ids();
    test_bind_models();
    test_echo_and_wrap(0x8168, K_8168);
    test_echo_and_wrap(0x8169, K_8169);
    test_echo_and_wrap(0x8136, K_8101);
    test_rx_full_and_errors();
    test_faults();
    test_tx_stall_and_link_down();
    test_no_bar_unknown_full_dma();
    test_probe_and_selftest();
    printf("test_r8169: %d checks, %d failures\n", fk_checks, fk_failures);
    return fk_failures ? 1 : 0;
}
