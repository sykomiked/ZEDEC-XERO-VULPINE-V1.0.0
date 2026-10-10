/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cardnet.h — Dragon, Phoenix and Thunderbird: three in-house CHARGE-card
 * networks for the nine forms of capital and the triple-ledger rails.
 *
 * WHAT IT IS. A freestanding issuer model. An issuer runs one network
 * (Dragon, Phoenix or Thunderbird), opens holder accounts, issues cards,
 * authorizes card transactions, settles them onto the ledger, closes
 * statement cycles and takes payments. All state is in one fixed-size
 * cn_issuer_t; nothing allocates, nothing calls libc, nothing divides 64-bit.
 *
 * CHARGE, NOT CREDIT. Every statement is due IN FULL by its due day. There
 * is no interest rate, no APR, no minimum payment, no revolving balance and
 * no late fee anywhere in the data model: there is no field that could hold
 * one. An unpaid statement past its due day makes the account DELINQUENT,
 * which stops new authorizations; the amount owed never grows with time.
 * The only fee the model can express is a flat, disclosed amount per settled
 * charge, paid by the MERCHANT, default 0 (cn_fee_t). cn_fee_for_charge()
 * takes neither a balance nor a date, so it cannot compute interest.
 *
 * NUMBERS (VSS proposal, section 2.5 and invariant C5). A PAN is 16 digits:
 *   [network prefix 3][issuer code 3][account digits 9][check 1]
 *   Dragon 880 + Luhn, Phoenix 882 + Damm, Thunderbird 884 + Verhoeff.
 * A PAN is valid for network X only if it carries X's prefix, passes X's
 * check AND fails both other networks' checks. Minting skips any candidate
 * that would pass a foreign check, so every issued PAN satisfies C5 in its
 * strong form ("passes its own algorithm, fails every other one"), and no
 * 16-digit string can be valid for two networks (cn_pan_network()).
 *
 * KEYS. Each card binds a holder ML-DSA-65 public key (FIPS 204, the vendored
 * reference code in kernel/src/pqsec). Every authorization carries a fresh
 * holder signature over a canonical challenge (PAN, ATC, amount, rail, form,
 * merchant, terminal, terminal unpredictable number, time): a dynamic,
 * post-quantum analogue of the EMV ARQC. The ATC must strictly increase.
 *
 * SELF-SERVICE REPLACEMENT. cn_reissue() mints a new PAN for the same account
 * and revokes the old card in one step: the new card is built fully first,
 * and only then are both table entries written, so a failure changes nothing.
 * New PANs come from SHAKE256(issuer seed, caller entropy, account,
 * generation, attempt): deterministic, no kernel RNG needed.
 *
 * LEDGER. Settling a charge posts DR rail 555 (holder receivable) and
 * CR rail 777 (merchant payable, plus the flat fee leg if any). A refund
 * posts the mirror (DR 777 / CR 555); a holder payment posts DR 555 (issuer
 * cash) / CR 777 (holder obligation discharged). C1 is checked two ways per
 * transaction, both to the minor unit: total DR == total CR, and total on
 * rail 555 == total on rail 777. Every posted transaction emits exactly one
 * hash-chained receipt naming rail 888 as its equity coordinate (VSS C2).
 *
 * NINE FORMS. Cards are tagged with one capital form and only authorize that
 * form. The four state-reserved forms (Social, Natural, Heritage/Intellectual,
 * Governance/Institutional) are inalienable in this repository (see
 * kernel/src/zcapital/zcapital.h), so their limits must be 0 and no card can
 * be issued for them; the five priceable forms carry per-transaction and
 * per-cycle limits.
 *
 * HONEST LIMITS. Dragon, Phoenix and Thunderbird are names inside this
 * deployment. They are NOT members of, affiliated with or certified by Visa,
 * Mastercard, EMVCo, any card scheme or any mobile-money operator. The
 * 880/882/884 prefixes sit in the ISO/IEC 7812 "8" major-industry range and
 * have NOT been registered with the ISO/IEC 7812 registration authority;
 * PANs are valid only inside one deployment until that is coordinated. This
 * code holds no licence and proves no compliance (PCI DSS, PSD2, consumer
 * credit law, AML/KYC, or any usury statute): running a card program needs
 * those, and counsel. Whether a given charge-card program is lawful and
 * interest-free in a given jurisdiction is a legal question; this code only
 * guarantees that its own arithmetic contains no interest term. Receipts
 * are hash-chained but not issuer-signed here; PAN uniqueness is checked
 * only against cards held in this issuer table; holder private keys never
 * enter this module, and key custody (secure element, HSM, or custodial
 * signing for feature phones, see cn_mobile.h) is the host's problem.
 */
#ifndef ZXV_CARDNET_H
#define ZXV_CARDNET_H

#include <stdint.h>
#include <stdbool.h>
#include "cn_check.h"

/* ML-DSA-65 sizes, mirrored from kernel/src/pqsec/pq_security.h so this
 * header stays include-light; cardnet.c checks them against the source. */
#define CN_PK_BYTES  1952u
#define CN_SK_BYTES  4032u
#define CN_SIG_BYTES 3309u

#define CN_PAN_LEN          16u
#define CN_PREFIX_LEN       3u
#define CN_ISSUER_CODE_LEN  3u
#define CN_MAX_ACCOUNTS     8u
#define CN_MAX_CARDS        16u
#define CN_MAX_AUTHS        32u
#define CN_MAX_TXNS         64u
#define CN_MAX_LEGS         192u
#define CN_MAX_RECEIPTS     64u
#define CN_MAX_STATEMENTS   32u
#define CN_TID_LEN          8u  /* ISO 8583 field 41 */
#define CN_MID_LEN          15u /* ISO 8583 field 42 */
#define CN_CHALLENGE_MAX    96u
#define CN_FEE_MAX_MINOR    1000000ull      /* hard cap on the flat merchant fee */
#define CN_AMOUNT_MAX_MINOR 999999999999ull /* 12 digits: ISO 8583 field 4 */

/* Vino rails (canonical numerics: kernel/src/pay/pay_rails.h). */
#include "../pay/pay_rails.h"
#define CN_RAIL_DEBIT  ZXV_RAIL_DEBIT  /* 555 */
#define CN_RAIL_CREDIT ZXV_RAIL_CREDIT /* 777 */
#define CN_RAIL_EQUITY ZXV_RAIL_EQUITY /* 888 */

/* ===== Networks ===== */
typedef enum {
    CN_NET_DRAGON = 0,      /* prefix 880, Luhn */
    CN_NET_PHOENIX = 1,     /* prefix 882, Damm */
    CN_NET_THUNDERBIRD = 2, /* prefix 884, Verhoeff */
    CN_NET_COUNT = 3,
    CN_NET_NONE = 255
} cn_network_t;

const char *cn_network_name(cn_network_t net);   /* "Dragon", ... or "none" */
const char *cn_network_prefix(cn_network_t net); /* "880", ... or "" */
/* Check digit for `len` payload digits under the network's algorithm. */
int cn_network_check_digit(cn_network_t net, const char *digits, uint32_t len);
/* The network's own algorithm only (no prefix, no exclusivity). */
bool cn_network_check_valid(cn_network_t net, const char *digits, uint32_t len);
/* Full C5 rule: 16 digits, the network's prefix, its own check passes and
 * both foreign checks fail. */
bool cn_pan_valid(cn_network_t net, const char *pan, uint32_t len);
/* The unique network a PAN is valid for, or CN_NET_NONE. */
cn_network_t cn_pan_network(const char *pan, uint32_t len);

/* ===== Capital forms: aliases of the canonical zcap_form_t =====
 * (kernel/src/zcapital/zcap_forms.h; the same values as capital_form_t in
 * finance/capital_forms.h). The ISO 8583 field 60 "FORM=<0-8>" digit is this
 * canonical index. */
#include "../zcapital/zcap_forms.h"
typedef enum {
    CN_FORM_FINANCIAL = ZCAP_FINANCIAL,
    CN_FORM_MATERIAL = ZCAP_MANUFACTURED,
    CN_FORM_KNOWLEDGE = ZCAP_INTELLECTUAL,
    CN_FORM_LIVING = ZCAP_HUMAN,
    CN_FORM_SOCIAL = ZCAP_SOCIAL,
    CN_FORM_NATURAL = ZCAP_NATURAL,
    CN_FORM_HERITAGE = ZCAP_CULTURAL,
    CN_FORM_GOVERNANCE = ZCAP_SPIRITUAL,
    CN_FORM_BUILT = ZCAP_SYSTEM,
    CN_FORM_COUNT = ZCAP_FORM_COUNT
} cn_form_t;
_Static_assert((int) CN_FORM_FINANCIAL == (int) ZCAP_FINANCIAL, "cardnet FINANCIAL");
_Static_assert((int) CN_FORM_MATERIAL == (int) ZCAP_MANUFACTURED, "cardnet MATERIAL");
_Static_assert((int) CN_FORM_KNOWLEDGE == (int) ZCAP_INTELLECTUAL, "cardnet KNOWLEDGE");
_Static_assert((int) CN_FORM_LIVING == (int) ZCAP_HUMAN, "cardnet LIVING");
_Static_assert((int) CN_FORM_SOCIAL == (int) ZCAP_SOCIAL, "cardnet SOCIAL");
_Static_assert((int) CN_FORM_NATURAL == (int) ZCAP_NATURAL, "cardnet NATURAL");
_Static_assert((int) CN_FORM_HERITAGE == (int) ZCAP_CULTURAL, "cardnet HERITAGE");
_Static_assert((int) CN_FORM_GOVERNANCE == (int) ZCAP_SPIRITUAL, "cardnet GOVERNANCE");
_Static_assert((int) CN_FORM_BUILT == (int) ZCAP_SYSTEM, "cardnet BUILT");
_Static_assert((int) CN_FORM_COUNT == ZCAP_FORM_COUNT, "cardnet count");

/* Priceable = not one of the four inalienable Crown forms. */
static inline bool cn_form_priceable(uint32_t f)
{
    return f < (uint32_t) CN_FORM_COUNT && !zcap_form_is_crown(f);
}

/* ===== Fees: flat only, merchant-paid, default zero =====
 * There is deliberately no rate, no period, no basis and no balance field.
 * kind is a closed set; cn_fee_validate() rejects anything else. */
typedef enum {
    CN_FEE_NONE = 0,        /* amount_minor must be 0 */
    CN_FEE_FLAT_PER_TXN = 1 /* amount_minor per settled charge, <= CN_FEE_MAX_MINOR */
} cn_fee_kind_t;

typedef struct {
    uint32_t kind; /* cn_fee_kind_t */
    uint64_t amount_minor;
} cn_fee_t;

bool cn_fee_validate(const cn_fee_t *fee);
/* Fee on one settled charge: the flat amount if it is strictly less than
 * the charge, else 0 (waived, so a merchant payout is never negative). Its
 * only inputs are the schedule and the charge amount: no balance, no time. */
uint64_t cn_fee_for_charge(const cn_fee_t *fee, uint64_t charge_minor);

/* ===== Result codes ===== */
typedef enum {
    CN_OK = 0,
    CN_ERR_ARG = -1,    /* NULL, bad length, out-of-range value */
    CN_ERR_CONFIG = -2, /* configuration rejected by cn_cfg_validate */
    CN_ERR_FULL = -3,   /* a fixed table is full; nothing changed */
    CN_ERR_NOT_FOUND = -4,
    CN_ERR_STATE = -5,    /* operation not allowed in the current state */
    CN_ERR_FORM = -6,     /* capital form not priceable or not configured */
    CN_ERR_MINT = -7,     /* no exclusive, unique PAN found in the attempt budget */
    CN_ERR_OVERFLOW = -8, /* amount arithmetic would overflow */
    CN_ERR_AMOUNT = -9    /* payment larger than what is owed, or zero */
} cn_rc_t;

/* ===== Issuer configuration ===== */
typedef struct {
    uint32_t network;                         /* cn_network_t */
    char issuer_code[CN_ISSUER_CODE_LEN + 1]; /* 3 digits, PAN positions 4-6 */
    uint8_t issuer_seed[32];                  /* secret, mixes into PAN derivation */
    uint32_t validity_months;                 /* 1..120 */
    uint32_t cycle_days;                      /* statement cycle, 7..62 */
    uint32_t grace_days;                      /* close -> due, 1..62; pay in full */
    uint64_t per_txn_limit[CN_FORM_COUNT];    /* 0 = form disabled */
    uint64_t per_cycle_limit[CN_FORM_COUNT];
    uint64_t account_ceiling; /* max owed (held + unbilled + billed) per account */
    cn_fee_t merchant_fee;
} cn_issuer_cfg_t;

/* Defaults: 36-month validity, 30-day cycle, 21-day grace, fee NONE,
 * state-reserved forms 0, priceable forms 1,000,000 minor per transaction
 * and 5,000,000 per cycle, ceiling 10,000,000, issuer code "001". */
void cn_cfg_default(cn_issuer_cfg_t *cfg, cn_network_t net);
cn_rc_t cn_cfg_validate(const cn_issuer_cfg_t *cfg);

/* ===== Time =====
 * Seconds since 2000-01-01T00:00:00Z (uint32: good to 2136). Days are
 * cn_time / 86400. Calendar conversion uses 32-bit arithmetic only. */
typedef uint32_t cn_time_t;
#define CN_SECS_PER_DAY 86400u
typedef struct {
    uint32_t year;
    uint32_t month, day, hour, minute, second;
} cn_civil_t;
void cn_civil_from_time(cn_time_t t, cn_civil_t *out);
/* Returns false for a date outside 2000-01-01 .. 2135-12-31 or invalid. */
bool cn_time_from_civil(const cn_civil_t *c, cn_time_t *out);

/* ===== Records ===== */
typedef enum { CN_ACCT_ACTIVE = 1, CN_ACCT_DELINQUENT = 2, CN_ACCT_CLOSED = 3 } cn_acct_state_t;

typedef struct {
    bool used;
    uint32_t state; /* cn_acct_state_t */
    uint32_t cycle_start_day;
    uint32_t due_day;                    /* of the last statement; meaningful while billed > 0 */
    uint64_t held;                       /* approved, not yet settled */
    uint64_t unbilled;                   /* settled this cycle */
    uint64_t billed;                     /* closed statements not yet paid; due IN FULL */
    uint64_t cycle_spent[CN_FORM_COUNT]; /* approved this cycle, per form */
    uint32_t generation;                 /* PAN derivations so far, feeds the next one */
} cn_account_t;

typedef enum { CN_CARD_ACTIVE = 1, CN_CARD_FROZEN = 2, CN_CARD_REVOKED = 3 } cn_card_state_t;

typedef struct {
    bool used;
    char pan[CN_PAN_LEN + 1];
    uint32_t state;    /* cn_card_state_t */
    uint32_t account;  /* index into cn_issuer_t.accounts */
    uint32_t form;     /* cn_form_t, the capital-form tag */
    uint32_t exp_year; /* valid through the last day of exp_month */
    uint32_t exp_month;
    uint16_t rail_debit, rail_credit, rail_equity; /* 555 / 777 / 888 */
    uint32_t atc;      /* last accepted application transaction counter */
    uint32_t replaces; /* card index this one replaced, or UINT32_MAX */
    cn_time_t issued_at;
    uint8_t holder_pk[CN_PK_BYTES];
} cn_card_t;

/* Authorization request: what the holder's device signs. */
typedef struct {
    char pan[CN_PAN_LEN + 1];
    uint64_t amount_minor; /* 1 .. CN_AMOUNT_MAX_MINOR */
    uint32_t currency;     /* must be CN_RAIL_DEBIT (555) */
    uint32_t form;         /* cn_form_t */
    uint32_t atc;          /* 1..65535, strictly increasing per card */
    uint8_t un[4];         /* terminal unpredictable number (EMV 9F37) */
    cn_time_t time;
    uint32_t stan;                    /* 0..999999 (ISO 8583 field 11) */
    char terminal_id[CN_TID_LEN + 1]; /* exactly 8 printable chars */
    char merchant_id[CN_MID_LEN + 1]; /* exactly 15 printable chars */
} cn_auth_req_t;

/* Canonical challenge bytes for req (domain "ZXV-CARDNET-ARQC-v1"). */
cn_rc_t cn_challenge(const cn_auth_req_t *req, uint8_t out[CN_CHALLENGE_MAX], uint32_t *out_len);
/* The ML-DSA context string used when signing / verifying the challenge. */
#define CN_SIG_CTX     "ZXV-CARDNET-ARQC-v1"
#define CN_SIG_CTX_LEN 19u
/* Holder-side helper: build the challenge and sign it. rnd may be NULL
 * (deterministic signing). sk never touches issuer state. */
cn_rc_t cn_holder_sign(const uint8_t sk[CN_SK_BYTES], const cn_auth_req_t *req,
                       const uint8_t *rnd32, uint8_t sig[CN_SIG_BYTES]);
/* 8-byte cryptogram (EMV tag 9F26 slot): first 8 bytes of SHA3-256(sig). */
void cn_cryptogram(const uint8_t sig[CN_SIG_BYTES], uint8_t out[8]);

/* Decline reasons, each with a conventional ISO 8583 field-39 code. */
typedef enum {
    CN_DECL_NONE = 0,     /* "00" approved */
    CN_DECL_BAD_PAN,      /* "14" not a valid PAN for this network */
    CN_DECL_UNKNOWN_CARD, /* "14" */
    CN_DECL_REVOKED,      /* "14" replaced or revoked */
    CN_DECL_FROZEN,       /* "62" restricted card */
    CN_DECL_EXPIRED,      /* "54" */
    CN_DECL_BAD_SIG,      /* "05" do not honor: cryptogram failed */
    CN_DECL_REPLAY,       /* "94" duplicate: ATC not increasing */
    CN_DECL_FORM,         /* "57" form not permitted on this card */
    CN_DECL_CURRENCY,     /* "57" not rail 555 */
    CN_DECL_TXN_LIMIT,    /* "61" exceeds per-transaction limit */
    CN_DECL_CYCLE_LIMIT,  /* "51" exceeds per-cycle limit for the form */
    CN_DECL_CEILING,      /* "51" exceeds account ceiling */
    CN_DECL_DELINQUENT,   /* "05" statement unpaid past due day */
    CN_DECL_FORMAT,       /* "30" malformed request */
    CN_DECL_SYSTEM        /* "96" table full */
} cn_decline_t;

const char *cn_decline_rc(cn_decline_t d); /* two ASCII digits */

typedef enum {
    CN_AUTH_APPROVED = 1,
    CN_AUTH_DECLINED = 2,
    CN_AUTH_SETTLED = 3,
    CN_AUTH_REVERSED = 4
} cn_auth_state_t;

typedef struct {
    bool used;
    uint32_t state;   /* cn_auth_state_t */
    uint32_t decline; /* cn_decline_t */
    char rc[3];       /* ISO 8583 field 39 */
    char approval[7]; /* ISO 8583 field 38, 6 alphanumerics when approved */
    uint32_t card;    /* card index, UINT32_MAX if unknown */
    uint32_t account;
    cn_auth_req_t req;
    uint8_t cryptogram[8];
    uint32_t txn; /* ledger transaction once settled / reversed */
} cn_auth_t;

/* ===== Ledger ===== */
typedef enum { CN_DR = 1, CN_CR = 2 } cn_side_t;
typedef enum {
    CN_LACCT_HOLDER_RECEIVABLE = 1, /* what holders owe the issuer */
    CN_LACCT_MERCHANT_PAYABLE = 2,  /* what the issuer owes merchants */
    CN_LACCT_NETWORK_FEES = 3,      /* flat merchant fees earned */
    CN_LACCT_ISSUER_CASH = 4        /* holder payments received */
} cn_lacct_t;

typedef struct {
    uint32_t txn;
    uint32_t side;    /* cn_side_t */
    uint32_t rail;    /* 555 or 777; see LEDGER in the header comment */
    uint32_t account; /* cn_lacct_t */
    uint64_t amount;
} cn_leg_t;

typedef enum { CN_TXN_CHARGE = 1, CN_TXN_REFUND = 2, CN_TXN_PAYMENT = 3 } cn_txn_kind_t;

/* Layer-3 companion record (VSS C2), append-only, hash-chained. */
typedef struct {
    uint32_t id;
    uint32_t txn;
    uint32_t kind;    /* cn_txn_kind_t */
    uint32_t account; /* holder account */
    uint32_t auth;    /* auth index, UINT32_MAX for a payment */
    char merchant_id[CN_MID_LEN + 1];
    uint64_t amount;
    uint64_t fee;
    uint32_t equity_rail; /* 888 */
    uint32_t form;
    cn_time_t time;
    uint8_t cryptogram[8]; /* holder cryptogram, zero for a payment */
    uint8_t prev[32];      /* digest of the previous receipt (zero for the first) */
    uint8_t digest[32];    /* SHA3-256 over this receipt's fields and prev */
} cn_receipt_t;

typedef struct {
    uint32_t id;
    uint32_t account;
    uint32_t open_day, close_day, due_day;
    uint64_t new_charges; /* settled charges minus refunds in the cycle */
    uint64_t amount_due;  /* everything owed at close; due IN FULL */
} cn_statement_t;

/* ===== Issuer ===== */
typedef struct {
    cn_issuer_cfg_t cfg;
    cn_account_t accounts[CN_MAX_ACCOUNTS];
    cn_card_t cards[CN_MAX_CARDS];
    cn_auth_t auths[CN_MAX_AUTHS];
    uint32_t n_auths;
    cn_leg_t legs[CN_MAX_LEGS];
    uint32_t n_legs;
    uint32_t n_txns;
    cn_receipt_t receipts[CN_MAX_RECEIPTS];
    uint32_t n_receipts;
    cn_statement_t statements[CN_MAX_STATEMENTS];
    uint32_t n_statements;
} cn_issuer_t;

/* Validates cfg and resets every table. */
cn_rc_t cn_issuer_init(cn_issuer_t *iss, const cn_issuer_cfg_t *cfg);

cn_rc_t cn_open_account(cn_issuer_t *iss, cn_time_t now, uint32_t *out_account);

/* Issue a card for `form` (must be priceable with a non-zero cycle limit).
 * entropy: 32 caller-supplied bytes. */
cn_rc_t cn_issue_card(cn_issuer_t *iss, uint32_t account, uint32_t form,
                      const uint8_t holder_pk[CN_PK_BYTES], const uint8_t entropy[32],
                      cn_time_t now, uint32_t *out_card);

/* Self-service replacement: new PAN, same account and form; the old card is
 * revoked in the same step. new_pk NULL keeps the old holder key. Fails with
 * no change at all if no exclusive unique PAN can be minted or no slot is
 * free (a slot is free if unused or holding a card revoked earlier). */
cn_rc_t cn_reissue_card(cn_issuer_t *iss, uint32_t card, const uint8_t *new_pk,
                        const uint8_t entropy[32], cn_time_t now, uint32_t *out_card);

cn_rc_t cn_set_frozen(cn_issuer_t *iss, uint32_t card, bool frozen);

/* Find a card by PAN (any state). Returns CN_ERR_NOT_FOUND if absent. */
cn_rc_t cn_find_card(const cn_issuer_t *iss, const char *pan, uint32_t *out_card);

/* Authorize. Always records the outcome (approved or declined) unless the
 * auth table is full; *out_auth receives its index. Returns CN_OK when a
 * record was written (check auth.state), CN_ERR_FULL otherwise. */
cn_rc_t cn_authorize(cn_issuer_t *iss, const cn_auth_req_t *req, const uint8_t sig[CN_SIG_BYTES],
                     uint32_t *out_auth);

/* Settle an approved authorization: post DR 555 / CR 777 legs, emit receipt. */
cn_rc_t cn_settle(cn_issuer_t *iss, uint32_t auth);

/* Reverse (ISO 8583 0400): an APPROVED auth releases its hold; a SETTLED one
 * posts the mirror legs (refund) and a receipt. */
cn_rc_t cn_reverse(cn_issuer_t *iss, uint32_t auth, cn_time_t now);

/* Advance the clock: closes every cycle that has run cycle_days, writing a
 * statement, and marks accounts whose billed amount is past due DELINQUENT.
 * The amount owed is never changed by this call. */
cn_rc_t cn_advance(cn_issuer_t *iss, cn_time_t now);

/* Holder payment: applied to billed first, then unbilled. amount must be
 * 1 .. (billed + unbilled). Clears DELINQUENT once billed reaches 0. */
cn_rc_t cn_pay(cn_issuer_t *iss, uint32_t account, uint64_t amount, cn_time_t now);

/* Total owed by an account (held + unbilled + billed). */
uint64_t cn_account_owed(const cn_issuer_t *iss, uint32_t account);

/* VSS C1 over one transaction / every transaction. */
bool cn_txn_balanced(const cn_issuer_t *iss, uint32_t txn);
bool cn_ledger_balanced(const cn_issuer_t *iss);
/* C2: one receipt per transaction and the hash chain recomputes. */
bool cn_receipts_verify(const cn_issuer_t *iss);

#endif /* ZXV_CARDNET_H */
