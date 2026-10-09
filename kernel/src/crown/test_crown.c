/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_crown.c — anchors the Crown pillar to EXTERNAL truths:
 *   (1) the anti-capture separation (crown.c contains no money-moving symbol)
 *       is verified authoritatively by the dedicated step in `make verify-all`,
 *       which greps this source AND the compiled object — not by this unit test.
 *   (2) a REAL Ed25519 signature (produced offline, embedded as a KAT vector)
 *       verifies via crown_isc_verify, and any one-byte tamper makes it fail.
 *   (3) crown_register reaches CROWN_REG_ACTIVE with the requested cap bits.
 *   (4) crown_revoke REFUSES non-payment (Grace of Somalia) and ACCEPTS a
 *       Return-Doctrine / Maxim ground — composing onepolicy.
 *   (5) crown_chn_register accepts a Crown form and refuses a Ministry form.
 */
#define TEST_HOST 1
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "crown.h"
#include "sha256.h"
#include "ed25519_verify.h"
#include "onepolicy.h"   /* to anchor (4) against onepolicy's stated behaviour */

static int g_asserts = 0;
#define CHECK(cond, msg) do { \
    g_asserts++; \
    if (!(cond)) { printf("  [FAIL] %s\n", msg); failures++; } \
    else         { printf("  [ok]   %s\n", msg); } \
} while (0)

/* ===== KAT vector: a genuine Ed25519 signature over sha256(ISC preimage),
 * produced by an INDEPENDENT offline signer (scratchpad/offline_sign.c) using
 * the orlp/ed25519 reference and a fixed seed. This is an external anchor: the
 * kernel's verifier never produced these bytes. preimage_len was 90. ===== */
static const uint8_t TEST_CROWN_PUBKEY[32] = {
    0xff,0xb3,0xb5,0x7c,0x2f,0x8b,0xd3,0xbb,
    0x1f,0xc0,0x72,0x03,0x7f,0xbf,0x02,0xd1,
    0xa0,0x91,0xc3,0xa9,0xff,0x80,0xd2,0x37,
    0x15,0xff,0x5a,0xcd,0x74,0x3c,0x27,0x8d,
};
static const uint8_t TEST_CROWN_SIG[64] = {
    0x79,0x5d,0x72,0xd4,0x50,0x8b,0x3c,0x5b,
    0xd0,0x2d,0x64,0x79,0x0f,0x1d,0x37,0xbd,
    0x43,0xbc,0x08,0x1c,0x69,0x07,0xf7,0x95,
    0x0c,0x41,0x0a,0x24,0x98,0x49,0x12,0x0d,
    0x76,0x81,0x70,0xe2,0xa3,0x0b,0x42,0x0f,
    0x7e,0x3d,0x68,0x06,0x35,0xb6,0x38,0x0a,
    0xe1,0x1b,0xa1,0xf9,0xb4,0x14,0x9f,0x96,
    0x64,0x1a,0xb3,0x7d,0xfb,0x71,0xdf,0x09,
};

/* Reconstruct the EXACT ISC the offline signer signed. */
static void build_signed_isc(crown_isc_t *c) {
    memset(c, 0, sizeof(*c));
    for (int i = 0; i < 32; i++) c->subject[i] = (uint8_t)(0xA0 + i);
    c->issue_epoch = 1;
    c->capability_bits = CROWN_CAP_GRIDCHAIN | CROWN_CAP_COMMONS_TRUST | CROWN_CAP_PS233;
    c->maxim_ok = 1;
    c->no_harm = 1;
}

int main(void) {
    int failures = 0;

    printf("== (2) ISC signature: real KAT verify + tamper detection ==\n");
    {
        crown_isc_t c;
        build_signed_isc(&c);
        /* preimage length must be exactly what the signer used. */
        uint8_t pre[CROWN_ISC_PREIMAGE_LEN];
        uint32_t n = crown_isc_build_preimage(&c, pre);
        CHECK(n == 90 && n == CROWN_ISC_PREIMAGE_LEN, "preimage length is 90 (matches offline signer)");

        crown_isc_attach_signature(&c, TEST_CROWN_SIG);
        CHECK(crown_isc_verify(&c, TEST_CROWN_PUBKEY) == true,
              "genuine offline signature verifies");

        /* Tamper a SIGNED field (capability_bits) -> must fail. */
        crown_isc_t t1 = c;
        t1.capability_bits ^= CROWN_CAP_PEACEKEEPING;
        CHECK(crown_isc_verify(&t1, TEST_CROWN_PUBKEY) == false,
              "flipping a capability bit breaks verification");

        /* Tamper the subject -> must fail. */
        crown_isc_t t2 = c;
        t2.subject[0] ^= 0x01;
        CHECK(crown_isc_verify(&t2, TEST_CROWN_PUBKEY) == false,
              "flipping one subject byte breaks verification");

        /* Tamper the signature itself -> must fail. */
        crown_isc_t t3 = c;
        t3.sig[10] ^= 0x01;
        CHECK(crown_isc_verify(&t3, TEST_CROWN_PUBKEY) == false,
              "flipping one signature byte breaks verification");

        /* Wrong key -> must fail. */
        uint8_t badkey[32];
        memcpy(badkey, TEST_CROWN_PUBKEY, 32);
        badkey[0] ^= 0x01;
        CHECK(crown_isc_verify(&c, badkey) == false,
              "verification fails under the wrong public key");

        /* revoked/revoke_reason are OUTSIDE the signed preimage: flipping them
         * must NOT break the signature (revocation is a registry act, not a re-sign). */
        crown_isc_t t4 = c;
        t4.revoked = 1; t4.revoke_reason = CROWN_REVOKE_FRAUD_ROOT;
        CHECK(crown_isc_verify(&t4, TEST_CROWN_PUBKEY) == true,
              "mutable revoked-status is not part of the signed preimage");
    }

    printf("== (3) crown_register reaches ACTIVE with requested cap bits ==\n");
    {
        crown_t cr;
        crown_init(&cr);
        uint8_t subj[32];
        for (int i = 0; i < 32; i++) subj[i] = (uint8_t)(i * 7 + 1);
        uint32_t caps = CROWN_CAP_DIPLOMATIC | CROWN_CAP_PAYITFORWARD | CROWN_CAP_CAPREGISTER;
        crown_isc_t out;
        int32_t rc = crown_register(&cr, subj, CROWN_ID_WITNESS, caps, &out);
        CHECK(rc == CROWN_OK, "crown_register returns CROWN_OK");
        CHECK(cr.last_stage == CROWN_REG_ACTIVE, "FSM reached CROWN_REG_ACTIVE");
        CHECK(out.capability_bits == caps, "requested capability bits are set exactly");
        CHECK(out.maxim_ok == 1 && out.no_harm == 1, "maxim_ok and no_harm attested");
        CHECK(out.issue_epoch == 1, "first issue got epoch 1");
        CHECK(out.revoked == 0, "freshly issued credential is not revoked");
        /* Honest: crown_register does NOT sign (no private key in kernel). */
        int sig_all_zero = 1;
        for (int i = 0; i < 64; i++) if (out.sig[i]) sig_all_zero = 0;
        CHECK(sig_all_zero, "issued credential is UNSIGNED (offline signing, no key in kernel)");
        /* No passport: an out-of-range method is the only rejection. */
        crown_isc_t out2;
        CHECK(crown_register(&cr, subj, (crown_id_method_t)99, caps, &out2) == CROWN_ERR_ARG,
              "an unknown id method is rejected (but no passport is ever required)");
    }

    printf("== (4) crown_revoke: refuses non-payment, accepts maxim/RD grounds ==\n");
    {
        crown_t cr; crown_init(&cr);
        crown_isc_t c; build_signed_isc(&c);

        /* Prove the composed predicate directly: a non-payment term FAILS the
         * Grace of Somalia gate (external anchor: onepolicy's stated behaviour). */
        op_term_t nonpay;
        memset(&nonpay, 0, sizeof(nonpay));
        nonpay.reciprocal = true;
        nonpay.revoke_for_nonpayment = true;
        CHECK(op_grace_of_somalia_ok(&nonpay) == false,
              "onepolicy: a revoke-for-non-payment term fails Grace of Somalia");

        /* crown_revoke REFUSES non-payment... */
        int32_t r_np = crown_revoke(&cr, &c, CROWN_REVOKE_NONPAYMENT);
        CHECK(r_np == CROWN_ERR_COERCION, "crown_revoke REFUSES non-payment (coercion)");
        CHECK(c.revoked == 0, "credential remains valid after a refused revocation");

        /* ...and other coercive grounds too. */
        CHECK(crown_revoke(&cr, &c, CROWN_REVOKE_KILL_SWITCH) == CROWN_ERR_COERCION,
              "crown_revoke REFUSES a kill-switch ground");
        CHECK(crown_revoke(&cr, &c, CROWN_REVOKE_DENY_AID) == CROWN_ERR_COERCION,
              "crown_revoke REFUSES a deny-aid ground");
        CHECK(c.revoked == 0, "still valid after all coercive attempts");

        /* A no-ground request (NONE) is refused, but not as coercion. */
        CHECK(crown_revoke(&cr, &c, CROWN_REVOKE_NONE) == CROWN_ERR_NO_GROUND,
              "crown_revoke REFUSES an empty ground");

        /* ACCEPTS a Return-Doctrine (fraud root) ground. Attach the genuine offline
         * signature first so we can prove verify-vs-active AFTER revocation. */
        crown_isc_t cf; build_signed_isc(&cf);
        crown_isc_attach_signature(&cf, TEST_CROWN_SIG);
        CHECK(crown_isc_verify(&cf, TEST_CROWN_PUBKEY) == true,
              "cf is genuinely signed before revocation");
        CHECK(crown_revoke(&cr, &cf, CROWN_REVOKE_FRAUD_ROOT) == CROWN_OK,
              "crown_revoke ACCEPTS a Return-Doctrine (fraud) ground");
        CHECK(cf.revoked == 1 && cf.revoke_reason == CROWN_REVOKE_FRAUD_ROOT,
              "revoked flag and reason recorded on fraud ground");
        /* The revoked credential STILL passes signature-only verify (the flag is
         * outside the preimage) — but crown_isc_is_active() must reject it. This is
         * the access gate: a revoked ISC authorises NOTHING. */
        CHECK(crown_isc_verify(&cf, TEST_CROWN_PUBKEY) == true,
              "a revoked credential still VERIFIES (signature-only, flag not signed)");
        CHECK(crown_isc_is_active(&cf, TEST_CROWN_PUBKEY) == false,
              "but crown_isc_is_active REJECTS it — revoked authorises nothing");

        /* ACCEPTS a Symbiotic-Maxim (asymmetric harm) ground. */
        crown_isc_t ch; build_signed_isc(&ch);
        CHECK(crown_revoke(&cr, &ch, CROWN_REVOKE_MAXIM_HARM) == CROWN_OK,
              "crown_revoke ACCEPTS a Symbiotic-Maxim (harm) ground");
        CHECK(ch.revoked == 1, "revoked flag set on maxim-harm ground");
    }

    printf("== (5) crown_chn_register: Crown forms in, Ministry forms out ==\n");
    {
        crown_t cr; crown_init(&cr);
        uint8_t subj[32], dig[32];
        for (int i = 0; i < 32; i++) { subj[i] = (uint8_t)i; dig[i] = (uint8_t)(255 - i); }

        CHECK(crown_chn_register(&cr, subj, ZCAP_CULTURAL, dig) == CROWN_OK,
              "accepts ZCAP_CULTURAL (a Crown form)");
        CHECK(crown_chn_register(&cr, subj, ZCAP_SPIRITUAL, dig) == CROWN_OK,
              "accepts ZCAP_SPIRITUAL (a Crown form)");
        CHECK(crown_chn_register(&cr, subj, ZCAP_NATURAL, dig) == CROWN_OK,
              "accepts ZCAP_NATURAL (a Crown form)");
        CHECK(crown_chn_register(&cr, subj, ZCAP_SOCIAL, dig) == CROWN_OK,
              "accepts ZCAP_SOCIAL (a Crown form)");

        CHECK(crown_chn_register(&cr, subj, ZCAP_FINANCIAL, dig) == CROWN_ERR_NOT_CROWN,
              "REFUSES ZCAP_FINANCIAL (a Ministry form)");
        CHECK(crown_chn_register(&cr, subj, ZCAP_INTELLECTUAL, dig) == CROWN_ERR_NOT_CROWN,
              "REFUSES ZCAP_INTELLECTUAL (a Ministry form)");
        CHECK(crown_chn_register(&cr, subj, ZCAP_SYSTEM, dig) == CROWN_ERR_NOT_CROWN,
              "REFUSES ZCAP_SYSTEM (Co-Juris, not a Crown form)");

        CHECK(cr.recognition_count == 4, "exactly the four Crown-form notes were recorded");
        /* A recognition is not ownership: the record type has no balance/price field. */
    }

    printf("\n%s  (%d assertions)\n",
           failures == 0 ? "[PASS] test_crown ALL GREEN" : "[FAIL] test_crown",
           g_asserts);
    return failures == 0 ? 0 : 1;
}
