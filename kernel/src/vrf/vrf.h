/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vrf.h — ECVRF-EDWARDS25519-SHA512-TAI, RFC 9381 ciphersuite 0x03: a
 * verifiable random function. The key holder computes, for any input alpha,
 * a 64-byte output beta and an 80-byte proof pi; anyone holding the public
 * key checks pi and gets the same beta. For a given (public key, alpha) there
 * is exactly one beta that verifies, so the key holder cannot choose or grind
 * the output, only publish it or withhold it.
 *
 * BUILDING BLOCKS. The edwards25519 field, group and scalar arithmetic and
 * SHA-512 are the vendored public-domain orlp/ed25519 (third_party/ed25519),
 * #included into vrf.c with every global symbol renamed to a vrf_ prefix, so
 * vrf.c links next to robin_debanks/ed25519_verify.c (which includes the same
 * files under their own names) without a duplicate symbol.
 *
 *   V1 SUITE. suite_string = 0x03; edwards25519 with cofactor 8; Hash =
 *      SHA-512 (hLen 64); ptLen = qLen = 32; cLen = 16; integers are
 *      little-endian; encode_to_curve_salt = PK_string (RFC 9381 5.5).
 *   V2 KEYS (RFC 8032 5.1.5). vrf_keypair(seed[32]) computes
 *      h = SHA-512(seed), x = h[0..31] with bits 0, 1, 2 and 255 cleared and
 *      bit 254 set, Y = x*B, pk = point_to_string(Y), sk = seed || pk (the
 *      libsodium layout). `seed` is the RFC's SK and must be 32 secret
 *      uniformly random bytes.
 *   V3 POINT DECODING (RFC 8032 5.1.3, string_to_point). A 32-byte string
 *      decodes when its low 255 bits y are < p = 2^255 - 19, the curve
 *      equation has a root x, and NOT (x == 0 with the sign bit set).
 *      Anything else is INVALID: the non-canonical encodings y + p (y < 19)
 *      and "-0" are refused, so every valid point has exactly one encoding.
 *   V4 ENCODE TO CURVE (5.4.1.1, try-and-increment). For ctr = 0, 1, ...:
 *      hash = SHA-512(0x03 || 0x01 || pk || alpha || ctr || 0x00), P =
 *      string_to_point(hash[0..31]) (V3), H = 8*P; the first ctr where P is
 *      valid and H is not the identity gives H. ctr is one octet: a run
 *      reaching 256 (probability about 2^-256) is reported as failure
 *      (vrf_encode_to_curve false, vrf_prove false, vrf_verify false).
 *   V5 NONCE (5.4.2.2, from RFC 8032). k = SHA-512(h[32..63] || h_string)
 *      mod q, h = SHA-512(seed), h_string = point_to_string(H).
 *   V6 CHALLENGE (5.4.3). c = the first 16 bytes of SHA-512(0x03 || 0x02 ||
 *      P1 || P2 || P3 || P4 || P5 || 0x00) (points encoded, V1).
 *   V7 PROVE (5.1). Gamma = x*H, k (V5), c = challenge(Y, H, Gamma, k*B,
 *      k*H), s = (k + c*x) mod q, pi = Gamma || c (16 bytes) || s (32
 *      bytes) = 80 bytes. vrf_prove re-derives pk from the seed and refuses
 *      (false, pi zeroed) when sk[32..63] is not that pk: a wrong half of sk
 *      would otherwise yield a proof under a key nobody holds.
 *   V8 DECODE PROOF (5.4.4). Gamma = string_to_point(pi[0..31]) (V3),
 *      c = pi[32..47], s = pi[48..79]; INVALID when Gamma is invalid or
 *      s >= q (q = 2^252 + 27742317777372353535851937790883648493). Gamma is
 *      NOT required to lie in the prime-order subgroup (the RFC does not
 *      require it; beta multiplies it by the cofactor).
 *   V9 PROOF TO HASH (5.2). beta = SHA-512(0x03 || 0x03 ||
 *      point_to_string(8*Gamma) || 0x00), 64 bytes. vrf_proof_to_hash
 *      decodes pi (V8) but does NOT check it: run it only on a proof you
 *      produced or one vrf_verify accepted.
 *   V10 VERIFY (5.3, validate_key = TRUE, the only mode offered). Y =
 *      string_to_point(pk) (V3); ECVRF_validate_key: 8*Y must not be the
 *      identity, so the eight small-order points (and their encodings) are
 *      refused; decode pi (V8); H (V4); U = s*B - c*Y; V = s*H - c*Gamma;
 *      valid exactly when challenge(Y, H, Gamma, U, V) == c, and then beta is
 *      V9. On any refusal beta is zeroed and the result is false.
 *   V11 CONSTANT TIME. Every scalar multiplication by a secret (x*B, k*B with
 *      the vendored comb ge_scalarmult_base; x*H and k*H with this module's
 *      own variable-base routine) runs in time independent of the scalar:
 *      a signed 4-bit fixed window (64 digits in [-8, 8]) over a table of
 *      1H..8H, each digit's table entry chosen by scanning all eight entries
 *      with fe_cmov and negated with a conditional move, and the group law
 *      is the complete (unified) extended-coordinate addition, so the
 *      identity and doubling cases take no branch. The pk check in V7
 *      compares in constant time; secrets on the stack are wiped. Verify
 *      handles public data only and uses vartime code (the vendored
 *      ge_double_scalarmult_vartime for U). try-and-increment's running time
 *      depends on alpha (the RFC's own caveat): alpha is public here.
 *   V12 ADAPTERS.
 *      vrf_dsp_verify is a dispute.h dsp_vrf_fn: alpha = the 32-byte dispute
 *      seed, proof = pi (proof_len must be VRF_PROOF_LEN), out = the FIRST
 *      32 bytes of beta. ctx points at a vrf_dsp_ctx_t (the VRF public key);
 *      since dsp_t has one ctx for all its hooks, a caller that needs a
 *      context of its own for verify/pred makes vrf_dsp_ctx_t the FIRST
 *      member of its context struct (a pointer to a struct is a pointer to
 *      its first member). vrf_dsp_out computes (pi, out) for the prover.
 *      vrf_beacon verifies (pk, alpha, pi) and returns beta[0..31] as a
 *      32-byte secret: for the Crown allotment (allot.h R2/R3) the beacon
 *      key holder is one more commit-reveal party. alpha must be fixed
 *      BEFORE the epoch opens (vrf_beacon_alpha: "ZXV-ALLOT-BEACON" ||
 *      epoch LE64), so the holder can commit
 *          allot_commitment(epoch, beacon_party, out)
 *      while OPEN; after close it publishes pi, anyone runs vrf_beacon and
 *      feeds `out` to allot_reveal(r, beacon_party, out). Because beta is
 *      unique for (pk, alpha), the beacon party's secret is publicly
 *      checkable and cannot be ground; like any party it can still withhold
 *      (R3).
 *
 *   V13 STABLE RESULTS. Every entry point that can refuse returns bool, and
 *      that bool is its whole status: true = VALID / done, false = INVALID /
 *      refused, with the output buffer zeroed. The signatures and this
 *      convention are FIXED (callers such as allotment call vrf_verify
 *      directly); no other status code exists or will be added, and why a
 *      proof was refused is deliberately not reported. Any alpha length is
 *      accepted (allotment passes a 32-byte header hash).
 *
 * HONEST LIMITS. A VRF is only as unpredictable as its key is secret: the
 * key holder learns every output in advance and can withhold one it dislikes
 * (bond it, or combine it with other parties as allotment R2 does). The
 * constant-time claim is about the C source (no secret-dependent branch or
 * index); it is not verified on the generated machine code of every
 * compiler, and the field code is the vendored ref10 code, trusted as is.
 * try-and-increment leaks the number of attempts, i.e. information about
 * alpha (public in every use here). Only validate_key = TRUE is offered.
 * The test checks the three RFC 9381 B.3 vectors byte for byte and
 * self-consistency; it is not a formal proof and no external audit is
 * claimed. Freestanding C11: no allocation, no floating point, no libc.
 */
#ifndef ZXV_VRF_H
#define ZXV_VRF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VRF_SUITE        0x03u
#define VRF_SEED_LEN     32u
#define VRF_PK_LEN       32u
#define VRF_SK_LEN       64u /* seed || pk */
#define VRF_PROOF_LEN    80u /* Gamma (32) || c (16) || s (32) */
#define VRF_OUT_LEN      64u /* beta */
#define VRF_C_LEN        16u
#define VRF_PT_LEN       32u /* ptLen = qLen = fLen */
#define VRF_HASH_LEN     64u /* hLen, SHA-512 */
#define VRF_BEACON_TAG   16u /* "ZXV-ALLOT-BEACON" */
#define VRF_BEACON_ALPHA 24u /* tag || epoch LE64 */

/* V2 */
void vrf_keypair(const uint8_t seed[VRF_SEED_LEN], uint8_t pk[VRF_PK_LEN], uint8_t sk[VRF_SK_LEN]);
/* V7: false (pi zeroed) on a NULL argument, alpha == NULL with alpha_len > 0,
 * an sk whose pk half does not match its seed, or a V4 failure. */
bool vrf_prove(const uint8_t sk[VRF_SK_LEN], const uint8_t *alpha, size_t alpha_len,
               uint8_t pi[VRF_PROOF_LEN]);
/* V10: true and beta when pi proves alpha under pk; false and beta zeroed
 * otherwise. */
bool vrf_verify(const uint8_t pk[VRF_PK_LEN], const uint8_t *alpha, size_t alpha_len,
                const uint8_t pi[VRF_PROOF_LEN], uint8_t beta[VRF_OUT_LEN]);
/* V9: false (beta zeroed) when pi does not decode (V8). Does not verify. */
bool vrf_proof_to_hash(const uint8_t pi[VRF_PROOF_LEN], uint8_t beta[VRF_OUT_LEN]);
/* V4: H for (pk, alpha), encoded; false when pk is NULL, alpha NULL with
 * alpha_len > 0, or ctr would pass 255. pk is used only as the salt. */
bool vrf_encode_to_curve(const uint8_t pk[VRF_PK_LEN], const uint8_t *alpha, size_t alpha_len,
                         uint8_t h[32]);
/* V3: true when s is a canonical encoding of a curve point. */
bool vrf_point_valid(const uint8_t s[32]);

/* V12: dispute.h dsp_vrf_fn. */
typedef struct {
    uint8_t pk[VRF_PK_LEN];
} vrf_dsp_ctx_t;
bool vrf_dsp_verify(void *ctx, const uint8_t seed[32], const uint8_t *proof, uint32_t proof_len,
                    const uint8_t out[32]);
/* The prover's side of vrf_dsp_verify: pi and out = beta[0..31] for seed. */
bool vrf_dsp_out(const uint8_t sk[VRF_SK_LEN], const uint8_t seed[32], uint8_t pi[VRF_PROOF_LEN],
                 uint8_t out[32]);

/* V12: allotment beacon party. */
void vrf_beacon_alpha(uint64_t epoch, uint8_t alpha[VRF_BEACON_ALPHA]);
bool vrf_beacon(const uint8_t pk[VRF_PK_LEN], const uint8_t *alpha, size_t alpha_len,
                const uint8_t pi[VRF_PROOF_LEN], uint8_t out[32]);

#ifdef TEST_HOST
/* Test only: the RFC 9381 B.3 intermediates (H, k, U = k*B, V = k*H) of a
 * proof. Exposes the nonce k: never in a kernel build. */
typedef struct {
    uint8_t h[32], k[32], u[32], v[32];
} vrf_trace_t;
bool vrf_prove_trace(const uint8_t sk[VRF_SK_LEN], const uint8_t *alpha, size_t alpha_len,
                     uint8_t pi[VRF_PROOF_LEN], vrf_trace_t *trace);
#endif

#endif /* ZXV_VRF_H */
