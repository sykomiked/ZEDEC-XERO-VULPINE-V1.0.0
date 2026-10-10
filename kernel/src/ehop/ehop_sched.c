/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ehop_sched.c — the band plan, the AM/FM/PM schedule and its involutive
 * application, plus the byte helpers the other ehop files share.
 *
 * Schedule derivation (all SHAKE256, kernel/src/mlkem/keccak.h):
 *   seed[32] || r[4] = SHAKE256("ZXV-EHOP-v1/sched" || key || le32 chan ||
 *                               le32 epoch || le32 sender || le64 seq || cfg)
 *   start_on   = r[0] & 1
 *   phase      = (pm_phase + pm_step * X + (le16(r[1..2]) * rate >> 16))
 *                mod rate,  X = 0 | epoch | seq by pm_mode
 *   first_len  = rate - phase                       (1 .. rate words)
 *   hop stream = ChaCha20 keystream under key = seed (nonce 0, counter 0..)
 *   next length (FM hop) = rate - hop + (le16(stream) * (2*hop + 1) >> 16)
 *   next depth  (AM)     = the k-th allowed mode, k = stream_byte * count >> 8
 * The stream is read only when it is needed (hop > 0, or more than one AM
 * depth), and always in the same order on both ends.
 */
#include "ehop_internal.h"
#include "../mlkem/keccak.h"
#include "../tls/aead.h"

/* ===== byte helpers ===== */
void ehop_wipe(void *p, uint32_t n)
{
    volatile uint8_t *v = (volatile uint8_t *) p;
    uint32_t i;
    for (i = 0; i < n; i++) v[i] = 0;
}

void ehop_cpy(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    volatile uint8_t *d = dst;
    uint32_t i;
    for (i = 0; i < n; i++) d[i] = src[i];
}

int ehop_ct_eq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint32_t i;
    uint8_t d = 0;
    for (i = 0; i < n; i++) d = (uint8_t) (d | (a[i] ^ b[i]));
    return (int) (1u & ((uint32_t) (d - 1u) >> 8));
}

void ehop_le16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

void ehop_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

void ehop_le64(uint8_t *p, uint64_t v)
{
    ehop_le32(p, (uint32_t) v);
    ehop_le32(p + 4, (uint32_t) (v >> 32));
}

uint32_t ehop_rd16(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8);
}

uint32_t ehop_rd32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

uint64_t ehop_rd64(const uint8_t *p)
{
    return (uint64_t) ehop_rd32(p) | ((uint64_t) ehop_rd32(p + 4) << 32);
}

uint32_t ehop_put_str(uint8_t *dst, const char *s)
{
    uint32_t n = 0;
    while (s[n]) {
        dst[n] = (uint8_t) s[n];
        n++;
    }
    return n;
}

void ehop_cfg_bytes(const ehop_cfg_t *cfg, uint8_t out[EHOP_CFG_BYTES])
{
    out[0] = cfg->width;
    out[1] = cfg->band;
    out[2] = cfg->am_mask;
    out[3] = cfg->pm_mode;
    ehop_le16(out + 4, cfg->fm_rate);
    ehop_le16(out + 6, cfg->fm_hop);
    ehop_le16(out + 8, cfg->pm_phase);
    ehop_le16(out + 10, cfg->pm_step);
}

/* ===== band plan =====
 * Enum order is priority order: CONTROL forwards first, BULK last. */
#define PM_ALL                                                                                     \
    ((uint8_t) ((1u << EHOP_PM_STATIC) | (1u << EHOP_PM_PER_EPOCH) | (1u << EHOP_PM_PER_FRAME)))

static const ehop_band_info_t g_bands[EHOP_BAND_COUNT] = {
    {"control", 1, 7, EHOP_AM_FULL | EHOP_AM_HALF, PM_ALL, 0, 512 + EHOP_OVERHEAD, 10},
    {"settle", 8, 31, EHOP_AM_FULL | EHOP_AM_HALF | EHOP_AM_PAIR, PM_ALL, 1, 1024 + EHOP_OVERHEAD,
     50},
    {"media", 32, 127, EHOP_AM_NIBBLE | EHOP_AM_PAIR,
     (uint8_t) ((1u << EHOP_PM_STATIC) | (1u << EHOP_PM_PER_FRAME)), 2, 1400, 30},
    {"chat", 128, 511, EHOP_AM_ALL, PM_ALL, 3, EHOP_MAX_FRAME, 250},
    {"bulk", 512, EHOP_RATE_MAX, EHOP_AM_ALL, PM_ALL, 4, EHOP_MAX_FRAME, 2000},
};

static const uint8_t g_class_band[EHOP_MSG_COUNT] = {
    EHOP_BAND_CONTROL, /* CONTROL */
    EHOP_BAND_CONTROL, /* ROUTING */
    EHOP_BAND_SETTLE,  /* PAYMENT */
    EHOP_BAND_SETTLE,  /* SETTLEMENT */
    EHOP_BAND_MEDIA,   /* VOICE */
    EHOP_BAND_MEDIA,   /* VIDEO */
    EHOP_BAND_CHAT,    /* CHAT */
    EHOP_BAND_CHAT,    /* ALERT */
    EHOP_BAND_BULK,    /* FILE */
};

const ehop_band_info_t *ehop_band_info(uint32_t band)
{
    if (band >= EHOP_BAND_COUNT) return NULL;
    return &g_bands[band];
}

uint32_t ehop_band_for_class(uint32_t msg_class)
{
    if (msg_class >= EHOP_MSG_COUNT) return EHOP_BAND_BULK;
    return g_class_band[msg_class];
}

int ehop_cfg_check(const ehop_cfg_t *cfg)
{
    const ehop_band_info_t *b;
    if (!cfg) return EHOP_EARG;
    if (cfg->width != 2 && cfg->width != 4 && cfg->width != 8) return EHOP_EARG;
    if (cfg->fm_rate < 1 || cfg->fm_rate > EHOP_RATE_MAX) return EHOP_EARG;
    if (cfg->fm_hop >= cfg->fm_rate) return EHOP_EARG;
    if (cfg->pm_phase >= cfg->fm_rate || cfg->pm_step >= cfg->fm_rate) return EHOP_EARG;
    if (cfg->pm_mode > EHOP_PM_PER_FRAME) return EHOP_EARG;
    if (cfg->am_mask == 0 || (cfg->am_mask & ~EHOP_AM_ALL) != 0) return EHOP_EARG;
    b = ehop_band_info(cfg->band);
    if (!b) return EHOP_EBAND;
    if (cfg->fm_rate < b->fm_min || cfg->fm_rate > b->fm_max) return EHOP_EBAND;
    if ((cfg->am_mask & ~b->am_mask) != 0) return EHOP_EBAND;
    if ((b->pm_modes & (1u << cfg->pm_mode)) == 0) return EHOP_EBAND;
    return EHOP_OK;
}

/* ===== schedule ===== */

/* x mod r for a 64-bit x and 1 <= r <= 4096, with 32-bit operations only. */
static uint32_t mod64_small(uint64_t x, uint32_t r)
{
    uint32_t hi = (uint32_t) (x >> 32), lo = (uint32_t) x;
    uint32_t p32 = (0xFFFFFFFFu % r + 1u) % r; /* 2^32 mod r */
    return ((hi % r) * p32 % r + lo % r) % r;
}

static uint32_t popcount4(uint32_t m)
{
    return (m & 1u) + ((m >> 1) & 1u) + ((m >> 2) & 1u) + ((m >> 3) & 1u);
}

int ehop_schedule_init(ehop_schedule_t *s, const uint8_t key[EHOP_KEY_BYTES], const ehop_cfg_t *cfg,
                       uint32_t chan_id, uint32_t epoch, uint32_t sender, uint64_t seq)
{
    uint8_t in[17 + EHOP_KEY_BYTES + 4 + 4 + 4 + 8 + EHOP_CFG_BYTES];
    uint8_t out[36];
    uint32_t n, rate, phase, x = 0;
    int rc;
    if (!s || !key) return EHOP_EARG;
    rc = ehop_cfg_check(cfg);
    if (rc != EHOP_OK) return rc;
    n = ehop_put_str(in, "ZXV-EHOP-v1/sched");
    ehop_cpy(in + n, key, EHOP_KEY_BYTES);
    n += EHOP_KEY_BYTES;
    ehop_le32(in + n, chan_id);
    n += 4;
    ehop_le32(in + n, epoch);
    n += 4;
    ehop_le32(in + n, sender);
    n += 4;
    ehop_le64(in + n, seq);
    n += 8;
    ehop_cfg_bytes(cfg, in + n);
    n += EHOP_CFG_BYTES;
    shake256(in, n, out, sizeof out);

    rate = cfg->fm_rate;
    ehop_cpy(s->seed, out, 32);
    s->width = cfg->width;
    s->am_mask = cfg->am_mask;
    s->am_fixed = popcount4(cfg->am_mask) == 1 ? cfg->am_mask : 0;
    s->start_on = (uint8_t) (out[32] & 1u);
    s->fm_rate = (uint16_t) rate;
    s->fm_hop = cfg->fm_hop;
    if (cfg->pm_mode == EHOP_PM_PER_EPOCH)
        x = epoch % rate;
    else if (cfg->pm_mode == EHOP_PM_PER_FRAME)
        x = mod64_small(seq, rate);
    phase = cfg->pm_phase + (cfg->pm_step * x) % rate + ((ehop_rd16(out + 33) * rate) >> 16);
    phase %= rate;
    s->first_len = (uint16_t) (rate - phase);
    ehop_wipe(in, sizeof in);
    ehop_wipe(out, sizeof out);
    return EHOP_OK;
}

void ehop_schedule_wipe(ehop_schedule_t *s)
{
    if (s) ehop_wipe(s, sizeof *s);
}

/* The keyed hop stream, read lazily: ChaCha20 blocks keyed by the
 * SHAKE256-derived seed (block counter from 0, all-zero nonce; the seed is
 * unique per frame, so a (key, nonce) pair is never reused). ChaCha20 is the
 * expansion PRF because it is several times faster than this tree's
 * Keccak-f[1600] per output byte, and a low FM rate reads a lot of stream. */
typedef struct {
    const uint8_t *seed;
    uint32_t ctr, pos;
    uint8_t buf[CHACHA20_BLOCK];
} hop_stream_t;

static uint32_t stream_byte(hop_stream_t *st)
{
    if (st->pos >= sizeof st->buf) {
        static const uint8_t zero_nonce[CHACHA20_NONCE_LEN] = {0};
        chacha20_block(st->seed, st->ctr++, zero_nonce, st->buf);
        st->pos = 0;
    }
    return st->buf[st->pos++];
}

static uint32_t next_len(const ehop_schedule_t *s, hop_stream_t *st)
{
    uint32_t v, span;
    if (s->fm_hop == 0) return s->fm_rate;
    v = stream_byte(st);
    v |= stream_byte(st) << 8;
    span = 2u * s->fm_hop + 1u;
    return (uint32_t) s->fm_rate - s->fm_hop + ((v * span) >> 16);
}

static uint32_t next_mode(const ehop_schedule_t *s, hop_stream_t *st)
{
    uint32_t k, bit;
    if (s->am_fixed) return s->am_fixed;
    k = (stream_byte(st) * popcount4(s->am_mask)) >> 8;
    for (bit = 1; bit <= EHOP_AM_FULL; bit <<= 1) {
        if (s->am_mask & bit) {
            if (k == 0) return bit;
            k--;
        }
    }
    return EHOP_AM_FULL; /* unreachable: am_mask is non-empty */
}

/* One word (or a short tail of n < width bytes). Every case is its own
 * inverse. */
static void reorder(uint8_t *p, uint32_t n, uint32_t mode)
{
    uint32_t i, h;
    uint8_t t;
    switch (mode) {
    case EHOP_AM_NIBBLE:
        for (i = 0; i < n; i++) p[i] = (uint8_t) ((p[i] << 4) | (p[i] >> 4));
        break;
    case EHOP_AM_PAIR:
        for (i = 0; i + 1 < n; i += 2) {
            t = p[i];
            p[i] = p[i + 1];
            p[i + 1] = t;
        }
        break;
    case EHOP_AM_HALF:
        h = n >> 1;
        for (i = 0; i < h; i++) {
            t = p[i];
            p[i] = p[n - h + i];
            p[n - h + i] = t;
        }
        break;
    default: /* EHOP_AM_FULL */
        for (i = 0; i < (n >> 1); i++) {
            t = p[i];
            p[i] = p[n - 1 - i];
            p[n - 1 - i] = t;
        }
        break;
    }
}

/* Full-width words, specialised so the common case runs without a per-byte
 * loop. Still pure byte moves: no alignment or host-endianness assumption. */
static void reorder_run(uint8_t *p, uint32_t nbytes, uint32_t w, uint32_t mode)
{
    uint32_t off;
    if (mode == EHOP_AM_NIBBLE) {
        for (off = 0; off < nbytes; off++) p[off] = (uint8_t) ((p[off] << 4) | (p[off] >> 4));
        return;
    }
    if (w == 2 || mode == EHOP_AM_PAIR) {
        /* For 16-bit words every byte-moving depth is the byte swap. */
        for (off = 0; off + 1 < nbytes; off += 2) {
            uint8_t t = p[off];
            p[off] = p[off + 1];
            p[off + 1] = t;
        }
        return;
    }
    if (w == 4) {
        for (off = 0; off + 4 <= nbytes; off += 4) {
            uint8_t a = p[off], b = p[off + 1], c = p[off + 2], d = p[off + 3];
            if (mode == EHOP_AM_FULL) {
                p[off] = d;
                p[off + 1] = c;
                p[off + 2] = b;
                p[off + 3] = a;
            } else { /* HALF */
                p[off] = c;
                p[off + 1] = d;
                p[off + 2] = a;
                p[off + 3] = b;
            }
        }
        return;
    }
    for (off = 0; off + 8 <= nbytes; off += 8) {
        uint32_t lo = (uint32_t) p[off] | ((uint32_t) p[off + 1] << 8) |
                      ((uint32_t) p[off + 2] << 16) | ((uint32_t) p[off + 3] << 24);
        uint32_t hi = (uint32_t) p[off + 4] | ((uint32_t) p[off + 5] << 8) |
                      ((uint32_t) p[off + 6] << 16) | ((uint32_t) p[off + 7] << 24);
        if (mode == EHOP_AM_FULL) {
            uint32_t t = lo;
            lo = (hi >> 24) | ((hi >> 8) & 0xFF00u) | ((hi << 8) & 0xFF0000u) | (hi << 24);
            hi = (t >> 24) | ((t >> 8) & 0xFF00u) | ((t << 8) & 0xFF0000u) | (t << 24);
        } else { /* HALF */
            uint32_t t = lo;
            lo = hi;
            hi = t;
        }
        ehop_le32(p + off, lo);
        ehop_le32(p + off + 4, hi);
    }
}

void ehop_apply(const ehop_schedule_t *s, uint8_t *buf, uint32_t len)
{
    hop_stream_t st;
    uint32_t off = 0, w, seg, on, mode = 0;
    if (!s || !buf) return;
    w = s->width;
    st.seed = s->seed;
    st.ctr = 0;
    st.pos = sizeof st.buf;
    seg = s->first_len;
    on = s->start_on;
    if (on) mode = next_mode(s, &st);
    while (off < len) {
        uint32_t nb = seg * w, end;
        if (nb > len - off) nb = len - off;
        end = off + nb;
        if (on) {
            uint32_t full = nb - nb % w;
            reorder_run(buf + off, full, w, mode);
            if (full < nb) /* tail: a short final word */
                reorder(buf + off + full, nb - full, mode);
        }
        off = end;
        if (off >= len) break;
        on ^= 1u;
        seg = next_len(s, &st);
        if (on) mode = next_mode(s, &st);
    }
    ehop_wipe(&st.buf, sizeof st.buf);
}
