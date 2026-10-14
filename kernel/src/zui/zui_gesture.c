/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_gesture.c — the touchpad gesture recogniser (zui_gesture.h). */
#include "zui_gesture.h"

enum { ST_IDLE = 0, ST_POSSIBLE, ST_SCROLL, ST_ZOOM, ST_ROTATE, ST_DONE };

#define COAST_STOP_Q8  64  /* under a quarter pixel a frame: stop  */
#define COAST_FRESH_MS 50u /* the last scroll this recent may coast */
#define COAST_STEPS    2000u

static int32_t iabs(int32_t v)
{
    return v < 0 ? -v : v;
}

/* ---- integer geometry ------------------------------------------------------------- */

int32_t zui_tpad_isqrt(int64_t v)
{
    if (v <= 0) return 0;
    uint64_t x = (uint64_t) v, r = 0, bit = (uint64_t) 1 << 62;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= r + bit) {
            x -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (int32_t) r;
}

/* tan(d degrees) x4096 for d = 0..45 */
static const uint16_t TAN_Q12[46] = {
    0,    71,   143,  215,  286,  358,  431,  503,  576,  649,  722,  796,  871,  946,  1021, 1098,
    1175, 1252, 1331, 1410, 1491, 1572, 1655, 1739, 1824, 1910, 1998, 2087, 2178, 2270, 2365, 2461,
    2559, 2660, 2763, 2868, 2976, 3087, 3200, 3317, 3437, 3561, 3688, 3820, 3955, 4096};

int32_t zui_tpad_atan2_deg(int32_t y, int32_t x)
{
    if (x == 0 && y == 0) return 0;
    int64_t ax = x < 0 ? -(int64_t) x : x, ay = y < 0 ? -(int64_t) y : y;
    int64_t mn = ax < ay ? ax : ay, mx = ax < ay ? ay : ax;
    int64_t r = (mn << 12) / mx; /* 0..4096 */
    int32_t d = 0;
    int64_t best = 1 << 20;
    for (int32_t i = 0; i <= 45; i++) {
        int64_t e = r - TAN_Q12[i];
        if (e < 0) e = -e;
        if (e < best) {
            best = e;
            d = i;
        }
    }
    int32_t a = ay > ax ? 90 - d : d; /* first quadrant */
    if (x < 0) a = 180 - a;
    if (y < 0) a = 360 - a;
    return a % 360;
}

/* ---- set-up ------------------------------------------------------------------------- */

void zui_tpad_defaults(zui_tpad_cfg_t *c)
{
    c->tap_slop_px = ZUI_TAP_SLOP_PX;
    c->tap_ms = 250;
    c->scroll_px = 8;
    c->pinch_px = 24;
    c->rotate_deg = 12;
    c->swipe_px = 80;
    c->palm_px = 30;
    c->speed_q8 = 256;
    c->natural = true;
    c->tap_to_click = true;
    c->inertia_min_q8 = 2 * 256;
    c->decay_q8 = 243; /* about 0.95 a frame */
}

void zui_tpad_init(zui_tpad_t *t)
{
    uint8_t *b = (uint8_t *) t;
    for (uint32_t i = 0; i < sizeof *t; i++) b[i] = 0;
    zui_tpad_defaults(&t->cfg);
}

void zui_tpad_set_bounds(zui_tpad_t *t, int32_t w, int32_t h, int32_t px, int32_t py)
{
    t->bound_w = w;
    t->bound_h = h;
    t->px = px;
    t->py = py;
}

/* ---- output ------------------------------------------------------------------------- */

typedef struct {
    zui_tpad_ev_t *out;
    uint32_t cap, n;
    uint32_t ms;
} sink_t;

static zui_tpad_ev_t *emit(sink_t *s, zui_tpad_t *t, uint8_t kind, uint8_t phase)
{
    if (s->n >= s->cap) return 0;
    zui_tpad_ev_t *e = &s->out[s->n++];
    e->kind = kind;
    e->phase = phase;
    e->fingers = t->fingers;
    e->x = t->lcx;
    e->y = t->lcy;
    e->dx = e->dy = 0;
    e->scale_q8 = 256;
    e->angle = t->ang_acc;
    e->ms = s->ms;
    return e;
}

/* ---- the recogniser ------------------------------------------------------------------- */

static uint32_t live(const zui_tpad_t *t, const zui_tpad_finger_t **f, uint32_t max)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < t->nf && k < max; i++)
        if (!t->f[i].palm) f[k++] = &t->f[i];
    return k;
}

typedef struct {
    uint32_t n;
    int32_t cx, cy, dist, ang, spread;
} shape_t;

static shape_t shape_of(const zui_tpad_t *t)
{
    const zui_tpad_finger_t *f[ZUI_TPAD_CONTACTS];
    shape_t s = {0, 0, 0, 0, 0, 0};
    s.n = live(t, f, ZUI_TPAD_CONTACTS);
    if (!s.n) return s;
    int64_t sx = 0, sy = 0;
    for (uint32_t i = 0; i < s.n; i++) {
        sx += f[i]->x;
        sy += f[i]->y;
    }
    s.cx = (int32_t) (sx / (int64_t) s.n);
    s.cy = (int32_t) (sy / (int64_t) s.n);
    if (s.n >= 2) {
        int32_t dx = f[1]->x - f[0]->x, dy = f[1]->y - f[0]->y;
        s.dist = zui_tpad_isqrt((int64_t) dx * dx + (int64_t) dy * dy);
        s.ang = zui_tpad_atan2_deg(dy, dx);
        int64_t sum = 0;
        for (uint32_t i = 0; i < s.n; i++) {
            int32_t ex = f[i]->x - s.cx, ey = f[i]->y - s.cy;
            sum += zui_tpad_isqrt((int64_t) ex * ex + (int64_t) ey * ey);
        }
        s.spread = (int32_t) (sum / (int64_t) s.n);
    }
    return s;
}

static void reference(zui_tpad_t *t, const shape_t *s)
{
    t->cx0 = t->lcx = s->cx;
    t->cy0 = t->lcy = s->cy;
    t->dist0 = s->dist > 0 ? s->dist : 1;
    t->ldist = s->dist;
    t->ang0 = t->lang = s->ang;
    t->ang_acc = 0;
    t->spread0 = s->spread;
}

static void content_delta(const zui_tpad_t *t, int32_t fdx, int32_t fdy, int32_t *dx, int32_t *dy)
{
    *dx = t->cfg.natural ? -fdx : fdx;
    *dy = t->cfg.natural ? -fdy : fdy;
}

static void track_velocity(zui_tpad_t *t, int32_t dx, int32_t dy, uint32_t ms)
{
    uint32_t dt = ms - t->last_scroll_ms;
    if (dt == 0) dt = 1;
    if (dt > 1000) dt = 1000;
    int32_t ix = (int32_t) (((int64_t) dx * 256 * ZUI_TPAD_FRAME_MS) / dt);
    int32_t iy = (int32_t) (((int64_t) dy * 256 * ZUI_TPAD_FRAME_MS) / dt);
    t->vx_q8 = (t->vx_q8 + ix) / 2;
    t->vy_q8 = (t->vy_q8 + iy) / 2;
    t->last_scroll_ms = ms;
}

/* G7, G5: the running gesture ends (lift_all: every finger is up). */
static void end_gesture(zui_tpad_t *t, sink_t *s, bool lift_all)
{
    zui_tpad_ev_t *e;
    switch (t->state) {
    case ST_SCROLL:
        if (lift_all && s->ms - t->last_scroll_ms <= COAST_FRESH_MS &&
            (iabs(t->vx_q8) >= t->cfg.inertia_min_q8 || iabs(t->vy_q8) >= t->cfg.inertia_min_q8)) {
            t->coasting = true;
            t->coast_ms = s->ms;
            t->acc_x_q8 = t->acc_y_q8 = 0;
        } else {
            emit(s, t, ZUI_TPAD_SCROLL, ZUI_TPAD_END);
        }
        break;
    case ST_ZOOM:
        e = emit(s, t, ZUI_TPAD_ZOOM, ZUI_TPAD_END);
        if (e) e->scale_q8 = (int32_t) (((int64_t) t->ldist * 256) / t->dist0);
        break;
    case ST_ROTATE:
        emit(s, t, ZUI_TPAD_ROTATE, ZUI_TPAD_END);
        break;
    default:
        break;
    }
}

static void pointer_move(zui_tpad_t *t, sink_t *s, int32_t fdx, int32_t fdy)
{
    int32_t mx = fdx * t->cfg.speed_q8 + t->rem_x;
    int32_t my = fdy * t->cfg.speed_q8 + t->rem_y;
    int32_t dx = mx / 256, dy = my / 256;
    t->rem_x = mx - dx * 256;
    t->rem_y = my - dy * 256;
    if (!dx && !dy) return;
    t->px += dx;
    t->py += dy;
    if (t->bound_w > 0) {
        if (t->px < 0) t->px = 0;
        if (t->px >= t->bound_w) t->px = t->bound_w - 1;
    }
    if (t->bound_h > 0) {
        if (t->py < 0) t->py = 0;
        if (t->py >= t->bound_h) t->py = t->bound_h - 1;
    }
    zui_tpad_ev_t *e = emit(s, t, ZUI_TPAD_MOVE, ZUI_TPAD_UPDATE);
    if (!e) return;
    e->dx = dx;
    e->dy = dy;
    e->x = t->px;
    e->y = t->py;
}

static void update_fingers(zui_tpad_t *t, const zui_tpad_contact_t *c, uint32_t n)
{
    /* lifted fingers go; the others keep their order */
    uint32_t k = 0;
    for (uint32_t i = 0; i < t->nf; i++) {
        bool still = false;
        for (uint32_t j = 0; j < n; j++) {
            if (c[j].id == t->f[i].id) {
                t->f[i].x = c[j].x;
                t->f[i].y = c[j].y;
                still = true;
                break;
            }
        }
        if (still) {
            if (k != i) {
                const uint8_t *a = (const uint8_t *) &t->f[i];
                uint8_t *b = (uint8_t *) &t->f[k];
                for (uint32_t j = 0; j < sizeof t->f[0]; j++) b[j] = a[j];
            }
            k++;
        }
    }
    t->nf = k;
    /* G2: a new contact is a palm if it lands large */
    for (uint32_t j = 0; j < n && t->nf < ZUI_TPAD_CONTACTS; j++) {
        bool known = false;
        for (uint32_t i = 0; i < t->nf; i++)
            if (t->f[i].id == c[j].id) known = true;
        if (known) continue;
        zui_tpad_finger_t *f = &t->f[t->nf++];
        f->id = c[j].id;
        f->palm = c[j].size > t->cfg.palm_px;
        f->x = f->sx = c[j].x;
        f->y = f->sy = c[j].y;
    }
}

uint32_t zui_tpad_frame(zui_tpad_t *t, const zui_tpad_contact_t *c, uint32_t n, uint32_t ms,
                        zui_tpad_ev_t *out, uint32_t cap)
{
    sink_t s = {out, cap, 0, ms};
    update_fingers(t, c, n);
    shape_t sh = shape_of(t);
    uint8_t count = (uint8_t) sh.n;

    if (count > 0 && t->coasting) { /* G5: a touch stops the coast */
        t->coasting = false;
        emit(&s, t, ZUI_TPAD_SCROLL, ZUI_TPAD_END);
    }

    if (count != t->fingers) {
        end_gesture(t, &s, count == 0);
        if (t->fingers == 0) { /* a new touch */
            t->t0 = ms;
            t->tap_ok = true;
            t->max_fingers = count;
            t->state = ST_POSSIBLE;
            t->rem_x = t->rem_y = 0;
        } else if (count == 0) { /* G3: everything lifted */
            if (t->state == ST_POSSIBLE && t->tap_ok && t->cfg.tap_to_click &&
                ms - t->t0 <= t->cfg.tap_ms) {
                uint8_t k = t->max_fingers == 1   ? ZUI_TPAD_CLICK
                            : t->max_fingers == 2 ? ZUI_TPAD_SECONDARY_CLICK
                            : t->max_fingers == 3 ? ZUI_TPAD_MIDDLE_CLICK
                                                  : ZUI_TPAD_NONE;
                if (k) {
                    zui_tpad_ev_t *e = emit(&s, t, k, ZUI_TPAD_END);
                    if (e) {
                        e->fingers = t->max_fingers;
                        e->x = t->px;
                        e->y = t->py;
                    }
                }
            }
            t->state = ST_IDLE;
        } else { /* G7 */
            if (t->state != ST_POSSIBLE) t->tap_ok = false;
            if (count > t->max_fingers) t->max_fingers = count;
            t->state = ST_POSSIBLE;
        }
        t->fingers = count;
        reference(t, &sh);
        return s.n;
    }
    if (count == 0) return s.n;

    /* G3: tap slop, per finger from where it landed */
    for (uint32_t i = 0; i < t->nf; i++) {
        const zui_tpad_finger_t *f = &t->f[i];
        if (f->palm) continue;
        if (iabs(f->x - f->sx) > t->cfg.tap_slop_px || iabs(f->y - f->sy) > t->cfg.tap_slop_px)
            t->tap_ok = false;
    }

    int32_t fdx = sh.cx - t->lcx, fdy = sh.cy - t->lcy;
    if (count == 1) {
        if (fdx || fdy) pointer_move(t, &s, fdx, fdy);
    } else if (count == 2) {
        int32_t da = sh.ang - t->lang;
        if (da > 180) da -= 360;
        if (da < -180) da += 360;
        t->ang_acc += da;
        t->lang = sh.ang;
        int32_t scale = (int32_t) (((int64_t) sh.dist * 256) / t->dist0);
        t->lcx = sh.cx;
        t->lcy = sh.cy;
        zui_tpad_ev_t *e;
        switch (t->state) {
        case ST_POSSIBLE:
            if (iabs(sh.dist - t->dist0) >= t->cfg.pinch_px) {
                t->state = ST_ZOOM;
                t->tap_ok = false;
                e = emit(&s, t, ZUI_TPAD_ZOOM, ZUI_TPAD_BEGIN);
                if (e) e->scale_q8 = scale;
            } else if (iabs(t->ang_acc) >= t->cfg.rotate_deg) {
                t->state = ST_ROTATE;
                t->tap_ok = false;
                emit(&s, t, ZUI_TPAD_ROTATE, ZUI_TPAD_BEGIN);
            } else if (iabs(sh.cx - t->cx0) >= t->cfg.scroll_px ||
                       iabs(sh.cy - t->cy0) >= t->cfg.scroll_px) {
                t->state = ST_SCROLL;
                t->tap_ok = false;
                t->vx_q8 = t->vy_q8 = 0;
                t->last_scroll_ms = ms;
                e = emit(&s, t, ZUI_TPAD_SCROLL, ZUI_TPAD_BEGIN);
                if (e) content_delta(t, sh.cx - t->cx0, sh.cy - t->cy0, &e->dx, &e->dy);
            }
            break;
        case ST_SCROLL:
            if (fdx || fdy) {
                e = emit(&s, t, ZUI_TPAD_SCROLL, ZUI_TPAD_UPDATE);
                int32_t dx, dy;
                content_delta(t, fdx, fdy, &dx, &dy);
                if (e) {
                    e->dx = dx;
                    e->dy = dy;
                }
                track_velocity(t, dx, dy, ms);
            }
            break;
        case ST_ZOOM:
            if (sh.dist != t->ldist) {
                e = emit(&s, t, ZUI_TPAD_ZOOM, ZUI_TPAD_UPDATE);
                if (e) e->scale_q8 = scale;
            }
            break;
        case ST_ROTATE:
            if (da) emit(&s, t, ZUI_TPAD_ROTATE, ZUI_TPAD_UPDATE);
            break;
        default:
            break;
        }
        t->ldist = sh.dist;
        return s.n;
    } else if (count == 3 && t->state == ST_POSSIBLE) {
        int32_t tx = sh.cx - t->cx0, ty = sh.cy - t->cy0;
        if (iabs(tx) >= t->cfg.swipe_px || iabs(ty) >= t->cfg.swipe_px) {
            uint8_t k = iabs(tx) >= iabs(ty) ? (tx < 0 ? ZUI_TPAD_SWIPE_LEFT : ZUI_TPAD_SWIPE_RIGHT)
                                             : (ty < 0 ? ZUI_TPAD_SWIPE_UP : ZUI_TPAD_SWIPE_DOWN);
            t->state = ST_DONE;
            t->tap_ok = false;
            t->lcx = sh.cx;
            t->lcy = sh.cy;
            zui_tpad_ev_t *e = emit(&s, t, k, ZUI_TPAD_END);
            if (e) {
                e->dx = tx;
                e->dy = ty;
            }
        }
    } else if (count == 4 && t->state == ST_POSSIBLE) {
        int32_t d = sh.spread - t->spread0;
        if (iabs(d) >= t->cfg.pinch_px) {
            t->state = ST_DONE;
            t->tap_ok = false;
            emit(&s, t, d < 0 ? ZUI_TPAD_PINCH4_IN : ZUI_TPAD_PINCH4_OUT, ZUI_TPAD_END);
        }
    }
    t->lcx = sh.cx;
    t->lcy = sh.cy;
    return s.n;
}

uint32_t zui_tpad_tick(zui_tpad_t *t, uint32_t ms, zui_tpad_ev_t *out, uint32_t cap)
{
    sink_t s = {out, cap, 0, ms};
    if (!t->coasting || cap < 2) return 0;
    uint32_t steps = (ms - t->coast_ms) / ZUI_TPAD_FRAME_MS;
    if (steps == 0) return 0;
    if (steps > COAST_STEPS) steps = COAST_STEPS;
    t->coast_ms += steps * ZUI_TPAD_FRAME_MS;
    int32_t decay = t->cfg.decay_q8;
    if (decay > 255) decay = 255;
    if (decay < 0) decay = 0;
    int32_t tx = 0, ty = 0;
    bool stop = false;
    for (uint32_t i = 0; i < steps && !stop; i++) {
        t->vx_q8 = (int32_t) (((int64_t) t->vx_q8 * decay) / 256);
        t->vy_q8 = (int32_t) (((int64_t) t->vy_q8 * decay) / 256);
        t->acc_x_q8 += t->vx_q8;
        t->acc_y_q8 += t->vy_q8;
        int32_t dx = t->acc_x_q8 / 256, dy = t->acc_y_q8 / 256;
        t->acc_x_q8 -= dx * 256;
        t->acc_y_q8 -= dy * 256;
        tx += dx;
        ty += dy;
        if (iabs(t->vx_q8) < COAST_STOP_Q8 && iabs(t->vy_q8) < COAST_STOP_Q8) stop = true;
    }
    uint8_t keep = t->fingers;
    t->fingers = 2;
    if (tx || ty) {
        zui_tpad_ev_t *e = emit(&s, t, ZUI_TPAD_SCROLL, ZUI_TPAD_INERTIA);
        if (e) {
            e->dx = tx;
            e->dy = ty;
        }
    }
    if (stop) {
        t->coasting = false;
        t->vx_q8 = t->vy_q8 = 0;
        emit(&s, t, ZUI_TPAD_SCROLL, ZUI_TPAD_END);
    }
    t->fingers = keep;
    return s.n;
}

/* ---- G8 ---------------------------------------------------------------------------------- */

static zui_event_t *ev(zui_event_t *out, uint32_t cap, uint32_t *n, uint8_t type,
                       const zui_tpad_ev_t *g)
{
    if (*n >= cap) return 0;
    zui_event_t *e = &out[(*n)++];
    uint8_t *b = (uint8_t *) e;
    for (uint32_t i = 0; i < sizeof *e; i++) b[i] = 0;
    e->type = type;
    e->ms = g->ms;
    return e;
}

uint32_t zui_tpad_events(const zui_tpad_t *t, const zui_tpad_ev_t *g, zui_event_t *out,
                         uint32_t cap)
{
    uint32_t n = 0;
    zui_event_t *e;
    uint8_t btn = 0;
    switch (g->kind) {
    case ZUI_TPAD_MOVE:
        e = ev(out, cap, &n, ZUI_EV_PTR_MOVE, g);
        if (e) {
            e->x = g->x;
            e->y = g->y;
        }
        return n;
    case ZUI_TPAD_CLICK:
        btn = ZUI_BTN_LEFT;
        break;
    case ZUI_TPAD_SECONDARY_CLICK:
        btn = ZUI_BTN_RIGHT;
        break;
    case ZUI_TPAD_MIDDLE_CLICK:
        btn = ZUI_BTN_MIDDLE;
        break;
    case ZUI_TPAD_SCROLL:
        if (!g->dx && !g->dy) return 0;
        e = ev(out, cap, &n, ZUI_EV_SCROLL, g);
        if (e) {
            e->x = t->px;
            e->y = t->py;
            e->dx = g->dx;
            e->dy = g->dy;
        }
        return n;
    default:
        return 0;
    }
    e = ev(out, cap, &n, ZUI_EV_PTR_DOWN, g);
    if (e) {
        e->x = g->x;
        e->y = g->y;
        e->button = btn;
    }
    e = ev(out, cap, &n, ZUI_EV_PTR_UP, g);
    if (e) {
        e->x = g->x;
        e->y = g->y;
        e->button = btn;
    }
    return n;
}
