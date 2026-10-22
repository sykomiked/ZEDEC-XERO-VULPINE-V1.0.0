/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_igc.c — igc.c against a register-level fake I225 / I226.
 *
 * This fake is the ONLY thing igc.c has been run against: QEMU has no
 * I225/I226 model. It follows the register layout of the mainline Linux igc
 * driver: the legacy interrupt block at 0x1500 (ICR/IMS/IMC/IAM; a driver
 * that writes igb's 0x00C0-0x00E0 leaves interrupts unmasked here), EECD
 * auto-read done after CTRL.RST, CTRL.RST restoring dirty defaults
 * (forced speed, VLAN mode, PHY reset, sixteen dirty receive addresses, a
 * dirty multicast table), the link only after the PHY at MDIC address 0 is
 * told to restart autonegotiation (and 500 ms of negotiation), STATUS
 * reporting 2.5G speed, queue 0 moving only with RXDCTL/TXDCTL.ENABLE and
 * the RCTL/TCTL enables, SRRCTL deciding buffer size and descriptor type,
 * advanced receive write-back overwriting the read format (so a driver that
 * does not rewrite the buffer address loses it), advanced transmit data
 * descriptors with DTYP/DEXT/PAYLEN checked, and the receive address
 * filter. nicpeer.h answers DHCP and echoes test frames. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hwfake.h"
#include "nicpeer.h"

enum {
    CTRL = 0x0,
    STATUS = 0x8,
    EECD = 0x10,
    CTRL_EXT = 0x18,
    MDIC = 0x20,
    RCTL = 0x100,
    TCTL = 0x400,
    ICR = 0x1500,
    IMS = 0x1508,
    IMC = 0x150C,
    IAM = 0x1510,
    EIMS = 0x1524,
    EIMC = 0x1528,
    EIAC = 0x152C,
    EIAM = 0x1530,
    MTA = 0x5200,
    RAL0 = 0x5400,
    RAH0 = 0x5404,
    RDBAL = 0xC000,
    RDBAH = 0xC004,
    RDLEN = 0xC008,
    SRRCTL = 0xC00C,
    RDH = 0xC010,
    RDT = 0xC018,
    RXDCTL = 0xC028,
    TDBAL = 0xE000,
    TDBAH = 0xE004,
    TDLEN = 0xE008,
    TDH = 0xE010,
    TDT = 0xE018,
    TXDCTL = 0xE028,
    /* igb's interrupt registers: unused on igc, written means a wrong layout */
    IGB_ICR = 0xC0,
    IGB_IMC = 0xD8,
    IGB_IAM = 0xE0,
};
#define REGS     0x20000
#define S_SPD100 (1u << 6)
#define S_SPD1G  (1u << 7)
#define S_SPD2G5 (1u << 22)

typedef struct figc {
    uint8_t *r;
    uint8_t mac[6];
    peer_t peer;
    int reset_stuck, zero_mac, unplugged, tx_dd_never, ones, rx_err_next, split_next, rxq_stuck,
        txq_stuck, mdic_dead, master_stuck, autord_never;
    uint32_t speed; /* STATUS speed bits while the link is up */
    uint64_t an_at; /* 0 = autonegotiation never restarted since reset */
    uint32_t resets, tx_frames, rx_frames, rx_filtered, rx_full, violations, an_restarts;
    uint32_t tx_cmd_or, tx_cmd_and, last_tx_len;
} figc_t;

static uint32_t rd(figc_t *f, uint32_t o)
{
    return fk_rd32(f->r + o);
}
static void wr(figc_t *f, uint32_t o, uint32_t v)
{
    fk_wr32(f->r + o, v);
}

static void do_reset(figc_t *f)
{
    f->resets++;
    memset(f->r, 0, REGS);
    /* dirty defaults the driver has to clear: FRCSPD, FRCDPX, VME, PHY_RST */
    wr(f, CTRL, (1u << 11) | (1u << 12) | (1u << 30) | (1u << 31));
    wr(f, IMS, 0x9D);
    wr(f, EIMS, 0x1F);
    wr(f, EIAC, 0x1F);
    wr(f, EIAM, 0x1F);
    wr(f, IAM, 0x9D);
    wr(f, ICR, 0x04);
    for (uint32_t i = 0; i < 128; i++) wr(f, MTA + 4 * i, 0xA5A5A5A5u);
    for (uint32_t i = 1; i < 16; i++) {
        wr(f, RAL0 + 8 * i, 0x12345678u);
        wr(f, RAH0 + 8 * i, 0x80009ABCu);
    }
    wr(f, STATUS, 1);
    wr(f, EECD, f->autord_never ? 0 : 1u << 9);
    wr(f, MDIC, 1u << 28);
    f->an_at = 0;
    if (!f->zero_mac) {
        wr(f, RAL0,
           (uint32_t) f->mac[0] | ((uint32_t) f->mac[1] << 8) | ((uint32_t) f->mac[2] << 16) |
               ((uint32_t) f->mac[3] << 24));
        wr(f, RAH0, (uint32_t) f->mac[4] | ((uint32_t) f->mac[5] << 8) | (1u << 31));
    }
}

static int link_ok(figc_t *f, uint64_t now)
{
    uint32_t c = rd(f, CTRL);
    return !f->unplugged && (c & (1u << 6)) && !(c & ((1u << 11) | (1u << 12) | (1u << 31))) &&
           f->an_at && now >= f->an_at;
}

static int rx_accept(figc_t *f, const uint8_t *fr)
{
    static const uint8_t bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (!memcmp(fr, bc, 6)) return (rd(f, RCTL) >> 15) & 1;
    for (uint32_t k = 0; k < 16; k++) {
        uint32_t ral = rd(f, RAL0 + 8 * k), rah = rd(f, RAH0 + 8 * k);
        uint8_t m[6] = {(uint8_t) ral,         (uint8_t) (ral >> 8), (uint8_t) (ral >> 16),
                        (uint8_t) (ral >> 24), (uint8_t) rah,        (uint8_t) (rah >> 8)};
        if ((rah >> 31) && !memcmp(fr, m, 6)) return 1;
    }
    return 0;
}

static void do_tx(figc_t *f)
{
    uint32_t n = rd(f, TDLEN) / 16;
    if (!(rd(f, TCTL) & 2) || !(rd(f, TXDCTL) & (1u << 25)) || n == 0) return;
    uint32_t tdh = rd(f, TDH), tdt = rd(f, TDT);
    if (tdt >= n || tdh >= n) {
        f->violations++;
        return;
    }
    uint8_t *ring = (uint8_t *) fk_ptr(((uint64_t) rd(f, TDBAH) << 32) | rd(f, TDBAL));
    while (tdh != tdt) {
        if (f->tx_dd_never) return;
        uint8_t *d = ring + tdh * 16;
        uint32_t ctl = fk_rd32(d + 8), ol = fk_rd32(d + 12);
        uint32_t len = ctl & 0xFFFF, cmd = ctl >> 24;
        f->tx_cmd_or |= cmd;
        f->tx_cmd_and &= cmd;
        if (((ctl >> 20) & 0xF) != 3 || !(cmd & 0x20) || !(cmd & 1)) f->violations++;
        if ((ol >> 14) != len) f->violations++; /* PAYLEN */
        if (ol & 1) f->violations++;            /* handed over with DD still set */
        uint8_t fr[PEER_FRAME];
        memset(fr, 0, sizeof fr);
        if (len > 1514)
            f->violations++;
        else
            memcpy(fr, fk_ptr(fk_rd64(d)), len);
        if (len < 60 && (rd(f, TCTL) & 8)) len = 60;
        f->last_tx_len = len;
        f->tx_frames++;
        if (cmd & 2) peer_on_tx(&f->peer, fr, len);
        if (cmd & 8) fk_wr32(d + 12, ol | 1); /* DD */
        tdh = (tdh + 1) % n;
        wr(f, TDH, tdh);
    }
}

static void put_rx(figc_t *f, uint8_t *ring, uint32_t n, const uint8_t *src, uint32_t len,
                   uint32_t staterr)
{
    uint32_t h = rd(f, RDH);
    uint8_t *d = ring + h * 16;
    /* rearmed in full: header address 0 (one-buffer mode), so no stale
     * write-back (DD, length) is left in it */
    if (fk_rd64(d + 8) != 0) f->violations++;
    uint64_t a = fk_rd64(d);
    if (!a) {
        f->violations++; /* the buffer address was not rewritten */
        return;
    }
    uint32_t bs = (rd(f, SRRCTL) & 0x7F) * 1024;
    if (len > bs) len = bs;
    memcpy(fk_ptr(a), src, len);
    fk_wr64(d, 0); /* write-back format overwrites the address */
    fk_wr32(d + 8, staterr);
    fk_wr32(d + 12, len);
    wr(f, RDH, (h + 1) % n);
}

static void do_rx(figc_t *f)
{
    uint32_t n = rd(f, RDLEN) / 16;
    if (!(rd(f, RCTL) & 2) || !(rd(f, RXDCTL) & (1u << 25)) || n == 0) return;
    if (((rd(f, SRRCTL) >> 25) & 7) != 1) {
        f->violations++; /* only advanced one-buffer is modelled */
        return;
    }
    uint8_t *ring = (uint8_t *) fk_ptr(((uint64_t) rd(f, RDBAH) << 32) | rd(f, RDBAL));
    peer_frame_t *pf;
    while ((pf = peer_front(&f->peer)) != 0) {
        uint32_t h = rd(f, RDH), t = rd(f, RDT);
        if (t >= n) {
            f->violations++;
            return;
        }
        uint32_t need = f->split_next ? 2 : 1;
        if ((t + n - h) % n < need) {
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
        if (!(rd(f, RCTL) & (1u << 26))) {
            memset(buf + len, 0xCC, 4);
            len += 4;
        }
        uint32_t err = (uint32_t) pf->err << 24;
        if (f->rx_err_next) {
            err = 0x80000000u;
            f->rx_err_next = 0;
        }
        if (f->split_next) {
            f->split_next = 0;
            put_rx(f, ring, n, buf, len / 2, 1);
            put_rx(f, ring, n, buf + len / 2, len - len / 2, 3);
        } else {
            put_rx(f, ring, n, buf, len, 3 | err);
        }
        f->rx_frames++;
        peer_pop(&f->peer);
    }
}

static void tick(fake_dev_t *d, uint64_t now)
{
    figc_t *f = (figc_t *) d->state;
    if (f->ones) {
        memset(f->r, 0xFF, REGS);
        return;
    }
    if (rd(f, CTRL) & (1u << 26)) {
        if (f->reset_stuck) return;
        do_reset(f);
    }
    if (rd(f, IGB_ICR) | rd(f, IGB_IMC) | rd(f, IGB_IAM)) f->violations++;
    uint32_t imc = rd(f, IMC), eimc = rd(f, EIMC);
    if (imc) wr(f, IMS, rd(f, IMS) & ~imc), wr(f, IMC, 0);
    if (eimc) wr(f, EIMS, rd(f, EIMS) & ~eimc), wr(f, EIMC, 0);
    /* bus master status follows GIO_MASTER_DISABLE */
    uint32_t st = rd(f, STATUS) & ~(1u << 19);
    if (!(rd(f, CTRL) & 4) || f->master_stuck) st |= 1u << 19;
    wr(f, STATUS, st);
    uint32_t mdic = rd(f, MDIC);
    if (!(mdic & (1u << 28)) && !f->mdic_dead) {
        uint32_t phy = (mdic >> 21) & 0x1F, reg = (mdic >> 16) & 0x1F, op = (mdic >> 26) & 3;
        if (phy != 0)
            mdic |= 1u << 30;
        else if (op == 1 && reg == 0 && (mdic & 0x1200) == 0x1200) {
            f->an_at = now + 500000;
            f->an_restarts++;
        }
        wr(f, MDIC, mdic | (1u << 28));
    }
    if (f->rxq_stuck) wr(f, RXDCTL, rd(f, RXDCTL) & ~(1u << 25));
    if (f->txq_stuck) wr(f, TXDCTL, rd(f, TXDCTL) & ~(1u << 25));
    st = rd(f, STATUS) & ~(2u | S_SPD100 | S_SPD1G | S_SPD2G5);
    wr(f, STATUS, link_ok(f, now) ? st | 2 | f->speed : st);
    if (!link_ok(f, now)) return;
    do_tx(f);
    do_rx(f);
}

static const uint8_t mac_a[6] = {0xA8, 0xA1, 0x59, 0x22, 0x50, 0x01};

static figc_t *add_igc(fake_plat_t *fp, uint32_t bdf, uint16_t did)
{
    fake_dev_t *d = fake_dev_new(fp, bdf, 0x8086, did, 0x02, 0x00, 0x00);
    figc_t *f = (figc_t *) calloc(1, sizeof *f);
    f->r = fake_dev_bar(d, 0, REGS);
    fake_dev_bar(d, 3, 0x4000); /* MSI-X table BAR, as on the real part */
    memcpy(f->mac, mac_a, 6);
    f->mac[5] = (uint8_t) (bdf >> 3);
    f->speed = S_SPD2G5;
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
    return igc_bind(&fp->plat, &g_dev[0], r, note);
}

typedef struct env {
    fake_plat_t fp;
    hw_registry_t r;
    hw_line_t note;
    figc_t *f;
    hw_verdict_t v;
} env_t;

static env_t *env_new(uint16_t did, void (*breakit)(figc_t *))
{
    env_t *e = (env_t *) calloc(1, sizeof *e);
    fake_plat_init(&e->fp);
    e->f = add_igc(&e->fp, HW_BDF(0, 7, 0), did);
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

/* Every id of Linux v7.2 igc_hw.h IGC_DEV_ID_* (all in igc_pci_tbl). */
static const uint16_t want_ids[] = {0x15F2, 0x15F3, 0x15F8, 0x15F7, 0x3100, 0x3101, 0x3102, 0x5502,
                                    0x5503, 0x0D9F, 0x125B, 0x125C, 0x125D, 0x125E, 0x125F, 0x15FD};

static void test_ids(void)
{
    uint32_t n = 0;
    for (const hw_id_t *id = igc_ids; id->vendor; id++) {
        n++;
        CHECK(id->vendor == 0x8086);
        for (const hw_id_t *j = id + 1; j->vendor; j++) CHECK(j->device != id->device);
        for (const hw_id_t *j = e1000_ids; j->vendor; j++) CHECK(j->device != id->device);
        for (const hw_id_t *j = igb_ids; j->vendor; j++) CHECK(j->device != id->device);
        int listed = 0;
        for (uint32_t i = 0; i < sizeof want_ids / sizeof want_ids[0]; i++)
            listed |= want_ids[i] == id->device;
        CHECK(listed);
    }
    CHECK(n == sizeof want_ids / sizeof want_ids[0]);
    hw_pci_dev_t d;
    memset(&d, 0, sizeof d);
    d.vendor = 0x8086;
    d.class_code = 2;
    for (uint32_t i = 0; i < sizeof want_ids / sizeof want_ids[0]; i++) {
        d.device = want_ids[i];
        const hw_driver_t *m = hw_match(hw_drivers, hw_n_drivers, &d);
        CHECK(m && m->bind == igc_bind && strcmp(m->name, "igc") == 0);
    }
    /* the same device ids from another vendor are not igc's */
    d.vendor = 0x10EC;
    d.device = 0x15F3;
    const hw_driver_t *m = hw_match(hw_drivers, hw_n_drivers, &d);
    CHECK(!m || m->bind != igc_bind);
}

typedef struct model {
    uint16_t did;
    const char *name;
} model_t;

static void test_bind_models(void)
{
    static const model_t ms[] = {
        {0x15F2, "I225-LM"}, {0x15F3, "I225-V"},  {0x0D9F, "I225-IT"}, {0x125B, "I226-LM"},
        {0x125C, "I226-V"},  {0x125D, "I226-IT"}, {0x3102, "I226-K"},  {0x125E, "I221-V"},
    };
    for (uint32_t i = 0; i < sizeof ms / sizeof ms[0]; i++) {
        env_t *e = env_new(ms[i].did, 0);
        figc_t *f = e->f;
        char want[96];
        snprintf(want, sizeof want, "%s a8:a1:59:22:50:07 link up as eth0, 2500 Mb/s", ms[i].name);
        if (strcmp(e->note.b, want)) printf("  note '%s' want '%s'\n", e->note.b, want);
        CHECK(e->v == HW_T);
        CHECK(strcmp(e->note.b, want) == 0);
        CHECK(e->r.n_nic == 1);
        hw_nic_t *n = e->r.nic[0];
        CHECK(strcmp(n->name, "eth0") == 0 && n->link_up == 1);
        CHECK(memcmp(n->mac, f->mac, 6) == 0);
        CHECK(f->resets == 1 && f->an_restarts == 1);
        CHECK((e->fp.devs[0]->cfg[1] & 0x0407) == 0x0406);
        uint32_t ctrl = rd(f, CTRL);
        CHECK(ctrl & (1u << 6));
        CHECK((ctrl &
               ((1u << 2) | (1u << 11) | (1u << 12) | (1u << 26) | (1u << 30) | (1u << 31))) == 0);
        CHECK(rd(f, IMS) == 0 && rd(f, EIMS) == 0);
        CHECK(rd(f, EIAC) == 0 && rd(f, EIAM) == 0 && rd(f, IAM) == 0);
        CHECK(rd(f, RCTL) == ((1u << 1) | (1u << 15) | (1u << 26)));
        CHECK(rd(f, TCTL) == ((1u << 1) | (1u << 3) | (0xFu << 4) | (0x3Fu << 12) | (1u << 24)));
        CHECK(rd(f, SRRCTL) == (2u | (4u << 8) | (1u << 25)));
        CHECK(rd(f, RXDCTL) == (8u | (8u << 8) | (1u << 16) | (1u << 25)));
        CHECK(rd(f, TXDCTL) == (8u | (1u << 8) | (1u << 25)));
        CHECK(rd(f, RDLEN) == 32 * 16 && rd(f, TDLEN) == 16 * 16);
        CHECK(rd(f, RDH) == 0 && rd(f, RDT) == 31 && rd(f, TDH) == 0 && rd(f, TDT) == 0);
        CHECK((rd(f, RDBAL) & 127) == 0 && (rd(f, TDBAL) & 127) == 0);
        CHECK(rd(f, RAH0) >> 31);
        CHECK((rd(f, CTRL_EXT) >> 28) & 1);
        uint32_t mta = 0, rar = 0;
        for (uint32_t k = 0; k < 128; k++) mta |= rd(f, MTA + 4 * k);
        for (uint32_t k = 1; k < 16; k++) rar |= rd(f, RAL0 + 8 * k) | rd(f, RAH0 + 8 * k);
        CHECK(mta == 0 && rar == 0);
        uint8_t *ring = (uint8_t *) fk_ptr(((uint64_t) rd(f, RDBAH) << 32) | rd(f, RDBAL));
        for (uint32_t k = 1; k < 32; k++) {
            CHECK(fk_rd64(ring + k * 16) == fk_rd64(ring) + 2048ULL * k);
            CHECK(fk_rd64(ring + k * 16 + 8) == 0);
        }
        /* 10 ms quiesce + 5 ms reset settle + 500 ms autonegotiation */
        CHECK(e->fp.now_us >= 515000 && e->fp.now_us < 600000);
        CHECK(f->violations == 0);
        env_free(e);
    }
}

static void test_speeds(void)
{
    static const struct {
        uint32_t bits;
        const char *say;
    } sp[] = {{S_SPD1G, ", 1000 Mb/s"}, {S_SPD100, ", 100 Mb/s"}, {0, ", 10 Mb/s"}};
    for (uint32_t i = 0; i < sizeof sp / sizeof sp[0]; i++) {
        env_t *e = env_new(0x125C, 0);
        CHECK(e->v == HW_T);
        e->f->speed = sp[i].bits;
        hw_registry_t r2;
        memset(&r2, 0, sizeof r2);
        hw_line_t note;
        hw_line_init(&note);
        CHECK(igc_bind(&e->fp.plat, &g_dev[0], &r2, &note) == HW_T);
        const char *tail = strstr(note.b, "link up as eth0");
        CHECK(tail && strcmp(tail + strlen("link up as eth0"), sp[i].say) == 0);
        env_free(e);
    }
}

static void test_echo_and_wrap(void)
{
    env_t *e = env_new(0x125C, 0);
    figc_t *f = e->f;
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
    CHECK(f->tx_frames == 100 && f->rx_frames == 100);
    CHECK(f->peer.echoes == 100 && f->peer.last_len == 60 + (99 * 97) % 1455);
    CHECK(f->violations == 0);
    CHECK(f->tx_cmd_and == 0x2B && f->tx_cmd_or == 0x2B); /* EOP|IFCS|RS|DEXT */
    peer_mk(tx, peer_mac, n->mac, 42, 7);
    CHECK(n->send(n, tx, 42) == 0);
    CHECK(f->last_tx_len == 60);
    wait_us(&e->fp, 10);
    CHECK(n->recv(n, rx, sizeof rx) == 60);
    CHECK(n->recv(n, rx, sizeof rx) == 0);
    uint32_t before = f->tx_frames;
    CHECK(n->send(n, tx, 0) == -1);
    CHECK(n->send(n, tx, 1515) == -1);
    CHECK(n->send(n, 0, 60) == -1);
    CHECK(f->tx_frames == before);
    CHECK(n->send(n, tx, 1514) == 0);
    wait_us(&e->fp, 10);
    CHECK(n->recv(n, rx, 0) == 0);
    CHECK(n->recv(n, 0, 64) == 0);
    memset(rx, 0, sizeof rx);
    CHECK(n->recv(n, rx, 20) == 20);
    CHECK(memcmp(rx, n->mac, 6) == 0 && rx[20] == 0);
    CHECK(n->recv(n, rx, sizeof rx) == 0);
    CHECK(f->violations == 0);
    env_free(e);
}

static void test_rx_full_and_errors(void)
{
    env_t *e = env_new(0x15F3, 0);
    figc_t *f = e->f;
    hw_nic_t *n = e->r.nic[0];
    uint8_t fr[200], rx[2048];
    /* RDT = 31 of 32: 31 descriptors are the hardware's */
    for (uint32_t k = 0; k < 50; k++) {
        peer_mk(fr, n->mac, peer_mac, 64 + k, k);
        peer_push(&f->peer, fr, 64 + k, 0);
    }
    wait_us(&e->fp, 10);
    CHECK(f->rx_frames == 31 && f->rx_full > 0 && f->peer.count == 19);
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
    /* RXE (STATERR bit 31) drops the frame; L4E/IPE (29, 30) do not */
    peer_push(&f->peer, fr, 77, 0x80);
    wait_us(&e->fp, 10);
    CHECK(n->recv(n, rx, sizeof rx) == 0);
    peer_push(&f->peer, fr, 77, 0x60);
    wait_us(&e->fp, 10);
    CHECK(n->recv(n, rx, sizeof rx) == 77);
    static const uint8_t other[6] = {0x02, 0, 0, 0, 0, 9};
    static const uint8_t bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t dirty[6] = {0x78, 0x56, 0x34, 0x12, 0xBC, 0x9A}; /* reset RAR[1..15] */
    peer_mk(fr, other, peer_mac, 64, 3);
    peer_push(&f->peer, fr, 64, 0);
    peer_mk(fr, dirty, peer_mac, 66, 5);
    peer_push(&f->peer, fr, 66, 0);
    peer_mk(fr, bc, peer_mac, 65, 4);
    peer_push(&f->peer, fr, 65, 0);
    wait_us(&e->fp, 10);
    CHECK(f->rx_filtered == 2);
    CHECK(n->recv(n, rx, sizeof rx) == 65);
    CHECK(n->recv(n, rx, sizeof rx) == 0);
    CHECK(f->violations == 0);
    env_free(e);
}

static void k_reset(figc_t *f)
{
    f->reset_stuck = 1;
}
static void k_ones(figc_t *f)
{
    f->ones = 1;
    memset(f->r, 0xFF, REGS);
}
static void k_zero(figc_t *f)
{
    f->zero_mac = 1;
}
static void k_group(figc_t *f)
{
    f->mac[0] = 0x03;
}
static void k_unplug(figc_t *f)
{
    f->unplugged = 1;
}
static void k_rxq(figc_t *f)
{
    f->rxq_stuck = 1;
}
static void k_txq(figc_t *f)
{
    f->txq_stuck = 1;
}
static void k_mdic(figc_t *f)
{
    f->mdic_dead = 1;
}
static void k_master(figc_t *f)
{
    f->master_stuck = 1;
}
static void k_autord(figc_t *f)
{
    f->autord_never = 1;
}

typedef struct fault {
    const char *what;
    uint16_t did;
    void (*breakit)(figc_t *);
    hw_verdict_t want;
    const char *note;
} fault_t;

static void test_faults(void)
{
    static const fault_t fs[] = {
        {"reset stuck", 0x15F3, k_reset, HW_F, "I225-V reset timed out"},
        {"all-ones", 0x125C, k_ones, HW_F, "I226-V registers read all-ones"},
        {"zero MAC", 0x125B, k_zero, HW_F, "I226-LM no MAC address"},
        {"blank NVM part", 0x125F, k_zero, HW_F, "I226 (blank NVM) no MAC address"},
        {"group MAC", 0x15F3, k_group, HW_B, "03:a1:59:22:50:07 has the group bit set"},
        {"unplugged", 0x125C, k_unplug, HW_T, "I226-V a8:a1:59:22:50:07 link down as eth0"},
        {"RX queue stuck", 0x125C, k_rxq, HW_F, "a8:a1:59:22:50:07 RX queue did not enable"},
        {"TX queue stuck", 0x15F3, k_txq, HW_F, "TX queue did not enable"},
        {"MDIC dead", 0x125C, k_mdic, HW_T, "link down as eth0 (PHY did not answer)"},
        {"bus master stuck", 0x125C, k_master, HW_T, "link up as eth0, 2500 Mb/s"},
        {"NVM auto-read never done", 0x125C, k_autord, HW_T, "link up as eth0, 2500 Mb/s"},
    };
    for (uint32_t i = 0; i < sizeof fs / sizeof fs[0]; i++) {
        env_t *e = env_new(fs[i].did, fs[i].breakit);
        if (e->v != fs[i].want || !strstr(e->note.b, fs[i].note))
            printf("  fault '%s': verdict %d note '%s'\n", fs[i].what, e->v, e->note.b);
        CHECK(e->v == fs[i].want);
        CHECK(strstr(e->note.b, fs[i].note) != 0);
        CHECK((e->v == HW_T) == (e->r.n_nic == 1));
        CHECK(e->fp.now_us < 3700000u);
        if (e->v == HW_T) CHECK(e->f->violations == 0);
        env_free(e);
    }
}

static void test_tx_full_and_link_down(void)
{
    env_t *e = env_new(0x125C, 0);
    hw_nic_t *n = e->r.nic[0];
    e->f->tx_dd_never = 1;
    uint8_t tx[64];
    peer_mk(tx, peer_mac, n->mac, 64, 0);
    uint64_t t0 = e->fp.now_us;
    CHECK(n->send(n, tx, 64) == -1);
    CHECK(e->fp.now_us - t0 >= 50000 && e->fp.now_us - t0 < 60000);
    /* capacity is TXN - 1 = 15 outstanding: the 16th is refused at once */
    for (int k = 1; k < 15; k++) CHECK(n->send(n, tx, 64) == -1);
    CHECK(rd(e->f, TDT) == 15);
    t0 = e->fp.now_us;
    CHECK(n->send(n, tx, 64) == -2);
    CHECK(e->fp.now_us == t0);
    CHECK(rd(e->f, TDT) == 15);
    e->f->tx_dd_never = 0;
    wait_us(&e->fp, 10);
    CHECK(e->f->tx_frames == 15);
    /* reclaimed: the ring takes frames again and wraps */
    for (int k = 0; k < 20; k++) CHECK(n->send(n, tx, 64) == 0);
    CHECK(e->f->tx_frames == 35);
    CHECK(e->f->violations == 0);
    env_free(e);

    e = env_new(0x125C, k_unplug);
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
    /* cable pulled while running: link goes down, send reports it, no hang */
    e->f->unplugged = 1;
    wait_us(&e->fp, 10);
    uint32_t sent = e->f->tx_frames;
    CHECK(n->send(n, tx, 64) == 0);
    CHECK(n->link_up == 0 && e->f->tx_frames == sent);
    env_free(e);
}

static void test_no_bar_unknown_full_dma(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fake_dev_new(&fp, HW_BDF(0, 7, 0), 0x8086, 0x125C, 2, 0, 0);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_one(&fp, &r, &note) == HW_N);
    CHECK(strcmp(note.b, "BAR0 not assigned") == 0);
    fake_plat_free(&fp);

    env_t *e = env_new(0x125C, 0);
    hw_pci_dev_t d = g_dev[0];
    d.device = 0x10C9; /* an igb id is not igc's */
    hw_line_init(&e->note);
    CHECK(igc_bind(&e->fp.plat, &d, &e->r, &e->note) == HW_N);
    CHECK(strcmp(e->note.b, "unknown device id") == 0);
    d.device = 0x125C;
    d.vendor = 0x10EC; /* right device id, wrong vendor */
    hw_line_init(&e->note);
    CHECK(igc_bind(&e->fp.plat, &d, &e->r, &e->note) == HW_N);
    CHECK(strcmp(e->note.b, "unknown device id") == 0);
    d.vendor = 0x8086;
    e->r.n_nic = HW_MAX_NIC;
    hw_line_init(&e->note);
    CHECK(igc_bind(&e->fp.plat, &d, &e->r, &e->note) == HW_N);
    CHECK(strstr(e->note.b, "NIC table full") != 0);
    env_free(e);

    for (uint32_t k = 1; k <= 5; k++) {
        fake_plat_init(&fp);
        figc_t *f = add_igc(&fp, HW_BDF(0, 6, 0), 0x125C);
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
    figc_t *a = add_igc(&fp, HW_BDF(0, 2, 0), 0x125C);
    figc_t *b = add_igc(&fp, HW_BDF(0, 3, 0), 0x15F2);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_probe_stats_t st = hw_probe_all(&fp.plat, &r);
    CHECK(st.functions == 2 && st.bound == 2 && st.failed == 0 && st.held == 0);
    CHECK(fake_log_has(&fp, "[DRIVER ONLINE] igc 00:02.0 8086:125c I226-V "
                            "a8:a1:59:22:50:02 link up as eth0, 2500 Mb/s"));
    CHECK(fake_log_has(&fp, "[DRIVER ONLINE] igc 00:03.0 8086:15f2 I225-LM "
                            "a8:a1:59:22:50:03 link up as eth1, 2500 Mb/s"));
    CHECK(hw_selftest_all(&fp.plat, &r) == 2);
    CHECK(fake_log_has(&fp, "[SELFTEST OK] eth0 DHCP offer received; mac a8:a1:59:22:50:02; "
                            "offered 10.0.2.15 by 10.0.2.2"));
    CHECK(fake_log_has(&fp, "[SELFTEST OK] eth1 DHCP offer received"));
    CHECK(a->peer.discovers == 1 && b->peer.discovers == 1);
    CHECK(a->violations == 0 && b->violations == 0);
    fake_plat_free(&fp);
    free(a);
    free(b);

    fake_plat_init(&fp);
    a = add_igc(&fp, HW_BDF(0, 2, 0), 0x125C);
    a->peer.no_dhcp = 1;
    memset(&r, 0, sizeof r);
    hw_probe_all(&fp.plat, &r);
    CHECK(hw_selftest_all(&fp.plat, &r) == 0);
    CHECK(a->peer.discovers == 3);
    CHECK(fake_log_has(&fp, "[SELFTEST NONE] eth0"));
    fake_plat_free(&fp);
    free(a);
}

int main(void)
{
    test_ids();
    test_bind_models();
    test_speeds();
    test_echo_and_wrap();
    test_rx_full_and_errors();
    test_faults();
    test_tx_full_and_link_down();
    test_no_bar_unknown_full_dma();
    test_probe_and_selftest();
    printf("test_igc: %d checks, %d failures\n", fk_checks, fk_failures);
    return fk_failures ? 1 : 0;
}
