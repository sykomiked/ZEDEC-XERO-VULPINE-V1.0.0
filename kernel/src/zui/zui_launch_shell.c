/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_launch_shell.c — the pictures and sounds of every app's launch screen.
 * See zui_launch_shell.h. */
#include "zui_launch_shell.h"

static const char *const LOGO_FILE[ZUI_LT_LOGOS] = {
    "36n9-gold.png", "acoto-gold.png", "ascw-gold.png", "smic-gold.png", "cis-gold.png"};

/* LS6: the same path ending .zxvg instead of .png */
static int vec_path(char *out, uint32_t cap, const char *png)
{
    uint32_t n = 0;
    while (png[n]) n++;
    if (n < 4 || n + 2 >= cap) return -1;
    for (uint32_t i = 0; i + 4 < n; i++) out[i] = png[i];
    static const char EXT[] = ".zxvg";
    for (uint32_t i = 0; i < 6; i++) out[n - 4 + i] = EXT[i];
    return 0;
}

/* LS6: read path's .zxvg into buf; its length, or 0 when there is none */
static uint32_t load_vec(zui_launch_shell_t *ls, const char *png, uint8_t *buf, uint32_t cap)
{
    const zui_ls_config_t *c = &ls->cfg;
    char path[ZUI_LS_PATH];
    if (!c->vec || !c->read || !buf || !cap || vec_path(path, sizeof path, png)) return 0;
    int32_t n = c->read(c->read_ctx, path, buf, cap);
    return n > 0 ? (uint32_t) n : 0;
}

static void zero(void *p, uint32_t n)
{
    uint8_t *b = p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

/* dir + "/" + name into out */
static int join(char *out, uint32_t cap, const char *dir, const char *name)
{
    uint32_t n = 0;
    if (dir)
        for (; *dir; dir++) {
            if (n + 1 >= cap) return -1;
            out[n++] = *dir;
        }
    if (n && out[n - 1] != '/') {
        if (n + 1 >= cap) return -1;
        out[n++] = '/';
    }
    for (; *name; name++) {
        if (n + 1 >= cap) return -1;
        out[n++] = *name;
    }
    out[n] = 0;
    return 0;
}

int zui_ls_icon_path(const char *dir, const char *app, char *out, uint32_t cap)
{
    if (!app || !out || !cap) return -1;
    const char *f = zxl_icon_file(app);
    if (f) return join(out, cap, dir, f);
    /* an app a person made: its name, lower case, letters and digits */
    char name[ZUI_WM_NAME_MAX + 8];
    uint32_t n = 0;
    for (const char *s = app; *s && n + 5 < sizeof name; s++) {
        char ch = *s;
        if (ch >= 'A' && ch <= 'Z') ch = (char) (ch - 'A' + 'a');
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) name[n++] = ch;
    }
    if (!n) return -1;
    name[n] = 0;
    if (join(name + n, sizeof name - n, 0, ".png")) return -1;
    return join(out, cap, dir, name);
}

/* read one PNG and fit it; 0 when it is drawn and ready */
static int load(zui_launch_shell_t *ls, const char *path, zui_image_t *img, uint32_t *px, int32_t m,
                uint32_t key)
{
    const zui_ls_config_t *c = &ls->cfg;
    if (!c->read || !c->file || !c->work) return -1;
    int32_t n = c->read(c->read_ctx, path, c->file, c->file_cap);
    if (n <= 0) return -1;
    return zui_image_load(img, px, m, m, c->file, (uint32_t) n, key, c->work, c->work_cap) ? -1 : 0;
}

/* LS6: a vector drawn at strength into box. It is drawn once at the box's
 * size into img (px holds cap x cap) and blended from there every frame
 * after, so a launch frame costs a blit, not a redraw; a box larger than
 * that is drawn live. -1 when the vector is malformed. */
static int vec_draw(zui_launch_shell_t *ls, zui_surface_t *s, const uint8_t *d, uint32_t n,
                    zui_rect_t box, uint32_t strength, zui_image_t *img, uint32_t *px, int32_t cap)
{
    const zui_ls_config_t *c = &ls->cfg;
    if (box.w < 1 || box.h < 1) return 0;
    if (box.w > cap || box.h > cap) return c->vec(c->vec_ctx, s, d, n, box, strength, 0);
    if (img->px != px || img->w != box.w || img->h != box.h) {
        /* drawn on black and on white: the difference is its coverage */
        uint32_t m = (uint32_t) (box.w * box.h);
        zui_surface_t k, w;
        zui_surface_init(&k, ls->bake[0], box.w, box.h, box.w);
        zui_surface_init(&w, ls->bake[1], box.w, box.h, box.w);
        for (uint32_t i = 0; i < m; i++) ls->bake[0][i] = 0, ls->bake[1][i] = 0xFFFFFFu;
        zui_rect_t all = {0, 0, box.w, box.h};
        img->px = 0;
        if (c->vec(c->vec_ctx, &k, d, n, all, 256u, 0) < 0 ||
            c->vec(c->vec_ctx, &w, d, n, all, 256u, 0) < 0)
            return -1;
        for (uint32_t i = 0; i < m; i++) {
            uint32_t b = ls->bake[0][i], h = ls->bake[1][i], a = 0, out = 0;
            for (uint32_t sh = 0; sh < 24; sh += 8) {
                int32_t cov = 255 - (int32_t) ((h >> sh) & 255u) + (int32_t) ((b >> sh) & 255u);
                if (cov > (int32_t) a) a = (uint32_t) (cov > 255 ? 255 : cov);
            }
            for (uint32_t sh = 0; sh < 24; sh += 8) {
                uint32_t v = (b >> sh) & 255u;
                out |= (v > a ? a : v) << sh;
            }
            px[i] = a << 24 | out;
        }
        img->px = px;
        img->w = box.w;
        img->h = box.h;
    }
    zui_image_draw(s, img, box, strength, ZUI_IMG_NO_KEY);
    return 0;
}

/* the app's picture: its vector if it has one, else its PNG */
static void load_icon(zui_launch_shell_t *ls, zui_ls_icon_t *e)
{
    char path[ZUI_LS_PATH];
    e->state = ZUI_LS_MISSING;
    if (zui_ls_icon_path(ls->cfg.icons_dir, e->app, path, sizeof path)) return;
    if (!e->no_vec && (e->vec_len = load_vec(ls, path, e->vec, sizeof e->vec)) != 0) {
        e->state = ZUI_LS_VECTOR;
        e->img.px = 0; /* not drawn at any size yet */
        return;
    }
    if (!load(ls, path, &e->img, e->px, ZUI_LS_ICON_PX, ZUI_IMG_KEY_GROUND))
        e->state = ZUI_LS_LOADED;
}

static zui_ls_icon_t *icon_for(zui_launch_shell_t *ls, const char *app)
{
    zui_ls_icon_t *hit = 0, *old = &ls->icon[0];
    for (uint32_t i = 0; i < ZUI_LS_ICONS; i++) {
        zui_ls_icon_t *e = &ls->icon[i];
        if (e->state != ZUI_LS_EMPTY && same(e->app, app)) hit = e;
        if (e->age < old->age) old = e;
    }
    if (hit) return hit;
    /* LS1: not kept: read it into the oldest place */
    zui_ls_icon_t *e = old;
    uint32_t i = 0;
    for (; app[i] && i + 1 < sizeof e->app; i++) e->app[i] = app[i];
    e->app[i] = 0;
    e->age = ++ls->age;
    e->no_vec = 0;
    load_icon(ls, e);
    return e;
}

/* ---- the callbacks ---------------------------------------------------------- */
static void draw_icon(void *ctx, const char *app, zxl_icon_ink_t ink, zui_surface_t *s,
                      zui_rect_t box, uint32_t lit)
{
    zui_launch_shell_t *ls = ctx;
    if (!app) return;
    zui_ls_icon_t *e = icon_for(ls, app);
    e->age = ++ls->age;
    if (lit > 256u) lit = 256u;
    uint32_t tint = ink == ZXL_ICON_PALETTE ? ZXL_GOLD_RGB : ZUI_IMG_NO_KEY;
    if (e->state == ZUI_LS_VECTOR) { /* LS6: at this box's size, as drawn */
        uint32_t at = lit < 256u ? 36u * (256u - lit) / 256u : 0u;
        if (lit > at) at = lit;
        if (vec_draw(ls, s, e->vec, e->vec_len, box, at, &e->img, e->px, ZUI_LS_ICON_PX) >= 0)
            return;
        e->no_vec = 1; /* malformed: its PNG from now on */
        load_icon(ls, e);
    }
    if (e->state != ZUI_LS_LOADED) return; /* LS5 */
    /* LS2: the faint engraving, then the icon itself as it lights */
    if (lit < 256u) zui_image_draw(s, &e->img, box, 36u * (256u - lit) / 256u, ZXL_GOLD_RGB);
    zui_image_draw(s, &e->img, box, lit, tint);
}

static void draw_logo(void *ctx, uint32_t which, zui_surface_t *s, zui_rect_t box, uint32_t lit)
{
    zui_launch_shell_t *ls = ctx;
    if (which >= ZUI_LT_LOGOS) return;
    if (ls->logo_state[which] == ZUI_LS_EMPTY) zui_ls_load_logos(ls);
    if (lit > 256u) lit = 256u;
    if (ls->logo_state[which] == ZUI_LS_VECTOR) { /* LS6 */
        if (vec_draw(ls, s, ls->logo_vec[which], ls->logo_vec_len[which], box,
                     46u + 210u * lit / 256u, &ls->logo[which], ls->logo_px[which],
                     ZUI_LS_LOGO_PX) >= 0)
            return;
        ls->logo_no_vec[which] = 1; /* malformed: its PNG from now on */
        ls->logo_state[which] = ZUI_LS_EMPTY;
        zui_ls_load_logos(ls);
    }
    if (ls->logo_state[which] != ZUI_LS_LOADED) return;
    zui_image_draw(s, &ls->logo[which], box, 46u + 210u * lit / 256u, ZUI_IMG_NO_KEY);
}

static void play(void *ctx, uint32_t window, const zxl_theme_t *theme, const zxl_event_t *ev)
{
    zui_launch_shell_t *ls = ctx;
    (void) window;
    if (ev->kind == ZXL_EV_BEGIN) { /* LS5: a new launch tries missing files again */
        for (uint32_t i = 0; i < ZUI_LS_ICONS; i++)
            if (ls->icon[i].state == ZUI_LS_MISSING) ls->icon[i].state = ZUI_LS_EMPTY;
        for (uint32_t i = 0; i < ZUI_LT_LOGOS; i++)
            if (ls->logo_state[i] == ZUI_LS_MISSING) ls->logo_state[i] = ZUI_LS_EMPTY;
    }
    zxl_player_play(&ls->player, theme, ev);
    zui_ls_pump(ls); /* start it now, not at the next frame */
}

/* ---- public --------------------------------------------------------------------- */
void zui_ls_init(zui_launch_shell_t *ls, const zui_ls_config_t *cfg)
{
    if (!ls) return;
    zero(ls, sizeof *ls);
    if (cfg) ls->cfg = *cfg;
    zxl_player_init(&ls->player);
    if (ls->cfg.audio)
        ls->stream =
            audio_create_stream(ls->cfg.audio, false, AUDIO_FMT_PCM_S16LE, ZXL_SYN_RATE, 2);
}

zui_lt_shell_t zui_ls_shell(zui_launch_shell_t *ls)
{
    zui_lt_shell_t s = {draw_icon, draw_logo, play, ls};
    return s;
}

uint32_t zui_ls_load_logos(zui_launch_shell_t *ls)
{
    uint32_t got = 0;
    if (!ls) return 0;
    for (uint32_t i = 0; i < ZUI_LT_LOGOS; i++) {
        if (ls->logo_state[i] == ZUI_LS_LOADED || ls->logo_state[i] == ZUI_LS_VECTOR) {
            got++;
            continue;
        }
        char path[ZUI_LS_PATH];
        ls->logo_state[i] = ZUI_LS_MISSING;
        if (join(path, sizeof path, ls->cfg.logos_dir, LOGO_FILE[i])) continue;
        uint32_t room = ls->cfg.vec_store_cap - ls->vec_used, n;
        if (!ls->logo_no_vec[i] && ls->cfg.vec_store && ls->vec_used < ls->cfg.vec_store_cap &&
            (n = load_vec(ls, path, ls->cfg.vec_store + ls->vec_used, room)) != 0) {
            ls->logo_vec[i] = ls->cfg.vec_store + ls->vec_used;
            ls->logo_vec_len[i] = n;
            ls->vec_used += n;
            ls->logo_state[i] = ZUI_LS_VECTOR;
            ls->logo[i].px = 0;
            got++;
            continue;
        }
        if (!load(ls, path, &ls->logo[i], ls->logo_px[i], ZUI_LS_LOGO_PX, ZUI_IMG_NO_KEY)) {
            ls->logo_state[i] = ZUI_LS_LOADED;
            got++;
        }
    }
    return got;
}

uint32_t zui_ls_render(zui_launch_shell_t *ls, int16_t *lr, uint32_t frames)
{
    return ls ? zxl_player_render(&ls->player, lr, frames) : 0;
}

uint32_t zui_ls_pump(zui_launch_shell_t *ls)
{
    if (!ls || !ls->cfg.audio || !ls->stream) return 0;
    uint32_t out = 0;
    while (zxl_player_active(&ls->player)) {
        int32_t space = audio_stream_space(ls->cfg.audio, ls->stream);
        if (space < 4) break;
        uint32_t n = (uint32_t) space / 4u;
        if (n > ZXL_SYN_BLOCK) n = ZXL_SYN_BLOCK;
        zxl_player_render(&ls->player, ls->pcm, n);
        int w = audio_write(ls->cfg.audio, ls->stream, ls->pcm, n * 4u);
        if (w <= 0) break;
        out += (uint32_t) w / 4u;
    }
    ls->frames_out += out;
    return out;
}
