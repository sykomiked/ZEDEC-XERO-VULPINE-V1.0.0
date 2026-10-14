/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_hda.c — hda.c against a register-level fake HD Audio controller with
 * fake codecs.
 *
 * The controller: GCTL.CRST, STATESTS (W1C) after reset, CORB/RIRB in host
 * "DMA" memory with the selectable sizes, the CORBRP reset handshake, RIRBWP
 * reset, and QEMU's response pacing (after RINTCNT responses the CORB stalls
 * until RIRBSTS.RINTFL, raised only when RIRBCTL.RINTCTL is set, is cleared).
 * Stream descriptors: SRST, RUN, CBL/LVI/BDL/FMT validation, and LPIB moving
 * at 192 bytes per ms only while an output stream runs and a codec converter
 * is bound to its stream tag, as QEMU's codec-paced transfer does. The bytes
 * the stream consumes are captured so tests can compare them with the PCM.
 *
 * The codecs: root node, function groups and a widget graph (converters,
 * mixers, selectors, pins, connection lists in short/long form with ranges),
 * keeping the state every set verb writes so a test can walk the programmed
 * path the way the signal would. Knobs break the controller and codecs every
 * way the driver must survive. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hwfake.h"

#define NW  64
#define NSD 8

typedef struct fw {
    uint32_t wcaps, pincaps, cfg, outamp, inamp, pcm;
    uint16_t raw[16];
    uint8_t nraw;
    int lng;
    uint8_t conn[40];
    uint8_t nconn;
    /* state */
    uint8_t power, sel, pinctl, eapd, strm;
    uint16_t fmt;
    uint8_t out_gain[2], out_mute[2];
    uint8_t in_gain[16][2], in_mute[16][2];
    uint32_t sel_verbs;
} fw_t;

typedef struct fcodec {
    int present, silent, unsol;
    uint32_t vendor;
    uint8_t fg_start, fg_count, afg;
    uint32_t fg_type[4]; /* bit 8: unsolicited capable */
    uint8_t wstart, wcount;
    uint32_t afg_outamp, afg_inamp, afg_pcm;
    uint8_t afg_power;
    fw_t w[256];
    uint32_t verbs, bad_gain, bad_sel;
} fcodec_t;

typedef struct fhda {
    uint8_t *r;
    fcodec_t c[15];
    /* knobs */
    int crst_stuck_low, crst_stuck_high, corb_dead, rirb_dead, corbrp_stuck;
    int corb_run_stuck, corb_never_runs;
    uint32_t drop_verb; /* the Nth verb (1-based) gets no response */
    int srst_never_set, srst_never_clear;
    uint32_t codec_delay_us;
    /* state */
    int in_reset, sts_given, short_reset;
    uint64_t reset_at;
    uint64_t out_at;
    uint32_t statests, pub_statests, rirbsts, pub_rirbsts;
    uint32_t corb_rp, rirb_wp, rirb_count, corb_n, rirb_n;
    int corbrp_rst;
    uint32_t lpib[NSD], frac[NSD];
    int running[NSD], srst[NSD];
    uint64_t last[NSD];
    /* observations */
    uint8_t played[1 << 18];
    uint32_t played_n, resets, verbs, unsol_sent, bad_stream, starts;
    int sd_started;
} fhda_t;

static uint32_t rd32(fhda_t *f, uint32_t o)
{
    return fk_rd32(f->r + o);
}
static void wr32(fhda_t *f, uint32_t o, uint32_t v)
{
    fk_wr32(f->r + o, v);
}
static uint32_t rd16(fhda_t *f, uint32_t o)
{
    return (uint32_t) f->r[o] | ((uint32_t) f->r[o + 1] << 8);
}
static void wr16(fhda_t *f, uint32_t o, uint32_t v)
{
    f->r[o] = (uint8_t) v;
    f->r[o + 1] = (uint8_t) (v >> 8);
}
static uint32_t ISS(fhda_t *f)
{
    return (rd16(f, 0) >> 8) & 0xF;
}
static uint32_t SDO(uint32_t i)
{
    return 0x80 + i * 0x20;
}

/* ---- fake codec ------------------------------------------------------------ */
static fw_t *wid(fcodec_t *c, uint32_t nid)
{
    if (nid >= c->wstart && nid < (uint32_t) c->wstart + c->wcount) return &c->w[nid];
    return 0;
}

static void expand(fw_t *w)
{
    w->nconn = 0;
    uint32_t prev = 0;
    for (uint32_t i = 0; i < w->nraw; i++) {
        uint32_t e = w->raw[i], rb = w->lng ? 0x8000 : 0x80, id = e & (w->lng ? 0x7FFF : 0x7F);
        if ((e & rb) && prev)
            for (uint32_t x = prev + 1; x <= id; x++) w->conn[w->nconn++] = (uint8_t) x;
        else
            w->conn[w->nconn++] = (uint8_t) id;
        prev = id;
    }
}

static void set_conns(fw_t *w, int lng, uint32_t n, const uint16_t *raw)
{
    w->wcaps |= 1u << 8;
    w->lng = lng;
    w->nraw = (uint8_t) n;
    memcpy(w->raw, raw, n * sizeof raw[0]);
    expand(w);
}

static uint32_t amp_steps(fcodec_t *c, fw_t *w, int out)
{
    uint32_t caps =
        (w->wcaps & 8) ? (out ? w->outamp : w->inamp) : (out ? c->afg_outamp : c->afg_inamp);
    return (caps >> 8) & 0x7F;
}

static uint32_t codec_verb(fcodec_t *c, uint32_t v)
{
    uint32_t nid = (v >> 20) & 0xFF, id4 = (v >> 16) & 0xF, id, pl;
    if (id4 == 0x7 || id4 == 0xF) {
        id = (v >> 8) & 0xFFF;
        pl = v & 0xFF;
    } else {
        id = id4;
        pl = v & 0xFFFF;
    }
    c->verbs++;
    if (nid == 0) {
        if (id != 0xF00) return 0;
        if (pl == 0) return c->vendor;
        if (pl == 2) return 0x00100101;
        if (pl == 4) return ((uint32_t) c->fg_start << 16) | c->fg_count;
        return 0;
    }
    if (nid >= c->fg_start && nid < (uint32_t) c->fg_start + c->fg_count) {
        uint32_t i = nid - c->fg_start;
        if (id == 0xF00) {
            if (pl == 5) return c->fg_type[i];
            if (nid != c->afg) return 0;
            if (pl == 4) return ((uint32_t) c->wstart << 16) | c->wcount;
            if (pl == 0x12) return c->afg_outamp;
            if (pl == 0x0D) return c->afg_inamp;
            if (pl == 0x0A) return c->afg_pcm;
            return 0;
        }
        if (id == 0x705 && nid == c->afg) c->afg_power = (uint8_t) (pl & 0xF);
        if (id == 0xF05) return c->afg_power;
        return 0;
    }
    fw_t *w = wid(c, nid);
    if (!w) return 0;
    switch (id) {
    case 0xF00:
        if (pl == 9) return w->wcaps;
        if (pl == 0x0C) return w->pincaps;
        if (pl == 0x0E) return w->nraw | (w->lng ? 0x80u : 0);
        if (pl == 0x12) return (w->wcaps & 8) ? w->outamp : 0;
        if (pl == 0x0D) return (w->wcaps & 8) ? w->inamp : 0;
        if (pl == 0x0A) return (w->wcaps & 16) ? w->pcm : 0;
        return 0;
    case 0xF02: {
        uint32_t r = 0, per = w->lng ? 2 : 4, bits = w->lng ? 16 : 8;
        for (uint32_t j = 0; j < per && pl + j < w->nraw; j++)
            r |= (uint32_t) w->raw[pl + j] << (j * bits);
        return r;
    }
    case 0x701:
        w->sel_verbs++;
        if (pl >= w->nconn) c->bad_sel++;
        w->sel = (uint8_t) pl;
        return 0;
    case 0xF01:
        return w->sel;
    case 0x705:
        w->power = (uint8_t) (pl & 0xF);
        return 0;
    case 0xF05:
        return w->power;
    case 0x706:
        w->strm = (uint8_t) pl;
        return 0;
    case 0xF06:
        return w->strm;
    case 0x707:
        w->pinctl = (uint8_t) pl;
        return 0;
    case 0xF07:
        return w->pinctl;
    case 0x70C:
        w->eapd = (uint8_t) pl;
        return 0;
    case 0xF0C:
        return w->eapd;
    case 0xF1C:
        return w->cfg;
    case 0x2:
        w->fmt = (uint16_t) pl;
        return 0;
    case 0xA:
        return w->fmt;
    case 0x3: {
        int out = (pl >> 15) & 1, in = (pl >> 14) & 1;
        uint32_t idx = (pl >> 8) & 0xF, g = pl & 0x7F, m = (pl >> 7) & 1;
        if (g > amp_steps(c, w, out)) c->bad_gain++;
        for (int ch = 0; ch < 2; ch++) {
            if (!((pl >> (13 - ch)) & 1)) continue;
            if (out) {
                w->out_gain[ch] = (uint8_t) g;
                w->out_mute[ch] = (uint8_t) m;
            }
            if (in) {
                w->in_gain[idx][ch] = (uint8_t) g;
                w->in_mute[idx][ch] = (uint8_t) m;
            }
        }
        return 0;
    }
    }
    return 0;
}

/* ---- fake controller -------------------------------------------------------- */
static uint32_t ring_n(uint32_t sel)
{
    return sel == 2 ? 256 : sel == 1 ? 16 : 2;
}

static void respond(fhda_t *f, uint32_t res, uint32_t ex)
{
    if (!(f->r[0x5C] & 2)) return; /* RIRB DMA off: dropped */
    f->rirb_n = ring_n(f->r[0x5E] & 3);
    f->rirb_wp = (f->rirb_wp + 1) & (f->rirb_n - 1);
    uint8_t *e = (uint8_t *) fk_ptr(fk_rd64(f->r + 0x50)) + 8 * f->rirb_wp;
    fk_wr32(e, res);
    fk_wr32(e + 4, ex);
    uint32_t cnt = rd16(f, 0x5A) & 0xFF;
    if (++f->rirb_count == (cnt ? cnt : 256) && (f->r[0x5C] & 1)) f->rirbsts |= 1;
}

static int stalled(fhda_t *f)
{
    uint32_t cnt = rd16(f, 0x5A) & 0xFF;
    return f->rirb_count >= (cnt ? cnt : 256);
}

static void ctrl_reset(fhda_t *f)
{
    uint8_t csz = f->r[0x4E], rsz = f->r[0x5E]; /* capability bits survive */
    memset(f->r + 0x08, 0, 0x80 - 0x08);
    f->r[0x4E] = csz;
    f->r[0x5E] = rsz;
    for (uint32_t i = 0; i < NSD; i++) {
        memset(f->r + SDO(i), 0, 0x20);
        f->running[i] = f->srst[i] = 0;
        f->lpib[i] = 0;
    }
    f->statests = f->pub_statests = f->rirbsts = f->pub_rirbsts = 0;
    f->corb_rp = f->rirb_wp = f->rirb_count = 0;
    f->corbrp_rst = 0;
    f->sts_given = 0;
    f->resets++;
}

/* BDL check at RUN: LVI >= 1, entries 128-byte aligned, lengths summing to
 * CBL, format and stream tag set. */
static int stream_ok(fhda_t *f, uint32_t i)
{
    uint32_t o = SDO(i), cbl = rd32(f, o + 8), lvi = rd16(f, o + 0xC), fmt = rd16(f, o + 0x12);
    uint64_t bdl = fk_rd64(f->r + o + 0x18);
    uint32_t tag = (rd32(f, o) >> 20) & 0xF;
    if (lvi < 1 || !bdl || bdl % 128 || fmt != 0x0011 || tag == 0 || cbl == 0) return 0;
    uint64_t sum = 0;
    for (uint32_t k = 0; k <= lvi; k++) {
        const uint8_t *e = (const uint8_t *) fk_ptr(bdl) + 16 * k;
        uint32_t len = fk_rd32(e + 8);
        if (fk_rd64(e) % 128 || len == 0 || len % 128) return 0;
        sum += len;
    }
    return sum == cbl;
}

static const fw_t *dac_for_tag(fhda_t *f, uint32_t tag)
{
    for (uint32_t ci = 0; ci < 15; ci++) {
        fcodec_t *c = &f->c[ci];
        if (!c->present) continue;
        for (uint32_t n = c->wstart; n < (uint32_t) c->wstart + c->wcount; n++)
            if (((c->w[n].wcaps >> 20) & 0xF) == 0 && (c->w[n].strm >> 4) == tag) return &c->w[n];
    }
    return 0;
}

static void stream_copy(fhda_t *f, uint32_t i, uint32_t pos, uint32_t n)
{
    uint32_t o = SDO(i), lvi = rd16(f, o + 0xC);
    uint64_t bdl = fk_rd64(f->r + o + 0x18);
    while (n--) {
        uint32_t at = pos, k = 0, len = 0;
        for (k = 0; k <= lvi; k++) {
            len = fk_rd32((const uint8_t *) fk_ptr(bdl) + 16 * k + 8);
            if (at < len) break;
            at -= len;
        }
        if (k > lvi) return;
        const uint8_t *buf =
            (const uint8_t *) fk_ptr(fk_rd64((const uint8_t *) fk_ptr(bdl) + 16 * k));
        if (f->played_n < sizeof f->played) f->played[f->played_n++] = buf[at];
        pos = (pos + 1) % rd32(f, o + 8);
    }
}

static void streams_tick(fhda_t *f, uint64_t now)
{
    for (uint32_t i = 0; i < NSD; i++) {
        uint32_t o = SDO(i), ctl = rd32(f, o) & 0x00FFFFFFu;
        if (ctl & 1) {
            if (f->srst_never_set) { /* QEMU before 8.0: reset at once, SRST reads 0 */
                ctl &= ~1u;
                f->running[i] = 0;
                f->lpib[i] = 0;
                f->frac[i] = 0;
            } else if (!f->srst[i]) {
                f->srst[i] = 1;
                f->running[i] = 0;
                f->lpib[i] = 0;
                f->frac[i] = 0;
            }
        } else if (f->srst[i]) {
            if (f->srst_never_clear)
                ctl |= 1;
            else
                f->srst[i] = 0;
        }
        if ((ctl & 2) && !(ctl & 1)) {
            if (!f->running[i]) {
                f->starts++;
                if (!stream_ok(f, i)) {
                    f->bad_stream++;
                    ctl &= ~2u; /* descriptor error: the engine stops */
                } else {
                    f->running[i] = 1;
                    f->last[i] = now;
                    f->sd_started = (int) i;
                }
            }
            if (f->running[i]) {
                const fw_t *dac = dac_for_tag(f, (ctl >> 20) & 0xF);
                uint64_t el = now - f->last[i];
                f->last[i] = now;
                /* input streams capture, they never consume */
                if (i >= ISS(f) && dac && dac->fmt == rd16(f, o + 0x12)) {
                    uint32_t q = (uint32_t) (el * 192) + f->frac[i];
                    uint32_t bytes = q / 1000, cbl = rd32(f, o + 8);
                    f->frac[i] = q % 1000;
                    stream_copy(f, i, f->lpib[i], bytes);
                    f->lpib[i] = (uint32_t) (((uint64_t) f->lpib[i] + bytes) % cbl);
                }
            }
        } else {
            f->running[i] = 0;
        }
        wr32(f, o, ctl | (rd32(f, o) & 0xFF000000u));
        wr32(f, o + 4, f->lpib[i]);
    }
}

static void tick(fake_dev_t *d, uint64_t now)
{
    fhda_t *f = (fhda_t *) d->state;
    uint32_t gctl = rd32(f, 0x08);
    if (f->crst_stuck_low) gctl &= ~1u;
    if (f->crst_stuck_high) gctl |= 1;
    wr32(f, 0x08, gctl);
    if (f->corb_run_stuck) f->r[0x4C] |= 2;
    if (f->corb_never_runs) f->r[0x4C] &= (uint8_t) ~2u;
    if (!(gctl & 1)) {
        if (!f->in_reset) {
            f->in_reset = 1;
            f->reset_at = now;
            ctrl_reset(f);
        }
        return;
    }
    if (f->in_reset) {
        f->in_reset = 0;
        f->out_at = now;
        if (now - f->reset_at < 100) f->short_reset = 1; /* CRST held low too briefly */
    }
    if (!f->sts_given && now >= f->out_at + f->codec_delay_us) {
        f->sts_given = 1;
        for (uint32_t i = 0; i < 15; i++)
            if (f->c[i].present) f->statests |= 1u << i;
    }
    uint32_t m = rd16(f, 0x0E);
    if (m != f->pub_statests) f->statests &= ~m;
    wr16(f, 0x0E, f->statests);
    f->pub_statests = f->statests;
    m = f->r[0x5D];
    if (m != f->pub_rirbsts) {
        uint32_t old = f->rirbsts;
        f->rirbsts &= ~m;
        if ((old & 1) && !(f->rirbsts & 1)) f->rirb_count = 0;
    }
    /* CORB read pointer: reset handshake, else the hardware value */
    uint32_t crp = rd16(f, 0x4A);
    if (crp & 0x8000) {
        f->corb_rp = 0;
        f->corbrp_rst = 1;
    }
    uint32_t wpw = rd16(f, 0x58);
    if (wpw & 0x8000) f->rirb_wp = 0;
    /* CORB */
    f->corb_n = ring_n(f->r[0x4E] & 3);
    if ((f->r[0x4C] & 2) && !f->corb_dead && !(crp & 0x8000)) {
        uint32_t wp = rd16(f, 0x48) & (f->corb_n - 1);
        while (f->corb_rp != wp && !stalled(f)) {
            f->corb_rp = (f->corb_rp + 1) & (f->corb_n - 1);
            uint32_t v = fk_rd32((const uint8_t *) fk_ptr(fk_rd64(f->r + 0x40)) + 4 * f->corb_rp);
            uint32_t cad = v >> 28;
            f->verbs++;
            if (cad >= 15 || !f->c[cad].present) continue;
            fcodec_t *c = &f->c[cad];
            uint32_t res = codec_verb(c, v);
            if (c->silent || f->rirb_dead || f->verbs == f->drop_verb) continue;
            if (c->unsol && (f->verbs % 3) == 0) {
                respond(f, 0xDEAD0000u | cad, 0x10 | cad);
                f->unsol_sent++;
            }
            respond(f, res, cad);
        }
    }
    if (f->corbrp_stuck && f->corbrp_rst)
        wr16(f, 0x4A, 0x8000);
    else
        wr16(f, 0x4A, (crp & 0x8000) | f->corb_rp);
    wr16(f, 0x58, f->rirb_wp);
    f->r[0x5D] = (uint8_t) f->rirbsts;
    f->pub_rirbsts = f->rirbsts;
    streams_tick(f, now);
}

/* ---- codec graphs --------------------------------------------------------- */
#define WC(type) ((uint32_t) (type) << 20)
#define CAPS_PCM ((1u << 17) | (1u << 6) | (1u << 5))

/* QEMU hda-duplex: DAC 2 -> line-out pin 3; ADC 4 <- mic pin 5. */
static void graph_qemu(fcodec_t *c)
{
    memset(c, 0, sizeof *c);
    c->present = 1;
    c->vendor = 0x1af40022;
    c->fg_start = 1;
    c->fg_count = 1;
    c->fg_type[0] = 1;
    c->afg = 1;
    c->wstart = 2;
    c->wcount = 4;
    for (uint32_t n = 0; n < 256; n++)
        c->w[n].power = 3, c->w[n].out_mute[0] = c->w[n].out_mute[1] = 1;
    fw_t *dac = &c->w[2];
    dac->wcaps = WC(0) | 1 | 4 | 8 | 16;
    dac->outamp = (1u << 31) | (0x4A << 8) | 0x4A;
    dac->pcm = CAPS_PCM;
    fw_t *pin = &c->w[3];
    pin->wcaps = WC(4) | 1;
    pin->pincaps = 1u << 4;
    pin->cfg = 0x01014010; /* jack, line out */
    static const uint16_t c3[] = {2};
    set_conns(pin, 0, 1, c3);
    c->w[4].wcaps = WC(1) | 1;
    static const uint16_t c4[] = {5};
    set_conns(&c->w[4], 0, 1, c4);
    c->w[5].wcaps = WC(4) | 1;
    c->w[5].pincaps = 1u << 5;
    c->w[5].cfg = 0x01A19020; /* mic */
}

/* Laptop-like: an unusable DAC, a digital DAC, a mixer, a selector, speaker,
 * headphone, an unconnected line out, S/PDIF, power and beep widgets, behind
 * a modem function group. */
static void graph_real(fcodec_t *c)
{
    memset(c, 0, sizeof *c);
    c->present = 1;
    c->vendor = 0x10ec0269;
    c->fg_start = 1;
    c->fg_count = 2;
    c->fg_type[0] = 2; /* modem */
    c->fg_type[1] = 0x101;
    c->afg = 2;
    c->wstart = 3;
    c->wcount = 12;
    c->afg_outamp = (0x3F << 8) | 0x2F;
    c->afg_inamp = (0x1F << 8) | 0x17;
    c->afg_pcm = CAPS_PCM;
    for (uint32_t n = 0; n < 256; n++) {
        fw_t *w = &c->w[n];
        w->power = 3;
        w->out_mute[0] = w->out_mute[1] = 1;
        for (int k = 0; k < 16; k++) w->in_mute[k][0] = w->in_mute[k][1] = 1;
    }
    c->w[3].wcaps = WC(0) | 1 | 4 | (1u << 10);      /* DAC A */
    c->w[4].wcaps = WC(0) | 1 | 4 | 16 | (1u << 10); /* DAC B, 44.1 kHz only */
    c->w[4].pcm = (1u << 17) | (1u << 5);
    c->w[5].wcaps = WC(0) | 1 | (1u << 9);      /* S/PDIF converter */
    c->w[6].wcaps = WC(2) | 1 | 2 | (1u << 10); /* mixer */
    static const uint16_t c6[] = {7, 3};
    set_conns(&c->w[6], 0, 2, c6);
    c->w[7].wcaps = WC(4) | 1;
    c->w[7].pincaps = 1u << 5;
    c->w[7].cfg = 0x02A19040;
    c->w[8].wcaps = WC(3) | 1 | 4 | 8;   /* selector, own amp caps */
    c->w[8].outamp = (0x40 << 8) | 0x50; /* 0 dB step beyond the range */
    static const uint16_t c8[] = {4, 6};
    set_conns(&c->w[8], 0, 2, c8);
    c->w[9].wcaps = WC(4) | 1 | 4 | (1u << 10); /* headphone */
    c->w[9].pincaps = (1u << 4) | (1u << 3) | (1u << 16);
    c->w[9].cfg = 0x0221401F;
    static const uint16_t c9[] = {8, 4};
    set_conns(&c->w[9], 0, 2, c9);
    c->w[10].wcaps = WC(4) | 1 | 4 | (1u << 10); /* internal speaker */
    c->w[10].pincaps = (1u << 4) | (1u << 16);
    c->w[10].cfg = 0x90170110;
    static const uint16_t c10[] = {4, 6};
    set_conns(&c->w[10], 0, 2, c10);
    c->w[11].wcaps = WC(4) | 1; /* line out, not connected */
    c->w[11].pincaps = 1u << 4;
    c->w[11].cfg = 0x411111F0;
    static const uint16_t c11[] = {3};
    set_conns(&c->w[11], 0, 1, c11);
    c->w[12].wcaps = WC(4) | 1 | (1u << 9); /* S/PDIF out pin */
    c->w[12].pincaps = 1u << 4;
    c->w[12].cfg = 0x01451130;
    static const uint16_t c12[] = {5};
    set_conns(&c->w[12], 0, 1, c12);
    c->w[13].wcaps = WC(5);
    c->w[14].wcaps = WC(7);
}

/* Long-form connection list with a range, widgets at 0x20.. */
static void graph_long(fcodec_t *c)
{
    memset(c, 0, sizeof *c);
    c->present = 1;
    c->vendor = 0x11d41984;
    c->fg_start = 1;
    c->fg_count = 1;
    c->fg_type[0] = 1;
    c->afg = 1;
    c->wstart = 0x20;
    c->wcount = 0x10;
    c->afg_outamp = (0x20 << 8) | 0x18;
    c->afg_inamp = (0x10 << 8) | 0x08;
    for (uint32_t n = 0; n < 256; n++) {
        c->w[n].power = 3;
        for (int k = 0; k < 16; k++) c->w[n].in_mute[k][0] = c->w[n].in_mute[k][1] = 1;
    }
    for (uint32_t n = 0x24; n <= 0x28; n++) c->w[n].wcaps = WC(1) | 1;
    c->w[0x2A].wcaps = WC(0) | 1;
    c->w[0x21].wcaps = WC(2) | 1 | 2;
    static const uint16_t c21[] = {0x0024, 0x8028, 0x002A};
    set_conns(&c->w[0x21], 1, 3, c21);
    c->w[0x22].wcaps = WC(4) | 1;
    c->w[0x22].pincaps = 1u << 4;
    c->w[0x22].cfg = 0x01014010;
    static const uint16_t c22[] = {0x21};
    set_conns(&c->w[0x22], 0, 1, c22);
}

/* Inputs only. */
static void graph_noout(fcodec_t *c)
{
    graph_qemu(c);
    c->vendor = 0x14f15045;
    c->w[3].pincaps = 1u << 5;
}

/* Widgets straddling NID 128: only those below are walked. */
static void graph_high(fcodec_t *c)
{
    graph_qemu(c);
    c->wstart = 0x7C;
    c->wcount = 0x10;
    memset(&c->w[2], 0, 4 * sizeof c->w[0]);
    c->w[0x7C].wcaps = WC(0) | 1;
    c->w[0x7D].wcaps = WC(4) | 1;
    c->w[0x7D].pincaps = 1u << 4;
    c->w[0x7D].cfg = 0x01014010;
    static const uint16_t c7d[] = {0x7C};
    set_conns(&c->w[0x7D], 0, 1, c7d);
    c->w[0x80].wcaps = WC(0) | 1;
}

static fhda_t *add_hda(fake_plat_t *fp, uint32_t bdf, uint16_t device)
{
    fake_dev_t *d = fake_dev_new(fp, bdf, 0x8086, device, 0x04, 0x03, 0x00);
    fhda_t *f = (fhda_t *) calloc(1, sizeof *f);
    f->r = fake_dev_bar(d, 0, 0x4000);
    wr16(f, 0x00, (4u << 12) | (4u << 8) | 1); /* 4 out, 4 in, 64OK */
    f->r[0x02] = 0;
    f->r[0x03] = 1;
    f->r[0x4E] = 0x42;
    f->r[0x5E] = 0x42;
    f->codec_delay_us = 300;
    wr32(f, 0x08, 1); /* firmware left it out of reset */
    d->state = f;
    d->tick = tick;
    return f;
}

static hw_verdict_t bind_dev(fake_plat_t *fp, uint32_t idx, hw_registry_t *r, hw_line_t *note)
{
    hw_pci_dev_t devs[4];
    uint32_t n = hw_pci_scan(&fp->plat, devs, 4);
    CHECK(n > idx);
    hw_line_init(note);
    return hda_bind(&fp->plat, &devs[idx], r, note);
}

static void wait_ms(fake_plat_t *fp, uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) fp->plat.delay_us(fp->plat.ctx, 1000);
}

/* Follow the signal from the DAC bound to stream tag 1 out to an enabled pin
 * the way the fake codec state routes it; returns the pin NID or 0. */
static uint32_t audible_pin(fcodec_t *c, uint32_t *dac_out)
{
    for (uint32_t pn = c->wstart; pn < (uint32_t) c->wstart + c->wcount; pn++) {
        fw_t *pin = &c->w[pn];
        if (((pin->wcaps >> 20) & 0xF) != 4 || !(pin->pinctl & 0x40)) continue;
        uint32_t nid = pn;
        for (int depth = 0; depth < 8; depth++) {
            fw_t *w = &c->w[nid];
            uint32_t t = (w->wcaps >> 20) & 0xF;
            if ((w->wcaps & (1u << 10)) && w->power != 0) break;
            if ((w->wcaps & 4) && (w->out_mute[0] || w->out_mute[1])) break;
            if (t == 0) {
                if ((w->strm >> 4) == 1 && w->fmt == 0x0011) {
                    if (dac_out) *dac_out = nid;
                    return pn;
                }
                break;
            }
            uint32_t next = 0;
            if (t == 2) { /* mixer: the first unmuted input */
                for (uint32_t k = 0; k < w->nconn && !next; k++)
                    if (!(w->wcaps & 2) || (!w->in_mute[k][0] && !w->in_mute[k][1]))
                        next = w->conn[k];
            } else if (w->nconn) {
                if (w->sel >= w->nconn) break;
                if ((w->wcaps & 2) && t != 4 && (w->in_mute[w->sel][0] || w->in_mute[w->sel][1]))
                    break;
                next = w->conn[w->sel];
            }
            if (!next) break;
            nid = next;
        }
    }
    return 0;
}

/* ---- tests -------------------------------------------------------------- */
static void test_qemu_codec_and_play(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fhda_t *f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x293E);
    graph_qemu(&f->c[0]);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    /* the full probe path: class match, boot line, registry */
    hw_probe_stats_t st = hw_probe_all(&fp.plat, &r);
    CHECK(st.functions == 1 && st.bound == 1);
    CHECK(fake_log_has(&fp, "[DRIVER ONLINE] hda 00:1b.0 8086:293e ICH9 HDA 1.0, codec 0: "
                            "1af4:0022, out pin 0x03 (line out) via DAC 0x02"));
    CHECK(r.n_audio == 1);
    hw_audio_t *a = r.audio[0];
    CHECK(strncmp(a->name, "hda", 3) == 0 && a->name[3] >= '0' && a->name[3] <= '9' && !a->name[4]);
    CHECK(a->codecs == 1);
    CHECK((fp.devs[0]->cfg[1] & 0x0406) == 0x0406);
    /* controller: one reset, rings at 256 entries, RIRB paced and flag on */
    CHECK(f->resets == 1 && !f->short_reset);
    CHECK((rd32(f, 0x08) & 1) == 1);
    CHECK((f->r[0x4E] & 3) == 2 && (f->r[0x5E] & 3) == 2);
    CHECK(f->r[0x4E] == 0x42 && f->r[0x5E] == 0x42);
    CHECK((f->r[0x4C] & 2) && (f->r[0x5C] & 3) == 3);
    CHECK(rd16(f, 0x5A) == 1);
    CHECK(fk_rd64(f->r + 0x40) % 128 == 0 && fk_rd64(f->r + 0x50) % 128 == 0);
    CHECK(f->statests == 0); /* cleared after reading */
    CHECK(f->verbs > 10 && f->verbs == f->c[0].verbs);
    /* codec: the path as programmed */
    fcodec_t *c = &f->c[0];
    CHECK(c->afg_power == 0);
    CHECK(c->w[3].pinctl == 0x40);
    CHECK(c->w[2].fmt == 0x0011 && c->w[2].strm == 0x10);
    CHECK(c->w[2].out_mute[0] == 0 && c->w[2].out_mute[1] == 0);
    CHECK(c->w[2].out_gain[0] == 0x4A && c->w[2].out_gain[1] == 0x4A);
    CHECK(c->w[3].sel_verbs == 0); /* a single connection needs no select */
    CHECK(c->w[5].pinctl == 0 && c->w[4].strm == 0);
    CHECK(c->bad_gain == 0 && c->bad_sel == 0);
    uint32_t dac = 0;
    CHECK(audible_pin(c, &dac) == 3 && dac == 2);
    /* the stream is reset and idle until play() */
    uint32_t sd = 0x80 + 4 * 0x20;
    CHECK((rd32(f, sd) & 3) == 0 && f->starts == 0);

    CHECK(a->play(a, 0, 10) == -1);
    static int16_t pcm[4800 * 2];
    for (uint32_t i = 0; i < 4800 * 2; i++) pcm[i] = (int16_t) (i * 37 - 9000);
    CHECK(a->play(a, pcm, 0) == -1);
    CHECK(a->play(a, pcm, 4800) == 0);
    CHECK(f->starts == 1 && f->bad_stream == 0);
    CHECK(f->sd_started == 4); /* the first output stream, after 4 input streams */
    CHECK((rd32(f, sd) & 0x00F00003u) == 0x00100002u);
    CHECK(rd32(f, sd + 8) == 19200);
    CHECK(rd16(f, sd + 0xC) == 1 && rd16(f, sd + 0x12) == 0x0011);
    uint8_t *bdl = (uint8_t *) fk_ptr(fk_rd64(f->r + sd + 0x18));
    CHECK(fk_rd32(bdl + 8) == 9600 && fk_rd32(bdl + 24) == 9600);
    CHECK(fk_rd64(bdl + 16) == fk_rd64(bdl) + 9600);
    for (uint32_t i = 0; i < 8; i++)
        if (i != 4) CHECK(rd32(f, 0x80 + i * 0x20) == 0);
    wait_ms(&fp, 5);
    uint32_t pos = a->position(a);
    CHECK(pos >= 900 && pos <= 1100);
    CHECK(f->played_n >= 900 && memcmp(f->played, pcm, f->played_n) == 0);
    /* position stops the stream once the PCM has been consumed */
    uint32_t last = pos, polls = 0;
    while (pos < 19200 && polls < 1000) {
        wait_ms(&fp, 1);
        pos = a->position(a);
        CHECK(pos >= last);
        last = pos;
        polls++;
    }
    CHECK(pos == 19200);
    CHECK(polls >= 90 && polls <= 100);
    wait_ms(&fp, 1);
    CHECK((rd32(f, sd) & 2) == 0);
    uint32_t pn = f->played_n;
    CHECK(pn >= 19200 && pn < 19200 + 400);
    CHECK(memcmp(f->played, pcm, 19200) == 0);
    wait_ms(&fp, 20);
    CHECK(f->played_n == pn); /* stopped: nothing more consumed */
    CHECK(a->position(a) == 19200);

    /* a second play: odd length padded with silence, LPIB from 0 again */
    f->played_n = 0;
    CHECK(a->play(a, pcm, 1000) == 0);
    CHECK(rd32(f, sd + 8) == 4096);
    CHECK(fk_rd32(bdl + 8) == 2048);
    CHECK(f->starts == 2);
    CHECK(a->position(a) < 400);
    /* polled more often than the buffer lasts (21 ms here) */
    for (int i = 0; i < 6; i++) {
        wait_ms(&fp, 5);
        pos = a->position(a);
    }
    CHECK(pos == 4000);
    CHECK(f->played_n >= 4096);
    CHECK(memcmp(f->played, pcm, 4000) == 0);
    uint32_t zeros = 0;
    for (uint32_t i = 4000; i < 4096; i++) zeros += f->played[i] == 0;
    CHECK(zeros == 96);

    /* more than the buffer holds: truncated to 64 KB */
    static int16_t lots[40000 * 2];
    CHECK(a->play(a, lots, 40000) == 0);
    CHECK(rd32(f, sd + 8) == 65536);
    CHECK(fk_rd32(bdl + 8) == 32768);
    /* one frame: 4 bytes, a 256-byte buffer */
    CHECK(a->play(a, pcm, 1) == 0);
    CHECK(rd32(f, sd + 8) == 256 && fk_rd32(bdl + 8) == 128);
    wait_ms(&fp, 3);
    CHECK(a->position(a) == 4);

    /* the boot self-test */
    CHECK(hw_selftest_all(&fp.plat, &r) == 1);
    CHECK(fake_log_has(&fp, "[SELFTEST OK] hda"));
    CHECK(fake_log_has(&fp, "output stream DMA position "));
    fake_plat_free(&fp);
    free(f);
}

static void test_real_graph(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fhda_t *f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x1C20);
    graph_real(&f->c[0]);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_dev(&fp, 0, &r, &note) == HW_T);
    CHECK(strcmp(note.b,
                 "6 Series HDA 1.0, codec 0: 10ec:0269, out pin 0x0a (speaker) via DAC 0x03") == 0);
    fcodec_t *c = &f->c[0];
    CHECK(c->afg_power == 0);
    /* speaker pin: out enabled, EAPD, D0, amp unmuted at the AFG's 0 dB step,
     * connection 1 (the mixer) selected */
    CHECK(c->w[10].pinctl == 0x40 && c->w[10].eapd == 0x02 && c->w[10].power == 0);
    CHECK(c->w[10].sel == 1 && c->w[10].sel_verbs == 1);
    CHECK(c->w[10].out_mute[0] == 0 && c->w[10].out_mute[1] == 0 && c->w[10].out_gain[0] == 0x2F);
    /* mixer: its input 1 (DAC A) unmuted at 0 dB, input 0 untouched, no select */
    CHECK(c->w[6].power == 0 && c->w[6].sel_verbs == 0);
    CHECK(c->w[6].in_mute[1][0] == 0 && c->w[6].in_mute[1][1] == 0);
    CHECK(c->w[6].in_gain[1][0] == 0x17 && c->w[6].in_gain[1][1] == 0x17);
    CHECK(c->w[6].in_mute[0][0] == 1);
    /* DAC A */
    CHECK(c->w[3].power == 0 && c->w[3].fmt == 0x0011 && c->w[3].strm == 0x10);
    CHECK(c->w[3].out_mute[0] == 0 && c->w[3].out_gain[1] == 0x2F);
    /* nothing else touched */
    CHECK(c->w[4].strm == 0 && c->w[5].strm == 0 && c->w[9].pinctl == 0 && c->w[11].pinctl == 0);
    CHECK(c->w[12].pinctl == 0 && c->w[8].sel_verbs == 0 && c->w[9].power == 3);
    CHECK(c->bad_gain == 0 && c->bad_sel == 0);
    uint32_t dac = 0;
    CHECK(audible_pin(c, &dac) == 10 && dac == 3);
    hw_audio_t *a = r.audio[0];
    static int16_t pcm[512];
    for (uint32_t i = 0; i < 512; i++) pcm[i] = (int16_t) (i * 101);
    CHECK(a->play(a, pcm, 256) == 0);
    wait_ms(&fp, 2);
    CHECK(a->position(a) > 0);
    CHECK(memcmp(f->played, pcm, f->played_n < 1024 ? f->played_n : 1024) == 0);
    fake_plat_free(&fp);
    free(f);

    /* the line out connected: it wins, straight to DAC A */
    fake_plat_init(&fp);
    f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x1C20);
    graph_real(&f->c[0]);
    f->c[0].w[11].cfg = 0x01014010;
    memset(&r, 0, sizeof r);
    CHECK(bind_dev(&fp, 0, &r, &note) == HW_T);
    CHECK(strstr(note.b, "out pin 0x0b (line out) via DAC 0x03") != 0);
    /* SRST read back 1, so the reset is confirmed: no N on the line */
    CHECK(strstr(note.b, "unconfirmed") == 0);
    CHECK(f->c[0].w[11].pinctl == 0x40 && f->c[0].w[10].pinctl == 0);
    CHECK(audible_pin(&f->c[0], &dac) == 11 && dac == 3);
    fake_plat_free(&fp);
    free(f);

    /* no speaker or line out: the headphone, HP drive on, through the
     * selector (input 1, the mixer) whose gain is clamped to its range */
    fake_plat_init(&fp);
    f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x1C20);
    graph_real(&f->c[0]);
    f->c[0].w[10].pincaps = 0;
    f->c[0].w[11].pincaps = 0;
    memset(&r, 0, sizeof r);
    CHECK(bind_dev(&fp, 0, &r, &note) == HW_T);
    CHECK(strstr(note.b, "out pin 0x09 (headphone) via DAC 0x03") != 0);
    c = &f->c[0];
    CHECK(c->w[9].pinctl == 0xC0 && c->w[9].eapd == 2 && c->w[9].sel == 0 &&
          c->w[9].sel_verbs == 1);
    CHECK(c->w[8].sel == 1 && c->w[8].out_gain[0] == 0x40 && c->w[8].out_mute[0] == 0);
    CHECK(c->w[6].in_mute[1][0] == 0);
    CHECK(c->bad_gain == 0 && c->bad_sel == 0);
    CHECK(audible_pin(c, &dac) == 9 && dac == 3);
    fake_plat_free(&fp);
    free(f);

    /* only the unconnected line out left: still used */
    fake_plat_init(&fp);
    f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x1C20);
    graph_real(&f->c[0]);
    f->c[0].w[10].pincaps = 0;
    f->c[0].w[9].pincaps = 0;
    memset(&r, 0, sizeof r);
    CHECK(bind_dev(&fp, 0, &r, &note) == HW_T);
    CHECK(strstr(note.b, "out pin 0x0b (unconnected) via DAC 0x03") != 0);
    fake_plat_free(&fp);
    free(f);
}

static void test_long_form_and_high_nids(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fhda_t *f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x2668);
    graph_long(&f->c[0]);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_dev(&fp, 0, &r, &note) == HW_T);
    CHECK(strstr(note.b,
                 "ICH6 HDA 1.0, codec 0: 11d4:1984, out pin 0x22 (line out) via DAC 0x2a") != 0);
    fcodec_t *c = &f->c[0];
    CHECK(c->w[0x21].nconn == 6 && c->w[0x21].conn[5] == 0x2A);
    CHECK(c->w[0x21].in_mute[5][0] == 0 && c->w[0x21].in_mute[5][1] == 0);
    CHECK(c->w[0x21].in_gain[5][0] == 0x08);
    for (int k = 0; k < 5; k++) CHECK(c->w[0x21].in_mute[k][0] == 1);
    CHECK(c->w[0x2A].strm == 0x10 && c->w[0x2A].fmt == 0x11);
    uint32_t dac = 0;
    CHECK(audible_pin(c, &dac) == 0x22 && dac == 0x2A);
    fake_plat_free(&fp);
    free(f);

    fake_plat_init(&fp);
    f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x2668);
    graph_high(&f->c[0]);
    memset(&r, 0, sizeof r);
    CHECK(bind_dev(&fp, 0, &r, &note) == HW_T);
    CHECK(strstr(note.b, "out pin 0x7d (line out) via DAC 0x7c") != 0);
    fake_plat_free(&fp);
    free(f);
}

static void test_codecs(void)
{
    /* codec 0 has no output; codec 2 does; codec 1 is present but mute */
    fake_plat_t fp;
    fake_plat_init(&fp);
    fhda_t *f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x2668);
    graph_noout(&f->c[0]);
    graph_qemu(&f->c[1]);
    f->c[1].silent = 1;
    graph_qemu(&f->c[2]);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_dev(&fp, 0, &r, &note) == HW_T);
    CHECK(strstr(note.b, "codec 2: 1af4:0022, out pin 0x03") != 0);
    CHECK(r.audio[0]->codecs == 0x5);
    CHECK(f->c[2].w[2].strm == 0x10 && f->c[0].w[2].strm == 0);
    CHECK(f->c[1].verbs == 1); /* abandoned after the first silent verb */
    hw_audio_t *a = r.audio[0];
    static int16_t pcm[2048];
    CHECK(a->play(a, pcm, 1024) == 0);
    wait_ms(&fp, 3);
    CHECK(a->position(a) > 0);
    fake_plat_free(&fp);
    free(f);

    /* two good codecs: the first one is used, both answer */
    fake_plat_init(&fp);
    f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x2668);
    graph_qemu(&f->c[0]);
    graph_real(&f->c[3]);
    memset(&r, 0, sizeof r);
    CHECK(bind_dev(&fp, 0, &r, &note) == HW_T);
    CHECK(strstr(note.b, "codec 0: 1af4:0022") != 0);
    CHECK(r.audio[0]->codecs == 0x9);
    CHECK(f->c[3].w[3].strm == 0 && f->c[3].w[10].pinctl == 0 && f->c[3].w[3].pinctl == 0);
    CHECK(f->c[0].w[2].strm == 0x10 && f->c[0].w[3].pinctl == 0x40);
    fake_plat_free(&fp);
    free(f);

    /* unsolicited responses interleaved with the answers */
    fake_plat_init(&fp);
    f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x2668);
    graph_real(&f->c[0]);
    f->c[0].unsol = 1;
    memset(&r, 0, sizeof r);
    CHECK(bind_dev(&fp, 0, &r, &note) == HW_T);
    CHECK(f->unsol_sent > 5);
    CHECK(strstr(note.b, "out pin 0x0a (speaker) via DAC 0x03") != 0);
    CHECK(f->c[0].bad_sel == 0);
    fake_plat_free(&fp);
    free(f);
}

static void test_ring_sizes(void)
{
    static const struct {
        uint8_t corb, rirb, want_c, want_r;
    } sz[] = {
        {0x22, 0x22, 1, 1}, /* 16 entries only */
        {0x12, 0x12, 0, 0}, /* 2 entries only */
        {0x62, 0x12, 2, 0}, /* 256 CORB, 2 RIRB */
        {0x01, 0x01, 1, 1}, /* no capability bits: the size it has */
        {0x70, 0x30, 2, 1},
    };
    for (uint32_t i = 0; i < sizeof sz / sizeof sz[0]; i++) {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fhda_t *f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x2668);
        f->r[0x4E] = sz[i].corb;
        f->r[0x5E] = sz[i].rirb;
        graph_real(&f->c[0]);
        hw_registry_t r;
        memset(&r, 0, sizeof r);
        hw_line_t note;
        hw_verdict_t v = bind_dev(&fp, 0, &r, &note);
        if (v != HW_T)
            printf("  ring sizes %02x/%02x: verdict %d note '%s'\n", sz[i].corb, sz[i].rirb, v,
                   note.b);
        CHECK(v == HW_T);
        CHECK((f->r[0x4E] & 3) == sz[i].want_c && (f->r[0x5E] & 3) == sz[i].want_r);
        CHECK((f->r[0x4E] & 0xF0) == (sz[i].corb & 0xF0));
        CHECK(f->verbs > 40); /* every ring wrapped at least twice */
        CHECK(strstr(note.b, "via DAC 0x03") != 0);
        fake_plat_free(&fp);
        free(f);
    }
}

typedef struct {
    const char *what;
    void (*setup)(fhda_t *f);
    hw_verdict_t want;
    const char *note;
} fault_t;

static void k_crst_low(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->crst_stuck_low = 1;
}
static void k_crst_high(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->crst_stuck_high = 1;
}
static void k_nocodec(fhda_t *f)
{
    (void) f;
}
static void k_corb(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->corb_dead = 1;
}
static void k_rirb(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->rirb_dead = 1;
}
static void k_silent(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->c[0].silent = 1;
}
static void k_noout(fhda_t *f)
{
    graph_noout(&f->c[0]);
}
static void k_oss0(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    wr16(f, 0, 0x0401);
}
static void k_vmaj(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->r[3] = 2;
}
static void k_ones(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    memset(f->r, 0xFF, 4);
}
static void k_corbrp(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->corbrp_stuck = 1;
}
static void k_srst_set(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->srst_never_set = 1;
}
static void k_srst_clr(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->srst_never_clear = 1;
}
static void k_no48k(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->c[0].w[2].pcm = (1u << 17) | (1u << 5);
}
static void k_16only(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->c[0].w[2].pcm = (1u << 18) | (1u << 6);
}
static void k_pcm0(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->c[0].w[2].pcm = 0;
}
static void k_digital(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->c[0].w[2].wcaps |= 1u << 9;
}
static void k_pin_dig(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->c[0].w[3].wcaps |= 1u << 9;
}
static void k_modem(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->c[0].fg_type[0] = 2;
}
static void k_slow(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->codec_delay_us = 15000;
}
static void k_runstuck(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->corb_run_stuck = 1;
}
static void k_norun(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->corb_never_runs = 1;
}
static void k_late(fhda_t *f)
{
    graph_qemu(&f->c[0]);
    f->codec_delay_us = 40000;
}
static void k_loop(fhda_t *f) /* a selector cycle in front of the DAC */
{
    graph_qemu(&f->c[0]);
    fcodec_t *c = &f->c[0];
    c->wcount = 6;
    c->w[6].wcaps = WC(3) | 1;
    c->w[7].wcaps = WC(3) | 1;
    static const uint16_t c6[] = {7}, c7[] = {6, 2}, c3[] = {6};
    set_conns(&c->w[6], 0, 1, c6);
    set_conns(&c->w[7], 0, 2, c7);
    set_conns(&c->w[3], 0, 1, c3);
}

static void test_faults(void)
{
    static const fault_t faults[] = {
        {"CRST stuck at 0", k_crst_low, HW_F, "controller reset timed out"},
        {"CRST stuck at 1", k_crst_high, HW_F, "will not enter reset"},
        {"no codec", k_nocodec, HW_N, ", no codec"},
        {"CORB never consumed", k_corb, HW_F, "CORB not consumed"},
        {"RIRB never answers", k_rirb, HW_F, "codec did not answer"},
        {"codec silent", k_silent, HW_F, "codec did not answer"},
        {"no output pin", k_noout, HW_N, "no output pin with a usable DAC"},
        {"no output stream", k_oss0, HW_B, "no output stream"},
        {"version 2", k_vmaj, HW_B, "version is not 1.x"},
        {"all-ones", k_ones, HW_F, "all-ones"},
        {"CORBRP reset stuck", k_corbrp, HW_F, "CORB read pointer will not reset"},
        {"SRST resets at once, never reads 1", k_srst_set, HW_T, "via DAC 0x02; stream reset unconfirmed (N: SRST never read back 1)"},
        {"SRST never clears", k_srst_clr, HW_F, "output stream reset timed out"},
        {"DAC without 48 kHz", k_no48k, HW_N, "no output pin"},
        {"DAC without 16 bit", k_16only, HW_N, "no output pin"},
        {"DAC formats unreported", k_pcm0, HW_T, "via DAC 0x02"},
        {"digital DAC only", k_digital, HW_N, "no output pin"},
        {"digital pin only", k_pin_dig, HW_N, "no output pin"},
        {"modem function group only", k_modem, HW_N, "no output pin"},
        {"slow codec wake", k_slow, HW_T, "via DAC 0x02"},
        {"codec too late", k_late, HW_N, ", no codec"},
        {"CORB will not stop", k_runstuck, HW_F, "CORB/RIRB will not stop"},
        {"CORB will not run", k_norun, HW_F, "CORB will not run"},
        {"selector loop", k_loop, HW_T, "out pin 0x03 (line out) via DAC 0x02"},
    };
    for (uint32_t i = 0; i < sizeof faults / sizeof faults[0]; i++) {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fhda_t *f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x2668);
        faults[i].setup(f);
        hw_registry_t r;
        memset(&r, 0, sizeof r);
        hw_line_t note;
        uint64_t t0 = fp.now_us;
        hw_verdict_t v = bind_dev(&fp, 0, &r, &note);
        if (v != faults[i].want || !strstr(note.b, faults[i].note))
            printf("  fault '%s': verdict %d note '%s'\n", faults[i].what, v, note.b);
        CHECK(v == faults[i].want);
        CHECK(strstr(note.b, faults[i].note) != 0);
        CHECK((v == HW_T) == (r.n_audio == 1));
        CHECK(fp.now_us - t0 < 2000000); /* nothing waits long */
        if (faults[i].setup == k_loop)
            CHECK(f->c[0].w[3].sel_verbs == 0 && f->c[0].w[6].sel_verbs == 0);
        if (faults[i].setup == k_loop) CHECK(audible_pin(&f->c[0], 0) == 3);
        fake_plat_free(&fp);
        free(f);
    }
}

static void test_no_bar_64ok_dma_registry(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fake_dev_new(&fp, HW_BDF(0, 0x1b, 0), 0x8086, 0x2668, 0x04, 0x03, 0x00);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_dev(&fp, 0, &r, &note) == HW_N);
    CHECK(strstr(note.b, "BAR0") != 0);
    fake_plat_free(&fp);

    /* no 64-bit addressing: host DMA pointers are above 4 GB */
    fake_plat_init(&fp);
    fhda_t *f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x2668);
    graph_qemu(&f->c[0]);
    wr16(f, 0, rd16(f, 0) & ~1u);
    memset(&r, 0, sizeof r);
    CHECK(bind_dev(&fp, 0, &r, &note) == HW_F);
    CHECK(strstr(note.b, "above 4 GB") != 0);
    CHECK(f->verbs == 0);
    fake_plat_free(&fp);
    free(f);

    for (uint32_t k = 1; k <= 5; k++) {
        fake_plat_init(&fp);
        f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x2668);
        graph_qemu(&f->c[0]);
        fp.dma_fail_after = k;
        memset(&r, 0, sizeof r);
        CHECK(bind_dev(&fp, 0, &r, &note) == HW_F);
        CHECK(strstr(note.b, "out of DMA memory") != 0);
        CHECK(r.n_audio == 0);
        fake_plat_free(&fp);
        free(f);
    }

    fake_plat_init(&fp);
    f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x2668);
    graph_qemu(&f->c[0]);
    memset(&r, 0, sizeof r);
    r.n_audio = HW_MAX_AUDIO;
    CHECK(bind_dev(&fp, 0, &r, &note) == HW_N);
    CHECK(r.n_audio == HW_MAX_AUDIO);
    fake_plat_free(&fp);
    free(f);

    /* a stream that cannot start (RUN never latches): play() reports it */
    fake_plat_init(&fp);
    f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x2668);
    graph_qemu(&f->c[0]);
    memset(&r, 0, sizeof r);
    CHECK(bind_dev(&fp, 0, &r, &note) == HW_T);
    hw_audio_t *a = r.audio[0];
    static int16_t pcm[256];
    f->srst_never_clear = 1;
    CHECK(a->play(a, pcm, 128) == -1);
    CHECK(a->position(a) == 0);
    f->srst_never_clear = 0;
    f->srst[4] = 0;
    CHECK(a->play(a, pcm, 128) == 0);
    /* the codec unbinds its converter: DMA stalls, position stays 0 */
    f->c[0].w[2].strm = 0;
    wait_ms(&fp, 5);
    CHECK(a->position(a) == 0);
    f->c[0].w[2].strm = 0x10;
    wait_ms(&fp, 2);
    CHECK(a->position(a) > 0);
    fake_plat_free(&fp);
    free(f);
}

/* Every verb matters: drop the response to each one in turn and the bind
 * must never claim a working output. */
static void test_each_verb_dropped(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fhda_t *f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x2668);
    graph_real(&f->c[0]);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_dev(&fp, 0, &r, &note) == HW_T);
    uint32_t total = f->verbs;
    fake_plat_free(&fp);
    free(f);
    CHECK(total > 40);
    for (uint32_t k = 1; k <= total; k++) {
        fake_plat_init(&fp);
        f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x2668);
        graph_real(&f->c[0]);
        f->drop_verb = k;
        memset(&r, 0, sizeof r);
        hw_verdict_t v = bind_dev(&fp, 0, &r, &note);
        if (v == HW_T) printf("  dropped verb %u: still bound '%s'\n", k, note.b);
        CHECK(v != HW_T);
        CHECK(r.n_audio == 0);
        CHECK(v == HW_F);
        CHECK(strstr(note.b, k == 1 ? "codec did not answer" : "codec stopped answering") != 0);
        fake_plat_free(&fp);
        free(f);
    }
}

/* Lesser pins: another jack beats an unconnected line out; a headphone beats
 * another jack. */
static void test_pin_ranks(void)
{
    static const struct {
        uint32_t cfg9, cfg11;
        const char *want;
    } t[] = {
        {0x01F14050, 0x411111F0, "out pin 0x09 (other jack)"},
        {0x0221401F, 0x01F14050, "out pin 0x09 (headphone)"},
        {0x411111F0, 0x01F14050, "out pin 0x0b (other jack)"},
        {0x0221401F, 0x90170110, "out pin 0x0b (speaker)"},
    };
    for (uint32_t i = 0; i < 4; i++) {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fhda_t *f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x2668);
        graph_real(&f->c[0]);
        f->c[0].w[10].pincaps = 0;
        f->c[0].w[9].cfg = t[i].cfg9;
        f->c[0].w[11].cfg = t[i].cfg11;
        hw_registry_t r;
        memset(&r, 0, sizeof r);
        hw_line_t note;
        CHECK(bind_dev(&fp, 0, &r, &note) == HW_T);
        if (!strstr(note.b, t[i].want)) printf("  pin rank %u: '%s'\n", i, note.b);
        CHECK(strstr(note.b, t[i].want) != 0);
        fake_plat_free(&fp);
        free(f);
    }
}

/* A new play() while one is still running, and two controllers' names. */
static void test_replay_and_names(void)
{
    fake_plat_t fp;
    fake_plat_init(&fp);
    fhda_t *f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x2668);
    graph_qemu(&f->c[0]);
    fhda_t *g = add_hda(&fp, HW_BDF(0, 0x1c, 0), 0x2668);
    graph_qemu(&g->c[0]);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_line_t note;
    CHECK(bind_dev(&fp, 0, &r, &note) == HW_T);
    CHECK(bind_dev(&fp, 1, &r, &note) == HW_T);
    CHECK(r.n_audio == 2);
    CHECK(strncmp(r.audio[0]->name, "hda", 3) == 0 && strncmp(r.audio[1]->name, "hda", 3) == 0);
    CHECK(r.audio[1]->name[3] ==
          (char) (r.audio[0]->name[3] == '9' ? '0' : r.audio[0]->name[3] + 1));
    CHECK(r.audio[0]->name[4] == 0 && r.audio[1]->name[4] == 0);
    hw_audio_t *a = r.audio[0];
    static int16_t pcm[8000 * 2];
    for (uint32_t i = 0; i < 16000; i++) pcm[i] = (int16_t) (i * 7);
    CHECK(a->play(a, pcm, 8000) == 0); /* 32000 bytes */
    wait_ms(&fp, 30);
    uint32_t pos = a->position(a);
    CHECK(pos > 5000 && pos < 6500);
    CHECK(a->play(a, pcm, 500) == 0); /* 2000 bytes, from the start again */
    CHECK(a->position(a) < 300);
    for (int i = 0; i < 4; i++) {
        wait_ms(&fp, 3);
        pos = a->position(a);
    }
    CHECK(pos == 2000);
    wait_ms(&fp, 1);
    CHECK((rd32(f, 0x80 + 4 * 0x20) & 2) == 0);
    CHECK(g->starts == 0); /* the other controller never ran */
    fake_plat_free(&fp);
    free(f);
    free(g);
}

/* Other stream counts: the output descriptor follows however many input
 * streams GCAP reports. */
static void test_stream_layout(void)
{
    static const uint32_t gcaps[] = {(1u << 12) | (2u << 8) | 1, (2u << 12) | (0u << 8) | 1,
                                     (3u << 12) | (7u << 8) | (1u << 3) | 1};
    static const int want[] = {2, 0, 7};
    for (uint32_t i = 0; i < 3; i++) {
        fake_plat_t fp;
        fake_plat_init(&fp);
        fhda_t *f = add_hda(&fp, HW_BDF(0, 0x1b, 0), 0x2668);
        graph_qemu(&f->c[0]);
        wr16(f, 0, gcaps[i]);
        hw_registry_t r;
        memset(&r, 0, sizeof r);
        hw_line_t note;
        CHECK(bind_dev(&fp, 0, &r, &note) == HW_T);
        static int16_t pcm[1024];
        hw_audio_t *a = r.audio[0];
        CHECK(a->play(a, pcm, 512) == 0);
        CHECK(f->sd_started == want[i]);
        wait_ms(&fp, 2);
        CHECK(a->position(a) > 0);
        fake_plat_free(&fp);
        free(f);
    }
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    test_qemu_codec_and_play();
    test_real_graph();
    test_long_form_and_high_nids();
    test_codecs();
    test_ring_sizes();
    test_faults();
    test_no_bar_64ok_dma_registry();
    test_stream_layout();
    test_each_verb_dropped();
    test_pin_ranks();
    test_replay_and_names();
    printf("test_hda: %d checks, %d failures\n", fk_checks, fk_failures);
    return fk_failures ? 1 : 0;
}
