/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vbond.h — V-Bonds: fixed-term, transferable lots of locked V-Bills paid a
 * variable share of realised protocol revenue, on pay_ledger.
 *
 * OWNER REQUEST (2026-10-11, paraphrased): V-Bonds are long-horizon,
 * fixed-duration positions locked to Fibonacci epoch milestones (for example
 * F(21) = 10,946 phase-ticks), designed to support the network's development
 * and infrastructure; their return comes first from high-tier protocol
 * settlements and auction clearing premiums; a bond can be handed to another
 * player card without releasing the locked V-Bills early.
 * PROJECT RULES. NO USURY: a V-Bond carries no fixed or promised rate and
 * nothing in it grows with time or with a balance; its only return is a share
 * of REALISED revenue actually sitting in the bond pool when it is paid out.
 * There is no rate field anywhere. Closed clearing by default: a transfer is
 * a bilateral signed hand-over, never an order book. Terms are Fibonacci
 * milestones the operator chooses. The module ships OFF per jurisdiction.
 *
 *   B0 SWITCH. Issuing, transferring and crediting the pool are OFF by
 *      default. vbond_enable takes a jurisdiction manifest (jurisdiction
 *      code, enable flag, epoch) signed by the configured jurisdiction gateway
 *      key and checked by the caller's verify hook (ML-DSA in production),
 *      with its own tag "zxv/vbond-sw/v1"; the code must be this instance's
 *      and the epoch above the last one, so a later manifest can disable it
 *      again. While disabled those calls return VBOND_ERR_DISABLED. LEAVING
 *      IS NEVER BLOCKED: redemption at maturity and the distribution of what
 *      is already in the pool work whatever the switch says.
 *   B1 ISSUE. A bond is a lot of qty V-Bills locked for a term T = F(k)
 *      phase-ticks, k from the operator's allowed set (default {13, 17, 21}:
 *      233, 1597, 10946 ticks). vbond_issue moves qty V-Bills from the
 *      owner's V-Bill account to the bond lock account in ONE ledger transfer
 *      and records {serial, owner, payee, qty, k, start = tick, maturity =
 *      start + F(k), nonce = 0} in a fixed table of VBOND_MAX_BONDS. Serials
 *      start at 1 and are never reused, so a freed slot can never be mistaken
 *      for an earlier bond. Owners are ledger owner ids: the V-Bill account
 *      and the Vino payee account must both belong to `owner`, and `owner` is
 *      also the key id the verify hook checks for transfers. Issuing is
 *      authorised by the caller (as vswap_lock is); the module checks only
 *      that the accounts are the owner's.
 *   B2 TRANSFER. vbond_transfer hands bond `serial` from its current owner
 *      to another, with the current owner's signature over
 *      "zxv/vbond-tx/v1" || juris || serial || to || to_payee || nonce. The
 *      V-Bills stay in the lock account; only the owner and the Vino payee
 *      change, and the nonce goes up by one, so a signature works once. A
 *      transfer to the same owner re-registers the payee. Any price is
 *      settled by the two parties outside this call: there is no order book,
 *      no matching and no quote here.
 *   B3 MATURITY. vbond_redeem works only at tick >= maturity (maturity - 1
 *      is VBOND_ERR_IMMATURE) and only for the current owner; it moves the
 *      qty V-Bills from the lock account back to a V-Bill account of the
 *      owner's choosing in ONE posting and frees the slot. There is no early
 *      redemption: a transfer is the only exit before maturity. The
 *      principal is V-Bills, which redeem for Vino only at the reserve ratio
 *      (vswap W4): a bond CAN BE WORTH LESS at maturity than at issue.
 *   B4 PROFIT SHARE. The bond pool is a Vino account that the caller fills
 *      with (a) vbond_premium_credit: share_num/share_den of an auction
 *      epoch's clearing premium, premium = (price - floor) * qty when price >
 *      floor and 0 otherwise, the credited amount being
 *          floor(premium * share_num / share_den)
 *      (the sub-unit remainder stays with the source), and (b) any other
 *      realised revenue the operator routes there (vbond_pool_credit).
 *      vbond_distribute pays the WHOLE pool balance P to the live bonds
 *      (used and start <= tick; a matured bond that is not yet redeemed is
 *      still live), each with units u = qty * weight[term]. Weights default
 *      to F(k) and are operator-overridable integers that may not fall as the
 *      term grows. With U = sum u (must fit 64 bits):
 *          share_i = floor(P * u_i / U), and the P - sum share_i < count
 *      leftover units go one each to the largest remainders (P * u_i mod U),
 *      ties to the lower slot; sum share_i == P exactly. Each share goes to
 *      the bond's registered Vino payee. An empty pool pays nothing
 *      (VBOND_ERR_EMPTY). Nothing is ever promised.
 *   B5 CHECK. vbond_check: the sum of live bond qty equals the lock account's
 *      DEBIT, every bond's maturity is start + F(k) of an allowed k, and
 *      pay_ledger_check passes.
 *
 * POSTINGS AND BATCHING. Issue, redeem and each credit are ONE atomic
 * posting. A distribution pays in batches of at most PAY_MAX_LINES - 1
 * payees, each batch one posting with the pool line. Should a later batch be
 * refused (VBOND_ERR_PARTIAL), the earlier ones stand and each bond's
 * last_paid says exactly who was paid what. Identifiers are
 * SHA-256(seed || tag || seq) as in vswap; e2e is VBOND-<seq>.
 *
 * HONEST LIMITS. A transferable term instrument paying a share of protocol
 * revenue is very likely a security, and may be a debt-like or collective
 * investment product, in most jurisdictions; that is why it ships OFF behind
 * the jurisdiction switch, and a switch is not a legal opinion. The return
 * can be zero. The principal is V-Bills and can lose value against Vino. The
 * premium share is a configured fraction, not a commitment that any auction
 * will clear above its floor. Signatures are only as good as the verify
 * hook. Freestanding C11: no allocation, no float.
 */
#ifndef ZXV_VBOND_H
#define ZXV_VBOND_H

#include <stdbool.h>
#include <stdint.h>
#include "../pay/pay_ledger.h"

#define VBOND_NONE      0xFFFFFFFFu
#define VBOND_MAX_BONDS 64u
#define VBOND_MAX_TERMS 8u
#define VBOND_FIB_KMIN  1u
#define VBOND_FIB_KMAX  90u                  /* F(90) < 2^62 */
#define VBOND_LINE_MAX  ((uint64_t) 1 << 59) /* pay_ledger's per-line limit */

typedef enum {
    VBOND_OK = 0,
    VBOND_ERR_ARG = -1,
    VBOND_ERR_DISABLED = -2, /* B0 */
    VBOND_ERR_SIG = -3,      /* B0 manifest or B2 transfer signature */
    VBOND_ERR_EPOCH = -4,    /* B0: epoch not above the last */
    VBOND_ERR_JURIS = -5,    /* B0: manifest for another jurisdiction */
    VBOND_ERR_TERM = -6,     /* B1: k not in the allowed set */
    VBOND_ERR_FUNDS = -7,    /* the payer does not hold enough */
    VBOND_ERR_OVERFLOW = -8,
    VBOND_ERR_STATE = -9,     /* B5: a broken sum or record, a bad ledger */
    VBOND_ERR_LEDGER = -10,   /* the ledger refused (see last_ledger) */
    VBOND_ERR_EMPTY = -11,    /* B4: the pool is empty */
    VBOND_ERR_NOHOLD = -12,   /* B4: no live bond to pay */
    VBOND_ERR_FULL = -13,     /* B1: bond table full */
    VBOND_ERR_IMMATURE = -14, /* B3: tick < maturity */
    VBOND_ERR_PARTIAL = -15,  /* B4: a later batch was refused */
    VBOND_ERR_NOT_FOUND = -16,
    VBOND_ERR_OWNER = -17 /* not the bond's current owner */
} vbond_status_t;

/* verify(ctx, key, msg, len, sig, sig_len): true when sig is key's */
typedef bool (*vbond_verify_fn)(void *ctx, uint32_t key, const uint8_t *msg, uint32_t len,
                                const uint8_t *sig, uint32_t sig_len);

typedef struct {
    uint16_t vino, vbill;             /* ledger assets */
    uint32_t lock;                    /* the bond lock V-Bill account */
    uint32_t gateway;                 /* jurisdiction gateway key id (verify) */
    uint32_t juris;                   /* jurisdiction code this instance serves */
    uint32_t n_terms;                 /* 1..VBOND_MAX_TERMS */
    uint32_t k[VBOND_MAX_TERMS];      /* allowed Fibonacci indices, increasing */
    uint64_t weight[VBOND_MAX_TERMS]; /* B4 term weights, >= 1, not falling */
    uint64_t share_num, share_den;    /* B4 premium share, num <= den */
    vbond_verify_fn verify;
    void *vctx;
    uint8_t seed[32];
} vbond_cfg_t;

typedef struct {
    bool used;
    uint8_t term;   /* index into cfg.k / cfg.weight */
    uint32_t owner; /* ledger owner id and verify key id */
    uint32_t payee; /* the owner's Vino account for the profit share */
    uint64_t serial;
    uint64_t qty;
    uint64_t start, maturity;
    uint64_t nonce;     /* B2: transfers made so far */
    uint64_t last_paid; /* B4: the last distribution's Vino */
} vbond_bond_t;

typedef struct {
    pay_ledger_t *L;
    vbond_cfg_t cfg;
    bool enabled;
    uint64_t switch_epoch;
    vbond_bond_t bond[VBOND_MAX_BONDS];
    uint64_t next_serial;
    uint64_t seq, posted;
    pay_status_t last_ledger;
} vbond_t;

typedef struct {
    uint64_t pool;     /* P */
    uint64_t units;    /* U */
    uint64_t paid;     /* Vino paid out */
    uint32_t payees;   /* lines paid */
    uint32_t postings; /* postings that committed */
} vbond_dist_t;

/* Defaults: terms k = 13, 17, 21 with weights F(k) = 233, 1597, 10946,
 * premium share 1/2 (a placeholder for the operator to set). Accounts, keys
 * and the verify hook must still be set. */
void vbond_cfg_default(vbond_cfg_t *c);
/* Check the configuration against the ledger and start (switch OFF). The
 * lock account must be an empty V-Bill account. */
vbond_status_t vbond_init(vbond_t *s, pay_ledger_t *L, const vbond_cfg_t *c);

/* F(k): F(1) = F(2) = 1; 0 for k == 0 or k > VBOND_FIB_KMAX. */
uint64_t vbond_fib(uint32_t k);

/* B0: "zxv/vbond-sw/v1" || juris (4, LE) || enable (1) || epoch (8, LE). */
#define VBOND_SWITCH_MSG (15u + 4u + 1u + 8u)
void vbond_switch_msg(uint32_t juris, bool enable, uint64_t epoch, uint8_t out[VBOND_SWITCH_MSG]);
vbond_status_t vbond_enable(vbond_t *s, uint32_t juris, bool enable, uint64_t epoch,
                            const uint8_t *sig, uint32_t sig_len);

/* B1. *serial (optional) gets the new bond's serial. */
vbond_status_t vbond_issue(vbond_t *s, uint32_t owner, uint32_t vbill_acct, uint32_t vino_payee,
                           uint64_t qty, uint32_t k, uint64_t tick, uint64_t *serial);
/* The live bond with this serial, or NULL. */
const vbond_bond_t *vbond_get(const vbond_t *s, uint64_t serial);

/* B2: "zxv/vbond-tx/v1" || juris (4) || serial (8) || to (4) || to_payee (4)
 * || nonce (8), little-endian. */
#define VBOND_TX_MSG (15u + 4u + 8u + 4u + 4u + 8u)
void vbond_transfer_msg(uint32_t juris, uint64_t serial, uint32_t to, uint32_t to_payee,
                        uint64_t nonce, uint8_t out[VBOND_TX_MSG]);
vbond_status_t vbond_transfer(vbond_t *s, uint64_t serial, uint32_t from, uint32_t to,
                              uint32_t to_payee, const uint8_t *sig, uint32_t sig_len);

/* B3 */
vbond_status_t vbond_redeem(vbond_t *s, uint64_t serial, uint32_t owner, uint32_t vbill_acct,
                            uint64_t tick);

/* B4 (a): the exact amount credited for one auction epoch. */
vbond_status_t vbond_premium(const vbond_t *s, uint64_t price, uint64_t floor_price, uint64_t qty,
                             uint64_t *amount);
/* B4 (a): compute and post it from `src` to `pool` (nothing posted when 0).
 * *amount (optional) gets what was credited. */
vbond_status_t vbond_premium_credit(vbond_t *s, uint32_t src, uint32_t pool, uint64_t price,
                                    uint64_t floor_price, uint64_t qty, uint64_t tick,
                                    uint64_t *amount);
/* B4 (b): any other realised revenue, `amount` Vino from `src` to `pool`. */
vbond_status_t vbond_pool_credit(vbond_t *s, uint32_t src, uint32_t pool, uint64_t amount,
                                 uint64_t tick);
/* B4: pay the whole pool to the live bonds. out optional. */
vbond_status_t vbond_distribute(vbond_t *s, uint32_t pool, uint64_t tick, vbond_dist_t *out);

/* B5 */
vbond_status_t vbond_check(const vbond_t *s);

#endif /* ZXV_VBOND_H */
