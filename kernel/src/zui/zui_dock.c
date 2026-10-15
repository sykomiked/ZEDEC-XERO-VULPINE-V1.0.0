/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_dock.c — the astrolabe dock: the seal, its rings, turning, the pips
 * and the keyboard (zui_dock.h). */
#include "zui_dock.h"
#include "zui_motion.h"
#include "zui_progress.h"
#include "zui_tokens.h"

#define QUARTER (ZUI_DOCK_TURN / 4)
#define HALF    (ZUI_DOCK_TURN / 2)
#define ONE     16384 /* the sine table's unit */

/* sin(90 degrees * i / 256) * 16384, i = 0..256 (DK11) */
static const int16_t SIN_Q[257] = {
    0,     101,   201,   302,   402,   503,   603,   704,   804,   904,   1005,  1105,  1205,
    1306,  1406,  1506,  1606,  1706,  1806,  1906,  2006,  2105,  2205,  2305,  2404,  2503,
    2603,  2702,  2801,  2900,  2999,  3098,  3196,  3295,  3393,  3492,  3590,  3688,  3786,
    3883,  3981,  4078,  4176,  4273,  4370,  4467,  4563,  4660,  4756,  4852,  4948,  5044,
    5139,  5235,  5330,  5425,  5520,  5614,  5708,  5803,  5897,  5990,  6084,  6177,  6270,
    6363,  6455,  6547,  6639,  6731,  6823,  6914,  7005,  7096,  7186,  7276,  7366,  7456,
    7545,  7635,  7723,  7812,  7900,  7988,  8076,  8163,  8250,  8337,  8423,  8509,  8595,
    8680,  8765,  8850,  8935,  9019,  9102,  9186,  9269,  9352,  9434,  9516,  9598,  9679,
    9760,  9841,  9921,  10001, 10080, 10159, 10238, 10316, 10394, 10471, 10549, 10625, 10702,
    10778, 10853, 10928, 11003, 11077, 11151, 11224, 11297, 11370, 11442, 11514, 11585, 11656,
    11727, 11797, 11866, 11935, 12004, 12072, 12140, 12207, 12274, 12340, 12406, 12472, 12537,
    12601, 12665, 12729, 12792, 12854, 12916, 12978, 13039, 13100, 13160, 13219, 13279, 13337,
    13395, 13453, 13510, 13567, 13623, 13678, 13733, 13788, 13842, 13896, 13949, 14001, 14053,
    14104, 14155, 14206, 14256, 14305, 14354, 14402, 14449, 14497, 14543, 14589, 14635, 14680,
    14724, 14768, 14811, 14854, 14896, 14937, 14978, 15019, 15059, 15098, 15137, 15175, 15213,
    15250, 15286, 15322, 15357, 15392, 15426, 15460, 15493, 15525, 15557, 15588, 15619, 15649,
    15679, 15707, 15736, 15763, 15791, 15817, 15843, 15868, 15893, 15917, 15941, 15964, 15986,
    16008, 16029, 16049, 16069, 16088, 16107, 16125, 16143, 16160, 16176, 16192, 16207, 16221,
    16235, 16248, 16261, 16273, 16284, 16295, 16305, 16315, 16324, 16332, 16340, 16347, 16353,
    16359, 16364, 16369, 16373, 16376, 16379, 16381, 16383, 16384, 16384};

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

static int32_t iabs(int32_t a)
{
    return a < 0 ? -a : a;
}

static void copy(char *d, uint32_t cap, const char *s)
{
    uint32_t i = 0;
    for (; s && s[i] && i + 1u < cap; i++) d[i] = s[i];
    d[i] = 0;
}

static bool same(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

/* a / b rounded to nearest, b > 0 */
static int32_t rdiv(int64_t a, int64_t b)
{
    return (int32_t) (a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b));
}

int32_t zui_dock_sin(int32_t a)
{
    a %= ZUI_DOCK_TURN;
    if (a < 0) a += ZUI_DOCK_TURN;
    int32_t sign = 1;
    if (a >= HALF) {
        a -= HALF;
        sign = -1;
    }
    if (a > QUARTER) a = HALF - a;
    int32_t i = a >> 2, f = a & 3, v = SIN_Q[i];
    if (f) v += ((SIN_Q[i + 1] - SIN_Q[i]) * f + 2) >> 2;
    return sign * v;
}

int32_t zui_dock_cos(int32_t a)
{
    return zui_dock_sin(a + QUARTER);
}

/* The smallest angle in 0..QUARTER whose sine is at least num / r. */
static int32_t asin_up(int32_t num, int32_t r)
{
    if (num <= 0) return 0;
    if (num >= r) return QUARTER;
    int32_t lo = 0, hi = QUARTER;
    while (lo < hi) {
        int32_t mid = (lo + hi) / 2;
        if ((int64_t) zui_dock_sin(mid) * r >= (int64_t) num * ONE)
            hi = mid;
        else
            lo = mid + 1;
    }
    return lo;
}

/* The largest angle in 0..QUARTER whose sine is at most num / r. */
static int32_t asin_down(int32_t num, int32_t r)
{
    if (num <= 0) return 0;
    if (num >= r) return QUARTER;
    int32_t a = asin_up(num, r);
    return (int64_t) zui_dock_sin(a) * r > (int64_t) num * ONE ? a - 1 : a;
}

/* ---- geometry (DK1, DK2, DK3) -------------------------------------------- */

/* The dock's frame: origin on the screen corner (or the bottom centre), u
 * along the edge (su: +1 rightwards), v away from it (sv: +1 upwards). */
typedef struct {
    int32_t ox, oy, su, sv;
    bool half;
} frame_t;

static frame_t frame_of(const zui_dock_t *d)
{
    frame_t f = {0, d->h, 1, 1, false};
    if (d->compact) {
        f.ox = d->w / 2;
        f.half = true;
        return f;
    }
    bool right = d->corner == ZUI_DOCK_BOTTOM_RIGHT || d->corner == ZUI_DOCK_TOP_RIGHT;
    bool top = d->corner == ZUI_DOCK_TOP_LEFT || d->corner == ZUI_DOCK_TOP_RIGHT;
    f.ox = right ? d->w : 0;
    f.su = right ? -1 : 1;
    f.oy = top ? 0 : d->h;
    f.sv = top ? -1 : 1;
    return f;
}

static void at_angle(const frame_t *f, int32_t r, int32_t a, int32_t *x, int32_t *y)
{
    *x = f->ox + f->su * rdiv((int64_t) r * zui_dock_cos(a), ONE);
    *y = f->oy - f->sv * rdiv((int64_t) r * zui_dock_sin(a), ONE);
}

static int32_t spacing(const zui_dock_t *d)
{
    return d->icon + 2 * ZUI_DOCK_PAD + 2; /* centre to centre, as a chord */
}

static int32_t ring_radius(const zui_dock_t *d, uint32_t k)
{
    int32_t first = d->compact ? d->seal + d->icon + 20 : d->seal + 2 * d->icon + 22;
    int32_t gap = d->compact ? d->icon + 16 : d->icon + 14;
    return first + (int32_t) k * gap;
}

/* DK2: the usable arc of a ring and how many slots it shows. */
static void arc(const zui_dock_t *d, zui_dock_ring_t *g)
{
    int32_t r = g->r, m = d->icon / 2 + ZUI_DOCK_EDGE;
    g->cap = 0;
    g->lo = g->hi = g->step = 0;
    if (r <= m) return;
    int32_t lo = asin_up(m, r), hi;
    if (d->compact) {
        int32_t room = d->w / 2 - m;
        if (room <= 0 || r > d->h - m) return;
        if (r > room) lo = imax(lo, QUARTER - asin_down(room, r));
        hi = HALF - lo;
    } else {
        hi = QUARTER - lo;
        if (r > d->w - m) lo = imax(lo, QUARTER - asin_down(d->w - m, r));
        if (r > d->h - m) hi = imin(hi, asin_down(d->h - m, r));
    }
    if (hi < lo) return;
    int32_t half_chord = (spacing(d) + 1) / 2;
    if (half_chord >= r) return;
    g->lo = lo;
    g->hi = hi;
    g->step = 2 * asin_up(half_chord, r);
    uint32_t cap = (uint32_t) ((hi - lo) / g->step) + 1u;
    g->cap = (uint16_t) (cap > ZUI_DOCK_SLOTS ? ZUI_DOCK_SLOTS : cap);
}

static uint32_t shown(const zui_dock_ring_t *g)
{
    return g->n < g->cap ? g->n : g->cap;
}

static bool turns(const zui_dock_ring_t *g)
{
    return g->cap && g->n > g->cap;
}

/* Slots run left to right on the screen: with u rightwards that is the
 * angle going down. */
static int32_t dir_of(const frame_t *f)
{
    return f->su > 0 ? -1 : 1;
}

/* The angle of slot position p256 (slot units * 256) on ring g. */
static int32_t slot_angle(const frame_t *f, const zui_dock_ring_t *g, int32_t p256)
{
    int32_t k = (int32_t) shown(g), mid = (g->lo + g->hi) / 2;
    int64_t off = ((int64_t) (2 * p256 - (k - 1) * 256) * g->step) / 512;
    return mid + dir_of(f) * (int32_t) off;
}

static void put(zui_dock_ring_t *g, uint16_t item)
{
    if (g->n < ZUI_DOCK_ITEMS) g->item[g->n++] = item;
}

static int32_t total_slots(const zui_dock_t *d)
{
    int32_t n = 0;
    for (uint32_t k = 0; k < d->nring; k++) n += (int32_t) shown(&d->ring[k]);
    return n;
}

static void layout(zui_dock_t *d)
{
    uint16_t rot[ZUI_DOCK_RINGS];
    for (uint32_t k = 0; k < ZUI_DOCK_RINGS; k++) {
        zui_dock_ring_t *g = &d->ring[k];
        rot[k] = g->rot;
        g->r = ring_radius(d, k);
        arc(d, g);
        g->n = 0;
    }
    uint32_t n0 = d->nopen, n1 = d->nitem - d->nopen;
    uint32_t c0 = d->ring[0].cap, c1 = d->ring[1].cap, c2 = d->ring[2].cap;
    zui_dock_ring_t *r0 = &d->ring[0], *r1 = &d->ring[1], *r2 = &d->ring[2];
    if (!c1) { /* room for one ring only: everything on it, turning */
        for (uint32_t i = 0; i < d->nitem; i++) put(r0, (uint16_t) i);
    } else if ((n0 <= c0 && n1 <= c1) || (!c2 && n1)) {
        for (uint32_t i = 0; i < d->nitem; i++) put(i < n0 ? r0 : r1, (uint16_t) i);
    } else if (!n1) { /* no pinned apps: ring 1 takes the overflow */
        for (uint32_t i = 0; i < n0; i++) put(i < c0 ? r0 : r1, (uint16_t) i);
    } else {
        for (uint32_t i = 0; i < n0; i++) put(i < c0 ? r0 : r2, (uint16_t) i);
        for (uint32_t i = 0; i < n1; i++) put(i < c1 ? r1 : r2, (uint16_t) (n0 + i));
        /* r2 holds open overflow then pinned overflow: open came first */
        uint16_t tmp[ZUI_DOCK_ITEMS];
        uint32_t t = 0;
        for (uint32_t i = 0; i < r2->n; i++)
            if (r2->item[i] < n0) tmp[t++] = r2->item[i];
        for (uint32_t i = 0; i < r2->n; i++)
            if (r2->item[i] >= n0) tmp[t++] = r2->item[i];
        for (uint32_t i = 0; i < t; i++) r2->item[i] = tmp[i];
    }
    d->nring = 0;
    for (uint32_t k = 0; k < ZUI_DOCK_RINGS; k++) {
        zui_dock_ring_t *g = &d->ring[k];
        g->rot = turns(g) ? (uint16_t) (rot[k] % g->n) : 0;
        if (!turns(g)) g->spin = 0;
        if (g->n && g->cap) d->nring = k + 1u;
    }
    int32_t t = total_slots(d);
    if (d->focus >= t) d->focus = t ? t - 1 : -1;
    if (d->hot >= t) d->hot = -1;
}

uint32_t zui_dock_slots(const zui_dock_t *d, zui_dock_slot_t *out, uint32_t cap)
{
    frame_t f = frame_of(d);
    uint32_t n = 0;
    for (uint32_t k = 0; k < d->nring; k++) {
        const zui_dock_ring_t *g = &d->ring[k];
        for (uint32_t j = 0; j < shown(g); j++, n++) {
            if (!out || n >= cap) continue;
            zui_dock_slot_t *o = &out[n];
            o->ring = (uint8_t) k;
            o->slot = (uint8_t) j;
            o->item = g->item[(g->rot + j) % g->n];
            at_angle(&f, g->r, slot_angle(&f, g, (int32_t) j * 256), &o->cx, &o->cy);
        }
    }
    return n;
}

bool zui_dock_slot(const zui_dock_t *d, int32_t slot, zui_dock_slot_t *out)
{
    zui_dock_slot_t all[ZUI_DOCK_RINGS * ZUI_DOCK_SLOTS];
    uint32_t n = zui_dock_slots(d, all, ZUI_DOCK_RINGS * ZUI_DOCK_SLOTS);
    if (slot < 0 || (uint32_t) slot >= n) return false;
    *out = all[slot];
    return true;
}

static int32_t outer(const zui_dock_t *d)
{
    if (!d->nring) return d->seal + 16;
    return d->ring[d->nring - 1u].r + d->icon / 2 + 12;
}

static int64_t dist2(const zui_dock_t *d, int32_t x, int32_t y)
{
    frame_t f = frame_of(d);
    int64_t dx = x - f.ox, dy = y - f.oy;
    return dx * dx + dy * dy;
}

zui_rect_t zui_dock_keepout(const zui_dock_t *d)
{
    frame_t f = frame_of(d);
    int32_t s = d->seal;
    zui_rect_t r = {f.su > 0 ? f.ox : f.ox - s, f.sv > 0 ? f.oy - s : f.oy, s, s};
    if (f.half) r = (zui_rect_t){f.ox - s, f.oy - s, 2 * s, s};
    return r;
}

bool zui_dock_is_open(const zui_dock_t *d)
{
    return d->phase == ZUI_DOCK_BLOOMING || d->phase == ZUI_DOCK_OPEN;
}

int zui_dock_hit(const zui_dock_t *d, int32_t x, int32_t y, int32_t *slot)
{
    if (slot) *slot = -1;
    if (x < 0 || y < 0 || x >= d->w || y >= d->h) return ZUI_DOCK_HIT_NONE;
    int64_t r2 = dist2(d, x, y), s = d->seal;
    if (r2 < s * s) return ZUI_DOCK_HIT_SEAL;
    if (!zui_dock_is_open(d)) return ZUI_DOCK_HIT_NONE;
    zui_dock_slot_t all[ZUI_DOCK_RINGS * ZUI_DOCK_SLOTS];
    uint32_t n = zui_dock_slots(d, all, ZUI_DOCK_RINGS * ZUI_DOCK_SLOTS);
    int64_t t = d->icon / 2 + ZUI_DOCK_PAD;
    for (uint32_t i = 0; i < n; i++) {
        int64_t dx = x - all[i].cx, dy = y - all[i].cy;
        if (dx * dx + dy * dy <= t * t) {
            if (slot) *slot = (int32_t) i;
            return ZUI_DOCK_HIT_SLOT;
        }
    }
    int64_t o = outer(d);
    return r2 <= o * o ? ZUI_DOCK_HIT_PANEL : ZUI_DOCK_HIT_NONE;
}

/* ---- the apps ------------------------------------------------------------ */

static int32_t pinned_at(const zui_dock_t *d, const char *app)
{
    for (uint32_t i = 0; i < d->npinned; i++)
        if (same(d->pinned[i], app)) return (int32_t) i;
    return -1;
}

static void rebuild(zui_dock_t *d)
{
    d->nitem = d->nopen;
    for (uint32_t i = 0; i < d->nopen; i++) d->item[i].pinned = pinned_at(d, d->item[i].app) >= 0;
    for (uint32_t p = 0; p < d->npinned && d->nitem < ZUI_DOCK_ITEMS; p++) {
        bool open = false;
        for (uint32_t i = 0; i < d->nopen && !open; i++) open = same(d->item[i].app, d->pinned[p]);
        if (open) continue;
        zui_dock_item_t *it = &d->item[d->nitem++];
        mset(it, sizeof *it);
        copy(it->app, sizeof it->app, d->pinned[p]);
        copy(it->title, sizeof it->title, d->pinned_title[p]);
        it->pinned = true;
        it->activity = ZUI_DOCK_STOPPED;
    }
    layout(d);
}

void zui_dock_set_open(zui_dock_t *d, const zui_dock_item_t *open, uint32_t n)
{
    if (n > ZUI_DOCK_ITEMS) n = ZUI_DOCK_ITEMS;
    for (uint32_t i = 0; i < n; i++) {
        d->item[i] = open[i];
        if (!d->item[i].title[0]) copy(d->item[i].title, ZUI_DOCK_NAME, d->item[i].app);
        if (d->item[i].activity == ZUI_DOCK_STOPPED) d->item[i].activity = ZUI_DOCK_IDLE;
    }
    d->nopen = n;
    rebuild(d);
}

bool zui_dock_pin(zui_dock_t *d, const char *app, const char *title)
{
    if (!app || !app[0]) return false;
    if (pinned_at(d, app) >= 0) return true;
    if (d->npinned >= ZUI_DOCK_PINNED) return false;
    copy(d->pinned[d->npinned], ZUI_DOCK_NAME, app);
    copy(d->pinned_title[d->npinned], ZUI_DOCK_NAME, title && title[0] ? title : app);
    d->npinned++;
    rebuild(d);
    return true;
}

bool zui_dock_unpin(zui_dock_t *d, const char *app)
{
    int32_t at = pinned_at(d, app);
    if (at < 0) return false;
    for (uint32_t i = (uint32_t) at; i + 1u < d->npinned; i++) {
        copy(d->pinned[i], ZUI_DOCK_NAME, d->pinned[i + 1u]);
        copy(d->pinned_title[i], ZUI_DOCK_NAME, d->pinned_title[i + 1u]);
    }
    d->npinned--;
    rebuild(d);
    return true;
}

/* ---- set up -------------------------------------------------------------- */

void zui_dock_init(zui_dock_t *d, int32_t w, int32_t h, bool touch)
{
    mset(d, sizeof *d);
    d->touch = touch;
    d->seal = ZUI_DOCK_SEAL;
    d->icon = touch ? ZUI_DOCK_ICON_T : ZUI_DOCK_ICON;
    d->focus = d->hot = d->chosen = -1;
    d->press = -2;
    d->leave = -1;
    d->chord_key = 'd';
    d->chord_mods = ZUI_MOD_CMD | ZUI_MOD_SHIFT;
    zui_dock_resize(d, w, h, w < 600);
}

void zui_dock_resize(zui_dock_t *d, int32_t w, int32_t h, bool compact)
{
    d->w = imax(w, 1);
    d->h = imax(h, 1);
    d->compact = compact;
    layout(d);
}

void zui_dock_place(zui_dock_t *d, zui_dock_corner_t corner, int32_t seal, bool autohide)
{
    d->corner = corner > ZUI_DOCK_TOP_RIGHT ? ZUI_DOCK_BOTTOM_LEFT : corner;
    d->seal = imin(imax(seal, ZUI_DOCK_SEAL_MIN), ZUI_DOCK_SEAL_MAX);
    d->autohide = autohide;
    layout(d);
}

zui_dock_corner_t zui_dock_corner_for_edge(int panel_pos)
{
    switch (panel_pos) {
    case 1: /* left */
        return ZUI_DOCK_TOP_LEFT;
    case 2: /* right */
        return ZUI_DOCK_BOTTOM_RIGHT;
    case 3: /* top */
        return ZUI_DOCK_TOP_RIGHT;
    default:
        return ZUI_DOCK_BOTTOM_LEFT;
    }
}

/* ---- the bloom (DK2, DK4, DK5) ------------------------------------------ */

static void moved(zui_dock_t *d, int32_t slot)
{
    if (slot >= 0 && d->on_move) d->on_move(d, slot, d->on_move_ctx);
}

void zui_dock_bloom(zui_dock_t *d, bool open, bool by_keyboard)
{
    bool instant = zui_motion_duration(ZUI_DOCK_BLOOM_MS) == 0;
    d->press = -2;
    d->leave = -1;
    d->hot = -1;
    d->scroll = 0;
    if (open) {
        if (!zui_dock_is_open(d)) d->phase = instant ? ZUI_DOCK_OPEN : ZUI_DOCK_BLOOMING;
        if (instant) d->t = ZUI_DOCK_BLOOM_MS;
        d->kbd = d->sticky = by_keyboard;
        d->focus = by_keyboard && total_slots(d) ? 0 : -1;
        if (by_keyboard) moved(d, d->focus);
        return;
    }
    d->kbd = d->sticky = false;
    d->focus = -1;
    if (zui_dock_is_open(d)) d->phase = instant ? ZUI_DOCK_REST : ZUI_DOCK_CLOSING;
    if (instant) d->t = 0;
}

bool zui_dock_turn(zui_dock_t *d, uint32_t ring, int32_t steps)
{
    if (ring >= ZUI_DOCK_RINGS || !turns(&d->ring[ring]) || !steps) return false;
    zui_dock_ring_t *g = &d->ring[ring];
    int32_t n = g->n, r = ((int32_t) g->rot + steps) % n;
    g->rot = (uint16_t) (r < 0 ? r + n : r);
    g->spin_dir = steps > 0 ? 1 : -1;
    g->spin = zui_motion_duration(ZUI_DOCK_SPIN_MS) ? ZUI_DOCK_SPIN_MS : 0;
    return true;
}

static int choose(zui_dock_t *d, int32_t slot)
{
    zui_dock_slot_t s;
    if (!zui_dock_slot(d, slot, &s)) return ZUI_DOCK_USED;
    d->chosen = s.item;
    zui_dock_bloom(d, false, false);
    return ZUI_DOCK_CHOSE;
}

const zui_dock_item_t *zui_dock_chosen(const zui_dock_t *d)
{
    return d->chosen >= 0 && (uint32_t) d->chosen < d->nitem ? &d->item[d->chosen] : 0;
}

/* The ring a slot index is on, and its place on that ring. */
static bool locate(const zui_dock_t *d, int32_t slot, uint32_t *ring, int32_t *j)
{
    for (uint32_t k = 0; k < d->nring; k++) {
        int32_t n = (int32_t) shown(&d->ring[k]);
        if (slot < n) {
            *ring = k;
            *j = slot;
            return slot >= 0;
        }
        slot -= n;
    }
    return false;
}

static int32_t first_of(const zui_dock_t *d, uint32_t ring)
{
    int32_t s = 0;
    for (uint32_t k = 0; k < ring; k++) s += (int32_t) shown(&d->ring[k]);
    return s;
}

static void focus_to(zui_dock_t *d, int32_t slot)
{
    d->focus = slot;
    moved(d, slot);
}

/* DK8: Up/Down to the nearest slot on the next ring out (out) or in. */
static void across(zui_dock_t *d, bool out)
{
    uint32_t k;
    int32_t j;
    if (!locate(d, d->focus, &k, &j)) return;
    int32_t nk = (int32_t) k + (out ? 1 : -1);
    while (nk >= 0 && nk < (int32_t) d->nring && !shown(&d->ring[nk])) nk += out ? 1 : -1;
    if (nk < 0 || nk >= (int32_t) d->nring) return;
    zui_dock_slot_t from, to;
    zui_dock_slot(d, d->focus, &from);
    int32_t best = -1;
    int64_t bd = 0;
    int32_t a = first_of(d, (uint32_t) nk), n = (int32_t) shown(&d->ring[nk]);
    for (int32_t i = 0; i < n; i++) {
        zui_dock_slot(d, a + i, &to);
        int64_t dx = to.cx - from.cx, dy = to.cy - from.cy, dd = dx * dx + dy * dy;
        if (best < 0 || dd < bd) best = a + i, bd = dd;
    }
    focus_to(d, best);
}

static void along(zui_dock_t *d, int32_t by)
{
    uint32_t k;
    int32_t j;
    if (!locate(d, d->focus, &k, &j)) return;
    int32_t n = (int32_t) shown(&d->ring[k]), nj = j + by;
    if (nj < 0 || nj >= n) {
        if (zui_dock_turn(d, k, by)) moved(d, d->focus); /* a new app under focus */
        return;
    }
    focus_to(d, d->focus + by);
}

static int key(zui_dock_t *d, const zui_event_t *e)
{
    uint8_t mods = e->mods & (ZUI_MOD_CMD | ZUI_MOD_SHIFT | ZUI_MOD_ALT | ZUI_MOD_CTRL);
    uint16_t k = e->key >= 'A' && e->key <= 'Z' ? (uint16_t) (e->key + 32) : e->key;
    if (e->type == ZUI_EV_KEY_DOWN && d->chord_key && k == d->chord_key && mods == d->chord_mods) {
        zui_dock_bloom(d, !(zui_dock_is_open(d) && d->kbd), true);
        return ZUI_DOCK_USED;
    }
    if (!zui_dock_is_open(d) || !d->kbd) return ZUI_DOCK_IGNORED;
    if (e->type != ZUI_EV_KEY_DOWN) return ZUI_DOCK_USED;
    int32_t total = total_slots(d);
    bool shift = (e->mods & ZUI_MOD_SHIFT) != 0;
    bool up_out =
        d->compact || d->corner == ZUI_DOCK_BOTTOM_LEFT || d->corner == ZUI_DOCK_BOTTOM_RIGHT;
    if (k == ZUI_KEY_ESCAPE || k == ZUI_KEY_BACK) {
        zui_dock_bloom(d, false, false);
        return ZUI_DOCK_USED;
    }
    if (!total) return ZUI_DOCK_USED;
    if (d->focus < 0 && k != ZUI_KEY_ENTER && k != ZUI_KEY_SPACE) {
        focus_to(d, 0);
        return ZUI_DOCK_USED;
    }
    switch (k) {
    case ZUI_KEY_ENTER:
    case ZUI_KEY_SPACE:
        return d->focus >= 0 ? choose(d, d->focus) : ZUI_DOCK_USED;
    case ZUI_KEY_LEFT:
        along(d, -1);
        break;
    case ZUI_KEY_RIGHT:
        along(d, 1);
        break;
    case ZUI_KEY_UP:
        across(d, up_out);
        break;
    case ZUI_KEY_DOWN:
        across(d, !up_out);
        break;
    case ZUI_KEY_TAB:
        focus_to(d, shift ? (d->focus + total - 1) % total : (d->focus + 1) % total);
        break;
    case ZUI_KEY_HOME:
    case ZUI_KEY_END: {
        uint32_t ring;
        int32_t j;
        if (locate(d, d->focus, &ring, &j)) {
            int32_t a = first_of(d, ring);
            focus_to(d, k == ZUI_KEY_HOME ? a : a + (int32_t) shown(&d->ring[ring]) - 1);
        }
        break;
    }
    default:
        break;
    }
    return ZUI_DOCK_USED;
}

static void hover(zui_dock_t *d, int h, int32_t slot)
{
    int32_t nh = h == ZUI_DOCK_HIT_SLOT ? slot : -1;
    if (nh != d->hot) {
        d->hot = nh;
        moved(d, nh);
    }
}

static int ptr_move(zui_dock_t *d, int32_t x, int32_t y, bool touch)
{
    int32_t slot;
    int h = zui_dock_hit(d, x, y, &slot);
    d->near = h == ZUI_DOCK_HIT_SEAL;
    if (!zui_dock_is_open(d)) {
        if (h == ZUI_DOCK_HIT_SEAL && !touch && d->press == -2) {
            zui_dock_bloom(d, true, false);
            return ZUI_DOCK_USED;
        }
        return ZUI_DOCK_IGNORED;
    }
    hover(d, h, slot);
    int64_t o = outer(d) + 12;
    bool in = dist2(d, x, y) <= o * o;
    if (in)
        d->leave = -1;
    else if (!d->sticky && d->press == -2 && d->leave < 0)
        d->leave = ZUI_DOCK_LEAVE_MS;
    return in || d->press != -2 ? ZUI_DOCK_USED : ZUI_DOCK_IGNORED;
}

static int ptr_down(zui_dock_t *d, int32_t x, int32_t y, bool touch)
{
    int32_t slot;
    int h = zui_dock_hit(d, x, y, &slot);
    if (!zui_dock_is_open(d)) {
        if (h != ZUI_DOCK_HIT_SEAL) return ZUI_DOCK_IGNORED;
        zui_dock_bloom(d, true, false);
        d->sticky = touch;
        d->press = -1;
        d->toggle = false; /* a press that opened it: letting go keeps it open */
        return ZUI_DOCK_USED;
    }
    d->leave = -1;
    if (h == ZUI_DOCK_HIT_SEAL) {
        d->press = -1;
        d->toggle = true; /* pressed while open: letting go here closes */
        return ZUI_DOCK_USED;
    }
    if (h == ZUI_DOCK_HIT_SLOT || h == ZUI_DOCK_HIT_PANEL) {
        d->press = h == ZUI_DOCK_HIT_SLOT ? slot : -3;
        hover(d, h, slot);
        return ZUI_DOCK_USED;
    }
    bool keep = d->sticky; /* a sticky bloom eats the press that closes it */
    zui_dock_bloom(d, false, false);
    return keep ? ZUI_DOCK_USED : ZUI_DOCK_IGNORED;
}

static int ptr_up(zui_dock_t *d, int32_t x, int32_t y)
{
    int32_t p = d->press, slot;
    bool closing = d->toggle;
    d->press = -2;
    if (p == -2) return ZUI_DOCK_IGNORED;
    int h = zui_dock_hit(d, x, y, &slot);
    if (h == ZUI_DOCK_HIT_SLOT) return choose(d, slot);
    if (p == -1 && h == ZUI_DOCK_HIT_SEAL && closing) zui_dock_bloom(d, false, false);
    return ZUI_DOCK_USED;
}

static int scroll(zui_dock_t *d, const zui_event_t *e)
{
    if (!zui_dock_is_open(d)) return ZUI_DOCK_IGNORED;
    int h = zui_dock_hit(d, e->x, e->y, 0);
    if (h == ZUI_DOCK_HIT_NONE) return ZUI_DOCK_IGNORED;
    int64_t r2 = dist2(d, e->x, e->y);
    int32_t ring = -1;
    for (uint32_t k = 0; k < d->nring; k++) {
        int64_t lo = d->ring[k].r - d->icon / 2 - 7, hi = d->ring[k].r + d->icon / 2 + 7;
        if (turns(&d->ring[k]) && r2 >= lo * lo && r2 <= hi * hi) ring = (int32_t) k;
    }
    for (uint32_t k = d->nring; ring < 0 && k > 0; k--)
        if (turns(&d->ring[k - 1u])) ring = (int32_t) k - 1;
    if (ring < 0) return ZUI_DOCK_USED;
    d->scroll += e->dy ? e->dy : e->dx;
    while (iabs(d->scroll) >= ZUI_DOCK_SCROLL) {
        int32_t s = d->scroll > 0 ? 1 : -1;
        d->scroll -= s * ZUI_DOCK_SCROLL;
        zui_dock_turn(d, (uint32_t) ring, s);
    }
    return ZUI_DOCK_USED;
}

static int pointer(zui_dock_t *d, uint8_t type, int32_t x, int32_t y, bool touch)
{
    switch (type) {
    case ZUI_EV_PTR_DOWN:
        return ptr_down(d, x, y, touch);
    case ZUI_EV_PTR_MOVE:
        return ptr_move(d, x, y, touch);
    default:
        return ptr_up(d, x, y);
    }
}

int zui_dock_event(zui_dock_t *d, const zui_event_t *e)
{
    d->chosen = -1;
    switch (e->type) {
    case ZUI_EV_KEY_DOWN:
    case ZUI_EV_KEY_UP:
        return key(d, e);
    case ZUI_EV_TEXT:
    case ZUI_EV_PREEDIT:
        return zui_dock_is_open(d) && d->kbd ? ZUI_DOCK_USED : ZUI_DOCK_IGNORED;
    case ZUI_EV_TOUCH_BEGIN:
    case ZUI_EV_TOUCH_MOVE:
    case ZUI_EV_TOUCH_END:
    case ZUI_EV_TOUCH_CANCEL: {
        if (e->touch_id) return ZUI_DOCK_IGNORED; /* a second finger */
        uint8_t t = e->type == ZUI_EV_TOUCH_BEGIN  ? ZUI_EV_PTR_DOWN
                    : e->type == ZUI_EV_TOUCH_MOVE ? ZUI_EV_PTR_MOVE
                                                   : ZUI_EV_PTR_UP;
        d->tev = t; /* zui_input.h I2: its pointer twin follows; drop that */
        d->tx = e->x;
        d->ty = e->y;
        d->tms = e->ms;
        return pointer(d, t, e->x, e->y, true);
    }
    case ZUI_EV_PTR_DOWN:
    case ZUI_EV_PTR_MOVE:
    case ZUI_EV_PTR_UP:
        if (d->tev == e->type && d->tx == e->x && d->ty == e->y && d->tms == e->ms) {
            d->tev = 0;
            return zui_dock_is_open(d) || d->press != -2 ? ZUI_DOCK_USED : ZUI_DOCK_IGNORED;
        }
        d->tev = 0;
        return pointer(d, e->type, e->x, e->y, false);
    case ZUI_EV_SCROLL:
        return scroll(d, e);
    case ZUI_EV_RESIZE:
        zui_dock_resize(d, e->x, e->y, e->x < 600);
        return ZUI_DOCK_IGNORED; /* the owner resizes too */
    default:
        return ZUI_DOCK_IGNORED;
    }
}

void zui_dock_tick(zui_dock_t *d, uint32_t ms)
{
    d->clock += ms;
    uint32_t dt = zui_motion_step(ms, ZUI_DOCK_BLOOM_MS);
    if (d->phase == ZUI_DOCK_BLOOMING) {
        d->t += dt;
        if (d->t >= ZUI_DOCK_BLOOM_MS) {
            d->t = ZUI_DOCK_BLOOM_MS;
            d->phase = ZUI_DOCK_OPEN;
        }
    } else if (d->phase == ZUI_DOCK_CLOSING) {
        d->t = d->t > dt ? d->t - dt : 0;
        if (!d->t) d->phase = ZUI_DOCK_REST;
    }
    if (d->leave >= 0 && zui_dock_is_open(d)) {
        d->leave -= (int32_t) (ms > 100000u ? 100000u : ms);
        if (d->leave <= 0) zui_dock_bloom(d, false, false);
    }
    uint32_t ds = zui_motion_step(ms, ZUI_DOCK_SPIN_MS);
    for (uint32_t k = 0; k < ZUI_DOCK_RINGS; k++) {
        zui_dock_ring_t *g = &d->ring[k];
        g->spin = (uint32_t) g->spin > ds ? g->spin - (int32_t) ds : 0;
    }
}

/* ---- speech (DK9) -------------------------------------------------------- */

typedef struct {
    char *out;
    uint32_t cap, n;
} sb_t;

static void sb_s(sb_t *b, const char *s)
{
    while (s && *s && b->n + 1u < b->cap) b->out[b->n++] = *s++;
    if (b->cap) b->out[b->n < b->cap ? b->n : b->cap - 1u] = 0;
}

static void sb_u(sb_t *b, uint32_t v)
{
    char num[12];
    uint32_t k = 0;
    do {
        num[k++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v && k < sizeof num);
    char rev[12];
    for (uint32_t i = 0; i < k; i++) rev[i] = num[k - 1u - i];
    rev[k] = 0;
    sb_s(b, rev);
}

static const char *state_word(uint8_t a)
{
    return a == ZUI_DOCK_ASK    ? "needs input"
           : a == ZUI_DOCK_BUSY ? "running"
           : a == ZUI_DOCK_IDLE ? "open"
                                : "not running";
}

static void say_item(sb_t *b, const zui_dock_item_t *it)
{
    sb_s(b, it->title[0] ? it->title : it->app);
    sb_s(b, ", ");
    sb_s(b, state_word(it->activity));
    if (it->minimised) sb_s(b, ", minimised");
    if (it->front) sb_s(b, ", in front");
}

uint32_t zui_dock_say(const zui_dock_t *d, int32_t slot, char *out, uint32_t cap)
{
    sb_t b = {out, cap, 0};
    if (cap) out[0] = 0;
    zui_dock_slot_t s;
    if (zui_dock_slot(d, slot, &s)) say_item(&b, &d->item[s.item]);
    return b.n;
}

static uint32_t asking(const zui_dock_t *d)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < d->nopen; i++) n += d->item[i].activity == ZUI_DOCK_ASK;
    return n;
}

uint32_t zui_dock_a11y(const zui_dock_t *d, char *out, uint32_t cap)
{
    sb_t b = {out, cap, 0};
    if (cap) out[0] = 0;
    if (!zui_dock_is_open(d)) {
        uint32_t n = asking(d);
        sb_s(&b, "button \"Apps");
        if (n) {
            sb_s(&b, ", ");
            sb_u(&b, n);
            sb_s(&b, n == 1u ? " needs input" : " need input");
        }
        sb_s(&b, "\" collapsed\n");
        return b.n;
    }
    sb_s(&b, "toolbar \"Apps\" expanded\n");
    zui_dock_slot_t all[ZUI_DOCK_RINGS * ZUI_DOCK_SLOTS];
    uint32_t n = zui_dock_slots(d, all, ZUI_DOCK_RINGS * ZUI_DOCK_SLOTS);
    for (uint32_t i = 0; i < n; i++) {
        sb_s(&b, "  button \"");
        say_item(&b, &d->item[all[i].item]);
        sb_s(&b, "\"");
        if (d->kbd && (int32_t) i == d->focus) sb_s(&b, " focused");
        sb_s(&b, "\n");
    }
    return b.n;
}

/* ---- drawing (DK7, DK10) ------------------------------------------------- */

static zui_rect_t square(int32_t cx, int32_t cy, int32_t r)
{
    zui_rect_t q = {cx - r, cy - r, 2 * r, 2 * r};
    return q;
}

static void disc(zui_surface_t *s, int32_t cx, int32_t cy, int32_t r, uint32_t rgb, uint32_t a)
{
    if (r > 0 && a) zui_fill_rrect(s, square(cx, cy, r), r, rgb, a);
}

static void ring_line(zui_surface_t *s, int32_t cx, int32_t cy, int32_t r, int32_t wd, uint32_t rgb,
                      uint32_t a)
{
    if (r > wd && a) zui_stroke_rrect(s, square(cx, cy, r), r, wd, rgb, a);
}

/* A carved circle: the shade inside, the light tint just outside. */
static void carve(zui_surface_t *s, int32_t cx, int32_t cy, int32_t r, uint32_t a_lo, uint32_t a_hi)
{
    ring_line(s, cx, cy, r, 1, ZUI_ENGRAVE_LO, a_lo);
    ring_line(s, cx, cy, r + 1, 1, ZUI_ENGRAVE_HI, a_hi);
}

static uint32_t scale(uint32_t a, uint32_t lit)
{
    return (a * (lit > 256u ? 256u : lit)) >> 8;
}

/* 0..256..0 over one period: the pips' pulse (DK7). Still with reduced motion. */
static uint32_t pulse(uint32_t clock, uint32_t period)
{
    if (zui_motion_style() != ZUI_MOTION_FULL || !period) return 256u;
    uint32_t p = clock % period, h = period / 2u;
    return p < h ? p * 256u / h : (period - p) * 256u / h;
}

static void pip(const zui_dock_t *d, zui_surface_t *s, int32_t x, int32_t y, uint8_t act,
                uint32_t lit)
{
    int32_t r = d->touch ? 4 : 3;
    switch (act) {
    case ZUI_DOCK_ASK: {
        uint32_t q = pulse(d->clock, ZUI_DOCK_FAST_MS);
        disc(s, x, y, r, ZUI_GOLD, scale(255u, lit));
        int32_t hr = r + 3 + (int32_t) (q * 2u / 256u);
        ring_line(
            s, x, y, hr, 1, ZUI_GOLD,
            scale(zui_motion_style() == ZUI_MOTION_FULL ? 255u - q * 160u / 256u : 150u, lit));
        break;
    }
    case ZUI_DOCK_BUSY:
        disc(s, x, y, r, ZUI_OK,
             scale(150u + pulse(d->clock, ZUI_DOCK_MEDIUM_MS) * 105u / 256u, lit));
        break;
    case ZUI_DOCK_IDLE:
        disc(s, x, y, r, ZUI_MUTED,
             scale(110u + pulse(d->clock, ZUI_DOCK_SLOW_MS) * 110u / 256u, lit));
        break;
    default:
        ring_line(s, x, y, r, 1, ZUI_FAINT, scale(220u, lit));
        break;
    }
}

/* The first character of a title, as UTF-8 (upper case for ASCII). */
static void initial(const char *t, char out[5])
{
    uint32_t n = 0;
    if (t[0]) {
        out[n++] = t[0] >= 'a' && t[0] <= 'z' ? (char) (t[0] - 32) : t[0];
        while (n < 4u && (t[n] & 0xC0) == 0x80) out[n] = t[n], n++;
    }
    out[n] = 0;
}

static void default_icon(const zui_dock_t *d, zui_surface_t *s, zui_rect_t box,
                         const zui_dock_item_t *it, uint32_t lit)
{
    int32_t r = box.w / 2, cx = box.x + r, cy = box.y + r;
    disc(s, cx, cy, r, ZUI_RAISED, scale(255u, lit));
    zui_engrave(s, box, r, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, scale(70u, lit));
    if (!it) { /* the seal's mark: an alidade over a small limb */
        ring_line(s, cx, cy, r / 2, 1, ZUI_TEXT_2, scale(255u, lit));
        disc(s, cx, cy, imax(1, r / 6), ZUI_TEXT_2, scale(255u, lit));
        return;
    }
    if (d->text && d->text->font) {
        char c[5];
        initial(it->title[0] ? it->title : it->app, c);
        int32_t tw = zui_text_width(d->text, c);
        int32_t base = cy + (zui_text_ascent(d->text) + zui_text_descent(d->text)) / 2;
        zui_text_a(s, d->text, cx - tw / 2, base, ZUI_TEXT, scale(255u, lit), c);
    } else {
        ring_line(s, cx, cy, imax(2, r / 3), 1, ZUI_TEXT_2, scale(220u, lit));
    }
}

static void icon(const zui_dock_t *d, zui_surface_t *s, zui_rect_t box, const zui_dock_item_t *it,
                 uint32_t lit)
{
    if (d->icon_fn)
        d->icon_fn(s, box, it, lit, d->icon_ctx);
    else
        default_icon(d, s, box, it, lit);
}

static uint32_t eased(const zui_dock_t *d)
{
    if (d->phase == ZUI_DOCK_REST) return 0;
    return zui_motion_ease_q8(d->t * 256u / ZUI_DOCK_BLOOM_MS);
}

static int32_t grow(const zui_dock_t *d, int32_t r, uint32_t e)
{
    return d->seal + (int32_t) (((int64_t) (r - d->seal) * e) >> 8);
}

static void seal(const zui_dock_t *d, zui_surface_t *s, const frame_t *f, bool open)
{
    bool ask = asking(d) > 0;
    uint32_t a = d->autohide && !d->near && !ask && !open ? 90u : 255u;
    int32_t r = d->seal;
    disc(s, f->ox, f->oy, r, ZUI_PANEL, a);
    carve(s, f->ox, f->oy, r, a, scale(90u, a + 1u));
    ring_line(s, f->ox, f->oy, r - 8, 1, ZUI_HAIRLINE, a);
    int32_t span = f->half ? HALF : QUARTER;
    for (int32_t g = ZUI_DOCK_TURN / 24; g < span; g += ZUI_DOCK_TURN / 24) {
        int32_t x, y; /* the limb's graduations, every 15 degrees */
        at_angle(f, r - 4, g, &x, &y);
        zui_fill_a(s, (zui_rect_t){x - 1, y - 1, 2, 2}, ZUI_EDGE, a);
    }
    int32_t mid = span / 2, mx, my, m = r * 11 / 20;
    at_angle(f, r / 2 + (f->half ? 4 : 0), mid, &mx, &my);
    icon(d, s, (zui_rect_t){mx - m / 2, my - m / 2, m, m}, 0, a + 1u);
    if (ask && !open) { /* DK7: the gold tick on the rim */
        uint32_t q = pulse(d->clock, ZUI_DOCK_FAST_MS);
        uint32_t ta = zui_motion_style() == ZUI_MOTION_FULL ? 150u + q * 105u / 256u : 255u;
        for (int32_t k = r - 7; k <= r + 3; k += 2) {
            int32_t x, y;
            at_angle(f, k, mid, &x, &y);
            disc(s, x, y, 2, ZUI_GOLD, ta);
        }
    }
}

static int32_t front_slot(const zui_dock_t *d)
{
    zui_dock_slot_t all[ZUI_DOCK_RINGS * ZUI_DOCK_SLOTS];
    uint32_t n = zui_dock_slots(d, all, ZUI_DOCK_RINGS * ZUI_DOCK_SLOTS);
    for (uint32_t i = 0; i < n; i++)
        if (d->item[all[i].item].front) return (int32_t) i;
    return -1;
}

static void label(const zui_dock_t *d, zui_surface_t *s, const frame_t *f, int32_t slot)
{
    zui_dock_slot_t sl;
    if (!d->text || !d->text->font || !zui_dock_slot(d, slot, &sl)) return;
    const zui_dock_item_t *it = &d->item[sl.item];
    const char *t = it->title[0] ? it->title : it->app;
    int32_t tw = imin(zui_text_width(d->text, t), 240), pad = ZUI_S2;
    int32_t th = zui_text_ascent(d->text) - zui_text_descent(d->text) + 2 * ZUI_S1;
    int32_t bw = tw + 2 * pad, o = outer(d) + 10, x, y;
    if (f->half) {
        x = f->ox - bw / 2;
        y = f->oy - o - th;
    } else {
        const zui_dock_ring_t *g = &d->ring[sl.ring];
        at_angle(f, o, slot_angle(f, g, sl.slot * 256), &x, &y);
        if (f->su < 0) x -= bw;
        y -= th / 2;
    }
    x = imin(imax(x, ZUI_S1), d->w - bw - ZUI_S1);
    y = imin(imax(y, ZUI_S1), d->h - th - ZUI_S1);
    zui_rect_t tag = {x, y, bw, th};
    zui_fill_rrect(s, tag, ZUI_R_CONTROL, ZUI_PANEL, 255);
    zui_engrave(s, tag, ZUI_R_CONTROL, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 70u);
    zui_text_fit(s, d->text, x + pad, y + ZUI_S1 + zui_text_ascent(d->text), tw, ZUI_TEXT, t);
}

static void ring_apps(const zui_dock_t *d, zui_surface_t *s, const frame_t *f, uint32_t k,
                      uint32_t e, int32_t base, int32_t active)
{
    const zui_dock_ring_t *g = &d->ring[k];
    int32_t n = (int32_t) shown(g), R = grow(d, g->r, e), half = d->icon / 2;
    if (!n) return;
    carve(s, f->ox, f->oy, R, scale(k == 2u ? 150u : 255u, e), scale(k == 2u ? 26u : 54u, e));
    for (int32_t j = 0; j + 1 < n; j++) { /* graduations between the slots */
        int32_t x, y;
        at_angle(f, R, slot_angle(f, g, j * 256 + 128), &x, &y);
        disc(s, x, y, 1, ZUI_EDGE, scale(200u, e));
    }
    for (int32_t k2 = 0; turns(g) && k2 < 3; k2++) { /* DK4: the ring goes on: fading marks */
        int32_t x, y, at0 = -96 - k2 * 40, at1 = (n - 1) * 256 + 96 + k2 * 40;
        uint32_t a = scale(220u - (uint32_t) k2 * 70u, e);
        at_angle(f, R, slot_angle(f, g, at0), &x, &y);
        disc(s, x, y, 2, ZUI_TEXT_2, a);
        at_angle(f, R, slot_angle(f, g, at1), &x, &y);
        disc(s, x, y, 2, ZUI_TEXT_2, a);
    }
    bool spin = turns(g) && g->spin > 0;
    int32_t off = spin ? g->spin_dir * g->spin * 256 / ZUI_DOCK_SPIN_MS : 0;
    for (int32_t j = spin ? -1 : 0; j <= (spin ? n : n - 1); j++) {
        int32_t p = j * 256 + off; /* where it is drawn, in slot units * 256 */
        int32_t over = p < 0 ? -p : p > (n - 1) * 256 ? p - (n - 1) * 256 : 0;
        if (over >= 256) continue;
        uint32_t fade = (uint32_t) (256 - over);
        int32_t idx = ((int32_t) g->rot + j) % (int32_t) g->n;
        if (idx < 0) idx += g->n;
        const zui_dock_item_t *it = &d->item[g->item[idx]];
        int32_t a = slot_angle(f, g, p), cx, cy, px, py;
        at_angle(f, R, a, &cx, &cy);
        uint32_t lit = scale(fade, e);
        lit = it->minimised ? lit * 150u / 256u : it->win ? lit : lit * 210u / 256u;
        zui_rect_t box = {cx - half, cy - half, d->icon, d->icon};
        icon(d, s, box, it, lit);
        at_angle(f, R - half - 6, a, &px, &py);
        pip(d, s, px, py, it->activity, scale(fade, e));
        if (it->progress && it->progress < 256u && e >= 256u && fade >= 256u)
            zui_bar_draw(s, (zui_rect_t){cx - half + 4, cy + half + 3, d->icon - 8, 2},
                         ZUI_BAR_GATHER, it->progress, d->clock, 0, 0);
        if (!spin && base + j == active) /* DK10: the one gold element */
            ring_line(s, cx, cy, half + 3, 2, ZUI_GOLD, scale(255u, e));
    }
}

void zui_dock_draw(const zui_dock_t *d, zui_surface_t *s)
{
    frame_t f = frame_of(d);
    uint32_t e = eased(d);
    if (e) {
        int32_t o = grow(d, outer(d), e);
        disc(s, f.ox, f.oy, o, ZUI_PANEL, scale(zui_motion_alpha(236u), e));
        carve(s, f.ox, f.oy, o, scale(255u, e), scale(70u, e));
        int32_t active = d->kbd && d->focus >= 0 ? d->focus : d->hot >= 0 ? d->hot : front_slot(d);
        int32_t base = 0;
        for (uint32_t k = 0; k < d->nring; k++) {
            ring_apps(d, s, &f, k, e, base, active);
            base += (int32_t) shown(&d->ring[k]);
        }
        int32_t named = d->kbd && d->focus >= 0 ? d->focus : d->hot;
        if (e >= 256u && named >= 0) label(d, s, &f, named);
    }
    seal(d, s, &f, e > 0);
}
