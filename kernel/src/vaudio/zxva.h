/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxva.h — ZXVA, the native vector audio format, and its renderer.
 *
 * The owner's rule for graphics (ZXVG, zui_vec.h) applied to sound: the
 * native form of a sound is a description, not a sample grid. A ZXVA file is
 * a set of parametric layers that are rendered to PCM at ANY sample rate
 * (8 kHz .. 768 kHz) and bit depth (16, 24, 32) on demand, the way a ZXVG is
 * drawn at any pixel size. The full byte layout, the model and measured
 * fidelity are in ZXVA.md next to this file; tools/zxva/ makes files
 * (wav2zxva.py analyses a recording, author.py compiles hand-written sounds)
 * and renders them (zxvarender.c).
 *
 * Layers (each a chunk type; a file may mix them freely):
 *   PARTIAL  a sinusoidal track: breakpoints of (time, frequency, amplitude,
 *            optional phase). Frequency and amplitude are linear between
 *            breakpoints; phase is the exact integral of frequency, plus (in
 *            "locked" tracks) a constant correction per segment so the phase
 *            hits the stored value at every breakpoint (McAulay-Quatieri style
 *            phase locking, with quadratic instead of cubic phase).
 *   NOISE    a stochastic layer: band levels over time on fixed band edges,
 *            rendered from a counter-based PRNG through one band-pass per
 *            band, normalised so each band's power is the stored level at
 *            every output rate.
 *   GRAINS   transients: short tone grains (windowed sinusoids) and noise
 *            grains (16 seeded sinusoids spread over a band), each with a
 *            raised-cosine attack, exponential decay and short release.
 *   VOICE    rate-independent instruments: MODAL (sum of partials with ratios
 *            and half-lives: sine, bells), FM pair (index envelope) and
 *            PLUCK (a Karplus-Strong string realised as its modal
 *            equivalent: harmonics with frequency-dependent decay).
 *   FVOICE   frame-exact recipe voices for a declared native rate (tone,
 *            noise, sweep with per-frame Q30 multipliers, LCG noise, one-pole
 *            filters, echoes). Bit-exact at the native rate; converted in
 *            integer maths, equivalent in content, at any other rate.
 *   NORM     a stored peak and target: the whole mix is scaled by
 *            target/peak, so loudness normalisation needs no pre-pass.
 *   RESIDUAL the exact difference between a source recording and the
 *            parametric render at the source's native rate and depth, coded
 *            losslessly (fixed predictor + Rice). "ZXVA + residual" rendered at
 *            the native rate and depth reproduces the recording sample-exactly.
 *
 * Rules:
 *   A1  Integer only and freestanding: no libc, no allocation, no float. All
 *       memory is the caller's zxva_work_t.
 *   A2  Deterministic: the same file, rate, depth and flags give the same
 *       samples on every platform and compiler. Every rounding is spelled out
 *       (floor unless stated), the PRNG is counter-based, and nothing depends
 *       on the host's int width or on right shifts of negative numbers.
 *   A3  Rate independence: every PARTIAL, GRAIN and VOICE sample is a closed
 *       form of its exact time n/R (phase polynomials and envelopes evaluated
 *       per sample, not accumulated), so the same file at 48 kHz and 96 kHz
 *       is the same continuous signal sampled twice, to ~1e-12 of a turn.
 *   A4  Band-limited: a sinusoid is faded out between 0.45 and 0.48 of the
 *       output rate by its instantaneous frequency and is never synthesised
 *       above 0.48 R; noise bands are clipped at 0.45 R (their power scaled
 *       by the part kept); FM indices are reduced so the Carson bandwidth
 *       stays under 0.45 R.
 *   A5  Streaming: zxva_render(frame, n) renders any span. Rendering in blocks
 *       of any size, in order, equals one render of the whole. Closed-form
 *       layers are also exact under arbitrary seeks; the stateful ones (NOISE
 *       filters, FVOICE recipes, RESIDUAL blocks) continue exactly when the
 *       next call starts where the last ended or jumps forward, and otherwise
 *       restart from a deterministic point (NOISE: a 50 ms pre-roll, so a
 *       backward seek gives noise that differs from a straight-through render
 *       only by a decayed filter transient; FVOICE: from the voice start).
 *   A6  Output: a Q40 mix bus (full scale 2^40) quantised to 16/24/32 bit
 *       with optional TPDF dither (counter-based, so also deterministic) and
 *       saturation.
 *   A7  The reader fails closed: zxva_check validates the whole file (every
 *       varint, count, range, time, the residual bitstreams down to the last
 *       padding bit) before anything renders; a malformed file renders
 *       nothing and zxva_open returns < 0.
 *
 * WHAT IS AND IS NOT IMPLEMENTED / WHERE IT CANNOT MATCH A RECORDING
 *   - Sinusoidal partials are excellent for tonal, harmonic and sustained
 *     sounds and for synthesised interface sounds, decent for voice (voiced
 *     parts are partials, fricatives go to the noise layer), and weak for
 *     dense mixes, reverb tails, crowds and sharp transients: those become
 *     band-noise or a few grains, which keep the spectral envelope and timing
 *     but not the waveform. Waveform SNR for such material is low; carry the
 *     RESIDUAL when the waveform matters.
 *   - The noise layer reproduces band power over time, never the waveform; its
 *     spectral resolution is the band set (2nd-order band-passes, so the
 *     envelope is smoothed between band centres).
 *   - "Infinite fidelity" means the format adds no aliasing and no
 *     quantisation of its own at any rate: at 192 kHz a partial is a 192 kHz
 *     sine with ~-160 dB oscillator error. It does not invent detail the
 *     source never had: an analysed 48 kHz recording has nothing above 24 kHz
 *     at any output rate.
 *   - The RESIDUAL is exact only at its native rate and depth; at other rates
 *     it is ignored (no resampler is implied).
 *   - NOISE renders identical noise only for identical rates: at another rate
 *     the noise has the same band powers but different samples. FVOICE noise
 *     likewise.
 *   - Not implemented: phase-locked cubic (MQ) interpolation (we use quadratic
 *     phase plus a constant correction, so frequency steps slightly at
 *     breakpoints), stored sample snippets for transients, reverb, HRTF.
 *
 * ---- FVOICE semantics (frame recipes, bit-exact at the native rate Rn) ----
 * Let i = 0,1,.. be the frame index within one instance of the voice. floor
 * shifts everywhere ("x >> s" below means floor(x / 2^s), also for negative x).
 *   sin(p)      zxva_sin_q30: Q30 sine of a Q64 phase (p / 2^64 turns).
 *   TONE        per partial k: inc_k = floor(f_mHz * 2^64 / (1000 * Rn)),
 *               ph_k(0) = 0, ph_k(i+1) = ph_k(i) + inc_k (mod 2^64);
 *               x_k = (sin(ph_k(i)) * amp_q14) >> 14;
 *               e_k(i) = (i * 2^30) / A_k for i < A_k (integer division),
 *               e_k(A_k) = 2^30, e_k(i+1) = (e_k(i) * dec_k) >> 30 after;
 *               v(i) = sum_k (x_k * e_k(i)) >> 30.
 *   NOISE       s(-1) = seed, s(i) = s(i-1) * mul + add (mod 2^32);
 *               x = (int32) s(i) >> 1 (Q30);  lp += ((x - lp) * a_lp) >> 16;
 *               w = lp; if a_hp: hp += ((w - hp) * a_hp) >> 16, w = w - hp;
 *               v(i) = (w * amp_q14) >> 14. (lp and hp start at 0.)
 *   SWEEP       inc(0) = floor(f0_mHz * 2^64 / (1000 * Rn)), ph(0) = 0;
 *               v(i) = (sin(ph(i)) * amp_q14) >> 14;
 *               ph(i+1) = ph(i) + inc(i); inc(i+1) = (inc(i) * glide) >> 30.
 *   Voice env E(i), applied as v(i) = (v(i) * E(i)) >> 30:
 *               NONE 2^30;  ASR (A, R): (i * 2^30) / A for i < A, then 2^30,
 *               and min(that, ((len - i) * 2^30) / R) for i >= len - R;
 *               HALFSINE: sin(floor(i * 2^63 / len));  AD (A, dec): as e_k.
 *   Stereo      G_c = gain_c (Q30, signed 64-bit) to channel ch (c = 0) and
 *               ch + 1 (c = 1); contribution to the Q40 bus (v * G_c) >> 20,
 *               computed in 128 bits.
 *   Echo        instance j = 1..necho starts j * delay frames later with
 *               G_c(j) = (G_c * g(j)) >> 30, g(0) = 2^30, g(j) = (g(j-1) *
 *               gain) >> 30; with mirror set, odd j swap G_0 and G_1.
 *   At R != Rn  frame counts become floor((N * R + Rn / 2) / Rn), per-frame
 *               Q30 multipliers m become m^(Rn/R) and one-pole alphas a become
 *               1 - (1 - a)^(Rn/R) (zxva_pow_q30, integer log2/exp2),
 *               frequencies are re-derived from mHz. Equivalent in content,
 *               not bit-identical.
 *   NORM        bus = (bus * floor(target * 2^32 / peak)) >> 32 before
 *               quantisation (128-bit product).
 *   Quantise    Q40 -> b bits: s = 41 - b; without dither
 *               y = (x + 2^(s-1)) >> s; with dither y = (x + d + 2^(s-1)) >> s,
 *               d = u1 + u2 - 2^s (u1, u2 uniform in [0, 2^s) from the
 *               counter PRNG); then saturated to b bits.
 */
#ifndef ZXV_ZXVA_H
#define ZXV_ZXVA_H

#include <stdint.h>
#include <stdbool.h>

/* ---- caps (override at build time to trade memory for capacity) -------- */
#ifndef ZXVA_MAX_TRACKS
#    define ZXVA_MAX_TRACKS 1024 /* PARTIAL tracks                          */
#endif
#ifndef ZXVA_MAX_NOISE
#    define ZXVA_MAX_NOISE 8 /* NOISE tracks                                */
#endif
#ifndef ZXVA_MAX_VOICES
#    define ZXVA_MAX_VOICES 64 /* VOICE chunks                              */
#endif
#ifndef ZXVA_MAX_FVOICES
#    define ZXVA_MAX_FVOICES 32 /* FVOICE chunks                            */
#endif
#define ZXVA_MAX_CH      8
#define ZXVA_MAX_BANDS   32
#define ZXVA_MAX_MODAL   32 /* partials of one MODAL voice                   */
#define ZXVA_PLUCK_HARM  48 /* harmonics of a PLUCK voice                    */
#define ZXVA_MAX_ENV     32 /* envelope points of one VOICE                  */
#define ZXVA_FV_PARTIALS 16 /* partials of one FVOICE tone                   */
#define ZXVA_FV_ECHO     6  /* echo taps of one FVOICE                       */
#define ZXVA_MAX_GRAINS  65535u
#define ZXVA_BLOCK       256  /* internal mix block, frames                  */
#define ZXVA_RES_BLOCK   4096 /* residual block, frames (format constant)   */
#define ZXVA_NOISE_K     16   /* sinusoids in a noise grain                  */

#define ZXVA_RATE_MIN 8000u
#define ZXVA_RATE_MAX 768000u

/* chunk tags */
enum {
    ZXVA_END = 0,
    ZXVA_PARTIAL = 1,
    ZXVA_NOISE = 2,
    ZXVA_GRAINS = 3,
    ZXVA_VOICE = 4,
    ZXVA_RESIDUAL = 5,
    ZXVA_FVOICE = 6,
    ZXVA_NORM = 7,
    ZXVA_META = 8 /* provenance and measured fidelity; never affects samples */
};
enum { ZXVA_V_MODAL = 0, ZXVA_V_FM = 1, ZXVA_V_PLUCK = 2 };
enum { ZXVA_FV_TONE = 0, ZXVA_FV_NOISE = 1, ZXVA_FV_SWEEP = 2 };
enum { ZXVA_ENV_NONE = 0, ZXVA_ENV_ASR = 1, ZXVA_ENV_HALFSINE = 2, ZXVA_ENV_AD = 3 };

/* zxva_open flags: which layers to render, and options */
#define ZXVA_L_PARTIAL  0x01u
#define ZXVA_L_NOISE    0x02u
#define ZXVA_L_GRAIN    0x04u
#define ZXVA_L_VOICE    0x08u
#define ZXVA_L_FVOICE   0x10u
#define ZXVA_L_ALL      0x1Fu
#define ZXVA_F_DITHER   0x20u /* TPDF dither when quantising                */
#define ZXVA_F_RESIDUAL 0x40u /* add the residual (needs native rate+depth) */
#define ZXVA_F_NONORM   0x80u /* ignore the NORM chunk                      */

/* errors */
#define ZXVA_EFORMAT (-1) /* malformed file                                 */
#define ZXVA_ECAP    (-2) /* valid but over this build's caps               */
#define ZXVA_EARG    (-3) /* bad argument (rate, bits, flags, NULL)         */

typedef struct {
    uint32_t nch, dur_us, seed;
    uint32_t npartial, nnoise, ngrain, nvoice, nfvoice;
    uint32_t res_rate, res_bits; /* 0 if the file has no residual           */
    uint64_t res_frames;
    bool has_norm;
    bool has_meta;
} zxva_info_t;

/* ---- META (chunk 0x08): where a file came from and how faithful it is ---
 * Informative only: rendering ignores it, so adding or removing it never
 * changes a sample (and never invalidates a residual). Added after version 1
 * shipped without bumping the version: every file valid before is still
 * valid; a file with META is refused by a reader that predates it (that
 * reader fails closed on an unknown tag, as it must). Byte layout in ZXVA.md
 * section 3. */
#define ZXVA_CID_LEN 36 /* CIDv1 raw sha2-256: 01 55 12 20 || SHA-256(bytes) */
enum {
    ZXVA_SRC_UNKNOWN = 0,
    ZXVA_SRC_WAV = 1,
    ZXVA_SRC_AIFF = 2,
    ZXVA_SRC_FLAC = 3,
    ZXVA_SRC_PCM = 4,
    ZXVA_SRC_OTHER = 5
};
enum { ZXVA_AN_UNKNOWN = 0, ZXVA_AN_KERNEL = 1, ZXVA_AN_WAV2ZXVA = 2, ZXVA_AN_AUTHOR = 3 };
enum { ZXVA_EXACT_NONE = 0, ZXVA_EXACT_VERIFIED = 1, ZXVA_EXACT_UNVERIFIED = 2 };
typedef struct {
    uint32_t mver;               /* 1; a higher version is skipped (len) */
    uint8_t cid[ZXVA_CID_LEN];   /* the ORIGINAL file's bytes, kept by CID */
    uint64_t src_bytes;          /* size of that file                     */
    uint32_t src_rate, src_bits; /* its native rate and depth (0 unknown) */
    uint32_t src_kind;           /* ZXVA_SRC_*                            */
    uint32_t analyser;           /* ZXVA_AN_*                             */
    int32_t snr_cdb;             /* parametric render vs source, 1/100 dB */
    uint32_t band_err_cdb;       /* ERB band-envelope RMS error, 1/100 dB */
    uint32_t exact;              /* ZXVA_EXACT_*                          */
} zxva_meta_t;

/* Read the META chunk of a valid file. 0 = found (version 1, filled in),
 * 1 = no META or a newer META version (m untouched), < 0 = file invalid. */
int zxva_meta(const uint8_t *d, uint32_t n, zxva_meta_t *m);

/* ---- work memory (opaque in spirit; sized here so the caller owns it) --- */
typedef struct {
    const uint8_t *hdr, *p;
    uint32_t left, tstart, tend;
    uint8_t ch, locked, active, have;
    int32_t g[2];
    uint32_t ta, tb, fa, fb, ca, cb;
    int32_t dph;
    uint64_t pha, n0, n1, A;
    int64_t B, G, Gh, Aa, Sa;
    int sh;
    uint8_t bl; /* 0 full, 1 fade per sample, 2 silent */
} zxva_ptrack_t;

typedef struct {
    const uint8_t *hdr, *fr;
    uint8_t ch, nb;
    int32_t g[2];
    uint32_t t0, hop, nfr, fi; /* fi: index of the frame in cur[]          */
    uint16_t edge[ZXVA_MAX_BANDS + 1];
    int32_t cc[ZXVA_MAX_BANDS], nc[ZXVA_MAX_BANDS]; /* codes of fi, fi+1     */
    int64_t ca[ZXVA_MAX_BANDS], na[ZXVA_MAX_BANDS]; /* their amplitudes, Q30 */
    int64_t b0[ZXVA_MAX_BANDS], a1[ZXVA_MAX_BANDS], a2[ZXVA_MAX_BANDS];
    int64_t nrm[ZXVA_MAX_BANDS];   /* Q30, 0 = band off at this rate          */
    int64_t st[ZXVA_MAX_BANDS][5]; /* x1 x2 y1 y2 err                      */
    uint64_t n0, pos;
} zxva_ntrack_t;

typedef struct {
    uint64_t ph[ZXVA_FV_PARTIALS], inc[ZXVA_FV_PARTIALS];
    int64_t e[ZXVA_FV_PARTIALS];
    int64_t lp, hp, E;
    uint64_t hq, hr; /* HALFSINE phase floor(i 2^63 / len), stepped exactly */
    uint32_t s;
    uint64_t i; /* next frame index of this instance                       */
} zxva_fvstate_t;

typedef struct {
    const uint8_t *p;
    uint32_t rate, start, len, kind, ch, env, envA, envB, necho, delay, egain, mirror;
    int64_t g[2];
    /* rate-converted parameters */
    uint64_t start_r, len_r, delay_r, A_r, R_r, hsq, hsr;
    int64_t dec_r, glide_r, alp_r, ahp_r;
    uint32_t np;
    uint64_t pinc[ZXVA_FV_PARTIALS];
    uint64_t pA[ZXVA_FV_PARTIALS];
    int64_t pdec[ZXVA_FV_PARTIALS], pamp[ZXVA_FV_PARTIALS];
    uint32_t seed, mul, add;
    int64_t amp;
    uint64_t inc0;
    zxva_fvstate_t inst[ZXVA_FV_ECHO + 1];
} zxva_fvoice_t;

typedef struct {
    const uint8_t *d;
    uint32_t n;
    zxva_info_t info;
    uint32_t rate, bits, flags;
    uint64_t frames, inc_us; /* inc_us: 1e6 * 2^32 / rate, Q32 us/sample    */
    uint32_t ntrack, nnoise, nvoice, nfv;
    zxva_ptrack_t trk[ZXVA_MAX_TRACKS];
    zxva_ntrack_t noi[ZXVA_MAX_NOISE];
    const uint8_t *voice[ZXVA_MAX_VOICES];
    zxva_fvoice_t fv[ZXVA_MAX_FVOICES];
    const uint8_t *grains; /* first grain record, or 0                     */
    uint32_t ngrain;
    const uint8_t *gcur; /* grain cursor: first grain that may sound      */
    uint32_t gcur_i, gcur_t0;
    uint64_t norm_g; /* Q32 gain, 0 = none                            */
    /* residual */
    const uint8_t *res, *rcur;
    uint64_t rblk;  /* block decoded in rbuf, or ~0                         */
    uint64_t rnext; /* block rcur points at                                  */
    int32_t rbuf[ZXVA_RES_BLOCK * ZXVA_MAX_CH];
    int64_t acc[ZXVA_BLOCK * ZXVA_MAX_CH];
} zxva_work_t;

/* Validate the whole file (A7). 0 = valid; ZXVA_EFORMAT / ZXVA_ECAP. info
 * may be NULL. */
int zxva_check(const uint8_t *d, uint32_t n, zxva_info_t *info);

/* Prepare to render at `rate` (8000..768000) and `bits` (16, 24, 32) with
 * ZXVA_L_* | ZXVA_F_* flags (0 means ZXVA_L_ALL). The data must stay alive and
 * unchanged while w is used. ZXVA_F_RESIDUAL needs a residual and the rate and
 * bits it was made at. */
int zxva_open(zxva_work_t *w, const uint8_t *d, uint32_t n, uint32_t rate, uint32_t bits,
              uint32_t flags);

/* frames in the whole sound at the open rate: ceil(dur_us * rate / 1e6) */
uint64_t zxva_frames(const zxva_work_t *w);

/* Render frames [frame, frame + nframes) into out (interleaved, nch per frame,
 * right-justified `bits`-bit samples in int32). Frames past the end are 0.
 * Returns nframes, or < 0. */
int32_t zxva_render(zxva_work_t *w, uint64_t frame, int32_t *out, uint32_t nframes);

/* The same span, but ADD the unquantised Q40 mix (after NORM) to acc
 * (interleaved int64): for mixing several sounds (cues) on one bus before one
 * quantisation. The residual is not added here. */
int32_t zxva_render_acc(zxva_work_t *w, uint64_t frame, int64_t *acc, uint32_t nframes);

/* Quantise a Q40 bus to `bits` as zxva_render does (A6). `frame` and `seed`
 * key the dither; pass dither = false for plain rounding. */
void zxva_quantise(const int64_t *acc, int32_t *out, uint32_t nframes, uint32_t nch, uint32_t bits,
                   bool dither, uint32_t seed, uint64_t frame);

/* ---- primitives, exported for tools, tests and recipe ports ------------ */
int32_t zxva_sin_q30(uint64_t phase);  /* sin(2 pi phase / 2^64), Q30   */
int64_t zxva_code_amp(uint32_t code);  /* level code -> linear Q30      */
int64_t zxva_exp2_q30(int64_t e_q32);  /* 2^(e / 2^32), Q30, saturates  */
int64_t zxva_log2_q32(uint64_t x_q30); /* log2(x / 2^30), Q32; x > 0    */
int64_t zxva_pow_q30(uint64_t x_q30, uint32_t num, uint32_t den); /* (x/2^30)^(num/den) */
uint32_t zxva_lcg(uint32_t s, uint32_t mul, uint32_t add);        /* s*mul+add     */
int64_t zxva_onepole(int64_t y, int64_t x, int64_t alpha_q16);    /* y+((x-y)a>>16) */
int64_t zxva_decay(int64_t e, int64_t mult_q30);                  /* (e*m)>>30      */

#endif
