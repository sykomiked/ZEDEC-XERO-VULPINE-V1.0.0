/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_applib.c — the app library and the app card (zui_applib.h AL1..AL6). */
#include "zui_applib.h"
#include "zui_tokens.h"

#define SEGS 4u

static int32_t pt(uint32_t q8, int32_t v)
{
    return (int32_t) (((int64_t) v * (int64_t) q8 + 128) >> 8);
}
static int32_t P(const zui_applib_t *l, int32_t v)
{
    return pt(l->scale_q8, v);
}
static bool in(zui_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
static zui_rect_t meet(zui_rect_t a, zui_rect_t b)
{
    int32_t x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
    int32_t x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int32_t y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    return (zui_rect_t){x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0};
}
static void set_clip(zui_surface_t *s, zui_rect_t r)
{
    s->clip = meet(r, (zui_rect_t){0, 0, s->w, s->h});
}
static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}
static char *put(char *o, char *end, const char *s)
{
    while (*s && o < end - 1) *o++ = *s++;
    *o = 0;
    return o;
}
static char *put_u(char *o, char *end, uint32_t v)
{
    char t[12];
    uint32_t n = 0;
    do {
        t[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v && n < sizeof t);
    while (n && o < end - 1) *o++ = t[--n];
    *o = 0;
    return o;
}

/* ---- the filter --------------------------------------------------------- */

static void refilter(zui_applib_t *l)
{
    l->nshown = 0;
    zui_app_t app;
    for (uint32_t i = 0; zui_apps_get(l->apps, i, &app); i++) {
        if (l->layer && app.layer != l->layer) continue;
        if (!zui_apps_match(&app, l->query)) continue;
        l->shown[l->nshown++] = (uint16_t) i;
    }
    l->scroll = 0;
    l->hover = -1;
    l->press = -1;
    l->focus = l->nshown ? 0 : -1;
}

void zui_applib_cache_init(zui_applib_cache_t *c, uint32_t *px, uint32_t slots, uint32_t side)
{
    c->px = px;
    c->slots = slots > 64u ? 64u : slots;
    c->side = side;
    for (uint32_t i = 0; i < 64u; i++) {
        c->tag[i] = -1;
        c->tag_size[i] = 0;
    }
}

void zui_applib_init(zui_applib_t *l, const zui_apps_t *apps, zui_vec_work_t *work,
                     zui_applib_cache_t *cache, const zui_applib_faces_t *faces, uint32_t scale_q8)
{
    uint8_t *p = (uint8_t *) l;
    for (uint32_t i = 0; i < sizeof *l; i++) p[i] = 0;
    l->apps = apps;
    l->work = work;
    l->cache = cache;
    if (faces) l->f = *faces;
    l->scale_q8 = scale_q8 ? scale_q8 : 256u;
    l->press_seg = -1;
    l->act_app = -1;
    refilter(l);
}

void zui_applib_place(zui_applib_t *l, zui_rect_t box, uint32_t scale_q8)
{
    l->box = box;
    if (scale_q8) l->scale_q8 = scale_q8;
    int32_t m = zui_applib_max_scroll(l);
    if (l->scroll > m) l->scroll = m;
}

void zui_applib_set_layer(zui_applib_t *l, uint32_t layer)
{
    l->layer = layer > 3u ? 0u : layer;
    refilter(l);
}

void zui_applib_set_query(zui_applib_t *l, const char *q)
{
    uint32_t n = 0;
    for (; q && q[n] && n + 1 < ZUI_APPLIB_QUERY; n++) l->query[n] = q[n];
    l->query[n] = 0;
    l->qlen = n;
    refilter(l);
}

/* ---- geometry ------------------------------------------------------------ */

static int32_t pad(const zui_applib_t *l)
{
    return P(l, ZUI_S6);
}
static int32_t head_h(const zui_applib_t *l)
{
    return P(l, 40);
}
static int32_t ctl_h(const zui_applib_t *l)
{
    return P(l, 32);
}
static int32_t ctl_y(const zui_applib_t *l)
{
    return l->box.y + pad(l) + head_h(l) + P(l, ZUI_S2);
}

zui_rect_t zui_applib_field(const zui_applib_t *l)
{
    int32_t w = P(l, ZUI_APPLIB_FIELD_W);
    int32_t room = l->box.w - 2 * pad(l);
    if (w > room / 2) w = room / 2;
    return (zui_rect_t){l->box.x + l->box.w - pad(l) - w, ctl_y(l), w, ctl_h(l)};
}

zui_rect_t zui_applib_segment(const zui_applib_t *l, uint32_t layer)
{
    zui_rect_t f = zui_applib_field(l);
    int32_t x0 = l->box.x + pad(l);
    int32_t room = f.x - P(l, ZUI_S4) - x0;
    int32_t w = P(l, 104);
    if ((int32_t) SEGS * w > room) w = room / (int32_t) SEGS;
    if (w < 0) w = 0;
    return (zui_rect_t){x0 + (int32_t) (layer > 3u ? 3u : layer) * w, ctl_y(l), w, ctl_h(l)};
}

zui_rect_t zui_applib_clear_btn(const zui_applib_t *l)
{
    zui_rect_t f = zui_applib_field(l);
    int32_t d = f.h - P(l, ZUI_S2);
    return (zui_rect_t){f.x + f.w - d - P(l, ZUI_S1), f.y + (f.h - d) / 2, d, d};
}

zui_rect_t zui_applib_grid(const zui_applib_t *l)
{
    int32_t top = ctl_y(l) + ctl_h(l) + P(l, ZUI_S5);
    int32_t bottom = l->box.y + l->box.h - P(l, ZUI_S4);
    return (zui_rect_t){l->box.x + pad(l), top, l->box.w - 2 * pad(l),
                        bottom > top ? bottom - top : 0};
}

uint32_t zui_applib_columns(const zui_applib_t *l)
{
    zui_rect_t g = zui_applib_grid(l);
    int32_t step = P(l, ZUI_APPLIB_TILE_W + ZUI_APPLIB_GAP);
    int32_t c = step > 0 ? (g.w + P(l, ZUI_APPLIB_GAP)) / step : 0;
    return c > 0 ? (uint32_t) c : 1u;
}

static int32_t row_step(const zui_applib_t *l)
{
    return P(l, ZUI_APPLIB_TILE_H + ZUI_APPLIB_GAP);
}

zui_rect_t zui_applib_tile(const zui_applib_t *l, uint32_t pos)
{
    zui_rect_t g = zui_applib_grid(l);
    uint32_t c = zui_applib_columns(l);
    int32_t tw = P(l, ZUI_APPLIB_TILE_W), th = P(l, ZUI_APPLIB_TILE_H);
    int32_t used = (int32_t) c * tw + ((int32_t) c - 1) * P(l, ZUI_APPLIB_GAP);
    int32_t x0 = g.x + (g.w - used) / 2; /* the grid is centred */
    return (zui_rect_t){x0 + (int32_t) (pos % c) * P(l, ZUI_APPLIB_TILE_W + ZUI_APPLIB_GAP),
                        g.y - l->scroll + (int32_t) (pos / c) * row_step(l), tw, th};
}

int32_t zui_applib_max_scroll(const zui_applib_t *l)
{
    uint32_t c = zui_applib_columns(l);
    int32_t rows = (int32_t) ((l->nshown + c - 1) / c);
    int32_t content = rows ? rows * row_step(l) - P(l, ZUI_APPLIB_GAP) : 0;
    int32_t m = content - zui_applib_grid(l).h;
    return m > 0 ? m : 0;
}

static void clamp_scroll(zui_applib_t *l)
{
    int32_t m = zui_applib_max_scroll(l);
    if (l->scroll > m) l->scroll = m;
    if (l->scroll < 0) l->scroll = 0;
}

/* AL2: keep the focused tile in view */
static void reveal(zui_applib_t *l)
{
    if (l->focus < 0) return;
    zui_rect_t g = zui_applib_grid(l), t = zui_applib_tile(l, (uint32_t) l->focus);
    if (t.y < g.y) l->scroll -= g.y - t.y;
    if (t.y + t.h > g.y + g.h) l->scroll += t.y + t.h - (g.y + g.h);
    clamp_scroll(l);
}

static int32_t tile_at(const zui_applib_t *l, int32_t x, int32_t y)
{
    zui_rect_t g = zui_applib_grid(l);
    if (!in(g, x, y)) return -1;
    uint32_t c = zui_applib_columns(l);
    zui_rect_t t0 = zui_applib_tile(l, 0);
    int32_t sx = P(l, ZUI_APPLIB_TILE_W + ZUI_APPLIB_GAP), sy = row_step(l);
    if (x < t0.x || y < t0.y || sx <= 0 || sy <= 0) return -1;
    int32_t col = (x - t0.x) / sx, row = (y - t0.y) / sy;
    if (col >= (int32_t) c) return -1;
    zui_rect_t t = zui_applib_tile(l, (uint32_t) (row * (int32_t) c + col));
    if (!in(t, x, y)) return -1; /* in a gap */
    int32_t pos = row * (int32_t) c + col;
    return pos < (int32_t) l->nshown ? pos : -1;
}

/* ---- events -------------------------------------------------------------- */

static void query_changed(zui_applib_t *l)
{
    l->query[l->qlen] = 0;
    refilter(l);
}

static void back_one(zui_applib_t *l)
{
    if (!l->qlen) return;
    l->qlen--;
    while (l->qlen && ((uint8_t) l->query[l->qlen] & 0xC0u) == 0x80u) l->qlen--;
    query_changed(l);
}

static void choose(zui_applib_t *l, int32_t pos)
{
    if (pos < 0 || pos >= (int32_t) l->nshown) return;
    l->act = ZUI_APPLIB_OPEN;
    l->act_app = l->shown[pos];
}

static bool key(zui_applib_t *l, const zui_event_t *e)
{
    uint32_t c = zui_applib_columns(l);
    int32_t n = (int32_t) l->nshown, f = l->focus;
    int32_t page = (int32_t) c * (zui_applib_grid(l).h / (row_step(l) > 0 ? row_step(l) : 1));
    if (page < (int32_t) c) page = (int32_t) c;
    switch (e->key) {
    case ZUI_KEY_ESCAPE:
        if (l->qlen) {
            l->qlen = 0;
            query_changed(l);
        } else {
            l->act = ZUI_APPLIB_CLOSE;
        }
        return true;
    case ZUI_KEY_BACKSPACE:
        back_one(l);
        return true;
    case ZUI_KEY_ENTER:
        choose(l, f);
        return true;
    case ZUI_KEY_LEFT:
        f -= 1;
        break;
    case ZUI_KEY_RIGHT:
        f += 1;
        break;
    case ZUI_KEY_UP:
        f -= (int32_t) c;
        break;
    case ZUI_KEY_DOWN:
        f += (int32_t) c;
        break;
    case ZUI_KEY_PAGE_UP:
        f -= page;
        break;
    case ZUI_KEY_PAGE_DOWN:
        f += page;
        break;
    case ZUI_KEY_HOME:
        f = 0;
        break;
    case ZUI_KEY_END:
        f = n - 1;
        break;
    case ZUI_KEY_TAB: {
        uint32_t next = (l->layer + ((e->mods & ZUI_MOD_SHIFT) ? 3u : 1u)) % 4u;
        zui_applib_set_layer(l, next);
        return true;
    }
    default:
        return false;
    }
    if (!n) return true;
    if (f < 0) f = 0;
    if (f >= n) f = n - 1;
    l->focus = f;
    reveal(l);
    return true;
}

static void text(zui_applib_t *l, const char *t)
{
    for (uint32_t i = 0; t[i]; i++) {
        uint8_t b = (uint8_t) t[i];
        if (b < 0x20u || b == 0x7Fu) continue; /* controls never search */
        if (l->qlen + 1 >= ZUI_APPLIB_QUERY) break;
        l->query[l->qlen++] = (char) b;
    }
    query_changed(l);
}

bool zui_applib_event(zui_applib_t *l, const zui_event_t *e)
{
    l->act = ZUI_APPLIB_NONE;
    l->act_app = -1;
    switch (e->type) {
    case ZUI_EV_KEY_DOWN:
        if (e->mods & (ZUI_MOD_CMD | ZUI_MOD_CTRL | ZUI_MOD_META)) return false;
        return key(l, e);
    case ZUI_EV_TEXT:
        if (!e->text[0]) return false;
        text(l, e->text);
        return true;
    case ZUI_EV_SCROLL:
        if (!in(l->box, e->x, e->y)) return false;
        l->scroll += e->dy;
        clamp_scroll(l);
        l->hover = tile_at(l, e->x, e->y);
        return true;
    case ZUI_EV_PTR_MOVE:
        if (!in(l->box, e->x, e->y)) {
            l->hover = -1;
            return false;
        }
        l->hover = tile_at(l, e->x, e->y);
        return true;
    case ZUI_EV_PTR_DOWN:
        if (!in(l->box, e->x, e->y) || e->button != ZUI_BTN_LEFT) return false;
        l->press = tile_at(l, e->x, e->y);
        l->press_seg = -1;
        l->press_clear = false;
        for (uint32_t k = 0; k < SEGS; k++)
            if (in(zui_applib_segment(l, k), e->x, e->y)) l->press_seg = (int32_t) k;
        if (l->qlen && in(zui_applib_clear_btn(l), e->x, e->y)) l->press_clear = true;
        if (!l->nshown && l->qlen) { /* AL4: the empty state's button */
            zui_rect_t g = zui_applib_grid(l);
            int32_t bw = P(l, 140), bh = P(l, 32);
            zui_rect_t b = {g.x + (g.w - bw) / 2, g.y + P(l, 150), bw, bh};
            if (in(b, e->x, e->y)) l->press_clear = true;
        }
        if (l->press >= 0) l->focus = l->press;
        return true;
    case ZUI_EV_PTR_UP: {
        if (e->button != ZUI_BTN_LEFT) return false;
        bool used = l->press >= 0 || l->press_seg >= 0 || l->press_clear;
        if (l->press >= 0 && tile_at(l, e->x, e->y) == l->press) choose(l, l->press);
        if (l->press_seg >= 0 && in(zui_applib_segment(l, (uint32_t) l->press_seg), e->x, e->y))
            zui_applib_set_layer(l, (uint32_t) l->press_seg);
        if (l->press_clear) {
            l->qlen = 0;
            query_changed(l);
        }
        l->press = -1;
        l->press_seg = -1;
        l->press_clear = false;
        return used || in(l->box, e->x, e->y);
    }
    case ZUI_EV_RESIZE:
    case ZUI_EV_SCALE:
        clamp_scroll(l);
        return false;
    default:
        return false;
    }
}

/* ---- drawing -------------------------------------------------------------- */

static int32_t base_mid(const zui_text_t *t, zui_rect_t r)
{
    int32_t cap = (zui_text_ascent(t) * 7) / 10;
    return r.y + (r.h + cap) / 2;
}

static void text_mid(zui_surface_t *s, const zui_text_t *t, zui_rect_t r, uint32_t ink,
                     const char *str)
{
    if (!t || !t->font) return;
    int32_t w = zui_text_width(t, str);
    if (w > r.w) {
        zui_text_fit(s, t, r.x, base_mid(t, r), r.w, ink, str);
        return;
    }
    zui_text(s, t, r.x + (r.w - w) / 2, base_mid(t, r), ink, str);
}

/* the magnifier, drawn as vector strokes (a ring and a handle) */
static void magnifier(zui_surface_t *s, zui_vec_work_t *w, zui_rect_t r, uint32_t rgb)
{
    zui_vec_t v;
    zui_vec_init(&v, s, w);
    int32_t d = r.w < r.h ? r.w : r.h;
    int32_t cx = r.x + d * 42 / 100, cy = r.y + d * 42 / 100, rad = d * 28 / 100;
    zui_vec_stroke_t st = {0};
    st.width = d * 16 / 10; /* 1/16 px: a tenth of the mark */
    st.cap = ZUI_VEC_CAP_ROUND;
    st.join = ZUI_VEC_JOIN_ROUND;
    st.miter_q8 = 1024;
    zui_vec_paint_t p = zui_vec_solid(0xFF000000u | rgb);
    zui_vec_begin(&v);
    zui_vec_circle(&v, cx * 16, cy * 16, rad * 16);
    zui_vec_stroke(&v, &p, &st);
    zui_vec_begin(&v);
    zui_vec_move_to(&v, (cx + rad * 7 / 10) * 16, (cy + rad * 7 / 10) * 16);
    zui_vec_line_to(&v, (r.x + d * 88 / 100) * 16, (r.y + d * 88 / 100) * 16);
    zui_vec_stroke(&v, &p, &st);
}

/* AL6: an icon, from the cache when it can */
static void icon(zui_applib_t *l, zui_surface_t *s, uint32_t idx, zui_rect_t box, uint32_t ground)
{
    zui_app_t app;
    if (!zui_apps_get(l->apps, idx, &app)) return;
    zui_applib_cache_t *c = l->cache;
    int32_t side = box.w < box.h ? box.w : box.h;
    if (!c || !c->px || !c->slots || side <= 0 || (uint32_t) side > c->side) {
        zui_app_icon(s, l->work, &app, box, 0);
        return;
    }
    uint32_t slot = idx % c->slots;
    uint32_t *px = c->px + (uint64_t) slot * c->side * c->side;
    if (c->tag[slot] != (int32_t) idx || c->tag_size[slot] != side) {
        zui_surface_t cs;
        zui_surface_init(&cs, px, side, side, side);
        zui_fill(&cs, (zui_rect_t){0, 0, side, side}, ground);
        zui_app_icon(&cs, l->work, &app, (zui_rect_t){0, 0, side, side}, 0);
        c->tag[slot] = (int32_t) idx;
        c->tag_size[slot] = side;
    }
    zui_rect_t d = meet((zui_rect_t){box.x, box.y, side, side}, s->clip);
    for (int32_t y = d.y; y < d.y + d.h; y++) {
        const uint32_t *src = px + (y - box.y) * side + (d.x - box.x);
        uint32_t *dst = s->px + (int64_t) y * s->stride + d.x;
        for (int32_t x = 0; x < d.w; x++) dst[x] = src[x];
    }
}

static void header(zui_applib_t *l, zui_surface_t *s)
{
    int32_t x = l->box.x + pad(l), y = l->box.y + pad(l);
    zui_rect_t head = {x, y, l->box.w - 2 * pad(l), head_h(l)};
    if (l->f.headline && l->f.headline->font)
        zui_text(s, l->f.headline, x, base_mid(l->f.headline, head), ZUI_TEXT, "Apps");
    char line[64], *o = line, *end = line + sizeof line;
    uint32_t all = zui_apps_count(l->apps);
    if (l->nshown == all) {
        o = put_u(o, end, all);
        put(o, end, " apps");
    } else {
        o = put(o, end, "Showing ");
        o = put_u(o, end, l->nshown);
        o = put(o, end, " of ");
        o = put_u(o, end, all);
    }
    if (l->f.body && l->f.body->font) {
        int32_t w = zui_text_width(l->f.body, line);
        zui_text(s, l->f.body, head.x + head.w - w, base_mid(l->f.body, head), ZUI_MUTED, line);
    }

    /* the layer switch: one carved track, the chosen half raised */
    static const char *const NAME[SEGS] = {"All", "Layer 1", "Layer 2", "Layer 3"};
    zui_rect_t a = zui_applib_segment(l, 0), z = zui_applib_segment(l, SEGS - 1);
    zui_rect_t track = {a.x, a.y, z.x + z.w - a.x, a.h};
    int32_t rad = P(l, ZUI_R_CONTROL);
    zui_fill_rrect(s, track, rad, ZUI_PANEL, 255);
    zui_engrave(s, track, rad, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 40);
    for (uint32_t k = 0; k < SEGS; k++) {
        zui_rect_t r = zui_applib_segment(l, k);
        bool on = l->layer == k;
        if (on) {
            zui_rect_t in2 = {r.x + P(l, 2), r.y + P(l, 2), r.w - P(l, 4), r.h - P(l, 4)};
            zui_fill_rrect(s, in2, rad - P(l, 2), ZUI_RAISED, 255);
            zui_stroke_rrect(s, in2, rad - P(l, 2), P(l, 1) > 0 ? P(l, 1) : 1, ZUI_GOLD, 255);
        }
        char lab[24], *q = lab, *qe = lab + sizeof lab;
        q = put(q, qe, NAME[k]);
        q = put(q, qe, "  ");
        put_u(q, qe, zui_apps_in_layer(l->apps, k));
        text_mid(s, l->f.label, r, on ? ZUI_TEXT : ZUI_MUTED, lab);
    }

    /* the search field */
    zui_rect_t f = zui_applib_field(l);
    zui_fill_rrect(s, f, rad, ZUI_PANEL, 255);
    zui_stroke_rrect(s, f, rad, P(l, 1) > 0 ? P(l, 1) : 1, l->qlen ? ZUI_GOLD : ZUI_EDGE, 255);
    int32_t g = f.h - P(l, ZUI_S4);
    zui_rect_t mg = {f.x + P(l, ZUI_S2), f.y + (f.h - g) / 2, g, g};
    magnifier(s, l->work, mg, ZUI_MUTED);
    zui_rect_t tr = {mg.x + g + P(l, ZUI_S2), f.y, f.w - g - P(l, ZUI_S4) - f.h, f.h};
    if (l->f.body && l->f.body->font) {
        int32_t b = base_mid(l->f.body, f);
        if (l->qlen) {
            int32_t end_x = zui_text_fit(s, l->f.body, tr.x, b, tr.w, ZUI_TEXT, l->query);
            zui_fill(s,
                     (zui_rect_t){end_x + P(l, 1), f.y + P(l, ZUI_S2), P(l, 2) > 0 ? P(l, 2) : 1,
                                  f.h - P(l, ZUI_S4)},
                     ZUI_GOLD); /* the caret: typing always lands here */
        } else {
            char ph[40], *pq = ph, *pe = ph + sizeof ph;
            pq = put(pq, pe, "Search ");
            pq = put_u(pq, pe, zui_apps_count(l->apps));
            put(pq, pe, " apps");
            zui_text_fit(s, l->f.body, tr.x, b, tr.w, ZUI_MUTED, ph);
        }
    }
    if (l->qlen) { /* the clear button: an x in a ring */
        zui_rect_t cb = zui_applib_clear_btn(l);
        int32_t m = cb.w / 3, t2 = P(l, 2) > 0 ? P(l, 2) : 1;
        for (int32_t i = 0; i <= cb.w - 2 * m; i++) {
            zui_fill(s, (zui_rect_t){cb.x + m + i, cb.y + m + i, t2, t2}, ZUI_MUTED);
            zui_fill(s, (zui_rect_t){cb.x + cb.w - m - i - t2, cb.y + m + i, t2, t2}, ZUI_MUTED);
        }
    }
}

static void empty(zui_applib_t *l, zui_surface_t *s)
{
    zui_rect_t g = zui_applib_grid(l);
    int32_t d = P(l, 64);
    magnifier(s, l->work, (zui_rect_t){g.x + (g.w - d) / 2, g.y + P(l, 40), d, d}, ZUI_GOLD);
    char line[96], *o = line, *end = line + sizeof line;
    o = put(o, end, "No app matches \xE2\x80\x9C");
    o = put(o, end, l->query);
    put(o, end, "\xE2\x80\x9D");
    if (l->f.title && l->f.title->font) {
        int32_t w = zui_text_width(l->f.title, line);
        if (w > g.w) w = g.w;
        zui_text_fit(s, l->f.title, g.x + (g.w - w) / 2, g.y + P(l, 130), g.w, ZUI_TEXT, line);
    }
    int32_t bw = P(l, 140), bh = P(l, 32);
    zui_rect_t b = {g.x + (g.w - bw) / 2, g.y + P(l, 150), bw, bh};
    zui_fill_rrect(s, b, P(l, ZUI_R_CONTROL), ZUI_GOLD, 255);
    text_mid(s, l->f.body, b, ZUI_VOID, "Clear search");
}

static void scroll_indicator(zui_applib_t *l, zui_surface_t *s)
{
    int32_t m = zui_applib_max_scroll(l);
    if (m <= 0) return;
    zui_rect_t g = zui_applib_grid(l);
    int32_t content = g.h + m;
    int32_t th = (int32_t) ((int64_t) g.h * g.h / content);
    if (th < P(l, 24)) th = P(l, 24);
    int32_t ty = g.y + (int32_t) ((int64_t) (g.h - th) * l->scroll / m);
    int32_t w = P(l, 4) > 0 ? P(l, 4) : 1;
    int32_t x = l->box.x + l->box.w - pad(l) / 2 - w / 2;
    zui_fill_rrect(s, (zui_rect_t){x, g.y, w, g.h}, w / 2, ZUI_HAIRLINE, 255);
    zui_fill_rrect(s, (zui_rect_t){x, ty, w, th}, w / 2, ZUI_MUTED, 255);
}

void zui_applib_draw(zui_applib_t *l, zui_surface_t *s)
{
    zui_rect_t keep = s->clip;
    set_clip(s, meet(l->box, keep));
    zui_fill(s, l->box, ZUI_VOID);
    header(l, s);
    if (!l->nshown) {
        empty(l, s);
        s->clip = keep;
        return;
    }
    zui_rect_t g = zui_applib_grid(l);
    set_clip(s, meet(g, keep));
    uint32_t c = zui_applib_columns(l);
    int32_t rs = row_step(l);
    uint32_t first = rs > 0 ? (uint32_t) (l->scroll / rs) * c : 0;
    int32_t is = P(l, ZUI_APPLIB_ICON), rad = P(l, ZUI_R_PANEL);
    int32_t ring = P(l, 2) > 0 ? P(l, 2) : 1;
    for (uint32_t pos = first; pos < l->nshown; pos++) {
        zui_rect_t t = zui_applib_tile(l, pos);
        if (t.y >= g.y + g.h) break;
        bool hot = (int32_t) pos == l->hover, foc = (int32_t) pos == l->focus;
        if (hot || foc) {
            zui_fill_rrect(s, t, rad, ZUI_PANEL, 255);
            zui_stroke_rrect(s, t, rad, ring, foc ? ZUI_GOLD : ZUI_EDGE, 255);
        }
        zui_rect_t ib = {t.x + (t.w - is) / 2, t.y + P(l, ZUI_S3), is, is};
        zui_app_t app;
        zui_apps_get(l->apps, l->shown[pos], &app);
        if (hot || foc) /* on the raised tile: drawn, not copied from the cache */
            zui_app_icon(s, l->work, &app, ib, 0);
        else
            icon(l, s, l->shown[pos], ib, ZUI_VOID);
        zui_rect_t nb = {t.x + P(l, ZUI_S1), ib.y + is + P(l, ZUI_S2), t.w - P(l, ZUI_S2),
                         P(l, 20)};
        text_mid(s, l->f.label, nb, hot || foc ? ZUI_TEXT : ZUI_TEXT_2, app.name);
        zui_rect_t lb = {nb.x, nb.y + nb.h, nb.w, P(l, 16)};
        text_mid(s, l->f.caption, lb, ZUI_MUTED, app.category);
    }
    s->clip = meet(l->box, keep);
    scroll_indicator(l, s);
    s->clip = keep;
}

/* ---- the card (AL5) ------------------------------------------------------ */

/* draw s wrapped to w at (x, y top); returns the y under the last line */
static int32_t wrap(zui_surface_t *s, const zui_text_t *t, int32_t x, int32_t y, int32_t w,
                    uint32_t ink, const char *str, uint32_t q8)
{
    if (!t || !t->font || !str[0]) return y;
    int32_t lh = zui_text_ascent(t) - zui_text_descent(t) + pt(q8, 4);
    char line[160];
    uint32_t i = 0, n = slen(str);
    while (i < n) {
        while (i < n && str[i] == ' ') i++;
        if (i >= n) break;
        uint32_t end = i, j = i;
        while (j < n) { /* add whole words while they fit */
            uint32_t k = j;
            while (k < n && str[k] != ' ') k++;
            if (k - i + 1 > sizeof line) break;
            for (uint32_t q = i; q < k; q++) line[q - i] = str[q];
            line[k - i] = 0;
            bool fits = zui_text_width(t, line) <= w;
            if (!fits && end > i) break;
            end = k; /* a single word too long for the line still goes (fit cuts it) */
            if (!fits) break;
            j = k;
            while (j < n && str[j] == ' ') j++;
        }
        if (end == i) end = i + (n - i < sizeof line - 1 ? n - i : (uint32_t) sizeof line - 1);
        for (uint32_t q = i; q < end; q++) line[q - i] = str[q];
        line[end - i] = 0;
        zui_text_fit(s, t, x, y + zui_text_ascent(t), w, ink, line);
        y += lh;
        i = end;
    }
    return y;
}

/* the status line sits under the name and the meta line, beside the icon */
static int32_t status_y(uint32_t q8, zui_rect_t box)
{
    return box.y + pt(q8, ZUI_S5) + pt(q8, 64);
}
static int32_t text_x(uint32_t q8, zui_rect_t box)
{
    return box.x + pt(q8, ZUI_S5) + pt(q8, 96) + pt(q8, ZUI_S5);
}

zui_rect_t zui_applib_card_open(uint32_t q8, bool built, zui_rect_t box)
{
    if (!built) return (zui_rect_t){0, 0, 0, 0};
    return (zui_rect_t){text_x(q8, box), status_y(q8, box), pt(q8, 120), pt(q8, 32)};
}

static int32_t section(zui_surface_t *s, const zui_applib_faces_t *f, uint32_t q8, int32_t x,
                       int32_t y, int32_t w, const char *title, const char *body)
{
    if (!body || !body[0]) return y;
    if (f->caption && f->caption->font) {
        zui_text(s, f->caption, x, y + zui_text_ascent(f->caption), ZUI_GOLD, title);
        y += zui_text_ascent(f->caption) - zui_text_descent(f->caption) + pt(q8, ZUI_S1);
    }
    y = wrap(s, f->body, x, y, w, ZUI_TEXT_2, body, q8);
    return y + pt(q8, ZUI_S4);
}

void zui_applib_card(zui_surface_t *s, zui_vec_work_t *w, const zui_applib_faces_t *f, uint32_t q8,
                     const zui_app_t *app, bool built, zui_rect_t box)
{
    zui_rect_t keep = s->clip;
    set_clip(s, meet(box, keep));
    zui_fill(s, box, ZUI_VOID);
    int32_t m = pt(q8, ZUI_S5), is = pt(q8, 96);
    zui_rect_t ib = {box.x + m, box.y + m, is, is};
    zui_app_icon(s, w, app, ib, 0);
    int32_t tx = text_x(q8, box);
    zui_rect_t ob = zui_applib_card_open(q8, built, box);
    int32_t tw = box.x + box.w - m - tx;
    if (f->headline && f->headline->font)
        zui_text_fit(s, f->headline, tx, ib.y + zui_text_ascent(f->headline), tw, ZUI_TEXT,
                     app->name);
    char meta[96], *o = meta, *end = meta + sizeof meta;
    o = put(o, end, "Layer ");
    o = put_u(o, end, app->layer);
    o = put(o, end, " \xC2\xB7 ");
    o = put(o, end, app->category);
    o = put(o, end, " \xC2\xB7 No. ");
    put_u(o, end, app->number);
    int32_t y = ib.y + pt(q8, 40);
    if (f->body && f->body->font)
        zui_text_fit(s, f->body, tx, y + zui_text_ascent(f->body), tw, ZUI_MUTED, meta);
    y = status_y(q8, box);
    /* the status: gold Open when there is a window, else said plainly */
    int32_t rad = pt(q8, ZUI_R_CONTROL);
    if (built) {
        zui_fill_rrect(s, ob, rad, ZUI_GOLD, 255);
        text_mid(s, f->body, ob, ZUI_VOID, "Open");
    } else if (f->label && f->label->font) {
        const char *ns = "Not built yet";
        int32_t pw = zui_text_width(f->label, ns) + pt(q8, ZUI_S4);
        zui_rect_t pill = {tx, y + pt(q8, 4), pw, pt(q8, 24)};
        zui_stroke_rrect(s, pill, pill.h / 2, pt(q8, 1) > 0 ? pt(q8, 1) : 1, ZUI_EDGE, 255);
        text_mid(s, f->label, pill, ZUI_MUTED, ns);
    }
    y = ib.y + is + pt(q8, ZUI_S5);
    int32_t x = box.x + m, cw = box.w - 2 * m;
    zui_hline(s, x, y - pt(q8, ZUI_S3), cw, 1, ZUI_HAIRLINE);
    y = section(s, f, q8, x, y, cw, "What it does", app->does);
    y = section(s, f, q8, x, y, cw, "Built from", app->built_from);
    if (app->pillars && app->pillars[0]) { /* one line per language */
        if (f->caption && f->caption->font) {
            zui_text(s, f->caption, x, y + zui_text_ascent(f->caption), ZUI_GOLD, "Languages");
            y += zui_text_ascent(f->caption) - zui_text_descent(f->caption) + pt(q8, ZUI_S1);
        }
        const char *p = app->pillars;
        while (*p) {
            char part[128];
            uint32_t k = 0;
            while (*p == ' ') p++;
            while (*p && *p != ';' && k + 1 < sizeof part) part[k++] = *p++;
            part[k] = 0;
            if (*p == ';') p++;
            if (k) y = wrap(s, f->body, x, y, cw, ZUI_TEXT_2, part, q8);
        }
        y += pt(q8, ZUI_S4);
    }
    section(s, f, q8, x, y, cw, "When facts disagree", app->paraconsistent);
    s->clip = keep;
}
