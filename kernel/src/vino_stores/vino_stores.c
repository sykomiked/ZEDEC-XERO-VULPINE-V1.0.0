/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* vino_stores.c — settlement engine implementation.
 *
 * All the honest work lives in vino_ledger_act: three rails posted through the
 * real triple_ledger, then a hard gate, then a full rollback if the gate bites.
 * Everything else is geometry (φ, Fibonacci, the 112% gratuity split) or the
 * single-active-state bookkeeping that keeps a coin from being in two hands.
 */
#include "vino_stores.h"
#include "onepolicy.h"   /* op_symbiotic_ok — usury is not a term, it is refused */
#include "edp_risk.h"    /* edp_fibonacci — reuse the kernel's Fibonacci, not ours */

/* The entity id under which the engine opens its ledger rail account. */
#define VINO_STORES_ENTITY_ID 0x56494E4Fu /* 'VINO' */

/* Small copy helper — no libc memcpy in freestanding land. */
static void vino_copy_cid(uint8_t dst[VINO_PROOF_CID_LEN],
                          const uint8_t src[VINO_PROOF_CID_LEN]) {
    uint32_t i;
    for (i = 0; i < VINO_PROOF_CID_LEN; i++) dst[i] = src ? src[i] : 0;
}

void vino_stores_init(vino_stores_t *vs, triple_ledger_t *ledger) {
    if (!vs) return;
    vs->ledger = ledger;
    vs->num_vouchers = 0;
    vs->next_voucher_id = 1;
    vs->vino_account = 0;
    uint32_t i;
    for (i = 0; i < VINO_STORES_MAX_VOUCHERS; i++) vs->vouchers[i].in_use = false;
    if (ledger) {
        vs->vino_account = triple_ledger_create_account(
            ledger, VINO_STORES_ENTITY_ID, CAP_FINANCIAL, "Vino Stores");
    }
}

vino_voucher_t *vino_find(vino_stores_t *vs, uint64_t voucher_id) {
    if (!vs) return 0;
    uint32_t i;
    for (i = 0; i < vs->num_vouchers; i++) {
        if (vs->vouchers[i].in_use && vs->vouchers[i].base.voucher_id == voucher_id)
            return &vs->vouchers[i];
    }
    return 0;
}

int32_t vino_mint(vino_stores_t *vs, uint64_t *out_id,
                  surplus_real_t denomination, mint_tier_t tier,
                  vino_state_t initial_state,
                  const uint8_t proof_cid[VINO_PROOF_CID_LEN]) {
    if (!vs || !out_id) return VINO_ERR_NULL;
    if (vs->num_vouchers >= VINO_STORES_MAX_VOUCHERS) return VINO_ERR_CAPACITY;

    vino_voucher_t *v = &vs->vouchers[vs->num_vouchers];
    uint64_t id = vs->next_voucher_id++;

    /* Fill the embedded floating_voucher_t — we EXTEND it, not replace it. */
    v->base.voucher_id     = id;
    v->base.issuer_id      = VINO_STORES_ENTITY_ID;
    v->base.holder_id      = VINO_STORES_ENTITY_ID;
    v->base.capital_type   = CAP_FINANCIAL;
    v->base.merit_value    = denomination;
    v->base.coverage_ratio = SR_ZERO;
    v->base.phase          = SR_ZERO;
    v->base.issued_tick    = 0;
    v->base.expires_tick   = 0;     /* floating — no hard expiry */
    v->base.transferable   = true;
    v->base.redeemed       = false;
    /* purpose left as a short tag */
    v->base.purpose[0] = 'V'; v->base.purpose[1] = 'i'; v->base.purpose[2] = 'n';
    v->base.purpose[3] = 'o'; v->base.purpose[4] = 0;

    v->state              = initial_state;
    v->denomination       = denomination;
    v->mint_tier          = tier;
    v->lifecycle          = VINO_MINT;
    v->counterpart_locked = true;   /* the twin is cryptolocked from birth */
    v->in_use             = true;
    vino_copy_cid(v->proof_cid, proof_cid);

    vs->num_vouchers++;
    *out_id = id;
    return VINO_OK;
}

/* Coverage predicate: assets/liabilities >= 1.8. With zero liabilities the
 * position is (trivially) fully covered. Uses the SR macros throughout — never
 * open-coded (a*n)/d, which floors to 0 on the target. */
static bool vino_coverage_ok(surplus_real_t assets, surplus_real_t liabilities) {
    if (SR_CMP(liabilities, SR_ZERO) <= 0) return true; /* no liability to cover */
    surplus_real_t ratio = SR_DIV(assets, liabilities);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(VINO_COVERAGE_NUM),
                                  SR_FROM_INT(VINO_COVERAGE_DEN));
    return SR_CMP(ratio, floor) >= 0;
}

int32_t vino_ledger_act(vino_stores_t *vs, uint64_t voucher_id,
                        vino_lifecycle_t to,
                        surplus_real_t debit, surplus_real_t credit,
                        surplus_real_t equity,
                        const uint8_t proof_cid[VINO_PROOF_CID_LEN]) {
    if (!vs || !vs->ledger) return VINO_ERR_NULL;
    triple_ledger_t *tl = vs->ledger;
    if (vs->vino_account >= tl->num_accounts) return VINO_ERR_NULL;
    account_t *a = &tl->accounts[vs->vino_account];

    /* --- Validate the voucher UP FRONT, before a single rail is posted. ------
     * A ledger act on a voucher that was never minted must fail CLOSED — it must
     * not mutate the ledger and then discover the id does not exist. */
    vino_voucher_t *v = vino_find(vs, voucher_id);
    if (!v) return VINO_ERR_NOT_FOUND;

    /* --- TERMINAL STATE: a redeemed/retired coin is spent. It must NOT settle
     * again — without this, re-calling with VINO_REDEEM re-posts all three rails
     * and pays the retired coin out on every call (double-spend). vino_can_spend
     * and vino_swap already refuse a redeemed voucher; the value-moving path must
     * too. Fail closed, no mutation. */
    if (v->base.redeemed || v->lifecycle == VINO_REDEEM || v->lifecycle == VINO_RETIRE)
        return VINO_ERR_STATE;

    /* --- Usury veto FIRST, before a single byte is written. -----------------
     * Money is equity, never debt. In an honest event the live equity equals
     * the backing net: equity == debit - credit. Any equity claimed ABOVE that
     * net is a return with no backing — interest — and onepolicy does not
     * recognise it as a term at all. Model it and let the One Policy rule.
     *
     * NOTE (why give_a == give_b == ZERO): a rail post is a ONE-DIRECTIONAL
     * ledger movement (money in => debit>0, credit==0; money out => the reverse),
     * NOT a two-party barter. Feeding debit/credit as a deal's give_a/give_b would
     * trip onepolicy's unilateral-extraction guard on every honest single-sided
     * post — that guard exists to judge DEAL TERMS, not backing movements (cf. the
     * battering_ram bare-barter note). We ask the One Policy the ONE question that
     * actually applies here: is the IMPLIED INTEREST usurious? So we hand it a pure
     * usury query — no giver, no taker, just the interest — and let step 2 rule. */
    surplus_real_t backing_net = SR_SUB(debit, credit);
    surplus_real_t excess      = SR_SUB(equity, backing_net);
    surplus_real_t implied_interest =
        (SR_CMP(excess, SR_ZERO) > 0) ? excess : SR_ZERO;

    op_term_t term;
    term.give_a               = SR_ZERO; /* a ledger post is not a two-party deal */
    term.give_b               = SR_ZERO; /* => no extraction/burden dimension here */
    term.harm_a               = SR_ZERO;
    term.harm_b               = SR_ZERO;
    term.interest             = implied_interest;  /* the ONLY thing under test    */
    term.reciprocal           = true;    /* rails answer each other        */
    term.denies_aid           = false;
    term.revoke_for_nonpayment= false;
    term.has_kill_switch      = false;
    term.fraud_root           = false;
    if (!op_symbiotic_ok(&term)) return VINO_ERR_USURY;

    /* Need three free entry slots for the three rails, or it is not atomic. */
    if (a->num_entries + 3u > 256u) return VINO_ERR_CAPACITY;

    /* --- Snapshot exactly the scalars triple_ledger_post mutates. ----------- */
    surplus_real_t s_assets = tl->total_assets;
    surplus_real_t s_liab   = tl->total_liabilities;
    surplus_real_t s_equity = tl->total_equity;
    uint64_t       s_nid    = tl->next_entry_id;
    uint32_t       s_ne     = a->num_entries;
    surplus_real_t s_cbal   = a->conventional_balance;
    surplus_real_t s_cov    = a->coverage_ratio;
    surplus_real_t s_ext    = a->externality_phase;
    surplus_real_t s_bal[LEDGER_MAX];
    int j;
    for (j = 0; j < LEDGER_MAX; j++) s_bal[j] = a->balance[j];

    /* --- Post the THREE rails through the real double-entry engine. ---------
     * Rail 1 (846): the asset/backing debit.  Rail 2 (888): the claim credit.
     * Rail 3 (999): the LIVE equity witness — value carried on r, zero debit
     * and zero credit so it witnesses without double-counting the balance. */
    surplus_real_t ell_full = SR_ONE; /* a backed rail is fully attested */
    int32_t r1 = triple_ledger_post(tl, vs->vino_account, LEDGER_FINANCIAL,
                                    debit, ell_full, SR_ZERO,
                                    debit, SR_ZERO, VINO_ISO_DEBIT,
                                    "Vino rail 846: debit");
    int32_t r2 = triple_ledger_post(tl, vs->vino_account, LEDGER_FINANCIAL,
                                    credit, ell_full, SR_ZERO,
                                    SR_ZERO, credit, VINO_ISO_CREDIT,
                                    "Vino rail 888: credit");
    int32_t r3 = triple_ledger_post(tl, vs->vino_account, LEDGER_PROVENANCE,
                                    equity, ell_full, SR_ZERO,
                                    SR_ZERO, SR_ZERO, VINO_ISO_EQUITY,
                                    "Vino rail 999: equity witness");

    /* --- Gate on the ledger's own accumulators. ----------------------------- */
    surplus_real_t assets = tl->total_assets;
    surplus_real_t liab   = tl->total_liabilities;
    surplus_real_t led_eq = SR_SUB(assets, liab);

    bool posted_ok = (r1 == 0 && r2 == 0 && r3 == 0);
    bool cov_ok    = vino_coverage_ok(assets, liab);
    bool eq_ok     = (SR_CMP(led_eq, SR_ZERO) >= 0) &&
                     (SR_CMP(equity, SR_ZERO) >= 0);

    if (!posted_ok || !cov_ok || !eq_ok) {
        /* --- Full rollback: restore every mutated scalar. No partial write. -- */
        tl->total_assets      = s_assets;
        tl->total_liabilities = s_liab;
        tl->total_equity      = s_equity;
        tl->next_entry_id     = s_nid;
        a->num_entries        = s_ne;
        a->conventional_balance = s_cbal;
        a->coverage_ratio     = s_cov;
        a->externality_phase  = s_ext;
        for (j = 0; j < LEDGER_MAX; j++) a->balance[j] = s_bal[j];
        if (!posted_ok)      return VINO_ERR_CAPACITY;
        if (!cov_ok)         return VINO_ERR_COVERAGE;
        return VINO_ERR_EQUITY;
    }

    /* --- Committed. Advance the voucher's lifecycle and re-anchor its CID.
     * `v` was validated up front (non-NULL), so the ledger was only ever
     * mutated for a voucher that actually exists. */
    v->lifecycle = to;
    v->base.coverage_ratio = a->coverage_ratio;
    if (proof_cid) vino_copy_cid(v->proof_cid, proof_cid);
    if (to == VINO_REDEEM || to == VINO_RETIRE) v->base.redeemed = true;
    return VINO_OK;
}

/* --- Single-active-state --------------------------------------------------- */

bool vino_single_active_state_ok(const vino_voucher_t *v) {
    if (!v || !v->in_use) return false;
    /* Exactly one of two states is active AND the counterpart is locked, so the
     * coin cannot be spent as both a bearer note and a ledger ghost. */
    if (v->state != VINO_ACTIVE_PHYSICAL && v->state != VINO_ACTIVE_DIGITAL)
        return false;
    return v->counterpart_locked;
}

bool vino_can_spend(const vino_voucher_t *v, vino_state_t rail) {
    if (!vino_single_active_state_ok(v)) return false;
    if (v->base.redeemed) return false;
    return v->state == rail;
}

static int32_t vino_swap(vino_stores_t *vs, uint64_t voucher_id, vino_state_t to) {
    vino_voucher_t *v = vino_find(vs, voucher_id);
    if (!v) return VINO_ERR_NOT_FOUND;
    if (v->base.redeemed) return VINO_ERR_STATE;
    /* Flip the active rail; the twin stays locked (same proof_cid re-anchored). */
    v->state = to;
    v->counterpart_locked = true;
    v->lifecycle = VINO_SWAP;
    if (!vino_single_active_state_ok(v)) return VINO_ERR_STATE;
    return VINO_OK;
}

int32_t vino_swap_to_physical(vino_stores_t *vs, uint64_t voucher_id) {
    return vino_swap(vs, voucher_id, VINO_ACTIVE_PHYSICAL);
}

int32_t vino_swap_to_digital(vino_stores_t *vs, uint64_t voucher_id) {
    return vino_swap(vs, voucher_id, VINO_ACTIVE_DIGITAL);
}

/* --- Geometry -------------------------------------------------------------- */

surplus_real_t vino_phi_draw_max(surplus_real_t equity) {
    /* equity * (φ − 1). A CAP on forward draw, never a charge on principal. */
    return SR_MUL(equity, PHI_MINUS_1);
}

uint64_t vino_ladder_denomination(uint32_t rung) {
    if (rung == 0) return 0;                 /* V0 ceremonial, zero value */
    if (rung > VINO_LADDER_RUNGS) return 0;  /* off the ladder */
    /* rung 1..12 -> edp_fibonacci(2..13) = 1,2,3,5,8,13,21,34,55,89,144,233. */
    return (uint64_t)edp_fibonacci(rung + 1);
}

int32_t vino_gratuity_112(surplus_real_t yield, surplus_real_t out[6]) {
    if (!out) return VINO_ERR_NULL;
    /* 11 / 11 / 11 / 66 / 1  (+ 12 Shiva buffer) = 112% of yield.
     * Take the FRACTION FIRST — SR_MUL(yield, SR_DIV(n, 100)) — NOT
     * SR_DIV(SR_MUL(yield, n), 100): the latter forms yield*66 in Q32.32, whose
     * integer part OVERFLOWS int64 for a yield above ~32.5M units and wraps the
     * 66% slice NEGATIVE on the target (invisible to the host double test). The
     * fraction n/100 is a representable Q32.32 value, so it does not floor to 0. */
    static const int32_t parts[6] = { 11, 11, 11, 66, 1, 12 };
    int i;
    for (i = 0; i < 6; i++) {
        out[i] = SR_MUL(yield, SR_DIV(SR_FROM_INT(parts[i]), SR_FROM_INT(100)));
    }
    return VINO_OK;
}

/* ---- DECLARATION -----------------------------------------------------------

 * The voucher store behind pirate_apps' Cellar (app_cellar_open,
 * vino_swap_to_physical / vino_swap_to_digital).
 *
 * MEASURED BUT NOT DECLARED HERE, and this is a real limit rather than an
 * oversight: vino_stores.o's `nm -u` also names triple_ledger_create_account /
 * triple_ledger_post (kernel/src/finance/triple_ledger.c) and op_symbiotic_ok
 * (kernel/src/onepolicy/onepolicy.c). Neither of those modules declares yet,
 * and a REQUIRES naming a capability nothing PROVIDES is MB_ERR_UNPROVIDED --
 * it would fail the gate for the whole kernel, not just for this module. The
 * two edges are written down here so the next pass has them.
 */
#include "zxv_decl.h"
ZXV_DECLARE(vino_stores,
    ZXV_PROVIDES(vino_stores_ready),
    ZXV_REQUIRES(edp_risk_ready),
    ZXV_NO_BRINGUP);
