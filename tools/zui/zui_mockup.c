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

/* ---- fonts --------------------------------------------------------------- */
static ttf_font_t F_REG, F_MED, F_SEMI, F_MONO, F_MONO_MED;
static zui_glyph_cache_t CACHE;

static void load_font(ttf_font_t *f, const char *dir, const char *name) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n);
    if (!b || fread(b, 1, (size_t)n, fp) != (size_t)n) { fprintf(stderr, "read %s\n", path); exit(2); }
    fclose(fp);
    if (!ttf_parse(f, b, (uint32_t)n)) { fprintf(stderr, "bad font %s\n", path); exit(2); }
}

/* ---- render context: screen, depth, current layer ------------------------ */
typedef struct {
    zui_surface_t s;
    zui_screen_t  scr;
    zui_plan_t    p;
    zui_depth_t   d;
    int           eye;
    int32_t       dx, dy, z;
    int32_t       strength;     /* depth slider: multiplies every layer's z */
} ctx_t;

static int32_t PT(ctx_t *c, int32_t pt) { return zui_px(&c->scr, pt); }

static void layer(ctx_t *c, int32_t z_pt) {
    c->z = PT(c, z_pt) * (c->strength ? c->strength : 1);
    zui_layer_offset(&c->d, c->z, c->eye, &c->dx, &c->dy);
}

static uint32_t C(ctx_t *c, uint32_t rgb) { return zui_chroma_px(&c->d, rgb, c->z); }

static zui_rect_t R(ctx_t *c, int32_t x, int32_t y, int32_t w, int32_t h) {
    zui_rect_t r = { x + c->dx, y + c->dy, w, h };
    return r;
}

static zui_text_t font(ctx_t *c, const ttf_font_t *f, int32_t pt) {
    zui_text_t t = { f, &CACHE, (uint32_t)PT(c, pt), f == &F_MONO || f == &F_MONO_MED };
    return t;
}

static int32_t text(ctx_t *c, const ttf_font_t *f, int32_t pt, int32_t x,
                    int32_t base, uint32_t rgb, const char *s) {
    zui_text_t t = font(c, f, pt);
    return zui_text(&c->s, &t, x + c->dx, base + c->dy, C(c, rgb), s) - c->dx;
}

static int32_t text_w(ctx_t *c, const ttf_font_t *f, int32_t pt, const char *s) {
    zui_text_t t = font(c, f, pt);
    return zui_text_width(&t, s);
}

static void text_r(ctx_t *c, const ttf_font_t *f, int32_t pt, int32_t right,
                   int32_t base, uint32_t rgb, const char *s) {
    text(c, f, pt, right - text_w(c, f, pt, s), base, rgb, s);
}

static void text_fit(ctx_t *c, const ttf_font_t *f, int32_t pt, int32_t x,
                     int32_t base, int32_t w, uint32_t rgb, const char *s) {
    zui_text_t t = font(c, f, pt);
    zui_text_fit(&c->s, &t, x + c->dx, base + c->dy, w, C(c, rgb), s);
}

/* word-wrap a paragraph; returns the baseline after the last line */
static int32_t para(ctx_t *c, const ttf_font_t *f, int32_t pt, int32_t x,
                    int32_t base, int32_t w, int32_t lead_pt, uint32_t rgb,
                    const char *s) {
    char line[512] = "", trial[512];
    const char *p = s;
    while (*p) {
        const char *e = p;
        while (*e && *e != ' ') e++;
        char word[256];
        size_t n = (size_t)(e - p) < sizeof word - 1 ? (size_t)(e - p) : sizeof word - 1;
        memcpy(word, p, n); word[n] = 0;
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
    if (line[0]) { text(c, f, pt, x, base, rgb, line); base += PT(c, lead_pt); }
    return base;
}

static void fill(ctx_t *c, zui_rect_t r, uint32_t rgb) { zui_fill(&c->s, r, C(c, rgb)); }
static void hair_h(ctx_t *c, int32_t x, int32_t y, int32_t w) {
    zui_hline(&c->s, x + c->dx, y + c->dy, w, 1, C(c, ZUI_HAIRLINE));
}
static void hair_v(ctx_t *c, int32_t x, int32_t y, int32_t h) {
    zui_vline(&c->s, x + c->dx, y + c->dy, h, 1, C(c, ZUI_HAIRLINE));
}

/* ---- icons: drawn from primitives, one stroke weight, no emoji ---------- */
typedef enum { IC_STUDIO, IC_LEDGER, IC_MESH, IC_STORE, IC_APPS, IC_SETTINGS } icon_t;

static void dot(ctx_t *c, int32_t cx, int32_t cy, int32_t r, uint32_t rgb) {
    zui_fill_rrect(&c->s, R(c, cx - r, cy - r, 2 * r, 2 * r), r, C(c, rgb), 255);
}
static void ring(ctx_t *c, int32_t cx, int32_t cy, int32_t r, int32_t w, uint32_t rgb) {
    zui_stroke_rrect(&c->s, R(c, cx - r, cy - r, 2 * r, 2 * r), r, w, C(c, rgb), 255);
}

static void icon(ctx_t *c, icon_t k, int32_t cx, int32_t cy, uint32_t rgb) {
    int32_t u = PT(c, 1), sw = u < 1 ? 1 : (u * 3 + 1) / 2;
    switch (k) {
    case IC_STUDIO:
        ring(c, cx, cy, PT(c, 8), sw, rgb); dot(c, cx, cy, PT(c, 3), rgb); break;
    case IC_LEDGER:
        for (int i = -1; i <= 1; i++)
            zui_hline(&c->s, cx - PT(c, 8) + c->dx, cy + i * PT(c, 5) - sw / 2 + c->dy,
                      i == 1 ? PT(c, 10) : PT(c, 16), sw, C(c, rgb));
        break;
    case IC_MESH:
        zui_hline(&c->s, cx - PT(c, 7) + c->dx, cy - sw / 2 + c->dy, PT(c, 14), sw, C(c, rgb));
        dot(c, cx - PT(c, 7), cy, PT(c, 3), rgb); dot(c, cx + PT(c, 7), cy, PT(c, 3), rgb);
        ring(c, cx, cy, PT(c, 3), sw, rgb);
        break;
    case IC_STORE:
        zui_stroke_rrect(&c->s, R(c, cx - PT(c, 8), cy - PT(c, 5), PT(c, 16), PT(c, 13)),
                         PT(c, 2), sw, C(c, rgb), 255);
        zui_stroke_rrect(&c->s, R(c, cx - PT(c, 4), cy - PT(c, 9), PT(c, 8), PT(c, 6)),
                         PT(c, 3), sw, C(c, rgb), 255);
        break;
    case IC_APPS:
        for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++)
            zui_stroke_rrect(&c->s, R(c, cx - PT(c, 8) + i * PT(c, 9), cy - PT(c, 8) + j * PT(c, 9),
                             PT(c, 7), PT(c, 7)), PT(c, 2), sw, C(c, rgb), 255);
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

static const char *NAV[6] = { "Studio", "Ledger", "Mesh", "Store", "Apps", "Settings" };

/* ---- shared pieces ------------------------------------------------------- */
static int32_t button(ctx_t *c, int32_t x, int32_t y, const char *s, int primary) {
    int32_t w = text_w(c, &F_MED, 13, s) + PT(c, 28), h = PT(c, 32);
    if (c->p.target_px > h) h = c->p.target_px;
    zui_rect_t r = R(c, x, y, w, h);
    if (primary) zui_fill_rrect(&c->s, r, PT(c, ZUI_R_CONTROL), C(c, ZUI_GOLD), 255);
    else zui_stroke_rrect(&c->s, r, PT(c, ZUI_R_CONTROL), 1, C(c, ZUI_EDGE), 255);
    text(c, &F_MED, 13, x + PT(c, 14), y + h / 2 + PT(c, 5), primary ? ZUI_VOID : ZUI_TEXT, s);
    return w;
}

static void budget_bar(ctx_t *c, int32_t x, int32_t y, int32_t w, int32_t used, int32_t total,
                       uint32_t rgb) {
    int32_t h = PT(c, 4);
    zui_fill_rrect(&c->s, R(c, x, y, w, h), h / 2, C(c, ZUI_HAIRLINE), 255);
    int32_t f = (int32_t)((int64_t)w * used / (total ? total : 1));
    if (f > 0) zui_fill_rrect(&c->s, R(c, x, y, f, h), h / 2, C(c, rgb), 255);
}

/* The top strip: wordmark, place, Ask field, swarm meter. */
static void top_bar(ctx_t *c, const char *place) {
    zui_rect_t t = c->p.top;
    layer(c, 0);
    fill(c, t, ZUI_VOID);
    hair_h(c, t.x, t.y + t.h - 1, t.w);
    int32_t g = c->p.gutter_px, mid = t.y + t.h / 2;
    int32_t x = text(c, &F_SEMI, 15, t.x + g, mid + PT(c, 5), ZUI_TEXT, "ZXV");
    dot(c, x + PT(c, 3), mid + PT(c, 3), PT(c, 2) > 1 ? PT(c, 2) : 1, ZUI_GOLD_GLOW);
    if (c->p.cls != ZUI_COMPACT)
        text(c, &F_REG, 13, x + PT(c, 14), mid + PT(c, 5), ZUI_MUTED, place);

    if (c->p.cls == ZUI_COMPACT) {
        const char *m = "1,840 / 4,181";
        int32_t w = text_w(c, &F_MONO, 11, m);
        text(c, &F_MONO, 11, t.x + t.w - g - w, mid + PT(c, 4), ZUI_TEXT_2, m);
        dot(c, t.x + t.w - g - w - PT(c, 10), mid, PT(c, 3), ZUI_OK);
        return;
    }
    int32_t aw = PT(c, c->p.cls == ZUI_LARGE ? 520 : 420);
    int32_t ax = t.x + (t.w - aw) / 2;
    if (c->p.cls == ZUI_MEDIUM) ax = t.x + PT(c, 150);
    zui_rect_t a = R(c, ax, mid - PT(c, 14), aw, PT(c, 28));
    zui_fill_rrect(&c->s, a, PT(c, ZUI_R_CONTROL), C(c, ZUI_PANEL), 255);
    zui_stroke_rrect(&c->s, a, PT(c, ZUI_R_CONTROL), 1, C(c, ZUI_EDGE), 255);
    text(c, &F_REG, 13, ax + PT(c, 12), mid + PT(c, 5), ZUI_FAINT, "Ask the swarm about anything on screen");
    const char *k = "Cmd K";
    text_r(c, &F_MONO, 11, ax + aw - PT(c, 10), mid + PT(c, 4), ZUI_MUTED, k);

    int32_t rx = t.x + t.w - g;
    const char *m = "1,840 / 4,181 tok";
    int32_t mw = text_w(c, &F_MONO, 11, m);
    text(c, &F_MONO, 11, rx - mw, mid - PT(c, 2), ZUI_TEXT_2, m);
    budget_bar(c, rx - mw, mid + PT(c, 5), mw, 1840, 4181, ZUI_TEXT_2);
    dot(c, rx - mw - PT(c, 14), mid, PT(c, 3), ZUI_OK);
    text_r(c, &F_REG, 12, rx - mw - PT(c, 24), mid + PT(c, 4), ZUI_MUTED, "5 agents");
}

static void rail(ctx_t *c, int sel) {
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
            zui_fill_rrect(&c->s, R(c, r.x + PT(c, 8), y, r.w - PT(c, 16), h),
                           PT(c, ZUI_R_CONTROL), C(c, ZUI_RAISED), 255);
            zui_fill(&c->s, R(c, r.x + PT(c, 8), y + PT(c, 8), PT(c, 2), h - PT(c, 16)), C(c, ZUI_TEXT));
        }
        uint32_t col = i == sel ? ZUI_TEXT : ZUI_MUTED;
        if (labels) {
            icon(c, (icon_t)i, r.x + PT(c, 32), y + h / 2, col);
            text(c, i == sel ? &F_MED : &F_REG, 14, r.x + PT(c, 54), y + h / 2 + PT(c, 5), col, NAV[i]);
        } else {
            icon(c, (icon_t)i, r.x + r.w / 2, y + PT(c, 18), col);
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
        text(c, &F_MONO, 11, r.x + PT(c, 20), by + PT(c, 42), ZUI_TEXT_2, "M3 Max \xC2\xB7 37 tok/s");
        text(c, &F_MONO, 11, r.x + PT(c, 20), by + PT(c, 60), ZUI_MUTED, "mesh: 2 peers");
    }
}

static void bottom_bar(ctx_t *c, int sel) {
    zui_rect_t b = c->p.bottom;
    if (b.w == 0) return;
    layer(c, 0);
    fill(c, b, ZUI_VOID);
    hair_h(c, b.x, b.y, b.w);
    static const int tabs[5] = { 0, 1, 2, 4, 5 };
    for (int i = 0; i < 5; i++) {
        int32_t cx = b.x + b.w * (2 * i + 1) / 10;
        uint32_t col = tabs[i] == sel ? ZUI_TEXT : ZUI_MUTED;
        icon(c, (icon_t)tabs[i], cx, b.y + PT(c, 22), col);
        int32_t w = text_w(c, &F_REG, 10, NAV[tabs[i]]);
        text(c, &F_REG, 10, cx - w / 2, b.y + PT(c, 46), col, NAV[tabs[i]]);
    }
}

/* ---- Studio: conversation list + conversation + swarm inspector ---------- */
typedef struct { const char *title, *preview, *when; int archived; } conv_t;
static const conv_t CONVS[] = {
    { "Grant application draft", "Swarm: tightened the budget section and...", "09:41", 0 },
    { "Tonopah array field notes", "You: compare yesterday's readings with...", "08:02", 0 },
    { "Mesh peer onboarding", "Swarm: both peers signed the agreement", "Sat", 0 },
    { "V-Bill epoch 14 summary", "Swarm: clearing held the reserve floor", "Fri", 0 },
    { "Phone pairing checklist", "Archived", "Oct 3", 1 },
    { "Hardware license questions", "Archived", "Oct 2", 1 },
};

static void conv_list(ctx_t *c) {
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
        layer(c, v->archived ? -40 : 0);        /* withdrawn: behind the glass */
        int32_t h = PT(c, 64);
        if (i == 0) {
            layer(c, 0);
            zui_fill_rrect(&c->s, R(c, l.x + PT(c, 8), y, l.w - PT(c, 16), h - PT(c, 4)),
                           PT(c, ZUI_R_CONTROL), C(c, ZUI_RAISED), 255);
        }
        uint32_t tc = v->archived ? ZUI_MUTED : ZUI_TEXT;
        text_fit(c, i == 0 ? &F_MED : &F_REG, 14, l.x + g, y + PT(c, 24),
                 l.w - 2 * g - PT(c, 44), tc, v->title);
        text_r(c, &F_MONO, 11, l.x + l.w - g, y + PT(c, 24), ZUI_FAINT, v->when);
        text_fit(c, &F_REG, 12, l.x + g, y + PT(c, 44), l.w - 2 * g, ZUI_MUTED, v->preview);
        y += h;
    }
    zui_unclip(&c->s);
}

static const char *Q1 = "Tighten the budget section of the grant draft and check the totals against the ledger.";
static const char *A1 = "Done. The budget section now leads with the three cost lines the reviewers ask about, "
                        "and every figure matches the Sicily ledger. One total was off: equipment was listed "
                        "as 12,400 but the ledger entries add to 12,040.";
static const char *S1 = "Change the equipment total from 12,400 to 12,040 in section 4.2.";

/* returns bottom y of the suggestion card */
static int32_t held_card(ctx_t *c, int32_t x, int32_t y, int32_t w) {
    layer(c, 0);                                   /* S0: held, at the glass */
    int32_t pad = PT(c, 16);
    int32_t inner = w - 2 * pad;
    /* measure: label + wrapped text + buttons */
    zui_rect_t probe_clip = c->s.clip;
    c->s.clip.w = 0;                               /* draw nothing: measure */
    int32_t end = para(c, &F_REG, 14, x + pad, y + PT(c, 50), inner, 22, ZUI_TEXT, S1);
    c->s.clip = probe_clip;
    int32_t h = end - y + PT(c, 8) + (c->p.target_px > PT(c, 32) ? c->p.target_px : PT(c, 32)) + pad;
    zui_rect_t r = R(c, x, y, w, h);
    zui_fill_rrect(&c->s, r, PT(c, ZUI_R_PANEL), C(c, ZUI_PANEL), 255);
    zui_dash_rrect(&c->s, r, PT(c, ZUI_R_PANEL), PT(c, 1) > 0 ? PT(c, 1) : 1, PT(c, 5), C(c, ZUI_GOLD));
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

static void conversation(ctx_t *c) {
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
    text(c, &F_REG, 12, x, y, ZUI_MUTED, "3 agents \xC2\xB7 local \xC2\xB7 nothing leaves this device");
    y += PT(c, compact ? 22 : 30);
    hair_h(c, x, y, w);
    y += PT(c, compact ? 28 : 36);

    text(c, &F_MED, 12, x, y, ZUI_MUTED, "You");
    y += PT(c, 24);
    y = para(c, &F_REG, 14, x, y, w, 22, ZUI_TEXT, Q1);
    y += PT(c, compact ? 14 : 18);

    /* the answer: S+ (active), drawn forward */
    layer(c, 24);
    text(c, &F_MED, 12, x + PT(c, 12), y - PT(c, 4), ZUI_TEXT_2, "Swarm");
    text(c, &F_REG, 12, x + PT(c, 12) + text_w(c, &F_MED, 12, "Swarm ") , y - PT(c, 4), ZUI_MUTED,
         compact ? "\xC2\xB7 writer + checker" : "\xC2\xB7 planner \xE2\x86\x92 writer \xE2\x86\x92 checker");
    y += PT(c, 20);
    int32_t top = y - PT(c, 34);
    y = para(c, &F_REG, 14, x + PT(c, 12), y, w - PT(c, 12), 22, ZUI_TEXT, A1);
    zui_fill(&c->s, R(c, x, top + PT(c, 2), PT(c, 2), y - top - PT(c, 6)), C(c, ZUI_EDGE));
    text(c, &F_MONO, 11, x + PT(c, 12), y + PT(c, 2), ZUI_FAINT,
         compact ? "qwen2.5-1.5b \xC2\xB7 412 tok \xC2\xB7 1.9 s" :
                   "qwen2.5-1.5b \xC2\xB7 writer 212 \xC2\xB7 checker 200 tok \xC2\xB7 1.9 s \xC2\xB7 ledger read 14 entries");
    y += PT(c, 26);

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
    zui_fill_rrect(&c->s, R(c, ax - PT(c, 14), ay - PT(c, 14), PT(c, 28), PT(c, 28)), PT(c, 6), C(c, ZUI_RAISED), 255);
    zui_hline(&c->s, ax - PT(c, 6) + c->dx, ay - 1 + c->dy, PT(c, 12), PT(c, 2), C(c, ZUI_TEXT_2));
    for (int i = 0; i < PT(c, 5); i++) {
        zui_fill(&c->s, R(c, ax + PT(c, 6) - i - 1, ay - 1 - i, PT(c, 2), PT(c, 2)), C(c, ZUI_TEXT_2));
        zui_fill(&c->s, R(c, ax + PT(c, 6) - i - 1, ay - 1 + i, PT(c, 2), PT(c, 2)), C(c, ZUI_TEXT_2));
    }
}

typedef struct { const char *name, *model; int32_t tok; int active; } agent_t;
static const agent_t AGENTS[] = {
    { "planner",   "qwen2.5-1.5b", 1597, 0 },
    { "writer",    "qwen2.5-1.5b + lora", 987, 1 },
    { "checker",   "phi-mini", 610, 1 },
    { "retriever", "embed-small", 377, 0 },
    { "critic",    "smol-360m", 233, 0 },
    { "reserve",   "unallocated", 377, 0 },
};

static void inspector(ctx_t *c) {
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
        layer(c, a->active ? 16 : 0);
        if (a->active) dot(c, x + PT(c, 3), y - PT(c, 4), PT(c, 3), ZUI_OK);
        text(c, &F_MED, 13, x + PT(c, 14), y, a->tok == 377 && i == 5 ? ZUI_MUTED : ZUI_TEXT, a->name);
        char v[32]; snprintf(v, sizeof v, "%d", a->tok);
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

static void studio(ctx_t *c) {
    fill(c, (zui_rect_t){ 0, 0, c->s.w, c->s.h }, ZUI_VOID);
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

static void writer(ctx_t *c, int palette) {
    fill(c, (zui_rect_t){ 0, 0, c->s.w, c->s.h }, ZUI_VOID);
    zui_rect_t l = c->p.list;
    if (l.w) {
        layer(c, 0);
        fill(c, l, ZUI_PANEL);
        hair_v(c, l.x + l.w - 1, l.y, l.h);
        text(c, &F_MED, 11, l.x + PT(c, 16), l.y + PT(c, 28), ZUI_MUTED, "DOCUMENTS");
        static const char *docs[] = { "Tonopah array field notes", "Grant application draft",
                                      "Peer agreement v2", "Reading list" };
        for (int i = 0; i < 4; i++) {
            int32_t y = l.y + PT(c, 44) + i * PT(c, 44);
            if (i == 0) zui_fill_rrect(&c->s, R(c, l.x + PT(c, 8), y, l.w - PT(c, 16), PT(c, 40)),
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
    layer(c, -24);
    int32_t y0 = y;
    y = para(c, &F_REG, 15, x, y, w, 26, ZUI_FAINT, DOC[1]);
    layer(c, 0);
    y += PT(c, 10);
    int32_t pad = PT(c, 14);
    zui_rect_t probe = c->s.clip; c->s.clip.w = 0;
    int32_t e2 = para(c, &F_REG, 15, x + pad, y + PT(c, 40), w - 2 * pad, 26, ZUI_TEXT, DOC_SUGGEST);
    c->s.clip = probe;
    int32_t h = e2 - y + PT(c, 12);
    zui_rect_t r = R(c, x - pad, y, w + 2 * pad, h);
    zui_fill_rrect(&c->s, r, PT(c, ZUI_R_PANEL), C(c, ZUI_PANEL), 255);
    zui_dash_rrect(&c->s, r, PT(c, ZUI_R_PANEL), 1, PT(c, 5), C(c, ZUI_GOLD));
    text(c, &F_MED, 11, x, y + PT(c, 22), ZUI_GOLD_GLOW, "REWRITE \xC2\xB7 HELD");
    text_r(c, &F_MONO, 11, x + w, y + PT(c, 22), ZUI_MUTED, "writer \xC2\xB7 164 tok \xC2\xB7 enter accept  esc reject");
    para(c, &F_REG, 15, x, y + PT(c, 48), w, 26, ZUI_TEXT, DOC_SUGGEST);
    (void)y0;
    y += h + PT(c, 22);
    y = para(c, &F_REG, 15, x, y, w, 26, ZUI_TEXT_2, DOC[2]);

    if (!palette) return;
    /* the Ask palette: app-declared AI actions over a dimmed screen (S+) */
    layer(c, 0);
    zui_fill_a(&c->s, (zui_rect_t){ 0, 0, c->s.w, c->s.h }, ZUI_VOID, 170);
    layer(c, 40);
    int32_t pw = PT(c, 600);
    if (pw > c->s.w - 2 * PT(c, 16)) pw = c->s.w - 2 * PT(c, 16);
    int32_t px = (c->s.w - pw) / 2, py = c->p.top.h + PT(c, compact ? 24 : 72);
    int32_t ph = PT(c, 372);
    zui_rect_t pr = R(c, px, py, pw, ph);
    zui_fill_rrect(&c->s, pr, PT(c, 12), C(c, ZUI_PANEL), 255);
    zui_stroke_rrect(&c->s, pr, PT(c, 12), 1, C(c, ZUI_EDGE), 255);
    int32_t ix = px + PT(c, 20);
    text(c, &F_REG, 16, ix, py + PT(c, 36), ZUI_TEXT, "make the second paragraph clearer");
    zui_fill(&c->s, R(c, ix + text_w(c, &F_REG, 16, "make the second paragraph clearer") + PT(c, 2),
                      py + PT(c, 20), PT(c, 2), PT(c, 22)), C(c, ZUI_GOLD_GLOW));
    hair_h(c, px, py + PT(c, 56), pw);
    text(c, &F_MED, 11, ix, py + PT(c, 82), ZUI_MUTED, "WRITER ACTIONS");
    static const char *acts[][3] = {
        { "Rewrite selection", "writer", "~170 tok" },
        { "Summarise document", "planner", "~320 tok" },
        { "Explain \xE2\x80\x9C" "residual carrier" "\xE2\x80\x9D", "retriever", "~90 tok" },
        { "Check figures against the ledger", "checker", "~240 tok" },
        { "Draft a reply email", "writer", "~400 tok" },
    };
    for (int i = 0; i < 5; i++) {
        int32_t ry = py + PT(c, 96) + i * PT(c, 44);
        if (i == 0) zui_fill_rrect(&c->s, R(c, px + PT(c, 8), ry, pw - PT(c, 16), PT(c, 40)),
                                   PT(c, ZUI_R_CONTROL), C(c, ZUI_RAISED), 255);
        text(c, &F_REG, 14, ix, ry + PT(c, 25), i == 0 ? ZUI_TEXT : ZUI_TEXT_2, acts[i][0]);
        if (!compact) text_r(c, &F_MONO, 11, px + pw - PT(c, 20), ry + PT(c, 25), ZUI_MUTED, acts[i][2]);
        if (!compact) text_r(c, &F_MONO, 11, px + pw - PT(c, 100), ry + PT(c, 25), ZUI_FAINT, acts[i][1]);
    }
    hair_h(c, px, py + ph - PT(c, 40), pw);
    text(c, &F_REG, 12, ix, py + ph - PT(c, 15), ZUI_MUTED, compact ? "Runs on this device" :
         "Runs on this Mac \xC2\xB7 nothing leaves the device");
    text_r(c, &F_MONO, 11, px + pw - PT(c, 20), py + ph - PT(c, 15), ZUI_FAINT, "enter run   esc close");
}

static void writer_doc(ctx_t *c) {
    writer(c, 0);
    rail(c, 4);
    bottom_bar(c, 4);
    top_bar(c, "Apps \xE2\x80\xBA Writer");
}

static void writer_screen(ctx_t *c) {
    writer(c, 1);
    rail(c, 4);
    bottom_bar(c, 4);
    top_bar(c, "Apps \xE2\x80\xBA Writer");
}

/* ---- output -------------------------------------------------------------- */
static uint32_t *alloc_px(int32_t w, int32_t h) {
    uint32_t *p = calloc((size_t)w * (size_t)h, 4);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(2); }
    return p;
}

static void write_ppm(const char *dir, const char *name, const zui_surface_t *s) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(2); }
    fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
    for (int32_t y = 0; y < s->h; y++)
        for (int32_t x = 0; x < s->w; x++) {
            uint32_t v = s->px[(int64_t)y * s->stride + x];
            fputc((v >> 16) & 0xFF, f); fputc((v >> 8) & 0xFF, f); fputc(v & 0xFF, f);
        }
    fclose(f);
    printf("wrote %s (%dx%d)\n", path, s->w, s->h);
}

typedef void (*screen_fn)(ctx_t *);

static void render(const char *dir, const char *name, screen_fn fn, uint32_t w, uint32_t h,
                   uint32_t scale_q8, zui_input_t in, zui_depth_mode_t mode, int32_t head_x,
                   uint32_t px_per_cm) {
    ctx_t c;
    memset(&c, 0, sizeof c);
    c.scr.px_w = w; c.scr.px_h = h; c.scr.scale_q8 = scale_q8; c.scr.input = in;
    zui_plan(&c.scr, &c.p);
    zui_depth_init(&c.d, mode, px_per_cm);
    c.d.head_x = head_x;
    c.strength = mode == ZUI_DEPTH_FLAT ? 1 : 4;   /* the user's depth slider */
    uint32_t *px = alloc_px((int32_t)w, (int32_t)h);
    zui_surface_init(&c.s, px, (int32_t)w, (int32_t)h, (int32_t)w);
    if (mode == ZUI_DEPTH_ANAGLYPH_AMBER_BLUE || mode == ZUI_DEPTH_ANAGLYPH_RED_CYAN) {
        uint32_t *lp = alloc_px((int32_t)w, (int32_t)h), *rp = alloc_px((int32_t)w, (int32_t)h);
        zui_surface_t L, Rs;
        zui_surface_init(&L, lp, (int32_t)w, (int32_t)h, (int32_t)w);
        zui_surface_init(&Rs, rp, (int32_t)w, (int32_t)h, (int32_t)w);
        c.s = L; c.eye = -1; fn(&c);
        c.s = Rs; c.eye = +1; fn(&c);
        zui_surface_t O;
        zui_surface_init(&O, px, (int32_t)w, (int32_t)h, (int32_t)w);
        zui_anaglyph(&c.d, &O, &L, &Rs);
        c.s = O;
        free(lp); free(rp);
    } else {
        fn(&c);
    }
    write_ppm(dir, name, &c.s);
    free(px);
}

static void tokens_json(void) {
    printf("{\n  \"color\": {\n");
    printf("    \"void\": \"#%06X\", \"panel\": \"#%06X\", \"raised\": \"#%06X\",\n", ZUI_VOID, ZUI_PANEL, ZUI_RAISED);
    printf("    \"hairline\": \"#%06X\", \"edge\": \"#%06X\",\n", ZUI_HAIRLINE, ZUI_EDGE);
    printf("    \"text\": \"#%06X\", \"text2\": \"#%06X\", \"muted\": \"#%06X\", \"faint\": \"#%06X\",\n",
           ZUI_TEXT, ZUI_TEXT_2, ZUI_MUTED, ZUI_FAINT);
    printf("    \"gold\": \"#%06X\", \"goldHi\": \"#%06X\", \"goldGlow\": \"#%06X\",\n", ZUI_GOLD, ZUI_GOLD_HI, ZUI_GOLD_GLOW);
    printf("    \"ok\": \"#%06X\", \"info\": \"#%06X\", \"alert\": \"#%06X\"\n  },\n", ZUI_OK, ZUI_INFO, ZUI_ALERT);
    printf("  \"space\": [%d, %d, %d, %d, %d, %d, %d],\n", ZUI_S1, ZUI_S2, ZUI_S3, ZUI_S4, ZUI_S5, ZUI_S6, ZUI_S7);
    printf("  \"radius\": { \"control\": %d, \"panel\": %d },\n", ZUI_R_CONTROL, ZUI_R_PANEL);
    printf("  \"type\": { \"caption\": %d, \"label\": %d, \"body\": %d, \"title\": %d, \"headline\": %d, \"display\": %d,\n",
           ZUI_T_CAPTION, ZUI_T_LABEL, ZUI_T_BODY, ZUI_T_TITLE, ZUI_T_HEADLINE, ZUI_T_DISPLAY);
    printf("            \"sans\": \"IBM Plex Sans\", \"mono\": \"IBM Plex Mono\" },\n");
    printf("  \"target\": { \"touch\": %d, \"pointer\": %d },\n", ZUI_TOUCH_MIN, ZUI_POINTER_MIN);
    printf("  \"motionMs\": %d,\n", ZUI_MOTION_MS);
    printf("  \"sizeClasses\": { \"compact\": 0, \"medium\": 600, \"expanded\": 1024, \"large\": 1600 }\n}\n");
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--tokens") == 0) { tokens_json(); return 0; }
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

    /* sizes: phone 390x844 pt @2x, laptop 1440x900 pt @1x, large 2560x1440 @1x */
    render(out, "studio-phone", studio, 780, 1688, 512, ZUI_INPUT_TOUCH, ZUI_DEPTH_FLAT, 0, 60);
    render(out, "studio-laptop", studio, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0, 48);
    render(out, "studio-large", studio, 2560, 1440, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0, 36);
    render(out, "writer-laptop", writer_screen, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0, 48);
    render(out, "writer-doc-laptop", writer_doc, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_FLAT, 0, 48);
    render(out, "writer-phone", writer_screen, 780, 1688, 512, ZUI_INPUT_TOUCH, ZUI_DEPTH_FLAT, 0, 60);
    render(out, "depth-amber-blue", studio, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_ANAGLYPH_AMBER_BLUE, 0, 48);
    render(out, "depth-red-cyan", studio, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_ANAGLYPH_RED_CYAN, 0, 48);
    render(out, "depth-chroma", studio, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_CHROMA, 0, 48);
    for (int i = 0; i < 12; i++) {
        /* head sweeps +-15 cm and back, as a camera tracker would report */
        static const int sweep[12] = { 0, 4, 7, 9, 7, 4, 0, -4, -7, -9, -7, -4 };
        char n[64];
        snprintf(n, sizeof n, "depth-parallax-%02d", i);
        render(out, n, studio, 1440, 900, 256, ZUI_INPUT_POINTER, ZUI_DEPTH_PARALLAX,
               sweep[i] * 80, 48);
    }
    printf("glyph cache: %u hits, %u misses\n", CACHE.hits, CACHE.misses);
    return 0;
}
