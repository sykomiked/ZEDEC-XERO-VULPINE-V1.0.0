/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* crown.h — THE CROWN PILLAR (the Sicilian Crown / House of Curzi).
 *
 * WHAT THIS IS
 * -----------
 * The Crown ISSUES sovereign credentials and RECOGNISES the four inalienable
 * ("Crown") forms of capital. That is the whole of its authority: it notarises
 * personhood and standing, and it verifies signatures. It is, by deliberate
 * construction, a passport office physically incapable of reaching into your
 * wallet.
 *
 * The flavour is not decoration, it is the security model:
 *   "You do not apply to exist; the OS already concedes you exist and merely
 *    offers to notarise it."
 *
 * HARD ANTI-CAPTURE RULE (enforced by crown.c; verified by the dedicated
 * anti-capture step in `make verify-all`, which greps this source AND the
 * compiled object for any money-moving symbol and finds none)
 * -----------------------------------------------------------------
 * crown.c NEVER includes any ministry / finance / ledger / wallet header and
 * NEVER references a money-moving symbol (no triple_ledger, no vino, no
 * balance arithmetic, no zcap_exchange). The Crown holds credential/signature
 * authority and touches money NOWHERE. The two Ministry-side registration
 * steps (FINANCIAL, OVERSIGHT) are, from the Crown, opaque stubs it does not
 * perform — it merely knows they exist and steps over them.
 *
 * KEYS
 * ----
 * The Crown VERIFIES with a public key. It never holds a private key: issuance
 * signatures are produced OFFLINE (HSM-backed) and supplied as input. So
 * crown_register produces an UNSIGNED credential (sig == zeros); the offline
 * signature is attached afterward and checked with crown_isc_verify. This is
 * honest: there is no place in this kernel that can forge a Crown signature,
 * because there is no private key here to forge it with.
 */
#ifndef ZXV_CROWN_H
#define ZXV_CROWN_H

#include <stdint.h>
#include <stdbool.h>
#include "zcapital.h"   /* zcap_form_t only — we classify Crown forms, never move value */

/* ===== Capability bits carried by an Individual Sovereign Credential ===== */
#define CROWN_CAP_GRIDCHAIN     (1u << 0)  /* may anchor into the GridChain        */
#define CROWN_CAP_VINO_SETTLE   (1u << 1)  /* may be a party to a settlement*       */
#define CROWN_CAP_COMMONS_TRUST (1u << 2)  /* trusted in the commons               */
#define CROWN_CAP_PS233         (1u << 3)  /* PS-233 standing                       */
#define CROWN_CAP_DIPLOMATIC    (1u << 4)  /* diplomatic recognition               */
#define CROWN_CAP_PAYITFORWARD  (1u << 5)  /* pay-it-forward participant            */
#define CROWN_CAP_CAPREGISTER   (1u << 6)  /* may hold a capital-register entry     */
#define CROWN_CAP_PEACEKEEPING  (1u << 7)  /* peacekeeping mandate                  */
/* (*) CROWN_CAP_VINO_SETTLE is a BIT the credential CARRIES; the Crown grants the
 *     standing but performs no settlement itself. The name is a permission label,
 *     not a call into any settlement engine — the Crown cannot reach one. */

/* ===== The Individual Sovereign Credential — a small fixed record ===== */
typedef struct {
    uint8_t  subject[32];           /* who this credential is about (a hash/id)   */
    uint32_t issue_epoch;           /* Crown-assigned issue epoch (monotonic)     */
    uint32_t capability_bits;       /* OR of CROWN_CAP_*                          */
    uint8_t  maxim_ok;              /* attests conformance to the Symbiotic Maxim */
    uint8_t  no_harm;               /* attests the no-harm covenant               */
    uint8_t  gridchain_anchor[32];  /* GridChain inclusion anchor (see self-issues)*/
    uint8_t  sig[64];               /* Ed25519 signature over the ISC preimage    */
    uint8_t  revoked;               /* mutable registry status, NOT in the sig    */
    uint8_t  revoke_reason;         /* CROWN_REVOKE_* if revoked                   */
} crown_isc_t;

/* ===== Identity methods — NO passport required ===== */
typedef enum {
    CROWN_ID_DID = 0,          /* a decentralised identifier                     */
    CROWN_ID_BIOMETRIC_HASH,   /* a hash of a biometric template                 */
    CROWN_ID_WITNESS           /* attestation by existing recognised subjects    */
} crown_id_method_t;

/* ===== The 7-step registration FSM =====
 * Steps 4 (FINANCIAL) and 6 (OVERSIGHT) are Ministry-side. From the Crown they
 * are OPAQUE STUBS: it does not perform them, it steps past them. */
typedef enum {
    CROWN_REG_APPLY = 0,       /* 1 */
    CROWN_REG_VERIFY_ID,       /* 2 */
    CROWN_REG_CREDENTIAL,      /* 3 */
    CROWN_REG_FINANCIAL_STUB,  /* 4 — Ministry-side; opaque to the Crown          */
    CROWN_REG_ACTIVATE,        /* 5 */
    CROWN_REG_OVERSIGHT_STUB,  /* 6 — Ministry-side; opaque to the Crown          */
    CROWN_REG_ACTIVE,          /* 7 */
    CROWN_REG_REVOKED          /* terminal, only via a maxim/Return-Doctrine ground*/
} crown_reg_stage_t;

/* ===== Revocation grounds. The ONLY admissible grounds are a Symbiotic-Maxim
 * or Return-Doctrine violation. Non-payment is NEVER a ground (Grace of Somalia). */
typedef enum {
    CROWN_REVOKE_NONE = 0,
    CROWN_REVOKE_NONPAYMENT,    /* FORBIDDEN ground — crown_revoke MUST refuse it  */
    CROWN_REVOKE_FRAUD_ROOT,    /* Return Doctrine: root of title is fraud         */
    CROWN_REVOKE_MAXIM_HARM,    /* Symbiotic Maxim: asymmetric harm on a party     */
    CROWN_REVOKE_KILL_SWITCH,   /* FORBIDDEN ground — coercion, crown_revoke refuses*/
    CROWN_REVOKE_DENY_AID       /* FORBIDDEN ground — coercion, crown_revoke refuses*/
} crown_revoke_reason_t;

/* Return codes (0 == ok, negative == refused/failed). */
#define CROWN_OK               0
#define CROWN_ERR_ARG        (-1)
#define CROWN_ERR_FSM        (-2)
#define CROWN_ERR_FULL       (-3)
#define CROWN_ERR_NOT_CROWN  (-4)   /* CHN: a Ministry/System form was offered     */
#define CROWN_ERR_COERCION   (-5)   /* revoke refused: forbidden (coercive) ground */
#define CROWN_ERR_NO_GROUND  (-6)   /* revoke refused: no valid maxim/RD violation  */

/* ===== A Cultural Heritage Note recognition record (recognition, NOT ownership) ===== */
typedef struct {
    uint8_t     subject[32];
    zcap_form_t form;
    uint8_t     digest[32];
    uint8_t     used;
} crown_chn_record_t;

#define CROWN_MAX_RECOGNITIONS 32

/* ===== The Crown itself — a tiny fixed-size notary, no allocation ===== */
typedef struct {
    uint32_t           epoch;          /* next issue epoch (monotonic)            */
    uint32_t           issued_count;   /* how many ISCs issued                    */
    crown_reg_stage_t  last_stage;     /* stage the last registration reached     */
    crown_chn_record_t recognitions[CROWN_MAX_RECOGNITIONS];
    uint32_t           recognition_count;
} crown_t;

/* ===== ISC signing preimage =====
 * The signature covers the CREDENTIAL CONTENT only: domain tag + subject +
 * issue_epoch + capability_bits + maxim_ok + no_harm + gridchain_anchor.
 * It deliberately EXCLUDES sig (obviously) and the mutable revoked/revoke_reason
 * status — revocation is a registry action, not a re-signing, so a revoked ISC
 * still bears its original (now-superseded) signature. This inline lives in the
 * header so the Crown and any offline signer serialise byte-identical preimages. */
#define CROWN_ISC_DOMAIN     "ZXV-CROWN-ISC-v1"      /* 16 bytes, no NUL          */
#define CROWN_ISC_DOMAIN_LEN 16u
#define CROWN_ISC_PREIMAGE_LEN (CROWN_ISC_DOMAIN_LEN + 32u + 4u + 4u + 1u + 1u + 32u) /* 90 */

static inline uint32_t crown_isc_build_preimage(const crown_isc_t *c,
                                                uint8_t buf[CROWN_ISC_PREIMAGE_LEN]) {
    static const char TAG[] = CROWN_ISC_DOMAIN;   /* sizeof == 17 (incl NUL)      */
    uint32_t n = 0, i;
    for (i = 0; i < CROWN_ISC_DOMAIN_LEN; i++) buf[n++] = (uint8_t)TAG[i];
    for (i = 0; i < 32; i++)                   buf[n++] = c->subject[i];
    buf[n++] = (uint8_t)(c->issue_epoch      & 0xffu);
    buf[n++] = (uint8_t)((c->issue_epoch>>8 ) & 0xffu);
    buf[n++] = (uint8_t)((c->issue_epoch>>16) & 0xffu);
    buf[n++] = (uint8_t)((c->issue_epoch>>24) & 0xffu);
    buf[n++] = (uint8_t)(c->capability_bits      & 0xffu);
    buf[n++] = (uint8_t)((c->capability_bits>>8 ) & 0xffu);
    buf[n++] = (uint8_t)((c->capability_bits>>16) & 0xffu);
    buf[n++] = (uint8_t)((c->capability_bits>>24) & 0xffu);
    buf[n++] = c->maxim_ok;
    buf[n++] = c->no_harm;
    for (i = 0; i < 32; i++)                   buf[n++] = c->gridchain_anchor[i];
    return n; /* == CROWN_ISC_PREIMAGE_LEN */
}

/* ===== Public API ===== */

void crown_init(crown_t *cr);

/* Drive the 7-step FSM to CROWN_REG_ACTIVE and fill *out (epoch, caps,
 * maxim_ok=1, no_harm=1, subject). The sig field is left ZEROED — the Crown
 * holds no private key and signs NOTHING; the offline signature is attached
 * later (see crown_isc_attach_signature) and checked with crown_isc_verify. */
int32_t crown_register(crown_t *cr, const uint8_t subject[32],
                       crown_id_method_t method, uint32_t capability_bits,
                       crown_isc_t *out);

/* Attach an OFFLINE-produced Ed25519 signature to an issued credential. */
void crown_isc_attach_signature(crown_isc_t *c, const uint8_t sig[64]);

/* Verify the credential's SIGNATURE ONLY: sha256 the preimage, then ed25519_verify
 * it under the Crown public key. A tampered ISC (any signed field, or the sig
 * itself) fails. NOTE: the preimage excludes the mutable `revoked` flag, so a
 * REVOKED credential still verifies here. To gate access, use crown_isc_is_active. */
bool crown_isc_verify(const crown_isc_t *c, const uint8_t crown_pubkey[32]);

/* Is the credential currently ACTIVE — i.e. may it authorise an action NOW?
 * True iff the signature verifies AND the credential is not revoked. This is the
 * gate callers should use; crown_isc_verify is only the cryptographic half. */
bool crown_isc_is_active(const crown_isc_t *c, const uint8_t crown_pubkey[32]);

/* Revoke — ALLOWED ONLY on a Symbiotic-Maxim or Return-Doctrine violation.
 * A non-payment (or any coercive) reason is REFUSED via op_grace_of_somalia_ok.
 * On success sets c->revoked=1, c->revoke_reason=reason and returns CROWN_OK. */
int32_t crown_revoke(crown_t *cr, crown_isc_t *c, uint8_t reason);

/* Cultural Heritage Note: recognise a Crown (inalienable) form only. Ministry
 * and System forms are refused. Grants a recognition RECORD, never ownership —
 * you cannot buy a language, and you cannot be sold one either. */
int32_t crown_chn_register(crown_t *cr, const uint8_t subject[32],
                           zcap_form_t form, const uint8_t digest[32]);

/* True iff `form` is one of the four inalienable Crown forms. */
bool crown_form_is_crown(zcap_form_t form);

#endif /* ZXV_CROWN_H */
