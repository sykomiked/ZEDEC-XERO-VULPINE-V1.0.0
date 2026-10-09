/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* crown.c — the Crown pillar. Issues sovereign credentials, verifies Crown
 * signatures, recognises the four inalienable forms of capital.
 *
 * ANTI-CAPTURE: this file includes NO ministry/finance/ledger/wallet header and
 * references NO money-moving symbol. It composes exactly three things — The One
 * Policy (to keep revocation non-coercive), SHA-256, and the Ed25519 verifier.
 * A passport office cannot reach into your wallet; neither can this .c, because
 * it does not even know where the wallet lives.
 */
#include "crown.h"
#include "onepolicy.h"          /* revocation must pass the Grace of Somalia      */
#include "sha256.h"             /* preimage compression                           */
#include "ed25519_verify.h"     /* public-key verification (NO private key here)  */

/* Local, libc-free byte helpers so this file needs no <string.h> in either the
 * host or the freestanding build. Integer-only, as the house rules demand. */
static void crown_memset(uint8_t *d, uint8_t v, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) d[i] = v;
}
static void crown_memcpy(uint8_t *d, const uint8_t *s, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

void crown_init(crown_t *cr) {
    if (!cr) return;
    cr->epoch = 1;              /* epoch 0 reserved for "never issued"            */
    cr->issued_count = 0;
    cr->last_stage = CROWN_REG_APPLY;
    cr->recognition_count = 0;
    for (uint32_t i = 0; i < CROWN_MAX_RECOGNITIONS; i++) {
        crown_memset(cr->recognitions[i].subject, 0, 32);
        crown_memset(cr->recognitions[i].digest, 0, 32);
        cr->recognitions[i].form = ZCAP_FINANCIAL;
        cr->recognitions[i].used = 0;
    }
}

bool crown_form_is_crown(zcap_form_t form) {
    /* The four inalienable forms — identical to the set for which
     * zcap_who_owns_this(form) == ZCAP_AUTH_CROWN and zcap_is_priceable(form)
     * is false. Classified locally so we need not link the capital engine. */
    return form == ZCAP_SOCIAL || form == ZCAP_NATURAL ||
           form == ZCAP_CULTURAL || form == ZCAP_SPIRITUAL;
}

int32_t crown_register(crown_t *cr, const uint8_t subject[32],
                       crown_id_method_t method, uint32_t capability_bits,
                       crown_isc_t *out) {
    if (!cr || !subject || !out) return CROWN_ERR_ARG;
    if (method != CROWN_ID_DID && method != CROWN_ID_BIOMETRIC_HASH &&
        method != CROWN_ID_WITNESS)
        return CROWN_ERR_ARG;   /* no passport required, but SOME method must be named */

    /* Step 1: APPLY — really just "the OS concedes you exist". */
    crown_reg_stage_t stage = CROWN_REG_APPLY;

    /* Step 2: VERIFY_ID — any of the three methods suffices; none is a passport. */
    stage = CROWN_REG_VERIFY_ID;

    /* Step 3: CREDENTIAL — mint the (as-yet unsigned) record. */
    stage = CROWN_REG_CREDENTIAL;
    crown_memset((uint8_t*)out, 0, (uint32_t)sizeof(*out));
    crown_memcpy(out->subject, subject, 32);
    out->issue_epoch     = cr->epoch;
    out->capability_bits = capability_bits;
    out->maxim_ok        = 1;   /* the Crown attests conformance to the Maxim     */
    out->no_harm         = 1;   /* ...and the no-harm covenant                    */
    /* gridchain_anchor left ZERO: binding to a real GridChain block is an OPS
     * BOUNDARY (needs the chain). We do not fabricate an inclusion proof. */
    out->revoked = 0;
    out->revoke_reason = CROWN_REVOKE_NONE;
    /* sig left ZERO: the Crown holds no private key. Signed OFFLINE, attached later. */

    /* Step 4: FINANCIAL_STUB — Ministry-side, OPAQUE to the Crown. We step over
     * it. We do not know, and cannot perform, whatever happens in there. */
    stage = CROWN_REG_FINANCIAL_STUB;

    /* Step 5: ACTIVATE. */
    stage = CROWN_REG_ACTIVATE;

    /* Step 6: OVERSIGHT_STUB — also Ministry-side and opaque. Stepped over. */
    stage = CROWN_REG_OVERSIGHT_STUB;

    /* Step 7: ACTIVE. */
    stage = CROWN_REG_ACTIVE;

    cr->last_stage = stage;
    cr->epoch++;
    cr->issued_count++;
    return CROWN_OK;
}

void crown_isc_attach_signature(crown_isc_t *c, const uint8_t sig[64]) {
    if (!c || !sig) return;
    crown_memcpy(c->sig, sig, 64);
}

bool crown_isc_verify(const crown_isc_t *c, const uint8_t crown_pubkey[32]) {
    if (!c || !crown_pubkey) return false;
    uint8_t buf[CROWN_ISC_PREIMAGE_LEN];
    uint32_t n = crown_isc_build_preimage(c, buf);
    uint8_t digest[SHA256_DIGEST_LEN];
    sha256(buf, (size_t)n, digest);
    /* SIGNATURE ONLY. The preimage excludes the mutable `revoked` flag by design,
     * so a REVOKED credential still bears a valid signature and still returns true
     * here. Gate ACCESS with crown_isc_is_active(), never with this alone. */
    return ed25519_verify(digest, SHA256_DIGEST_LEN, c->sig, crown_pubkey);
}

bool crown_isc_is_active(const crown_isc_t *c, const uint8_t crown_pubkey[32]) {
    /* The credential authorises an action NOW iff its signature verifies AND it
     * has not been revoked. This is the gate callers want; crown_isc_verify is
     * the cryptographic half only. Revocation is a registry action (not a
     * re-signing), so it can never be captured in the signature — it must be
     * consulted here, on top of a valid signature. */
    if (!c) return false;
    if (c->revoked) return false;
    return crown_isc_verify(c, crown_pubkey);
}

/* Build the alleged-wrong term for a revocation ground, then let The One Policy
 * decide. The Crown never invents a ground; it only revokes when the ground is
 * itself a real Maxim / Return-Doctrine violation, and it REFUSES the moment the
 * proffered ground is coercion (non-payment, kill switch, denial of aid). */
int32_t crown_revoke(crown_t *cr, crown_isc_t *c, uint8_t reason) {
    if (!cr || !c) return CROWN_ERR_ARG;

    op_term_t t;
    /* Baseline: a balanced, reciprocal, harmless term (i.e. NOT a violation). */
    t.give_a = SR_ZERO; t.give_b = SR_ZERO;
    t.harm_a = SR_ZERO; t.harm_b = SR_ZERO;
    t.interest = SR_ZERO;
    t.reciprocal = true;
    t.denies_aid = false;
    t.revoke_for_nonpayment = false;
    t.has_kill_switch = false;
    t.fraud_root = false;

    switch (reason) {
        case CROWN_REVOKE_NONPAYMENT:
            /* The forbidden ask: "revoke them because they didn't pay." */
            t.revoke_for_nonpayment = true;
            break;
        case CROWN_REVOKE_KILL_SWITCH:
            t.has_kill_switch = true;
            break;
        case CROWN_REVOKE_DENY_AID:
            t.denies_aid = true;
            break;
        case CROWN_REVOKE_FRAUD_ROOT:
            /* Return Doctrine: the subject's claim/root of title is fraud. */
            t.fraud_root = true;
            break;
        case CROWN_REVOKE_MAXIM_HARM:
            /* Symbiotic Maxim: the subject inflicted asymmetric, uncured harm. */
            t.harm_a = SR_ONE;      /* a party is harmed...            */
            t.reciprocal = false;   /* ...and nothing binds a return   */
            break;
        default:
            return CROWN_ERR_NO_GROUND;   /* CROWN_REVOKE_NONE or unknown */
    }

    /* Grace of Somalia FIRST: if the proffered ground is coercive, the Crown
     * refuses outright — this is the kill-switch-free mandate, and it is the
     * whole point of routing revocation through One Policy. */
    if (!op_grace_of_somalia_ok(&t)) return CROWN_ERR_COERCION;

    /* Otherwise a valid ground must be a Return-Doctrine reversion OR one of the
     * three Maxim verdicts. Anything else (e.g. a perfectly fine term) is NOT a
     * ground to revoke. */
    bool valid_ground = false;
    if (op_return_doctrine_reverts(&t)) {
        valid_ground = true;
    } else {
        op_verdict_t v = op_evaluate(&t);
        if (v == OP_VOID_ASYMMETRIC_HARM ||
            v == OP_VOID_UNILATERAL_EXTRACTION ||
            v == OP_VOID_NONRECIPROCAL_BURDEN)
            valid_ground = true;
    }
    if (!valid_ground) return CROWN_ERR_NO_GROUND;

    c->revoked = 1;
    c->revoke_reason = reason;
    return CROWN_OK;
}

int32_t crown_chn_register(crown_t *cr, const uint8_t subject[32],
                           zcap_form_t form, const uint8_t digest[32]) {
    if (!cr || !subject || !digest) return CROWN_ERR_ARG;
    /* ONLY the four inalienable Crown forms may bear a Cultural Heritage Note.
     * A Ministry form (Financial/Manufactured/Intellectual/Human) or the System
     * form is refused: those are priced elsewhere; a heritage note is not a price. */
    if (!crown_form_is_crown(form)) return CROWN_ERR_NOT_CROWN;
    if (cr->recognition_count >= CROWN_MAX_RECOGNITIONS) return CROWN_ERR_FULL;

    crown_chn_record_t *r = &cr->recognitions[cr->recognition_count];
    crown_memcpy(r->subject, subject, 32);
    r->form = form;
    crown_memcpy(r->digest, digest, 32);
    r->used = 1;
    cr->recognition_count++;
    /* A RECOGNITION, not a deed: the note says "this exists and is honoured",
     * never "this is owned". You cannot buy a river; you may only witness it. */
    return CROWN_OK;
}
