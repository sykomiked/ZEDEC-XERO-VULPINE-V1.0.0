/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_mockup.c — renders the key ZXV screens with the real zui renderer.
 *
 * These are review mockups, drawn by the same code the native shell will use
 * (no browser, no design tool), at phone, laptop and large-display sizes, plus
 * the depth modes. Sample data only.
 *
 *   zui_mockup <font-dir> <out-dir>      write PPM frames
 *   zui_mockup --tokens                  print the design tokens as JSON
 *
 * <font-dir> holds IBMPlexSans-{Regular,Medium,SemiBold}.ttf and
 * IBMPlexMono-{Regular,Medium}.ttf (SIL OFL 1.1, not vendored here).
 *
 * Build: see tools/zui/README.md.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zui_tokens.h"
#include "zui_layout.h"
#include "zui_canvas.h"
#include "zui_depth.h"
#include "zui_logic.h"
#include "zui_space.h"
#include "zui_chiglet.h"
#include "zui_emote.h"
#include "zui_progress.h"
#include "zui_login.h"
#include "zui_pxl.h"

/* ---- fonts --------------------------------------------------------------- */
static ttf_font_t F_REG, F_MED, F_SEMI, F_MONO, F_MONO_MED;
static zui_glyph_cache_t CACHE;

static void load_font(ttf_font_t *f, const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(2);
    }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *b = malloc((size_t) n);
    if (!b || fread(b, 1, (size_t) n, fp) != (size_t) n) {
        fprintf(stderr, "read %s\n", path);
        exit(2);
    }
    fclose(fp);
    if (!ttf_parse(f, b, (uint32_t) n)) {
        fprintf(stderr, "bad font %s\n", path);
        exit(2);
    }
}

/* ---- render context: screen, depth, current layer ------------------------ */
typedef struct {
    zui_surface_t s;
    zui_screen_t scr;
    zui_plan_t p;
    zui_depth_t d;
    int eye;
    int32_t dx, dy, z;
    int32_t strength; /* depth slider: multiplies every layer's z */
    zui_look_t look;  /* the logic engine's look for this layer   */
    uint32_t bg;      /* ground the current layer sits on         */
    lpx_t lt;         /* the current layer's paraconsistent tag    */
    zui_plane_t *pl;  /* the depth-filter plane, when one is on    */
} ctx_t;

static int32_t PT(ctx_t *c, int32_t pt)
{
    return zui_px(&c->scr, pt);
}

static void place(ctx_t *c, int32_t z_pt)
{
    c->z = PT(c, z_pt) * (c->strength ? c->strength : 1);
    zui_layer_offset(&c->d, c->z, c->eye, &c->dx, &c->dy);
}

/* an untagged layer: plain, at depth z */
static void layer(ctx_t *c, int32_t z_pt)
{
    c->lt = lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_MANIFEST);
    c->look = zui_look(c->lt);
    place(c, z_pt);
}

/* a tagged layer: the paraconsistent engine decides depth, tint and shade */
static void tag(ctx_t *c, uint8_t base, uint8_t pol, uint8_t phase)
{
    c->lt = lpx_make(base, pol, phase);
    c->look = zui_look(c->lt);
    place(c, c->look.z_pt);
}

/* Tell the depth filter what was drawn here: the layer's tag, and its depth
 * (the logic's own travel plus any raise this surface has). */
static void stamp(ctx_t *c, zui_rect_t r, int32_t rad, int32_t raise)
{
    if (c->pl) zui_plane_mark(c->pl, r, rad, c->lt, c->look.z_pt * 6 + raise);
}

static uint32_t C(ctx_t *c, uint32_t rgb)
{
    return zui_look_color(&c->look, zui_chroma_px(&c->d, rgb, c->z), c->bg ? c->bg : ZUI_VOID, 11);
}
static uint32_t T(ctx_t *c, uint32_t rgb)
{
    return zui_look_text(&c->look, zui_chroma_px(&c->d, rgb, c->z), c->bg ? c->bg : ZUI_VOID);
}

static zui_rect_t R(ctx_t *c, int32_t x, int32_t y, int32_t w, int32_t h)
{
    zui_rect_t r = {x + c->dx, y + c->dy, w, h};
    return r;
}

static zui_text_t font(ctx_t *c, const ttf_font_t *f, int32_t pt)
{
    zui_text_t t = {f, &CACHE, (uint32_t) PT(c, pt), f == &F_MONO || f == &F_MONO_MED};
    return t;
}

static int32_t text(ctx_t *c, const ttf_font_t *f, int32_t pt, int32_t x, int32_t base,
                    uint32_t rgb, const char *s)
{
    zui_text_t t = font(c, f, pt);
    return zui_text(&c->s, &t, x + c->dx, base + c->dy, T(c, rgb), s) - c->dx;
}

static int32_t text_w(ctx_t *c, const ttf_font_t *f, int32_t pt, const char *s)
{
    zui_text_t t = font(c, f, pt);
    return zui_text_width(&t, s);
}

static void text_r(ctx_t *c, const ttf_font_t *f, int32_t pt, int32_t right, int32_t base,
                   uint32_t rgb, const char *s)
{
    text(c, f, pt, right - text_w(c, f, pt, s), base, rgb, s);
}

static void text_fit(ctx_t *c, const ttf_font_t *f, int32_t pt, int32_t x, int32_t base, int32_t w,
                     uint32_t rgb, const char *s)
{
    zui_text_t t = font(c, f, pt);
    zui_text_fit(&c->s, &t, x + c->dx, base + c->dy, w, T(c, rgb), s);
}

/* word-wrap a paragraph; returns the baseline after the last line */
static int32_t para(ctx_t *c, const ttf_font_t *f, int32_t pt, int32_t x, int32_t base, int32_t w,
                    int32_t lead_pt, uint32_t rgb, const char *s)
{
    char line[512] = "", trial[512];
    const char *p = s;
    while (*p) {
        const char *e = p;
        while (*e && *e != ' ') e++;
        char word[256];
        size_t n = (size_t) (e - p) < sizeof word - 1 ? (size_t) (e - p) : sizeof word - 1;
        memcpy(word, p, n);
        word[n] = 0;
        snprintf(trial, sizeof trial, "%s%s%s", line, line[0] ? " " : "", word);
        if (line[0] && text_w(c, f, pt, trial) > w) {
            text(c, f, pt, x, base, rgb, line);
            base += PT(c, lead_pt);
            snprintf(line, sizeof line, "%s", word);
        } else {
            snprintf(line, sizeof line, "%s", trial);
        }
        p = *e ? e + 1 : e;
    }
    if (line[0]) {
        text(c, f, pt, x, base, rgb, line);
        base += PT(c, lead_pt);
    }
    return base;
}

static void fill(ctx_t *c, zui_rect_t r, uint32_t rgb)
{
    zui_fill(&c->s, r, C(c, rgb));
}
static void hair_h(ctx_t *c, int32_t x, int32_t y, int32_t w)
{
    zui_hline(&c->s, x + c->dx, y + c->dy, w, 1, C(c, ZUI_HAIRLINE));
}
static void hair_v(ctx_t *c, int32_t x, int32_t y, int32_t h)
{
    zui_vline(&c->s, x + c->dx, y + c->dy, h, 1, C(c, ZUI_HAIRLINE));
}

/* ---- icons: drawn from primitives, one stroke weight, no emoji ---------- */
typedef enum { IC_STUDIO, IC_LEDGER, IC_MESH, IC_STORE, IC_APPS, IC_SETTINGS } icon_t;

static void dot(ctx_t *c, int32_t cx, int32_t cy, int32_t r, uint32_t rgb)
{
    zui_fill_rrect(&c->s, R(c, cx - r, cy - r, 2 * r, 2 * r), r, C(c, rgb), 255);
}
static void ring(ctx_t *c, int32_t cx, int32_t cy, int32_t r, int32_t w, uint32_t rgb)
{
    zui_stroke_rrect(&c->s, R(c, cx - r, cy - r, 2 * r, 2 * r), r, w, C(c, rgb), 255);
}

static void icon(ctx_t *c, icon_t k, int32_t cx, int32_t cy, uint32_t rgb)
{
    int32_t u = PT(c, 1), sw = u < 1 ? 1 : (u * 3 + 1) / 2;
    switch (k) {
    case IC_STUDIO:
        ring(c, cx, cy, PT(c, 8), sw, rgb);
        dot(c, cx, cy, PT(c, 3), rgb);
        break;
    case IC_LEDGER:
        for (int i = -1; i <= 1; i++)
            zui_hline(&c->s, cx - PT(c, 8) + c->dx, cy + i * PT(c, 5) - sw / 2 + c->dy,
                      i == 1 ? PT(c, 10) : PT(c, 16), sw, C(c, rgb));
        break;
    case IC_MESH:
        zui_hline(&c->s, cx - PT(c, 7) + c->dx, cy - sw / 2 + c->dy, PT(c, 14), sw, C(c, rgb));
        dot(c, cx - PT(c, 7), cy, PT(c, 3), rgb);
        dot(c, cx + PT(c, 7), cy, PT(c, 3), rgb);
        ring(c, cx, cy, PT(c, 3), sw, rgb);
        break;
    case IC_STORE:
        zui_stroke_rrect(&c->s, R(c, cx - PT(c, 8), cy - PT(c, 5), PT(c, 16), PT(c, 13)), PT(c, 2),
                         sw, C(c, rgb), 255);
        zui_stroke_rrect(&c->s, R(c, cx - PT(c, 4), cy - PT(c, 9), PT(c, 8), PT(c, 6)), PT(c, 3),
                         sw, C(c, rgb), 255);
        break;
    case IC_APPS:
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 2; i++)
                zui_stroke_rrect(&c->s,
                                 R(c, cx - PT(c, 8) + i * PT(c, 9), cy - PT(c, 8) + j * PT(c, 9),
                                   PT(c, 7), PT(c, 7)),
                                 PT(c, 2), sw, C(c, rgb), 255);
        break;
    case IC_SETTINGS:
        for (int i = 0; i < 2; i++) {
            int32_t y = cy - PT(c, 4) + i * PT(c, 8);
            zui_hline(&c->s, cx - PT(c, 8) + c->dx, y - sw / 2 + c->dy, PT(c, 16), sw, C(c, rgb));
            dot(c, cx + (i ? PT(c, 3) : -PT(c, 3)), y, PT(c, 3), rgb);
        }
        break;
    }
}

static const char *NAV[6] = {"Studio", "Ledger", "Mesh", "Store", "Apps", "Settings"};
static int SPACE_SEL = -1; /* >= 0: top bar shows the space switcher */
static const char *SPACES[5] = {"Home", "Office", "Team", "Cloud top", "Park"};

/* BOTH: the two disagreeing values drawn as colour and anti-colour, side by
 * side in one pill, so neither claim is hidden. */
static int32_t split_chip(ctx_t *c, int32_t x, int32_t y, const char *a, const char *b)
{
    uint32_t col = ZUI_GOLD, anti = zui_anti(ZUI_GOLD);
    uint32_t anti_txt = zui_mix(anti, 0xFFFFFF, 110);
    int32_t pad = PT(c, 8), gap = PT(c, 6);
    int32_t wa = text_w(c, &F_MONO, 11, a), wb = text_w(c, &F_MONO, 11, b);
    int32_t w = pad + wa + gap * 2 + 1 + wb + pad, h = PT(c, 20);
    int32_t mid = x + pad + wa + gap;
    zui_rect_t r = R(c, x, y, w, h);
    zui_fill_rrect(&c->s, r, h / 2, C(c, ZUI_RAISED), 255);
    zui_rect_t keep = c->s.clip;
    zui_clip(&c->s, R(c, x, y, mid - x, h));
    zui_stroke_rrect(&c->s, r, h / 2, 1, C(c, col), 255);
    c->s.clip = keep;
    zui_clip(&c->s, R(c, mid, y, x + w - mid, h));
    zui_stroke_rrect(&c->s, r, h / 2, 1, C(c, anti_txt), 255);
    c->s.clip = keep;
    zui_vline(&c->s, mid + c->dx, y + PT(c, 4) + c->dy, h - PT(c, 8), 1, C(c, ZUI_EDGE));
    text(c, &F_MONO, 11, x + pad, y + PT(c, 14), ZUI_GOLD_GLOW, a);
    text(c, &F_MONO, 11, mid + gap, y + PT(c, 14), anti_txt, b);
    return w;
}

/* the deterministic receipt every AI result carries */
static void receipt(ctx_t *c, int32_t x, int32_t base, int compact, const char *hash)
{
    char buf[160];
    if (compact)
        snprintf(buf, sizeof buf, "deterministic \xC2\xB7 replay %s", hash);
    else
        snprintf(buf, sizeof buf,
                 "deterministic \xC2\xB7 replay %s \xC2\xB7 same bytes on every ZXV device", hash);
    dot(c, x + PT(c, 3), base - PT(c, 4), PT(c, 2) > 1 ? PT(c, 2) : 1, ZUI_OK);
    int32_t e = text(c, &F_MONO, 11, x + PT(c, 12), base, ZUI_MUTED, buf);
    if (!compact) text(c, &F_MED, 11, e + PT(c, 12), base, ZUI_TEXT_2, "Verify");
}

/* ---- shared pieces ------------------------------------------------------- */
static int32_t button(ctx_t *c, int32_t x, int32_t y, const char *s, int primary)
{
    int32_t w = text_w(c, &F_MED, 13, s) + PT(c, 28), h = PT(c, 32);
    if (c->p.target_px > h) h = c->p.target_px;
    zui_rect_t r = R(c, x, y, w, h);
    if (primary)
        zui_fill_rrect(&c->s, r, PT(c, ZUI_R_CONTROL), C(c, ZUI_GOLD), 255);
    else
        zui_stroke_rrect(&c->s, r, PT(c, ZUI_R_CONTROL), 1, C(c, ZUI_EDGE), 255);
    text(c, &F_MED, 13, x + PT(c, 14), y + h / 2 + PT(c, 5), primary ? ZUI_VOID : ZUI_TEXT, s);
    return w;
}

static void budget_bar(ctx_t *c, int32_t x, int32_t y, int32_t w, int32_t used, int32_t total,
                       uint32_t rgb)
{
    int32_t h = PT(c, 4);
    zui_fill_rrect(&c->s, R(c, x, y, w, h), h / 2, C(c, ZUI_HAIRLINE), 255);
    int32_t f = (int32_t) ((int64_t) w * used / (total ? total : 1));
    if (f > 0) zui_fill_rrect(&c->s, R(c, x, y, f, h), h / 2, C(c, rgb), 255);
}

/* The top strip: wordmark, place, Ask field, swarm meter. */
/* ---- the swarm's feelings: live emoji faces ------------------------------ */
typedef struct {
    const char *name, *role;
    swarm_feeling_t was, now;
    zui_health_t h;
    const char *note;
    uint32_t re, im;
} sw_agent_t;
static const sw_agent_t SW_AGENTS[5] = {
    {"planner",
     "splits the ask",
     {SWARM_EMO_CALM, 3},
     {SWARM_EMO_CALM, 3},
     ZUI_HEALTH_OK,
     "steady",
     412,
     120},
    {"writer",
     "drafts the text",
     {SWARM_EMO_JOY, 2},
     {SWARM_EMO_JOY, 2},
     ZUI_HEALTH_OK,
     "steady",
     610,
     80},
    {"checker",
     "checks the figures",
     {SWARM_EMO_CURIOUS, 3},
     {SWARM_EMO_WORRY, 4},
     ZUI_HEALTH_OK,
     "its second check disagreed with the ledger",
     388,
     199},
    {"ledger",
     "reads Sicily",
     {SWARM_EMO_NEUTRAL, 0},
     {SWARM_EMO_NEUTRAL, 0},
     ZUI_HEALTH_SLOW,
     "missed one cycle deadline",
     254,
     0},
    {"retriever",
     "fetches sources",
     {SWARM_EMO_FRUSTRATION, 3},
     {SWARM_EMO_FRUSTRATION, 3},
     ZUI_HEALTH_FAILING,
     "2 failed fetches \xC2\xB7 retrying on another peer",
     176,
     199},
};
static const swarm_feeling_t SWARM_MOOD = {SWARM_EMO_CALM, 3};
static uint32_t MORPH_T = 256, TICK = 0; /* animation frame state */

static zui_face_t agent_face(int i)
{
    return zui_face_lerp(zui_face_feeling(SW_AGENTS[i].was), zui_face_feeling(SW_AGENTS[i].now),
                         zui_emote_ease(MORPH_T));
}

static void face_at(ctx_t *c, zui_face_t f, zui_health_t h, int32_t cx, int32_t cy, int32_t r_pt)
{
    zui_face_draw(&c->s, &f, h, cx + c->dx, cy + c->dy, PT(c, r_pt), TICK);
}

static void top_bar(ctx_t *c, const char *place)
{
    c->bg = ZUI_VOID;
    zui_rect_t t = c->p.top;
    layer(c, 0);
    fill(c, t, ZUI_VOID);
    hair_h(c, t.x, t.y + t.h - 1, t.w);
    int32_t g = c->p.gutter_px, mid = t.y + t.h / 2;
    int32_t x = text(c, &F_SEMI, 15, t.x + g, mid + PT(c, 5), ZUI_TEXT, "ZXV");
    dot(c, x + PT(c, 3), mid + PT(c, 3), PT(c, 2) > 1 ? PT(c, 2) : 1, ZUI_GOLD_GLOW);
    if (SPACE_SEL >= 0 && c->p.cls >= ZUI_EXPANDED) {
        int32_t px = x + PT(c, 18);
        for (int i = 0; i < 5; i++) {
            int32_t w = text_w(c, &F_REG, 13, SPACES[i]) + PT(c, 20);
            if (i == SPACE_SEL)
                zui_fill_rrect(&c->s, R(c, px, mid - PT(c, 13), w, PT(c, 26)), PT(c, ZUI_R_CONTROL),
                               C(c, ZUI_RAISED), 255);
            text(c, i == SPACE_SEL ? &F_MED : &F_REG, 13, px + PT(c, 10), mid + PT(c, 5),
                 i == SPACE_SEL ? ZUI_TEXT : ZUI_MUTED, SPACES[i]);
            px += w + PT(c, 2);
        }
    } else if (c->p.cls != ZUI_COMPACT)
        text(c, &F_REG, 13, x + PT(c, 14), mid + PT(c, 5), ZUI_MUTED, place);

    if (c->p.cls == ZUI_COMPACT) {
        const char *m = "1,840 / 4,181";
        int32_t w = text_w(c, &F_MONO, 11, m);
        text(c, &F_MONO, 11, t.x + t.w - g - w, mid + PT(c, 4), ZUI_TEXT_2, m);
        face_at(c, zui_face_feeling(SWARM_MOOD), ZUI_HEALTH_OK, t.x + t.w - g - w - PT(c, 14), mid,
                8);
        return;
    }
    int32_t aw = PT(c, c->p.cls == ZUI_LARGE ? 520 : (SPACE_SEL >= 0 ? 300 : 420));
    int32_t ax = t.x + (t.w - aw) / 2;
    if (SPACE_SEL >= 0 && c->p.cls == ZUI_EXPANDED) ax = t.x + PT(c, 600);
    if (c->p.cls == ZUI_MEDIUM) ax = t.x + PT(c, 150);
    zui_rect_t a = R(c, ax, mid - PT(c, 14), aw, PT(c, 28));
    zui_fill_rrect(&c->s, a, PT(c, ZUI_R_CONTROL), C(c, ZUI_PANEL), 255);
    zui_stroke_rrect(&c->s, a, PT(c, ZUI_R_CONTROL), 1, C(c, ZUI_EDGE), 255);
    text(c, &F_REG, 13, ax + PT(c, 12), mid + PT(c, 5), ZUI_FAINT,
         "Ask the swarm about anything on screen");
    const char *k = "Cmd K";
    text_r(c, &F_MONO, 11, ax + aw - PT(c, 10), mid + PT(c, 4), ZUI_MUTED, k);

    int32_t rx = t.x + t.w - g;
    const char *m = "1,840 / 4,181 tok";
    int32_t mw = text_w(c, &F_MONO, 11, m);
    text(c, &F_MONO, 11, rx - mw, mid - PT(c, 2), ZUI_TEXT_2, m);
    budget_bar(c, rx - mw, mid + PT(c, 5), mw, 1840, 4181, ZUI_TEXT_2);
    /* the status bar: one live face per agent, morphing as it feels */
    for (int i = 0; i < 5; i++)
        face_at(c, agent_face(4 - i), SW_AGENTS[4 - i].h, rx - mw - PT(c, 20) - i * PT(c, 24), mid,
                8);
}

static void rail(ctx_t *c, int sel)
{
    c->bg = ZUI_VOID;
    zui_rect_t r = c->p.rail;
    if (r.w == 0) return;
    layer(c, 0);
    fill(c, r, ZUI_VOID);
    hair_v(c, r.x + r.w - 1, r.y, r.h);
    int labels = c->p.cls == ZUI_LARGE;
    int32_t y = r.y + PT(c, 12);
    for (int i = 0; i < 6; i++) {
        int32_t h = PT(c, labels ? 36 : 52);
        if (i == sel) {
            zui_fill_rrect(&c->s, R(c, r.x + PT(c, 8), y, r.w - PT(c, 16), h), PT(c, ZUI_R_CONTROL),
                           C(c, ZUI_RAISED), 255);
            zui_fill(&c->s, R(c, r.x + PT(c, 8), y + PT(c, 8), PT(c, 2), h - PT(c, 16)),
                     C(c, ZUI_TEXT));
        }
        uint32_t col = i == sel ? ZUI_TEXT : ZUI_MUTED;
        if (labels) {
            icon(c, (icon_t) i, r.x + PT(c, 32), y + h / 2, col);
            text(c, i == sel ? &F_MED : &F_REG, 14, r.x + PT(c, 54), y + h / 2 + PT(c, 5), col,
                 NAV[i]);
        } else {
            icon(c, (icon_t) i, r.x + r.w / 2, y + PT(c, 18), col);
            zui_text_t t = font(c, &F_REG, 10);
            int32_t w = zui_text_width(&t, NAV[i]);
            text(c, &F_REG, 10, r.x + (r.w - w) / 2, y + PT(c, 42), col, NAV[i]);
        }
        y += h + PT(c, 4);
    }
    if (labels) {
        int32_t by = r.y + r.h - PT(c, 84);
        hair_h(c, r.x + PT(c, 16), by, r.w - PT(c, 32));
        text(c, &F_REG, 11, r.x + PT(c, 20), by + PT(c, 24), ZUI_MUTED, "This Mac");
        text(c, &F_MONO, 11, r.x + PT(c, 20), by + PT(c, 42), ZUI_TEXT_2,
             "M3 Max \xC2\xB7 37 tok/s");
        text(c, &F_MONO, 11, r.x + PT(c, 20), by + PT(c, 60), ZUI_MUTED, "mesh: 2 peers");
    }
}

static void bottom_bar(ctx_t *c, int sel)
{
    c->bg = ZUI_VOID;
    zui_rect_t b = c->p.bottom;
    if (b.w == 0) return;
    layer(c, 0);
    fill(c, b, ZUI_VOID);
    hair_h(c, b.x, b.y, b.w);
    static const int tabs[5] = {0, 1, 2, 4, 5};
    for (int i = 0; i < 5; i++) {
        int32_t cx = b.x + b.w * (2 * i + 1) / 10;
        uint32_t col = tabs[i] == sel ? ZUI_TEXT : ZUI_MUTED;
        icon(c, (icon_t) tabs[i], cx, b.y + PT(c, 22), col);
        int32_t w = text_w(c, &F_REG, 10, NAV[tabs[i]]);
        text(c, &F_REG, 10, cx - w / 2, b.y + PT(c, 46), col, NAV[tabs[i]]);
    }
}

/* ---- Studio: conversation list + conversation + swarm inspector ---------- */
typedef struct {
    const char *title, *preview, *when;
    int archived;
} conv_t;
static const conv_t CONVS[] = {
    {"Grant application draft", "Swarm: tightened the budget section and...", "09:41", 0},
    {"Tonopah array field notes", "You: compare yesterday's readings with...", "08:02", 0},
    {"Mesh peer onboarding", "Swarm: both peers signed the agreement", "Sat", 0},
    {"V-Bill epoch 14 summary", "Swarm: clearing held the reserve floor", "Fri", 0},
    {"Phone pairing checklist", "Archived", "Oct 3", 1},
    {"Hardware license questions", "Archived", "Oct 2", 1},
};

static void conv_list(ctx_t *c)
{
    c->bg = ZUI_PANEL;
    zui_rect_t l = c->p.list;
    if (l.w == 0) return;
    layer(c, 0);
    fill(c, l, ZUI_PANEL);
    hair_v(c, l.x + l.w - 1, l.y, l.h);
    int32_t g = PT(c, 16);
    text(c, &F_MED, 11, l.x + g, l.y + PT(c, 28), ZUI_MUTED, "CONVERSATIONS");
    text_r(c, &F_MONO, 11, l.x + l.w - g, l.y + PT(c, 28), ZUI_FAINT, "6");
    int32_t y = l.y + PT(c, 44);
    zui_clip(&c->s, l);
    for (size_t i = 0; i < sizeof CONVS / sizeof CONVS[0]; i++) {
        const conv_t *v = &CONVS[i];
        c->bg = ZUI_PANEL;
        if (v->archived)
            tag(c, LPX_B_FALSE, LPX_P_NEUTRAL, LPX_PH_DISSOLVING); /* withdrawn */
        else if (i == 0)
            tag(c, LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST);
        else
            layer(c, 0);
        int32_t h = PT(c, 64);
        if (i == 0) {
            zui_fill_rrect(&c->s, R(c, l.x + PT(c, 8), y, l.w - PT(c, 16), h - PT(c, 4)),
                           PT(c, ZUI_R_CONTROL), C(c, ZUI_RAISED), 255);
        }
        uint32_t tc = v->archived ? ZUI_MUTED : ZUI_TEXT;
        text_fit(c, i == 0 ? &F_MED : &F_REG, 14, l.x + g, y + PT(c, 24), l.w - 2 * g - PT(c, 44),
                 tc, v->title);
        text_r(c, &F_MONO, 11, l.x + l.w - g, y + PT(c, 24), ZUI_FAINT, v->when);
        text_fit(c, &F_REG, 12, l.x + g, y + PT(c, 44), l.w - 2 * g, ZUI_MUTED, v->preview);
        y += h;
    }
    zui_unclip(&c->s);
}

static const char *Q1 =
    "Tighten the budget section of the grant draft and check the totals against the ledger.";
static const char *A1 =
    "Done. The budget section now leads with the three cost lines the reviewers ask about, "
    "and every figure matches the Sicily ledger. One total was off: equipment was listed "
    "as 12,400 but the ledger entries add to 12,040.";
static const char *S1 = "Change the equipment total from 12,400 to 12,040 in section 4.2.";

/* returns bottom y of the suggestion card */
static int32_t held_card(ctx_t *c, int32_t x, int32_t y, int32_t w)
{
    c->bg = ZUI_PANEL;
    layer(c, 0); /* S0: held, at the glass */
    int32_t pad = PT(c, 16);
    int32_t inner = w - 2 * pad;
    /* measure: label + wrapped text + buttons */
    zui_rect_t probe_clip = c->s.clip;
    c->s.clip.w = 0; /* draw nothing: measure */
    int32_t end = para(c, &F_REG, 14, x + pad, y + PT(c, 50), inner, 22, ZUI_TEXT, S1);
    c->s.clip = probe_clip;
    int32_t h =
        end - y + PT(c, 8) + (c->p.target_px > PT(c, 32) ? c->p.target_px : PT(c, 32)) + pad;
    zui_rect_t r = R(c, x, y, w, h);
    zui_fill_rrect(&c->s, r, PT(c, ZUI_R_PANEL), C(c, ZUI_PANEL), 255);
    zui_dash_rrect(&c->s, r, PT(c, ZUI_R_PANEL), PT(c, 1) > 0 ? PT(c, 1) : 1, PT(c, 5),
                   C(c, ZUI_GOLD));
    text(c, &F_MED, 11, x + pad, y + PT(c, 26), ZUI_GOLD_GLOW, "SUGGESTED EDIT");
    text_r(c, &F_MONO, 11, x + w - pad, y + PT(c, 26), ZUI_MUTED, "held \xC2\xB7 not applied");
    end = para(c, &F_REG, 14, x + pad, y + PT(c, 50), inner, 22, ZUI_TEXT, S1);
    int32_t by = end - PT(c, 6);
    int32_t bw = button(c, x + pad, by, "Accept", 1);
    button(c, x + pad + bw + PT(c, 8), by, "Reject", 0);
    if (c->p.cls != ZUI_COMPACT)
        text_r(c, &F_MONO, 11, x + w - pad, by + PT(c, 20), ZUI_FAINT, "enter accept   esc reject");
    return y + h;
}

static void conversation(ctx_t *c)
{
    c->bg = ZUI_VOID;
    zui_rect_t m = c->p.main;
    layer(c, 0);
    fill(c, m, ZUI_VOID);
    int compact = c->p.cls == ZUI_COMPACT;
    int32_t g = PT(c, compact ? 16 : 32);
    int32_t maxw = PT(c, 680);
    int32_t w = m.w - 2 * g < maxw ? m.w - 2 * g : maxw;
    int32_t x = m.x + (m.w - w) / 2;
    if (compact) x = m.x + g;
    int32_t y = m.y + PT(c, compact ? 34 : 44);

    text(c, &F_SEMI, compact ? 17 : 22, x, y, ZUI_TEXT, "Grant application draft");
    y += PT(c, 22);
    text(c, &F_REG, 12, x, y, ZUI_MUTED,
         "3 agents \xC2\xB7 local \xC2\xB7 nothing leaves this device");
    y += PT(c, compact ? 22 : 30);
    hair_h(c, x, y, w);
    y += PT(c, compact ? 28 : 36);

    text(c, &F_MED, 12, x, y, ZUI_MUTED, "You");
    y += PT(c, 24);
    y = para(c, &F_REG, 14, x, y, w, 22, ZUI_TEXT, Q1);
    y += PT(c, compact ? 14 : 18);

    /* the answer: attested by the checker (TRUE, converging, manifest) */
    tag(c, LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST);
    text(c, &F_MED, 12, x + PT(c, 12), y - PT(c, 4), ZUI_TEXT_2, "Swarm");
    text(c, &F_REG, 12, x + PT(c, 12) + text_w(c, &F_MED, 12, "Swarm "), y - PT(c, 4), ZUI_MUTED,
         compact ? "\xC2\xB7 writer + checker"
                 : "\xC2\xB7 planner \xE2\x86\x92 writer \xE2\x86\x92 checker");
    y += PT(c, 20);
    int32_t top = y - PT(c, 34);
    y = para(c, &F_REG, 14, x + PT(c, 12), y, w - PT(c, 12), 22, ZUI_TEXT, A1);
    /* the disputed figure: both sources shown, colour and anti-colour */
    layer(c, 0);
    {
        int32_t e = text(c, &F_REG, 12, x + PT(c, 12), y + PT(c, 2), ZUI_MUTED, "Equipment total");
        int32_t cw = split_chip(c, e + PT(c, 8), y - PT(c, 12), "proposal 12,400", "ledger 12,040");
        if (!compact)
            text(c, &F_REG, 12, e + PT(c, 16) + cw, y + PT(c, 2), ZUI_MUTED,
                 "both stand until you choose");
    }
    y += PT(c, 26);
    tag(c, LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST);
    zui_fill(&c->s, R(c, x, top + PT(c, 2), PT(c, 2), y - top - PT(c, 6)), C(c, ZUI_EDGE));
    text(c, &F_MONO, 11, x + PT(c, 12), y + PT(c, 2), ZUI_FAINT,
         compact ? "qwen2.5-1.5b \xC2\xB7 412 tok \xC2\xB7 1.9 s"
                 : "qwen2.5-1.5b \xC2\xB7 writer 212 \xC2\xB7 checker 200 tok \xC2\xB7 1.9 s "
                   "\xC2\xB7 ledger read 14 entries");
    y += PT(c, 20);
    receipt(c, x + PT(c, 12), y, compact,
            "7c41e0\xE2\x80\xA6"
            "a92f");
    y += PT(c, 22);

    held_card(c, x, y, w);

    /* composer, pinned to the bottom of the pane */
    layer(c, 0);
    int32_t ch = PT(c, 48);
    int32_t cy = m.y + m.h - ch - PT(c, compact ? 12 : 24);
    zui_rect_t cr = R(c, x, cy, w, ch);
    zui_fill_rrect(&c->s, cr, PT(c, ZUI_R_PANEL), C(c, ZUI_PANEL), 255);
    zui_stroke_rrect(&c->s, cr, PT(c, ZUI_R_PANEL), 1, C(c, ZUI_EDGE), 255);
    text(c, &F_REG, 14, x + PT(c, 16), cy + ch / 2 + PT(c, 5), ZUI_FAINT,
         compact ? "Reply to the swarm" : "Reply, or Cmd K for this app's AI actions");
    /* send: an arrow drawn from primitives */
    int32_t ax = x + w - PT(c, 28), ay = cy + ch / 2;
    zui_fill_rrect(&c->s, R(c, ax - PT(c, 14), ay - PT(c, 14), PT(c, 28), PT(c, 28)), PT(c, 6),
                   C(c, ZUI_RAISED), 255);
    zui_hline(&c->s, ax - PT(c, 6) + c->dx, ay - 1 + c->dy, PT(c, 12), PT(c, 2), C(c, ZUI_TEXT_2));
    for (int i = 0; i < PT(c, 5); i++) {
        zui_fill(&c->s, R(c, ax + PT(c, 6) - i - 1, ay - 1 - i, PT(c, 2), PT(c, 2)),
                 C(c, ZUI_TEXT_2));
        zui_fill(&c->s, R(c, ax + PT(c, 6) - i - 1, ay - 1 + i, PT(c, 2), PT(c, 2)),
                 C(c, ZUI_TEXT_2));
    }
}

typedef struct {
    const char *name, *model;
    int32_t tok;
    int active;
} agent_t;
static const agent_t AGENTS[] = {
    {"planner", "qwen2.5-1.5b", 1597, 0}, {"writer", "qwen2.5-1.5b + lora", 987, 1},
    {"checker", "phi-mini", 610, 1},      {"retriever", "embed-small", 377, 0},
    {"critic", "smol-360m", 233, 0},      {"reserve", "unallocated", 377, 0},
};

static void inspector(ctx_t *c)
{
    c->bg = ZUI_PANEL;
    zui_rect_t r = c->p.inspector;
    if (r.w == 0) return;
    layer(c, 0);
    fill(c, r, ZUI_PANEL);
    hair_v(c, r.x, r.y, r.h);
    int32_t g = PT(c, 20), x = r.x + g, w = r.w - 2 * g, y = r.y + PT(c, 32);
    text(c, &F_MED, 11, x, y, ZUI_MUTED, "SWARM THIS CYCLE");
    y += PT(c, 40);
    int32_t e = text(c, &F_MONO_MED, 32, x, y, ZUI_TEXT, "4,181");
    text(c, &F_REG, 12, e + PT(c, 8), y, ZUI_MUTED, "tokens per cycle");
    y += PT(c, 22);
    text(c, &F_REG, 12, x, y, ZUI_MUTED, "Fibonacci split \xC2\xB7 no agent above 38%");
    y += PT(c, 30);
    for (size_t i = 0; i < sizeof AGENTS / sizeof AGENTS[0]; i++) {
        const agent_t *a = &AGENTS[i];
        c->bg = ZUI_PANEL;
        if (a->active)
            tag(c, LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_EMERGING);
        else if (i == 3)
            tag(c, LPX_B_TRUE, LPX_P_ENTROPIC, LPX_PH_MANIFEST); /* stale index */
        else if (i == 5)
            tag(c, LPX_B_FALSE, LPX_P_NEUTRAL, LPX_PH_MANIFEST); /* unallocated */
        else
            layer(c, 0);
        if (a->active) dot(c, x + PT(c, 3), y - PT(c, 4), PT(c, 3), ZUI_OK);
        text(c, &F_MED, 13, x + PT(c, 14), y, a->tok == 377 && i == 5 ? ZUI_MUTED : ZUI_TEXT,
             a->name);
        char v[32];
        snprintf(v, sizeof v, "%d", a->tok);
        text_r(c, &F_MONO, 12, x + w, y, ZUI_TEXT_2, v);
        text(c, &F_MONO, 11, x + PT(c, 14), y + PT(c, 17), ZUI_FAINT, a->model);
        budget_bar(c, x + PT(c, 14), y + PT(c, 26), w - PT(c, 14), a->tok, 1597,
                   a->active ? ZUI_TEXT_2 : ZUI_EDGE);
        y += PT(c, 52);
    }
    layer(c, 0);
    y += PT(c, 4);
    hair_h(c, x, y, w);
    y += PT(c, 30);
    text(c, &F_MED, 11, x, y, ZUI_MUTED, "CHECKS ON THIS ANSWER");
    y += PT(c, 28);
    tag(c, LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST);
    dot(c, x + PT(c, 3), y - PT(c, 4), PT(c, 3), ZUI_OK);
    text(c, &F_REG, 13, x + PT(c, 14), y, ZUI_TEXT, "Budget lines");
    text_r(c, &F_REG, 12, x + w, y, ZUI_TEXT_2, "attested");
    y += PT(c, 28);
    layer(c, 0);
    text(c, &F_REG, 13, x + PT(c, 14), y, ZUI_TEXT, "Equipment");
    split_chip(c, x + w - text_w(c, &F_MONO, 11, "12,40012,040") - PT(c, 29), y - PT(c, 14),
               "12,400", "12,040");
    y += PT(c, 28);
    layer(c, 0);
    ring(c, x + PT(c, 3), y - PT(c, 4), PT(c, 3), 1, ZUI_MUTED);
    text(c, &F_REG, 13, x + PT(c, 14), y, ZUI_TEXT, "Reviewer list");
    text_r(c, &F_REG, 12, x + w, y, ZUI_MUTED, "unknown");
    y += PT(c, 28);
    tag(c, LPX_B_FALSE, LPX_P_FORBIDDEN, LPX_PH_MANIFEST);
    zui_stroke_rrect(&c->s, R(c, x - PT(c, 6), y - PT(c, 18), w + PT(c, 12), PT(c, 26)), PT(c, 4),
                     1, ZUI_ALERT, 255);
    text(c, &F_REG, 13, x + PT(c, 14), y, ZUI_TEXT, "Peer 7f3a signature");
    text_r(c, &F_REG, 12, x + w, y, ZUI_ALERT, "quarantined");
    layer(c, 0);
    y += PT(c, 22);
    hair_h(c, x, y, w);
    y += PT(c, 30);
    text(c, &F_MED, 11, x, y, ZUI_MUTED, "ECONOMY");
    y += PT(c, 26);
    text(c, &F_REG, 13, x, y, ZUI_TEXT, "Mood");
    text_r(c, &F_REG, 13, x + w, y, ZUI_TEXT_2, "calm, curious");
    y += PT(c, 24);
    text(c, &F_REG, 13, x, y, ZUI_TEXT, "Market");
    text_r(c, &F_REG, 13, x + w, y, ZUI_TEXT_2, "cooperative clearing");
    y += PT(c, 24);
    text(c, &F_REG, 13, x, y, ZUI_TEXT, "Earned today");
    text_r(c, &F_MONO, 13, x + w, y, ZUI_TEXT_2, "0.42 VFV");
}

static void studio(ctx_t *c)
{
    fill(c, (zui_rect_t){0, 0, c->s.w, c->s.h}, ZUI_VOID);
    conv_list(c);
    conversation(c);
    inspector(c);
    rail(c, 0);
    bottom_bar(c, 0);
    top_bar(c, "Studio");
}

/* ---- Writer with the Ask palette open ------------------------------------ */
static const char *DOC[] = {
    "The array ran for nine hours on the night of October 10. All ten trunks held lock "
    "except trunk 7, which dropped to on-hook twice near 02:00 when the generator cycled.",
    "Readings were within the expected band and the residual carrier stayed at its idle "
    "baseline, which suggests the dropouts came from the power supply rather than the "
    "receiver chain, although that would need confirming against the logger output.",
    "Next visit: swap the generator for the battery bank and repeat the trunk 7 sweep.",
};
static const char *DOC_SUGGEST =
    "Readings stayed in band and the residual carrier held its idle baseline, so the "
    "dropouts most likely came from the generator, not the receiver. Confirm against the logger.";

static void writer(ctx_t *c, int palette)
{
    c->bg = ZUI_VOID;
    fill(c, (zui_rect_t){0, 0, c->s.w, c->s.h}, ZUI_VOID);
    zui_rect_t l = c->p.list;
    if (l.w) {
        layer(c, 0);
        fill(c, l, ZUI_PANEL);
        hair_v(c, l.x + l.w - 1, l.y, l.h);
        text(c, &F_MED, 11, l.x + PT(c, 16), l.y + PT(c, 28), ZUI_MUTED, "DOCUMENTS");
        static const char *docs[] = {"Tonopah array field notes", "Grant application draft",
                                     "Peer agreement v2", "Reading list"};
        for (int i = 0; i < 4; i++) {
            int32_t y = l.y + PT(c, 44) + i * PT(c, 44);
            if (i == 0)
                zui_fill_rrect(&c->s, R(c, l.x + PT(c, 8), y, l.w - PT(c, 16), PT(c, 40)),
                               PT(c, ZUI_R_CONTROL), C(c, ZUI_RAISED), 255);
            text_fit(c, i == 0 ? &F_MED : &F_REG, 14, l.x + PT(c, 16), y + PT(c, 25),
                     l.w - PT(c, 32), i == 0 ? ZUI_TEXT : ZUI_TEXT_2, docs[i]);
        }
    }
    zui_rect_t m = c->p.main;
    int compact = c->p.cls == ZUI_COMPACT;
    int32_t g = PT(c, compact ? 16 : 48), maxw = PT(c, 640);
    int32_t w = m.w - 2 * g < maxw ? m.w - 2 * g : maxw;
    int32_t x = compact ? m.x + g : m.x + (m.w - w) / 2;
    int32_t y = m.y + PT(c, compact ? 36 : 56);
    layer(c, 0);
    text(c, &F_MONO, 11, x, y, ZUI_FAINT, "WRITER \xC2\xB7 saved \xC2\xB7 412 words");
    y += PT(c, 36);
    text(c, &F_SEMI, compact ? 22 : 28, x, y, ZUI_TEXT, "Tonopah array field notes");
    y += PT(c, 36);
    y = para(c, &F_REG, 15, x, y, w, 26, ZUI_TEXT_2, DOC[0]);
    y += PT(c, 12);
    /* original paragraph, struck back (S-), and the held rewrite at the glass */
    tag(c, LPX_B_FALSE, LPX_P_NEGENTROPIC, LPX_PH_DISSOLVING); /* being replaced */
    int32_t y0 = y;
    y = para(c, &F_REG, 15, x, y, w, 26, ZUI_FAINT, DOC[1]);
    layer(c, 0);
    y += PT(c, 10);
    int32_t pad = PT(c, 14);
    zui_rect_t probe = c->s.clip;
    c->s.clip.w = 0;
    int32_t e2 =
        para(c, &F_REG, 15, x + pad, y + PT(c, 40), w - 2 * pad, 26, ZUI_TEXT, DOC_SUGGEST);
    c->s.clip = probe;
    int32_t h = e2 - y + PT(c, 12);
    zui_rect_t r = R(c, x - pad, y, w + 2 * pad, h);
    zui_fill_rrect(&c->s, r, PT(c, ZUI_R_PANEL), C(c, ZUI_PANEL), 255);
    zui_dash_rrect(&c->s, r, PT(c, ZUI_R_PANEL), 1, PT(c, 5), C(c, ZUI_GOLD));
    text(c, &F_MED, 11, x, y + PT(c, 22), ZUI_GOLD_GLOW, "REWRITE \xC2\xB7 HELD");
    text_r(c, &F_MONO, 11, x + w, y + PT(c, 22), ZUI_MUTED,
           "writer \xC2\xB7 164 tok \xC2\xB7 enter accept  esc reject");
    para(c, &F_REG, 15, x, y + PT(c, 48), w, 26, ZUI_TEXT, DOC_SUGGEST);
    (void) y0;
    y += h + PT(c, 22);
    y = para(c, &F_REG, 15, x, y, w, 26, ZUI_TEXT_2, DOC[2]);

    if (!palette) return;
    /* the Ask palette: app-declared AI actions over a dimmed screen (S+) */
    layer(c, 0);
    zui_fill_a(&c->s, (zui_rect_t){0, 0, c->s.w, c->s.h}, ZUI_VOID, 170);
    tag(c, LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST);
    c->bg = ZUI_PANEL;
    int32_t pw = PT(c, 600);
    if (pw > c->s.w - 2 * PT(c, 16)) pw = c->s.w - 2 * PT(c, 16);
    int32_t px = (c->s.w - pw) / 2, py = c->p.top.h + PT(c, compact ? 24 : 72);
    int32_t ph = PT(c, 372);
    zui_rect_t pr = R(c, px, py, pw, ph);
    zui_fill_rrect(&c->s, pr, PT(c, 12), C(c, ZUI_PANEL), 255);
    zui_stroke_rrect(&c->s, pr, PT(c, 12), 1, C(c, ZUI_EDGE), 255);
    int32_t ix = px + PT(c, 20);
    text(c, &F_REG, 16, ix, py + PT(c, 36), ZUI_TEXT, "make the second paragraph clearer");
    zui_fill(&c->s,
             R(c, ix + text_w(c, &F_REG, 16, "make the second paragraph clearer") + PT(c, 2),
               py + PT(c, 20), PT(c, 2), PT(c, 22)),
             C(c, ZUI_GOLD_GLOW));
    hair_h(c, px, py + PT(c, 56), pw);
    text(c, &F_MED, 11, ix, py + PT(c, 82), ZUI_MUTED, "WRITER ACTIONS");
    static const char *acts[][3] = {
        {"Rewrite selection", "writer", "~170 tok"},
        {"Summarise document", "planner", "~320 tok"},
        {"Explain \xE2\x80\x9C"
         "residual carrier"
         "\xE2\x80\x9D",
         "retriever", "~90 tok"},
        {"Check figures against the ledger", "checker", "~240 tok"},
        {"Draft a reply email", "writer", "~400 tok"},
    };
    for (int i = 0; i < 5; i++) {
        int32_t ry = py + PT(c, 96) + i * PT(c, 44);
        if (i == 0)
            zui_fill_rrect(&c->s, R(c, px + PT(c, 8), ry, pw - PT(c, 16), PT(c, 40)),
                           PT(c, ZUI_R_CONTROL), C(c, ZUI_RAISED), 255);
        text(c, &F_REG, 14, ix, ry + PT(c, 25), i == 0 ? ZUI_TEXT : ZUI_TEXT_2, acts[i][0]);
        if (!compact)
            text_r(c, &F_MONO, 11, px + pw - PT(c, 20), ry + PT(c, 25), ZUI_MUTED, acts[i][2]);
        if (!compact)
            text_r(c, &F_MONO, 11, px + pw - PT(c, 100), ry + PT(c, 25), ZUI_FAINT, acts[i][1]);
    }
    hair_h(c, px, py + ph - PT(c, 40), pw);
    text(c, &F_REG, 12, ix, py + ph - PT(c, 15), ZUI_MUTED,
         compact ? "Runs on this device" : "Runs on this Mac \xC2\xB7 nothing leaves the device");
    text_r(c, &F_MONO, 11, px + pw - PT(c, 20), py + ph - PT(c, 15), ZUI_FAINT,
           "enter run   esc close");
}

static void writer_doc(ctx_t *c)
{
    writer(c, 0);
    rail(c, 4);
    bottom_bar(c, 4);
    top_bar(c, "Apps \xE2\x80\xBA Writer");
}

static void writer_screen(ctx_t *c)
{
    writer(c, 1);
    rail(c, 4);
    bottom_bar(c, 4);
    top_bar(c, "Apps \xE2\x80\xBA Writer");
}

/* ---- engraving and pictograms ------------------------------------------- */
static void engrave(ctx_t *c, zui_rect_t r, int32_t rad_pt)
{
    zui_engrave(&c->s, r, PT(c, rad_pt), C(c, ZUI_ENGRAVE_LO), C(c, ZUI_ENGRAVE_HI), 46);
}

/* a heading cut into the ground: shade above, light lip below, then the face */
static int32_t heading(ctx_t *c, const ttf_font_t *f, int32_t pt, int32_t x, int32_t base,
                       uint32_t rgb, const char *s)
{
    zui_text_t t = font(c, f, pt);
    int32_t o = PT(c, 1) > 0 ? PT(c, 1) : 1;
    zui_text_a(&c->s, &t, x + c->dx, base + c->dy - o, C(c, ZUI_ENGRAVE_LO), 200, s);
    zui_text_a(&c->s, &t, x + c->dx, base + c->dy + o, C(c, ZUI_ENGRAVE_HI), 40, s);
    return text(c, f, pt, x, base, rgb, s);
}

/* filled triangle by scanlines: apex (ax, ay), base from (bx0..bx1, by) */
static void tri(ctx_t *c, int32_t ax, int32_t ay, int32_t bx0, int32_t bx1, int32_t by,
                uint32_t rgb)
{
    int32_t h = by - ay;
    if (h <= 0) return;
    for (int32_t y = 0; y <= h; y++) {
        int32_t l = ax + (bx0 - ax) * y / h, r = ax + (bx1 - ax) * y / h;
        zui_hline(&c->s, l + c->dx, ay + y + c->dy, r - l + 1, 1, C(c, rgb));
    }
}

typedef enum {
    PG_HOME,
    PG_OFFICE,
    PG_TEAM,
    PG_CLOUD,
    PG_PARK,
    PG_WATCH,
    PG_HALL,
    PG_FILE,
    PG_FOLDER,
    PG_LOCK
} pict_t;

/* pictograms for places, drawn from primitives at size sz points */
static void pict(ctx_t *c, pict_t k, int32_t cx, int32_t cy, int32_t sz, uint32_t rgb)
{
    int32_t u = PT(c, sz) / 16;
    if (u < 1) u = 1;
    switch (k) {
    case PG_HOME:
        tri(c, cx, cy - 7 * u, cx - 8 * u, cx + 8 * u, cy - u, rgb);
        zui_fill(&c->s, R(c, cx - 6 * u, cy - u, 12 * u, 8 * u), C(c, rgb));
        zui_fill(&c->s, R(c, cx - 2 * u, cy + 2 * u, 4 * u, 5 * u), C(c, c->bg));
        break;
    case PG_OFFICE:
        zui_stroke_rrect(&c->s, R(c, cx - 5 * u, cy - 8 * u, 10 * u, 16 * u), u, u + 1, C(c, rgb),
                         255);
        dot(c, cx + 2 * u, cy, u + 1, rgb);
        break;
    case PG_TEAM:
        ring(c, cx, cy, 4 * u, u + 1, rgb);
        dot(c, cx, cy - 7 * u, 2 * u, rgb);
        dot(c, cx, cy + 7 * u, 2 * u, rgb);
        dot(c, cx - 7 * u, cy, 2 * u, rgb);
        dot(c, cx + 7 * u, cy, 2 * u, rgb);
        break;
    case PG_CLOUD:
        dot(c, cx - 4 * u, cy + u, 4 * u, rgb);
        dot(c, cx + 4 * u, cy + u, 4 * u, rgb);
        dot(c, cx, cy - 2 * u, 5 * u, rgb);
        zui_fill(&c->s, R(c, cx - 4 * u, cy + u, 8 * u, 4 * u), C(c, rgb));
        break;
    case PG_PARK:
        zui_fill(&c->s, R(c, cx - u, cy, 2 * u, 8 * u), C(c, rgb));
        dot(c, cx, cy - 3 * u, 6 * u, rgb);
        dot(c, cx + 6 * u, cy + 3 * u, 3 * u, rgb);
        break;
    case PG_WATCH:
        zui_stroke_rrect(&c->s, R(c, cx - 8 * u, cy - 6 * u, 16 * u, 12 * u), 2 * u, u + 1,
                         C(c, rgb), 255);
        tri(c, cx + 3 * u, cy, cx - 2 * u, cx - 2 * u, cy, rgb);
        for (int32_t i = 0; i <= 3 * u; i++)
            zui_vline(&c->s, cx - 2 * u + i + c->dx, cy - 3 * u + i + c->dy, 6 * u - 2 * i + 1, 1,
                      C(c, rgb));
        break;
    case PG_HALL:
        zui_hline(&c->s, cx - 8 * u + c->dx, cy + c->dy, 16 * u, u + 1, C(c, rgb));
        for (int32_t i = 0; i <= 3 * u; i++) {
            zui_vline(&c->s, cx - 8 * u + i + c->dx, cy - i + c->dy, 2 * i + 1, 1, C(c, rgb));
            zui_vline(&c->s, cx + 8 * u - i + c->dx, cy - i + c->dy, 2 * i + 1, 1, C(c, rgb));
        }
        break;
    case PG_FILE:
        zui_stroke_rrect(&c->s, R(c, cx - 6 * u, cy - 8 * u, 12 * u, 16 * u), u, u + 1, C(c, rgb),
                         255);
        for (int i = 0; i < 3; i++)
            zui_hline(&c->s, cx - 3 * u + c->dx, cy - 3 * u + i * 3 * u + c->dy, 6 * u, 1,
                      C(c, rgb));
        break;
    case PG_FOLDER:
        zui_fill_rrect(&c->s, R(c, cx - 8 * u, cy - 7 * u, 7 * u, 4 * u), u, C(c, rgb), 255);
        zui_fill_rrect(&c->s, R(c, cx - 8 * u, cy - 5 * u, 16 * u, 12 * u), u, C(c, rgb), 255);
        break;
    case PG_LOCK:
        ring(c, cx, cy - 3 * u, 3 * u, u + 1, rgb);
        zui_fill_rrect(&c->s, R(c, cx - 5 * u, cy - u, 10 * u, 8 * u), u, C(c, rgb), 255);
        break;
    }
}

static void avatar(ctx_t *c, int32_t cx, int32_t cy, const char *ini, uint32_t rgb)
{
    int32_t r = PT(c, 11);
    zui_fill_rrect(&c->s, R(c, cx - r, cy - r, 2 * r, 2 * r), r, C(c, ZUI_RAISED), 255);
    zui_stroke_rrect(&c->s, R(c, cx - r, cy - r, 2 * r, 2 * r), r, 1, C(c, rgb), 255);
    int32_t w = text_w(c, &F_MED, 10, ini);
    text(c, &F_MED, 10, cx - w / 2, cy + PT(c, 4), rgb, ini);
}

/* ---- Spaces: the building plan ------------------------------------------- */
typedef struct {
    pict_t pg;
    const char *name, *who, *rule;
    uint8_t base, pol, ph;
    int locked;
} room_t;

static void room(ctx_t *c, zui_rect_t r, const room_t *m, int here)
{
    tag(c, m->base, m->pol, m->ph);
    c->bg = ZUI_PANEL;
    zui_rect_t rr = R(c, r.x, r.y, r.w, r.h);
    zui_fill_rrect(&c->s, rr, PT(c, ZUI_R_PANEL), C(c, here ? ZUI_RAISED : ZUI_PANEL), 255);
    engrave(c, r, ZUI_R_PANEL);
    if (here) zui_stroke_rrect(&c->s, rr, PT(c, ZUI_R_PANEL), PT(c, 1) + 1, C(c, ZUI_TEXT_2), 255);
    int32_t p = PT(c, 16);
    pict(c, m->locked ? PG_LOCK : m->pg, r.x + p + PT(c, 10), r.y + p + PT(c, 10), 20,
         here ? ZUI_TEXT : ZUI_TEXT_2);
    heading(c, &F_SEMI, 15, r.x + p + PT(c, 30), r.y + p + PT(c, 9), ZUI_TEXT, m->name);
    text_fit(c, &F_REG, 12, r.x + p + PT(c, 30), r.y + p + PT(c, 27), r.w - 2 * p - PT(c, 30),
             ZUI_MUTED, m->who);
    if (m->rule) text_fit(c, &F_MONO, 11, r.x + p, r.y + r.h - p, r.w - 2 * p, ZUI_FAINT, m->rule);
    if (here)
        text_r(c, &F_MED, 11, r.x + r.w - p, r.y + p + PT(c, 9), ZUI_GOLD_GLOW, "YOU ARE HERE");
}

static void file_chip(ctx_t *c, int32_t x, int32_t y, int32_t w, pict_t pg, const char *name)
{
    zui_fill_rrect(&c->s, R(c, x, y, w, PT(c, 28)), PT(c, ZUI_R_CONTROL), C(c, ZUI_VOID), 255);
    engrave(c, (zui_rect_t){x, y, w, PT(c, 28)}, ZUI_R_CONTROL);
    pict(c, pg, x + PT(c, 14), y + PT(c, 14), 13, ZUI_MUTED);
    text_fit(c, &F_REG, 12, x + PT(c, 28), y + PT(c, 18), w - PT(c, 36), ZUI_TEXT_2, name);
}

static void spaces_plan(ctx_t *c, zui_rect_t A)
{
    int compact = c->p.cls == ZUI_COMPACT;
    int32_t g = PT(c, compact ? 12 : 24), x0 = A.x + g, y0 = A.y + PT(c, compact ? 16 : 20);
    int32_t W = A.w - 2 * g;
    static const room_t HOME = {
        PG_HOME,         "Home",     "only you \xC2\xB7 your desktop and files",
        "private",       LPX_B_TRUE, LPX_P_NEGENTROPIC,
        LPX_PH_MANIFEST, 0};
    static const room_t OFF1 = {PG_OFFICE,
                                "Office \xC2\xB7 Grant team",
                                "4 members \xC2\xB7 2 here now",
                                "hierarchical \xC2\xB7 owner decides",
                                LPX_B_TRUE,
                                LPX_P_NEUTRAL,
                                LPX_PH_MANIFEST,
                                0};
    static const room_t OFF2 = {PG_OFFICE,       "Office \xC2\xB7 Legal review",
                                "no access",     0,
                                LPX_B_FALSE,     LPX_P_NEUTRAL,
                                LPX_PH_MANIFEST, 1};
    static const room_t TEAM = {PG_TEAM,
                                "Team \xC2\xB7 Tonopah array",
                                "9 members \xC2\xB7 flat",
                                "horizontal \xC2\xB7 60% quorum",
                                LPX_B_TRUE,
                                LPX_P_NEUTRAL,
                                LPX_PH_EMERGING,
                                0};
    static const room_t CLOUD = {PG_CLOUD,
                                 "Cloud top",
                                 "your public space \xC2\xB7 served from this Mac over the mesh",
                                 "anyone may look \xC2\xB7 only you arrange",
                                 LPX_B_NEITHER,
                                 LPX_P_NEUTRAL,
                                 LPX_PH_MANIFEST,
                                 0};
    static const room_t PARK = {PG_PARK,
                                "Park",
                                "the commons \xC2\xB7 38 people here",
                                "anyone may look and leave something",
                                LPX_B_TRUE,
                                LPX_P_NEUTRAL,
                                LPX_PH_MANIFEST,
                                0};

    layer(c, 0);
    c->bg = ZUI_VOID;
    heading(c, &F_SEMI, compact ? 20 : 24, x0, y0 + PT(c, 20), ZUI_TEXT, "Spaces");
    if (!compact)
        text(c, &F_REG, 13, x0 + PT(c, 100), y0 + PT(c, 20), ZUI_MUTED,
             "a house, offices and a team room inside; your cloud top and the park outside");
    int32_t y = y0 + PT(c, compact ? 36 : 44);

    if (compact) {
        const room_t *inside[4] = {&HOME, &OFF1, &TEAM, &OFF2};
        for (int i = 0; i < 4; i++) {
            room(c, (zui_rect_t){x0, y, W, PT(c, 86)}, inside[i], i == 0);
            y += PT(c, 94);
        }
        layer(c, 0);
        c->bg = ZUI_VOID;
        zui_fill_rrect(&c->s, R(c, x0, y, W, PT(c, 70)), PT(c, ZUI_R_PANEL), C(c, ZUI_VOID), 255);
        zui_dash_rrect(&c->s, R(c, x0, y, W, PT(c, 70)), PT(c, ZUI_R_PANEL), 1, PT(c, 4),
                       C(c, ZUI_GOLD));
        pict(c, PG_HALL, x0 + PT(c, 24), y + PT(c, 22), 16, ZUI_GOLD_GLOW);
        text(c, &F_MED, 11, x0 + PT(c, 44), y + PT(c, 26), ZUI_GOLD_GLOW,
             "IN THE HALL \xC2\xB7 HELD");
        text(c, &F_REG, 13, x0 + PT(c, 16), y + PT(c, 52), ZUI_TEXT,
             "budget-v3.pdf  Home \xE2\x86\x92 Cloud top");
        text_r(c, &F_MED, 13, x0 + W - PT(c, 16), y + PT(c, 52), ZUI_GOLD, "Confirm");
        y += PT(c, 78);
        room(c, (zui_rect_t){x0, y, W, PT(c, 86)}, &CLOUD, 0);
        y += PT(c, 94);
        room(c, (zui_rect_t){x0, y, W, PT(c, 120)}, &PARK, 0);
        tag(c, LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_MANIFEST);
        pict(c, PG_WATCH, x0 + PT(c, 26), y + PT(c, 76), 14, ZUI_TEXT_2);
        text(c, &F_REG, 12, x0 + PT(c, 44), y + PT(c, 80), ZUI_TEXT_2,
             "Watch hall \xC2\xB7 14 watching \xC2\xB7 14 serving");
        return;
    }

    /* inside: four rooms in a row */
    int32_t rh = PT(c, 210), gap = PT(c, 12);
    int32_t w1 = W * 30 / 100, w2 = W * 22 / 100, w3 = W * 16 / 100,
            w4 = W - w1 - w2 - w3 - 3 * gap;
    int32_t rx = x0;
    room(c, (zui_rect_t){rx, y, w1, rh}, &HOME, 1);
    { /* home: a few of your files */
        static const char *f[4] = {"Grants", "Field notes", "budget-v3.pdf", "Photos"};
        static const pict_t k[4] = {PG_FOLDER, PG_FOLDER, PG_FILE, PG_FOLDER};
        for (int i = 0; i < 4; i++)
            file_chip(c, rx + PT(c, 16) + (i % 2) * ((w1 - PT(c, 40)) / 2 + PT(c, 8)),
                      y + PT(c, 70) + (i / 2) * PT(c, 36), (w1 - PT(c, 40)) / 2, k[i], f[i]);
    }
    rx += w1 + gap;
    room(c, (zui_rect_t){rx, y, w2, rh}, &OFF1, 0);
    avatar(c, rx + PT(c, 30), y + PT(c, 92), "MC", ZUI_TEXT_2);
    avatar(c, rx + PT(c, 58), y + PT(c, 92), "AR", ZUI_TEXT_2);
    text(c, &F_REG, 12, rx + PT(c, 16), y + PT(c, 134), ZUI_MUTED, "Grant application draft");
    rx += w2 + gap;
    room(c, (zui_rect_t){rx, y, w3, rh}, &OFF2, 0);
    rx += w3 + gap;
    room(c, (zui_rect_t){rx, y, w4, rh}, &TEAM, 0);
    layer(c, 0);
    c->bg = ZUI_PANEL;
    text(c, &F_REG, 12, rx + PT(c, 16), y + PT(c, 84), ZUI_TEXT_2,
         "Proposal: open the team room to the Park");
    split_chip(c, rx + PT(c, 16), y + PT(c, 96), "3 yes", "1 objection");
    text(c, &F_REG, 12, rx + PT(c, 16), y + PT(c, 140), ZUI_MUTED, "quorum met with an objection:");
    text(c, &F_REG, 12, rx + PT(c, 16), y + PT(c, 158), ZUI_MUTED,
         "the members settle it together");
    y += rh + gap;

    /* the hall: the space between spaces */
    int32_t hh = PT(c, 72);
    layer(c, 0);
    c->bg = ZUI_VOID;
    zui_fill(&c->s, R(c, x0, y, W, hh), C(c, ZUI_ENGRAVE_LO));
    engrave(c, (zui_rect_t){x0, y, W, hh}, 4);
    pict(c, PG_HALL, x0 + PT(c, 26), y + hh / 2, 18, ZUI_MUTED);
    text(c, &F_MED, 11, x0 + PT(c, 48), y + PT(c, 30), ZUI_MUTED, "HALL");
    text(c, &F_REG, 12, x0 + PT(c, 48), y + PT(c, 48), ZUI_FAINT, "the space between spaces");
    { /* a held crossing: toward a wider audience, so it waits for you */
        int32_t cx = x0 + PT(c, 250), cw = PT(c, 470);
        zui_rect_t cr = R(c, cx, y + PT(c, 12), cw, hh - PT(c, 24));
        zui_fill_rrect(&c->s, cr, PT(c, ZUI_R_CONTROL), C(c, ZUI_PANEL), 255);
        zui_dash_rrect(&c->s, cr, PT(c, ZUI_R_CONTROL), 1, PT(c, 4), C(c, ZUI_GOLD));
        c->bg = ZUI_PANEL;
        pict(c, PG_FILE, cx + PT(c, 20), y + hh / 2, 14, ZUI_TEXT_2);
        text(c, &F_REG, 13, cx + PT(c, 36), y + hh / 2 + PT(c, 5), ZUI_TEXT,
             "budget-v3.pdf   Home \xE2\x86\x92 Cloud top");
        text(c, &F_MONO, 11, cx + PT(c, 280), y + hh / 2 + PT(c, 4), ZUI_MUTED, "held");
        int32_t bw = text_w(c, &F_MED, 13, "Confirm") + PT(c, 24);
        zui_fill_rrect(&c->s, R(c, cx + cw - bw - PT(c, 10), y + PT(c, 20), bw, hh - PT(c, 40)),
                       PT(c, ZUI_R_CONTROL), C(c, ZUI_GOLD), 255);
        text(c, &F_MED, 13, cx + cw - bw + PT(c, 2), y + hh / 2 + PT(c, 5), ZUI_VOID, "Confirm");
        c->bg = ZUI_VOID;
    }
    text_r(c, &F_REG, 12, x0 + W - PT(c, 16), y + hh / 2 + PT(c, 5), ZUI_FAINT,
           "nothing reaches a wider audience without you");
    y += hh + gap;

    /* outside: your cloud top and the park */
    int32_t oh = A.y + A.h - y - PT(c, 20);
    if (oh > PT(c, 300)) oh = PT(c, 300);
    int32_t wc = W * 34 / 100, wp = W - wc - gap;
    room(c, (zui_rect_t){x0, y, wc, oh}, &CLOUD, 0);
    layer(c, 0);
    c->bg = ZUI_PANEL;
    file_chip(c, x0 + PT(c, 16), y + PT(c, 70), wc - PT(c, 32), PG_FILE,
              "Tonopah array: public readings");
    file_chip(c, x0 + PT(c, 16), y + PT(c, 106), wc - PT(c, 32), PG_FILE, "About me");
    text(c, &F_MONO, 11, x0 + PT(c, 16), y + PT(c, 160), ZUI_FAINT,
         "12 visitors today \xC2\xB7 cid bafy\xE2\x80\xA6q4mz");
    if (oh >=
        PT(c, 280)) { /* make another public space: a CID anchors it, visitors help serve it */
        int32_t bw = text_w(c, &F_MED, 13, "Make a public space") + PT(c, 28), by = y + PT(c, 182);
        zui_rect_t br = R(c, x0 + PT(c, 16), by, bw, PT(c, 32));
        zui_fill_rrect(&c->s, br, PT(c, ZUI_R_CONTROL), C(c, ZUI_PANEL), 255);
        zui_stroke_rrect(&c->s, br, PT(c, ZUI_R_CONTROL), 1, C(c, ZUI_GOLD), 255);
        text(c, &F_MED, 13, x0 + PT(c, 30), by + PT(c, 21), ZUI_GOLD, "Make a public space");
        text(c, &F_REG, 11, x0 + PT(c, 16), by + PT(c, 52), ZUI_FAINT,
             "its CID is its address; everyone who visits helps host it");
    }

    int32_t px = x0 + wc + gap;
    room(c, (zui_rect_t){px, y, wp, oh}, &PARK, 0);
    /* the watch hall: everyone watching also serves, so it gets faster */
    tag(c, LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_EMERGING);
    c->bg = ZUI_PANEL;
    int32_t wx = px + PT(c, 16), wy = y + PT(c, 64), ww = wp * 52 / 100;
    zui_fill_rrect(&c->s, R(c, wx, wy, ww, oh - PT(c, 104)), PT(c, ZUI_R_PANEL), C(c, ZUI_VOID),
                   255);
    engrave(c, (zui_rect_t){wx, wy, ww, oh - PT(c, 104)}, ZUI_R_PANEL);
    pict(c, PG_WATCH, wx + PT(c, 24), wy + PT(c, 24), 18, ZUI_TEXT);
    c->bg = ZUI_VOID;
    text(c, &F_SEMI, 14, wx + PT(c, 46), wy + PT(c, 29), ZUI_TEXT, "Watch hall");
    text(c, &F_REG, 12, wx + PT(c, 16), wy + PT(c, 58), ZUI_TEXT_2,
         "Tonopah sky timelapse \xC2\xB7 together");
    for (int i = 0; i < 6; i++) {
        static const char *ini[6] = {"MC", "AR", "JT", "KO", "LS", "+9"};
        avatar(c, wx + PT(c, 28) + i * PT(c, 26), wy + PT(c, 88), ini[i],
               i == 0 ? ZUI_GOLD_GLOW : ZUI_TEXT_2);
    }
    text(c, &F_MONO, 11, wx + PT(c, 16), wy + PT(c, 124), ZUI_MUTED,
         "14 watching \xC2\xB7 14 peers serving");
    budget_bar(c, wx + PT(c, 16), wy + PT(c, 134), ww - PT(c, 32), 945, 1000, ZUI_OK);
    text(c, &F_MONO, 11, wx + PT(c, 16), wy + PT(c, 152), ZUI_MUTED,
         "9.4\xC3\x97 the origin's capacity \xC2\xB7 ipfs");
    /* open workspaces in the park */
    layer(c, 0);
    c->bg = ZUI_PANEL;
    int32_t sx = wx + ww + PT(c, 20), sw = px + wp - sx - PT(c, 16);
    text(c, &F_MED, 11, sx, wy + PT(c, 14), ZUI_MUTED, "OPEN WORKSPACES");
    static const char *ws[3][2] = {{"Seed library", "flat \xC2\xB7 60%"},
                                   {"Mesh radio club", "hierarchical"},
                                   {"Desert data commons", "flat \xC2\xB7 majority"}};
    for (int i = 0; i < 3; i++) {
        int32_t yy = wy + PT(c, 26) + i * PT(c, 34);
        file_chip(c, sx, yy, sw, PG_TEAM, ws[i][0]);
        text_r(c, &F_MONO, 10, sx + sw - PT(c, 10), yy + PT(c, 18), ZUI_FAINT, ws[i][1]);
    }
    int32_t bw = text_w(c, &F_MED, 13, "Start a workspace") + PT(c, 28);
    int32_t by = wy + PT(c, 26) + 3 * PT(c, 34) + PT(c, 6);
    zui_fill_rrect(&c->s, R(c, sx, by, bw, PT(c, 32)), PT(c, ZUI_R_CONTROL), C(c, ZUI_GOLD), 255);
    text(c, &F_MED, 13, sx + PT(c, 14), by + PT(c, 21), ZUI_VOID, "Start a workspace");
    text(c, &F_REG, 11, sx, by + PT(c, 50), ZUI_FAINT,
         "choose flat or hierarchical; change it later");
}

static void spaces_screen(ctx_t *c)
{
    fill(c, (zui_rect_t){0, 0, c->s.w, c->s.h}, ZUI_VOID);
    zui_rect_t A = c->p.main;
    if (c->p.list.w) {
        A.x = c->p.list.x;
        A.w += c->p.list.w;
    }
    spaces_plan(c, A);
    rail(c, 0);
    bottom_bar(c, 0);
    top_bar(c, "Spaces");
}

/* ---- Home: your desktop and files ---------------------------------------- */
static void home_screen(ctx_t *c)
{
    fill(c, (zui_rect_t){0, 0, c->s.w, c->s.h}, ZUI_VOID);
    zui_rect_t A = c->p.main;
    if (c->p.list.w) {
        A.x = c->p.list.x;
        A.w += c->p.list.w;
    }
    int32_t g = PT(c, 32), x0 = A.x + g, W = A.w - 2 * g, y = A.y + PT(c, 48);
    layer(c, 0);
    c->bg = ZUI_VOID;
    heading(c, &F_SEMI, 24, x0, y, ZUI_TEXT, "Home");
    text(c, &F_REG, 13, x0 + PT(c, 84), y, ZUI_MUTED,
         "only you \xC2\xB7 ~/Documents/ZXV \xC2\xB7 412 files \xC2\xB7 2.1 GB");
    y += PT(c, 32);
    /* folders as carved tiles */
    static const char *fn[8] = {"Grants", "Field notes", "Photos",   "Ledger exports",
                                "Music",  "Models",      "Hardware", "Inbox"};
    static const char *fc[8] = {"14 files",  "38 files", "syncing",    "2 versions",
                                "61 tracks", "3 packs",  "local only", "6 new"};
    int cols = W > PT(c, 900) ? 4 : 3;
    int32_t tw = (W - (cols - 1) * PT(c, 14)) / cols, th = PT(c, 96);
    for (int i = 0; i < 8; i++) {
        int32_t tx = x0 + (i % cols) * (tw + PT(c, 14)), ty = y + (i / cols) * (th + PT(c, 14));
        if (i == 7)
            tag(c, LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_EMERGING);
        else if (i == 3) /* two versions disagree: both stay visible */
            tag(c, LPX_B_BOTH, LPX_P_NEUTRAL, LPX_PH_MANIFEST);
        else if (i == 2) /* still syncing: unsettled, so angular and brief */
            tag(c, LPX_B_TRUE, LPX_P_ENTROPIC, LPX_PH_EMERGING);
        else if (i == 6) /* local only: held at the glass */
            layer(c, 0);
        else
            tag(c, LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_MANIFEST);
        c->bg = ZUI_PANEL;
        /* shape is an axis too: the tag rounds or squares the corner */
        int32_t rpt = c->pl ? zui_pxl_radius(c->lt, ZUI_R_PANEL) : ZUI_R_PANEL, rad = PT(c, rpt);
        zui_fill_rrect(&c->s, R(c, tx, ty, tw, th), rad, C(c, ZUI_PANEL), 255);
        stamp(c, R(c, tx, ty, tw, th), rad, 24);
        engrave(c, (zui_rect_t){tx, ty, tw, th}, rpt);
        pict(c, PG_FOLDER, tx + PT(c, 30), ty + PT(c, 34), 24, i == 7 ? ZUI_GOLD_GLOW : ZUI_TEXT_2);
        heading(c, &F_MED, 14, tx + PT(c, 18), ty + PT(c, 62), ZUI_TEXT, fn[i]);
        text(c, &F_MONO, 11, tx + PT(c, 18), ty + PT(c, 79), ZUI_MUTED, fc[i]);
        zui_foil_rect(&c->s, &c->d, R(c, tx, ty, tw, th), rad, c->z ? c->z : PT(c, 10));
    }
    y += 2 * (th + PT(c, 14)) + (8 > 2 * cols ? th + PT(c, 14) : 0) + PT(c, 10);
    /* a held AI tidy-up: deterministic, waits for you */
    layer(c, 0);
    c->bg = ZUI_PANEL;
    int32_t ch = PT(c, 112);
    zui_rect_t cr = R(c, x0, y, W, ch);
    zui_fill_rrect(&c->s, cr, PT(c, ZUI_R_PANEL), C(c, ZUI_PANEL), 255);
    zui_dash_rrect(&c->s, cr, PT(c, ZUI_R_PANEL), 1, PT(c, 5), C(c, ZUI_GOLD));
    stamp(c, cr, PT(c, ZUI_R_PANEL), 0); /* held: it waits at the glass */
    text(c, &F_MED, 11, x0 + PT(c, 16), y + PT(c, 26), ZUI_GOLD_GLOW,
         "SUGGESTED TIDY-UP \xC2\xB7 HELD");
    text(c, &F_REG, 14, x0 + PT(c, 16), y + PT(c, 52), ZUI_TEXT,
         "File the 6 new Inbox items: 4 into Field notes, 2 receipts into Ledger exports.");
    receipt(c, x0 + PT(c, 16), y + PT(c, 78), 0,
            "e19b72\xE2\x80\xA6"
            "044c");
    int32_t bw = button(c, x0 + W - PT(c, 200), y + PT(c, 64), "Accept", 1);
    button(c, x0 + W - PT(c, 200) + bw + PT(c, 8), y + PT(c, 64), "Reject", 0);
    rail(c, 0);
    bottom_bar(c, 0);
    top_bar(c, "Home");
    tag(c, LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    stamp(c, c->p.rail, 0, 14);
    stamp(c, c->p.top, 0, 20);
}

/* ---- the Chiglet: companion pop-up and Web4 ------------------------------ */
static const zui_chiglet_t MY_CHG = {{0, 5, 3, 1, 6, 2, 3, 1}, 3, ZUI_CHG_SPEAKING};

static void chiglet_at(ctx_t *c, const zui_chiglet_t *k, int32_t cx, int32_t cy, int32_t r_pt,
                       uint32_t tick)
{
    zui_chiglet_draw(&c->s, k, cx + c->dx, cy + c->dy, PT(c, r_pt), tick);
}

/* the companion pops up beside what you are doing, with one short line */
static void chiglet_popup(ctx_t *c, const char *line1, const char *line2)
{
    tag(c, LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_EMERGING);
    c->bg = ZUI_PANEL;
    int compact = c->p.cls == ZUI_COMPACT;
    int32_t bw = PT(c, compact ? 300 : 340), bh = PT(c, 74);
    int32_t mx = c->p.main.x + c->p.main.w, my = c->p.main.y + c->p.main.h;
    int32_t bx = mx - bw - PT(c, compact ? 100 : 130), by = my - bh - PT(c, compact ? 84 : 96);
    if (bx < c->p.main.x + PT(c, 8)) bx = c->p.main.x + PT(c, 8);
    zui_rect_t r = R(c, bx, by, bw, bh);
    zui_fill_rrect(&c->s, r, PT(c, 12), C(c, ZUI_PANEL), 255);
    zui_stroke_rrect(&c->s, r, PT(c, 12), 1, C(c, zui_chiglet_accent(&MY_CHG)), 255);
    text(c, &F_MED, 11, bx + PT(c, 14), by + PT(c, 20), ZUI_GOLD_GLOW,
         "TALA \xC2\xB7 YOUR CHIGLET \xC2\xB7 GEN 3");
    text_fit(c, &F_REG, 13, bx + PT(c, 14), by + PT(c, 42), bw - PT(c, 28), ZUI_TEXT, line1);
    text_fit(c, &F_REG, 13, bx + PT(c, 14), by + PT(c, 62), bw - PT(c, 28), ZUI_TEXT_2, line2);
    chiglet_at(c, &MY_CHG, bx + bw + PT(c, compact ? 40 : 60), by + bh / 2, compact ? 22 : 28, 5);
}

static void studio_with_chiglet(ctx_t *c)
{
    studio(c);
    chiglet_popup(c, "The equipment total disagrees with the ledger.",
                  "I held the fix for you. Accept it, or ask me why.");
}

/* Web4: your Chiglet meeting other people's Chiglets, peer to peer */
typedef struct {
    const char *owner, *name;
    zui_chiglet_t k;
} peer_chg_t;

static void streaks(ctx_t *c, zui_rect_t A, uint32_t seed)
{
    /* the accelerated web: thin speed lines carved into the ground */
    for (int i = 0; i < 40; i++) {
        seed = seed * 1664525u + 1013904223u;
        int32_t y = A.y + (int32_t) (seed % (uint32_t) A.h);
        int32_t x = A.x + (int32_t) ((seed >> 8) % (uint32_t) A.w);
        int32_t w = PT(c, 40) + (int32_t) ((seed >> 16) % (uint32_t) PT(c, 220));
        uint32_t col = (seed >> 3) & 1u ? ZUI_HAIRLINE : ZUI_ENGRAVE_LO;
        zui_hline(&c->s, x + c->dx, y + c->dy, w, 1, C(c, col));
    }
}

static void envelope(ctx_t *c, int32_t x, int32_t y, int32_t w, const char *kind, const char *what,
                     const char *meta, int held)
{
    int32_t h = PT(c, 54);
    zui_rect_t r = R(c, x, y, w, h);
    zui_fill_rrect(&c->s, r, PT(c, ZUI_R_CONTROL), C(c, ZUI_PANEL), 255);
    if (held)
        zui_dash_rrect(&c->s, r, PT(c, ZUI_R_CONTROL), 1, PT(c, 4), C(c, ZUI_GOLD));
    else
        engrave(c, (zui_rect_t){x, y, w, h}, ZUI_R_CONTROL);
    uint32_t bg = c->bg;
    c->bg = ZUI_PANEL;
    text(c, &F_MONO_MED, 10, x + PT(c, 12), y + PT(c, 20), held ? ZUI_GOLD_GLOW : ZUI_OK, kind);
    text_fit(c, &F_REG, 13, x + PT(c, 92), y + PT(c, 20), w - PT(c, 104), ZUI_TEXT, what);
    text_fit(c, &F_MONO, 10, x + PT(c, 12), y + PT(c, 41), w - PT(c, 24), ZUI_MUTED, meta);
    c->bg = bg;
}

static void web4_screen(ctx_t *c)
{
    fill(c, (zui_rect_t){0, 0, c->s.w, c->s.h}, ZUI_VOID);
    zui_rect_t A = c->p.main;
    if (c->p.list.w) {
        A.x = c->p.list.x;
        A.w += c->p.list.w;
    }
    layer(c, -40);
    c->bg = ZUI_VOID; /* the network, behind the glass */
    streaks(c, A, 7);
    layer(c, 0);
    int32_t g = PT(c, 32), x0 = A.x + g, W = A.w - 2 * g, y = A.y + PT(c, 48);
    heading(c, &F_SEMI, 24, x0, y, ZUI_TEXT, "Web4");
    text(c, &F_REG, 13, x0 + PT(c, 82), y, ZUI_MUTED,
         "companions meeting companions \xC2\xB7 signed, peer to peer, no server in the middle");
    y += PT(c, 40);

    /* your Chiglet on the left, forward: it is acting for you */
    int32_t lx = x0 + PT(c, 110), ly = y + PT(c, 190);
    tag(c, LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST);
    chiglet_at(c, &MY_CHG, lx, ly, 62, 9);
    { /* caption centred under her, kept inside her own column */
        static const char *cap[4] = {"Tala", "your companion \xC2\xB7 gen 3", "grown with you",
                                     "dna 00 05 03 01 06 02 03 01"};
        const ttf_font_t *cf[4] = {&F_SEMI, &F_REG, &F_REG, &F_MONO};
        static const int cs[4] = {16, 12, 12, 10};
        static const uint32_t cc[4] = {ZUI_TEXT, ZUI_MUTED, ZUI_MUTED, ZUI_FAINT};
        static const int cy[4] = {112, 132, 148, 168};
        for (int i = 0; i < 4; i++)
            text(c, cf[i], cs[i], lx - text_w(c, cf[i], cs[i], cap[i]) / 2, ly + PT(c, cy[i]),
                 cc[i], cap[i]);
    }

    /* peers' Chiglets on the right, each on its own person's device */
    static const peer_chg_t P[3] = {
        {"Seed library (park workspace)", "Moss", {{1, 2, 2, 2, 3, 1, 1, 0}, 2, ZUI_CHG_DECIDED}},
        {"A. Ramos (Grant team)", "Pip", {{2, 6, 1, 0, 2, 3, 4, 2}, 1, ZUI_CHG_SPEAKING}},
        {"Mesh radio club", "Static", {{4, 1, 3, 2, 0, 3, 2, 3}, 4, ZUI_CHG_UNCERTAIN}},
    };
    int32_t rx = x0 + W - PT(c, 90);
    for (int i = 0; i < 3; i++) {
        int32_t py = y + PT(c, 50) + i * PT(c, 150);
        if (P[i].k.mood == ZUI_CHG_UNCERTAIN)
            tag(c, LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_EMERGING);
        else
            tag(c, LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_MANIFEST);
        chiglet_at(c, &P[i].k, rx, py + PT(c, 30), 30, 9 + (uint32_t) i);
        text_r(c, &F_SEMI, 14, rx - PT(c, 56), py + PT(c, 26), ZUI_TEXT, P[i].name);
        text_r(c, &F_REG, 11, rx - PT(c, 56), py + PT(c, 44), ZUI_MUTED, P[i].owner);
    }

    /* lanes: from Tala's edge to the near edge of each peer's label; drawn
     * first so the envelopes ride on top of them */
    tag(c, LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_EMERGING);
    for (int i = 0; i < 3; i++) {
        int32_t py = y + PT(c, 50) + i * PT(c, 150) + PT(c, 30);
        int32_t lw = text_w(c, &F_SEMI, 14, P[i].name), ow = text_w(c, &F_REG, 11, P[i].owner);
        int32_t x1 = lx + PT(c, 80), x2 = rx - PT(c, 56) - (lw > ow ? lw : ow) - PT(c, 14);
        int32_t steps = (x2 - x1) / PT(c, 7);
        if (steps < 2) steps = 2;
        for (int k = 0; k <= steps; k++)
            dot(c, x1 + (x2 - x1) * k / steps, ly + (py - ly) * k / steps, PT(c, 1) + 1,
                i == 2 ? ZUI_GOLD : ZUI_OK);
    }

    /* the conversation between them: signed envelopes on the lanes */
    layer(c, 0);
    c->bg = ZUI_VOID;
    int32_t ex = lx + PT(c, 130), ew = rx - PT(c, 240) - ex;
    int32_t ey = y + PT(c, 34);
    envelope(c, ex, ey, ew, "REQUEST",
             "Tala \xE2\x86\x92 Moss: drought-hardy seed list for Tonopah",
             "ml-dsa-65 signed \xC2\xB7 seq 41 \xC2\xB7 tool seeds.search \xC2\xB7 free \xC2\xB7 "
             "via cid bafy\xE2\x80\xA6"
             "7k2q, 2 hops",
             0);
    envelope(c, ex, ey + PT(c, 64), ew, "RESPONSE",
             "Moss \xE2\x86\x92 Tala: 12 varieties, 3 held in the library",
             "signature checked \xC2\xB7 0.4 s \xC2\xB7 deterministic replay 51ac\xE2\x80\xA6"
             "e0",
             0);
    envelope(c, ex, ey + PT(c, 150), ew, "STREAM",
             "Pip \xE2\x86\x94 Tala: drafting the budget note together",
             "chunk 6 of ~9 \xC2\xB7 both people can see every line", 0);
    envelope(c, ex, ey + PT(c, 236), ew, "CONSENT", "Static asks Tala to share your call sign",
             "personal data: waits for you \xC2\xB7 your Chiglet never shares it on its own", 1);
    {
        int32_t by = ey + PT(c, 236) + PT(c, 62);
        int32_t bw = button(c, ex, by, "Allow once", 1);
        button(c, ex + bw + PT(c, 8), by, "Decline", 0);
    }
    rail(c, 2);
    bottom_bar(c, 2);
    top_bar(c, "Web4");
}

/* ---- Swarm: how each agent feels and whether it is working ---------------- */
static void fib_pips(ctx_t *c, int32_t x, int32_t base, uint8_t k, uint32_t rgb)
{
    static const int32_t H[5] = {3, 5, 7, 10, 14}; /* charge 1 2 3 5 8, scaled */
    for (int i = 0; i < 5; i++) {
        zui_rect_t b = R(c, x + i * PT(c, 6), base - PT(c, H[i]), PT(c, 4), PT(c, H[i]));
        zui_fill_rrect(&c->s, b, PT(c, 1), C(c, i < k ? rgb : ZUI_HAIRLINE), 255);
    }
}

static void swarm_screen(ctx_t *c)
{
    fill(c, (zui_rect_t){0, 0, c->s.w, c->s.h}, ZUI_VOID);
    zui_rect_t A = c->p.main;
    if (c->p.list.w) {
        A.x = c->p.list.x;
        A.w += c->p.list.w;
    }
    int compact = c->p.cls == ZUI_COMPACT;
    int32_t g = PT(c, compact ? 16 : 32), x0 = A.x + g, W = A.w - 2 * g,
            y = A.y + PT(c, compact ? 40 : 48);
    layer(c, 0);
    c->bg = ZUI_VOID;
    heading(c, &F_SEMI, 24, x0, y, ZUI_TEXT, "Swarm");
    if (!compact)
        text(c, &F_REG, 13, x0 + PT(c, 92), y, ZUI_MUTED,
             "how each agent feels and whether it is working \xC2\xB7 live \xC2\xB7 all on this "
             "Mac");
    y += PT(c, 28);

    /* the swarm's mood, large */
    int32_t mw = compact ? W : PT(c, 300), mh = compact ? PT(c, 150) : PT(c, 420);
    zui_rect_t mr = {x0, y, mw, mh};
    c->look = zui_look(zui_emote_tag(SWARM_MOOD, ZUI_HEALTH_OK));
    place(c, c->look.z_pt);
    zui_fill_rrect(&c->s, R(c, mr.x, mr.y, mr.w, mr.h), PT(c, ZUI_R_PANEL), C(c, ZUI_PANEL), 255);
    engrave(c, mr, ZUI_R_PANEL);
    c->bg = ZUI_PANEL;
    if (compact) {
        face_at(c, zui_face_feeling(SWARM_MOOD), ZUI_HEALTH_OK, x0 + PT(c, 60), y + mh / 2, 40);
        text(c, &F_MED, 11, x0 + PT(c, 120), y + PT(c, 44), ZUI_MUTED, "SWARM MOOD");
        text(c, &F_SEMI, 20, x0 + PT(c, 120), y + PT(c, 72), ZUI_TEXT, "calm \xC2\xB7 3");
        text(c, &F_MONO, 11, x0 + PT(c, 120), y + PT(c, 98), ZUI_TEXT_2,
             "597 of 4,181 tok on feeling");
        text(c, &F_REG, 12, x0 + PT(c, 120), y + PT(c, 120), ZUI_MUTED, "the rest split by level");
        y += mh + PT(c, 12);
    } else {
        face_at(c, zui_face_feeling(SWARM_MOOD), ZUI_HEALTH_OK, x0 + mw / 2, y + PT(c, 130), 82);
        text(c, &F_MED, 11, x0 + PT(c, 24), y + PT(c, 260), ZUI_MUTED, "SWARM MOOD");
        text(c, &F_SEMI, 22, x0 + PT(c, 24), y + PT(c, 290), ZUI_TEXT, "Calm, intensity 3");
        fib_pips(c, x0 + mw - PT(c, 54), y + PT(c, 290), 3, ZUI_OK);
        para(c, &F_REG, 13, x0 + PT(c, 24), y + PT(c, 318), mw - PT(c, 48), 19, ZUI_TEXT_2,
             "Charge 3 of 21 puts 597 of this cycle's 4,181 tokens on the feeling axis. The rest "
             "is split by swarm level, Fibonacci as always.");
        text(c, &F_MONO, 11, x0 + PT(c, 24), y + mh - PT(c, 20), ZUI_FAINT,
             "U+1F60C \xC2\xB7 cycle 1,204");
    }

    /* one row per agent */
    int32_t lx = compact ? x0 : x0 + mw + PT(c, 20), lw = compact ? W : W - mw - PT(c, 20);
    int32_t ly = compact ? y : y, rh = PT(c, compact ? 76 : 72);
    layer(c, 0);
    c->bg = ZUI_VOID;
    if (!compact) {
        text(c, &F_MED, 11, lx + PT(c, 16), ly + PT(c, 14), ZUI_MUTED, "AGENTS");
        text_r(c, &F_MED, 11, lx + lw - PT(c, 16), ly + PT(c, 14), ZUI_MUTED,
               "TOKENS  LOGIC + FEELING");
        ly += PT(c, 24);
    }
    for (int i = 0; i < 5; i++) {
        const sw_agent_t *a = &SW_AGENTS[i];
        zui_rect_t row = {lx, ly + i * (rh + PT(c, 8)), lw, rh};
        c->look = zui_look(zui_emote_tag(a->now, a->h));
        place(c, c->look.z_pt);
        c->bg = ZUI_VOID;
        zui_fill_rrect(&c->s, R(c, row.x, row.y, row.w, row.h), PT(c, ZUI_R_PANEL), C(c, ZUI_PANEL),
                       255);
        if (a->h == ZUI_HEALTH_FAILING)
            zui_stroke_rrect(&c->s, R(c, row.x, row.y, row.w, row.h), PT(c, ZUI_R_PANEL), 1,
                             C(c, ZUI_ALERT), 200);
        else
            engrave(c, row, ZUI_R_PANEL);
        c->bg = ZUI_PANEL;
        face_at(c, agent_face(i), a->h, row.x + PT(c, 36), row.y + rh / 2, 20);
        int32_t tx = row.x + PT(c, 72);
        text(c, &F_SEMI, 14, tx, row.y + PT(c, 28), ZUI_TEXT, a->name);
        text(c, &F_REG, 12, tx + text_w(c, &F_SEMI, 14, a->name) + PT(c, 8), row.y + PT(c, 28),
             ZUI_MUTED, a->role);
        char feel[160];
        const swarm_emotion_profile_t *pr = swarm_emotion_profile(a->now.emotion);
        if (a->now.intensity)
            snprintf(feel, sizeof feel, "%s %u \xC2\xB7 temp %u.%02u \xC2\xB7 checks %ux",
                     zui_emote_word(a->now.emotion), a->now.intensity,
                     pr->temperature_milli / 1000u, (pr->temperature_milli % 1000u) / 10u,
                     pr->verify_passes);
        else
            snprintf(feel, sizeof feel, "neutral \xC2\xB7 no feeling charge");
        if (i == 2 && MORPH_T < 256)
            snprintf(feel, sizeof feel, "curious 3 \xE2\x86\x92 worried 4 \xC2\xB7 now checks 3x");
        text(c, &F_REG, 12, tx, row.y + PT(c, 48), ZUI_TEXT_2, feel);
        uint32_t hc = a->h == ZUI_HEALTH_FAILING ? ZUI_ALERT
                      : a->h == ZUI_HEALTH_SLOW  ? ZUI_GOLD
                                                 : ZUI_TEXT_2;
        if (!compact) text(c, &F_REG, 12, tx + PT(c, 250), row.y + PT(c, 48), hc, a->note);
        fib_pips(c, row.x + lw - PT(c, compact ? 44 : 190), row.y + PT(c, 30), a->now.intensity,
                 a->h == ZUI_HEALTH_FAILING ? ZUI_ALERT : ZUI_GOLD);
        if (!compact) {
            char tok[64];
            snprintf(tok, sizeof tok, "%u + %u", a->re, a->im);
            text_r(c, &F_MONO, 12, row.x + lw - PT(c, 16), row.y + PT(c, 30), ZUI_TEXT, tok);
            text_r(c, &F_MONO, 10, row.x + lw - PT(c, 16), row.y + PT(c, 48), ZUI_FAINT,
                   a->h == ZUI_HEALTH_OK     ? "ok"
                   : a->h == ZUI_HEALTH_SLOW ? "slow"
                                             : "failing");
        }
    }
    y = (compact ? ly : ly) + 5 * (rh + PT(c, 8)) + PT(c, compact ? 8 : 20);
    if (!compact && y < A.y + PT(c, 520)) y = A.y + PT(c, 520);

    /* a change of feeling is a walk, not a swap */
    if (!compact || y + PT(c, 120) < A.y + A.h) {
        layer(c, 0);
        c->bg = ZUI_VOID;
        text(c, &F_MED, 11, x0, y + PT(c, 14), ZUI_MUTED,
             "A FEELING CHANGES BY WALKING, NOT SWAPPING");
        int n = compact ? 5 : 9;
        int32_t step = (W - PT(c, 40)) / (n - 1), fy = y + PT(c, 62);
        zui_face_t a = zui_face_feeling(SW_AGENTS[2].was), b = zui_face_feeling(SW_AGENTS[2].now);
        for (int i = 0; i < n; i++) {
            uint32_t t = (uint32_t) (i * 256 / (n - 1));
            face_at(c, zui_face_lerp(a, b, t), ZUI_HEALTH_OK, x0 + PT(c, 20) + i * step, fy,
                    compact ? 18 : 22);
            if (!compact) {
                char lab[16];
                snprintf(lab, sizeof lab, "%u ms", (unsigned) (t * ZUI_EMOTE_MORPH_MS / 256u));
                int32_t w = text_w(c, &F_MONO, 10, lab);
                text(c, &F_MONO, 10, x0 + PT(c, 20) + i * step - w / 2, fy + PT(c, 40), ZUI_FAINT,
                     lab);
            }
        }
        if (!compact)
            text(c, &F_REG, 12, x0, fy + PT(c, 66), ZUI_MUTED,
                 "checker, curious 3 \xE2\x86\x92 worried 4: every frame between is a real face, "
                 "so you see it change, not flicker");
    }
    rail(c, 0);
    bottom_bar(c, 0);
    top_bar(c, "Swarm");
}

/* one big face walking through every feeling, for the morph animation */
static swarm_feeling_t CYC_A = {SWARM_EMO_NEUTRAL, 0}, CYC_B = {SWARM_EMO_CALM, 5};
static void face_cycle(ctx_t *c)
{
    fill(c, (zui_rect_t){0, 0, c->s.w, c->s.h}, ZUI_VOID);
    layer(c, 0);
    c->bg = ZUI_VOID;
    int32_t cx = c->s.w / 2, cy = c->s.h / 2 - PT(c, 30);
    zui_face_t f =
        zui_face_lerp(zui_face_feeling(CYC_A), zui_face_feeling(CYC_B), zui_emote_ease(MORPH_T));
    face_at(c, f, ZUI_HEALTH_OK, cx, cy, 120);
    char lab[96];
    if (MORPH_T >= 256)
        snprintf(lab, sizeof lab, "%s", zui_emote_word(CYC_B.emotion));
    else
        snprintf(lab, sizeof lab, "%s \xE2\x86\x92 %s", zui_emote_word(CYC_A.emotion),
                 zui_emote_word(CYC_B.emotion));
    int32_t w = text_w(c, &F_MED, 20, lab);
    text(c, &F_MED, 20, cx - w / 2, cy + PT(c, 172), ZUI_TEXT, lab);
}

/* ---- App launch: the boot screen's loading dynamic, reused for every app -- */
static ctx_t *LC; /* the context part painters draw through */
typedef struct {
    pict_t k;
    const char *label;
    int chg;
} launch_part_t;
static uint32_t lit_col(uint32_t lit)
{
    return zui_mix(zui_mix(ZUI_VOID, ZUI_GOLD, 46u), ZUI_GOLD, lit > 255u ? 255u : lit);
}

static void paint_part(zui_surface_t *s, zui_rect_t b, uint32_t lit, void *v)
{
    (void) s;
    const launch_part_t *p = v;
    uint32_t col = lit_col(lit);
    if (p->chg) { /* the user's Chiglet joins the launch */
        zui_chiglet_t k = MY_CHG;
        k.mood = ZUI_CHG_DECIDED;
        zui_chiglet_draw(&LC->s, &k, b.x + b.w / 2, b.y + b.h * 40 / 100, b.w / 5, 0);
        if (lit < 200) zui_fill_a(&LC->s, b, ZUI_PANEL, (200u - lit) * 255u / 256u);
    } else {
        pict(LC, p->k, b.x + b.w / 2, b.y + b.h * 40 / 100, 64, col);
    }
    int32_t w = text_w(LC, &F_MED, 12, p->label);
    text(LC, &F_MED, 12, b.x + (b.w - w) / 2, b.y + b.h - PT(LC, 8), col, p->label);
}

static void paint_mark(zui_surface_t *s, zui_rect_t b, uint32_t lit, void *v)
{
    (void) s;
    (void) v;
    uint32_t col = lit_col(lit);
    LC->bg = ZUI_VOID;
    pict(LC, PG_FILE, b.x + b.w / 2, b.y + b.h * 40 / 100, 200, col);
    LC->bg = ZUI_PANEL;
    const char *nm = "Quillon";
    int32_t w = text_w(LC, &F_SEMI, 34, nm);
    text(LC, &F_SEMI, 34, b.x + (b.w - w) / 2, b.y + b.h - PT(LC, 22), col, nm);
}

static uint32_t LAUNCH_G = 256, LAUNCH_L = 256, LAUNCH_R = 150, LAUNCH_O = 0;
static void launch_screen(ctx_t *c)
{
    static const launch_part_t P[5] = {{PG_FILE, "budget-v3.pdf", 0},
                                       {PG_FOLDER, "Grants", 0},
                                       {PG_FILE, "Tala", 1},
                                       {PG_TEAM, "writer + checker", 0},
                                       {PG_LOCK, "qwen2.5 1.5b", 0}};
    LC = c;
    layer(c, 0);
    c->bg = ZUI_PANEL;
    zui_launch_t l = {
        paint_mark,
        {paint_part, paint_part, paint_part, paint_part, paint_part},
        {0, (void *) &P[0], (void *) &P[1], (void *) &P[2], (void *) &P[3], (void *) &P[4]},
        5,
        &c->d};
    zui_rect_t a = c->p.main;
    if (c->p.list.w) {
        a.x = c->p.list.x;
        a.w += c->p.list.w;
    }
    zui_launch_draw(&c->s, a, (int32_t) c->scr.scale_q8, &l,
                    (zui_launch_state_t){LAUNCH_G, LAUNCH_L, LAUNCH_R, LAUNCH_O}, TICK);
    rail(c, 4);
    bottom_bar(c, 3);
    top_bar(c, "Quillon");
}

/* the launch opening like a vault onto the app it loaded (Quillon) */
static zui_rect_t launch_area(ctx_t *c)
{
    zui_rect_t a = c->p.main;
    if (c->p.list.w) {
        a.x = c->p.list.x;
        a.w += c->p.list.w;
    }
    return a;
}
static void launch_vault_screen(ctx_t *c)
{
    size_t n = (size_t) c->s.w * (size_t) c->s.h;
    uint32_t *behind = malloc(n * 4), *face = malloc(n * 4);
    if (!behind || !face) exit(2);
    writer_screen(c);
    memcpy(behind, c->s.px, n * 4);
    zui_fill(&c->s, (zui_rect_t){0, 0, c->s.w, c->s.h}, ZUI_VOID);
    launch_screen(c);
    memcpy(face, c->s.px, n * 4);
    zui_rect_t a = launch_area(c);
    zui_surface_t F, B;
    zui_surface_init(&F, face + a.y * c->s.stride + a.x, a.w, a.h, c->s.stride);
    zui_surface_init(&B, behind + a.y * c->s.stride + a.x, a.w, a.h, c->s.stride);
    zui_vault_draw(&c->s, a, (int32_t) c->scr.scale_q8, &F, &B, LAUNCH_O);
    free(behind);
    free(face);
}

/* ---- login after the boot vault ------------------------------------------ */
static uint8_t *FOX;
static int32_t FOX_W, FOX_H; /* the gold fox, RGBA, optional */
static void load_fox(const char *dir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/fox.rgba", dir);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    int32_t wh[2];
    if (fread(wh, 4, 2, f) != 2) {
        fclose(f);
        return;
    }
    FOX = malloc((size_t) wh[0] * (size_t) wh[1] * 4);
    if (FOX &&
        fread(FOX, 4, (size_t) wh[0] * (size_t) wh[1], f) == (size_t) wh[0] * (size_t) wh[1]) {
        FOX_W = wh[0];
        FOX_H = wh[1];
    }
    fclose(f);
}
static void fox_mark(zui_surface_t *s, zui_rect_t b, void *v)
{
    (void) v;
    if (!FOX_W) {
        zui_stroke_rrect(s, b, b.w / 2, 2, ZUI_GOLD, 255u);
        return;
    }
    int32_t side = b.w < b.h ? b.w : b.h;
    int32_t w = FOX_W >= FOX_H ? side : side * FOX_W / FOX_H,
            h = FOX_W >= FOX_H ? side * FOX_H / FOX_W : side;
    int32_t x0 = b.x + (b.w - w) / 2, y0 = b.y + (b.h - h) / 2;
    for (int32_t y = 0; y < h; y++)
        for (int32_t x = 0; x < w; x++) { /* box-filtered downscale */
            int32_t sx0 = x * FOX_W / w, sx1 = (x + 1) * FOX_W / w, sy0 = y * FOX_H / h,
                    sy1 = (y + 1) * FOX_H / h;
            uint32_t r = 0, g = 0, bl = 0, a = 0, k = 0;
            for (int32_t yy = sy0; yy < (sy1 > sy0 ? sy1 : sy0 + 1); yy++)
                for (int32_t xx = sx0; xx < (sx1 > sx0 ? sx1 : sx0 + 1); xx++) {
                    const uint8_t *q = FOX + ((size_t) yy * (size_t) FOX_W + (size_t) xx) * 4;
                    r += q[0] * q[3];
                    g += q[1] * q[3];
                    bl += q[2] * q[3];
                    a += q[3];
                    k++;
                }
            if (!a) continue;
            zui_blend_px(s, x0 + x, y0 + y, ((r / a) << 16) | ((g / a) << 8) | (bl / a), a / k);
        }
}

static const zui_login_person_t LG_PEOPLE[] = {
    {"Michael", 0, ZUI_LOCK_PASS, false},      {"Ana Maria", 0x8FD0FFu, ZUI_LOCK_PASS, false},
    {"Kai", 0x7BE8A6u, ZUI_LOCK_NONE, false},  {"Noor", 0xFF9DB0u, ZUI_LOCK_PASSKEY, false},
    {"Tala", 0xE8D27Au, ZUI_LOCK_NONE, false}, {"Guest", 0, ZUI_LOCK_NONE, true}};
static uint32_t LG_CHOSEN = 0, LG_TYPED = 6, LG_WAIT = 0, LG_SHAKE = 0;
static bool LG_WRONG = false;
static void login_screen(ctx_t *c)
{
    layer(c, 0);
    c->bg = ZUI_VOID;
    zui_login_t l = {LG_PEOPLE, 6,       LG_CHOSEN, LG_TYPED, LG_WAIT,  LG_WRONG, true,
                     LG_SHAKE,  "06:12", 0,         0,        fox_mark, 0,        &c->d};
    zui_text_t big = font(c, &F_SEMI, 30), small = font(c, &F_REG, 14);
    zui_text_t ini = font(c, &F_SEMI, 64), rini = font(c, &F_SEMI, 22);
    zui_login_fonts_t f = {&big, &small, &ini, &rini};
    zui_login_draw(&c->s, (zui_rect_t){0, 0, c->s.w, c->s.h}, (int32_t) c->scr.scale_q8, &l, &f, 4);
}

/* ---- output -------------------------------------------------------------- */
static uint32_t *alloc_px(int32_t w, int32_t h)
{
    uint32_t *p = calloc((size_t) w * (size_t) h, 4);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    return p;
}

static void write_ppm(const char *dir, const char *name, const zui_surface_t *s)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(2);
    }
    fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
    for (int32_t y = 0; y < s->h; y++)
        for (int32_t x = 0; x < s->w; x++) {
            uint32_t v = s->px[(int64_t) y * s->stride + x];
            fputc((v >> 16) & 0xFF, f);
            fputc((v >> 8) & 0xFF, f);
            fputc(v & 0xFF, f);
        }
    fclose(f);
    printf("wrote %s (%dx%d)\n", path, s->w, s->h);
}

typedef void (*screen_fn)(ctx_t *);

/* The depth filter in the present step: 0 off, else strength 1..256. */
static int32_t PXL = 0, PXL_LX = -181, PXL_LY = -181;
static uint32_t PXL_TICK = 0;

static void render(const char *dir, const char *name, screen_fn fn, uint32_t w, uint32_t h,
                   uint32_t scale_q8, zui_input_t in, zui_depth_mode_t mode, int32_t head_x,
                   uint32_t px_per_cm)
{
    ctx_t c;
    memset(&c, 0, sizeof c);
    c.scr.px_w = w;
    c.scr.px_h = h;
    c.scr.scale_q8 = scale_q8;
    c.scr.input = in;
    zui_plan(&c.scr, &c.p);
    zui_depth_init(&c.d, mode, px_per_cm);
    c.d.head_x = head_x;
    c.strength = mode == ZUI_DEPTH_FLAT ? 1 : 4; /* the user's depth slider */
    uint32_t *px = alloc_px((int32_t) w, (int32_t) h);
    zui_surface_init(&c.s, px, (int32_t) w, (int32_t) h, (int32_t) w);
    if (mode == ZUI_DEPTH_ANAGLYPH_AMBER_BLUE || mode == ZUI_DEPTH_ANAGLYPH_RED_CYAN) {
        uint32_t *lp = alloc_px((int32_t) w, (int32_t) h), *rp = alloc_px((int32_t) w, (int32_t) h);
        zui_surface_t L, Rs;
        zui_surface_init(&L, lp, (int32_t) w, (int32_t) h, (int32_t) w);
        zui_surface_init(&Rs, rp, (int32_t) w, (int32_t) h, (int32_t) w);
        c.s = L;
        c.eye = -1;
        fn(&c);
        c.s = Rs;
        c.eye = +1;
        fn(&c);
        zui_surface_t O;
        zui_surface_init(&O, px, (int32_t) w, (int32_t) h, (int32_t) w);
        zui_anaglyph(&c.d, &O, &L, &Rs);
        c.s = O;
        free(lp);
        free(rp);
    } else if (PXL) {
        zui_plane_t pl;
        uint8_t *tg = malloc((size_t) w * h);
        int8_t *zz = malloc((size_t) w * h);
        static zui_pxl_lut_t lut;
        if (!tg || !zz) exit(1);
        zui_plane_init(&pl, tg, zz, (int32_t) w, (int32_t) h, (int32_t) w);
        zui_plane_clear(&pl);
        c.pl = &pl;
        fn(&c);
        zui_pxl_build(&lut, PXL);
        zui_pxl_apply(&c.s, &pl, &lut, PXL_LX, PXL_LY, PXL_TICK);
        free(tg);
        free(zz);
    } else {
        fn(&c);
    }
    write_ppm(dir, name, &c.s);
    free(px);
}

static void tokens_json(void)
{
    printf("{\n  \"color\": {\n");
    printf("    \"void\": \"#%06X\", \"panel\": \"#%06X\", \"raised\": \"#%06X\",\n", ZUI_VOID,
           ZUI_PANEL, ZUI_RAISED);
    printf("    \"hairline\": \"#%06X\", \"edge\": \"#%06X\",\n", ZUI_HAIRLINE, ZUI_EDGE);
    printf("    \"text\": \"#%06X\", \"text2\": \"#%06X\", \"muted\": \"#%06X\", \"faint\": "
           "\"#%06X\",\n",
           ZUI_TEXT, ZUI_TEXT_2, ZUI_MUTED, ZUI_FAINT);
    printf("    \"gold\": \"#%06X\", \"goldHi\": \"#%06X\", \"goldGlow\": \"#%06X\",\n", ZUI_GOLD,
           ZUI_GOLD_HI, ZUI_GOLD_GLOW);
    printf("    \"ok\": \"#%06X\", \"info\": \"#%06X\", \"alert\": \"#%06X\"\n  },\n", ZUI_OK,
           ZUI_INFO, ZUI_ALERT);
    printf("  \"space\": [%d, %d, %d, %d, %d, %d, %d],\n", ZUI_S1, ZUI_S2, ZUI_S3, ZUI_S4, ZUI_S5,
           ZUI_S6, ZUI_S7);
    printf("  \"radius\": { \"control\": %d, \"panel\": %d },\n", ZUI_R_CONTROL, ZUI_R_PANEL);
    printf("  \"type\": { \"caption\": %d, \"label\": %d, \"body\": %d, \"title\": %d, "
           "\"headline\": %d, \"display\": %d,\n",
           ZUI_T_CAPTION, ZUI_T_LABEL, ZUI_T_BODY, ZUI_T_TITLE, ZUI_T_HEADLINE, ZUI_T_DISPLAY);
    printf("            \"sans\": \"IBM Plex Sans\", \"mono\": \"IBM Plex Mono\" },\n");
    printf("  \"target\": { \"touch\": %d, \"pointer\": %d },\n", ZUI_TOUCH_MIN, ZUI_POINTER_MIN);
    printf("  \"motionMs\": %d,\n", ZUI_MOTION_MS);
    printf("  \"sizeClasses\": { \"compact\": 0, \"medium\": 600, \"expanded\": 1024, \"large\": "
           "1600 }\n}\n");
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--tokens") == 0) {
        tokens_json();
        return 0;
    }
    if (argc != 3) {
        fprintf(stderr, "usage: zui_mockup <font-dir> <out-dir> | --tokens\n");
        return 2;
    }
    const char *fd = argv[1], *out = argv[2];
    load_font(&F_REG, fd, "IBMPlexSans-Regular.ttf");
    load_font(&F_MED, fd, "IBMPlexSans-Medium.ttf");
    load_font(&F_SEMI, fd, "IBMPlexSans-SemiBold.ttf");
    load_font(&F_MONO, fd, "IBMPlexMono-Regular.ttf");
    load_font(&F_MONO_MED, fd, "IBMPlexMono-Medium.ttf");
    zui_glyph_cache_init(&CACHE);
    load_fox(fd);

    /* sizes: phone 390x844 pt @2x, laptop 1440x900 pt @1x, large 2560x1440 @1x */
    render(out, "studio-chiglet-laptop", studio_with_chiglet, 1440, 900, 256, ZUI_INPUT_POINTER,
           ZUI_DEPTH_FLAT, 0, 48);
    render(out, "studio-chiglet-phone", studio_with_chiglet, 780, 1688, 512, ZUI_INPUT_TOUCH,
           ZUI_DEPTH_FLAT, 0, 60);
    render(out, "web4-laptop", web4_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0,
           48);
    render(out, "web4-large", web4_screen, 2560, 1440, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0,
           36);
    render(out, "studio-phone", studio, 780, 1688, 512, ZUI_INPUT_TOUCH, ZUI_DEPTH_FLAT, 0, 60);
    render(out, "studio-laptop", studio, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0, 48);
    render(out, "studio-large", studio, 2560, 1440, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0, 36);
    render(out, "writer-laptop", writer_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT,
           0, 48);
    render(out, "writer-doc-laptop", writer_doc, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT,
           0, 48);
    render(out, "writer-phone", writer_screen, 780, 1688, 512, ZUI_INPUT_TOUCH, ZUI_DEPTH_FLAT, 0,
           60);
    SPACE_SEL = 0;
    render(out, "home-laptop", home_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0,
           48);
    render(out, "spaces-laptop", spaces_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT,
           0, 48);
    render(out, "spaces-large", spaces_screen, 2560, 1440, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT,
           0, 36);
    render(out, "spaces-phone", spaces_screen, 780, 1688, 512, ZUI_INPUT_TOUCH, ZUI_DEPTH_FLAT, 0,
           60);
    render(out, "spaces-amber-blue", spaces_screen, 1440, 900, 256, ZUI_INPUT_POINTER,
           ZUI_DEPTH_ANAGLYPH_AMBER_BLUE, 0, 48);
    for (int i = 0; i < 16; i++) {
        /* travelling across the plan: the viewpoint sweeps and layers part by depth */
        char n[64];
        snprintf(n, sizeof n, "spaces-travel-%02d", i);
        render(out, n, spaces_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_PARALLAX,
               (i - 8) * 110, 48);
    }
    SPACE_SEL = -1;
    render(out, "depth-amber-blue", studio, 1440, 900, 256, ZUI_INPUT_POINTER,
           ZUI_DEPTH_ANAGLYPH_AMBER_BLUE, 0, 48);
    render(out, "depth-red-cyan", studio, 1440, 900, 256, ZUI_INPUT_POINTER,
           ZUI_DEPTH_ANAGLYPH_RED_CYAN, 0, 48);
    render(out, "depth-chroma", studio, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_CHROMA, 0, 48);
    for (int i = 0; i < 12; i++) {
        /* head sweeps +-15 cm and back, as a camera tracker would report */
        static const int sweep[12] = {0, 4, 7, 9, 7, 4, 0, -4, -7, -9, -7, -4};
        char n[64];
        snprintf(n, sizeof n, "depth-parallax-%02d", i);
        render(out, n, studio, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_PARALLAX, sweep[i] * 80,
               48);
    }
    /* the swarm's live faces */
    render(out, "swarm-laptop", swarm_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0,
           48);
    render(out, "swarm-phone", swarm_screen, 780, 1688, 512, ZUI_INPUT_TOUCH, ZUI_DEPTH_FLAT, 0,
           60);
    for (int i = 0; i < 16; i++) { /* checker turns from curious to worried, live */
        char n[64];
        snprintf(n, sizeof n, "swarm-live-%02d", i);
        MORPH_T = i < 3 ? 0 : i < 11 ? (uint32_t) (i - 3) * 32u + 32u : 256u;
        TICK = (uint32_t) i;
        render(out, n, swarm_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0, 48);
    }
    MORPH_T = 256;
    TICK = 0;
    { /* one face through all eight feelings at full intensity */
        static const swarm_emotion_t order[9] = {
            SWARM_EMO_NEUTRAL,     SWARM_EMO_CALM,    SWARM_EMO_JOY,
            SWARM_EMO_CURIOUS,     SWARM_EMO_LOVE,    SWARM_EMO_WORRY,
            SWARM_EMO_FRUSTRATION, SWARM_EMO_SADNESS, SWARM_EMO_NEUTRAL};
        int fr = 0;
        for (int e = 0; e < 8; e++) {
            CYC_A = (swarm_feeling_t){order[e], 5};
            CYC_B = (swarm_feeling_t){order[e + 1], 5};
            for (int k = 1; k <= 12; k++) {
                char n[64];
                snprintf(n, sizeof n, "emote-morph-%03d", fr++);
                MORPH_T = k <= 8 ? (uint32_t) k * 32u : 256u;
                render(out, n, face_cycle, 560, 560, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0, 48);
            }
        }
        MORPH_T = 256;
    }
    /* an app launching with the same loading dynamic as the boot */
    LAUNCH_G = 160;
    LAUNCH_L = 0;
    LAUNCH_R = 0;
    render(out, "launch-gathering-laptop", launch_screen, 1440, 900, 256, ZUI_INPUT_POINTER,
           ZUI_DEPTH_FLAT, 0, 48);
    LAUNCH_G = 256;
    LAUNCH_L = 256;
    LAUNCH_R = 150;
    render(out, "launch-laptop", launch_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT,
           0, 48);
    render(out, "launch-phone", launch_screen, 780, 1688, 512, ZUI_INPUT_TOUCH, ZUI_DEPTH_FLAT, 0,
           60);
    {
        int fr = 0;
        for (int k = 0; k <= 16; k++) {
            char n[64];
            snprintf(n, sizeof n, "launch-anim-%03d", fr++);
            LAUNCH_G = (uint32_t) k * 16u;
            LAUNCH_L = 0;
            LAUNCH_R = 0;
            render(out, n, launch_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0, 48);
        }
        for (int k = 1; k <= 6; k++) {
            char n[64];
            snprintf(n, sizeof n, "launch-anim-%03d", fr++);
            LAUNCH_G = 256;
            LAUNCH_L = (uint32_t) k * 256u / 6u;
            LAUNCH_R = 0;
            render(out, n, launch_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0, 48);
        }
        for (int k = 1; k <= 8; k++) {
            char n[64];
            snprintf(n, sizeof n, "launch-anim-%03d", fr++);
            LAUNCH_G = 256;
            LAUNCH_L = 256;
            LAUNCH_R = (uint32_t) k * 32u;
            render(out, n, launch_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0, 48);
        }
    }
    LAUNCH_O = 0;
    SPACE_SEL = 0;
    render(out, "foil-home-laptop", home_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FOIL,
           0, 48);
    render(out, "foil-login-laptop", login_screen, 1440, 900, 256, ZUI_INPUT_POINTER,
           ZUI_DEPTH_FOIL, 0, 48);
    render(out, "foil-launch-laptop", launch_screen, 1440, 900, 256, ZUI_INPUT_POINTER,
           ZUI_DEPTH_FOIL, 0, 48);
    for (int k = 0; k < 16; k++) {
        char n[64];
        int32_t hx = (int32_t) (k < 8 ? k : 16 - k) * 40 - 160;
        snprintf(n, sizeof n, "foil-tilt-%03d", k);
        render(out, n, home_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FOIL, hx, 48);
        snprintf(n, sizeof n, "foil-login-tilt-%03d", k);
        render(out, n, login_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FOIL, hx, 48);
    }
    /* the paraconsistent pixel filter: before / after, and the light moving */
    render(out, "pxl-home-before", home_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT,
           0, 48);
    PXL = 256;
    render(out, "pxl-home-after", home_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0,
           48);
    for (int k = 0; k < 16; k++) {
        char n[64];
        PXL_LX = (int32_t) (k < 8 ? k : 16 - k) * 64 - 256;
        PXL_TICK = (uint32_t) k * 8u;
        snprintf(n, sizeof n, "pxl-light-%03d", k);
        render(out, n, home_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0, 48);
    }
    PXL = 0;
    PXL_LX = -181;
    PXL_TICK = 0;
    render(out, "login-laptop", login_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0,
           48);
    render(out, "login-phone", login_screen, 780, 1688, 512, ZUI_INPUT_TOUCH, ZUI_DEPTH_FLAT, 0,
           60);
    LG_CHOSEN = 2;
    LG_TYPED = 0;
    render(out, "login-open-laptop", login_screen, 1440, 900, 256, ZUI_INPUT_POINTER,
           ZUI_DEPTH_FLAT, 0, 48);
    LG_CHOSEN = 3;
    render(out, "login-passkey-laptop", login_screen, 1440, 900, 256, ZUI_INPUT_POINTER,
           ZUI_DEPTH_FLAT, 0, 48);
    LG_CHOSEN = 1;
    LG_TYPED = 3;
    LG_WRONG = true;
    LG_WAIT = 30;
    render(out, "login-wait-laptop", login_screen, 1440, 900, 256, ZUI_INPUT_POINTER,
           ZUI_DEPTH_FLAT, 0, 48);
    LG_CHOSEN = 0;
    LG_TYPED = 6;
    LG_WRONG = false;
    LG_WAIT = 0;
    {
        int fr = 0;
        LAUNCH_G = 256;
        LAUNCH_L = 256;
        for (int k = 0; k <= 8; k++) {
            char n[64];
            snprintf(n, sizeof n, "launch-vault-%03d", fr++);
            LAUNCH_R = 64u + (uint32_t) k * 24u;
            LAUNCH_O = 0;
            render(out, n, launch_vault_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT,
                   0, 48);
        }
        for (int k = 1; k <= 16; k++) {
            char n[64];
            snprintf(n, sizeof n, "launch-vault-%03d", fr++);
            LAUNCH_R = 256;
            LAUNCH_O = (uint32_t) k * 16u;
            render(out, n, launch_vault_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT,
                   0, 48);
        }
        LAUNCH_O = 0;
    }
    printf("glyph cache: %u hits, %u misses\n", CACHE.hits, CACHE.misses);
    return 0;
}
