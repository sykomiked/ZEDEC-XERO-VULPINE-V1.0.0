/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_r8125.c — r8169.c's RTL8125 path against a register-level fake
 * RTL8125 (10EC:8125, and the Killer E3000, 10EC:3000).
 *
 * This fake is the ONLY thing the RTL8125 path has been run against: QEMU
 * has no model of it. It follows the mainline Linux r8169 driver's view of
 * the chip: an I/O BAR0, the 64 KB register window in BAR2 and an MSI-X
 * BAR4; the chip version in TxConfig bits 20-31 (read-only); the 32-bit
 * interrupt mask/status at 0x38/0x3C, where the older chips have TxPoll
 * (so an 8168-style NPQ kick lands in the interrupt mask and transmits
 * nothing); INT_CFG0 at 0x34; transmit kicked through TxPoll_8125 0x90 bit
 * 0; RSS and the queue count at 0x4500/0x4800; receive gated by
 * MISC.RXDV_GATED_EN; MCU.NOW_IS_OOB and the RX/TX FIFO empty bits; MAC OCP
 * registers through OCPDR 0xB0 with the new transmit descriptor format
 * (OCP 0xEB58 bit 0) on after reset, under which legacy descriptors are not
 * sent; the saved station address at MAC0_BKP 0x19E0. Reset restores dirty
 * defaults (receive gated and open, VLAN stripping on, RSS on, every
 * interrupt unmasked) and is counted as unsafe unless receive was closed
 * and gated first. Rings, OWN handoff, EOR wrap and PHYstatus link are as
 * in test_r8169.c, including receive lengths with the CRC and no padding of
 * short frames. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hwfake.h"
#include "nicpeer.h"

enum {
    IDR0 = 0x00,
    MAR0 = 0x08,
    TNPDS = 0x20,
    INTCFG0 = 0x34,
    CHIPCMD = 0x37,
    IMR = 0x38,
    ISR = 0x3C,
    TXCONFIG = 0x40,
    RXCONFIG = 0x44,
    CFG9346 = 0x50,
    PHYSTATUS = 0x6C,
    TXPOLL = 0x90,
    OCPDR = 0xB0,
    MCU = 0xD3,
    RXMAXSIZE = 0xDA,
    CPLUSCMD = 0xE0,
    RDSAR = 0xE4,
    MAXTXPKT = 0xEC,
    MISC = 0xF0,
    MAC0_BKP = 0x19E0,
    RSS_CTRL = 0x4500,
    QNUM_CTRL = 0x4800,
};
#define REGS      0x10000
#define OWN       (1u << 31)
#define EOR       (1u << 30)
#define FS        (1u << 29)
#define LS        (1u << 28)
#define RES       (1u << 21)
#define XID_MASK  0xFCF00000u
#define RXDV_GATE (1u << 19)
#define OOB       0x80
#define FIFO_MT   0x30
#define OCP_NDESC 0xEB58

typedef struct frtl {
    uint8_t *r;
    uint32_t xid;
    uint8_t mac[6], bkp[6];
    uint16_t ocp[0x8000];
    peer_t peer;
    int reset_stuck, zero_mac, unplugged, tx_dd_never, ones, rx_err_next, split_next, fifo_busy;
    uint64_t link_at;
    uint32_t txi, rxi;
    uint32_t resets, unsafe_resets, kicks, tx_frames, rx_frames, rx_filtered, rx_full, violations,
        short_tx, new_fmt_drops;
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
    memset(f->r, 0, REGS);
    if (!f->zero_mac) memcpy(f->r + IDR0, f->mac, 6);
    memcpy(f->r + MAC0_BKP, f->bkp, 6);
    memset(f->r + MAR0, 0xFF, 8);
    wr16(f, CPLUSCMD, 0x0060);
    wr32(f, IMR, 0xFFFFFFFFu);
    wr32(f, ISR, 0x000000A5u);
    f->r[INTCFG0] = 0x01;
    wr32(f, TXCONFIG, (f->xid << 20) & XID_MASK);
    wr32(f, RXCONFIG, (3u << 22) | 0x0F); /* VLAN strip on, accepting */
    wr32(f, MISC, RXDV_GATE);
    f->r[MCU] = (uint8_t) (OOB | (f->fifo_busy ? 0 : FIFO_MT));
    wr32(f, RSS_CTRL, 0x1);
    wr16(f, QNUM_CTRL, 0x3);
    memset(f->ocp, 0, sizeof f->ocp);
    f->ocp[OCP_NDESC / 2] = 0x0001;
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
    if (!(rd16(f, TXPOLL) & 1)) return;
    if (f->tx_dd_never) return; /* the DMA engine never picks the kick up */
    wr16(f, TXPOLL, 0);
    f->kicks++;
    if (!(f->r[CHIPCMD] & 0x04)) return;
    if (f->ocp[OCP_NDESC / 2] & 1) {
        f->new_fmt_drops++; /* legacy descriptors read as the new format: nothing sent */
        return;
    }
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
        if (len < 60) f->short_tx++;
        f->last_tx_len = len;
        f->tx_frames++;
        peer_on_tx(&f->peer, fr, len);
        fk_wr32(d, o1 & ~OWN);
        if (o1 & EOR)
            f->txi = 0;
        else if (++f->txi >= 16) {
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
    if (!(f->r[CHIPCMD] & 0x08) || (rd32(f, MISC) & RXDV_GATE)) return;
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
        memset(buf + len, 0xCC, 4);
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

/* MAC OCP: a write (bit 31) stores the low half at reg; a read leaves the
 * data in the low half. Either way OCPDR then holds reg and data with bit
 * 31 clear, which reads back as the same register again. */
static void do_ocp(frtl_t *f)
{
    uint32_t v = rd32(f, OCPDR), reg = (v >> 15) & 0xFFFE;
    if (v & (1u << 31)) f->ocp[reg / 2] = (uint16_t) v;
    wr32(f, OCPDR, (reg << 15) | f->ocp[reg / 2]);
}

static void tick(fake_dev_t *d, uint64_t now)
{
    frtl_t *f = (frtl_t *) d->state;
    if (f->ones) {
        memset(f->r, 0xFF, REGS);
        return;
    }
    if (f->r[CHIPCMD] & 0x10) {
        if (f->reset_stuck) return;
        if ((rd32(f, RXCONFIG) & 0x3F) || !(rd32(f, MISC) & RXDV_GATE)) f->unsafe_resets++;
        do_reset(f);
    }
    wr32(f, TXCONFIG, (rd32(f, TXCONFIG) & ~XID_MASK) | ((f->xid << 20) & XID_MASK));
    do_ocp(f);
    f->r[PHYSTATUS] = link_ok(f, now) ? 0x02 | 0x01 : 0;
    if (!link_ok(f, now)) return;
    do_tx(f);
    do_rx(f);
}

static const uint8_t mac_a[6] = {0x00, 0xE0, 0x4C, 0x68, 0x00, 0x01};
static const uint8_t bkp_a[6] = {0x00, 0xE0, 0x4C, 0x25, 0x00, 0x01};

static frtl_t *add_rtl(fake_plat_t *fp, uint32_t bdf, uint16_t did, uint32_t xid)
{
    fake_dev_t *d = fake_dev_new(fp, bdf, 0x10EC, did, 0x02, 0x00, 0x00);
    frtl_t *f = (frtl_t *) calloc(1, sizeof *f);
    fake_dev_bar_at(d, 0, 0xE000, 256, FAKE_BAR_IO);
    f->r = fake_dev_bar(d, 2, REGS);
    fake_dev_bar(d, 4, 0x4000);
    f->xid = xid;
    memcpy(f->mac, mac_a, 6);
    memcpy(f->bkp, bkp_a, 6);
    f->mac[5] = f->bkp[5] = (uint8_t) (bdf >> 8);
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

static env_t *env_new(uint16_t did, uint32_t xid, void (*breakit)(frtl_t *))
{
    env_t *e = (env_t *) calloc(1, sizeof *e);
    fake_plat_init(&e->fp);
    e->f = add_rtl(&e->fp, HW_BDF(1, 0, 0), did, xid);
    e->f->mac[5] = 0x42;
    e->f->bkp[5] = 0x42;
    if (breakit) breakit(e->f);
    do_reset(e->f);
    if (e->f->ones) memset(e->f->r, 0xFF, REGS);
    e->f->resets = 0;
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
    hw_pci_dev_t d;
    memset(&d, 0, sizeof d);
    d.vendor = 0x10EC;
    d.class_code = 2;
    static const uint16_t ours[] = {0x8125, 0x3000};
    for (uint32_t i = 0; i < 2; i++) {
        int listed = 0;
        for (const hw_id_t *id = r8169_ids; id->vendor; id++)
            listed += id->vendor == 0x10EC && id->device == ours[i];
        CHECK(listed == 1);
        d.device = ours[i];
        const hw_driver_t *m = hw_match(hw_drivers, hw_n_drivers, &d);
        CHECK(m && m->bind == r8169_bind && strcmp(m->name, "r8169") == 0);
    }
    /* RTL8126 (5G), RTL8127 (10G) and the Killer E5000 are other designs */
    static const uint16_t not_ours[] = {0x8126, 0x8127, 0x5000};
    for (uint32_t i = 0; i < 3; i++) {
        d.device = not_ours[i];
        const hw_driver_t *m = hw_match(hw_drivers, hw_n_drivers, &d);
        CHECK(!m || m->bind != r8169_bind);
    }
}

typedef struct ver {
    uint32_t xid;
    const char *name;
    int pause;
} ver_t;

static void check_bound(env_t *e, int pause)
{
    frtl_t *f = e->f;
    wait_us(&e->fp, 1); /* the fake latches the last OCP write on its next tick */
    CHECK(e->v == HW_T);
    CHECK(e->r.n_nic == 1);
    hw_nic_t *n = e->r.nic[0];
    CHECK(strcmp(n->name, "eth0") == 0 && n->link_up == 1);
    CHECK(f->resets == 1 && f->unsafe_resets == 0);
    CHECK((e->fp.devs[0]->cfg[1] & 0x0407) == 0x0406);
    CHECK(rd32(f, IMR) == 0 && f->r[INTCFG0] == 0);
    CHECK((rd32(f, MISC) & RXDV_GATE) == 0);
    CHECK((f->r[MCU] & OOB) == 0);
    CHECK(rd32(f, RSS_CTRL) == 0 && rd16(f, QNUM_CTRL) == 0);
    CHECK((f->ocp[OCP_NDESC / 2] & 1) == 0);
    CHECK((rd16(f, CPLUSCMD) & 0x60) == 0);
    CHECK(rd16(f, RXMAXSIZE) == 2048);
    CHECK(f->r[MAXTXPKT] == 0);
    CHECK(f->r[CFG9346] == 0x00);
    CHECK(f->r[CHIPCMD] == 0x0C);
    CHECK(rd32(f, RXCONFIG) == ((8u << 27) | (7u << 8) | 8u | 2u | (pause ? 1u << 11 : 0)));
    CHECK((rd32(f, TXCONFIG) & ~XID_MASK) == ((3u << 24) | (7u << 8)));
    CHECK(rd32(f, MAR0) == 0 && rd32(f, MAR0 + 4) == 0);
    uint8_t *tr = ring_at(f, TNPDS), *rr = ring_at(f, RDSAR);
    CHECK(((uintptr_t) tr & 255) == 0 && ((uintptr_t) rr & 255) == 0);
    int rx_ok = 1, tx_ok = 1;
    for (uint32_t k = 0; k < 32; k++) {
        if (fk_rd32(rr + k * 16) != (OWN | (k == 31 ? EOR : 0) | 2048)) rx_ok = 0;
        if (fk_rd64(rr + k * 16 + 8) != fk_rd64(rr + 8) + 2048ULL * k) rx_ok = 0;
    }
    for (uint32_t k = 0; k < 16; k++)
        if (fk_rd32(tr + k * 16) != (k == 15 ? EOR : 0)) tx_ok = 0;
    CHECK(rx_ok && tx_ok);
}

static void test_bind_versions(void)
{
    /* Linux v7.2 rtl_chip_infos 8125 rows; 0xE41 has XID bit 11, outside
     * the 0x7CF mask, and is still an 8125B. */
    static const ver_t vs[] = {
        {0x609, "RTL8125A", 0}, {0x641, "RTL8125B", 1},  {0xE41, "RTL8125B", 1},
        {0x688, "RTL8125D", 1}, {0x689, "RTL8125D", 1},  {0x68A, "RTL8125K", 1},
        {0x68B, "RTL9151A", 1}, {0x708, "RTL8125CP", 1}, {0x681, "RTL8125BP", 1},
    };
    for (uint32_t i = 0; i < sizeof vs / sizeof vs[0]; i++) {
        env_t *e = env_new(0x8125, vs[i].xid, 0);
        char want[96];
        snprintf(want, sizeof want, "RTL8125 %s 00:e0:4c:25:00:42 link up as eth0", vs[i].name);
        if (strcmp(e->note.b, want)) printf("  note '%s' want '%s'\n", e->note.b, want);
        CHECK(strcmp(e->note.b, want) == 0);
        check_bound(e, vs[i].pause);
        env_free(e);
    }
    env_t *e = env_new(0x3000, 0x641, 0);
    CHECK(strcmp(e->note.b, "Killer E3000 RTL8125B 00:e0:4c:25:00:42 link up as eth0") == 0);
    check_bound(e, 1);
    env_free(e);
}

static void k_bkp_zero(frtl_t *f)
{
    memset(f->bkp, 0, 6);
}
static void k_bkp_group(frtl_t *f)
{
    f->bkp[0] = 0x01;
}
static void k_both_zero(frtl_t *f)
{
    memset(f->bkp, 0, 6);
    f->zero_mac = 1;
}
static void k_idr_group(frtl_t *f)
{
    memset(f->bkp, 0, 6);
    f->mac[0] = 0x01;
}

static void test_mac_sources(void)
{
    /* the saved copy wins and is written to IDR0-5 */
    env_t *e = env_new(0x8125, 0x641, 0);
    CHECK(e->v == HW_T);
    CHECK(memcmp(e->r.nic[0]->mac, e->f->bkp, 6) == 0);
    CHECK(memcmp(e->f->r + IDR0, e->f->bkp, 6) == 0);
    env_free(e);
    static const struct {
        void (*k)(frtl_t *);
        hw_verdict_t want;
        const char *note;
    } cs[] = {
        {k_bkp_zero, HW_T, "RTL8125B 00:e0:4c:68:00:42 link up as eth0"},
        {k_bkp_group, HW_T, "RTL8125B 00:e0:4c:68:00:42 link up as eth0"},
        {k_both_zero, HW_F, "RTL8125B no MAC address"},
        {k_idr_group, HW_B, "01:e0:4c:68:00:42 has the group bit set"},
    };
    for (uint32_t i = 0; i < sizeof cs / sizeof cs[0]; i++) {
        e = env_new(0x8125, 0x641, cs[i].k);
        if (e->v != cs[i].want || !strstr(e->note.b, cs[i].note))
            printf("  mac case %u: verdict %d note '%s'\n", i, e->v, e->note.b);
        CHECK(e->v == cs[i].want);
        CHECK(strstr(e->note.b, cs[i].note) != 0);
        if (e->v == HW_T) {
            CHECK(memcmp(e->r.nic[0]->mac, e->f->mac, 6) == 0);
            CHECK(memcmp(e->f->r + IDR0, e->f->mac, 6) == 0);
        }
        env_free(e);
    }
}

static void test_echo_and_wrap(uint32_t xid)
{
    env_t *e = env_new(0x8125, xid, 0);
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
    CHECK(f->new_fmt_drops == 0 && f->violations == 0);
    CHECK(rd32(f, IMR) == 0); /* no kick written into the interrupt mask */
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
    env_t *e = env_new(0x8125, 0x641, 0);
    frtl_t *f = e->f;
    hw_nic_t *n = e->r.nic[0];
    uint8_t fr[200], rx[2048];
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
}
static void k_unplug(frtl_t *f)
{
    f->unplugged = 1;
}
static void k_fifo(frtl_t *f)
{
    f->fifo_busy = 1;
}
static void k_late(frtl_t *f)
{
    f->link_at = 2000000;
}

typedef struct fault {
    const char *what;
    uint32_t xid;
    void (*breakit)(frtl_t *);
    hw_verdict_t want;
    const char *note;
} fault_t;

static void test_faults(void)
{
    static const fault_t fs[] = {
        {"reset stuck", 0x641, k_reset, HW_F, "RTL8125 RTL8125B reset timed out"},
        {"all-ones", 0x641, k_ones, HW_F, "RTL8125 registers read all-ones"},
        {"RTL8126A behind an 8125 id", 0x64A, 0, HW_B,
         "RTL8125 TxConfig XID 64a is not an RTL8125"},
        {"RTL8127A behind an 8125 id", 0x6C9, 0, HW_B, "TxConfig XID 6c9 is not an RTL8125"},
        {"RTL8168h XID", 0x541, 0, HW_B, "TxConfig XID 541 is not an RTL8125"},
        {"unplugged", 0x641, k_unplug, HW_T, "00:e0:4c:25:00:42 link down as eth0"},
        {"FIFO never empty", 0x641, k_fifo, HW_T, "link up as eth0"},
        {"link after 2 s", 0x609, k_late, HW_T, "RTL8125A 00:e0:4c:25:00:42 link up as eth0"},
    };
    for (uint32_t i = 0; i < sizeof fs / sizeof fs[0]; i++) {
        env_t *e = env_new(0x8125, fs[i].xid, fs[i].breakit);
        if (e->v != fs[i].want || !strstr(e->note.b, fs[i].note))
            printf("  fault '%s': verdict %d note '%s'\n", fs[i].what, e->v, e->note.b);
        CHECK(e->v == fs[i].want);
        CHECK(strstr(e->note.b, fs[i].note) != 0);
        CHECK((e->v == HW_T) == (e->r.n_nic == 1));
        CHECK(e->fp.now_us < 2600000u);
        if (e->v == HW_B) CHECK(e->f->resets == 0); /* held untouched */
        if (e->v == HW_T) CHECK(e->f->unsafe_resets == 0);
        env_free(e);
    }
}

static void test_tx_full_and_link_down(void)
{
    env_t *e = env_new(0x8125, 0x641, 0);
    hw_nic_t *n = e->r.nic[0];
    e->f->tx_dd_never = 1;
    uint8_t tx[64];
    peer_mk(tx, peer_mac, n->mac, 64, 0);
    uint64_t t0 = e->fp.now_us;
    CHECK(n->send(n, tx, 64) == -1);
    CHECK(e->fp.now_us - t0 >= 50000 && e->fp.now_us - t0 < 60000);
    /* all 16 can be outstanding; the 17th is refused at once */
    for (int k = 1; k < 16; k++) CHECK(n->send(n, tx, 64) == -1);
    t0 = e->fp.now_us;
    CHECK(n->send(n, tx, 64) == -2);
    CHECK(e->fp.now_us == t0);
    e->f->tx_dd_never = 0;
    wait_us(&e->fp, 10);
    CHECK(e->f->tx_frames == 16);
    for (int k = 0; k < 20; k++) CHECK(n->send(n, tx, 64) == 0);
    CHECK(e->f->tx_frames == 36);
    CHECK(e->f->violations == 0);
    env_free(e);

    e = env_new(0x8125, 0x641, k_unplug);
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
    fake_dev_new(&fp, HW_BDF(1, 0, 0), 0x10EC, 0x8125, 2, 0, 0);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_one(&fp, &r, &note) == HW_N);
    CHECK(strcmp(note.b, "no memory BAR") == 0);
    fake_plat_free(&fp);

    /* a 256-byte window (8168-sized) cannot hold the 8125 registers */
    fake_plat_init(&fp);
    fake_dev_t *d0 = fake_dev_new(&fp, HW_BDF(1, 0, 0), 0x10EC, 0x8125, 2, 0, 0);
    fake_dev_bar(d0, 2, 256);
    memset(&r, 0, sizeof r);
    CHECK(bind_one(&fp, &r, &note) == HW_N);
    CHECK(strcmp(note.b, "no memory BAR") == 0);
    fake_plat_free(&fp);

    env_t *e = env_new(0x8125, 0x641, 0);
    hw_pci_dev_t d = g_dev[0];
    d.device = 0x8126;
    hw_line_init(&e->note);
    CHECK(r8169_bind(&e->fp.plat, &d, &e->r, &e->note) == HW_N);
    CHECK(strcmp(e->note.b, "unknown device id") == 0);
    d.device = 0x8125;
    d.vendor = 0x8086; /* right device id, wrong vendor */
    hw_line_init(&e->note);
    CHECK(r8169_bind(&e->fp.plat, &d, &e->r, &e->note) == HW_N);
    CHECK(strcmp(e->note.b, "unknown device id") == 0);
    d.vendor = 0x10EC;
    e->r.n_nic = HW_MAX_NIC;
    hw_line_init(&e->note);
    CHECK(r8169_bind(&e->fp.plat, &d, &e->r, &e->note) == HW_N);
    CHECK(strstr(e->note.b, "NIC table full") != 0);
    env_free(e);

    for (uint32_t k = 1; k <= 5; k++) {
        fake_plat_init(&fp);
        frtl_t *f = add_rtl(&fp, HW_BDF(1, 0, 0), 0x8125, 0x641);
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
    frtl_t *a = add_rtl(&fp, HW_BDF(1, 0, 0), 0x8125, 0x641);
    frtl_t *b = add_rtl(&fp, HW_BDF(2, 0, 0), 0x3000, 0x609);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_probe_stats_t st = hw_probe_all(&fp.plat, &r);
    CHECK(st.functions == 2 && st.bound == 2 && st.failed == 0 && st.held == 0);
    CHECK(fake_log_has(&fp, "[DRIVER ONLINE] r8169 01:00.0 10ec:8125 RTL8125 RTL8125B "
                            "00:e0:4c:25:00:01 link up as eth0"));
    CHECK(fake_log_has(&fp, "[DRIVER ONLINE] r8169 02:00.0 10ec:3000 Killer E3000 RTL8125A "
                            "00:e0:4c:25:00:02 link up as eth1"));
    CHECK(hw_selftest_all(&fp.plat, &r) == 2);
    CHECK(fake_log_has(&fp, "[SELFTEST OK] eth0 DHCP offer received; mac 00:e0:4c:25:00:01; "
                            "offered 10.0.2.15 by 10.0.2.2"));
    CHECK(fake_log_has(&fp, "[SELFTEST OK] eth1 DHCP offer received"));
    CHECK(a->peer.discovers == 1 && b->peer.discovers == 1);
    CHECK(a->violations == 0 && b->violations == 0);
    fake_plat_free(&fp);
    free(a);
    free(b);

    /* a contradicted chip is held, the boot goes on */
    fake_plat_init(&fp);
    a = add_rtl(&fp, HW_BDF(1, 0, 0), 0x8125, 0x64A);
    memset(&r, 0, sizeof r);
    st = hw_probe_all(&fp.plat, &r);
    CHECK(st.held == 1 && st.bound == 0 && r.n_nic == 0);
    CHECK(fake_log_has(&fp, "[DRIVER HELD] r8169 01:00.0 10ec:8125 contradiction: RTL8125 "
                            "TxConfig XID 64a is not an RTL8125"));
    fake_plat_free(&fp);
    free(a);
}

int main(void)
{
    test_ids();
    test_bind_versions();
    test_mac_sources();
    test_echo_and_wrap(0x641);
    test_echo_and_wrap(0x609);
    test_rx_full_and_errors();
    test_faults();
    test_tx_full_and_link_down();
    test_no_bar_unknown_full_dma();
    test_probe_and_selftest();
    printf("test_r8125: %d checks, %d failures\n", fk_checks, fk_failures);
    return fk_failures ? 1 : 0;
}
