/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* circuit.c — the Circuit board's screen (circuit.h). */
#include "circuit.h"
#include "zui_tokens.h"

/* Sizes on the board, in points (scaled to fit, below). */
enum {
    PART_W = 176,
    PART_HEAD = 40,
    PART_ROW = 24,
    PART_FOOT = 10,
    IN_W = 112,
    IN_H = 32,
    OUT_W = 128,
    OUT_H = 36,
    DOT = 5,
    MARGIN = 24
};

/* ---- geometry ------------------------------------------------------------------ */

static zui_rect_t board_rect(const cb_view_t *v)
{
    const zui_widget_t *w = zui_w(v->sh.ui, v->board);
    zui_rect_t r = {0, 0, 0, 0};
    return w ? w->r : r;
}

static void node_pt(const cb_view_t *v, int node, uint32_t i, int32_t *x, int32_t *y, int32_t *w,
                    int32_t *h)
{
    const zxwire_circuit_t *c = &v->b->c;
    if (node == ZXBOARD_NODE_PART) {
        uint32_t np = c->part[i].nports ? c->part[i].nports : 1u;
        *x = c->part[i].x, *y = c->part[i].y, *w = PART_W;
        *h = PART_HEAD + (int32_t) np * PART_ROW + PART_FOOT;
    } else if (node == ZXBOARD_NODE_INPUT) {
        *x = c->in_x[i], *y = c->in_y[i], *w = IN_W, *h = IN_H;
    } else {
        *x = c->out_x, *y = c->out_y, *w = OUT_W, *h = OUT_H;
    }
}

/* Points to pixels, shrunk (never grown) so every node fits the board. */
static int32_t zoom_q8(const cb_view_t *v)
{
    zui_rect_t r = board_rect(v);
    const zxwire_circuit_t *c = &v->b->c;
    int32_t ex = 0, ey = 0, x, y, w, h;
    for (uint32_t i = 0; i < c->nparts + c->ninputs + 1u; i++) {
        int node = i < c->nparts                ? ZXBOARD_NODE_PART
                   : i < c->nparts + c->ninputs ? ZXBOARD_NODE_INPUT
                                                : ZXBOARD_NODE_OUT;
        uint32_t k = node == ZXBOARD_NODE_PART ? i : node == ZXBOARD_NODE_INPUT ? i - c->nparts : 0;
        node_pt(v, node, k, &x, &y, &w, &h);
        if (x + w > ex) ex = x + w;
        if (y + h > ey) ey = y + h;
    }
    int32_t pw = zui_px(&v->sh.ui->screen, ex + MARGIN),
            ph = zui_px(&v->sh.ui->screen, ey + MARGIN);
    int32_t z = 256;
    if (pw > 0 && r.w > 0 && (int64_t) pw * 256 > (int64_t) r.w * z)
        z = (int32_t) (((int64_t) r.w * 256) / pw);
    if (ph > 0 && r.h > 0 && (int64_t) ph * z > (int64_t) r.h * 256)
        z = (int32_t) (((int64_t) r.h * 256) / ph);
    return z < 96 ? 96 : z; /* past this the text would not fit: the board scrolls instead */
}

static int32_t P(const cb_view_t *v, int32_t pt, int32_t z)
{
    return (int32_t) (((int64_t) zui_px(&v->sh.ui->screen, pt) * z) / 256);
}

zui_rect_t cb_node_rect(const cb_view_t *v, int node, uint32_t i)
{
    zui_rect_t r = board_rect(v), o;
    int32_t x, y, w, h, z = zoom_q8(v);
    node_pt(v, node, i, &x, &y, &w, &h);
    o.x = r.x + P(v, x, z), o.y = r.y + P(v, y, z), o.w = P(v, w, z), o.h = P(v, h, z);
    return o;
}

void cb_port_at(const cb_view_t *v, uint32_t part, uint32_t port, int32_t *x, int32_t *y)
{
    zui_rect_t r = cb_node_rect(v, ZXBOARD_NODE_PART, part);
    int32_t z = zoom_q8(v);
    *x = r.x;
    *y = r.y + P(v, PART_HEAD + PART_ROW / 2 + PART_ROW * (int32_t) port, z);
}

void cb_answer_at(const cb_view_t *v, int node, uint32_t i, int32_t *x, int32_t *y)
{
    zui_rect_t r = cb_node_rect(v, node, i);
    int32_t z = zoom_q8(v);
    *x = r.x + r.w;
    *y = node == ZXBOARD_NODE_PART ? r.y + P(v, PART_HEAD + PART_ROW / 2, z) : r.y + r.h / 2;
}

/* The out node's one input. */
static void out_in_at(const cb_view_t *v, int32_t *x, int32_t *y)
{
    zui_rect_t r = cb_node_rect(v, ZXBOARD_NODE_OUT, 0);
    *x = r.x;
    *y = r.y + r.h / 2;
}

static void src_at(const cb_view_t *v, const zxwire_src_t *s, int32_t *x, int32_t *y)
{
    cb_answer_at(v, s->kind == ZXWIRE_SRC_PART ? ZXBOARD_NODE_PART : ZXBOARD_NODE_INPUT, s->index,
                 x, y);
}

/* ---- painting ------------------------------------------------------------------- */

static int32_t iabs(int32_t a)
{
    return a < 0 ? -a : a;
}

/* A straight run, solid or dashed. */
static void run(zui_surface_t *s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t t,
                int32_t dash, uint32_t rgb)
{
    bool h = y0 == y1;
    int32_t a = h ? (x0 < x1 ? x0 : x1) : (y0 < y1 ? y0 : y1);
    int32_t len = h ? iabs(x1 - x0) : iabs(y1 - y0);
    for (int32_t at = 0; at <= len; at += dash ? dash * 2 : len + 1) {
        int32_t n = dash ? (at + dash > len ? len - at + 1 : dash) : len + 1;
        if (h)
            zui_hline(s, a + at, y0 - t / 2, n, t, rgb);
        else
            zui_vline(s, x0 - t / 2, a + at, n, t, rgb);
    }
}

/* CV3: a wire, as a circuit trace: across, down, across. */
static void trace(zui_surface_t *s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t t,
                  int32_t dash, uint32_t rgb)
{
    int32_t mx = x1 > x0 ? (x0 + x1) / 2 : x0 + t * 8;
    if (x1 <= x0) { /* backwards: out, down, back, in */
        int32_t my = (y0 + y1) / 2;
        int32_t lx = x1 - t * 8;
        run(s, x0, y0, mx, y0, t, dash, rgb);
        run(s, mx, y0, mx, my, t, dash, rgb);
        run(s, mx, my, lx, my, t, dash, rgb);
        run(s, lx, my, lx, y1, t, dash, rgb);
        run(s, lx, y1, x1, y1, t, dash, rgb);
        return;
    }
    run(s, x0, y0, mx, y0, t, dash, rgb);
    run(s, mx, y0, mx, y1, t, dash, rgb);
    run(s, mx, y1, x1, y1, t, dash, rgb);
}

static uint32_t state_rgb(int32_t st)
{
    return st == ZXWIRE_OK ? ZUI_OK : st == ZXWIRE_HELD ? ZUI_GOLD : ZUI_FAINT;
}

/* CV3: a dot: round for numbers, square for truths; filled when wired. */
static void dot(zui_surface_t *s, int32_t x, int32_t y, int32_t r, uint8_t kind, int32_t st,
                bool wired)
{
    zui_rect_t b = {x - r, y - r, 2 * r + 1, 2 * r + 1};
    int32_t rad = kind == ZXAPP_KIND_TRUTH ? 1 : r;
    zui_fill_rrect(s, b, rad, ZUI_VOID, 255);
    if (wired)
        zui_fill_rrect(s, b, rad, state_rgb(st), 255);
    else
        zui_stroke_rrect(s, b, rad, 1, state_rgb(st), 255);
}

static const zui_text_t *font(const zui_ui_t *ui, int which)
{
    return which == 0 ? &ui->theme.body : which == 1 ? &ui->theme.label : &ui->theme.mono;
}

/* The font's ascent; with no font (tests) the fixed-advance stand-in. */
static int32_t asc(const zui_text_t *t)
{
    return t->font ? zui_text_ascent(t) : (int32_t) (t->size_px * 3u / 4u);
}

/* CV5: the badge: a gold ring with the part's initial, all vector. */
static void badge(const zui_ui_t *ui, zui_surface_t *s, int32_t cx, int32_t cy, int32_t r,
                  const char *word)
{
    zui_rect_t b = {cx - r, cy - r, 2 * r + 1, 2 * r + 1};
    zui_fill_rrect(s, b, r, ZUI_VOID, 255);
    zui_stroke_rrect(s, b, r, r > 8 ? 2 : 1, ZUI_GOLD, 255);
    char ch[2] = {word[0] >= 'a' && word[0] <= 'z' ? (char) (word[0] - 32) : word[0], 0};
    const zui_text_t *t = font(ui, 1);
    int32_t tw = zui_text_width(t, ch);
    zui_text(s, t, cx - tw / 2, cy + asc(t) / 2 - 1, ZUI_GOLD, ch);
}

static void paint_part(cb_view_t *v, zui_surface_t *s, uint32_t i)
{
    zui_ui_t *ui = v->sh.ui;
    const zxwire_part_t *P0 = &v->b->c.part[i];
    zui_rect_t r = cb_node_rect(v, ZXBOARD_NODE_PART, i);
    int32_t z = zoom_q8(v), rad = P(v, ZUI_R_PANEL, z);
    bool missing = v->chk.part[i] != ZXWIRE_OK;
    zui_fill_rrect(s, r, rad, ZUI_PANEL, 255);
    if (missing)
        zui_dash_rrect(s, r, rad, 1, P(v, 4, z), ZUI_FAINT);
    else
        zui_engrave(s, r, rad, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 48);
    if (v->node == ZXBOARD_NODE_PART && v->sel == i) zui_stroke_rrect(s, r, rad, 2, ZUI_TEXT, 255);
    const zui_text_t *body = font(ui, 0), *lab = font(ui, 1);
    int32_t br = P(v, 11, z);
    badge(ui, s, r.x + P(v, 8, z) + br, r.y + P(v, 20, z), br, P0->entry);
    int32_t tx = r.x + P(v, 16, z) + 2 * br, tw = r.x + r.w - tx - P(v, 8, z);
    zui_text_fit(s, body, tx, r.y + P(v, 18, z), tw, ZUI_TEXT, P0->name);
    zui_text_fit(s, lab, tx, r.y + P(v, 33, z), tw, missing ? ZUI_ALERT : ZUI_MUTED,
                 missing ? "not on the shelf" : P0->entry);
    zui_hline(s, r.x + P(v, 8, z), r.y + P(v, PART_HEAD - 2, z), r.w - P(v, 16, z), 1,
              ZUI_HAIRLINE);
    int32_t dr = P(v, DOT, z);
    for (uint32_t j = 0; j < P0->nports; j++) {
        int32_t x, y;
        cb_port_at(v, i, j, &x, &y);
        bool on = v->node == ZXBOARD_NODE_PART && v->sel == i && v->port == (int32_t) j;
        zui_text_fit(s, lab, x + P(v, 12, z), y + asc(lab) / 2, r.w / 2, on ? ZUI_TEXT : ZUI_TEXT_2,
                     P0->port[j]);
        dot(s, x, y, dr, ZXAPP_KIND_NUM, v->chk.port[i][j], P0->src[j].kind != ZXWIRE_SRC_NONE);
        if (on)
            zui_stroke_rrect(s, (zui_rect_t){x - dr - 3, y - dr - 3, 2 * dr + 7, 2 * dr + 7},
                             dr + 3, 1, ZUI_TEXT, 255);
    }
    int32_t ax, ay;
    cb_answer_at(v, ZXBOARD_NODE_PART, i, &ax, &ay);
    const char *kw = v->chk.kind[i] == ZXAPP_KIND_TRUTH ? "truth" : v->chk.kind[i] ? "num" : "?";
    zui_text(s, lab, ax - P(v, 12, z) - zui_text_width(lab, kw), ay + asc(lab) / 2, ZUI_MUTED, kw);
    dot(s, ax, ay, dr, v->chk.kind[i] ? v->chk.kind[i] : ZXAPP_KIND_NUM,
        missing ? ZXWIRE_OPEN : ZXWIRE_OK, !missing);
}

static void paint_pin(cb_view_t *v, zui_surface_t *s, int node, uint32_t i)
{
    zui_ui_t *ui = v->sh.ui;
    const zxwire_circuit_t *c = &v->b->c;
    zui_rect_t r = cb_node_rect(v, node, i);
    int32_t z = zoom_q8(v), dr = P(v, DOT, z);
    zui_fill_rrect(s, r, r.h / 2, ZUI_RAISED, 255);
    zui_engrave(s, r, r.h / 2, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 40);
    if (v->node == node && (node == ZXBOARD_NODE_OUT || v->sel == i))
        zui_stroke_rrect(s, r, r.h / 2, 2, ZUI_TEXT, 255);
    const zui_text_t *lab = font(ui, 1);
    int32_t base = r.y + r.h / 2 + asc(lab) / 2 - 1;
    if (node == ZXBOARD_NODE_INPUT) {
        zui_text_fit(s, lab, r.x + P(v, 14, z), base, r.w - P(v, 28, z), ZUI_TEXT, c->input[i]);
        int32_t x, y;
        cb_answer_at(v, node, i, &x, &y);
        dot(s, x, y, dr, ZXAPP_KIND_NUM, ZXWIRE_OK, true);
    } else {
        const char *t = c->kind == ZXAPP_KIND_TRUTH ? "answer: truth" : "answer: numbers";
        zui_text_fit(s, lab, r.x + P(v, 16, z), base, r.w - P(v, 24, z), ZUI_TEXT, t);
        int32_t x, y;
        out_in_at(v, &x, &y);
        dot(s, x, y, dr, c->kind, v->chk.out, c->out.kind != ZXWIRE_SRC_NONE);
    }
}

/* CV3: "held" on a wire that is BOTH. */
static void tag(cb_view_t *v, zui_surface_t *s, int32_t x, int32_t y)
{
    const zui_text_t *lab = font(v->sh.ui, 1);
    int32_t w = zui_text_width(lab, "held") + 10, h = asc(lab) + 6;
    zui_rect_t b = {x - w / 2, y - h / 2, w, h};
    zui_fill_rrect(s, b, 3, ZUI_VOID, 255);
    zui_dash_rrect(s, b, 3, 1, 3, ZUI_GOLD);
    zui_text(s, lab, b.x + 5, b.y + h - 4, ZUI_GOLD, "held");
}

static void wire_one(cb_view_t *v, zui_surface_t *s, const zxwire_src_t *src, int32_t x1,
                     int32_t y1, int32_t st)
{
    if (src->kind != ZXWIRE_SRC_INPUT && src->kind != ZXWIRE_SRC_PART) return;
    int32_t x0, y0, t = P(v, 2, zoom_q8(v));
    if (t < 1) t = 1;
    src_at(v, src, &x0, &y0);
    trace(s, x0, y0, x1, y1, t, st == ZXWIRE_OK ? 0 : P(v, 5, zoom_q8(v)) + 1, state_rgb(st));
    if (st == ZXWIRE_HELD) /* on the middle run: the vertical, or the way back */
        tag(v, s, (x0 + x1) / 2, (y0 + y1) / 2);
}

/* A setting, written beside the input it feeds. */
static void setting(cb_view_t *v, zui_surface_t *s, const zxwire_src_t *src, int32_t x, int32_t y)
{
    char t[64];
    uint32_t n = 0;
    for (uint32_t i = 0; i < src->nconst && n + 24u < sizeof t; i++) {
        if (i) t[n++] = ',';
        n += zv_i64(t + n, sizeof t - n, src->konst[i]);
    }
    t[n] = 0;
    const zui_text_t *m = font(v->sh.ui, 2);
    int32_t w = zui_text_width(m, t) + 10, h = asc(m) + 8;
    zui_rect_t b = {x - P(v, 10, zoom_q8(v)) - w, y - h / 2, w, h};
    zui_fill_rrect(s, b, 4, ZUI_RAISED, 255);
    zui_stroke_rrect(s, b, 4, 1, ZUI_EDGE, 255);
    zui_text(s, m, b.x + 5, b.y + h - 5, ZUI_TEXT, t);
}

static void paint_board(zui_ui_t *ui, zui_id_t w, zui_surface_t *s, zui_rect_t r, void *ctx)
{
    (void) w;
    cb_view_t *v = ctx;
    const zxwire_circuit_t *c = &v->b->c;
    int32_t rad = zui_px(&ui->screen, ZUI_R_PANEL);
    zui_fill_rrect(s, r, rad, ZUI_VOID, 255);
    zui_engrave(s, r, rad, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 32);
    int32_t g = P(v, 24, zoom_q8(v));
    if (g < 8) g = 8;
    for (int32_t y = r.y + g; y < r.y + r.h; y += g) /* the board's dot grid */
        for (int32_t x = r.x + g; x < r.x + r.w; x += g)
            zui_fill(s, (zui_rect_t){x, y, 1, 1}, ZUI_HAIRLINE);
    for (uint32_t i = 0; i < c->nparts; i++) /* wires under the tiles */
        for (uint32_t j = 0; j < c->part[i].nports; j++) {
            int32_t x, y;
            cb_port_at(v, i, j, &x, &y);
            wire_one(v, s, &c->part[i].src[j], x, y, v->chk.port[i][j]);
        }
    int32_t ox, oy;
    out_in_at(v, &ox, &oy);
    wire_one(v, s, &c->out, ox, oy, v->chk.out);
    for (uint32_t i = 0; i < c->ninputs; i++) paint_pin(v, s, ZXBOARD_NODE_INPUT, i);
    for (uint32_t i = 0; i < c->nparts; i++) paint_part(v, s, i);
    paint_pin(v, s, ZXBOARD_NODE_OUT, 0);
    for (uint32_t i = 0; i < c->nparts; i++) /* settings over the tiles' edges */
        for (uint32_t j = 0; j < c->part[i].nports; j++)
            if (c->part[i].src[j].kind == ZXWIRE_SRC_CONST) {
                int32_t x, y;
                cb_port_at(v, i, j, &x, &y);
                setting(v, s, &c->part[i].src[j], x, y);
            }
    if (c->out.kind == ZXWIRE_SRC_CONST) setting(v, s, &c->out, ox, oy);
    if (v->drag == 2) { /* CV2: the wire being drawn */
        int32_t x0, y0;
        cb_answer_at(v, v->drag_node, v->drag_i, &x0, &y0);
        trace(s, x0, y0, v->ptr_x, v->ptr_y, 2, 4, ZUI_TEXT_2);
    }
    if (!c->nparts) {
        const zui_text_t *b = font(ui, 0);
        const char *t = "Open an app or block on the shelf to place it here";
        zui_text(s, b, r.x + (r.w - zui_text_width(b, t)) / 2, r.y + r.h / 2, ZUI_MUTED, t);
    }
}

/* ---- the lists ------------------------------------------------------------------ */

static void shelf_row(const zui_ui_t *ui, zui_id_t list, int32_t row, zui_row_t *out, void *ctx)
{
    (void) ui, (void) list;
    const cb_view_t *v = ctx;
    zxboard_item_t it;
    if (row < 0 || !zxboard_shelf(v->b, (uint32_t) row, &it)) return;
    zv_cat(out->text, sizeof out->text, it.ref, 0);
    zv_cat(out->detail, sizeof out->detail, it.prov->kind == ZXAPP_KIND_TRUTH ? "truth" : "num", 0);
    out->lead_rgb = ZUI_GOLD;
}

static void src_words(const cb_view_t *v, const zxwire_src_t *s, char *t, uint32_t cap)
{
    const zxwire_circuit_t *c = &v->b->c;
    if (s->kind == ZXWIRE_SRC_INPUT)
        zv_cat(t, cap, "from ", c->input[s->index]);
    else if (s->kind == ZXWIRE_SRC_PART)
        zv_cat(t, cap, "from ", c->part[s->index].name);
    else if (s->kind == ZXWIRE_SRC_CONST) {
        uint32_t n = zv_cat(t, cap, "set ", 0);
        for (uint32_t i = 0; i < s->nconst && n + 22u < cap; i++) {
            if (i) n += zv_cat(t + n, cap - n, ",", 0);
            n += zv_i64(t + n, cap - n, s->konst[i]);
        }
    } else
        zv_cat(t, cap, "not wired", 0);
}

static void port_row(const zui_ui_t *ui, zui_id_t list, int32_t row, zui_row_t *out, void *ctx)
{
    (void) ui, (void) list;
    const cb_view_t *v = ctx;
    const zxwire_circuit_t *c = &v->b->c;
    if (v->node == ZXBOARD_NODE_OUT && row == 0) {
        zv_cat(out->text, sizeof out->text, "answer", 0);
        src_words(v, &c->out, out->detail, sizeof out->detail);
        out->lead_rgb = state_rgb(v->chk.out);
        return;
    }
    if (v->node != ZXBOARD_NODE_PART || row < 0 || (uint32_t) row >= c->part[v->sel].nports) return;
    zv_cat(out->text, sizeof out->text, c->part[v->sel].port[row], 0);
    src_words(v, &c->part[v->sel].src[row], out->detail, sizeof out->detail);
    out->lead_rgb = state_rgb(v->chk.port[v->sel][row]);
}

static void source_row(const zui_ui_t *ui, zui_id_t list, int32_t row, zui_row_t *out, void *ctx)
{
    (void) ui, (void) list;
    const cb_view_t *v = ctx;
    const zxwire_circuit_t *c = &v->b->c;
    if (row < 0) return;
    if ((uint32_t) row < c->ninputs) {
        zv_cat(out->text, sizeof out->text, c->input[row], 0);
        zv_cat(out->detail, sizeof out->detail, "input", 0);
        return;
    }
    uint32_t p = (uint32_t) row - c->ninputs;
    if (p >= c->nparts) return;
    zv_cat(out->text, sizeof out->text, c->part[p].name, 0);
    zv_cat(out->detail, sizeof out->detail, v->chk.kind[p] == ZXAPP_KIND_TRUTH ? "truth" : "num",
           0);
    out->muted = v->chk.kind[p] == ZXAPP_KIND_TRUTH; /* a truth will hold an input */
}

/* ---- building and syncing ------------------------------------------------------- */

void cb_view_build(cb_view_t *v, zui_ui_t *ui, zxboard_t *b)
{
    v->b = b;
    v->node = -1;
    v->sel = 0;
    v->port = -1;
    v->drag = 0;
    v->saved_len = 0;
    v->cid[0] = 0;
    v->setting_buf[0] = 0;
    zv_shell_t *s = &v->sh;
    char title[64];
    zv_cat(title, sizeof title, "Circuit board: ", b->name);
    zv_shell_build(s, ui, title);
    v->shelf = zui_list(ui, s->body, shelf_row, v);
    zui_set_name(ui, v->shelf, "Shelf");
    zui_set_text(ui, v->shelf, "Nothing on the shelf: no blocks are registered");
    zui_set_size(ui, v->shelf, ZUI_SIZE_FIXED, 220);
    zui_w(ui, v->shelf)->font = ZUI_TEXT_MONO;
    v->board = zui_canvas(ui, s->body, paint_board, v);
    zui_set_name(ui, v->board, "Board");
    zui_set_size(ui, v->board, ZUI_SIZE_FLEX, 1);
    v->side = zui_box(ui, s->body, ZUI_COL);
    zui_set_size(ui, v->side, ZUI_SIZE_FIXED, 260);
    zui_set_pad(ui, v->side, 0, ZUI_S2);
    v->part_name = zui_label(ui, v->side, "");
    zui_set_font(ui, v->part_name, ZUI_TEXT_TITLE, ZUI_TEXT);
    v->part_what = zui_label(ui, v->side, "");
    zui_set_font(ui, v->part_what, ZUI_TEXT_LABEL, ZUI_MUTED);
    v->kind = zui_button(ui, v->side, "", ZUI_BTN_SECONDARY);
    v->ports = zui_list(ui, v->side, port_row, v);
    zui_set_name(ui, v->ports, "Inputs");
    zui_set_size(ui, v->ports, ZUI_SIZE_FIXED, 150);
    zui_id_t wl = zui_label(ui, v->side, "Wire from");
    zui_set_font(ui, wl, ZUI_TEXT_LABEL, ZUI_MUTED);
    v->sources = zui_list(ui, v->side, source_row, v);
    zui_set_name(ui, v->sources, "Wire from");
    zui_set_size(ui, v->sources, ZUI_SIZE_FLEX, 1);
    v->setting = zui_field(ui, v->side, v->setting_buf, sizeof v->setting_buf, false);
    zui_set_name(ui, v->setting, "Setting");
    zui_w(ui, v->setting)->font = ZUI_TEXT_MONO;
    zv_cat(zui_w(ui, v->setting)->u.field.placeholder, ZUI_LABEL_MAX, "a setting: 25 or 1,2,3", 0);
    zui_id_t bar = zui_box(ui, v->side, ZUI_ROW);
    v->remove = zui_button(ui, bar, "Remove", ZUI_BTN_SECONDARY);
    v->save = zui_button(ui, bar, "Save", ZUI_BTN_PRIMARY);
    zui_set_shortcut(ui, v->save, "S");
    cb_view_sync(v);
}

void cb_view_sync(cb_view_t *v)
{
    zui_ui_t *ui = v->sh.ui;
    const zxwire_circuit_t *c = &v->b->c;
    zxboard_check(v->b, &v->chk);
    if (v->node == ZXBOARD_NODE_PART && v->sel >= c->nparts) v->node = -1;
    if (v->node == ZXBOARD_NODE_INPUT && v->sel >= c->ninputs) v->node = -1;
    zui_list_set_rows(ui, v->shelf, (int32_t) zxboard_shelf_count(v->b));
    int32_t nports = v->node == ZXBOARD_NODE_PART  ? (int32_t) c->part[v->sel].nports
                     : v->node == ZXBOARD_NODE_OUT ? 1
                                                   : 0;
    if (v->port >= nports) v->port = -1;
    zui_list_set_rows(ui, v->ports, nports);
    zui_list_select(ui, v->ports, v->port);
    zui_list_set_rows(ui, v->sources, (int32_t) (c->ninputs + c->nparts));
    const char *nm = v->node == ZXBOARD_NODE_PART    ? c->part[v->sel].name
                     : v->node == ZXBOARD_NODE_INPUT ? c->input[v->sel]
                     : v->node == ZXBOARD_NODE_OUT   ? "The answer"
                                                     : "Pick a part";
    zui_set_text(ui, v->part_name, nm);
    zui_set_text(ui, v->part_what,
                 v->node == ZXBOARD_NODE_PART    ? c->part[v->sel].entry
                 : v->node == ZXBOARD_NODE_INPUT ? "an input of this app"
                 : v->node == ZXBOARD_NODE_OUT   ? "what this app answers"
                                                 : "on the board, to wire it");
    zui_set_hidden(ui, v->kind, v->node != ZXBOARD_NODE_OUT);
    zui_set_text(ui, v->kind,
                 c->kind == ZXAPP_KIND_TRUTH ? "Answers a truth: make it numbers"
                                             : "Answers numbers: make it a truth");
    bool wiring = v->port >= 0;
    zui_set_hidden(ui, v->sources, !wiring);
    zui_set_hidden(ui, v->setting, !wiring);
    zui_set_disabled(ui, v->remove, v->node != ZXBOARD_NODE_PART && v->node != ZXBOARD_NODE_INPUT);
    /* CV3: the status line, in the logic's words */
    char *t = v->status_text;
    uint32_t cap = sizeof v->status_text, n = 0;
    if (v->chk.lp == ZXAPP_TRUE)
        n = zv_cat(t, cap, "TRUE: every wire fits", v->cid[0] ? ". Saved " : "");
    else if (v->chk.lp == ZXAPP_BOTH)
        n = zv_cat(t, cap, "BOTH, held: ", v->chk.why);
    else
        n = zv_cat(t, cap, "NEITHER, still to wire: ", v->chk.why);
    if (v->chk.lp == ZXAPP_TRUE && v->cid[0]) {
        char shortid[24];
        uint32_t k = 0;
        for (; k < 19u && v->cid[k]; k++) shortid[k] = v->cid[k];
        shortid[k] = 0;
        zv_cat(t + n, cap - n, shortid, "...");
    }
    zv_shell_status(&v->sh, t);
    zui_invalidate(ui, v->board);
}

static void say(cb_view_t *v, const char *a, const char *b)
{
    zv_shell_t *s = &v->sh;
    zv_cat(s->error_text, sizeof s->error_text, a, b);
    zui_set_text(s->ui, s->error, s->error_text);
    zui_set_hidden(s->ui, s->error, !s->error_text[0]);
}

/* Where a newly placed part goes: the next free cell of a loose grid. */
static void free_spot(const cb_view_t *v, int32_t *x, int32_t *y)
{
    const zxwire_circuit_t *c = &v->b->c;
    for (uint32_t k = 0; k < 64u; k++) {
        int32_t cx = 168 + 208 * (int32_t) (k % 2u), cy = 40 + 150 * (int32_t) (k / 2u);
        bool taken = false;
        for (uint32_t i = 0; i < c->nparts && !taken; i++)
            taken = iabs(c->part[i].x - cx) < 100 && iabs(c->part[i].y - cy) < 75;
        if (!taken) {
            *x = cx, *y = cy;
            return;
        }
    }
    *x = 168, *y = 40;
}

static void pick(cb_view_t *v, int node, uint32_t i)
{
    v->node = node;
    v->sel = i;
    v->port = node == ZXBOARD_NODE_OUT ? 0 : -1;
}

static void wire_to(cb_view_t *v, const zxwire_src_t *src)
{
    int rc = v->node == ZXBOARD_NODE_OUT ? zxboard_out(v->b, src)
                                         : zxboard_wire(v->b, v->sel, (uint32_t) v->port, src);
    say(v, rc == ZXAPP_OK ? "" : v->b->err, 0);
    v->cid[0] = 0;
}

bool cb_view_action(cb_view_t *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    zui_ui_t *ui = v->sh.ui;
    if (w == v->shelf && a == ZUI_ACT_OPEN) { /* CV1 */
        zxboard_item_t it;
        int32_t x, y;
        free_spot(v, &x, &y);
        int p =
            zxboard_shelf(v->b, (uint32_t) arg, &it) ? zxboard_part_add(v->b, it.ref, x, y) : -1;
        say(v, p < 0 ? v->b->err : "", 0);
        if (p >= 0) pick(v, ZXBOARD_NODE_PART, (uint32_t) p);
        v->cid[0] = 0;
        cb_view_sync(v);
        return true;
    }
    if (w == v->shelf && a == ZUI_ACT_SELECT) return true;
    if (w == v->ports && a == ZUI_ACT_SELECT) {
        v->port = arg;
        cb_view_sync(v);
        return true;
    }
    if (w == v->sources && (a == ZUI_ACT_SELECT || a == ZUI_ACT_OPEN) && v->port >= 0 && arg >= 0) {
        uint32_t ni = v->b->c.ninputs;
        zxwire_src_t s = (uint32_t) arg < ni ? zxboard_src_input((uint32_t) arg)
                                             : zxboard_src_part((uint32_t) arg - ni);
        wire_to(v, &s);
        cb_view_sync(v);
        return true;
    }
    if (w == v->sources) return true;
    if (w == v->setting && a == ZUI_ACT_SUBMIT && v->port >= 0) {
        zxwire_src_t s;
        if (!zxboard_src_setting(v->setting_buf, &s))
            say(v, "a setting is up to 8 whole numbers, like 25 or 1,2,3", 0);
        else
            wire_to(v, &s);
        cb_view_sync(v);
        return true;
    }
    if (w == v->setting) return a == ZUI_ACT_CHANGE || a == ZUI_ACT_SUBMIT;
    if (w == v->kind && a == ZUI_ACT_PRESS) {
        zxboard_answer_kind(v->b,
                            v->b->c.kind == ZXAPP_KIND_TRUTH ? ZXAPP_KIND_NUM : ZXAPP_KIND_TRUTH);
        v->cid[0] = 0;
        cb_view_sync(v);
        return true;
    }
    if (w == v->remove && a == ZUI_ACT_PRESS) {
        int rc = v->node == ZXBOARD_NODE_PART    ? zxboard_part_remove(v->b, v->sel)
                 : v->node == ZXBOARD_NODE_INPUT ? zxboard_input_remove(v->b, v->sel)
                                                 : ZXAPP_E_ARG;
        say(v, rc == ZXAPP_OK ? "" : "pick a part or an input to remove it", 0);
        if (rc == ZXAPP_OK) v->node = -1;
        v->cid[0] = 0;
        cb_view_sync(v);
        return true;
    }
    if (w == v->save && a == ZUI_ACT_PRESS) { /* CV4 */
        v->saved_len = zxboard_save(v->b, v->saved, sizeof v->saved);
        if (v->saved_len < 0) {
            v->cid[0] = 0;
            say(v, v->b->err, 0);
        } else {
            zxapp_cid(v->saved, (uint32_t) v->saved_len, v->cid);
            say(v, "", 0);
        }
        cb_view_sync(v);
        return true;
    }
    (void) ui;
    return false;
}

/* ---- the pointer (CV2) ---------------------------------------------------------- */

static bool near(int32_t x, int32_t y, int32_t px, int32_t py, int32_t r)
{
    return iabs(x - px) <= r && iabs(y - py) <= r;
}

static bool inside(zui_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/* An answer dot under the pointer: its node and index. */
static bool hit_answer(const cb_view_t *v, int32_t x, int32_t y, int *node, uint32_t *i)
{
    int32_t r = P(v, DOT + 5, zoom_q8(v)), ax, ay;
    for (uint32_t k = 0; k < v->b->c.nparts; k++) {
        cb_answer_at(v, ZXBOARD_NODE_PART, k, &ax, &ay);
        if (near(x, y, ax, ay, r)) return *node = ZXBOARD_NODE_PART, *i = k, true;
    }
    for (uint32_t k = 0; k < v->b->c.ninputs; k++) {
        cb_answer_at(v, ZXBOARD_NODE_INPUT, k, &ax, &ay);
        if (near(x, y, ax, ay, r)) return *node = ZXBOARD_NODE_INPUT, *i = k, true;
    }
    return false;
}

/* An input dot under the pointer: a part's port, or the answer's (port -1). */
static bool hit_port(const cb_view_t *v, int32_t x, int32_t y, uint32_t *part, int32_t *port)
{
    int32_t r = P(v, DOT + 5, zoom_q8(v)), px, py;
    for (uint32_t k = 0; k < v->b->c.nparts; k++)
        for (uint32_t j = 0; j < v->b->c.part[k].nports; j++) {
            cb_port_at(v, k, j, &px, &py);
            if (near(x, y, px, py, r)) return *part = k, *port = (int32_t) j, true;
        }
    out_in_at(v, &px, &py);
    if (near(x, y, px, py, r)) return *part = 0, *port = -1, true;
    return false;
}

static bool hit_node(const cb_view_t *v, int32_t x, int32_t y, int *node, uint32_t *i)
{
    for (uint32_t k = v->b->c.nparts; k-- > 0;) /* the top tile first */
        if (inside(cb_node_rect(v, ZXBOARD_NODE_PART, k), x, y))
            return *node = ZXBOARD_NODE_PART, *i = k, true;
    for (uint32_t k = 0; k < v->b->c.ninputs; k++)
        if (inside(cb_node_rect(v, ZXBOARD_NODE_INPUT, k), x, y))
            return *node = ZXBOARD_NODE_INPUT, *i = k, true;
    if (inside(cb_node_rect(v, ZXBOARD_NODE_OUT, 0), x, y))
        return *node = ZXBOARD_NODE_OUT, *i = 0, true;
    return false;
}

bool cb_view_pointer(cb_view_t *v, const zui_event_t *e)
{
    zui_rect_t r = board_rect(v);
    int32_t x = e->x, y = e->y, z = zoom_q8(v);
    if (e->type == ZUI_EV_PTR_DOWN || e->type == ZUI_EV_TOUCH_BEGIN) {
        if (!inside(r, x, y)) return false;
        int node;
        uint32_t i, part;
        int32_t port;
        v->ptr_x = x, v->ptr_y = y;
        if (hit_answer(v, x, y, &node, &i)) {
            v->drag = 2, v->drag_node = node, v->drag_i = i;
        } else if (hit_port(v, x, y, &part, &port)) {
            if (port < 0)
                pick(v, ZXBOARD_NODE_OUT, 0);
            else {
                pick(v, ZXBOARD_NODE_PART, part);
                v->port = port;
            }
        } else if (hit_node(v, x, y, &node, &i)) {
            pick(v, node, i);
            zui_rect_t nr = cb_node_rect(v, node, i);
            v->drag = 1, v->drag_node = node, v->drag_i = i;
            v->grab_x = x - nr.x, v->grab_y = y - nr.y;
        } else
            v->node = -1;
        cb_view_sync(v);
        return true;
    }
    if (!v->drag) return false;
    if (e->type == ZUI_EV_PTR_MOVE || e->type == ZUI_EV_TOUCH_MOVE) {
        v->ptr_x = x, v->ptr_y = y;
        if (v->drag == 1) { /* points from pixels, kept on the board */
            int32_t px = x - v->grab_x - r.x, py = y - v->grab_y - r.y;
            int32_t unit = P(v, 256, z);
            int32_t bx = unit > 0 ? (int32_t) (((int64_t) px * 256) / unit) : 0;
            int32_t by = unit > 0 ? (int32_t) (((int64_t) py * 256) / unit) : 0;
            zxboard_move(v->b, v->drag_node, v->drag_i, bx < 0 ? 0 : bx, by < 0 ? 0 : by);
        }
        zui_invalidate(v->sh.ui, v->board);
        return true;
    }
    if (e->type == ZUI_EV_PTR_UP || e->type == ZUI_EV_TOUCH_END || e->type == ZUI_EV_TOUCH_CANCEL) {
        if (v->drag == 2 && e->type != ZUI_EV_TOUCH_CANCEL) {
            uint32_t part;
            int32_t port;
            if (hit_port(v, x, y, &part, &port)) {
                zxwire_src_t s = v->drag_node == ZXBOARD_NODE_PART ? zxboard_src_part(v->drag_i)
                                                                   : zxboard_src_input(v->drag_i);
                if (port < 0)
                    pick(v, ZXBOARD_NODE_OUT, 0);
                else {
                    pick(v, ZXBOARD_NODE_PART, part);
                    v->port = port;
                }
                wire_to(v, &s);
            }
        }
        if (v->drag == 1) v->cid[0] = 0;
        v->drag = 0;
        cb_view_sync(v);
        return true;
    }
    return false;
}
