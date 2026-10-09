/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* pq_mlkem1024.c — ML-KEM-1024 (FIPS 203, category 5) for pq_matrix.
 *
 * One translation unit over the pq-crystals kyber reference vendored in
 * mlkem1024/ (CC0 / Apache-2.0, see mlkem1024/LICENSE and README.zxv),
 * built with KYBER_K = 4. Building it here, rather than file by file,
 * pins the parameter set to this unit so no Makefile flag can build a
 * different one by accident. The reference already namespaces its
 * symbols (pqcrystals_kyber1024_ref_*, pqcrystals_kyber_fips202_ref_*),
 * so it links next to the in-house ML-KEM-768 and the dilithium build.
 *
 * Only the _derand entry points are reachable from the API: the caller
 * passes d, z and m. Checked against NIST ACVP in test_pq_matrix.c.
 */
#define KYBER_K 4

/* The reference's non-derand entry points draw from randombytes(). They
 * are compiled (they share kem.c) but never called; the stub below makes
 * any accidental call produce an obviously unusable all-zero draw rather
 * than reading uninitialised memory. */
#define randombytes zxv_mlkem1024_randombytes_unused

#include "mlkem1024/cbd.c"
#include "mlkem1024/fips202.c"
#include "mlkem1024/indcpa.c"
#include "mlkem1024/kem.c"
#include "mlkem1024/ntt.c"
#include "mlkem1024/poly.c"
#include "mlkem1024/polyvec.c"
#include "mlkem1024/reduce.c"
#include "mlkem1024/symmetric-shake.c"
#include "mlkem1024/verify.c"

#include "pq_matrix_algs.h"

#if KYBER_PUBLICKEYBYTES != PQM_MLKEM1024_EK_BYTES ||                                              \
    KYBER_SECRETKEYBYTES != PQM_MLKEM1024_DK_BYTES ||                                              \
    KYBER_CIPHERTEXTBYTES != PQM_MLKEM1024_CT_BYTES || KYBER_SSBYTES != PQM_SS_BYTES
#    error "vendored kyber parameters do not match ML-KEM-1024"
#endif

void randombytes(uint8_t *out, size_t outlen)
{
    for (size_t i = 0; i < outlen; i++) out[i] = 0;
}

void pqm_mlkem1024_keygen(const uint8_t d[32], const uint8_t z[32],
                          uint8_t ek[PQM_MLKEM1024_EK_BYTES], uint8_t dk[PQM_MLKEM1024_DK_BYTES])
{
    uint8_t coins[2 * KYBER_SYMBYTES];
    for (unsigned i = 0; i < KYBER_SYMBYTES; i++) {
        coins[i] = d[i];
        coins[KYBER_SYMBYTES + i] = z[i];
    }
    crypto_kem_keypair_derand(ek, dk, coins);
    volatile uint8_t *w = coins;
    for (unsigned i = 0; i < sizeof coins; i++) w[i] = 0;
}

/* FIPS 203 section 7.2: every 12-bit coefficient of the encoded vector t
 * must already be reduced mod q, i.e. ByteEncode12(ByteDecode12(t)) == t. */
bool pqm_mlkem1024_check_ek(const uint8_t ek[PQM_MLKEM1024_EK_BYTES])
{
    unsigned bad = 0;
    for (unsigned i = 0; i < KYBER_POLYVECBYTES; i += 3) {
        unsigned a = ek[i] | ((unsigned) (ek[i + 1] & 0x0f) << 8);
        unsigned b = (ek[i + 1] >> 4) | ((unsigned) ek[i + 2] << 4);
        bad |= (unsigned) (a >= KYBER_Q) | (unsigned) (b >= KYBER_Q);
    }
    return bad == 0;
}

/* FIPS 203 section 7.3: the H(ek) stored in dk must match the ek stored
 * next to it. (dk is the decapsulator's own key; this is a storage check.) */
bool pqm_mlkem1024_check_dk(const uint8_t dk[PQM_MLKEM1024_DK_BYTES])
{
    uint8_t h[KYBER_SYMBYTES];
    hash_h(h, dk + KYBER_INDCPA_SECRETKEYBYTES, KYBER_PUBLICKEYBYTES);
    return verify(h, dk + KYBER_SECRETKEYBYTES - 2 * KYBER_SYMBYTES, KYBER_SYMBYTES) == 0;
}

bool pqm_mlkem1024_encaps(const uint8_t ek[PQM_MLKEM1024_EK_BYTES], const uint8_t m[32],
                          uint8_t ct[PQM_MLKEM1024_CT_BYTES], uint8_t ss[PQM_SS_BYTES])
{
    if (!pqm_mlkem1024_check_ek(ek)) {
        for (unsigned i = 0; i < PQM_MLKEM1024_CT_BYTES; i++) ct[i] = 0;
        for (unsigned i = 0; i < PQM_SS_BYTES; i++) ss[i] = 0;
        return false;
    }
    crypto_kem_enc_derand(ct, ss, ek, m);
    return true;
}

void pqm_mlkem1024_decaps(const uint8_t dk[PQM_MLKEM1024_DK_BYTES],
                          const uint8_t ct[PQM_MLKEM1024_CT_BYTES], uint8_t ss[PQM_SS_BYTES])
{
    crypto_kem_dec(ss, ct, dk);
}
