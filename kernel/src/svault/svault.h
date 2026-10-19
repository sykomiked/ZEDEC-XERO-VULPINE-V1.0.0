/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* svault.h — the protocol solvency vault, on pay_ledger.
 *
 * OWNER DECISION (implemented here): "the 0.08889% assurance fee splits 50%
 * reserve floor / 25% V-Bill dividend / 15% node bounties / 10% regenerative.
 * ONE THIRD of the reserve share (16.67% of the fee) funds a protocol-owned
 * solvency vault, which buys V-Bills with collected fees only, until the
 * vault holds 1/3 of V-Bills outstanding; past the cap the money overflows to
 * (stays in) the physical reserve. Vault V-Bills do NOT count toward the Vino
 * redemption floor (anti-reflexivity)." And: "first-loss: vault V-Bills
 * absorb a toxic-asset haircut first." No usury: nothing here accrues with
 * time or grows with a balance.
 *
 * THE FOUR ACCOUNTS (fixed at svault_init)
 *   reserve      the reserve-floor bucket account (pay_assure.h B1), fiat
 *   cash         the vault's cash account, same asset as `reserve`
 *   vbill_issuer the V-Bill issuer; its CREDIT is N, the V-Bills outstanding
 *   vbills       the vault's V-Bill account; its DEBIT is h, the holdings
 * `cash` and `vbills` must be empty when the vault is set up, so that every
 * unit in them is accounted for by the counters below (S6).
 *
 *   S1 SWEEP WITH CARRY. The vault keeps share_total (every reserve share it
 *      was offered) and a remainder r in [0, 3). For a reserve share s:
 *          x = s + r,  part = floor(x / 3),  r' = x mod 3,
 *      so over ANY sequence of shares, starting from r = 0,
 *          swept_total + overflow_total == floor(share_total / 3)
 *      exactly (the same carry rule as pay_assure.h F2). svault_sweep moves
 *      `part` from `reserve` to `cash` in ONE ledger transfer, unless the
 *      vault is at its cap (S2: room == 0), in which case nothing moves and
 *      `part` is recorded as overflow kept in the reserve. A part of 0 posts
 *      nothing (SVAULT_ZERO) but still advances the carry. A refused transfer
 *      changes nothing: not the carry, not a counter.
 *      SOLVENCY FLOOR. A sweep that moves money refuses (SVAULT_ERR_FLOOR,
 *      nothing changed) unless the reserve left after it still passes S4
 *      with the vault's floor (floor_num / floor_den, backing), set by
 *      svault_set_floor. svault_init sets floor 0 (only h > N fails), so
 *      callers that never set a floor keep their behaviour; a deployment
 *      sets its real floor right after init. Overflow and zero parts move
 *      nothing and are not checked.
 *      svault_sweep_split takes the four-bucket output of pay_assure_split and
 *      sweeps its reserve share; svault_sweep_fee does the split itself.
 *   S2 CAP. Holdings h may not exceed floor(N / 3):
 *          room = floor(N / 3) - h, or 0 when that is negative
 *      (h can sit above the cap after other holders redeem; the vault then
 *      buys nothing and sweeps nothing until N grows again).
 *   S3 BUY. svault_buy(price, max_qty) buys
 *          qty = min(max_qty, room, floor(cash / price))
 *      V-Bills at `price` fiat minor units each (price > 0) in ONE atomic
 *      posting of four lines: cash -qty*price, seller fiat +qty*price,
 *      seller V-Bill -qty, vault V-Bill +qty. qty == 0 posts nothing and
 *      returns SVAULT_ZERO. The cash it spends came only from S1 sweeps:
 *      it buys with at most own = swept_total - spent_total, never with cash
 *      someone else posted into the vault's cash account (S6).
 *      The seller's fiat and V-Bill accounts must have the same ledger owner
 *      (SVAULT_ERR_AUTH); authenticating the caller as that owner belongs to
 *      the layer above (zxvd).
 *      The quantity and cost of the last buy are left in last_qty/last_cost.
 *   S4 REDEMPTION FLOOR. zxv_verify_vault_solvency(reserve, N, h, fn, fd,
 *      backing) is true exactly when
 *          reserve * fd >= fn * (N - h) * backing
 *      (reserve >= f * (N - h) * backing with f = fn/fd), compared exactly in
 *      up to 192 bits. Vault holdings h are EXCLUDED from the base: a V-Bill
 *      the protocol owns is not a claim on the reserve. fd == 0 or h > N
 *      fails closed (false). svault_redemption_floor gives the per-V-Bill
 *      floor as the exact, unreduced rational reserve / (N - h); with
 *      N == h there is no outside holder and it returns SVAULT_ZERO.
 *      The svault_ wrappers read reserve (the reserve account's DEBIT), N and
 *      h from the ledger; vault cash is not counted as reserve.
 *   S5 FIRST LOSS. svault_first_loss(loss) redeems b = min(loss, h) vault
 *      V-Bills back to the issuer (pay_ledger_redeem, at par: N and h both
 *      fall by b) and reports loss - b as the part it could not cover. It
 *      never touches the reserve or the vault's cash (see S6 for V-Bills the
 *      vault did not buy). b == 0 posts nothing
 *      and returns SVAULT_ZERO.
 *   S6 AUDIT. Lifetime counters share_total, swept_total, overflow_total,
 *      spent_total, vbills_bought, vbills_burned. svault_check verifies
 *          3 * (swept_total + overflow_total) + r == share_total, r < 3,
 *          spent_total <= swept_total,
 *          cash DEBIT >= swept_total - spent_total, cash CREDIT == 0,
 *          vbills_burned <= vbills_bought,
 *          vault V-Bill DEBIT >= vbills_bought - vbills_burned, CREDIT == 0.
 *      Only svault's own postings are counted: anyone may post INTO the
 *      vault's accounts (the ledger cannot stop it), so a deposit from
 *      outside is surplus that S6 ignores and S3 never spends; it cannot
 *      break the audit. Value leaving the accounts other than by svault
 *      still does. svault_first_loss burns outside V-Bills too (they are
 *      vault-held and absorb first loss like the rest), the vault's own
 *      first: what it burns beyond bought - burned counts in foreign_burned.
 *
 * Every arithmetic step is overflow-checked before anything is posted; every
 * division is a documented floor whose remainder is either carried (S1) or
 * left where it was (S3: cash below one price stays in the vault).
 * Requests: the caller supplies each pay_posting_req_t with its identifiers
 * (UETR, end-to-end id, idempotency key); svault overwrites its lines and
 * kind. No operation here is signed; authorising a sweep or a buy is the
 * caller's policy (a verify hook as in vhub.h belongs there, not here).
 *
 * HONEST LIMITS. Vault V-Bills are still securities exposure, likely
 * securities in many jurisdictions; the 1/3 cap limits reflexivity, it does
 * not remove it; the vault cannot create money, only spend fees it was
 * given; buying at a caller-supplied price is only as fair as that price.
 * It is designed to support a solvency policy; it is no guarantee of
 * solvency and no legal or regulatory status is claimed. Freestanding C11:
 * no allocation, no floating point, no libc.
 */
#ifndef ZXV_SVAULT_H
#define ZXV_SVAULT_H

#include <stdbool.h>
#include <stdint.h>
#include "../pay/pay_ledger.h"

#define SVAULT_CAP_DEN   3u /* S2: the vault holds at most 1/3 of N          */
#define SVAULT_SHARE_DEN 3u /* S1: the vault gets 1/3 of the reserve share */

typedef enum {
    SVAULT_OK = 0,
    SVAULT_OVERFLOW = 1, /* S1: at the cap, the part stayed in the reserve */
    SVAULT_ZERO = 2,     /* nothing to move: zero part / qty / burn / base */
    SVAULT_ERR_ARG = -1,
    SVAULT_ERR_STATE = -2,    /* init: an account not usable as described */
    SVAULT_ERR_OVERFLOW = -3, /* a counter would overflow                 */
    SVAULT_ERR_LEDGER = -4,   /* the ledger refused (see last_ledger)     */
    SVAULT_ERR_FLOOR = -5,    /* S1: the sweep would break the floor      */
    SVAULT_ERR_AUTH = -6      /* S3: seller accounts of different owners  */
} svault_status_t;

typedef struct {
    uint32_t reserve, cash, vbill_issuer, vbills; /* ledger accounts */
    uint64_t carry;                               /* S1 r, < 3       */
    uint64_t share_total, swept_total, overflow_total, spent_total;
    uint64_t vbills_bought, vbills_burned;
    uint64_t foreign_burned;      /* S5: outside V-Bills burned (S6) */
    uint64_t last_qty, last_cost; /* S3: the last buy */
    uint64_t floor_num, floor_den, backing; /* S1 solvency floor (S4) */
    pay_status_t last_ledger;
    bool ready;
} svault_t;

/* Set up the vault over existing accounts (see THE FOUR ACCOUNTS). Refuses
 * (SVAULT_ERR_STATE) accounts that are missing, inactive, not distinct, of
 * mismatched assets, an issuer that is not PAY_ACCT_ISSUER, a vault or
 * reserve account that is, or a cash / V-Bill account that is not empty. */
svault_status_t svault_init(svault_t *v, const pay_ledger_t *L, uint32_t reserve, uint32_t cash,
                            uint32_t vbill_issuer, uint32_t vbills);

/* S1: the solvency floor a sweep must keep (S4 terms). floor_den == 0 is
 * refused (SVAULT_ERR_ARG). */
svault_status_t svault_set_floor(svault_t *v, uint64_t floor_num, uint64_t floor_den,
                                 uint64_t backing);

/* S1 (pure): part = floor((share + rem_in) / 3), rem_out = the rest. False
 * (outputs untouched) when rem_in >= 3 or share + rem_in overflows. */
bool svault_carry(uint64_t rem_in, uint64_t share, uint64_t *part, uint64_t *rem_out);

/* S1. SVAULT_OK (moved), SVAULT_OVERFLOW (kept in the reserve), SVAULT_ZERO
 * (part 0), or an error with nothing changed (SVAULT_ERR_FLOOR: the reserve
 * left would be below the floor). */
svault_status_t svault_sweep(svault_t *v, pay_ledger_t *L, pay_posting_req_t *req,
                             uint64_t reserve_share);
/* S1 from pay_assure_split's output: sweeps parts[PAY_ASSURE_RESERVE_FLOOR]. */
svault_status_t svault_sweep_split(svault_t *v, pay_ledger_t *L, pay_posting_req_t *req,
                                   const uint64_t parts[PAY_ASSURE_BUCKETS]);
/* S1 from a fee: pay_assure_split(fee) then svault_sweep_split. */
svault_status_t svault_sweep_fee(svault_t *v, pay_ledger_t *L, pay_posting_req_t *req,
                                 uint64_t fee);

/* S2 (pure): floor(n / 3). */
uint64_t svault_cap(uint64_t n);
/* S2 from the ledger: floor(N / 3) - h, or 0. 0 for a vault not set up. */
uint64_t svault_room(const svault_t *v, const pay_ledger_t *L);
/* N (issuer CREDIT), h (vault V-Bill DEBIT), cash and reserve (DEBITs).
 * Any pointer may be NULL. False when the vault is not set up. */
bool svault_balances(const svault_t *v, const pay_ledger_t *L, uint64_t *n, uint64_t *h,
                     uint64_t *cash, uint64_t *reserve);

/* S3. SVAULT_OK, SVAULT_ZERO (qty 0, nothing posted), or an error
 * (SVAULT_ERR_AUTH: the seller's two accounts have different owners). */
svault_status_t svault_buy(svault_t *v, pay_ledger_t *L, pay_posting_req_t *req,
                           uint32_t seller_fiat_acct, uint32_t seller_vbill_acct, uint64_t price,
                           uint64_t max_qty);

/* S4 (pure): reserve * fd >= fn * (n - h) * backing, exactly. False when
 * fd == 0 or h > n. */
bool zxv_verify_vault_solvency(uint64_t reserve, uint64_t n, uint64_t h, uint64_t floor_num,
                               uint64_t floor_den, uint64_t backing);
/* S4 (pure): *num = reserve, *den = n - h. SVAULT_ZERO (0/0) when n == h,
 * SVAULT_ERR_ARG when h > n or an output is NULL. */
svault_status_t svault_redemption_floor(uint64_t reserve, uint64_t n, uint64_t h, uint64_t *num,
                                        uint64_t *den);
/* S4 from the ledger. False for a vault not set up. */
bool svault_verify_solvency(const svault_t *v, const pay_ledger_t *L, uint64_t floor_num,
                            uint64_t floor_den, uint64_t backing);
svault_status_t svault_floor(const svault_t *v, const pay_ledger_t *L, uint64_t *num,
                             uint64_t *den);

/* S5. *uncovered (optional) gets loss_vbills - burned. SVAULT_OK, SVAULT_ZERO
 * (h == 0 or loss 0: nothing posted, *uncovered = loss), or an error (then
 * *uncovered = loss as well: nothing was absorbed). */
svault_status_t svault_first_loss(svault_t *v, pay_ledger_t *L, pay_posting_req_t *req,
                                  uint64_t loss_vbills, uint64_t *uncovered);

/* S6. */
bool svault_check(const svault_t *v, const pay_ledger_t *L);

#endif /* ZXV_SVAULT_H */
