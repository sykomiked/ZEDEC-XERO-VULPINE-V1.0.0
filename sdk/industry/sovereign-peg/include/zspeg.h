/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zspeg.h — sovereign-peg: an OPERATOR toolkit over the libzxv V-Bill hub
 * (zxv_vhub, ABI 1.5) for central-bank engineers. It is not app-facing:
 * every mutating call takes an operator capability, and nothing here is
 * meant to be reachable from a wallet, a shop or a game.
 *
 *   zsp_ccy_add        register a test currency on the hub (zxv_vhub_add_ccy)
 *   zsp_peg_parse      a peg written as an exact decimal or fraction ->
 *                      num/den minor units per V-Bill, never rounded
 *   zsp_peg_set        apply the issuer's ML-DSA-87 signed peg
 *                      (zxv_vhub_set_peg), refused if circulation would no
 *                      longer be covered by the currency's V-Bill float
 *   zsp_quote          zxv_vhub_quote, exactly as the library computes it
 *   zsp_issue          the hub faucet (zxv_vhub_issue), within the backed-
 *                      only rule below
 *   zsp_flow_submit    a cross-currency flow judged by the 64-state capital
 *                      controls, then swapped by zxv_vhub_swap (which still
 *                      applies its own limit rules and backing checks)
 *   zsp_decide         a registered officer's signed release / deny of a
 *                      held flow
 *   zsp_epoch_next     rule-epoch rollover (zxv_vhub_next_epoch)
 *
 * CAPITAL CONTROLS. A flow is tagged with a hexagram (zxv_hex_*):
 *   base      from its authorisations: zxv_lp_from_evidence(approvals,
 *             objections): only approvals TRUE, only objections FALSE, both
 *             BOTH (conflicting authorisations), none NEITHER;
 *   polarity  FORBIDDEN if the sender is blocked or the flow is an outflow
 *             above the currency's per-flow outflow cap; NEGENTROPIC when two
 *             or more approvals agree and nobody objects; else NEUTRAL;
 *   phase     POTENTIAL at submission, MANIFEST once swapped.
 * A 64-entry policy table maps every tag to ALLOW, HOLD or REFUSE. Its
 * default is zxv_hex_action (PROCEED -> ALLOW, WAIT / ESCALATE -> HOLD,
 * REFUSE / ROLLBACK -> REFUSE). The operator may change entries, except
 * that a FORBIDDEN tag always refuses (and is quarantined), and a BOTH or a
 * FALSE tag can never be set to ALLOW. A held BOTH flow is written to the
 * quarantine log as held, and waits for a human officer.
 *
 * BACKED-ONLY ISSUANCE. The library keeps every pool and V-Bill float from
 * going below zero. On top of that, this kit refuses (ZSP_E_UNBACKED) an
 * issue to a holder, or a new peg, that would leave a currency's circulation
 * (issuer CREDIT minus the hub pool, read from zxv_vhub_balance) worth more
 * than its V-Bill float at the peg: circulation * den <= float * num, compared
 * exactly with zxv_rat_cmp. A swap moves circulation and float by the same
 * peg value, so it keeps the rule. Issuing to a pool (hub liquidity) or
 * V-Bills to a float is allowed.
 *
 * Every accepted call is an ML-DSA-87 signed, SHA3-256 chained record that
 * names the operator cap's process id (the operator decision log).
 *
 * HONEST LIMITS: the hub is the library's in-process demonstration hub with
 * test currencies only (ISO 4217 codes are refused) and no real value. No
 * fiat enters or leaves through this kit. Not connected to any central bank,
 * settlement rail or issuer; nothing here is a legal or regulatory check.
 *
 * Integer code only (R11). Kit API 0.1 (preview). */
#ifndef ZSPEG_H
#define ZSPEG_H

#include "zind_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZSP_VERSION_MAJOR 0
#define ZSP_VERSION_MINOR 1

/* Status codes (-4300..-4399). */
#define ZSP_E_UNBACKED    (-4301) /* circulation would exceed the V-Bill float at the peg  */
#define ZSP_E_POLICY      (-4302) /* that policy entry may not be set to that decision     */
#define ZSP_E_OFFICER     (-4303) /* the key is not a registered officer                   */
#define ZSP_E_REFUSED     (-4304) /* the capital controls refused the flow                 */
#define ZSP_E_QUARANTINED (-4305) /* the flow is Forbidden: refused and quarantined        */
#define ZSP_E_OPERATOR    (-4306) /* the cap is not an operator cap of this desk's context */

const char *zsp_status_name(int32_t status);

/* What an operator cap must hold. Reads need ZXV_CAP_LEDGER_READ only. */
#define ZSP_CAP_OPERATOR                                                                       \
    (ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ | ZXV_CAP_CRYPTO_SIGN | ZXV_CAP_ATTEST)

/* Policy decisions. */
#define ZSP_ALLOW  1
#define ZSP_HOLD   2
#define ZSP_REFUSE 3

/* Officer decisions on a held flow. */
#define ZSP_RELEASE 1
#define ZSP_DENY    2

/* Flow states. */
#define ZSP_FLOW_DONE        1 /* allowed by policy and swapped (Manifest)         */
#define ZSP_FLOW_HELD        2 /* held for an officer                              */
#define ZSP_FLOW_REFUSED     3 /* refused by policy                                */
#define ZSP_FLOW_QUARANTINED 4 /* Forbidden: refused and quarantined              */
#define ZSP_FLOW_RELEASED    5 /* held, released by an officer and swapped         */
#define ZSP_FLOW_DENIED      6 /* held, denied by an officer                       */
#define ZSP_FLOW_FAILED      7 /* allowed by policy, refused by the library        */

/* Record kinds (chain domain "sovereign-peg"). */
#define ZSP_REC_CREATE   1
#define ZSP_REC_CCY      2
#define ZSP_REC_PEG      3
#define ZSP_REC_LIMIT    4
#define ZSP_REC_CONTROL  5
#define ZSP_REC_POLICY   6
#define ZSP_REC_ACCOUNT  7
#define ZSP_REC_ISSUE    8
#define ZSP_REC_FLOW     9
#define ZSP_REC_DECISION 10
#define ZSP_REC_EPOCH    11
#define ZSP_REC_OFFICER  12
#define ZSP_REC_BLOCK    13

#define ZSP_MAX_FLOWS    65536u
#define ZSP_MAX_OFFICERS 16u
#define ZSP_MAX_BLOCKED  64u
#define ZSP_DECISION_CTX "sovereign-peg/decision" /* ML-DSA context of officer decisions */

typedef struct zsp_desk zsp_desk;

typedef struct {
    uint8_t from_slot, to_slot;
    uint32_t sender, receiver; /* hub holder accounts (zsp_account_open) */
    uint64_t amount;           /* minor units of from_slot              */
    uint64_t tick;             /* caller time, passed to zxv_vhub_swap  */
    uint32_t approvals;        /* authorisations on file for the flow   */
    uint32_t objections;       /* objections on file                    */
} zsp_flow;

/* A desk owns one hub created on the operator cap's context. hub_seed: 32
 * bytes (deterministic posting keys; NULL = random). The signer and log are
 * borrowed; qlog may be NULL (no quarantine records). */
int32_t zsp_desk_create(const zxv_cap *op, const uint8_t *hub_seed, const zind_signer *desk_signer,
                        zxv_hexlog *qlog, zsp_desk **out);
void zsp_desk_destroy(zsp_desk *d);
const zind_chain *zsp_log(const zsp_desk *d);

/* Register an officer (a person) whose signed decisions release or deny
 * held flows. */
int32_t zsp_officer_add(zsp_desk *d, const zxv_cap *op, const uint8_t pk[ZIND_PK_BYTES]);

int32_t zsp_ccy_add(zsp_desk *d, const zxv_cap *op, uint8_t slot, const char *code,
                    uint8_t minor_units, const uint8_t issuer_pk[ZIND_PK_BYTES],
                    uint64_t pool_funding, uint64_t vbill_backing);

/* Pure: a peg written as currency units per V-Bill ("1550", "0.25",
 * "1/3") with `minor_units` decimals -> the reduced num/den minor units per
 * V-Bill the issuer signs. Negative, zero, or beyond ZXV_VHUB_AMOUNT_MAX is
 * ZXV_E_RANGE; text that is not an exact number is ZXV_E_FORMAT. */
int32_t zsp_peg_parse(const char *rate, uint8_t minor_units, uint64_t *out_num,
                      uint64_t *out_den);
/* Pure: the 40 bytes an issuer signs (zxv_vhub_peg_msg). */
int32_t zsp_peg_message(uint8_t slot, uint64_t num, uint64_t den, uint64_t epoch,
                        uint8_t out[ZXV_VHUB_PEG_MSG_BYTES]);
/* Apply a signed peg. A signature that does not verify is quarantined. */
int32_t zsp_peg_set(zsp_desk *d, const zxv_cap *op, uint8_t slot, uint64_t num, uint64_t den,
                    uint64_t epoch, const uint8_t sig[ZIND_SIG_BYTES]);
int32_t zsp_peg_get(zsp_desk *d, const zxv_cap *rd, uint8_t slot, uint64_t *out_num,
                    uint64_t *out_den, uint64_t *out_epoch);

/* The library's limit rule (at most `limit` out per sender per epoch). */
int32_t zsp_limit_add(zsp_desk *d, const zxv_cap *op, uint8_t slot, uint64_t limit);
/* The kit's capital control: a single outflow from `slot` above `cap_minor`
 * is Forbidden. 0 removes the cap. */
int32_t zsp_outflow_cap_set(zsp_desk *d, const zxv_cap *op, uint8_t slot, uint64_t cap_minor);
/* Every flow from `account` is Forbidden from now on (no unblock). */
int32_t zsp_block_sender(zsp_desk *d, const zxv_cap *op, uint32_t account);

int32_t zsp_account_open(zsp_desk *d, const zxv_cap *op, uint8_t slot, uint32_t owner,
                         uint32_t *out_account);
/* Issue to a holder account (backed-only rule), a pool, or V-Bills to a
 * V-Bill float. */
int32_t zsp_issue(zsp_desk *d, const zxv_cap *op, uint32_t account, uint64_t amount);
/* A currency's circulation and float, and whether the float covers the
 * circulation at the current peg (1) or not (0; also 0 with no peg and
 * a non-zero circulation). */
int32_t zsp_coverage(zsp_desk *d, const zxv_cap *rd, uint8_t slot, uint64_t *out_circulation,
                     uint64_t *out_float, int32_t *out_covered);
int32_t zsp_balance(zsp_desk *d, const zxv_cap *rd, uint32_t account, int64_t *out_equity);
int32_t zsp_ccy_accounts(zsp_desk *d, const zxv_cap *rd, uint8_t slot, uint32_t *out_issuer,
                         uint32_t *out_pool, uint32_t *out_float);

int32_t zsp_quote(zsp_desk *d, const zxv_cap *rd, uint8_t from_slot, uint8_t to_slot,
                  uint64_t want, uint64_t *out_amount, uint64_t *out_vbills,
                  uint64_t *out_received);

/* The policy table. */
int32_t zsp_policy_get(const zsp_desk *d, uint8_t tag, int32_t *out_decision);
int32_t zsp_policy_set(zsp_desk *d, const zxv_cap *op, uint8_t tag, int32_t decision);
/* The tag the controls would give this flow now (no side effects). */
int32_t zsp_flow_tag(const zsp_desk *d, const zsp_flow *f, uint8_t *out_tag);

/* Judge and, if allowed, swap. *out_id (always set when a flow record was
 * written) names the flow. ZXV_OK (swapped), ZIND_E_HELD, ZSP_E_REFUSED,
 * ZSP_E_QUARANTINED, or the library's refusal (ZXV_E_LIMIT, ZXV_E_FUNDS,
 * ZXV_E_RANGE, ...) with the flow FAILED. Argument errors write nothing. */
int32_t zsp_flow_submit(zsp_desk *d, const zxv_cap *op, const zsp_flow *f, uint32_t *out_id);
int32_t zsp_flow_state(const zsp_desk *d, uint32_t id, int32_t *out_state, uint8_t *out_tag,
                       int32_t *out_status);
/* The bytes an officer signs (context ZSP_DECISION_CTX): "zsp/decision/v1"
 * || u32le flow id || the flow record's hash || u8 decision. */
int32_t zsp_decision_message(const zsp_desk *d, uint32_t id, int32_t decision, uint8_t *buf,
                             size_t cap, size_t *out_len);
/* Apply an officer's decision on a held flow. A release swaps at `tick`;
 * if the library refuses the swap the flow stays held and its status is
 * returned. */
int32_t zsp_decide(zsp_desk *d, const zxv_cap *op, uint32_t id,
                   const uint8_t officer_pk[ZIND_PK_BYTES], int32_t decision, uint64_t tick,
                   const uint8_t sig[ZIND_SIG_BYTES]);

int32_t zsp_epoch_next(zsp_desk *d, const zxv_cap *op, uint64_t *out_epoch);
/* zxv_vhub_check plus the coverage of every pegged currency. ZSP_E_UNBACKED
 * names the first uncovered slot in the last error. */
int32_t zsp_check(zsp_desk *d, const zxv_cap *rd, uint64_t *out_postings);

#ifdef __cplusplus
}
#endif

#endif /* ZSPEG_H */
