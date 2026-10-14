/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* royalty.h — IP royalties from realised sales only, with an exact
 * derivative-work upstream chain, a largest-remainder owner split and an
 * opt-in giving / investing split, every payment shown as a quote first, on
 * pay_ledger.
 *
 * AGREED DESIGN (docs/ARCHITECTURE.md): "IP royalties, contribution grants,
 * giving and investing split: value only from realised royalties; grants
 * capped by reserve surplus; split opt-in and shown before confirming."
 * Contribution grants are kernel/src/grants (not duplicated here; R12 is the
 * only hook). The old community_chest split once summed to 105% of a price
 * (docs/AUDIT_REPORT.md); every split in this module sums EXACTLY to what it
 * splits, and roy_check proves it per work over its whole life.
 *
 * NO USURY, NO ADVANCES. Royalties are computed and paid only from a sale or
 * use that is already realised on the ledger (R5). There is no advance
 * against future royalties, no future-royalty note, no securitisation or
 * sale of unrealised royalties, and nothing accrues with time or balance: no
 * interest, no late charge, no minimum guarantee. There is no function here
 * that pays anything before a sale.
 *
 *   R1 WORKS. A work is identified by a 32-byte content hash (computed by the
 *      caller). roy_register records a manifest (roy_manifest_t: id, version
 *      >= 1, 1..ROY_MAX_OWNERS owners, optional parent and upstream share,
 *      licence terms) signed by its FIRST owner through the verify hook over
 *      roy_manifest_msg:
 *          "zxv/royalty-work/v1" || id || version || n_owners ||
 *          8 x (owner id, account, weight) (unused slots zero) ||
 *          has_parent || parent id || up_num || up_den ||
 *          terms kind || terms num || terms den || terms flat
 *      (integers little-endian; ROY_MANIFEST_MSG bytes). Owner ids are
 *      distinct and every weight is > 0 with a sum that fits 64 bits; owner
 *      accounts must be active accounts of the fee account's asset and
 *      capital form, other than the fee account (several owners may share an
 *      account). A work id already registered is refused (ROY_ERR_DUP).
 *   R2 DERIVATIVE WORKS. A manifest may name a parent work and an upstream
 *      share up_num/up_den, 0 < up_num <= up_den (no parent: both 0). The
 *      parent must already be registered, so a cycle cannot form; depth (a
 *      root is 0, a child is its parent's depth + 1) is at most
 *      ROY_MAX_DEPTH. Parent and upstream share are fixed at registration:
 *      a later version may not change them (the parent's owners never signed
 *      a change).
 *   R3 TERMS. ROY_TERMS_PERCENT takes num/den of the gross, 0 < num <= den;
 *      ROY_TERMS_FLAT takes `flat` minor units per realised use, flat > 0
 *      (num and den 0). ROY_TERMS_ZXV_HW_COMMERCIAL is the ZXV Hardware
 *      License 1.0 section 4.3 term for commercial manufacture: 18/100 of
 *      gross revenue. (That section names ILLUMAHEART FOUNDATION as payee;
 *      this module hardcodes no payee: the payee is whatever owner set the
 *      registered manifest names.)
 *   R4 VERSIONS. Terms, owners, weights and accounts change only through
 *      roy_update with a new manifest for the same id whose version is
 *      strictly greater, the same parent and upstream share, signed over
 *      roy_manifest_msg by EVERY current owner (sigs[i] by current owner i).
 *      An owner id present in both versions keeps its R8 settings; others
 *      start at none. Old manifests can never be replayed (version).
 *   R5 REALISED ONLY. roy_pay takes the payer's account, the gross amount,
 *      and a receipt id: the provenance hash (pay_receipt_t.hash) of the
 *      ledger posting that realised the sale or use. It must be a journal
 *      record still in the ledger's window, not reversed, whose lines give
 *      the payer a net DEBIT gain of at least `gross` (the payer received at
 *      least the gross it declares). A receipt id is consumed once
 *      (ROY_ERR_DUP), and so is the hash of every royalty posting this
 *      module makes, so a royalty payout can never be presented as a sale.
 *      0 < gross < PAY_BAL_MAX.
 *   R6 ROYALTY. PERCENT: R = floor(gross * num / den) (the remainder is not
 *      royalty: it stays with the payer). FLAT: R = flat and gross >= flat
 *      is required (a royalty never exceeds what was realised).
 *   R7 ORDER OF DEDUCTIONS. (a) fee = pay_assure_fee(R), the 0.08889%
 *      assurance fee on the royalty, to the module's fee account (its
 *      sub-unit remainder is not carried: it stays in the royalty). (b) x0 =
 *      R - fee reaches the paid work (level 0). At level i with an upstream
 *      share n/d to its parent, u = floor(x_i * n / d) goes up as x_{i+1}
 *      and the level keeps k_i = x_i - u (the floor's remainder stays with
 *      the child); the root keeps all it receives. (c) Each level's k_i is
 *      split among that work's owners by weight with the LARGEST-REMAINDER
 *      method: q_j = floor(k * w_j / W), and the k - sum q_j (< n) leftover
 *      units go one each to the largest remainders k * w_j mod W, ties to
 *      the lower owner index; so sum q_j == k exactly.
 *          R == fee + sum_i k_i == fee + sum over owners of their gross.
 *   R8 GIVING / INVESTING, OPT-IN. Each owner may direct giving_bps and
 *      invest_bps (sum <= 10000) of their own payout to a giving account and
 *      an investing account of their choosing (routing only: what the
 *      investing account buys is out of scope). DEFAULT 0: nothing is
 *      diverted unless the owner signs roy_split_msg:
 *          "zxv/royalty-split/v1" || work id || work version || owner id ||
 *          giving acct || giving bps || invest acct || invest bps || seq
 *      with seq above the owner's last and the version equal to the work's
 *      current one (so an old opt-in cannot be replayed after an update).
 *      For an owner gross a: giving = floor(a * gbps / 10000), investing =
 *      floor(a * ibps / 10000), net = a - giving - investing (exact). A bps
 *      of 0 requires its account to be 0; a nonzero bps needs an active
 *      account of the module's asset and form, not the fee account.
 *   R9 ONE POSTING. A payment is ONE atomic pay_ledger posting, at most
 *      PAY_MAX_LINES (8) lines: the payer's line plus one line per DISTINCT
 *      receiving account (amounts to the same account are merged; zero
 *      amounts post no line). So the module requires, for every registered
 *      work at every moment, 1 + |{fee account} U {owner accounts of every
 *      level of its chain} U {giving / investing accounts with bps > 0}|
 *      <= PAY_MAX_LINES; any register, update or opt-in that would break
 *      this for any work is refused (ROY_ERR_LINES) with nothing changed.
 *      ROY_MAX_OWNERS and ROY_MAX_DEPTH are therefore reachable only when
 *      accounts are shared. The payer may not be a receiving account.
 *   R10 QUOTE. roy_quote(work, gross) returns the whole breakdown: royalty,
 *      fee, each level's received / upstream / kept amounts, each owner's
 *      gross, giving, investing and net with their accounts, the merged
 *      ledger legs, and a 32-byte digest: SHA-256 over
 *          "zxv/royalty-quote/v1" || fee account || gross || royalty || fee
 *          || n_levels || per level: work id, version, terms, up share,
 *          received, upstream, kept, n_owners, per owner: id, account,
 *          weight, giving acct/bps, invest acct/bps, opt-in seq, gross,
 *          giving, investing, net
 *      (little-endian). Any change of terms, weights, accounts, opt-ins or
 *      versions anywhere in the chain changes the digest.
 *   R11 PAY. roy_pay recomputes the quote and refuses (ROY_ERR_STALE_QUOTE)
 *      unless its digest equals the one the payer was shown; then R5 checks,
 *      then ONE posting (payer -R, merged legs +). A refused call changes
 *      nothing: no counter, no receipt, no balance. R == 0 (a small gross on
 *      a PERCENT term) posts nothing, consumes the receipt, counts the gross
 *      and returns ROY_ZERO.
 *   R12 GRANTS CAP. kernel/src/grants caps each distribution by its own pool
 *      balance (times an operator fraction), not by the reserve surplus. The
 *      reserve-surplus cap of the agreed design is applied to what a caller
 *      credits into that pool: roy_grant_cap(surplus, requested) =
 *      min(surplus, requested). Nothing else of grants is duplicated.
 *   R13 AUDIT. Per work, lifetime counters: payments, gross realised,
 *      royalty, fee, upstream in, upstream out, owners paid (net), giving,
 *      investing. roy_check verifies for every work
 *          royalty <= gross,
 *          royalty + up_in == fee + up_out + owners + giving + investing,
 *      and over all works sum up_out == sum up_in, plus the structure (R1,
 *      R2, R8, R9) and pay_ledger_check. Every counter update is overflow-
 *      checked before anything posts.
 *
 * HONEST LIMITS. Registering a hash proves who signed a manifest, not who
 * authored or owns the work; disputes over authorship or ownership are for
 * people and courts. Royalties may be subject to tax, withholding and
 * licensing law in each jurisdiction (needs legal review); this module
 * withholds nothing. The 18% hardware royalty rests on contract, copyright
 * and trademark (the licence's own terms), not on patents; its collection
 * terms outside a realised sale (reports, audits, the licence's own remedies)
 * are not modelled here and nothing here computes interest. A receipt is
 * checked only against the ledger's journal window (PAY_JOURNAL_MAX
 * postings); the payer's declared gross is checked to be no more than it
 * received in that posting, not that it is the whole sale. The verify hook is
 * the caller's (ML-DSA in production). Designed to support honest royalty
 * accounting; it claims no legal or regulatory status. Freestanding C11: no
 * allocation, no floating point, no libc.
 */
#ifndef ZXV_ROYALTY_H
#define ZXV_ROYALTY_H

#include <stdbool.h>
#include <stdint.h>
#include "../pay/pay_ledger.h"

#define ROY_MAX_WORKS    32u
#define ROY_MAX_OWNERS   8u
#define ROY_MAX_DEPTH    8u /* R2: ancestors of a work */
#define ROY_MAX_LEVELS   (ROY_MAX_DEPTH + 1u)
#define ROY_MAX_PAYOUTS  (ROY_MAX_LEVELS * ROY_MAX_OWNERS)
#define ROY_MAX_RECEIPTS 1024u /* R5: consumed ids (sales and payouts) */
#define ROY_BPS          10000u
#define ROY_ID_LEN       32u
#define ROY_DIGEST_LEN   32u
#define ROY_MANIFEST_MSG (19u + 32u + 8u + 4u + ROY_MAX_OWNERS * 16u + 1u + 32u + 16u + 1u + 24u)
#define ROY_SPLIT_MSG    (20u + 32u + 8u + 4u + 4u + 2u + 4u + 2u + 8u)

typedef enum {
    ROY_OK = 0,
    ROY_ZERO = 1, /* R11: royalty 0, receipt consumed, nothing posted */
    ROY_ERR_ARG = -1,
    ROY_ERR_FULL = -2,
    ROY_ERR_SIG = -3,          /* R1/R4/R8: bad signature */
    ROY_ERR_DUP = -4,          /* R1 id already registered; R5 receipt used */
    ROY_ERR_UNKNOWN = -5,      /* no such work / parent / owner */
    ROY_ERR_VERSION = -6,      /* R4/R8: version or seq not above the last */
    ROY_ERR_DEPTH = -7,        /* R2 */
    ROY_ERR_LINES = -8,        /* R9 */
    ROY_ERR_UNREALISED = -9,   /* R5: no realised sale for this receipt */
    ROY_ERR_STALE_QUOTE = -10, /* R11 */
    ROY_ERR_OVERFLOW = -11,
    ROY_ERR_LEDGER = -12, /* the ledger refused (see last_ledger) */
    ROY_ERR_PARENT = -13  /* R4: parent or upstream share changed */
} roy_status_t;

typedef enum { ROY_TERMS_PERCENT = 1, ROY_TERMS_FLAT = 2 } roy_terms_kind_t;

typedef struct {
    uint8_t kind;      /* roy_terms_kind_t */
    uint64_t num, den; /* PERCENT */
    uint64_t flat;     /* FLAT, minor units per use */
} roy_terms_t;

/* R3: ZXV Hardware License 1.0 s4.3, commercial manufacture: 18% of gross.
 * Payee named by that licence: ILLUMAHEART FOUNDATION (comment only; the
 * payee is the registered owner set). */
#define ROY_HW_NUM                  18u
#define ROY_HW_DEN                  100u
#define ROY_TERMS_ZXV_HW_COMMERCIAL ((roy_terms_t){ROY_TERMS_PERCENT, ROY_HW_NUM, ROY_HW_DEN, 0u})

typedef struct {
    uint32_t id;      /* signer id for the verify hook */
    uint32_t account; /* ledger account paid */
    uint64_t weight;  /* > 0 */
} roy_owner_t;

typedef struct {
    uint8_t id[ROY_ID_LEN];
    uint64_t version; /* >= 1, strictly increasing (R4) */
    roy_owner_t owner[ROY_MAX_OWNERS];
    uint32_t n_owners;
    bool has_parent;
    uint8_t parent[ROY_ID_LEN];
    uint64_t up_num, up_den;
    roy_terms_t terms;
} roy_manifest_t;

/* R8 */
typedef struct {
    uint32_t giving_acct, invest_acct;
    uint16_t giving_bps, invest_bps;
    uint64_t seq;
} roy_split_t;

typedef struct {
    const uint8_t *sig;
    uint32_t len;
} roy_sig_t;

typedef bool (*roy_verify_fn)(void *ctx, uint32_t signer, const uint8_t *msg, uint32_t len,
                              const uint8_t *sig, uint32_t sig_len);

/* R13 lifetime counters of one work */
typedef struct {
    uint64_t payments, gross, royalty, fee, up_in, up_out, owners, giving, investing;
} roy_totals_t;

typedef struct {
    bool used;
    roy_manifest_t m;
    roy_split_t split[ROY_MAX_OWNERS]; /* by owner slot */
    uint32_t parent;                   /* index, valid when m.has_parent */
    uint32_t depth;
    roy_totals_t tot;
} roy_work_t;

typedef struct {
    uint32_t level, owner_id;
    uint32_t account, giving_acct, invest_acct;
    uint64_t gross, giving, investing, net;
} roy_payout_t;

typedef struct {
    uint32_t work; /* index */
    uint8_t id[ROY_ID_LEN];
    uint64_t version;
    uint64_t received, upstream, kept;
} roy_level_t;

typedef struct {
    uint64_t gross, royalty, fee;
    uint64_t upstream_total, owners_total, giving_total, investing_total;
    roy_level_t level[ROY_MAX_LEVELS];
    uint32_t n_levels;
    roy_payout_t payout[ROY_MAX_PAYOUTS];
    uint32_t n_payouts;
    pay_line_t leg[PAY_MAX_LINES]; /* receiving legs, merged, nonzero */
    uint32_t n_legs;
    uint8_t digest[ROY_DIGEST_LEN];
} roy_quote_t;

typedef struct {
    uint16_t asset;
    uint8_t cap;
    uint32_t fee_acct;
    roy_verify_fn verify;
    void *vctx;
    roy_work_t work[ROY_MAX_WORKS];
    uint32_t n_works;
    uint8_t receipt[ROY_MAX_RECEIPTS][ROY_ID_LEN];
    uint32_t n_receipts;
    roy_quote_t scratch; /* roy_pay's recomputed quote (no big stack frame) */
    pay_status_t last_ledger;
    bool ready;
} roy_t;

/* The fee account fixes the asset and capital form of every account used.
 * It must be active and not an issuer. */
roy_status_t roy_init(roy_t *r, const pay_ledger_t *L, uint32_t fee_acct, roy_verify_fn verify,
                      void *vctx);

/* R3 */
bool roy_terms_ok(const roy_terms_t *t);
void roy_terms_zxv_hw(roy_terms_t *t);
/* R6 (pure): the royalty on `gross`. False when the terms are bad, gross is
 * 0 or >= PAY_BAL_MAX, or FLAT with gross < flat. */
bool roy_royalty(const roy_terms_t *t, uint64_t gross, uint64_t *royalty);

/* R1 / R4 */
void roy_manifest_msg(const roy_manifest_t *m, uint8_t out[ROY_MANIFEST_MSG]);
roy_status_t roy_register(roy_t *r, const pay_ledger_t *L, const roy_manifest_t *m,
                          const uint8_t *sig, uint32_t sig_len);
roy_status_t roy_update(roy_t *r, const pay_ledger_t *L, const roy_manifest_t *m,
                        const roy_sig_t *sigs, uint32_t n_sigs);
/* Index of a registered work, or -1. */
int32_t roy_find(const roy_t *r, const uint8_t id[ROY_ID_LEN]);
const roy_work_t *roy_work(const roy_t *r, const uint8_t id[ROY_ID_LEN]);

/* R8 */
void roy_split_msg(const uint8_t work[ROY_ID_LEN], uint64_t version, uint32_t owner_id,
                   const roy_split_t *s, uint8_t out[ROY_SPLIT_MSG]);
roy_status_t roy_split_set(roy_t *r, const pay_ledger_t *L, const uint8_t work[ROY_ID_LEN],
                           uint32_t owner_id, const roy_split_t *s, const uint8_t *sig,
                           uint32_t sig_len);

/* R7 (pure): largest-remainder split of `amount` by w[0..n), 1 <= n <=
 * ROY_MAX_OWNERS, every w > 0, sum fits. False (out untouched) otherwise. */
bool roy_lrm(uint64_t amount, const uint64_t *w, uint32_t n, uint64_t *out);

/* R9: the number of lines a payment for this work posts at most (payer
 * included); 0 for an unknown work. */
uint32_t roy_lines(const roy_t *r, const uint8_t id[ROY_ID_LEN]);

/* R10 */
roy_status_t roy_quote(const roy_t *r, const uint8_t work[ROY_ID_LEN], uint64_t gross,
                       roy_quote_t *q);

/* R5 / R11. `out` (optional) gets the quote that was paid. */
roy_status_t roy_pay(roy_t *r, pay_ledger_t *L, pay_posting_req_t *req, uint32_t payer,
                     const uint8_t work[ROY_ID_LEN], uint64_t gross,
                     const uint8_t receipt[ROY_ID_LEN], const uint8_t digest[ROY_DIGEST_LEN],
                     roy_quote_t *out);
bool roy_receipt_used(const roy_t *r, const uint8_t id[ROY_ID_LEN]);

/* R12 */
uint64_t roy_grant_cap(uint64_t reserve_surplus, uint64_t requested);

/* R13 */
bool roy_check(const roy_t *r, const pay_ledger_t *L);

#endif /* ZXV_ROYALTY_H */
