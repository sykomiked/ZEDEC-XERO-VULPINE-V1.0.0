/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* murmuration.h — Murmuration's native screen (zv.h): the swarm's agents
 * with their share of the cycle and who witnessed it, the selected agent's
 * face (zui_emote.h), the busiest agents, the model and the swarm's earnings.
 *
 *   U1 KRON LOGIC. The numbers about the cycle (each agent's share in per
 *      mille, the busiest three) are the app's KRON entries cycle_share and
 *      busiest (apps/murmuration.zxapp). When the app has no KRON runtime,
 *      or the snapshot is truncated (the entries only see the kept agents),
 *      the view uses the model's C (murm_cycle_share) instead; both give
 *      the same exact numbers, and the status line says which ran.
 *   U2 THE FACE is the agent's feeling, or the swarm's mood with nothing
 *      selected. A FALSE witness or a halted ledger shows as the FAILING
 *      ring, never as a sad face: feeling and health stay apart.
 *   U3 One gold element: Refresh. */
#ifndef ZXV_VIEW_MURMURATION_H
#define ZXV_VIEW_MURMURATION_H

#include "murmuration_model.h"
#include "zv.h"
#include "zxapp.h"

#define MURM_VIEW_BUSIEST 3u

typedef struct {
    zv_shell_t sh;
    murmuration_t *m;
    zui_id_t list_pane, agents, detail, back, face, who, feeling, allot, witness;
    zui_id_t busiest, model, earnings, refresh;
    bool show_detail;
    zxapp_t *app; /* NULL: C only (U1) */
    zxapp_host_t *host;
    uint32_t share[MURM_AGENTS]; /* per mille, the last sync's */
    int32_t top[MURM_VIEW_BUSIEST];
    uint32_t ntop;
    bool kron; /* the last sync's numbers came from KRON */
} murm_view_t;

void murm_view_build(murm_view_t *v, zui_ui_t *ui, murmuration_t *m);
/* U1: bind the app's manifest and the host its runtimes live in. */
void murm_view_logic(murm_view_t *v, zxapp_t *app, zxapp_host_t *host);
void murm_view_sync(murm_view_t *v);
bool murm_view_action(murm_view_t *v, zui_id_t w, zui_action_t a, int32_t arg);

#endif /* ZXV_VIEW_MURMURATION_H */
