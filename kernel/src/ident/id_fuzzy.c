/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* id_fuzzy.c -- binary BCH(255, k, t) codes and the code-offset fuzzy
 * extractor (Dodis, Reyzin, Smith 2004; Juels-Wattenberg fuzzy
 * commitment).
 *
 * GF(2^8) with x^8 + x^4 + x^3 + x^2 + 1 (0x11D), alpha = 2. The generator
 * polynomial is the product of the distinct minimal polynomials of
 * alpha^1 .. alpha^2t, built at run time. Decoding: syndromes,
 * Berlekamp-Massey, Chien search; a locator whose root count differs from
 * its degree is a decoding failure, so a vector more than t bits away
 * from every codeword is (almost always) rejected rather than mis-decoded.
 *
 * Gen(w): for each 255-bit block w_b pick a random codeword c_b; helper
 * P_b = w_b XOR c_b; key = SHAKE256("ZXV-ident/fe-key" || salt || w).
 * Rep(w', P): c'_b = decode(w'_b XOR P_b), w_b = c'_b XOR P_b, same key.
 *
 * HONEST LIMITS: P leaks up to n-k bits of w per block; uniform random
 * bit errors are what BCH corrects best, and real biometric noise is
 * bursty and feature-dependent, so the host must supply stable bits.
 * Table lookups indexed by syndromes are not constant time.
 */
#include "id_internal.h"

static uint8_t gexp[512], glog[256];
static bool gf_ready;

static void gf_init(void)
{
    uint32_t x = 1;
    if (gf_ready) return;
    for (uint32_t i = 0; i < 255; i++) {
        gexp[i] = (uint8_t) x;
        glog[x] = (uint8_t) i;
        x <<= 1;
        if (x & 0x100u) x ^= 0x11Du;
    }
    for (uint32_t i = 255; i < 512; i++) gexp[i] = gexp[i - 255];
    glog[0] = 0;
    gf_ready = true;
}

static uint8_t gmul(uint8_t a, uint8_t b)
{
    if (!a || !b) return 0;
    return gexp[(uint32_t) glog[a] + glog[b]];
}

static uint8_t ginv(uint8_t a)
{
    return gexp[255u - glog[a]];
}

/* Generator cache. */
static uint32_t gen_t;
static uint32_t gen_deg;
static uint8_t gen_poly[256]; /* coefficients 0/1, index = power */

static bool build_gen(uint32_t t)
{
    uint8_t used[255];
    uint8_t mp[256]; /* GF(256) coefficients of a minimal polynomial */
    uint8_t g2[256];
    if (t < ID_FE_T_MIN || t > ID_FE_T_MAX) return false;
    gf_init();
    if (gen_t == t) return true;
    id_mset(used, 0, sizeof used);
    id_mset(gen_poly, 0, sizeof gen_poly);
    gen_poly[0] = 1;
    gen_deg = 0;
    for (uint32_t i = 1; i <= 2u * t; i++) {
        if (used[i]) continue;
        uint32_t md = 0;
        id_mset(mp, 0, sizeof mp);
        mp[0] = 1;
        uint32_t c = i;
        do {
            used[c] = 1;
            /* mp *= (x + alpha^c) */
            uint8_t r = gexp[c];
            for (uint32_t j = md + 1; j > 0; j--) mp[j] = (uint8_t) (mp[j - 1] ^ gmul(mp[j], r));
            mp[0] = gmul(mp[0], r);
            md++;
            c = (c * 2u) % 255u;
        } while (c != i);
        for (uint32_t j = 0; j <= md; j++)
            if (mp[j] > 1) return false; /* not a binary polynomial: cannot happen */
        if (gen_deg + md > 254u) return false;
        id_mset(g2, 0, sizeof g2);
        for (uint32_t a = 0; a <= gen_deg; a++)
            if (gen_poly[a])
                for (uint32_t b = 0; b <= md; b++) g2[a + b] ^= mp[b];
        gen_deg += md;
        id_mcpy(gen_poly, g2, sizeof g2);
    }
    gen_t = t;
    return true;
}

uint32_t id_bch_k(uint32_t t)
{
    if (!build_gen(t)) return 0;
    return ID_BCH_N - gen_deg;
}

static uint8_t bit_get(const uint8_t *p, uint32_t i)
{
    return (uint8_t) ((p[i >> 3] >> (i & 7u)) & 1u);
}

static void bit_put(uint8_t *p, uint32_t i, uint8_t v)
{
    p[i >> 3] = (uint8_t) ((p[i >> 3] & ~(1u << (i & 7u))) | ((v & 1u) << (i & 7u)));
}

id_status_t id_bch_encode(uint32_t t, const uint8_t *msg, uint8_t cw[32])
{
    uint8_t work[ID_BCH_N];
    if (!msg || !cw || !build_gen(t)) return ID_ERR_ARG;
    uint32_t k = ID_BCH_N - gen_deg;
    id_mset(work, 0, sizeof work);
    for (uint32_t i = 0; i < k; i++) work[gen_deg + i] = bit_get(msg, i);
    for (uint32_t i = ID_BCH_N; i-- > gen_deg;) {
        if (!work[i]) continue;
        for (uint32_t j = 0; j <= gen_deg; j++) work[i - gen_deg + j] ^= gen_poly[j];
    }
    id_mset(cw, 0, 32);
    for (uint32_t i = 0; i < gen_deg; i++) bit_put(cw, i, work[i]);
    for (uint32_t i = 0; i < k; i++) bit_put(cw, gen_deg + i, bit_get(msg, i));
    id_wipe(work, sizeof work);
    return ID_OK;
}

static bool syndromes(uint32_t t, const uint8_t cw[32], uint8_t S[2 * ID_FE_T_MAX + 1])
{
    bool any = false;
    for (uint32_t j = 1; j <= 2u * t; j++) {
        uint8_t s = 0;
        for (uint32_t i = 0; i < ID_BCH_N; i++)
            if (bit_get(cw, i)) s ^= gexp[(i * j) % 255u];
        S[j] = s;
        any |= s != 0;
    }
    return any;
}

int32_t id_bch_decode(uint32_t t, uint8_t cw[32])
{
    uint8_t S[2 * ID_FE_T_MAX + 1];
    uint8_t C[2 * ID_FE_T_MAX + 2], B[2 * ID_FE_T_MAX + 2], T[2 * ID_FE_T_MAX + 2];
    if (!cw || !build_gen(t)) return -1;
    cw[31] &= 0x7fu; /* bit 255 is not part of the code */
    if (!syndromes(t, cw, S)) return 0;
    uint32_t L = 0, m = 1, n2 = 2u * t;
    uint8_t b = 1;
    id_mset(C, 0, sizeof C);
    id_mset(B, 0, sizeof B);
    C[0] = B[0] = 1;
    for (uint32_t n = 0; n < n2; n++) {
        uint8_t d = S[n + 1];
        for (uint32_t i = 1; i <= L; i++) d ^= gmul(C[i], S[n + 1 - i]);
        if (d == 0) {
            m++;
            continue;
        }
        uint8_t coef = gmul(d, ginv(b));
        if (2u * L <= n) {
            id_mcpy(T, C, sizeof C);
            for (uint32_t i = 0; i + m <= n2 + 1u; i++) C[i + m] ^= gmul(coef, B[i]);
            L = n + 1u - L;
            id_mcpy(B, T, sizeof T);
            b = d;
            m = 1;
        } else {
            for (uint32_t i = 0; i + m <= n2 + 1u; i++) C[i + m] ^= gmul(coef, B[i]);
            m++;
        }
    }
    if (L > t) return -1;
    uint32_t roots = 0;
    uint8_t pos[ID_FE_T_MAX];
    for (uint32_t i = 0; i < ID_BCH_N; i++) {
        uint8_t v = C[0];
        for (uint32_t k = 1; k <= L; k++) {
            if (!C[k]) continue;
            uint32_t e = (255u - (i * k) % 255u) % 255u;
            v ^= gmul(C[k], gexp[e]);
        }
        if (v == 0) {
            if (roots >= L) return -1;
            pos[roots++] = (uint8_t) i;
        }
    }
    if (roots != L) return -1;
    for (uint32_t i = 0; i < roots; i++) cw[pos[i] >> 3] ^= (uint8_t) (1u << (pos[i] & 7u));
    if (syndromes(t, cw, S)) return -1;
    return (int32_t) roots;
}

/* ---- fuzzy extractor ---- */
static bool fe_blocks(uint32_t nbits, uint32_t *blocks)
{
    uint32_t b = nbits / ID_BCH_N;
    if (b == 0 || b > ID_FE_MAX_BLOCKS || b * ID_BCH_N != nbits) return false;
    *blocks = b;
    return true;
}

static void fe_get_block(const uint8_t *f, uint32_t blk, uint8_t out[32])
{
    id_mset(out, 0, 32);
    for (uint32_t i = 0; i < ID_BCH_N; i++) bit_put(out, i, bit_get(f, blk * ID_BCH_N + i));
}

static void fe_key(const uint8_t *w, uint32_t nbits, const uint8_t salt[ID_SALT], uint8_t key[32])
{
    uint8_t nb[2] = {(uint8_t) (nbits >> 8), (uint8_t) nbits};
    id_shake(key, 32, "ZXV-ident/fe-key", salt, ID_SALT, nb, 2, w, (nbits + 7u) / 8u);
}

id_status_t id_fe_gen(uint32_t t, const uint8_t *features, uint32_t nbits, const id_host_t *h,
                      const uint8_t salt[ID_SALT], uint8_t *helper, uint8_t key[32])
{
    uint32_t blocks;
    uint8_t wb[32], msg[32], cw[32];
    if (!features || !h || !h->random || !salt || !helper || !key) return ID_ERR_ARG;
    if (!fe_blocks(nbits, &blocks) || !build_gen(t)) return ID_ERR_ARG;
    uint32_t k = ID_BCH_N - gen_deg;
    for (uint32_t b = 0; b < blocks; b++) {
        fe_get_block(features, b, wb);
        h->random(h->ctx, msg, 32);
        for (uint32_t i = k; i < 256; i++) bit_put(msg, i, 0);
        id_bch_encode(t, msg, cw);
        for (uint32_t i = 0; i < 32; i++) helper[32u * b + i] = (uint8_t) (wb[i] ^ cw[i]);
    }
    fe_key(features, nbits, salt, key);
    id_wipe(wb, 32);
    id_wipe(msg, 32);
    id_wipe(cw, 32);
    return ID_OK;
}

id_status_t id_fe_rep(uint32_t t, const uint8_t *features, uint32_t nbits, const uint8_t *helper,
                      const uint8_t salt[ID_SALT], uint8_t key[32])
{
    uint32_t blocks;
    uint8_t wb[32];
    uint8_t w[ID_FE_MAX_BYTES];
    id_status_t st = ID_OK;
    if (!features || !helper || !salt || !key) return ID_ERR_ARG;
    if (!fe_blocks(nbits, &blocks) || !build_gen(t)) return ID_ERR_ARG;
    id_mset(w, 0, sizeof w);
    for (uint32_t b = 0; b < blocks; b++) {
        fe_get_block(features, b, wb);
        for (uint32_t i = 0; i < 32; i++) wb[i] ^= helper[32u * b + i];
        if (id_bch_decode(t, wb) < 0) {
            st = ID_ERR_NOMATCH;
            break;
        }
        for (uint32_t i = 0; i < 32; i++) wb[i] ^= helper[32u * b + i];
        for (uint32_t i = 0; i < ID_BCH_N; i++) bit_put(w, b * ID_BCH_N + i, bit_get(wb, i));
    }
    if (st == ID_OK)
        fe_key(w, nbits, salt, key);
    else
        id_mset(key, 0, 32);
    id_wipe(w, sizeof w);
    id_wipe(wb, sizeof wb);
    return st;
}
