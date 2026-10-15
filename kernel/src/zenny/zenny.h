/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zenny.h — Zenny chips: user-built compressed baskets, exact manifest core.
 *
 * TRADEMARK NOTE. "Zenny" is a currency name in Capcom's Mega Man / Breath of
 * Fire games; the name needs a trademark check before any public use. Code
 * identifiers keep the zenny_ prefix so a rename is a mechanical
 * search-and-replace.
 *
 * OWNER SPEC (quoted): "a Zenny chip is a user-built basket with a MANDATORY
 * V-Bill anchor leg plus weighted fiat / commodity / compute voucher legs,
 * recorded in a WORM manifest; tap-to-pay partial decompression;
 * central-bank co-minted bilateral chips. Compression is a minting feature:
 * the minter picks kappa and the rules at mint and may later rebalance the
 * leg weights; only allowed asset kinds compress."
 * Agreed rules: the chip claim equals the deposited value at mint and at
 * every rebalance (exact check); claims above the deposit only via explicit
 * credit rules (not in this module: refused). kappa is DENSITY, not
 * exposure: exposure above 1x is leverage and margin is OFF (refused).
 * Commodity voucher legs need a custodian attestation. Partial decompression
 * is a disposal, recorded in a receipt / tax-manifest log, filled at the
 * batch price with the 0.08889% fee. Compression never multiplies value or
 * dividend. No zero-sanctions or HQLA claims. Overdraft fails closed.
 *
 *   Z1 MANIFEST. A chip has an id (SHA-256 of its canonical mint content,
 *      zenny_mint_digest), a minter (and a co-minter when BILATERAL), kappa
 *      (reference units per chip, >= 1), a rules bitfield fixed at mint and
 *      1..ZENNY_MAX_LEGS legs: kind (VBILL, FIAT, COMMODITY, COMPUTE; any
 *      other value is refused), asset id, custodian, qty, weight. Every leg
 *      is valued at a caller-supplied price num/den (reference units per leg
 *      unit, den > 0): value_i = qty_i * num_i / den_i, which must divide
 *      exactly (ZENNY_ERR_INEXACT) and fit 64 bits (ZENNY_ERR_OVERFLOW).
 *   Z2 EXACT CLAIM. T = sum value_i > 0; chips = T / kappa must be exact;
 *      with W = sum weight_i every leg must satisfy value_i * W ==
 *      weight_i * T exactly (128-bit products, ZENNY_ERR_WEIGHT). At least
 *      one VBILL leg with weight > 0 is mandatory (ZENNY_ERR_ANCHOR). So
 *      chips * kappa == T: the claim is the deposit, never more. The minter
 *      signs every mint; a BILATERAL chip is also signed by its co-minter
 *      (a different signer). Every COMMODITY leg carries its custodian's
 *      attestation over zenny_att_digest(chip id, leg). The MARGIN rule bit
 *      is refused (ZENNY_ERR_LEVERAGE) and so is the CREDIT bit
 *      (ZENNY_ERR_CREDIT): neither is implemented here.
 *   Z3 WORM. Every version (mint, rebalance, decompression) is appended to
 *      the chip's hash chain: hash_v = SHA-256(hash_{v-1} || content_v ||
 *      v), hash_{-1} = 32 zero bytes. Kappa, rules, minter, co-minter and
 *      each leg's kind, asset and custodian never change after mint.
 *      zenny_verify_chain recomputes the id and every hash. When
 *      ZENNY_MAX_VERSIONS versions exist the chip refuses further changes
 *      (ZENNY_ERR_FULL): the log is never overwritten.
 *   Z4 REBALANCE. Only with ALLOW_REBALANCE (ZENNY_ERR_NOT_ALLOWED), signed
 *      by the minter (and the co-minter when BILATERAL) over
 *      zenny_rebalance_digest, which binds the current version hash (no
 *      replay). The request repeats the immutable fields; any difference is
 *      ZENNY_ERR_IMMUTABLE. New qty / weights at new prices pass every Z1/Z2
 *      check, and T_new / kappa must equal the chips outstanding
 *      (ZENNY_ERR_CLAIM): a rebalance never changes the claim. Commodity
 *      legs need fresh attestations over their new quantities.
 *   Z5 DECOMPRESS. c chips out of N (tap-to-pay): leg i pays out
 *      out_i = qty_i * c / N, exact for every leg or refused. c > N is
 *      ZENNY_ERR_FUNDS (overdraft fails closed). The disposal is valued at
 *      the caller's batch price per leg (exact, as in Z1) and the receipt
 *      records chip id, version, c, out_i, value_i, value, fee =
 *      pay_assure_fee(value) (a floor) and tick. Receipts sit in a fixed
 *      ring of ZENNY_MAX_RECEIPTS until the caller drains them with
 *      zenny_receipt_ack; a full ring refuses further decompression
 *      (ZENNY_ERR_LOG) instead of overwriting tax records. The new version
 *      keeps the last valuation prices, so the remainder still satisfies
 *      value == chips * kappa (zenny_check). zenny_quote gives the largest
 *      c <= want that decompression would accept (0 if none).
 *   Z6 LEDGER. Posting the leg movements (deposit at mint, leg swaps at
 *      rebalance, payouts and the fee at decompression) is the caller's job
 *      on its own ledger; this module is the exact manifest / accounting
 *      core and never holds an asset. Who may decompress a chip is the
 *      caller's authorisation decision too.
 *
 *   Z7 TOP-UP. Only with ALLOW_TOPUP (fixed at mint). zenny_topup adds
 *      `amount` reference units by buying into the existing legs in exactly
 *      the current weights: leg i gets value amount * weight_i / W and
 *      qty amount * weight_i / W * den_i / num_i at the caller's purchase
 *      prices, each division exact; amount / kappa new chips (exact; kappa
 *      never changes) go to `recipient`, recorded in the appended version.
 *      The whole new basket is then re-checked at the purchase prices as in
 *      Z2 and must be worth exactly (chips + amount / kappa) * kappa
 *      (ZENNY_ERR_CLAIM otherwise: if prices moved since the last
 *      valuation, rebalance first). Only BACKED legs may take value: VBILL
 *      and FIAT are backed; a COMMODITY leg is backed only by a standing
 *      custodian attestation (zenny_attest: a quantity cap and an expiry
 *      tick, each newer one expiring later) with tick < expiry and the new
 *      qty <= cap; COMPUTE vouchers are never backed in this design. A
 *      weighted unbacked leg refuses the top-up (ZENNY_ERR_UNBACKED);
 *      weight-0 legs take nothing. Every refusal changes nothing.
 *      At fixed prices the exact amounts are the multiples of one unit
 *      (zenny_topup_unit; it depends on kappa, the weights and the prices,
 *      so it changes when prices do). zenny_topup_quote gives
 *      want - want % unit when the top-up accepts that amount, else 0.
 *      zenny_allot_credit adapts an allotment's
 *      basket-NAV credit (dest ZENNY_ALLOT_DEST_NAV) to zenny_topup. No
 *      fee and no disposal receipt: a top-up is a deposit, and posting it
 *      is the caller's (Z6).
 *
 * HONEST LIMITS. A chip is a claim on its legs, not new money; compression
 * changes density only (more value per chip unit), never the value. A
 * commodity attestation proves who signed it, not that the goods exist.
 * Whether a chip is a security, e-money or a collective investment scheme
 * depends on jurisdiction and needs legal review; nothing here makes a
 * sanctions, HQLA or regulatory claim. Designed to support co-minting by a
 * central bank; not connected to any. Signatures go through the caller's
 * verify hook (ML-DSA in production). Standing attestations (Z7) live
 * beside the chain, not in it; the top-up version that relied on one is
 * chained. Freestanding C11: no allocation, no
 * float.
 */
#ifndef ZXV_ZENNY_H
#define ZXV_ZENNY_H

#include <stdbool.h>
#include <stdint.h>
#include "../pay/pay_assure.h"

#define ZENNY_MAX_LEGS     8u
#define ZENNY_MAX_CHIPS    8u
#define ZENNY_MAX_VERSIONS 16u
#define ZENNY_MAX_RECEIPTS 16u
#define ZENNY_SIG_MSG      (16u + 32u) /* tag || digest */

typedef enum {
    ZENNY_VBILL = 1,
    ZENNY_FIAT = 2,
    ZENNY_COMMODITY = 3,
    ZENNY_COMPUTE = 4
} zenny_kind_t;

/* Z1 rules bitfield, fixed at mint */
#define ZENNY_RULE_ALLOW_REBALANCE 0x1u
#define ZENNY_RULE_BILATERAL       0x2u
#define ZENNY_RULE_MARGIN          0x4u /* leverage: always refused */
#define ZENNY_RULE_CREDIT          0x8u /* claim above deposit: always refused */
#define ZENNY_RULE_ALLOW_TOPUP     0x10u
#define ZENNY_RULES_KNOWN          0x1Fu

typedef enum {
    ZENNY_OK = 0,
    ZENNY_ERR_ARG = -1,
    ZENNY_ERR_FULL = -2,         /* no chip slot, or the version log is full */
    ZENNY_ERR_KIND = -3,         /* Z1 */
    ZENNY_ERR_LEGS = -4,         /* Z1: 0 or more than ZENNY_MAX_LEGS legs */
    ZENNY_ERR_KAPPA = -5,        /* Z1: kappa 0 */
    ZENNY_ERR_PRICE = -6,        /* Z1: den 0 */
    ZENNY_ERR_OVERFLOW = -7,     /* Z1/Z2 */
    ZENNY_ERR_INEXACT = -8,      /* Z1/Z2/Z5 */
    ZENNY_ERR_WEIGHT = -9,       /* Z2 */
    ZENNY_ERR_ANCHOR = -10,      /* Z2: no V-Bill leg with weight > 0 */
    ZENNY_ERR_SIG = -11,         /* Z2/Z4 */
    ZENNY_ERR_ATTEST = -12,      /* Z2/Z4 */
    ZENNY_ERR_LEVERAGE = -13,    /* Z2 */
    ZENNY_ERR_CREDIT = -14,      /* Z2 */
    ZENNY_ERR_DUP = -15,         /* Z1: a chip with this id exists */
    ZENNY_ERR_IMMUTABLE = -16,   /* Z3/Z4 */
    ZENNY_ERR_CLAIM = -17,       /* Z4: the claim would change */
    ZENNY_ERR_NOT_ALLOWED = -18, /* Z4: no ALLOW_REBALANCE */
    ZENNY_ERR_FUNDS = -19,       /* Z5: overdraft */
    ZENNY_ERR_CHAIN = -20,       /* Z3: the chain does not verify */
    ZENNY_ERR_LOG = -21,         /* Z5: receipt ring full */
    ZENNY_ERR_UNBACKED = -22,    /* Z7: a weighted leg is not backed */
    ZENNY_ERR_STALE = -23        /* Z7: attestation expiry not later */
} zenny_status_t;

typedef enum { ZENNY_SIG_MINT = 0, ZENNY_SIG_REBALANCE = 1, ZENNY_SIG_ATTEST = 2 } zenny_purpose_t;

typedef enum {
    ZENNY_OP_MINT = 1,
    ZENNY_OP_REBALANCE = 2,
    ZENNY_OP_DECOMPRESS = 3,
    ZENNY_OP_TOPUP = 4
} zenny_op_t;

typedef struct {
    uint8_t kind;       /* zenny_kind_t */
    uint32_t custodian; /* attests COMMODITY legs; hashed for every leg */
    uint64_t asset;
    uint64_t qty;
    uint64_t weight;
} zenny_leg_t;

typedef struct {
    uint64_t num, den; /* reference units per leg unit */
} zenny_price_t;

/* A mint or rebalance request. For a rebalance `nonce` is ignored and the
 * immutable fields must repeat the chip's. att[i] / att_len[i] is the
 * custodian's signature for COMMODITY leg i (ignored for other kinds). */
typedef struct {
    uint32_t minter, co_minter;
    uint64_t kappa;
    uint32_t rules;
    uint64_t nonce;
    uint32_t n_legs;
    zenny_leg_t leg[ZENNY_MAX_LEGS];
    zenny_price_t price[ZENNY_MAX_LEGS];
    uint64_t tick;
    const uint8_t *sig_minter;
    uint32_t sig_minter_len;
    const uint8_t *sig_co;
    uint32_t sig_co_len;
    const uint8_t *att[ZENNY_MAX_LEGS];
    uint32_t att_len[ZENNY_MAX_LEGS];
} zenny_req_t;

/* One WORM version. `price` is the valuation price of the last mint or
 * rebalance (kept through decompressions). */
typedef struct {
    uint8_t op; /* zenny_op_t */
    uint64_t version;
    uint64_t chips; /* outstanding after this version */
    uint64_t tick;
    uint32_t recipient; /* Z7: who received a top-up's new chips (0 otherwise) */
    uint32_t n_legs;
    zenny_leg_t leg[ZENNY_MAX_LEGS];
    zenny_price_t price[ZENNY_MAX_LEGS];
    uint8_t prev[32], hash[32];
} zenny_version_t;

typedef struct {
    bool used;
    uint8_t id[32];
    uint32_t minter, co_minter;
    uint64_t kappa;
    uint32_t rules;
    uint64_t nonce;
    uint32_t n_ver;
    zenny_version_t ver[ZENNY_MAX_VERSIONS];
    uint64_t att_cap[ZENNY_MAX_LEGS]; /* Z7 standing attestations */
    uint64_t att_exp[ZENNY_MAX_LEGS]; /* expiry tick; 0 = none */
} zenny_chip_t;

/* Z5 disposal receipt (tax-manifest record) */
typedef struct {
    uint64_t seq;
    uint8_t chip_id[32];
    uint64_t version; /* the decompression's version number */
    uint64_t chips;   /* c */
    uint32_t n_legs;
    uint64_t out[ZENNY_MAX_LEGS];   /* leg units paid out */
    uint64_t value[ZENNY_MAX_LEGS]; /* out_i at the batch price */
    uint64_t total;                 /* sum value[i], reference units */
    uint64_t fee;                   /* pay_assure_fee(total) */
    uint64_t tick;
} zenny_receipt_t;

/* verify(ctx, signer, msg, len, sig, sig_len): true when sig is signer's */
typedef bool (*zenny_verify_fn)(void *ctx, uint32_t signer, const uint8_t *msg, uint32_t len,
                                const uint8_t *sig, uint32_t sig_len);

typedef struct {
    zenny_verify_fn verify;
    void *vctx;
    zenny_chip_t chip[ZENNY_MAX_CHIPS];
    zenny_receipt_t rcpt[ZENNY_MAX_RECEIPTS];
    uint64_t r_next;  /* seq of the next receipt */
    uint64_t r_acked; /* receipts with seq < r_acked are drained */
} zenny_t;

void zenny_init(zenny_t *z, zenny_verify_fn verify, void *vctx);

/* The message every signer signs: a 16-byte tag ("zxv/zenny/mint/1",
 * "zxv/zenny/rebl/1" or "zxv/zenny/attn/1") followed by the digest. */
void zenny_sign_msg(zenny_purpose_t p, const uint8_t digest[32], uint8_t out[ZENNY_SIG_MSG]);
/* Z1: the chip id: SHA-256 over the canonical mint content (minter,
 * co-minter, kappa, rules, legs, prices, nonce; little-endian). */
void zenny_mint_digest(const zenny_req_t *r, uint8_t out[32]);
/* Z4: what a rebalance is signed over (binds the current version hash). */
zenny_status_t zenny_rebalance_digest(const zenny_t *z, uint32_t slot, const zenny_req_t *r,
                                      uint8_t out[32]);
/* Z2: what a custodian attests for one leg of one chip. */
void zenny_att_digest(const uint8_t chip_id[32], const zenny_leg_t *leg, uint8_t out[32]);

zenny_status_t zenny_mint(zenny_t *z, const zenny_req_t *r, uint32_t *slot);
zenny_status_t zenny_rebalance(zenny_t *z, uint32_t slot, const zenny_req_t *r);
/* Z5: the largest c <= want that zenny_decompress accepts with `batch`
 * prices (NULL: payout exactness only); 0 if none. */
uint64_t zenny_quote(const zenny_t *z, uint32_t slot, uint64_t want, const zenny_price_t *batch);
/* Z5: `batch` has one price per leg; `out` (optional) gets the receipt. */
zenny_status_t zenny_decompress(zenny_t *z, uint32_t slot, uint64_t c, const zenny_price_t *batch,
                                uint64_t tick, zenny_receipt_t *out);

zenny_status_t zenny_verify_chain(const zenny_t *z, uint32_t slot);
/* Z2 on the current version: value at the stored prices == chips * kappa,
 * weights exact, anchor present. */
zenny_status_t zenny_check(const zenny_t *z, uint32_t slot);
const zenny_version_t *zenny_current(const zenny_t *z, uint32_t slot);
bool zenny_find(const zenny_t *z, const uint8_t id[32], uint32_t *slot);

/* Z5 receipt ring: get an undrained receipt by seq; ack drains every
 * receipt with seq < upto (upto <= the next seq). */
zenny_status_t zenny_receipt_get(const zenny_t *z, uint64_t seq, zenny_receipt_t *out);
zenny_status_t zenny_receipt_ack(zenny_t *z, uint64_t upto);

/* ===== Z7: top-up ===== */
/* What a custodian signs for a standing attestation of leg i: SHA-256 of
 * "zxv/zenny/cap/1." || chip id || i || kind || custodian || asset || cap ||
 * expires (little-endian u64s), signed with purpose ZENNY_SIG_ATTEST. */
void zenny_cap_digest(const uint8_t chip_id[32], uint32_t i, const zenny_leg_t *leg, uint64_t cap,
                      uint64_t expires, uint8_t out[32]);
/* Record a standing attestation for COMMODITY leg i: the custodian holds up
 * to `cap` units for this chip until tick `expires` (> the last expiry). */
zenny_status_t zenny_attest(zenny_t *z, uint32_t slot, uint32_t i, uint64_t cap, uint64_t expires,
                            const uint8_t *sig, uint32_t sig_len);
/* Add `amount` reference units at `prices` (one per leg); amount / kappa new
 * chips go to `recipient`. `added` (optional) gets that count. */
zenny_status_t zenny_topup(zenny_t *z, uint32_t slot, uint64_t amount, const zenny_price_t *prices,
                           uint32_t recipient, uint64_t tick, uint64_t *added);
/* The exact top-up amounts at fixed `prices` are exactly the multiples of
 * one unit: lcm(kappa, W / gcd(W, weight_i)) times, for each weighted leg,
 * num_i / gcd(num_i, b_i * den_i) combined by lcm, where b_i is the value
 * one such step buys in leg i. It depends on kappa, the weights and the
 * prices, so it changes when prices change. 0 if no exact top-up exists
 * (no chip, NULL prices, a weighted leg at num 0, or above 64 bits). */
uint64_t zenny_topup_unit(const zenny_t *z, uint32_t slot, const zenny_price_t *prices);
/* want - want % zenny_topup_unit (the largest exact amount <= want); 0 if
 * there is none or if zenny_topup would refuse
 * that amount now (unbacked, over a cap, moved prices, overflow). */
uint64_t zenny_topup_quote(const zenny_t *z, uint32_t slot, uint64_t want,
                           const zenny_price_t *prices, uint64_t tick);

/* Allotment adapter: an allotment credits its basket-NAV destination through this.
 * The allotment's headers are not included; the value is mirrored here. */
#define ZENNY_ALLOT_DEST_NAV 3u
typedef struct {
    zenny_t *z;
    uint32_t slot;
    const zenny_price_t *prices;
    uint32_t recipient;
    uint64_t tick;
    zenny_status_t last; /* the last zenny_topup result */
} zenny_allot_ctx_t;
/* dest ZENNY_ALLOT_DEST_NAV: zenny_topup(amount) to ctx->recipient; true
 * only on success. Any other dest (or a NULL ctx) returns false and changes
 * nothing. `who` (the allotment's payer) is not used: the chips go to the
 * recipient in ctx. */
bool zenny_allot_credit(void *ctx, uint32_t dest, const void *who, uint64_t amount);

#endif /* ZXV_ZENNY_H */
