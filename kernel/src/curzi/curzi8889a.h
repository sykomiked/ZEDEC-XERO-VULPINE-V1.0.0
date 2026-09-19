/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* curzi8889a.h — CURZI-8889-A: a post-quantum COMPOSITE key establishment and
 * tiered-access standard for ZXV / ZEDEC pqOS.
 *
 * ============================================================================
 * WHAT THIS IS, IN ONE PARAGRAPH
 * ============================================================================
 * CURZI-8889-A is NOT a new lattice problem. It is a COMPOSITION of primitives
 * that are already standardised and already attacked in public for years, plus
 * a threshold layer, a canonical error-correcting layer, and a tiered access
 * hierarchy. Every claim it makes is a claim about the composition, never about
 * inventing hardness. That distinction is the entire safety argument.
 *
 * ============================================================================
 * THE CLAIM WE MAKE, AND THE ONE WE REFUSE
 * ============================================================================
 * CLAIM (defensible): CURZI-8889-A is strictly stronger than ML-KEM-768 alone.
 *   It CONTAINS ML-KEM and additionally fails only if the classical component
 *   ALSO fails, only if k of n independent instances are ALSO broken, and it
 *   removes every single point of key compromise.
 *
 * REFUSED: that it "beats" ML-KEM's lattice security, or that it surpasses all
 *   known encryption. It cannot beat what it contains. ML-KEM's strength comes
 *   from six years of public cryptanalysis, not from cleverness of design, and
 *   nothing composed on top of it inherits more than it started with.
 *
 * WHY NO NOVEL LATTICE. Cryptographic hardness comes from a REDUCTION to a
 * studied problem plus sustained public attack. A bespoke lattice -- however
 * beautiful, however irregular, at any dimension -- has neither. Dimensions 8,
 * 24, 144 are additionally far below the reach of BKZ (public SVP records pass
 * 180 dimensions on academic hardware, classically). E8 and the Leech lattice
 * are the WORST possible hardness candidates precisely because they are the
 * best understood objects in the field and decode efficiently. They are used
 * here for what they are genuinely optimal at: ERROR CORRECTION and CANONICAL
 * CONSTANTS. See LAYER 3.
 *
 * ============================================================================
 * NO BACKDOOR -- ENFORCED STRUCTURALLY, NOT PROMISED
 * ============================================================================
 *  1. No escrow path exists. The combiner is a one-way KDF; there is no second
 *     decapsulation route, no master key, no recovery key, no "support" mode.
 *  2. Every constant is CANONICAL. The [8,4,4] extended Hamming code and the
 *     [24,12,8] extended binary Golay code are mathematically unique objects;
 *     there is no designer freedom in them, therefore nowhere to hide a trapdoor.
 *     Contrast an arbitrary S-box or an unexplained "magic" constant, where the
 *     CHOICE itself is the hiding place.
 *  3. Every derivation is deterministic and covered by test vectors, so a third
 *     party recomputes and compares rather than trusting this text.
 *  4. Parameters are not weakenable at run time. There is no negotiation
 *     downgrade, no cipher-suite agility, no "legacy mode".
 *
 * ============================================================================
 * LAYER 0 -- HARDNESS (unchanged, vetted, NEVER modified)
 * ============================================================================
 *   ML-KEM-768 or ML-KEM-1024, exactly as FIPS 203 specifies, byte-for-byte.
 *   Verified against NIST ACVP vectors by kernel/src/mlkem/test_mlkem_kat.c.
 *
 *   *** ML-KEM'S INTERNALS ARE OFF LIMITS TO EVERY OTHER LAYER IN THIS FILE. ***
 *   It is tempting to push the LAYER 3 code inside ML-KEM's message encoding to
 *   cut the decryption-failure rate at the source. Doing so would produce
 *   ciphertexts no other implementation can read and would invalidate the KATs
 *   that are the only reason we trust this code at all. The composite wraps
 *   ML-KEM; it never reaches into it.
 *
 * LAYER 1 -- HYBRID (fails only if BOTH fail)
 *   ML-KEM shared secret || X25519 shared secret, fed with the FULL transcript
 *   into HKDF. This is the standard KEM-combiner shape deployed in TLS today.
 *   If lattices fall, X25519 still stands; if a quantum computer arrives,
 *   ML-KEM still stands. Security is the MAX of the two, not the min.
 *
 * LAYER 2 -- THRESHOLD (no single point of failure)
 *   N independently-seeded instances (default N = 33), combined k-of-N by
 *   Shamir secret sharing over GF(2^8). Default k = 17.
 *     - Any k-1 = 16 compromised instances learn NOTHING. Not "less"; nothing.
 *       Shamir is information-theoretic below threshold: 16 shares are
 *       consistent with every possible secret.
 *     - Any N-k = 16 lost instances still recover. Secrecy and availability at
 *       the same time, which a simple XOR-of-all split cannot give you.
 *   This is the literal formalisation of "no single point of failure" and of
 *   "more people working together strengthens everyone": raising N raises the
 *   attacker's cost while LOWERING each participant's individual exposure.
 *
 * LAYER 3 -- CANONICAL CODE (the honest home for E8 and Leech)
 *   Binary codes underlying the two densest lattices, applied to the composite
 *   transport (NOT to ML-KEM -- see LAYER 0):
 *     - [8,4,4] extended Hamming  -- the code of E8 under Construction A
 *     - [24,12,8] extended Golay  -- the code of Leech under Construction A
 *   Six Golay blocks give the 144-bit frame the owner specified: 6 x 24 = 144,
 *   correcting up to 3 errors per block. These serve two purposes at once:
 *   real error correction on a noisy or hostile transport, and a source of
 *   constants nobody could have chosen adversarially.
 *
 * LAYER 4 -- SIX-PHASE DOMAIN SEPARATION
 *   The six M5 logic states (m5_types.h: FALSE, TRUE, GLUT, GLUT_PLUS,
 *   GLUT_MINUS, GLUT_NEUTRAL) each label an independent sub-key derivation.
 *   A key derived for one phase cannot decrypt another; the tri-space role and
 *   the 13-phase ring position are bound into the transcript, so a ciphertext
 *   replayed into the wrong phase derives a different, useless key.
 *
 * LAYER 5 -- TIERED ACCESS (144 domains x 61 levels)
 *   Hierarchical one-way key derivation, so a peer decrypts ONLY its tier and
 *   never has to hold, fetch, or decrypt the whole store:
 *     - 144 DOMAINS  (the "basic parameters"), independent by construction
 *     - 61 LEVELS per domain                     => 144 * 61 = 8784 GRANTABLE
 *     - plus 144 domain roots and 1 system root  => 8929 ADDRESSABLE keys
 *
 *   *** 8889 IS THE NAME OF THIS STANDARD, NOT A COUNT THIS STRUCTURE EMITS. ***
 *   An earlier draft of this header asserted "8889 grantable" in the same breath
 *   as "144 * 61 = 8784", which is self-contradictory, and the tier test caught
 *   it. The number cannot be rescued by re-choosing the rectangle either:
 *   8889 = 3 * 2963 with 2963 prime, so its only factor pairs are 1x8889 and
 *   3x2963 -- neither factor is <= 255, so no (uint16 domain, uint8 level) grid
 *   produces it. Reporting the real figures and keeping 8889 as the identity is
 *   the only honest resolution; inventing 105 filler tiers to hit the number
 *   would be making the code lie to match a comment. Measured, not asserted:
 *   test_curzi_tier.c prints both counts on every run.
 *   Holding the key at (domain d, level L) lets you derive every level ABOVE L
 *   in that domain and nothing below it, and nothing at all in another domain.
 *   The chain is one-way because each step is a KDF, so descent is impossible
 *   without the parent. A grant is 32 bytes.
 *
 *   P2P CONSEQUENCE, which is the point: content is chunked and each chunk is
 *   sealed under its own tier key. A peer can STORE and SERVE ciphertext it
 *   cannot read. Sharing bandwidth never implies sharing plaintext, so the
 *   network can be social and the data can stay private at the same time.
 *
 * ============================================================================
 * WHAT WOULD FALSIFY THIS DESIGN
 * ============================================================================
 * State it plainly so it can be checked rather than believed:
 *   - If test_mlkem_kat stops matching the ACVP vectors, LAYER 0 is void and
 *     everything above it is void with it.
 *   - If any k-1 subset of shares can be shown to constrain the secret, LAYER 2
 *     is void.
 *   - If a tier key at level L can derive a key at level < L, LAYER 5 is void.
 *   - If any constant in this system is not recomputable from the Hamming or
 *     Golay definition, the no-backdoor argument is void.
 * Each of these has a corresponding negative test; none is left to inspection.
 *
 * Freestanding: integer only, no libc, no allocation, no floating point.
 *
 * Author: H.M. Michael-Laurence: Curzi (c) — 36N9 Genetics, LLC
 */
#ifndef CURZI8889A_H
#define CURZI8889A_H

#include <stdint.h>
#include <stddef.h>
#include "m5_types.h"
#include "pq_security.h"   /* trit_t, trit_canon, TRIT_CANONICAL_COUNT */

/* ---- identity ---------------------------------------------------------- */
#define CURZI_NAME        "CURZI-8889-A"
#define CURZI_VERSION     1u

/* ---- LAYER 2: threshold ------------------------------------------------ */
#define CURZI_N_INSTANCES 33u   /* independently-seeded KEM instances        */
#define CURZI_K_THRESHOLD 17u   /* shares required; 16 compromised learn 0   */
#define CURZI_SS_BYTES    32u   /* shared-secret width                       */

/* ---- LAYER 3: canonical codes ------------------------------------------ */
#define CURZI_HAMMING_N   8u    /* [8,4,4]  extended Hamming  (E8)           */
#define CURZI_HAMMING_K   4u
#define CURZI_GOLAY_N     24u   /* [24,12,8] extended Golay   (Leech)        */
#define CURZI_GOLAY_K     12u
#define CURZI_GOLAY_T     3u    /* corrects up to 3 errors per block         */
#define CURZI_CODE_BLOCKS 6u    /* 6 x 24 = 144-bit frame                    */
#define CURZI_CODE_BITS   (CURZI_GOLAY_N * CURZI_CODE_BLOCKS)   /* 144 */

/* ---- LAYER 4: phase domain separation ---------------------------------- *
 * FIVE, NOT SIX. trit_t has six enumerators denoting five states: TRIT_GLUT (2)
 * is a deprecated spelling of TRIT_GLUT_NEUTRAL (5), and m5_types.h carries the
 * standing rule "ACCEPT TRIT_GLUT ANYWHERE. NEVER EMIT IT" with
 * TRIT_CANONICAL_COUNT == 5. An earlier draft of this header said 6.
 *
 * This is not a cosmetic miscount. Six KDF domains over six enumerators would
 * mean two peers that both intend "neutral" but spell it differently derive
 * DIFFERENT keys and silently fail to communicate. curzi8889a.c canonicalises
 * every phase on entry; curzi8889a_selfcheck() test 4 asserts that GLUT and
 * GLUT_NEUTRAL produce the SAME key, and fails if anyone removes that. */
#define CURZI_PHASES      TRIT_CANONICAL_COUNT   /* == 5 */

/* ---- LAYER 5: tiered access -------------------------------------------- */
#define CURZI_DOMAINS     144u
#define CURZI_LEVELS      61u
/* MEASURED, and these three are consistent with each other by construction:
 *   GRANTABLE   = 144 * 61            = 8784   (what a peer can actually be given)
 *   STRUCTURAL  = 1 system + 144 domain =  145  (never handed out)
 *   ADDRESSABLE = GRANTABLE + STRUCTURAL = 8929
 * CURZI_TIER_NAME is the standard's identity and is deliberately NOT equal to
 * any of them -- see the LAYER 5 note. Do not "fix" the arithmetic to match the
 * name; the name is not a count. */
#define CURZI_TIER_GRANTABLE   (CURZI_DOMAINS * CURZI_LEVELS)          /* 8784 */
#define CURZI_TIER_STRUCTURAL  (1u + CURZI_DOMAINS)                    /*  145 */
#define CURZI_TIER_ADDRESSABLE (CURZI_TIER_GRANTABLE + CURZI_TIER_STRUCTURAL) /* 8929 */
#define CURZI_TIER_NAME        8889u   /* identity of the standard, not a count */

/* ---- error codes (fail closed; 0 is the only success) ------------------ */
typedef enum {
    CURZI_OK = 0,
    CURZI_E_NULL,          /* null argument                                  */
    CURZI_E_THRESHOLD,     /* fewer than k shares supplied                   */
    CURZI_E_SHARE_DUP,     /* duplicate share index: breaks reconstruction   */
    CURZI_E_SHARE_INDEX,   /* share index 0 is the secret itself; forbidden  */
    CURZI_E_TIER_RANGE,    /* domain or level outside the declared space     */
    CURZI_E_TIER_DESCEND,  /* attempted derivation toward a LOWER level      */
    CURZI_E_CODE_UNCORRECT,/* more errors than the code can correct          */
    CURZI_E_TRANSCRIPT,    /* transcript binding mismatch (wrong phase/role) */
    CURZI_E_REASON_COUNT
} curzi_err_t;

/* ======================= LAYER 2: Shamir over GF(2^8) ==================== *
 * Field: GF(2^8) with the AES reduction polynomial x^8+x^4+x^3+x+1 (0x11b).
 * Chosen because it is canonical and already ubiquitous -- not tuned here.
 * The secret is split BYTEWISE: 32 independent GF(256) polynomials.
 * Share index 0 is refused: f(0) IS the secret. */
typedef struct {
    uint8_t index;                    /* 1..255, distinct, never 0           */
    uint8_t y[CURZI_SS_BYTES];
} curzi_share_t;

curzi_err_t curzi_split(const uint8_t secret[CURZI_SS_BYTES],
                        uint8_t n, uint8_t k,
                        const uint8_t *randomness, size_t rand_len,
                        curzi_share_t *out_shares);

curzi_err_t curzi_combine(const curzi_share_t *shares, uint8_t count,
                          uint8_t k, uint8_t out_secret[CURZI_SS_BYTES]);

/* ======================= LAYER 3: canonical codes ======================== */
uint8_t  curzi_hamming84_encode(uint8_t nibble);          /* 4 bits -> 8     */
int      curzi_hamming84_decode(uint8_t codeword, uint8_t *out_nibble);

void     curzi_golay_encode(uint16_t data12, uint8_t out[3]);   /* 12 -> 24  */
int      curzi_golay_decode(const uint8_t in[3], uint16_t *out_data12);

/* 144-bit frame = 6 Golay blocks. Returns corrected-error count, or negative
 * curzi_err_t if any block exceeded 3 errors. */
int      curzi_frame_encode(const uint8_t in9[9], uint8_t out18[18]);
int      curzi_frame_decode(const uint8_t in18[18], uint8_t out9[9]);

/* ======================= LAYER 5: tiered access ========================== *
 * curzi_tier_derive walks from (from_domain, from_level) to (to_domain,
 * to_level). It REFUSES to descend and refuses to cross domains: those are
 * CURZI_E_TIER_DESCEND and CURZI_E_TIER_RANGE, not silent no-ops. */
curzi_err_t curzi_tier_root(const uint8_t system_root[32],
                            uint16_t domain, uint8_t out_key[32]);

curzi_err_t curzi_tier_derive(const uint8_t parent_key[32],
                              uint16_t domain, uint8_t from_level,
                              uint8_t to_level, uint8_t out_key[32]);

/* ======================= LAYER 1 + LAYER 4 =============================== *
 * The composite. Both secrets, the phase, the tri-space role and the 13-ring
 * position are bound into one injectively-encoded KDF input. */
curzi_err_t curzi_hybrid_combine(const uint8_t ss_pq[CURZI_SS_BYTES],
                                 const uint8_t ss_classical[CURZI_SS_BYTES],
                                 const uint8_t *transcript, uint32_t transcript_len,
                                 trit_t phase, uint8_t tri_role, uint8_t ring_pos,
                                 uint8_t out_key[CURZI_SS_BYTES]);

/* Signed hybrid: transcript is signed by ML-DSA before KDF input. */
curzi_err_t curzi_hybrid_combine_signed(const uint8_t ss_pq[CURZI_SS_BYTES],
                                        const uint8_t ss_classical[CURZI_SS_BYTES],
                                        const uint8_t *transcript, uint32_t transcript_len,
                                        trit_t phase, uint8_t tri_role, uint8_t ring_pos,
                                        const uint8_t mldsa_sk[PQ_MLDSA65_SK_BYTES],
                                        uint8_t out_key[CURZI_SS_BYTES],
                                        pq_hybrid_sig_t *out_sig);

curzi_err_t curzi_phase_key(const uint8_t master[CURZI_SS_BYTES],
                            trit_t phase, uint8_t out_key[CURZI_SS_BYTES]);

/* Chunk structure for P2P ciphertext routing */
typedef struct {
    uint32_t chunk_index;
    uint16_t domain;
    uint8_t level;
    uint8_t encrypted_payload[256];
    uint32_t payload_len;
    pq_hybrid_sig_t chunk_sig;
} curzi_chunk_t;

/* ======================= LAYER 5: P2P ciphertext chunking ==================== */
uint32_t curzi_chunk_payload(const uint8_t *payload, uint32_t payload_len,
                              const uint8_t tier_key[32],
                              curzi_chunk_t *out_chunks, uint32_t max_chunks);

/* ======================= LAYER 5: pre-fix key invalidation ================= */
curzi_err_t curzi_key_validate_timestamp(const uint8_t *key_timestamp, uint32_t len);

/* ======================= self-check ====================================== */
/* Returns 0 only if every layer's negative tests also hold -- including that
 * k-1 shares reconstruct the WRONG secret, and that descent is refused. */
int curzi8889a_selfcheck(void);

#endif /* CURZI8889A_H */
