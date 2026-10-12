/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ztdeliver.h — tri-deliver: courier escrow released by an offline handoff
 * proof.
 *
 *   ztd_parcel_create   the sender's payment moves into escrow: Potential
 *   ztd_dispatch        a named courier takes the parcel: Emerging
 *   ztd_handoff_submit  the recipient's signature over (parcel id, courier,
 *                       time) — Ed25519 or ML-DSA-87, made on the
 *                       recipient's own device, offline — releases escrow
 *                       to the payee: Manifest
 *   ztd_close           Dissolving
 *
 * A dispute freezes the parcel as BOTH with its money still in escrow; only
 * a registered arbiter's signed decision moves it (pay the payee or refund
 * the sender). A proof is accepted once: a replayed proof, a second handoff
 * for a delivered parcel, a proof for another parcel or courier, or a time
 * outside the delivery window is refused. Every step is an ML-DSA-87
 * signed, SHA3-256 chained record; forged proofs go to the quarantine log.
 *
 * The kit settles on the libzxv ledger; it is not a payment processor or a
 * money transmitter, and an integration with a courier platform is a
 * public-API integration that platform has not endorsed. Clocks are the
 * devices' clocks (see README "Limits").
 *
 * Integer code only (R11). Kit API 0.1 (preview). */
#ifndef ZTDELIVER_H
#define ZTDELIVER_H

#include "zind_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZTD_VERSION_MAJOR 0
#define ZTD_VERSION_MINOR 1

/* Status codes (-3200..-3299). */
#define ZTD_E_COURIER (-3201) /* the proof names another courier than the dispatched one */
#define ZTD_E_WINDOW  (-3202) /* the handoff time is outside the delivery window          */
#define ZTD_E_ARBITER (-3203) /* the key is not a registered arbiter                      */

const char *ztd_status_name(int32_t status);

#define ZTD_ALG_ED25519 1
#define ZTD_ALG_MLDSA87 2

/* Parcel states. */
#define ZTD_ST_ESCROW    1 /* funds in escrow, not dispatched (Potential) */
#define ZTD_ST_OUT       2 /* out for delivery (Emerging)                 */
#define ZTD_ST_DELIVERED 3 /* handoff proven, escrow released (Manifest)  */
#define ZTD_ST_DISPUTED  4 /* BOTH: frozen for an arbiter                 */
#define ZTD_ST_REFUNDED  5 /* the arbiter refunded the sender             */
#define ZTD_ST_CANCELLED 6 /* cancelled before dispatch, refunded         */
#define ZTD_ST_CLOSED    7 /* Dissolving                                  */

/* Arbiter outcomes. */
#define ZTD_PAY_PAYEE     1
#define ZTD_REFUND_SENDER 2

/* Record kinds (chain domain "tri-deliver"). */
#define ZTD_REC_CREATE   1
#define ZTD_REC_DISPATCH 2
#define ZTD_REC_DELIVER  3
#define ZTD_REC_DISPUTE  4
#define ZTD_REC_ARBITER  5
#define ZTD_REC_CANCEL   6
#define ZTD_REC_CLOSE    7

#define ZTD_ID_MAX       32u /* parcel and courier ids: [A-Za-z0-9._:/-] */
#define ZTD_MAX_PARCELS  65536u
#define ZTD_HANDOFF_CTX  "tri-deliver/handoff" /* ML-DSA context string */
#define ZTD_ARBITER_CTX  "tri-deliver/arbiter"

typedef struct ztd_desk ztd_desk;

/* A desk in one ISO 4217 currency; opens its escrow account. The cap needs
 * LEDGER_POST, LEDGER_READ and CRYPTO_SIGN (and ATTEST with a quarantine
 * log, which may be NULL). Signer and log are borrowed. */
int32_t ztd_desk_create(const zxv_cap *cap, const char *ccy, const zind_signer *desk_signer,
                        zxv_hexlog *qlog, ztd_desk **out);
void ztd_desk_destroy(ztd_desk *d);
uint32_t ztd_escrow_account(const ztd_desk *d);
int32_t ztd_arbiter_add(ztd_desk *d, const uint8_t pk[ZIND_PK_BYTES]);

/* Create a parcel and move amount_minor (> 0) from sender to escrow.
 * recipient_pk: 32 bytes (ED25519) or ZIND_PK_BYTES (MLDSA87). window: the
 * seconds after dispatch within which a handoff is accepted (1..30 days).
 * A parcel id is never reused on a desk. */
int32_t ztd_parcel_create(ztd_desk *d, const char *parcel_id, uint32_t sender_account,
                          uint32_t payee_account, int64_t amount_minor, int32_t recipient_alg,
                          const uint8_t *recipient_pk, size_t pk_len, uint64_t window_seconds,
                          uint32_t *out_parcel);
/* Escrow -> out for delivery with `courier` at `time` (unix seconds). */
int32_t ztd_dispatch(ztd_desk *d, uint32_t parcel, const char *courier, uint64_t time);

/* The canonical bytes the recipient signs: "ztd/handoff/v1" || u8 len ||
 * parcel id || u8 len || courier || u64le time. Pure; a recipient app
 * builds the same bytes itself. */
int32_t ztd_handoff_message(const char *parcel_id, const char *courier, uint64_t time,
                            uint8_t *buf, size_t cap, size_t *out_len);
/* Submit a handoff proof. On success escrow goes to the payee. */
int32_t ztd_handoff_submit(ztd_desk *d, uint32_t parcel, const char *courier, uint64_t time,
                           const uint8_t *sig, size_t sig_len);

/* Freeze the parcel as BOTH (sender, recipient or courier disagrees),
 * while its money is still in escrow (ESCROW or OUT). `reason` is a free
 * caller code recorded in the signed record. */
int32_t ztd_dispute(ztd_desk *d, uint32_t parcel, uint32_t reason);
/* The bytes an arbiter signs (context ZTD_ARBITER_CTX): the parcel id, the
 * hash of its dispute record and the outcome. */
int32_t ztd_arbiter_message(const ztd_desk *d, uint32_t parcel, int32_t outcome, uint8_t *buf,
                            size_t cap, size_t *out_len);
/* Apply a registered arbiter's signed decision (desk cap needs ATTEST). */
int32_t ztd_arbiter_resolve(ztd_desk *d, uint32_t parcel, const uint8_t arbiter_pk[ZIND_PK_BYTES],
                            int32_t outcome, const uint8_t sig[ZIND_SIG_BYTES]);
/* Cancel before dispatch: escrow back to the sender. */
int32_t ztd_cancel(ztd_desk *d, uint32_t parcel);
/* Delivered -> closed. */
int32_t ztd_close(ztd_desk *d, uint32_t parcel);

int32_t ztd_parcel_state(const ztd_desk *d, uint32_t parcel, int32_t *out_state,
                         uint8_t *out_tag);
/* Lookup by id. */
int32_t ztd_parcel_find(const ztd_desk *d, const char *parcel_id, uint32_t *out_parcel);
/* Money currently held for parcels in escrow (ESCROW, OUT, DISPUTED). */
int32_t ztd_escrow_owed(const ztd_desk *d, int64_t *out_minor);
const zind_chain *ztd_records(const ztd_desk *d);

#ifdef __cplusplus
}
#endif

#endif /* ZTDELIVER_H */
