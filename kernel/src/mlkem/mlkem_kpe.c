/* mlkem_kpe.c — ML-KEM-768 K-PKE layer (FIPS 203 Algorithms 13-15)
 * See mlkem_kpe.h for design notes.
 *
 * Structural reference: Go standard library crypto/internal/fips140/
 * mlkem/mlkem768.go (a production, FIPS-140-target implementation),
 * cross-checked against FIPS 203 itself for the algorithm definitions.
 * Matrix seed order (rho, j, i) and the transpose access pattern in
 * Encrypt exactly mirror that reference to preserve KAT compatibility.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "mlkem_kpe.h"
#include "mlkem_sample.h"
#include "keccak.h"

/* G = SHA3-512, domain-separated by appending the module rank k as a
 * single byte (FIPS 203's convention to prevent cross-parameter-set
 * confusion). Output split into rho (first 32 bytes) and sigma. */
static void g_hash(const uint8_t d[32], uint8_t rho[32], uint8_t sigma[32]) {
    uint8_t input[33];
    for (int i = 0; i < 32; i++) input[i] = d[i];
    input[32] = KPE_K;
    uint8_t out[64];
    sha3_512(input, 33, out);
    for (int i = 0; i < 32; i++) { rho[i] = out[i]; sigma[i] = out[32 + i]; }
}

/* PRF_eta(s, n) = SHAKE256(s || n, 64*eta bytes). ML-KEM-768 uses
 * eta1=eta2=2 uniformly, so this always produces 128 bytes. */
static void prf_eta2(const uint8_t s[32], uint8_t n, uint8_t out[128]) {
    uint8_t input[33];
    for (int i = 0; i < 32; i++) input[i] = s[i];
    input[32] = n;
    shake256(input, 33, out, 128);
}

static void vec_ntt(poly_vec_t *v) {
    for (int i = 0; i < KPE_K; i++) poly_ntt(&v->v[i]);
}

/* Dot product of two NTT-domain vectors, result in NTT domain. */
static void vec_dot_ntt(const poly_vec_t *a, const poly_vec_t *b, poly_t *out) {
    poly_t acc = {0}, tmp;
    for (int i = 0; i < KPE_K; i++) {
        poly_basemul(&a->v[i], &b->v[i], &tmp);
        poly_add(&acc, &tmp, &acc);
    }
    *out = acc;
}

void kpe_keygen(const uint8_t d[32], uint8_t ek[KPE_EK_BYTES], uint8_t dk[KPE_DK_BYTES]) {
    uint8_t rho[32], sigma[32];
    g_hash(d, rho, sigma);

    /* A_hat[i][j] via seed (rho, j, i) -- matches reference convention */
    poly_t a_hat[KPE_K][KPE_K];
    for (int i = 0; i < KPE_K; i++) {
        for (int j = 0; j < KPE_K; j++) {
            sample_ntt(rho, (uint8_t)j, (uint8_t)i, &a_hat[i][j]);
        }
    }

    uint8_t n = 0;
    poly_vec_t s, e;
    for (int i = 0; i < KPE_K; i++) {
        uint8_t buf[128];
        prf_eta2(sigma, n++, buf);
        poly_cbd_eta2(buf, &s.v[i]);
    }
    for (int i = 0; i < KPE_K; i++) {
        uint8_t buf[128];
        prf_eta2(sigma, n++, buf);
        poly_cbd_eta2(buf, &e.v[i]);
    }

    vec_ntt(&s);
    vec_ntt(&e);

    /* t_hat[i] = e_hat[i] + sum_j A_hat[i][j] * s_hat[j] */
    poly_vec_t t;
    for (int i = 0; i < KPE_K; i++) {
        poly_t acc = e.v[i], tmp;
        for (int j = 0; j < KPE_K; j++) {
            poly_basemul(&a_hat[i][j], &s.v[j], &tmp);
            poly_add(&acc, &tmp, &acc);
        }
        t.v[i] = acc;
    }

    for (int i = 0; i < KPE_K; i++) byte_encode(&t.v[i], 12, ek + i * 384);
    for (int i = 0; i < 32; i++) ek[384 * KPE_K + i] = rho[i];

    for (int i = 0; i < KPE_K; i++) byte_encode(&s.v[i], 12, dk + i * 384);
}

void kpe_encrypt(const uint8_t ek[KPE_EK_BYTES], const uint8_t m[32],
                  const uint8_t rand[32], uint8_t c[KPE_CT_BYTES]) {
    poly_vec_t t_hat;
    for (int i = 0; i < KPE_K; i++) byte_decode(ek + i * 384, 12, &t_hat.v[i]);
    const uint8_t *rho = ek + 384 * KPE_K;

    poly_t a_hat[KPE_K][KPE_K];
    for (int i = 0; i < KPE_K; i++) {
        for (int j = 0; j < KPE_K; j++) {
            sample_ntt(rho, (uint8_t)j, (uint8_t)i, &a_hat[i][j]);
        }
    }

    uint8_t n = 0;
    poly_vec_t r, e1;
    poly_t e2;
    for (int i = 0; i < KPE_K; i++) {
        uint8_t buf[128];
        prf_eta2(rand, n++, buf);
        poly_cbd_eta2(buf, &r.v[i]);
    }
    for (int i = 0; i < KPE_K; i++) {
        uint8_t buf[128];
        prf_eta2(rand, n++, buf);
        poly_cbd_eta2(buf, &e1.v[i]);
    }
    {
        uint8_t buf[128];
        prf_eta2(rand, n++, buf);
        poly_cbd_eta2(buf, &e2);
    }

    poly_vec_t r_hat = r;
    vec_ntt(&r_hat);

    /* u[i] = InvNTT( sum_j A_hat[j][i] * r_hat[j] ) + e1[i]  -- note the
     * TRANSPOSED access (A_hat[j][i], not [i][j]) matching the
     * reference's `a[j*k+i]` pattern for encryption. */
    poly_vec_t u;
    for (int i = 0; i < KPE_K; i++) {
        poly_t acc = {0}, tmp;
        for (int j = 0; j < KPE_K; j++) {
            poly_basemul(&a_hat[j][i], &r_hat.v[j], &tmp);
            poly_add(&acc, &tmp, &acc);
        }
        poly_invntt(&acc);
        poly_add(&acc, &e1.v[i], &u.v[i]);
    }

    /* mu = Decompress_1(m) : decode the 32-byte (256-bit) message into
     * a polynomial with coefficients in {0, round(Q/2)}. */
    poly_t mu;
    {
        poly_t m_bits;
        byte_decode(m, 1, &m_bits); /* 32 bytes = 256 bits, d=1 */
        for (int i = 0; i < MLKEM_N; i++) {
            mu.coeffs[i] = scalar_decompress((uint16_t)m_bits.coeffs[i], 1);
        }
    }

    /* v = InvNTT( t_hat . r_hat ) + e2 + mu */
    poly_t v_ntt, v;
    vec_dot_ntt(&t_hat, &r_hat, &v_ntt);
    poly_invntt(&v_ntt);
    poly_add(&v_ntt, &e2, &v);
    poly_add(&v, &mu, &v);

    for (int i = 0; i < KPE_K; i++) {
        poly_compress(&u.v[i], MLKEM768_DU, c + i * (MLKEM768_DU * MLKEM_N / 8));
    }
    poly_compress(&v, MLKEM768_DV, c + KPE_C1_BYTES);
}

void kpe_decrypt(const uint8_t dk[KPE_DK_BYTES], const uint8_t c[KPE_CT_BYTES],
                  uint8_t m[32]) {
    poly_vec_t u;
    for (int i = 0; i < KPE_K; i++) {
        poly_decompress(c + i * (MLKEM768_DU * MLKEM_N / 8), MLKEM768_DU, &u.v[i]);
    }
    poly_t v;
    poly_decompress(c + KPE_C1_BYTES, MLKEM768_DV, &v);

    poly_vec_t s_hat;
    for (int i = 0; i < KPE_K; i++) byte_decode(dk + i * 384, 12, &s_hat.v[i]);

    poly_vec_t u_hat = u;
    vec_ntt(&u_hat);

    poly_t mask_ntt, mask;
    vec_dot_ntt(&s_hat, &u_hat, &mask_ntt);
    mask = mask_ntt;
    poly_invntt(&mask);

    poly_t w;
    poly_sub(&v, &mask, &w);

    poly_t m_bits;
    for (int i = 0; i < MLKEM_N; i++) {
        m_bits.coeffs[i] = (int16_t)scalar_compress(w.coeffs[i], 1);
    }
    byte_encode(&m_bits, 1, m);
}
