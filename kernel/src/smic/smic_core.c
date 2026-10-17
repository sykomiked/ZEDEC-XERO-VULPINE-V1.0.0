/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
#include "smic_core.h"

static int id_eq(const uint8_t a[32], const uint8_t b[32])
{
    uint32_t i;
    for (i = 0; i < 32; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static smic_node_t *find_mut(smic_t *r, const uint8_t node_id[32])
{
    uint32_t i;
    for (i = 0; i < r->cap; i++)
        if (r->ents[i].used && id_eq(r->ents[i].node_id, node_id)) return &r->ents[i];
    return 0;
}

/* tag <- admission base + evidence polarity; action = the table's word.
 * Quarantine (BOTH) reads as ESCALATE; forbidden as ROLLBACK. */
static lpx_action_t verdict(smic_node_t *n, uint64_t now)
{
    uint8_t base, pol;
    if (lpx_forbidden(n->health)) return LPX_ACT_ROLLBACK;
    base = lpx_base_from_lpres(n->admitted);
    pol = lpx_polarity(0, 0, n->pos_ev, n->neg_ev, 0);
    n->health = lpx_set(n->health, base, pol);
    n->quarantined = (base == LPX_B_BOTH);
    n->last_seen = now;
    return lpx_action(n->health);
}

void smic_init(smic_t *r, smic_node_t *ents, uint32_t cap)
{
    uint32_t i;
    if (!r || !ents || !cap) return;
    r->ents = ents;
    r->cap = cap;
    r->n = 0;
    for (i = 0; i < cap; i++) {
        smic_node_t *n = &ents[i];
        uint8_t *p = (uint8_t *) n;
        uint32_t j;
        for (j = 0; j < sizeof *n; j++) p[j] = 0;
        n->admitted = lpres_kjoin(n->admitted, LPRES_STATE_TRUE);
        n->health = lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    }
}

lpx_action_t smic_admit(smic_t *r, const smic_hw_t *hw, uint64_t now)
{
    smic_node_t *n;
    uint32_t i;

    if (!r || !r->ents || !hw || hw->region >= SMIC_REGIONS ||
        hw->arch == SMIC_ARCH_NONE || !hw->caps)
        return LPX_ACT_REFUSE;

    n = find_mut(r, hw->node_id);
    if (!n) {
        if (r->n >= r->cap) return LPX_ACT_REFUSE;
        for (i = 0; i < r->cap; i++)
            if (!r->ents[i].used) break;
        n = &r->ents[i];
        n->used = 1;
        r->n++;
    }
    n->quarantined = 0;
    for (i = 0; i < 32; i++) n->node_id[i] = hw->node_id[i];
    n->region = hw->region;
    n->role = hw->role;
    n->octane = hw->octane;
    n->arch = hw->arch;
    n->flags = hw->flags;
    n->caps = hw->caps;
    n->disk_mb = hw->disk_mb;
    n->provisioned_at = hw->provisioned_at;
    n->net_mbps = hw->net_mbps;
    n->fw_crc = hw->fw_crc;

    n->pos_ev++;
    /* an isolated node stays isolated until an operator clears it —
     * a fresh attestation is not a pardon */
    if (!lpx_forbidden(n->health) && n->admitted != LPRES_STATE_FALSE)
        n->admitted = lpres_kjoin(n->admitted, LPRES_STATE_TRUE);
    return verdict(n, now);
}

lpx_action_t smic_health(smic_t *r, const uint8_t node_id[32], uint8_t ok, uint64_t now)
{
    smic_node_t *n;
    if (!r || !r->ents || !node_id) return LPX_ACT_REFUSE;
    n = find_mut(r, node_id);
    if (!n) return LPX_ACT_WAIT;
    if (ok) n->pos_ev++; else n->neg_ev++;
    if (!lpx_forbidden(n->health))
        n->admitted = lpres_kjoin(n->admitted, ok ? LPRES_STATE_TRUE : LPRES_STATE_FALSE);
    return verdict(n, now);
}

lpx_action_t smic_forbid(smic_t *r, const uint8_t node_id[32])
{
    smic_node_t *n;
    if (!r || !r->ents || !node_id) return LPX_ACT_REFUSE;
    n = find_mut(r, node_id);
    if (!n) return LPX_ACT_WAIT;
    n->admitted = LPRES_STATE_FALSE;
    n->quarantined = 1;
    n->health = lpx_forbid(n->health);
    return LPX_ACT_ROLLBACK;
}

lpx_action_t smic_clear(smic_t *r, const uint8_t node_id[32])
{
    smic_node_t *n;
    if (!r || !r->ents || !node_id) return LPX_ACT_REFUSE;
    n = find_mut(r, node_id);
    if (!n) return LPX_ACT_WAIT;
    n->admitted = LPRES_STATE_NEITHER;
    n->quarantined = 0;
    n->pos_ev = n->neg_ev = 0;
    n->health = lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    return LPX_ACT_WAIT;
}

uint32_t smic_reap(smic_t *r, uint64_t now, uint64_t stale_ms)
{
    uint32_t i, touched = 0;
    if (!r || !r->ents) return 0;
    for (i = 0; i < r->cap; i++) {
        smic_node_t *n = &r->ents[i];
        if (!n->used || n->last_seen >= now || now - n->last_seen < stale_ms) continue;
        if (lpx_pol(n->health) != LPX_P_FORBIDDEN)
            n->health = lpx_set(n->health, lpx_base(n->health), LPX_P_ENTROPIC);
        touched++;
    }
    return touched;
}

const smic_node_t *smic_find(const smic_t *r, const uint8_t node_id[32])
{
    uint32_t i;
    if (!r || !r->ents || !node_id) return 0;
    for (i = 0; i < r->cap; i++)
        if (r->ents[i].used && id_eq(r->ents[i].node_id, node_id)) return &r->ents[i];
    return 0;
}

void smic_ring(const smic_t *r, uint8_t out[SMIC_REGIONS], uint32_t *total)
{
    uint32_t i, t = 0;
    if (out)
        for (i = 0; i < SMIC_REGIONS; i++) out[i] = 0;
    if (!r || !r->ents) {
        if (total) *total = 0;
        return;
    }
    for (i = 0; i < r->cap; i++) {
        const smic_node_t *n = &r->ents[i];
        if (!n->used || n->admitted != LPRES_STATE_TRUE || n->quarantined) continue;
        t++;
        if (out && n->region < SMIC_REGIONS) out[n->region]++;
    }
    if (total) *total = t;
}
