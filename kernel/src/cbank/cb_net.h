/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cb_net.h — a PAPSS-style multilateral payment and net settlement engine.
 *
 * Model. Direct participants (banks, the self-banking node) each hold a
 * prefunded position in ONE local currency at that currency's issuing
 * central bank. A payer participant S pays a payee participant R: S is
 * debited in its currency, R is credited in its currency, and the engine's
 * settlement agent stands in the middle of every leg. Cross-currency legs
 * are converted through one settlement unit U (cfg.settle_unit, an ISO 4217
 * code the operator chooses) at signed, fresh rates from configured sources.
 *
 * Conversion (exact integers, amounts in minor units, e = ISO minor unit):
 *   rate of c: 1 major c = m_c / 10^s_c major U   (U itself: m = 1, s = 0)
 *   u    = floor(send * m_S * 10^e_U / 10^(s_S + e_S))
 *   recv = floor(u * 10^(s_R + e_R) / (m_R * 10^e_U))
 * The instructed amount is always the payer's amount (send). Each floor
 * leaves a remainder that is recorded on the payment as an exact fraction
 * (rem/den) and is never charged to anyone. If either currency profile uses
 * CB_ROUND_EXACT, a payment with any remainder is rejected (AM12). Use
 * cb_net_quote to find what the payee will receive before sending.
 *
 * Bookings for one accepted payment (send, recv, u):
 *   pos[S] -= send   agent[ccyS] += send   upos[S] -= u   aupos[ccyS] += u
 *   pos[R] += recv   agent[ccyR] -= recv   upos[R] += u   aupos[ccyR] -= u
 * Conservation invariants (checked by cb_net_check and at cycle close):
 *   (1) for every currency c: sum(pos[p], p in c) + agent[c] == 0, exactly;
 *   (2) sum over participants of upos == 0 and sum over currencies of
 *       aupos == 0, exactly (every payment adds +u and -u).
 * Returns and cancellations book the exact reverse of the original legs at
 * the ORIGINAL amounts (no re-conversion), so both invariants still hold.
 *
 * Liquidity: a payment is accepted only if, after the debit,
 *   prefund - prefund*reserve_bps/10000 + net_debit_cap + pos[S] >= 0
 * (net_debit_cap is interest-free and defaults to 0, i.e. fully prefunded).
 * The same test protects the payee when a return or cancellation debits it.
 *
 * Settlement cycle: cb_net_open_cycle, then payments, then
 * cb_net_close_cycle, which checks both invariants, writes one net
 * settlement instruction per configured currency (to its issuing central
 * bank: every participant's net and the total versus the settlement agent),
 * applies each net to the participant's prefund, marks accepted payments
 * settled and resets positions.
 *
 * Status lifecycle (ISO 20022 TxSts in brackets):
 *   submit -> ACCEPTED [ACSP] -> close -> SETTLED [ACSC]
 *   submit -> REJECTED [RJCT] with a reason code
 *   ACCEPTED or SETTLED -> return -> RETURNED (pacs.004)
 *   ACCEPTED (same open cycle) -> cancel -> CANCELLED (camt.029 CNCL)
 * Idempotency: a message id seen before returns the stored outcome with
 * `duplicate` set and books nothing; the same id with different content is
 * rejected (DUPL) without touching the original. Return ids and cancellation
 * case ids are idempotent the same way.
 *
 * HONEST LIMITS. This is a deterministic in-memory engine with fixed
 * capacity (CB_NET_MAX_PART participants, CB_NET_MAX_PAY payment records per
 * engine lifetime: when full, new payments are refused). It has no
 * persistence, no network transport, no RTGS link, no SWIFT/CIPS
 * connectivity and no PAPSS connection; "PAPSS-style" describes the
 * prefunded multilateral-netting design, not membership or certification.
 * FX liquidity of the settlement agent across currency pools is reported
 * (per-currency nets) but its funding is an operational matter outside this
 * code. Partial returns are not supported (one full return per payment).
 */
#ifndef ZXV_CB_NET_H
#define ZXV_CB_NET_H

#include <stdint.h>
#include <stdbool.h>
#include "cb_config.h"

#define CB_NET_MAX_PART 64u
#define CB_NET_MAX_PAY  512u
#define CB_ID_LEN       36u /* Max35Text + NUL */
#define CB_UETR_LEN     37u
#define CB_AMT_MAX      1000000000000000ull /* 10^15 minor units per payment */

typedef enum {
    CB_PS_NONE = 0,
    CB_PS_ACCEPTED = 1,
    CB_PS_SETTLED = 2,
    CB_PS_REJECTED = 3,
    CB_PS_RETURNED = 4,
    CB_PS_CANCELLED = 5
} cb_pay_status;

typedef struct {
    char bic[CB_BIC_LEN];
    char country[3];
    char ccy[4];
    uint8_t role;
    bool active;
    int64_t prefund; /* balance held at the issuing central bank */
} cb_participant;

typedef struct {
    char msg_id[CB_ID_LEN];
    char e2e_id[CB_ID_LEN];
    char uetr[CB_UETR_LEN]; /* optional, "" if none */
    uint16_t dbtr_agt;      /* participant index of the payer */
    uint16_t cdtr_agt;      /* participant index of the payee */
    uint64_t amount;        /* in the payer participant's currency */
    char purpose[5];        /* ISO external purpose code, optional */
    char dbtr_name[CB_NAME_LEN];
    char cdtr_name[CB_NAME_LEN];
    char dbtr_ctry[3];
    char cdtr_ctry[3];
    bool fi_transfer; /* pacs.009 (bank's own account) rather than pacs.008 */
} cb_pay_req;

typedef struct {
    cb_pay_req req;
    uint8_t status;
    char reason[5];
    uint32_t cycle;
    uint64_t send, recv, unit;
    uint64_t rem1, den1; /* send -> unit remainder, exact fraction of a U minor unit */
    uint64_t rem2, den2; /* unit -> recv remainder, fraction of an R minor unit */
    char rtr_id[CB_ID_LEN];
    char rtr_reason[5];
    char case_id[CB_ID_LEN];
    uint64_t accepted_at;
} cb_pay_rec;

typedef struct {
    int status; /* cb_pay_status */
    char reason[5];
    bool duplicate;
    uint32_t index; /* record index, or 0xffffffff */
    uint64_t send, recv, unit;
} cb_pay_result;

typedef struct {
    uint16_t part;
    int64_t net; /* + receives, - pays (local minor units) */
} cb_settle_line;

typedef struct {
    char ccy[4];
    char issuer_bic[CB_BIC_LEN];
    char issuer_name[CB_NAME_LEN];
    uint32_t cycle;
    int64_t participants_net; /* sum of lines */
    int64_t agent_net;        /* == -participants_net */
    int64_t unit_net;         /* value of this currency pool's net in U */
    uint16_t n_lines;
    cb_settle_line lines[CB_NET_MAX_PART];
} cb_settle_instr;

typedef struct {
    const cb_config *cfg;
    uint16_t n_part;
    cb_participant part[CB_NET_MAX_PART];
    int64_t pos[CB_NET_MAX_PART];
    int64_t upos[CB_NET_MAX_PART];
    int64_t agent[CB_MAX_CCY];
    int64_t aupos[CB_MAX_CCY];
    uint64_t corr_used[CB_MAX_CORRIDORS];
    bool have_rate[CB_MAX_CCY];
    cb_rate_rec rate[CB_MAX_CCY];
    uint32_t cycle;
    bool cycle_open;
    uint32_t n_pay;
    cb_pay_rec pay[CB_NET_MAX_PAY];
} cb_net;

/* Engine error codes (in addition to cb_config.h codes). */
#define CB_E_CYCLE   (-40)
#define CB_E_PART    (-41)
#define CB_E_PERM    (-42)
#define CB_E_CONSERV (-43)
#define CB_E_ID      (-44)

int cb_net_init(cb_net *e, const cb_config *cfg); /* cfg must validate */
/* Admit a direct participant. Its role must hold CB_PERM_HOLD_PREFUND, its
 * currency must be configured, and if the currency is not NONRESIDENT-open
 * its country must be the issuer country. Returns the index or < 0. */
int cb_net_add_participant(cb_net *e, const char *bic, const char *country, const char *ccy,
                           cb_role role, int64_t prefund);
int cb_net_set_active(cb_net *e, uint16_t part, bool active);
int cb_net_find_participant(const cb_net *e, const char *bic);
/* Prefund top-up or withdrawal outside a cycle's positions. */
int cb_net_prefund(cb_net *e, uint16_t part, int64_t delta);

/* Accept a signed rate record (cb_rate_check) if its seq is newer. */
int cb_net_rate_submit(cb_net *e, const cb_rate_rec *r, uint64_t now);

int cb_net_open_cycle(cb_net *e, uint64_t now);
/* What R would receive for `amount` from S right now (no booking). */
int cb_net_quote(const cb_net *e, uint16_t s, uint16_t r, uint64_t amount, uint64_t now,
                 uint64_t *recv, uint64_t *unit, bool *exact);
int cb_net_submit(cb_net *e, const cb_pay_req *req, uint64_t now, cb_pay_result *res);
int cb_net_return(cb_net *e, const char *orig_msg_id, const char *rtr_id, const char *reason,
                  uint64_t now, cb_pay_result *res);
/* camt.056 handling. *accepted tells whether the payment was cancelled;
 * reason gets the camt.029 rejection code otherwise (ARDT, AGNT, NOOR, AM04). */
int cb_net_cancel(cb_net *e, const char *orig_msg_id, const char *case_id, uint64_t now,
                  bool *accepted, char reason[5]);
/* Close the cycle. Writes one instruction per configured currency into out
 * (cap entries) and sets *n. */
int cb_net_close_cycle(cb_net *e, uint64_t now, cb_settle_instr *out, uint32_t cap, uint32_t *n);
/* Check both conservation invariants now. */
int cb_net_check(const cb_net *e);
const cb_pay_rec *cb_net_find(const cb_net *e, const char *msg_id);
/* ISO 20022 TxSts code for a status ("ACSP", "ACSC", "RJCT", "CANC", ...). */
const char *cb_net_txsts(int status);

#endif /* ZXV_CB_NET_H */
