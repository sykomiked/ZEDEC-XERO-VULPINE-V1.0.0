/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvmerch.h — v-merchant: the MERCHANT side of point-of-sale and agent
 * payments on the libzxv ledger.
 *
 *   zvm_invoice_create  an invoice in one ISO 4217 currency: Potential
 *   zvm_accept          a customer's signed payment proof, checked OFFLINE
 *                       against the invoice and the terminal's list of payer
 *                       keys (Ed25519 or ML-DSA-87): Emerging, queued
 *   zvm_settle          back online: every queued proof is posted payer ->
 *                       merchant on the ledger, the assurance fee is charged
 *                       with zxv_fee_assure_carry (one carry per currency)
 *                       and split by zxv_fee_split into four fee accounts:
 *                       Manifest. A proof another till already settled is
 *                       refused by the ledger (its reference is the proof's
 *                       identity) and quarantined; a payer without funds
 *                       bounces and the invoice reopens.
 *   zvm_eod_close       an end-of-day report, exact per currency (count,
 *                       gross, fee, net, the four fee lines, bounces,
 *                       duplicates), whose digest goes into a signed record
 *
 * A proof's identity is SHA3-256 over (payer account, payer nonce): a payer
 * nonce pays once, on this terminal and, through the ledger reference, on
 * any terminal posting to the same ledger.
 *
 * OUT OF SCOPE: the customer's wallet and voucher redemption UX belong to
 * the Vino Embed kit; this kit only verifies what a wallet signed. No cash
 * in, no cash out, no fiat on- or off-ramp: money moves only between ledger
 * accounts that already hold it.
 *
 * Integer code only (R11). Kit API 0.1 (preview). */
#ifndef ZVMERCH_H
#define ZVMERCH_H

#include "zind_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZVM_VERSION_MAJOR 0
#define ZVM_VERSION_MINOR 1

/* Status codes (-4500..-4599). */
#define ZVM_E_EXPIRED  (-4501) /* the proof is dated outside the invoice's validity   */
#define ZVM_E_MISMATCH (-4502) /* merchant, invoice, currency or amount do not match  */
#define ZVM_E_PAYER    (-4503) /* the payer (or its key) is not on the terminal's list */
#define ZVM_E_CARD     (-4504) /* a card number fails its network's check digit       */
#define ZVM_E_PENDING  (-4505) /* proofs are still queued: settle before closing      */

const char *zvm_status_name(int32_t status);

#define ZVM_ALG_ED25519 1
#define ZVM_ALG_MLDSA87 2

/* Invoice states. */
#define ZVM_INV_OPEN     1 /* waiting for a payment (Potential)               */
#define ZVM_INV_ACCEPTED 2 /* a proof was verified offline, queued (Emerging) */
#define ZVM_INV_SETTLED  3 /* posted on the ledger (Manifest)                 */
#define ZVM_INV_VOID     4 /* cancelled by the merchant while open            */

/* Proof states. */
#define ZVM_PRF_QUEUED    1
#define ZVM_PRF_SETTLED   2
#define ZVM_PRF_BOUNCED   3 /* the payer could not pay: the invoice reopened  */
#define ZVM_PRF_DUPLICATE 4 /* already settled elsewhere: refused, quarantined */

/* Record kinds (chain domain "v-merchant"). */
#define ZVM_REC_CCY     1
#define ZVM_REC_PAYER   2
#define ZVM_REC_INVOICE 3
#define ZVM_REC_ACCEPT  4
#define ZVM_REC_SETTLE  5
#define ZVM_REC_BOUNCE  6
#define ZVM_REC_DUP     7
#define ZVM_REC_VOID    8
#define ZVM_REC_EOD     9

#define ZVM_ID_MAX       32u /* merchant and invoice ids: [A-Za-z0-9._:/-] */
#define ZVM_MAX_CCY      8u
#define ZVM_MAX_PAYERS   4096u
#define ZVM_MAX_INVOICES 65536u
#define ZVM_PAY_CTX      "v-merchant/pay" /* ML-DSA context of payment proofs */
#define ZVM_PROOF_MAX    (4 + 1 + 3 * 33 + 8 + 4 + 8 + 8 + 1 + 2 + ZIND_SIG_BYTES)

/* A payment proof as it travels from the customer's device to the till
 * (QR, NFC, Bluetooth, a file): the signed fields and the signature. */
typedef struct {
    char merchant[ZVM_ID_MAX + 1];
    char invoice[ZVM_ID_MAX + 1];
    char ccy[4];
    int64_t amount;    /* minor units                  */
    uint32_t payer;    /* the payer's ledger account   */
    uint64_t nonce;    /* the payer's own counter      */
    uint64_t time;     /* the payer device's clock     */
    int32_t alg;       /* ZVM_ALG_*                    */
    uint8_t sig[ZIND_SIG_BYTES];
    size_t sig_len;    /* 64 or ZIND_SIG_BYTES         */
} zvm_proof;

/* The bytes the customer's device signs (Ed25519 as they are, ML-DSA-87
 * under ZVM_PAY_CTX): "zvm/pay/v1" || str merchant || str invoice || str ccy
 * || u64le amount || u32le payer || u64le nonce || u64le time. Pure. */
int32_t zvm_pay_bytes(const zvm_proof *p, uint8_t *buf, size_t cap, size_t *out_len);
/* Wire form: "ZVMP" u8 1, the signed fields as above, u8 alg, u16le sig
 * length, sig. decode is strict (exact length, ids, algorithm and
 * signature length agree) and checks nothing cryptographic: ZXV_E_FORMAT. */
int32_t zvm_proof_encode(const zvm_proof *p, uint8_t *buf, size_t cap, size_t *out_len);
int32_t zvm_proof_decode(const uint8_t *buf, size_t len, zvm_proof *out);

/* Pure: a card number's check digit for a ZXV_NET_* network
 * (zxv_card_check_digit); ZVM_E_CARD if it fails. */
int32_t zvm_card_check(int32_t network, const char *pan);

typedef struct zvm_terminal zvm_terminal;

/* The cap needs LEDGER_POST, LEDGER_READ and CRYPTO_SIGN (and ATTEST with
 * a quarantine log, which may be NULL). Signer and log are borrowed. */
int32_t zvm_terminal_create(const zxv_cap *cap, const char *merchant_id,
                            const zind_signer *signer, zxv_hexlog *qlog, zvm_terminal **out);
void zvm_terminal_destroy(zvm_terminal *t);
const zind_chain *zvm_records(const zvm_terminal *t);

/* Accept a currency, settled into the merchant's own account; opens the
 * four fee accounts of that currency (ZXV_FEE_* order). */
int32_t zvm_ccy_add(zvm_terminal *t, const char *ccy, uint32_t merchant_account);
int32_t zvm_fee_accounts(const zvm_terminal *t, const char *ccy, uint32_t out[ZXV_FEE_BUCKETS]);

/* A payer the terminal trusts offline: its ledger account and key. When
 * card_network is non-zero, `pan` is checked with zvm_card_check (only its
 * last four digits are kept). */
int32_t zvm_payer_add(zvm_terminal *t, uint32_t payer_account, int32_t alg, const uint8_t *pk,
                      size_t pk_len, int32_t card_network, const char *pan);

int32_t zvm_invoice_create(zvm_terminal *t, const char *invoice_id, const char *ccy,
                           int64_t amount_minor, uint64_t issued_at, uint64_t expires_at,
                           uint32_t *out_invoice);
int32_t zvm_invoice_void(zvm_terminal *t, uint32_t invoice);
int32_t zvm_invoice_state(const zvm_terminal *t, uint32_t invoice, int32_t *out_state,
                          uint8_t *out_tag);
int32_t zvm_invoice_find(const zvm_terminal *t, const char *invoice_id, uint32_t *out_invoice);

/* Offline: verify a proof against its invoice and queue it. ZXV_OK;
 * ZVM_E_MISMATCH, ZVM_E_EXPIRED, ZVM_E_PAYER, ZXV_E_VERIFY (quarantined),
 * ZIND_E_REPLAY (the payer nonce was used, or the invoice is already paid),
 * ZXV_E_NOT_FOUND (no such invoice). *out_proof (may be NULL) names it. */
int32_t zvm_accept(zvm_terminal *t, const zvm_proof *p, uint32_t *out_proof);
int32_t zvm_proof_state(const zvm_terminal *t, uint32_t proof, int32_t *out_state,
                        int32_t *out_status);

typedef struct {
    uint32_t settled, bounced, duplicates;
} zvm_batch;

/* Online: post every queued proof, oldest first. Each proof ends SETTLED,
 * BOUNCED or DUPLICATE; the batch itself fails only on a ledger fault. */
int32_t zvm_settle(zvm_terminal *t, zvm_batch *out);

/* The open day's exact totals for one currency. Any out may be NULL. */
typedef struct {
    uint32_t settled, bounced, duplicates;
    int64_t gross, fee, net;
    int64_t fee_part[ZXV_FEE_BUCKETS];
} zvm_totals;
int32_t zvm_day_totals(const zvm_terminal *t, const char *ccy, zvm_totals *out);
/* The end-of-day report text for the open day (two-call pattern):
 *   EOD <merchant> day <day>
 *   <CCY> settled <n> gross <g> fee <f> net <n> bounced <n> duplicates <n>
 *   <CCY> fee reserve-floor <a> vbill-pool <b> infrastructure <c> regenerative <d>
 * amounts as exact decimals in major units. */
int32_t zvm_eod_report(const zvm_terminal *t, uint32_t day, char *buf, size_t cap,
                       size_t *out_len);
/* Close the day: the report's SHA3-256 and the per-currency totals go into
 * a signed record, and the day's totals start again at zero. ZVM_E_PENDING
 * while proofs are queued; days must increase (ZXV_E_STATE). out_digest
 * (may be NULL) gets the report digest. */
int32_t zvm_eod_close(zvm_terminal *t, uint32_t day, uint8_t out_digest[32]);

#ifdef __cplusplus
}
#endif

#endif /* ZVMERCH_H */
