/* wyverneye.c — intent forged, then judged. See wyverneye.h.
 * Pure composition: ref_forge (Refinery) then wyrm_judge (Wyrmgate), with the
 * forged sigil's own direction counted as the first piece of evidence. */
#include "wyverneye.h"

bool wyvern_read(const char *intent, uint32_t len, eno_voice_t voice,
                 const wyvern_context_t *ctx, wyvern_reading_t *out) {
    if (!intent || !ctx || !out) return false;

    /* 1. Speech is free: forge the sigil from the intent. This never gates. */
    if (ref_forge(intent, len, voice, &out->card) != REF_OK) return false;

    /* 2. Assemble the action for judgment. The sigil the speaker forged is
     *    ONE evidence direction; independent corroboration from the world
     *    fills the rest. A lone sigil is therefore R=1 — not enough. */
    wyrm_event_t ev;
    ev.ordinal = ctx->ordinal;
    ev.parent_ordinal = ctx->parent_ordinal;
    ev.parent_committed = ctx->parent_committed;

    uint32_t nd = (ctx->n_deltas <= WYRM_MAX_DELTAS) ? ctx->n_deltas : WYRM_MAX_DELTAS;
    for (uint32_t i = 0; i < nd; i++) ev.delta[i] = ctx->delta[i];
    ev.n_deltas = nd;

    ev.evidence = ctx->evidence;

    /* evidence direction 0 = the forged sigil itself */
    for (uint32_t d = 0; d < CHG_DIM; d++) ev.ev[0][d] = out->card.fx.evidence[d];
    uint32_t nc = (ctx->n_corroboration <= WYV_MAX_CORROB)
                      ? ctx->n_corroboration : WYV_MAX_CORROB;
    for (uint32_t k = 0; k < nc; k++)
        for (uint32_t d = 0; d < CHG_DIM; d++)
            ev.ev[k + 1][d] = ctx->corroboration[k][d];
    ev.n_ev = nc + 1u;
    ev.r_min = ctx->r_min;

    ev.route_available = ctx->route_available;
    ev.phases_healthy = ctx->phases_healthy;

    /* 3. Judge whether the intent may act. */
    out->verdict = wyrm_judge(&ev);
    out->corroborators = nc;
    return true;
}
