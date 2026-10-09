/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* pq_hqc5.c — HQC-5 (code-based KEM, category 5) for pq_matrix.
 *
 * HQC is the second KEM NIST selected (March 2025), and the one whose
 * hardness rests on a different problem from ML-KEM: decoding random
 * quasi-cyclic codes in the Hamming metric (QCSD), not module lattices.
 * A break of lattice cryptography does not carry over to it.
 *
 * One translation unit over the official HQC reference implementation,
 * release v5.0.0 (2025-08-22), vendored in hqc5/ (public domain, see
 * hqc5/LICENSE and hqc5/README.zxv). The reference uses short global
 * names (sha3_256, shake256, fft, encode, crypto_kem_*) that collide with
 * the kernel's own Keccak and with other vendored code, so every external
 * symbol of this unit is renamed to zxv_hqc5_* below; nm on the object
 * shows only that prefix and the pqm_hqc5_* API.
 *
 * Randomness: the reference draws seed_kem (KeyGen) and m, salt (Encaps)
 * from a global SHAKE-256 PRNG. Here those draws are redirected to the
 * caller's buffers, so the library never generates randomness and the
 * official KATs are reproduced exactly (test_pq_matrix.c expands each KAT
 * seed through the same SHAKE-256 PRNG to obtain those buffers).
 *
 * libc: the reference needs memcpy/memset/memcmp (<string.h>). Its debug
 * printers (vect_print, VERBOSE dumps) are compiled out below, so no
 * stdio symbol is linked, but <stdio.h> must exist at compile time.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Debug printing in the reference is dead code here; keep it unlinked. */
#define printf(...) ((void) 0)

/* Rename every external symbol of the vendored HQC unit. */
#define code_decode                   zxv_hqc5_code_decode
#define code_encode                   zxv_hqc5_code_encode
#define compute_generator_poly        zxv_hqc5_compute_generator_poly
#define crypto_kem_dec                zxv_hqc5_crypto_kem_dec
#define crypto_kem_enc                zxv_hqc5_crypto_kem_enc
#define crypto_kem_keypair            zxv_hqc5_crypto_kem_keypair
#define encode                        zxv_hqc5_encode
#define expand_and_sum                zxv_hqc5_expand_and_sum
#define fft                           zxv_hqc5_fft
#define fft_retrieve_error_poly       zxv_hqc5_fft_retrieve_error_poly
#define find_peaks                    zxv_hqc5_find_peaks
#define gf_carryless_mul              zxv_hqc5_gf_carryless_mul
#define gf_generate                   zxv_hqc5_gf_generate
#define gf_inverse                    zxv_hqc5_gf_inverse
#define gf_mul                        zxv_hqc5_gf_mul
#define gf_square                     zxv_hqc5_gf_square
#define hadamard                      zxv_hqc5_hadamard
#define hash_g                        zxv_hqc5_hash_g
#define hash_h                        zxv_hqc5_hash_h
#define hash_i                        zxv_hqc5_hash_i
#define hash_j                        zxv_hqc5_hash_j
#define hqc_c_kem_from_string         zxv_hqc5_hqc_c_kem_from_string
#define hqc_c_kem_to_string           zxv_hqc5_hqc_c_kem_to_string
#define hqc_dk_pke_from_string        zxv_hqc5_hqc_dk_pke_from_string
#define hqc_ek_pke_from_string        zxv_hqc5_hqc_ek_pke_from_string
#define hqc_pke_decrypt               zxv_hqc5_hqc_pke_decrypt
#define hqc_pke_encrypt               zxv_hqc5_hqc_pke_encrypt
#define hqc_pke_keygen                zxv_hqc5_hqc_pke_keygen
#define memset_volatile               zxv_hqc5_memset_volatile
#define prng_init                     zxv_hqc5_prng_init
#define reed_muller_decode            zxv_hqc5_reed_muller_decode
#define reed_muller_encode            zxv_hqc5_reed_muller_encode
#define reed_solomon_decode           zxv_hqc5_reed_solomon_decode
#define reed_solomon_encode           zxv_hqc5_reed_solomon_encode
#define sha3_256                      zxv_hqc5_sha3_256
#define sha3_256_inc_absorb           zxv_hqc5_sha3_256_inc_absorb
#define sha3_256_inc_finalize         zxv_hqc5_sha3_256_inc_finalize
#define sha3_256_inc_init             zxv_hqc5_sha3_256_inc_init
#define sha3_384                      zxv_hqc5_sha3_384
#define sha3_384_inc_absorb           zxv_hqc5_sha3_384_inc_absorb
#define sha3_384_inc_finalize         zxv_hqc5_sha3_384_inc_finalize
#define sha3_384_inc_init             zxv_hqc5_sha3_384_inc_init
#define sha3_512                      zxv_hqc5_sha3_512
#define sha3_512_inc_absorb           zxv_hqc5_sha3_512_inc_absorb
#define sha3_512_inc_finalize         zxv_hqc5_sha3_512_inc_finalize
#define sha3_512_inc_init             zxv_hqc5_sha3_512_inc_init
#define shake128                      zxv_hqc5_shake128
#define shake128_absorb               zxv_hqc5_shake128_absorb
#define shake128_inc_absorb           zxv_hqc5_shake128_inc_absorb
#define shake128_inc_finalize         zxv_hqc5_shake128_inc_finalize
#define shake128_inc_init             zxv_hqc5_shake128_inc_init
#define shake128_inc_squeeze          zxv_hqc5_shake128_inc_squeeze
#define shake128_squeezeblocks        zxv_hqc5_shake128_squeezeblocks
#define shake256                      zxv_hqc5_shake256
#define shake256_absorb               zxv_hqc5_shake256_absorb
#define shake256_inc_absorb           zxv_hqc5_shake256_inc_absorb
#define shake256_inc_finalize         zxv_hqc5_shake256_inc_finalize
#define shake256_inc_init             zxv_hqc5_shake256_inc_init
#define shake256_inc_squeeze          zxv_hqc5_shake256_inc_squeeze
#define shake256_prng_ctx             zxv_hqc5_shake256_prng_ctx
#define shake256_squeezeblocks        zxv_hqc5_shake256_squeezeblocks
#define vect_add                      zxv_hqc5_vect_add
#define vect_compare                  zxv_hqc5_vect_compare
#define vect_generate_random_support1 zxv_hqc5_vect_generate_random_support1
#define vect_generate_random_support2 zxv_hqc5_vect_generate_random_support2
#define vect_mul                      zxv_hqc5_vect_mul
#define vect_print                    zxv_hqc5_vect_print
#define vect_sample_fixed_weight1     zxv_hqc5_vect_sample_fixed_weight1
#define vect_sample_fixed_weight2     zxv_hqc5_vect_sample_fixed_weight2
#define vect_set_random               zxv_hqc5_vect_set_random
#define vect_truncate                 zxv_hqc5_vect_truncate
#define vect_write_support_to_vector  zxv_hqc5_vect_write_support_to_vector
#define xof_get_bytes                 zxv_hqc5_xof_get_bytes
#define xof_init                      zxv_hqc5_xof_init
/* The reference PRNG is compiled (symmetric.c defines it) under a private
 * name and never used; kem.c's draws are redirected further down. */
#define prng_get_bytes zxv_hqc5_prng_get_bytes_unused

#include "hqc5/fips202.c"
#include "hqc5/symmetric.c"
#include "hqc5/crypto_memset.c"
#include "hqc5/gf.c"
#include "hqc5/gf2x.c"
#include "hqc5/fft.c"
#include "hqc5/reed_solomon.c"
#include "hqc5/reed_muller.c"
#include "hqc5/code.c"
#include "hqc5/vector.c"
#include "hqc5/parsing.c"
#include "hqc5/hqc.c"

/* kem.c's PRNG draws come from the caller's buffer through this slot, in
 * the order the reference draws them. Single-threaded by design. */
static const uint8_t *g_hqc_draw;
static size_t g_hqc_draw_left;

static void zxv_hqc5_draw(uint8_t *out, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        out[i] = (g_hqc_draw && g_hqc_draw_left) ? *g_hqc_draw++ : 0;
        if (g_hqc_draw_left) g_hqc_draw_left--;
    }
}
#undef prng_get_bytes
#define prng_get_bytes zxv_hqc5_draw

#include "hqc5/kem.c"

#include "pq_matrix_algs.h"

#if CRYPTO_PUBLICKEYBYTES != PQM_HQC5_PK_BYTES || CRYPTO_SECRETKEYBYTES != PQM_HQC5_SK_BYTES ||    \
    CRYPTO_CIPHERTEXTBYTES != PQM_HQC5_CT_BYTES || CRYPTO_BYTES != PQM_SS_BYTES ||                 \
    SALT_BYTES != PQM_HQC5_SALT_BYTES || SEED_BYTES != 32 || PARAM_SECURITY_BYTES != 32
#    error "vendored HQC parameters do not match HQC-5"
#endif

void pqm_hqc5_keygen(const uint8_t seed_kem[32], uint8_t pk[PQM_HQC5_PK_BYTES],
                     uint8_t sk[PQM_HQC5_SK_BYTES])
{
    g_hqc_draw = seed_kem;
    g_hqc_draw_left = SEED_BYTES;
    crypto_kem_keypair(pk, sk);
    g_hqc_draw = 0;
    g_hqc_draw_left = 0;
}

void pqm_hqc5_encaps(const uint8_t pk[PQM_HQC5_PK_BYTES], const uint8_t m[32],
                     const uint8_t salt[PQM_HQC5_SALT_BYTES], uint8_t ct[PQM_HQC5_CT_BYTES],
                     uint8_t ss[PQM_SS_BYTES])
{
    uint8_t coins[PARAM_SECURITY_BYTES + SALT_BYTES];
    for (unsigned i = 0; i < PARAM_SECURITY_BYTES; i++) coins[i] = m[i];
    for (unsigned i = 0; i < SALT_BYTES; i++) coins[PARAM_SECURITY_BYTES + i] = salt[i];
    g_hqc_draw = coins;
    g_hqc_draw_left = sizeof coins;
    crypto_kem_enc(ct, ss, pk);
    g_hqc_draw = 0;
    g_hqc_draw_left = 0;
    memset_zero(coins, sizeof coins);
}

void pqm_hqc5_decaps(const uint8_t sk[PQM_HQC5_SK_BYTES], const uint8_t ct[PQM_HQC5_CT_BYTES],
                     uint8_t ss[PQM_SS_BYTES])
{
    crypto_kem_dec(ss, ct, sk);
}
