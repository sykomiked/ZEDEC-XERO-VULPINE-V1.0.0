/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* lex_rhodia.c — general average, salvage, sovereign immunity, safe passage.
 *
 * All numeric work goes through the SR_ macros: multiply-then-divide so the
 * Q32.32 target never floors a proportion to zero, and residue-to-the-largest
 * so conservation is EXACT rather than "within epsilon".
 */

#include "lex_rhodia.h"
#include "ipfs.h"   /* ipfs_cid_from_bytes — your hash is your key */

/* ---- tiny local byte helpers (house style: no libc mem*) ---- */
static bool eq32(const uint8_t *a, const uint8_t *b) {
    for (uint32_t i = 0; i < ZXV_HASH_LEN; i++) if (a[i] != b[i]) return false;
    return true;
}
static void copy_n(uint8_t *d, const uint8_t *s, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) d[i] = s ? s[i] : 0;
}

/* ===== GENERAL AVERAGE ===== */

zxv_status_t lex_rhodia_general_average(surplus_real_t sacrifice,
                                        const ga_party_t *parties, uint32_t n,
                                        surplus_real_t *contrib_out) {
    if (!parties || !contrib_out || n == 0) return ZXV_EDEGEN;

    /* total stake + index of the largest stake (residue lands here) */
    surplus_real_t total = SR_ZERO;
    uint32_t big = 0;
    for (uint32_t i = 0; i < n; i++) {
        total = SR_ADD(total, parties[i].stake);
        if (SR_CMP(parties[i].stake, parties[big].stake) > 0) big = i;
    }
    if (SR_CMP(total, SR_ZERO) <= 0) return ZXV_EDEGEN;   /* never divide by zero */

    /* contrib_i = sacrifice * stake_i / total — MULTIPLY first (target-safe) */
    surplus_real_t sum = SR_ZERO;
    for (uint32_t i = 0; i < n; i++) {
        surplus_real_t c = SR_DIV(SR_MUL(sacrifice, parties[i].stake), total);
        contrib_out[i] = c;
        sum = SR_ADD(sum, c);
    }

    /* Assign the whole truncation residue to the largest-stake party so that
     * sum(contrib) == sacrifice EXACTLY. On the double host the residue is ~0;
     * on the Q32.32 target it soaks up the flooring so conservation is exact. */
    surplus_real_t residue = SR_SUB(sacrifice, sum);
    contrib_out[big] = SR_ADD(contrib_out[big], residue);
    return ZXV_OK;
}

/* ===== SALVAGE ===== */

zxv_status_t lex_rhodia_salvage_award(surplus_real_t salved, surplus_real_t risk,
                                      surplus_real_t *award_out) {
    if (!award_out) return ZXV_EDEGEN;
    if (SR_CMP(salved, SR_ZERO) <= 0) return ZXV_EDEGEN;

    /* BASE = 0.10, RISK_BONUS = 0.40 as fixed-point constants (ratios of ints,
     * never open-coded (x*n)/d fractions). */
    const surplus_real_t BASE       = SR_DIV(SR_FROM_INT(1), SR_FROM_INT(10));
    const surplus_real_t RISK_BONUS = SR_DIV(SR_FROM_INT(2), SR_FROM_INT(5));

    surplus_real_t rate = SR_ADD(BASE, SR_MUL(RISK_BONUS, risk));
    if (SR_CMP(rate, SR_ZERO) < 0) rate = SR_ZERO;   /* clamp low  */
    if (SR_CMP(rate, SR_ONE)  > 0) rate = SR_ONE;    /* clamp high */

    *award_out = SR_MUL(salved, rate);               /* 0 < award <= salved */
    return ZXV_OK;
}

/* ===== SOVEREIGN IMMUNITY ===== */

void vessel_flag_raise(vessel_flag_t *v, const void *content, uint32_t len,
                       zxv_node_id_t keyholder, const uint8_t sig[64]) {
    if (!v) return;
    ipfs_cid_from_bytes((const uint8_t *)content, len, v->flag_hash);
    for (uint32_t i = 0; i < 64; i++) v->sovereign_sig[i] = sig ? sig[i] : 0;
    v->keyholder = keyholder;
}

bool vessel_flag_verify(const vessel_flag_t *v, const void *content, uint32_t len) {
    if (!v) return false;
    uint8_t recomputed[ZXV_HASH_LEN];
    ipfs_cid_from_bytes((const uint8_t *)content, len, recomputed);
    /* A rename mutates the content, so the recomputed hash diverges. There is no
     * trusted "immune = true" byte anywhere in this decision. */
    return eq32(recomputed, v->flag_hash);
}

bool vessel_immune_from(const vessel_flag_t *v, zxv_node_id_t actor) {
    if (!v) return false;
    /* Immunity vests in the keyholder alone; ZXV_NODE_NONE holds nothing. */
    return actor != ZXV_NODE_NONE && actor == v->keyholder;
}

/* ===== SAFE PASSAGE ===== */

void safe_passage_request(safe_passage_grant_t *g, const vessel_flag_t *vessel,
                          zxv_node_id_t through, uint64_t issued_tick,
                          uint64_t ttl) {
    if (!g) return;
    if (vessel) {
        copy_n(g->vessel.flag_hash, vessel->flag_hash, ZXV_HASH_LEN);
        copy_n(g->vessel.sovereign_sig, vessel->sovereign_sig, 64);
        g->vessel.keyholder = vessel->keyholder;
    } else {
        copy_n(g->vessel.flag_hash, 0, ZXV_HASH_LEN);
        copy_n(g->vessel.sovereign_sig, 0, 64);
        g->vessel.keyholder = ZXV_NODE_NONE;
    }
    g->through     = through;
    g->issued_tick = issued_tick;
    /* Saturate rather than wrap: a colossal ttl must never land the deadline in
     * the past. */
    g->ttl_deadline = (ttl > UINT64_MAX - issued_tick) ? UINT64_MAX
                                                       : issued_tick + ttl;
}

bool safe_passage_valid(const safe_passage_grant_t *g, uint64_t now_tick) {
    if (!g) return false;
    /* Non-retroactive: not before issue, valid up to AND including the deadline. */
    return now_tick >= g->issued_tick && now_tick <= g->ttl_deadline;
}
