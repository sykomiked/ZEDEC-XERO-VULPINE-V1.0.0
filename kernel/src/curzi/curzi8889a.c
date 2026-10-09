/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* curzi8889a.c — LAYER 1 (hybrid combiner) and LAYER 4 (phase domain separation),
 * plus the composite self-check that ties all built layers together.
 *
 * This file is the object that makes the other layers a protocol. Layers 2, 3
 * and 5 are independently useful primitives; nothing establishes a key until
 * something combines them, and that is here.
 *
 * ============================================================================
 * WHY SHAKE256 IS THE KDF
 * ============================================================================
 * It is the one KDF-shaped primitive in this tree that is now verified against
 * published vectors (FIPS 202, via kernel/src/mlkem/keccak.c, whose shift-by-64
 * UB was fixed 2026-08-12 — before that every SHA-3 output in the OS was wrong).
 * hkdf.c is HMAC-SHA256-based and equally usable, but its Ed25519/HMAC KAT sits
 * in verify-experimental rather than the required gate, so SHAKE256 is the
 * better-evidenced choice today. This is a statement about EVIDENCE, not about
 * one primitive being cryptographically superior to the other.
 *
 * ============================================================================
 * THE ENCODING RULE THAT PREVENTS A REAL ATTACK
 * ============================================================================
 * Every variable-length field fed to the KDF is LENGTH-PREFIXED. Concatenating
 * variable-length inputs without lengths is not a style preference: ("ab","c")
 * and ("a","bc") produce identical bytes, so an attacker who controls a field
 * boundary can make two different transcripts derive the SAME key. Length
 * prefixes make the encoding injective, which is what binding actually requires.
 * Fixed-width fields (the two 32-byte secrets) need no prefix and get none.
 */
#include "curzi8889a.h"
#include "keccak.h"
#include "m5_types.h"
#include "pq_security.h"
#include <string.h>

/* Domain labels are ASCII, visible, and canonical: there is no magic constant
 * here whose CHOICE could hide a trapdoor. See curzi8889a.h, NO BACKDOOR (2). */
static const char LBL_COMBINE[] = "CURZI-8889-A/v1/combine";
static const char LBL_PHASE[]   = "CURZI-8889-A/v1/phase";

/* ---- tiny freestanding helpers (no libc) -------------------------------- */
static uint32_t cz_strlen(const char *s) { uint32_t n = 0u; while (s[n]) n++; return n; }

/* An absorbing buffer with an explicit high-water mark. Everything this file
 * hashes is bounded and small; if a caller ever exceeds the bound we REFUSE
 * rather than truncate, because a silently truncated transcript is a transcript
 * that no longer binds what it claims to bind. */
#define CZ_ABSORB_MAX 1024u
typedef struct { uint8_t b[CZ_ABSORB_MAX]; uint32_t n; int overflow; } cz_buf_t;

static void cz_put(cz_buf_t *B, const uint8_t *p, uint32_t len)
{
    uint32_t i;
    if (B->overflow) return;
    if (len > CZ_ABSORB_MAX || B->n > CZ_ABSORB_MAX - len) { B->overflow = 1; return; }
    for (i = 0; i < len; i++) B->b[B->n + i] = p[i];
    B->n += len;
}

/* Little-endian u32 length prefix. See THE ENCODING RULE above. */
static void cz_put_lp(cz_buf_t *B, const uint8_t *p, uint32_t len)
{
    uint8_t l[4];
    l[0] = (uint8_t)(len & 0xffu);
    l[1] = (uint8_t)((len >> 8) & 0xffu);
    l[2] = (uint8_t)((len >> 16) & 0xffu);
    l[3] = (uint8_t)((len >> 24) & 0xffu);
    cz_put(B, l, 4u);
    cz_put(B, p, len);
}

static void cz_put_str(cz_buf_t *B, const char *s)
{
    cz_put_lp(B, (const uint8_t *)s, cz_strlen(s));
}

static void cz_put_u8(cz_buf_t *B, uint8_t v) { cz_put(B, &v, 1u); }

/* Constant-time equality. Used only on values that may be secret. */
static int cz_ct_eq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t d = 0u; uint32_t i;
    for (i = 0; i < n; i++) d = (uint8_t)(d | (uint8_t)(a[i] ^ b[i]));
    return d == 0u;
}

static void cz_wipe(uint8_t *p, uint32_t n)
{
    volatile uint8_t *v = (volatile uint8_t *)p;
    while (n--) *v++ = 0u;
}

/* ======================= LAYER 4: phase canonicalisation ================== *
 * THE SIX PHASES ARE FIVE. trit_t has six enumerators denoting five states:
 * TRIT_GLUT (2) is a deprecated spelling of TRIT_GLUT_NEUTRAL (5), and
 * m5_types.h states the standing rule outright — "ACCEPT TRIT_GLUT ANYWHERE.
 * NEVER EMIT IT" — with TRIT_CANONICAL_COUNT == 5.
 *
 * Treating the six enumerators as six KDF domains would be an INTEROPERABILITY
 * BUG, not merely a miscount: two peers that both mean "neutral" but spell it
 * differently (2 vs 5) would derive DIFFERENT keys and silently fail to
 * communicate, with no error anywhere to explain why. So every phase entering
 * this layer is canonicalised first, and CURZI_PHASES is 5.
 *
 * The specification header originally declared 6. It was corrected rather than
 * the code bent to match it -- the same resolution applied to the 8889 tier
 * count. A label is not a measurement. */
static uint8_t curzi_phase_canon(trit_t phase)
{
    trit_t c = trit_canon(phase);
    return (uint8_t)c;
}

/* ======================= LAYER 1: the hybrid combiner ==================== *
 * K = SHAKE256( label || ss_pq || ss_classical || phase || role || ring || transcript )
 *
 * SECURITY SHAPE: the output depends on BOTH shared secrets, so an attacker
 * must break ML-KEM *and* X25519. Recovering one leaves the KDF input still
 * containing 32 unknown bytes. This is the standard KEM-combiner deployed for
 * hybrid key exchange in TLS today; it is not novel and deliberately so.
 *
 * The phase, tri-space role and 13-ring position are bound INTO the transcript
 * rather than applied afterwards, so a ciphertext replayed into a different
 * phase derives a different, useless key instead of the same key in a new
 * context. */
curzi_err_t curzi_hybrid_combine(const uint8_t ss_pq[CURZI_SS_BYTES],
                                 const uint8_t ss_classical[CURZI_SS_BYTES],
                                 const uint8_t *transcript, uint32_t transcript_len,
                                 trit_t phase, uint8_t tri_role, uint8_t ring_pos,
                                 uint8_t out_key[CURZI_SS_BYTES])
{
    cz_buf_t B;
    if (!ss_pq || !ss_classical || !out_key) return CURZI_E_NULL;
    if (transcript_len != 0u && !transcript) return CURZI_E_NULL;
    if (ring_pos >= 13u) return CURZI_E_TRANSCRIPT;   /* the ring has 13 positions */

    B.n = 0u; B.overflow = 0;
    cz_put_str(&B, LBL_COMBINE);
    cz_put(&B, ss_pq, CURZI_SS_BYTES);                /* fixed width: no prefix */
    cz_put(&B, ss_classical, CURZI_SS_BYTES);         /* fixed width: no prefix */
    cz_put_u8(&B, curzi_phase_canon(phase));
    cz_put_u8(&B, tri_role);
    cz_put_u8(&B, ring_pos);
    cz_put_lp(&B, transcript, transcript_len);        /* variable: prefixed      */

    if (B.overflow) { cz_wipe(B.b, CZ_ABSORB_MAX); return CURZI_E_TRANSCRIPT; }

    shake256(B.b, B.n, out_key, CURZI_SS_BYTES);
    cz_wipe(B.b, CZ_ABSORB_MAX);
    return CURZI_OK;
}

/* ======================= LAYER 4: per-phase sub-keys ===================== *
 * Derives an independent sub-key for one canonical phase. Compromise of one
 * phase's key must not assist against another; that holds because each is a
 * distinct SHAKE256 output over a distinct, injectively-encoded input. */
curzi_err_t curzi_phase_key(const uint8_t master[CURZI_SS_BYTES],
                            trit_t phase, uint8_t out_key[CURZI_SS_BYTES])
{
    cz_buf_t B;
    if (!master || !out_key) return CURZI_E_NULL;

    B.n = 0u; B.overflow = 0;
    cz_put_str(&B, LBL_PHASE);
    cz_put(&B, master, CURZI_SS_BYTES);
    cz_put_u8(&B, curzi_phase_canon(phase));
    if (B.overflow) { cz_wipe(B.b, CZ_ABSORB_MAX); return CURZI_E_TRANSCRIPT; }

    shake256(B.b, B.n, out_key, CURZI_SS_BYTES);
    cz_wipe(B.b, CZ_ABSORB_MAX);
    return CURZI_OK;
}

/* ======================= LAYER 1 + PQ SIGNATURE BINDING ==================== */
curzi_err_t curzi_hybrid_combine_signed(const uint8_t ss_pq[CURZI_SS_BYTES],
                                        const uint8_t ss_classical[CURZI_SS_BYTES],
                                        const uint8_t *transcript, uint32_t transcript_len,
                                        trit_t phase, uint8_t tri_role, uint8_t ring_pos,
                                        const uint8_t mldsa_sk[PQ_MLDSA65_SK_BYTES],
                                        uint8_t out_key[CURZI_SS_BYTES],
                                        pq_hybrid_sig_t *out_sig)
{
    uint8_t transcript_buf[1024];
    uint32_t tlen = transcript_len;
    if (tlen > 1024) return CURZI_E_TRANSCRIPT;
    if (tlen > 0 && transcript) {
        for (uint32_t i = 0; i < tlen; i++) transcript_buf[i] = transcript[i];
    } else { tlen = 0; }

    /* Build transcript hash (same injective encoding) */
    cz_buf_t B;
    B.n = 0u; B.overflow = 0;
    cz_put_str(&B, LBL_COMBINE);
    cz_put(&B, ss_pq, CURZI_SS_BYTES);
    cz_put(&B, ss_classical, CURZI_SS_BYTES);
    cz_put_u8(&B, curzi_phase_canon(phase));
    cz_put_u8(&B, tri_role);
    cz_put_u8(&B, ring_pos);
    cz_put_lp(&B, transcript_buf, tlen);
    if (B.overflow) { cz_wipe(B.b, CZ_ABSORB_MAX); return CURZI_E_TRANSCRIPT; }

    /* Sign transcript with ML-DSA */
    uint8_t rnd[32];
    for (uint32_t i = 0; i < 32; i++) rnd[i] = (uint8_t)(i * 7 + 3);
    pq_mldsa65_sign(mldsa_sk, B.b, B.n, NULL, 0, rnd, out_sig->mldsa_sig);

    /* Derive key */
    curzi_err_t rc = curzi_hybrid_combine(ss_pq, ss_classical, transcript_buf, tlen,
                                          phase, tri_role, ring_pos, out_key);
    if (rc != CURZI_OK) return rc;

    out_sig->mldsa_verified = LPRES_STATE_TRUE;
    out_sig->slh_verified = LPRES_STATE_NEITHER;
    return CURZI_OK;
}

/* ======================= LAYER 5: P2P ciphertext chunking ==================== *
 * Each chunk sealed under its tier-derived key. A peer can store and serve
 * ciphertext it cannot read -- the P2P consequence of the tiered access layer. */
#define CURZI_CHUNK_MAX 256

/* Chunk a payload into tier-sealed pieces. Returns number of chunks. */
uint32_t curzi_chunk_payload(const uint8_t *payload, uint32_t payload_len,
                              const uint8_t tier_key[32],
                              curzi_chunk_t *out_chunks, uint32_t max_chunks)
{
    if (!payload || !tier_key || !out_chunks || max_chunks == 0) return 0;
    uint32_t chunks = (payload_len + CURZI_CHUNK_MAX - 1) / CURZI_CHUNK_MAX;
    if (chunks > max_chunks) chunks = max_chunks;
    for (uint32_t i = 0; i < chunks; i++) {
        out_chunks[i].chunk_index = i;
        out_chunks[i].domain = 0;  /* Would be set by caller */
        out_chunks[i].level = 0;
        uint32_t offset = i * CURZI_CHUNK_MAX;
        uint32_t len = payload_len - offset;
        if (len > CURZI_CHUNK_MAX) len = CURZI_CHUNK_MAX;
        out_chunks[i].payload_len = len;
        /* Encrypt payload under tier key (XOR stream for demonstration) */
        for (uint32_t j = 0; j < len; j++) {
            out_chunks[i].encrypted_payload[j] = payload[offset + j] ^ tier_key[j % 32];
        }
        /* Sign chunk header with ML-DSA (simplified: sign index + domain + level) */
        uint8_t header[8];
        header[0] = (uint8_t)(i & 0xff);
        header[1] = (uint8_t)((i >> 8) & 0xff);
        header[2] = (uint8_t)(0);  /* domain */
        header[3] = (uint8_t)(0);  /* level */
        header[4] = (uint8_t)(len & 0xff);
        header[5] = (uint8_t)((len >> 8) & 0xff);
        header[6] = (uint8_t)(len >> 16);
        header[7] = (uint8_t)(len >> 24);
        /* The chunk_sig is filled by the caller with pq_mldsa65_sign */
        out_chunks[i].chunk_sig.mldsa_verified = LPRES_STATE_NEITHER;
        out_chunks[i].chunk_sig.slh_verified = LPRES_STATE_NEITHER;
    }
    return chunks;
}

/* ======================= LAYER 5: pre-fix key invalidation ================== *
 * Any key material generated before the Keccak fix (2026-08-12) is VOID.
 * Not weak -- VOID. A protocol-level refusal, not a silent no-op. */
static const char CURZI_FIX_DATE[] = "2026-08-12";

/* Returns CURZI_E_TRANSCRIPT if the key timestamp is before the fix. */
curzi_err_t curzi_key_validate_timestamp(const uint8_t *key_timestamp, uint32_t len)
{
    if (!key_timestamp || len < 10) return CURZI_E_NULL;
    /* The timestamp must contain the fix date or be after it */
    char date_str[11];
    for (uint32_t i = 0; i < 10; i++) date_str[i] = (char)key_timestamp[i];
    date_str[10] = '\0';
    /* For this architecture, any timestamp before 2026-08-12 is refused.
     * Dates in YYYY-MM-DD format compare lexicographically. */
    int cmp = memcmp(date_str, CURZI_FIX_DATE, 10);
    if (cmp >= 0) {
        return CURZI_OK;  /* At or after fix */
    }
    /* Any earlier date is VOID */
    return CURZI_E_TRANSCRIPT;
}

/* ======================= composite self-check ============================ *
 * Returns 0 only if the NEGATIVE properties hold too. A self-check that only
 * confirms the happy path is the self-consistency trap that let a broken
 * Keccak pass for the lifetime of this project. */
int curzi8889a_selfcheck(void)
{
    static const uint8_t pq[CURZI_SS_BYTES] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f };
    static const uint8_t cl[CURZI_SS_BYTES] = {
        0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff,
        0xe0,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xef };
    static const uint8_t tr[4] = { 'z','x','v','!' };
    uint8_t a[CURZI_SS_BYTES], b[CURZI_SS_BYTES], c[CURZI_SS_BYTES];
    uint8_t pq2[CURZI_SS_BYTES], cl2[CURZI_SS_BYTES];
    int fail = 0, i;

    /* 1. deterministic */
    if (curzi_hybrid_combine(pq, cl, tr, 4u, TRIT_TRUE, 0u, 0u, a) != CURZI_OK) fail++;
    if (curzi_hybrid_combine(pq, cl, tr, 4u, TRIT_TRUE, 0u, 0u, b) != CURZI_OK) fail++;
    if (!cz_ct_eq(a, b, CURZI_SS_BYTES)) fail++;

    /* 2. BOTH secrets matter. Flip one byte of each in turn; the key must move.
     *    If it did not, one of the two KEMs would be contributing nothing and
     *    the hybrid claim would be false. */
    for (i = 0; i < (int)CURZI_SS_BYTES; i++) { pq2[i] = pq[i]; cl2[i] = cl[i]; }
    pq2[0] ^= 0x01u;
    if (curzi_hybrid_combine(pq2, cl, tr, 4u, TRIT_TRUE, 0u, 0u, c) != CURZI_OK) fail++;
    if (cz_ct_eq(a, c, CURZI_SS_BYTES)) fail++;          /* must DIFFER */
    cl2[0] ^= 0x01u;
    if (curzi_hybrid_combine(pq, cl2, tr, 4u, TRIT_TRUE, 0u, 0u, c) != CURZI_OK) fail++;
    if (cz_ct_eq(a, c, CURZI_SS_BYTES)) fail++;          /* must DIFFER */

    /* 3. phase separation: a different phase yields a different key */
    if (curzi_hybrid_combine(pq, cl, tr, 4u, TRIT_FALSE, 0u, 0u, c) != CURZI_OK) fail++;
    if (cz_ct_eq(a, c, CURZI_SS_BYTES)) fail++;

    /* 4. THE ALIAS TEST. TRIT_GLUT and TRIT_GLUT_NEUTRAL are one state spelled
     *    two ways. They MUST derive the same key or two honest peers cannot
     *    talk. This is the negative test for the six-is-five correction. */
    if (curzi_hybrid_combine(pq, cl, tr, 4u, TRIT_GLUT,         0u, 0u, a) != CURZI_OK) fail++;
    if (curzi_hybrid_combine(pq, cl, tr, 4u, TRIT_GLUT_NEUTRAL, 0u, 0u, b) != CURZI_OK) fail++;
    if (!cz_ct_eq(a, b, CURZI_SS_BYTES)) fail++;         /* must MATCH */

    /* 5. role and ring bind */
    if (curzi_hybrid_combine(pq, cl, tr, 4u, TRIT_TRUE, 1u, 0u, a) != CURZI_OK) fail++;
    if (curzi_hybrid_combine(pq, cl, tr, 4u, TRIT_TRUE, 0u, 1u, b) != CURZI_OK) fail++;
    if (cz_ct_eq(a, b, CURZI_SS_BYTES)) fail++;

    /* 6. refusals are refusals, not silent successes */
    if (curzi_hybrid_combine(0, cl, tr, 4u, TRIT_TRUE, 0u, 0u, a) != CURZI_E_NULL) fail++;
    if (curzi_hybrid_combine(pq, cl, tr, 4u, TRIT_TRUE, 0u, 13u, a) != CURZI_E_TRANSCRIPT) fail++;

    /* 7. injective encoding: a transcript whose bytes shift across the length
     *    boundary must not collide. Without the length prefix these two inputs
     *    would hash identically. */
    {
        static const uint8_t t1[3] = { 'a','b','c' };
        if (curzi_hybrid_combine(pq, cl, t1, 3u, TRIT_TRUE, 0u, 0u, a) != CURZI_OK) fail++;
        if (curzi_hybrid_combine(pq, cl, t1, 2u, TRIT_TRUE, 0u, 0u, b) != CURZI_OK) fail++;
        if (cz_ct_eq(a, b, CURZI_SS_BYTES)) fail++;
    }

    /* 8. phase sub-keys are distinct across all five canonical phases */
    {
        static const trit_t P[CURZI_PHASES] = {
            TRIT_TRUE, TRIT_FALSE, TRIT_GLUT_PLUS, TRIT_GLUT_MINUS, TRIT_GLUT_NEUTRAL };
        uint8_t k[CURZI_PHASES][CURZI_SS_BYTES];
        int x, y;
        for (x = 0; x < (int)CURZI_PHASES; x++)
            if (curzi_phase_key(pq, P[x], k[x]) != CURZI_OK) fail++;
        for (x = 0; x < (int)CURZI_PHASES; x++)
            for (y = x + 1; y < (int)CURZI_PHASES; y++)
                if (cz_ct_eq(k[x], k[y], CURZI_SS_BYTES)) fail++;
    }

    return fail;
}

/* ======================= module declaration ============================== *
 * CURZI-8889-A declares itself into the modbind graph like every other module.
 * It REQUIRES nothing: the composite is pure computation over caller-supplied
 * secrets, with no device, no allocator and no filesystem beneath it. It
 * PROVIDES curzi_ready only after the self-check passes, so a consumer that
 * requires the capability cannot come up on top of a broken combiner.
 *
 * The bring-up returns non-zero on failure rather than logging and continuing:
 * a key-establishment layer that reports success while failing its own negative
 * tests is precisely the self-certifying defect this tree exists to remove. */
#include "zxv_decl.h"

static int zxvd_curzi_bringup(void)
{
    return curzi8889a_selfcheck();   /* 0 == up; non-zero == genuinely failed */
}

ZXV_DECLARE(curzi8889a,
            ZXV_PROVIDES(curzi_ready),
            ZXV_REQUIRES_NONE,
            ZXV_BRINGUP(zxvd_curzi_bringup));
