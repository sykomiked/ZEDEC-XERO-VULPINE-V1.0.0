/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* murmuration.h — Murmuration's native screen (zv.h): the swarm's agents
 * with their share of the cycle and who witnessed it, the selected agent's
 * face (zui_emote.h), the busiest agents, the model and the swarm's earnings.
 * One app in three languages: this screen is C, the cycle's numbers KRON,
 * the limits they are checked against Eve, both from the swarm_shares
 * block the app uses (apps/murmuration.zxapp, apps/blocks/swarm_shares.zxblock).
 *
 *   U1 KRON LOGIC. The numbers about the cycle (each agent's share in per
 *      mille, the busiest three) are the KRON entries swarm_shares.cycle_share
 *      and swarm_shares.busiest. When the app has no KRON runtime,
 *      or the snapshot is truncated (the entries only see the kept agents),
 *      the view uses the model's C (murm_cycle_share) instead; both give
 *      the same exact numbers, and the status line says which ran (U5).
 *   U4 EVE LIMITS. swarm_shares.within_limits (Eve, limits.eve) checks each
 *      agent's share and intensity; an agent outside them is marked, never
 *      changed. Without an Eve runtime the view checks the same limits in
 *      C (MURM_VIEW_SHARE_CAP).
 *   U5 WITNESSED. The C model always computes the same numbers, so it is a
 *      second witness to KRON and Eve (zxapp.h A5): agreeing is TRUE and
 *      the status says so; disagreeing is BOTH, and the view holds the
 *      numbers ("held") and says why instead of picking one.
 *   U2 THE FACE is the agent's feeling, or the swarm's mood with nothing
 *      selected. A FALSE witness or a halted ledger shows as the FAILING
 *      ring, never as a sad face: feeling and health stay apart.
 *   U3 One gold element: Refresh. */
#ifndef ZXV_VIEW_MURMURATION_H
#define ZXV_VIEW_MURMURATION_H

#include "murmuration_model.h"
#include "zv.h"
#include "zxapp.h"

#define MURM_VIEW_BUSIEST   3u
#define MURM_VIEW_SHARE_CAP 500u /* per mille; limits.eve's agent_limits.share */

typedef struct {
    zv_shell_t sh;
    murmuration_t *m;
    zui_id_t list_pane, agents, detail, back, face, who, feeling, allot, witness;
    zui_id_t limits, busiest, model, earnings, refresh;
    bool show_detail;
    zxapp_t *app; /* NULL: C only (U1) */
    zxapp_host_t *host;
    uint32_t share[MURM_AGENTS]; /* per mille, the last sync's */
    int32_t top[MURM_VIEW_BUSIEST];
    uint32_t ntop;
    bool over[MURM_AGENTS]; /* outside the limits (U4) */
    bool kron;              /* the last sync's numbers came from KRON */
    bool eve;               /* and its limits from Eve */
    int32_t shares_lp;      /* U5: ZXAPP_TRUE (KRON = C), BOTH (held), NEITHER (C only) */
    int32_t limits_lp;      /* U5: the same for Eve's limits */
    bool said_held;         /* the error line is our "Held:" */
} murm_view_t;

void murm_view_build(murm_view_t *v, zui_ui_t *ui, murmuration_t *m);
/* U1: bind the app's manifest and the host its runtimes live in. */
void murm_view_logic(murm_view_t *v, zxapp_t *app, zxapp_host_t *host);
void murm_view_sync(murm_view_t *v);
bool murm_view_action(murm_view_t *v, zui_id_t w, zui_action_t a, int32_t arg);

#endif /* ZXV_VIEW_MURMURATION_H */
