/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_econ.h — the external economy of the mesh, across the nine forms of
 * capital, on the triple ledger. Design: docs/VINEA.md section 7.
 *
 * THREE TOKEN KINDS (each a 9-form vector, swarm_cap_t == zcap_form_t order)
 *   INTERNAL  the node's own per-cycle swarm budget (src/swarm/swarm_budget.h).
 *             Local only, never transferable. No message handler can reach it;
 *             only the owner's GATE converts, both ways, capped.
 *   EXTERNAL  mesh currency: a claim on the ISSUING node's resources, earned by
 *             contributing compute/storage/memory/files to it, spent there.
 *             Denominated identically everywhere (1 unit of a form is 1 unit),
 *             held as bilateral balances recorded by both parties.
 *   NEUTRAL   unassigned tokens held by the mesh: a node's capacity pledged to
 *             the mesh (minted only by its owner's gate export) that belongs to
 *             nobody until the node's cycle settlement distributes it.
 *
 * FORMS: only the five priceable forms (Financial, Manufactured,
 * Intellectual, Human, System) can be EXTERNAL/NEUTRAL or traded. The four
 * Crown forms (Social, Natural, Cultural, Spiritual) are inalienable
 * (zcapital.h): a receipt in a Crown form is refused (VNA_ERR_INALIENABLE).
 * Social capital is still EARNED (+1 per verified receipt, per counterparty,
 * as in swarm_ledger.h W4) but is never moved.
 *
 * MOVES (the only ways a balance changes; each is a signed receipt or an
 * owner-gated call, and each posts FINANCIAL + PROVENANCE + EXTERNALITY
 * entries to the hash-chained, ML-DSA-signed ledger):
 *   EXPORT      gate: owner spends own INTERNAL tokens -> NEUTRAL pool (mint N)
 *   IMPORT      gate: verified remote compute received this cycle -> next
 *               cycle's INTERNAL rate (mint I), capped
 *   DISTRIBUTE  settle: NEUTRAL pool -> EXTERNAL held by peers (burn N, mint E),
 *               signed by the issuer
 *   PAY         buyer spends seller-issued EXTERNAL at the seller (burn E),
 *               countersigned by both, agreement-checked at the seller
 *   CREDIT      seller delivered on credit: the buyer records demand-weighted
 *               contribution value (paid at the buyer's settle), countersigned
 *   EXPIRE      settle: undistributable NEUTRAL burns (no hoarding, like R6)
 *
 * SETTLEMENT (per cycle, per priceable form, from swarm_market.h M2-M4 and
 * swarm_budget.h R1-R5, reused not re-implemented):
 *   G1  floor = 8/21 of the pool is the COMMONS: split over every member peer
 *       by the Fibonacci rule (levels of capacity 1,2,3,5,... ranked by
 *       lifetime contribution; level weights F(L-d+1) — adjacent levels in
 *       consecutive-Fibonacci, i.e. golden, proportion; equal inside a level).
 *       Newcomers sit in the deepest level and still receive: growth.
 *   G2  market = 13/21 is paid in proportion to the demand-weighted value each
 *       peer contributed this cycle, no peer above 8/21 of it (anti-monopoly,
 *       swarm_capped_split); what nobody may take falls back to the commons.
 *   G3  no peer's total may exceed max(8/21 of the pool, an equal share), or
 *       ceil(13/21) when there are exactly two members (else the equal-share
 *       floor would force a 50/50 split regardless of contribution); the
 *       excess goes equally to peers under the cap (like M7).
 *   G4  every split is largest-remainder integer arithmetic: the parts sum to
 *       EXACTLY the pool. Undistributable residue EXPIRES.
 *
 * DEMAND PRICING: price = base * (S + D) / S, capped at base * 21/8, where D
 * is recent demand (requests, decaying by 13/21 each cycle) and S the known
 * supply (>= 1). A resource in higher demand is valued higher, so its
 * provider's CREDIT value — and with it its share of the market part of the
 * buyer's pool — grows: high demand earns its owner more remote compute.
 *
 * DOUBLE SPENDING without a server: an EXTERNAL token is a liability of its
 * issuer and lives on the issuer's book; it can only be spent AT the issuer,
 * which decrements it atomically before serving. It cannot be passed to a
 * third party (no rippling), so there is no second place to spend it. Every
 * receipt between two peers carries a per-pair sequence number and the hash
 * of the previous receipt between them, and is signed by both: a replayed
 * receipt fails the sequence, a forked history is two signed receipts with
 * the same sequence (a transferable proof of equivocation). What this does
 * NOT prevent without global consensus: an issuer that refuses to honour its
 * own tokens (default is provable from the signed DISTRIBUTE receipts, not
 * preventable); Sybil members diluting a commons the owner admitted them to;
 * collusion between two identities to trade with each other.
 */
#ifndef VNA_ECON_H
#define VNA_ECON_H

#include "vna_id.h"
#include "vna_schema.h"
#include "vna_agree.h"
#include "../swarm/swarm_budget.h"
#include "../swarm/swarm_market.h"

#define VNA_FORMS 9u /* SWARM_CAP_COUNT == ZCAP_FORM_COUNT */

typedef enum {
    VNA_TK_INTERNAL = 0,
    VNA_TK_EXTERNAL = 1,
    VNA_TK_NEUTRAL = 2,
    VNA_TK_KINDS = 3
} vna_token_kind_t;

typedef enum {
    VNA_TR_PAY = 1,
    VNA_TR_CREDIT = 2,
    VNA_TR_DISTRIBUTE = 3,
    VNA_TR_MODE_MAX = 3
} vna_trade_mode_t;

/* ---- trade receipt (countersigned) ---- */
#define VNA_RCPT_MAGIC 0x32544E56u /* "VNT2" */
#define VNA_RCPT_MAX   7200u
typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t mode;     /* vna_trade_mode_t */
    uint8_t kind;     /* must be VNA_TK_EXTERNAL */
    uint8_t form;     /* swarm_cap_t; must be priceable */
    uint8_t resource; /* vna_resource_t */
    vna_id_t seller;  /* DISTRIBUTE: the issuer */
    vna_id_t buyer;   /* DISTRIBUTE: the recipient */
    uint64_t units;
    uint64_t price;
    uint32_t demand_x1000; /* externality: price / base, in thousandths */
    uint64_t pair_seq;
    uint8_t prev[32];
    uint64_t ts;
    uint8_t agreement_cid[32]; /* the seller's agreement it trades under */
    uint16_t hk_len;
    uint8_t hk[255]; /* the canonical Hackronomicon command the trade answers */
    uint8_t sig_seller[VNA_SIG_LEN];
    uint8_t sig_buyer[VNA_SIG_LEN]; /* zero for DISTRIBUTE */
} vna_receipt_t;

extern const vna_schema_t vna_receipt_schema;

/* ---- hash-chained triple-ledger entries ---- */
typedef enum { VNA_LAX_FINANCIAL = 0, VNA_LAX_PROVENANCE = 1, VNA_LAX_EXTERNALITY = 2 } vna_lax_t;

#define VNA_ACC_SELF     0x0000u
#define VNA_ACC_MINT     0xFFFDu
#define VNA_ACC_BURN     0xFFFEu
#define VNA_ACC_POOL     0xFFFFu
#define VNA_LEDGER_MAGIC 0x324C4E56u /* "VNL2" */

typedef struct {
    uint32_t magic;
    uint64_t seq;
    uint8_t prev[32];
    uint8_t axis; /* vna_lax_t */
    uint8_t kind; /* vna_token_kind_t */
    uint8_t form; /* swarm_cap_t */
    uint8_t mode; /* vna_trade_mode_t, or 0x10 + gate op */
    vna_id_t counterparty;
    uint64_t amount; /* FINANCIAL: value moved; PROVENANCE: signatures verified; EXT: demand */
    uint16_t debit, credit; /* FINANCIAL: account indices (VNA_ACC_*) */
    uint8_t ref[32];        /* receipt hash (all three entries of a trade share it) */
    uint64_t aux;
    uint64_t ts;
} vna_lentry_t;

extern const vna_schema_t vna_lentry_schema;

#define VNA_LEDGER_RING 192u
typedef struct {
    vna_lentry_t ring[VNA_LEDGER_RING];
    uint32_t head, n;
    uint64_t count;
    uint8_t base[32]; /* hash before the oldest entry still in the ring */
    uint8_t tip[32];
    uint8_t tip_sig[VNA_SIG_LEN]; /* ML-DSA-65 over tip (covers the whole chain) */
    uint64_t debits, credits;     /* double-entry projection totals */
} vna_ledger_t;

/* ---- accounts and the book ---- */
typedef struct {
    vna_id_t id;
    uint8_t pk[VNA_PK_LEN];
    bool used;
    bool member;                           /* eligible for the commons (admitted by the owner) */
    uint64_t bal[VNA_TK_KINDS][VNA_FORMS]; /* [EXTERNAL]: issued by me, held by peer */
    uint64_t held[VNA_FORMS];              /* issued by peer, held by me */
    uint64_t contrib[VNA_FORMS];           /* CREDIT value this cycle */
    uint64_t contrib_total;
    uint64_t receivable[VNA_FORMS]; /* I delivered on credit to them (informational) */
    uint64_t social;                /* Crown: earned, never moved */
    uint64_t pair_seq;
    uint8_t pair_prev[32];
} vna_account_t;

typedef struct {
    const vna_identity_t *idn;
    vna_account_t *acct;
    uint32_t cap, n;
    uint64_t pool[VNA_FORMS]; /* NEUTRAL */
    uint64_t minted[VNA_TK_KINDS][VNA_FORMS];
    uint64_t burned[VNA_TK_KINDS][VNA_FORMS];
    uint64_t held_in[VNA_FORMS], held_out[VNA_FORMS];
    uint64_t received_units[VNA_RES_COUNT]; /* resources received this cycle (verified) */
    uint64_t demand[VNA_RES_COUNT];
    uint64_t supply[VNA_RES_COUNT];
    uint64_t cycle;
    vna_ledger_t ledger;
    uint32_t rejected; /* receipts refused */
} vna_book_t;

void vna_book_init(vna_book_t *b, const vna_identity_t *idn, vna_account_t *acct, uint32_t cap);
/* Register a counterparty (binding + PoW checked). member: commons-eligible. */
vna_status_t vna_book_add_peer(vna_book_t *b, const vna_id_t *id, const uint8_t pk[VNA_PK_LEN],
                               uint64_t pow, uint32_t pow_bits, bool member);
vna_account_t *vna_book_find(vna_book_t *b, const vna_id_t *id);
/* Trust = lifetime contribution value + Social capital earned (saturating). */
uint64_t vna_book_trust(void *book, const vna_id_t *peer);

/* Build a receipt for a trade with `peer`; pair_seq/prev come from the book. */
vna_status_t vna_receipt_prepare(vna_book_t *b, vna_receipt_t *r, uint8_t mode, uint8_t form,
                                 uint8_t resource, const vna_id_t *seller, const vna_id_t *buyer,
                                 uint64_t units, uint64_t price, uint32_t demand_x1000, uint64_t ts,
                                 const uint8_t agreement_cid[32], const char *hk);
/* Sign as the seller and encode. Returns the length or -1. */
int32_t vna_receipt_sign_seller(vna_receipt_t *r, const vna_identity_t *idn, const uint8_t rnd[32],
                                uint8_t *out, uint32_t cap);
/* Buyer: verify the seller's signature, then countersign. Returns the new
 * length or a negative vna_status_t. */
int32_t vna_receipt_countersign(const uint8_t *in, uint32_t len,
                                const uint8_t seller_pk[VNA_PK_LEN], const vna_identity_t *idn,
                                const uint8_t rnd[32], vna_receipt_t *scratch, uint8_t *out,
                                uint32_t cap);

/* Apply a receipt to the book (see MOVES). agr/us: this node's agreement and
 * usage, checked when the node is the one giving up a resource. rnd: 32
 * bytes for the ledger-head signature. Fails closed on anything off:
 * signature, binding, kind, Crown form, pair sequence/prev, balance,
 * agreement. */
vna_status_t vna_book_apply(vna_book_t *b, const uint8_t *bytes, uint32_t len,
                            const vna_agreement_t *agr, vna_usage_t *us, uint64_t now,
                            const uint8_t rnd[32]);

/* ---- settlement ---- */
typedef struct {
    uint8_t (*buf)[VNA_RCPT_MAX]; /* receipt encodings for the recipients */
    uint32_t *len;
    uint32_t cap, n;
} vna_rcpt_out_t;

/* Distribute every priceable form's pool (G1-G4), sign one DISTRIBUTE receipt
 * per recipient and form (written to out for delivery), expire the residue,
 * decay demand, reset the cycle counters. rnd: fed per signature. */
vna_status_t vna_book_settle(vna_book_t *b, uint64_t now, vna_drbg_t *rng, vna_rcpt_out_t *out);

/* The pure split behind settle, exposed for tests: pool over n members with
 * per-member contribution this cycle (cyc) and lifetime (life); writes
 * share[] (and optionally the commons total, each member's Fibonacci level
 * and its commons part), returns the undistributable residue.
 * n <= SWARM_MAX_MODELS, pool <= 2^48. */
uint64_t vna_econ_split(uint64_t pool, const uint64_t *cyc, const uint64_t *life,
                        const vna_id_t *ids, uint32_t n, uint64_t *share, uint64_t *commons_out,
                        uint64_t *level_of, uint64_t *commons_share);

/* ---- conservation (per kind, per form) ---- */
/* True iff: pool == minted N - burned N; sum bal[E] == minted E - burned E;
 * sum held == held_in - held_out; peers' INTERNAL/NEUTRAL rows are 0; Crown
 * forms hold nothing; the ledger chain verifies. */
bool vna_book_conserved(const vna_book_t *b);

/* Recompute every hash of the ledger chain and verify the head signature. */
bool vna_ledger_verify(const vna_ledger_t *l, const uint8_t pk[VNA_PK_LEN]);

/* ---- demand pricing ---- */
void vna_book_note_demand(vna_book_t *b, vna_resource_t r, uint64_t requests);
void vna_book_set_supply(vna_book_t *b, vna_resource_t r, uint64_t providers);
uint64_t vna_econ_price(uint64_t base, uint64_t demand, uint64_t supply);
uint64_t vna_book_price(const vna_book_t *b, vna_resource_t r, uint64_t base);

/* ---- the gate between INTERNAL and EXTERNAL (owner only) ---- */
typedef struct {
    uint8_t owner_tag[32]; /* SHA3-256 of the owner's 32-byte secret */
    uint64_t cycle;
    uint64_t export_cap, import_cap; /* per cycle */
    uint64_t exported, imported;
    uint32_t ops, max_ops; /* rate limit: conversions per cycle */
} vna_gate_t;

void vna_gate_init(vna_gate_t *g, const uint8_t owner_secret[32], uint64_t export_cap,
                   uint64_t import_cap, uint32_t max_ops);
vna_status_t vna_gate_begin_cycle(vna_gate_t *g, const uint8_t owner_secret[32]);
/* Spend `amount` of the owner's own internal tokens (swarm_budget_consume on
 * model_id, in the open cycle) into the NEUTRAL pool of `form`. */
vna_status_t vna_gate_export(vna_gate_t *g, const uint8_t owner_secret[32], swarm_budget_t *sb,
                             uint32_t model_id, vna_book_t *b, uint8_t form, uint64_t amount,
                             uint64_t *granted);
/* Turn verified remote compute received this cycle into extra internal rate
 * for the next cycle: swarm_budget_set_rate(sb, base_rate + imported). */
vna_status_t vna_gate_import(vna_gate_t *g, const uint8_t owner_secret[32], vna_book_t *b,
                             uint8_t form, uint64_t amount, swarm_budget_t *sb, uint64_t base_rate);

/* ---- AI negotiation (pure, bounded integers) ---- */
typedef enum { VNA_NEG_BUYER = 0, VNA_NEG_SELLER = 1 } vna_neg_role_t;
typedef enum { VNA_NEG_OPEN = 0, VNA_NEG_COUNTER, VNA_NEG_ACCEPT, VNA_NEG_FAIL } vna_neg_status_t;

typedef struct {
    uint8_t role;
    uint8_t status;
    uint64_t limit;   /* buyer: min(budget, valuation); seller: reserve price */
    uint64_t current; /* my standing offer */
    uint64_t deal;
    uint32_t round, max_rounds;
} vna_neg_t;

/* Buyer: opening <= limit; seller: opening >= limit. */
vna_status_t vna_neg_init(vna_neg_t *n, vna_neg_role_t role, uint64_t limit, uint64_t opening,
                          uint32_t max_rounds);
/* React to the other side's offer: accept it (never outside my limit), or
 * concede 8/21 of the remaining distance to my limit (at least 1) and
 * counter, or fail after max_rounds. *mine receives my counter-offer. */
vna_neg_status_t vna_neg_receive(vna_neg_t *n, uint64_t theirs, uint64_t *mine);

/* Budget planning for the agent: split `total` over n wanted items in
 * proportion to their priorities (largest remainder, exact). */
void vna_econ_plan(uint64_t total, const uint64_t *priority, uint32_t n, uint64_t *out);

#endif /* VNA_ECON_H */
