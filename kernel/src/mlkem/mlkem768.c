/* mlkem768.c — Top-level ML-KEM-768 (FIPS 203 Algorithms 19-21)
 * See mlkem768.h for design notes.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * License: SEL-3.3 (kernel component)
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

/* Constant-time-ish comparison: always scans the full length so the
 * number of matching bytes doesn't leak through early exit, avoiding
 * a trivial timing side-channel on the re-encryption check below. */
static int ct_equal(const uint8_t *a, const uint8_t *b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) diff = (uint8_t)(diff | (a[i] ^ b[i]));
    return diff == 0;
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

    int match = ct_equal(c, c_prime, MLKEM768_CT_BYTES);
    for (int i = 0; i < 32; i++) {
        ss[i] = match ? k_prime[i] : k_bar[i];
    }
}
