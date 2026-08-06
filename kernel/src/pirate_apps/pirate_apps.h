/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* pirate_apps.h — the whole pirate deck of core apps.
 *
 * "Every window a porthole onto a capability that is real or honestly dark."
 *
 * The Hold, the Counter, the Cellar, the Spyglass, the Club. Each app is a
 * THIN, HONEST UI over a capability module — a workshop/desktop surface that
 * surfaces the module's real state AND its NOT-AVAILABLE state. That is the
 * whole discipline here:
 *
 *   - AN APP HOLDS NO CAPABILITY OF ITS OWN. There is no settlement, no crypto,
 *     no ledger, no content store in this module. Grep pirate_apps.c for
 *     sha256 / ed25519 / vino_ledger_act / a hand-rolled fill — nothing. Every
 *     side-effect DELEGATES to the bound module and returns its result verbatim.
 *
 *   - AN APP NEVER FABRICATES A SUCCESS THE MODULE DID NOT RETURN. When the
 *     module's ops boundary fails closed (an ipfs node with no transport, a
 *     broker with no settlement backend), the app reports UNAVAILABLE and the
 *     delegate returns the module's exact NOT-AVAILABLE code — not invented
 *     content, not a painted "ok".
 *
 * OPS BOUNDARY: drawing to a real framebuffer is NOT this module's job. An app
 * produces a LOGICAL VIEW (a small text render buffer) + delegate actions; the
 * display subsystem draws that view. The capability is always the module's.
 *
 * Freestanding: integer only, fixed-size buffers, no libc, no allocation, no
 * floating point.
 */
#ifndef ZXV_PIRATE_APPS_H
#define ZXV_PIRATE_APPS_H

#include <stdint.h>
#include <stdbool.h>

#include "ipfs.h"             /* the Hold      */
#include "broker.h"           /* the Counter   */
#include "vino_stores.h"      /* the Cellar    */
#include "finance_markets.h"  /* the Spyglass  */
#include "../social_spaces/social.h" /* the Club */

#define APP_RENDER_CAP 256u   /* the logical view buffer (ops boundary: not pixels) */

/* Which porthole this handle is bound to. Exactly one module pointer is set. */
typedef enum {
    APP_NONE = 0,
    APP_HOLD,       /* the Hold     — browse/get CIDs (ipfs)             */
    APP_COUNTER,    /* the Counter  — Mister Shanghai's listings (broker) */
    APP_CELLAR,     /* the Cellar   — voucher wallet + swap (vino_stores) */
    APP_SPYGLASS,   /* the Spyglass — positions + posted quotes (fm)     */
    APP_CLUB        /* the Club     — Don Tovani / Zevion's Hideout (soc) */
} app_kind_t;

/* The porthole's light. AVAILABLE means the bound module can ACTUALLY serve;
 * UNAVAILABLE means it is bound but its ops boundary is dark (fail-closed);
 * UNBOUND means no module is attached at all. Honest, never optimistic. */
typedef enum {
    APP_STATUS_UNBOUND = 0,
    APP_STATUS_UNAVAILABLE,
    APP_STATUS_AVAILABLE
} app_status_t;

/* A bound handle + a small logical render buffer. NO capability lives here:
 * every pointer below is BORROWED, never owned, and every side-effect this
 * struct participates in is delegated back to the pointed-at module. */
typedef struct {
    app_kind_t     kind;
    ipfs_node_t   *hold;       /* set iff kind == APP_HOLD     */
    broker_t      *counter;    /* set iff kind == APP_COUNTER  */
    vino_stores_t *cellar;     /* set iff kind == APP_CELLAR   */
    fm_book_t     *spyglass;   /* set iff kind == APP_SPYGLASS */
    soc_world_t   *club;       /* set iff kind == APP_CLUB     */
    char           render[APP_RENDER_CAP];
    uint32_t       render_len;
} app_ctx_t;

/* ---- the deck: each opens by BINDING a module and rendering its real state ----
 * All return 0 on success, -1 on a NULL argument. Binding NULL as the module is
 * allowed and legitimate: the porthole simply opens DARK (status UNAVAILABLE),
 * which is the whole point — an unbacked window is honest, not hidden. */
int32_t app_hold_open(app_ctx_t *a, ipfs_node_t *node);
int32_t app_counter_open(app_ctx_t *a, broker_t *broker);
int32_t app_cellar_open(app_ctx_t *a, vino_stores_t *stores);
int32_t app_spyglass_open(app_ctx_t *a, fm_book_t *book);
int32_t app_club_open(app_ctx_t *a, soc_world_t *world);

/* The porthole's light, computed from the BOUND module's real ability to serve:
 *   Hold     -> a fetch transport is bound (else fetches would fail closed)
 *   Counter  -> a signature-verify hook is bound (else listing fails closed)
 *   Cellar   -> a triple-ledger is bound (else it cannot post an act)
 *   Spyglass -> the book is initialised
 *   Club     -> at least one space is open (somewhere to actually gather)
 * Never AVAILABLE for a NULL/absent module. */
app_status_t app_status(const app_ctx_t *a);

/* The logical view text (NUL-terminated). This is what a display subsystem
 * would draw — the app itself paints no pixels (ops boundary). */
const char *app_render(const app_ctx_t *a);
uint32_t    app_render_len(const app_ctx_t *a);

/* ================= delegate actions (verbatim pass-through) =================
 * Each returns EXACTLY the bound module's own result code. The app adds no
 * settlement, no crypto, no ledger — it only forwards the call. A wrong-kind or
 * unbound handle fails closed with that module's NOT-AVAILABLE code, never a
 * fabricated success. */

/* THE HOLD: delegate to ipfs_get_verify. No transport => IPFS_ERR_NO_TRANSPORT
 * verbatim; mismatched bytes => IPFS_ERR_CID_MISMATCH; the app invents nothing. */
ipfs_result_t app_hold_fetch(app_ctx_t *a, const uint8_t cid[IPFS_CID_LEN],
                             uint8_t *buf, uint32_t cap, uint32_t *out_len);

/* THE COUNTER: read-through to the broker's listing count / a listing. */
uint32_t                 app_counter_count(const app_ctx_t *a);
const broker_listing_t  *app_counter_listing(const app_ctx_t *a, int32_t idx);
/* Delegate settlement to broker_settle. No settlement backend =>
 * BROKER_ERR_NO_SETTLEMENT verbatim — a sale is never simulated. */
broker_result_t app_counter_settle(app_ctx_t *a, int32_t listing_idx,
                                   const uint8_t buyer[BROKER_KEY_LEN],
                                   uint64_t amount);

/* THE CELLAR: delegate a rail swap to vino_swap_to_physical and return its code
 * verbatim (VINO_OK / VINO_ERR_NOT_FOUND / VINO_ERR_STATE ...). */
int32_t app_cellar_swap(app_ctx_t *a, uint64_t voucher_id);
int32_t app_cellar_swap_digital(app_ctx_t *a, uint64_t voucher_id);

/* THE SPYGLASS: read-through position count + delegate a mark-to-quote PnL
 * report to fm_pnl_report verbatim (FM_NO_PRICE stays an honest absence). */
uint32_t app_spyglass_positions(const app_ctx_t *a);
int32_t  app_spyglass_pnl(const app_ctx_t *a, uint32_t acct, fm_pnl_t *out);

/* THE CLUB: read-through space count + delegate a post to soc_post verbatim. */
uint32_t app_club_spaces(const app_ctx_t *a);
int32_t  app_club_say(app_ctx_t *a, int32_t space, uint32_t person,
                      const uint8_t *msg, uint32_t len);

#endif /* ZXV_PIRATE_APPS_H */
