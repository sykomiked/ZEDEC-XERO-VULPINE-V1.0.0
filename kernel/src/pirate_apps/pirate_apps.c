/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pirate_apps.c — thin, honest portholes over the capability modules.
 *
 * Every function here either (a) reads a module's state to build a logical view
 * string, or (b) forwards a call to a module and returns its result verbatim.
 * There is deliberately NO capability implemented in this file: no settlement,
 * no crypto, no ledger, no content store. If you find one, it is a bug.
 */
#include "pirate_apps.h"

/* ---- a tiny freestanding string builder (no libc, no allocation) ---- */
static void sb_reset(app_ctx_t *a) {
    a->render[0] = '\0';
    a->render_len = 0;
}

static void sb_puts(app_ctx_t *a, const char *s) {
    uint32_t i = a->render_len;
    while (s && *s && i + 1u < APP_RENDER_CAP) {
        a->render[i++] = *s++;
    }
    a->render[i] = '\0';
    a->render_len = i;
}

static void sb_putu(app_ctx_t *a, uint32_t v) {
    char tmp[11];               /* up to 10 digits for uint32 + NUL */
    uint32_t n = 0;
    if (v == 0) { sb_puts(a, "0"); return; }
    while (v > 0 && n < 10u) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    /* reverse into the render buffer, respecting the cap */
    while (n > 0 && a->render_len + 1u < APP_RENDER_CAP) {
        a->render[a->render_len++] = tmp[--n];
    }
    a->render[a->render_len] = '\0';
}

/* The two words a porthole ever says about itself: lit or dark. */
static void sb_lamp(app_ctx_t *a) {
    sb_puts(a, app_status(a) == APP_STATUS_AVAILABLE ? " [LIT]" : " [DARK]");
}

/* ======================= the deck: open = bind + render ==================== */

static void app_clear(app_ctx_t *a) {
    a->kind = APP_NONE;
    a->hold = 0; a->counter = 0; a->cellar = 0; a->spyglass = 0; a->club = 0;
    sb_reset(a);
}

int32_t app_hold_open(app_ctx_t *a, ipfs_node_t *node) {
    if (!a) return -1;
    app_clear(a);
    a->kind = APP_HOLD;
    a->hold = node;
    sb_puts(a, "The Hold: content by its own fingerprint.");
    sb_lamp(a);
    return 0;
}

int32_t app_counter_open(app_ctx_t *a, broker_t *broker) {
    if (!a) return -1;
    app_clear(a);
    a->kind = APP_COUNTER;
    a->counter = broker;
    sb_puts(a, "Mister Shanghai's Counter: ");
    sb_putu(a, app_counter_count(a));
    sb_puts(a, " listing(s).");
    sb_lamp(a);
    return 0;
}

int32_t app_cellar_open(app_ctx_t *a, vino_stores_t *stores) {
    if (!a) return -1;
    app_clear(a);
    a->kind = APP_CELLAR;
    a->cellar = stores;
    sb_puts(a, "The Cellar: ");
    sb_putu(a, stores ? stores->num_vouchers : 0u);
    sb_puts(a, " voucher(s) on the rack.");
    sb_lamp(a);
    return 0;
}

int32_t app_spyglass_open(app_ctx_t *a, fm_book_t *book) {
    if (!a) return -1;
    app_clear(a);
    a->kind = APP_SPYGLASS;
    a->spyglass = book;
    sb_puts(a, "The Spyglass: ");
    sb_putu(a, app_spyglass_positions(a));
    sb_puts(a, " position(s) on the horizon.");
    sb_lamp(a);
    return 0;
}

int32_t app_club_open(app_ctx_t *a, soc_world_t *world) {
    if (!a) return -1;
    app_clear(a);
    a->kind = APP_CLUB;
    a->club = world;
    sb_puts(a, "The Club: ");
    sb_putu(a, app_club_spaces(a));
    sb_puts(a, " room(s) open.");
    sb_lamp(a);
    return 0;
}

/* ======================= status: the module's real light =================== */

app_status_t app_status(const app_ctx_t *a) {
    if (!a) return APP_STATUS_UNBOUND;
    switch (a->kind) {
    case APP_HOLD:
        if (!a->hold) return APP_STATUS_UNBOUND;
        /* serving iff a fetch transport is bound; else fetches fail closed. */
        return a->hold->t ? APP_STATUS_AVAILABLE : APP_STATUS_UNAVAILABLE;
    case APP_COUNTER:
        if (!a->counter) return APP_STATUS_UNBOUND;
        /* serving iff a signature-verify hook is bound; else broker_list fails
         * closed with BROKER_ERR_NO_VERIFY — the House stocks nothing. */
        return a->counter->verify ? APP_STATUS_AVAILABLE : APP_STATUS_UNAVAILABLE;
    case APP_CELLAR:
        if (!a->cellar) return APP_STATUS_UNBOUND;
        /* serving iff a triple-ledger is bound; every act posts THROUGH it. */
        return a->cellar->ledger ? APP_STATUS_AVAILABLE : APP_STATUS_UNAVAILABLE;
    case APP_SPYGLASS:
        if (!a->spyglass) return APP_STATUS_UNBOUND;
        return a->spyglass->inited ? APP_STATUS_AVAILABLE : APP_STATUS_UNAVAILABLE;
    case APP_CLUB:
        if (!a->club) return APP_STATUS_UNBOUND;
        /* serving iff at least one room is open — somewhere to gather. */
        return a->club->n_spaces > 0u ? APP_STATUS_AVAILABLE : APP_STATUS_UNAVAILABLE;
    case APP_NONE:
    default:
        return APP_STATUS_UNBOUND;
    }
}

const char *app_render(const app_ctx_t *a) { return a ? a->render : ""; }
uint32_t    app_render_len(const app_ctx_t *a) { return a ? a->render_len : 0u; }

/* ===================== delegate actions (verbatim only) ==================== */

ipfs_result_t app_hold_fetch(app_ctx_t *a, const uint8_t cid[IPFS_CID_LEN],
                             uint8_t *buf, uint32_t cap, uint32_t *out_len) {
    if (!a || a->kind != APP_HOLD || !a->hold) return IPFS_ERR_NO_TRANSPORT;
    /* forward, and return EXACTLY what the module decided. No fabrication path
     * exists here — if the transport is dark this yields NO_TRANSPORT. */
    return ipfs_get_verify(a->hold, cid, buf, cap, out_len);
}

uint32_t app_counter_count(const app_ctx_t *a) {
    if (!a || a->kind != APP_COUNTER || !a->counter) return 0u;
    return a->counter->n;
}

const broker_listing_t *app_counter_listing(const app_ctx_t *a, int32_t idx) {
    if (!a || a->kind != APP_COUNTER || !a->counter) return 0;
    return broker_get(a->counter, idx);
}

broker_result_t app_counter_settle(app_ctx_t *a, int32_t listing_idx,
                                   const uint8_t buyer[BROKER_KEY_LEN],
                                   uint64_t amount) {
    if (!a || a->kind != APP_COUNTER || !a->counter) return BROKER_ERR_NO_SETTLEMENT;
    return broker_settle(a->counter, listing_idx, buyer, amount);
}

int32_t app_cellar_swap(app_ctx_t *a, uint64_t voucher_id) {
    if (!a || a->kind != APP_CELLAR || !a->cellar) return VINO_ERR_NULL;
    return vino_swap_to_physical(a->cellar, voucher_id);
}

int32_t app_cellar_swap_digital(app_ctx_t *a, uint64_t voucher_id) {
    if (!a || a->kind != APP_CELLAR || !a->cellar) return VINO_ERR_NULL;
    return vino_swap_to_digital(a->cellar, voucher_id);
}

uint32_t app_spyglass_positions(const app_ctx_t *a) {
    if (!a || a->kind != APP_SPYGLASS || !a->spyglass) return 0u;
    return a->spyglass->num_positions;
}

int32_t app_spyglass_pnl(const app_ctx_t *a, uint32_t acct, fm_pnl_t *out) {
    if (!a || a->kind != APP_SPYGLASS || !a->spyglass) return FM_ERR_NULL;
    return fm_pnl_report(a->spyglass, acct, out);
}

uint32_t app_club_spaces(const app_ctx_t *a) {
    if (!a || a->kind != APP_CLUB || !a->club) return 0u;
    return a->club->n_spaces;
}

int32_t app_club_say(app_ctx_t *a, int32_t space, uint32_t person,
                     const uint8_t *msg, uint32_t len) {
    if (!a || a->kind != APP_CLUB || !a->club) return SOC_ERR_NULL;
    return soc_post(a->club, space, person, msg, len);
}

/* ---- DECLARATION -----------------------------------------------------------

 * ROOTING THE CALLER, NOT THE LEAF. pirate_apps.o was measured at linked=0,
 * dropped=18. It is the top of five separate chains: the Hold -> ipfs, the
 * Counter -> broker, the Cellar -> vino_stores, the Spyglass ->
 * finance_markets, the Club -> social_spaces -> concord.
 *
 * MB_MAX_CAPS IS 4 AND THIS MODULE HAS 5 EDGES. That is a hard ceiling
 * (modbind.h:162) and a fifth argument does not compile. The four declared are
 * the four whose providers are themselves declared. THE FIFTH, WRITTEN DOWN
 * HERE SO IT IS NOT LOST: finance_markets (U fm_pnl_report,
 * kernel/src/finance_markets/finance_markets.c) behind app_spyglass_open. It
 * is undeclared, not absent -- the Spyglass still links, and a reader who
 * assumes four requirements means four dependencies would be wrong.
 *
 * The bring-up opens the Club against a real initialised world, which is the
 * chain that reaches furthest: pirate_apps -> social_spaces -> concord ->
 * chiglet.
 */
#include "zxv_decl.h"
static int zxvd_pirate_apps_bringup(void) {
    static app_ctx_t ctx;
    static soc_world_t world;

    if (app_status(&ctx) != APP_STATUS_UNBOUND) return -1;

    /* OPENING IS THIS MODULE'S PRIMARY OPERATION -- it is a launcher, and the
     * five surfaces are the five things it does. All five open functions are
     * null-safe by inspection (each is `if (!a) return -1` then a guarded
     * store; app_counter_count and app_spyglass_positions both begin
     * `if (!a || a->kind != ... || !a-><backend>) return 0u`), so a surface can
     * be opened with no backend bound and reports honestly instead of faulting.
     *
     * WHAT THIS DELIBERATELY DOES NOT DO: call app_hold_fetch,
     * app_counter_settle or app_cellar_swap. Those are transactions that need a
     * real backend, and calling them purely to pull ipfs_get_verify and
     * broker_settle into the image would be inflating `nm` rather than using
     * the module -- the same sin as a GC root, wearing a function call. */
    if (app_hold_open(&ctx, 0) < 0)     return -1;   /* the Hold     -> ipfs  */
    if (app_counter_open(&ctx, 0) < 0)  return -1;   /* the Counter  -> broker */
    if (app_cellar_open(&ctx, 0) < 0)   return -1;   /* the Cellar   -> vino  */
    if (app_spyglass_open(&ctx, 0) < 0) return -1;   /* the Spyglass -> fm    */

    /* The Club is the one chain with a real backend available at boot, so it
     * is opened against an initialised world rather than a null one. */
    soc_init(&world);
    if (app_club_open(&ctx, &world) < 0) return -1;
    if (app_render(&ctx) == 0) return -1;
    return 0;
}

ZXV_DECLARE(pirate_apps,
    ZXV_PROVIDES(pirate_apps_ready),
    ZXV_REQUIRES(social_spaces_ready, ipfs_ready, broker_ready, vino_stores_ready),
    ZXV_BRINGUP(zxvd_pirate_apps_bringup));
