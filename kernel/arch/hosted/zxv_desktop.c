/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_desktop.c — the hosted ZXV desktop. See zxv_desktop.h. */
#if !defined(_WIN32) && !defined(__APPLE__)
#    define _POSIX_C_SOURCE 200809L
#endif
#include "zxv_desktop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "burrow.h"
#include "glint.h"
#include "murmuration.h"
#include "quillon.h"
#include "spindle.h"
#include "zui_present_host.h"
#include "zui_tokens.h"
#include "zxv_rpc_client.h"

/* zui_palette.h C6: every colour by role, from the chosen set */
#define C(role) zui_color(cs, ZUI_C_##role)

#ifndef ZXV_FONT_DIR
#    define ZXV_FONT_DIR "assets/fonts/ascw-golden"
#endif

#ifndef ZXV_APPS_DIR
#    define ZXV_APPS_DIR "assets/icons/apps"
#endif

#define APPS_PACK "zxv-apps.zxap"
#define FONT_MAIN "ASCW-Golden-Latin2.ttf"
#define FONT_MONO "ASCW-Golden-Mono.ttf"

#define RPC_TIMEOUT_MS   5000
#define PROBE_EVERY_MS   2000u
#define PROBE_TIMEOUT_MS 500
#define GLINT_BUF        (4u << 20)
#define EVQ              256u
#define PATH_CAP         1024u
#define LIB_SLOTS        64u  /* HD6: icons kept drawn, a screenful */
#define LIB_SIDE         128u /* 64 pt icons up to 2x */
#define CARDS            6u

/* the apps on the rail, in order. The file manager is the Banker (its model
 * and view are still called burrow in the code). */
enum { APP_BURROW, APP_QUILLON, APP_GLINT, APP_SPINDLE, APP_MURMURATION, APP_N };
static const zui_desk_app_t APPS[APP_N] = {
    {"Banker", "B", NULL, 0},  {"Quillon", "Q", NULL, 0},     {"Glint", "G", NULL, 0},
    {"Spindle", "S", NULL, 0}, {"Murmuration", "M", NULL, 0},
};

/* HD4 */
#define SPACES 5u
static const char *const SPACE_NAME[SPACES] = {"Home", "Office", "Team", "Cloud top", "Park"};

typedef struct appwin appwin_t;

struct appwin {
    zxv_desktop_t *d;
    uint32_t app, id;
    bool dead; /* closed: freed at the end of the step */
    appwin_t *next_dead;
    zui_ui_t ui;
    uint32_t *px;
    int32_t pw, ph;
    zui_surface_t surf;
    union {
        burrow_view_t burrow;
        quillon_view_t quillon;
        glint_view_t glint;
        spindle_view_t spindle;
        murm_view_t murm;
    } v;
};

/* HD6: an app's card in a window */
typedef struct {
    zxv_desktop_t *d;
    int32_t app; /* catalogue index */
    uint32_t id; /* its window, 0 none */
    uint32_t age;
} card_t;

/* a file Burrow hands over, opened after the event that asked */
typedef struct {
    bool want;
    uint8_t kind;
    char space[ZXM_SPACE_MAX + 1];
    char path[BURROW_PATH_MAX + 1];
} pending_open_t;

struct zxv_desktop {
    bool headless;
    zui_present_t p;
    const zui_present_ops_t *ops;
    zui_surface_t surf;
    uint32_t *px;
    int32_t w, h;
    uint32_t scale_q8;
    zui_queue_t q;
    zui_event_t fed[EVQ];
    uint32_t fed_head, fed_tail;
    uint32_t now, last_tick, fake_ms;
    bool dirty;
    uint32_t tick;

    /* faces (zui_desk_faces_t points at these) */
    ttf_font_t font, mono;
    uint8_t *font_data, *mono_data;
    bool have_font, have_mono;
    zui_glyph_cache_t *cache, *cache_mono;
    zui_text_t t_title, t_body, t_caption, t_mark, t_wm, t_label;
    zui_theme_t theme;

    zui_desk_t desk;
    appwin_t *win[APP_N];
    zui_scheme_t scheme; /* HD7 */
    const zui_colors_t *colors;

    /* HD6: the catalogue, its icons and the library */
    uint8_t *apps_data;
    zui_apps_t apps;
    bool have_apps;
    zui_vec_work_t *vw;
    uint32_t *lib_px;
    zui_applib_cache_t lib_cache;
    zui_applib_t lib;
    card_t card[CARDS];
    uint32_t card_age;
    appwin_t *dead;
    pending_open_t pending;

    bool (*clip_set)(void *ctx, const char *s, uint32_t n);
    uint32_t (*clip_get)(void *ctx, char *out, uint32_t cap);
    void *clip_ctx;

    /* Spaces */
    bool spaces;
    uint32_t space;
    int32_t space_press;

    /* zxvd */
    char rpc[PATH_CAP];
    bool connected;
    uint32_t last_probe;
    bool probed;
    char note[ZUI_DESK_STATUS];
    char req[ZXM_REQ_MAX + 1];
    zxm_link_t link;

    /* the models (kept while their windows come and go) */
    burrow_t burrow;
    burrow_t glint_dir; /* Glint's own folder */
    quillon_t quillon;
    glint_t glint;
    spindle_t spindle;
    murmuration_t murm;
    uint8_t *glint_buf;
};

/* ---------------------------------------------------------------- zxvd */

static int32_t rpc_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxv_desktop_t *d = (zxv_desktop_t *) ctx;
    if (!d->rpc[0] || len > ZXM_REQ_MAX || cap == 0) return -1;
    memcpy(d->req, req, len);
    d->req[len] = 0;
    long n = zxv_rpc_call(d->rpc, d->req, resp, cap, RPC_TIMEOUT_MS);
    bool was = d->connected;
    d->connected = n >= 0;
    if (was != d->connected) d->dirty = true;
    return n < 0 ? -1 : (int32_t) n;
}

static void probe(zxv_desktop_t *d)
{
    bool was = d->connected;
    if (!d->rpc[0]) {
        d->connected = false;
    } else {
        char reply[8192];
        d->connected =
            zxv_rpc_call(d->rpc, "{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"zxvd.version\"}",
                         reply, sizeof reply, PROBE_TIMEOUT_MS) >= 0;
    }
    d->last_probe = d->now;
    d->probed = true;
    if (was != d->connected) d->dirty = true;
}

static void status(zxv_desktop_t *d)
{
    char s[ZUI_DESK_STATUS];
    if (d->note[0])
        snprintf(s, sizeof s, "%s", d->note);
    else if (d->space)
        snprintf(s, sizeof s, "%s  \xc2\xb7  %s", SPACE_NAME[d->space],
                 d->connected ? "zxvd connected" : "zxvd not running");
    else
        snprintf(s, sizeof s, "%s", d->connected ? "zxvd connected" : "zxvd not running");
    if (strcmp(s, d->desk.status)) {
        zui_desk_status(&d->desk, s);
        d->dirty = true;
    }
}

void zxv_desktop_note(zxv_desktop_t *d, const char *text)
{
    if (!d) return;
    snprintf(d->note, sizeof d->note, "%s", text ? text : "");
    status(d);
}

void zxv_desktop_set_rpc(zxv_desktop_t *d, const char *path)
{
    if (!d) return;
    snprintf(d->rpc, sizeof d->rpc, "%s", path ? path : "");
    probe(d);
    status(d);
}

bool zxv_desktop_connected(const zxv_desktop_t *d)
{
    return d && d->connected;
}

/* ---------------------------------------------------------------- faces */

static uint8_t *read_file(const char *path, uint32_t *len)
{
    FILE *f = path && path[0] ? fopen(path, "rb") : NULL;
    if (!f) return NULL;
    uint8_t *buf = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n > 0 && n < (64L << 20) && fseek(f, 0, SEEK_SET) == 0) {
            buf = (uint8_t *) malloc((size_t) n);
            if (buf && fread(buf, 1, (size_t) n, f) != (size_t) n) {
                free(buf);
                buf = NULL;
            }
            if (buf) *len = (uint32_t) n;
        }
    }
    fclose(f);
    return buf;
}

static bool load_font(const char *path, ttf_font_t *f, uint8_t **data)
{
    uint32_t n = 0;
    uint8_t *b = read_file(path, &n);
    if (!b) return false;
    if (!ttf_parse(f, b, n)) {
        free(b);
        return false;
    }
    *data = b;
    return true;
}

static int32_t px_of(uint32_t scale_q8, int32_t pt)
{
    return (int32_t) (((int64_t) pt * scale_q8 + 128) >> 8);
}

static void face(zui_text_t *t, const ttf_font_t *f, zui_glyph_cache_t *c, int32_t px, bool tab)
{
    t->font = f;
    t->cache = f ? c : NULL;
    t->size_px = (uint32_t) (px > 4 ? px : 4);
    t->tabular = tab;
}

static void set_faces(zxv_desktop_t *d)
{
    uint32_t s = d->scale_q8;
    const ttf_font_t *f = d->have_font ? &d->font : NULL;
    const ttf_font_t *m = d->have_mono ? &d->mono : f;
    zui_glyph_cache_t *mc = d->have_mono ? d->cache_mono : d->cache;
    face(&d->t_title, f, d->cache, px_of(s, ZUI_T_TITLE), false);
    face(&d->t_body, f, d->cache, px_of(s, ZUI_T_BODY), false);
    face(&d->t_caption, f, d->cache, px_of(s, ZUI_T_CAPTION), false);
    face(&d->t_mark, f, d->cache, px_of(s, ZUI_T_HEADLINE), false);
    face(&d->t_label, f, d->cache, px_of(s, ZUI_T_LABEL), false);
    /* the window manager's title bars are pixels, not points (zui_wm.h) */
    int32_t wmpx = px_of(s, ZUI_T_BODY);
    if (wmpx > (ZUI_WM_TITLE * 9) / 20) wmpx = (ZUI_WM_TITLE * 9) / 20;
    face(&d->t_wm, f, d->cache, wmpx, false);
    memset(&d->theme, 0, sizeof d->theme);
    face(&d->theme.body, f, d->cache, px_of(s, ZUI_T_BODY), false);
    face(&d->theme.label, f, d->cache, px_of(s, ZUI_T_LABEL), false);
    face(&d->theme.title, f, d->cache, px_of(s, ZUI_T_TITLE), false);
    face(&d->theme.mono, m, mc, px_of(s, ZUI_T_BODY - 1), true);
}

static void desk_faces(zxv_desktop_t *d)
{
    d->desk.f.title = &d->t_title;
    d->desk.f.body = &d->t_body;
    d->desk.f.caption = &d->t_caption;
    d->desk.f.mark = &d->t_mark;
    d->desk.wm.text = &d->t_wm;
}

static const char *pick_font(const zxv_desktop_cfg_t *cfg)
{
    if (cfg->font && cfg->font[0]) return cfg->font;
    const char *e = getenv("ZXV_FONT");
    if (e && e[0]) return e;
    return NULL;
}

static void open_fonts(zxv_desktop_t *d, const zxv_desktop_cfg_t *cfg)
{
    static char path[PATH_CAP], mpath[PATH_CAP];
    const char *fp = pick_font(cfg);
    if (fp) {
        d->have_font = load_font(fp, &d->font, &d->font_data);
        snprintf(path, sizeof path, "%s", fp);
    } else {
        const char *dirs[] = {ZXV_FONT_DIR, "assets/fonts/ascw-golden", "fonts", "."};
        for (uint32_t i = 0; i < sizeof dirs / sizeof dirs[0] && !d->have_font; i++) {
            snprintf(path, sizeof path, "%s/%s", dirs[i], FONT_MAIN);
            d->have_font = load_font(path, &d->font, &d->font_data);
        }
    }
    const char *mp = cfg->mono && cfg->mono[0] ? cfg->mono : getenv("ZXV_MONO");
    if (mp && mp[0]) {
        d->have_mono = load_font(mp, &d->mono, &d->mono_data);
    } else if (d->have_font) {
        /* the mono face beside the main one, when the bundle has it */
        snprintf(mpath, sizeof mpath, "%s", path);
        char *slash = strrchr(mpath, '/');
        size_t at = slash ? (size_t) (slash - mpath) + 1 : 0;
        if (at + sizeof FONT_MONO < sizeof mpath) {
            memcpy(mpath + at, FONT_MONO, sizeof FONT_MONO);
            d->have_mono = load_font(mpath, &d->mono, &d->mono_data);
        }
    }
}

/* ---------------------------------------------------------------- HD6 the catalogue */

static bool load_apps(zxv_desktop_t *d, const char *path)
{
    uint32_t n = 0;
    uint8_t *b = read_file(path, &n);
    if (!b) return false;
    if (zui_apps_bind(&d->apps, b, n) != 0) {
        free(b);
        return false;
    }
    d->apps_data = b;
    return true;
}

/* cfg.apps, $ZXV_APPS, the repo's pack, then beside the font (a bundle) */
static void open_apps(zxv_desktop_t *d, const zxv_desktop_cfg_t *cfg, const char *font)
{
    char path[PATH_CAP];
    const char *e = cfg->apps && cfg->apps[0] ? cfg->apps : getenv("ZXV_APPS");
    if (e && e[0]) {
        d->have_apps = load_apps(d, e);
        return;
    }
    const char *dirs[] = {ZXV_APPS_DIR, "assets/icons/apps"};
    for (uint32_t i = 0; i < 2 && !d->have_apps; i++) {
        snprintf(path, sizeof path, "%s/%s", dirs[i], APPS_PACK);
        d->have_apps = load_apps(d, path);
    }
    const char *slash = font ? strrchr(font, '/') : NULL;
    int at = slash ? (int) (slash - font) + 1 : 0;
    if (!d->have_apps && font) {
        snprintf(path, sizeof path, "%.*s%s", at, font, APPS_PACK);
        d->have_apps = load_apps(d, path);
    }
    if (!d->have_apps && font) {
        snprintf(path, sizeof path, "%.*s../Icons/%s", at, font, APPS_PACK);
        d->have_apps = load_apps(d, path);
    }
}

static void lib_faces(zxv_desktop_t *d)
{
    d->lib.f.headline = &d->t_mark;
    d->lib.f.title = &d->t_title;
    d->lib.f.body = &d->t_body;
    d->lib.f.label = &d->t_label;
    d->lib.f.caption = &d->t_caption;
}

/* the library and the rail's icons, once the desk has its apps */
static void library_init(zxv_desktop_t *d)
{
    if (!d->have_apps) return;
    d->vw = (zui_vec_work_t *) malloc(sizeof *d->vw);
    d->lib_px = (uint32_t *) malloc((size_t) LIB_SLOTS * LIB_SIDE * LIB_SIDE * 4u);
    if (!d->vw) {
        d->have_apps = false;
        free(d->lib_px);
        d->lib_px = NULL;
        return;
    }
    zui_applib_cache_init(&d->lib_cache, d->lib_px, d->lib_px ? LIB_SLOTS : 0u, LIB_SIDE);
    zui_applib_init(&d->lib, &d->apps, d->vw, &d->lib_cache, NULL, d->scale_q8);
    lib_faces(d);
    zui_desk_use_work(&d->desk, d->vw);
    for (uint32_t i = 0; i < APP_N; i++) {
        zui_app_t app;
        int32_t k = zui_apps_find(&d->apps, APPS[i].name);
        if (k >= 0 && zui_apps_get(&d->apps, (uint32_t) k, &app))
            zui_desk_icon(&d->desk, i, app.icon, app.icon_len);
    }
}

/* HD7: one set for the desk, its windows, the library, the cards and Spaces */
void zxv_desktop_set_scheme(zxv_desktop_t *d, zui_scheme_t scheme)
{
    if (!d) return;
    if ((uint32_t) scheme >= (uint32_t) ZUI_SCHEME_COUNT) scheme = ZUI_SCHEME_DARK;
    d->scheme = scheme;
    d->colors = zui_scheme_colors(scheme);
    zui_desk_set_colors(&d->desk, d->colors);
    d->lib.f.colors = d->colors;
    for (uint32_t i = 0; i < APP_N; i++)
        if (d->win[i]) {
            zui_ui_set_colors(&d->win[i]->ui, d->colors);
            if (d->win[i]->pw > 0)
                zui_damage_all(&d->win[i]->ui.damage, d->win[i]->pw, d->win[i]->ph);
        }
    if (d->have_apps) /* the icon cache was drawn on the old ground */
        zui_applib_cache_init(&d->lib_cache, d->lib_px, d->lib_px ? LIB_SLOTS : 0u, LIB_SIDE);
    d->dirty = true;
}

zui_scheme_t zxv_desktop_scheme(const zxv_desktop_t *d)
{
    return d ? d->scheme : ZUI_SCHEME_DARK;
}

zui_applib_t *zxv_desktop_library(zxv_desktop_t *d)
{
    return d && d->have_apps ? &d->lib : NULL;
}

bool zxv_desktop_library_open(const zxv_desktop_t *d)
{
    return d && d->desk.library;
}

/* ---------------------------------------------------------------- app windows */

static void on_action(zui_ui_t *ui, zui_id_t w, zui_action_t a, int32_t arg, void *ctx)
{
    (void) ui;
    appwin_t *aw = (appwin_t *) ctx;
    switch (aw->app) {
    case APP_BURROW:
        burrow_view_action(&aw->v.burrow, w, a, arg);
        break;
    case APP_QUILLON:
        quillon_view_action(&aw->v.quillon, w, a, arg);
        break;
    case APP_GLINT:
        glint_view_action(&aw->v.glint, w, a, arg);
        break;
    case APP_SPINDLE:
        spindle_view_action(&aw->v.spindle, w, a, arg);
        break;
    case APP_MURMURATION:
        murm_view_action(&aw->v.murm, w, a, arg);
        break;
    default:
        break;
    }
    aw->d->dirty = true;
}

/* HD2: the window's own surface, the size of its content */
static bool fit(appwin_t *aw, int32_t w, int32_t h)
{
    if (w <= 0 || h <= 0) return false;
    if (aw->px && aw->pw == w && aw->ph == h) return true;
    uint32_t *px = (uint32_t *) malloc((size_t) w * (size_t) h * 4u);
    if (!px) return false;
    free(aw->px);
    aw->px = px;
    aw->pw = w;
    aw->ph = h;
    zui_surface_init(&aw->surf, px, w, h, w);
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = ZUI_EV_RESIZE;
    e.x = w;
    e.y = h;
    e.ms = aw->d->now;
    zui_ui_event(&aw->ui, &e); /* a new plan; the layout follows at paint */
    zui_damage_all(&aw->ui.damage, w, h);
    return true;
}

static void blit(zui_surface_t *s, zui_rect_t c, const zui_surface_t *src)
{
    zui_rect_t k = s->clip;
    int32_t x0 = c.x > k.x ? c.x : k.x, y0 = c.y > k.y ? c.y : k.y;
    int32_t x1 = c.x + c.w < k.x + k.w ? c.x + c.w : k.x + k.w;
    int32_t y1 = c.y + c.h < k.y + k.h ? c.y + c.h : k.y + k.h;
    if (x1 > c.x + src->w) x1 = c.x + src->w;
    if (y1 > c.y + src->h) y1 = c.y + src->h;
    if (x1 <= x0 || y1 <= y0) return;
    for (int32_t y = y0; y < y1; y++)
        memcpy(s->px + (size_t) y * (size_t) s->stride + (size_t) x0,
               src->px + (size_t) (y - c.y) * (size_t) src->stride + (size_t) (x0 - c.x),
               (size_t) (x1 - x0) * 4u);
}

static void win_paint(void *ctx, zui_surface_t *s, zui_rect_t c, uint32_t tick)
{
    (void) tick;
    appwin_t *aw = (appwin_t *) ctx;
    if (aw->dead || !fit(aw, c.w, c.h)) return;
    zui_ui_paint(&aw->ui, &aw->surf);
    /* the whole window is copied out below, so the damage is spent here:
     * what is left afterwards is new (damaged()) */
    zui_damage_clear(&aw->ui.damage);
    blit(s, c, &aw->surf);
}

static bool win_event(void *ctx, const zui_event_t *e, zui_rect_t c)
{
    appwin_t *aw = (appwin_t *) ctx;
    if (aw->dead || !fit(aw, c.w, c.h)) return false;
    aw->d->dirty = true;
    return zui_ui_event(&aw->ui, e);
}

static void win_closed(void *ctx)
{
    appwin_t *aw = (appwin_t *) ctx;
    zxv_desktop_t *d = aw->d;
    if (aw->dead) return;
    aw->dead = true;
    if (aw->app < APP_N && d->win[aw->app] == aw) d->win[aw->app] = NULL;
    aw->next_dead = d->dead;
    d->dead = aw;
    d->dirty = true;
}

static void bury(zxv_desktop_t *d)
{
    while (d->dead) {
        appwin_t *aw = d->dead;
        d->dead = aw->next_dead;
        free(aw->px);
        free(aw);
    }
}

static void burrow_opens(void *ctx, uint8_t kind, const char *space, const char *path)
{
    zxv_desktop_t *d = (zxv_desktop_t *) ctx;
    pending_open_t *p = &d->pending;
    p->want = true;
    p->kind = kind;
    snprintf(p->space, sizeof p->space, "%s", space ? space : "");
    snprintf(p->path, sizeof p->path, "%s", path ? path : "");
}

/* Load the app's model and build its view; feeds the launch its progress. */
static void load_app(zxv_desktop_t *d, appwin_t *aw)
{
    zui_wm_t *wm = &d->desk.wm;
    zxm_link_t *l = &d->link;
    int rc = ZXM_OK;
    zui_wm_progress(wm, aw->id, 32);
    switch (aw->app) {
    case APP_BURROW:
        rc = burrow_init(&d->burrow, l);
        zui_wm_progress(wm, aw->id, 160);
        burrow_view_build(&aw->v.burrow, &aw->ui, &d->burrow);
        aw->v.burrow.open_fn = burrow_opens;
        aw->v.burrow.open_ctx = d;
        zv_shell_result(&aw->v.burrow.sh, rc, l);
        break;
    case APP_QUILLON:
        quillon_new(&d->quillon, l);
        zui_wm_progress(wm, aw->id, 160);
        quillon_view_build(&aw->v.quillon, &aw->ui, &d->quillon);
        break;
    case APP_GLINT:
        rc = burrow_init(&d->glint_dir, l);
        glint_scan(&d->glint, &d->glint_dir);
        zui_wm_progress(wm, aw->id, 160);
        glint_view_build(&aw->v.glint, &aw->ui, &d->glint, d->glint_buf,
                         d->glint_buf ? GLINT_BUF : 0);
        zv_shell_result(&aw->v.glint.sh, rc, l);
        break;
    case APP_SPINDLE:
        spindle_init(&d->spindle, l);
        rc = spindle_discover(&d->spindle);
        zui_wm_progress(wm, aw->id, 160);
        spindle_view_build(&aw->v.spindle, &aw->ui, &d->spindle);
        zv_shell_result(&aw->v.spindle.sh, rc, l);
        break;
    case APP_MURMURATION:
        murm_init(&d->murm, l);
        rc = murm_refresh(&d->murm);
        zui_wm_progress(wm, aw->id, 160);
        murm_view_build(&aw->v.murm, &aw->ui, &d->murm);
        zv_shell_result(&aw->v.murm.sh, rc, l);
        break;
    default:
        break;
    }
    zui_wm_progress(wm, aw->id, 256);
    d->dirty = true;
}

uint32_t zxv_desktop_launch(zxv_desktop_t *d, uint32_t app)
{
    if (!d || app >= APP_N) return 0;
    d->desk.library = false;
    if (d->spaces) {
        d->spaces = false;
        d->desk.spaces = false;
    }
    d->dirty = true;
    if (d->win[app]) return zui_desk_open(&d->desk, app, APPS[app].name, NULL);
    appwin_t *aw = (appwin_t *) calloc(1, sizeof *aw);
    if (!aw) return 0;
    aw->d = d;
    aw->app = app;
    zui_wm_client_t c = {win_paint, win_event, win_closed, aw};
    uint32_t id = zui_desk_open(&d->desk, app, APPS[app].name, &c);
    zui_wm_window_t *w = id ? zui_wm_get(&d->desk.wm, id) : NULL;
    if (!w) {
        free(aw);
        return 0;
    }
    aw->id = id;
    d->win[app] = aw;
    zui_rect_t r = zui_wm_content(&d->desk.wm, w);
    zui_screen_t sc = {(uint32_t) (r.w > 1 ? r.w : 1), (uint32_t) (r.h > 1 ? r.h : 1), d->scale_q8,
                       d->desk.wm.touch ? ZUI_INPUT_TOUCH : ZUI_INPUT_POINTER};
    zui_ui_init(&aw->ui, &sc, &d->theme);
    zui_ui_set_colors(&aw->ui, d->colors);
    zui_ui_on(&aw->ui, on_action, aw);
    aw->ui.clip_set = d->clip_set;
    aw->ui.clip_get = d->clip_get;
    aw->ui.clip_ctx = d->clip_ctx;
    load_app(d, aw);
    return id;
}

/* ---------------------------------------------------------------- HD6 cards */

static void card_paint(void *ctx, zui_surface_t *s, zui_rect_t c, uint32_t tick)
{
    card_t *k = (card_t *) ctx;
    zui_app_t app;
    (void) tick;
    if (zui_apps_get(&k->d->apps, (uint32_t) k->app, &app))
        zui_applib_card(s, k->d->vw, &k->d->lib.f, k->d->scale_q8, &app, false, c);
}

static void card_closed(void *ctx)
{
    card_t *k = (card_t *) ctx;
    k->id = 0;
    k->d->dirty = true;
}

static uint32_t open_card(zxv_desktop_t *d, int32_t idx)
{
    zui_app_t app;
    if (!d->have_apps || !zui_apps_get(&d->apps, (uint32_t) idx, &app)) return 0;
    for (uint32_t i = 0; i < CARDS; i++)
        if (d->card[i].id && d->card[i].app == idx) {
            zui_wm_focus(&d->desk.wm, d->card[i].id);
            return d->card[i].id;
        }
    card_t *k = NULL;
    for (uint32_t i = 0; i < CARDS && !k; i++)
        if (!d->card[i].id) k = &d->card[i];
    if (!k) { /* every card is open: the oldest one gives way */
        k = &d->card[0];
        for (uint32_t i = 1; i < CARDS; i++)
            if (d->card[i].age < k->age) k = &d->card[i];
        zui_wm_close(&d->desk.wm, k->id);
    }
    zui_wm_client_t c = {card_paint, NULL, card_closed, k};
    k->d = d;
    k->app = idx;
    k->age = ++d->card_age;
    k->id = zui_wm_open(&d->desk.wm, app.name, app.name, NULL, &c);
    if (!k->id) {
        zxv_desktop_note(d, "Too many windows are open; close one to see that card");
        return 0;
    }
    zui_wm_progress(&d->desk.wm, k->id, 256u);
    /* the window ends under the card's last line: title bar and border
     * (frame minus content) plus what the card needs at that width,
     * centred in the room above the window strip */
    zui_rect_t a = zui_wm_work(&d->desk.wm);
    int32_t w = px_of(d->scale_q8, 520), h = a.h;
    if (w > a.w) w = a.w;
    zui_wm_move(&d->desk.wm, k->id, (zui_rect_t){a.x, a.y, w, h});
    zui_wm_window_t *win = zui_wm_get(&d->desk.wm, k->id);
    if (win) {
        zui_rect_t c = zui_wm_content(&d->desk.wm, win);
        h = (win->r.h - c.h) + zui_applib_card_height(&d->lib.f, d->scale_q8, &app, c.w);
        if (h > a.h) h = a.h;
    }
    zui_wm_move(&d->desk.wm, k->id, (zui_rect_t){a.x + (a.w - w) / 2, a.y + (a.h - h) / 2, w, h});
    d->dirty = true;
    return k->id;
}

/* catalogue app i: its window when it is on the rail, else its card */
uint32_t zxv_desktop_choose(zxv_desktop_t *d, uint32_t i)
{
    zui_app_t app;
    if (!d || !d->have_apps || !zui_apps_get(&d->apps, i, &app)) return 0;
    d->desk.library = false;
    for (uint32_t r = 0; r < APP_N; r++)
        if (!strcmp(APPS[r].name, app.name)) return zxv_desktop_launch(d, r);
    return open_card(d, (int32_t) i);
}

static void library_show(zxv_desktop_t *d, bool on)
{
    if (on && !d->have_apps) {
        zxv_desktop_note(d, "The app catalogue is missing (assets/icons/apps/zxv-apps.zxap)");
        on = false;
    }
    if (on) {
        d->spaces = d->desk.spaces = false;
        zui_applib_set_query(&d->lib, ""); /* each visit starts fresh, at the top */
    }
    d->desk.library = on;
    d->dirty = true;
}

/* what the library asked for after an event */
static void library_act(zxv_desktop_t *d)
{
    if (d->lib.act == ZUI_APPLIB_CLOSE) library_show(d, false);
    if (d->lib.act == ZUI_APPLIB_OPEN && d->lib.act_app >= 0)
        zxv_desktop_choose(d, (uint32_t) d->lib.act_app);
}

/* Burrow's Open: text to Quillon, pictures, sound and video to Glint. */
static void do_pending(zxv_desktop_t *d)
{
    pending_open_t p = d->pending;
    d->pending.want = false;
    if (!p.want) return;
    if (p.kind == BURROW_K_TEXT) {
        if (!zxv_desktop_launch(d, APP_QUILLON) || !d->win[APP_QUILLON]) return;
        quillon_view_t *v = &d->win[APP_QUILLON]->v.quillon;
        int rc = quillon_open(&d->quillon, p.space, p.path);
        quillon_view_sync(v);
        zv_shell_result(&v->sh, rc, &d->link);
    } else if (p.kind == BURROW_K_IMAGE || p.kind == BURROW_K_AUDIO || p.kind == BURROW_K_VIDEO) {
        if (!zxv_desktop_launch(d, APP_GLINT) || !d->win[APP_GLINT]) return;
        glint_view_t *v = &d->win[APP_GLINT]->v.glint;
        char dir[BURROW_PATH_MAX + 1];
        snprintf(dir, sizeof dir, "%s", p.path);
        char *slash = strrchr(dir, '/');
        const char *name = slash ? slash + 1 : p.path;
        if (slash)
            *slash = 0;
        else
            dir[0] = 0;
        int rc = burrow_set_space(&d->glint_dir, p.space);
        if (rc == ZXM_OK) rc = burrow_open(&d->glint_dir, dir);
        glint_scan(&d->glint, &d->glint_dir);
        for (uint32_t i = 0; rc == ZXM_OK && i < d->glint.n; i++)
            if (!strcmp(d->glint.item[i].name, name)) {
                rc = glint_view(&d->glint, (int32_t) i);
                break;
            }
        glint_view_sync(v);
        zv_shell_result(&v->sh, rc, &d->link);
    }
    d->dirty = true;
}

zui_ui_t *zxv_desktop_app_ui(zxv_desktop_t *d, uint32_t app)
{
    return d && app < APP_N && d->win[app] ? &d->win[app]->ui : NULL;
}

uint32_t zxv_desktop_apps(const zxv_desktop_t *d)
{
    return d ? d->desk.napps : 0;
}

const char *zxv_desktop_app_name(const zxv_desktop_t *d, uint32_t app)
{
    return d && app < d->desk.napps ? d->desk.app[app].name : NULL;
}

/* ---------------------------------------------------------------- Spaces */

zui_rect_t zxv_desktop_space_card(const zxv_desktop_t *d, uint32_t i)
{
    zui_rect_t none = {0, 0, 0, 0};
    if (!d || !d->spaces || i >= SPACES) return none;
    zui_rect_t a = zui_desk_area(&d->desk);
    int32_t g = px_of(d->scale_q8, ZUI_S5), head = px_of(d->scale_q8, 72);
    int32_t cols = a.w >= px_of(d->scale_q8, 5 * 150) + 6 * g   ? 5
                   : a.w >= px_of(d->scale_q8, 3 * 150) + 4 * g ? 3
                                                                : 2;
    int32_t cw = (a.w - (cols + 1) * g) / cols, maxw = px_of(d->scale_q8, 200);
    if (cw > maxw) cw = maxw;
    if (cw < 1) return none;
    int32_t ch = (cw * 3) / 4;
    int32_t rowlen = (int32_t) SPACES < cols ? (int32_t) SPACES : cols;
    int32_t col = (int32_t) i % cols, row = (int32_t) i / cols;
    int32_t inrow = row == ((int32_t) SPACES - 1) / cols ? (int32_t) SPACES - row * cols : rowlen;
    int32_t span = inrow * cw + (inrow - 1) * g;
    int32_t x = a.x + (a.w - span) / 2 + col * (cw + g);
    int32_t y = a.y + head + row * (ch + g);
    return (zui_rect_t){x, y, cw, ch};
}

static int32_t card_at(const zxv_desktop_t *d, int32_t x, int32_t y)
{
    for (uint32_t i = 0; i < SPACES; i++) {
        zui_rect_t r = zxv_desktop_space_card(d, i);
        if (r.w && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h) return (int32_t) i;
    }
    return -1;
}

static void text_in(zui_surface_t *s, const zui_text_t *t, zui_rect_t r, int32_t base, uint32_t rgb,
                    const char *str)
{
    if (!t->font) return;
    int32_t w = zui_text_width(t, str);
    zui_text_fit(s, t, w < r.w ? r.x + (r.w - w) / 2 : r.x, base, r.w, rgb, str);
}

/* The space's sign, vector only: Home a roof, Office a desk, Team three
 * heads, Cloud top a rising arc, Park two trees. */
static void sign(zui_surface_t *s, uint32_t i, int32_t cx, int32_t cy, int32_t u, uint32_t rgb)
{
    int32_t k = u / 6 > 1 ? u / 6 : 1;
    switch (i) {
    case 0: /* a roof over a door */
        for (int32_t t = 0; t < u; t++) {
            zui_fill(s, (zui_rect_t){cx - t, cy - u + t, k, k}, rgb);
            zui_fill(s, (zui_rect_t){cx + t, cy - u + t, k, k}, rgb);
        }
        zui_stroke_rrect(s, (zui_rect_t){cx - u * 2 / 3, cy, u * 4 / 3, u * 2 / 3}, 0, k, rgb, 255);
        break;
    case 1: /* a desk and a screen */
        zui_stroke_rrect(s, (zui_rect_t){cx - u * 2 / 3, cy - u, u * 4 / 3, u}, k, k, rgb, 255);
        zui_hline(s, cx - u, cy + u / 3, 2 * u, k, rgb);
        break;
    case 2: /* three heads */
        for (int32_t j = -1; j <= 1; j++) {
            int32_t r = u / 3;
            zui_stroke_rrect(s, (zui_rect_t){cx + j * u * 2 / 3 - r, cy - u / 2 - r, 2 * r, 2 * r},
                             r, k, rgb, 255);
            zui_fill_rrect(s, (zui_rect_t){cx + j * u * 2 / 3 - r, cy + r / 2, 2 * r, r}, r / 2,
                           rgb, 255);
        }
        break;
    case 3: /* a cloud */
        zui_fill_rrect(s, (zui_rect_t){cx - u, cy - u / 3, 2 * u, u * 2 / 3}, u / 3, rgb, 255);
        zui_fill_rrect(s, (zui_rect_t){cx - u / 2, cy - u * 3 / 4, u, u}, u / 2, rgb, 255);
        break;
    default: /* two trees */
        for (int32_t j = -1; j <= 1; j += 2) {
            int32_t tx = cx + j * u / 2;
            zui_fill_rrect(s, (zui_rect_t){tx - u / 3, cy - u, u * 2 / 3, u}, u / 3, rgb, 255);
            zui_vline(s, tx - k / 2, cy - u / 4, u * 3 / 4, k, rgb);
        }
        break;
    }
}

static void draw_spaces(zxv_desktop_t *d, zui_surface_t *s)
{
    const zui_colors_t *cs = d->colors;
    zui_rect_t a = zui_desk_area(&d->desk);
    zui_unclip(s);
    zui_clip(s, a);
    zui_fill(s, a, C(VOID));
    if (d->t_mark.font)
        zui_text(s, &d->t_mark, a.x + px_of(d->scale_q8, ZUI_S5), a.y + px_of(d->scale_q8, 44),
                 C(TEXT), "Spaces");
    int32_t rad = px_of(d->scale_q8, ZUI_R_PANEL);
    for (uint32_t i = 0; i < SPACES; i++) {
        zui_rect_t r = zxv_desktop_space_card(d, i);
        if (!r.w) continue;
        bool on = d->space == i, hot = d->space_press == (int32_t) i;
        zui_fill_rrect(s, r, rad, hot ? C(RAISED) : C(PANEL), 255);
        zui_engrave(s, r, rad, C(ENGRAVE_LO), C(ENGRAVE_HI), 70);
        if (on) {
            int32_t g = px_of(d->scale_q8, 2);
            zui_stroke_rrect(s, (zui_rect_t){r.x - g, r.y - g, r.w + 2 * g, r.h + 2 * g}, rad + g,
                             g, C(GOLD), 255);
        }
        sign(s, i, r.x + r.w / 2, r.y + r.h * 2 / 5, r.h / 6, on ? C(GOLD) : C(TEXT_2));
        text_in(s, &d->t_body, (zui_rect_t){r.x + rad, r.y, r.w - 2 * rad, r.h},
                r.y + r.h - px_of(d->scale_q8, 18), on ? C(TEXT) : C(TEXT_2), SPACE_NAME[i]);
    }
    zui_unclip(s);
}

static void spaces_event(zxv_desktop_t *d, const zui_event_t *e)
{
    if (e->type == ZUI_EV_KEY_DOWN && e->key == ZUI_KEY_ESCAPE) {
        d->spaces = d->desk.spaces = false;
        d->dirty = true;
        return;
    }
    if (e->type == ZUI_EV_PTR_DOWN && e->button == ZUI_BTN_LEFT) {
        d->space_press = card_at(d, e->x, e->y);
        d->dirty = true;
    } else if (e->type == ZUI_EV_PTR_UP) {
        int32_t c = card_at(d, e->x, e->y);
        if (c >= 0 && c == d->space_press) {
            d->space = (uint32_t) c;
            d->spaces = d->desk.spaces = false;
            status(d);
        }
        d->space_press = -1;
        d->dirty = true;
    }
}

uint32_t zxv_desktop_space(const zxv_desktop_t *d)
{
    return d ? d->space : 0;
}

bool zxv_desktop_in_spaces(const zxv_desktop_t *d)
{
    return d && d->spaces;
}

/* ---------------------------------------------------------------- the loop */

static bool alloc_surface(zxv_desktop_t *d, int32_t w, int32_t h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    uint32_t *px = (uint32_t *) malloc((size_t) w * (size_t) h * 4u);
    if (!px) return false;
    free(d->px);
    d->px = px;
    d->w = w;
    d->h = h;
    zui_surface_init(&d->surf, px, w, h, w);
    return true;
}

static void resized(zxv_desktop_t *d, int32_t w, int32_t h, uint32_t scale_q8)
{
    bool rescale = scale_q8 && scale_q8 != d->scale_q8;
    if ((w != d->w || h != d->h) && !alloc_surface(d, w, h)) return;
    if (rescale) {
        d->scale_q8 = scale_q8;
        set_faces(d);
        for (uint32_t i = 0; i < APP_N; i++) {
            appwin_t *aw = d->win[i];
            if (!aw) continue;
            aw->ui.theme = d->theme;
            zui_event_t e;
            memset(&e, 0, sizeof e);
            e.type = ZUI_EV_SCALE;
            e.x = (int32_t) scale_q8;
            zui_ui_event(&aw->ui, &e);
            if (aw->pw > 0) zui_damage_all(&aw->ui.damage, aw->pw, aw->ph);
        }
    }
    zui_desk_resize(&d->desk, d->w, d->h, d->scale_q8);
    d->dirty = true;
}

static bool in_rect(zui_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static bool damaged(const zxv_desktop_t *d)
{
    for (uint32_t i = 0; i < APP_N; i++)
        if (d->win[i] && d->win[i]->ui.damage.n) return true;
    return false;
}

/* One event; false when it asks the desktop to close. */
static bool handle(zxv_desktop_t *d, const zui_event_t *e)
{
    switch (e->type) {
    case ZUI_EV_CLOSE:
        return false;
    case ZUI_EV_RESIZE:
        resized(d, e->x, e->y, 0);
        return true;
    case ZUI_EV_SCALE:
        resized(d, d->w, d->h, (uint32_t) e->x);
        return true;
    case ZUI_EV_TICK:
        return true;
    default:
        break;
    }
    bool move = e->type == ZUI_EV_PTR_MOVE;
    if (!move) d->dirty = true;
    bool ptr = e->type == ZUI_EV_PTR_DOWN || e->type == ZUI_EV_PTR_MOVE ||
               e->type == ZUI_EV_PTR_UP || e->type == ZUI_EV_SCROLL;
    if (d->spaces &&
        ((ptr && in_rect(zui_desk_area(&d->desk), e->x, e->y)) || e->type == ZUI_EV_KEY_DOWN)) {
        spaces_event(d, e);
        return true;
    }
    if (d->desk.library) { /* HD6: the library has the area and the keyboard */
        bool key =
            (e->type == ZUI_EV_KEY_DOWN || e->type == ZUI_EV_TEXT) && !(e->mods & ZUI_MOD_CMD);
        bool inside = ptr && in_rect(zui_desk_area(&d->desk), e->x, e->y);
        if (key || inside || e->type == ZUI_EV_PTR_UP) {
            bool lused = zui_applib_event(&d->lib, e);
            if (lused || !move) d->dirty = true;
            library_act(d);
            if (key || inside) return true;
        }
    }
    int32_t hover = d->desk.hover;
    bool used = zui_desk_event(&d->desk, e);
    /* a move redraws only when something under it changed */
    if (move && (used || hover != d->desk.hover || damaged(d))) d->dirty = true;
    switch (d->desk.act) {
    case ZUI_DESK_LAUNCH:
        if (d->desk.act_app >= 0) zxv_desktop_launch(d, (uint32_t) d->desk.act_app);
        break;
    case ZUI_DESK_LIBRARY:
        library_show(d, !d->desk.library);
        break;
    case ZUI_DESK_SPACES:
        d->desk.library = false;
        d->spaces = d->desk.spaces = true;
        d->space_press = -1;
        break;
    case ZUI_DESK_HOME:
        d->spaces = d->desk.spaces = false;
        break;
    default:
        break;
    }
    do_pending(d);
    return true;
}

static bool animating(const zxv_desktop_t *d)
{
    for (uint32_t i = 0; i < ZUI_WM_MAX; i++) {
        const zui_wm_window_t *w = &d->desk.wm.win[i];
        if (w->id && w->phase != ZUI_WIN_OPEN && !w->minimised) return true;
    }
    return false;
}

static uint32_t clock_ms(zxv_desktop_t *d)
{
    if (d->headless) return d->fake_ms;
    return d->ops->now_ms(&d->p);
}

int zxv_desktop_step(zxv_desktop_t *d, uint32_t wait_ms)
{
    if (!d) return -1;
    int rc = 1;
    if (!d->headless) {
        int n = d->ops->poll(&d->p, &d->q, animating(d) ? 0 : wait_ms);
        if (n < 0) rc = -1;
        if (n > 0 && (d->p.screen.px_w != (uint32_t) d->w || d->p.screen.px_h != (uint32_t) d->h ||
                      d->p.screen.scale_q8 != d->scale_q8))
            resized(d, (int32_t) d->p.screen.px_w, (int32_t) d->p.screen.px_h,
                    d->p.screen.scale_q8);
    } else {
        d->fake_ms += 16;
    }
    d->now = clock_ms(d);
    zui_event_t e;
    while (rc == 1 && d->fed_tail != d->fed_head) {
        e = d->fed[d->fed_tail++ % EVQ];
        if (!e.ms) e.ms = d->now;
        if (!handle(d, &e)) rc = 0;
    }
    while (rc == 1 && zui_pop(&d->q, &e))
        if (!handle(d, &e)) rc = 0;
    bury(d);
    if (rc != 1) return rc;

    if (!d->probed || d->now - d->last_probe >= PROBE_EVERY_MS) probe(d);
    status(d);
    uint32_t dt = d->now - d->last_tick;
    d->last_tick = d->now;
    if (dt > 100u) dt = 100u; /* a stall does not skip the doors */
    bool anim = animating(d);
    zui_desk_tick(&d->desk, dt);
    if (anim) d->dirty = true;
    if (!d->dirty) return 1;
    d->dirty = false;
    d->tick++;
    zui_desk_draw(&d->desk, &d->surf, d->now);
    if (d->spaces) draw_spaces(d, &d->surf);
    if (d->desk.library) {
        zui_applib_place(&d->lib, zui_desk_area(&d->desk), d->scale_q8);
        zui_applib_draw(&d->lib, &d->surf);
    }
    if (!d->headless) {
        zui_damage_t dmg;
        zui_damage_clear(&dmg);
        zui_damage_all(&dmg, d->w, d->h);
        int pr = d->ops->present(&d->p, &d->surf, &dmg);
        if (pr == ZUI_PRESENT_E_LOST) return -1;
    }
    return 1;
}

void zxv_desktop_feed(zxv_desktop_t *d, const zui_event_t *e)
{
    if (!d || !e) return;
    if (d->fed_head - d->fed_tail >= EVQ) d->fed_tail++;
    d->fed[d->fed_head++ % EVQ] = *e;
}

const zui_surface_t *zxv_desktop_surface(const zxv_desktop_t *d)
{
    return d ? &d->surf : NULL;
}

zui_desk_t *zxv_desktop_desk(zxv_desktop_t *d)
{
    return d ? &d->desk : NULL;
}

zui_present_t *zxv_desktop_present(zxv_desktop_t *d)
{
    return d && !d->headless ? &d->p : NULL;
}

/* ---------------------------------------------------------------- open / close */

static bool open_present(zxv_desktop_t *d, const zxv_desktop_cfg_t *cfg, const char **why)
{
    const char *title = cfg->title ? cfg->title : "ZXV";
    int32_t w = cfg->w_pt > 0 ? cfg->w_pt : 1180, h = cfg->h_pt > 0 ? cfg->h_pt : 780;
    const zui_present_ops_t *cand[8];
    uint32_t n = zui_present_candidates(cand, 8);
    if (n > 8) n = 8;
    for (uint32_t i = 0; i < n; i++) {
        if (cfg->backend && cfg->backend[0] && strcmp(cand[i]->name, cfg->backend)) continue;
        memset(&d->p, 0, sizeof d->p);
        d->p.ops = cand[i];
        if (cand[i]->open(&d->p, title, w, h) == ZUI_PRESENT_OK) {
            d->ops = cand[i];
            return true;
        }
    }
    if (why)
        *why = cfg->backend && cfg->backend[0] ? "the named present layer did not open"
                                               : "no present layer opened";
    return false;
}

zxv_desktop_t *zxv_desktop_open(const zxv_desktop_cfg_t *cfg, const char **why)
{
    static const zxv_desktop_cfg_t none;
    if (!cfg) cfg = &none;
    if (why) *why = "out of memory";
    zxv_desktop_t *d = (zxv_desktop_t *) calloc(1, sizeof *d);
    if (!d) return NULL;
    d->cache = (zui_glyph_cache_t *) malloc(sizeof *d->cache);
    d->glint_buf = (uint8_t *) malloc(GLINT_BUF);
    if (!d->cache) {
        zxv_desktop_close(d);
        return NULL;
    }
    zui_glyph_cache_init(d->cache);
    open_fonts(d, cfg);
    {
        const char *fp = pick_font(cfg);
        char fpath[PATH_CAP];
        snprintf(fpath, sizeof fpath, "%s/%s", ZXV_FONT_DIR, FONT_MAIN);
        open_apps(d, cfg, fp ? fp : fpath);
    }
    if (d->have_mono) {
        d->cache_mono = (zui_glyph_cache_t *) malloc(sizeof *d->cache_mono);
        if (d->cache_mono)
            zui_glyph_cache_init(d->cache_mono);
        else
            d->have_mono = false;
    }
    d->headless = cfg->headless;
    d->clip_set = cfg->clip_set;
    d->clip_get = cfg->clip_get;
    d->clip_ctx = cfg->clip_ctx;
    d->space_press = -1;
    zui_queue_init(&d->q);
    if (d->headless) {
        d->scale_q8 = cfg->scale_q8 ? cfg->scale_q8 : 256u;
        d->fake_ms = 1000;
        int32_t w = px_of(d->scale_q8, cfg->w_pt > 0 ? cfg->w_pt : 1180);
        int32_t h = px_of(d->scale_q8, cfg->h_pt > 0 ? cfg->h_pt : 780);
        if (!alloc_surface(d, w, h)) {
            zxv_desktop_close(d);
            return NULL;
        }
    } else {
        if (!open_present(d, cfg, why)) {
            zxv_desktop_close(d);
            return NULL;
        }
        d->scale_q8 = d->p.screen.scale_q8 ? d->p.screen.scale_q8 : 256u;
        if (!alloc_surface(d, (int32_t) d->p.screen.px_w, (int32_t) d->p.screen.px_h)) {
            zxv_desktop_close(d);
            return NULL;
        }
    }
    set_faces(d);
    zui_desk_faces_t f = {&d->t_title, &d->t_body, &d->t_caption, &d->t_mark};
    zui_desk_init(&d->desk, d->w, d->h, d->scale_q8, false, &f);
    desk_faces(d);
    for (uint32_t i = 0; i < APP_N; i++) zui_desk_add(&d->desk, APPS[i].name, APPS[i].mark);
    library_init(d);
    {
        const char *sc = cfg->scheme ? cfg->scheme : getenv("ZXV_SCHEME");
        zui_scheme_t k = ZUI_SCHEME_DARK;
        if (sc && !strcmp(sc, "light")) k = ZUI_SCHEME_LIGHT;
        if (sc && (!strcmp(sc, "high-contrast") || !strcmp(sc, "contrast")))
            k = ZUI_SCHEME_HIGH_CONTRAST;
        zxv_desktop_set_scheme(d, k);
    }
    zxm_link_init(&d->link, rpc_send, d);
    const char *rpc = cfg->rpc ? cfg->rpc : getenv("ZXV_RPC");
    snprintf(d->rpc, sizeof d->rpc, "%s", rpc ? rpc : "");
    d->now = d->last_tick = clock_ms(d);
    probe(d);
    status(d);
    d->dirty = true;
    if (why) *why = NULL;
    return d;
}

void zxv_desktop_close(zxv_desktop_t *d)
{
    if (!d) return;
    for (uint32_t i = 0; i < APP_N; i++) {
        appwin_t *aw = d->win[i];
        if (!aw) continue;
        aw->dead = true; /* the window manager goes with the desktop */
        d->win[i] = NULL;
        aw->next_dead = d->dead;
        d->dead = aw;
    }
    bury(d);
    if (d->ops) d->ops->close(&d->p);
    free(d->px);
    free(d->font_data);
    free(d->mono_data);
    free(d->cache);
    free(d->cache_mono);
    free(d->glint_buf);
    free(d->apps_data);
    free(d->vw);
    free(d->lib_px);
    free(d);
}
