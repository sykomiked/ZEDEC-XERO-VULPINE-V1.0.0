/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_switch.c — switch access: group and item scanning, the point scan,
 * the action menu and the highlight (zui_switch.h). */
#include "zui_switch.h"
#include "zui_tokens.h"

/* ---- small helpers ------------------------------------------------------ */

static void mset(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static int32_t imin(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

static int32_t imax(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

static uint32_t clampu(uint32_t v, uint32_t lo, uint32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static zui_rect_t grow(zui_rect_t r, int32_t d)
{
    return (zui_rect_t){r.x - d, r.y - d, r.w + 2 * d, r.h + 2 * d};
}

static zui_rect_t join(zui_rect_t a, zui_rect_t b)
{
    int32_t x0 = imin(a.x, b.x), y0 = imin(a.y, b.y);
    int32_t x1 = imax(a.x + a.w, b.x + b.w), y1 = imax(a.y + a.h, b.y + b.h);
    return (zui_rect_t){x0, y0, x1 - x0, y1 - y0};
}

static int32_t cx(zui_rect_t r)
{
    return r.x + r.w / 2;
}

static int32_t cy(zui_rect_t r)
{
    return r.y + r.h / 2;
}

static const char *const ENGLISH[ZUI_SW_ACT_COUNT] = {"Activate",    "Long press", "Scroll up",
                                                      "Scroll down", "Back",       "Cancel"};

#define MENU_ROW 32
#define MENU_W   176

/* ---- configuration -------------------------------------------------------- */

void zui_sw_config_default(zui_sw_config_t *c)
{
    if (!c) return;
    mset(c, sizeof *c);
    c->mode = ZUI_SW_AUTO;
    c->grouping = ZUI_SW_ROWS;
    c->interval_ms = 1000;
    c->first_ms = 500;
    c->dwell_ms = 0;
    c->debounce_ms = 100;
    c->loops = 3;
    c->menu = true;
    c->point = true;
    c->menu_mask = (uint8_t) ((1u << ZUI_SW_ACT_COUNT) - 1u);
    c->point_ms = 50;
    c->point_step = 0;
    c->key_select = ZUI_KEY_SPACE;
    c->key_next = ZUI_KEY_ENTER;
}

void zui_switch_config(zui_switch_t *sw, const zui_sw_config_t *cfg)
{
    zui_sw_config_t c;
    if (cfg)
        c = *cfg;
    else
        zui_sw_config_default(&c);
    c.mode = c.mode == ZUI_SW_STEP ? ZUI_SW_STEP : ZUI_SW_AUTO;
    if (c.grouping != ZUI_SW_COLUMNS && c.grouping != ZUI_SW_LINEAR) c.grouping = ZUI_SW_ROWS;
    c.interval_ms = clampu(c.interval_ms, 100, 20000);
    c.first_ms = clampu(c.first_ms, 0, 20000);
    c.dwell_ms = clampu(c.dwell_ms, 0, 10000);
    c.debounce_ms = clampu(c.debounce_ms, 0, 10000);
    c.loops = (uint8_t) clampu(c.loops, 1, 20);
    c.point_ms = clampu(c.point_ms, 10, 20000);
    if (c.point_step < 0) c.point_step = 0;
    sw->cfg = c;
    sw->n_menu = 0;
    for (uint32_t v = 0; v < ZUI_SW_ACT_COUNT; v++)
        if (c.menu_mask & (1u << v)) sw->menu_verbs[sw->n_menu++] = (uint8_t) v;
}

void zui_switch_init(zui_switch_t *sw, const zui_sw_config_t *cfg)
{
    mset(sw, sizeof *sw);
    zui_switch_config(sw, cfg);
    sw->state = ZUI_SW_OFF;
}

void zui_switch_attach(zui_switch_t *sw, zui_ui_t *ui, int32_t x, int32_t y, zui_wm_t *wm)
{
    sw->ui = ui;
    sw->ui_x = x;
    sw->ui_y = y;
    sw->wm = wm;
    if (wm)
        sw->area = (zui_rect_t){0, 0, wm->w, wm->h};
    else if (ui)
        sw->area = (zui_rect_t){x, y, (int32_t) ui->screen.px_w, (int32_t) ui->screen.px_h};
}

void zui_switch_set_area(zui_switch_t *sw, zui_rect_t area)
{
    sw->area = area;
}

void zui_switch_on_act(zui_switch_t *sw, zui_sw_act_fn fn, void *ctx)
{
    sw->on_act = fn;
    sw->act_ctx = ctx;
}

void zui_switch_set_words(zui_switch_t *sw, const char *const *words)
{
    sw->words = words;
}

const char *zui_switch_word(const zui_switch_t *sw, zui_sw_verb_t v)
{
    if ((uint32_t) v >= ZUI_SW_ACT_COUNT) return "";
    if (sw && sw->words && sw->words[v]) return sw->words[v];
    return ENGLISH[v];
}

/* ---- SW1: targets ------------------------------------------------------------ */

static bool shown_w(const zui_ui_t *ui, zui_id_t id)
{
    for (zui_id_t p = id; p != ZUI_NONE; p = ui->w[p].parent)
        if (ui->w[p].hidden) return false;
    return true;
}

static bool usable(const zui_ui_t *ui, zui_id_t id)
{
    const zui_widget_t *w = &ui->w[id];
    if (!w->focusable || w->disabled || !shown_w(ui, id)) return false;
    if (w->kind == ZUI_W_FIELD && w->u.field.readonly && w->parent != ZUI_NONE &&
        ui->w[w->parent].kind == ZUI_W_HELD)
        return false;
    if (ui->modal != ZUI_NONE) { /* zui_widget.h W9: only inside the open dialog */
        zui_id_t p = id;
        while (p != ZUI_NONE && p != ui->modal) p = ui->w[p].parent;
        if (p == ZUI_NONE) return false;
    }
    if (w->r.w <= 0 || w->r.h <= 0) return false;
    /* on screen */
    return w->r.x < (int32_t) ui->screen.px_w && w->r.y < (int32_t) ui->screen.px_h &&
           w->r.x + w->r.w > 0 && w->r.y + w->r.h > 0;
}

static zui_id_t preorder_next(const zui_ui_t *ui, zui_id_t id)
{
    if (ui->w[id].first != ZUI_NONE) return ui->w[id].first;
    while (id != ZUI_NONE) {
        if (ui->w[id].next != ZUI_NONE) return ui->w[id].next;
        id = ui->w[id].parent;
    }
    return ZUI_NONE;
}

static void add(zui_switch_t *sw, uint8_t kind, int32_t id, zui_rect_t r)
{
    if (sw->nt >= ZUI_SW_TARGETS) return;
    sw->t[sw->nt].kind = kind;
    sw->t[sw->nt].id = id;
    sw->t[sw->nt].r = r;
    sw->nt++;
}

static void add_group(zui_switch_t *sw, uint32_t first, uint32_t n, bool point)
{
    if (sw->ng >= ZUI_SW_GROUPS || (!n && !point)) return;
    zui_sw_group_t *g = &sw->g[sw->ng++];
    g->first = (uint16_t) first;
    g->n = (uint16_t) n;
    g->point = point;
    g->r = point ? sw->area : sw->t[first].r;
    for (uint32_t i = 1; i < n; i++) g->r = join(g->r, sw->t[first + i].r);
}

/* Stable insertion sort of t[a, b) by the centre along one axis. */
static void sort_axis(zui_switch_t *sw, uint32_t a, uint32_t b, bool by_y)
{
    for (uint32_t i = a + 1u; i < b; i++) {
        zui_sw_target_t k = sw->t[i];
        int32_t kv = by_y ? cy(k.r) : cx(k.r);
        uint32_t j = i;
        while (j > a && (by_y ? cy(sw->t[j - 1u].r) : cx(sw->t[j - 1u].r)) > kv) {
            sw->t[j] = sw->t[j - 1u];
            j--;
        }
        sw->t[j] = k;
    }
}

/* SW2: rows (or columns) of t[a, b). keep groups free for the wm and the point scan. */
static void group_range(zui_switch_t *sw, uint32_t a, uint32_t b, uint32_t keep)
{
    if (a >= b) return;
    if (sw->cfg.grouping == ZUI_SW_LINEAR) {
        add_group(sw, a, b - a, false);
        return;
    }
    bool rows = sw->cfg.grouping == ZUI_SW_ROWS;
    sort_axis(sw, a, b, rows);
    uint32_t s = a;
    while (s < b) {
        zui_rect_t f = sw->t[s].r;
        int32_t end = rows ? f.y + f.h : f.x + f.w;
        uint32_t e = s + 1u;
        bool last = sw->ng + 1u + keep >= ZUI_SW_GROUPS;
        while (e < b && (last || (rows ? cy(sw->t[e].r) : cx(sw->t[e].r)) < end)) e++;
        sort_axis(sw, s, e, !rows);
        add_group(sw, s, e - s, false);
        s = e;
    }
}

static void layout_ui(zui_ui_t *ui)
{
    zui_event_t e;
    mset(&e, sizeof e);
    e.type = ZUI_EV_NONE;
    (void) zui_ui_event(ui, &e); /* lays out if needed; does nothing else */
}

/* SW1, SW2: the dock's seal, then (open) each ring a group of its slots. */
static void collect_dock(zui_switch_t *sw)
{
    const zui_dock_t *d = &sw->wm->dock;
    uint32_t a = sw->nt;
    add(sw, ZUI_SW_T_DOCK, -1, zui_dock_keepout(d));
    add_group(sw, a, sw->nt - a, false);
    if (!zui_dock_is_open(d)) return;
    zui_dock_slot_t s[ZUI_DOCK_RINGS * ZUI_DOCK_SLOTS];
    uint32_t n = zui_dock_slots(d, s, ZUI_DOCK_RINGS * ZUI_DOCK_SLOTS);
    int32_t h = d->icon / 2 + ZUI_DOCK_PAD;
    for (uint32_t k = 0; k < ZUI_DOCK_RINGS; k++) {
        a = sw->nt;
        for (uint32_t i = 0; i < n; i++)
            if (s[i].ring == k)
                add(sw, ZUI_SW_T_DOCK, s[i].item,
                    (zui_rect_t){s[i].cx - h, s[i].cy - h, 2 * h, 2 * h});
        add_group(sw, a, sw->nt - a, false);
    }
}

static void collect(zui_switch_t *sw)
{
    sw->nt = 0;
    sw->ng = 0;
    uint32_t keep = (sw->wm ? 2u + ZUI_DOCK_RINGS : 0u) + (sw->cfg.point ? 1u : 0u);
    zui_ui_t *ui = sw->ui;
    if (ui && ui->root != ZUI_NONE) {
        layout_ui(ui);
        if (ui->menu.open) { /* SW1: a menu is modal: its items only */
            int32_t rh = zui_px(&ui->screen, ui->screen.input == ZUI_INPUT_POINTER ? 26 : 44);
            int32_t top = ui->menu.r.y + zui_px(&ui->screen, ZUI_S1);
            for (uint32_t i = 0; i < ui->menu.n; i++) {
                if (ui->menu.off[i] || (ui->menu.item[i][0] == '-' && !ui->menu.item[i][1]))
                    continue;
                zui_rect_t r = {ui->menu.r.x + sw->ui_x, top + (int32_t) i * rh + sw->ui_y,
                                ui->menu.r.w, rh};
                add(sw, ZUI_SW_T_MENU_ITEM, (int32_t) i, r);
            }
            add_group(sw, 0, sw->nt, false);
        } else if (!ui->palette.open) {
            for (zui_id_t id = ui->root; id != ZUI_NONE; id = preorder_next(ui, id)) {
                if (!usable(ui, id)) continue;
                zui_rect_t r = ui->w[id].r;
                r.x += sw->ui_x;
                r.y += sw->ui_y;
                add(sw, ZUI_SW_T_WIDGET, id, r);
            }
            group_range(sw, 0, sw->nt, keep);
        }
    }
    if (sw->wm) {
        uint32_t ids[ZUI_WM_MAX];
        uint32_t n = zui_wm_strip(sw->wm, ids, ZUI_WM_MAX), a = sw->nt;
        for (uint32_t i = 0; i < n; i++) {
            const zui_wm_window_t *w = zui_wm_get(sw->wm, ids[i]);
            if (w && !w->minimised) add(sw, ZUI_SW_T_WINDOW, (int32_t) ids[i], w->r);
        }
        add_group(sw, a, sw->nt - a, false);
        a = sw->nt;
        if (sw->wm->use_dock) {
            collect_dock(sw);
        } else {
            for (uint32_t i = 0; i < n; i++)
                add(sw, ZUI_SW_T_STRIP, (int32_t) ids[i], zui_wm_strip_slot(sw->wm, i, n));
            add_group(sw, a, sw->nt - a, false);
        }
    }
    if (sw->cfg.point && sw->area.w > 0 && sw->area.h > 0) add_group(sw, 0, 0, true);
}

/* ---- scanning --------------------------------------------------------------------- */

static bool scanning(const zui_switch_t *sw)
{
    return sw->state >= ZUI_SW_GROUP;
}

static void moved(zui_switch_t *sw)
{
    sw->moves++;
    if (sw->on_move) sw->on_move(sw, sw->move_ctx);
}

static void enter(zui_switch_t *sw, zui_sw_state_t st)
{
    sw->state = st;
    sw->passes = 0;
    sw->elapsed = 0;
    moved(sw);
}

static void enter_point(zui_switch_t *sw)
{
    sw->px = sw->area.x;
    sw->py = sw->area.y;
    enter(sw, ZUI_SW_POINT_X);
}

static void enter_items(zui_switch_t *sw)
{
    sw->item = 0;
    enter(sw, ZUI_SW_ITEM);
}

void zui_switch_start(zui_switch_t *sw)
{
    collect(sw);
    sw->group = 0;
    sw->item = 0;
    sw->hot = 0;
    if (sw->ng == 0) {
        enter(sw, ZUI_SW_IDLE);
        return;
    }
    if (sw->ng == 1) {
        if (sw->g[0].point)
            enter_point(sw);
        else
            enter_items(sw);
        return;
    }
    if (sw->wm && sw->wm->use_dock && zui_dock_is_open(&sw->wm->dock))
        for (uint32_t i = 0; i < sw->ng; i++) /* SW2: the dock was opened: its rings */
            if (sw->g[i].n && sw->t[sw->g[i].first].kind == ZUI_SW_T_DOCK &&
                sw->t[sw->g[i].first].id >= 0) {
                sw->group = i;
                break;
            }
    enter(sw, ZUI_SW_GROUP);
}

void zui_switch_stop(zui_switch_t *sw)
{
    sw->state = ZUI_SW_OFF;
    for (uint32_t i = 0; i < ZUI_SW_SWITCHES; i++) sw->held[i] = sw->fired[i] = false;
}

static int32_t step_x(const zui_switch_t *sw)
{
    return sw->cfg.point_step ? sw->cfg.point_step : imax(1, sw->area.w / 64);
}

static int32_t step_y(const zui_switch_t *sw)
{
    return sw->cfg.point_step ? sw->cfg.point_step : imax(1, sw->area.h / 64);
}

/* A full pass with no choice (SW3). */
static void lap(zui_switch_t *sw)
{
    sw->passes++;
    if (sw->cfg.mode != ZUI_SW_AUTO || sw->passes < sw->cfg.loops) return;
    switch (sw->state) {
    case ZUI_SW_ITEM:
        if (sw->ng > 1)
            enter(sw, ZUI_SW_GROUP);
        else
            enter(sw, ZUI_SW_IDLE);
        break;
    case ZUI_SW_GROUP:
        enter(sw, ZUI_SW_IDLE);
        break;
    default: /* the menu and the point scan give up: back to the start */
        zui_switch_start(sw);
        if (sw->state == ZUI_SW_POINT_X) enter(sw, ZUI_SW_IDLE); /* nothing else to scan */
        break;
    }
}

static void advance(zui_switch_t *sw)
{
    switch (sw->state) {
    case ZUI_SW_GROUP:
        sw->group = (sw->group + 1u) % sw->ng;
        moved(sw);
        if (sw->group == 0) lap(sw);
        break;
    case ZUI_SW_ITEM: {
        uint32_t stops = sw->g[sw->group].n + (sw->ng > 1 ? 1u : 0u);
        sw->item = (sw->item + 1u) % stops;
        moved(sw);
        if (sw->item == 0) lap(sw);
        break;
    }
    case ZUI_SW_MENU:
        sw->hot = (sw->hot + 1u) % sw->n_menu;
        moved(sw);
        if (sw->hot == 0) lap(sw);
        break;
    case ZUI_SW_POINT_X:
        sw->px += step_x(sw);
        if (sw->px >= sw->area.x + sw->area.w) sw->px = sw->area.x;
        moved(sw);
        if (sw->px == sw->area.x) lap(sw);
        break;
    case ZUI_SW_POINT_Y:
        sw->py += step_y(sw);
        if (sw->py >= sw->area.y + sw->area.h) sw->py = sw->area.y;
        moved(sw);
        if (sw->py == sw->area.y) lap(sw);
        break;
    default:
        break;
    }
}

static void act(zui_switch_t *sw, zui_sw_verb_t v, const zui_sw_target_t *t)
{
    zui_sw_action_t a;
    a.verb = (uint8_t) v;
    a.target = *t;
    a.x = t->kind == ZUI_SW_T_POINT ? t->r.x : cx(t->r);
    a.y = t->kind == ZUI_SW_T_POINT ? t->r.y : cy(t->r);
    sw->actions++;
    if (sw->on_act)
        sw->on_act(sw, &a, sw->act_ctx);
    else
        (void) zui_switch_apply(sw, &a);
}

static void choose(zui_switch_t *sw, const zui_sw_target_t *t)
{
    sw->chosen = *t;
    if (sw->cfg.menu && sw->n_menu) {
        sw->hot = 0;
        enter(sw, ZUI_SW_MENU);
        return;
    }
    act(sw, ZUI_SW_ACT_ACTIVATE, t);
    zui_switch_start(sw);
}

static void select_now(zui_switch_t *sw)
{
    switch (sw->state) {
    case ZUI_SW_IDLE:
        zui_switch_start(sw);
        break;
    case ZUI_SW_GROUP: {
        const zui_sw_group_t *g = &sw->g[sw->group];
        if (g->point)
            enter_point(sw);
        else if (g->n == 1)
            choose(sw, &sw->t[g->first]);
        else
            enter_items(sw);
        break;
    }
    case ZUI_SW_ITEM: {
        const zui_sw_group_t *g = &sw->g[sw->group];
        if (sw->item >= g->n) /* the group again: back to the groups */
            enter(sw, ZUI_SW_GROUP);
        else
            choose(sw, &sw->t[g->first + sw->item]);
        break;
    }
    case ZUI_SW_MENU: {
        zui_sw_verb_t v = (zui_sw_verb_t) sw->menu_verbs[sw->hot];
        zui_sw_target_t t = sw->chosen;
        if (v != ZUI_SW_ACT_CANCEL) act(sw, v, &t);
        zui_switch_start(sw);
        break;
    }
    case ZUI_SW_POINT_X:
        sw->py = sw->area.y;
        enter(sw, ZUI_SW_POINT_Y);
        break;
    case ZUI_SW_POINT_Y: {
        zui_sw_target_t t;
        t.kind = ZUI_SW_T_POINT;
        t.id = -1;
        t.r = (zui_rect_t){sw->px, sw->py, 1, 1};
        choose(sw, &t);
        break;
    }
    default:
        break;
    }
}

static void fire(zui_switch_t *sw, zui_sw_switch_t which)
{
    sw->fired[which] = true;
    sw->last_fire_ms = sw->now_ms;
    sw->fired_once = true;
    sw->elapsed = 0;
    if (which == ZUI_SW_SELECT)
        select_now(sw);
    else if (scanning(sw))
        advance(sw);
}

void zui_switch_press(zui_switch_t *sw, zui_sw_switch_t which, bool down)
{
    if ((uint32_t) which >= ZUI_SW_SWITCHES || sw->state == ZUI_SW_OFF) return;
    if (!down) {
        sw->held[which] = false;
        sw->fired[which] = false;
        sw->hold_ms[which] = 0;
        return;
    }
    if (sw->held[which]) return;
    sw->held[which] = true;
    sw->hold_ms[which] = 0;
    sw->fired[which] = false;
    if (sw->fired_once && sw->now_ms - sw->last_fire_ms < sw->cfg.debounce_ms) {
        sw->fired[which] = true; /* SW5: a bounce: used up, does nothing */
        return;
    }
    if (sw->cfg.dwell_ms == 0) fire(sw, which);
}

bool zui_switch_event(zui_switch_t *sw, const zui_event_t *e)
{
    if (sw->state == ZUI_SW_OFF || !e) return false;
    if (e->type != ZUI_EV_KEY_DOWN && e->type != ZUI_EV_KEY_UP) return false;
    zui_sw_switch_t which;
    if (sw->cfg.key_select && e->key == sw->cfg.key_select)
        which = ZUI_SW_SELECT;
    else if (sw->cfg.key_next && e->key == sw->cfg.key_next)
        which = ZUI_SW_NEXT;
    else
        return false;
    if (e->type == ZUI_EV_KEY_DOWN && e->repeat) return true;
    zui_switch_press(sw, which, e->type == ZUI_EV_KEY_DOWN);
    return true;
}

static bool at_first(const zui_switch_t *sw)
{
    switch (sw->state) {
    case ZUI_SW_GROUP:
        return sw->group == 0;
    case ZUI_SW_ITEM:
        return sw->item == 0;
    case ZUI_SW_MENU:
        return sw->hot == 0;
    case ZUI_SW_POINT_X:
        return sw->px == sw->area.x;
    case ZUI_SW_POINT_Y:
        return sw->py == sw->area.y;
    default:
        return false;
    }
}

void zui_switch_tick(zui_switch_t *sw, uint32_t ms)
{
    if (sw->state == ZUI_SW_OFF) return;
    sw->now_ms += ms;
    bool any = false;
    for (uint32_t i = 0; i < ZUI_SW_SWITCHES; i++) {
        if (!sw->held[i]) continue;
        any = true;
        if (sw->fired[i]) continue;
        sw->hold_ms[i] += ms;
        if (sw->hold_ms[i] >= sw->cfg.dwell_ms) fire(sw, (zui_sw_switch_t) i);
    }
    if (any || sw->cfg.mode != ZUI_SW_AUTO || !scanning(sw)) return; /* SW5: held: wait */
    sw->elapsed += ms;
    for (uint32_t guard = 0; guard < 100000u && scanning(sw); guard++) {
        bool point = sw->state == ZUI_SW_POINT_X || sw->state == ZUI_SW_POINT_Y;
        uint32_t th = point ? sw->cfg.point_ms : sw->cfg.interval_ms;
        if (at_first(sw)) th += sw->cfg.first_ms;
        if (sw->elapsed < th) break;
        sw->elapsed -= th;
        advance(sw);
    }
}

/* ---- where the highlight is ------------------------------------------------------ */

bool zui_switch_highlight(const zui_switch_t *sw, zui_rect_t *r)
{
    zui_rect_t o = {0, 0, 0, 0};
    bool ok = true;
    switch (sw->state) {
    case ZUI_SW_GROUP:
        o = sw->g[sw->group].r;
        break;
    case ZUI_SW_ITEM: {
        const zui_sw_group_t *g = &sw->g[sw->group];
        o = sw->item < g->n ? sw->t[g->first + sw->item].r : g->r;
        break;
    }
    case ZUI_SW_MENU: {
        zui_rect_t m = zui_switch_menu_rect(sw);
        o = (zui_rect_t){m.x, m.y + (int32_t) sw->hot * MENU_ROW, m.w, MENU_ROW};
        break;
    }
    case ZUI_SW_POINT_X:
        o = (zui_rect_t){sw->px - 1, sw->area.y, 2, sw->area.h};
        break;
    case ZUI_SW_POINT_Y:
        o = (zui_rect_t){sw->area.x, sw->py - 1, sw->area.w, 2};
        break;
    default:
        ok = false;
        break;
    }
    if (r) *r = o;
    return ok;
}

zui_rect_t zui_switch_menu_rect(const zui_switch_t *sw)
{
    zui_rect_t a = sw->area, t = sw->chosen.r;
    int32_t h = (int32_t) sw->n_menu * MENU_ROW, w = MENU_W;
    int32_t y = t.y + t.h + ZUI_S1;
    if (y + h > a.y + a.h) y = t.y - ZUI_S1 - h; /* no room below: above */
    if (y < a.y) y = imax(a.y, a.y + a.h - h);   /* nor above: at the bottom */
    int32_t x = imax(a.x, imin(t.x, a.x + a.w - w));
    return (zui_rect_t){x, y, w, h};
}

zui_sw_verb_t zui_switch_menu_verb(const zui_switch_t *sw)
{
    if (sw->state != ZUI_SW_MENU || !sw->n_menu) return ZUI_SW_ACT_COUNT;
    return (zui_sw_verb_t) sw->menu_verbs[sw->hot];
}

/* ---- SW8: performing ---------------------------------------------------------------- */

static bool ui_key(zui_switch_t *sw, uint16_t key)
{
    if (!sw->ui) return false;
    zui_event_t e;
    mset(&e, sizeof e);
    e.type = ZUI_EV_KEY_DOWN;
    e.key = key;
    e.ms = sw->now_ms;
    bool used = zui_ui_event(sw->ui, &e);
    e.type = ZUI_EV_KEY_UP;
    (void) zui_ui_event(sw->ui, &e);
    return used;
}

static bool wm_key(zui_switch_t *sw, uint16_t key)
{
    zui_event_t e;
    mset(&e, sizeof e);
    e.type = ZUI_EV_KEY_DOWN;
    e.key = key;
    e.ms = sw->now_ms;
    bool used = zui_wm_event(sw->wm, &e);
    e.type = ZUI_EV_KEY_UP;
    (void) zui_wm_event(sw->wm, &e);
    return used;
}

static bool send(zui_switch_t *sw, zui_event_t *e)
{
    e->ms = sw->now_ms;
    if (sw->wm) return zui_wm_event(sw->wm, e);
    if (!sw->ui) return false;
    e->x -= sw->ui_x;
    e->y -= sw->ui_y;
    return zui_ui_event(sw->ui, e);
}

static bool click(zui_switch_t *sw, int32_t x, int32_t y, uint8_t button)
{
    zui_event_t e;
    mset(&e, sizeof e);
    e.type = ZUI_EV_PTR_DOWN;
    e.button = button;
    e.x = x;
    e.y = y;
    bool used = send(sw, &e);
    mset(&e, sizeof e);
    e.type = ZUI_EV_PTR_UP;
    e.button = button;
    e.x = x;
    e.y = y;
    return send(sw, &e) || used;
}

static bool scroll(zui_switch_t *sw, int32_t x, int32_t y, int32_t dy)
{
    zui_event_t e;
    mset(&e, sizeof e);
    e.type = ZUI_EV_SCROLL;
    e.x = x;
    e.y = y;
    e.dy = dy;
    return send(sw, &e);
}

static int32_t page(zui_rect_t r)
{
    return imax(40, r.h * 3 / 4);
}

/* SW8: a dock left open would eat a press meant for what is under it. */
static void dock_shut(zui_switch_t *sw, const zui_sw_action_t *a)
{
    if (!sw->wm || !sw->wm->use_dock || !zui_dock_is_open(&sw->wm->dock)) return;
    if (a->target.kind == ZUI_SW_T_POINT &&
        zui_dock_hit(&sw->wm->dock, a->x, a->y, 0) != ZUI_DOCK_HIT_NONE)
        return;
    zui_dock_bloom(&sw->wm->dock, false, false);
}

/* SW8: turn the ring a dock item is on. */
static bool dock_turn(zui_switch_t *sw, int32_t item, int32_t by)
{
    zui_dock_slot_t s[ZUI_DOCK_RINGS * ZUI_DOCK_SLOTS];
    uint32_t n = zui_dock_slots(&sw->wm->dock, s, ZUI_DOCK_RINGS * ZUI_DOCK_SLOTS);
    for (uint32_t i = 0; i < n; i++)
        if (s[i].item == item) return zui_dock_turn(&sw->wm->dock, s[i].ring, by);
    return false;
}

bool zui_switch_apply(zui_switch_t *sw, const zui_sw_action_t *a)
{
    if (!a || a->verb >= ZUI_SW_ACT_CANCEL) return false;
    zui_sw_verb_t v = (zui_sw_verb_t) a->verb;
    const zui_sw_target_t *t = &a->target;
    switch (t->kind) {
    case ZUI_SW_T_WIDGET: {
        zui_ui_t *ui = sw->ui;
        zui_widget_t *w = ui ? zui_w(ui, t->id) : 0;
        if (!w) return false;
        if (v == ZUI_SW_ACT_BACK) return ui_key(sw, ZUI_KEY_BACK);
        if (v == ZUI_SW_ACT_SCROLL_UP || v == ZUI_SW_ACT_SCROLL_DOWN) {
            zui_event_t e;
            mset(&e, sizeof e);
            e.type = ZUI_EV_SCROLL;
            e.x = a->x - sw->ui_x;
            e.y = a->y - sw->ui_y;
            e.dy = v == ZUI_SW_ACT_SCROLL_DOWN ? page(t->r) : -page(t->r);
            e.ms = sw->now_ms;
            return zui_ui_event(ui, &e);
        }
        zui_focus(ui, t->id, true);
        if (v == ZUI_SW_ACT_LONG_PRESS) return ui_key(sw, ZUI_KEY_MENU);
        if (w->kind == ZUI_W_FIELD) return true; /* a field is used by typing into it */
        return ui_key(sw, ZUI_KEY_ENTER);
    }
    case ZUI_SW_T_MENU_ITEM:
        if (!sw->ui || !sw->ui->menu.open) return false;
        if (v == ZUI_SW_ACT_ACTIVATE) {
            if (t->id < 0 || (uint32_t) t->id >= sw->ui->menu.n || sw->ui->menu.off[t->id])
                return false;
            sw->ui->menu.hot = t->id;
            return ui_key(sw, ZUI_KEY_ENTER);
        }
        if (v == ZUI_SW_ACT_BACK || v == ZUI_SW_ACT_LONG_PRESS) return ui_key(sw, ZUI_KEY_BACK);
        return false;
    case ZUI_SW_T_WINDOW:
        if (!sw->wm || !zui_wm_get(sw->wm, (uint32_t) t->id)) return false;
        if (v == ZUI_SW_ACT_SCROLL_UP || v == ZUI_SW_ACT_SCROLL_DOWN)
            return scroll(sw, a->x, a->y, v == ZUI_SW_ACT_SCROLL_DOWN ? page(t->r) : -page(t->r));
        dock_shut(sw, a);
        if (!zui_wm_focus(sw->wm, (uint32_t) t->id)) return false;
        if (v == ZUI_SW_ACT_BACK) return wm_key(sw, ZUI_KEY_BACK);
        return true;
    case ZUI_SW_T_STRIP:
        if (!sw->wm) return false;
        if (v == ZUI_SW_ACT_ACTIVATE) return click(sw, a->x, a->y, ZUI_BTN_LEFT);
        if (v == ZUI_SW_ACT_BACK) return wm_key(sw, ZUI_KEY_BACK);
        return false;
    case ZUI_SW_T_DOCK: {
        zui_wm_t *wm = sw->wm;
        if (!wm || !wm->use_dock) return false;
        if (v == ZUI_SW_ACT_BACK) return wm_key(sw, ZUI_KEY_BACK);
        if (t->id < 0) {
            if (v != ZUI_SW_ACT_ACTIVATE) return false;
            zui_dock_bloom(&wm->dock, !zui_dock_is_open(&wm->dock), true);
            return true;
        }
        if (v == ZUI_SW_ACT_SCROLL_UP || v == ZUI_SW_ACT_SCROLL_DOWN)
            return dock_turn(sw, t->id, v == ZUI_SW_ACT_SCROLL_DOWN ? 1 : -1);
        if (v != ZUI_SW_ACT_ACTIVATE) return false;
        return zui_wm_dock_pick(wm, (uint32_t) t->id);
    }
    case ZUI_SW_T_POINT:
        dock_shut(sw, a);
        switch (v) {
        case ZUI_SW_ACT_ACTIVATE:
            return click(sw, a->x, a->y, ZUI_BTN_LEFT);
        case ZUI_SW_ACT_LONG_PRESS:
            return click(sw, a->x, a->y, ZUI_BTN_RIGHT);
        case ZUI_SW_ACT_SCROLL_UP:
        case ZUI_SW_ACT_SCROLL_DOWN:
            return scroll(sw, a->x, a->y, v == ZUI_SW_ACT_SCROLL_DOWN ? 120 : -120);
        default:
            return sw->wm ? wm_key(sw, ZUI_KEY_BACK) : ui_key(sw, ZUI_KEY_BACK);
        }
    default:
        return false;
    }
}

/* ---- SW9: drawing ----------------------------------------------------------------------- */

/* The one active element: cut into the ground, edged in gold. */
static void highlight(zui_surface_t *s, zui_rect_t r)
{
    zui_engrave(s, grow(r, 4), ZUI_R_CONTROL, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 140u);
    zui_stroke_rrect(s, grow(r, 2), ZUI_R_CONTROL, 2, ZUI_GOLD, 255u);
}

static void faint(zui_surface_t *s, zui_rect_t r)
{
    zui_engrave(s, grow(r, 4), ZUI_R_CONTROL, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 60u);
}

static void sweep_v(zui_surface_t *s, int32_t x, zui_rect_t a, uint32_t rgb)
{
    zui_vline(s, x - 2, a.y, a.h, 1, ZUI_ENGRAVE_LO);
    zui_vline(s, x - 1, a.y, a.h, 2, rgb);
    zui_vline(s, x + 1, a.y, a.h, 1, ZUI_ENGRAVE_HI);
}

static void sweep_h(zui_surface_t *s, int32_t y, zui_rect_t a, uint32_t rgb)
{
    zui_hline(s, a.x, y - 2, a.w, 1, ZUI_ENGRAVE_LO);
    zui_hline(s, a.x, y - 1, a.w, 2, rgb);
    zui_hline(s, a.x, y + 1, a.w, 1, ZUI_ENGRAVE_HI);
}

void zui_switch_draw(const zui_switch_t *sw, zui_surface_t *s, const zui_text_t *text)
{
    switch (sw->state) {
    case ZUI_SW_GROUP: {
        const zui_sw_group_t *g = &sw->g[sw->group];
        highlight(s, g->point ? grow(g->r, -6) : g->r);
        break;
    }
    case ZUI_SW_ITEM: {
        const zui_sw_group_t *g = &sw->g[sw->group];
        if (sw->item < g->n) {
            if (sw->ng > 1) faint(s, g->r);
            highlight(s, sw->t[g->first + sw->item].r);
        } else { /* the way back: the group, dashed */
            zui_engrave(s, grow(g->r, 4), ZUI_R_CONTROL, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 140u);
            zui_dash_rrect(s, grow(g->r, 2), ZUI_R_CONTROL, 2, 6, ZUI_GOLD);
        }
        break;
    }
    case ZUI_SW_MENU: {
        if (sw->chosen.kind != ZUI_SW_T_POINT) faint(s, sw->chosen.r);
        zui_rect_t m = zui_switch_menu_rect(sw);
        zui_fill_rrect(s, m, ZUI_R_PANEL, ZUI_PANEL, 255u);
        zui_engrave(s, m, ZUI_R_PANEL, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 80u);
        for (uint32_t i = 0; i < sw->n_menu; i++) {
            zui_rect_t row = {m.x + 2, m.y + (int32_t) i * MENU_ROW + 2, m.w - 4, MENU_ROW - 4};
            if (i == sw->hot) {
                zui_fill_rrect(s, row, ZUI_R_CONTROL, ZUI_RAISED, 255u);
                zui_stroke_rrect(s, row, ZUI_R_CONTROL, 2, ZUI_GOLD, 255u);
            }
            if (text && text->font) {
                int32_t base = row.y + (row.h + zui_text_ascent(text) + zui_text_descent(text)) / 2;
                zui_text_fit(s, text, row.x + ZUI_S3, base, row.w - 2 * ZUI_S3,
                             i == sw->hot ? ZUI_TEXT : ZUI_TEXT_2,
                             zui_switch_word(sw, (zui_sw_verb_t) sw->menu_verbs[i]));
            }
        }
        break;
    }
    case ZUI_SW_POINT_X:
        sweep_v(s, sw->px, sw->area, ZUI_GOLD);
        break;
    case ZUI_SW_POINT_Y:
        sweep_v(s, sw->px, sw->area, ZUI_TEXT_2);
        sweep_h(s, sw->py, sw->area, ZUI_GOLD);
        break;
    default:
        break;
    }
}
