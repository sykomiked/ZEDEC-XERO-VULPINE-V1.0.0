/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alloc.c — see alloc.h. The engine that hands capital back to whoever grew it,
 * and a token no throne can claw back. */
#include "alloc.h"
#include "sha256.h"   /* the same CID primitive src/ipfs content-addresses with */

/* ============================================================= *
 *  PART 1 — sustainable capital allocation                       *
 * ============================================================= */

static bool form_in_range(zcap_form_t form) {
    return (int)form >= 0 && (int)form < ZCAP_FORM_COUNT;
}

#ifndef TEST_HOST
/* Exact integer helpers for the Q32.32 path. A Q32.32 fraction such as
 * 30/100 or 11/100 is not representable, so "take the fraction first" left
 * shares a few ulps short (30 became 29.99999999) and made 11% of 100 land
 * just under 11 (refusing the customary rate). These compute the rational
 * expressions exactly with a 128-bit product built from 32-bit limbs: no
 * __int128, no hardware divide. */
static void alloc_mul64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
    uint64_t al = a & 0xffffffffu, ah = a >> 32, bl = b & 0xffffffffu, bh = b >> 32;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + (lh & 0xffffffffu) + (hl & 0xffffffffu);
    *lo = (ll & 0xffffffffu) | (mid << 32);
    *hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}

/* floor(a * b / d) for d > 0 when the quotient fits in 64 bits. */
static uint64_t alloc_muldiv(uint64_t a, uint64_t b, uint64_t d)
{
    uint64_t hi, lo, r = 0, q = 0;
    alloc_mul64(a, b, &hi, &lo);
    for (int i = 127; i >= 0; i--) {
        uint64_t bit = (i >= 64) ? (hi >> (i - 64)) & 1u : (lo >> i) & 1u;
        uint64_t carry = r >> 63;
        r = (r << 1) | bit;
        if (carry || r >= d) {
            r -= d;
            if (i < 64) q |= (uint64_t) 1 << i;
        }
    }
    return q;
}
#endif

/* pool * units / total, for 0 < units <= total and pool > 0. */
static surplus_real_t alloc_prop_share(surplus_real_t pool, surplus_real_t units,
                                       surplus_real_t total)
{
#ifdef TEST_HOST
    return pool * units / total;
#else
    /* Raw Q32.32 scales cancel: (P*U/T) raw == P_raw * U_raw / T_raw. */
    return (surplus_real_t) alloc_muldiv((uint64_t) pool, (uint64_t) units, (uint64_t) total);
#endif
}

/* deduction <= principal * NUM / DEN, compared exactly. */
static bool alloc_within_rate(surplus_real_t deduction, surplus_real_t principal, uint32_t num,
                              uint32_t den)
{
#ifdef TEST_HOST
    return deduction * (double) den <= principal * (double) num;
#else
    if (SR_CMP(deduction, SR_ZERO) <= 0) return true;
    if (SR_CMP(principal, SR_ZERO) <= 0) return false;
    uint64_t dh, dl, ph, pl;
    alloc_mul64((uint64_t) deduction, den, &dh, &dl);
    alloc_mul64((uint64_t) principal, num, &ph, &pl);
    return dh < ph || (dh == ph && dl <= pl);
#endif
}

void alloc_pool_init(alloc_pool_t *p) {
    if (!p) return;
    for (int f = 0; f < ZCAP_FORM_COUNT; f++) {
        p->count[f] = 0;
        p->pooled[f] = SR_ZERO;
        for (int i = 0; i < ALLOC_MAX_CONTRIBUTORS; i++) {
            p->book[f][i].contributor = 0;
            p->book[f][i].units = SR_ZERO;
            p->book[f][i].active = false;
        }
    }
}

int32_t alloc_contribute(alloc_pool_t *p, uint32_t contributor,
                         zcap_form_t form, surplus_real_t units) {
    if (!p || !form_in_range(form)) return -ALLOC_ERR_ARG;
    if (SR_CMP(units, SR_ZERO) <= 0) return -ALLOC_ERR_ARG; /* no zero/negative "gift" */

    /* Accumulate onto an existing entry if this party already contributed. */
    for (uint32_t i = 0; i < p->count[form]; i++) {
        if (p->book[form][i].active && p->book[form][i].contributor == contributor) {
            p->book[form][i].units = SR_ADD(p->book[form][i].units, units);
            p->pooled[form] = SR_ADD(p->pooled[form], units);
            return (int32_t)i;
        }
    }
    if (p->count[form] >= ALLOC_MAX_CONTRIBUTORS) return -ALLOC_ERR_FULL;

    uint32_t slot = p->count[form];
    p->book[form][slot].contributor = contributor;
    p->book[form][slot].units = units;
    p->book[form][slot].active = true;
    p->count[form] = slot + 1;
    p->pooled[form] = SR_ADD(p->pooled[form], units);
    return (int32_t)slot;
}

int32_t alloc_distribute(alloc_pool_t *p, zcap_form_t form, alloc_result_t *out) {
    if (!p || !out || !form_in_range(form)) return -ALLOC_ERR_ARG;

    const surplus_real_t pool = p->pooled[form];
    const uint32_t n = p->count[form];
    if (n == 0 || SR_CMP(pool, SR_ZERO) <= 0) return -ALLOC_ERR_EMPTY; /* never invent a payout */

    /* Sum of ACTIVE contributed units — the proportionality denominator. */
    surplus_real_t total_units = SR_ZERO;
    for (uint32_t i = 0; i < n; i++)
        if (p->book[form][i].active)
            total_units = SR_ADD(total_units, p->book[form][i].units);
    if (SR_CMP(total_units, SR_ZERO) <= 0) return -ALLOC_ERR_EMPTY;

    /* The distribution is reciprocal by construction: the contributors conveyed
     * pool of this form IN, and the same pool flows back OUT to them — a
     * conserved distribution to willing contributors. Its integrity is
     * CONSERVATION (the shares sum to the pool exactly) plus the sustainability
     * bound (alloc_sustainable_ok, applied where a draw is taken). The maxim's
     * harm/coercion/interest gate belongs on a DEAL with terms, not on a bare
     * conserved split — feeding it hardcoded-benign inputs here would be theatre. */

    /* Proportional split: floor(pool * units / total), computed exactly
     * (alloc_prop_share). SR_DIV(SR_MUL(pool, units), total) would overflow
     * int64 for large contributions, and SR_MUL(pool, SR_DIV(units, total))
     * rounds the fraction, so 30/100 of 100 came out a few ulps under 30 on
     * the target. The final active share takes the remainder so the shares
     * sum to pool EXACTLY. */
    out->form = form;
    out->count = 0;
    out->total = SR_ZERO;

    /* Locate the last active index so its share can absorb the remainder. */
    int32_t last_active = -1;
    for (uint32_t i = 0; i < n; i++)
        if (p->book[form][i].active) last_active = (int32_t)i;

    surplus_real_t running = SR_ZERO;
    for (uint32_t i = 0; i < n; i++) {
        if (!p->book[form][i].active) continue;
        surplus_real_t share;
        if ((int32_t)i == last_active) {
            share = SR_SUB(pool, running);          /* exact conservation */
        } else {
            share = alloc_prop_share(pool, p->book[form][i].units, total_units);
            running = SR_ADD(running, share);
        }
        out->shares[out->count].contributor = p->book[form][i].contributor;
        out->shares[out->count].amount = share;
        out->count++;
        out->total = SR_ADD(out->total, share);
    }
    return ALLOC_OK;
}

bool alloc_sustainable_ok(const alloc_txn_t *txn) {
    if (!txn) return false;

    /* (a) Not a term unless it is reciprocal. The One Policy is the court. */
    if (!op_symbiotic_ok(&txn->term)) return false;

    /* (b) Never draw a steward stock below its SUPPLIED sustainable-yield floor.
     *     remaining = stock_before - draw; refuse if remaining < floor. */
    surplus_real_t remaining = SR_SUB(txn->stock_before, txn->draw);
    if (SR_CMP(remaining, txn->yield_floor) < 0) return false;

    /* (c) No deduction above the customary rate (11%): deduction * 100 <=
     *     principal * 11, compared exactly in 128 bits (alloc_within_rate), so
     *     it cannot overflow and a rounded Q32.32 rate cannot refuse exactly
     *     11%. Exactly 11% is customary and permitted; 12% is a taking. */
    if (!alloc_within_rate(txn->deduction, txn->principal, ALLOC_CUSTOMARY_RATE_NUM,
                           ALLOC_CUSTOMARY_RATE_DEN))
        return false;

    return true;
}

zcap_result_t alloc_acquire_with_financial(zcap_vec_t *v, zcap_form_t to,
                                           surplus_real_t amount) {
    /* Source is hard-wired to financial: it is the only form that may be spent to
     * acquire another. zcap_exchange enforces the rest — a non-priceable Crown
     * `to` returns ZCAP_INALIENABLE and leaves the holding untouched. */
    if (!v) return ZCAP_INSUFFICIENT;
    return zcap_exchange(v, ZCAP_FINANCIAL, to, amount);
}

/* ============================================================= *
 *  PART 2 — the non-replicable, kill-switch-free platform token  *
 * ============================================================= */

/* Content-address a token's provenance: SHA-256 over genesis || owner || nonce.
 * The id therefore commits to the exact founding genesis it was minted against —
 * provenance you can verify, not a serial number a mint hands out. */
static void ptoken_content_address(const uint8_t genesis[PTOKEN_ID_LEN],
                                   uint32_t owner, uint64_t nonce,
                                   uint8_t out_id[PTOKEN_ID_LEN]) {
    uint8_t buf[PTOKEN_ID_LEN + 4 + 8];
    uint32_t j = 0;
    for (uint32_t i = 0; i < PTOKEN_ID_LEN; i++) buf[j++] = genesis[i];
    for (uint32_t i = 0; i < 4; i++) buf[j++] = (uint8_t)(owner >> (8u * i));
    for (uint32_t i = 0; i < 8; i++) buf[j++] = (uint8_t)(nonce >> (8u * i));
    sha256(buf, sizeof(buf), out_id);
}

static bool id_eq(const uint8_t a[PTOKEN_ID_LEN], const uint8_t b[PTOKEN_ID_LEN]) {
    for (uint32_t i = 0; i < PTOKEN_ID_LEN; i++)
        if (a[i] != b[i]) return false;
    return true;
}

void ptoken_ledger_init(ptoken_ledger_t *l) {
    if (!l) return;
    l->count = 0;
    for (uint32_t i = 0; i < PTOKEN_LEDGER_CAP; i++) {
        for (uint32_t b = 0; b < PTOKEN_ID_LEN; b++) l->entries[i].id[b] = 0;
        l->entries[i].owner = 0;
        l->entries[i].nonce = 0;
        l->entries[i].spent = false;
    }
}

int32_t ptoken_mint(ptoken_ledger_t *l, uint32_t owner,
                    const uint8_t genesis[PTOKEN_ID_LEN], ptoken_t *out) {
    if (!l || !genesis || !out) return -PTOKEN_ERR_ARG;
    if (l->count >= PTOKEN_LEDGER_CAP) return -PTOKEN_ERR_FULL;

    uint64_t nonce = (uint64_t)l->count;   /* append ordinal — phase-tick, not clock */
    ptoken_t tok;
    ptoken_content_address(genesis, owner, nonce, tok.id);
    tok.owner = owner;
    tok.nonce = nonce;
    tok.spent = false;

    l->entries[l->count] = tok;
    l->count++;
    *out = tok;
    return PTOKEN_OK;
}

/* Find the index of the live (unspent) token with this id, or -1. If an entry
 * with the id exists but is spent, report that via *was_spent so the caller can
 * distinguish a double-spend from an unknown id. */
static int32_t ptoken_find_live(const ptoken_ledger_t *l,
                                const uint8_t id[PTOKEN_ID_LEN],
                                bool *was_spent) {
    *was_spent = false;
    for (uint32_t i = 0; i < l->count; i++) {
        if (id_eq(l->entries[i].id, id)) {
            if (l->entries[i].spent) { *was_spent = true; continue; }
            return (int32_t)i;
        }
    }
    return -1;
}

int32_t ptoken_transfer(ptoken_ledger_t *l, const uint8_t id[PTOKEN_ID_LEN],
                       uint32_t from, uint32_t to) {
    if (!l || !id) return -PTOKEN_ERR_ARG;

    bool was_spent = false;
    int32_t idx = ptoken_find_live(l, id, &was_spent);

    /* Double-spend refusal: the id was already transferred out. Refuse BEFORE any
     * mutation so no balance moves. This is the non-replicable invariant. */
    if (idx < 0) {
        if (was_spent) return -PTOKEN_ERR_SPENT;
        return -PTOKEN_ERR_NOT_FOUND;
    }
    /* Only the current owner may move it. There is no seizure path — `to` cannot
     * take a token `from` does not hold. */
    if (l->entries[idx].owner != from) return -PTOKEN_ERR_NOT_OWNER;
    if (l->count >= PTOKEN_LEDGER_CAP) return -PTOKEN_ERR_FULL;

    /* Append-only: spend the old, mint the successor. */
    uint64_t nonce = (uint64_t)l->count;
    ptoken_t nxt;
    ptoken_content_address(l->entries[idx].id, to, nonce, nxt.id);
    nxt.owner = to;
    nxt.nonce = nonce;
    nxt.spent = false;

    l->entries[idx].spent = true;      /* the single permitted mutation */
    l->entries[l->count] = nxt;
    l->count++;
    return PTOKEN_OK;
}

uint32_t ptoken_balance(const ptoken_ledger_t *l, uint32_t owner) {
    if (!l) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < l->count; i++)
        if (!l->entries[i].spent && l->entries[i].owner == owner) n++;
    return n;
}

/* ---- DECLARATION -----------------------------------------------------------
 * The requirement on sha256_ready is not architectural taste: this file
 * includes sha256.h (alloc.c:6) and content-addresses every minted token
 * through it, so a sha256 that is absent or wrong makes every token id wrong.
 * mm_ready is the other half -- the pool and the ledger are caller-owned
 * structures that must be mapped before they are touched.
 *
 * NO BRING-UP YET: alloc owns no global state at all (every entry point takes
 * the pool or the ledger as an argument), so there is no instance here to bring
 * up. Declaring the edge is still worth doing on its own -- it is what makes
 * "alloc silently stopped hashing" a graph question rather than a mystery. */
#include "zxv_decl.h"

ZXV_DECLARE(alloc,
    ZXV_PROVIDES(alloc_ready),
    ZXV_REQUIRES(mm_ready, sha256_ready),
    ZXV_NO_BRINGUP);
