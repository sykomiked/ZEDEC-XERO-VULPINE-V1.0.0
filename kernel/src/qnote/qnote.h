/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* qnote.h — Q-Notes: toxic-asset quarantine lots, sold by Dutch auction,
 * with first-loss and salvage recovery, on pay_ledger.
 *
 * OWNER DECISIONS implemented here: "toxic-asset quarantine to Q-Notes +
 * Dutch auction proceeds to reserve"; "first-loss burn for toxic haircuts"
 * (the solvency vault absorbs first: svault.h S5); "solvency funded by
 * slashed bonds + salvage auctions". Holders' funds are never seized: a
 * quarantined claim's Vino / V-Bills stay where they are (mint.h M8); Q-Notes
 * are a new, separate claim sold to willing buyers. No usury: Q-Notes never
 * carry interest and nothing here grows with time or balance.
 *
 * ACCOUNTS. `reserve` and `escrow` (qnote_init) are non-issuer accounts of
 * one fiat asset; escrow must start empty and is used only by this module.
 * Each lot has its OWN Q-Note asset and issuer account (an ISSUER account
 * with CREDIT 0, of an asset no other lot uses), so the ledger itself says
 * who holds which lot's notes.
 *
 *   Q1 LOT. qnote_lot_open(claim id, face F, unit_face u, issuer, ladder)
 *      opens a lot for a quarantined claim (mint_quarantine's face: the
 *      uncovered minted value, fiat minor units). It offers U = floor(F / u)
 *      Q-Note units of face u each; the dust F - U*u is never offered (it
 *      stays part of the loss, Q4). U >= 1. A claim id may open one lot.
 *      Q-Notes are a claim on whatever is salvaged from that asset (Q5),
 *      never on the reserve, and never carry interest.
 *   Q2 BIDS. While OPEN a bidder submits (fiat account, Q-Note account of
 *      the lot's asset, qty, limit) with 1 <= qty <= U and p_min <= limit <=
 *      p_start. The bid LOCKS qty * limit by ONE ledger transfer bidder ->
 *      escrow; a refused transfer records nothing. At most QNOTE_MAX_BIDS.
 *   Q3 CLEARING (as vauction.h A6, with supply fixed at U). The ladder runs
 *      p_start, p_start - step, ..., ending exactly on p_min (rungs <=
 *      QNOTE_MAX_RUNGS); p_start <= u, so no unit sells above its face.
 *      D(p) = sum of qty over bids with limit >= p. The price is the first
 *      rung with D(p) >= U and all U units sell: bids with limit >= the
 *      previous rung P fill in full (D(P) < U), the marginal bids (p <=
 *      limit < P) share U - D(P) pro rata by qty, floor(A * qty / T) each,
 *      leftover units one each in submission order. If no rung clears, the
 *      price is p_min and every bid fills in full: sold = D(p_min) < U. The
 *      unsold units (U - sold) are cancelled, never issued (recorded).
 *   Q4 SETTLEMENT AND LOSS. Per winning bid ONE posting: escrow -locked,
 *      reserve +fill*p, the lot's issuer CREDIT +fill, the bidder's Q-Note
 *      account +fill, the bidder's fiat +(locked - fill*p) (omitted when 0);
 *      a losing bid gets ONE refund posting. ALL proceeds (sold * p) go to
 *      the reserve and none is burned; escrow returns to where it was. A
 *      refused posting leaves the lot CLEARED and qnote_settle resumes where
 *      it stopped. Then loss = F - proceeds (0 if negative; proceeds <= U*u
 *      <= F by Q3, so never negative in practice).
 *   Q5 RECOVERY. qnote_recover(lot, from, amount) takes salvage proceeds
 *      from `from` (a fiat account) after settlement. Holders are senior up
 *      to the face of their notes: with H = units outstanding (the issuer's
 *      CREDIT) and carry c (fiat held in escrow for this lot, c <= H*u),
 *      x = min(amount, H*u - c) goes to escrow for holders and amount - x
 *      goes to the reserve, in ONE posting. Then D = c + x and T =
 *      floor(D / u) units are REDEEMED (the notes are retired at par, u fiat
 *      each): every holder (each active non-issuer account of the lot's
 *      asset with a balance, in account order) gets floor(T * h / H) units
 *      redeemed and paid, leftover units one each in account order; each
 *      holder is ONE posting: escrow -r*u, payee +r*u, holder -r, issuer
 *      CREDIT -r. The payee is the first active non-issuer account of the
 *      fiat asset with the holder's owner (not escrow or reserve); a holder
 *      without one refuses the whole recovery (QNOTE_ERR_PAYEE) before
 *      anything posts. The new carry is D minus what was paid (< u when
 *      every posting went through; a refused holder posting stops the round
 *      with QNOTE_ERR_LEDGER and its share simply stays in the carry). So
 *      over the life of a lot holders receive at most sold * u <= F, paid
 *      in whole units of face, and everything beyond goes to the reserve.
 *   Q6 FIRST LOSS. qnote_first_loss(lot, vault, backing) converts the loss
 *      to V-Bills at `backing` fiat minor units per V-Bill, ROUNDED UP:
 *      L_v = ceil(loss / backing), so the vault covers at least the loss,
 *      and calls svault_first_loss(L_v): the vault burns b = min(L_v, its
 *      holdings) V-Bills (svault.h S5). covered = min(loss, b * backing);
 *      uncovered = loss - covered is reported for the slashed-bond / salvage
 *      path. Once per lot, after settlement; loss 0 posts nothing
 *      (QNOTE_ZERO).
 *   Q7 CLOSE. qnote_close moves the lot's carry (sub-unit dust, or shares a
 *      refused posting left behind) from escrow to the reserve and closes the
 *      lot: no more recoveries. Units still outstanding stay on the ledger
 *      (never seized) and are recorded as written off.
 *   Q8 AUDIT. qnote_check: escrow DEBIT == sum over lots of (bid locks not
 *      yet settled + carry), escrow CREDIT 0; per lot: sold + unsold == U,
 *      proceeds == (settled fills) * price, issuer CREDIT == settled fills -
 *      retired, paid == retired * u, carry <= outstanding * u, loss == F -
 *      proceeds once settled, first-loss figures consistent; and the
 *      reserve's inflow counter == proceeds + recovery excess + closed carry
 *      over all lots. Every movement is a ledger transfer of one fiat asset
 *      (pay_ledger L1), so no fiat is created or destroyed.
 *
 * Ledger requests carry identifiers derived by SHA3-256 from
 * "zxv/qnote/req" || seed || counter (domain-separated from mint.c's ids).
 * Every multiply and add is overflow-checked; every division is a
 * documented floor whose remainder is allocated (Q3 leftovers), carried
 * (Q5) or reported (Q1 dust, Q6 rounding up).
 *
 * HONEST LIMITS. Q-Notes are likely securities in many jurisdictions (a
 * claim on salvage sold at auction); this is a mechanism, not an offering,
 * and needs legal review before any real use. A Q-Note is worth only what is
 * actually recovered; nothing here promises a recovery. The first-loss burn
 * is policy: it reduces V-Bills outstanding, it does not create money; the
 * uncovered rest must come from slashed bonds or salvage, outside this
 * module. No legal or regulatory status is claimed. Freestanding C11: no
 * allocation, no floating point, no libc.
 */
#ifndef ZXV_QNOTE_H
#define ZXV_QNOTE_H

#include <stdbool.h>
#include <stdint.h>
#include "../pay/pay_ledger.h"
#include "../svault/svault.h"

#define QNOTE_MAX_LOTS  8u
#define QNOTE_MAX_BIDS  16u
#define QNOTE_MAX_RUNGS 4096u

typedef enum {
    QNOTE_OK = 0,
    QNOTE_ZERO = 1, /* Q6: no loss, nothing posted */
    QNOTE_ERR_ARG = -1,
    QNOTE_ERR_FULL = -2,
    QNOTE_ERR_STATE = -3,
    QNOTE_ERR_DUP = -4,    /* Q1: the claim already has a lot */
    QNOTE_ERR_LEDGER = -5, /* see last_ledger */
    QNOTE_ERR_PAYEE = -6,  /* Q5 */
    QNOTE_ERR_VAULT = -7,  /* Q6: svault refused */
    QNOTE_ERR_OVERFLOW = -8
} qnote_status_t;

typedef enum {
    QNOTE_FREE = 0,
    QNOTE_OPEN = 1,
    QNOTE_CLEARED = 2,
    QNOTE_SETTLED = 3,
    QNOTE_CLOSED = 4
} qnote_state_t;

typedef struct {
    uint32_t fiat, qacct; /* the bidder's ledger accounts */
    uint64_t qty, limit, locked, fill;
    bool settled;
} qnote_bid_t;

typedef struct {
    uint8_t state; /* qnote_state_t */
    uint8_t claim[32];
    uint64_t face, unit_face, units, dust; /* Q1 */
    uint32_t issuer;
    uint16_t asset;
    uint64_t p_start, p_step, p_min;
    qnote_bid_t bid[QNOTE_MAX_BIDS];
    uint32_t n_bids;
    uint64_t locked;                        /* Q2: locks not yet settled */
    uint64_t price, sold, unsold, proceeds; /* Q3/Q4 (proceeds as posted) */
    uint32_t winners;
    uint64_t loss; /* Q4 */
    bool fl_done;  /* Q6 */
    uint64_t fl_vbills, fl_burned, fl_covered, fl_uncovered;
    uint64_t carry, paid, retired, excess, recovered; /* Q5 */
    uint64_t closed_carry, written_off;               /* Q7 */
} qnote_lot_t;

typedef struct {
    pay_ledger_t *L;
    uint32_t reserve, escrow;
    uint16_t fiat;
    uint8_t seed[32];
    uint64_t seq;
    qnote_lot_t lot[QNOTE_MAX_LOTS];
    uint64_t reserve_in; /* Q8 */
    pay_status_t last_ledger;
    bool ready;
} qnote_t;

/* Bind to the ledger (see ACCOUNTS). QNOTE_ERR_ARG on NULLs or accounts not
 * usable as described. */
qnote_status_t qnote_init(qnote_t *q, pay_ledger_t *L, uint32_t reserve, uint32_t escrow,
                          const uint8_t seed[32]);

/* Q1. p_start <= unit_face, p_min >= 1, p_step >= 1, p_min <= p_start. */
qnote_status_t qnote_lot_open(qnote_t *q, const uint8_t claim[32], uint64_t face,
                              uint64_t unit_face, uint32_t issuer, uint64_t p_start,
                              uint64_t p_step, uint64_t p_min, uint32_t *lot);
const qnote_lot_t *qnote_lot(const qnote_t *q, uint32_t lot);
/* Q2 */
qnote_status_t qnote_bid(qnote_t *q, uint32_t lot, uint32_t fiat, uint32_t qacct, uint64_t qty,
                         uint64_t limit, uint64_t tick, uint32_t *index);
/* D(p) of Q3 */
uint64_t qnote_demand(const qnote_t *q, uint32_t lot, uint64_t p);
/* Q3 / Q4 */
qnote_status_t qnote_clear(qnote_t *q, uint32_t lot);
qnote_status_t qnote_settle(qnote_t *q, uint32_t lot, uint64_t tick);
/* Q4: loss of a settled (or closed) lot */
qnote_status_t qnote_loss(const qnote_t *q, uint32_t lot, uint64_t *loss);
/* Q6. *burned / *uncovered optional (V-Bills burned, fiat left uncovered). */
qnote_status_t qnote_first_loss(qnote_t *q, uint32_t lot, svault_t *vault, uint64_t backing,
                                uint64_t *burned, uint64_t *uncovered);
/* Q5 */
qnote_status_t qnote_recover(qnote_t *q, uint32_t lot, uint32_t from, uint64_t amount,
                             uint64_t tick);
/* Q7 */
qnote_status_t qnote_close(qnote_t *q, uint32_t lot, uint64_t tick);
/* Q8 */
bool qnote_check(const qnote_t *q);

#endif /* ZXV_QNOTE_H */
