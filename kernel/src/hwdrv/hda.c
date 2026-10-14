/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hda.c — Intel High Definition Audio class driver (PCI class 04/03), polled.
 *
 * Follows the HDA specification 1.0a: controller reset (GCTL.CRST), codec
 * discovery through STATESTS, CORB/RIRB command rings in DMA memory, then per
 * codec: vendor id, function groups, the audio function group's widgets. One
 * output path is chosen, from a pin complex that can drive output (line out
 * first, then speaker, then headphone, by its default configuration) back
 * through mixers/selectors to an audio output converter (DAC) that takes
 * 48 kHz 16-bit PCM. Along the path: connection selects, amps unmuted at
 * their 0 dB step, widgets powered to D0, pin output enabled, EAPD on.
 *
 * Output: the first output stream descriptor (after GCAP.ISS input streams),
 * stream tag 1, a two-entry BDL over a 64 KB buffer, format 0x0011 (48 kHz,
 * 16 bit, 2 channels). play() copies the PCM, starts the stream and returns;
 * position() reports the bytes consumed (from LPIB) and stops the stream once
 * the whole PCM has been consumed.
 *
 * Register widths matter here: HDA has 8-, 16- and 32-bit registers at
 * unaligned offsets, and controllers (QEMU's among them) decode each one only
 * at its own width. Every read that follows a write the device must act on
 * comes after at least one delay_us.
 *
 * Verdicts: HW_T an output path is programmed and the stream resets; HW_F the
 * controller or codec stops answering (reset, CORB, RIRB, a stream that
 * stays in reset); HW_B
 * the controller contradicts itself (version not 1.x, no output stream); HW_N
 * nothing to drive (no BAR, no codec, no output pin with a usable DAC). */
#include "hwdrv.h"

#define GCAP      0x00 /* 16 */
#define VMIN      0x02 /* 8 */
#define VMAJ      0x03 /* 8 */
#define GCTL      0x08 /* 32 */
#define STATESTS  0x0E /* 16 */
#define CORBLBASE 0x40
#define CORBUBASE 0x44
#define CORBWP    0x48 /* 16 */
#define CORBRP    0x4A /* 16 */
#define CORBCTL   0x4C /* 8 */
#define CORBSIZE  0x4E /* 8 */
#define RIRBLBASE 0x50
#define RIRBUBASE 0x54
#define RIRBWP    0x58 /* 16 */
#define RINTCNT   0x5A /* 16 */
#define RIRBCTL   0x5C /* 8 */
#define RIRBSTS   0x5D /* 8 */
#define RIRBSIZE  0x5E /* 8 */
#define SD(i)     (0x80u + (i) * 0x20u)
#define SD_CTL    0x00 /* 24 bits + STS byte */
#define SD_LPIB   0x04
#define SD_CBL    0x08
#define SD_LVI    0x0C /* 16 */
#define SD_FMT    0x12 /* 16 */
#define SD_BDPL   0x18
#define SD_BDPU   0x1C

#define GCTL_CRST       0x01u
#define CORBRP_RST      0x8000u
#define CORBCTL_RUN     0x02u
#define RIRBWP_RST      0x8000u
#define RIRBCTL_DMAEN   0x02u
#define RIRBCTL_RINTCTL 0x01u
#define SDCTL_SRST      0x01u
#define SDCTL_RUN       0x02u

/* parameters (verb F00) */
#define PAR_VENDOR  0x00
#define PAR_NODES   0x04
#define PAR_FGTYPE  0x05
#define PAR_WCAPS   0x09
#define PAR_PCM     0x0A
#define PAR_PINCAPS 0x0C
#define PAR_INAMP   0x0D
#define PAR_CONNLEN 0x0E
#define PAR_OUTAMP  0x12

#define WCAP_STEREO   (1u << 0)
#define WCAP_INAMP    (1u << 1)
#define WCAP_OUTAMP   (1u << 2)
#define WCAP_AMPOVRD  (1u << 3)
#define WCAP_FMTOVRD  (1u << 4)
#define WCAP_CONNLIST (1u << 8)
#define WCAP_DIGITAL  (1u << 9)
#define WCAP_POWER    (1u << 10)

#define W_OUT 0x0
#define W_IN  0x1
#define W_MIX 0x2
#define W_SEL 0x3
#define W_PIN 0x4

#define PINCAP_HP   (1u << 3)
#define PINCAP_OUT  (1u << 4)
#define PINCAP_EAPD (1u << 16)

#define PCM_48K   (1u << 6)
#define PCM_16BIT (1u << 17)

#define FMT_48K_16_2CH 0x0011u
#define STREAM_TAG     1u

#define MAX_NID   128
#define MAX_CONN  32
#define MAX_DEPTH 6
#define BUF_BYTES 65536u

#define T_RESET 100000u /* CRST either way */
#define T_CODEC 20000u  /* STATESTS after reset */
#define T_VERB  20000u  /* one codec response */
#define T_RING  1000u   /* CORB/RIRB control bits */
#define T_SRST  100000u /* stream reset either way */
#define T_LATCH 1000u   /* SRST reading back as 1, best effort */

typedef struct hda_widget {
    uint32_t caps, pincaps, cfg;
    uint8_t nconn;
    uint8_t conn[MAX_CONN];
} hda_widget_t;

typedef struct hda_dev {
    const hw_plat_t *p;
    volatile void *r;
    int dma64;
    uint8_t *corb, *rirb;
    uint64_t corb_phys, rirb_phys;
    uint32_t corb_n, rirb_n, corb_wp, rirb_rp;
    int corb_dead; /* the controller stopped taking verbs: give up on everything */
    uint32_t sd;   /* output stream descriptor offset */
    uint8_t *bdl, *buf;
    uint64_t bdl_phys, buf_phys;
    uint32_t cbl, bytes, prev_lpib, played;
    int running;
    /* the codec being walked */
    uint32_t codec, afg, afg_outamp, afg_inamp, afg_pcm, wstart, wend;
    hda_widget_t w[MAX_NID];
    /* the chosen path: path[0] = pin ... path[len-1] = DAC */
    uint8_t path[MAX_DEPTH], pidx[MAX_DEPTH];
    uint32_t len;
    hw_audio_t audio;
} hda_dev_t;

static uint32_t g_hda_n; /* controllers registered: hda0, hda1, ... */

static void put32(uint8_t *b, uint32_t off, uint32_t v)
{
    b[off] = (uint8_t) v;
    b[off + 1] = (uint8_t) (v >> 8);
    b[off + 2] = (uint8_t) (v >> 16);
    b[off + 3] = (uint8_t) (v >> 24);
}

static uint32_t get32(const volatile uint8_t *b, uint32_t off)
{
    return (uint32_t) b[off] | ((uint32_t) b[off + 1] << 8) | ((uint32_t) b[off + 2] << 16) |
           ((uint32_t) b[off + 3] << 24);
}

static int wait8(hda_dev_t *h, uint32_t off, uint8_t mask, uint8_t want, uint32_t timeout_us)
{
    for (uint32_t waited = 0;; waited += 10) {
        h->p->delay_us(h->p->ctx, 10);
        if ((hw_rd8(h->r, off) & mask) == want) return 1;
        if (waited >= timeout_us) return 0;
    }
}

static int wait16(hda_dev_t *h, uint32_t off, uint16_t mask, uint16_t want, uint32_t timeout_us)
{
    for (uint32_t waited = 0;; waited += 10) {
        h->p->delay_us(h->p->ctx, 10);
        if ((hw_rd16(h->r, off) & mask) == want) return 1;
        if (waited >= timeout_us) return 0;
    }
}

static int wait32(hda_dev_t *h, uint32_t off, uint32_t mask, uint32_t want, uint32_t timeout_us)
{
    h->p->delay_us(h->p->ctx, 10);
    return hw_wait32(h->p, h->r, off, mask, want, timeout_us);
}

static int dma_ok(const hda_dev_t *h, uint64_t phys, uint32_t size)
{
    return h->dma64 || ((phys + size - 1) >> 32) == 0;
}

/* ---- verbs --------------------------------------------------------------- */
/* Send one verb through the CORB and wait for its solicited response in the
 * RIRB. 0 = *resp written; -1 = no response (corb_dead set when the CORB was
 * not even consumed). */
static int verb(hda_dev_t *h, uint32_t v, uint32_t *resp)
{
    if (h->corb_dead) return -1;
    uint32_t cad = v >> 28;
    h->corb_wp = (h->corb_wp + 1) & (h->corb_n - 1);
    put32(h->corb, h->corb_wp * 4, v);
    hw_wmb();
    hw_wr16(h->r, CORBWP, (uint16_t) h->corb_wp);
    for (uint32_t waited = 0;; waited += 10) {
        h->p->delay_us(h->p->ctx, 10);
        uint32_t wp = hw_rd16(h->r, RIRBWP) & (h->rirb_n - 1);
        while (h->rirb_rp != wp) {
            h->rirb_rp = (h->rirb_rp + 1) & (h->rirb_n - 1);
            uint32_t res = get32(h->rirb, h->rirb_rp * 8), ex = get32(h->rirb, h->rirb_rp * 8 + 4);
            /* Response interrupt flag: the controller holds the next verb
             * until it is cleared (RINTCNT is 1). */
            hw_wr8(h->r, RIRBSTS, 0x05);
            if (ex & 0x10) continue;         /* unsolicited */
            if ((ex & 0xF) != cad) continue; /* a late answer from another codec */
            *resp = res;
            return 0;
        }
        if (waited >= T_VERB) break;
    }
    if ((hw_rd16(h->r, CORBRP) & 0xFF) != h->corb_wp) h->corb_dead = 1;
    return -1;
}

static uint32_t v12(uint32_t cad, uint32_t nid, uint32_t id, uint32_t payload)
{
    return (cad << 28) | ((nid & 0xFF) << 20) | ((id & 0xFFF) << 8) | (payload & 0xFF);
}

static uint32_t v4(uint32_t cad, uint32_t nid, uint32_t id, uint32_t payload)
{
    return (cad << 28) | ((nid & 0xFF) << 20) | ((id & 0xF) << 16) | (payload & 0xFFFF);
}

static int param(hda_dev_t *h, uint32_t nid, uint32_t par, uint32_t *out)
{
    return verb(h, v12(h->codec, nid, 0xF00, par), out);
}

static int set12(hda_dev_t *h, uint32_t nid, uint32_t id, uint32_t payload)
{
    uint32_t dummy;
    return verb(h, v12(h->codec, nid, id, payload), &dummy);
}

static int set4(hda_dev_t *h, uint32_t nid, uint32_t id, uint32_t payload)
{
    uint32_t dummy;
    return verb(h, v4(h->codec, nid, id, payload), &dummy);
}

/* ---- widget graph ---------------------------------------------------------- */
static int wtype(const hda_widget_t *w)
{
    return (int) ((w->caps >> 20) & 0xF);
}

/* Connection list, ranges expanded. */
static int read_conns(hda_dev_t *h, uint32_t nid)
{
    hda_widget_t *w = &h->w[nid];
    w->nconn = 0;
    if (!(w->caps & WCAP_CONNLIST)) return 0;
    uint32_t len;
    if (param(h, nid, PAR_CONNLEN, &len) != 0) return -1;
    uint32_t n = len & 0x7F, lng = (len >> 7) & 1;
    uint32_t per = lng ? 2 : 4, bits = lng ? 16 : 8;
    uint32_t idmask = lng ? 0x7FFF : 0x7F, rangebit = lng ? 0x8000 : 0x80;
    uint32_t prev = 0;
    for (uint32_t i = 0; i < n; i += per) {
        uint32_t resp;
        if (verb(h, v12(h->codec, nid, 0xF02, i), &resp) != 0) return -1;
        for (uint32_t j = 0; j < per && i + j < n; j++) {
            uint32_t e = (resp >> (j * bits)) & (lng ? 0xFFFF : 0xFF);
            uint32_t id = e & idmask;
            if ((e & rangebit) && prev && prev < id) {
                for (uint32_t x = prev + 1; x <= id && w->nconn < MAX_CONN; x++)
                    w->conn[w->nconn++] = (uint8_t) x;
            } else if (w->nconn < MAX_CONN && id < 256) {
                w->conn[w->nconn++] = (uint8_t) id;
            }
            prev = id;
        }
    }
    return 0;
}

static int in_graph(const hda_dev_t *h, uint32_t nid)
{
    return nid >= h->wstart && nid < h->wend;
}

/* A DAC that can take 48 kHz 16-bit PCM (an unreported format word is taken
 * on trust). -1 = the codec did not answer. */
static int dac_ok(hda_dev_t *h, uint32_t nid)
{
    const hda_widget_t *w = &h->w[nid];
    if (wtype(w) != W_OUT || (w->caps & WCAP_DIGITAL)) return 0;
    uint32_t pcm = h->afg_pcm;
    if ((w->caps & WCAP_FMTOVRD) && param(h, nid, PAR_PCM, &pcm) != 0) return -1;
    return pcm == 0 || ((pcm & PCM_48K) && (pcm & PCM_16BIT));
}

/* Depth-first from nid (at depth) to a DAC; fills path/pidx. 1 = found,
 * 0 = not through here, -1 = the codec did not answer. */
static int find_dac(hda_dev_t *h, uint32_t nid, uint32_t depth)
{
    const hda_widget_t *w = &h->w[nid];
    int t = wtype(w);
    if (t == W_OUT) {
        int ok = dac_ok(h, nid);
        if (ok != 1) return ok;
        h->path[depth] = (uint8_t) nid;
        h->len = depth + 1;
        return 1;
    }
    if (depth + 1 >= MAX_DEPTH) return 0;
    if (!(t == W_MIX || t == W_SEL || (t == W_PIN && depth == 0))) return 0;
    if (w->caps & WCAP_DIGITAL) return 0;
    h->path[depth] = (uint8_t) nid;
    for (uint32_t i = 0; i < w->nconn; i++) {
        uint32_t c = w->conn[i], seen = 0;
        if (!in_graph(h, c)) continue;
        for (uint32_t k = 0; k <= depth; k++) seen |= h->path[k] == c;
        if (seen) continue;
        h->pidx[depth] = (uint8_t) i;
        int found = find_dac(h, c, depth + 1);
        if (found) return found;
    }
    return 0;
}

/* Output pins ranked by default configuration: line out 5, speaker 4,
 * headphone 3, other jacks 2, "no physical connection" 1; 0 = not an analog
 * output pin. */
static uint32_t pin_rank(const hda_widget_t *w)
{
    if (wtype(w) != W_PIN || !(w->pincaps & PINCAP_OUT) || (w->caps & WCAP_DIGITAL)) return 0;
    if ((w->cfg >> 30) == 1) return 1;
    switch ((w->cfg >> 20) & 0xF) {
    case 0:
        return 5;
    case 1:
        return 4;
    case 2:
        return 3;
    default:
        return 2;
    }
}

/* Unmuted, both channels, at the widget's 0 dB step (or the top of its range
 * when 0 dB lies beyond it). */
static int set_amp(hda_dev_t *h, uint32_t nid, int out, uint32_t index)
{
    uint32_t caps = out ? h->afg_outamp : h->afg_inamp;
    if ((h->w[nid].caps & WCAP_AMPOVRD) && param(h, nid, out ? PAR_OUTAMP : PAR_INAMP, &caps) != 0)
        return -1;
    uint32_t off = caps & 0x7F, steps = (caps >> 8) & 0x7F;
    uint32_t gain = off <= steps ? off : steps;
    return set4(h, nid, 0x3, (out ? 0x8000u : 0x4000u) | 0x3000u | ((index & 0xF) << 8) | gain);
}

static int program_path(hda_dev_t *h)
{
    for (uint32_t k = 0; k < h->len; k++) {
        uint32_t nid = h->path[k];
        const hda_widget_t *w = &h->w[nid];
        int t = wtype(w), rc = 0;
        if (w->caps & WCAP_POWER) rc |= set12(h, nid, 0x705, 0); /* D0 */
        if (k + 1 < h->len) {
            if (t != W_MIX && w->nconn > 1) rc |= set12(h, nid, 0x701, h->pidx[k]);
            if (t != W_PIN && (w->caps & WCAP_INAMP)) rc |= set_amp(h, nid, 0, h->pidx[k]);
        }
        if (w->caps & WCAP_OUTAMP) rc |= set_amp(h, nid, 1, 0);
        if (t == W_PIN) {
            uint32_t ctl = 0x40; /* out enable */
            if (((w->cfg >> 20) & 0xF) == 2 && (w->pincaps & PINCAP_HP)) ctl |= 0x80;
            rc |= set12(h, nid, 0x707, ctl);
            if (w->pincaps & PINCAP_EAPD) rc |= set12(h, nid, 0x70C, 0x02);
        }
        if (t == W_OUT) {
            rc |= set4(h, nid, 0x2, FMT_48K_16_2CH);
            rc |= set12(h, nid, 0x706, STREAM_TAG << 4); /* channel 0 */
        }
        if (rc) return -1;
    }
    return 0;
}

/* Walk one codec: 1 = output path found (h->path), 0 = nothing usable,
 * -1 = the codec stopped answering. */
static int walk_codec(hda_dev_t *h)
{
    uint32_t sub, fgtype;
    h->afg = 0;
    if (param(h, 0, PAR_NODES, &sub) != 0) return -1;
    uint32_t start = (sub >> 16) & 0xFF, count = sub & 0xFF;
    for (uint32_t fg = start; fg < start + count && fg < 256; fg++) {
        if (param(h, fg, PAR_FGTYPE, &fgtype) != 0) return -1;
        if ((fgtype & 0xFF) == 1) {
            h->afg = fg;
            break;
        }
    }
    if (!h->afg) return 0;
    if (set12(h, h->afg, 0x705, 0) != 0) return -1; /* function group to D0 */
    if (param(h, h->afg, PAR_NODES, &sub) != 0 || param(h, h->afg, PAR_OUTAMP, &h->afg_outamp) ||
        param(h, h->afg, PAR_INAMP, &h->afg_inamp) || param(h, h->afg, PAR_PCM, &h->afg_pcm))
        return -1;
    h->wstart = (sub >> 16) & 0xFF;
    h->wend = h->wstart + (sub & 0xFF);
    if (h->wend > MAX_NID) h->wend = MAX_NID;
    hw_memset(h->w, 0, sizeof h->w);
    for (uint32_t nid = h->wstart; nid < h->wend; nid++) {
        hda_widget_t *w = &h->w[nid];
        if (param(h, nid, PAR_WCAPS, &w->caps) != 0) return -1;
        if (wtype(w) == W_PIN) {
            if (param(h, nid, PAR_PINCAPS, &w->pincaps) != 0) return -1;
            if (verb(h, v12(h->codec, nid, 0xF1C, 0), &w->cfg) != 0) return -1;
        }
        if (read_conns(h, nid) != 0) return -1;
    }
    for (uint32_t rank = 5; rank >= 1; rank--)
        for (uint32_t nid = h->wstart; nid < h->wend; nid++)
            if (pin_rank(&h->w[nid]) == rank) {
                int found = find_dac(h, nid, 0);
                if (found) return found;
            }
    return 0;
}

/* ---- stream ---------------------------------------------------------------- */
static int stream_reset(hda_dev_t *h)
{
    uint32_t ctl = hw_rd32(h->r, h->sd + SD_CTL) & 0x00FFFFFFu & ~(SDCTL_RUN | SDCTL_SRST);
    hw_wr32(h->r, h->sd + SD_CTL, ctl);
    if (!wait32(h, h->sd + SD_CTL, SDCTL_RUN, 0, T_SRST)) return 0;
    hw_wr32(h->r, h->sd + SD_CTL, ctl | SDCTL_SRST);
    /* Some controllers finish the reset at once and never show SRST as 1
     * (QEMU before 8.0 among them), so the latch is waited for but not
     * required, as Linux does. Leaving reset is required. */
    (void) wait32(h, h->sd + SD_CTL, SDCTL_SRST, SDCTL_SRST, T_LATCH);
    hw_wr32(h->r, h->sd + SD_CTL, ctl);
    return wait32(h, h->sd + SD_CTL, SDCTL_SRST, 0, T_SRST);
}

static void stream_stop(hda_dev_t *h)
{
    hw_wr32(h->r, h->sd + SD_CTL, hw_rd32(h->r, h->sd + SD_CTL) & 0x00FFFFFFu & ~SDCTL_RUN);
    h->running = 0;
}

static int hda_play(hw_audio_t *a, const int16_t *frames, uint32_t nframes)
{
    hda_dev_t *h = (hda_dev_t *) a->drv;
    if (!frames || nframes == 0) return -1;
    uint32_t bytes = nframes > BUF_BYTES / 4 ? BUF_BYTES : nframes * 4;
    h->running = 0;
    if (!stream_reset(h)) return -1;
    hw_memcpy(h->buf, frames, bytes);
    /* Two BDL entries of equal length, each a multiple of 128 bytes; the
     * padding is silence. */
    uint32_t total = (bytes + 255) & ~255u, half = total / 2;
    hw_memset(h->buf + bytes, 0, total - bytes);
    hw_memset(h->bdl, 0, 32);
    put32(h->bdl, 0, (uint32_t) h->buf_phys);
    put32(h->bdl, 4, (uint32_t) (h->buf_phys >> 32));
    put32(h->bdl, 8, half);
    put32(h->bdl, 16, (uint32_t) (h->buf_phys + half));
    put32(h->bdl, 20, (uint32_t) ((h->buf_phys + half) >> 32));
    put32(h->bdl, 24, half);
    hw_wr32(h->r, h->sd + SD_CBL, total);
    hw_wr16(h->r, h->sd + SD_LVI, 1);
    hw_wr16(h->r, h->sd + SD_FMT, (uint16_t) FMT_48K_16_2CH);
    hw_wr32(h->r, h->sd + SD_BDPL, (uint32_t) h->bdl_phys);
    hw_wr32(h->r, h->sd + SD_BDPU, (uint32_t) (h->bdl_phys >> 32));
    uint32_t ctl = (hw_rd32(h->r, h->sd + SD_CTL) & 0x000FFFFFu & ~(SDCTL_RUN | SDCTL_SRST)) |
                   (STREAM_TAG << 20);
    hw_wr32(h->r, h->sd + SD_CTL, ctl);
    hw_wmb();
    hw_wr32(h->r, h->sd + SD_CTL, ctl | SDCTL_RUN);
    if (!wait32(h, h->sd + SD_CTL, SDCTL_RUN, SDCTL_RUN, T_RING)) return -1;
    h->cbl = total;
    h->bytes = bytes;
    h->prev_lpib = 0;
    h->played = 0;
    h->running = 1;
    return 0;
}

static uint32_t hda_position(hw_audio_t *a)
{
    hda_dev_t *h = (hda_dev_t *) a->drv;
    if (!h->running) return h->played;
    uint32_t lpib = hw_rd32(h->r, h->sd + SD_LPIB);
    if (lpib >= h->cbl) lpib = 0;
    h->played += lpib >= h->prev_lpib ? lpib - h->prev_lpib : h->cbl - h->prev_lpib + lpib;
    h->prev_lpib = lpib;
    if (h->played >= h->bytes) { /* played through once: stop before it loops */
        stream_stop(h);
        h->played = h->bytes;
    }
    return h->played;
}

/* ---- bind ------------------------------------------------------------------ */
static uint32_t ring_entries(uint8_t size_reg, uint8_t *sel)
{
    uint32_t cap = (size_reg >> 4) & 0x7;
    if (cap & 4) {
        *sel = 2;
        return 256;
    }
    if (cap & 2) {
        *sel = 1;
        return 16;
    }
    if (cap & 1) {
        *sel = 0;
        return 2;
    }
    *sel = size_reg & 3; /* no capability bits: keep the one size it has */
    return *sel == 2 ? 256 : *sel == 1 ? 16 : 2;
}

static void note_ctrl(hw_line_t *note, const hw_pci_dev_t *d, uint32_t vmaj, uint32_t vmin)
{
    static const struct {
        uint16_t vendor, device;
        const char *name;
    } known[] = {
        {0x8086, 0x2668, "ICH6 "},  {0x8086, 0x27D8, "ICH7 "},     {0x8086, 0x293E, "ICH9 "},
        {0x8086, 0x3A3E, "ICH10 "}, {0x8086, 0x1C20, "6 Series "}, {0x1002, 0x4383, "SB7x0 "},
    };
    for (uint32_t i = 0; i < sizeof known / sizeof known[0]; i++)
        if (known[i].vendor == d->vendor && known[i].device == d->device)
            hw_line_s(note, known[i].name);
    hw_line_s(note, "HDA ");
    hw_line_u(note, vmaj);
    hw_line_c(note, '.');
    hw_line_u(note, vmin);
}

static const char *pin_kind(const hda_widget_t *w)
{
    if ((w->cfg >> 30) == 1) return "unconnected";
    switch ((w->cfg >> 20) & 0xF) {
    case 0:
        return "line out";
    case 1:
        return "speaker";
    case 2:
        return "headphone";
    default:
        return "other jack";
    }
}

hw_verdict_t hda_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r, hw_line_t *note)
{
    volatile void *regs = hw_pci_map_bar(p, d, 0);
    if (!regs) {
        hw_line_s(note, "BAR0 not assigned");
        return HW_N;
    }
    hda_dev_t *h = (hda_dev_t *) p->dma_alloc(p->ctx, sizeof(hda_dev_t), 64, 0);
    if (!h) {
        hw_line_s(note, "out of DMA memory");
        return HW_F;
    }
    h->p = p;
    h->r = regs;
    hw_pci_enable(p, d);
    uint32_t gcap = hw_rd16(regs, GCAP), vmaj = hw_rd8(regs, VMAJ), vmin = hw_rd8(regs, VMIN);
    if (gcap == 0xFFFF && vmaj == 0xFF) {
        hw_line_s(note, "registers read all-ones");
        return HW_F;
    }
    note_ctrl(note, d, vmaj, vmin);
    uint32_t iss = (gcap >> 8) & 0xF, oss = (gcap >> 12) & 0xF, bss = (gcap >> 3) & 0x1F;
    if (vmaj != 1 || oss == 0) {
        hw_line_s(note, vmaj != 1 ? ": version is not 1.x" : ": no output stream");
        return HW_B;
    }
    h->dma64 = (int) (gcap & 1);
    h->sd = SD(iss);

    /* Quiesce whatever firmware left running, then reset the controller:
     * CRST 0, hold, CRST 1, give codecs 1 ms to request an address. */
    hw_wr8(regs, CORBCTL, 0);
    hw_wr8(regs, RIRBCTL, 0);
    for (uint32_t i = 0; i < iss + oss + bss && i < 30; i++)
        hw_wr32(regs, SD(i) + SD_CTL, hw_rd32(regs, SD(i) + SD_CTL) & 0x00FFFFFFu & ~SDCTL_RUN);
    hw_wr32(regs, GCTL, hw_rd32(regs, GCTL) & ~GCTL_CRST);
    if (!wait32(h, GCTL, GCTL_CRST, 0, T_RESET)) {
        hw_line_s(note, ", controller will not enter reset");
        return HW_F;
    }
    p->delay_us(p->ctx, 100);
    hw_wr32(regs, GCTL, hw_rd32(regs, GCTL) | GCTL_CRST);
    if (!wait32(h, GCTL, GCTL_CRST, GCTL_CRST, T_RESET)) {
        hw_line_s(note, ", controller reset timed out");
        return HW_F;
    }
    p->delay_us(p->ctx, 1000);
    uint32_t present = hw_rd16(regs, STATESTS) & 0x7FFF;
    for (uint32_t waited = 0; !present && waited < T_CODEC; waited += 10) {
        p->delay_us(p->ctx, 10);
        present = hw_rd16(regs, STATESTS) & 0x7FFF;
    }
    hw_wr16(regs, STATESTS, 0x7FFF);
    if (!present) {
        hw_line_s(note, ", no codec");
        return HW_N;
    }

    /* CORB and RIRB at their largest supported sizes. */
    uint8_t csel, rsel;
    h->corb_n = ring_entries(hw_rd8(regs, CORBSIZE), &csel);
    h->rirb_n = ring_entries(hw_rd8(regs, RIRBSIZE), &rsel);
    h->corb = (uint8_t *) p->dma_alloc(p->ctx, 1024, 128, &h->corb_phys);
    h->rirb = (uint8_t *) p->dma_alloc(p->ctx, 2048, 128, &h->rirb_phys);
    h->bdl = (uint8_t *) p->dma_alloc(p->ctx, 256, 128, &h->bdl_phys);
    h->buf = (uint8_t *) p->dma_alloc(p->ctx, BUF_BYTES, 4096, &h->buf_phys);
    if (!h->corb || !h->rirb || !h->bdl || !h->buf) {
        hw_line_s(note, ", out of DMA memory");
        return HW_F;
    }
    if (!dma_ok(h, h->corb_phys, 1024) || !dma_ok(h, h->rirb_phys, 2048) ||
        !dma_ok(h, h->bdl_phys, 256) || !dma_ok(h, h->buf_phys, BUF_BYTES)) {
        hw_line_s(note, ", DMA memory above 4 GB and no 64-bit addressing");
        return HW_F;
    }
    if (!wait8(h, CORBCTL, CORBCTL_RUN, 0, T_RING) ||
        !wait8(h, RIRBCTL, RIRBCTL_DMAEN, 0, T_RING)) {
        hw_line_s(note, ", CORB/RIRB will not stop");
        return HW_F;
    }
    hw_wr8(regs, CORBSIZE, (uint8_t) ((hw_rd8(regs, CORBSIZE) & ~3u) | csel));
    hw_wr32(regs, CORBLBASE, (uint32_t) h->corb_phys);
    hw_wr32(regs, CORBUBASE, (uint32_t) (h->corb_phys >> 32));
    hw_wr16(regs, CORBWP, 0);
    hw_wr16(regs, CORBRP, CORBRP_RST);
    wait16(h, CORBRP, CORBRP_RST, CORBRP_RST, T_RING); /* QEMU clears it at once */
    hw_wr16(regs, CORBRP, 0);
    if (!wait16(h, CORBRP, 0xFFFF, 0, T_RING)) {
        hw_line_s(note, ", CORB read pointer will not reset");
        return HW_F;
    }
    hw_wr8(regs, RIRBSIZE, (uint8_t) ((hw_rd8(regs, RIRBSIZE) & ~3u) | rsel));
    hw_wr32(regs, RIRBLBASE, (uint32_t) h->rirb_phys);
    hw_wr32(regs, RIRBUBASE, (uint32_t) (h->rirb_phys >> 32));
    hw_wr16(regs, RIRBWP, RIRBWP_RST);
    hw_wr16(regs, RINTCNT, 1);
    hw_wr8(regs, RIRBSTS, 0x05);
    /* RINTCTL only raises the status flag: INTCTL and PCI INTx stay off. */
    hw_wr8(regs, RIRBCTL, RIRBCTL_DMAEN | RIRBCTL_RINTCTL);
    hw_wr8(regs, CORBCTL, CORBCTL_RUN);
    if (!wait8(h, CORBCTL, CORBCTL_RUN, CORBCTL_RUN, T_RING)) {
        hw_line_s(note, ", CORB will not run");
        return HW_F;
    }
    h->corb_wp = 0;
    h->rirb_rp = 0;

    /* Codecs: vendor ids of all, the first with an output path is used. */
    uint32_t answered = 0, walk_failed = 0, found = 0, vendor = 0, found_vendor = 0, found_cad = 0;
    for (uint32_t cad = 0; cad < 15 && !h->corb_dead; cad++) {
        if (!(present & (1u << cad))) continue;
        h->codec = cad;
        if (param(h, 0, PAR_VENDOR, &vendor) != 0) continue;
        answered |= 1u << cad;
        if (found) continue;
        int w = walk_codec(h);
        if (w == 1) {
            found = 1;
            found_vendor = vendor;
            found_cad = cad;
        } else if (w < 0) {
            walk_failed |= 1u << cad;
        }
    }
    if (h->corb_dead) {
        hw_line_s(note, ", CORB not consumed");
        return HW_F;
    }
    if (!answered) {
        hw_line_s(note, ", codec did not answer (RIRB empty)");
        return HW_F;
    }
    if (!found && walk_failed) {
        hw_line_s(note, ", codec stopped answering during the widget walk");
        return HW_F;
    }
    if (!found) {
        hw_line_s(note, ", no output pin with a usable DAC");
        return HW_N;
    }
    h->codec = found_cad;
    if (program_path(h) != 0) {
        hw_line_s(note, ", codec stopped answering while the path was set");
        return HW_F;
    }
    hw_line_s(note, ", codec ");
    hw_line_u(note, found_cad);
    hw_line_s(note, ": ");
    hw_line_x(note, found_vendor >> 16, 4);
    hw_line_c(note, ':');
    hw_line_x(note, found_vendor & 0xFFFF, 4);
    hw_line_s(note, ", out pin 0x");
    hw_line_x(note, h->path[0], 2);
    hw_line_s(note, " (");
    hw_line_s(note, pin_kind(&h->w[h->path[0]]));
    hw_line_s(note, ") via DAC 0x");
    hw_line_x(note, h->path[h->len - 1], 2);
    if (!stream_reset(h)) {
        hw_line_s(note, "; output stream reset timed out");
        return HW_F;
    }
    hw_audio_t *a = &h->audio;
    a->name[0] = 'h';
    a->name[1] = 'd';
    a->name[2] = 'a';
    a->name[3] = (char) ('0' + g_hda_n % 10);
    a->name[4] = 0;
    a->codecs = answered;
    a->drv = h;
    a->play = hda_play;
    a->position = hda_position;
    if (hw_reg_audio(r, a) != 0) return HW_N;
    g_hda_n++;
    return HW_T;
}
