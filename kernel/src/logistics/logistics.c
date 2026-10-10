/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* logistics.c — the crates, the syndicate, the negotiator's cut, and the escrow
 * that stays shut until delivery. No libc, no float on target, no invented facts. */

#include "logistics.h"
#include "ed25519_verify.h"   /* ed25519_verify — the built-in delivery attestation check */

/* ---- tiny freestanding helpers (no libc dependency) ---- */

static void cid_copy(uint8_t dst[LOG_CID_LEN], const uint8_t src[LOG_CID_LEN]) {
    for (uint32_t i = 0; i < LOG_CID_LEN; i++) dst[i] = src[i];
}

static log_contract_t *find_mut(log_state_t *s, uint64_t id) {
    if (!s || id == 0) return 0;
    for (uint32_t i = 0; i < LOG_MAX_CONTRACTS; i++) {
        if (s->contracts[i].in_use && s->contracts[i].id == id) return &s->contracts[i];
    }
    return 0;
}

/* Distribute `amount` among n members by `shares`, writing out[0..n-1] so that
 * the parts sum to `amount` EXACTLY. The last member absorbs the rounding
 * remainder — cargo is conserved to the unit, never minted or leaked. Takes the
 * FRACTION first (share/sum) then scales, so a big `amount` never overflows the
 * Q32.32 intermediate. Returns LOG_OK or a negative LOG_ERR_*. */
#ifndef TEST_HOST
/* Exact floor(a * b / d) on raw Q32.32 magnitudes (scales cancel), with a
 * 128-bit product from 32-bit limbs: no __int128, no hardware divide. The
 * old "fraction first" form rounded 1/5 in Q32.32, so 100 * 1/5 came out a
 * few ulps under 20 on the target. Requires a*b/d < 2^64 (true for share
 * <= sum). */
static uint64_t log_muldiv(uint64_t a, uint64_t b, uint64_t d)
{
    uint64_t al = a & 0xffffffffu, ah = a >> 32, bl = b & 0xffffffffu, bh = b >> 32;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + (lh & 0xffffffffu) + (hl & 0xffffffffu);
    uint64_t lo = (ll & 0xffffffffu) | (mid << 32);
    uint64_t hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    uint64_t r = 0, q = 0;
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

/* amount * share / sum, for 0 <= share <= sum, sum > 0. */
static surplus_real_t log_share(surplus_real_t amount, surplus_real_t share, surplus_real_t sum)
{
#ifdef TEST_HOST
    return amount * share / sum;
#else
    uint64_t mag = (amount < 0) ? (uint64_t) 0 - (uint64_t) amount : (uint64_t) amount;
    uint64_t q = log_muldiv(mag, (uint64_t) share, (uint64_t) sum);
    return (amount < 0) ? -(surplus_real_t) q : (surplus_real_t) q;
#endif
}

static int32_t split_proportional(surplus_real_t amount, const surplus_real_t shares[],
                                  uint32_t n, surplus_real_t out[]) {
    if (n == 0 || n > LOG_MAX_PARTIES) return LOG_ERR_SHARES;
    surplus_real_t ssum = SR_ZERO;
    for (uint32_t i = 0; i < n; i++) {
        if (SR_CMP(shares[i], SR_ZERO) < 0) return LOG_ERR_SHARES;  /* no negative shares */
        ssum = SR_ADD(ssum, shares[i]);
    }
    if (SR_CMP(ssum, SR_ZERO) <= 0) return LOG_ERR_SHARES;          /* need a positive pie */

    surplus_real_t acc = SR_ZERO;
    for (uint32_t i = 0; i + 1 < n; i++) {
        out[i] = log_share(amount, shares[i], ssum); /* exact; no (x*n)/d overflow */
        acc = SR_ADD(acc, out[i]);
    }
    out[n - 1] = SR_SUB(amount, acc);                    /* remainder => exact conservation */
    return LOG_OK;
}

/* ================= lifecycle ================= */

void log_init(log_state_t *s) {
    if (!s) return;
    for (uint32_t i = 0; i < LOG_MAX_CONTRACTS; i++) {
        s->contracts[i].in_use = false;
        s->contracts[i].id = 0;
    }
    for (uint32_t i = 0; i < LOG_MAX_SPLITS; i++) {
        s->splits[i].in_use = false;
        s->splits[i].contract_id = 0;
    }
    rep_init(&s->rep);
    s->next_id = 1;
    s->verify = 0;              /* UNBOUND => escrow fails closed until set */
}

/* ================= (3)+(4) open a contract ================= */

int32_t log_contract_open(log_state_t *s,
                          const uint32_t parties[], uint32_t n_parties,
                          surplus_real_t total_units,
                          const uint8_t proof_cid[LOG_CID_LEN],
                          const op_term_t *term) {
    if (!s) return LOG_ERR_NULL;
    if (n_parties > LOG_MAX_PARTIES) return LOG_ERR_SHARES;
    if (n_parties > 0 && !parties) return LOG_ERR_NULL;

    /* Compose The One Policy: a term that produces asymmetric harm (or usury,
     * coercion, a fraud root) is, by definition, not a term. Refuse it. */
    if (term && !op_symbiotic_ok(term)) return LOG_ERR_VOID;

    /* Find a free slot. */
    log_contract_t *c = 0;
    for (uint32_t i = 0; i < LOG_MAX_CONTRACTS; i++) {
        if (!s->contracts[i].in_use) { c = &s->contracts[i]; break; }
    }
    if (!c) return LOG_ERR_FULL;

    c->in_use = true;
    c->id = s->next_id++;
    c->n_parties = n_parties;
    for (uint32_t i = 0; i < n_parties; i++) c->parties[i] = parties[i];
    c->total_units = total_units;
    c->escrow_held = SR_ZERO;
    c->delivered = false;
    c->parent_id = 0;

    /* Commodity backing: record the SUPPLIED photo-attestation CID verbatim. The
     * device does not photograph the world — an absent CID stays UNBOUND, never
     * invented. */
    if (proof_cid) {
        cid_copy(c->proof_cid, proof_cid);
        c->has_proof_cid = true;
    } else {
        for (uint32_t i = 0; i < LOG_CID_LEN; i++) c->proof_cid[i] = 0;
        c->has_proof_cid = false;
    }
    return (int32_t)c->id;
}

/* ================= (1) syndicate ================= */

static log_split_t *fresh_split(log_state_t *s) {
    for (uint32_t i = 0; i < LOG_MAX_SPLITS; i++) {
        if (!s->splits[i].in_use) return &s->splits[i];
    }
    return 0;
}

int32_t log_syndicate_form(log_state_t *s, uint64_t contract_id,
                           const uint32_t members[], const surplus_real_t shares[],
                           uint32_t n) {
    if (!s || !members || !shares) return LOG_ERR_NULL;
    if (n == 0 || n > LOG_MAX_PARTIES) return LOG_ERR_SHARES;
    log_contract_t *c = find_mut(s, contract_id);
    if (!c) return LOG_ERR_NOT_FOUND;

    log_split_t *sp = fresh_split(s);
    if (!sp) return LOG_ERR_FULL;

    int32_t rc = split_proportional(c->total_units, shares, n, sp->alloc);
    if (rc != LOG_OK) return rc;

    sp->in_use = true;
    sp->contract_id = contract_id;
    sp->n = n;
    for (uint32_t i = 0; i < n; i++) sp->members[i] = members[i];
    sp->is_subcontract = false;
    sp->negotiator = 0;
    sp->negotiator_cut = SR_ZERO;
    sp->margin = SR_ZERO;
    return LOG_OK;
}

/* ================= (2) secondary marketplace ================= */

int32_t log_subcontract_split(log_state_t *s, uint64_t parent_id,
                              const uint32_t members[], const surplus_real_t shares[],
                              uint32_t n, uint32_t negotiator, surplus_real_t margin,
                              const op_term_t *term) {
    if (!s || !members || !shares) return LOG_ERR_NULL;
    if (n == 0 || n > LOG_MAX_PARTIES) return LOG_ERR_SHARES;
    if (SR_CMP(margin, SR_ZERO) < 0) return LOG_ERR_MARGIN;

    /* The customary ceiling: 11%. Take the fraction first, exactly as 11/100. */
    surplus_real_t customary = SR_DIV(SR_FROM_INT(LOG_CUSTOMARY_NUM),
                                      SR_FROM_INT(LOG_CUSTOMARY_DEN));
    if (SR_CMP(margin, customary) > 0) return LOG_ERR_MARGIN;   /* over 11% — refused */

    /* Compose onepolicy: an asymmetric-harm term is not a term. */
    if (term && !op_symbiotic_ok(term)) return LOG_ERR_VOID;

    log_contract_t *c = find_mut(s, parent_id);
    if (!c) return LOG_ERR_NOT_FOUND;

    log_split_t *sp = fresh_split(s);
    if (!sp) return LOG_ERR_FULL;

    /* Negotiator cut = total * margin (value * fraction — the safe order). The
     * sub-contractors split the remainder, conserved so cut + sum == total. */
    surplus_real_t cut = SR_MUL(c->total_units, margin);
    surplus_real_t remainder = SR_SUB(c->total_units, cut);

    int32_t rc = split_proportional(remainder, shares, n, sp->alloc);
    if (rc != LOG_OK) return rc;

    sp->in_use = true;
    sp->contract_id = parent_id;
    sp->n = n;
    for (uint32_t i = 0; i < n; i++) sp->members[i] = members[i];
    sp->is_subcontract = true;
    sp->negotiator = negotiator;
    sp->negotiator_cut = cut;
    sp->margin = margin;
    return LOG_OK;
}

/* ================= (5) automated escrow ================= */

int32_t log_escrow_deposit(log_state_t *s, uint64_t contract_id,
                           surplus_real_t amount) {
    if (!s) return LOG_ERR_NULL;
    if (SR_CMP(amount, SR_ZERO) < 0) return LOG_ERR_AMOUNT;
    /* Material capital must be a priceable form to be escrowable. It is
     * (ZCAP_MANUFACTURED) — but we ASK zcapital rather than assume, so the
     * inalienability wall is honoured, not re-implemented. */
    if (!zcap_is_priceable(LOG_ESCROW_FORM)) return LOG_ERR_CAPITAL;

    log_contract_t *c = find_mut(s, contract_id);
    if (!c) return LOG_ERR_NOT_FOUND;

    c->escrow_held = SR_ADD(c->escrow_held, amount);
    return LOG_OK;
}

void log_set_verifier(log_state_t *s, log_deliv_verify_fn fn) {
    if (s) s->verify = fn;
}

bool log_ed25519_delivery_verify(uint64_t contract_id, const log_delivery_t *d) {
    if (!d) return false;
    /* Canonical message: contract_id LE64 (8) || delivery_cid (LOG_CID_LEN) ||
     * confirmed (1). We verify THIS signature — never the fact it asserts. */
    uint8_t msg[8 + LOG_CID_LEN + 1];
    uint64_t id = contract_id;
    for (int i = 0; i < 8; i++) { msg[i] = (uint8_t)(id & 0xFFu); id >>= 8; }
    for (uint32_t i = 0; i < LOG_CID_LEN; i++) msg[8 + i] = d->delivery_cid[i];
    msg[8 + LOG_CID_LEN] = d->confirmed ? 1u : 0u;
    return ed25519_verify(msg, sizeof(msg), d->sig, d->attestor);
}

/* Is `party` actually a signatory of this contract? An outsider's "it arrived"
 * is not an attestation — only a counterparty can witness the crates land. */
static bool log_is_party(const log_contract_t *c, uint32_t party) {
    for (uint32_t i = 0; i < c->n_parties; i++)
        if (c->parties[i] == party) return true;
    return false;
}

log_result_t log_escrow_release(log_state_t *s, uint64_t contract_id,
                                const log_delivery_t *proof) {
    log_result_t r;
    r.status = LOG_HELD;
    r.released = SR_ZERO;
    r.escrow_remaining = SR_ZERO;

    if (!s) { r.status = LOG_BAD_ARG; return r; }
    log_contract_t *c = find_mut(s, contract_id);
    if (!c) { r.status = LOG_NO_CONTRACT; return r; }

    r.escrow_remaining = c->escrow_held;

    /* THE gate — every clause fails CLOSED (LOG_HELD, escrow untouched). We never
     * invent a delivery the world did not report, and we never trust a bare word.
     *
     *  (a) an attestation must be SUPPLIED and assert delivery;
     *  (b) the goods must have had a witness bound at open (has_proof_cid) —
     *      you cannot "deliver" crates nobody ever pinned;
     *  (c) the confirming party must be ON the contract — an outsider cannot
     *      spring someone else's escrow;
     *  (d) a verifier must be BOUND — unverified, `confirmed` is just a bool a
     *      caller flipped, so with no oracle the escrow stays shut; and
     *  (e) the verifier must ACCEPT the signature over the canonical message,
     *      binding this attestation to THIS contract and THIS evidence. */
    if (!proof || !proof->confirmed)             { r.status = LOG_HELD; return r; }  /* (a) */
    if (!c->has_proof_cid)                        { r.status = LOG_HELD; return r; }  /* (b) */
    if (!log_is_party(c, proof->confirming_party)){ r.status = LOG_HELD; return r; }  /* (c) */
    if (!s->verify)                               { r.status = LOG_HELD; return r; }  /* (d) */
    if (!s->verify(contract_id, proof))           { r.status = LOG_HELD; return r; }  /* (e) */

    /* IDEMPOTENCY: a contract is delivered, paid, and credited exactly ONCE.
     * Without this, re-calling with any confirmed attestation against an already-
     * delivered (escrow-empty) contract would re-run the credibility awards below,
     * letting a party FARM follow-through with no new delivery. One delivery, one
     * rung. */
    if (c->delivered) {
        r.status = LOG_HELD;      /* nothing left to release; no re-credit */
        r.released = SR_ZERO;
        return r;
    }

    /* Delivery confirmed. Pay out the whole held amount and record follow-through
     * for every party — a completed delivery raises credibility. */
    r.released = c->escrow_held;
    c->escrow_held = SR_ZERO;
    c->delivered = true;
    r.escrow_remaining = SR_ZERO;
    r.status = LOG_RELEASED;

    for (uint32_t i = 0; i < c->n_parties; i++) {
        uint32_t party = c->parties[i];
        /* This module awards only the follow-through badge, so badge_score IS the
         * current level; award one rung higher (monotonic — never lowers it). */
        uint32_t now = badge_score(&s->rep, party);
        (void)badge_award(&s->rep, party, LOG_BADGE_FOLLOWTHROUGH, now + 1u);
    }
    return r;
}

/* ================= (3) credibility ================= */

surplus_real_t log_credibility(const log_state_t *s, uint32_t party) {
    if (!s) return SR_ZERO;
    /* Follow-through rating from reputation.h — rises with completed deliveries. */
    uint32_t score = badge_score(&s->rep, party);
    return SR_FROM_INT((int64_t)score);
}

/* ================= read-only lookups ================= */

const log_contract_t *log_contract_find(const log_state_t *s, uint64_t contract_id) {
    if (!s || contract_id == 0) return 0;
    for (uint32_t i = 0; i < LOG_MAX_CONTRACTS; i++) {
        if (s->contracts[i].in_use && s->contracts[i].id == contract_id)
            return &s->contracts[i];
    }
    return 0;
}

const log_split_t *log_split_find(const log_state_t *s, uint64_t contract_id) {
    if (!s || contract_id == 0) return 0;
    for (uint32_t i = 0; i < LOG_MAX_SPLITS; i++) {
        if (s->splits[i].in_use && s->splits[i].contract_id == contract_id)
            return &s->splits[i];
    }
    return 0;
}
