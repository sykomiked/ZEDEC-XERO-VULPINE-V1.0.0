/* pterm_mux.c — Phase-tick synchronized master/sub terminals.
 * See pterm_mux.h for the ISF-derived dispatch model.
 */
#include "pterm_mux.h"

static void pmux_zero(pmux_sub_t *s) {
    for (uint32_t i = 0; i < PMUX_NAME_LEN; i++) s->name[i] = 0;
    s->state = PMUX_SLOT_FREE;
    s->console = 0;
    s->u_q16 = 0;
    s->weight_q16 = 0;
    s->credit_q16 = 0;
    s->dispatches = 0;
    s->last_tick = 0;
}

void pmux_init(pmux_t *m) {
    if (!m) return;
    for (uint32_t i = 0; i < PMUX_MAX_SUBS; i++) pmux_zero(&m->sub[i]);
    m->num_subs = 0;
    m->num_active = 0;
    m->focus = 0;
    m->ticks = 0;
    m->total_dispatch = 0;
    m->initialized = true;
}

/* ISF weight: g(u) = 1 + (N-1)u, evaluated in Q16.16.
 *
 * N is the number of ACTIVE subs — the "blocks" of the ISF setup. With
 * N=1 every u collapses to g=1 (a lone terminal contributes exactly one
 * terminal's worth, as the axioms require: g(0)=1). With u=1 (fully
 * orthogonal) g=N, matching axiom S4's f(1)=ln N. */
void pmux_recompute_weights(pmux_t *m) {
    if (!m) return;
    uint32_t n = 0;
    for (uint32_t i = 0; i < PMUX_MAX_SUBS; i++)
        if (m->sub[i].state == PMUX_SLOT_ACTIVE) n++;
    m->num_active = n;

    uint32_t nm1 = (n > 1) ? (n - 1) : 0;
    for (uint32_t i = 0; i < PMUX_MAX_SUBS; i++) {
        pmux_sub_t *s = &m->sub[i];
        if (s->state != PMUX_SLOT_ACTIVE) { s->weight_q16 = 0; continue; }
        /* 1 + (N-1)*u   in Q16.16 */
        uint64_t term = (uint64_t)nm1 * (uint64_t)s->u_q16;
        uint64_t w = (uint64_t)PMUX_ONE + term;
        if (w > 0xFFFFFFFFull) w = 0xFFFFFFFFull;
        s->weight_q16 = (uint32_t)w;
    }
}

int32_t pmux_spawn(pmux_t *m, const char *name, uint32_t console,
                   uint32_t u_pct) {
    if (!m || !m->initialized) return -1;
    if (u_pct > 100) u_pct = 100;

    for (uint32_t i = 0; i < PMUX_MAX_SUBS; i++) {
        pmux_sub_t *s = &m->sub[i];
        if (s->state != PMUX_SLOT_FREE) continue;
        pmux_zero(s);
        uint32_t k = 0;
        if (name) {
            while (k < PMUX_NAME_LEN - 1 && name[k]) { s->name[k] = name[k]; k++; }
        }
        s->name[k] = '\0';
        s->state = PMUX_SLOT_ACTIVE;
        s->console = console;
        s->u_q16 = PMUX_FROM_PCT(u_pct);
        m->num_subs++;
        pmux_recompute_weights(m);
        return (int32_t)i;
    }
    return -1;   /* full */
}

bool pmux_close(pmux_t *m, uint32_t idx) {
    if (!m || idx >= PMUX_MAX_SUBS) return false;
    if (m->sub[idx].state == PMUX_SLOT_FREE) return false;
    pmux_zero(&m->sub[idx]);
    if (m->num_subs) m->num_subs--;
    if (m->focus == idx) m->focus = 0;
    pmux_recompute_weights(m);
    return true;
}

bool pmux_set_state(pmux_t *m, uint32_t idx, pmux_state_t st) {
    if (!m || idx >= PMUX_MAX_SUBS) return false;
    if (m->sub[idx].state == PMUX_SLOT_FREE) return false;
    m->sub[idx].state = st;
    pmux_recompute_weights(m);
    return true;
}

bool pmux_set_u(pmux_t *m, uint32_t idx, uint32_t u_pct) {
    if (!m || idx >= PMUX_MAX_SUBS) return false;
    if (m->sub[idx].state == PMUX_SLOT_FREE) return false;
    if (u_pct > 100) u_pct = 100;
    m->sub[idx].u_q16 = PMUX_FROM_PCT(u_pct);
    pmux_recompute_weights(m);
    return true;
}

bool pmux_set_focus(pmux_t *m, uint32_t idx) {
    if (!m || idx >= PMUX_MAX_SUBS) return false;
    if (m->sub[idx].state == PMUX_SLOT_FREE) return false;
    m->focus = idx;
    return true;
}

/* One phase tick of the rotation.
 *
 * Every active sub accrues credit equal to its ISF weight. The first
 * sub whose credit reaches a full share (PMUX_ONE) is dispatched and
 * pays that share back. Scanning starts after the previously dispatched
 * slot so equal-weight subs alternate fairly instead of starving.
 *
 * No wall clock is consulted: the caller supplies causality by calling
 * this once per kernel event cycle. */
int32_t pmux_phase_tick(pmux_t *m) {
    if (!m || !m->initialized) return -1;
    m->ticks++;
    if (m->num_active == 0) return -1;

    /* Exactly ONE share of dispatch credit exists per phase tick, split
     * between the active subs in proportion to their ISF weight:
     *
     *     credit_i += g_i / SUM(g)
     *
     * Normalizing by the total weight is what makes the long-run
     * dispatch ratio equal the ratio of the g(u) values. (Accruing the
     * raw weight instead lets credit grow without bound, after which
     * scan order — not surplus — decides who runs.) */
    uint64_t total_w = 0;
    for (uint32_t i = 0; i < PMUX_MAX_SUBS; i++)
        if (m->sub[i].state == PMUX_SLOT_ACTIVE)
            total_w += m->sub[i].weight_q16;
    if (total_w == 0) return -1;

    for (uint32_t i = 0; i < PMUX_MAX_SUBS; i++) {
        pmux_sub_t *s = &m->sub[i];
        if (s->state != PMUX_SLOT_ACTIVE) continue;
        /* share = weight/total, in Q16.16 */
        uint64_t share = ((uint64_t)s->weight_q16 << 16) / total_w;
        uint64_t c = (uint64_t)s->credit_q16 + share;
        s->credit_q16 = (c > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)c;
    }

    /* round-robin scan start, so ties rotate */
    uint32_t start = (uint32_t)(m->total_dispatch % PMUX_MAX_SUBS);
    for (uint32_t k = 0; k < PMUX_MAX_SUBS; k++) {
        uint32_t i = (start + k) % PMUX_MAX_SUBS;
        pmux_sub_t *s = &m->sub[i];
        if (s->state != PMUX_SLOT_ACTIVE) continue;
        if (s->credit_q16 >= PMUX_ONE) {
            s->credit_q16 -= PMUX_ONE;
            s->dispatches++;
            s->last_tick = m->ticks;
            m->total_dispatch++;
            return (int32_t)i;
        }
    }
    return -1;
}
