/* fractal_seed.c — the holographic seed synthesizer (see fractal_seed.h).
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Freestanding: no libc, no float, no allocation. The IFS recurrence is
 * exact integer arithmetic in surplus_real_t (int64 Q32.32 in kernel
 * images): a weight unfolded twice is bit-identical. The phase-lock folds
 * the Solfeggio frequency and the SCP-144 lattice index into the recurrence
 * state so the generated weights align with the system's invariants.
 */
#include "fractal_seed.h"

#include "../robin_debanks/sha256.h"   /* streaming SHA-256 for the digest  */

/* --gc-sections KEEP: no boot caller yet (the Chiglet load path adopts it).
 * Same reasoning and spelling as tvl_rom.c's TVL_KEEP. */
#if defined(__GNUC__) && (__GNUC__ >= 11)
#  define FRS_KEEP __attribute__((used, retain))
#else
#  define FRS_KEEP __attribute__((used))
#endif

/* ---- helpers -------------------------------------------------------------- */

static void frs_zero(void *p, uint32_t n){
    uint8_t *b = (uint8_t *)p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

/* Big-endian scalar reads: the compiler (fractal_seed_compile.py) writes
 * struct.pack(">I") / (">iiiiiiII"), so the on-disk seed is big-endian on
 * every target. Written as shift/mask, not a cast, so the byte order never
 * depends on the host. */
static uint32_t rd32(const uint8_t *p){
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static int32_t rd32s(const uint8_t *p){
    return (int32_t)rd32(p);
}

/* Q32.32 multiply with saturation to int64: a*b >> 32, computed portably
 * with only 64-bit arithmetic (no __int128, no 64-bit variable division —
 * the freestanding rule). The product is split as (a_hi*2^16 + a_lo) *
 * (b_hi*2^16 + b_lo); each partial is < 2^49 so no 64-bit overflow in the
 * multiply, and `full` reconstructs the exact two's-complement product, so
 * (int64_t)full >> 32 is the exact arithmetic-shift result. Real inputs
 * (int32 coefficients) cannot overflow int64; the defensive clamp refuses a
 * sign-inconsistent wrap from a malformed seed. */
static int64_t qmul(int64_t a, int64_t b){
    int64_t a_hi = a >> 16, a_lo = a & 0xFFFF;
    int64_t b_hi = b >> 16, b_lo = b & 0xFFFF;
    uint64_t p = (uint64_t)(a_hi * b_hi);          /* < 2^49               */
    uint64_t t = (uint64_t)(a_hi * b_lo + a_lo * b_hi);  /* < 2^50          */
    uint64_t l = (uint64_t)(a_lo * b_lo);          /* < 2^32               */
    /* a*b in Q64.64: p in bits 32..63, t in 16..47, l in 0..31. */
    uint64_t full = (p << 32) | (t << 16) | l;
    int64_t r = (int64_t)full >> 32;               /* exact, sign-extended  */
    /* sign-consistency clamp (never fires on real inputs). */
    if ((a < 0) != (b < 0)){
        if (r > 0) return INT64_MIN;
    } else {
        if (r < 0) return INT64_MAX;
    }
    return r;
}

/* The SCP-144 lattice fold: a deterministic integer mixing of the layer's
 * Solfeggio frequency and the 144-dimensional lattice index, used as the
 * recurrence's per-point seed. Exact, no float. */
static uint64_t lattice_fold(uint32_t freq, uint32_t i, uint32_t j){
    uint64_t x = (uint64_t)freq * 0x9E3779B97F4A7C15ull
               + (uint64_t)i * 0xBF58476D1CE4E5B9ull
               + (uint64_t)j * 0x94D049BB133111EBull;
    x ^= x >> 29; x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 32; x *= 0x94D049BB133111EBull;
    x ^= x >> 29;
    return x;
}

/* ---- parse ---------------------------------------------------------------- */

FRS_KEEP bool fractal_seed_parse(const uint8_t *img, uint32_t len,
                                 fractal_seed_t *out){
    uint8_t calc[FRS_DIGEST_LEN];
    uint32_t layers, i, off;

    if (!img || !out) return false;
    frs_zero(out, (uint32_t)sizeof *out);
    if (len < FRS_HDR_SIZE) return false;
    if (!(img[0] == FRS_MAGIC0 && img[1] == FRS_MAGIC1 &&
          img[2] == FRS_MAGIC2 && img[3] == FRS_MAGIC3)) return false;
    if (rd32(img + 4) != FRS_VERSION) return false;

    layers = rd32(img + 8);
    if (layers == 0u || layers > FRS_MAX_LAYERS) return false;

    /* digest over the WHOLE file with the digest field zeroed (the compiler
     * hashes the same shape): a tampered byte fails the load. */
    {
        uint8_t tmp[12 + FRS_DIGEST_LEN];
        sha256_ctx_t ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, img, 12u);
        for (i = 0; i < FRS_DIGEST_LEN; i++) tmp[i] = 0u;
        sha256_update(&ctx, tmp, FRS_DIGEST_LEN);
        if (len > 12u + FRS_DIGEST_LEN)
            sha256_update(&ctx, img + 12u + FRS_DIGEST_LEN,
                          (size_t)(len - 12u - FRS_DIGEST_LEN));
        sha256_final(&ctx, calc);
    }
    for (i = 0; i < FRS_DIGEST_LEN; i++)
        if (calc[i] != img[12 + i]) return false;

    off = FRS_HDR_SIZE;
    for (i = 0; i < layers; i++){
        fractal_seed_layer_t *l = &out->layers[i];
        uint32_t name_len;
        if (off + 32u > len) return false;
        l->a  = rd32s(img + off);      l->b  = rd32s(img + off + 4);
        l->c  = rd32s(img + off + 8);  l->d  = rd32s(img + off + 12);
        l->tx = rd32s(img + off + 16); l->ty = rd32s(img + off + 20);
        l->freq   = rd32(img + off + 24);
        l->scale  = rd32(img + off + 28);
        off += 32;
        /* the name is the next bytes, NUL-terminated and bounded. */
        name_len = 0u;
        while (off + name_len < len && img[off + name_len] != 0u) name_len++;
        if (name_len == 0u || name_len >= FRS_NAME_LEN) return false;
        for (uint32_t k = 0; k < name_len; k++) l->name[k] = (char)img[off + k];
        l->name[name_len] = 0;
        off += name_len + 1u;
    }
    if (off != len) return false;       /* EXACT: no slack bytes            */

    out->hdr.version = FRS_VERSION;
    out->hdr.layer_count = layers;
    for (i = 0; i < FRS_DIGEST_LEN; i++) out->hdr.digest[i] = img[12 + i];
    out->layer_count = layers;
    out->valid = true;
    return true;
}

/* ---- unfold ---------------------------------------------------------------- */

FRS_KEEP surplus_real_t fractal_seed_unfold(const fractal_seed_t *s,
                                            uint32_t layer,
                                            uint32_t i, uint32_t j){
    const fractal_seed_layer_t *l;
    uint64_t state;
    int64_t x, y;
    int32_t freq;

    if (!s || !s->valid || layer >= s->layer_count) return SR_ZERO;
    l = &s->layers[layer];

    /* initial point from the lattice fold (SCP-144 geometry + Solfeggio). */
    freq = (int32_t)l->freq;
    state = lattice_fold((uint32_t)freq, i, j);
    x = (int64_t)(int32_t)(uint32_t)state;
    y = (int64_t)(int32_t)(uint32_t)(state >> 32);

    /* two affine iterations of the IFS: x' = a*x + b*y + tx, etc. */
    {
        int64_t nx = qmul(l->a, x) + qmul(l->b, y) + (int64_t)l->tx;
        int64_t ny = qmul(l->c, x) + qmul(l->d, y) + (int64_t)l->ty;
        x = nx; y = ny;
        nx = qmul(l->a, x) + qmul(l->b, y) + (int64_t)l->tx;
        ny = qmul(l->c, x) + qmul(l->d, y) + (int64_t)l->ty;
        x = nx; y = ny;
    }

    /* fold the two coordinates into one weight, scaled. */
    {
        int64_t w = (x ^ (y >> 1)) * (int64_t)l->scale;
        return (surplus_real_t)(w >> 32);
    }
}

/* ---- build a Chiglet model from the seed ----------------------------------- */

FRS_KEEP bool fractal_seed_build_model(const fractal_seed_t *s, chg_model_t *m){
    uint32_t layer, i, j;
    if (!s || !s->valid || !m) return false;
    if (m->K > CHG_MAX_EXPERTS || m->D > CHG_DIM || m->L > CHG_MAX_LABELS)
        return false;
    if (s->layer_count < 1u) return false;

    for (layer = 0; layer < s->layer_count && layer < FRS_MAX_LAYERS; layer++){
        /* each layer unfolds one prototype row (label) across the experts. */
        if (layer >= m->L) break;
        for (j = 0; j < m->D; j++){
            /* average the layer's fold over the experts for this column. */
            int64_t acc = 0;
            for (i = 0; i < m->K && i < CHG_MAX_EXPERTS; i++){
                acc += (int64_t)fractal_seed_unfold(s, layer, i, j);
            }
            m->proto[layer][j] = (surplus_real_t)(acc / (int64_t)m->K);
        }
    }
    m->loaded = true;
    return true;
}

FRS_KEEP const uint8_t *fractal_seed_digest(const fractal_seed_t *s){
    return s ? s->hdr.digest : NULL;
}