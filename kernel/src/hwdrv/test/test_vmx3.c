/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vmx3.c — vmx3.c against a register-level fake vmxnet3 (UPT1).
 *
 * The fake models what the driver relies on: the BAR0/BAR1 register windows,
 * the version selects, the command register (the result replaces the command
 * on the next tick — the driver's bounded poll gives the fake its tick), the
 * driver-shared and queue-shared blocks, both receive rings with head/body
 * buffer types, the shared receive completion ring and the tx completion
 * ring — each with vmxnet3's generation-bit ownership. A peer (nicpeer.h)
 * answers DHCP and echoes test frames. Fault knobs break each piece the
 * driver must survive. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hwfake.h"
#include "nicpeer.h"

/* register offsets the fake uses (same values as the driver's) */
enum {
    VRRS = 0x00,
    UVRS = 0x08,
    DSL = 0x10,
    DSH = 0x18,
    CMD = 0x20,
    MACL = 0x28,
    MACH = 0x30,
    TXH0 = 0x600,
};
enum {
    CMD_ENABLE = 0xCAFE0000,
    CMD_DISABLE = 0xCAFE0001,
    CMD_SET_RXMODE = 0xCAFE0003,
    CMD_SET_FILTER = 0xCAFE0004,
    CMD_SET_FEATURE = 0xCAFE0009,
    CMD_GET_LINK = 0xF00D0002,
    CMD_GET_MACL = 0xF00D0003,
    CMD_GET_MACH = 0xF00D0004,
};
/* driver-shared / queue-shared offsets the fake reads */
enum {
    DS_MAGIC = 0,
    DS_QS = 40,
    DS_MTU = 56,
    DS_NTXQ = 62,
    DS_NRXQ = 63,
    DS_RXMODE = 120,
    DS_MCASTLEN = 124,
    DS_MCAST = 128,
};
enum {
    TS_CMD = 16,
    TS_COMP = 32,
    TS_CMDLEN = 56,
    TS_COMPLEN = 64,
    RS_UPD = 0,
    RS_CMD0 = 16,
    RS_CMD1 = 24,
    RS_COMP = 32,
    RS_CMDLEN0 = 56,
    RS_CMDLEN1 = 60,
    RS_COMPLEN = 64,
};
#define TXN   16
#define RXN   32
#define RXBUF 2048

typedef struct fvmx {
    fake_dev_t *dev;
    uint8_t *b0, *b1;
    uint8_t mac[6];
    peer_t peer;
    int enabled;
    uint32_t rxmode, mcast_len;
    uint64_t ds_pa, qs_pa;
    uint64_t txd, txcd, rxd[2], rxcd, mcast;
    uint32_t txn, txcn, rxn[2], rxcn;
    uint32_t tx_cons, tx_gen;
    uint32_t txc_next, txc_gen;
    uint32_t rx_cons[2], rx_gen[2];
    uint32_t rxc_next, rxc_gen;
    uint32_t last_cmd;
    /* fault knobs */
    int bad_vrrs, bad_uvrs, dead_cmd, no_mac, group_mac, enable_refuses, link_down, comp_stall,
        rx_err_next, split_next;
    /* observations */
    uint32_t enables, disables, tx_frames, rx_frames, rx_filtered, rx_full, violations, ds_bad;
    uint32_t last_tx_len;
} fvmx_t;

static uint32_t rd(fvmx_t *f, int bar, uint32_t o)
{
    return fk_rd32((bar ? f->b1 : f->b0) + o);
}
static void wr(fvmx_t *f, int bar, uint32_t o, uint32_t v)
{
    fk_wr32((bar ? f->b1 : f->b0) + o, v);
}

static uint8_t *ds(fvmx_t *f)
{
    return f->ds_pa ? (uint8_t *) fk_ptr(f->ds_pa) : 0;
}
static uint8_t *qs_tx(fvmx_t *f)
{
    return f->qs_pa ? (uint8_t *) fk_ptr(f->qs_pa) : 0;
}
static uint8_t *qs_rx(fvmx_t *f)
{
    return f->qs_pa ? (uint8_t *) fk_ptr(f->qs_pa + 256) : 0;
}

static void post_txc(fvmx_t *f, uint32_t eopidx)
{
    if (f->comp_stall) return;
    uint8_t *cd = (uint8_t *) fk_ptr(f->txcd) + f->txc_next * 16;
    fk_wr32(cd + 0, eopidx);
    fk_wr32(cd + 12, f->txc_gen << 31);
    if (++f->txc_next == f->txcn) {
        f->txc_next = 0;
        f->txc_gen ^= 1;
    }
}

static void post_rxc(fvmx_t *f, uint32_t rid, uint32_t idx, uint32_t len, int err, int sop, int eop)
{
    uint8_t *cd = (uint8_t *) fk_ptr(f->rxcd) + f->rxc_next * 16;
    fk_wr32(cd + 0, idx | (eop ? 1u << 14 : 0) | (sop ? 1u << 15 : 0) | (rid << 16));
    fk_wr32(cd + 8, len | (err ? 1u << 14 : 0));
    fk_wr32(cd + 12, f->rxc_gen << 31);
    if (++f->rxc_next == f->rxcn) {
        f->rxc_next = 0;
        f->rxc_gen ^= 1;
    }
}

/* One command per tick: the last thing the driver wrote to CMD executes and
 * the register gets the result, which is what the driver's poll then reads. */
static void do_cmd(fvmx_t *f)
{
    uint32_t c = rd(f, 1, CMD);
    if (c == f->last_cmd) return;
    if ((c >> 16) != 0xCAFE && (c >> 16) != 0xF00D) return;
    f->last_cmd = c;
    uint32_t res = 0;
    switch (c) {
    case CMD_ENABLE: {
        /* DSL/DSH were written before this command: latch them. */
        f->ds_pa = fk_rd32(f->b1 + DSL) | ((uint64_t) fk_rd32(f->b1 + DSH) << 32);
        f->qs_pa = ds(f) ? fk_rd64(ds(f) + DS_QS) : 0;
        if (f->enable_refuses || !ds(f) || fk_rd32(ds(f) + DS_MAGIC) != 0xBABEFEE1u) {
            res = 1;
            break;
        }
        uint8_t *t = qs_tx(f), *r = qs_rx(f);
        if (!t || !r) {
            res = 1;
            break;
        }
        f->txd = fk_rd64(t + TS_CMD);
        f->txcd = fk_rd64(t + TS_COMP);
        f->txn = fk_rd32(t + TS_CMDLEN);
        f->txcn = fk_rd32(t + TS_COMPLEN);
        f->rxd[0] = fk_rd64(r + RS_CMD0);
        f->rxd[1] = fk_rd64(r + RS_CMD1);
        f->rxcd = fk_rd64(r + RS_COMP);
        f->rxn[0] = fk_rd32(r + RS_CMDLEN0);
        f->rxn[1] = fk_rd32(r + RS_CMDLEN1);
        f->rxcn = fk_rd32(r + RS_COMPLEN);
        f->mcast = fk_rd64(ds(f) + DS_MCAST);
        f->mcast_len = fk_rd32(ds(f) + DS_MCASTLEN);
        if (fk_rd32(ds(f) + DS_MTU) != 1500 || ds(f)[DS_NTXQ] != 1 || ds(f)[DS_NRXQ] != 1)
            f->ds_bad++;
        f->enabled = 1;
        f->enables++;
        break;
    }
    case CMD_DISABLE:
        f->enabled = 0;
        f->disables++;
        break;
    case CMD_SET_RXMODE:
        if (ds(f)) f->rxmode = fk_rd32(ds(f) + DS_RXMODE);
        break;
    case CMD_GET_LINK:
        res = f->link_down ? 0 : (1 | (10000u << 16));
        break;
    case CMD_GET_MACL:
        res = f->no_mac ? 0
                        : (uint32_t) f->mac[0] | ((uint32_t) f->mac[1] << 8) |
                              ((uint32_t) f->mac[2] << 16) | ((uint32_t) f->mac[3] << 24);
        break;
    case CMD_GET_MACH:
        res = f->no_mac ? 0 : (uint32_t) f->mac[4] | ((uint32_t) f->mac[5] << 8);
        break;
    default:
        break; /* SET_FILTER / SET_FEATURE / ... acknowledge with 0 */
    }
    if (f->dead_cmd) return; /* the register never answers */
    wr(f, 1, CMD, res);
    f->last_cmd = res;
}

static int rx_accept(fvmx_t *f, const uint8_t *fr)
{
    static const uint8_t bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (!memcmp(fr, bc, 6)) return (f->rxmode >> 2) & 1;
    if (fr[0] & 1) return (f->rxmode >> 3) & 1; /* multicast: ALLMULTI */
    return (f->rxmode & 1) && !memcmp(fr, f->mac, 6);
}

static void do_tx(fvmx_t *f)
{
    if (!f->enabled || !f->txd) return;
    uint32_t prod = rd(f, 0, TXH0);
    if (prod >= f->txn) {
        f->violations++;
        return;
    }
    while (f->tx_cons != prod) {
        uint8_t *d = (uint8_t *) fk_ptr(f->txd) + f->tx_cons * 16;
        uint32_t w2 = fk_rd32(d + 8), w3 = fk_rd32(d + 12);
        if (((w2 >> 14) & 1) != f->tx_gen) { /* not the driver's generation */
            f->violations++;
            break;
        }
        uint32_t len = w2 & 0x3FFF;
        if (!(w3 & (1u << 12)) || !(w3 & (1u << 13))) f->violations++; /* EOP|COMPREQ */
        if (len > 1514) f->violations++;
        uint8_t fr[PEER_FRAME];
        memset(fr, 0, sizeof fr);
        if (len && len <= PEER_FRAME) memcpy(fr, fk_ptr(fk_rd64(d)), len);
        f->last_tx_len = len;
        f->tx_frames++;
        peer_on_tx(&f->peer, fr, len);
        post_txc(f, f->tx_cons);
        if (++f->tx_cons == f->txn) {
            f->tx_cons = 0;
            f->tx_gen ^= 1;
        }
    }
}

/* Consume one receive descriptor in ring `rid`: copy the frame into its
 * buffer and post the completion. 0 = the slot is still the driver's. */
static int rx_one(fvmx_t *f, uint32_t rid, const uint8_t *fr, uint32_t len, int err, int sop,
                  int eop)
{
    uint32_t cons = f->rx_cons[rid];
    uint8_t *d = (uint8_t *) fk_ptr(f->rxd[rid]) + cons * 16;
    if (((fk_rd32(d + 8) >> 31) & 1) != f->rx_gen[rid]) return 0;
    uint32_t blen = fk_rd32(d + 8) & 0x3FFF;
    if (blen < len) {
        f->violations++;
        return 0;
    }
    memcpy(fk_ptr(fk_rd64(d)), fr, len);
    post_rxc(f, rid, cons, len, err, sop, eop);
    if (++f->rx_cons[rid] == f->rxn[rid]) {
        f->rx_cons[rid] = 0;
        f->rx_gen[rid] ^= 1;
    }
    qs_rx(f)[RS_UPD] = 1; /* the producer index needs a doorbell */
    return 1;
}

static void do_rx(fvmx_t *f)
{
    if (!f->enabled || !f->rxd[0]) return;
    peer_frame_t *pf;
    while ((pf = peer_front(&f->peer)) != 0) {
        if (!rx_accept(f, pf->b)) {
            f->rx_filtered++;
            peer_pop(&f->peer);
            continue;
        }
        int err = pf->err;
        if (f->rx_err_next) {
            err = 1;
            f->rx_err_next = 0;
        }
        int done;
        if (f->split_next && pf->len > 8) {
            /* a frame scattered over two descriptors: head SOP, tail EOP */
            f->split_next = 0;
            uint32_t h = pf->len / 2;
            done =
                rx_one(f, 0, pf->b, h, err, 1, 0) && rx_one(f, 0, pf->b + h, pf->len - h, 0, 0, 1);
        } else {
            done = rx_one(f, 0, pf->b, pf->len, err, 1, 1);
        }
        if (!done) {
            f->rx_full++;
            return; /* frames wait in the FIFO */
        }
        f->rx_frames++;
        peer_pop(&f->peer);
    }
}

static void tick(fake_dev_t *d, uint64_t now)
{
    (void) now;
    fvmx_t *f = (fvmx_t *) d->state;
    do_cmd(f);
    if (!f->enabled) return;
    do_tx(f);
    do_rx(f);
}

static const uint8_t mac_a[6] = {0x00, 0x50, 0x56, 0x5A, 0x58, 0x01};

static fvmx_t *add_vmx(fake_plat_t *fp, uint32_t bdf)
{
    fake_dev_t *d = fake_dev_new(fp, bdf, 0x15AD, 0x07B0, 0x02, 0x00, 0x00);
    fvmx_t *f = (fvmx_t *) calloc(1, sizeof *f);
    f->dev = d;
    f->b0 = fake_dev_bar(d, 0, 0x2000);
    f->b1 = fake_dev_bar(d, 2, 0x1000); /* slot 2: BAR0 is 64-bit */
    memcpy(f->mac, mac_a, 6);
    f->mac[5] = (uint8_t) (bdf >> 3);
    wr(f, 1, VRRS, 0x3); /* revisions 1 and 2 on offer */
    wr(f, 1, UVRS, 0x1); /* UPT version 1 */
    f->tx_gen = 1;
    f->txc_gen = 1;
    f->rx_gen[0] = f->rx_gen[1] = 1;
    f->rxc_gen = 1;
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
    return vmx3_bind(&fp->plat, &g_dev[0], r, note);
}

typedef struct env {
    fake_plat_t fp;
    hw_registry_t r;
    hw_line_t note;
    fvmx_t *f;
    hw_verdict_t v;
} env_t;

static env_t *env_new(void (*breakit)(fvmx_t *))
{
    env_t *e = (env_t *) calloc(1, sizeof *e);
    fake_plat_init(&e->fp);
    e->f = add_vmx(&e->fp, HW_BDF(0, 5, 0));
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

/* ---- happy paths ------------------------------------------------------------ */
static void test_bind(void)
{
    env_t *e = env_new(0);
    fvmx_t *f = e->f;
    char want[96];
    snprintf(want, sizeof want, "vmxnet3 00:50:56:5a:58:05 link up as eth0");
    if (strcmp(e->note.b, want)) printf("  note '%s' want '%s'\n", e->note.b, want);
    CHECK(e->v == HW_T);
    CHECK(strcmp(e->note.b, want) == 0);
    CHECK(e->r.n_nic == 1);
    hw_nic_t *n = e->r.nic[0];
    CHECK(strcmp(n->name, "eth0") == 0);
    CHECK(memcmp(n->mac, f->mac, 6) == 0);
    CHECK(n->link_up == 1);
    CHECK(f->enables == 1);
    /* version selects written back, MAC programmed into the window */
    CHECK(rd(f, 1, VRRS) == 2);
    CHECK(rd(f, 1, UVRS) == 1);
    CHECK(rd(f, 1, MACL) == ((uint32_t) f->mac[0] | ((uint32_t) f->mac[1] << 8) |
                             ((uint32_t) f->mac[2] << 16) | ((uint32_t) f->mac[3] << 24)));
    CHECK(rd(f, 1, MACH) == ((uint32_t) f->mac[4] | ((uint32_t) f->mac[5] << 8)));
    /* the shared block the device got */
    uint8_t *s = ds(f);
    CHECK(s != 0 && fk_rd32(s + DS_MAGIC) == 0xBABEFEE1u);
    CHECK(fk_rd32(s + DS_MTU) == 1500);
    CHECK(fk_rd32(s + DS_RXMODE) == (1 | 4 | 8));
    CHECK(f->ds_bad == 0);
    /* every receive descriptor of ring 1 points at a buffer, head-type, and
     * is marked device-owned (gen 1) */
    uint8_t *ring = (uint8_t *) fk_ptr(f->rxd[0]);
    for (uint32_t k = 0; k < RXN; k++) {
        uint32_t w2 = fk_rd32(ring + k * 16 + 8);
        CHECK((w2 & 0x3FFF) == RXBUF);
        CHECK(((w2 >> 14) & 1) == 0);
        CHECK(w2 >> 31);
        CHECK(fk_rd64(ring + k * 16));
    }
    /* ring 2 carries body buffers */
    ring = (uint8_t *) fk_ptr(f->rxd[1]);
    for (uint32_t k = 0; k < RXN; k++) {
        uint32_t w2 = fk_rd32(ring + k * 16 + 8);
        CHECK((w2 & 0x3FFF) == RXBUF && ((w2 >> 14) & 1) && (w2 >> 31));
    }
    env_free(e);
}

static void test_echo_and_wrap(void)
{
    env_t *e = env_new(0);
    fvmx_t *f = e->f;
    CHECK(e->v == HW_T);
    hw_nic_t *n = e->r.nic[0];
    uint8_t tx[1600], rx[2048];
    /* 100 round trips wrap the 16-entry TX ring 6 times and the 32-entry RX
     * ring 3 times; sizes sweep 60..1514 */
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
    CHECK(f->tx_frames == 100 && f->rx_frames == 100);
    CHECK(f->violations == 0);
    CHECK(n->send(n, tx, 0) == -1);
    CHECK(n->send(n, tx, 1515) == -1);
    CHECK(n->send(n, 0, 60) == -1);
    /* a short buffer gets the head of the frame, the frame is consumed */
    memset(rx, 0, sizeof rx);
    CHECK(n->send(n, tx, 60) == 0);
    wait_us(&e->fp, 10);
    CHECK(n->recv(n, rx, 20) == 20);
    CHECK(memcmp(rx, n->mac, 6) == 0 && rx[20] == 0);
    CHECK(n->recv(n, rx, sizeof rx) == 0);
    env_free(e);
}

static void test_rx_full_and_errors(void)
{
    env_t *e = env_new(0);
    fvmx_t *f = e->f;
    hw_nic_t *n = e->r.nic[0];
    uint8_t fr[200], rx[2048];
    /* 40 frames while nobody reads: the ring holds 32, the rest wait in the
     * FIFO, nothing is overwritten */
    for (uint32_t k = 0; k < 40; k++) {
        peer_mk(fr, n->mac, peer_mac, 64 + k, k);
        peer_push(&f->peer, fr, 64 + k, 0);
    }
    wait_us(&e->fp, 10);
    CHECK(f->rx_frames == 32);
    CHECK(f->rx_full > 0);
    CHECK(f->peer.count == 8);
    int order = 1;
    for (uint32_t k = 0; k < 40; k++) {
        int got = n->recv(n, rx, sizeof rx);
        if (!got) {
            wait_us(&e->fp, 10);
            got = n->recv(n, rx, sizeof rx);
        }
        peer_mk(fr, n->mac, peer_mac, 64 + k, k);
        if (got != (int) (64 + k) || memcmp(rx, fr, 64 + k)) order = 0;
    }
    CHECK(order);
    CHECK(f->violations == 0);
    CHECK(f->peer.count == 0);

    /* a frame with the error bit, a frame split over two descriptors, then a
     * good one: recv skips the bad one, reassembles the split, returns the
     * good one */
    uint8_t fr2[200];
    peer_mk(fr, n->mac, peer_mac, 100, 1);
    f->rx_err_next = 1;
    peer_push(&f->peer, fr, 100, 0);
    wait_us(&e->fp, 10);
    memcpy(fr2, fr, 100);
    f->split_next = 1;
    peer_push(&f->peer, fr2, 100, 0);
    wait_us(&e->fp, 10);
    peer_mk(fr, n->mac, peer_mac, 77, 2);
    peer_push(&f->peer, fr, 77, 0);
    wait_us(&e->fp, 10);
    memset(rx, 0, sizeof rx);
    CHECK(n->recv(n, rx, sizeof rx) == 100); /* the split frame, reassembled */
    CHECK(memcmp(rx, fr2, 100) == 0);
    memset(rx, 0, sizeof rx);
    CHECK(n->recv(n, rx, sizeof rx) == 77);
    CHECK(memcmp(rx, fr, 77) == 0);
    CHECK(n->recv(n, rx, sizeof rx) == 0);

    /* address filter: another station's unicast never reaches the ring,
     * broadcast does */
    static const uint8_t other[6] = {0x02, 0, 0, 0, 0, 9};
    peer_mk(fr, other, peer_mac, 64, 3);
    peer_push(&f->peer, fr, 64, 0);
    static const uint8_t bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    peer_mk(fr, bc, peer_mac, 65, 4);
    peer_push(&f->peer, fr, 65, 0);
    wait_us(&e->fp, 10);
    CHECK(f->rx_filtered == 1);
    CHECK(n->recv(n, rx, sizeof rx) == 65);
    CHECK(n->recv(n, rx, sizeof rx) == 0);
    CHECK(f->violations == 0);
    env_free(e);
}

static void test_selftest_nic(void)
{
    /* hw_probe_all + hw_selftest_all: a DHCP offer through the fake */
    env_t *e = (env_t *) calloc(1, sizeof *e);
    fake_plat_init(&e->fp);
    e->f = add_vmx(&e->fp, HW_BDF(0, 5, 0));
    hw_probe_stats_t st = hw_probe_all(&e->fp.plat, &e->r);
    CHECK(st.bound == 1 && st.failed == 0);
    uint32_t ok = hw_selftest_all(&e->fp.plat, &e->r);
    CHECK(ok == 1);
    CHECK(fake_log_has(&e->fp, "[DRIVER ONLINE] vmx3 00:05.0 15ad:07b0"));
    CHECK(fake_log_has(&e->fp, "DHCP offer received"));
    env_free(e);
}

/* ---- faults ----------------------------------------------------------------- */
static void k_vrrs(fvmx_t *f)
{
    wr(f, 1, VRRS, 0);
}
static void k_uvrs(fvmx_t *f)
{
    wr(f, 1, UVRS, 0);
}
static void k_nomac(fvmx_t *f)
{
    f->no_mac = 1;
}
static void k_group(fvmx_t *f)
{
    f->mac[0] |= 1;
}
static void k_enable(fvmx_t *f)
{
    f->enable_refuses = 1;
}
static void k_deadcmd(fvmx_t *f)
{
    f->dead_cmd = 1;
}
static void k_down(fvmx_t *f)
{
    f->link_down = 1;
}
static void k_stall(fvmx_t *f)
{
    f->comp_stall = 1;
}
static void k_nobar(fvmx_t *f)
{
    f->dev->cfg[6] = 0; /* BAR1 (slot 2) unassigned */
    f->dev->cfg[7] = 0;
    f->dev->bar_size[2] = 0;
}

static void test_faults(void)
{
    env_t *e;
    e = env_new(k_vrrs);
    CHECK(e->v == HW_F);
    CHECK(strstr(e->note.b, "unsupported hardware version"));
    env_free(e);
    e = env_new(k_uvrs);
    CHECK(e->v == HW_F);
    CHECK(strstr(e->note.b, "incompatible UPT version"));
    env_free(e);
    e = env_new(k_nomac);
    CHECK(e->v == HW_F);
    CHECK(strstr(e->note.b, "no MAC address"));
    env_free(e);
    e = env_new(k_group);
    CHECK(e->v == HW_B);
    CHECK(strstr(e->note.b, "group bit"));
    env_free(e);
    e = env_new(k_enable);
    CHECK(e->v == HW_F);
    CHECK(strstr(e->note.b, "refused to enable"));
    env_free(e);
    e = env_new(k_deadcmd);
    CHECK(e->v == HW_F);
    CHECK(strstr(e->note.b, "never answered"));
    env_free(e);
    e = env_new(k_nobar);
    CHECK(e->v == HW_N);
    CHECK(strstr(e->note.b, "not assigned"));
    env_free(e);
    /* link down: binds, reports down, sends still queue */
    e = env_new(k_down);
    CHECK(e->v == HW_T);
    hw_nic_t *n = e->r.nic[0];
    CHECK(n->link_up == 0);
    CHECK(strstr(e->note.b, "link down"));
    uint8_t tx[64];
    peer_mk(tx, peer_mac, n->mac, 64, 1);
    CHECK(n->send(n, tx, 64) == 0); /* the fake completes it anyway */
    env_free(e);
    /* completions that never come with the link up: the transmitter is stuck */
    e = env_new(k_stall);
    CHECK(e->v == HW_T);
    n = e->r.nic[0];
    peer_mk(tx, peer_mac, n->mac, 64, 2);
    CHECK(n->send(n, tx, 64) == -1);
    env_free(e);
    /* out of DMA memory partway through the allocations */
    {
        fake_plat_t fp;
        hw_registry_t r = {0};
        hw_line_t note;
        fvmx_t *f2;
        fake_plat_init(&fp);
        fp.dma_fail_after = 3; /* driver struct, ds, qs succeed; txd fails */
        f2 = add_vmx(&fp, HW_BDF(0, 5, 0));
        hw_pci_dev_t dv[4];
        CHECK(hw_pci_scan(&fp.plat, dv, 4) == 1);
        hw_line_init(&note);
        CHECK(vmx3_bind(&fp.plat, &dv[0], &r, &note) == HW_F);
        CHECK(strstr(note.b, "out of DMA memory"));
        fake_plat_free(&fp);
        free(f2); /* add_vmx's struct is not in the fake's allocator */
    }
}

static void test_routing(void)
{
    /* vendor 15AD:07B0, class 02/00 routes to vmx3 */
    hw_pci_dev_t d;
    memset(&d, 0, sizeof d);
    d.vendor = 0x15AD;
    d.device = 0x07B0;
    d.class_code = 2;
    const hw_driver_t *m = hw_match(hw_drivers, hw_n_drivers, &d);
    CHECK(m && m->bind == vmx3_bind);
    CHECK(vmx3_ids[0].vendor == 0x15AD && vmx3_ids[0].device == 0x07B0);
    /* an e1000 id does not */
    d.vendor = 0x8086;
    d.device = 0x100E;
    m = hw_match(hw_drivers, hw_n_drivers, &d);
    CHECK(m && m->bind != vmx3_bind);
}

static void test_phy_off(void)
{
    env_t *e = env_new(0);
    hw_nic_t *n = e->r.nic[0];
    CHECK(n->phy_off(n) == 1); /* MAC stopped, no PHY to power down */
    CHECK(n->link_up == 0);
    CHECK(e->f->disables == 1);
    env_free(e);
}

int main(void)
{
    test_bind();
    test_echo_and_wrap();
    test_rx_full_and_errors();
    test_selftest_nic();
    test_faults();
    test_routing();
    test_phy_off();
    printf("test_vmx3: %d checks, %d failures\n", fk_checks, fk_failures);
    return fk_failures ? 1 : 0;
}
