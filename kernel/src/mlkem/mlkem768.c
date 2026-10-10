/* mlkem768.c — Top-level ML-KEM-768 (FIPS 203 Algorithms 19-21)
 * See mlkem768.h for design notes.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "mlkem768.h"
#include "keccak.h"

/* dk layout: dk_pke(1152) || ek_pke(1184) || h(32) || z(32) = 2400 bytes */
#define DK_PKE_OFF   0
#define EK_PKE_OFF   KPE_DK_BYTES
#define H_OFF        (KPE_DK_BYTES + KPE_EK_BYTES)
#define Z_OFF        (KPE_DK_BYTES + KPE_EK_BYTES + 32)

void mlkem768_keygen(const uint8_t d[32], const uint8_t z[32],
                      uint8_t ek[MLKEM768_EK_BYTES],
                      uint8_t dk[MLKEM768_DK_BYTES]) {
    uint8_t dk_pke[KPE_DK_BYTES];
    kpe_keygen(d, ek, dk_pke); /* ek == ek_pke directly, sizes match */

    uint8_t h[32];
    sha3_256(ek, MLKEM768_EK_BYTES, h);

    for (int i = 0; i < KPE_DK_BYTES; i++) dk[DK_PKE_OFF + i] = dk_pke[i];
    for (int i = 0; i < KPE_EK_BYTES; i++) dk[EK_PKE_OFF + i] = ek[i];
    for (int i = 0; i < 32; i++) dk[H_OFF + i] = h[i];
    for (int i = 0; i < 32; i++) dk[Z_OFF + i] = z[i];
}

void mlkem768_encaps(const uint8_t ek[MLKEM768_EK_BYTES], const uint8_t m[32],
                      uint8_t c[MLKEM768_CT_BYTES], uint8_t ss[MLKEM768_SS_BYTES]) {
    uint8_t h_ek[32];
    sha3_256(ek, MLKEM768_EK_BYTES, h_ek);

    uint8_t g_input[64], g_out[64];
    for (int i = 0; i < 32; i++) g_input[i] = m[i];
    for (int i = 0; i < 32; i++) g_input[32 + i] = h_ek[i];
    sha3_512(g_input, 64, g_out);

    const uint8_t *K = g_out;      /* first 32 bytes: shared secret */
    const uint8_t *r = g_out + 32; /* last 32 bytes: encryption randomness */

    kpe_encrypt(ek, m, r, c);

    for (int i = 0; i < 32; i++) ss[i] = K[i];
}

/* Constant-time equality MASK: 0xFF when a == b over all len bytes, 0x00
 * otherwise. Always scans the full length (no early exit) and derives the
 * mask arithmetically from the OR-accumulated difference, so no comparison
 * result ever becomes a branch condition. The empty asm keeps the compiler
 * from proving the mask is 0/0xFF-valued and turning the select below back
 * into a branch (the same barrier the pq-crystals reference cmov uses). */
static uint8_t ct_eq_mask(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint32_t diff = 0;
    for (size_t i = 0; i < len; i++) diff |= (uint32_t) (a[i] ^ b[i]);
    /* diff in [0,255]: diff - 1 wraps to 0xFFFFFFFF only when diff == 0 */
    uint8_t mask = (uint8_t) ((diff - 1u) >> 8);
#if defined(__GNUC__) || defined(__clang__)
    __asm__("" : "+r"(mask));
#endif
    return mask;
}

void mlkem768_decaps(const uint8_t dk[MLKEM768_DK_BYTES], const uint8_t c[MLKEM768_CT_BYTES],
                      uint8_t ss[MLKEM768_SS_BYTES]) {
    const uint8_t *dk_pke = dk + DK_PKE_OFF;
    const uint8_t *ek_pke = dk + EK_PKE_OFF;
    const uint8_t *h = dk + H_OFF;
    const uint8_t *z = dk + Z_OFF;

    uint8_t m_prime[32];
    kpe_decrypt(dk_pke, c, m_prime);

    uint8_t g_input[64], g_out[64];
    for (int i = 0; i < 32; i++) g_input[i] = m_prime[i];
    for (int i = 0; i < 32; i++) g_input[32 + i] = h[i];
    sha3_512(g_input, 64, g_out);
    const uint8_t *k_prime = g_out;
    const uint8_t *r_prime = g_out + 32;

    /* Implicit rejection fallback: K_bar = SHAKE256(z || c), computed
     * unconditionally (not only on the failure path) so its cost
     * doesn't itself leak whether re-encryption will match. */
    uint8_t j_input[32 + MLKEM768_CT_BYTES];
    for (int i = 0; i < 32; i++) j_input[i] = z[i];
    for (int i = 0; i < MLKEM768_CT_BYTES; i++) j_input[32 + i] = c[i];
    uint8_t k_bar[32];
    shake256(j_input, sizeof(j_input), k_bar, 32);

    uint8_t c_prime[MLKEM768_CT_BYTES];
    kpe_encrypt(ek_pke, m_prime, r_prime, c_prime);

    /* Branch-free select (FIPS 203 implicit rejection must not reveal which
     * key was chosen): mask is 0xFF on a match, 0x00 otherwise, and ss is
     * K' & mask | K_bar & ~mask computed with XOR/AND only (cmov). */
    uint8_t mask = ct_eq_mask(c, c_prime, MLKEM768_CT_BYTES);
    for (int i = 0; i < 32; i++) {
        ss[i] = (uint8_t) (k_bar[i] ^ ((k_prime[i] ^ k_bar[i]) & mask));
    }
}
