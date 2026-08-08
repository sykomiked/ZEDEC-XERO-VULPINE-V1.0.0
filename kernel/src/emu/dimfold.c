/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* dimfold.c — Dimensional Fold: flatten/unflatten as compression + encryption +
 * a reversible nested transform. See dimfold.h. */
#include "dimfold.h"
#include "sha256.h"

/* ======================================================================== *
 * 1. TRANSFORM — reversible integer Haar / S-transform, applied at NESTED
 *    levels (the Russian-doll matrix). One level splits pairs (x0,x1) into a
 *    smooth coefficient s and a detail d, exactly reversibly:
 *        d = x1 - x0 ;  s = x0 + (d >> 1)         (>>1 = floor divide by 2)
 *      inverse:  x0 = s - (d >> 1) ;  x1 = x0 + d
 *    The smooth half is recursed — coarser and coarser — so a[0] ends up the
 *    single coarse IDENTITY and the rest are nested detail SHELLS (φ-scaled
 *    multiresolution). Smooth / self-similar data pushes almost all detail
 *    coefficients to zero, which is what makes compression work.
 * ======================================================================== */

void dimfold_fold(int32_t *a, int n){
    int32_t tmp[1024];                       /* n <= 1024 */
    for (int len = n; len >= 2; len >>= 1){
        int half = len >> 1;
        for (int i = 0; i < half; i++){
            int32_t x0 = a[2*i], x1 = a[2*i+1];
            int32_t d  = x1 - x0;
            int32_t s  = x0 + (d >> 1);
            tmp[i]        = s;               /* smooth -> low half  */
            tmp[half + i] = d;               /* detail -> high half */
        }
        for (int i = 0; i < len; i++) a[i] = tmp[i];
    }
}

void dimfold_unfold(int32_t *a, int n){
    int32_t tmp[1024];
    for (int len = 2; len <= n; len <<= 1){
        int half = len >> 1;
        for (int i = 0; i < half; i++){
            int32_t s = a[i], d = a[half + i];
            int32_t x0 = s - (d >> 1);
            int32_t x1 = x0 + d;
            tmp[2*i]   = x0;
            tmp[2*i+1] = x1;
        }
        for (int i = 0; i < len; i++) a[i] = tmp[i];
    }
}

/* ======================================================================== *
 * 2. COMPRESS / EXPAND — fold the bytes, then code the sparse coefficients
 *    with zig-zag varints and zero-run skips. Lossless.
 * ======================================================================== */

static int next_pow2(int n){ int p = 1; while (p < n) p <<= 1; return p; }

int dimfold_bound(int n){ return n * 5 + 16; }   /* worst case: no coeff is zero */

/* LEB128 unsigned varint */
static int put_varint(uint8_t *o, int oi, uint32_t v){
    while (v >= 0x80){ o[oi++] = (uint8_t)(v | 0x80); v >>= 7; }
    o[oi++] = (uint8_t)v; return oi;
}
static int get_varint(const uint8_t *in, int n, int *pi, uint32_t *out){
    uint32_t v = 0; int shift = 0, i = *pi;
    while (i < n){
        uint8_t b = in[i++];
        v |= (uint32_t)(b & 0x7F) << shift;
        if (!(b & 0x80)){ *pi = i; *out = v; return 1; }
        shift += 7; if (shift > 28) break;
    }
    return 0;                                 /* truncated */
}
static uint32_t zigzag(int32_t v){ return ((uint32_t)v << 1) ^ (uint32_t)(v >> 31); }
static int32_t  unzigzag(uint32_t z){ return (int32_t)(z >> 1) ^ -(int32_t)(z & 1); }

int dimfold_compress(const uint8_t *in, int n, uint8_t *out, int out_cap){
    if (n <= 0 || out_cap < dimfold_bound(n)) return 0;
    int N = next_pow2(n);
    if (N > 1024) return 0;                   /* block cap; caller chunks larger */
    int32_t a[1024];
    for (int i = 0; i < N; i++) a[i] = (i < n) ? (int32_t)in[i] : 0;
    dimfold_fold(a, N);

    int oi = 0;
    oi = put_varint(out, oi, (uint32_t)n);    /* original length */
    /* trim trailing zero coefficients (the decoder refills them) */
    int last = N - 1; while (last >= 0 && a[last] == 0) last--;
    int i = 0;
    while (i <= last){
        uint32_t run = 0;
        while (i <= last && a[i] == 0){ run++; i++; }
        oi = put_varint(out, oi, run);        /* zeros to skip */
        if (i <= last){ oi = put_varint(out, oi, zigzag(a[i])); i++; }
    }
    return oi;
}

int dimfold_expand(const uint8_t *in, int n, uint8_t *out, int out_cap){
    int pi = 0; uint32_t orig = 0;
    if (!get_varint(in, n, &pi, &orig)) return 0;
    if ((int)orig <= 0 || (int)orig > out_cap) return 0;
    int N = next_pow2((int)orig);
    if (N > 1024) return 0;
    int32_t a[1024];
    for (int i = 0; i < N; i++) a[i] = 0;
    int idx = 0;
    while (pi < n && idx < N){
        uint32_t run = 0;
        if (!get_varint(in, n, &pi, &run)) break;
        idx += (int)run;
        if (pi < n && idx < N){
            uint32_t z = 0;
            if (!get_varint(in, n, &pi, &z)) break;
            a[idx++] = unzigzag(z);
        }
    }
    dimfold_unfold(a, N);
    for (int i = 0; i < (int)orig; i++){
        int32_t v = a[i]; if (v < 0) v = 0; if (v > 255) v = 255;
        out[i] = (uint8_t)v;
    }
    return (int)orig;
}

/* ======================================================================== *
 * 3. SEAL / OPEN — the PRIME lock. Keystream = SHA-256d(key || counter),
 *    XORed into the data. Re-applying the same key opens it.
 * ======================================================================== */

static void sha256d(const uint8_t *in, int n, uint8_t out[32]){
    uint8_t t[32]; sha256(in, (size_t)n, t); sha256(t, 32, out);
}
static void keystream_block(const uint8_t *key, int klen, uint32_t ctr, uint8_t out[32]){
    uint8_t buf[72]; int p = 0;
    if (klen > 64) klen = 64;
    for (int i = 0; i < klen; i++) buf[p++] = key[i];
    buf[p++] = (uint8_t)(ctr);       buf[p++] = (uint8_t)(ctr >> 8);
    buf[p++] = (uint8_t)(ctr >> 16); buf[p++] = (uint8_t)(ctr >> 24);
    sha256d(buf, p, out);
}

void dimfold_seal(uint8_t *data, int n, const uint8_t *key, int keylen){
    uint8_t ks[32]; uint32_t ctr = 0;
    for (int i = 0; i < n; i++){
        if ((i & 31) == 0){ keystream_block(key, keylen, ctr, ks); ctr++; }
        data[i] ^= ks[i & 31];
    }
}
void dimfold_open(uint8_t *data, int n, const uint8_t *key, int keylen){
    dimfold_seal(data, n, key, keylen);       /* XOR is involutive */
}

/* ======================================================================== *
 * 4. IDENTITY — SHA-256d over the bytes.
 * ======================================================================== */
void dimfold_identity(const uint8_t *data, int n, uint8_t out[32]){
    sha256d(data, n, out);
}

/* ======================================================================== *
 * Self-check: every face round-trips losslessly; Fibonacci recovery key -> φ.
 * ======================================================================== */
int dimfold_selfcheck(uint32_t *ratio_permille_out){
    /* a representative scanline: piecewise-constant regions (sky / figure / ground)
     * — exactly the self-similar, low-detail structure the fold makes sparse. */
    uint8_t src[256], work[256], back[256];
    for (int i = 0; i < 256; i++){
        uint8_t v = 0x14;                      /* sky run            */
        if (i >= 96 && i < 128) v = 0xE5;      /* figure band        */
        else if (i >= 176)      v = 0x2E;      /* ground run         */
        src[i] = v;
    }

    /* (a) TRANSFORM round-trips exactly */
    int32_t a[256];
    for (int i = 0; i < 256; i++) a[i] = src[i];
    dimfold_fold(a, 256);
    dimfold_unfold(a, 256);
    int transform_ok = 1;
    for (int i = 0; i < 256; i++) if (a[i] != (int32_t)src[i]) transform_ok = 0;

    /* (b) COMPRESS/EXPAND round-trips exactly, and actually shrinks smooth data */
    uint8_t comp[1300];
    int clen = dimfold_compress(src, 256, comp, (int)sizeof comp);
    int xlen = dimfold_expand(comp, clen, back, (int)sizeof back);
    int compress_ok = (clen > 0) && (xlen == 256);
    if (compress_ok) for (int i = 0; i < 256; i++) if (back[i] != src[i]) compress_ok = 0;
    int shrank = compress_ok && (clen < 256);
    uint32_t ratio = clen > 0 ? (uint32_t)((clen * 1000) / 256) : 1000;

    /* (c) SEAL/OPEN round-trips exactly, and the sealed bytes differ from plain */
    static const uint8_t key[16] = { 'Z','X','V','-','d','i','m','f','o','l','d','-','k','e','y','!' };
    for (int i = 0; i < 256; i++) work[i] = src[i];
    dimfold_seal(work, 256, key, 16);
    int changed = 0; for (int i = 0; i < 256; i++) if (work[i] != src[i]) changed = 1;
    dimfold_open(work, 256, key, 16);
    int crypto_ok = changed;
    for (int i = 0; i < 256; i++) if (work[i] != src[i]) crypto_ok = 0;

    /* (d) the FIBONACCI recovery key aligns to φ (the axiom of alignment) */
    long fa = 1, fb = 1; for (int k = 0; k < 40; k++){ long t = fa + fb; fa = fb; fb = t; }
    /* fb/fa ≈ φ; check |fb/fa - φ| small using integer cross-multiply:
     * |fb - φ·fa| < ε·fa  <=>  |1000·fb - 1618·fa| < fa   (φ ≈ 1.618) */
    long lhs = 1000L*fb - 1618L*fa; if (lhs < 0) lhs = -lhs;
    int aligned = (lhs < fa);

    if (ratio_permille_out) *ratio_permille_out = ratio;
    return transform_ok && compress_ok && shrank && crypto_ok && aligned;
}
