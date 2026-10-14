/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_workspace.c — saved workspaces, the panel, shortcuts and themes on
 * the settings store (zui_workspace.h WS1..WS9). */
#include "zui_workspace.h"
#include "zui_tokens.h"

/* ---- small helpers (no libc) ------------------------------------------- */

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static bool seq(const char *a, const char *b)
{
    uint32_t i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static void scopy(char *d, uint32_t cap, const char *s)
{
    uint32_t i = 0;
    for (; s && s[i] && i + 1u < cap; i++) d[i] = s[i];
    d[i] = 0;
}

static void zero(void *p, uint32_t n)
{
    uint8_t *z = p;
    for (uint32_t i = 0; i < n; i++) z[i] = 0;
}

static int32_t imin(int32_t a, int32_t b)
{
    return a < b ? a : b;
}
static int32_t imax(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

/* a key name built from parts */
typedef struct {
    char s[ZUI_SET_KEY_MAX];
    uint32_t n;
} kname_t;

static void k_str(kname_t *k, const char *s)
{
    for (uint32_t i = 0; s[i] && k->n + 1u < ZUI_SET_KEY_MAX; i++) k->s[k->n++] = s[i];
    k->s[k->n] = 0;
}

static void k_num(kname_t *k, uint32_t v)
{
    char t[10];
    uint32_t n = 0;
    do {
        t[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n && k->n + 1u < ZUI_SET_KEY_MAX) k->s[k->n++] = t[--n];
    k->s[k->n] = 0;
}

/* workspace.<slot>.<field>, or workspace.<slot>.w<win>.<field> */
static const char *ws_key(kname_t *k, uint32_t slot, int32_t win, const char *field)
{
    k->n = 0;
    k_str(k, "workspace.");
    k_num(k, slot);
    if (win >= 0) {
        k_str(k, ".w");
        k_num(k, (uint32_t) win);
    }
    k_str(k, ".");
    k_str(k, field);
    return k->s;
}

static const char *pin_key(kname_t *k, uint32_t i)
{
    k->n = 0;
    k_str(k, "panel.pin.");
    k_num(k, i);
    return k->s;
}

static const char *const ACTIONS[ZUI_ACT_COUNT] = {"switch-next", "switch-prev", "close-window",
                                                   "minimise",    "maximise",    "lock-now",
                                                   "ask",         "new-window",  "quit-app"};
static const char *const DEFAULTS[ZUI_ACT_COUNT] = {"cmd+tab", "cmd+shift+tab", "cmd+w",
                                                    "cmd+m",   "cmd+ctrl+f",    "cmd+shift+l",
                                                    "cmd+k",   "cmd+n",         "cmd+q"};

static const char *act_key(kname_t *k, uint32_t a)
{
    k->n = 0;
    k_str(k, "shortcut.");
    k_str(k, ACTIONS[a]);
    return k->s;
}

/* ---- the schema (WS1) -------------------------------------------------- */

static const char *const SCHEMES[ZUI_SCHEME_COUNT] = {"dark", "light", "high-contrast"};
static const char *const ACCENTS[ZUI_ACCENT_COUNT] = {"gold",  "amber",  "jade", "sky",   "rose",
                                                      "coral", "bronze", "pine", "ocean", "plum"};
static const char *const DENSITIES[3] = {"compact", "regular", "roomy"};
static const char *const POSITIONS[4] = {"bottom", "left", "right", "top"};
static const char *const STATES[3] = {"normal", "maximised", "minimised"};

#define COORD 32767

#define NSCHEMA (ZUI_WS_SAVED * (5u + ZUI_WS_WINS * 6u) + 3u + ZUI_WS_PINNED + ZUI_ACT_COUNT + 6u)

int zui_ws_schema(zui_settings_t *st)
{
    if (zui_set_key(st, "theme.scheme") >= 0) return ZUI_SET_OK;
    if (st->nkeys + NSCHEMA > ZUI_SET_KEYS) return ZUI_SET_E_FULL; /* all or nothing */
    kname_t k;
    int r = 0;
#define DEF(x)                                                                                     \
    do {                                                                                           \
        r = (x);                                                                                   \
        if (r < 0) return r;                                                                       \
    } while (0)
    for (uint32_t s = 0; s < ZUI_WS_SAVED; s++) {
        DEF(zui_set_def_str(st, ws_key(&k, s, -1, "name"), ""));
        DEF(zui_set_def_int(st, ws_key(&k, s, -1, "sw"), 0, 0, COORD, 1));
        DEF(zui_set_def_int(st, ws_key(&k, s, -1, "sh"), 0, 0, COORD, 1));
        DEF(zui_set_def_int(st, ws_key(&k, s, -1, "n"), 0, 0, (int32_t) ZUI_WS_WINS, 1));
        DEF(zui_set_def_int(st, ws_key(&k, s, -1, "front"), -1, -1, (int32_t) ZUI_WS_WINS - 1, 1));
        for (int32_t w = 0; w < (int32_t) ZUI_WS_WINS; w++) {
            DEF(zui_set_def_str(st, ws_key(&k, s, w, "app"), ""));
            DEF(zui_set_def_int(st, ws_key(&k, s, w, "x"), 0, -COORD, COORD, 1));
            DEF(zui_set_def_int(st, ws_key(&k, s, w, "y"), 0, -COORD, COORD, 1));
            DEF(zui_set_def_int(st, ws_key(&k, s, w, "w"), 0, 0, COORD, 1));
            DEF(zui_set_def_int(st, ws_key(&k, s, w, "h"), 0, 0, COORD, 1));
            DEF(zui_set_def_enum(st, ws_key(&k, s, w, "state"), STATES, 3, 0));
        }
    }
    DEF(zui_set_def_enum(st, "panel.position", POSITIONS, 4, ZUI_PANEL_BOTTOM));
    DEF(zui_set_def_int(st, "panel.size", ZUI_WM_STRIP, ZUI_WS_PANEL_MIN, ZUI_WS_PANEL_MAX, 4));
    DEF(zui_set_def_bool(st, "panel.autohide", false));
    for (uint32_t i = 0; i < ZUI_WS_PINNED; i++) DEF(zui_set_def_str(st, pin_key(&k, i), ""));
    for (uint32_t a = 0; a < ZUI_ACT_COUNT; a++)
        DEF(zui_set_def_str(st, act_key(&k, a), DEFAULTS[a]));
    DEF(zui_set_def_enum(st, "theme.scheme", SCHEMES, ZUI_SCHEME_COUNT, ZUI_SCHEME_DARK));
    DEF(zui_set_def_enum(st, "theme.accent", ACCENTS, ZUI_ACCENT_COUNT, ZUI_ACCENT_GOLD));
    DEF(zui_set_def_color(st, "theme.focus", ZUI_C_GOLD));
    DEF(zui_set_def_int(st, "theme.text-scale", 100, 80, 200, 10));
    DEF(zui_set_def_bool(st, "theme.reduced-motion", false));
    DEF(zui_set_def_enum(st, "theme.density", DENSITIES, 3, ZUI_DENSITY_REGULAR));
#undef DEF
    return ZUI_SET_OK;
}

int zui_ws_init(zui_workspace_t *ws, zui_settings_t *st, zui_set_view_t view)
{
    zero(ws, (uint32_t) sizeof *ws);
    int r = zui_ws_schema(st);
    if (r) return r;
    ws->st = st;
    ws->view = view;
    if (view.user) {
        ws->layer = ZUI_LAYER_USER;
        ws->scope.user = view.user;
    } else if (view.seat) {
        ws->layer = ZUI_LAYER_SEAT;
        ws->scope.seat = view.seat;
    } else {
        ws->layer = ZUI_LAYER_DEVICE;
    }
    return ZUI_SET_OK;
}

static int32_t rd(zui_workspace_t *ws, const char *key)
{
    return zui_set_int(ws->st, ws->view, key);
}

static void rds(zui_workspace_t *ws, const char *key, char *buf)
{
    zui_set_str(ws->st, ws->view, key, buf, ZUI_SET_STR_MAX);
}

static int wr(zui_workspace_t *ws, const char *key, int32_t v)
{
    return zui_set_set_int(ws->st, ws->layer, ws->scope, key, v);
}

static int wrs(zui_workspace_t *ws, const char *key, const char *v)
{
    return zui_set_set_str(ws->st, ws->layer, ws->scope, key, v);
}

static int first_err(int a, int b)
{
    return a ? a : b;
}

/* What the layers below this workspace's own would show for key (and
 * whether a lock there fixes it). */
static void below(zui_workspace_t *ws, const char *key, zui_set_value_t *out, bool *locked)
{
    int k = zui_set_key(ws->st, key);
    *locked = false;
    zero(out, (uint32_t) sizeof *out);
    if (k < 0) return;
    *out = zui_set_schema(ws->st, (uint32_t) k)->def;
    for (uint32_t l = ZUI_LAYER_SYSTEM; l < (uint32_t) ws->layer && l <= ZUI_LAYER_SEAT; l++) {
        zui_set_view_t sc = {0, 0, 0};
        if (l == ZUI_LAYER_SEAT) {
            if (!ws->view.seat) continue;
            sc.seat = ws->view.seat;
        }
        zui_set_value_t v;
        bool lk = false;
        if (zui_set_peek(ws->st, (zui_set_layer_t) l, sc, key, &v, &lk)) continue;
        *out = v;
        if (lk) {
            *locked = true;
            return;
        }
    }
}

/* ---- saved workspaces (WS2, WS3) --------------------------------------- */

static int find_slot(zui_workspace_t *ws, const char *name)
{
    kname_t k;
    char b[ZUI_SET_STR_MAX];
    for (uint32_t s = 0; s < ZUI_WS_SAVED; s++) {
        rds(ws, ws_key(&k, s, -1, "name"), b);
        if (b[0] && seq(b, name)) return (int) s;
    }
    return -1;
}

static bool good_name(const char *name)
{
    uint32_t n = slen(name);
    if (n == 0 || n >= ZUI_SET_STR_MAX) return false;
    for (uint32_t i = 0; i < n; i++)
        if ((uint8_t) name[i] < 0x20u || name[i] == 0x7F) return false;
    return true;
}

static int32_t clampc(int32_t v, int32_t lo)
{
    return imin(imax(v, lo), COORD);
}

int zui_ws_save(zui_workspace_t *ws, const zui_wm_t *wm, const char *name)
{
    if (!good_name(name)) return ZUI_SET_E_RANGE;
    int slot = find_slot(ws, name);
    if (slot < 0) {
        kname_t k;
        char b[ZUI_SET_STR_MAX];
        for (uint32_t s = 0; s < ZUI_WS_SAVED && slot < 0; s++) {
            rds(ws, ws_key(&k, s, -1, "name"), b);
            if (!b[0]) slot = (int) s;
        }
        if (slot < 0) return ZUI_SET_E_FULL;
    }
    uint32_t s = (uint32_t) slot;
    zui_wm_t *m = (zui_wm_t *) (uintptr_t) wm;
    uint32_t ids[ZUI_WM_MAX];
    uint32_t n = zui_wm_strip(wm, ids, ZUI_WM_MAX);
    uint32_t keep = n < ZUI_WS_WINS ? n : ZUI_WS_WINS;
    zui_rect_t wa = zui_wm_work(wm);
    uint32_t front = zui_wm_front(wm);
    int32_t fidx = -1;
    kname_t k;
    int r = 0;
    r = first_err(r, wrs(ws, ws_key(&k, s, -1, "name"), name));
    r = first_err(r, wr(ws, ws_key(&k, s, -1, "sw"), clampc(wa.w, 0)));
    r = first_err(r, wr(ws, ws_key(&k, s, -1, "sh"), clampc(wa.h, 0)));
    r = first_err(r, wr(ws, ws_key(&k, s, -1, "n"), (int32_t) keep));
    for (uint32_t i = 0; i < ZUI_WS_WINS; i++) {
        int32_t wi = (int32_t) i;
        if (i >= keep) { /* nothing there: the defaults show */
            static const char *const F[] = {"app", "x", "y", "w", "h", "state"};
            for (uint32_t f = 0; f < 6u; f++)
                r = first_err(r,
                              zui_set_reset(ws->st, ws->layer, ws->scope, ws_key(&k, s, wi, F[f])));
            continue;
        }
        const zui_wm_window_t *w = zui_wm_get(m, ids[i]);
        zui_rect_t f = w->maximised ? w->restore : w->r;
        if (ids[i] == front) fidx = wi;
        r = first_err(r, wrs(ws, ws_key(&k, s, wi, "app"), w->app));
        r = first_err(r, wr(ws, ws_key(&k, s, wi, "x"), clampc(f.x - wa.x, -COORD)));
        r = first_err(r, wr(ws, ws_key(&k, s, wi, "y"), clampc(f.y - wa.y, -COORD)));
        r = first_err(r, wr(ws, ws_key(&k, s, wi, "w"), clampc(f.w, 0)));
        r = first_err(r, wr(ws, ws_key(&k, s, wi, "h"), clampc(f.h, 0)));
        r = first_err(
            r, wr(ws, ws_key(&k, s, wi, "state"), w->maximised ? 1 : (w->minimised ? 2 : 0)));
    }
    r = first_err(r, wr(ws, ws_key(&k, s, -1, "front"), fidx));
    return r ? r : (int) keep;
}

static int32_t scale(int32_t v, int32_t to, int32_t from)
{
    if (from <= 0) return v;
    return (int32_t) ((int64_t) v * to / from);
}

int zui_ws_restore(zui_workspace_t *ws, zui_wm_t *wm, const char *name, zui_ws_open_fn open,
                   void *ctx)
{
    if (!name) return ZUI_WS_E_NOTFOUND;
    int slot = find_slot(ws, name);
    if (slot < 0) return ZUI_WS_E_NOTFOUND;
    uint32_t s = (uint32_t) slot;
    kname_t k;
    int32_t n = rd(ws, ws_key(&k, s, -1, "n"));
    int32_t front = rd(ws, ws_key(&k, s, -1, "front"));
    int32_t sw = rd(ws, ws_key(&k, s, -1, "sw")), sh = rd(ws, ws_key(&k, s, -1, "sh"));
    zui_rect_t wa = zui_wm_work(wm);
    uint32_t ids[ZUI_WS_WINS] = {0};
    int opened = 0;
    for (int32_t i = 0; i < n; i++) {
        char app[ZUI_SET_STR_MAX];
        rds(ws, ws_key(&k, s, i, "app"), app);
        if (!app[0]) continue;
        zui_wm_client_t c;
        zui_launch_t l;
        zero(&c, (uint32_t) sizeof c);
        zero(&l, (uint32_t) sizeof l);
        if (open && !open(ctx, app, &c, &l)) continue;
        uint32_t id = zui_wm_open(wm, app, app, &l, &c);
        if (!id) break; /* the manager is full */
        ids[i] = id;
        opened++;
        if (wm->compact) continue; /* WS3: just in order */
        zui_rect_t r = {wa.x + scale(rd(ws, ws_key(&k, s, i, "x")), wa.w, sw),
                        wa.y + scale(rd(ws, ws_key(&k, s, i, "y")), wa.h, sh),
                        scale(rd(ws, ws_key(&k, s, i, "w")), wa.w, sw),
                        scale(rd(ws, ws_key(&k, s, i, "h")), wa.h, sh)};
        r.w = imin(imax(r.w, ZUI_WM_MIN_W), wa.w);
        r.h = imin(imax(r.h, ZUI_WM_MIN_H), wa.h);
        r.x = imax(wa.x, imin(r.x, wa.x + wa.w - r.w));
        r.y = imax(wa.y, imin(r.y, wa.y + wa.h - r.h));
        zui_wm_move(wm, id, r);
        int32_t state = rd(ws, ws_key(&k, s, i, "state"));
        if (state == 1) zui_wm_maximise(wm, id, true);
        if (state == 2) zui_wm_minimise(wm, id);
    }
    if (front >= 0 && front < n && ids[front]) zui_wm_focus(wm, ids[front]);
    return opened;
}

int zui_ws_forget(zui_workspace_t *ws, const char *name)
{
    int slot = name ? find_slot(ws, name) : -1;
    if (slot < 0) return ZUI_WS_E_NOTFOUND;
    kname_t k;
    static const char *const TOP[] = {"name", "sw", "sh", "n", "front"};
    static const char *const F[] = {"app", "x", "y", "w", "h", "state"};
    int r = 0;
    for (uint32_t f = 0; f < 5u; f++)
        r = first_err(r, zui_set_reset(ws->st, ws->layer, ws->scope,
                                       ws_key(&k, (uint32_t) slot, -1, TOP[f])));
    for (int32_t w = 0; w < (int32_t) ZUI_WS_WINS; w++)
        for (uint32_t f = 0; f < 6u; f++)
            r = first_err(r, zui_set_reset(ws->st, ws->layer, ws->scope,
                                           ws_key(&k, (uint32_t) slot, w, F[f])));
    /* a name still showing from a layer below cannot be forgotten here */
    if (!r && find_slot(ws, name) == slot) r = ZUI_SET_E_LOCKED;
    return r;
}

uint32_t zui_ws_names(zui_workspace_t *ws, char names[][ZUI_SET_STR_MAX], uint32_t cap)
{
    uint32_t n = 0;
    kname_t k;
    char b[ZUI_SET_STR_MAX];
    for (uint32_t s = 0; s < ZUI_WS_SAVED; s++) {
        rds(ws, ws_key(&k, s, -1, "name"), b);
        if (!b[0]) continue;
        if (names && n < cap) scopy(names[n], ZUI_SET_STR_MAX, b);
        n++;
    }
    return n;
}

/* ---- the panel (WS4) --------------------------------------------------- */

void zui_ws_panel(zui_workspace_t *ws, zui_ws_panel_t *out)
{
    zero(out, (uint32_t) sizeof *out);
    out->pos = (zui_panel_pos_t) rd(ws, "panel.position");
    out->size = rd(ws, "panel.size");
    out->autohide = rd(ws, "panel.autohide") != 0;
    kname_t k;
    for (uint32_t i = 0; i < ZUI_WS_PINNED; i++) {
        char b[ZUI_SET_STR_MAX];
        rds(ws, pin_key(&k, i), b);
        if (!b[0]) continue;
        bool dup = false;
        for (uint32_t j = 0; j < out->npinned; j++)
            if (seq(out->pinned[j], b)) dup = true;
        if (!dup) scopy(out->pinned[out->npinned++], ZUI_SET_STR_MAX, b);
    }
}

int zui_ws_set_panel(zui_workspace_t *ws, zui_panel_pos_t pos, int32_t size, bool autohide)
{
    if ((uint32_t) pos > (uint32_t) ZUI_PANEL_TOP) return ZUI_SET_E_RANGE;
    if (size < ZUI_WS_PANEL_MIN || size > ZUI_WS_PANEL_MAX || size % 4) return ZUI_SET_E_RANGE;
    int r = wr(ws, "panel.size", size);
    r = first_err(r, wr(ws, "panel.position", (int32_t) pos));
    return first_err(r, wr(ws, "panel.autohide", autohide ? 1 : 0));
}

static int write_pins(zui_workspace_t *ws, const zui_ws_panel_t *p)
{
    kname_t k;
    int r = 0;
    for (uint32_t i = 0; i < ZUI_WS_PINNED; i++)
        r = first_err(r, wrs(ws, pin_key(&k, i), i < p->npinned ? p->pinned[i] : ""));
    return r;
}

static int pinned_at(const zui_ws_panel_t *p, const char *app)
{
    for (uint32_t i = 0; i < p->npinned; i++)
        if (seq(p->pinned[i], app)) return (int) i;
    return -1;
}

int zui_ws_pin(zui_workspace_t *ws, const char *app, uint32_t at)
{
    if (!good_name(app)) return ZUI_SET_E_RANGE;
    zui_ws_panel_t p;
    zui_ws_panel(ws, &p);
    int was = pinned_at(&p, app);
    if (was >= 0) { /* already pinned: move it */
        for (uint32_t i = (uint32_t) was; i + 1u < p.npinned; i++)
            scopy(p.pinned[i], ZUI_SET_STR_MAX, p.pinned[i + 1u]);
        p.npinned--;
    } else if (p.npinned >= ZUI_WS_PINNED) {
        return ZUI_SET_E_FULL;
    }
    if (at > p.npinned) at = p.npinned;
    for (uint32_t i = p.npinned; i > at; i--) scopy(p.pinned[i], ZUI_SET_STR_MAX, p.pinned[i - 1u]);
    scopy(p.pinned[at], ZUI_SET_STR_MAX, app);
    p.npinned++;
    return write_pins(ws, &p);
}

int zui_ws_unpin(zui_workspace_t *ws, const char *app)
{
    if (!app) return ZUI_WS_E_NOTFOUND;
    zui_ws_panel_t p;
    zui_ws_panel(ws, &p);
    int at = pinned_at(&p, app);
    if (at < 0) return ZUI_WS_E_NOTFOUND;
    for (uint32_t i = (uint32_t) at; i + 1u < p.npinned; i++)
        scopy(p.pinned[i], ZUI_SET_STR_MAX, p.pinned[i + 1u]);
    p.npinned--;
    return write_pins(ws, &p);
}

int zui_ws_pins_reset(zui_workspace_t *ws)
{
    kname_t k;
    int r = 0;
    for (uint32_t i = 0; i < ZUI_WS_PINNED; i++)
        r = first_err(r, zui_set_reset(ws->st, ws->layer, ws->scope, pin_key(&k, i)));
    return r;
}

zui_rect_t zui_ws_panel_rect(zui_workspace_t *ws, int32_t w, int32_t h)
{
    zui_rect_t r = {0, 0, 0, 0};
    if (rd(ws, "panel.autohide")) return r;
    int32_t s = rd(ws, "panel.size");
    switch ((zui_panel_pos_t) rd(ws, "panel.position")) {
    case ZUI_PANEL_LEFT:
        r = (zui_rect_t){0, 0, imin(s, w), h};
        break;
    case ZUI_PANEL_RIGHT:
        r = (zui_rect_t){imax(0, w - s), 0, imin(s, w), h};
        break;
    case ZUI_PANEL_TOP:
        r = (zui_rect_t){0, 0, w, imin(s, h)};
        break;
    default:
        r = (zui_rect_t){0, imax(0, h - s), w, imin(s, h)};
    }
    return r;
}

zui_rect_t zui_ws_work(zui_workspace_t *ws, int32_t w, int32_t h)
{
    zui_rect_t p = zui_ws_panel_rect(ws, w, h), r = {0, 0, w, h};
    if (!p.w || !p.h) return r;
    switch ((zui_panel_pos_t) rd(ws, "panel.position")) {
    case ZUI_PANEL_LEFT:
        r.x = p.w, r.w = w - p.w;
        break;
    case ZUI_PANEL_RIGHT:
        r.w = w - p.w;
        break;
    case ZUI_PANEL_TOP:
        r.y = p.h, r.h = h - p.h;
        break;
    default:
        r.h = h - p.h;
    }
    return r;
}

/* ---- shortcuts (WS5, WS6) ---------------------------------------------- */

const char *zui_ws_action_name(zui_ws_action_t a)
{
    return (uint32_t) a < ZUI_ACT_COUNT ? ACTIONS[a] : "?";
}

int zui_ws_action(const char *name)
{
    for (uint32_t a = 0; name && a < ZUI_ACT_COUNT; a++)
        if (seq(ACTIONS[a], name)) return (int) a;
    return -1;
}

static const struct {
    const char *name;
    uint16_t key;
} KEYS[] = {{"tab", ZUI_KEY_TAB},
            {"enter", ZUI_KEY_ENTER},
            {"esc", ZUI_KEY_ESCAPE},
            {"space", ZUI_KEY_SPACE},
            {"backspace", ZUI_KEY_BACKSPACE},
            {"delete", ZUI_KEY_DELETE},
            {"left", ZUI_KEY_LEFT},
            {"right", ZUI_KEY_RIGHT},
            {"up", ZUI_KEY_UP},
            {"down", ZUI_KEY_DOWN},
            {"home", ZUI_KEY_HOME},
            {"end", ZUI_KEY_END},
            {"pageup", ZUI_KEY_PAGE_UP},
            {"pagedown", ZUI_KEY_PAGE_DOWN},
            {"insert", ZUI_KEY_INSERT},
            {"menu", ZUI_KEY_MENU},
            {"back", ZUI_KEY_BACK}};
#define NKEYS (sizeof KEYS / sizeof KEYS[0])

static const struct {
    const char *name;
    uint8_t bit;
} MODS[] = {{"cmd", ZUI_MOD_CMD},
            {"ctrl", ZUI_MOD_CTRL},
            {"alt", ZUI_MOD_ALT},
            {"shift", ZUI_MOD_SHIFT},
            {"meta", ZUI_MOD_META}};
#define NMODS 5u

static bool punct(char c)
{
    const char *p = ",./;'[]-=`\\";
    for (uint32_t i = 0; p[i]; i++)
        if (p[i] == c) return true;
    return false;
}

static bool neqn(const char *a, uint32_t n, const char *b)
{
    for (uint32_t i = 0; i < n; i++)
        if (!b[i] || a[i] != b[i]) return false;
    return b[n] == 0;
}

/* a key name s[0..n) to its code, 0 if none */
static uint16_t key_code(const char *s, uint32_t n)
{
    if (n == 1) {
        char c = s[0];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || punct(c)) return (uint16_t) c;
        return 0;
    }
    for (uint32_t i = 0; i < NKEYS; i++)
        if (neqn(s, n, KEYS[i].name)) return KEYS[i].key;
    if (n >= 2u && n <= 3u && s[0] == 'f') {
        uint32_t v = 0;
        for (uint32_t i = 1; i < n; i++) {
            if (s[i] < '0' || s[i] > '9') return 0;
            v = v * 10u + (uint32_t) (s[i] - '0');
        }
        if (s[1] == '0' || v < 1u || v > 12u) return 0;
        return (uint16_t) (ZUI_KEY_F1 + v - 1u);
    }
    return 0;
}

static bool types_text(uint16_t key)
{
    return key == ZUI_KEY_SPACE || (key > ZUI_KEY_SPACE && key < ZUI_KEY_DELETE);
}

bool zui_chord_parse(const char *t, zui_chord_t *out)
{
    zui_chord_t c = {0, 0};
    if (!t) return false;
    uint32_t n = slen(t), p = 0;
    if (n == 0) {
        if (out) *out = c;
        return true; /* unbound */
    }
    for (;;) {
        uint32_t q = p;
        while (q < n && t[q] != '+') q++;
        if (q == p) return false; /* empty part */
        if (q == n) {             /* the key */
            c.key = key_code(t + p, q - p);
            if (!c.key) return false;
            break;
        }
        uint32_t m = 0;
        while (m < NMODS && !neqn(t + p, q - p, MODS[m].name)) m++;
        if (m == NMODS || (c.mods & MODS[m].bit)) return false;
        c.mods |= MODS[m].bit;
        p = q + 1u;
    }
    uint8_t real = ZUI_MOD_CMD | ZUI_MOD_CTRL | ZUI_MOD_ALT | ZUI_MOD_META;
    if (types_text(c.key) && !(c.mods & real)) return false; /* would eat typing */
    if (out) *out = c;
    return true;
}

int zui_chord_format(zui_chord_t c, char *buf, uint32_t cap)
{
    char t[ZUI_WS_CHORD_MAX]; /* the longest: all five mods and "backspace" */
    uint32_t n = 0;
    if (!buf || !cap) return ZUI_SET_E_SPACE;
    if (c.key) {
        for (uint32_t m = 0; m < NMODS; m++) {
            if (!(c.mods & MODS[m].bit)) continue;
            for (uint32_t i = 0; MODS[m].name[i]; i++) t[n++] = MODS[m].name[i];
            t[n++] = '+';
        }
        const char *name = 0;
        for (uint32_t i = 0; i < NKEYS && !name; i++)
            if (KEYS[i].key == c.key) name = KEYS[i].name;
        if (name) {
            for (uint32_t i = 0; name[i]; i++) t[n++] = name[i];
        } else if (c.key >= ZUI_KEY_F1 && c.key <= ZUI_KEY_F12) {
            uint32_t v = (uint32_t) (c.key - ZUI_KEY_F1) + 1u;
            t[n++] = 'f';
            if (v >= 10u) t[n++] = '1';
            t[n++] = (char) ('0' + v % 10u);
        } else {
            t[n++] = (char) c.key;
        }
    }
    if (n + 1u > cap) return ZUI_SET_E_SPACE;
    for (uint32_t i = 0; i < n; i++) buf[i] = t[i];
    buf[n] = 0;
    return (int) n;
}

zui_chord_t zui_ws_default_chord(zui_ws_action_t a)
{
    zui_chord_t c = {0, 0};
    if ((uint32_t) a < ZUI_ACT_COUNT) zui_chord_parse(DEFAULTS[a], &c);
    return c;
}

static zui_chord_t chord_of(const char *text, zui_ws_action_t a)
{
    zui_chord_t c;
    if (!zui_chord_parse(text, &c)) return zui_ws_default_chord(a);
    if (a == ZUI_ACT_LOCK_NOW && !c.key) return zui_ws_default_chord(a);
    return c;
}

zui_chord_t zui_ws_binding(zui_workspace_t *ws, zui_ws_action_t a)
{
    zui_chord_t none = {0, 0};
    if ((uint32_t) a >= ZUI_ACT_COUNT) return none;
    kname_t k;
    char b[ZUI_SET_STR_MAX];
    rds(ws, act_key(&k, (uint32_t) a), b);
    return chord_of(b, a);
}

static bool chord_eq(zui_chord_t a, zui_chord_t b)
{
    return a.key && a.key == b.key && a.mods == b.mods;
}

/* the other action already holding c, or -1 */
static int holder_of(zui_workspace_t *ws, zui_ws_action_t a, zui_chord_t c)
{
    for (uint32_t b = 0; b < ZUI_ACT_COUNT; b++)
        if (b != (uint32_t) a && chord_eq(zui_ws_binding(ws, (zui_ws_action_t) b), c))
            return (int) b;
    return -1;
}

int zui_ws_bind(zui_workspace_t *ws, zui_ws_action_t a, const char *chord, int *holder)
{
    if ((uint32_t) a >= ZUI_ACT_COUNT) return ZUI_SET_E_NOKEY;
    zui_chord_t c;
    if (!zui_chord_parse(chord, &c)) return ZUI_SET_E_TYPE;
    if (a == ZUI_ACT_LOCK_NOW && !c.key) return ZUI_SET_E_RANGE; /* must stay bound */
    int h = holder_of(ws, a, c);
    if (h >= 0) {
        if (holder) *holder = h;
        return ZUI_WS_E_CONFLICT;
    }
    char t[ZUI_WS_CHORD_MAX];
    zui_chord_format(c, t, sizeof t);
    kname_t k;
    return wrs(ws, act_key(&k, (uint32_t) a), t);
}

int zui_ws_bind_reset(zui_workspace_t *ws, zui_ws_action_t a, int *holder)
{
    if ((uint32_t) a >= ZUI_ACT_COUNT) return ZUI_SET_E_NOKEY;
    kname_t k;
    zui_set_value_t v;
    bool lk;
    below(ws, act_key(&k, (uint32_t) a), &v, &lk);
    int h = holder_of(ws, a, chord_of(v.s, a));
    if (h >= 0) {
        if (holder) *holder = h;
        return ZUI_WS_E_CONFLICT;
    }
    return zui_set_reset(ws->st, ws->layer, ws->scope, act_key(&k, (uint32_t) a));
}

int zui_ws_bind_reset_all(zui_workspace_t *ws)
{
    kname_t k;
    int r = 0;
    for (uint32_t a = 0; a < ZUI_ACT_COUNT; a++)
        r = first_err(r, zui_set_reset(ws->st, ws->layer, ws->scope, act_key(&k, a)));
    return r;
}

bool zui_ws_conflict(zui_workspace_t *ws, int *a, int *b)
{
    zui_chord_t c[ZUI_ACT_COUNT];
    for (uint32_t i = 0; i < ZUI_ACT_COUNT; i++) c[i] = zui_ws_binding(ws, (zui_ws_action_t) i);
    for (uint32_t i = 0; i < ZUI_ACT_COUNT; i++)
        for (uint32_t j = i + 1u; j < ZUI_ACT_COUNT; j++)
            if (chord_eq(c[i], c[j])) {
                if (a) *a = (int) i;
                if (b) *b = (int) j;
                return true;
            }
    return false;
}

int zui_ws_match(zui_workspace_t *ws, const zui_event_t *e)
{
    if (!e || e->type != ZUI_EV_KEY_DOWN || !e->key) return -1;
    uint8_t m = e->mods & (ZUI_MOD_CMD | ZUI_MOD_CTRL | ZUI_MOD_ALT | ZUI_MOD_SHIFT | ZUI_MOD_META);
    if (m & ZUI_MOD_CMD) m &= (uint8_t) ~(ws->cmd_is_meta ? ZUI_MOD_META : ZUI_MOD_CTRL);
    uint16_t key = e->key;
    if (key >= 'A' && key <= 'Z') key = (uint16_t) (key - 'A' + 'a');
    zui_chord_t c = {m, key};
    for (uint32_t a = 0; a < ZUI_ACT_COUNT; a++)
        if (chord_eq(zui_ws_binding(ws, (zui_ws_action_t) a), c)) return (int) a;
    return -1;
}

/* ---- themes and comfort (WS7, WS8) ------------------------------------- */

/* the accent and its hover shade; gold (0) is the set's own */
static const uint32_t ACCENT_RGB[ZUI_ACCENT_COUNT][2] = {
    {0, 0},                  /* gold: from the set              */
    {0xFFB347u, 0xFFC46Bu},  /* amber   (dark grounds)          */
    {0x5FD39Au, 0x86E6B4u},  /* jade                            */
    {0x7CC4FFu, 0xA6D8FFu},  /* sky                             */
    {0xFF8FA3u, 0xFFB0BFu},  /* rose                            */
    {0xFF7A5Cu, 0xFF9478u},  /* coral   (not high contrast)     */
    {0x8A4500u, 0x753A00u},  /* bronze  (light grounds)         */
    {0x0F6B3Eu, 0x0A5A33u},  /* pine                            */
    {0x0B57A8u, 0x094A8Fu},  /* ocean                           */
    {0x6B3FB0u, 0x5A3399u}}; /* plum                            */

const char *zui_accent_name(zui_accent_t a)
{
    return (uint32_t) a < ZUI_ACCENT_COUNT ? ACCENTS[a] : "?";
}

static void accent_rgb(zui_scheme_t scheme, zui_accent_t a, uint32_t *base, uint32_t *hi)
{
    if (a == ZUI_ACCENT_GOLD) {
        *base = zui_scheme_color(scheme, ZUI_C_GOLD);
        *hi = zui_scheme_color(scheme, ZUI_C_GOLD_HI);
    } else {
        *base = ACCENT_RGB[a][0];
        *hi = ACCENT_RGB[a][1];
    }
}

int zui_ws_theme_check(zui_scheme_t scheme, zui_accent_t accent)
{
    if ((uint32_t) scheme >= ZUI_SCHEME_COUNT || (uint32_t) accent >= ZUI_ACCENT_COUNT)
        return ZUI_SET_E_RANGE;
    uint32_t need = scheme == ZUI_SCHEME_HIGH_CONTRAST ? ZUI_CONTRAST_AAA : ZUI_CONTRAST_AA;
    uint32_t c[2];
    accent_rgb(scheme, accent, &c[0], &c[1]);
    static const zui_color_role_t G[] = {ZUI_C_VOID, ZUI_C_PANEL, ZUI_C_RAISED};
    /* as text on each ground; the void-coloured label on an accent button
     * is the same pair as the accent on the void */
    for (uint32_t i = 0; i < 2u; i++) {
        for (uint32_t g = 0; g < 3u; g++)
            if (!zui_contrast_ok(c[i], zui_scheme_color(scheme, G[g]), need))
                return ZUI_WS_E_CONTRAST;
    }
    return ZUI_SET_OK;
}

int zui_ws_focus_check(zui_scheme_t scheme, zui_color_role_t focus)
{
    if ((uint32_t) scheme >= ZUI_SCHEME_COUNT || (uint32_t) focus >= ZUI_C_COUNT)
        return ZUI_SET_E_RANGE;
    uint32_t f = zui_scheme_color(scheme, focus);
    if (!zui_contrast_ok(f, zui_scheme_color(scheme, ZUI_C_VOID), ZUI_CONTRAST_LARGE) ||
        !zui_contrast_ok(f, zui_scheme_color(scheme, ZUI_C_PANEL), ZUI_CONTRAST_LARGE))
        return ZUI_WS_E_CONTRAST;
    return ZUI_SET_OK;
}

/* 0: key is free to write; 1: a lock below already holds want (nothing to
 * write); -1: a lock below holds something else */
static int held(zui_workspace_t *ws, const char *key, int32_t want)
{
    zui_set_value_t v;
    bool lk;
    below(ws, key, &v, &lk);
    if (!lk) return 0;
    return v.i == want ? 1 : -1;
}

int zui_ws_set_theme(zui_workspace_t *ws, zui_scheme_t scheme, zui_accent_t accent)
{
    int r = zui_ws_theme_check(scheme, accent);
    if (r) return r;
    int hs = held(ws, "theme.scheme", (int32_t) scheme);
    int ha = held(ws, "theme.accent", (int32_t) accent);
    if (hs < 0 || ha < 0) return ZUI_SET_E_LOCKED; /* nothing written */
    if (!hs) r = wr(ws, "theme.scheme", (int32_t) scheme);
    if (!ha) r = first_err(r, wr(ws, "theme.accent", (int32_t) accent));
    return r;
}

int zui_ws_set_focus(zui_workspace_t *ws, zui_color_role_t focus)
{
    int r = zui_ws_focus_check((zui_scheme_t) rd(ws, "theme.scheme"), focus);
    return r ? r : wr(ws, "theme.focus", (int32_t) focus);
}

int zui_ws_set_comfort(zui_workspace_t *ws, int32_t text_scale, bool reduced_motion,
                       zui_density_t density)
{
    if ((uint32_t) density > (uint32_t) ZUI_DENSITY_ROOMY) return ZUI_SET_E_RANGE;
    int r = wr(ws, "theme.text-scale", text_scale);
    if (r) return r;
    r = wr(ws, "theme.reduced-motion", reduced_motion ? 1 : 0);
    return first_err(r, wr(ws, "theme.density", (int32_t) density));
}

void zui_ws_theme(zui_workspace_t *ws, zui_ws_theme_t *out)
{
    out->scheme = (zui_scheme_t) rd(ws, "theme.scheme");
    out->accent = (zui_accent_t) rd(ws, "theme.accent");
    if (zui_ws_theme_check(out->scheme, out->accent)) out->accent = ZUI_ACCENT_GOLD;
    out->focus = (zui_color_role_t) rd(ws, "theme.focus");
    if (zui_ws_focus_check(out->scheme, out->focus)) out->focus = ZUI_C_GOLD;
    out->text_scale = rd(ws, "theme.text-scale");
    out->reduced_motion = rd(ws, "theme.reduced-motion") != 0;
    out->density = (zui_density_t) rd(ws, "theme.density");
}

void zui_ws_colors(zui_workspace_t *ws, zui_colors_t *out)
{
    zui_ws_theme_t t;
    zui_ws_theme(ws, &t);
    *out = *zui_scheme_colors(t.scheme);
    uint32_t base, hi;
    accent_rgb(t.scheme, t.accent, &base, &hi);
    if (t.accent == ZUI_ACCENT_GOLD) return;
    out->c[ZUI_C_GOLD] = base;
    out->c[ZUI_C_GOLD_HI] = hi;
    out->c[ZUI_C_GOLD_GLOW] = hi;
}

int32_t zui_ws_text_pt(zui_workspace_t *ws, int32_t pt)
{
    return (pt * rd(ws, "theme.text-scale") + 50) / 100;
}

int32_t zui_ws_space(zui_workspace_t *ws, int32_t pt)
{
    switch ((zui_density_t) rd(ws, "theme.density")) {
    case ZUI_DENSITY_COMPACT:
        return (pt * 3 + 2) / 4;
    case ZUI_DENSITY_ROOMY:
        return (pt * 5 + 2) / 4;
    default:
        return pt;
    }
}

int32_t zui_ws_target(zui_workspace_t *ws, int32_t pt, bool touch)
{
    return imax(zui_ws_space(ws, pt), touch ? ZUI_TOUCH_MIN : ZUI_POINTER_MIN);
}

uint32_t zui_ws_motion_ms(zui_workspace_t *ws, uint32_t ms)
{
    return rd(ws, "theme.reduced-motion") ? 0u : ms;
}
