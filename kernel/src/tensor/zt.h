/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* zt.h — ZXV tensor engine: the integer arithmetic that runs a language
 * model, built on the golden ratio.
 *
 * Every value is a whole number, so a model gives bit-for-bit the same
 * answer on every machine (Intel, Apple Silicon, ARM, RISC-V), which also
 * lets a witness check an agent's work by re-running it.
 *
 *   T1  FIXED POINT.  Activations are Q16.16 (zt_fx): value = raw / 65536.
 *   T2  BLOCKS.  Weights and activations are quantised in blocks of 32
 *       eight-bit values with one scale each (zt_q8_t), the layout used by
 *       today's 8-bit model files. Dot products accumulate in integers. A
 *       scale can carry a right shift (scale / 2^shift), so the tiny f16
 *       scales in model files are held exactly instead of rounded to Q16.
 *   T3  GOLDEN SCALES.  A block's scale can be snapped up to a power of phi,
 *       stored as one signed byte k. phi^k = F(k) phi + F(k-1) exactly, so the
 *       ladder is computed from Fibonacci numbers, not floating point. Steps
 *       of 1.618 instead of 2 waste less of each block's range.
 *   T4  FIBONACCI TILING.  Matrix products split the larger side at the
 *       golden ratio (F(n-1) : F(n-2)) until tiles fit in 21 x 21, so the
 *       working set shrinks the way a cache wants on any machine (it is
 *       cache-oblivious). The result is identical to the plain product.
 *   T5  THE NONLINEAR PARTS.  RMS norm (integer square root), softmax and
 *       SiLU (2^x from a 257-entry table with interpolation), all integer.
 *   T6  FIBONACCI HASHING.  The attention cache places a key at
 *       (key * 2^64 / phi) >> (64 - bits), Knuth's golden multiplicative
 *       hash, which spreads consecutive keys as evenly as possible.
 *   T7  NESTED GOLDEN COILS.  The tensor scaffold is ten nested shells, laid
 *       out the way an ABHA coil is wound. Shell s holds F(b + s) slots, so
 *       each shell is about phi times the one inside it. The wire visits a
 *       shell's slots by a fixed stride: the golden stride F(b + s - 1), or
 *       the ABHA doubling stride 2 when the shell size is odd. Either is a
 *       bijection, so every element has exactly one place and one way back.
 *       Consecutive elements land about size/phi apart (the three-gap
 *       theorem), which spreads neighbouring rows across memory banks.
 *   T8  SCALING, NOT FLAT.  The shells form a hierarchy: slot j of shell s+1
 *       has the parent j * F(b+s) / F(b+s+1) in shell s (about j / phi). The
 *       number of places grows by phi per shell, the exponential growth of
 *       hyperbolic space, so the distance between two places is the number
 *       of steps to their common ancestor (a tree metric), not a straight
 *       line. Nearby data shares coarse summaries in the inner shells. The
 *       parent links form a dendritic tree, branching about phi ways per
 *       shell, and T11 repeats that tree inside every slot: a dendritic
 *       fractal.
 *       THE CORE.  In the hole of the torus sits the core coil (the STA
 *       position): F(b - 1) slots wound straight along the axis (stride 1),
 *       one golden step smaller than shell 0. Shell 0's parents are core
 *       slots, so every path through the coil ends in the core, and the core
 *       is the engine's hot working set.
 *   T9  INTERACTION SURPLUS.  The ISF functional f(u) = ln(1 + (N-1) u) with
 *       u = 1 - cos^2(a, b), all in Q16 integers. A gate keeps a vector (an
 *       attention head's output, an agent's answer) only if it adds at least
 *       a threshold of surplus over every vector already kept, so redundant
 *       work is skipped. The open-system ledger Q' = Q - dQ + eS - C says
 *       whether the core can grow this cycle or should get more efficient.
 *   T10 THE COIL AND ITS FIELD ARE DIFFERENT THINGS.  The coil (T7, T8) is
 *       the wiring: where each element lives and the path the core takes to
 *       reach it. The field is what the wiring emits: every place sends its
 *       value inward to its parent, weakened by 1/phi per shell, so an inner
 *       place carries a summary of everything outside it. A query reads the
 *       field near the centre first and only follows the coil outward into
 *       the branches that matter.
 *   T11 FRACTAL, SO DENSITY NEVER OVERLOADS.  A tensor larger than one coil
 *       is a coil of coils: each slot holds another whole coil, as many
 *       levels deep as needed (an element's index written in base `total`,
 *       one digit per level). A device picks the largest coil that fits its
 *       fast memory (zt_coil_fit) and adds levels instead of density, so the
 *       same model runs on a phone, a laptop or a server, only deeper.
 *   T12 ALTERNATING CURRENT.  The field moves in two half-cycles, like an
 *       alternating current. On the positive half every place passes 1/phi^2
 *       of its field inward to its parent, outermost shell first, so charge
 *       gathers in the core. On the negative half every place takes 1/phi^2
 *       of its parent's field back outward, core first. Each move subtracts
 *       exactly what it adds, so the total field is conserved to the unit.
 *   T14 THE FIELD IS READ AS INTERFERENCE.  Two fields meet on the coil: the
 *       evidence for (pos) and against (neg), from two agents, two heads or
 *       the two half-cycles. Where they agree the reading is clear; where
 *       both are strong they interfere, and the place holds a contradiction
 *       instead of exploding (paraconsistent logic). Each place reads one of
 *       the swarm's truth states, in swarm_hk.h order: TRUE, FALSE, GLUT
 *       (both strong), NEUTRAL (both weak), PARADOX (a glut that reaches the
 *       core, so no inner place can settle it: escalate), UNKNOWN (no
 *       evidence at all). The signed interference pos - neg goes with it.
 *   T15 HOLOGRAPHIC CODING: NEGATIVE SPACE COMPRESSES POSITIVE SPACE.  Every
 *       place on the coil is predicted by its parent, and only the
 *       difference is stored: the prediction is the negative space, the
 *       data the positive, and what is kept is the gap between them, which
 *       is small wherever data that shares a parent is alike. Decoding goes
 *       core outward and is exact. Stop after any shell and every deeper
 *       place takes its nearest ancestor's value, so any inner part of the
 *       code holds a coarse picture of the whole, as a piece of a hologram
 *       does. Lossless; the saving depends on how alike neighbours are.
 *   T16 UBH-168 FRAMES WITH ALTERNATING ENDIANNESS.  Tensor words travel in
 *       21-octet UBH-168 frames: one tag octet, then five 32-bit words. The
 *       byte order alternates word by word, little then big then little
 *       (UBH_ENDIAN_MIXED in ubh.h), so a constant reads as a square wave in
 *       the raw bytes. The tag holds the phase of the first word, the word
 *       count and a 4-bit check. Decoding never depends on the machine's own
 *       byte order.
 *   T13 ANY HARDWARE.  A device is described by its fast memory, its banks
 *       and its lanes. zt_device_plan picks the coil and the fractal depth
 *       for it; the arithmetic never changes, so every device computes the
 *       same numbers, and one device can stand in for another.
 *   T17-T20 live with their code: lattice quantisers (zt_lattice.h), GGUF
 *       model files (zt_gguf.h), the BPE tokenizer (zt_tok.h) and rotary
 *       positions (zt_rope.h).
 * Freestanding: no libc, no floating point, no 64-bit division helpers.
 */
#ifndef ZT_H
#define ZT_H

#include <stdint.h>
#include <stdbool.h>

typedef int32_t zt_fx; /* T1 */
#define ZT_ONE   65536
#define ZT_BLOCK 32u

typedef struct {
    int32_t scale; /* Q16: value = q * scale / 65536 */
    int8_t phi_k;  /* T3: scale is phi^k (Q16) when golden */
    uint8_t shift; /* finer scales: value = q * (scale >> shift); 0 from zt_quantize */
    int8_t q[ZT_BLOCK];
} zt_q8_t;

/* Unsigned 64-bit divide without libgcc. */
uint64_t zt_udiv64(uint64_t n, uint64_t d, uint64_t *rem);

/* T3: phi^k in Q16 for -24 <= k <= 40 (0 outside). */
int64_t zt_phi_pow(int32_t k);

/* T2, T3: quantise n values (n a multiple of 32). golden = snap scales to
 * powers of phi. */
void zt_quantize(const zt_fx *x, uint32_t n, zt_q8_t *out, bool golden);
void zt_dequantize(const zt_q8_t *in, uint32_t nblocks, zt_fx *out);
/* T2: dot product of two quantised vectors of nblocks blocks, in Q16. */
zt_fx zt_dot(const zt_q8_t *a, const zt_q8_t *b, uint32_t nblocks);

/* y[r] = W[r] . x for `rows` rows of nblocks blocks each. */
void zt_matvec(const zt_q8_t *w, uint32_t rows, const zt_q8_t *x, uint32_t nblocks, zt_fx *y);
/* T4: C[i][j] = A[i] . B[j] for A (m rows) and B (n rows), nblocks each;
 * C is m x n, row-major. */
void zt_matmul_fib(const zt_q8_t *a, uint32_t m, const zt_q8_t *b, uint32_t n, uint32_t nblocks,
                   zt_fx *c);

/* T5 */
uint32_t zt_isqrt64(uint64_t v);
void zt_rmsnorm(const zt_fx *x, const zt_fx *gain, uint32_t n, zt_fx *y);
zt_fx zt_exp(zt_fx x);                 /* x <= 0; e^x in Q16 */
void zt_softmax(zt_fx *x, uint32_t n); /* in place; outputs sum to 1.0 within n/65536 */
zt_fx zt_silu(zt_fx x);

/* T6 */
uint32_t zt_fib_hash(uint64_t key, uint32_t bits);

/* T7, T8 */
#define ZT_COIL_SHELLS 10u

typedef enum { ZT_WIND_GOLDEN = 0, ZT_WIND_ABHA } zt_wind_t;

typedef struct {
    uint32_t base; /* shell s holds F(base + s) slots */
    uint32_t size[ZT_COIL_SHELLS];
    uint32_t stride[ZT_COIL_SHELLS];
    uint32_t unstride[ZT_COIL_SHELLS]; /* stride's inverse mod size */
    uint32_t offset[ZT_COIL_SHELLS];   /* first element index of each shell */
    uint32_t total;                    /* F(base + 11) - F(base + 1) */
    uint32_t core;                     /* F(base - 1) core slots */
} zt_coil_t;

#define ZT_SHELL_CORE 0xFFFFFFFFu /* zt_place_t.shell of a core slot */

typedef struct {
    uint32_t shell, slot;
} zt_place_t;

/* base 5..36. Returns false if base is out of range. */
bool zt_coil_init(zt_coil_t *c, uint32_t base, zt_wind_t wind);
zt_place_t zt_coil_place(const zt_coil_t *c, uint32_t index); /* index < total */
uint32_t zt_coil_index(const zt_coil_t *c, zt_place_t p);     /* the inverse */
zt_place_t zt_coil_parent(const zt_coil_t *c, zt_place_t p);  /* a core slot is its own parent */
uint32_t zt_coil_dist(const zt_coil_t *c, zt_place_t a, zt_place_t b);
/* T7: phi^-shell in Q16, the radius scale of a shell counted from outside. */
zt_fx zt_coil_scale(uint32_t shell);

/* T10: value is indexed by element (0..total-1). The field lives on the
 * coil: field[offset[shell] + slot], then the core at field[total + slot]. */
void zt_coil_field(const zt_coil_t *c, const zt_fx *value, int64_t *field);

/* T12: phase 0 is the positive (inward) half, 1 the negative (outward). */
void zt_coil_ac(const zt_coil_t *c, int64_t *field, uint32_t phase);

/* T14: pos, neg and the outputs are field-shaped (total + core entries). */
enum {
    ZT_TRUTH_TRUE = 0,
    ZT_TRUTH_FALSE,
    ZT_TRUTH_GLUT,
    ZT_TRUTH_NEUTRAL,
    ZT_TRUTH_PARADOX,
    ZT_TRUTH_UNKNOWN
};
void zt_coil_interfere(const zt_coil_t *c, const int64_t *pos, const int64_t *neg,
                       int64_t threshold, uint8_t *truth, int64_t *interference);

/* T15: arrays are indexed by place (offset[shell] + slot), total entries.
 * Decode `shells` shells exactly (10 for all of them); deeper places take
 * their nearest decoded ancestor's value. */
void zt_holo_encode(const zt_coil_t *c, const int64_t *value, int64_t *residual);
void zt_holo_decode(const zt_coil_t *c, const int64_t *residual, uint32_t shells, int64_t *value);

/* T16 */
#define ZT_FRAME_OCTETS 21u
#define ZT_FRAME_WORDS  5u
/* Pack n words into ceil(n/5) frames; phase 0 starts little-endian. */
uint32_t zt_frame_pack(const uint32_t *words, uint32_t n, uint32_t phase, uint8_t *out);
/* Unpack `frames` frames; returns the word count, or -1 on a bad frame. */
int32_t zt_frame_unpack(const uint8_t *in, uint32_t frames, uint32_t *words);

/* T11 */
#define ZT_FRACTAL_MAX 16u
/* Largest base whose coil holds at most max_elems (at least base 5). */
uint32_t zt_coil_fit(uint64_t max_elems);
/* Levels needed for n elements: the smallest L with total^L >= n. */
uint32_t zt_fractal_levels(const zt_coil_t *c, uint64_t n);
/* out[0] is the outermost (coarsest) level. Returns false if index does not
 * fit in `levels` levels. */
bool zt_fractal_place(const zt_coil_t *c, uint32_t levels, uint64_t index, zt_place_t *out);
uint64_t zt_fractal_index(const zt_coil_t *c, uint32_t levels, const zt_place_t *in);

/* T13 */
typedef struct {
    uint64_t fast_elems; /* elements that fit in the fastest memory */
    uint32_t banks;      /* memory banks or channels, at least 1 */
    uint32_t lanes;      /* parallel lanes (SIMD width, cores), at least 1 */
} zt_device_t;

typedef struct {
    zt_coil_t coil;
    uint32_t levels;
} zt_plan_t;

bool zt_device_plan(const zt_device_t *d, uint64_t n_elems, zt_wind_t wind, zt_plan_t *out);
uint32_t zt_coil_bank(const zt_coil_t *c, zt_place_t p, uint32_t banks);

/* T9 */
zt_fx zt_log2(uint64_t x_q16); /* x > 0, Q16 in and out */
zt_fx zt_ln(uint64_t x_q16);
zt_fx zt_surplus_u(const zt_fx *a, const zt_fx *b, uint32_t n); /* 0..ZT_ONE */
zt_fx zt_surplus_f(zt_fx u, uint32_t N);                        /* 0..ln N */
/* Keep vectors (rows of length n) that each add at least `threshold` surplus
 * over every kept one. keep[i] is set; returns the number kept. */
uint32_t zt_surplus_gate(const zt_fx *v, uint32_t count, uint32_t n, uint32_t N, zt_fx threshold,
                         bool *keep, zt_fx *total_surplus);

typedef struct {
    int64_t Q;   /* stored capacity, Q16 */
    zt_fx delta; /* decay per cycle, Q16 in [0, 1] */
    zt_fx eta;   /* surplus conversion, Q16 */
    uint32_t N;
} zt_isf_t;

void zt_isf_init(zt_isf_t *s, int64_t Q0, zt_fx delta, zt_fx eta, uint32_t N);
bool zt_isf_can_grow(const zt_isf_t *s, zt_fx S, zt_fx C); /* e*S > d*Q + C */
int64_t zt_isf_step(zt_isf_t *s, zt_fx S, zt_fx C);        /* never below 0 */
int64_t zt_isf_ceiling(const zt_isf_t *s);                 /* max(Q, e ln N / d) */

#endif /* ZT_H */
