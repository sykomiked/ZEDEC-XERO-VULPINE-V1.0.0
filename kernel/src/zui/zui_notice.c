/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_notice.c — the notification center (zui_notice.h). */
#include "zui_notice.h"

#include "zui_tokens.h"

/* zui_palette.h C6: every colour by role, in the active set */
#define C(role) zui_color(c->colors, ZUI_C_##role)

/* ---- small helpers --------------------------------------------------------- */

static bool same(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; a[i] && a[i] == b[i]; i++) {
    }
    return a[i] == b[i];
}

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Copy, cut on a UTF-8 boundary (NC1). NULL is "". */
static void copy(char *d, uint32_t cap, const char *s)
{
    uint32_t i = 0;
    if (s)
        for (; s[i] && i + 1u < cap; i++) d[i] = s[i];
    if (s && s[i])
        while (i > 0 && ((uint8_t) s[i] & 0xC0u) == 0x80u) i--;
    d[i] = 0;
}

static void zero(void *v, uint32_t n)
{
    uint8_t *z = v;
    for (uint32_t i = 0; i < n; i++) z[i] = 0;
}

static uint64_t clock_at(zui_notice_center_t *c, uint64_t now)
{
    if (now > c->now) c->now = now;
    return c->now;
}

static int32_t find(const zui_notice_center_t *c, uint32_t id)
{
    if (!c || !id) return -1;
    for (uint32_t i = 0; i < ZUI_NOTICE_MAX; i++)
        if (c->n[i].id == id) return (int32_t) i;
    return -1;
}

static zui_notice_t *get(zui_notice_center_t *c, uint32_t id)
{
    int32_t i = find(c, id);
    return i < 0 ? 0 : &c->n[i];
}

/* NC3: a is newer than b. */
static bool newer(const zui_notice_t *a, const zui_notice_t *b)
{
    return a->time_ms != b->time_ms ? a->time_ms > b->time_ms : a->seq > b->seq;
}

static bool on_list(const char list[][ZUI_NOTICE_APP_MAX], uint32_t n, const char *app)
{
    for (uint32_t i = 0; i < n; i++)
        if (same(list[i], app)) return true;
    return false;
}

/* ---- setup -------------------------------------------------------------------- */

void zui_notice_init(zui_notice_center_t *c)
{
    zero(c, sizeof *c);
    c->cap = ZUI_NOTICE_MAX;
    c->next_id = 1;
    c->next_seq = 1;
    c->banner_ms = ZUI_NOTICE_BANNER_MS;
    c->breakthrough = ZUI_NPRI_URGENT;
}

uint32_t zui_notice_count(const zui_notice_center_t *c)
{
    uint32_t n = 0;
    if (!c) return 0;
    for (uint32_t i = 0; i < ZUI_NOTICE_MAX; i++)
        if (c->n[i].id) n++;
    return n;
}

const zui_notice_t *zui_notice_get(const zui_notice_center_t *c, uint32_t id)
{
    int32_t i = find(c, id);
    return i < 0 ? 0 : &c->n[i];
}

/* ---- NC5: banners ----------------------------------------------------------- */

zui_gate_t zui_notice_decide(bool dnd, zui_npri_t breakthrough, bool allowed, bool muted,
                             zui_npri_t pri)
{
    if (muted) return ZUI_GATE_SILENT;
    if (!dnd) return ZUI_GATE_SHOW;
    return (pri >= breakthrough || allowed) ? ZUI_GATE_SHOW : ZUI_GATE_HOLD;
}

static zui_gate_t gate_of(const zui_notice_center_t *c, const zui_notice_t *n)
{
    if (c->gate) return c->gate(c->gate_ctx, n);
    return zui_notice_decide(c->dnd, (zui_npri_t) c->breakthrough,
                             on_list(c->allow, c->nallow, n->app),
                             on_list(c->muted, c->nmuted, n->app), (zui_npri_t) n->pri);
}

/* The best waiting banner: the highest priority, then the oldest. */
static zui_notice_t *best_waiting(zui_notice_center_t *c)
{
    zui_notice_t *b = 0;
    for (uint32_t i = 0; i < ZUI_NOTICE_MAX; i++) {
        zui_notice_t *n = &c->n[i];
        if (!n->id || n->banner != ZUI_BANNER_WAITING) continue;
        if (!b || n->pri > b->pri || (n->pri == b->pri && n->seq < b->seq)) b = n;
    }
    return b;
}

static void show(zui_notice_center_t *c, zui_notice_t *n)
{
    n->banner = ZUI_BANNER_SHOWING;
    c->banner = n->id;
    c->banner_since = c->now;
}

/* Put the right banner on screen. */
static void promote(zui_notice_center_t *c)
{
    zui_notice_t *cur = get(c, c->banner);
    if (cur && cur->banner != ZUI_BANNER_SHOWING) cur = 0;
    if (!cur) c->banner = 0;
    zui_notice_t *b = best_waiting(c);
    if (!b) return;
    if (cur) {
        if (cur->pri >= ZUI_NPRI_URGENT || b->pri < ZUI_NPRI_URGENT) return;
        cur->banner = ZUI_BANNER_WAITING; /* an urgent one takes its place */
    }
    show(c, b);
}

/* Take n's banner down, wherever it is. */
static void unbanner(zui_notice_center_t *c, zui_notice_t *n)
{
    if (n->banner == ZUI_BANNER_SHOWING && c->banner == n->id) c->banner = 0;
    n->banner = ZUI_BANNER_NONE;
}

uint32_t zui_notice_waiting(const zui_notice_center_t *c)
{
    uint32_t k = 0;
    if (!c) return 0;
    for (uint32_t i = 0; i < ZUI_NOTICE_MAX; i++)
        if (c->n[i].id && c->n[i].banner == ZUI_BANNER_WAITING) k++;
    return k;
}

/* NC5: n joins the queue, or loses its banner when the queue is full of
 * more urgent ones. */
static void enqueue(zui_notice_center_t *c, zui_notice_t *n)
{
    if (zui_notice_waiting(c) >= ZUI_NOTICE_BANNERS) {
        zui_notice_t *v = 0;
        for (uint32_t i = 0; i < ZUI_NOTICE_MAX; i++) {
            zui_notice_t *w = &c->n[i];
            if (!w->id || w->banner != ZUI_BANNER_WAITING) continue;
            if (!v || w->pri < v->pri || (w->pri == v->pri && w->seq < v->seq)) v = w;
        }
        c->banner_dropped++;
        if (!v || v->pri > n->pri) {
            n->banner = ZUI_BANNER_NONE;
            return;
        }
        v->banner = ZUI_BANNER_NONE;
    }
    n->banner = ZUI_BANNER_WAITING;
}

/* NC5, NC6: what happens to n's banner now. */
static void route(zui_notice_center_t *c, zui_notice_t *n)
{
    if (n->read) {
        unbanner(c, n);
        return;
    }
    zui_gate_t g = gate_of(c, n);
    if (g == ZUI_GATE_SILENT)
        unbanner(c, n);
    else if (g == ZUI_GATE_HOLD)
        n->banner = ZUI_BANNER_HELD;
    else
        enqueue(c, n);
}

uint32_t zui_notice_banner(const zui_notice_center_t *c)
{
    return c ? c->banner : 0;
}

void zui_notice_tick(zui_notice_center_t *c, uint64_t now_ms)
{
    if (!c) return;
    uint64_t now = clock_at(c, now_ms);
    zui_notice_t *cur = get(c, c->banner);
    if (cur && cur->pri < ZUI_NPRI_URGENT && now - c->banner_since >= (uint64_t) c->banner_ms)
        unbanner(c, cur);
    promote(c);
}

void zui_notice_banner_dismiss(zui_notice_center_t *c, uint64_t now_ms)
{
    if (!c) return;
    clock_at(c, now_ms);
    zui_notice_t *cur = get(c, c->banner);
    if (cur) unbanner(c, cur);
    c->banner = 0;
    promote(c);
}

/* ---- NC2: post, update, withdraw ------------------------------------------- */

static void remove_at(zui_notice_center_t *c, uint32_t i)
{
    if (c->banner == c->n[i].id) c->banner = 0;
    if (c->sel == c->n[i].id) c->sel = 0;
    zero(&c->n[i], sizeof c->n[i]);
}

/* NC9: drop the oldest posted until there is room for `room` more. */
static void evict(zui_notice_center_t *c, uint32_t room)
{
    while (zui_notice_count(c) + room > c->cap && zui_notice_count(c)) {
        int32_t old = -1;
        for (uint32_t i = 0; i < ZUI_NOTICE_MAX; i++)
            if (c->n[i].id && (old < 0 || c->n[i].seq < c->n[old].seq)) old = (int32_t) i;
        remove_at(c, (uint32_t) old);
        c->evicted++;
    }
}

void zui_notice_set_cap(zui_notice_center_t *c, uint32_t cap)
{
    if (!c) return;
    c->cap = cap > ZUI_NOTICE_MAX ? ZUI_NOTICE_MAX : cap;
    evict(c, 0);
    promote(c);
}

/* The text, actions, priority, time and author of a post or an update. */
static void fill(zui_notice_t *n, const zui_notice_spec_t *spec)
{
    copy(n->title, sizeof n->title, spec->title);
    copy(n->body, sizeof n->body, spec->body);
    uint32_t k = spec->act ? spec->nact : 0;
    if (k > ZUI_NOTICE_ACTIONS) k = ZUI_NOTICE_ACTIONS;
    zero(n->act, sizeof n->act);
    for (uint32_t i = 0; i < k; i++) {
        copy(n->act[i].label, sizeof n->act[i].label, spec->act[i].label);
        copy(n->act[i].action, sizeof n->act[i].action, spec->act[i].action);
    }
    n->nact = (uint8_t) k;
    uint32_t pri = (uint32_t) spec->pri;
    n->pri = (uint8_t) (pri > ZUI_NPRI_URGENT ? ZUI_NPRI_URGENT : pri);
    n->time_ms = spec->time_ms;
    if (spec->agent && spec->agent[0]) {
        n->ai = true;
        copy(n->agent, sizeof n->agent, spec->agent);
    }
    if (n->ai) { /* NC7: new words from an AI wait for the person again */
        n->held = ZUI_HELD_PENDING;
        if (n->pri > ZUI_NPRI_NORMAL) n->pri = ZUI_NPRI_NORMAL;
    }
}

uint32_t zui_notice_post(zui_notice_center_t *c, const zui_notice_spec_t *spec, uint64_t now_ms)
{
    if (!c || !spec || !spec->app || !spec->app[0] || !c->cap) return 0;
    clock_at(c, now_ms);
    evict(c, 1);
    zui_notice_t *n = 0;
    for (uint32_t i = 0; i < ZUI_NOTICE_MAX && !n; i++)
        if (!c->n[i].id) n = &c->n[i];
    if (!n) return 0;
    zero(n, sizeof *n);
    n->id = c->next_id++;
    if (!c->next_id) c->next_id = 1;
    n->seq = c->next_seq++;
    copy(n->app, sizeof n->app, spec->app);
    copy(n->thread, sizeof n->thread, spec->thread);
    fill(n, spec);
    route(c, n);
    promote(c);
    return n->id;
}

bool zui_notice_update(zui_notice_center_t *c, uint32_t id, const zui_notice_spec_t *spec,
                       bool alert, uint64_t now_ms)
{
    zui_notice_t *n = get(c, id);
    if (!n || !spec) return false;
    clock_at(c, now_ms);
    fill(n, spec); /* the app and the thread stay */
    if (alert) {
        n->read = false;
        if (n->banner == ZUI_BANNER_SHOWING)
            c->banner_since = c->now; /* shown afresh */
        else
            route(c, n);
    }
    promote(c);
    return true;
}

bool zui_notice_withdraw(zui_notice_center_t *c, uint32_t id, uint64_t now_ms)
{
    int32_t i = find(c, id);
    if (i < 0) return false;
    clock_at(c, now_ms);
    remove_at(c, (uint32_t) i);
    promote(c);
    return true;
}

uint32_t zui_notice_withdraw_thread(zui_notice_center_t *c, const char *app, const char *thread,
                                    uint64_t now_ms)
{
    uint32_t k = 0;
    if (!c || !app) return 0;
    clock_at(c, now_ms);
    char t[ZUI_NOTICE_THREAD_MAX];
    copy(t, sizeof t, thread);
    for (uint32_t i = 0; i < ZUI_NOTICE_MAX; i++)
        if (c->n[i].id && same(c->n[i].app, app) && same(c->n[i].thread, t)) {
            remove_at(c, i);
            k++;
        }
    promote(c);
    return k;
}

/* ---- NC3: groups ------------------------------------------------------------- */

static bool group_newer(const zui_notice_group_t *a, const zui_notice_group_t *b)
{
    return a->newest_ms != b->newest_ms ? a->newest_ms > b->newest_ms
                                        : a->newest_seq > b->newest_seq;
}

static void group_add(zui_notice_group_t *g, const zui_notice_t *n)
{
    g->count++;
    if (!n->read) g->unread++;
    if (g->count == 1u || n->time_ms > g->newest_ms ||
        (n->time_ms == g->newest_ms && n->seq > g->newest_seq)) {
        g->newest = n->id;
        g->newest_ms = n->time_ms;
        g->newest_seq = n->seq;
    }
}

/* Groups by app (app NULL), or by thread within one app. */
static uint32_t groups(const zui_notice_center_t *c, const char *app, zui_notice_group_t *out,
                       uint32_t cap)
{
    zui_notice_group_t g[ZUI_NOTICE_MAX];
    uint32_t ng = 0;
    if (!c || (!out && cap)) return 0;
    for (uint32_t i = 0; i < ZUI_NOTICE_MAX; i++) {
        const zui_notice_t *n = &c->n[i];
        if (!n->id || (app && !same(n->app, app))) continue;
        uint32_t k = 0;
        for (; k < ng; k++)
            if (same(g[k].app, n->app) && (!app || same(g[k].thread, n->thread))) break;
        if (k == ng) {
            zero(&g[ng], sizeof g[ng]);
            g[ng].app = n->app;
            g[ng].thread = app ? n->thread : 0;
            ng++;
        }
        group_add(&g[k], n);
    }
    for (uint32_t i = 1; i < ng; i++) /* newest first: insertion sort */
        for (uint32_t j = i; j > 0 && group_newer(&g[j], &g[j - 1u]); j--) {
            zui_notice_group_t t = g[j];
            g[j] = g[j - 1u];
            g[j - 1u] = t;
        }
    for (uint32_t i = 0; i < ng && i < cap; i++) out[i] = g[i];
    return ng;
}

uint32_t zui_notice_apps(const zui_notice_center_t *c, zui_notice_group_t *out, uint32_t cap)
{
    return groups(c, 0, out, cap);
}

uint32_t zui_notice_threads(const zui_notice_center_t *c, const char *app, zui_notice_group_t *out,
                            uint32_t cap)
{
    if (!app) return 0;
    return groups(c, app, out, cap);
}

static bool matches(const zui_notice_t *n, const char *app, const char *thread)
{
    if (!n->id) return false;
    if (app && !same(n->app, app)) return false;
    return !(app && thread && !same(n->thread, thread));
}

uint32_t zui_notice_list(const zui_notice_center_t *c, const char *app, const char *thread,
                         uint32_t *ids, uint32_t cap)
{
    const zui_notice_t *m[ZUI_NOTICE_MAX];
    uint32_t k = 0;
    if (!c || (!ids && cap)) return 0;
    for (uint32_t i = 0; i < ZUI_NOTICE_MAX; i++)
        if (matches(&c->n[i], app, thread)) m[k++] = &c->n[i];
    for (uint32_t i = 1; i < k; i++)
        for (uint32_t j = i; j > 0 && newer(m[j], m[j - 1u]); j--) {
            const zui_notice_t *t = m[j];
            m[j] = m[j - 1u];
            m[j - 1u] = t;
        }
    for (uint32_t i = 0; i < k && i < cap; i++) ids[i] = m[i]->id;
    return k;
}

/* ---- NC4: read ------------------------------------------------------------------ */

static void set_read(zui_notice_center_t *c, zui_notice_t *n, bool read)
{
    n->read = read;
    if (read) unbanner(c, n);
}

bool zui_notice_mark_read(zui_notice_center_t *c, uint32_t id, bool read)
{
    zui_notice_t *n = get(c, id);
    if (!n) return false;
    set_read(c, n, read);
    promote(c);
    return true;
}

uint32_t zui_notice_mark_group_read(zui_notice_center_t *c, const char *app, const char *thread)
{
    uint32_t k = 0;
    if (!c) return 0;
    for (uint32_t i = 0; i < ZUI_NOTICE_MAX; i++)
        if (matches(&c->n[i], app, thread) && !c->n[i].read) {
            set_read(c, &c->n[i], true);
            k++;
        }
    promote(c);
    return k;
}

uint32_t zui_notice_unread(const zui_notice_center_t *c, const char *app, const char *thread)
{
    uint32_t k = 0;
    if (!c) return 0;
    for (uint32_t i = 0; i < ZUI_NOTICE_MAX; i++)
        if (matches(&c->n[i], app, thread) && !c->n[i].read) k++;
    return k;
}

/* ---- NC6: do not disturb ------------------------------------------------------ */

uint32_t zui_notice_regate(zui_notice_center_t *c, uint64_t now_ms)
{
    uint32_t released = 0, last = 0;
    if (!c) return 0;
    clock_at(c, now_ms);
    for (;;) { /* oldest first, so a full queue keeps the right ones */
        zui_notice_t *n = 0;
        for (uint32_t i = 0; i < ZUI_NOTICE_MAX; i++) {
            zui_notice_t *w = &c->n[i];
            if (!w->id || w->seq <= last) continue;
            if (w->banner != ZUI_BANNER_WAITING && w->banner != ZUI_BANNER_HELD) continue;
            if (!n || w->seq < n->seq) n = w;
        }
        if (!n) break;
        last = n->seq;
        bool was_held = n->banner == ZUI_BANNER_HELD;
        n->banner = ZUI_BANNER_NONE;
        route(c, n);
        if (was_held && n->banner == ZUI_BANNER_WAITING) released++;
    }
    zui_notice_t *cur = get(c, c->banner); /* a banner on screen goes when muted */
    if (cur && gate_of(c, cur) == ZUI_GATE_SILENT) unbanner(c, cur);
    promote(c);
    return released;
}

uint32_t zui_notice_set_dnd(zui_notice_center_t *c, bool on, zui_npri_t breakthrough,
                            uint64_t now_ms)
{
    if (!c) return 0;
    c->dnd = on;
    c->breakthrough = (uint8_t) (breakthrough > ZUI_NPRI_URGENT ? ZUI_NPRI_URGENT : breakthrough);
    return zui_notice_regate(c, now_ms);
}

static bool list_set(char list[][ZUI_NOTICE_APP_MAX], uint32_t *n, const char *app, bool on)
{
    if (!app || !app[0]) return false;
    char key[ZUI_NOTICE_APP_MAX];
    copy(key, sizeof key, app);
    for (uint32_t i = 0; i < *n; i++)
        if (same(list[i], key)) {
            if (on) return true;
            for (uint32_t j = i + 1u; j < *n; j++) copy(list[j - 1u], ZUI_NOTICE_APP_MAX, list[j]);
            (*n)--;
            list[*n][0] = 0;
            return true;
        }
    if (!on) return true;
    if (*n >= ZUI_NOTICE_LIST) return false;
    copy(list[(*n)++], ZUI_NOTICE_APP_MAX, key);
    return true;
}

bool zui_notice_allow(zui_notice_center_t *c, const char *app, bool allowed)
{
    if (!c || !list_set(c->allow, &c->nallow, app, allowed)) return false;
    zui_notice_regate(c, c->now);
    return true;
}

bool zui_notice_mute(zui_notice_center_t *c, const char *app, bool muted)
{
    if (!c || !list_set(c->muted, &c->nmuted, app, muted)) return false;
    zui_notice_regate(c, c->now);
    return true;
}

void zui_notice_set_gate(zui_notice_center_t *c, zui_notice_gate_fn gate, void *ctx)
{
    if (!c) return;
    c->gate = gate;
    c->gate_ctx = ctx;
}

/* ---- NC7, NC8: AI, and acting --------------------------------------------- */

bool zui_notice_accept(zui_notice_center_t *c, uint32_t id)
{
    zui_notice_t *n = get(c, id);
    if (!n || !n->ai || n->held != ZUI_HELD_PENDING) return false;
    n->held = ZUI_HELD_ACCEPTED;
    return true;
}

bool zui_notice_reject(zui_notice_center_t *c, uint32_t id)
{
    zui_notice_t *n = get(c, id);
    if (!n || !n->ai || n->held != ZUI_HELD_PENDING) return false;
    n->held = ZUI_HELD_REJECTED;
    set_read(c, n, true);
    promote(c);
    return true;
}

int zui_notice_act(zui_notice_center_t *c, uint32_t id, int32_t i, const char **action,
                   uint64_t now_ms)
{
    if (action) *action = 0;
    if (!c || !action) return ZUI_NOTICE_E_ARG;
    zui_notice_t *n = get(c, id);
    if (!n || i < -1 || i >= (int32_t) n->nact) return ZUI_NOTICE_E_NONE;
    clock_at(c, now_ms);
    if (i >= 0 && n->ai && n->held == ZUI_HELD_PENDING) return ZUI_NOTICE_E_HELD;
    if (i >= 0 && n->ai && n->held == ZUI_HELD_REJECTED) return ZUI_NOTICE_E_REJECTED;
    *action = i < 0 ? n->app : n->act[i].action;
    set_read(c, n, true);
    promote(c);
    return ZUI_NOTICE_OK;
}

/* ---- NC10: drawing ------------------------------------------------------------- */

static bool has_font(const zui_text_t *t)
{
    return t && t->font && t->cache;
}

static int32_t line_h(const zui_text_t *t)
{
    return has_font(t) ? zui_text_ascent(t) - zui_text_descent(t) + 4 : 16;
}

static int32_t text_w(const zui_text_t *t, const char *s)
{
    return has_font(t) ? zui_text_width(t, s) : 8 * (int32_t) slen(s);
}

/* Text in a line box whose top is y. */
static void put_text(zui_surface_t *s, const zui_text_t *t, int32_t x, int32_t y, int32_t max_w,
                     uint32_t rgb, const char *str)
{
    if (!has_font(t) || max_w <= 0 || !str[0]) return;
    zui_text_fit(s, t, x, y + 2 + zui_text_ascent(t), max_w, rgb, str);
}

static zui_rect_t isect(zui_rect_t a, zui_rect_t b)
{
    int32_t x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
    int32_t x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int32_t y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    zui_rect_t r = {x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0};
    return r;
}

static void add_hit(zui_notice_center_t *c, uint32_t id, int32_t action, bool banner, zui_rect_t r,
                    zui_rect_t view)
{
    zui_rect_t v = isect(r, view);
    if (!v.w || !v.h || c->nhit >= ZUI_NOTICE_HITS) return;
    c->hit[c->nhit++] = (zui_notice_hit_t){id, action, banner, v};
}

static void drop_hits(zui_notice_center_t *c, bool banner)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < c->nhit; i++)
        if (c->hit[i].banner != banner) c->hit[k++] = c->hit[i];
    c->nhit = k;
}

/* The buttons a notice shows: its actions, or Accept / Reject while held. */
static uint32_t buttons(const zui_notice_t *n, const char *label[ZUI_NOTICE_ACTIONS],
                        int32_t code[ZUI_NOTICE_ACTIONS])
{
    if (n->ai && n->held == ZUI_HELD_PENDING) {
        label[0] = "Accept";
        code[0] = ZUI_NOTICE_HIT_ACCEPT;
        label[1] = "Reject";
        code[1] = ZUI_NOTICE_HIT_REJECT;
        return 2;
    }
    if (n->ai && n->held == ZUI_HELD_REJECTED) return 0;
    for (uint32_t i = 0; i < n->nact; i++) {
        label[i] = n->act[i].label;
        code[i] = (int32_t) i;
    }
    return n->nact;
}

uint32_t zui_notice_summary(const zui_notice_center_t *c, const char *app, char *out, uint32_t cap)
{
    if (!out || !cap) return 0;
    uint32_t v = c ? zui_notice_list(c, app, 0, 0, 0) : 0, n = 0, nd = 0;
    char d[12];
    do {
        d[nd++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    bool one = nd == 1 && d[0] == '1';
    while (nd && n + 1u < cap) out[n++] = d[--nd];
    for (const char *p = one ? " notification" : " notifications"; *p && n + 1u < cap; p++)
        out[n++] = *p;
    out[n] = 0;
    return n;
}

static int32_t card_h(const zui_notice_center_t *c, const zui_notice_t *n, const zui_text_t *t)
{
    const char *label[ZUI_NOTICE_ACTIONS];
    int32_t code[ZUI_NOTICE_ACTIONS];
    int32_t lh = line_h(t), h = ZUI_S3 * 2 + lh * 3; /* app line, title, body */
    if (c->hide_previews) return ZUI_S3 * 2 + lh * 2; /* NC11: app line, count */
    if (buttons(n, label, code)) h += ZUI_S2 + lh + ZUI_S2;
    return h;
}

/* One notice as a carved panel. primary: its first button is the gold one. */
static void draw_card(zui_notice_center_t *c, zui_surface_t *s, zui_rect_t r, zui_rect_t view,
                      const zui_notice_t *n, const zui_text_t *t, bool primary, bool banner)
{
    int32_t lh = line_h(t), pad = ZUI_S3;
    zui_fill_rrect(s, r, ZUI_R_PANEL, C(PANEL), 255);
    zui_engrave(s, r, ZUI_R_PANEL, C(ENGRAVE_LO), C(ENGRAVE_HI), 40);
    if (n->ai) { /* NC7: marked; dashed, but not gold (NC10) */
        zui_rect_t in = {r.x + 3, r.y + 3, r.w - 6, r.h - 6};
        zui_dash_rrect(s, in, ZUI_R_PANEL - 2, 1, 4, C(TEXT_2));
    }
    int32_t x = r.x + pad, y = r.y + pad, w = r.w - 2 * pad;
    if (!n->read) { /* the unread dot */
        zui_rect_t d = {r.x + 4, y + lh / 2 - 3, 6, 6};
        zui_fill_rrect(s, d, 3, C(INFO), 255);
    }
    /* app line, and who wrote it when an AI did */
    char tag[ZUI_NOTICE_AGENT_MAX + 8];
    tag[0] = 0;
    if (n->ai) {
        uint32_t k = 0;
        for (const char *p = "AI · "; *p; p++) tag[k++] = *p;
        for (const char *p = n->agent; *p && k + 1u < sizeof tag; p++) tag[k++] = *p;
        tag[k] = 0;
    }
    int32_t tw = tag[0] ? text_w(t, tag) : 0;
    put_text(s, t, x, y, w - tw - (tw ? ZUI_S2 : 0), C(MUTED), n->app);
    if (tag[0]) put_text(s, t, x + w - tw, y, tw, C(MUTED), tag);
    y += lh;
    if (c->hide_previews) { /* NC11: how many, nothing of what */
        char sum[32];
        zui_notice_summary(c, n->app, sum, sizeof sum);
        put_text(s, t, x, y, w, C(TEXT), sum);
        add_hit(c, n->id, ZUI_NOTICE_HIT_CARD, banner, r, view);
        return;
    }
    put_text(s, t, x, y, w, n->read ? C(TEXT_2) : C(TEXT), n->title);
    y += lh;
    put_text(s, t, x, y, w, C(TEXT_2), n->body);
    y += lh;
    add_hit(c, n->id, ZUI_NOTICE_HIT_CARD, banner, r, view);
    const char *label[ZUI_NOTICE_ACTIONS];
    int32_t code[ZUI_NOTICE_ACTIONS];
    uint32_t nb = buttons(n, label, code);
    y += ZUI_S2;
    int32_t bx = x;
    for (uint32_t i = 0; i < nb; i++) {
        int32_t bw = text_w(t, label[i]) + 2 * ZUI_S3;
        if (bx + bw > r.x + r.w - pad) break;
        zui_rect_t b = {bx, y, bw, lh + 4};
        bool gold = primary && i == 0;
        if (gold) {
            zui_fill_rrect(s, b, ZUI_R_CONTROL, C(GOLD), 255);
        } else {
            zui_fill_rrect(s, b, ZUI_R_CONTROL, C(RAISED), 255);
            zui_stroke_rrect(s, b, ZUI_R_CONTROL, 1, C(EDGE), 255);
        }
        put_text(s, t, b.x + ZUI_S3, b.y, bw - 2 * ZUI_S3, gold ? C(VOID) : C(TEXT), label[i]);
        add_hit(c, n->id, code[i], banner, b, view);
        bx += bw + ZUI_S2;
    }
}

int32_t zui_notice_draw_center(zui_notice_center_t *c, zui_surface_t *s, zui_rect_t r,
                               const zui_text_t *text)
{
    if (!c || !s) return 0;
    drop_hits(c, false);
    zui_rect_t keep = s->clip;
    zui_rect_t view = isect(keep, r);
    s->clip = view;
    zui_fill(s, r, C(VOID));
    int32_t lh = line_h(text), pad = ZUI_S4, top = r.y + pad - c->scroll, y = top;
    /* header: the title and the unread count */
    put_text(s, text, r.x + pad, y, r.w - 2 * pad, C(TEXT), "Notifications");
    uint32_t unread = zui_notice_unread(c, 0, 0);
    if (unread) {
        char cnt[24];
        uint32_t k = 0, v = unread;
        char d[12];
        uint32_t nd = 0;
        do {
            d[nd++] = (char) ('0' + v % 10u);
            v /= 10u;
        } while (v);
        while (nd) cnt[k++] = d[--nd];
        for (const char *p = " unread"; *p; p++) cnt[k++] = *p;
        cnt[k] = 0;
        int32_t cw = text_w(text, cnt);
        put_text(s, text, r.x + r.w - pad - cw, y, cw, C(MUTED), cnt);
    }
    y += lh + pad;
    /* the selected notice: the one asked for, else the newest */
    uint32_t sel = c->sel;
    if (!get(c, sel)) zui_notice_list(c, 0, 0, &sel, 1);
    if (!zui_notice_count(c)) {
        put_text(s, text, r.x + pad, y, r.w - 2 * pad, C(MUTED), "Nothing new");
        y += lh;
    }
    zui_notice_group_t apps[ZUI_NOTICE_MAX], th[ZUI_NOTICE_MAX];
    uint32_t na = zui_notice_apps(c, apps, ZUI_NOTICE_MAX);
    for (uint32_t a = 0; a < na; a++) {
        put_text(s, text, r.x + pad, y, r.w - 2 * pad, C(MUTED), apps[a].app);
        y += lh;
        zui_hline(s, r.x + pad, y, r.w - 2 * pad, 1, C(HAIRLINE));
        y += ZUI_S2;
        if (c->hide_previews) { /* NC11: one card for the app */
            const zui_notice_t *N = zui_notice_get(c, apps[a].newest);
            zui_rect_t card = {r.x + pad, y, r.w - 2 * pad, card_h(c, N, text)};
            if (N && card.y < r.y + r.h && card.y + card.h > r.y)
                draw_card(c, s, card, view, N, text, false, false);
            y += card.h + 2 * ZUI_S2;
            continue;
        }
        uint32_t nt = zui_notice_threads(c, apps[a].app, th, ZUI_NOTICE_MAX);
        for (uint32_t k = 0; k < nt; k++) {
            uint32_t ids[ZUI_NOTICE_MAX];
            uint32_t n = zui_notice_list(c, apps[a].app, th[k].thread, ids, ZUI_NOTICE_MAX);
            uint32_t shown = n < ZUI_NOTICE_SHOWN ? n : ZUI_NOTICE_SHOWN;
            for (uint32_t j = 0; j < shown; j++) {
                const zui_notice_t *N = zui_notice_get(c, ids[j]);
                zui_rect_t card = {r.x + pad, y, r.w - 2 * pad, card_h(c, N, text)};
                if (card.y < r.y + r.h && card.y + card.h > r.y)
                    draw_card(c, s, card, view, N, text, N->id == sel, false);
                y += card.h + ZUI_S2;
            }
            if (n > shown) { /* "+3 more" */
                char more[24];
                uint32_t q = 0, v = n - shown;
                char d[12];
                uint32_t nd = 0;
                more[q++] = '+';
                do {
                    d[nd++] = (char) ('0' + v % 10u);
                    v /= 10u;
                } while (v);
                while (nd) more[q++] = d[--nd];
                for (const char *p = " more"; *p; p++) more[q++] = *p;
                more[q] = 0;
                put_text(s, text, r.x + pad + ZUI_S3, y, r.w - 2 * pad, C(MUTED), more);
                y += lh + ZUI_S2;
            }
        }
        y += ZUI_S2;
    }
    s->clip = keep;
    return y - top + 2 * pad;
}

bool zui_notice_draw_banner(zui_notice_center_t *c, zui_surface_t *s, zui_rect_t r,
                            const zui_text_t *text)
{
    if (!c || !s) return false;
    drop_hits(c, true);
    const zui_notice_t *n = zui_notice_get(c, c->banner);
    if (!n) return false;
    zui_rect_t keep = s->clip;
    zui_rect_t view = isect(keep, r);
    s->clip = view;
    zui_rect_t card = r;
    int32_t need = card_h(c, n, text);
    if (card.h > need) card.h = need;
    draw_card(c, s, card, view, n, text, true, true);
    zui_stroke_rrect(s, card, ZUI_R_PANEL, 1, C(EDGE), 255);
    s->clip = keep;
    return true;
}

uint32_t zui_notice_hit(const zui_notice_center_t *c, int32_t x, int32_t y, int32_t *action,
                        bool *banner)
{
    if (action) *action = ZUI_NOTICE_HIT_CARD;
    if (banner) *banner = false;
    if (!c) return 0;
    /* a button before the card under it; the banner before the panel */
    for (int pass = 0; pass < 4; pass++) {
        bool want_banner = pass < 2, want_button = (pass % 2) == 0;
        for (uint32_t i = 0; i < c->nhit; i++) {
            const zui_notice_hit_t *h = &c->hit[i];
            if (h->banner != want_banner || (h->action != ZUI_NOTICE_HIT_CARD) != want_button)
                continue;
            if (x >= h->r.x && x < h->r.x + h->r.w && y >= h->r.y && y < h->r.y + h->r.h) {
                if (action) *action = h->action;
                if (banner) *banner = h->banner;
                return h->id;
            }
        }
    }
    return 0;
}
