/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dtmf.c — see dtmf.h. Integer Goertzel, table-sine generator, RFC 4733,
 * SIP INFO dtmf-relay, menu driver. No floating point anywhere. */
#include "dtmf.h"
#include "legacy_util.h"

const char DTMF_SYMBOLS[16] = {'1', '2', '3', 'A', '4', '5', '6', 'B',
                               '7', '8', '9', 'C', '*', '0', '#', 'D'};

/* row freqs 697 770 852 941 ; col freqs 1209 1336 1477 1633 */
/* Goertzel 2*cos(2*pi*f/8000) in Q14 (see gen script in commit log). */
static const int32_t COEFF_Q14[8] = {27980, 26956, 25701, 24219, 19073, 16325, 13085, 9315};

/* Q16 phase increments for a 256-entry table at 8 kHz. */
static const uint32_t PHASE_INC[8] = {1461715, 1614807, 1786774, 1973420,
                                      2535457, 2801795, 3097494, 3424649};

/* Q15 sine table, 256 entries (generated, exact ints). */
static const int16_t SINE[256] = {
    0,      804,    1608,   2410,   3212,   4011,   4808,   5602,   6393,   7179,   7962,   8739,
    9512,   10278,  11039,  11793,  12539,  13279,  14010,  14732,  15446,  16151,  16846,  17530,
    18204,  18868,  19519,  20159,  20787,  21403,  22005,  22594,  23170,  23731,  24279,  24811,
    25329,  25832,  26319,  26790,  27245,  27683,  28105,  28510,  28898,  29268,  29621,  29956,
    30273,  30571,  30852,  31113,  31356,  31580,  31785,  31971,  32137,  32285,  32412,  32521,
    32609,  32678,  32728,  32757,  32767,  32757,  32728,  32678,  32609,  32521,  32412,  32285,
    32137,  31971,  31785,  31580,  31356,  31113,  30852,  30571,  30273,  29956,  29621,  29268,
    28898,  28510,  28105,  27683,  27245,  26790,  26319,  25832,  25329,  24811,  24279,  23731,
    23170,  22594,  22005,  21403,  20787,  20159,  19519,  18868,  18204,  17530,  16846,  16151,
    15446,  14732,  14010,  13279,  12539,  11793,  11039,  10278,  9512,   8739,   7962,   7179,
    6393,   5602,   4808,   4011,   3212,   2410,   1608,   804,    0,      -804,   -1608,  -2410,
    -3212,  -4011,  -4808,  -5602,  -6393,  -7179,  -7962,  -8739,  -9512,  -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530, -18204, -18868, -19519, -20159,
    -20787, -21403, -22005, -22594, -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956, -30273, -30571, -30852, -31113,
    -31356, -31580, -31785, -31971, -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285, -32137, -31971, -31785, -31580,
    -31356, -31113, -30852, -30571, -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731, -23170, -22594, -22005, -21403,
    -20787, -20159, -19519, -18868, -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278, -9512,  -8739,  -7962,  -7179,  -6393,  -5602,  -4808,  -4011,
    -3212,  -2410,  -1608,  -804};

void dtmf_det_init(dtmf_detector *d)
{
    /* 8 dB forward twist (power ratio 6.31 -> Q8 1615), 4 dB reverse (2.51 ->
     * Q8 643), and the selected tone must dominate off-tones by ~6 dB (4x). */
    d->fwd_twist_q8 = 1615;
    d->rev_twist_q8 = 643;
    d->rel_threshold_q8 = 1024; /* 4.0x in Q8 */
}

/* Goertzel energy of a block at frequency index f (0..7). */
static int64_t goertzel(const int16_t *pcm, int f)
{
    int64_t s1 = 0, s2 = 0;
    int64_t coeff = COEFF_Q14[f];
    for (uint32_t n = 0; n < DTMF_BLOCK; n++) {
        int64_t s0 = (int64_t) pcm[n] + ((coeff * s1) >> 14) - s2;
        s2 = s1;
        s1 = s0;
    }
    int64_t prod = (s1 * s2) >> 14;
    int64_t power = s1 * s1 + s2 * s2 - coeff * prod;
    if (power < 0) power = 0;
    return power;
}

char dtmf_detect_block(const dtmf_detector *d, const int16_t *pcm)
{
    int64_t e[8];
    for (int i = 0; i < 8; i++) e[i] = goertzel(pcm, i);

    /* best row (0..3) and best col (4..7) */
    int row = 0, col = 4;
    for (int i = 1; i < 4; i++)
        if (e[i] > e[row]) row = i;
    for (int i = 5; i < 8; i++)
        if (e[i] > e[col]) col = i;

    int64_t er = e[row], ec = e[col];

    /* absolute energy floor: reject silence/low-level noise.
     * A single DTMF-amplitude tone over the block yields ~1e11+; require both
     * groups to carry real energy. */
    const int64_t FLOOR = 100000000LL; /* 1e8 */
    if (er < FLOOR || ec < FLOOR) return 0;

    /* twist check (power-ratio form) */
    if (er >= ec) {
        /* forward twist: col weaker; require er <= ec * fwd_ratio */
        if (er > (ec * (int64_t) d->fwd_twist_q8) >> 8) return 0;
    } else {
        /* reverse twist: col stronger; require ec <= er * rev_ratio */
        if (ec > (er * (int64_t) d->rev_twist_q8) >> 8) return 0;
    }

    /* dominance: each selected tone must exceed every off-tone in its own
     * group by rel_threshold, and the opposite group's runner-up too. This
     * rejects single tones (where one group has no real energy — caught by
     * FLOOR) and broadband noise (no clear winner). */
    int64_t thr = (int64_t) d->rel_threshold_q8;
    for (int i = 0; i < 4; i++)
        if (i != row && er < (e[i] * thr) >> 8) return 0;
    for (int i = 4; i < 8; i++)
        if (i != col && ec < (e[i] * thr) >> 8) return 0;

    int idx = row * 4 + (col - 4);
    return DTMF_SYMBOLS[idx];
}

bool dtmf_generate(char symbol, int16_t *pcm, uint32_t n, uint16_t amp, uint32_t *phase_row,
                   uint32_t *phase_col)
{
    int idx = -1;
    for (int i = 0; i < 16; i++)
        if (DTMF_SYMBOLS[i] == symbol) idx = i;
    if (idx < 0) return false;
    int row = idx / 4;       /* 0..3 */
    int col = 4 + (idx % 4); /* 4..7 */
    uint32_t pr = *phase_row, pc = *phase_col;
    for (uint32_t i = 0; i < n; i++) {
        int32_t sr = SINE[(pr >> 16) & 0xFF];
        int32_t sc = SINE[(pc >> 16) & 0xFF];
        /* each tone scaled by amp/32768; sum of two tones */
        int32_t v = (int32_t) ((sr * (int32_t) amp) >> 15) + (int32_t) ((sc * (int32_t) amp) >> 15);
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        pcm[i] = (int16_t) v;
        pr += PHASE_INC[row];
        pc += PHASE_INC[col];
    }
    *phase_row = pr;
    *phase_col = pc;
    return true;
}

int dtmf_symbol_to_event(char symbol)
{
    /* RFC 4733: 0-9 -> 0-9, * -> 10, # -> 11, A-D -> 12-15 */
    if (symbol >= '0' && symbol <= '9') return symbol - '0';
    if (symbol == '*') return 10;
    if (symbol == '#') return 11;
    if (symbol >= 'A' && symbol <= 'D') return 12 + (symbol - 'A');
    return -1;
}

char dtmf_event_to_symbol(uint8_t event)
{
    if (event <= 9) return (char) ('0' + event);
    if (event == 10) return '*';
    if (event == 11) return '#';
    if (event >= 12 && event <= 15) return (char) ('A' + (event - 12));
    return 0;
}

uint32_t dtmf_rtp_encode(const dtmf_rtp_event *e, uint8_t *out, uint32_t cap)
{
    if (cap < 4 || e->event > 15 || e->volume > 63) return 0;
    out[0] = e->event;
    out[1] = (uint8_t) ((e->end ? 0x80 : 0) | (e->volume & 0x3F)); /* E, R=0, volume */
    lg_put16(out + 2, e->duration);
    return 4;
}

bool dtmf_rtp_decode(const uint8_t *in, uint32_t len, dtmf_rtp_event *e)
{
    if (len < 4) return false;
    e->event = in[0];
    if (e->event > 15) return false;
    e->end = (in[1] & 0x80) != 0;
    e->volume = in[1] & 0x3F;
    e->duration = lg_get16(in + 2);
    return true;
}

/* ===== SIP INFO application/dtmf-relay ===== */
/* lines "Signal=<c>" and "Duration=<ms>" separated by CRLF or LF */
bool dtmf_info_parse(const uint8_t *body, uint32_t len, char *symbol, uint32_t *duration_ms)
{
    bool got_sig = false;
    *duration_ms = 0;
    uint32_t i = 0;
    while (i < len) {
        /* find key start */
        uint32_t ls = i;
        while (i < len && body[i] != '\n') i++;
        uint32_t le = i; /* exclusive */
        if (le > ls && body[le - 1] == '\r') le--;
        if (i < len) i++; /* skip LF */
        /* split on '=' */
        uint32_t eq = ls;
        while (eq < le && body[eq] != '=') eq++;
        if (eq >= le) continue;
        const char *key = (const char *) body + ls;
        uint32_t klen = eq - ls;
        const char *val = (const char *) body + eq + 1;
        uint32_t vlen = le - eq - 1;
        if (klen == 6 && lg_ascii_ieq(key, "Signal", 6)) {
            if (vlen >= 1) {
                *symbol = val[0];
                got_sig = true;
            }
        } else if (klen == 8 && lg_ascii_ieq(key, "Duration", 8)) {
            uint32_t v = 0;
            for (uint32_t k = 0; k < vlen && lg_is_digit((uint8_t) val[k]); k++)
                v = v * 10 + (uint32_t) (val[k] - '0');
            *duration_ms = v;
        }
    }
    return got_sig;
}

uint32_t dtmf_info_build(char symbol, uint32_t duration_ms, uint8_t *out, uint32_t cap)
{
    if (dtmf_symbol_to_event(symbol) < 0) return 0;
    lg_writer w;
    lg_w_init(&w, out, cap);
    lg_w_str(&w, "Signal=");
    lg_w_byte(&w, (uint8_t) symbol);
    lg_w_str(&w, "\r\nDuration=");
    lg_w_u32(&w, duration_ms);
    lg_w_str(&w, "\r\n");
    if (!lg_w_ok(&w)) return 0;
    return w.len;
}

/* ===== menu driver ===== */
void dtmf_menu_init(dtmf_menu *m, const dtmf_menu_node *nodes, uint8_t nnodes, uint8_t start)
{
    m->nodes = nodes;
    m->nnodes = nnodes;
    m->current = start;
    m->needs_strong_auth = false;
}

bool dtmf_menu_press(dtmf_menu *m, char digit)
{
    if (m->current >= m->nnodes) return false;
    const dtmf_menu_node *node = &m->nodes[m->current];
    for (uint8_t i = 0; i < node->nedges && i < DTMF_MENU_MAX_EDGES; i++) {
        if (node->edges[i].digit == digit) {
            if (node->edges[i].target >= m->nnodes) return false;
            m->current = node->edges[i].target;
            /* A confirm edge never authorizes on its own: it flags that the
             * host's configured strong authentication must run next. */
            if (node->edges[i].is_confirm) m->needs_strong_auth = true;
            return true;
        }
    }
    return false;
}
