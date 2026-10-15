/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_kinetic.c — the kinetic state engine for vector app icons
 * (zui_kinetic.h KI1..KI8). */
#include "zui_kinetic.h"
#include "zui_motion.h"
#include "zui_tokens.h"

#define STEP_MS    4u       /* KI8: fixed spring step             */
#define SNAP_STEPS 1024u    /* a longer gap lands the springs     */
#define FULL_TURN  3600     /* tenths of a degree                 */
#define FIRE_MOD   3600000u /* fire_acc wraps at one turn * 1000  */
#define AIM_SCALE  64       /* aim spring units per tenth degree  */
#define LIFT_EPS   8
#define AIM_EPS    16

static const char *const names[ZUI_KINETIC_GROUPS] = {"chassis", "core-artifact",
                                                      "kinetic-element"};

/* sin(degree) in 16.16 for 0..90 degrees. */
static const int32_t sin_deg[91] = {
    0,     1144,  2287,  3430,  4572,  5712,  6850,  7987,  9121,  10252, 11380, 12505, 13626,
    14742, 15855, 16962, 18064, 19161, 20252, 21336, 22415, 23486, 24550, 25607, 26656, 27697,
    28729, 29753, 30767, 31772, 32768, 33754, 34729, 35693, 36647, 37590, 38521, 39441, 40348,
    41243, 42126, 42995, 43852, 44695, 45525, 46341, 47143, 47930, 48703, 49461, 50203, 50931,
    51643, 52339, 53020, 53684, 54332, 54963, 55578, 56175, 56756, 57319, 57865, 58393, 58903,
    59396, 59870, 60326, 60764, 61183, 61584, 61966, 62328, 62672, 62997, 63303, 63589, 63856,
    64104, 64332, 64540, 64729, 64898, 65048, 65177, 65287, 65376, 65446, 65496, 65526, 65536};

/* atan(i / 64) in sixteenths of a tenth of a degree, i = 0..64. */
static const int32_t atan_tab[65] = {
    0,    143,  286,  429,  572,  715,  857,  999,  1140, 1281, 1421, 1560, 1699,
    1837, 1974, 2110, 2246, 2380, 2513, 2646, 2777, 2907, 3035, 3163, 3289, 3414,
    3538, 3660, 3781, 3900, 4018, 4135, 4250, 4364, 4477, 4588, 4697, 4805, 4912,
    5017, 5121, 5223, 5324, 5423, 5521, 5618, 5713, 5807, 5899, 5990, 6080, 6168,
    6255, 6341, 6425, 6508, 6590, 6670, 6750, 6828, 6904, 6980, 7054, 7128, 7200};

static int32_t iabs(int32_t v)
{
    return v < 0 ? -v : v;
}

static int32_t mulq(int32_t a, int32_t b)
{
    return (int32_t) (((int64_t) a * b + 32768) >> 16);
}

static bool reduced(void)
{
    return zui_motion_get()->reduce_motion;
}

/* KI1: elapsed ms from then to now; a clock that went backwards gives 0. */
static uint32_t since(uint32_t now, uint32_t then)
{
    uint32_t d = now - then;
    return d > 0x7FFFFFFFu ? 0u : d;
}

/* ---- helpers ------------------------------------------------------------- */

int32_t zui_kinetic_sin(int32_t dd)
{
    int32_t a = dd % FULL_TURN;
    if (a < 0) a += FULL_TURN;
    int32_t sign = 1;
    if (a >= 1800) {
        a -= 1800;
        sign = -1;
    }
    if (a > 900) a = 1800 - a;
    int32_t d = a / 10, f = a % 10;
    int32_t v = sin_deg[d];
    if (f) v += ((sin_deg[d + 1] - sin_deg[d]) * f + 5) / 10;
    return sign * v;
}

int32_t zui_kinetic_cos(int32_t dd)
{
    return zui_kinetic_sin(dd + 900);
}

int32_t zui_kinetic_atan2(int32_t dy, int32_t dx)
{
    int64_t ax = dx < 0 ? -(int64_t) dx : dx, ay = dy < 0 ? -(int64_t) dy : dy;
    if (!ax && !ay) return 0;
    while (ax > 32767 || ay > 32767) ax >>= 1, ay >>= 1;
    bool steep = ay > ax;
    uint32_t lo = (uint32_t) (steep ? ax : ay), hi = (uint32_t) (steep ? ay : ax);
    uint32_t r = (lo << 16) / hi; /* 0..65536 */
    uint32_t i = r >> 10, f = r & 1023u;
    int32_t a16 = atan_tab[i];
    if (i < 64) a16 += (int32_t) (((uint32_t) (atan_tab[i + 1] - atan_tab[i]) * f + 512u) >> 10);
    int32_t a = (a16 + 8) >> 4; /* 0..450 */
    if (steep) a = 900 - a;
    if (dx < 0 && dy >= 0)
        a = 1800 - a;
    else if (dx < 0)
        a = 1800 + a;
    else if (dy < 0)
        a = FULL_TURN - a;
    return a % FULL_TURN;
}

uint32_t zui_kinetic_period_ms(zui_kinetic_activity_t a)
{
    switch (a) {
    case ZUI_KINETIC_OPEN_IDLE:
        return ZUI_KINETIC_SLOW_MS;
    case ZUI_KINETIC_RUNNING:
        return ZUI_KINETIC_MEDIUM_MS;
    case ZUI_KINETIC_NEEDS_INPUT:
        return ZUI_KINETIC_FAST_MS;
    default:
        return 0;
    }
}

uint32_t zui_kinetic_fire_rate(uint32_t tokens)
{
    uint32_t cap = ZUI_KINETIC_FIRE_MAX / ZUI_KINETIC_FIRE_DD;
    return tokens >= cap ? ZUI_KINETIC_FIRE_MAX : tokens * ZUI_KINETIC_FIRE_DD;
}

void zui_kinetic_spring_step(zui_kinetic_spring_t *s, int32_t target)
{
    /* x'' = w^2 (t - x) - 2 w x' with w = 25/256 per step (24.4 rad/s):
     * critically damped, semi-implicit Euler, v in x/256 per step. */
    int64_t d = (int64_t) target - s->x;
    int64_t v = s->v;
    v += (d * 625) >> 8;
    v -= (v * 50) >> 8;
    s->v = (int32_t) v;
    s->x += (int32_t) ((v + 128) >> 8);
}

static void spring_snap(zui_kinetic_spring_t *s, int32_t target)
{
    s->x = target;
    s->v = 0;
}

static void spring_land(zui_kinetic_spring_t *s, int32_t target, int32_t eps)
{
    if (iabs(target - s->x) <= eps && iabs(s->v) <= 512) spring_snap(s, target);
}

static bool spring_rest(const zui_kinetic_spring_t *s, int32_t target)
{
    return s->x == target && s->v == 0;
}

/* ---- the instance -------------------------------------------------------- */

static int32_t lift_target(const zui_kinetic_t *k)
{
    return k->state == ZUI_KINETIC_HOVER ? ZUI_KINETIC_LIFT : ZUI_KINETIC_ONE;
}

/* KI4: the aim toward the pointer, tenths of a degree from straight up. */
static int32_t aim_target(const zui_kinetic_t *k)
{
    if (k->state != ZUI_KINETIC_HOVER || !k->pointer_in) return 0;
    int32_t dx = k->ptr_x - k->pivot_x, dy = k->ptr_y - k->pivot_y;
    if (!dx && !dy) return 0;
    int32_t d = zui_kinetic_atan2(dy, dx) - 2700; /* 270 degrees is up */
    if (d < -1800) d += FULL_TURN;
    if (d >= 1800) d -= FULL_TURN;
    if (d > ZUI_KINETIC_AIM_MAX) d = ZUI_KINETIC_AIM_MAX;
    if (d < -ZUI_KINETIC_AIM_MAX) d = -ZUI_KINETIC_AIM_MAX;
    if (k->tier == ZUI_KINETIC_L1)
        d = (d + (d >= 0 ? ZUI_KINETIC_CLICK / 2 : -ZUI_KINETIC_CLICK / 2)) / ZUI_KINETIC_CLICK *
            ZUI_KINETIC_CLICK;
    return d;
}

static int32_t fire_angle(const zui_kinetic_t *k)
{
    int32_t a = (int32_t) (k->fire_acc / 1000u);
    if (k->tier == ZUI_KINETIC_L1) a = a / ZUI_KINETIC_CLICK * ZUI_KINETIC_CLICK;
    return a;
}

/* Ambient motion runs on the core in every state but STALL, and on the
 * kinetic element in IDLE, HOVER and BOTH (FIRING turns it instead). */
static bool ambient_element(zui_kinetic_state_t s)
{
    return s == ZUI_KINETIC_IDLE || s == ZUI_KINETIC_HOVER || s == ZUI_KINETIC_BOTH;
}

static uint32_t halves_at(const zui_kinetic_t *k, uint32_t now)
{
    uint32_t p = zui_kinetic_period_ms(k->act);
    return p ? (now - k->epoch_ms) / (p / 2u) : 0u;
}

/* Bring the clock, the firing angle, the springs and the L1 clicks to now. */
static void advance(zui_kinetic_t *k, uint32_t now)
{
    uint32_t dt = since(now, k->now_ms);
    if (!dt) return;
    k->now_ms = now;
    bool rm = reduced();
    if (k->state == ZUI_KINETIC_FIRING && !rm) {
        /* rate * FIRE_MOD is whole turns, so only dt mod FIRE_MOD matters; the
         * sum stays in 32 bits in chunks of 10^6 ms (rate <= 3600) */
        uint32_t rate = zui_kinetic_fire_rate(k->work), left = dt % FIRE_MOD;
        while (left) {
            uint32_t c = left > 1000000u ? 1000000u : left;
            k->fire_acc = (k->fire_acc + rate * c % FIRE_MOD) % FIRE_MOD;
            left -= c;
        }
    }
    int32_t lt = lift_target(k), at = aim_target(k) * AIM_SCALE;
    if (rm || dt / STEP_MS > SNAP_STEPS) {
        spring_snap(&k->lift, lt);
        spring_snap(&k->aim, rm ? 0 : at);
        k->spring_rem = 0;
    } else {
        uint32_t t = k->spring_rem + dt, n = t / STEP_MS;
        k->spring_rem = t % STEP_MS;
        for (uint32_t i = 0; i < n; i++) {
            if (!spring_rest(&k->lift, lt)) {
                zui_kinetic_spring_step(&k->lift, lt);
                spring_land(&k->lift, lt, LIFT_EPS);
            }
            if (!spring_rest(&k->aim, at)) {
                zui_kinetic_spring_step(&k->aim, at);
                spring_land(&k->aim, at, AIM_EPS);
            }
        }
        if (k->tier == ZUI_KINETIC_L1) spring_snap(&k->aim, at); /* no easing */
    }
    uint32_t h = halves_at(k, now);
    if (k->tier == ZUI_KINETIC_L1 && !rm && ambient_element(k->state) && h != k->halves) {
        int32_t c = (int32_t) ((h - k->halves) % 24u) * ZUI_KINETIC_CLICK;
        k->clicks = (k->clicks + c) % FULL_TURN;
    }
    k->halves = h;
}

void zui_kinetic_init(zui_kinetic_t *k, zui_kinetic_tier_t tier, uint32_t now_ms)
{
    if (!k) return;
    k->state = ZUI_KINETIC_IDLE;
    k->tier = (tier >= ZUI_KINETIC_L1 && tier <= ZUI_KINETIC_L3) ? tier : ZUI_KINETIC_L2;
    k->act = ZUI_KINETIC_NOT_RUNNING;
    k->escrow = false;
    k->visible = true;
    k->dirty = true;
    k->have_last = false;
    k->pointer_in = false;
    k->now_ms = k->entered_ms = k->epoch_ms = now_ms;
    k->spring_rem = 0;
    spring_snap(&k->lift, ZUI_KINETIC_ONE);
    spring_snap(&k->aim, 0);
    k->pivot_x = k->pivot_y = ZUI_KINETIC_PIVOT;
    k->ptr_x = k->ptr_y = ZUI_KINETIC_PIVOT;
    k->work = 0;
    k->fire_acc = 0;
    k->stall_base = 0;
    k->clicks = 0;
    k->halves = 0;
}

static void enter(zui_kinetic_t *k, zui_kinetic_state_t s, uint32_t now_ms)
{
    if (s == ZUI_KINETIC_STALL) k->stall_base = fire_angle(k);
    k->state = s;
    k->entered_ms = now_ms;
    k->dirty = true;
}

bool zui_kinetic_set_state(zui_kinetic_t *k, zui_kinetic_state_t s, uint32_t now_ms)
{
    if (!k || (uint32_t) s > (uint32_t) ZUI_KINETIC_BOTH) return false;
    advance(k, now_ms);
    if (k->escrow && s != ZUI_KINETIC_BOTH) return false;
    if (s == k->state) return true;
    if (s == ZUI_KINETIC_BOTH) k->escrow = true;
    enter(k, s, now_ms);
    return true;
}

bool zui_kinetic_settle(zui_kinetic_t *k, zui_kinetic_state_t s, uint32_t now_ms)
{
    if (!k || !k->escrow || s == ZUI_KINETIC_BOTH || (uint32_t) s > (uint32_t) ZUI_KINETIC_BOTH)
        return false;
    advance(k, now_ms);
    k->escrow = false;
    enter(k, s, now_ms);
    return true;
}

void zui_kinetic_set_activity(zui_kinetic_t *k, zui_kinetic_activity_t a, uint32_t now_ms)
{
    if (!k || (uint32_t) a > (uint32_t) ZUI_KINETIC_NEEDS_INPUT || a == k->act) return;
    advance(k, now_ms);
    uint32_t op = zui_kinetic_period_ms(k->act), np = zui_kinetic_period_ms(a);
    if (op && np) { /* KI2: keep the phase fraction */
        uint32_t frac = ((now_ms - k->epoch_ms) % op) * 65536u / op;
        k->epoch_ms = now_ms - (uint32_t) (((uint64_t) frac * np) >> 16);
    } else {
        k->epoch_ms = now_ms;
    }
    k->act = a;
    k->halves = halves_at(k, now_ms);
    k->dirty = true;
}

void zui_kinetic_set_visible(zui_kinetic_t *k, bool visible)
{
    if (!k || k->visible == visible) return;
    k->visible = visible;
    k->dirty = true;
}

void zui_kinetic_set_pointer(zui_kinetic_t *k, int32_t x, int32_t y, bool inside)
{
    if (!k) return;
    k->ptr_x = x;
    k->ptr_y = y;
    k->pointer_in = inside;
}

void zui_kinetic_set_work(zui_kinetic_t *k, uint32_t tokens_per_cycle)
{
    if (k) k->work = tokens_per_cycle;
}

void zui_kinetic_set_pivot(zui_kinetic_t *k, int32_t x, int32_t y)
{
    if (!k) return;
    k->pivot_x = x;
    k->pivot_y = y;
    k->dirty = true;
}

/* ---- the frame ----------------------------------------------------------- */

static void group_set(zui_kinetic_group_t *g, int i, int32_t s, int32_t rot, int32_t tx, int32_t ty,
                      int32_t px, int32_t py)
{
    int32_t c = zui_kinetic_cos(rot), sn = zui_kinetic_sin(rot);
    int32_t a = mulq(s, c), b = mulq(s, sn);
    g->group = names[i];
    g->m[0] = a;
    g->m[1] = b;
    g->m[2] = -b;
    g->m[3] = a;
    g->m[4] = tx;
    g->m[5] = ty;
    g->px = px * 16; /* 1/16 icon units, as zui_vec */
    g->py = py * 16;
    g->opacity = 256;
    g->tint = 0;
    g->tint_amt = 0;
    g->grad_shift = 0;
}

static void group_copy(zui_kinetic_group_t *d, const zui_kinetic_group_t *s)
{
    d->group = s->group;
    for (int i = 0; i < 6; i++) d->m[i] = s->m[i];
    d->px = s->px;
    d->py = s->py;
    d->opacity = s->opacity;
    d->tint = s->tint;
    d->tint_amt = s->tint_amt;
    d->grad_shift = s->grad_shift;
}

static bool group_eq(const zui_kinetic_group_t *a, const zui_kinetic_group_t *b)
{
    for (int i = 0; i < 6; i++)
        if (a->m[i] != b->m[i]) return false;
    return a->group == b->group && a->px == b->px && a->py == b->py && a->opacity == b->opacity &&
           a->tint == b->tint && a->tint_amt == b->tint_amt && a->grad_shift == b->grad_shift;
}

static void tint(zui_kinetic_group_t *g, uint32_t color, uint32_t amt)
{
    g->tint = color;
    g->tint_amt = (uint16_t) amt;
}

static bool frame_eq(const zui_kinetic_frame_t *a, const zui_kinetic_frame_t *b)
{
    for (int i = 0; i < ZUI_KINETIC_GROUPS; i++)
        if (!group_eq(&a->g[i], &b->g[i]) || !group_eq(&a->ghost[i], &b->ghost[i])) return false;
    return a->ghost_on == b->ghost_on && a->pip == b->pip && a->pip_color == b->pip_color &&
           a->pip_opacity == b->pip_opacity && a->pip_required == b->pip_required;
}

static void frame_copy(zui_kinetic_frame_t *d, const zui_kinetic_frame_t *s)
{
    for (int i = 0; i < ZUI_KINETIC_GROUPS; i++) {
        group_copy(&d->g[i], &s->g[i]);
        group_copy(&d->ghost[i], &s->ghost[i]);
    }
    d->ghost_on = s->ghost_on;
    d->pip = s->pip;
    d->pip_color = s->pip_color;
    d->pip_opacity = s->pip_opacity;
    d->pip_required = s->pip_required;
    d->redraw = s->redraw;
    d->animating = s->animating;
}

/* 0..256 -> 0..256 along a smoothstep (L2, L3 easing). */
static int32_t smooth(int32_t f)
{
    return (int32_t) (((int64_t) f * f * (768 - 2 * f)) >> 16);
}

static int32_t ramp(uint32_t el, uint32_t total)
{
    return el >= total ? 256 : (int32_t) (el * 256u / total);
}

/* L1: four equal steps from 1.0 to the hover lift. */
static int32_t lift_steps(int32_t x)
{
    const int32_t span = ZUI_KINETIC_LIFT - ZUI_KINETIC_ONE;
    int32_t lv = ((x - ZUI_KINETIC_ONE) * 4 + span / 2) / span;
    if (lv < 0) lv = 0;
    if (lv > 4) lv = 4;
    return ZUI_KINETIC_ONE + lv * span / 4;
}

static void pip_fill(zui_kinetic_frame_t *o, zui_kinetic_activity_t a, bool rm)
{
    static const uint32_t col[4] = {0u, ZUI_MUTED, ZUI_GOLD, ZUI_GOLD_HI};
    static const uint16_t op[4] = {0, 128, 256, 256};
    o->pip = (zui_kinetic_pip_t) a; /* the enums line up (KI6) */
    o->pip_color = col[a];
    o->pip_opacity = op[a];
    o->pip_required = rm && a != ZUI_KINETIC_NOT_RUNNING;
}

bool zui_kinetic_frame(zui_kinetic_t *k, uint32_t now_ms, zui_kinetic_frame_t *out)
{
    if (!k || !out) return false;
    advance(k, now_ms);
    const bool rm = reduced();
    const zui_kinetic_tier_t tier = k->tier;
    const uint32_t p = zui_kinetic_period_ms(k->act);
    const uint32_t el = since(k->now_ms, k->entered_ms);
    const zui_kinetic_state_t st = k->state;

    /* the ambient wave at the tempo; L1 samples and holds it 4 times a period */
    int32_t ph = p ? (int32_t) (((k->now_ms - k->epoch_ms) % p) * 3600u / p) : 0;
    if (tier == ZUI_KINETIC_L1) ph = ph / 900 * 900;
    const int32_t wave = zui_kinetic_sin(ph);
    const bool amb = p && !rm && st != ZUI_KINETIC_STALL;
    const bool amb_el = amb && ambient_element(st);

    /* between 4 ms steps the springs are read part way along their velocity */
    const int32_t rem = (int32_t) k->spring_rem;
    int32_t lift = k->lift.x + (int32_t) (((int64_t) k->lift.v * rem) >> 10);
    if (tier == ZUI_KINETIC_L1) lift = lift_steps(lift);
    int32_t ax = k->aim.x + (int32_t) (((int64_t) k->aim.v * rem) >> 10);
    int32_t aim = tier == ZUI_KINETIC_L1 ? aim_target(k) : (ax + (ax >= 0 ? 32 : -32)) / AIM_SCALE;
    if (rm) aim = 0;
    int32_t rot = (st == ZUI_KINETIC_STALL ? k->stall_base : fire_angle(k)) + k->clicks + aim;
    int32_t s_core = lift, s_el = lift, ty_core = 0, gs_core = 0, gs_el = 0;

    if (amb) {
        if (tier == ZUI_KINETIC_L3) {
            gs_core = mulq(ZUI_KINETIC_SHIMMER, wave);
            if (amb_el) gs_el = gs_core;
        } else {
            int32_t breath = ZUI_KINETIC_ONE + mulq(ZUI_KINETIC_BREATH, wave);
            s_core = mulq(lift, breath);
            if (amb_el) s_el = s_core;
            if (tier == ZUI_KINETIC_L2) ty_core = -ZUI_KINETIC_CHEST * wave; /* 16.16 */
        }
    }
    if (st == ZUI_KINETIC_STALL) {
        int32_t f = rm ? 256 : ramp(el, ZUI_KINETIC_STRAIN_MS);
        f = tier == ZUI_KINETIC_L1 ? f / 64 * 64 : smooth(f);
        int32_t q = ZUI_KINETIC_STRAIN * f; /* dd * 256, rounded once */
        if (p && !rm) {
            if (tier == ZUI_KINETIC_L1)
                q += (ph < 1800 ? ZUI_KINETIC_JITTER : -ZUI_KINETIC_JITTER) * 256;
            else if (tier == ZUI_KINETIC_L2)
                q += (ZUI_KINETIC_JITTER * wave) >> 8;
            else
                gs_core = gs_el = mulq(ZUI_KINETIC_SHIMMER, wave);
        }
        rot += (q + 128) >> 8;
    }

    const int32_t c = ZUI_KINETIC_PIVOT;
    group_set(&out->g[ZUI_KINETIC_CHASSIS], ZUI_KINETIC_CHASSIS, lift, 0, 0, 0, c, c);
    group_set(&out->g[ZUI_KINETIC_CORE], ZUI_KINETIC_CORE, s_core, 0, 0, ty_core, c, c);
    group_set(&out->g[ZUI_KINETIC_ELEMENT], ZUI_KINETIC_ELEMENT, s_el, rot % FULL_TURN, 0, 0,
              k->pivot_x, k->pivot_y);
    out->g[ZUI_KINETIC_CORE].grad_shift = gs_core;
    out->g[ZUI_KINETIC_ELEMENT].grad_shift = gs_el;

    if (st == ZUI_KINETIC_STALL) {
        tint(&out->g[ZUI_KINETIC_CORE], ZUI_KINETIC_STALL_TINT, ZUI_KINETIC_TINT_STALL);
        tint(&out->g[ZUI_KINETIC_ELEMENT], ZUI_KINETIC_STALL_TINT, ZUI_KINETIC_TINT_STALL);
    }
    if (st == ZUI_KINETIC_FIRING && rm)
        tint(&out->g[ZUI_KINETIC_ELEMENT], ZUI_KINETIC_FIRE_TINT, ZUI_KINETIC_TINT_FIRE);

    out->ghost_on = st == ZUI_KINETIC_BOTH;
    for (int i = 0; i < ZUI_KINETIC_GROUPS; i++) group_copy(&out->ghost[i], &out->g[i]);
    if (out->ghost_on) {
        int32_t f = (rm || tier == ZUI_KINETIC_L1) ? 256 : smooth(ramp(el, ZUI_KINETIC_SPLIT_MS));
        int32_t d = ZUI_KINETIC_SPLIT * f * 256; /* 16.16 */
        int32_t gop = ZUI_KINETIC_GHOST;
        if (p && !rm && tier != ZUI_KINETIC_L3) gop += mulq(ZUI_KINETIC_PULSE, wave);
        for (int i = 0; i < ZUI_KINETIC_GROUPS; i++) {
            zui_kinetic_group_t *a = &out->g[i], *b = &out->ghost[i];
            tint(a, ZUI_KINETIC_QUARANTINE, ZUI_KINETIC_TINT_BOTH);
            tint(b, ZUI_KINETIC_QUARANTINE, ZUI_KINETIC_TINT_BOTH);
            a->m[4] -= d;
            b->m[4] += d;
            b->opacity = (uint16_t) gop;
            b->grad_shift = -a->grad_shift; /* L3: the copies shimmer in counter-phase */
        }
    }
    pip_fill(out, k->act, rm);

    out->animating =
        !rm && (!spring_rest(&k->lift, lift_target(k)) ||
                (tier != ZUI_KINETIC_L1 && !spring_rest(&k->aim, aim_target(k) * AIM_SCALE)) ||
                amb || (st == ZUI_KINETIC_FIRING && k->work) ||
                (st == ZUI_KINETIC_STALL && (el < ZUI_KINETIC_STRAIN_MS || p)) ||
                (st == ZUI_KINETIC_BOTH && el < ZUI_KINETIC_SPLIT_MS && tier != ZUI_KINETIC_L1));
    if (!k->visible) { /* KI5: nothing on screen to redraw */
        out->redraw = false;
        out->animating = false;
        return false;
    }
    out->redraw = !k->have_last || k->dirty || !frame_eq(out, &k->last);
    frame_copy(&k->last, out);
    k->have_last = true;
    k->dirty = false;
    return out->redraw;
}
