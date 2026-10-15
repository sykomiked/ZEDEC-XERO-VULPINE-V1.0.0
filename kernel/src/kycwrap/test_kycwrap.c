/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_kycwrap.c — the KYC wrapper (kycwrap.h D1-D7): the ZK slot that is
 * not available, and the minimal-disclosure backend (NOT zero-knowledge)
 * with real ML-DSA-87 issuer and holder keys (cpk.h). */
#include <stdlib.h>
#include "../capctl/cp_test.h"
#include "kycwrap.h"
#include "../robin_debanks/sha256.h"

#define T0  1767225600ull /* 2026-01-01T00:00:00Z, day 20454 */
#define DAY 86400ull
#define EXP 20482u /* T0 + 28 days, a multiple of 7 */

typedef struct {
    uint8_t pk[CPK_PK_LEN];
    uint8_t sk[CPK_SK_LEN];
} keypair_t;

static void sign_with(void *ctx, const uint8_t *msg, size_t len, uint8_t sig[KYC_SIG_LEN])
{
    const keypair_t *k = (const keypair_t *) ctx;
    cpk_sign(k->sk, msg, len, NULL, sig);
}

static void kp(keypair_t *k, uint8_t s)
{
    uint8_t seed[32];
    memset(seed, s, 32);
    cpk_keygen(seed, k->pk, k->sk);
}

static keypair_t ISS, UNTRUSTED, ISS_FR, HA[4], HB[2];
static cpk_ring_t RING;
static uint8_t SEED_A[32], SEED_B[32];
static uint8_t TA[4][KYC_MD_TOKEN_LEN], TB[2][KYC_MD_TOKEN_LEN];
static uint8_t T_BAD[KYC_MD_TOKEN_LEN], T_FR[KYC_MD_TOKEN_LEN];
static uint8_t P[KYC_MD_PRESENT_LEN], P2[KYC_MD_PRESENT_LEN];
static const kyc_pred_t OVER18 = {KYC_PRED_AGE_OVER, 18, {0, 0}};
static const kyc_pred_t OVER21 = {KYC_PRED_AGE_OVER, 21, {0, 0}};

static kyc_md_issuer_t issuer_of(keypair_t *k, const char *cc)
{
    kyc_md_issuer_t is;
    memset(&is, 0, sizeof is);
    cpk_key_id(k->pk, is.issuer_id);
    memcpy(is.issuer_country, cc, 2);
    is.sign = sign_with;
    is.sctx = k;
    return is;
}

static uint8_t RL[KYC_MD_REVLIST_MAX], RLSIG[CPK_SIG_LEN];

static size_t revlist(keypair_t *signer, uint32_t version, const uint8_t *seed, uint16_t count,
                      uint64_t not_after)
{
    kyc_revlist_t r;
    memset(&r, 0, sizeof r);
    r.hdr.kind = CP_KIND_KYC_REVOKE;
    memcpy(r.hdr.country, "DE", 2);
    r.hdr.version = version;
    r.hdr.not_before = T0;
    r.hdr.not_after = not_after;
    cpk_key_id(ISS.pk, r.hdr.key_id);
    if (seed) {
        memcpy(r.e[0].seed, seed, 32);
        r.e[0].count = count;
        r.n = 1;
    }
    size_t n = kyc_md_revlist_encode(&r, RL, sizeof RL);
    uint8_t msg[CP_MSG_LEN];
    cp_manifest_msg(RL, n, msg);
    cpk_sign(signer->sk, msg, sizeof msg, NULL, RLSIG);
    return n;
}

static void test_basics(void)
{
    kyc_pred_t p = {KYC_PRED_RESIDENT, 0, {'D', 'E'}};
    CHECK(kyc_pred_ok(&OVER18) && kyc_pred_ok(&p) &&
              kyc_pred_ok(&(kyc_pred_t){KYC_PRED_KYCD, 0, {0, 0}}),
          "valid predicates: over 18, resident of DE, KYC'd");
    CHECK(!kyc_pred_ok(&(kyc_pred_t){KYC_PRED_AGE_OVER, 0, {0, 0}}) &&
              !kyc_pred_ok(&(kyc_pred_t){KYC_PRED_AGE_OVER, 151, {0, 0}}) &&
              !kyc_pred_ok(&(kyc_pred_t){KYC_PRED_AGE_OVER, 18, {'D', 'E'}}) &&
              !kyc_pred_ok(&(kyc_pred_t){KYC_PRED_RESIDENT, 0, {'d', 'e'}}) &&
              !kyc_pred_ok(&(kyc_pred_t){KYC_PRED_RESIDENT, 1, {'D', 'E'}}) &&
              !kyc_pred_ok(&(kyc_pred_t){KYC_PRED_KYCD, 1, {0, 0}}) &&
              !kyc_pred_ok(&(kyc_pred_t){4, 0, {0, 0}}) && !kyc_pred_ok(NULL),
          "invalid predicates refused (age 0/151, stray fields, lowercase, kind 4)");
    uint8_t seed[32] = {0}, id[32];
    static const uint8_t kat0[32] = {0x1f, 0xcf, 0x56, 0xb5, 0x9e, 0x7c, 0x6b, 0x23,
                                     0x38, 0x08, 0xb7, 0x7e, 0x83, 0xfb, 0x02, 0x54,
                                     0x6f, 0xdc, 0xe4, 0x40, 0x7c, 0x5b, 0x43, 0x97,
                                     0x05, 0xc0, 0xe1, 0x87, 0x25, 0x43, 0xe2, 0xdb};
    kyc_md_token_id(seed, 0, id);
    CHECK(!memcmp(id, kat0, 32), "token id KAT: zero seed, index 0 (Python hashlib)");
    static const uint8_t kat1[32] = {0x6f, 0xa8, 0x53, 0x9d, 0x66, 0x39, 0x2e, 0x7d,
                                     0x03, 0x52, 0x74, 0xac, 0x77, 0x7c, 0xbe, 0x4d,
                                     0x16, 0xa8, 0x02, 0xf8, 0x09, 0x64, 0x37, 0xfc,
                                     0x7c, 0x9c, 0xd7, 0xe9, 0x7f, 0x80, 0x10, 0xf6};
    for (int i = 0; i < 32; i++) seed[i] = (uint8_t) i;
    kyc_md_token_id(seed, 258, id);
    CHECK(!memcmp(id, kat1, 32), "token id KAT: seed 00..1f, index 258 (little-endian index)");
}

static void test_issue(void)
{
    kyc_md_issuer_t is = issuer_of(&ISS, "DE");
    uint8_t pks[4 * KYC_PK_LEN];
    for (int i = 0; i < 4; i++) memcpy(pks + i * KYC_PK_LEN, HA[i].pk, KYC_PK_LEN);
    CHECK(kyc_md_issue_batch(&is, SEED_A, &OVER18, EXP, pks, 4, &TA[0][0]) == KYC_OK,
          "batch A: four over-18 tokens, one fresh holder key each");
    uint8_t pkb[2 * KYC_PK_LEN];
    memcpy(pkb, HB[0].pk, KYC_PK_LEN);
    memcpy(pkb + KYC_PK_LEN, HB[1].pk, KYC_PK_LEN);
    CHECK(kyc_md_issue_batch(&is, SEED_B, &OVER18, EXP + 7, pkb, 2, &TB[0][0]) == KYC_OK,
          "batch B: two tokens expiring a week later");
    uint8_t dup[2 * KYC_PK_LEN], out2[2 * KYC_MD_TOKEN_LEN];
    memcpy(dup, HA[0].pk, KYC_PK_LEN);
    memcpy(dup + KYC_PK_LEN, HA[0].pk, KYC_PK_LEN);
    CHECK(kyc_md_issue_batch(&is, SEED_A, &OVER18, EXP, dup, 2, out2) == KYC_E_ARG,
          "a batch reusing one holder key is refused (it would be linkable)");
    uint8_t one[KYC_MD_TOKEN_LEN];
    CHECK(kyc_md_issue(&is, SEED_A, 0, &OVER18, EXP + 1, HA[0].pk, one) == KYC_E_ARG,
          "expiry not a multiple of 7 days refused");
    CHECK(kyc_md_issue(&is, SEED_A, KYC_MD_BATCH_MAX, &OVER18, EXP, HA[0].pk, one) == KYC_E_ARG,
          "index 256 refused");
    CHECK(kyc_md_issue(&is, SEED_A, 0, &(kyc_pred_t){KYC_PRED_AGE_OVER, 0, {0, 0}}, EXP, HA[0].pk,
                       one) == KYC_E_ARG,
          "invalid predicate refused");
    kyc_md_issuer_t bad = is;
    memcpy(bad.issuer_country, "de", 2);
    CHECK(kyc_md_issue(&bad, SEED_A, 0, &OVER18, EXP, HA[0].pk, one) == KYC_E_ARG,
          "lowercase issuer country refused");
    CHECK(kyc_md_issue_batch(&is, SEED_A, &OVER18, EXP, pks, 0, out2) == KYC_E_ARG &&
              kyc_md_issue_batch(&is, SEED_A, &OVER18, EXP, pks, KYC_MD_BATCH_MAX + 1, out2) ==
                  KYC_E_ARG,
          "batch of 0 or 257 refused");

    /* Contents: two tokens share only population-wide fields. */
    uint8_t id1[32];
    kyc_md_token_id(SEED_A, 1, id1);
    CHECK(!memcmp(TA[0], "ZXVK", 4) && TA[0][4] == 1 && TA[0][5] == KYC_PRED_AGE_OVER &&
              TA[0][6] == 18 && !memcmp(TA[0] + 9, "DE", 2) && !memcmp(TA[1] + 43, id1, 32) &&
              TA[0][75] == (EXP & 0xff) && !memcmp(TA[0] + 79, HA[0].pk, KYC_PK_LEN),
          "token layout: magic, version, predicate, issuer country, token id, expiry, holder key");
    CHECK(!memcmp(TA[0], TA[1], 43) && !memcmp(TA[0] + 75, TA[1] + 75, 4) &&
              memcmp(TA[0] + 43, TA[1] + 43, 32) && memcmp(TA[0] + 79, TA[1] + 79, KYC_PK_LEN),
          "tokens A0 and A1 differ only in token id, holder key (and signature)");

    kyc_md_issuer_t un = issuer_of(&UNTRUSTED, "DE");
    CHECK(kyc_md_issue(&un, SEED_A, 0, &OVER18, EXP, HA[2].pk, T_BAD) == KYC_OK,
          "a token from an issuer nobody trusts");
    kyc_md_issuer_t fr = issuer_of(&ISS_FR, "DE");
    CHECK(kyc_md_issue(&fr, SEED_A, 0, &OVER18, EXP, HA[2].pk, T_FR) == KYC_OK,
          "a token claiming DE from an issuer trusted only for FR");
}

static void test_flow(void)
{
    uint8_t vid1[32], vid2[32], rnd[32];
    memset(vid1, 0xA1, 32);
    memset(vid2, 0xA2, 32);
    static kyc_spent_t sp1[8], sp2[8];
    static kyc_md_verifier_t M1, M2;
    kyc_md_verifier_init(&M1, cpk_verify, &RING, cpk_verify_pk, sp1, 8);
    kyc_md_verifier_init(&M2, cpk_verify, &RING, cpk_verify_pk, sp2, 8);
    kyc_backend_t B1, B2;
    kyc_md_backend(&B1, &M1);
    kyc_md_backend(&B2, &M2);
    CHECK(!B1.zero_knowledge && strstr(B1.name, "NOT zero-knowledge") != NULL,
          "minimal-disclosure backend says it is not zero-knowledge");
    static kyc_verifier_t V1, V2;
    kyc_verifier_init(&V1, vid1, &B1);
    kyc_verifier_init(&V2, vid2, &B2);
    kyc_request_t r1, r2;
    kyc_result_t res;
    memset(rnd, 1, 32);
    CHECK(V1.enabled == false && kyc_request_new(&V1, &OVER18, rnd, T0, &r1) == KYC_E_DISABLED,
          "off by default: request_new is disabled");
    CHECK(kyc_verify(&V1, &r1, P, sizeof P, T0, &res) == KYC_E_DISABLED,
          "off by default: verify is disabled");
    kyc_verifier_enable(&V1, true);
    kyc_verifier_enable(&V2, true);

    /* Revocation lists: an empty one from the issuer. */
    size_t n = revlist(&ISS, 1, NULL, 0, T0 + 365 * DAY);
    CHECK(kyc_md_install_revlist(&M1, RL, n, RLSIG, sizeof RLSIG, T0) == KYC_OK &&
              kyc_md_install_revlist(&M2, RL, n, RLSIG, sizeof RLSIG, T0) == KYC_OK,
          "empty signed revocation list v1 installed at both verifiers");

    kyc_md_slot_t wallet[4];
    for (int i = 0; i < 4; i++) wallet[i] = (kyc_md_slot_t){TA[i], sign_with, &HA[i], false};

    CHECK(kyc_request_new(&V1, &OVER18, rnd, T0, &r1) == KYC_OK &&
              !memcmp(r1.verifier_id, vid1, 32) && r1.issued_at == T0,
          "V1 asks: over 18?");
    CHECK(kyc_md_wallet_present(wallet, 4, &r1, T0, P) == KYC_OK && wallet[0].used &&
              !wallet[1].used,
          "wallet answers with token A0 and marks it used");
    CHECK(kyc_verify(&V1, &r1, P, sizeof P, T0 + 10, &res) == KYC_OK && res.pred.age == 18 &&
              !memcmp(res.issuer_country, "DE", 2) && res.expiry_day == EXP &&
              !memcmp(res.token_id, TA[0] + 43, 32) && M1.n_spent == 1,
          "V1 accepts: over 18, issuer DE, expiry week; token id recorded as spent");
    CHECK(kyc_verify(&V1, &r1, P, sizeof P, T0 + 11, &res) == KYC_E_CHALLENGE,
          "replay of the same presentation: challenge already consumed");
    memset(rnd, 2, 32);
    kyc_request_new(&V1, &OVER18, rnd, T0 + 20, &r1);
    CHECK(kyc_md_present(TA[0], &r1, sign_with, &HA[0], P) == KYC_OK &&
              kyc_verify(&V1, &r1, P, sizeof P, T0 + 21, &res) == KYC_E_SPENT,
          "same token, fresh challenge, same verifier: spent");

    /* V2: a different verifier gets a different token. */
    memset(rnd, 3, 32);
    kyc_request_new(&V2, &OVER18, rnd, T0, &r2);
    CHECK(kyc_md_wallet_present(wallet, 4, &r2, T0, P2) == KYC_OK && wallet[1].used,
          "wallet answers V2 with token A1");
    CHECK(kyc_verify(&V2, &r2, P2, sizeof P2, T0, &res) == KYC_OK &&
              !memcmp(res.token_id, TA[1] + 43, 32),
          "V2 accepts token A1");
    CHECK(!memcmp(P, P2, 43) && memcmp(P + 43, P2 + 43, 32) && memcmp(P + 79, P2 + 79, KYC_PK_LEN),
          "V1's and V2's presentations share no per-person field (only issuer, predicate, week)");

    /* Binding: a presentation made for V1 does not work at V2. */
    memset(rnd, 4, 32);
    kyc_request_new(&V1, &OVER18, rnd, T0 + 30, &r1);
    kyc_md_present(TA[2], &r1, sign_with, &HA[2], P);
    CHECK(kyc_verify(&V2, &r1, P, sizeof P, T0 + 31, &res) == KYC_E_CHALLENGE,
          "V1's request replayed at V2: V2 never issued that challenge");
    memset(rnd, 5, 32);
    kyc_request_new(&V2, &OVER18, rnd, T0 + 30, &r2);
    CHECK(kyc_verify(&V2, &r2, P, sizeof P, T0 + 31, &res) == KYC_E_HOLDER,
          "V1's presentation under V2's challenge: holder signature fails");

    /* Predicates must match exactly. */
    memset(rnd, 6, 32);
    kyc_request_t r21;
    kyc_request_new(&V1, &OVER21, rnd, T0 + 40, &r21);
    CHECK(kyc_md_wallet_present(wallet, 4, &r21, T0, P) == KYC_E_FULL,
          "wallet has no over-21 token: nothing presented");
    CHECK(kyc_md_present(TA[2], &r21, sign_with, &HA[2], P) == KYC_E_PREDICATE,
          "holder refuses to present over-18 for over-21");
    kyc_request_t fake = r21;
    fake.pred = OVER18;
    kyc_md_present(TA[2], &fake, sign_with, &HA[2], P);
    CHECK(kyc_verify(&V1, &r21, P, sizeof P, T0 + 41, &res) == KYC_E_PREDICATE,
          "verifier refuses an over-18 token for an over-21 question");
    kyc_request_t r16;
    memset(rnd, 7, 32);
    kyc_request_new(&V1, &(kyc_pred_t){KYC_PRED_AGE_OVER, 16, {0, 0}}, rnd, T0 + 42, &r16);
    fake = r16;
    fake.pred = OVER18;
    kyc_md_present(TA[2], &fake, sign_with, &HA[2], P);
    CHECK(kyc_verify(&V1, &r16, P, sizeof P, T0 + 43, &res) == KYC_E_PREDICATE,
          "an over-18 token does not answer over-16 (it would disclose more than asked)");

    /* Tampering. Failures never spend the token. */
    memset(rnd, 8, 32);
    kyc_request_new(&V1, &OVER21, rnd, T0 + 50, &r21);
    memcpy(P, TA[2], KYC_MD_TOKEN_LEN);
    P[6] = 21; /* edit the age in the token */
    kyc_md_present(P, &r21, sign_with, &HA[2], P);
    CHECK(kyc_verify(&V1, &r21, P, sizeof P, T0 + 51, &res) == KYC_E_ISSUER,
          "token edited from 18 to 21: issuer signature fails");
    memset(rnd, 9, 32);
    kyc_request_new(&V1, &OVER18, rnd, T0 + 60, &r1);
    kyc_md_present(TA[2], &r1, sign_with, &HA[2], P);
    P[KYC_MD_PRESENT_LEN - 1] ^= 1;
    CHECK(kyc_verify(&V1, &r1, P, sizeof P, T0 + 61, &res) == KYC_E_HOLDER,
          "flipped bit in the holder signature");
    memset(rnd, 10, 32);
    kyc_request_new(&V1, &OVER18, rnd, T0 + 62, &r1);
    kyc_md_present(TA[2], &r1, sign_with, &HA[3], P);
    CHECK(kyc_verify(&V1, &r1, P, sizeof P, T0 + 63, &res) == KYC_E_HOLDER,
          "signed with another token's holder key (a stolen token without its key)");
    memset(rnd, 11, 32);
    kyc_request_new(&V1, &OVER18, rnd, T0 + 64, &r1);
    kyc_md_present(TA[2], &r1, sign_with, &HA[2], P);
    CHECK(kyc_verify(&V1, &r1, P, sizeof P - 1, T0 + 65, &res) == KYC_E_DECODE,
          "one byte short: decode error");
    kyc_request_new(&V1, &OVER18, rnd, T0 + 66, &r1);
    P[0] = 'Y';
    CHECK(kyc_verify(&V1, &r1, P, sizeof P, T0 + 67, &res) == KYC_E_DECODE, "bad magic");
    memset(rnd, 12, 32);
    kyc_request_new(&V1, &OVER18, rnd, T0 + 70, &r1);
    kyc_md_present(T_BAD, &r1, sign_with, &HA[2], P);
    CHECK(kyc_verify(&V1, &r1, P, sizeof P, T0 + 71, &res) == KYC_E_ISSUER,
          "token from an untrusted issuer");
    memset(rnd, 13, 32);
    kyc_request_new(&V1, &OVER18, rnd, T0 + 72, &r1);
    kyc_md_present(T_FR, &r1, sign_with, &HA[2], P);
    CHECK(kyc_verify(&V1, &r1, P, sizeof P, T0 + 73, &res) == KYC_E_ISSUER,
          "issuer trusted for FR cannot issue as DE");
    CHECK(M1.n_spent == 1, "no failed presentation was recorded as spent");

    /* Expiry: the expiry day itself is fine, the day after is not. */
    uint64_t last = (uint64_t) EXP * DAY + DAY - 1, after = (uint64_t) (EXP + 1) * DAY;
    memset(rnd, 14, 32);
    kyc_request_new(&V1, &OVER18, rnd, after, &r1);
    kyc_md_present(TA[3], &r1, sign_with, &HA[3], P);
    CHECK(kyc_verify(&V1, &r1, P, sizeof P, after, &res) == KYC_E_EXPIRED,
          "first second after the expiry day: expired");
    kyc_request_new(&V1, &OVER18, rnd, last, &r1);
    kyc_md_present(TA[2], &r1, sign_with, &HA[2], P);
    CHECK(kyc_verify(&V1, &r1, P, sizeof P, last, &res) == KYC_OK,
          "last second of the expiry day: accepted (token A2)");

    /* D6 revocation. */
    n = revlist(&ISS, 2, SEED_A, 4, T0 + 365 * DAY);
    uint8_t bad[KYC_MD_REVLIST_MAX];
    memcpy(bad, RL, n);
    bad[CP_HDR_LEN + 2] ^= 1;
    CHECK(kyc_md_install_revlist(&M1, bad, n, RLSIG, sizeof RLSIG, T0) == KYC_E_SIG,
          "tampered revocation list: signature fails");
    CHECK(kyc_md_install_revlist(&M1, RL, n, RLSIG, sizeof RLSIG, T0 - 1) == KYC_E_WINDOW,
          "revocation list before its window");
    CHECK(kyc_md_install_revlist(&M1, RL, n, RLSIG, sizeof RLSIG, T0) == KYC_OK,
          "revocation list v2 (batch A seed, 4 tokens) installed");
    CHECK(kyc_md_install_revlist(&M1, RL, n, RLSIG, sizeof RLSIG, T0) == KYC_E_VERSION,
          "same revocation list version again refused");
    memset(rnd, 15, 32);
    kyc_request_new(&V1, &OVER18, rnd, T0 + 80, &r1);
    kyc_md_present(TA[3], &r1, sign_with, &HA[3], P);
    CHECK(kyc_verify(&V1, &r1, P, sizeof P, T0 + 81, &res) == KYC_E_REVOKED,
          "token of a revoked batch refused");
    memset(rnd, 16, 32);
    kyc_request_new(&V1, &OVER18, rnd, T0 + 82, &r1);
    kyc_md_present(TB[0], &r1, sign_with, &HB[0], P);
    CHECK(kyc_verify(&V1, &r1, P, sizeof P, T0 + 83, &res) == KYC_OK,
          "token of another batch still accepted");
    n = revlist(&HA[0], 3, NULL, 0, T0 + 365 * DAY);
    CHECK(kyc_md_install_revlist(&M1, RL, n, RLSIG, sizeof RLSIG, T0) == KYC_E_SIG,
          "revocation list signed by a holder key, not the issuer: refused");
    memset(rnd, 17, 32);
    kyc_request_new(&V1, &OVER18, rnd, T0 + 366 * DAY, &r1);
    kyc_md_present(TB[1], &r1, sign_with, &HB[1], P);
    CHECK(kyc_verify(&V1, &r1, P, sizeof P, T0 + 366 * DAY, &res) == KYC_E_EXPIRED,
          "(token B1 is past expiry a year later)");

    /* No current revocation list: fail closed, unless explicitly relaxed. */
    static kyc_spent_t sp3[1];
    static kyc_md_verifier_t M3;
    kyc_md_verifier_init(&M3, cpk_verify, &RING, cpk_verify_pk, sp3, 1);
    CHECK(M3.require_revlist == true, "require_revlist defaults to true");
    kyc_backend_t B3;
    kyc_md_backend(&B3, &M3);
    static kyc_verifier_t V3;
    kyc_verifier_init(&V3, vid1, &B3);
    kyc_verifier_enable(&V3, true);
    memset(rnd, 18, 32);
    kyc_request_new(&V3, &OVER18, rnd, T0, &r1);
    kyc_md_present(TA[3], &r1, sign_with, &HA[3], P);
    CHECK(kyc_verify(&V3, &r1, P, sizeof P, T0, &res) == KYC_E_REVLIST,
          "no revocation list for the issuer: refused");
    n = revlist(&ISS, 1, NULL, 0, T0 + DAY);
    kyc_md_install_revlist(&M3, RL, n, RLSIG, sizeof RLSIG, T0);
    kyc_request_new(&V3, &OVER18, rnd, T0 + 2 * DAY, &r1);
    kyc_md_present(TA[3], &r1, sign_with, &HA[3], P);
    CHECK(kyc_verify(&V3, &r1, P, sizeof P, T0 + 2 * DAY, &res) == KYC_E_REVLIST,
          "revocation list past its not_after: refused");
    M3.require_revlist = false;
    kyc_request_new(&V3, &OVER18, rnd, T0 + 2 * DAY, &r1);
    kyc_md_present(TA[3], &r1, sign_with, &HA[3], P);
    CHECK(kyc_verify(&V3, &r1, P, sizeof P, T0 + 2 * DAY, &res) == KYC_OK,
          "require_revlist off (operator choice): accepted, A3 spent here");

    /* D5 spent set full (capacity 1). */
    memset(rnd, 19, 32);
    kyc_request_new(&V3, &OVER18, rnd, T0, &r1);
    kyc_md_present(TB[0], &r1, sign_with, &HB[0], P);
    CHECK(kyc_verify(&V3, &r1, P, sizeof P, T0, &res) == KYC_E_SPENT_FULL && M3.n_spent == 1,
          "spent set full of unexpired tokens: refused, not accepted unrecorded");
    kyc_request_new(&V3, &OVER18, rnd, after, &r1);
    kyc_md_present(TB[0], &r1, sign_with, &HB[0], P);
    CHECK(kyc_verify(&V3, &r1, P, sizeof P, after, &res) == KYC_OK && M3.n_spent == 1 &&
              !memcmp(M3.spent[0].token_id, TB[0] + 43, 32),
          "after A3 expires its entry is dropped and B0 fits");
}

static void test_more(void)
{
    static kyc_spent_t sp[4];
    static kyc_md_verifier_t M4;
    kyc_md_verifier_init(&M4, cpk_verify, &RING, cpk_verify_pk, sp, 4);
    M4.require_revlist = false;
    kyc_backend_t B4;
    kyc_md_backend(&B4, &M4);
    kyc_result_t res;
    kyc_request_t rq;
    memset(&rq, 0, sizeof rq);
    rq.pred = OVER18;
    memset(rq.verifier_id, 0xB1, 32);
    memset(rq.challenge, 0x77, 32);
    rq.issued_at = T0;

    /* D3: an expiry that is not a whole week is refused even when the
     * issuer signed it. */
    uint8_t odd[KYC_MD_TOKEN_LEN];
    memcpy(odd, TA[2], sizeof odd);
    odd[75] = (uint8_t) ((EXP + 1) & 0xff);
    uint8_t msg[19 + 32];
    memcpy(msg, "zxv/kyc-md-token/v1", 19);
    sha256(odd, KYC_MD_BODY_LEN, msg + 19);
    cpk_sign(ISS.sk, msg, sizeof msg, NULL, odd + KYC_MD_BODY_LEN);
    CHECK(kyc_md_present(odd, &rq, sign_with, &HA[2], P) == KYC_E_DECODE,
          "holder refuses a token whose expiry is not a whole week");
    memcpy(P, odd, KYC_MD_TOKEN_LEN);
    CHECK(B4.verify(B4.ctx, &rq, P, sizeof P, T0, &res) == KYC_E_DECODE,
          "verifier refuses it too, although the issuer signature is valid");

    /* The holder signature covers the verifier id (backend level, without
     * the front's challenge table). */
    CHECK(kyc_md_present(TB[1], &rq, sign_with, &HB[1], P) == KYC_OK, "B1 presented for 0xB1..");
    kyc_request_t other = rq;
    memset(other.verifier_id, 0xB2, 32);
    CHECK(B4.verify(B4.ctx, &other, P, sizeof P, T0, &res) == KYC_E_HOLDER,
          "same challenge, other verifier id: holder signature fails");
    other = rq;
    other.challenge[31] ^= 1;
    CHECK(B4.verify(B4.ctx, &other, P, sizeof P, T0, &res) == KYC_E_HOLDER,
          "other challenge: holder signature fails");
    CHECK(B4.verify(B4.ctx, &rq, P, sizeof P, T0, &res) == KYC_OK, "the right request passes");

    /* Resident-of predicate. */
    kyc_md_issuer_t is = issuer_of(&ISS, "DE");
    uint8_t seed_c[32], tok[KYC_MD_TOKEN_LEN];
    memset(seed_c, 0x5C, 32);
    kyc_pred_t de = {KYC_PRED_RESIDENT, 0, {'D', 'E'}}, frp = {KYC_PRED_RESIDENT, 0, {'F', 'R'}};
    CHECK(kyc_md_issue(&is, seed_c, 0, &de, EXP, HA[1].pk, tok) == KYC_OK,
          "issue a resident-of-DE token");
    rq.pred = de;
    CHECK(kyc_md_present(tok, &rq, sign_with, &HA[1], P) == KYC_OK &&
              B4.verify(B4.ctx, &rq, P, sizeof P, T0, &res) == KYC_OK &&
              res.pred.kind == KYC_PRED_RESIDENT && !memcmp(res.pred.country, "DE", 2),
          "resident of DE: accepted");
    rq.pred = frp;
    rq.challenge[0] ^= 1;
    kyc_request_t fake = rq;
    fake.pred = de;
    kyc_md_present(tok, &fake, sign_with, &HA[1], P);
    CHECK(B4.verify(B4.ctx, &rq, P, sizeof P, T0, &res) == KYC_E_PREDICATE,
          "resident-of-DE token for a resident-of-FR question: refused");

    /* D5 wallet skips expired tokens. */
    kyc_md_slot_t w[2] = {{TA[3], sign_with, &HA[3], false}, {TB[1], sign_with, &HB[1], false}};
    rq.pred = OVER18;
    uint64_t after = (uint64_t) (EXP + 1) * DAY;
    CHECK(kyc_md_wallet_present(w, 2, &rq, after, P) == KYC_OK && !w[0].used && w[1].used &&
              !memcmp(P, TB[1], KYC_MD_TOKEN_LEN),
          "wallet skips an expired token and uses the next one");
    CHECK(kyc_md_wallet_present(w, 2, &rq, after, P) == KYC_E_FULL,
          "wallet with only an expired token left: nothing presented");
}

static void test_front(void)
{
    uint8_t vid[32], rnd[32], p[8] = {0};
    memset(vid, 0xC3, 32);
    kyc_backend_t zk;
    kyc_zk_backend(&zk);
    static kyc_verifier_t V;
    kyc_verifier_init(&V, vid, &zk);
    kyc_verifier_enable(&V, true);
    kyc_request_t r, q[KYC_MAX_PENDING];
    kyc_result_t res;
    memset(rnd, 1, 32);
    CHECK(kyc_request_new(&V, &OVER18, rnd, T0, &r) == KYC_OK &&
              kyc_verify(&V, &r, p, sizeof p, T0, &res) == KYC_E_NOT_AVAILABLE,
          "ZK backend: NOT_AVAILABLE (no vetted ZK library yet)");
    CHECK(strstr(zk.name, "not available") != NULL, "ZK backend name says not available");
    CHECK(kyc_request_new(&V, &(kyc_pred_t){5, 0, {0, 0}}, rnd, T0, &r) == KYC_E_ARG,
          "request with an invalid predicate refused");

    bool all = true;
    for (uint32_t i = 0; i < KYC_MAX_PENDING; i++) {
        rnd[0] = (uint8_t) i;
        if (kyc_request_new(&V, &OVER18, rnd, T0 + 100, &q[i]) != KYC_OK) all = false;
    }
    CHECK(all, "16 outstanding challenges");
    CHECK(kyc_request_new(&V, &OVER18, rnd, T0 + 100 + KYC_CHALLENGE_TTL, &r) == KYC_E_FULL,
          "17th while all are live: full");
    CHECK(kyc_verify(&V, &q[0], p, sizeof p, T0 + 100 + KYC_CHALLENGE_TTL, &res) ==
              KYC_E_NOT_AVAILABLE,
          "a challenge exactly TTL seconds old still reaches the backend");
    CHECK(kyc_verify(&V, &q[1], p, sizeof p, T0 + 101 + KYC_CHALLENGE_TTL, &res) == KYC_E_CHALLENGE,
          "a challenge TTL+1 seconds old is refused");
    CHECK(kyc_verify(&V, &q[1], p, sizeof p, T0 + 100, &res) == KYC_E_CHALLENGE,
          "and it was consumed by the refused attempt");
    kyc_request_t fake = q[2];
    fake.challenge[5] ^= 1;
    CHECK(kyc_verify(&V, &fake, p, sizeof p, T0 + 100, &res) == KYC_E_CHALLENGE,
          "a challenge the verifier never issued");
    fake = q[2];
    fake.pred = OVER21;
    CHECK(kyc_verify(&V, &fake, p, sizeof p, T0 + 100, &res) == KYC_E_CHALLENGE,
          "a recorded challenge with a different predicate");
    CHECK(kyc_request_new(&V, &OVER18, rnd, T0 + 101 + KYC_CHALLENGE_TTL, &r) == KYC_OK,
          "expired challenges free their slots");
    kyc_verifier_t none;
    kyc_verifier_init(&none, vid, NULL);
    kyc_verifier_enable(&none, true);
    kyc_request_new(&none, &OVER18, rnd, T0, &r);
    CHECK(kyc_verify(&none, &r, p, sizeof p, T0, &res) == KYC_E_NOT_AVAILABLE,
          "no backend: not available");
    CHECK(!strcmp(kyc_status_name(KYC_E_SPENT_FULL), "spent_set_full") &&
              !strcmp(kyc_status_name((kyc_status_t) 42), "unknown"),
          "status names");
}

int main(void)
{
    printf("=== test_kycwrap: KYC wrapper (ZK slot + minimal disclosure, NOT ZK) ===\n");
    kp(&ISS, 21);
    kp(&UNTRUSTED, 22);
    kp(&ISS_FR, 23);
    for (int i = 0; i < 4; i++) kp(&HA[i], (uint8_t) (31 + i));
    for (int i = 0; i < 2; i++) kp(&HB[i], (uint8_t) (41 + i));
    memset(SEED_A, 0x5A, 32);
    memset(SEED_B, 0x5B, 32);
    cpk_init(&RING);
    CHECK(cpk_add(&RING, ISS.pk, (1u << CP_KIND_KYC_TOKEN) | (1u << CP_KIND_KYC_REVOKE), "DE") &&
              cpk_add(&RING, ISS_FR.pk, 1u << CP_KIND_KYC_TOKEN, "FR"),
          "keyring: DE issuer (tokens and revocation lists), FR issuer (tokens)");
    test_basics();
    test_issue();
    test_flow();
    test_more();
    test_front();
    return cp_test_done("test_kycwrap");
}
