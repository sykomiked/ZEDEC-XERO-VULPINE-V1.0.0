/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* id_argon2.c -- BLAKE2b (RFC 7693) and Argon2id v1.3 (RFC 9106), bounded.
 *
 * Memory comes from the caller; parameters are capped (ID_ARGON2_MAX_*).
 * Lanes are computed one after another (no threads), which gives the
 * same tag as a parallel implementation. Checked against the RFC 9106
 * section 5.3 test vector in test_ident.c.
 *
 * HONEST LIMITS: the data-dependent passes access memory at addresses
 * derived from the password (as every Argon2id does); a co-resident
 * attacker who can observe cache lines learns something about it.
 */
#include "id_internal.h"

/* ===== BLAKE2b ===== */
typedef struct {
    uint64_t h[8];
    uint64_t t;
    uint8_t buf[128];
    uint32_t n;
    uint32_t outlen;
} id_b2_t;

static const uint64_t B2_IV[8] = {
    0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
    0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull};

static const uint8_t B2_SIGMA[12][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
};

static uint64_t rotr64(uint64_t x, unsigned n)
{
    return (x >> n) | (x << (64u - n));
}

static uint64_t ld64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

static void st64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void st32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

#define B2G(a, b, c, d, x, y)                                                                      \
    do {                                                                                           \
        v[a] = v[a] + v[b] + (x);                                                                  \
        v[d] = rotr64(v[d] ^ v[a], 32);                                                            \
        v[c] = v[c] + v[d];                                                                        \
        v[b] = rotr64(v[b] ^ v[c], 24);                                                            \
        v[a] = v[a] + v[b] + (y);                                                                  \
        v[d] = rotr64(v[d] ^ v[a], 16);                                                            \
        v[c] = v[c] + v[d];                                                                        \
        v[b] = rotr64(v[b] ^ v[c], 63);                                                            \
    } while (0)

static void b2_compress(id_b2_t *s, const uint8_t blk[128], bool last)
{
    uint64_t m[16], v[16];
    for (int i = 0; i < 16; i++) m[i] = ld64(blk + 8 * i);
    for (int i = 0; i < 8; i++) {
        v[i] = s->h[i];
        v[i + 8] = B2_IV[i];
    }
    v[12] ^= s->t;
    if (last) v[14] = ~v[14];
    for (int r = 0; r < 12; r++) {
        const uint8_t *z = B2_SIGMA[r];
        B2G(0, 4, 8, 12, m[z[0]], m[z[1]]);
        B2G(1, 5, 9, 13, m[z[2]], m[z[3]]);
        B2G(2, 6, 10, 14, m[z[4]], m[z[5]]);
        B2G(3, 7, 11, 15, m[z[6]], m[z[7]]);
        B2G(0, 5, 10, 15, m[z[8]], m[z[9]]);
        B2G(1, 6, 11, 12, m[z[10]], m[z[11]]);
        B2G(2, 7, 8, 13, m[z[12]], m[z[13]]);
        B2G(3, 4, 9, 14, m[z[14]], m[z[15]]);
    }
    for (int i = 0; i < 8; i++) s->h[i] ^= v[i] ^ v[i + 8];
}

static void b2_init(id_b2_t *s, uint32_t outlen)
{
    for (int i = 0; i < 8; i++) s->h[i] = B2_IV[i];
    s->h[0] ^= 0x01010000ull ^ outlen;
    s->t = 0;
    s->n = 0;
    s->outlen = outlen;
}

static void b2_update(id_b2_t *s, const uint8_t *in, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        if (s->n == 128) {
            s->t += 128;
            b2_compress(s, s->buf, false);
            s->n = 0;
        }
        s->buf[s->n++] = in[i];
    }
}

static void b2_final(id_b2_t *s, uint8_t *out)
{
    uint8_t full[64];
    s->t += s->n;
    for (uint32_t i = s->n; i < 128; i++) s->buf[i] = 0;
    b2_compress(s, s->buf, true);
    for (int i = 0; i < 8; i++) st64(full + 8 * i, s->h[i]);
    id_mcpy(out, full, s->outlen);
    id_wipe(full, sizeof full);
    id_wipe(s, sizeof *s);
}

void id_blake2b(const uint8_t *in, uint32_t len, uint8_t *out, uint32_t out_len)
{
    id_b2_t s;
    if (out_len == 0 || out_len > 64) return;
    b2_init(&s, out_len);
    b2_update(&s, in, len);
    b2_final(&s, out);
}

static void b2_u32(id_b2_t *s, uint32_t v)
{
    uint8_t b[4];
    st32(b, v);
    b2_update(s, b, 4);
}

/* H' (RFC 9106 section 3.3): variable-length hash. */
static void hprime(uint8_t *out, uint32_t outlen, const uint8_t *in, uint32_t inlen)
{
    id_b2_t s;
    if (outlen <= 64) {
        b2_init(&s, outlen);
        b2_u32(&s, outlen);
        b2_update(&s, in, inlen);
        b2_final(&s, out);
        return;
    }
    uint8_t v[64];
    uint32_t r = (outlen + 31u) / 32u - 2u, o = 0;
    b2_init(&s, 64);
    b2_u32(&s, outlen);
    b2_update(&s, in, inlen);
    b2_final(&s, v);
    id_mcpy(out, v, 32);
    o = 32;
    for (uint32_t i = 1; i < r; i++) {
        id_blake2b(v, 64, v, 64);
        id_mcpy(out + o, v, 32);
        o += 32;
    }
    id_blake2b(v, 64, out + o, outlen - 32u * r);
    id_wipe(v, sizeof v);
}

/* ===== Argon2 compression G ===== */
#define QW 128u /* 64-bit words per block */

static uint64_t fbla(uint64_t a, uint64_t b)
{
    return a + b + 2u * (uint64_t) (uint32_t) a * (uint64_t) (uint32_t) b;
}

#define AGB(a, b, c, d)                                                                            \
    do {                                                                                           \
        a = fbla(a, b);                                                                            \
        d = rotr64(d ^ a, 32);                                                                     \
        c = fbla(c, d);                                                                            \
        b = rotr64(b ^ c, 24);                                                                     \
        a = fbla(a, b);                                                                            \
        d = rotr64(d ^ a, 16);                                                                     \
        c = fbla(c, d);                                                                            \
        b = rotr64(b ^ c, 63);                                                                     \
    } while (0)

static void perm(uint64_t *v0, uint64_t *v1, uint64_t *v2, uint64_t *v3, uint64_t *v4, uint64_t *v5,
                 uint64_t *v6, uint64_t *v7, uint64_t *v8, uint64_t *v9, uint64_t *v10,
                 uint64_t *v11, uint64_t *v12, uint64_t *v13, uint64_t *v14, uint64_t *v15)
{
    AGB(*v0, *v4, *v8, *v12);
    AGB(*v1, *v5, *v9, *v13);
    AGB(*v2, *v6, *v10, *v14);
    AGB(*v3, *v7, *v11, *v15);
    AGB(*v0, *v5, *v10, *v15);
    AGB(*v1, *v6, *v11, *v12);
    AGB(*v2, *v7, *v8, *v13);
    AGB(*v3, *v4, *v9, *v14);
}

static uint64_t g_r[QW], g_tmp[QW];

static void fill_block(const uint64_t *prev, const uint64_t *ref, uint64_t *next, bool xr)
{
    uint64_t *R = g_r, *T = g_tmp;
    for (uint32_t i = 0; i < QW; i++) {
        R[i] = ref[i] ^ prev[i];
        T[i] = R[i];
        if (xr) T[i] ^= next[i];
    }
    for (uint32_t i = 0; i < 8; i++) {
        uint64_t *q = R + 16u * i;
        perm(&q[0], &q[1], &q[2], &q[3], &q[4], &q[5], &q[6], &q[7], &q[8], &q[9], &q[10], &q[11],
             &q[12], &q[13], &q[14], &q[15]);
    }
    for (uint32_t i = 0; i < 8; i++) {
        uint64_t *q = R + 2u * i;
        perm(&q[0], &q[1], &q[16], &q[17], &q[32], &q[33], &q[48], &q[49], &q[64], &q[65], &q[80],
             &q[81], &q[96], &q[97], &q[112], &q[113]);
    }
    for (uint32_t i = 0; i < QW; i++) next[i] = T[i] ^ R[i];
}

static void blk_from_bytes(uint64_t *b, const uint8_t *p)
{
    for (uint32_t i = 0; i < QW; i++) b[i] = ld64(p + 8u * i);
}

static void blk_to_bytes(uint8_t *p, const uint64_t *b)
{
    for (uint32_t i = 0; i < QW; i++) st64(p + 8u * i, b[i]);
}

void id_kdf_default(id_kdf_params_t *kp)
{
    kp->m_kib = ID_LOOKUP_M_KIB;
    kp->t = ID_LOOKUP_T;
    kp->p = ID_LOOKUP_P;
}

static uint64_t g_zero[QW], g_in[QW], g_addr[QW];

static void next_addresses(void)
{
    g_in[6]++;
    fill_block(g_zero, g_in, g_addr, false);
    fill_block(g_zero, g_addr, g_addr, false);
}

id_status_t id_argon2id(const id_kdf_params_t *kp, const uint8_t *pwd, uint32_t pwd_len,
                        const uint8_t *salt, uint32_t salt_len, const uint8_t *secret,
                        uint32_t secret_len, const uint8_t *ad, uint32_t ad_len, uint64_t *mem,
                        uint32_t mem_blocks, uint8_t *out, uint32_t out_len)
{
    if (!kp || !mem || !out || (!pwd && pwd_len) || !salt) return ID_ERR_ARG;
    uint32_t p = kp->p, t = kp->t;
    if (p == 0 || p > ID_ARGON2_MAX_P || t == 0 || t > ID_ARGON2_MAX_T) return ID_ERR_ARG;
    if (out_len < 4 || out_len > ID_ARGON2_MAX_OUT || salt_len < 8) return ID_ERR_ARG;
    if (kp->m_kib < 8u * p) return ID_ERR_ARG;
    uint32_t mprime = (kp->m_kib / (4u * p)) * (4u * p);
    if (mprime > mem_blocks) return ID_ERR_SIZE;
    uint32_t q = mprime / p, sl = q / 4u;

    /* H0 */
    id_b2_t s;
    uint8_t h0[72], tmp[ID_ARGON2_BLOCK];
    b2_init(&s, 64);
    b2_u32(&s, p);
    b2_u32(&s, out_len);
    b2_u32(&s, kp->m_kib);
    b2_u32(&s, t);
    b2_u32(&s, 0x13u);
    b2_u32(&s, 2u); /* Argon2id */
    b2_u32(&s, pwd_len);
    if (pwd_len) b2_update(&s, pwd, pwd_len);
    b2_u32(&s, salt_len);
    b2_update(&s, salt, salt_len);
    b2_u32(&s, secret ? secret_len : 0);
    if (secret && secret_len) b2_update(&s, secret, secret_len);
    b2_u32(&s, ad ? ad_len : 0);
    if (ad && ad_len) b2_update(&s, ad, ad_len);
    b2_final(&s, h0);

    for (uint32_t l = 0; l < p; l++) {
        for (uint32_t j = 0; j < 2; j++) {
            st32(h0 + 64, j);
            st32(h0 + 68, l);
            hprime(tmp, ID_ARGON2_BLOCK, h0, 72);
            blk_from_bytes(mem + (uint64_t) QW * (l * q + j), tmp);
        }
    }

    for (uint32_t pass = 0; pass < t; pass++) {
        for (uint32_t slice = 0; slice < 4; slice++) {
            for (uint32_t lane = 0; lane < p; lane++) {
                bool indep = (pass == 0 && slice < 2);
                uint32_t start = (pass == 0 && slice == 0) ? 2u : 0u;
                if (indep) {
                    id_mset(g_zero, 0, sizeof g_zero);
                    id_mset(g_in, 0, sizeof g_in);
                    g_in[0] = pass;
                    g_in[1] = lane;
                    g_in[2] = slice;
                    g_in[3] = mprime;
                    g_in[4] = t;
                    g_in[5] = 2u;
                    if (start == 2u) next_addresses();
                }
                uint32_t cur = lane * q + slice * sl + start;
                uint32_t prev = (cur % q == 0) ? cur + q - 1u : cur - 1u;
                for (uint32_t i = start; i < sl; i++, cur++, prev++) {
                    if (cur % q == 1u) prev = cur - 1u;
                    uint64_t pr;
                    if (indep) {
                        if (i % QW == 0) next_addresses();
                        pr = g_addr[i % QW];
                    } else {
                        pr = mem[(uint64_t) QW * prev];
                    }
                    uint32_t ref_lane = (uint32_t) (pr >> 32) % p;
                    if (pass == 0 && slice == 0) ref_lane = lane;
                    bool same = ref_lane == lane;
                    uint32_t area;
                    if (pass == 0) {
                        if (slice == 0)
                            area = i - 1u;
                        else if (same)
                            area = slice * sl + i - 1u;
                        else
                            area = slice * sl + (i == 0 ? 0xffffffffu : 0u);
                    } else {
                        if (same)
                            area = q - sl + i - 1u;
                        else
                            area = q - sl + (i == 0 ? 0xffffffffu : 0u);
                    }
                    uint64_t x = (uint32_t) pr;
                    x = (x * x) >> 32;
                    uint32_t rel = area - 1u - (uint32_t) (((uint64_t) area * x) >> 32);
                    uint32_t sp = 0;
                    if (pass != 0) sp = (slice == 3u) ? 0u : (slice + 1u) * sl;
                    uint32_t ai = (sp + rel) % q; /* both < q: no overflow */
                    fill_block(mem + (uint64_t) QW * prev,
                               mem + (uint64_t) QW * (ref_lane * q + ai), mem + (uint64_t) QW * cur,
                               pass != 0);
                }
            }
        }
    }

    uint64_t *c = g_tmp;
    /* g_tmp is free again here: fill_block is done. */
    for (uint32_t i = 0; i < QW; i++) c[i] = mem[(uint64_t) QW * (q - 1u) + i];
    for (uint32_t l = 1; l < p; l++)
        for (uint32_t i = 0; i < QW; i++) c[i] ^= mem[(uint64_t) QW * (l * q + q - 1u) + i];
    blk_to_bytes(tmp, c);
    hprime(out, out_len, tmp, ID_ARGON2_BLOCK);

    id_wipe(tmp, sizeof tmp);
    id_wipe(h0, sizeof h0);
    id_wipe(g_r, sizeof g_r);
    id_wipe(g_tmp, sizeof g_tmp);
    id_wipe(g_addr, sizeof g_addr);
    id_wipe(mem, (size_t) mprime * ID_ARGON2_BLOCK);
    return ID_OK;
}
