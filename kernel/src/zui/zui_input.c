/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_input.c — the event ring, touch-as-pointer, gestures (zui_input.h). */
#include "zui_input.h"

static void copy_ev(zui_event_t *d, const zui_event_t *s)
{
    const uint8_t *a = (const uint8_t *) s;
    uint8_t *b = (uint8_t *) d;
    for (uint32_t i = 0; i < sizeof *d; i++) b[i] = a[i];
}

static void zero_ev(zui_event_t *e)
{
    uint8_t *b = (uint8_t *) e;
    for (uint32_t i = 0; i < sizeof *e; i++) b[i] = 0;
}

void zui_queue_init(zui_queue_t *q)
{
    q->head = q->tail = 0;
    q->dropped = 0;
    q->buttons = 0;
    q->mods = 0;
    q->px = q->py = 0;
}

uint32_t zui_queue_len(const zui_queue_t *q)
{
    return q->head - q->tail;
}

/* I1: make room in a full ring. The oldest pointer move goes first (later
 * moves supersede it); with none queued, the oldest event of any kind. */
static void make_room(zui_queue_t *q)
{
    for (uint32_t i = q->tail; i != q->head; i++) {
        zui_event_t *e = &q->ev[i & (ZUI_INPUT_RING - 1u)];
        if (e->type == ZUI_EV_PTR_MOVE || e->type == ZUI_EV_TOUCH_MOVE || e->type == ZUI_EV_TICK) {
            for (uint32_t j = i; j != q->tail; j--)
                copy_ev(&q->ev[j & (ZUI_INPUT_RING - 1u)],
                        &q->ev[(j - 1u) & (ZUI_INPUT_RING - 1u)]);
            q->tail++;
            q->dropped++;
            return;
        }
    }
    q->tail++;
    q->dropped++;
}

static void put(zui_queue_t *q, const zui_event_t *e)
{
    if (zui_queue_len(q) >= ZUI_INPUT_RING) make_room(q);
    copy_ev(&q->ev[q->head & (ZUI_INPUT_RING - 1u)], e);
    q->head++;
}

void zui_push(zui_queue_t *q, const zui_event_t *e)
{
    switch (e->type) {
    case ZUI_EV_KEY_DOWN:
    case ZUI_EV_KEY_UP:
        q->mods = e->mods;
        break;
    case ZUI_EV_PTR_DOWN:
        q->buttons |= e->button;
        q->px = e->x;
        q->py = e->y;
        break;
    case ZUI_EV_PTR_UP:
        q->buttons &= (uint8_t) ~e->button;
        q->px = e->x;
        q->py = e->y;
        break;
    case ZUI_EV_PTR_MOVE:
        q->px = e->x;
        q->py = e->y;
        break;
    default:
        break;
    }
    put(q, e);
    /* I2: the first finger is also the pointer. */
    if (e->touch_id == 0 && (e->type == ZUI_EV_TOUCH_BEGIN || e->type == ZUI_EV_TOUCH_MOVE ||
                             e->type == ZUI_EV_TOUCH_END || e->type == ZUI_EV_TOUCH_CANCEL)) {
        zui_event_t p;
        zero_ev(&p);
        p.type = e->type == ZUI_EV_TOUCH_BEGIN  ? ZUI_EV_PTR_DOWN
                 : e->type == ZUI_EV_TOUCH_MOVE ? ZUI_EV_PTR_MOVE
                                                : ZUI_EV_PTR_UP;
        p.button = ZUI_BTN_LEFT;
        p.x = e->x;
        p.y = e->y;
        p.ms = e->ms;
        p.mods = q->mods;
        zui_push(q, &p);
    }
}

bool zui_pop(zui_queue_t *q, zui_event_t *out)
{
    if (q->head == q->tail) return false;
    copy_ev(out, &q->ev[q->tail & (ZUI_INPUT_RING - 1u)]);
    q->tail++;
    return true;
}

void zui_push_key(zui_queue_t *q, bool down, uint16_t key, uint8_t mods, bool repeat, uint32_t ms)
{
    zui_event_t e;
    zero_ev(&e);
    e.type = down ? ZUI_EV_KEY_DOWN : ZUI_EV_KEY_UP;
    e.key = key;
    e.mods = mods;
    e.repeat = repeat ? 1 : 0;
    e.ms = ms;
    zui_push(q, &e);
}

void zui_push_text(zui_queue_t *q, uint8_t type, const char *utf8, uint32_t ms)
{
    zui_event_t e;
    zero_ev(&e);
    e.type = type;
    e.ms = ms;
    e.mods = q->mods;
    uint32_t n = 0;
    while (utf8 && utf8[n]) n++;
    n = zui_utf8_fit(utf8, n, ZUI_TEXT_EV_MAX);
    for (uint32_t i = 0; i < n; i++) e.text[i] = utf8[i];
    e.text[n] = 0;
    zui_push(q, &e);
}

void zui_push_ptr(zui_queue_t *q, uint8_t type, int32_t x, int32_t y, uint8_t button, uint32_t ms)
{
    zui_event_t e;
    zero_ev(&e);
    e.type = type;
    e.x = x;
    e.y = y;
    e.button = type == ZUI_EV_PTR_MOVE ? q->buttons : button;
    e.mods = q->mods;
    e.ms = ms;
    zui_push(q, &e);
}

void zui_push_scroll(zui_queue_t *q, int32_t x, int32_t y, int32_t dx, int32_t dy, uint32_t ms)
{
    zui_event_t e;
    zero_ev(&e);
    e.type = ZUI_EV_SCROLL;
    e.x = x;
    e.y = y;
    e.dx = dx;
    e.dy = dy;
    e.mods = q->mods;
    e.ms = ms;
    zui_push(q, &e);
}

void zui_push_touch(zui_queue_t *q, uint8_t type, uint16_t id, int32_t x, int32_t y, uint32_t ms)
{
    zui_event_t e;
    zero_ev(&e);
    e.type = type;
    e.touch_id = id;
    e.x = x;
    e.y = y;
    e.ms = ms;
    e.mods = q->mods;
    zui_push(q, &e);
}

/* ---- I3: gestures -------------------------------------------------------- */

void zui_gesture_init(zui_gesture_state_t *g, int32_t slop_px)
{
    g->down = g->moved = g->long_fired = false;
    g->x0 = g->y0 = 0;
    g->t0 = 0;
    g->last_tap_ms = 0;
    g->last_tap_x = g->last_tap_y = 0;
    g->have_last_tap = false;
    g->slop_px = slop_px > 0 ? slop_px : ZUI_TAP_SLOP_PX;
}

static int32_t iabs32(int32_t v)
{
    return v < 0 ? -v : v;
}

static bool within(const zui_gesture_state_t *g, int32_t x0, int32_t y0, int32_t x, int32_t y)
{
    return iabs32(x - x0) <= g->slop_px && iabs32(y - y0) <= g->slop_px;
}

zui_gesture_t zui_gesture_feed(zui_gesture_state_t *g, const zui_event_t *e)
{
    switch (e->type) {
    case ZUI_EV_PTR_DOWN:
        if (e->button != ZUI_BTN_LEFT) return ZUI_GESTURE_NONE;
        g->down = true;
        g->moved = false;
        g->long_fired = false;
        g->x0 = e->x;
        g->y0 = e->y;
        g->t0 = e->ms;
        return ZUI_GESTURE_NONE;
    case ZUI_EV_PTR_MOVE:
        if (!g->down || g->moved || g->long_fired) return ZUI_GESTURE_NONE;
        if (!within(g, g->x0, g->y0, e->x, e->y)) {
            g->moved = true;
            g->have_last_tap = false;
            return ZUI_GESTURE_DRAG;
        }
        /* a still finger may have been held long enough */
        /* FALLTHROUGH */
    case ZUI_EV_TICK:
        if (g->down && !g->moved && !g->long_fired &&
            e->ms - g->t0 >= (uint32_t) ZUI_LONGPRESS_MS) {
            g->long_fired = true;
            g->have_last_tap = false;
            return ZUI_GESTURE_LONG_PRESS;
        }
        return ZUI_GESTURE_NONE;
    case ZUI_EV_PTR_UP: {
        if (!g->down || e->button != ZUI_BTN_LEFT) return ZUI_GESTURE_NONE;
        g->down = false;
        if (g->moved || g->long_fired) return ZUI_GESTURE_NONE;
        if (!within(g, g->x0, g->y0, e->x, e->y)) return ZUI_GESTURE_NONE;
        if (e->ms - g->t0 >= (uint32_t) ZUI_LONGPRESS_MS) {
            g->have_last_tap = false;
            return ZUI_GESTURE_LONG_PRESS;
        }
        if (g->have_last_tap && e->ms - g->last_tap_ms <= (uint32_t) ZUI_DOUBLE_MS &&
            within(g, g->last_tap_x, g->last_tap_y, e->x, e->y)) {
            g->have_last_tap = false;
            return ZUI_GESTURE_DOUBLE_TAP;
        }
        g->have_last_tap = true;
        g->last_tap_ms = e->ms;
        g->last_tap_x = e->x;
        g->last_tap_y = e->y;
        return ZUI_GESTURE_TAP;
    }
    default:
        return ZUI_GESTURE_NONE;
    }
}

/* ---- UTF-8 ----------------------------------------------------------------- */

uint32_t zui_utf8_prev(const char *s, uint32_t at)
{
    if (at == 0) return 0;
    at--;
    while (at > 0 && ((uint8_t) s[at] & 0xC0u) == 0x80u) at--;
    return at;
}

uint32_t zui_utf8_next(const char *s, uint32_t n, uint32_t at)
{
    if (at >= n) return n;
    at++;
    while (at < n && ((uint8_t) s[at] & 0xC0u) == 0x80u) at++;
    return at;
}

uint32_t zui_utf8_fit(const char *s, uint32_t n, uint32_t cap)
{
    if (n <= cap) return n;
    uint32_t k = cap;
    while (k > 0 && ((uint8_t) s[k] & 0xC0u) == 0x80u) k--;
    return k;
}
