/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_xhci.c — xhci.c, usbkbd.c and usbmsc.c against a register-level fake
 * xHCI controller with fake USB functions behind its root ports.
 *
 * The controller fake implements what the driver uses: USB Legacy Support
 * and Supported Protocol capabilities, USBCMD/USBSTS (halt, HCRST, CNR),
 * PORTSC with write-1-to-clear change bits and port reset, the command ring
 * (Enable Slot, Address Device, Evaluate Context, Configure Endpoint, Reset /
 * Stop Endpoint, Set TR Dequeue), transfer rings (Setup/Data/Status and
 * Normal TRBs, links with toggle cycle) and one event ring segment. Registers
 * are plain memory, so the fake finds driver writes by comparing each
 * register with the value it last published, and doorbells by a sentinel.
 *
 * Behind the ports: a HID boot keyboard, a bulk-only SCSI disk, and a hub
 * that can hold either. Data toggles are modelled per endpoint on both sides,
 * so a halt cleared on one side only is caught. Every protocol rule the fake
 * checks counts into f->viol; each test expects it to stay 0. Knobs break
 * the controller or a device in each way the driver must survive. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hwfake.h"
#include "../usb.h"

#define BAR_SZ  0x4000u
#define CAPLEN  0x40u
#define OPB     CAPLEN
#define RTS     0x1000u
#define DBO     0x2000u
#define XECP    0x800u
#define NPORTS  4
#define MAXSLOT 40
#define SENT    0xFFFFFFFFu /* doorbell sentinel */
/* A reserved bit the fake sets in what it publishes for USBSTS and PORTSC:
 * a driver write never carries it, so even a write of the value just read
 * (writing 1 to clear a set change bit) is seen. */
#define MARK (1u << 28)

#define O_CMD    (OPB + 0x00)
#define O_STS    (OPB + 0x04)
#define O_PGSZ   (OPB + 0x08)
#define O_CRCR   (OPB + 0x18)
#define O_DCBAAP (OPB + 0x30)
#define O_CONFIG (OPB + 0x38)
#define O_PS(n)  (OPB + 0x400 + 0x10 * ((n) - 1))
#define R_IMAN   (RTS + 0x20)
#define R_ERSTSZ (RTS + 0x28)
#define R_ERSTBA (RTS + 0x30)
#define R_ERDP   (RTS + 0x38)

#define PS_CCS (1u << 0)
#define PS_PED (1u << 1)
#define PS_PR  (1u << 4)
#define PS_PP  (1u << 9)
#define PS_CSC (1u << 17)
#define PS_PEC (1u << 18)
#define PS_WRC (1u << 19)
#define PS_PRC (1u << 21)
#define PS_CHG (0x7Fu << 17)
#define PS_WPR (1u << 31)

enum { NAK = -100, STALL = -101 };
enum { K_KBD = 1, K_DISK, K_HUB, K_OTHER };
enum { EP_DIS = 0, EP_RUN = 1, EP_HALT = 2, EP_STOP = 3 };

/* ---- fake USB functions ------------------------------------------------- */
typedef struct fusb {
    int kind;
    uint8_t speed, mps0;
    uint8_t dd[18];
    uint8_t cfg[96];
    uint32_t cfg_len;
    uint8_t config, addressed;
    uint8_t halted[32], tog[32]; /* device side, by endpoint index num + 16 * IN */
    uint32_t clear_halts, set_configs, ctrl_reqs;
    int stall_req; /* bRequest to stall, -1 none */
    int nak_ctrl;  /* never answer a control transfer after this many */
    uint32_t ep_mps;
    /* keyboard */
    int protocol, idle, leds;
    uint8_t rep[64][8];
    uint64_t rep_at[64];
    uint32_t rh, rt;
    int stall_intr_once;
    /* disk (bulk-only, SCSI) */
    uint8_t *disk;
    uint32_t disk_blocks, bs;
    uint64_t cap_blocks; /* what READ CAPACITY reports */
    int phase;           /* 0 CBW, 1 data in, 2 data out, 3 CSW */
    uint32_t tag, dlen, dpos, residue;
    uint8_t status, key, asc, ascq;
    uint8_t data[20000];
    uint64_t out_lba;
    int short_csw_once, phase_err_once, stall_read_once, stall_csw_once, not_ready, no_medium,
        ua_once, nak_bulk, inq_type, bad_tag_once, short_read_once, spinup;
    int short_dd; /* GET_DESCRIPTOR(device) answers 12 bytes */
    uint32_t cbws, bot_resets, turs, reads, writes, senses, last_op, proto_err;
    /* hub */
    struct fusb *child[5];
    uint32_t pst[5]; /* wPortStatus | wPortChange << 16 */
    uint32_t hub_powered, hub_resets;
    uint32_t hub_ports;
} fusb_t;

static void put16(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t) v;
    b[1] = (uint8_t) (v >> 8);
}

static uint32_t cfg_add(fusb_t *u, const uint8_t *d, uint32_t n)
{
    memcpy(u->cfg + u->cfg_len, d, n);
    u->cfg_len += n;
    put16(u->cfg + 2, u->cfg_len);
    return n;
}

static void dev_desc(fusb_t *u, uint16_t vid, uint16_t pid, uint8_t cls)
{
    uint8_t d[18] = {18, 1, 0x00, 0x02, cls, 0, 0, u->mps0, 0, 0, 0, 0, 0, 1, 1, 2, 0, 1};
    if (u->speed >= 4) {
        d[3] = 3;
        d[7] = 9; /* SuperSpeed: 2^9 */
    }
    put16(d + 8, vid);
    put16(d + 10, pid);
    memcpy(u->dd, d, 18);
    uint8_t c[9] = {9, 2, 0, 0, 1, 1, 0, 0x80, 50};
    u->cfg_len = 0;
    cfg_add(u, c, 9);
}

static void ep_desc(fusb_t *u, uint8_t addr, uint8_t attr, uint16_t mps, uint8_t ival)
{
    uint8_t e[7] = {7, 5, addr, attr, 0, 0, ival};
    put16(e + 4, mps);
    cfg_add(u, e, 7);
    if (u->speed >= 4) {
        uint8_t ss[6] = {6, 0x30, 3, 0, 0, 0}; /* bMaxBurst 3 */
        cfg_add(u, ss, 6);
    }
}

static fusb_t *new_kbd(uint8_t speed)
{
    fusb_t *u = (fusb_t *) calloc(1, sizeof *u);
    u->kind = K_KBD;
    u->speed = speed;
    u->mps0 = 8;
    u->stall_req = -1;
    u->nak_ctrl = -1;
    u->protocol = u->idle = u->leds = -1;
    dev_desc(u, 0x046d, 0xc31c, 0);
    uint8_t i[9] = {9, 4, 0, 0, 1, 3, 1, 1, 0};
    uint8_t hid[9] = {9, 0x21, 0x11, 0x01, 0, 1, 0x22, 63, 0};
    /* a mouse interface first, which the driver must skip */
    uint8_t m[9] = {9, 4, 1, 0, 1, 3, 1, 2, 0};
    cfg_add(u, m, 9);
    ep_desc(u, 0x82, 3, 4, 10);
    cfg_add(u, i, 9);
    cfg_add(u, hid, 9);
    ep_desc(u, 0x81, 3, 8, 10);
    u->cfg[4] = 2;
    u->ep_mps = 8;
    return u;
}

static fusb_t *new_disk(uint8_t speed, uint32_t blocks, uint32_t bs)
{
    fusb_t *u = (fusb_t *) calloc(1, sizeof *u);
    u->kind = K_DISK;
    u->speed = speed;
    u->mps0 = speed >= 4 ? 9 : (speed == 3 ? 64 : 8);
    u->stall_req = -1;
    u->nak_ctrl = -1;
    u->disk_blocks = blocks;
    u->bs = bs;
    u->cap_blocks = blocks;
    u->disk = (uint8_t *) calloc(blocks, bs);
    for (uint32_t i = 0; i < blocks * bs; i++) u->disk[i] = (uint8_t) (i * 13 + i / bs);
    memcpy(u->disk + bs, "EFI PART", 8);
    dev_desc(u, 0x0781, 0x5581, 0);
    uint16_t mps = speed >= 4 ? 1024 : (speed == 3 ? 512 : 64);
    u->ep_mps = mps;
    uint8_t i[9] = {9, 4, 0, 0, 2, 8, 6, 0x50, 0};
    /* alternate setting 1 (UAS) must not add endpoints */
    uint8_t alt[9] = {9, 4, 0, 1, 2, 8, 6, 0x62, 0};
    cfg_add(u, i, 9);
    ep_desc(u, 0x81, 2, mps, 0);
    ep_desc(u, 0x02, 2, mps, 0);
    cfg_add(u, alt, 9);
    ep_desc(u, 0x83, 2, mps, 0);
    ep_desc(u, 0x04, 2, mps, 0);
    return u;
}

static fusb_t *new_hub(uint8_t speed)
{
    fusb_t *u = (fusb_t *) calloc(1, sizeof *u);
    u->kind = K_HUB;
    u->speed = speed;
    u->mps0 = 64;
    u->stall_req = -1;
    u->nak_ctrl = -1;
    u->hub_ports = 4;
    dev_desc(u, 0x05e3, 0x0608, 9);
    uint8_t i[9] = {9, 4, 0, 0, 1, 9, 0, 1, 0};
    cfg_add(u, i, 9);
    ep_desc(u, 0x81, 3, 1, 12);
    return u;
}

static fusb_t *new_other(void)
{
    fusb_t *u = (fusb_t *) calloc(1, sizeof *u);
    u->kind = K_OTHER;
    u->speed = 3;
    u->mps0 = 64;
    u->stall_req = -1;
    u->nak_ctrl = -1;
    dev_desc(u, 0x1234, 0x5678, 0xFF);
    uint8_t i[9] = {9, 4, 0, 0, 0, 0xFF, 0, 0, 0};
    cfg_add(u, i, 9);
    return u;
}

static void free_usb(fusb_t *u)
{
    if (!u) return;
    for (int i = 0; i < 5; i++) free_usb(u->child[i]);
    free(u->disk);
    free(u);
}

static void kbd_push(fusb_t *u, uint64_t at, uint8_t mods, uint8_t k0, uint8_t k1)
{
    uint8_t *r = u->rep[u->rt % 64];
    memset(r, 0, 8);
    r[0] = mods;
    r[2] = k0;
    r[3] = k1;
    u->rep_at[u->rt % 64] = at;
    u->rt++;
}

/* ---- SCSI over bulk-only ------------------------------------------------- */
static uint32_t be32(const uint8_t *b)
{
    return ((uint32_t) b[0] << 24) | ((uint32_t) b[1] << 16) | ((uint32_t) b[2] << 8) | b[3];
}
static void wbe32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t) (v >> 24);
    b[1] = (uint8_t) (v >> 16);
    b[2] = (uint8_t) (v >> 8);
    b[3] = (uint8_t) v;
}

static void sense_set(fusb_t *u, uint8_t key, uint8_t asc)
{
    u->key = key;
    u->asc = asc;
    u->status = 1;
}

/* Execute a CBW's command block; fills u->data for IN commands. */
static void scsi(fusb_t *u, const uint8_t *cb, uint32_t xlen, int in)
{
    u->status = 0;
    u->dlen = 0;
    u->dpos = 0;
    u->last_op = cb[0];
    switch (cb[0]) {
    case 0x12: /* INQUIRY */
        memset(u->data, 0, 36);
        u->data[0] = (uint8_t) u->inq_type;
        u->data[4] = 31;
        memcpy(u->data + 8, "ZXVFAKE USB DISK        0001", 28);
        u->dlen = 36;
        break;
    case 0x00: /* TEST UNIT READY */
        u->turs++;
        if (u->no_medium)
            sense_set(u, 2, 0x3A);
        else if (u->not_ready)
            sense_set(u, 2, 0x04);
        else if (u->spinup) { /* becoming ready */
            u->spinup--;
            sense_set(u, 2, 0x04);
            u->ascq = 1;
        } else if (u->ua_once) {
            u->ua_once = 0;
            sense_set(u, 6, 0x29);
        }
        break;
    case 0x03: /* REQUEST SENSE */
        u->senses++;
        memset(u->data, 0, 18);
        u->data[0] = 0x70;
        u->data[2] = u->key;
        u->data[7] = 10;
        u->data[12] = u->asc;
        u->data[13] = u->ascq;
        u->dlen = 18;
        if (!u->not_ready && !u->no_medium) u->key = u->asc = u->ascq = 0;
        break;
    case 0x25: /* READ CAPACITY(10) */
        wbe32(u->data,
              u->cap_blocks > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t) (u->cap_blocks - 1));
        wbe32(u->data + 4, u->bs);
        u->dlen = 8;
        break;
    case 0x9E: /* READ CAPACITY(16) */
        if (cb[1] != 0x10) {
            sense_set(u, 5, 0x24);
            break;
        }
        memset(u->data, 0, 32);
        wbe32(u->data, (uint32_t) ((u->cap_blocks - 1) >> 32));
        wbe32(u->data + 4, (uint32_t) (u->cap_blocks - 1));
        wbe32(u->data + 8, u->bs);
        u->dlen = 32;
        break;
    case 0x28:
    case 0x2A:
    case 0x88:
    case 0x8A: {
        int big = cb[0] >= 0x88;
        uint64_t lba = big ? ((uint64_t) be32(cb + 2) << 32) | be32(cb + 6) : be32(cb + 2);
        uint32_t n = big ? be32(cb + 10) : (uint32_t) ((cb[7] << 8) | cb[8]);
        if (n * u->bs != xlen || in != (cb[0] == 0x28 || cb[0] == 0x88)) u->proto_err++;
        lba %= u->disk_blocks; /* the huge-capacity knob maps high LBAs down */
        if (lba + n > u->disk_blocks) {
            sense_set(u, 5, 0x21);
            break;
        }
        if (in) {
            memcpy(u->data, u->disk + lba * u->bs, n * u->bs);
            u->dlen = n * u->bs;
            u->reads++;
        } else {
            u->out_lba = lba;
            u->writes++;
        }
        break;
    }
    default:
        sense_set(u, 5, 0x20);
    }
}

/* ---- the controller ------------------------------------------------------- */
typedef struct fep {
    uint64_t deq;
    uint8_t dcs, state, type, armed, tog, cerr, interval;
    uint16_t mps;
    uint32_t avg, resets, readds;
} fep_t;

typedef struct fslot {
    int on;
    fusb_t *dev;
    uint32_t route, root, speed, entries, hub, nports, tt_slot, tt_port, ttt;
    fep_t ep[32];
} fslot_t;

typedef struct fx {
    uint8_t *r;
    fake_dev_t *pd;
    uint64_t now;
    fusb_t *port[NPORTS + 1];
    uint32_t ps[NPORTS + 1], pub_ps[NPORTS + 1], pub_sts, sts, pub_leg;
    uint64_t pr_done[NPORTS + 1];
    int running, rst_pending, csz;
    uint64_t rst_at, cnr_at, bios_req_at;
    /* command ring and event ring */
    uint64_t cr;
    uint8_t ccs;
    uint64_t seg;
    uint32_t seg_n, eidx;
    uint8_t pcs;
    fslot_t slot[MAXSLOT + 1];
    /* knobs */
    int never_halts, never_ready, rst_stuck, bios_owned, bios_never, ppc, no_train, pr_stuck, hse,
        dead_cmds, fail_type, fail_cc, scratch, maxslots;
    /* observations */
    uint32_t viol, resets, warm_resets, port_resets, cmds[24], doorbells, evts, stale_evts,
        overflow, smi_ctl, runs;
} fx_t;

static fx_t *g_fx;

#define VIOL(f, why)                                                                               \
    do {                                                                                           \
        (f)->viol++;                                                                               \
        printf("  fake xhci: %s (line %d)\n", why, __LINE__);                                      \
    } while (0)

static uint32_t rd(fx_t *f, uint32_t o)
{
    return fk_rd32(f->r + o);
}
static void wr(fx_t *f, uint32_t o, uint32_t v)
{
    fk_wr32(f->r + o, v);
}

static void post(fx_t *f, uint64_t ptr, uint32_t st, uint32_t ctl)
{
    if (!f->seg) return;
    uint64_t erdp = fk_rd64(f->r + R_ERDP) & ~0xFull;
    uint32_t deq = (uint32_t) ((erdp - f->seg) / 16);
    if ((f->eidx + 1) % f->seg_n == deq) {
        f->overflow++;
        return;
    }
    uint8_t *e = (uint8_t *) fk_ptr(f->seg) + f->eidx * 16;
    fk_wr64(e, ptr);
    fk_wr32(e + 8, st);
    fk_wr32(e + 12, (ctl & ~1u) | f->pcs);
    f->evts++;
    if (++f->eidx == f->seg_n) {
        f->eidx = 0;
        f->pcs ^= 1;
    }
}

static void xfer_evt(fx_t *f, uint32_t slot, uint32_t dci, uint64_t trb, uint32_t cc, uint32_t res)
{
    post(f, trb, (cc << 24) | res, (32u << 10) | (dci << 16) | (slot << 24));
}

/* the next TRB the controller owns at (deq, cs), following links */
static uint8_t *trb_next(uint64_t *deq, uint8_t *cs)
{
    for (int guard = 0; guard < 4; guard++) {
        uint8_t *t = (uint8_t *) fk_ptr(*deq);
        uint32_t ctl = fk_rd32(t + 12);
        if ((ctl & 1) != *cs) return 0;
        if (((ctl >> 10) & 0x3F) == 6) {
            *deq = fk_rd64(t) & ~0xFull;
            if (ctl & 2) *cs ^= 1;
            continue;
        }
        *deq += 16;
        return t;
    }
    return 0;
}

static uint8_t *octx(fx_t *f, uint32_t slot)
{
    uint64_t dcbaa = fk_rd64(f->r + O_DCBAAP);
    return (uint8_t *) fk_ptr(fk_rd64((uint8_t *) fk_ptr(dcbaa) + 8 * slot));
}

static void set_state(fx_t *f, uint32_t slot, uint32_t dci, uint8_t st)
{
    f->slot[slot].ep[dci].state = st;
    uint8_t *c = octx(f, slot) + dci * f->csz;
    fk_wr32(c, (fk_rd32(c) & ~7u) | st);
}

static int ep_index(uint32_t dci)
{
    return (int) (dci / 2) + ((dci & 1) ? 16 : 0);
}

/* toggles: both sides must agree whenever data moves */
static void toggle(fx_t *f, fep_t *e, fusb_t *u, uint32_t dci, uint32_t bytes, uint32_t mps)
{
    int i = ep_index(dci);
    if (e->tog != u->tog[i]) VIOL(f, "data toggle mismatch");
    uint32_t pk = bytes ? (bytes + mps - 1) / mps : 1;
    e->tog ^= pk & 1;
    u->tog[i] = e->tog;
}

/* ---- device behaviour ------------------------------------------------------ */
static int hub_ctrl(fusb_t *u, const uint8_t *s, uint8_t *buf, uint32_t len)
{
    uint32_t rt = s[0], req = s[1], val = s[2] | (s[3] << 8), idx = s[4] | (s[5] << 8);
    if (rt == 0xA0 && req == 6 && (val >> 8) == 0x29) {
        uint8_t hd[9] = {9, 0x29, (uint8_t) u->hub_ports, 0x00, 0, 50, 100, 0, 0xFF};
        uint32_t n = len < 9 ? len : 9;
        memcpy(buf, hd, n);
        return (int) n;
    }
    if ((rt == 0x23 || rt == 0xA3) && (idx == 0 || idx > u->hub_ports)) return STALL;
    if (rt == 0x23 && req == 3) { /* SET_FEATURE(port) */
        if (val == 8) {
            u->hub_powered |= 1u << idx;
            u->pst[idx] |= 0x100;
            if (u->child[idx]) u->pst[idx] |= 1 | (1u << 16);
        } else if (val == 4) {
            u->hub_resets++;
            if (u->pst[idx] & 1) {
                fusb_t *c = u->child[idx];
                u->pst[idx] |= 2 | (1u << 20);
                u->pst[idx] &= ~0x600u;
                if (c->speed == 2) u->pst[idx] |= 0x200;
                if (c->speed == 3) u->pst[idx] |= 0x400;
            }
        } else {
            return STALL;
        }
        return 0;
    }
    if (rt == 0x23 && req == 1) { /* CLEAR_FEATURE(port) */
        if (val == 16)
            u->pst[idx] &= ~(1u << 16);
        else if (val == 20)
            u->pst[idx] &= ~(1u << 20);
        else
            return STALL;
        return 0;
    }
    if (rt == 0xA3 && req == 0 && len == 4) {
        fk_wr32(buf, u->pst[idx]);
        return 4;
    }
    return STALL;
}

static int dev_ctrl(fx_t *f, fusb_t *u, const uint8_t *s, uint8_t *buf, uint32_t len)
{
    uint32_t rt = s[0], req = s[1], val = s[2] | (s[3] << 8), idx = s[4] | (s[5] << 8);
    (void) f;
    if (u->nak_ctrl == 0) return NAK;
    if (u->nak_ctrl > 0) u->nak_ctrl--;
    u->ctrl_reqs++;
    if ((int) req == u->stall_req) return STALL;
    if (rt == 0x80 && req == 6) {
        const uint8_t *src = 0;
        uint32_t n = 0;
        if (val == 0x0100)
            src = u->dd, n = u->short_dd && len > 8 ? 12 : 18;
        else if (val == 0x0200)
            src = u->cfg, n = u->cfg_len;
        else
            return STALL;
        n = n < len ? n : len;
        memcpy(buf, src, n);
        return (int) n;
    }
    if (rt == 0x00 && req == 9) {
        if (val != u->cfg[5]) return STALL;
        u->config = (uint8_t) val;
        u->set_configs++;
        memset(u->tog, 0, sizeof u->tog);
        return 0;
    }
    if (rt == 0x02 && req == 1 && val == 0) { /* CLEAR_FEATURE(ENDPOINT_HALT) */
        int i = (int) (idx & 0xF) + ((idx & 0x80) ? 16 : 0);
        u->halted[i] = 0;
        u->tog[i] = 0;
        u->clear_halts++;
        return 0;
    }
    if (u->kind == K_KBD && rt == 0x21 && req == 0x0B && idx == 0) {
        u->protocol = (int) val;
        return 0;
    }
    if (u->kind == K_KBD && rt == 0x21 && req == 0x09 && val == 0x0200 && idx == 0 && len == 1) {
        u->leds = buf[0];
        return 0;
    }
    if (u->kind == K_KBD && rt == 0x21 && req == 0x0A && idx == 0) {
        u->idle = (int) val;
        return 0;
    }
    if (u->kind == K_DISK && rt == 0x21 && req == 0xFF && idx == 0) {
        u->bot_resets++;
        u->phase = 0;
        return 0;
    }
    if (u->kind == K_HUB) return hub_ctrl(u, s, buf, len);
    return STALL;
}

/* Bulk/interrupt transfer on endpoint index i of u. Returns bytes, NAK or STALL. */
static int dev_data(fx_t *f, fusb_t *u, int i, uint8_t *buf, uint32_t len)
{
    int in = i >= 16;
    if (u->halted[i]) return STALL;
    if (u->kind == K_KBD) {
        if (!in || (i & 0xF) != 1) return STALL;
        if (u->rh == u->rt || u->rep_at[u->rh % 64] > f->now) return NAK;
        if (u->stall_intr_once) {
            u->stall_intr_once = 0;
            u->halted[i] = 1;
            return STALL;
        }
        uint32_t n = len < 8 ? len : 8;
        memcpy(buf, u->rep[u->rh % 64], n);
        u->rh++;
        return (int) n;
    }
    if (u->kind != K_DISK || (i & 0xF) != (in ? 1 : 2)) return STALL;
    if (u->nak_bulk) return NAK;
    if (!in) {
        if (u->phase == 0) { /* CBW */
            if (len != 31 || fk_rd32(buf) != 0x43425355u || buf[13] != 0 || buf[14] < 6 ||
                buf[14] > 16) {
                u->proto_err++;
                u->halted[i] = 1;
                return STALL;
            }
            u->cbws++;
            u->tag = fk_rd32(buf + 4);
            uint32_t xlen = fk_rd32(buf + 8);
            int din = (buf[12] & 0x80) != 0;
            scsi(u, buf + 15, xlen, din);
            u->residue = xlen;
            if (xlen == 0)
                u->phase = 3;
            else if (din) {
                u->phase = 1;
                if (u->status) u->dlen = 0;
                if (u->dlen > xlen) u->dlen = xlen;
            } else {
                u->phase = 2;
                u->dlen = xlen;
            }
            return 31;
        }
        if (u->phase == 2) { /* data out */
            if (len != u->dlen) u->proto_err++;
            memcpy(u->disk + u->out_lba * u->bs, buf, len);
            u->residue = u->dlen - len;
            u->phase = 3;
            return (int) len;
        }
        u->proto_err++;
        return STALL;
    }
    if (u->phase == 1) { /* data in */
        if (u->stall_read_once && (u->last_op == 0x28 || u->last_op == 0x88)) {
            u->stall_read_once = 0;
            u->halted[i] = 1;
            u->status = 1;
            u->phase = 3;
            return STALL;
        }
        if (u->dlen == 0 && u->status) { /* failed command: stall the data stage */
            u->halted[i] = 1;
            u->phase = 3;
            return STALL;
        }
        uint32_t n = u->dlen < len ? u->dlen : len;
        if (u->short_read_once && u->last_op == 0x28) { /* half the data, CSW still good */
            u->short_read_once = 0;
            n /= 2;
        }
        memcpy(buf, u->data, n);
        u->residue -= n;
        u->phase = 3;
        return (int) n;
    }
    if (u->phase == 3) { /* CSW */
        if (u->stall_csw_once) {
            u->stall_csw_once = 0;
            u->halted[i] = 1;
            return STALL;
        }
        if (len < 13) u->proto_err++;
        fk_wr32(buf, 0x53425355u);
        fk_wr32(buf + 4, u->tag + (u->bad_tag_once ? 1u : 0u));
        u->bad_tag_once = 0;
        fk_wr32(buf + 8, u->residue);
        buf[12] = u->status;
        u->phase = 0;
        if (u->phase_err_once) {
            u->phase_err_once = 0;
            buf[12] = 2;
        }
        if (u->short_csw_once) {
            u->short_csw_once = 0;
            return 12;
        }
        return 13;
    }
    u->proto_err++;
    return STALL;
}

/* ---- transfer rings --------------------------------------------------------- */
enum { R_OK, R_NAK, R_HALT, R_BAD };

static int do_control(fx_t *f, fslot_t *s, uint32_t slot, uint8_t *t, uint64_t *deq, uint8_t *cs)
{
    uint32_t ctl = fk_rd32(t + 12);
    if (((ctl >> 10) & 0x3F) != 2 || !(ctl & 0x40) || (fk_rd32(t + 8) & 0x1FFFF) != 8) {
        VIOL(f, "control TD does not start with an immediate 8-byte Setup TRB");
        return R_BAD;
    }
    uint8_t setup[8];
    memcpy(setup, t, 8);
    uint32_t wlen = setup[6] | (setup[7] << 8), trt = (ctl >> 16) & 3;
    int in = (setup[0] & 0x80) != 0;
    if (trt != (wlen ? (in ? 3u : 2u) : 0u)) VIOL(f, "Setup TRB transfer type");
    uint8_t *dt = 0, *buf = 0;
    if (wlen) {
        dt = trb_next(deq, cs);
        if (!dt || ((fk_rd32(dt + 12) >> 10) & 0x3F) != 3) {
            VIOL(f, "missing Data stage TRB");
            return R_BAD;
        }
        uint32_t dctl = fk_rd32(dt + 12);
        if (((dctl >> 16) & 1) != (uint32_t) in || (fk_rd32(dt + 8) & 0x1FFFF) != wlen)
            VIOL(f, "Data stage direction or length");
        buf = (uint8_t *) fk_ptr(fk_rd64(dt));
    }
    uint8_t *st = trb_next(deq, cs);
    if (!st || ((fk_rd32(st + 12) >> 10) & 0x3F) != 4) {
        VIOL(f, "missing Status stage TRB");
        return R_BAD;
    }
    uint32_t sctl = fk_rd32(st + 12);
    if (((sctl >> 16) & 1) != (uint32_t) ((wlen && in) ? 0 : 1)) VIOL(f, "Status stage direction");
    fusb_t *u = s->dev;
    uint8_t tmp[USB_CTRL_BUF];
    if (!in && wlen) memcpy(tmp, buf, wlen);
    int r = dev_ctrl(f, u, setup, tmp, wlen);
    if (r == NAK) return R_NAK;
    uint64_t at = (uint64_t) (uintptr_t) (dt ? dt : st);
    if (r == STALL) {
        xfer_evt(f, slot, 1, at, 6, wlen);
        return R_HALT;
    }
    if (in && r > 0) memcpy(buf, tmp, (size_t) r);
    if (dt && in && (uint32_t) r < wlen && (fk_rd32(dt + 12) & 4))
        xfer_evt(f, slot, 1, (uint64_t) (uintptr_t) dt, 13, wlen - (uint32_t) r);
    if (sctl & 0x20) xfer_evt(f, slot, 1, (uint64_t) (uintptr_t) st, 1, 0);
    return R_OK;
}

static int do_normal(fx_t *f, fslot_t *s, uint32_t slot, uint32_t dci, uint8_t *t)
{
    uint32_t ctl = fk_rd32(t + 12), len = fk_rd32(t + 8) & 0x1FFFF;
    if (((ctl >> 10) & 0x3F) != 1) {
        VIOL(f, "non-Normal TRB on a bulk/interrupt ring");
        return R_BAD;
    }
    uint8_t *buf = (uint8_t *) fk_ptr(fk_rd64(t));
    if (((uintptr_t) buf & 0xFFFF) + len > 0x10000) VIOL(f, "TRB buffer crosses 64 KB");
    fep_t *e = &s->ep[dci];
    int r = dev_data(f, s->dev, ep_index(dci), buf, len);
    if (r == NAK) return R_NAK;
    uint64_t at = (uint64_t) (uintptr_t) t;
    if (r == STALL) {
        xfer_evt(f, slot, dci, at, 6, len);
        return R_HALT;
    }
    toggle(f, e, s->dev, dci, (uint32_t) r, e->mps);
    uint32_t res = len - (uint32_t) r;
    if (res && (ctl & 4))
        xfer_evt(f, slot, dci, at, 13, res);
    else if (ctl & 0x20)
        xfer_evt(f, slot, dci, at, 1, res);
    return R_OK;
}

static void run_ep(fx_t *f, uint32_t slot, uint32_t dci)
{
    fslot_t *s = &f->slot[slot];
    fep_t *e = &s->ep[dci];
    for (int guard = 0; guard < 300 && e->armed && e->state == EP_RUN; guard++) {
        uint64_t deq = e->deq;
        uint8_t cs = e->dcs;
        uint8_t *t = trb_next(&deq, &cs);
        if (!t) {
            e->armed = 0;
            return;
        }
        int r = dci == 1 ? do_control(f, s, slot, t, &deq, &cs) : do_normal(f, s, slot, dci, t);
        if (r == R_NAK) return;
        if (r == R_BAD) {
            e->armed = 0;
            return;
        }
        if (r == R_HALT) {
            set_state(f, slot, dci, EP_HALT);
            e->armed = 0;
            return;
        }
        e->deq = deq;
        e->dcs = cs;
    }
}

/* ---- commands ------------------------------------------------------------------ */
static fusb_t *route_dev(fx_t *f, uint32_t root, uint32_t route)
{
    if (root < 1 || root > NPORTS) return 0;
    fusb_t *u = f->port[root];
    if (!u || !route) return u;
    if (u->kind != K_HUB || (route & 0xF) > 4 || (route >> 4)) return 0;
    if (!(u->pst[route & 0xF] & 2)) return 0; /* hub port not enabled */
    return u->child[route & 0xF];
}

static uint32_t expect_interval(const fusb_t *u, uint32_t type, uint32_t bival)
{
    if (type != 3 && type != 7) return 0;
    if (u->speed >= 3) return bival ? (bival > 16 ? 15 : bival - 1) : 0;
    uint32_t iv = 0;
    while ((2u << iv) <= bival * 8) iv++;
    return iv < 3 ? 3 : (iv > 10 ? 10 : iv);
}

/* Check an input endpoint context against the device's descriptor. */
static void check_ep_ctx(fx_t *f, fusb_t *u, uint32_t dci, const uint8_t *c)
{
    uint32_t d0 = fk_rd32(c), d1 = fk_rd32(c + 4);
    uint32_t type = (d1 >> 3) & 7, mps = d1 >> 16, cerr = (d1 >> 1) & 3;
    uint32_t want_dir = dci & 1;
    const uint8_t *p = u->cfg;
    int found = 0;
    for (uint32_t off = 0; off + 2 <= u->cfg_len; off += p[off]) {
        if (p[off + 1] != 5) continue;
        uint32_t a = p[off + 2];
        if ((a & 0xF) * 2 + ((a & 0x80) ? 1 : 0) != dci) continue;
        found = 1;
        uint32_t attr = p[off + 3] & 3, dmps = p[off + 4] | (p[off + 5] << 8);
        uint32_t want_type = attr + (want_dir ? 4 : 0);
        if (type != want_type) VIOL(f, "endpoint context type");
        if (mps != (dmps & 0x7FF)) VIOL(f, "endpoint context max packet size");
        if (((d0 >> 16) & 0xFF) != expect_interval(u, type, p[off + 6]))
            VIOL(f, "endpoint context interval");
        if (u->speed >= 4 && ((d1 >> 8) & 0xFF) != 3) VIOL(f, "SuperSpeed max burst");
        if (attr == 3 && (fk_rd32(c + 16) >> 16) != mps * (((d1 >> 8) & 0xFF) + 1))
            VIOL(f, "max ESIT payload");
        break;
    }
    if (!found) VIOL(f, "endpoint context for an endpoint the interface lacks");
    if (cerr != 3) VIOL(f, "CErr not 3");
    uint64_t deq = fk_rd64(c + 8);
    if (!(deq & ~0xFull) || (deq & 0xE)) VIOL(f, "TR dequeue pointer");
    if (!(fk_rd32(c + 16) & 0xFFFF)) VIOL(f, "average TRB length 0");
}

static uint32_t exec_cmd(fx_t *f, const uint8_t *t, uint32_t *slot_out)
{
    uint32_t ctl = fk_rd32(t + 12), type = (ctl >> 10) & 0x3F, slot = ctl >> 24;
    uint32_t dci = (ctl >> 16) & 0x1F;
    if (type < 24) f->cmds[type]++;
    if ((int) type == f->fail_type) return (uint32_t) f->fail_cc;
    uint32_t cfgmax = rd(f, O_CONFIG) & 0xFF;
    if (type != 9 && (slot == 0 || slot > cfgmax || !f->slot[slot].on))
        return 11; /* slot not enabled */
    fslot_t *s = &f->slot[slot];
    uint8_t *ic = (uint8_t *) fk_ptr(fk_rd64(t));
    uint32_t drop = 0, add = 0;
    if (type == 11 || type == 12 || type == 13) {
        if (!ic || ((uintptr_t) ic & 63)) {
            VIOL(f, "input context pointer");
            return 5;
        }
        drop = fk_rd32(ic);
        add = fk_rd32(ic + 4);
    }
    switch (type) {
    case 9: /* Enable Slot */
        for (uint32_t i = 1; i <= cfgmax && i <= MAXSLOT; i++) {
            if (!f->slot[i].on) {
                memset(&f->slot[i], 0, sizeof f->slot[i]);
                f->slot[i].on = 1;
                *slot_out = i;
                return 1;
            }
        }
        return 9; /* no slots available */
    case 11: {    /* Address Device */
        if (ctl & (1u << 9)) VIOL(f, "BSR set");
        if (add != 3 || drop) VIOL(f, "Address Device add/drop flags");
        uint8_t *sc = ic + f->csz, *e0 = ic + 2 * f->csz;
        uint32_t d0 = fk_rd32(sc), d1 = fk_rd32(sc + 4), d2 = fk_rd32(sc + 8);
        s->route = d0 & 0xFFFFF;
        s->speed = (d0 >> 20) & 0xF;
        s->root = (d1 >> 16) & 0xFF;
        s->tt_slot = d2 & 0xFF;
        s->tt_port = (d2 >> 8) & 0xFF;
        if ((d0 >> 27) != 1) VIOL(f, "Address Device context entries");
        fusb_t *u = route_dev(f, s->root, s->route);
        if (!u) return 4; /* USB transaction error: nobody there */
        if (u->addressed) VIOL(f, "device addressed twice");
        if (s->speed != u->speed) VIOL(f, "slot speed");
        if (!octx(f, slot) || ((uintptr_t) octx(f, slot) & 63)) VIOL(f, "DCBAA entry");
        uint32_t e1 = fk_rd32(e0 + 4);
        uint32_t want = u->speed >= 4 ? 512 : (u->speed == 3 ? 64 : 8);
        if (((e1 >> 3) & 7) != 4 || ((e1 >> 1) & 3) != 3 || (e1 >> 16) != want)
            VIOL(f, "EP0 context type/CErr/MPS");
        uint64_t deq = fk_rd64(e0 + 8);
        if ((deq & 1) != 1 || (fk_rd32(e0 + 16) & 0xFFFF) != 8) VIOL(f, "EP0 dequeue/avg");
        fep_t *e = &s->ep[1];
        e->deq = deq & ~0xFull;
        e->dcs = 1;
        e->mps = (uint16_t) want;
        memcpy(octx(f, slot), sc, (size_t) f->csz);
        memcpy(octx(f, slot) + f->csz, e0, (size_t) f->csz);
        set_state(f, slot, 1, EP_RUN);
        s->dev = u;
        u->addressed = 1;
        return 1;
    }
    case 13: { /* Evaluate Context */
        if (drop || (add & ~3u) || !(add & 2)) VIOL(f, "Evaluate Context flags");
        uint32_t m = fk_rd32(ic + 2 * f->csz + 4) >> 16;
        if (m != s->dev->mps0) VIOL(f, "evaluated MPS0 differs from the descriptor");
        s->ep[1].mps = (uint16_t) m;
        return 1;
    }
    case 12: { /* Configure Endpoint */
        if (!(add & 1) || (add & 2) || (drop & 3)) VIOL(f, "Configure Endpoint A0/A1/D0/D1");
        uint8_t *sc = ic + f->csz;
        uint32_t d0 = fk_rd32(sc), d1 = fk_rd32(sc + 4), d2 = fk_rd32(sc + 8);
        s->entries = d0 >> 27;
        s->hub = (d0 >> 26) & 1;
        s->nports = d1 >> 24;
        s->ttt = (d2 >> 16) & 3;
        if ((d0 & 0xFFFFF) != s->route || ((d0 >> 20) & 0xF) != s->speed ||
            ((d1 >> 16) & 0xFF) != s->root)
            VIOL(f, "Configure Endpoint slot context changed route/speed/port");
        for (uint32_t i = 2; i < 32; i++) {
            if (drop & (1u << i)) {
                set_state(f, slot, i, EP_DIS);
                s->ep[i].armed = 0;
            }
            if (add & (1u << i)) {
                uint8_t *c = ic + (1 + i) * f->csz;
                check_ep_ctx(f, s->dev, i, c);
                if (i > s->entries) VIOL(f, "context entries below an added endpoint");
                fep_t *e = &s->ep[i];
                if (e->state != EP_DIS && !(drop & (1u << i))) VIOL(f, "added an enabled endpoint");
                if (drop & (1u << i)) e->readds++;
                uint64_t deq = fk_rd64(c + 8);
                e->deq = deq & ~0xFull;
                e->dcs = deq & 1;
                e->mps = (uint16_t) (fk_rd32(c + 4) >> 16);
                e->tog = 0;
                e->armed = 0;
                memcpy(octx(f, slot) + i * f->csz, c, (size_t) f->csz);
                set_state(f, slot, i, EP_RUN);
            }
        }
        return 1;
    }
    case 14: /* Reset Endpoint */
        if (dci < 1 || s->ep[dci].state != EP_HALT) return 19;
        if (ctl & (1u << 9)) VIOL(f, "Reset Endpoint TSP");
        s->ep[dci].tog = 0;
        s->ep[dci].resets++;
        set_state(f, slot, dci, EP_STOP);
        return 1;
    case 15: { /* Stop Endpoint */
        fep_t *e = &s->ep[dci];
        if (dci < 1 || e->state != EP_RUN) return 19;
        uint64_t deq = e->deq;
        uint8_t cs = e->dcs;
        uint8_t *tt = trb_next(&deq, &cs);
        if (tt) { /* the TD that was in flight completes as Stopped */
            xfer_evt(f, slot, dci, (uint64_t) (uintptr_t) tt, 26, 0);
            f->stale_evts++;
        }
        e->armed = 0;
        set_state(f, slot, dci, EP_STOP);
        return 1;
    }
    case 16: { /* Set TR Dequeue Pointer */
        fep_t *e = &s->ep[dci];
        if (dci < 1 || (e->state != EP_STOP)) return 19;
        uint64_t p = fk_rd64(t);
        if (!(p & ~0xFull)) VIOL(f, "Set TR Dequeue to 0");
        e->deq = p & ~0xFull;
        e->dcs = p & 1;
        return 1;
    }
    }
    return 5; /* TRB error */
}

static void run_cmds(fx_t *f)
{
    for (int guard = 0; guard < 64; guard++) {
        uint8_t *t = trb_next(&f->cr, &f->ccs);
        if (!t) return;
        if (f->dead_cmds) continue; /* swallowed: never completes */
        uint32_t slot = (fk_rd32(t + 12) >> 24) & 0xFF;
        uint32_t cc = exec_cmd(f, t, &slot);
        post(f, (uint64_t) (uintptr_t) t, cc << 24, (33u << 10) | (slot << 24));
    }
}

/* ---- registers ------------------------------------------------------------------- */
static int usb3_port(uint32_t n)
{
    return n >= 3;
}

static void port_connect(fx_t *f, uint32_t n)
{
    fusb_t *u = f->port[n];
    uint32_t v = f->ps[n] & PS_PP;
    if (u && (v & PS_PP)) {
        v |= PS_CCS | PS_CSC;
        if (usb3_port(n) && !f->no_train) v |= PS_PED | ((uint32_t) u->speed << 10);
    }
    f->ps[n] = v;
}

static void hc_reset(fx_t *f)
{
    f->resets++;
    for (uint32_t i = 1; i <= MAXSLOT; i++) {
        f->slot[i].on = 0;
        if (f->slot[i].dev) f->slot[i].dev->addressed = 0;
    }
    f->seg = 0;
    for (uint32_t n = 1; n <= NPORTS; n++) {
        f->ps[n] = f->ppc ? 0 : PS_PP;
        port_connect(f, n);
    }
    f->sts = 0x1 | 0x800; /* HCH, CNR */
    f->cnr_at = f->now + 300;
}

static void port_write(fx_t *f, uint32_t n, uint32_t w)
{
    uint32_t v = f->ps[n];
    if (w & PS_PED) VIOL(f, "PED written as 1 (disables the port)");
    if ((v & PS_PP) && !(w & PS_PP)) VIOL(f, "port power dropped by a write");
    v &= ~(w & PS_CHG);
    f->ps[n] = v;
    if ((w & PS_PP) && !(v & PS_PP)) {
        f->ps[n] |= PS_PP;
        port_connect(f, n);
    }
    if (w & PS_PR) {
        f->port_resets++;
        if (usb3_port(n)) VIOL(f, "USB2 reset on a USB3 port");
        f->ps[n] |= PS_PR;
        f->pr_done[n] = f->pr_stuck ? ~0ull : f->now + 2000;
    }
    if (w & PS_WPR) {
        f->warm_resets++;
        if (!usb3_port(n)) VIOL(f, "warm reset on a USB2 port");
        if (f->port[n]) f->ps[n] |= PS_PED | PS_WRC | PS_PRC | ((uint32_t) f->port[n]->speed << 10);
    }
}

static void regs(fx_t *f)
{
    /* legacy support: the BIOS lets go 5 ms after the OS asks */
    uint32_t leg = rd(f, XECP);
    if (leg != f->pub_leg) {
        if ((leg & (1u << 24)) && !f->bios_req_at) f->bios_req_at = f->now + 5000;
        f->pub_leg = leg;
    }
    if ((leg & (1u << 16)) && (leg & (1u << 24)) && !f->bios_never && f->bios_req_at &&
        f->now >= f->bios_req_at) {
        leg &= ~(1u << 16);
        wr(f, XECP, leg);
        f->pub_leg = leg;
    }
    f->smi_ctl = rd(f, XECP + 4);

    /* USBSTS: write 1 to clear */
    uint32_t sw = rd(f, O_STS);
    if (sw != f->pub_sts) {
        if (sw & ~0x41Cu) VIOL(f, "USBSTS write with bits other than RW1C ones");
        f->sts &= ~(sw & 0x41Cu);
    }
    /* USBCMD */
    uint32_t cmd = rd(f, O_CMD);
    if (cmd & 2) {
        if (f->running) VIOL(f, "HCRST while running");
        if (!f->rst_pending) {
            f->rst_pending = 1;
            f->rst_at = f->now + 500;
        }
        if (!f->rst_stuck && f->now >= f->rst_at) {
            f->rst_pending = 0;
            hc_reset(f);
            wr(f, O_CMD, cmd & ~2u);
            cmd &= ~2u;
        }
    }
    if ((f->sts & 0x800) && !f->never_ready && f->now >= f->cnr_at) f->sts &= ~0x800u;
    if ((cmd & 1) && !f->running) {
        if (f->sts & 0x800) VIOL(f, "run while CNR");
        if (cmd & 4) VIOL(f, "interrupts enabled");
        f->running = 1;
        f->runs++;
        f->sts &= ~1u;
        if (f->hse) f->sts |= 4;
        uint64_t crcr = fk_rd64(f->r + O_CRCR);
        f->cr = crcr & ~0x3Full;
        f->ccs = crcr & 1;
        if (!f->ccs || !f->cr) VIOL(f, "CRCR");
        uint8_t *erst = (uint8_t *) fk_ptr(fk_rd64(f->r + R_ERSTBA));
        if (rd(f, R_ERSTSZ) != 1 || !erst)
            VIOL(f, "ERST");
        else {
            f->seg = fk_rd64(erst);
            f->seg_n = fk_rd32(erst + 8);
            if (f->seg_n < 16 || (f->seg & 63)) VIOL(f, "event ring segment");
            if ((fk_rd64(f->r + R_ERDP) & ~0xFull) != f->seg) VIOL(f, "ERDP not at segment");
        }
        f->eidx = 0;
        f->pcs = 1;
        if (rd(f, R_IMAN) & 2) VIOL(f, "interrupter enabled");
        uint64_t dcbaa = fk_rd64(f->r + O_DCBAAP);
        if (!dcbaa || (dcbaa & 63)) VIOL(f, "DCBAAP");
        uint32_t cfg = rd(f, O_CONFIG) & 0xFF;
        if (cfg == 0 || cfg > (uint32_t) f->maxslots) VIOL(f, "CONFIG MaxSlotsEn");
        if (f->scratch && dcbaa) {
            uint64_t *arr = (uint64_t *) fk_ptr(((uint64_t *) fk_ptr(dcbaa))[0]);
            if (!arr)
                VIOL(f, "no scratchpad array");
            else
                for (int i = 0; i < f->scratch; i++)
                    if (!arr[i] || (arr[i] & 0xFFF)) VIOL(f, "scratchpad buffer");
        }
    }
    if (!(cmd & 1) && f->running && !f->never_halts) {
        f->running = 0;
        f->sts |= 1;
    }
    /* ports */
    for (uint32_t n = 1; n <= NPORTS; n++) {
        uint32_t w = rd(f, O_PS(n));
        if (w != f->pub_ps[n]) {
            if (w & MARK) VIOL(f, "reserved PORTSC bit written back");
            port_write(f, n, w);
        }
        if ((f->ps[n] & PS_PR) && f->now >= f->pr_done[n]) {
            f->ps[n] &= ~PS_PR;
            if (f->ps[n] & PS_CCS)
                f->ps[n] |= PS_PED | PS_PRC | ((uint32_t) f->port[n]->speed << 10);
        }
        if (f->ps[n] & (PS_CSC | PS_PRC | PS_WRC)) f->sts |= 0x10; /* PCD */
        f->pub_ps[n] = f->ps[n] | MARK;
        wr(f, O_PS(n), f->pub_ps[n]);
    }
    f->pub_sts = f->sts | MARK;
    wr(f, O_STS, f->pub_sts);
}

static void tick(fake_dev_t *d, uint64_t now)
{
    fx_t *f = (fx_t *) d->state;
    f->now = now;
    regs(f);
    if (!f->running) return;
    if (rd(f, DBO) != SENT) { /* command doorbell */
        if (rd(f, DBO) != 0) VIOL(f, "command doorbell target not 0");
        f->doorbells++;
        wr(f, DBO, SENT);
        run_cmds(f);
    }
    for (uint32_t s = 1; s <= MAXSLOT; s++) {
        if (!f->slot[s].on) continue;
        uint32_t db = rd(f, DBO + 4 * s);
        if (db != SENT) {
            f->doorbells++;
            wr(f, DBO + 4 * s, SENT);
            if (db < 1 || db > 31)
                VIOL(f, "doorbell target");
            else {
                fep_t *e = &f->slot[s].ep[db];
                if (e->state == EP_STOP) set_state(f, s, db, EP_RUN);
                if (e->state == EP_RUN) e->armed = 1;
            }
        }
        for (uint32_t i = 1; i < 32; i++)
            if (f->slot[s].ep[i].armed) run_ep(f, s, i);
    }
}

static fx_t *add_xhci(fake_plat_t *fp, uint32_t bdf)
{
    fake_dev_t *d = fake_dev_new(fp, bdf, 0x1b36, 0x000d, 0x0C, 0x03, 0x30);
    fx_t *f = (fx_t *) calloc(1, sizeof *f);
    f->pd = d;
    f->r = fake_dev_bar(d, 0, BAR_SZ);
    f->csz = 32;
    f->maxslots = 64;
    f->fail_type = -1;
    d->state = f;
    d->tick = tick;
    g_fx = f;
    return f;
}

/* Write the capability registers and the firmware's leftover state from
 * the knobs; called after a test set them. */
static void power_on(fx_t *f)
{
    wr(f, 0x00, (0x0100u << 16) | CAPLEN);
    wr(f, 0x04, ((uint32_t) NPORTS << 24) | (1u << 8) | (uint32_t) f->maxslots);
    uint32_t sp = (uint32_t) f->scratch;
    wr(f, 0x08, ((sp & 0x1F) << 27) | ((sp >> 5) << 21));
    wr(f, 0x10, ((XECP / 4) << 16) | (f->csz == 64 ? 4u : 0u) | (f->ppc ? 8u : 0u) | 1u);
    wr(f, 0x14, DBO);
    wr(f, 0x18, RTS);
    wr(f, O_PGSZ, 1);
    /* legacy support (next cap 4 dwords on), USB 2 ports 1-2, USB 3 ports 3-4 */
    wr(f, XECP, 0x0401u | (f->bios_owned ? (1u << 16) : 0));
    wr(f, XECP + 4, 0x0000E011u | 0xE0000000u); /* SMIs enabled, some pending */
    wr(f, XECP + 0x10, 0x02000402u);
    wr(f, XECP + 0x18, 0x00000201u);
    wr(f, XECP + 0x20, 0x03000002u);
    wr(f, XECP + 0x28, 0x00000203u);
    f->pub_leg = rd(f, XECP);
    for (uint32_t s = 0; s <= MAXSLOT; s++) wr(f, DBO + 4 * s, SENT);
    /* the firmware left it running */
    wr(f, O_CMD, 1);
    f->running = 1;
    f->sts = 0;
    for (uint32_t n = 1; n <= NPORTS; n++) {
        f->ps[n] = f->ppc ? 0 : PS_PP;
        port_connect(f, n);
        f->pub_ps[n] = f->ps[n] | MARK;
        wr(f, O_PS(n), f->pub_ps[n]);
    }
    f->pub_sts = MARK;
    wr(f, O_STS, MARK);
}

static hw_verdict_t bind(fake_plat_t *fp, fx_t *f, hw_registry_t *r, hw_line_t *note)
{
    power_on(f);
    hw_pci_dev_t devs[4];
    uint32_t n = hw_pci_scan(&fp->plat, devs, 4);
    CHECK(n == 1);
    memset(r, 0, sizeof *r);
    hw_line_init(note);
    hw_verdict_t v = xhci_bind(&fp->plat, &devs[0], r, note);
    if (getenv("HWFAKE_VERBOSE")) printf("  note: %s\n", note->b);
    return v;
}

static void free_fx(fake_plat_t *fp, fx_t *f)
{
    fake_plat_free(fp);
    for (int i = 0; i <= NPORTS; i++) free_usb(f->port[i]);
    free(f);
}

/* Poll the keyboard until n events or 200 ms. */
static int kbd_events(fake_plat_t *fp, hw_input_t *in, hw_key_t *ev, int n)
{
    int got = 0;
    for (int t = 0; t < 200 && got < n; t++) {
        while (got < n && in->poll(in, &ev[got])) got++;
        fp->plat.delay_us(fp->plat.ctx, 1000);
    }
    return got;
}

static fslot_t *slot_of(fx_t *f, fusb_t *u)
{
    for (uint32_t i = 1; i <= MAXSLOT; i++)
        if (f->slot[i].on && f->slot[i].dev == u) return &f->slot[i];
    return 0;
}

/* ---- tests ------------------------------------------------------------------------ */
static void test_happy_path(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fx_t *f = add_xhci(&fp, HW_BDF(0, 4, 0));
    fusb_t *kb = f->port[1] = new_kbd(1);
    fusb_t *dk = f->port[3] = new_disk(4, 256, 512);
    f->bios_owned = 1;
    hw_registry_t r;
    hw_line_t note;
    CHECK(bind(&fp, f, &r, &note) == HW_T);
    CHECK(strstr(note.b, "QEMU xHCI 4 ports; ") == note.b);
    CHECK(strstr(note.b, "usbkbd") != 0);
    CHECK(strstr(note.b, "(USB DISK 256 x 512 B)") != 0);
    CHECK(strstr(note.b, "BIOS") == 0);
    CHECK(f->viol == 0);
    /* PCI: memory decode and bus mastering on, INTx off */
    CHECK((f->pd->cfg[1] & 0x0406) == 0x0406);
    /* handoff: OS owned, BIOS released, SMIs off and acknowledged */
    CHECK((rd(f, XECP) & (1u << 24)) != 0);
    CHECK((rd(f, XECP) & (1u << 16)) == 0);
    CHECK((f->smi_ctl & 0xE011u) == 0);
    CHECK((f->smi_ctl & 0xE0000000u) == 0xE0000000u);
    /* reset, configured, running, no interrupts */
    CHECK(f->resets == 1);
    CHECK(f->runs == 1);
    CHECK((rd(f, O_CONFIG) & 0xFF) == 32);
    CHECK(rd(f, O_CMD) == 1);
    CHECK((rd(f, R_IMAN) & 2) == 0);
    CHECK(rd(f, R_ERSTSZ) == 1);
    fp.plat.delay_us(fp.plat.ctx, 10); /* let the fake see the last register write */
    CHECK((f->sts & 0x10) == 0);       /* PCD acknowledged */
    /* ports: USB2 port reset once, USB3 port left to train */
    CHECK(f->port_resets == 1);
    CHECK(f->warm_resets == 0);
    CHECK((f->ps[1] & (PS_CSC | PS_PRC)) == 0);
    CHECK((f->ps[3] & PS_CSC) == 0);
    /* commands */
    CHECK(f->cmds[9] == 2 && f->cmds[11] == 2 && f->cmds[12] == 2);
    CHECK(f->cmds[13] == 0); /* keyboard MPS0 8 needs no Evaluate Context */
    CHECK(kb->addressed && dk->addressed);
    CHECK(kb->config == 1 && dk->config == 1);
    CHECK(kb->protocol == 0 && kb->idle == 0 && kb->leds == 0);
    fslot_t *ks = slot_of(f, kb), *ds = slot_of(f, dk);
    CHECK(ks && ds && ks != ds);
    if (!ks || !ds) goto out;
    CHECK(ks->root == 1 && ks->route == 0 && ks->speed == 1);
    CHECK(ds->root == 3 && ds->speed == 4);
    CHECK(ks->entries == 3); /* EP1 IN: dci 3; the mouse interface's EP2 IN is not configured */
    CHECK(ks->ep[5].state == EP_DIS);
    CHECK(ks->ep[3].state == EP_RUN);
    CHECK(ds->entries == 4); /* EP1 IN dci 3, EP2 OUT dci 4; UAS alt's EP3/EP4 not */
    CHECK(ds->ep[7].state == EP_DIS && ds->ep[8].state == EP_DIS);
    CHECK(ds->ep[3].mps == 1024 && ds->ep[4].mps == 1024);
    CHECK(ks->hub == 0 && ks->tt_slot == 0);

    CHECK(r.n_blk == 1 && r.n_input == 1);
    hw_blk_t *b = r.blk[0];
    CHECK(b->block_size == 512 && b->blocks == 256);
    CHECK(strncmp(b->name, "usb", 3) == 0 && b->name[3] >= '0' && b->name[3] <= '9');
    CHECK(dk->turs >= 1);
    static uint8_t buf[64 * 512];
    CHECK(b->read(b, 1, 1, buf) == 0);
    CHECK(memcmp(buf, "EFI PART", 8) == 0);
    uint32_t rd0 = dk->reads;
    CHECK(b->read(b, 10, 40, buf) == 0); /* 20 KB: two commands through the 16 KB bounce */
    CHECK(dk->reads - rd0 == 2);
    CHECK(memcmp(buf, dk->disk + 10 * 512, 40 * 512) == 0);
    CHECK(dk->last_op == 0x28);
    CHECK(b->read(b, 3, 3, buf + 1) == 0);
    CHECK(memcmp(buf + 1, dk->disk + 3 * 512, 3 * 512) == 0);
    uint8_t w[9 * 512];
    for (uint32_t i = 0; i < sizeof w; i++) w[i] = (uint8_t) (0x5A ^ i);
    CHECK(b->write(b, 100, 9, w) == 0);
    CHECK(dk->last_op == 0x2A);
    CHECK(memcmp(dk->disk + 100 * 512, w, sizeof w) == 0);
    memset(buf, 0, sizeof buf);
    CHECK(b->read(b, 100, 9, buf) == 0);
    CHECK(memcmp(buf, w, sizeof w) == 0);
    uint32_t cbws = dk->cbws;
    CHECK(b->read(b, 256, 1, buf) != 0);
    CHECK(b->read(b, 255, 2, buf) != 0);
    CHECK(b->write(b, ~0ULL, 1, buf) != 0);
    CHECK(dk->cbws == cbws);
    CHECK(b->read(b, 255, 1, buf) == 0);
    CHECK(dk->proto_err == 0 && dk->bot_resets == 0);

    hw_input_t *in = r.input[0];
    CHECK(strncmp(in->name, "usbkbd", 6) == 0);
    hw_key_t ev[16];
    CHECK(in->poll(in, &ev[0]) == 0); /* nothing typed yet */
    uint64_t t0 = f->now;
    kbd_push(kb, t0, 0, 0x04, 0);       /* a down */
    kbd_push(kb, t0, 0x02, 0x04, 0);    /* left shift down (a still held) */
    kbd_push(kb, t0, 0x02, 0x04, 0x1E); /* + '1' -> '!' */
    kbd_push(kb, t0, 0x02, 0x01, 0x01); /* rollover error: ignored, nothing released */
    kbd_push(kb, t0, 0, 0, 0);          /* all up */
    kbd_push(kb, t0, 0x20, 0x2C, 0x38); /* right shift, space, '/' -> '?' */
    int n = kbd_events(&fp, in, ev, 16);
    CHECK(n == 9);
    CHECK(ev[0].usage == 0x04 && ev[0].ascii == 'a' && ev[0].pressed == 1 && ev[0].mods == 0);
    CHECK(ev[1].usage == 0xE1 && ev[1].pressed == 1 && ev[1].ascii == 0 && ev[1].mods == 2);
    CHECK(ev[2].usage == 0x1E && ev[2].ascii == '!' && ev[2].pressed == 1);
    CHECK(ev[3].usage == 0xE1 && ev[3].pressed == 0);
    CHECK(ev[4].usage == 0x04 && ev[4].pressed == 0 && ev[4].ascii == 0);
    CHECK(ev[5].usage == 0x1E && ev[5].pressed == 0);
    CHECK(ev[6].usage == 0xE5 && ev[6].pressed == 1);
    CHECK(ev[7].usage == 0x2C && ev[7].ascii == ' ');
    CHECK(ev[8].usage == 0x38 && ev[8].ascii == '?' && ev[8].mods == 0x20);
    /* type "zxv\n" and a few unshifted symbols */
    static const uint8_t keys[] = {0x1D, 0x1B, 0x19, 0x28, 0x2D, 0x2E, 0x2F, 0x30, 0x31,
                                   0x33, 0x34, 0x35, 0x36, 0x37, 0x27, 0x2B, 0x2A, 0x29};
    static const char want[] = "zxv\n-=[]\\;'`,.0\t\b\x1b";
    uint64_t t1 = f->now;
    kbd_push(kb, t1, 0x20, 0, 0); /* release everything first */
    kbd_push(kb, t1, 0, 0, 0);
    for (uint32_t i = 0; i < sizeof keys; i++) {
        kbd_push(kb, t1 + i * 1000, 0, keys[i], 0);
        kbd_push(kb, t1 + i * 1000 + 500, 0, 0, 0);
    }
    char typed[32];
    uint32_t nt = 0;
    for (int t = 0; t < 300; t++) {
        hw_key_t e;
        while (in->poll(in, &e))
            if (e.pressed && e.ascii && nt < sizeof typed - 1) typed[nt++] = (char) e.ascii;
        fp.plat.delay_us(fp.plat.ctx, 1000);
    }
    typed[nt] = 0;
    CHECK(nt == sizeof keys);
    CHECK(memcmp(typed, want, sizeof keys) == 0);
    /* shifted row */
    static const uint8_t sk[] = {0x04, 0x1D, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
                                 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37};
    static const char sw[] = "AZ@#$%^&*(_+{}|:\"~<>";
    uint64_t t2 = f->now;
    for (uint32_t i = 0; i < sizeof sk; i++) {
        kbd_push(kb, t2 + i * 1000, 0x02, sk[i], 0);
        kbd_push(kb, t2 + i * 1000 + 500, 0x02, 0, 0);
    }
    kbd_push(kb, t2 + 30000, 0, 0, 0);
    nt = 0;
    for (int t = 0; t < 300; t++) {
        hw_key_t e;
        while (in->poll(in, &e))
            if (e.pressed && e.ascii && nt < sizeof typed - 1) typed[nt++] = (char) e.ascii;
        fp.plat.delay_us(fp.plat.ctx, 1000);
    }
    CHECK(nt == sizeof sk);
    CHECK(memcmp(typed, sw, sizeof sk) == 0);
    /* non-ASCII keys: F1 (0x3A), arrows; usage reported, ascii 0 */
    uint64_t t3 = f->now;
    kbd_push(kb, t3, 0, 0x3A, 0x4F);
    kbd_push(kb, t3, 0, 0, 0);
    n = kbd_events(&fp, in, ev, 4);
    CHECK(n == 4);
    CHECK(ev[0].usage == 0x3A && ev[0].ascii == 0 && ev[0].pressed == 1);
    CHECK(ev[1].usage == 0x4F && ev[1].ascii == 0);
    CHECK(ev[2].pressed == 0 && ev[3].pressed == 0);
    CHECK(f->viol == 0 && f->overflow == 0);
out:
    free_fx(&fp, f);
}

/* Many transfers: every ring wraps (64-TRB transfer and command rings, the
 * 256-entry event ring) several times; data stays right. Stalled CSWs make
 * the commands: each costs a Reset Endpoint and a Set TR Dequeue. */
static void test_ring_wrap(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fx_t *f = add_xhci(&fp, HW_BDF(0, 4, 0));
    fusb_t *dk = f->port[2] = new_disk(3, 128, 512);
    fusb_t *kb = f->port[1] = new_kbd(2); /* low speed */
    hw_registry_t r;
    hw_line_t note;
    CHECK(bind(&fp, f, &r, &note) == HW_T);
    CHECK((rd(f, XECP) & (1u << 24)) != 0); /* OS owned even with no BIOS to wait for */
    CHECK(r.n_blk == 1 && r.n_input == 1);
    if (r.n_blk != 1 || r.n_input != 1) goto out;
    hw_blk_t *b = r.blk[0];
    uint8_t buf[512];
    int bad = 0;
    for (int i = 0; i < 300; i++) {
        if (b->read(b, (uint64_t) (i % 128), 1, buf) != 0 ||
            memcmp(buf, dk->disk + (i % 128) * 512, 512) != 0)
            bad++;
    }
    CHECK(bad == 0);
    CHECK(dk->reads >= 300);
    for (int i = 0; i < 40; i++) {
        dk->stall_csw_once = 1;
        if (b->read(b, (uint64_t) i, 1, buf) != 0 || memcmp(buf, dk->disk + i * 512, 512) != 0)
            bad++;
    }
    CHECK(bad == 0);
    CHECK(f->cmds[14] == 40 && f->cmds[16] == 40);
    CHECK(dk->clear_halts == 40);
    hw_input_t *in = r.input[0];
    for (int i = 0; i < 150; i++) {
        kbd_push(kb, f->now, 0, 0x04, 0);
        kbd_push(kb, f->now, 0, 0, 0);
        hw_key_t ev[2];
        if (kbd_events(&fp, in, ev, 2) != 2 || ev[0].ascii != 'a' || ev[1].pressed) bad++;
        if (kb->rt - kb->rh > 60) break;
    }
    CHECK(bad == 0);
    CHECK(f->evts > 512);
    CHECK(f->viol == 0 && f->overflow == 0);
    fslot_t *ks = slot_of(f, kb);
    CHECK(ks && ks->speed == 2 && ks->ep[1].mps == 8);
out:
    free_fx(&fp, f);
}

/* 64-byte contexts, scratchpad buffers, software port power, a USB3 link
 * that needs a warm reset, a full-speed disk with MPS0 64. */
static void test_variants(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fx_t *f = add_xhci(&fp, HW_BDF(0, 4, 0));
    f->csz = 64;
    f->scratch = 37; /* spans both halves of the HCSPARAMS2 field */
    f->ppc = 1;
    f->no_train = 1;
    f->maxslots = 8;
    fusb_t *dk = f->port[4] = new_disk(4, 64, 4096);
    fusb_t *fs = f->port[2] = new_disk(1, 64, 512);
    fs->mps0 = 64;
    fs->dd[7] = 64;
    hw_registry_t r;
    hw_line_t note;
    CHECK(bind(&fp, f, &r, &note) == HW_T);
    CHECK(f->viol == 0);
    CHECK((rd(f, O_CONFIG) & 0xFF) == 8);
    CHECK((f->ps[1] & PS_PP) && (f->ps[2] & PS_PP) && (f->ps[3] & PS_PP) && (f->ps[4] & PS_PP));
    CHECK(f->warm_resets == 1);
    CHECK(f->cmds[13] == 1); /* Evaluate Context for the full-speed disk */
    fslot_t *fsl = slot_of(f, fs);
    CHECK(fsl && fsl->ep[1].mps == 64);
    CHECK(r.n_blk == 2);
    CHECK(strstr(note.b, " x 4096 B)") != 0);
    CHECK(strstr(note.b, "64 x 512 B)") != 0);
    for (uint32_t i = 0; i < r.n_blk; i++) {
        hw_blk_t *b = r.blk[i];
        static uint8_t buf[8 * 4096];
        fusb_t *u = b->block_size == 4096 ? dk : fs;
        CHECK(b->read(b, 1, 1, buf) == 0);
        CHECK(memcmp(buf, "EFI PART", 8) == 0);
        CHECK(b->read(b, 2, 8, buf) == 0); /* 32 KB of 4 KB blocks: two commands */
        CHECK(memcmp(buf, u->disk + 2 * u->bs, 8 * u->bs) == 0);
    }
    CHECK(f->viol == 0);
    free_fx(&fp, f);
}

/* Intel Panther Point: ports are switched from the EHCI companion to xHCI
 * through config space; other Intel parts are left alone. */
static void test_intel_routing(void)
{
    for (int i = 0; i < 2; i++) {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fx_t *f = add_xhci(&fp, HW_BDF(0, 0x14, 0));
        f->pd->cfg[0] = ((uint32_t) (i ? 0xA12F : 0x1E31) << 16) | 0x8086;
        f->pd->cfg[0xDC / 4] = 0x0000000F; /* USB3PRM */
        f->pd->cfg[0xD4 / 4] = 0x00007FFF; /* XUSB2PRM */
        f->port[1] = new_kbd(1);
        hw_registry_t r;
        hw_line_t note;
        CHECK(bind(&fp, f, &r, &note) == HW_T);
        CHECK(strstr(note.b, "Intel xHCI 4 ports; usbkbd") == note.b);
        CHECK(f->pd->cfg[0xD8 / 4] == (i ? 0u : 0x0000000Fu));
        CHECK(f->pd->cfg[0xD0 / 4] == (i ? 0u : 0x00007FFFu));
        CHECK(f->viol == 0);
        free_fx(&fp, f);
    }
}

/* One level of hubs: a high-speed hub on a USB2 port with a full-speed
 * keyboard (needs the hub's transaction translator) and a high-speed disk. */
static void test_hub(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fx_t *f = add_xhci(&fp, HW_BDF(0, 4, 0));
    fusb_t *h = f->port[2] = new_hub(3);
    fusb_t *kb = h->child[1] = new_kbd(1);
    fusb_t *dk = h->child[3] = new_disk(3, 64, 512);
    fusb_t *h2 = h->child[4] = new_hub(3); /* second level: not descended */
    h2->child[1] = new_kbd(1);
    hw_registry_t r;
    hw_line_t note;
    CHECK(bind(&fp, f, &r, &note) == HW_T);
    CHECK(f->viol == 0);
    CHECK(strstr(note.b, "hub(4): ") != 0);
    CHECK(strstr(note.b, "1 other") != 0);
    CHECK(r.n_input == 1 && r.n_blk == 1);
    fslot_t *hs = slot_of(f, h), *ks = slot_of(f, kb), *ds = slot_of(f, dk);
    CHECK(hs && ks && ds);
    if (!hs || !ks || !ds) goto out;
    CHECK(hs->hub == 1 && hs->nports == 4 && hs->ttt == 0);
    CHECK(h->hub_powered == 0x1E);
    CHECK(h->hub_resets == 3);
    CHECK((h->pst[1] >> 16) == 0 && (h->pst[3] >> 16) == 0); /* changes acknowledged */
    CHECK(ks->route == 1 && ks->root == 2 && ks->speed == 1);
    uint32_t hslot = (uint32_t) (hs - f->slot);
    CHECK(ks->tt_slot == hslot && ks->tt_port == 1);
    CHECK(ds->route == 3 && ds->root == 2 && ds->speed == 3);
    CHECK(ds->tt_slot == 0 && ds->tt_port == 0);
    CHECK(h2->child[1]->addressed == 0);
    uint8_t buf[512];
    CHECK(r.blk[0]->read(r.blk[0], 1, 1, buf) == 0 && memcmp(buf, "EFI PART", 8) == 0);
    kbd_push(kb, f->now, 0, 0x1D, 0);
    hw_key_t ev;
    CHECK(kbd_events(&fp, r.input[0], &ev, 1) == 1 && ev.ascii == 'z');
    CHECK(f->viol == 0);
out:
    free_fx(&fp, f);
}

typedef struct {
    const char *what;
    void (*breakit)(fx_t *f);
    hw_verdict_t want;
    const char *note;
    uint32_t blk, inputs;
} fault_t;

static void k_never_halts(fx_t *f)
{
    f->never_halts = 1;
}
static void k_never_ready(fx_t *f)
{
    f->never_ready = 1;
}
static void k_rst_stuck(fx_t *f)
{
    f->rst_stuck = 1;
}
static void k_bios_never(fx_t *f)
{
    f->bios_owned = f->bios_never = 1;
}
static void k_hse(fx_t *f)
{
    f->hse = 1;
}
static void k_dead(fx_t *f)
{
    f->dead_cmds = 1;
}
static void k_en_slot(fx_t *f)
{
    f->fail_type = 9;
    f->fail_cc = 9;
}
static void k_addr(fx_t *f)
{
    f->fail_type = 11;
    f->fail_cc = 4;
}
static void k_cfg(fx_t *f)
{
    f->fail_type = 12;
    f->fail_cc = 12;
}
static void k_scratch(fx_t *f)
{
    f->scratch = 900;
}
static void k_pr_stuck(fx_t *f)
{
    f->pr_stuck = 1;
}
static void k_nodev(fx_t *f)
{
    free_usb(f->port[1]);
    free_usb(f->port[3]);
    f->port[1] = f->port[3] = 0;
}
static void k_idle(fx_t *f)
{
    f->port[1]->stall_req = 0x0A;
}
static void k_proto(fx_t *f)
{
    f->port[1]->stall_req = 0x0B;
}
static void k_setcfg(fx_t *f)
{
    f->port[3]->stall_req = 9;
}
static void k_desc(fx_t *f)
{
    f->port[1]->stall_req = 6;
}
static void k_nak(fx_t *f)
{
    f->port[1]->nak_ctrl = 1;
}
static void k_mps0(fx_t *f)
{
    f->port[1]->dd[7] = 7;
}
static void k_not_ready(fx_t *f)
{
    f->port[3]->not_ready = 1;
}
static void k_no_medium(fx_t *f)
{
    f->port[3]->no_medium = 1;
}
static void k_ua(fx_t *f)
{
    f->port[3]->ua_once = 1;
}
static void k_short_csw(fx_t *f)
{
    f->port[3]->short_csw_once = 1;
}
static void k_phase(fx_t *f)
{
    f->port[3]->phase_err_once = 1;
}
static void k_stall_csw(fx_t *f)
{
    f->port[3]->stall_csw_once = 1;
}
static void k_cdrom(fx_t *f)
{
    f->port[3]->inq_type = 5;
}
static void k_bs(fx_t *f)
{
    f->port[3]->bs = 1000;
}
static void k_nobulk(fx_t *f)
{
    f->port[3]->cfg[21] = f->port[3]->cfg[28] = 3;
} /* interrupt eps */
static void k_other(fx_t *f)
{
    free_usb(f->port[3]);
    f->port[3] = new_other();
}
static void k_bulknak(fx_t *f)
{
    f->port[3]->nak_bulk = 1;
}
static void k_short_dd(fx_t *f)
{
    f->port[3]->short_dd = 1;
}
static void k_bad_tag(fx_t *f)
{
    f->port[3]->bad_tag_once = 1;
}
static void k_caplen16(fx_t *f)
{
    wr(f, 0, 0x01000010u);
}
static void k_spinup(fx_t *f)
{
    f->port[3]->spinup = 30;
}
static void k_spinup_long(fx_t *f)
{
    f->port[3]->spinup = 100;
}

static void k_caplen(fx_t *f)
{
    wr(f, 0, 0x01000000u);
}
static void k_ports0(fx_t *f)
{
    wr(f, 4, rd(f, 4) & 0x00FFFFFFu);
}
static void k_slots0(fx_t *f)
{
    wr(f, 4, rd(f, 4) & ~0xFFu);
}
static void k_pgsz(fx_t *f)
{
    wr(f, O_PGSZ, 2);
}
static void k_dboff(fx_t *f)
{
    wr(f, 0x14, 0x3FF0);
}
static void k_rtsoff(fx_t *f)
{
    wr(f, 0x18, 0x4000);
}
static void k_ones(fx_t *f)
{
    memset(f->r, 0xFF, 32);
}

/* Knobs that act after power_on wrote the capability registers. */
static void (*const late[])(fx_t *) = {k_caplen, k_caplen16, k_ports0, k_slots0,
                                       k_pgsz,   k_dboff,    k_rtsoff, k_ones};

static int is_late(void (*k)(fx_t *))
{
    for (uint32_t i = 0; i < sizeof late / sizeof late[0]; i++)
        if (late[i] == k) return 1;
    return 0;
}

static void test_faults(void)
{
    static const fault_t faults[] = {
        {"never halts", k_never_halts, HW_F, "controller did not halt", 0, 0},
        {"never ready", k_never_ready, HW_F, "controller never ready", 0, 0},
        {"HCRST stuck", k_rst_stuck, HW_F, "reset timed out", 0, 0},
        {"BIOS keeps ownership", k_bios_never, HW_T, "(BIOS kept ownership, taken)", 1, 1},
        {"host system error", k_hse, HW_F, "host system or controller error", 0, 0},
        {"commands never complete", k_dead, HW_F, "command ring stopped", 0, 0},
        {"Enable Slot fails", k_en_slot, HW_T, "port 1: Enable Slot failed (cc 9)", 0, 0},
        {"Address Device fails", k_addr, HW_T, "port 3: Address Device failed (cc 4)", 0, 0},
        {"Configure Endpoint fails", k_cfg, HW_T, "Configure Endpoint failed (cc 12)", 0, 0},
        {"too many scratchpads", k_scratch, HW_F, "too many scratchpad buffers", 0, 0},
        {"USB2 port reset stuck", k_pr_stuck, HW_T, "port 1: reset failed", 1, 0},
        {"no devices", k_nodev, HW_T, "4 ports; no USB devices", 0, 0},
        {"SET_IDLE stalls", k_idle, HW_T, "usbkbd", 1, 1},
        {"SET_PROTOCOL stalls", k_proto, HW_T, "keyboard refused the boot protocol", 1, 0},
        {"SET_CONFIGURATION stalls", k_setcfg, HW_T, "SET_CONFIGURATION failed", 0, 1},
        {"GET_DESCRIPTOR stalls", k_desc, HW_T, "port 1: no device descriptor", 1, 0},
        {"control NAKs forever", k_nak, HW_T, "port 1: no device descriptor", 1, 0},
        {"bad MPS0", k_mps0, HW_T, "port 1: bad bMaxPacketSize0", 1, 0},
        {"disk not ready", k_not_ready, HW_T, "(USB DISK, not ready)", 0, 1},
        {"no medium", k_no_medium, HW_T, "(USB DISK, no medium)", 0, 1},
        {"unit attention once", k_ua, HW_T, "(USB DISK 256 x 512 B)", 1, 1},
        {"short CSW once", k_short_csw, HW_T, "(USB DISK 256 x 512 B)", 1, 1},
        {"phase error once", k_phase, HW_T, "(USB DISK 256 x 512 B)", 1, 1},
        {"CSW stalls once", k_stall_csw, HW_T, "(USB DISK 256 x 512 B)", 1, 1},
        {"not a disk", k_cdrom, HW_T, "not a direct-access device", 0, 1},
        {"odd block size", k_bs, HW_T, "unsupported block size", 0, 1},
        {"interface without endpoints", k_nobulk, HW_T, "lacks bulk endpoints", 0, 1},
        {"unsupported device", k_other, HW_T, "1 other", 0, 1},
        {"bulk NAKs forever", k_bulknak, HW_T, "INQUIRY failed", 0, 1},
        {"CAPLENGTH 0", k_caplen, HW_B, "CAPLENGTH", 0, 0},
        {"CAPLENGTH 0x10", k_caplen16, HW_B, "CAPLENGTH below 0x20", 0, 0},
        {"disk spins up for 3 s", k_spinup, HW_T, "(USB DISK 256 x 512 B)", 1, 1},
        {"disk never finishes spinning up", k_spinup_long, HW_T, "(USB DISK, not ready)", 0, 1},
        {"short device descriptor", k_short_dd, HW_T, "port 3: no device descriptor", 0, 1},
        {"CSW with the wrong tag", k_bad_tag, HW_T, "(USB DISK 256 x 512 B)", 1, 1},
        {"MaxPorts 0", k_ports0, HW_B, "MaxPorts or MaxSlots is 0", 0, 0},
        {"MaxSlots 0", k_slots0, HW_B, "MaxPorts or MaxSlots is 0", 0, 0},
        {"no 4 KB pages", k_pgsz, HW_B, "no 4 KB page size", 0, 0},
        {"doorbells outside BAR", k_dboff, HW_B, "outside BAR0", 0, 0},
        {"runtime outside BAR", k_rtsoff, HW_B, "outside BAR0", 0, 0},
        {"all-ones", k_ones, HW_F, "all-ones", 0, 0},
    };
    for (uint32_t i = 0; i < sizeof faults / sizeof faults[0]; i++) {
        const fault_t *ft = &faults[i];
        fake_plat_t fp;
        fake_plat_init(&fp);
        fx_t *f = add_xhci(&fp, HW_BDF(0, 4, 0));
        f->port[1] = new_kbd(1);
        f->port[3] = new_disk(3, 256, 512);
        if (!is_late(ft->breakit)) ft->breakit(f);
        hw_registry_t r;
        hw_line_t note;
        hw_verdict_t v;
        if (is_late(ft->breakit)) {
            power_on(f);
            ft->breakit(f);
            hw_pci_dev_t devs[2];
            CHECK(hw_pci_scan(&fp.plat, devs, 2) == 1);
            memset(&r, 0, sizeof r);
            hw_line_init(&note);
            v = xhci_bind(&fp.plat, &devs[0], &r, &note);
        } else {
            v = bind(&fp, f, &r, &note);
        }
        int ok = v == ft->want && strstr(note.b, ft->note) && r.n_blk == ft->blk &&
                 r.n_input == ft->inputs && f->viol == 0;
        if (!ok)
            printf("  fault '%s': verdict %d note '%s' blk %u input %u viol %u\n", ft->what, v,
                   note.b, r.n_blk, r.n_input, f->viol);
        CHECK(v == ft->want);
        CHECK(strstr(note.b, ft->note) != 0);
        CHECK(r.n_blk == ft->blk);
        CHECK(r.n_input == ft->inputs);
        CHECK(f->viol == 0);
        CHECK(fp.now_us < 30000000ull); /* bounded: never hangs the boot */
        /* per-fault extra checks */
        fusb_t *kb = f->port[1], *dk = f->port[3];
        if (ft->breakit == k_bios_never) CHECK((rd(f, XECP) & (1u << 16)) == 0);
        if (ft->breakit == k_idle) {
            fslot_t *ks = slot_of(f, kb);
            CHECK(ks && ks->ep[1].resets == 1); /* EP0 halted by the stall, reset */
            CHECK(kb->protocol == 0);
            CHECK(kb->leds == 0 && f->cmds[15] == 0); /* the next request went through */
        }
        if (ft->breakit == k_nak) CHECK(f->cmds[15] >= 1); /* Stop Endpoint after the timeout */
        if (ft->breakit == k_ua) CHECK(dk->turs == 2 && dk->senses == 1);
        if (ft->breakit == k_not_ready) CHECK(dk->turs == 8);
        if (ft->breakit == k_no_medium) CHECK(dk->turs == 1);
        if (ft->breakit == k_short_csw || ft->breakit == k_phase) {
            CHECK(dk->bot_resets == 1);
            CHECK(dk->clear_halts == 2);
            fslot_t *ds = slot_of(f, dk);
            CHECK(ds && ds->ep[3].readds + ds->ep[3].resets == 1 && ds->ep[4].readds == 1);
        }
        if (ft->breakit == k_spinup) CHECK(dk->turs == 31);
        if (ft->breakit == k_spinup_long) CHECK(dk->turs == 80);
        if (ft->breakit == k_bad_tag) CHECK(dk->bot_resets == 1 && dk->clear_halts == 2);
        if (ft->breakit == k_stall_csw) {
            CHECK(dk->bot_resets == 0 && dk->clear_halts == 1);
            fslot_t *ds = slot_of(f, dk);
            CHECK(ds && ds->ep[3].resets == 1);
        }
        if (ft->breakit == k_bulknak) CHECK(dk->bot_resets >= 1 && f->cmds[15] >= 1);
        free_fx(&fp, f);
    }
}

/* Faults that hit after bring-up: a stalled read, a stalled interrupt
 * endpoint, a disk larger than READ CAPACITY(10) can say. */
static void test_runtime_faults(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fx_t *f = add_xhci(&fp, HW_BDF(0, 4, 0));
    fusb_t *kb = f->port[1] = new_kbd(1);
    fusb_t *dk = f->port[3] = new_disk(4, 256, 512);
    dk->cap_blocks = 0x200000010ull; /* 8 GB of 512 B... times 4: needs (16) */
    hw_registry_t r;
    hw_line_t note;
    CHECK(bind(&fp, f, &r, &note) == HW_T);
    CHECK(strstr(note.b, "8589934608 x 512 B") != 0);
    CHECK(r.n_blk == 1 && r.n_input == 1);
    if (r.n_blk != 1 || r.n_input != 1) goto out;
    hw_blk_t *b = r.blk[0];
    CHECK(b->blocks == 0x200000010ull);
    uint8_t buf[1024];
    /* above 2^32: READ(16); the fake maps it to LBA 0x10 % 256 */
    CHECK(b->read(b, 0x100000010ull, 1, buf) == 0);
    CHECK(dk->last_op == 0x88);
    CHECK(memcmp(buf, dk->disk + 0x10 * 512, 512) == 0);
    CHECK(b->write(b, 0x100000020ull, 1, buf) == 0);
    CHECK(dk->last_op == 0x8A);
    CHECK(memcmp(dk->disk + 0x20 * 512, buf, 512) == 0);
    CHECK(b->read(b, 5, 1, buf) == 0 && dk->last_op == 0x28);
    /* a read whose data stage stalls fails once; the halt is cleared on both
     * sides and the next read works */
    dk->stall_read_once = 1;
    uint32_t ch = dk->clear_halts;
    CHECK(b->read(b, 7, 2, buf) != 0);
    CHECK(dk->clear_halts == ch + 1);
    CHECK(b->read(b, 7, 2, buf) == 0);
    CHECK(memcmp(buf, dk->disk + 7 * 512, 1024) == 0);
    CHECK(dk->bot_resets == 0);
    /* fewer bytes than asked with a passing CSW is still a failed read */
    dk->short_read_once = 1;
    CHECK(b->read(b, 9, 2, buf) != 0);
    CHECK(b->read(b, 9, 2, buf) == 0);
    CHECK(memcmp(buf, dk->disk + 9 * 512, 1024) == 0);
    /* a stalled interrupt endpoint is cleared and the keyboard carries on */
    kb->stall_intr_once = 1;
    kbd_push(kb, f->now, 0, 0x05, 0);
    kbd_push(kb, f->now, 0, 0, 0);
    hw_key_t ev[2];
    CHECK(kbd_events(&fp, r.input[0], ev, 2) == 2);
    CHECK(ev[0].ascii == 'b' && ev[1].pressed == 0);
    CHECK(kb->clear_halts == 1);
    /* a keyboard whose endpoint keeps failing is given up, poll stays safe */
    for (int i = 0; i < 12; i++) {
        kb->stall_intr_once = 1;
        kbd_push(kb, f->now, 0, 0x06, 0);
        hw_key_t e;
        for (int t = 0; t < 5; t++) {
            r.input[0]->poll(r.input[0], &e);
            fp.plat.delay_us(fp.plat.ctx, 1000);
        }
    }
    uint32_t reqs = kb->ctrl_reqs;
    for (int t = 0; t < 50; t++) {
        hw_key_t e;
        r.input[0]->poll(r.input[0], &e);
        fp.plat.delay_us(fp.plat.ctx, 1000);
    }
    CHECK(kb->ctrl_reqs == reqs); /* stopped: no more clear-halt attempts */
    kb->stall_intr_once = 1;
    kbd_push(kb, f->now, 0, 0x07, 0);
    hw_key_t e2;
    int got = 0;
    for (int t = 0; t < 50; t++) {
        got += r.input[0]->poll(r.input[0], &e2);
        fp.plat.delay_us(fp.plat.ctx, 1000);
    }
    CHECK(kb->ctrl_reqs == reqs && kb->clear_halts <= 9);
    CHECK(got <= 1);
    CHECK(f->viol == 0);
out:
    free_fx(&fp, f);
}

static void test_no_bar_and_dma_exhaustion(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fake_dev_new(&fp, HW_BDF(0, 4, 0), 0x1b36, 0x000d, 0x0C, 0x03, 0x30);
    hw_pci_dev_t devs[2];
    CHECK(hw_pci_scan(&fp.plat, devs, 2) == 1);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    hw_line_init(&note);
    CHECK(xhci_bind(&fp.plat, &devs[0], &r, &note) == HW_N);
    CHECK(strstr(note.b, "BAR0") != 0);
    fake_plat_free(&fp);

    /* every allocation in turn fails: never a crash; the controller fails
     * while its own memory is short, devices fail on their own after that */
    uint32_t ctrl_f = 0, dev_f = 0;
    for (uint32_t k = 1; k <= 40; k++) {
        fake_plat_init(&fp);
        fx_t *f = add_xhci(&fp, HW_BDF(0, 4, 0));
        f->port[1] = new_kbd(1);
        f->port[3] = new_disk(3, 64, 512);
        f->scratch = 2;
        fp.dma_fail_after = k;
        hw_verdict_t v = bind(&fp, f, &r, &note);
        CHECK(v == HW_F || v == HW_T);
        CHECK(strstr(note.b, "DMA") != 0 || (r.n_blk == 1 && r.n_input == 1));
        if (v == HW_F)
            ctrl_f++;
        else if (strstr(note.b, "DMA"))
            dev_f++;
        CHECK(f->viol == 0);
        free_fx(&fp, f);
    }
    CHECK(ctrl_f == 8); /* state, DCBAA, ERST, event ring, command ring, array, 2 pads */
    CHECK(dev_f >= 10);
}

/* The whole path the kernel takes: hw_probe_all, then the boot self-tests,
 * with keys typed while the input self-test polls. */
static void test_probe_and_selftest(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fx_t *f = add_xhci(&fp, HW_BDF(0, 4, 0));
    fusb_t *kb = f->port[1] = new_kbd(1);
    f->port[4] = new_disk(4, 64, 512);
    power_on(f);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_probe_stats_t st = hw_probe_all(&fp.plat, &r);
    CHECK(st.functions == 1 && st.bound == 1 && st.failed == 0);
    CHECK(fake_log_has(&fp, "[DRIVER ONLINE] xhci 00:04.0 1b36:000d QEMU xHCI 4 ports; usbkbd"));
    /* "zxv" typed half a second into the 2 s input self-test (the disk
     * self-test before it takes a few ms) */
    uint64_t t = f->now + 500000;
    kbd_push(kb, t, 0, 0x1D, 0);
    kbd_push(kb, t + 80000, 0, 0, 0);
    kbd_push(kb, t + 160000, 0, 0x1B, 0);
    kbd_push(kb, t + 240000, 0, 0, 0);
    kbd_push(kb, t + 320000, 0, 0x19, 0);
    kbd_push(kb, t + 400000, 0, 0, 0);
    CHECK(hw_selftest_all(&fp.plat, &r) == 2);
    CHECK(fake_log_has(&fp, "read LBA 1: GPT header 'EFI PART'; 64 blocks of 512"));
    CHECK(fake_log_has(&fp, "polling; 3 key press(es): \"zxv\""));
    CHECK(f->viol == 0);
    free_fx(&fp, f);
}

int main(void)
{
    test_happy_path();
    test_ring_wrap();
    test_variants();
    test_hub();
    test_intel_routing();
    test_faults();
    test_runtime_faults();
    test_no_bar_and_dma_exhaustion();
    test_probe_and_selftest();
    printf("test_xhci: %d checks, %d failures\n", fk_checks, fk_failures);
    return fk_failures ? 1 : 0;
}
