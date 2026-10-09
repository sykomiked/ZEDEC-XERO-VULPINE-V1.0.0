/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* pq_matrix.h — the post-quantum security matrix: layered hybrid KEM and
 * dual signatures at three policy levels.
 *
 * WHAT IT IS
 * ----------
 * Key exchange is a hybrid KEM. Each layer is an independent KEM, and the
 * shared secret is a hash over ALL of their secrets AND ciphertexts, so an
 * attacker has to break every layer, not just one:
 *
 *   level      lattice (FIPS 203)   code-based    classical   signatures
 *   ---------  -------------------  ------------  ----------  -----------------------------
 *   STANDARD   ML-KEM-768           -             X25519      ML-DSA-65
 *   HIGH       ML-KEM-1024          -             X25519      ML-DSA-87
 *   MATRIX     ML-KEM-1024          HQC-5         X25519      ML-DSA-87 + SLH-DSA-SHAKE-256s
 *
 * MATRIX is the default for economy keys (PQM_LEVEL_ECONOMY_DEFAULT). Its
 * three KEMs rest on three unrelated problems: module lattices (MLWE),
 * quasi-cyclic syndrome decoding in the Hamming metric (HQC), and the
 * elliptic-curve discrete log (X25519, which a quantum computer breaks but
 * which keeps today's security if both post-quantum layers fall to a new
 * classical attack). Its two signatures rest on lattices (ML-DSA) and on
 * hash functions alone (SLH-DSA); both must verify.
 *
 * THE KEM COMBINER (X-Wing / draft-ietf-hybrid-kem style)
 * -------------------------------------------------------
 *   ss = SHA3-256( ss_mlkem || ss_hqc || ss_x25519
 *                || ct_mlkem || ct_hqc || pk_x25519_eph || pk_x25519_static
 *                || "ZXV-PQM-v1" || level )
 * (ss_hqc / ct_hqc only at MATRIX). Every field has a fixed length for the
 * level, so the encoding is unambiguous. Hashing the ciphertexts keeps the
 * combination IND-CCA even if one component KEM turns out not to be
 * ciphertext-binding; hashing the X25519 keys is what makes the
 * Diffie-Hellman layer a KEM at all (as in X-Wing).
 *
 * THE DUAL SIGNATURE
 * ------------------
 * Each component signs the caller's message under a FIPS 204 / 205 context
 * string   "ZXV-PQM-v1/sig" || level || own alg id || partner alg id
 *          || SHA3-256(encoded composite public key) || |ctx| || ctx
 * so a half cannot be lifted out of a MATRIX signature and presented as a
 * HIGH (ML-DSA-only) signature, cannot be paired with a half made for
 * another key, and cannot be replayed under a different caller context.
 * pqm_verify() evaluates both halves and accepts only if both pass.
 *
 * WHAT IT COSTS
 * -------------
 * Only handshakes and signatures. Bulk data stays on 256-bit symmetric
 * crypto (ChaCha20-Poly1305 in src/tls/aead.c, keyed from ss), whose speed
 * does not depend on the level. test_pq_matrix.c prints both.
 *
 * RANDOMNESS
 * ----------
 * The library has no randomness source. Key generation takes a 32-byte
 * seed, encapsulation 32 bytes of coins, hedged signing 32 bytes of rnd;
 * each is expanded with SHAKE-256 under a domain label into the inputs of
 * the component algorithms. Those 32 bytes MUST come from a real CSPRNG
 * and MUST never be reused: a repeated encapsulation coin repeats the
 * shared secret. Signing with rnd = NULL is the deterministic variant.
 *
 * WIRE FORMAT (all integers big-endian)
 * -------------------------------------
 *   'Z' 'Q' | version (1) | object type | level | n components
 *   n x ( alg id : u16 | length : u32 | bytes )
 * Components appear in the fixed order of the level and every length is
 * fixed by its algorithm, so each (type, level) has exactly one valid
 * length; the decoders reject everything else, including trailing bytes.
 *
 * HONEST LIMITS
 * -------------
 * - These are reference implementations, not hardened ones. They avoid
 *   secret-dependent branches and table lookups where their authors
 *   designed them to, but they have not been verified against timing,
 *   cache, power or fault side channels on any particular CPU, and the
 *   kernel provides no protection against fault injection. HQC's reference
 *   in particular has had timing issues in earlier versions.
 * - "Highest available" means: the NIST category 5 parameter set of every
 *   post-quantum component (the largest standardised), plus diversity of
 *   hard problems, so that one mathematical breakthrough is not enough.
 *   It does not mean proven unbreakable. No one can prove that today, and
 *   category 5 is a cost estimate relative to AES-256, not a guarantee.
 * - HQC is selected by NIST but its FIPS is not final; HQC v5.0.0 is the
 *   submitters' latest specification and may still change on the way to
 *   the standard (which would change its KATs and the wire format's alg id).
 * - X25519 adds nothing against a quantum attacker; it is there so that a
 *   classical break of the new post-quantum schemes still leaves today's
 *   security in place.
 * - Secret material is zeroised where this library controls it (its own
 *   buffers and pqm_*_wipe()); copies the compiler spills to registers or
 *   other stack frames are outside its reach.
 * - Single-threaded: the component glue hands seeds to the vendored code
 *   through static slots, like the rest of the pqsec layer.
 */
#ifndef PQ_MATRIX_H
#define PQ_MATRIX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pq_matrix_algs.h"

/* ---- policy levels -------------------------------------------------- */
typedef enum {
    PQM_LEVEL_STANDARD = 1, /* ML-KEM-768 + X25519; ML-DSA-65 */
    PQM_LEVEL_HIGH = 2,     /* ML-KEM-1024 + X25519; ML-DSA-87 */
    PQM_LEVEL_MATRIX = 3,   /* ML-KEM-1024 + HQC-5 + X25519; ML-DSA-87 + SLH-DSA-256s */
} pqm_level_t;

#define PQM_LEVEL_ECONOMY_DEFAULT PQM_LEVEL_MATRIX

const char *pqm_level_name(pqm_level_t level);

/* ---- algorithm ids (wire format) ------------------------------------ */
#define PQM_ALG_NONE      0x0000u
#define PQM_ALG_X25519    0x0001u
#define PQM_ALG_MLKEM768  0x0101u
#define PQM_ALG_MLKEM1024 0x0102u
#define PQM_ALG_HQC5      0x0201u
#define PQM_ALG_MLDSA65   0x0301u
#define PQM_ALG_MLDSA87   0x0302u
#define PQM_ALG_SLH256S   0x0401u /* SLH-DSA-SHAKE-256s */
#define PQM_ALG_PKHASH    0xff01u /* SHA3-256 of the encoded signature pk (sig sk only) */

#define PQM_WIRE_VERSION 1u
#define PQM_LABEL        "ZXV-PQM-v1"

/* ---- sizes ---------------------------------------------------------- */
#define PQM_SEED_BYTES 32u  /* keygen seed, encaps coins, signing rnd */
#define PQM_CTX_MAX    128u /* caller context string for pqm_sign/verify */
#define PQM_X25519_LEN 32u

#define PQM_MLKEM768_EK_BYTES 1184u
#define PQM_MLKEM768_DK_BYTES 2400u
#define PQM_MLKEM768_CT_BYTES 1088u
#define PQM_MLDSA65_PK_BYTES  1952u
#define PQM_MLDSA65_SK_BYTES  4032u
#define PQM_MLDSA65_SIG_BYTES 3309u

/* Largest encoding of each object type over all levels (MATRIX). */
#define PQM_HDR_BYTES      6u
#define PQM_COMP_HDR_BYTES 6u
#define PQM_KEM_PK_MAX_BYTES                                                                       \
    (PQM_HDR_BYTES + 3 * PQM_COMP_HDR_BYTES + PQM_MLKEM1024_EK_BYTES + PQM_HQC5_PK_BYTES +         \
     PQM_X25519_LEN)
#define PQM_KEM_CT_MAX_BYTES                                                                       \
    (PQM_HDR_BYTES + 3 * PQM_COMP_HDR_BYTES + PQM_MLKEM1024_CT_BYTES + PQM_HQC5_CT_BYTES +         \
     PQM_X25519_LEN)
#define PQM_KEM_SK_MAX_BYTES                                                                       \
    (PQM_HDR_BYTES + 3 * PQM_COMP_HDR_BYTES + PQM_MLKEM1024_DK_BYTES + PQM_HQC5_SK_BYTES +         \
     2 * PQM_X25519_LEN)
#define PQM_SIG_PK_MAX_BYTES                                                                       \
    (PQM_HDR_BYTES + 2 * PQM_COMP_HDR_BYTES + PQM_MLDSA87_PK_BYTES + PQM_SLH256S_PK_BYTES)
#define PQM_SIG_MAX_BYTES                                                                          \
    (PQM_HDR_BYTES + 2 * PQM_COMP_HDR_BYTES + PQM_MLDSA87_SIG_BYTES + PQM_SLH256S_SIG_BYTES)
#define PQM_SIG_SK_MAX_BYTES                                                                       \
    (PQM_HDR_BYTES + 3 * PQM_COMP_HDR_BYTES + PQM_MLDSA87_SK_BYTES + PQM_SLH256S_SK_BYTES + 32u)

/* Object types in the wire header. */
typedef enum {
    PQM_OBJ_KEM_PK = 1,
    PQM_OBJ_KEM_CT = 2,
    PQM_OBJ_KEM_SK = 3,
    PQM_OBJ_SIG_PK = 4,
    PQM_OBJ_SIG = 5,
    PQM_OBJ_SIG_SK = 6,
} pqm_obj_t;

/* ---- in-memory objects ---------------------------------------------
 * Fixed-capacity structs sized for MATRIX; lower levels use a prefix of
 * each array (ML-KEM-768 keys in the ML-KEM arrays, and so on). They are
 * large (a MATRIX ciphertext is 16 KB, a signature 34 KB): keep them off
 * small kernel stacks. */
typedef struct {
    uint8_t level;
    uint8_t mlkem_ek[PQM_MLKEM1024_EK_BYTES];
    uint8_t hqc_pk[PQM_HQC5_PK_BYTES];
    uint8_t x25519_pk[PQM_X25519_LEN];
} pqm_kem_pk_t;

typedef struct {
    uint8_t level;
    uint8_t mlkem_dk[PQM_MLKEM1024_DK_BYTES];
    uint8_t hqc_sk[PQM_HQC5_SK_BYTES];
    uint8_t x25519_sk[PQM_X25519_LEN];
    uint8_t x25519_pk[PQM_X25519_LEN]; /* static pk, an input to the combiner */
} pqm_kem_sk_t;

typedef struct {
    uint8_t level;
    uint8_t mlkem_ct[PQM_MLKEM1024_CT_BYTES];
    uint8_t hqc_ct[PQM_HQC5_CT_BYTES];
    uint8_t x25519_eph[PQM_X25519_LEN];
} pqm_kem_ct_t;

typedef struct {
    uint8_t level;
    uint8_t mldsa_pk[PQM_MLDSA87_PK_BYTES];
    uint8_t slh_pk[PQM_SLH256S_PK_BYTES];
} pqm_sig_pk_t;

typedef struct {
    uint8_t level;
    uint8_t mldsa_sk[PQM_MLDSA87_SK_BYTES];
    uint8_t slh_sk[PQM_SLH256S_SK_BYTES];
    uint8_t pk_hash[32]; /* SHA3-256(pqm_sig_pk_encode(pk)), binds both halves to the key */
} pqm_sig_sk_t;

typedef struct {
    uint8_t level;
    uint8_t mldsa_sig[PQM_MLDSA87_SIG_BYTES];
    uint8_t slh_sig[PQM_SLH256S_SIG_BYTES];
} pqm_sig_t;

/* ---- hybrid KEM ----------------------------------------------------- */

/* Derive a key pair for `level` from 32 seed bytes. Returns false only on
 * an unknown level or NULL argument. */
bool pqm_kem_keygen(pqm_level_t level, const uint8_t seed[PQM_SEED_BYTES], pqm_kem_pk_t *pk,
                    pqm_kem_sk_t *sk);

/* Encapsulate to pk with 32 bytes of fresh coins. Returns false (and a
 * zero ss) if pk is malformed: unknown level, an ML-KEM key that fails the
 * FIPS 203 modulus check, or a low-order X25519 key. */
bool pqm_encaps(const pqm_kem_pk_t *pk, const uint8_t coins[PQM_SEED_BYTES], pqm_kem_ct_t *ct,
                uint8_t ss[PQM_SS_BYTES]);

/* Decapsulate. A tampered ciphertext does NOT return false: every
 * post-quantum layer rejects implicitly, so the caller gets a secret that
 * simply does not match the sender's and the session's first AEAD record
 * fails. Returns false only if sk and ct are for different levels, or on
 * NULL arguments. */
bool pqm_decaps(const pqm_kem_sk_t *sk, const pqm_kem_ct_t *ct, uint8_t ss[PQM_SS_BYTES]);

/* ---- dual signatures ------------------------------------------------ */

bool pqm_sig_keygen(pqm_level_t level, const uint8_t seed[PQM_SEED_BYTES], pqm_sig_pk_t *pk,
                    pqm_sig_sk_t *sk);

/* ctx: optional caller context (NULL/0, at most PQM_CTX_MAX bytes).
 * rnd: 32 fresh bytes for hedged signing, or NULL for deterministic. */
bool pqm_sign(const pqm_sig_sk_t *sk, const uint8_t *msg, size_t msg_len, const uint8_t *ctx,
              size_t ctx_len, const uint8_t *rnd, pqm_sig_t *sig);

/* True only if every component of the level verifies. */
bool pqm_verify(const pqm_sig_pk_t *pk, const uint8_t *msg, size_t msg_len, const uint8_t *ctx,
                size_t ctx_len, const pqm_sig_t *sig);

/* ---- serialisation --------------------------------------------------
 * *_size(level) is the exact encoded length (0 for an unknown level).
 * *_encode() returns the bytes written, or 0 if cap is too small or the
 * object is malformed. *_decode() accepts exactly one encoding per object
 * and returns false, leaving the object zeroed, on anything else. */
size_t pqm_encoded_size(pqm_obj_t type, pqm_level_t level);

size_t pqm_kem_pk_encode(const pqm_kem_pk_t *pk, uint8_t *out, size_t cap);
bool pqm_kem_pk_decode(pqm_kem_pk_t *pk, const uint8_t *in, size_t len);
size_t pqm_kem_ct_encode(const pqm_kem_ct_t *ct, uint8_t *out, size_t cap);
bool pqm_kem_ct_decode(pqm_kem_ct_t *ct, const uint8_t *in, size_t len);
size_t pqm_kem_sk_encode(const pqm_kem_sk_t *sk, uint8_t *out, size_t cap);
bool pqm_kem_sk_decode(pqm_kem_sk_t *sk, const uint8_t *in, size_t len);
size_t pqm_sig_pk_encode(const pqm_sig_pk_t *pk, uint8_t *out, size_t cap);
bool pqm_sig_pk_decode(pqm_sig_pk_t *pk, const uint8_t *in, size_t len);
size_t pqm_sig_encode(const pqm_sig_t *sig, uint8_t *out, size_t cap);
bool pqm_sig_decode(pqm_sig_t *sig, const uint8_t *in, size_t len);
size_t pqm_sig_sk_encode(const pqm_sig_sk_t *sk, uint8_t *out, size_t cap);
bool pqm_sig_sk_decode(pqm_sig_sk_t *sk, const uint8_t *in, size_t len);

/* ---- zeroisation ---------------------------------------------------- */
void pqm_wipe(void *p, size_t n);
void pqm_kem_sk_wipe(pqm_kem_sk_t *sk);
void pqm_sig_sk_wipe(pqm_sig_sk_t *sk);

#endif /* PQ_MATRIX_H */
