/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_e1000.c — e1000.c against a register-level fake PRO/1000.
 *
 * The fake models what the driver relies on: CTRL.RST self-clearing and
 * restoring defaults (with deliberately dirty CTRL/MTA/IMS so the driver has
 * to clean them), the link coming up only with SLU set and LRST/ILOS/PHY_RST
 * clear, EERD in both bit layouts, legacy receive and transmit rings walked
 * from RDH/TDH to the tails in tick(), the receive address filter (RAL0/RAH0
 * with AV, BAM for broadcast), SECRC, PSP padding, IFCS and RS. A peer
 * (nicpeer.h) on the other end of the cable answers DHCP and echoes test
 * frames. Fault knobs break each piece the driver must survive. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hwfake.h"
#include "nicpeer.h"

/* register offsets the fake uses (same values as the driver's) */
enum {
    CTRL = 0x0,
    STATUS = 0x8,
    EECD = 0x10,
    EERD = 0x14,
    MDIC = 0x20,
    CTRL_EXT = 0x18,
    ICR = 0xC0,
    IMS = 0xD0,
    IMC = 0xD8,
    RCTL = 0x100,
    TCTL = 0x400,
    TIPG = 0x410,
    RDBAL = 0x2800,
    RDBAH = 0x2804,
    RDLEN = 0x2808,
    RDH = 0x2810,
    RDT = 0x2818,
    TDBAL = 0x3800,
    TDBAH = 0x3804,
    TDLEN = 0x3808,
    TDH = 0x3810,
    TDT = 0x3818,
    TXDCTL = 0x3828,
    TARC0 = 0x3840,
    RFCTL = 0x5008,
    MTA = 0x5200,
    RAL0 = 0x5400,
    RAH0 = 0x5404,
    GCR = 0x5B00,
    GCR2 = 0x5B64,
};
enum { K_8254X, K_82574, K_PCH };

typedef struct fe1k {
    uint8_t *r;
    int kind;
    uint8_t mac[6];
    uint16_t eeprom[64];
    peer_t peer;
    /* fault knobs */
    int reset_stuck, zero_mac, eeprom_dead, unplugged, tx_dd_never, ones, rx_err_next, split_next,
        func1, mdic_dead;
    uint16_t phy_ctrl; /* PHY register 0, written through MDIC */
    uint64_t link_at; /* link comes up at this time (after SLU) */
    /* observations */
    uint32_t resets, tx_frames, rx_frames, rx_filtered, rx_full, violations, eerd_reads;
    uint32_t tx_cmd_or, tx_cmd_and;
    uint32_t last_tx_len;
} fe1k_t;

static uint32_t rd(fe1k_t *f, uint32_t o)
{
    return fk_rd32(f->r + o);
}
static void wr(fe1k_t *f, uint32_t o, uint32_t v)
{
    fk_wr32(f->r + o, v);
}

static void do_reset(fe1k_t *f)
{
    f->resets++;
    memset(f->r, 0, 0x6000);
    /* dirty defaults: the driver must clear each of these */
    wr(f, CTRL, (1u << 3) | (1u << 7) | (1u << 11) | (1u << 31));
    wr(f, IMS, 0x9D);
    for (uint32_t i = 0; i < 128; i++) wr(f, MTA + 4 * i, 0xA5A5A5A5u);
    wr(f, STATUS, 1 | (f->func1 ? 4 : 0));
    if (f->kind != K_8254X) wr(f, EECD, 1u << 9);
    if (f->kind == K_PCH) wr(f, STATUS, rd(f, STATUS) | (1u << 9));
    if (!f->zero_mac) {
        wr(f, RAL0,
           (uint32_t) f->mac[0] | ((uint32_t) f->mac[1] << 8) | ((uint32_t) f->mac[2] << 16) |
               ((uint32_t) f->mac[3] << 24));
        wr(f, RAH0, (uint32_t) f->mac[4] | ((uint32_t) f->mac[5] << 8) | (1u << 31));
    }
}

static int link_ok(fe1k_t *f, uint64_t now)
{
    uint32_t c = rd(f, CTRL);
    return !f->unplugged && !(f->phy_ctrl & 0x0800) && (c & (1u << 6)) && !(c & ((1u << 3) | (1u << 7) | (1u << 31))) &&
           now >= f->link_at;
}

static int rx_accept(fe1k_t *f, const uint8_t *fr)
{
    static const uint8_t bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (!memcmp(fr, bc, 6)) return (rd(f, RCTL) >> 15) & 1;
    uint32_t ral = rd(f, RAL0), rah = rd(f, RAH0);
    uint8_t m[6] = {(uint8_t) ral,         (uint8_t) (ral >> 8), (uint8_t) (ral >> 16),
                    (uint8_t) (ral >> 24), (uint8_t) rah,        (uint8_t) (rah >> 8)};
    return (rah >> 31) && !memcmp(fr, m, 6);
}

static void do_tx(fe1k_t *f)
{
    uint32_t n = rd(f, TDLEN) / 16;
    if (!(rd(f, TCTL) & 2) || n == 0) return;
    uint32_t tdh = rd(f, TDH), tdt = rd(f, TDT);
    if (tdt >= n || tdh >= n) {
        f->violations++;
        return;
    }
    uint8_t *ring = (uint8_t *) fk_ptr(((uint64_t) rd(f, TDBAH) << 32) | rd(f, TDBAL));
    while (tdh != tdt) {
        if (f->tx_dd_never) return; /* the transmitter has stalled */
        uint8_t *d = ring + tdh * 16;
        uint32_t len = d[8] | (uint32_t) d[9] << 8, cmd = d[11];
        f->tx_cmd_or |= cmd;
        f->tx_cmd_and &= cmd;
        if (!(cmd & 1)) f->violations++; /* every frame is one descriptor */
        uint8_t fr[PEER_FRAME];
        memset(fr, 0, sizeof fr);
        if (len > 1514)
            f->violations++;
        else
            memcpy(fr, fk_ptr(fk_rd64(d)), len);
        if (len < 60 && (rd(f, TCTL) & 8)) len = 60; /* PSP pads */
        f->last_tx_len = len;
        f->tx_frames++;
        if (cmd & 2) peer_on_tx(&f->peer, fr, len); /* no IFCS: bad CRC on the wire */
        if (cmd & 8) d[12] |= 1;                    /* RS: report DD */
        tdh = (tdh + 1) % n;
        wr(f, TDH, tdh);
    }
}

static void put_rx(fe1k_t *f, uint8_t *ring, uint32_t n, const uint8_t *src, uint32_t len,
                   uint8_t status, uint8_t err)
{
    uint32_t h = rd(f, RDH);
    uint8_t *d = ring + h * 16;
    if (d[12] & 1) f->violations++; /* a descriptor still holding a frame */
    uint32_t bs = 2048u >> ((rd(f, RCTL) >> 16) & 3);
    if (len > bs) len = bs;
    memcpy(fk_ptr(fk_rd64(d)), src, len);
    d[8] = (uint8_t) len;
    d[9] = (uint8_t) (len >> 8);
    d[12] = status;
    d[13] = err;
    wr(f, RDH, (h + 1) % n);
}

static void do_rx(fe1k_t *f)
{
    uint32_t n = rd(f, RDLEN) / 16;
    if (!(rd(f, RCTL) & 2) || n == 0) return;
    uint8_t *ring = (uint8_t *) fk_ptr(((uint64_t) rd(f, RDBAH) << 32) | rd(f, RDBAL));
    peer_frame_t *pf;
    while ((pf = peer_front(&f->peer)) != 0) {
        uint32_t h = rd(f, RDH), t = rd(f, RDT);
        if (t >= n) {
            f->violations++;
            return;
        }
        uint32_t free_slots = (t + n - h) % n;
        uint32_t need = f->split_next ? 2 : 1;
        if (free_slots < need) {
            f->rx_full++;
            return; /* frames wait in the FIFO */
        }
        if (!rx_accept(f, pf->b)) {
            f->rx_filtered++;
            peer_pop(&f->peer);
            continue;
        }
        uint8_t buf[PEER_FRAME + 4];
        uint32_t len = pf->len;
        memcpy(buf, pf->b, len);
        if (!(rd(f, RCTL) & (1u << 26))) { /* no SECRC: the CRC stays on */
            memset(buf + len, 0xCC, 4);
            len += 4;
        }
        uint8_t err = pf->err;
        if (f->rx_err_next) {
            err = 0x80; /* RXE */
            f->rx_err_next = 0;
        }
        if (f->split_next) {
            f->split_next = 0;
            put_rx(f, ring, n, buf, len / 2, 0x01, 0);
            put_rx(f, ring, n, buf + len / 2, len - len / 2, 0x03, 0);
        } else {
            put_rx(f, ring, n, buf, len, 0x03, err);
        }
        f->rx_frames++;
        peer_pop(&f->peer);
    }
}

static void tick(fake_dev_t *d, uint64_t now)
{
    fe1k_t *f = (fe1k_t *) d->state;
    if (f->ones) {
        memset(f->r, 0xFF, 0x6000);
        return;
    }
    if (rd(f, CTRL) & (1u << 26)) {
        if (f->reset_stuck) return;
        do_reset(f);
    }
    uint32_t imc = rd(f, IMC);
    if (imc) {
        wr(f, IMS, rd(f, IMS) & ~imc);
        wr(f, IMC, 0);
    }
    uint32_t eerd = rd(f, EERD);
    uint32_t done = f->kind == K_8254X ? 0x10 : 0x2, shift = f->kind == K_8254X ? 8 : 2;
    if ((eerd & 1) && !(eerd & done) && !f->eeprom_dead) {
        uint32_t a = (eerd >> shift) & 0x3F;
        f->eerd_reads++;
        wr(f, EERD, (eerd & ~1u & 0xFFFF) | done | ((uint32_t) f->eeprom[a] << 16));
    }
    uint32_t mdic = rd(f, MDIC);
    if (mdic && !(mdic & (1u << 28)) && !f->mdic_dead) {
        uint32_t phy = (mdic >> 21) & 31, reg = (mdic >> 16) & 31, op = (mdic >> 26) & 3;
        if (phy != 1 || op != 1)
            wr(f, MDIC, mdic | (1u << 28) | (1u << 30)); /* only PHY 1 writes answer */
        else {
            if (reg == 0) f->phy_ctrl = (uint16_t) mdic;
            wr(f, MDIC, mdic | (1u << 28));
        }
    }
    uint32_t st = rd(f, STATUS);
    wr(f, STATUS, link_ok(f, now) ? st | 2 : st & ~2u);
    if (!link_ok(f, now)) return; /* nothing moves on a dead wire */
    do_tx(f);
    do_rx(f);
}

static const uint8_t mac_a[6] = {0x52, 0x54, 0x00, 0x5A, 0x58, 0x01};

static fe1k_t *add_e1k(fake_plat_t *fp, uint32_t bdf, uint16_t did, int kind)
{
    fake_dev_t *d = fake_dev_new(fp, bdf, 0x8086, did, 0x02, 0x00, 0x00);
    fe1k_t *f = (fe1k_t *) calloc(1, sizeof *f);
    f->r = fake_dev_bar(d, 0, 0x20000);
    f->kind = kind;
    memcpy(f->mac, mac_a, 6);
    f->mac[5] = (uint8_t) (bdf >> 3);
    f->eeprom[0] = 0x1100;
    f->eeprom[1] = 0x3322;
    f->eeprom[2] = 0x5544;
    f->tx_cmd_and = 0xFF;
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
    return e1000_bind(&fp->plat, &g_dev[0], r, note);
}

typedef struct env {
    fake_plat_t fp;
    hw_registry_t r;
    hw_line_t note;
    fe1k_t *f;
    hw_verdict_t v;
} env_t;

static env_t *env_new(uint16_t did, int kind, void (*breakit)(fe1k_t *))
{
    env_t *e = (env_t *) calloc(1, sizeof *e);
    fake_plat_init(&e->fp);
    e->f = add_e1k(&e->fp, HW_BDF(0, 5, 0), did, kind);
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
typedef struct model {
    uint16_t did;
    int kind;
    const char *name;
} model_t;

static void test_bind_models(void)
{
    static const model_t ms[] = {
        {0x100E, K_8254X, "82540EM"}, {0x100F, K_8254X, "82545EM"}, {0x10D3, K_82574, "82574L"},
        {0x1502, K_PCH, "82579LM"},   {0x15BC, K_PCH, "I219-V"},    {0x153A, K_PCH, "I217-LM"},
    };
    for (uint32_t i = 0; i < sizeof ms / sizeof ms[0]; i++) {
        env_t *e = env_new(ms[i].did, ms[i].kind, 0);
        fe1k_t *f = e->f;
        char want[96];
        snprintf(want, sizeof want, "%s 52:54:00:5a:58:05 link up as eth0", ms[i].name);
        if (strcmp(e->note.b, want)) printf("  note '%s' want '%s'\n", e->note.b, want);
        CHECK(e->v == HW_T);
        CHECK(strcmp(e->note.b, want) == 0);
        CHECK(e->r.n_nic == 1);
        hw_nic_t *n = e->r.nic[0];
        CHECK(strcmp(n->name, "eth0") == 0);
        CHECK(memcmp(n->mac, f->mac, 6) == 0);
        CHECK(n->link_up == 1);
        CHECK(f->resets == 1);
        CHECK((e->fp.devs[0]->cfg[1] & 0x0407) == 0x0406);
        /* register state the driver left */
        uint32_t ctrl = rd(f, CTRL);
        CHECK((ctrl & (1u << 6)) && (ctrl & (1u << 5)));
        CHECK((ctrl & ((1u << 3) | (1u << 7) | (1u << 11) | (1u << 12) | (1u << 26) | (1u << 30) |
                       (1u << 31))) == 0);
        CHECK(rd(f, IMS) == 0);
        CHECK(rd(f, RCTL) == ((1u << 1) | (1u << 15) | (1u << 26)));
        uint32_t tctl = rd(f, TCTL);
        CHECK((tctl & ~(1u << 24)) == ((1u << 1) | (1u << 3) | (0xFu << 4) | (0x40u << 12)));
        CHECK(((tctl >> 24) & 1) == (ms[i].kind != K_8254X));
        CHECK(rd(f, TIPG) ==
              (ms[i].kind == K_8254X ? (10u | 8u << 10 | 6u << 20) : (8u | 8u << 10 | 6u << 20)));
        CHECK(rd(f, RDLEN) == 32 * 16 && rd(f, TDLEN) == 16 * 16);
        CHECK(rd(f, RDH) == 0 && rd(f, RDT) == 31 && rd(f, TDH) == 0 && rd(f, TDT) == 0);
        CHECK((rd(f, RDBAL) & 127) == 0 && (rd(f, TDBAL) & 127) == 0);
        CHECK(rd(f, RDBAL) != 0 && rd(f, TDBAL) != 0);
        CHECK(rd(f, RAH0) >> 31);
        uint32_t mta = 0;
        for (uint32_t k = 0; k < 128; k++) mta |= rd(f, MTA + 4 * k);
        CHECK(mta == 0);
        uint32_t ext = rd(f, CTRL_EXT);
        CHECK(((ext >> 28) & 1) == (ms[i].kind != K_8254X));
        CHECK(((ext >> 22) & 1) == (ms[i].kind != K_8254X));
        CHECK(((rd(f, TXDCTL) >> 22) & 1) == (ms[i].kind != K_8254X));
        CHECK(((rd(f, TXDCTL) >> 16) & 0x3F) == (ms[i].kind != K_8254X));
        CHECK(((rd(f, TXDCTL) >> 24) & 1) == (ms[i].kind != K_8254X));
        CHECK(((rd(f, GCR) >> 22) & 1) == (ms[i].kind == K_82574));
        CHECK((rd(f, GCR2) & 1) == (ms[i].kind == K_82574));
        CHECK(((rd(f, TARC0) >> 23) & 0x1B) == (ms[i].kind == K_PCH ? 0x1Bu : 0));
        CHECK(((rd(f, RFCTL) >> 6) & 3) == (ms[i].kind == K_PCH ? 3u : 0));
        /* PCH: LAN_INIT_DONE acknowledged */
        CHECK(((rd(f, STATUS) >> 9) & 1) == 0);
        /* each descriptor points at its own 2 KB buffer */
        uint8_t *ring = (uint8_t *) fk_ptr(((uint64_t) rd(f, RDBAH) << 32) | rd(f, RDBAL));
        for (uint32_t k = 1; k < 32; k++)
            CHECK(fk_rd64(ring + k * 16) == fk_rd64(ring) + 2048ULL * k);
        /* settle time before the first reset poll: 5 / 10 / 20 ms */
        CHECK(e->fp.now_us >= (ms[i].kind == K_8254X   ? 5000u
                               : ms[i].kind == K_82574 ? 10000u
                                                       : 20000u));
        env_free(e);
    }
}

static void test_ids(void)
{
    /* the table carries every requested id once, all Intel, none of igb's */
    static const uint16_t want[] = {
        0x100E, 0x100F, 0x10D3, 0x10BD, 0x294C, 0x10F5, 0x10EA, 0x10EB, 0x10EF, 0x10F0, 0x1502,
        0x1503, 0x153A, 0x153B, 0x155A, 0x1559, 0x15A0, 0x15A1, 0x15A2, 0x15A3, 0x156F, 0x1570,
        0x15B7, 0x15B8, 0x15B9, 0x15D7, 0x15D8, 0x15E3, 0x15D6, 0x15BD, 0x15BE, 0x15BB, 0x15BC,
        0x15DF, 0x15E0, 0x15E1, 0x15E2, 0x0D4E, 0x0D4F, 0x0D4C, 0x0D4D, 0x0D53, 0x0D55, 0x15FB,
        0x15FC, 0x15F9, 0x15FA, 0x1A1E, 0x1A1F, 0x1A1C, 0x1A1D, 0x550A, 0x550B, 0x550C, 0x550D};
    uint32_t n = 0;
    for (const hw_id_t *id = e1000_ids; id->vendor; id++) {
        n++;
        CHECK(id->vendor == 0x8086);
        CHECK(id->device != 0x10C9 && id->device != 0x1533 && id->device != 0x1521);
        for (const hw_id_t *j = id + 1; j->vendor; j++) CHECK(j->device != id->device);
    }
    CHECK(n >= sizeof want / sizeof want[0]);
    for (uint32_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        int found = 0;
        for (const hw_id_t *id = e1000_ids; id->vendor; id++) found |= id->device == want[i];
        if (!found) printf("  id %04x missing\n", want[i]);
        CHECK(found);
    }
    /* the probe table routes them to e1000 */
    hw_pci_dev_t d;
    memset(&d, 0, sizeof d);
    d.vendor = 0x8086;
    d.class_code = 2;
    for (uint32_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        d.device = want[i];
        const hw_driver_t *m = hw_match(hw_drivers, hw_n_drivers, &d);
        CHECK(m && m->bind == e1000_bind);
    }
}

/* ---- data path -------------------------------------------------------------- */
static void test_echo_and_wrap(uint16_t did, int kind)
{
    env_t *e = env_new(did, kind, 0);
    fe1k_t *f = e->f;
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
    CHECK(f->tx_cmd_and == 0x0B && f->tx_cmd_or == 0x0B); /* EOP|IFCS|RS, nothing else */
    /* short frame: the NIC pads (PSP); the echo is 60 bytes */
    peer_mk(tx, peer_mac, n->mac, 42, 7);
    CHECK(n->send(n, tx, 42) == 0);
    CHECK(f->last_tx_len == 60);
    wait_us(&e->fp, 10);
    CHECK(n->recv(n, rx, sizeof rx) == 60);
    CHECK(memcmp(rx + 12, tx + 12, 30) == 0);
    /* nothing more */
    CHECK(n->recv(n, rx, sizeof rx) == 0);
    /* refused before the NIC sees them */
    uint32_t before = f->tx_frames;
    CHECK(n->send(n, tx, 0) == -1);
    CHECK(n->send(n, tx, 1515) == -1);
    CHECK(n->send(n, 0, 60) == -1);
    CHECK(n->send(n, tx, 1514) == 0);
    CHECK(f->tx_frames == before + 1);
    wait_us(&e->fp, 10);
    /* a short buffer gets the head of the frame, the frame is consumed */
    CHECK(n->recv(n, rx, 0) == 0);
    CHECK(n->recv(n, 0, 100) == 0);
    memset(rx, 0, sizeof rx);
    CHECK(n->recv(n, rx, 20) == 20);
    CHECK(memcmp(rx, n->mac, 6) == 0 && rx[20] == 0);
    CHECK(n->recv(n, rx, sizeof rx) == 0);
    env_free(e);
}

static void test_rx_full_and_errors(void)
{
    env_t *e = env_new(0x100E, K_8254X, 0);
    fe1k_t *f = e->f;
    hw_nic_t *n = e->r.nic[0];
    uint8_t fr[200], rx[2048];
    /* 50 frames while nobody reads: the ring holds 31 (one slot stays empty),
     * the rest wait in the FIFO, nothing is overwritten */
    for (uint32_t k = 0; k < 50; k++) {
        peer_mk(fr, n->mac, peer_mac, 64 + k, k);
        peer_push(&f->peer, fr, 64 + k, 0);
    }
    wait_us(&e->fp, 10);
    CHECK(f->rx_frames == 31);
    CHECK(f->rx_full > 0);
    CHECK(f->peer.count == 19);
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
    CHECK(f->violations == 0);
    CHECK(f->peer.count == 0);

    /* a frame with an error, a frame split over two buffers, then a good one:
     * recv skips the bad ones and returns the good one */
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
    CHECK(f->rx_frames == 53);
    CHECK(n->recv(n, rx, sizeof rx) == 77);
    CHECK(memcmp(rx, fr, 77) == 0);
    CHECK(n->recv(n, rx, sizeof rx) == 0);
    /* every error bit the driver treats as fatal for the frame */
    static const uint8_t errs[] = {0x01, 0x02, 0x04, 0x10, 0x80};
    for (uint32_t i = 0; i < sizeof errs; i++) {
        peer_push(&f->peer, fr, 77, errs[i]);
        wait_us(&e->fp, 10);
        CHECK(n->recv(n, rx, sizeof rx) == 0);
    }
    /* checksum error bits (offload is off) do not drop the frame */
    peer_push(&f->peer, fr, 77, 0x60);
    wait_us(&e->fp, 10);
    CHECK(n->recv(n, rx, sizeof rx) == 77);

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

/* ---- faults ----------------------------------------------------------------- */
static void k_reset(fe1k_t *f)
{
    f->reset_stuck = 1;
}
static void k_ones(fe1k_t *f)
{
    f->ones = 1;
    memset(f->r, 0xFF, 0x6000);
}
static void k_zero(fe1k_t *f)
{
    f->zero_mac = 1;
}
static void k_zero_dead(fe1k_t *f)
{
    f->zero_mac = 1;
    f->eeprom_dead = 1;
}
static void k_zero_blank(fe1k_t *f)
{
    f->zero_mac = 1;
    memset(f->eeprom, 0, sizeof f->eeprom);
}
static void k_group(fe1k_t *f)
{
    f->mac[0] = 0x01;
}
static void k_unplug(fe1k_t *f)
{
    f->unplugged = 1;
}
static void k_late(fe1k_t *f)
{
    f->link_at = 1500000;
}
static void k_too_late(fe1k_t *f)
{
    f->link_at = 4000000;
}
static void k_dead_eeprom_only(fe1k_t *f)
{
    f->eeprom_dead = 1;
}

typedef struct fault {
    const char *what;
    uint16_t did;
    int kind;
    void (*breakit)(fe1k_t *);
    hw_verdict_t want;
    const char *note;
} fault_t;

static void test_faults(void)
{
    static const fault_t fs[] = {
        {"reset stuck", 0x100E, K_8254X, k_reset, HW_F, "82540EM reset timed out"},
        {"all-ones", 0x10D3, K_82574, k_ones, HW_F, "82574L registers read all-ones"},
        {"zero MAC, EEPROM", 0x100E, K_8254X, k_zero, HW_T,
         "82540EM 00:11:22:33:44:55 link up as eth0"},
        {"zero MAC, EEPROM 82574", 0x10D3, K_82574, k_zero, HW_T,
         "82574L 00:11:22:33:44:55 link up as eth0"},
        {"zero MAC, EEPROM dead", 0x100F, K_8254X, k_zero_dead, HW_F, "EEPROM read timed out"},
        {"zero MAC, EEPROM blank", 0x100F, K_8254X, k_zero_blank, HW_F, "82545EM no MAC address"},
        {"zero MAC on PCH", 0x15B8, K_PCH, k_zero, HW_F, "I219-V no MAC address"},
        {"dead EEPROM unused", 0x100E, K_8254X, k_dead_eeprom_only, HW_T, "link up as eth0"},
        {"group MAC", 0x100E, K_8254X, k_group, HW_B, "01:54:00:5a:58:05 has the group bit set"},
        {"unplugged", 0x10D3, K_82574, k_unplug, HW_T,
         "82574L 52:54:00:5a:58:05 link down as eth0"},
        {"link after 1.5 s", 0x100E, K_8254X, k_late, HW_T, "link up as eth0"},
        {"link after 4 s", 0x100E, K_8254X, k_too_late, HW_T, "link down as eth0"},
    };
    for (uint32_t i = 0; i < sizeof fs / sizeof fs[0]; i++) {
        env_t *e = env_new(fs[i].did, fs[i].kind, fs[i].breakit);
        if (e->v != fs[i].want || !strstr(e->note.b, fs[i].note))
            printf("  fault '%s': verdict %d note '%s'\n", fs[i].what, e->v, e->note.b);
        CHECK(e->v == fs[i].want);
        CHECK(strstr(e->note.b, fs[i].note) != 0);
        CHECK((e->v == HW_T) == (e->r.n_nic == 1));
        /* bounded: nothing waits more than reset + link + EEPROM budgets */
        CHECK(e->fp.now_us < 3000000u);
        if (e->v == HW_T && e->f->zero_mac) {
            /* the EEPROM MAC went into RAL0/RAH0, so unicast to it passes */
            CHECK(rd(e->f, RAL0) == 0x33221100u && rd(e->f, RAH0) == (0x5544u | 1u << 31));
            CHECK(e->f->eerd_reads == 3);
        }
        if (e->v == HW_T && !e->f->zero_mac) CHECK(e->f->eerd_reads == 0);
        env_free(e);
    }
}

static void k_func1(fe1k_t *f)
{
    f->zero_mac = 1;
    f->func1 = 1;
}

static void test_82546_port_b(void)
{
    env_t *e = env_new(0x1010, K_8254X, k_func1);
    CHECK(e->v == HW_T);
    CHECK(strstr(e->note.b, "82546EB 00:11:22:33:44:54 ") != 0);
    env_free(e);
    e = env_new(0x100E, K_8254X, k_func1); /* only the 82546 flips */
    CHECK(strstr(e->note.b, "00:11:22:33:44:55 ") != 0);
    env_free(e);
}

static void test_tx_stall_and_link_down(void)
{
    /* transmitter never reports DD with the link up: -1 after a bounded wait;
     * once 15 slots are stuck (the 16th would make the ring look empty to
     * the NIC), -2 at once */
    env_t *e = env_new(0x100E, K_8254X, 0);
    hw_nic_t *n = e->r.nic[0];
    e->f->tx_dd_never = 1;
    uint8_t tx[64];
    peer_mk(tx, peer_mac, n->mac, 64, 0);
    uint64_t t0 = e->fp.now_us;
    CHECK(n->send(n, tx, 64) == -1);
    CHECK(e->fp.now_us - t0 >= 50000 && e->fp.now_us - t0 < 60000);
    for (int k = 1; k < 15; k++) CHECK(n->send(n, tx, 64) == -1);
    t0 = e->fp.now_us;
    CHECK(n->send(n, tx, 64) == -2);
    CHECK(e->fp.now_us == t0);
    /* the transmitter recovers: the stuck slots complete, sending works */
    e->f->tx_dd_never = 0;
    wait_us(&e->fp, 10);
    CHECK(e->f->tx_frames == 15);
    CHECK(n->send(n, tx, 64) == 0);
    CHECK(e->f->tx_frames == 16);
    env_free(e);

    /* cable unplugged: frames are queued, not refused; the selftest says
     * NONE and the link flag follows the cable */
    e = env_new(0x10D3, K_82574, k_unplug);
    n = e->r.nic[0];
    CHECK(n->link_up == 0);
    CHECK(n->send(n, tx, 64) == 0);
    CHECK(e->f->tx_frames == 0);
    CHECK(hw_selftest_all(&e->fp.plat, &e->r) == 0);
    CHECK(fake_log_has(&e->fp, "[SELFTEST NONE] eth0 no DHCP offer"));
    e->f->unplugged = 0; /* plug it in: the queued frames go out */
    wait_us(&e->fp, 10);
    CHECK(e->f->tx_frames >= 1);
    CHECK(n->send(n, tx, 64) == 0);
    CHECK(n->link_up == 1);
    env_free(e);
}

/* The post-hydration seal: PHY powered down, Bus Master and Memory Space
 * cleared and read back, the wire dead. */
static void test_power_off(void)
{
    env_t *e = env_new(0x100E, K_8254X, 0);
    hw_nic_t *n = e->r.nic[0];
    uint32_t *cmd = &e->fp.devs[0]->cfg[1];
    CHECK(n->on_pci && n->bdf == HW_BDF(0, 5, 0) && (*cmd & 6) == 6);
    CHECK(hw_nic_power_off(&e->fp.plat, n) == (HW_NIC_OFF_PCI | HW_NIC_OFF_PHY));
    CHECK((*cmd & 6) == 0 && (e->f->phy_ctrl & 0x0800) && n->link_up == 0);
    CHECK(rd(e->f, RCTL) == 0 && rd(e->f, TCTL) == 0 && !(rd(e->f, CTRL) & (1u << 6)));
    wait_us(&e->fp, 10);
    CHECK(!(rd(e->f, STATUS) & 2));
    env_free(e);

    /* a PHY that never answers MDIC: the bus is still cut, PHY not claimed */
    e = env_new(0x10D3, K_82574, 0);
    e->f->mdic_dead = 1;
    n = e->r.nic[0];
    CHECK(hw_nic_power_off(&e->fp.plat, n) == HW_NIC_OFF_PCI);
    CHECK((e->fp.devs[0]->cfg[1] & 6) == 0 && e->f->phy_ctrl == 0);
    env_free(e);

    /* PCH: MAC stopped, PHY left alone (no semaphore), bus cut */
    e = env_new(0x15B7, K_PCH, 0);
    n = e->r.nic[0];
    CHECK(hw_nic_power_off(&e->fp.plat, n) == HW_NIC_OFF_PCI);
    CHECK((e->fp.devs[0]->cfg[1] & 6) == 0 && e->f->phy_ctrl == 0 && rd(e->f, RCTL) == 0);
    env_free(e);

    /* a command register that keeps its bits: reported, not claimed */
    e = env_new(0x100E, K_8254X, 0);
    e->fp.devs[0]->cmd_locked = 1;
    CHECK(hw_nic_power_off(&e->fp.plat, e->r.nic[0]) == -1);
    env_free(e);

    /* not on PCI: refused, nothing touched */
    hw_nic_t off = {0};
    CHECK(hw_nic_power_off(0, &off) == -1);
}

static void test_no_bar_unknown_full_dma(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fake_dev_new(&fp, HW_BDF(0, 5, 0), 0x8086, 0x100E, 2, 0, 0);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_one(&fp, &r, &note) == HW_N);
    CHECK(strcmp(note.b, "BAR0 not assigned") == 0);
    fake_plat_free(&fp);

    env_t *e = env_new(0x100E, K_8254X, 0);
    hw_pci_dev_t d = g_dev[0];
    d.device = 0x1234;
    hw_line_init(&e->note);
    CHECK(e1000_bind(&e->fp.plat, &d, &e->r, &e->note) == HW_N);
    CHECK(strcmp(e->note.b, "unknown device id") == 0);
    /* registry full */
    e->r.n_nic = HW_MAX_NIC;
    d.device = 0x100E;
    hw_line_init(&e->note);
    CHECK(e1000_bind(&e->fp.plat, &d, &e->r, &e->note) == HW_N);
    CHECK(strstr(e->note.b, "NIC table full") != 0);
    env_free(e);

    for (uint32_t k = 1; k <= 5; k++) {
        fake_plat_init(&fp);
        fe1k_t *f = add_e1k(&fp, HW_BDF(0, 6, 0), 0x100E, K_8254X);
        fp.dma_fail_after = k;
        memset(&r, 0, sizeof r);
        CHECK(bind_one(&fp, &r, &note) == HW_F);
        CHECK(strstr(note.b, "out of DMA memory") != 0);
        CHECK(r.n_nic == 0);
        fake_plat_free(&fp);
        free(f);
    }
}

/* ---- the boot path: probe + self-test over two NICs -------------------------- */
static void test_probe_and_selftest(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fe1k_t *a = add_e1k(&fp, HW_BDF(0, 2, 0), 0x100E, K_8254X);
    fe1k_t *b = add_e1k(&fp, HW_BDF(0, 3, 0), 0x10D3, K_82574);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_probe_stats_t st = hw_probe_all(&fp.plat, &r);
    CHECK(st.functions == 2 && st.bound == 2 && st.failed == 0 && st.held == 0);
    CHECK(r.n_nic == 2);
    CHECK(fake_log_has(&fp, "[DRIVER ONLINE] e1000 00:02.0 8086:100e 82540EM "
                            "52:54:00:5a:58:02 link up as eth0"));
    CHECK(fake_log_has(&fp, "[DRIVER ONLINE] e1000 00:03.0 8086:10d3 82574L "
                            "52:54:00:5a:58:03 link up as eth1"));
    CHECK(hw_selftest_all(&fp.plat, &r) == 2);
    CHECK(fake_log_has(&fp, "[SELFTEST OK] eth0 DHCP offer received; mac 52:54:00:5a:58:02; "
                            "offered 10.0.2.15 by 10.0.2.2"));
    CHECK(fake_log_has(&fp, "[SELFTEST OK] eth1 DHCP offer received; mac 52:54:00:5a:58:03"));
    CHECK(a->peer.discovers == 1 && b->peer.discovers == 1);
    /* the DISCOVER went out as built: broadcast, from our MAC, 342 bytes */
    CHECK(a->peer.last_len == 342 && a->peer.last[0] == 0xFF && a->peer.last[11] == 0x02);
    CHECK(a->violations == 0 && b->violations == 0);
    fake_plat_free(&fp);
    free(a);
    free(b);

    /* no DHCP server: NONE, not FAILED; three tries */
    fake_plat_init(&fp);
    a = add_e1k(&fp, HW_BDF(0, 2, 0), 0x100F, K_8254X);
    a->peer.no_dhcp = 1;
    memset(&r, 0, sizeof r);
    hw_probe_all(&fp.plat, &r);
    CHECK(hw_selftest_all(&fp.plat, &r) == 0);
    CHECK(a->peer.discovers == 3);
    CHECK(fake_log_has(&fp, "[SELFTEST NONE] eth0"));
    fake_plat_free(&fp);
    free(a);

    /* transmitter stalled: FAILED */
    fake_plat_init(&fp);
    a = add_e1k(&fp, HW_BDF(0, 2, 0), 0x100E, K_8254X);
    memset(&r, 0, sizeof r);
    hw_probe_all(&fp.plat, &r);
    a->tx_dd_never = 1;
    CHECK(hw_selftest_all(&fp.plat, &r) == 0);
    CHECK(fake_log_has(&fp, "[SELFTEST FAILED] eth0 transmit refused"));
    fake_plat_free(&fp);
    free(a);
}

int main(void)
{
    test_ids();
    test_bind_models();
    test_echo_and_wrap(0x100E, K_8254X);
    test_echo_and_wrap(0x10D3, K_82574);
    test_echo_and_wrap(0x15B7, K_PCH);
    test_rx_full_and_errors();
    test_faults();
    test_82546_port_b();
    test_tx_stall_and_link_down();
    test_power_off();
    test_no_bar_unknown_full_dma();
    test_probe_and_selftest();
    printf("test_e1000: %d checks, %d failures\n", fk_checks, fk_failures);
    return fk_failures ? 1 : 0;
}
