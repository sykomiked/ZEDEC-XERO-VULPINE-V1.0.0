/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_curzi8889a.c — host test for CURZI-8889-A LAYER 1 + LAYER 4
 * (curzi8889a.c: hybrid combiner, phase sub-keys, signed combiner, chunking,
 * the pre-fix timestamp gate, and the composite self-check).
 *
 * The keys are KNOWN ANSWERS, not self-agreement: each expected vector was
 * computed outside this tree with Python's hashlib.shake_256 over the encoding
 * the header specifies,
 *     u32le(len label) || label || ss_pq || ss_cl || canon(phase) || role ||
 *     ring || u32le(len transcript) || transcript
 * so a change to any field, its order, the length prefixes, the GLUT -> 5
 * canonicalisation or the KDF itself fails here. The signed variant is linked
 * against a recording stub of pq_mldsa65_sign (ML-DSA has its own KATs), so
 * the exact bytes it is asked to sign and the hedge randomness are checked.
 * Refusals are checked for their code AND for leaving the output untouched.
 *
 * Build (from kernel/):
 *   gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/modbind
 *       -Isrc/surplus -Isrc/curzi -Isrc/pqsec -Isrc/lpres -Isrc/mlkem
 *       src/curzi/test_curzi8889a.c src/curzi/curzi8889a.c src/mlkem/keccak.c */
#include <stdio.h>
#include <string.h>
#include "curzi8889a.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
        }                                                                                          \
    } while (0)

/* ---- recording stub for the ML-DSA signer -------------------------------- */
static struct {
    int calls;
    const uint8_t *sk;
    uint8_t msg[2048];
    uint32_t msg_len;
    const uint8_t *ctx;
    uint32_t ctx_len;
    uint8_t rnd[32];
    int rnd_null;
} g_sign;

void pq_mldsa65_sign(const uint8_t sk[PQ_MLDSA65_SK_BYTES], const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *ctx, uint32_t ctx_len, const uint8_t rnd[32],
                     uint8_t sig[PQ_MLDSA65_SIG_BYTES])
{
    g_sign.calls++;
    g_sign.sk = sk;
    g_sign.msg_len = msg_len;
    if (msg_len <= sizeof g_sign.msg) memcpy(g_sign.msg, msg, msg_len);
    g_sign.ctx = ctx;
    g_sign.ctx_len = ctx_len;
    g_sign.rnd_null = (rnd == NULL);
    if (rnd) memcpy(g_sign.rnd, rnd, 32);
    for (uint32_t i = 0; i < PQ_MLDSA65_SIG_BYTES; i++) sig[i] = (uint8_t) (0xA5 ^ i);
}

/* ---- known answers (Python hashlib.shake_256, see header comment) -------- */
static const uint8_t K_TRUE_ZXV[32] = {/* TRUE, role 0, ring 0, "zxv!" */
                                       0x67, 0x86, 0x65, 0x2c, 0xce, 0xc4, 0x93, 0xb4,
                                       0xff, 0xb7, 0xe3, 0x45, 0x89, 0x25, 0xf2, 0x27,
                                       0xbb, 0xf6, 0x17, 0x44, 0x99, 0x52, 0x02, 0x91,
                                       0xb5, 0xe7, 0xed, 0x34, 0xf4, 0x44, 0x18, 0x8b};
static const uint8_t K_GLUT_ABC[32] = {/* GLUT(->5), role 2, ring 12, "abc" */
                                       0x96, 0x73, 0x7e, 0x60, 0x9b, 0xbe, 0x36, 0x21,
                                       0xd1, 0x4f, 0x31, 0x5c, 0x6f, 0x37, 0x55, 0xba,
                                       0xff, 0xe2, 0xed, 0xd9, 0xaa, 0xca, 0xfb, 0xb9,
                                       0xa3, 0x6c, 0x72, 0xc2, 0x96, 0x4b, 0x6f, 0x0f};
static const uint8_t K_FALSE_EMPTY[32] = {/* FALSE, role 255, ring 7, empty */
                                          0x3a, 0x3c, 0xf9, 0x5d, 0xa7, 0x2e, 0xc8, 0x6f,
                                          0x2a, 0x08, 0x64, 0xc6, 0xa4, 0x06, 0xc9, 0x67,
                                          0xc8, 0x5a, 0x31, 0x85, 0x8e, 0xa7, 0xf5, 0xd9,
                                          0x06, 0x3e, 0xad, 0x41, 0x07, 0x72, 0x82, 0xce};
static const uint8_t K_MINUS_BIG[32] = {/* GLUT_MINUS, role 1, ring 3, 926-byte max */
                                        0x23, 0xcc, 0x81, 0x3c, 0xaa, 0x78, 0x2b, 0x3e,
                                        0x11, 0x02, 0x19, 0x60, 0x89, 0xf3, 0xdd, 0xe8,
                                        0xf4, 0xd4, 0xf8, 0x73, 0x2c, 0x32, 0x04, 0xb1,
                                        0xdb, 0xb9, 0x58, 0x5f, 0x03, 0x52, 0x8f, 0x37};
static const uint8_t K_PLUS_AB[32] = {/* GLUT_PLUS, role 0, ring 0, "ab" */
                                      0x28, 0x41, 0xf4, 0xe0, 0x61, 0xcc, 0x48, 0x1e,
                                      0x39, 0x36, 0x45, 0x80, 0x72, 0x48, 0x6f, 0x5c,
                                      0xae, 0xb4, 0xfb, 0x04, 0xd3, 0x65, 0x03, 0x10,
                                      0x40, 0x20, 0x22, 0x20, 0xbe, 0x38, 0x71, 0xc2};
static const uint8_t K_PHASE[6][32] = {
    /* phase 0 */ {0x08, 0x72, 0x90, 0xee, 0xe4, 0xd0, 0x62, 0x59, 0xc8, 0x6e, 0xec,
                   0x2a, 0xbc, 0x29, 0xaa, 0xaa, 0x99, 0x02, 0x23, 0x1d, 0x08, 0x8b,
                   0x3c, 0x20, 0x70, 0xf2, 0x08, 0x3a, 0x46, 0x2f, 0xb1, 0x79},
    /* phase 1 */
    {0x8d, 0xfc, 0x96, 0x39, 0x09, 0x38, 0x4d, 0xa8, 0xc9, 0x08, 0x11,
     0x3c, 0x67, 0x61, 0x8e, 0x5a, 0xd9, 0x57, 0xf5, 0x6e, 0xd8, 0x5c,
     0x21, 0xc4, 0x64, 0xbb, 0x47, 0x86, 0xb5, 0x20, 0x33, 0x79},
    /* phase 2 is the alias of 5: same vector as row 5 */
    {0x52, 0x44, 0x87, 0x4a, 0x7e, 0xa6, 0x9f, 0x15, 0x48, 0x22, 0x15,
     0xfb, 0x16, 0x2b, 0x57, 0xe2, 0x9f, 0xaf, 0x3f, 0x63, 0xcf, 0x8f,
     0xc5, 0x71, 0xf4, 0xe1, 0x91, 0x52, 0xc1, 0xe2, 0x7f, 0x39},
    /* phase 3 */
    {0x88, 0xf5, 0xba, 0x32, 0xfc, 0x9b, 0xa9, 0x83, 0xc5, 0x69, 0x43,
     0x31, 0x12, 0xad, 0xc6, 0xbd, 0x37, 0x80, 0xe8, 0x49, 0xe1, 0x33,
     0xcd, 0xa8, 0x57, 0x91, 0xf6, 0x19, 0xa6, 0xeb, 0x0e, 0x5f},
    /* phase 4 */
    {0xae, 0xa6, 0x41, 0x3b, 0x55, 0xa7, 0x4c, 0x4d, 0xa0, 0x4b, 0x51,
     0x73, 0x0b, 0xc9, 0x32, 0x60, 0x20, 0xf1, 0xef, 0x70, 0x4a, 0x3d,
     0x73, 0x74, 0x87, 0x3f, 0x21, 0x75, 0xf1, 0x75, 0x1a, 0x5d},
    /* phase 5 */
    {0x52, 0x44, 0x87, 0x4a, 0x7e, 0xa6, 0x9f, 0x15, 0x48, 0x22, 0x15,
     0xfb, 0x16, 0x2b, 0x57, 0xe2, 0x9f, 0xaf, 0x3f, 0x63, 0xcf, 0x8f,
     0xc5, 0x71, 0xf4, 0xe1, 0x91, 0x52, 0xc1, 0xe2, 0x7f, 0x39},
};

static uint8_t PQ[32], CL[32], BIG[1100];

static void init_inputs(void)
{
    for (int i = 0; i < 32; i++) {
        PQ[i] = (uint8_t) i;
        CL[i] = (uint8_t) (i < 16 ? 0xf0 + i : 0xe0 + (i - 16));
    }
    for (int i = 0; i < (int) sizeof BIG; i++) BIG[i] = (uint8_t) ((i * 31 + 7) & 0xff);
}

static int eq(const uint8_t *a, const uint8_t *b, size_t n)
{
    return memcmp(a, b, n) == 0;
}

static int all(const uint8_t *a, uint8_t v, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (a[i] != v) return 0;
    return 1;
}

/* The encoding, rebuilt here from the header text, for the signed variant. */
static uint32_t put_label(uint8_t *out, const char *L)
{
    uint32_t ll = (uint32_t) strlen(L);
    out[0] = (uint8_t) ll;
    out[1] = out[2] = out[3] = 0;
    memcpy(out + 4, L, ll);
    return 4 + ll;
}

static uint32_t encode_with(uint8_t *out, const uint8_t *pq, const uint8_t *cl, uint8_t phase,
                            uint8_t role, uint8_t ring, const uint8_t *tr, uint32_t tlen)
{
    uint32_t n = put_label(out, "CURZI-8889-A/v1/combine");
    memcpy(out + n, pq, 32);
    n += 32;
    memcpy(out + n, cl, 32);
    n += 32;
    out[n++] = phase;
    out[n++] = role;
    out[n++] = ring;
    out[n++] = (uint8_t) (tlen & 0xff);
    out[n++] = (uint8_t) (tlen >> 8);
    out[n++] = (uint8_t) (tlen >> 16);
    out[n++] = (uint8_t) (tlen >> 24);
    if (tlen) memcpy(out + n, tr, tlen);
    return n + tlen;
}

static uint32_t encode(uint8_t *out, uint8_t phase, uint8_t role, uint8_t ring, const uint8_t *tr,
                       uint32_t tlen)
{
    return encode_with(out, PQ, CL, phase, role, ring, tr, tlen);
}

static uint32_t encode_phase(uint8_t *out, uint8_t phase)
{
    uint32_t n = put_label(out, "CURZI-8889-A/v1/phase");
    memcpy(out + n, PQ, 32);
    n += 32;
    out[n++] = phase;
    return n;
}

static void test_combine(void)
{
    uint8_t k[32], k2[32];
    CHECK(curzi_hybrid_combine(PQ, CL, (const uint8_t *) "zxv!", 4, TRIT_TRUE, 0, 0, k) ==
                  CURZI_OK &&
              eq(k, K_TRUE_ZXV, 32),
          "KAT: TRUE / zxv!");
    CHECK(curzi_hybrid_combine(PQ, CL, (const uint8_t *) "abc", 3, TRIT_GLUT, 2, 12, k) ==
                  CURZI_OK &&
              eq(k, K_GLUT_ABC, 32),
          "KAT: GLUT canonicalised to 5, ring 12 (last legal position)");
    CHECK(curzi_hybrid_combine(PQ, CL, (const uint8_t *) "abc", 3, TRIT_GLUT_NEUTRAL, 2, 12, k2) ==
                  CURZI_OK &&
              eq(k2, K_GLUT_ABC, 32),
          "KAT: GLUT_NEUTRAL equals its alias");
    CHECK(curzi_hybrid_combine(PQ, CL, NULL, 0, TRIT_FALSE, 255, 7, k) == CURZI_OK &&
              eq(k, K_FALSE_EMPTY, 32),
          "KAT: empty transcript via NULL,0");
    CHECK(curzi_hybrid_combine(PQ, CL, BIG, 0, TRIT_FALSE, 255, 7, k) == CURZI_OK &&
              eq(k, K_FALSE_EMPTY, 32),
          "KAT: empty transcript via ptr,0");
    CHECK(curzi_hybrid_combine(PQ, CL, (const uint8_t *) "ab", 2, TRIT_GLUT_PLUS, 0, 0, k) ==
                  CURZI_OK &&
              eq(k, K_PLUS_AB, 32),
          "KAT: GLUT_PLUS / ab");
    /* 4+23+32+32+3+4 = 98 fixed bytes, so 926 is the largest transcript */
    CHECK(curzi_hybrid_combine(PQ, CL, BIG, 926, TRIT_GLUT_MINUS, 1, 3, k) == CURZI_OK &&
              eq(k, K_MINUS_BIG, 32),
          "KAT: 926-byte transcript (two-byte length prefix)");

    memset(k, 0xCC, 32);
    CHECK(curzi_hybrid_combine(PQ, CL, BIG, 927, TRIT_TRUE, 0, 0, k) == CURZI_E_TRANSCRIPT,
          "927-byte transcript refused, not truncated");
    CHECK(all(k, 0xCC, 32), "refusal leaves the key untouched");
    CHECK(curzi_hybrid_combine(PQ, CL, BIG, 1024, TRIT_TRUE, 0, 0, k) == CURZI_E_TRANSCRIPT,
          "1024-byte transcript refused");
    CHECK(curzi_hybrid_combine(PQ, CL, BIG, 0xFFFFFFFFu, TRIT_TRUE, 0, 0, k) == CURZI_E_TRANSCRIPT,
          "4 GiB length refused before any read");
    CHECK(curzi_hybrid_combine(PQ, CL, BIG, 0xFFFFFF00u, TRIT_TRUE, 0, 0, k) == CURZI_E_TRANSCRIPT,
          "near-wrap length refused");
    CHECK(curzi_hybrid_combine(PQ, CL, BIG, 4, TRIT_TRUE, 0, 13, k) == CURZI_E_TRANSCRIPT,
          "ring 13 refused");
    CHECK(curzi_hybrid_combine(PQ, CL, BIG, 4, TRIT_TRUE, 0, 255, k) == CURZI_E_TRANSCRIPT,
          "ring 255 refused");
    CHECK(curzi_hybrid_combine(NULL, CL, BIG, 4, TRIT_TRUE, 0, 0, k) == CURZI_E_NULL,
          "null pq refused");
    CHECK(curzi_hybrid_combine(PQ, NULL, BIG, 4, TRIT_TRUE, 0, 0, k) == CURZI_E_NULL,
          "null classical refused");
    CHECK(curzi_hybrid_combine(PQ, CL, BIG, 4, TRIT_TRUE, 0, 0, NULL) == CURZI_E_NULL,
          "null out refused");
    CHECK(curzi_hybrid_combine(PQ, CL, NULL, 4, TRIT_TRUE, 0, 0, k) == CURZI_E_NULL,
          "null transcript with nonzero length refused");
    CHECK(all(k, 0xCC, 32), "every refusal left the key untouched");

    /* every byte of BOTH secrets is bound (last bytes too) */
    uint8_t pq2[32], cl2[32];
    memcpy(pq2, PQ, 32);
    memcpy(cl2, CL, 32);
    pq2[31] ^= 0x80;
    curzi_hybrid_combine(pq2, CL, (const uint8_t *) "zxv!", 4, TRIT_TRUE, 0, 0, k);
    CHECK(!eq(k, K_TRUE_ZXV, 32), "last pq byte is bound");
    cl2[31] ^= 0x80;
    curzi_hybrid_combine(PQ, cl2, (const uint8_t *) "zxv!", 4, TRIT_TRUE, 0, 0, k);
    CHECK(!eq(k, K_TRUE_ZXV, 32), "last classical byte is bound");
}

static void test_phase_key(void)
{
    uint8_t k[32];
    const trit_t ph[6] = {TRIT_FALSE,     TRIT_TRUE,       TRIT_GLUT,
                          TRIT_GLUT_PLUS, TRIT_GLUT_MINUS, TRIT_GLUT_NEUTRAL};
    for (int i = 0; i < 6; i++) {
        memset(k, 0, 32);
        CHECK(curzi_phase_key(PQ, ph[i], k) == CURZI_OK && eq(k, K_PHASE[i], 32),
              "phase sub-key KAT");
    }
    memset(k, 0xCC, 32);
    CHECK(curzi_phase_key(NULL, TRIT_TRUE, k) == CURZI_E_NULL, "null master refused");
    CHECK(curzi_phase_key(PQ, TRIT_TRUE, NULL) == CURZI_E_NULL, "null out refused");
    CHECK(all(k, 0xCC, 32), "refusal leaves the sub-key untouched");
}

static void test_signed(void)
{
    static uint8_t sk[PQ_MLDSA65_SK_BYTES];
    static uint8_t want[2048];
    static pq_hybrid_sig_t sig;
    uint8_t k[32];

    memset(&sig, 0, sizeof sig);
    sig.mldsa_verified = LPRES_STATE_FALSE;
    sig.slh_verified = LPRES_STATE_BOTH;
    g_sign.calls = 0;
    CHECK(curzi_hybrid_combine_signed(PQ, CL, (const uint8_t *) "zxv!", 4, TRIT_TRUE, 0, 0, sk, k,
                                      &sig) == CURZI_OK,
          "signed combine ok");
    CHECK(eq(k, K_TRUE_ZXV, 32), "signed combine derives the same key as the unsigned one");
    uint32_t wl = encode(want, 1, 0, 0, (const uint8_t *) "zxv!", 4);
    CHECK(g_sign.calls == 1 && g_sign.sk == sk, "signed exactly once with the given key");
    CHECK(g_sign.msg_len == wl && wl == 102 && eq(g_sign.msg, want, wl),
          "signed message is the injective transcript encoding");
    CHECK(g_sign.ctx == NULL && g_sign.ctx_len == 0, "no ML-DSA context");
    int rok = !g_sign.rnd_null;
    for (int i = 0; i < 32; i++)
        if (g_sign.rnd[i] != (uint8_t) (i * 7 + 3)) rok = 0;
    CHECK(rok, "hedge randomness rnd[i] = 7i + 3");
    CHECK(sig.mldsa_sig[0] == 0xA5 && sig.mldsa_sig[PQ_MLDSA65_SIG_BYTES - 1] ==
                                          (uint8_t) (0xA5 ^ (PQ_MLDSA65_SIG_BYTES - 1)),
          "signature written into out_sig");
    CHECK(sig.mldsa_verified == LPRES_STATE_TRUE, "mldsa half marked TRUE");
    CHECK(sig.slh_verified == LPRES_STATE_NEITHER, "slh half marked NEITHER");

    /* canonical phase in the SIGNED bytes too, and the largest transcript */
    g_sign.calls = 0;
    CHECK(curzi_hybrid_combine_signed(PQ, CL, BIG, 926, TRIT_GLUT_MINUS, 1, 3, sk, k, &sig) ==
                  CURZI_OK &&
              eq(k, K_MINUS_BIG, 32),
          "signed combine, 926-byte transcript");
    wl = encode(want, 4, 1, 3, BIG, 926);
    CHECK(g_sign.calls == 1 && g_sign.msg_len == wl && eq(g_sign.msg, want, wl),
          "signed bytes for the 926-byte transcript");
    CHECK(curzi_hybrid_combine_signed(PQ, CL, (const uint8_t *) "abc", 3, TRIT_GLUT, 2, 12, sk, k,
                                      &sig) == CURZI_OK &&
              eq(k, K_GLUT_ABC, 32),
          "signed combine canonicalises GLUT");
    wl = encode(want, 5, 2, 12, (const uint8_t *) "abc", 3);
    CHECK(g_sign.msg_len == wl && eq(g_sign.msg, want, wl), "signed bytes carry phase 5");
    CHECK(curzi_hybrid_combine_signed(PQ, CL, NULL, 0, TRIT_FALSE, 255, 7, sk, k, &sig) ==
                  CURZI_OK &&
              eq(k, K_FALSE_EMPTY, 32),
          "signed combine, empty transcript");

    /* refusals: nothing signed, key and seal untouched */
    memset(k, 0xCC, 32);
    sig.mldsa_verified = LPRES_STATE_FALSE;
    g_sign.calls = 0;
    CHECK(curzi_hybrid_combine_signed(PQ, CL, BIG, 927, TRIT_TRUE, 0, 0, sk, k, &sig) ==
              CURZI_E_TRANSCRIPT,
          "signed: 927 bytes refused");
    CHECK(curzi_hybrid_combine_signed(PQ, CL, BIG, 1024, TRIT_TRUE, 0, 0, sk, k, &sig) ==
              CURZI_E_TRANSCRIPT,
          "signed: 1024 bytes refused");
    CHECK(curzi_hybrid_combine_signed(PQ, CL, BIG, 1025, TRIT_TRUE, 0, 0, sk, k, &sig) ==
              CURZI_E_TRANSCRIPT,
          "signed: 1025 bytes refused");
    CHECK(g_sign.calls == 0, "nothing was signed for an oversize transcript");
    CHECK(curzi_hybrid_combine_signed(NULL, CL, BIG, 4, TRIT_TRUE, 0, 0, sk, k, &sig) ==
              CURZI_E_NULL,
          "signed: null pq refused");
    CHECK(curzi_hybrid_combine_signed(PQ, NULL, BIG, 4, TRIT_TRUE, 0, 0, sk, k, &sig) ==
              CURZI_E_NULL,
          "signed: null classical refused");
    CHECK(curzi_hybrid_combine_signed(PQ, CL, BIG, 4, TRIT_TRUE, 0, 0, NULL, k, &sig) ==
              CURZI_E_NULL,
          "signed: null signing key refused");
    CHECK(curzi_hybrid_combine_signed(PQ, CL, BIG, 4, TRIT_TRUE, 0, 0, sk, NULL, &sig) ==
              CURZI_E_NULL,
          "signed: null out_key refused");
    CHECK(curzi_hybrid_combine_signed(PQ, CL, BIG, 4, TRIT_TRUE, 0, 0, sk, k, NULL) == CURZI_E_NULL,
          "signed: null out_sig refused");
    CHECK(curzi_hybrid_combine_signed(PQ, CL, NULL, 4, TRIT_TRUE, 0, 0, sk, k, &sig) ==
              CURZI_E_NULL,
          "signed: null transcript with nonzero length refused (as unsigned)");
    CHECK(g_sign.calls == 0, "nothing was signed for a null argument");
    CHECK(all(k, 0xCC, 32) && sig.mldsa_verified == LPRES_STATE_FALSE,
          "refusals leave key and seal untouched");
    /* ring 13: the combiner refuses, and the seal is not marked verified */
    CHECK(curzi_hybrid_combine_signed(PQ, CL, BIG, 4, TRIT_TRUE, 0, 13, sk, k, &sig) ==
              CURZI_E_TRANSCRIPT,
          "signed: ring 13 refused");
    CHECK(all(k, 0xCC, 32) && sig.mldsa_verified == LPRES_STATE_FALSE,
          "ring refusal leaves key and seal verdict untouched");
}

static void test_chunk(void)
{
    static uint8_t pay[700];
    static curzi_chunk_t ch[4];
    uint8_t key[32];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t) (0x11 * i + 1);
    for (int i = 0; i < 700; i++) pay[i] = (uint8_t) (i * 13 + 5);

    memset(ch, 0x77, sizeof ch);
    CHECK(curzi_chunk_payload(pay, 600, key, ch, 4) == 3, "600 bytes -> 3 chunks");
    int ok = 1;
    const uint32_t lens[3] = {256, 256, 88};
    for (uint32_t c = 0; c < 3; c++) {
        if (ch[c].chunk_index != c || ch[c].domain != 0 || ch[c].level != 0) ok = 0;
        if (ch[c].payload_len != lens[c]) ok = 0;
        if (ch[c].chunk_sig.mldsa_verified != LPRES_STATE_NEITHER ||
            ch[c].chunk_sig.slh_verified != LPRES_STATE_NEITHER)
            ok = 0;
        for (uint32_t j = 0; j < lens[c]; j++)
            if (ch[c].encrypted_payload[j] != (uint8_t) (pay[c * 256 + j] ^ key[j % 32])) ok = 0;
    }
    CHECK(ok, "chunk headers, lengths and XOR stream exact");
    CHECK(ch[2].encrypted_payload[88] == 0x77, "short last chunk writes only its length");
    CHECK(ch[3].chunk_index == 0x77777777u, "no chunk past the count is written");

    memset(ch, 0x77, sizeof ch);
    CHECK(curzi_chunk_payload(pay, 600, key, ch, 2) == 2, "clamped to max_chunks");
    CHECK(ch[1].payload_len == 256 && ch[2].chunk_index == 0x77777777u, "clamp writes 2 only");
    CHECK(curzi_chunk_payload(pay, 256, key, ch, 4) == 1 && ch[0].payload_len == 256,
          "256 bytes -> exactly 1 chunk");
    CHECK(curzi_chunk_payload(pay, 257, key, ch, 4) == 2 && ch[1].payload_len == 1 &&
              ch[1].encrypted_payload[0] == (uint8_t) (pay[256] ^ key[0]),
          "257 bytes -> 2 chunks, second of length 1");
    CHECK(curzi_chunk_payload(pay, 1, key, ch, 4) == 1 && ch[0].payload_len == 1,
          "1 byte -> 1 chunk");
    CHECK(curzi_chunk_payload(pay, 0, key, ch, 4) == 0, "empty payload -> 0 chunks");
    CHECK(curzi_chunk_payload(NULL, 10, key, ch, 4) == 0, "null payload -> 0");
    CHECK(curzi_chunk_payload(pay, 10, NULL, ch, 4) == 0, "null key -> 0");
    CHECK(curzi_chunk_payload(pay, 10, key, NULL, 4) == 0, "null out -> 0");
    CHECK(curzi_chunk_payload(pay, 10, key, ch, 0) == 0, "max 0 -> 0");
    CHECK(curzi_chunk_payload(pay, 600, key, ch, 1) == 1 && ch[0].payload_len == 256,
          "max 1 -> 1 chunk");
}

static void test_timestamp(void)
{
    const char *ts[] = {"2026-08-12",       "2026-08-13", "2027-01-01",
                        "2026-08-11",       "2025-12-31", "2026-08-12T00:00",
                        "2026-08-11T23:59", "1999-99-99", "9999-00-00"};
    const curzi_err_t want[] = {CURZI_OK,           CURZI_OK,           CURZI_OK,
                                CURZI_E_TRANSCRIPT, CURZI_E_TRANSCRIPT, CURZI_OK,
                                CURZI_E_TRANSCRIPT, CURZI_E_TRANSCRIPT, CURZI_OK};
    for (int i = 0; i < 9; i++) {
        curzi_err_t r =
            curzi_key_validate_timestamp((const uint8_t *) ts[i], (uint32_t) strlen(ts[i]));
        if (r != want[i]) printf("  ts %s -> %d\n", ts[i], (int) r);
        CHECK(r == want[i], "timestamp gate at the fix date");
    }
    CHECK(curzi_key_validate_timestamp((const uint8_t *) "2026-08-12", 9) == CURZI_E_NULL,
          "9 bytes refused");
    CHECK(curzi_key_validate_timestamp(NULL, 10) == CURZI_E_NULL, "null refused");
    /* exactly 10 bytes, no terminator: only those 10 may be read (ASan) */
    static const uint8_t exact[10] = {'2', '0', '2', '6', '-', '0', '8', '-', '1', '2'};
    CHECK(curzi_key_validate_timestamp(exact, 10) == CURZI_OK, "unterminated 10-byte date");
    static const uint8_t early[10] = {'2', '0', '2', '6', '-', '0', '8', '-', '1', '1'};
    CHECK(curzi_key_validate_timestamp(early, 10) == CURZI_E_TRANSCRIPT,
          "unterminated 10-byte early date");
}

/* ---- fault injection into the KDF --------------------------------------
 * curzi8889a_selfcheck exists to catch a broken combiner, so it is tested by
 * breaking the KDF underneath it (-Wl,--wrap=shake256) and requiring the
 * EXACT number of failures it reports for each fault today. A check removed
 * or weakened inside the self-check changes one of these counts. */
/* Byte offsets in the combiner's KDF input: u32 len + 23-byte label = 27,
 * ss_pq 27..58, ss_classical 59..90, phase 91, role 92, ring 93, then the
 * length-prefixed transcript from 94. */
enum {
    K_NONE,
    K_CONST,      /* every output all-zero: every "must differ" check fires */
    K_COUNTER,    /* every output distinct, in its LAST byte only */
    K_NO_CONTEXT, /* phase, role and ring ignored */
    K_NO_TAIL,    /* transcript and its length prefix ignored */
    K_NO_SECRETS, /* both 32-byte secrets ignored */
    K_NO_PHASE,   /* byte 91 ignored */
    K_NO_ROLE,    /* byte 92 ignored */
    K_NO_RING,    /* byte 93 ignored */
    K_NO_FIRST,   /* first byte of each secret ignored */
    K_RECORD,     /* honest KDF; every input is recorded */
    K_COUNTER0,   /* every output distinct, in its FIRST byte only */
    K_COUNT
};
static int g_kfault;
static uint32_t g_kcalls;
static uint8_t g_rec[32][160];
static uint32_t g_reclen[32], g_nrec;
void __real_shake256(const uint8_t *data, size_t len, uint8_t *out, size_t out_len);
void __wrap_shake256(const uint8_t *data, size_t len, uint8_t *out, size_t out_len);
void __wrap_shake256(const uint8_t *data, size_t len, uint8_t *out, size_t out_len)
{
    static uint8_t tmp[2048];
    if (g_kfault == K_RECORD) {
        if (g_nrec < 32 && len <= sizeof g_rec[0]) {
            memcpy(g_rec[g_nrec], data, len);
            g_reclen[g_nrec] = (uint32_t) len;
        }
        g_nrec++;
    }
    if (g_kfault == K_CONST || g_kfault == K_COUNTER || g_kfault == K_COUNTER0) {
        memset(out, 0, out_len);
        if (g_kfault == K_COUNTER) out[out_len - 1] = (uint8_t) ++g_kcalls;
        if (g_kfault == K_COUNTER0) out[0] = (uint8_t) ++g_kcalls;
        return;
    }
    if (g_kfault == K_NO_TAIL) len = len < 94 ? len : 94;
    memcpy(tmp, data, len);
    for (size_t i = 0; i < len; i++) {
        int blank = (g_kfault == K_NO_CONTEXT && i >= 91 && i < 94) ||
                    (g_kfault == K_NO_SECRETS && i >= 27 && i < 91) ||
                    (g_kfault == K_NO_PHASE && i == 91) || (g_kfault == K_NO_ROLE && i == 92) ||
                    (g_kfault == K_NO_RING && i == 93) ||
                    (g_kfault == K_NO_FIRST && (i == 27 || i == 59));
        if (blank) tmp[i] = 0;
    }
    __real_shake256(tmp, len, out, out_len);
}

/* The self-check's own vectors, rebuilt from its documented intent: the
 * 0x00..0x1f / 0xf0..0xef secrets, "zxv!", each single-field variation, the
 * "abc"/"ab" boundary pair and the five canonical phase sub-keys. With an
 * honest KDF, recorded call by call. */
static void test_selfcheck_vectors(void)
{
    static uint8_t want[16][160];
    uint32_t wl[16], n = 0;
    uint8_t pq2[32], cl2[32];
    const uint8_t *zxv = (const uint8_t *) "zxv!", *abc = (const uint8_t *) "abc";
    memcpy(pq2, PQ, 32);
    memcpy(cl2, CL, 32);
    pq2[0] ^= 1;
    cl2[0] ^= 1;
    wl[n] = encode(want[n], 1, 0, 0, zxv, 4), n++;
    wl[n] = encode(want[n], 1, 0, 0, zxv, 4), n++;
    wl[n] = encode_with(want[n], pq2, CL, 1, 0, 0, zxv, 4), n++;
    wl[n] = encode_with(want[n], PQ, cl2, 1, 0, 0, zxv, 4), n++;
    wl[n] = encode(want[n], 0, 0, 0, zxv, 4), n++;
    wl[n] = encode(want[n], 5, 0, 0, zxv, 4), n++;
    wl[n] = encode(want[n], 5, 0, 0, zxv, 4), n++;
    wl[n] = encode(want[n], 1, 1, 0, zxv, 4), n++;
    wl[n] = encode(want[n], 1, 0, 1, zxv, 4), n++;
    wl[n] = encode(want[n], 1, 0, 0, abc, 3), n++;
    wl[n] = encode(want[n], 1, 0, 0, abc, 2), n++;
    const uint8_t ph[5] = {1, 0, 3, 4, 5};
    for (int i = 0; i < 5; i++) wl[n] = encode_phase(want[n], ph[i]), n++;

    g_nrec = 0;
    g_kfault = K_RECORD;
    int rc = curzi8889a_selfcheck();
    g_kfault = K_NONE;
    CHECK(rc == 0, "selfcheck healthy while recorded");
    CHECK(g_nrec == n, "selfcheck makes exactly 16 KDF calls");
    for (uint32_t i = 0; i < n && i < g_nrec; i++) {
        int ok = g_reclen[i] == wl[i] && eq(g_rec[i], want[i], wl[i]);
        if (!ok) printf("  selfcheck KDF call %u differs\n", i);
        CHECK(ok, "selfcheck KDF input matches its documented vector");
    }
}

static void test_selfcheck_faults(void)
{
    static const int want[K_COUNT] = {0, 15, 2, 2, 1, 12, 1, 0, 0, 2, 0, 2};
    for (int f = 0; f < K_COUNT; f++) {
        g_kfault = f;
        g_kcalls = 0;
        int got = curzi8889a_selfcheck();
        g_kfault = K_NONE;
        if (got != want[f]) printf("  fault %d -> %d\n", f, got);
        CHECK(got == want[f], "selfcheck reports the exact failure count for the fault");
    }
}

int main(void)
{
    init_inputs();
    test_combine();
    test_phase_key();
    test_signed();
    test_chunk();
    test_timestamp();
    CHECK(curzi8889a_selfcheck() == 0, "composite selfcheck healthy");
    test_selfcheck_faults();
    test_selfcheck_vectors();
    CHECK(CURZI_PHASES == 5u && CURZI_TIER_GRANTABLE == 8784u && CURZI_TIER_ADDRESSABLE == 8929u,
          "header constants");
    printf("test_curzi8889a: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
