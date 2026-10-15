/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxl_synth.c — the launch sounds, made live in integers. See zxl_synth.h.
 *
 * Units: times in milliseconds while building (48 frames each), amplitudes in
 * thousandths, pans in thousandths from -1000 (left) to 1000 (right), ratios
 * in thousandths of the note's frequency. The numbers in the recipes are the
 * reference script's (tools/launch/launch_sounds.py), read across one for
 * one. */
#include "zxl_synth.h"

#include "zxl_synth_tab.inc"

#define Q30      (1u << 30)
#define FPMS     48u        /* frames per millisecond */
#define DEAD_Q30 (1u << 18) /* about -72 dB: a decayed voice stops */

enum { K_TONE = 1, K_NOISE, K_SWEEP };
enum { T_GLASS = 0, T_MARIMBA, T_CHIP, T_FLUTE, T_BELL, T_COIN, T_PLUCK, T_STRINGS };

/* ---- numbers ------------------------------------------------------------- */
static int32_t sinq(uint32_t ph) /* Q15 */
{
    uint32_t i = (ph >> 20) & 4095u, q = i >> 10, k = i & 1023u;
    int32_t v = q & 1u ? QSIN[1024u - k] : QSIN[k];
    return q & 2u ? -v : v;
}

static uint32_t mhz_of(int32_t midi)
{
    if (midi < 0) midi = 0;
    if (midi > 131) midi = 131;
    return TOP_MHZ[midi % 12] >> (10 - midi / 12);
}

/* phase step for f (mHz) times ratio (thousandths) at 48 kHz */
static uint32_t inc_of(uint32_t mhz, uint32_t ratio_milli)
{
    uint64_t f = (uint64_t) mhz * ratio_milli / 1000u; /* mHz */
    return (uint32_t) ((f << 32) / (ZXL_SYN_RATE * 1000ull));
}

/* exp(-1/n) in Q30, near enough for n in the hundreds and up */
static uint32_t decay_mul(uint32_t frames)
{
    if (frames < 2) frames = 2;
    return Q30 - (Q30 + frames / 2) / frames;
}

static uint64_t qmul(uint64_t a, uint64_t b)
{
    return (a * b) >> 30;
}

/* g (Q30) with g^n = ratio (Q30), by halving: the sweep's per-frame glide */
static uint32_t glide_of(uint64_t ratio_q30, uint32_t n)
{
    uint64_t lo = Q30 / 2, hi = (uint64_t) Q30 * 2;
    for (int it = 0; it < 40; it++) {
        uint64_t mid = (lo + hi) / 2, r = Q30, b = mid;
        uint32_t e = n;
        while (e && r < ((uint64_t) 1 << 50)) {
            if (e & 1u) r = qmul(r, b);
            b = qmul(b, b);
            if (b > ((uint64_t) 1 << 50)) b = (uint64_t) 1 << 50;
            e >>= 1;
        }
        if (r < ratio_q30)
            lo = mid;
        else
            hi = mid;
    }
    return (uint32_t) ((lo + hi) / 2);
}

/* the pan law the reference uses: angle (p + 1) pi / 4, L = cos, R = sin */
static void pan_gains(int32_t pan_milli, int32_t *l, int32_t *r)
{
    if (pan_milli < -1000) pan_milli = -1000;
    if (pan_milli > 1000) pan_milli = 1000;
    uint32_t k = (uint32_t) ((pan_milli + 1000) * 1024 / 2000);
    *l = QSIN[1024u - k];
    *r = QSIN[k];
}

/* ---- building a cue ------------------------------------------------------ */
static zxl_note_t *note_new(zxl_cue_t *c, uint32_t kind, uint32_t st_ms, int32_t amp_milli,
                            int32_t pan_milli)
{
    if (c->nn >= ZXL_SYN_NOTES) return 0;
    uint32_t start = st_ms * FPMS + c->b_delay;
    if (start >= c->len) return 0; /* an echo past the cue's end is cut, as there */
    zxl_note_t *n = &c->note[c->nn++];
    uint8_t *b = (uint8_t *) n;
    for (uint32_t i = 0; i < sizeof *n; i++) b[i] = 0;
    n->kind = (uint8_t) kind;
    n->start = start;
    n->dur = c->len - start;
    if (c->b_mirror) pan_milli = -pan_milli;
    int32_t gl, gr;
    pan_gains(pan_milli, &gl, &gr);
    int64_t a = (int64_t) amp_milli * c->b_gain_q16 / 1000; /* Q16 */
    n->gl = a * gl;                                         /* Q31 */
    n->gr = a * gr;
    n->p0 = c->npart;
    return n;
}

static void sustain(zxl_note_t *n, uint32_t a_ms, uint32_t r_ms, uint32_t dur_ms)
{
    n->sustain = 1;
    n->A = a_ms * FPMS;
    n->R = r_ms * FPMS;
    if (dur_ms * FPMS < n->dur) n->dur = dur_ms * FPMS;
}

static void partial(zxl_cue_t *c, zxl_note_t *n, uint32_t mhz, uint32_t ratio_milli,
                    int32_t amp_milli, uint32_t att_us, uint32_t dec_ms)
{
    if (!n || c->npart >= ZXL_SYN_PARTIALS) return;
    zxl_partial_t *p = &c->part_pool[c->npart++];
    n->np++;
    p->ph = 0;
    p->inc = inc_of(mhz, ratio_milli);
    p->amp = amp_milli * 16384 / 1000;
    p->att = att_us * FPMS / 1000u;
    p->att_q30 = p->att ? Q30 / p->att : 0;
    p->e = Q30;
    if (!dec_ms && !n->sustain) dec_ms = 1; /* only a held note rings on */
    p->mul = dec_ms ? decay_mul(dec_ms * FPMS) : Q30;
    p->dead = 0;
}

/* breath, a knock, the hiss of wings: white noise, smoothed over `smooth`
 * frames, or a click (the difference, softened over five) */
static zxl_note_t *noise(zxl_cue_t *c, uint32_t st_ms, uint32_t dec_ms, uint32_t seed,
                         uint32_t smooth, int hp, int32_t amp_milli, int32_t pan_milli)
{
    zxl_note_t *n = note_new(c, K_NOISE, st_ms, amp_milli, pan_milli);
    if (!n) return 0;
    n->seed = seed * 2654435761u + 1u;
    n->hp = (uint8_t) (hp ? 1 : 0);
    n->alpha_q16 = smooth > 1 ? 131072u / (smooth + 1u) : 65536u;
    n->att = FPMS; /* 1 ms */
    n->ea_q30 = Q30 / n->att;
    n->e = Q30;
    n->mul = dec_ms ? decay_mul(dec_ms * FPMS) : Q30;
    return n;
}

static void sweep(zxl_cue_t *c, uint32_t f0_hz, uint32_t f1_hz, uint32_t st_ms, uint32_t dur_ms,
                  int32_t amp_milli, int32_t pan_milli)
{
    zxl_note_t *n = note_new(c, K_SWEEP, st_ms, amp_milli, pan_milli);
    if (!n) return;
    uint32_t len = dur_ms * FPMS;
    if (len < n->dur) n->dur = len;
    n->sinc = (uint64_t) inc_of(f0_hz * 1000u, 1000u) << 16;
    n->glide_q30 = glide_of((uint64_t) f1_hz * Q30 / f0_hz, len);
    n->einc = (uint32_t) (0x80000000u / len); /* half a sine over the sweep */
}

/* ---- timbres: the reference's, partial for partial ----------------------- */
static void timbre(zxl_cue_t *c, uint32_t t, uint32_t mhz, uint32_t st_ms, uint32_t d_ms,
                   uint32_t seed, int32_t amp, int32_t pan)
{
    zxl_note_t *n = note_new(c, K_TONE, st_ms, amp, pan);
    if (!n) return;
    switch (t) {
    case T_GLASS:
        partial(c, n, mhz, 1000, 1000, 5000, d_ms);
        partial(c, n, mhz, 3000, 180, 5000, d_ms * 3 / 10);
        partial(c, n, mhz, 4200, 80, 3000, 100);
        break;
    case T_MARIMBA:
        partial(c, n, mhz, 1000, 1000, 2000, d_ms);
        partial(c, n, mhz, 3930, 350, 2000, d_ms / 4);
        partial(c, n, mhz, 9200, 120, 1000, 20);
        break;
    case T_CHIP: /* a soft square: odd harmonics, rolled off */
        partial(c, n, mhz, 1000, 1667, 2000, d_ms);
        partial(c, n, mhz, 3000, 185, 2000, d_ms);
        partial(c, n, mhz, 5000, 67, 2000, d_ms);
        partial(c, n, mhz, 7000, 34, 2000, d_ms);
        break;
    case T_FLUTE: { /* breathy, held for d, the breath under it */
        sustain(n, 60, 250, d_ms);
        partial(c, n, mhz, 1000, 1000, 0, 0);
        partial(c, n, mhz, 2000, 250, 0, 0);
        zxl_note_t *b = noise(c, st_ms, 0, seed, 30, 0, amp / 2, pan);
        if (b) sustain(b, 60, 250, d_ms);
        break;
    }
    case T_BELL:
        partial(c, n, mhz, 1000, 1000, 2000, d_ms);
        partial(c, n, mhz, 2000, 500, 2000, d_ms * 6 / 10);
        partial(c, n, mhz, 2760, 350, 2000, d_ms * 4 / 10);
        partial(c, n, mhz, 5400, 200, 2000, d_ms * 2 / 10);
        partial(c, n, mhz, 8900, 100, 2000, d_ms * 8 / 100);
        break;
    case T_COIN:
        partial(c, n, mhz, 1000, 1000, 800, 210);
        partial(c, n, mhz, 1470, 1000, 800, 172);
        partial(c, n, mhz, 2090, 1000, 800, 147);
        partial(c, n, mhz, 2560, 1000, 800, 137);
        partial(c, n, mhz, 3410, 1000, 800, 125);
        break;
    case T_PLUCK: /* kalimba-ish: a tine and a quick bright partial */
        partial(c, n, mhz, 1000, 1000, 1000, d_ms);
        partial(c, n, mhz, 5950, 400, 1000, 40);
        break;
    default: { /* strings: three detuned saws, rolled off, held for d */
        static const int32_t H[5] = {1000, 330, 172, 109, 76};
        static const uint32_t DT[3] = {996, 1000, 1004};
        sustain(n, 350, 500, d_ms);
        for (uint32_t k = 0; k < 5; k++)
            for (uint32_t j = 0; j < 3; j++) partial(c, n, mhz, DT[j] * (k + 1), H[k], 0, 0);
        break;
    }
    }
}

static void glass(zxl_cue_t *c, int32_t m, uint32_t st, uint32_t d, int32_t a, int32_t p)
{
    timbre(c, T_GLASS, mhz_of(m), st, d, 0, a, p);
}
static void marimba(zxl_cue_t *c, int32_t m, uint32_t st, uint32_t d, int32_t a, int32_t p)
{
    timbre(c, T_MARIMBA, mhz_of(m), st, d, 0, a, p);
}
static void chip(zxl_cue_t *c, int32_t m, uint32_t st, uint32_t d, int32_t a, int32_t p)
{
    timbre(c, T_CHIP, mhz_of(m), st, d, 0, a, p);
}
static void flute(zxl_cue_t *c, int32_t m, uint32_t st, uint32_t d, uint32_t seed, int32_t a,
                  int32_t p)
{
    timbre(c, T_FLUTE, mhz_of(m), st, d, seed, a, p);
}
static void bell(zxl_cue_t *c, int32_t m, uint32_t st, uint32_t d, int32_t a, int32_t p)
{
    timbre(c, T_BELL, mhz_of(m), st, d, 0, a, p);
}
static void coin(zxl_cue_t *c, int32_t m, uint32_t st, int32_t a, int32_t p)
{
    timbre(c, T_COIN, mhz_of(m), st, 0, 0, a, p);
}
static void pluck(zxl_cue_t *c, int32_t m, uint32_t st, uint32_t d, int32_t a, int32_t p)
{
    timbre(c, T_PLUCK, mhz_of(m), st, d, 0, a, p);
}
static void strings(zxl_cue_t *c, int32_t m, uint32_t st, uint32_t d, int32_t a, int32_t p)
{
    timbre(c, T_STRINGS, mhz_of(m), st, d, 0, a, p);
}

/* the reference's _voice(): each timbre at its own length for a given d */
static void voice(zxl_cue_t *c, uint32_t tb, int32_t m, uint32_t st, uint32_t d, int32_t a,
                  int32_t p)
{
    switch (tb) {
    case ZXL_TIMBRE_MARIMBA:
        marimba(c, m, st, d * 6 / 10, a, p);
        break;
    case ZXL_TIMBRE_CHIP:
        chip(c, m, st, d * 4 / 10, a, p);
        break;
    case ZXL_TIMBRE_FLUTE:
        flute(c, m, st, d, 7, a, p);
        break;
    case ZXL_TIMBRE_BELL:
        bell(c, m, st, d * 14 / 10, a, p);
        break;
    case ZXL_TIMBRE_COIN:
        coin(c, m, st, a, p);
        break;
    case ZXL_TIMBRE_PLUCK:
        pluck(c, m, st, d * 7 / 10, a, p);
        break;
    case ZXL_TIMBRE_STRINGS:
        strings(c, m, st, d * 14 / 10, a, p);
        break;
    default:
        glass(c, m, st, d, a, p);
        break;
    }
}

/* ---- the recipes ----------------------------------------------------------- */
/* tile i of the five, panned across: -s .. +s */
static int32_t spread(uint32_t i, int32_t s)
{
    return -s + 2 * s * (int32_t) i / (ZXL_TILES - 1);
}

#define PENTA_N 7
static const int8_t PENTA[PENTA_N] = {0, 2, 4, 7, 9, 12, 14};

typedef struct {
    int8_t call[5][2]; /* semitones, onset in hundredths of a second; 99 ends */
    int8_t chord[5];
    uint8_t gap_ms, top;
} motif_t;

/* clang-format off */
static const motif_t MOTIF[ZXL_MOTIFS] = {
    {{{0, 0}, {7, 14}, {12, 30}, {0, 99}},            {0, 4, 7, 12, 16},   45, 19},
    {{{12, 0}, {7, 14}, {0, 30}, {0, 99}},            {0, 7, 12, 16, 19},  50, 24},
    {{{0, 0}, {4, 10}, {7, 20}, {12, 34}, {0, 99}},   {0, 4, 7, 11, 14},   40, 16},
    {{{7, 0}, {12, 18}, {9, 30}, {0, 99}},            {0, 7, 9, 12, 16},   55, 21},
    {{{0, 0}, {12, 16}, {7, 26}, {14, 40}, {0, 99}},  {0, 2, 7, 12, 14},   40, 19},
    {{{4, 0}, {2, 12}, {0, 24}, {7, 40}, {0, 99}},    {0, 4, 9, 12, 16},   50, 24},
    {{{0, 0}, {0, 10}, {7, 26}, {0, 99}},             {0, 7, 12, 19, 24},  60, 28},
    {{{9, 0}, {7, 12}, {4, 24}, {2, 36}, {0, 48}},    {0, 4, 7, 9, 12},    35, 16},
    {{{0, 0}, {2, 9}, {4, 18}, {7, 27}, {0, 99}},     {-12, 0, 7, 12, 16}, 50, 19},
    {{{12, 0}, {14, 14}, {12, 26}, {7, 40}, {0, 99}}, {0, 5, 7, 12, 17},   45, 19},
    {{{0, 0}, {9, 18}, {0, 99}},                      {0, 9, 12, 16, 21},  60, 24},
    {{{7, 0}, {4, 10}, {9, 22}, {12, 36}, {0, 99}},   {0, 4, 7, 12, 19},   40, 24},
};
/* clang-format on */

/* every app but the six: its key, octave, timbre and motif */
static void generic(zxl_cue_t *c)
{
    const zxl_theme_t *t = c->theme;
    const motif_t *mo = &MOTIF[t->motif % ZXL_MOTIFS];
    int32_t base = 12 * ((int32_t) t->octave + 1) + t->key; /* hz(0) */
    int32_t side = t->motif % 2 ? -350 : 350;
    if (c->kind == ZXL_EV_BEGIN) {
        glass(c, base - 12, 0, 1200, 500, side); /* a low root, struck softly */
        for (int k = 0; k < 5 && mo->call[k][1] != 99; k++)
            voice(c, t->timbre, base + mo->call[k][0], 30 + 10u * (uint32_t) mo->call[k][1], 600,
                  900 - 80 * k, side);
    } else if (c->kind == ZXL_EV_TICK) {
        voice(c, t->timbre, base + PENTA[c->step < PENTA_N ? c->step : PENTA_N - 1], 0, 300, 1000,
              spread(c->part, 600));
    } else {
        int32_t p = -side / 2;
        for (uint32_t k = 0; k < 5; k++)
            voice(c, t->timbre, base + mo->chord[k] - 12, 40 + mo->gap_ms * k, 1000, 550, p);
        strings(c, base - 12, 20, 1700, 450, p);
        strings(c, base - 5, 60, 1600, 300, p);
        glass(c, base + mo->top, 60 + mo->gap_ms * 5u, 800, 350, p);
    }
}

/* the scale step a tick plays, for the six's seven-note scales */
static int32_t sc(const zxl_cue_t *c, const uint8_t s[7])
{
    return s[c->step < 7 ? c->step : 6];
}

static void banker(zxl_cue_t *c) /* earth and wood: a knock, the vault door opening */
{
    static const uint8_t S[7] = {62, 64, 66, 69, 71, 74, 76};
    if (c->kind == ZXL_EV_BEGIN) {
        noise(c, 0, 50, 11, 40, 0, 800, -150);
        marimba(c, 50, 20, 500, 900, -150);
        marimba(c, 57, 200, 500, 700, -150);
        marimba(c, 62, 380, 700, 600, -150);
        marimba(c, 69, 380, 600, 500, 400);
    } else if (c->kind == ZXL_EV_TICK) {
        marimba(c, sc(c, S), 0, 250, 1000, spread(c->part, 600));
    } else {
        sweep(c, 90, 40, 0, 500, 900, 0); /* the earth gives way */
        noise(c, 0, 350, 12, 80, 0, 250, 0);
        static const uint8_t CH[5] = {50, 57, 62, 66, 69};
        for (uint32_t k = 0; k < 5; k++) marimba(c, CH[k], 80 + 25 * k, 900, 600, -100);
        glass(c, 86, 200, 600, 400, 500);
    }
}

static void flint(zxl_cue_t *c) /* a struck spark, the tube warming */
{
    static const uint8_t S[7] = {69, 71, 73, 76, 78, 81, 83};
    if (c->kind == ZXL_EV_BEGIN) {
        noise(c, 0, 12, 21, 1, 1, 450, 200); /* flint on steel */
        noise(c, 50, 20, 22, 1, 1, 350, 200);
        glass(c, 93, 50, 300, 800, 200);
        glass(c, 100, 70, 200, 500, 200);
        chip(c, 57, 180, 450, 700, 200);
        chip(c, 64, 240, 400, 400, 200);
    } else if (c->kind == ZXL_EV_TICK) {
        int32_t p = spread(c->part, 500);
        noise(c, 0, 4, 30u + c->step, 1, 1, 250, p); /* a keystroke that plays a note */
        chip(c, sc(c, S), 0, 70, 1000, p);
    } else {
        sweep(c, 60, 15000, 0, 120, 500, 0); /* a fast whine */
        noise(c, 120, 30, 23, 6, 0, 900, 0); /* the thunk */
        static const uint8_t CH[4] = {57, 64, 69, 73};
        for (uint32_t k = 0; k < 4; k++) chip(c, CH[k], 140, 600, 500, 0);
        glass(c, 81, 160, 800, 300, 0);
    }
}

static void murmuration(zxl_cue_t *c) /* wings and breath: the swarm */
{
    static const uint8_t S[7] = {76, 78, 80, 83, 85, 88, 90};
    if (c->kind == ZXL_EV_BEGIN) {
        for (uint32_t k = 0; k < 9; k++) /* a flutter of wings, left to right */
            noise(c, 30 * k, 25, 40 + k, 12, 0, 500, -800 + 200 * (int32_t) k);
        flute(c, 76, 250, 900, 7, 600, -200);
        flute(c, 83, 450, 800, 8, 450, 300);
    } else if (c->kind == ZXL_EV_TICK) { /* a chirp */
        static const int16_t P[5] = {0, 604, -610, 12, 598};
        int32_t m = sc(c, S), p = P[c->part % 5];
        zxl_note_t *n = note_new(c, K_TONE, 0, 1000, p);
        uint32_t f = mhz_of(m);
        partial(c, n, f, 1000, 1000, 5000, 120);
        partial(c, n, f, 3000, 180, 5000, 36);
        partial(c, n, f, 4200, 80, 3000, 100);
        partial(c, n, f, 500, 250, 5000, 120); /* the flutter: f's half on either side */
        partial(c, n, f, 1500, 250, 5000, 120);
        timbre(c, T_GLASS, f * 3 / 2, 40, 80, 0, 600, p);
    } else {
        for (uint32_t k = 0; k < 14; k++) /* the flock lifts off, spreading outward */
            noise(c, 20 * k, 40, 60 + k, 10, 0, 400, (k & 1 ? -1 : 1) * (200 + 50 * (int32_t) k));
        static const uint8_t CH[4] = {64, 71, 76, 80};
        for (uint32_t j = 0; j < 4; j++) flute(c, CH[j], 120, 1500, 9 + j, 350, 0);
        glass(c, 88, 300, 1000, 300, 400);
    }
}

static void coffer(zxl_cue_t *c) /* a key, tumblers, coins, the lid */
{
    static const uint8_t S[7] = {79, 81, 83, 86, 88, 91, 93};
    if (c->kind == ZXL_EV_BEGIN) {
        static const uint16_t ST[4] = {0, 70, 130, 220};
        for (uint32_t k = 0; k < 4; k++) { /* a key turning through the tumblers */
            noise(c, ST[k], 6, 70 + k, 1, 1, 250, 100);
            timbre(c, T_MARIMBA, (1800 + 300 * k) * 1000, ST[k], 30, 0, 300, 100);
        }
        bell(c, 67, 320, 800, 700, 100);
        bell(c, 74, 380, 600, 400, 100);
    } else if (c->kind == ZXL_EV_TICK) {
        coin(c, sc(c, S), 0, 1000, spread(c->part, 500));
    } else {
        noise(c, 0, 50, 90, 30, 0, 1000, 0); /* the latch */
        sweep(c, 70, 45, 0, 250, 1000, 0);
        bell(c, 79, 100, 1200, 600, 0);
        bell(c, 86, 140, 1000, 450, 0);
        bell(c, 83, 180, 1000, 350, 0);
        for (uint32_t k = 0; k < 5; k++) coin(c, S[k], 250 + 60 * k, 250, 0); /* coins settle */
    }
}

static void cutwater(zxl_cue_t *c) /* the projector, a clapper, a swell */
{
    static const uint8_t S[7] = {72, 74, 76, 79, 81, 84, 86};
    if (c->kind == ZXL_EV_BEGIN) {
        static const uint16_t ST[14] = {0,   50,  97,  141, 182, 219, 254,
                                        285, 313, 337, 358, 375, 388, 397};
        for (uint32_t k = 0; k < 14; k++) noise(c, ST[k], 8, 100 + k, 1, 1, 160, 0); /* the reel */
        strings(c, 48, 100, 1500, 700, 0);
        strings(c, 55, 250, 1350, 500, 0);
    } else if (c->kind == ZXL_EV_TICK) {
        int32_t p = spread(c->part, 500);
        noise(c, 0, 5, 110u + c->step, 1, 1, 220, p); /* a frame through the gate */
        glass(c, sc(c, S), 3, 150, 1000, p);
    } else {
        noise(c, 0, 20, 120, 1, 1, 500, 0); /* the clapper */
        noise(c, 0, 60, 121, 8, 0, 400, 0);
        sweep(c, 55, 38, 20, 600, 1000, 0); /* a low hit */
        static const uint8_t CH[5] = {48, 55, 60, 64, 67};
        for (uint32_t k = 0; k < 5; k++) strings(c, CH[k], 30, 1900, 450, 0);
        glass(c, 84, 300, 1000, 300, 0);
    }
}

static void commons(zxl_cue_t *c) /* a call and an answer */
{
    static const uint8_t S[7] = {77, 79, 81, 84, 86, 89, 91};
    if (c->kind == ZXL_EV_BEGIN) {
        pluck(c, 77, 0, 500, 1000, -550); /* "hel-lo" */
        pluck(c, 84, 120, 500, 800, -550);
        pluck(c, 81, 420, 500, 800, 550); /* and the answer, from the other side */
        pluck(c, 77, 540, 500, 800, 550);
        glass(c, 53, 0, 1000, 300, 0);
    } else if (c->kind == ZXL_EV_TICK) {
        pluck(c, sc(c, S), 0, 250, 1000, c->part & 1u ? -500 : 500);
    } else {
        sweep(c, 300, 1200, 0, 60, 480, 0); /* a bubble pops open */
        static const uint8_t CH[5] = {65, 69, 72, 77, 81};
        for (uint32_t k = 0; k < 5; k++) pluck(c, CH[k], 60 + 40 * k, 1000, 500, 0);
        glass(c, 89, 250, 800, 400, 0);
        strings(c, 53, 50, 1600, 400, 0);
    }
}

/* ---- each tier's texture --------------------------------------------------- */
/* The owner (2026-10-15): the tiers sound like what they are. Over every app's
 * own phrase (which stays its own), a quiet layer in its tier's material:
 *   tier 1 (the 144 base apps)    dry wood and relays: a woodblock knock, a
 *                                 click under each tick, a relay's snap
 *   tier 2 (the composites)       heavy mechanism: a latch, an escapement's
 *                                 tick-tock under each tick, a furnace hum
 *   tier 3 (the crystal apps)     glass and bells: a glass ring, a small bell
 *                                 an octave over each tick, a bell's shimmer */
uint32_t zxl_synth_tier(const zxl_theme_t *t)
{
    const zxl_theme_t *first = zxl_curated(0);
    if (!t || !first || t < first || t >= first + ZXL_APPS) return 1; /* group defaults: base */
    uint32_t i = (uint32_t) (t - first);
    return i < ZXL_TIER1 ? 1u : i < ZXL_TIER2_END ? 2u : 3u;
}

static void hum(zxl_cue_t *c, int32_t m, uint32_t st, uint32_t dur, int32_t a)
{
    zxl_note_t *n = note_new(c, K_TONE, st, a, 0);
    if (!n) return;
    sustain(n, 200, 300, dur);
    partial(c, n, mhz_of(m), 1000, 1000, 0, 0);
    partial(c, n, mhz_of(m), 2000, 300, 0, 0);
}

static void texture(zxl_cue_t *c)
{
    const zxl_theme_t *t = c->theme;
    int32_t base = 12 * ((int32_t) t->octave + 1) + t->key;
    int32_t pan = c->kind == ZXL_EV_TICK ? spread(c->part, 600) : 0;
    uint32_t tier = zxl_synth_tier(t);
    if (tier == 1) {
        if (c->kind == ZXL_EV_BEGIN) { /* a woodblock knock */
            noise(c, 0, 8, 501, 1, 1, 300, 0);
            timbre(c, T_MARIMBA, mhz_of(base + 24), 0, 40, 0, 250, 0);
        } else if (c->kind == ZXL_EV_TICK) { /* a dry click */
            noise(c, 0, 3, 510u + c->step, 1, 1, 220, pan);
        } else { /* a relay's snap: pull-in and contact */
            noise(c, 0, 4, 520, 1, 1, 300, 0);
            noise(c, 25, 3, 521, 1, 1, 220, 0);
        }
    } else if (tier == 2) {
        if (c->kind == ZXL_EV_BEGIN) { /* a heavy latch */
            noise(c, 0, 40, 601, 30, 0, 450, 0);
            sweep(c, 80, 50, 0, 120, 350, 0);
        } else if (c->kind == ZXL_EV_TICK) { /* the escapement: tick, tock */
            noise(c, 0, 4, 610u + c->step, 1, 1, 200, pan);
            noise(c, 60, 4, 620u + c->step, 1, 1, 150, pan);
        } else { /* the latch throws, the furnace hums under the answer */
            noise(c, 0, 50, 630, 30, 0, 400, 0);
            hum(c, base - 24, 20, 1200, 160);
        }
    } else {
        if (c->kind == ZXL_EV_BEGIN) { /* a glass ring */
            glass(c, base + 24, 0, 900, 260, 0);
        } else if (c->kind == ZXL_EV_TICK) { /* a small bell an octave over */
            int32_t m = base + PENTA[c->step < PENTA_N ? c->step : PENTA_N - 1] + 12;
            bell(c, m, 0, 200, 300, pan);
        } else { /* a bell's shimmer over the chord */
            bell(c, base + 24, 80, 900, 220, 0);
            bell(c, base + 31, 140, 800, 160, 0);
        }
    }
}

/* ---- which recipe, how long, how loud ----------------------------------- */
typedef struct {
    const char *app;
    void (*fn)(zxl_cue_t *c);
    uint16_t len_ms[3];  /* begin, tick, end */
    uint16_t echo[2][2]; /* begin, end: delay ms, gain thousandths */
} recipe_t;

/* clang-format off */
static const recipe_t HAND[] = {
    {"Banker",      banker,      {1300, 450, 1800}, {{140, 250}, {160, 220}}},
    {"Flint",       flint,       {1000, 250, 1400}, {{ 90, 300}, {110, 200}}},
    {"Murmuration", murmuration, {1500, 350, 1900}, {{200, 250}, {220, 250}}},
    {"Coffer",      coffer,      {1200, 400, 2000}, {{120, 250}, {150, 220}}},
    {"Cutwater",    cutwater,    {1600, 350, 2100}, {{100, 180}, {180, 200}}},
    {"Commons",     commons,     {1300, 400, 1800}, {{130, 200}, {150, 220}}},
};
static const recipe_t GENERIC = {0, generic, {1400, 450, 1900}, {{130, 220}, {150, 220}}};
/* the reference's finish(): peak (thousandths) and fade (ms) per cue kind */
static const uint16_t PEAK[3] = {700, 450, 850};
static const uint16_t TICK_PEAK[6] = {450, 400, 400, 600, 400, 450}; /* the six's ticks */
static const uint16_t FADE[3] = {150, 50, 0}; /* end: per recipe below */
static const uint16_t END_FADE[6] = {400, 300, 500, 500, 600, 500};
static const uint16_t TICK_FADE[6] = {50, 30, 40, 50, 40, 50};
/* clang-format on */

static int streq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

static int hand_of(const zxl_theme_t *t)
{
    if (t && t->app)
        for (int i = 0; i < (int) (sizeof HAND / sizeof HAND[0]); i++)
            if (streq(t->app, HAND[i].app)) return i;
    return -1;
}

static int slot_of(uint32_t kind)
{
    return kind == ZXL_EV_BEGIN ? 0 : kind == ZXL_EV_TICK ? 1 : kind == ZXL_EV_END ? 2 : -1;
}

uint32_t zxl_cue_frames(const zxl_theme_t *t, uint32_t kind)
{
    int k = slot_of(kind), h = hand_of(t);
    if (k < 0) return 0;
    return (h >= 0 ? HAND[h].len_ms[k] : GENERIC.len_ms[k]) * FPMS;
}

static void build(zxl_cue_t *c)
{
    int k = slot_of(c->kind), h = hand_of(c->theme);
    const recipe_t *r = h >= 0 ? &HAND[h] : &GENERIC;
    c->nn = 0;
    c->npart = 0;
    c->pos = 0;
    c->len = r->len_ms[k] * FPMS;
    c->fade = (k == 2             ? (h >= 0 ? END_FADE[h] : 450u)
               : k == 1 && h >= 0 ? TICK_FADE[h]
                                  : FADE[k]) *
              FPMS;
    c->b_peak_q8 = (uint16_t) ((k == 1 && h >= 0 ? TICK_PEAK[h] : PEAK[k]) * 256u / 1000u);
    /* the dry phrase, then (begin and end) its echo three times over, each
     * bounce from the other side and quieter, as the reference's finish() */
    uint32_t taps = k == 1 ? 1 : 4;
    int64_t g = 65536;
    for (uint32_t e = 0; e < taps; e++) {
        c->b_delay = e * (k == 1 ? 0u : (uint32_t) r->echo[k / 2][0]) * FPMS;
        c->b_gain_q16 = (int32_t) g;
        c->b_mirror = (uint8_t) (e & 1u);
        r->fn(c);
        if (h < 0) texture(c); /* the six hand-made apps are their own textures */
        if (k != 1) g = g * r->echo[k / 2][1] / 1000;
    }
}

/* ---- rendering ------------------------------------------------------------- */
/* one note's frames [a, b) of the cue into acc (Q15 units, 1.0 = 32768) */
static void note_run(zxl_cue_t *c, zxl_note_t *n, uint32_t a, uint32_t b, int64_t *acc, uint32_t at)
{
    int32_t mono[ZXL_SYN_BLOCK];
    uint32_t cnt = b - a, tt0 = a - n->start;
    for (uint32_t i = 0; i < cnt; i++) mono[i] = 0;
    int alive = 0;
    if (n->kind == K_TONE) {
        for (uint32_t j = 0; j < n->np; j++) {
            zxl_partial_t *p = &c->part_pool[n->p0 + j];
            if (p->dead) continue;
            alive = 1;
            uint32_t i = 0;
            for (; i < cnt && tt0 + i < p->att; i++) { /* rising */
                int32_t s = (sinq(p->ph) * p->amp) >> 14;
                p->ph += p->inc;
                uint64_t env = (p->e * ((uint64_t) (tt0 + i) * p->att_q30)) >> 30;
                p->e = (uint32_t) qmul(p->e, p->mul);
                mono[i] += (int32_t) (((int64_t) s * (int64_t) env) >> 30);
            }
            if (p->mul == Q30) /* held: no decay (strings, flute) */
                for (; i < cnt; i++) {
                    mono[i] += (sinq(p->ph) * p->amp) >> 14;
                    p->ph += p->inc;
                }
            else
                for (; i < cnt; i++) { /* dying away */
                    int32_t s = (sinq(p->ph) * p->amp) >> 14;
                    p->ph += p->inc;
                    mono[i] += (int32_t) (((int64_t) s * p->e) >> 30);
                    p->e = (uint32_t) (((uint64_t) p->e * p->mul) >> 30);
                }
            if (tt0 + cnt >= p->att && p->e < DEAD_Q30) p->dead = 1;
        }
    } else if (n->kind == K_NOISE) {
        alive = 1;
        for (uint32_t i = 0; i < cnt; i++) {
            uint32_t tt = tt0 + i;
            n->seed = n->seed * 1664525u + 1013904223u;
            int32_t x = ((int32_t) (n->seed >> 16) - 32768) * 1774 / 1024; /* about N(0, 1) */
            if (n->alpha_q16 < 65536u)
                n->lp += (int32_t) (((int64_t) (x - n->lp) * n->alpha_q16) >> 16);
            else
                n->lp = x;
            int32_t y = n->lp;
            if (n->hp) { /* a click: the difference, softened over five */
                int32_t d = y - n->prev;
                n->prev = y;
                n->lp2 += (d - n->lp2) / 3;
                y = n->lp2;
            }
            uint64_t env = n->e;
            if (tt < n->att) env = (env * ((uint64_t) tt * n->ea_q30)) >> 30;
            n->e = (uint32_t) qmul(n->e, n->mul);
            mono[i] = (int32_t) (((int64_t) y * (int64_t) env) >> 30);
        }
        if (!n->sustain && tt0 + cnt >= n->att && n->e < DEAD_Q30) n->dead = 1;
    } else {
        alive = 1;
        for (uint32_t i = 0; i < cnt; i++) {
            int32_t s = sinq(n->ph), e = sinq(n->eph);
            n->ph += (uint32_t) (n->sinc >> 16);
            n->sinc = qmul(n->sinc, n->glide_q30);
            n->eph += n->einc;
            mono[i] = (int32_t) (((int64_t) s * e) >> 15);
        }
    }
    if (n->sustain) /* held: rise over A, fall over the last R before dur */
        for (uint32_t i = 0; i < cnt; i++) {
            uint64_t tt = tt0 + i, r = n->dur - tt;
            uint64_t g = tt < n->A ? (tt << 30) / n->A : Q30;
            if (r < n->R) g = (g * ((r << 30) / n->R)) >> 30;
            mono[i] = (int32_t) (((int64_t) mono[i] * (int64_t) g) >> 30);
        }
    if (!alive) n->dead = 1;
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t o = (at + i) * 2;
        acc[o] += ((int64_t) mono[i] * n->gl) >> 31;
        acc[o + 1] += ((int64_t) mono[i] * n->gr) >> 31;
    }
}

/* the next `cnt` frames of a cue, unscaled, into acc[0 .. 2 cnt) */
static void cue_block(zxl_cue_t *c, int64_t *acc, uint32_t cnt)
{
    for (uint32_t i = 0; i < cnt * 2; i++) acc[i] = 0;
    uint32_t a = c->pos, b = c->pos + cnt;
    for (uint32_t k = 0; k < c->nn; k++) {
        zxl_note_t *n = &c->note[k];
        if (n->dead) continue;
        uint32_t s = n->start, e = n->start + n->dur;
        if (e <= a) {
            n->dead = 1;
            continue;
        }
        if (s >= b) continue;
        uint32_t x = s > a ? s : a, y = e < b ? e : b;
        note_run(c, n, x, y, acc, x - a);
    }
    if (c->fade > 1)
        for (uint32_t i = 0; i < cnt; i++) {
            uint32_t f = a + i;
            if (f + c->fade < c->len) continue;
            uint64_t r = c->len - 1u - f, d = c->fade - 1u;
            uint64_t g = (r * r << 15) / (d * d);
            acc[2 * i] = (acc[2 * i] * (int64_t) g) >> 15;
            acc[2 * i + 1] = (acc[2 * i + 1] * (int64_t) g) >> 15;
        }
    c->pos = b;
}

static int64_t iabs64(int64_t v)
{
    return v < 0 ? -v : v;
}

/* The cue's loudest moment, playing it through silently from where it is
 * (the reference normalises every cue to its peak). At most `frames` frames
 * (0 = to the end); returns 1 when it reached the end. */
static int peak_run(zxl_cue_t *c, int64_t *scratch, int64_t *peak, uint32_t frames)
{
    uint32_t done = 0;
    while (c->pos < c->len && (!frames || done < frames)) {
        uint32_t cnt = c->len - c->pos;
        if (cnt > ZXL_SYN_BLOCK) cnt = ZXL_SYN_BLOCK;
        cue_block(c, scratch, cnt);
        for (uint32_t i = 0; i < cnt * 2; i++)
            if (iabs64(scratch[i]) > *peak) *peak = iabs64(scratch[i]);
        done += cnt;
    }
    return c->pos >= c->len;
}

static zxl_peak_t *peak_find(zxl_player_t *p, const zxl_theme_t *t, uint32_t kind)
{
    for (uint32_t i = 0; i < ZXL_SYN_PEAKS; i++)
        if (p->peaks[i].theme == t && p->peaks[i].kind == kind) return &p->peaks[i];
    return 0;
}

static void peak_keep(zxl_player_t *p, const zxl_theme_t *t, uint32_t kind, int64_t peak)
{
    zxl_peak_t *k = peak_find(p, t, kind);
    if (!k) k = &p->peaks[p->peak_next++ % ZXL_SYN_PEAKS];
    k->theme = t;
    k->kind = (uint8_t) kind;
    k->peak = peak;
}

/* finish the background job now (its cue is wanted) */
static void prep_finish(zxl_player_t *p)
{
    if (!p->prep.on) return;
    peak_run(&p->prep, p->acc, &p->prep_peak, 0);
    peak_keep(p, p->prep.theme, p->prep.kind, p->prep_peak);
    p->prep.on = 0;
}

static void prep_start(zxl_player_t *p, const zxl_theme_t *t, uint32_t kind)
{
    if (peak_find(p, t, kind) || (p->prep.on && p->prep.theme == t && p->prep.kind == kind)) return;
    prep_finish(p); /* one job at a time */
    p->prep.theme = t;
    p->prep.kind = (uint8_t) kind;
    p->prep.step = p->prep.part = 0;
    build(&p->prep);
    p->prep_peak = 1;
    p->prep.on = 1;
}

/* Lay a cue down ready to sound, normalised to its peak at the recipe's
 * level times gain. */
static void cue_start(zxl_player_t *p, zxl_cue_t *c, uint32_t gain_q8)
{
    if (p->prep.on && p->prep.theme == c->theme && p->prep.kind == c->kind) prep_finish(p);
    zxl_peak_t *k = c->kind == ZXL_EV_TICK ? 0 : peak_find(p, c->theme, c->kind);
    int64_t peak = 1;
    if (k) {
        peak = k->peak;
    } else {
        build(c);
        peak_run(c, p->acc, &peak, 0);
        if (c->kind != ZXL_EV_TICK) peak_keep(p, c->theme, c->kind, peak);
    }
    build(c);
    /* out = acc * scale >> 32, the peak landing at its level of full scale */
    c->scale_q32 = (int64_t) (((uint64_t) 32767u * c->b_peak_q8 * gain_q8 << 16) / (uint64_t) peak);
}

/* ---- the player ------------------------------------------------------------ */
void zxl_player_init(zxl_player_t *p)
{
    if (!p) return;
    uint8_t *b = (uint8_t *) p;
    for (uint32_t i = 0; i < sizeof *p; i++) b[i] = 0;
}

int zxl_player_play(zxl_player_t *p, const zxl_theme_t *t, const zxl_event_t *ev)
{
    if (!p || !t || !ev || !ev->audible || !ev->gain_q8 || slot_of(ev->kind) < 0) return -1;
    uint32_t k = 0, oldest = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < ZXL_SYN_CUES; i++) {
        if (!p->cue[i].on) {
            k = i;
            oldest = 0;
            break;
        }
        if (p->cue[i].age < oldest) oldest = p->cue[i].age, k = i;
    }
    zxl_cue_t *c = &p->cue[k];
    c->on = 0;
    c->theme = t;
    c->kind = ev->kind;
    c->step = ev->step;
    c->part = ev->part;
    c->age = ++p->age;
    cue_start(p, c, ev->gain_q8 > 256 ? 256u : ev->gain_q8);
    c->on = 1;
    if (ev->kind == ZXL_EV_BEGIN) zxl_player_prepare(p, t); /* its end, while it loads */
    return 0;
}

void zxl_player_prepare(zxl_player_t *p, const zxl_theme_t *t)
{
    if (!p || !t) return;
    if (!peak_find(p, t, ZXL_EV_END))
        prep_start(p, t, ZXL_EV_END);
    else if (!peak_find(p, t, ZXL_EV_BEGIN))
        prep_start(p, t, ZXL_EV_BEGIN);
}

uint32_t zxl_player_render(zxl_player_t *p, int16_t *lr, uint32_t frames)
{
    if (!p || !lr) return 0;
    int64_t mix[ZXL_SYN_BLOCK * 2];
    for (uint32_t done = 0; done < frames;) {
        uint32_t cnt = frames - done;
        if (cnt > ZXL_SYN_BLOCK) cnt = ZXL_SYN_BLOCK;
        for (uint32_t i = 0; i < cnt * 2; i++) mix[i] = 0;
        for (uint32_t k = 0; k < ZXL_SYN_CUES; k++) {
            zxl_cue_t *c = &p->cue[k];
            if (!c->on) continue;
            uint32_t n = c->len - c->pos < cnt ? c->len - c->pos : cnt;
            cue_block(c, p->acc, n);
            for (uint32_t i = 0; i < n * 2; i++) mix[i] += (p->acc[i] * c->scale_q32) >> 32;
            if (c->pos >= c->len) c->on = 0;
        }
        for (uint32_t i = 0; i < cnt * 2; i++) {
            int64_t v = mix[i];
            lr[done * 2 + i] = (int16_t) (v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        done += cnt;
    }
    /* the background job: four times the frames just played, so an end is
     * ready within about half a second of its begin */
    if (p->prep.on && peak_run(&p->prep, p->acc, &p->prep_peak, frames * 4u)) {
        peak_keep(p, p->prep.theme, p->prep.kind, p->prep_peak);
        p->prep.on = 0;
    }
    return zxl_player_active(p);
}

uint32_t zxl_player_active(const zxl_player_t *p)
{
    uint32_t n = 0;
    if (p)
        for (uint32_t i = 0; i < ZXL_SYN_CUES; i++) n += p->cue[i].on;
    return n;
}
