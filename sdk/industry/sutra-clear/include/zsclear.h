/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zsclear.h — Sutra-Clear (PAPSS-Core): a multilateral clearing house for
 * clearing houses and central-bank engineers.
 *
 * A house takes payment instructions in two spellings:
 *
 *   ISO 20022  a strict, bounded parser of a documented subset of
 *              pacs.008.001.09 (one transaction per message; the exact
 *              element list is in README.md, "The pacs.008 subset");
 *   Sutra      a NET treaty, as Sutra text or as the fixed COBOL copybook
 *              record, read by the ABI 1.5 treaty group (zxv_treaty_parse_*):
 *              each OWE line is one instruction.
 *
 * It validates each instruction, holds conflicting ones (the same
 * end-to-end id with different content) as BOTH for a registered operator,
 * and at the end of a cycle nets the accepted ones multilaterally per
 * currency in exact minor units (zxv_netting_* where the library has it,
 * otherwise the treaty engine zxv_treaty_run with OP_NET; checked int64, an
 * overflow refused, never wrapped). The 0.08889% assurance fee comes from
 * zxv_fee_assure_carry and its 50/25/15/10 split from zxv_fee_split; the
 * kit never re-derives the formula. Net positions settle through a hub
 * account per currency, all through the house's capability
 * (zxv_ledger_post_cap), after a funds check that refuses the whole cycle
 * rather than post part of it. Settlement instructions are rendered as ISO
 * 20022 with zxv_msg_* (a pacs.008 per settlement posting and a camt.053
 * per participant statement), and the cycle is written as one canonical
 * clearing-cycle record in the house's ML-DSA-87 signed, SHA3-256
 * hash-chained record chain (zind_chain, domain "sutra-clear"), which
 * Tri-Witness replays offline.
 *
 * Designed to support PAPSS and SWIFT ISO 20022 formats. NOT CONNECTED to
 * PAPSS, SWIFT, Afreximbank or any central bank or payment system; no
 * partnership, endorsement or certification is claimed. It does not move
 * money anywhere except the libzxv ledger it is given. It never settles
 * over amateur radio.
 *
 * Integer code only (R11). Kit API 0.1 (preview). */
#ifndef ZSCLEAR_H
#define ZSCLEAR_H

#include "zind_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZSC_VERSION_MAJOR 0
#define ZSC_VERSION_MINOR 1

/* Status codes (-4100..-4199). */
#define ZSC_E_SYNTAX      (-4101) /* input outside the pacs.008 subset (byte offset in the error) */
#define ZSC_E_FIELD       (-4102) /* a field value is malformed (length, characters, amount)     */
#define ZSC_E_PARTICIPANT (-4103) /* unknown participant, or debtor agent == creditor agent      */
#define ZSC_E_CYCLE       (-4104) /* no cycle open (or one is already open)                      */
#define ZSC_E_TREATY      (-4105) /* a treaty is not a usable NET batch for this house           */
#define ZSC_E_OPERATOR    (-4106) /* the key is not a registered operator                        */
#define ZSC_E_BROKEN      (-4107) /* a posting failed mid-cycle; the house refuses all work      */

const char *zsc_status_name(int32_t status);

/* Limits. */
#define ZSC_MAX_CCY          16u
#define ZSC_MAX_PARTICIPANTS 128u
#define ZSC_MAX_CYCLE_INSTR  65536u
#define ZSC_MAX_HELD         4096u
#define ZSC_MAX_VERSIONS     4u
#define ZSC_MAX_OPERATORS    8u
#define ZSC_MAX_SETTLED      (1u << 17) /* end-to-end ids settled or decided, per house */
#define ZSC_PACS_MAX         16384u     /* longest pacs.008 input accepted (bytes)     */

/* Where an instruction came from. */
#define ZSC_SRC_API     0
#define ZSC_SRC_PACS008 1
#define ZSC_SRC_SUTRA   2
#define ZSC_SRC_RECORD  3

/* One payment instruction. Strings are NUL-terminated.
 *   msg_id, e2e     1..35 bytes of printable ASCII (0x21..0x7e)
 *   cre_dt_tm       ISO 8601: YYYY-MM-DDThh:mm:ss[.f{1,6}][Z|+hh:mm|-hh:mm]
 *   ccy             ISO 4217 alpha-3 with minor units, enabled in the house
 *   amount          minor units, 1..ZXV_AMOUNT_MAX
 *   dbtr_agt, cdtr_agt  participant codes: 1..11 of [A-Z0-9], different
 *   dbtr_nm, cdtr_nm    1..70 bytes of UTF-8, no control characters
 *   dbtr_acct, cdtr_acct  1..34 bytes of printable ASCII */
typedef struct {
    uint8_t source; /* ZSC_SRC_* (not part of the content digest) */
    char msg_id[36];
    char cre_dt_tm[36];
    char e2e[36];
    char ccy[4];
    int64_t amount;
    char dbtr_agt[12];
    char cdtr_agt[12];
    char dbtr_nm[71];
    char dbtr_acct[35];
    char cdtr_nm[71];
    char cdtr_acct[35];
} zsc_instr;

/* Field checks only (no house): ZXV_OK or ZSC_E_FIELD / ZXV_E_CURRENCY /
 * ZXV_E_RANGE / ZSC_E_PARTICIPANT (same agent twice). */
int32_t zsc_instr_check(const zsc_instr *in);
/* SHA3-256 of the payment content: e2e, ccy, amount, both agents, names
 * and accounts (zind_subject "sutra-clear/instr"). Message id, creation
 * time and source are not content: a resent message is the same payment. */
int32_t zsc_instr_digest(const zsc_instr *in, uint8_t out[32]);

/* Parse one pacs.008 message of the documented subset (at most
 * ZSC_PACS_MAX bytes; NUL bytes refused). Pure: no house, no capability.
 * ZSC_E_SYNTAX with *out_err_off (may be NULL) at the first offending byte;
 * ZSC_E_FIELD, ZXV_E_CURRENCY or ZXV_E_RANGE for a bad value. source is set
 * to ZSC_SRC_PACS008. */
int32_t zsc_parse_pacs008(const char *xml, size_t len, zsc_instr *out, size_t *out_err_off);
/* Write an instruction as a message of that subset: libzxv's pacs.008
 * builder (zxv_msg_render, which does the escaping, amount and currency
 * checks) plus the two agent elements it has no field for, DbtrAgt and
 * CdtrAgt (BICFI for an 8- or 11-character BIC, else ClrSysMmbId/MmbId).
 * The cap needs LEDGER_READ (zxv_msg_create_cap). R4 two-call buffer. */
int32_t zsc_render_pacs008(const zxv_cap *cap, const zsc_instr *in, char *buf, size_t cap_len,
                           size_t *out_len);

typedef struct zsc_house zsc_house;

/* A clearing house. house_id: 1..8 of [A-Z0-9-] (it prefixes every ledger
 * account and reference the house makes). The cap needs LEDGER_POST,
 * LEDGER_READ and CRYPTO_SIGN, and ATTEST when a quarantine log is given
 * (may be NULL). The signer and log are borrowed and must outlive it. */
int32_t zsc_house_create(const zxv_cap *cap, const char *house_id, const zind_signer *signer,
                         zxv_hexlog *qlog, zsc_house **out);
void zsc_house_destroy(zsc_house *h);

/* A settlement currency (ISO 4217 with minor units; enabled in the context
 * through the cap if needed). Opens "<house>/hub/<ccy>" and the four fee
 * accounts "<house>/reserve|dividend|infra|regen/<ccy>", and a settlement
 * account for every participant already registered. */
int32_t zsc_currency_add(zsc_house *h, const char *ccy);
/* A participant: 1..11 of [A-Z0-9] (a BIC or a clearing-system member
 * code). Opens "<house>/<code>/<ccy>" (HOLDER) in every house currency; the
 * host funds it (prefunding) from its own issuer account. */
int32_t zsc_participant_add(zsc_house *h, const char *code);
/* A key allowed to resolve held instructions. */
int32_t zsc_operator_add(zsc_house *h, const uint8_t pk[ZIND_PK_BYTES]);

/* Ledger accounts. which: ZSC_ACCT_PARTICIPANT (code required) or one of
 * the house accounts (code ignored). */
#define ZSC_ACCT_PARTICIPANT 0
#define ZSC_ACCT_HUB         1
#define ZSC_ACCT_RESERVE     2 /* ZXV_FEE_RESERVE_FLOOR  */
#define ZSC_ACCT_DIVIDEND    3 /* ZXV_FEE_VBILL_DIVIDEND */
#define ZSC_ACCT_INFRA       4 /* ZXV_FEE_INFRA_BOUNTY   */
#define ZSC_ACCT_REGEN       5 /* ZXV_FEE_REGEN_CAPITAL  */
int32_t zsc_account(const zsc_house *h, int32_t which, const char *code, const char *ccy,
                    uint32_t *out_account);

/* ===== Cycles ===== */

/* Open cycle number n+1. cycle_id: 1..16 of [A-Za-z0-9-] (a label);
 * settle_dt: the cycle's settlement time, ISO 8601 as for cre_dt_tm (the
 * kit has no clock). ZSC_E_CYCLE if one is open. */
int32_t zsc_cycle_open(zsc_house *h, const char *cycle_id, const char *settle_dt);

/* Submit one instruction to the open cycle:
 *   ZXV_OK          accepted (pending)
 *   ZIND_E_REPLAY   the same payment again (same end-to-end id and content)
 *                   in this cycle or while held, or any reuse of an
 *                   end-to-end id already settled or decided (the latter is
 *                   logged to the quarantine log as Forbidden)
 *   ZIND_E_HELD     the same end-to-end id with different content: every
 *                   version is held as BOTH (taken out of the cycle) and
 *                   logged, until an operator's signed decision
 *   ZSC_E_PARTICIPANT / ZXV_E_CURRENCY / ZSC_E_FIELD / ZXV_E_RANGE  refused
 *   ZSC_E_CYCLE     no cycle open; ZXV_E_CAPACITY a limit was reached */
int32_t zsc_submit(zsc_house *h, const zsc_instr *in);
/* zsc_parse_pacs008 then zsc_submit. */
int32_t zsc_submit_pacs008(zsc_house *h, const char *xml, size_t len, size_t *out_err_off);

/* A NET treaty as a batch: asset = a house currency, every party id a
 * registered participant code, deposits and claims 0 (the house settles
 * from prefunded accounts, not treaty deposits); weights are checked by
 * libzxv and otherwise unused. OWE k (1-based) becomes the instruction
 * with e2e "<treaty name>-<k>", msg_id = the treaty name, cre_dt_tm = the
 * cycle's settle_dt, names and accounts = the party ids. The whole treaty
 * is checked before any obligation is submitted; then each obligation is
 * submitted as by zsc_submit and counted below. */
typedef struct {
    uint32_t accepted, held, replayed;
} zsc_batch;
int32_t zsc_submit_sutra(zsc_house *h, const char *text, zsc_batch *out);
int32_t zsc_submit_record(zsc_house *h, const uint8_t *rec, size_t len, zsc_batch *out);

/* ===== Held instructions (BOTH) ===== */
int32_t zsc_held_count(const zsc_house *h, uint32_t *out_count);
/* Held group i: its end-to-end id (buf >= 36) and number of versions. */
int32_t zsc_held_at(const zsc_house *h, uint32_t i, char *e2e, size_t cap, uint32_t *out_versions);
/* Version k (0-based, arrival order) of the group for e2e, and its digest. */
int32_t zsc_held_version(const zsc_house *h, const char *e2e, uint32_t k, zsc_instr *out,
                         uint8_t out_digest[32]);
/* The canonical bytes an operator signs (context "sutra-clear/operator"):
 *   "sutra-clear/decision/v1" || house key id[32] || u8 len e2e || e2e ||
 *   u8 choice || group digest[32] || chosen digest[32] (zeros for 0)
 * choice 0 rejects every version; k (1..versions) settles version k-1 in
 * the open cycle and rejects the rest. group digest = zind_subject
 * ("sutra-clear/held", the version digests in arrival order). */
int32_t zsc_decision_message(const zsc_house *h, const char *e2e, uint32_t choice, uint8_t *buf,
                             size_t cap, size_t *out_len);
/* Apply a registered operator's signed decision (needs an open cycle).
 * ZSC_E_OPERATOR for an unregistered key, ZXV_E_VERIFY for a bad signature
 * (logged as Forbidden), ZXV_E_NOT_FOUND when nothing is held under e2e
 * (a replayed decision), ZXV_E_ARG for a choice out of range. Rejected
 * versions are logged as Forbidden; the decision goes into this cycle's
 * record, signature included. */
int32_t zsc_resolve(zsc_house *h, const uint8_t op_pk[ZIND_PK_BYTES], const char *e2e,
                    uint32_t choice, const uint8_t sig[ZIND_SIG_BYTES]);

/* Take a pending instruction out of the open cycle (an operator's call
 * through the house, e.g. after ZXV_E_FUNDS or ZXV_E_RANGE at close). Its
 * end-to-end id counts as decided: a resubmission is ZIND_E_REPLAY. It is
 * logged as Forbidden and listed in this cycle's record. ZXV_E_NOT_FOUND
 * if nothing is pending under e2e. */
int32_t zsc_withdraw(zsc_house *h, const char *e2e);

typedef struct {
    uint32_t cycle_no;
    uint32_t instructions; /* settled                                  */
    uint32_t held;         /* still held at the close                  */
    uint32_t postings;     /* ledger postings (settlement and fees)    */
    uint64_t first_entry, last_entry; /* this cycle's entry ids (0 if none) */
    uint8_t backend;       /* ZSC_NET_*                                */
    uint32_t first_record, records; /* where the cycle record sits in the chain */
} zsc_cycle_info;

#define ZSC_NET_ZXV_NETTING 1 /* zxv_netting_* (ZXV_FEAT_NETTING)       */
#define ZSC_NET_TREATY      2 /* zxv_treaty_run OP_NET, in batches      */

/* Net through zxv_netting_* on ctx (the context of the house's cap)
 * instead of the treaty engine. ZXV_E_NOTIMPL when the library does not
 * report ZXV_FEAT_NETTING (true of the ABI 1.5 library: its netting calls
 * are stubs); the house then keeps the treaty engine. The netting group
 * has no capability form, which is why the context is passed here. */
int32_t zsc_use_netting(zsc_house *h, zxv_ctx *ctx);

/* Close the open cycle: net, fee, funds check, post, render, record.
 *   ZXV_E_FUNDS  a participant cannot cover its net debit plus its fee
 *                (named in the error); NOTHING is posted, the cycle stays
 *                open: fund the account and close again
 *   ZXV_E_RANGE  a net position, a balance or a total would leave the exact
 *                range (+-ZXV_AMOUNT_MAX) or overflow int64; nothing posted
 *   ZSC_E_BROKEN a posting failed after the checks passed (another writer
 *                on the same ledger); the house stops (every later call is
 *                ZSC_E_BROKEN) and the event is logged as Forbidden */
int32_t zsc_cycle_close(zsc_house *h, zsc_cycle_info *out);
int32_t zsc_cycle_is_open(const zsc_house *h, int32_t *out_open, uint32_t *out_cycle_no);
int32_t zsc_pending_count(const zsc_house *h, uint32_t *out_count);
/* Pending instruction i (0-based, arrival order) of the open cycle. */
int32_t zsc_pending_at(const zsc_house *h, uint32_t i, zsc_instr *out);

/* ===== Results of the last closed cycle ===== */
/* Net position (+ receives, - pays) of a participant in a currency. */
int32_t zsc_position(const zsc_house *h, const char *code, const char *ccy, int64_t *out_net);
typedef struct {
    uint64_t gross;    /* sum of the instructions it sent        */
    uint64_t rem_in;   /* carry before the cycle (0 <= r < 10^7) */
    uint64_t fee;      /* the assurance fee                       */
    uint64_t rem_out;  /* carry stored for the next cycle         */
    uint64_t split[4]; /* ZXV_FEE_* buckets                       */
} zsc_fee_info;
int32_t zsc_fee(const zsc_house *h, const char *code, const char *ccy, zsc_fee_info *out);

#define ZSC_POST_PAYIN  1 /* participant -> hub (net debit)  */
#define ZSC_POST_PAYOUT 2 /* hub -> participant (net credit) */
#define ZSC_POST_FEE    3 /* participant -> fee bucket        */
typedef struct {
    uint8_t kind;   /* ZSC_POST_*                  */
    uint8_t bucket; /* ZXV_FEE_* for a fee posting  */
    char ccy[4];
    char code[12];  /* the participant              */
    int64_t amount;
    uint64_t entry_id;
    char ref[36];   /* the ledger reference         */
} zsc_posting;
int32_t zsc_posting_count(const zsc_house *h, uint32_t *out_count);
int32_t zsc_posting_at(const zsc_house *h, uint32_t i, zsc_posting *out);

#define ZSC_ISO_PACS008 1 /* one settlement posting   */
#define ZSC_ISO_CAMT053 2 /* one participant statement */
int32_t zsc_iso_count(const zsc_house *h, uint32_t *out_count);
/* The XML of ISO output i (R4 two-call buffer) and its kind. */
int32_t zsc_iso_at(const zsc_house *h, uint32_t i, int32_t *out_kind, char *buf, size_t cap,
                   size_t *out_len);

/* The fee carry of a participant in a currency (what the next cycle starts
 * from). */
int32_t zsc_carry(const zsc_house *h, const char *code, const char *ccy, uint64_t *out_rem);

/* The house's signed record chain (borrowed). Record kinds: */
#define ZSC_REC_CYCLE 1 /* one fragment of a clearing-cycle record */
#define ZSC_FRAG_MAX  8000u
const zind_chain *zsc_records(const zsc_house *h);

/* The whole ledger journal of the cap's context, for Tri-Witness (needs
 * LEDGER_READ). Format "ZSJ1" (README.md, "Exports"): every account (id,
 * name, currency, kind) and every posting in entry order (id, from, to,
 * amount, reference). buf may be NULL to ask the size (ZXV_E_BUFFER). */
int32_t zsc_journal_export(const zxv_cap *cap, uint8_t *buf, size_t cap_len, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* ZSCLEAR_H */
