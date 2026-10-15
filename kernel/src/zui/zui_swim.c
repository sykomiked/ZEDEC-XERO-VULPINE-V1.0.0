/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_swim.c — the chiglet swimming over the screen (zui_swim.h SW1-SW9). */
#include "zui_swim.h"
#include "zui_motion.h"
#include "zui_tokens.h"

#define EXT ((int32_t) ZUI_SWIM_R * 2) /* half the drawn extent, px */
#define Q8  256

/* sin of i/256 of a turn, scaled by 1024 */
static const int16_t QSIN[65] = {
    0,   25,  50,  75,  100, 125,  150,  175,  200,  224,  249,  273,  297,  321, 345, 369, 392,
    415, 438, 460, 483, 505, 526,  548,  569,  590,  610,  630,  650,  669,  688, 706, 724, 742,
    759, 775, 792, 807, 822, 837,  851,  865,  878,  891,  903,  915,  926,  936, 946, 955, 964,
    972, 980, 987, 993, 999, 1004, 1009, 1013, 1016, 1019, 1021, 1023, 1024, 1024};

static int32_t sin256(uint32_t i)
{
    i &= 255u;
    if (i < 64u) return QSIN[i];
    if (i < 128u) return QSIN[128u - i];
    if (i < 192u) return -QSIN[i - 128u];
    return -QSIN[256u - i];
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static bool inside(zui_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static uint32_t isqrt64(uint64_t v)
{
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t) r;
}

/* keep a centre wholly inside the work area (SW7) */
static void keep_in(const zui_swim_t *sw, int32_t *cx, int32_t *cy)
{
    zui_rect_t w = sw->work;
    *cx = w.w < 2 * EXT ? w.x + w.w / 2 : clampi(*cx, w.x + EXT, w.x + w.w - EXT);
    *cy = w.h < 2 * EXT ? w.y + w.h / 2 : clampi(*cy, w.y + EXT, w.y + w.h - EXT);
}

/* SW7: where it waits beside t, in px */
static void wait_point(const zui_swim_t *sw, zui_rect_t t, int32_t *cx, int32_t *cy)
{
    zui_rect_t w = sw->work;
    int32_t y = t.y + EXT;
    if (t.x + t.w + ZUI_SWIM_GAP + 2 * EXT <= w.x + w.w) {
        *cx = t.x + t.w + ZUI_SWIM_GAP + EXT;
        *cy = y;
    } else if (t.x - ZUI_SWIM_GAP - 2 * EXT >= w.x) {
        *cx = t.x - ZUI_SWIM_GAP - EXT;
        *cy = y;
    } else if (t.y - ZUI_SWIM_GAP - 2 * EXT >= w.y) {
        *cx = t.x + t.w - EXT;
        *cy = t.y - ZUI_SWIM_GAP - EXT;
    } else {
        *cx = t.x + t.w - EXT;
        *cy = t.y + EXT;
    }
    keep_in(sw, cx, cy);
}

/* SW4: the corner it rests in, px */
static void corner(const zui_swim_t *sw, int32_t *cx, int32_t *cy)
{
    *cx = sw->work.x + sw->work.w - EXT - ZUI_SWIM_GAP;
    *cy = sw->work.y + sw->work.h - EXT - ZUI_SWIM_GAP;
    keep_in(sw, cx, cy);
}

/* where it comes in from: just past the work area edge nearest (cx, cy) */
static void entry(const zui_swim_t *sw, int32_t cx, int32_t cy, int32_t *ex, int32_t *ey)
{
    zui_rect_t w = sw->work;
    int32_t dl = cx - w.x, dr = w.x + w.w - cx, dt = cy - w.y, db = w.y + w.h - cy;
    *ex = cx;
    *ey = cy;
    int32_t m = dl;
    *ex = w.x - EXT;
    if (dr < m) {
        m = dr;
        *ex = w.x + w.w + EXT;
    }
    if (dt < m) {
        m = dt;
        *ex = cx;
        *ey = w.y - EXT;
    }
    if (db < m) {
        *ex = cx;
        *ey = w.y + w.h + EXT;
    }
}

void zui_swim_init(zui_swim_t *sw, zui_rect_t work, const zui_chiglet_t *look)
{
    if (!sw) return;
    uint8_t *p = (uint8_t *) sw;
    for (size_t i = 0; i < sizeof *sw; i++) p[i] = 0;
    sw->work = work;
    if (look) sw->look = *look;
    sw->state = ZUI_SWIM_HIDDEN;
}

void zui_swim_set_look(zui_swim_t *sw, const zui_chiglet_t *look)
{
    if (sw && look) sw->look = *look;
}

void zui_swim_set_work(zui_swim_t *sw, zui_rect_t work)
{
    if (!sw) return;
    sw->work = work;
    int32_t cx, cy;
    if (sw->state == ZUI_SWIM_PINNED || (sw->pinned && sw->state != ZUI_SWIM_HIDDEN)) {
        corner(sw, &cx, &cy);
    } else if (sw->has_offer) {
        wait_point(sw, sw->target, &cx, &cy);
    } else {
        return;
    }
    sw->to_x = cx * Q8;
    sw->to_y = cy * Q8;
}

void zui_swim_set_windows(zui_swim_t *sw, const zui_rect_t *r, uint32_t n)
{
    if (!sw) return;
    if (!r) n = 0;
    if (n > ZUI_SWIM_WINDOWS) n = ZUI_SWIM_WINDOWS;
    for (uint32_t i = 0; i < n; i++) sw->win[i] = r[i];
    sw->nwin = n;
}

static bool snoozed(const zui_swim_t *sw)
{
    return sw->snoozing && (int32_t) (sw->now - sw->snooze_until) < 0;
}

/* start a swim from where it is (or from the edge when hidden) to (cx, cy) */
static void swim_to(zui_swim_t *sw, int32_t cx, int32_t cy)
{
    if (sw->state == ZUI_SWIM_HIDDEN) {
        int32_t ex, ey;
        entry(sw, cx, cy, &ex, &ey);
        sw->x = ex * Q8;
        sw->y = ey * Q8;
        sw->alpha = 0;
    }
    sw->from_x = sw->x;
    sw->from_y = sw->y;
    sw->to_x = cx * Q8;
    sw->to_y = cy * Q8;
    sw->started = sw->now;
    sw->state = ZUI_SWIM_ARRIVING;
}

bool zui_swim_offer(zui_swim_t *sw, zui_rect_t target, const cmp_decision_t *d,
                    const uint8_t digest[CMP_HASH], const char *text)
{
    if (!sw || !d || !digest || !text) return false;
    if (d->verdict != CMP_PROCEED && d->verdict != CMP_ASK) return false;
    if (d->quarantined || d->seq == 0) return false;
    if (target.w <= 0 || target.h <= 0) return false;
    if (sw->away || snoozed(sw) || sw->state == ZUI_SWIM_OPEN) return false;
    size_t n = 0;
    while (n < ZUI_SWIM_TEXT && text[n]) n++;
    if (n == 0 || n >= ZUI_SWIM_TEXT) return false;
    for (size_t i = 0; i <= n; i++) sw->text[i] = text[i];
    for (uint32_t i = 0; i < CMP_HASH; i++) sw->digest[i] = digest[i];
    sw->target = target;
    sw->has_offer = true;
    if (sw->pinned) {
        if (sw->state != ZUI_SWIM_PINNED) {
            int32_t cx, cy;
            corner(sw, &cx, &cy);
            swim_to(sw, cx, cy);
        }
        return true;
    }
    int32_t cx, cy;
    wait_point(sw, target, &cx, &cy);
    swim_to(sw, cx, cy);
    return true;
}

/* leave, or go back to the corner when pinned */
static void settle(zui_swim_t *sw)
{
    if (sw->away) {
        if (sw->state != ZUI_SWIM_HIDDEN) sw->state = ZUI_SWIM_LEAVING;
        return;
    }
    if (sw->pinned) {
        int32_t cx, cy;
        corner(sw, &cx, &cy);
        swim_to(sw, cx, cy);
        return;
    }
    if (sw->state != ZUI_SWIM_HIDDEN) sw->state = ZUI_SWIM_LEAVING;
}

void zui_swim_withdraw(zui_swim_t *sw)
{
    if (!sw || !sw->has_offer) return;
    sw->has_offer = false;
    if (sw->state != ZUI_SWIM_PINNED) settle(sw);
}

/* SW6: inside a window the target is not in */
static bool through(const zui_swim_t *sw, int32_t px, int32_t py)
{
    int32_t tx = sw->target.x + sw->target.w / 2, ty = sw->target.y + sw->target.h / 2;
    for (uint32_t i = 0; i < sw->nwin; i++)
        if (inside(sw->win[i], px, py) && !(sw->has_offer && inside(sw->win[i], tx, ty)))
            return true;
    return false;
}

static void step_alpha(zui_swim_t *sw, uint32_t goal, uint32_t dt)
{
    uint32_t fd = zui_motion_duration(ZUI_SWIM_FADE_MS);
    if (fd == 0) {
        sw->alpha = goal;
        return;
    }
    uint64_t st = (uint64_t) 255u * dt / fd;
    if (st == 0 && dt) st = 1;
    if (st > 255u) st = 255u;
    if (sw->alpha < goal)
        sw->alpha = sw->alpha + (uint32_t) st > goal ? goal : sw->alpha + (uint32_t) st;
    else if (sw->alpha > goal)
        sw->alpha = sw->alpha < goal + (uint32_t) st ? goal : sw->alpha - (uint32_t) st;
}

void zui_swim_tick(zui_swim_t *sw, uint32_t now_ms)
{
    if (!sw) return;
    uint32_t dt = 0;
    if (!sw->clock_set) {
        sw->clock_set = true;
        sw->now = now_ms;
        sw->started = now_ms;
    } else {
        dt = now_ms - sw->now;
        if (dt > 0x80000000u)
            dt = 0; /* the clock stepped back: no time */
        else
            sw->now = now_ms;
    }
    if (sw->snoozing && !snoozed(sw)) sw->snoozing = false;
    bool full = zui_motion_style() == ZUI_MOTION_FULL;
    uint32_t goal = 255u;
    switch (sw->state) {
    case ZUI_SWIM_HIDDEN:
        sw->alpha = 0;
        return;
    case ZUI_SWIM_ARRIVING: {
        uint32_t p =
            full ? zui_motion_progress_q8(sw->now - sw->started, ZUI_SWIM_TRAVEL_MS) : 256u;
        if (p >= 256u) {
            sw->x = sw->to_x;
            sw->y = sw->to_y;
            sw->state = sw->pinned      ? ZUI_SWIM_PINNED
                        : sw->has_offer ? ZUI_SWIM_WAITING
                                        : ZUI_SWIM_LEAVING;
            break;
        }
        int64_t e = zui_motion_ease_q8(p);
        int64_t dx = (int64_t) sw->to_x - sw->from_x, dy = (int64_t) sw->to_y - sw->from_y;
        int64_t x = sw->from_x + dx * e / 256, y = sw->from_y + dy * e / 256;
        uint32_t len = isqrt64((uint64_t) (dx * dx + dy * dy));
        if (len) {
            /* SW5: the wave, zero at both ends */
            int64_t env = 4 * (int64_t) p * (256 - (int64_t) p) / 256;
            int64_t off = (int64_t) ZUI_SWIM_WAVE_PX * Q8 * sin256(p) / 1024 * env / 256;
            x += off * -dy / len;
            y += off * dx / len;
        }
        sw->x = (int32_t) x;
        sw->y = (int32_t) y;
        if (through(sw, sw->x / Q8, sw->y / Q8)) goal = ZUI_SWIM_THROUGH_A;
        break;
    }
    case ZUI_SWIM_WAITING: {
        sw->x = sw->to_x;
        sw->y = sw->to_y;
        if (full) {
            uint32_t ph = (sw->now % ZUI_SWIM_BOB_MS) * 256u / ZUI_SWIM_BOB_MS;
            sw->y += 3 * Q8 * sin256(ph) / 1024;
        }
        break;
    }
    case ZUI_SWIM_OPEN:
    case ZUI_SWIM_PINNED:
        sw->x = sw->to_x;
        sw->y = sw->to_y;
        break;
    case ZUI_SWIM_LEAVING:
        goal = 0;
        break;
    }
    step_alpha(sw, goal, dt);
    if (sw->state == ZUI_SWIM_LEAVING && sw->alpha == 0) sw->state = ZUI_SWIM_HIDDEN;
}

/* ---- the bubble -------------------------------------------------------- */

zui_rect_t zui_swim_bubble(const zui_swim_t *sw)
{
    zui_rect_t b = {0, 0, 0, 0};
    if (!sw) return b;
    int32_t cx = sw->to_x / Q8, cy = sw->to_y / Q8;
    zui_rect_t w = sw->work;
    b.w = ZUI_SWIM_BUBBLE_W;
    b.h = ZUI_SWIM_BUBBLE_H;
    b.x = cx - EXT - b.w >= w.x ? cx - EXT - b.w : cx + EXT;
    b.y = clampi(cy - b.h / 2, w.y, w.y + w.h - b.h);
    if (b.x + b.w > w.x + w.w) b.x = w.x + w.w - b.w;
    if (b.x < w.x) b.x = w.x;
    return b;
}

zui_rect_t zui_swim_extent(const zui_swim_t *sw)
{
    zui_rect_t r = {0, 0, 0, 0};
    if (!sw || sw->state == ZUI_SWIM_HIDDEN || sw->alpha == 0) return r;
    r.x = sw->x / Q8 - ZUI_SWIM_BOX / 2;
    r.y = sw->y / Q8 - ZUI_SWIM_BOX / 2;
    r.w = r.h = ZUI_SWIM_BOX;
    if (sw->state == ZUI_SWIM_OPEN) {
        zui_rect_t b = zui_swim_bubble(sw);
        int32_t x1 = r.x + r.w > b.x + b.w ? r.x + r.w : b.x + b.w;
        int32_t y1 = r.y + r.h > b.y + b.h ? r.y + r.h : b.y + b.h;
        r.x = r.x < b.x ? r.x : b.x;
        r.y = r.y < b.y ? r.y : b.y;
        r.w = x1 - r.x;
        r.h = y1 - r.y;
    }
    return r;
}

zui_rect_t zui_swim_button(const zui_swim_t *sw, uint32_t i)
{
    zui_rect_t b = zui_swim_bubble(sw), r = {0, 0, 0, 0};
    if (!sw || i > 2u) return r;
    int32_t bw = (b.w - 2 * 8 - 2 * 6) / 3;
    r.x = b.x + 8 + (int32_t) i * (bw + 6);
    r.y = b.y + b.h - 8 - ZUI_SWIM_BUTTON_H;
    r.w = bw;
    r.h = ZUI_SWIM_BUTTON_H;
    return r;
}

static bool visible(const zui_swim_t *sw)
{
    return sw->state != ZUI_SWIM_HIDDEN && sw->state != ZUI_SWIM_LEAVING && sw->alpha > 0;
}

bool zui_swim_hit(const zui_swim_t *sw, int32_t x, int32_t y)
{
    if (!sw || !visible(sw)) return false;
    if (sw->state == ZUI_SWIM_OPEN && inside(zui_swim_bubble(sw), x, y)) return true;
    int64_t dx = (int64_t) x - sw->x / Q8, dy = (int64_t) y - sw->y / Q8;
    return dx * dx + dy * dy <= (int64_t) ZUI_SWIM_R * ZUI_SWIM_R;
}

/* stop where it is and open the bubble there */
static zui_swim_act_t open_here(zui_swim_t *sw)
{
    if (!sw->has_offer) return ZUI_SWIM_NONE;
    sw->to_x = sw->x;
    sw->to_y = sw->y;
    sw->state = ZUI_SWIM_OPEN;
    return ZUI_SWIM_OPENED;
}

zui_swim_act_t zui_swim_open(zui_swim_t *sw)
{
    if (!sw || !visible(sw) || sw->state == ZUI_SWIM_OPEN) return ZUI_SWIM_NONE;
    return open_here(sw);
}

zui_swim_act_t zui_swim_tap(zui_swim_t *sw, int32_t x, int32_t y, uint8_t digest_out[CMP_HASH])
{
    if (!sw || !zui_swim_hit(sw, x, y)) return ZUI_SWIM_NONE;
    if (sw->state == ZUI_SWIM_OPEN) {
        if (inside(zui_swim_button(sw, 0), x, y)) {
            if (digest_out)
                for (uint32_t i = 0; i < CMP_HASH; i++) digest_out[i] = sw->digest[i];
            sw->has_offer = false;
            settle(sw);
            return ZUI_SWIM_ACCEPTED;
        }
        if (inside(zui_swim_button(sw, 1), x, y)) {
            sw->has_offer = false;
            sw->snoozing = true;
            sw->snooze_until = sw->now + ZUI_SWIM_SNOOZE_MS;
            settle(sw);
            return ZUI_SWIM_DISMISSED;
        }
        if (inside(zui_swim_button(sw, 2), x, y)) {
            bool on = !sw->pinned;
            zui_swim_pin(sw, on);
            return on ? ZUI_SWIM_PIN : ZUI_SWIM_UNPIN;
        }
        if (!inside(zui_swim_bubble(sw), x, y)) {
            /* the body: close the bubble */
            if (sw->pinned) {
                sw->state = ZUI_SWIM_PINNED;
            } else {
                sw->state = ZUI_SWIM_WAITING;
            }
            return ZUI_SWIM_CLOSED;
        }
        return ZUI_SWIM_NONE; /* on the bubble, not on a button: nothing */
    }
    return open_here(sw);
}

void zui_swim_pin(zui_swim_t *sw, bool on)
{
    if (!sw) return;
    sw->pinned = on;
    if (sw->away) return;
    if (on) {
        settle(sw);
        return;
    }
    if (sw->has_offer) {
        int32_t cx, cy;
        wait_point(sw, sw->target, &cx, &cy);
        swim_to(sw, cx, cy);
    } else if (sw->state != ZUI_SWIM_HIDDEN) {
        sw->state = ZUI_SWIM_LEAVING;
    }
}

void zui_swim_away(zui_swim_t *sw, bool on)
{
    if (!sw) return;
    sw->away = on;
    if (on) {
        sw->has_offer = false;
        if (sw->state != ZUI_SWIM_HIDDEN) sw->state = ZUI_SWIM_LEAVING;
    } else if (sw->pinned) {
        settle(sw);
    }
}

/* ---- drawing ------------------------------------------------------------ */

zui_swim_state_t zui_swim_state(const zui_swim_t *sw)
{
    return sw ? sw->state : ZUI_SWIM_HIDDEN;
}

void zui_swim_pos(const zui_swim_t *sw, int32_t *x, int32_t *y)
{
    if (x) *x = sw ? sw->x / Q8 : 0;
    if (y) *y = sw ? sw->y / Q8 : 0;
}

uint32_t zui_swim_alpha(const zui_swim_t *sw)
{
    if (!sw || sw->state == ZUI_SWIM_HIDDEN) return 0;
    return zui_motion_alpha(sw->alpha);
}

int32_t zui_swim_depth(const zui_swim_t *sw)
{
    if (!sw || sw->state != ZUI_SWIM_ARRIVING) return 0;
    return through(sw, sw->x / Q8, sw->y / Q8) ? -ZUI_SWIM_DEPTH_PX : 0;
}

/* the next word's end after `at` (spaces before it included) */
static size_t word_end(const char *txt, size_t at)
{
    while (txt[at] == ' ') at++;
    while (txt[at] && txt[at] != ' ') at++;
    return at;
}

/* the words, wrapped onto up to three lines inside the bubble; the last
 * line takes the rest, cut with an ellipsis */
static void words(zui_surface_t *s, const zui_text_t *t, zui_rect_t b, const char *txt)
{
    char line[ZUI_SWIM_TEXT];
    int32_t lh = zui_text_ascent(t) - zui_text_descent(t) + 2;
    int32_t maxw = b.w - 24, base = b.y + 10 + zui_text_ascent(t);
    size_t at = 0;
    while (txt[at] == ' ') at++;
    for (int ln = 0; ln < 3 && txt[at]; ln++) {
        size_t end = word_end(txt, at); /* at least one word */
        if (ln < 2) {
            for (;;) {
                size_t e = word_end(txt, end), k = 0;
                if (e == end) break;
                for (size_t i = at; i < e; i++) line[k++] = txt[i];
                line[k] = 0;
                if (zui_text_width(t, line) > maxw) break;
                end = e;
            }
        } else {
            while (txt[end]) end++;
        }
        size_t k = 0;
        for (size_t i = at; i < end; i++) line[k++] = txt[i];
        line[k] = 0;
        zui_text_fit(s, t, b.x + 12, base, maxw, ZUI_TEXT, line);
        base += lh;
        at = end;
        while (txt[at] == ' ') at++;
    }
}

static void bubble(zui_swim_t *sw, zui_surface_t *s, const zui_text_t *t, uint32_t a)
{
    static const char *const LABEL[3] = {"Accept", "Not now", "Pin"};
    zui_rect_t b = zui_swim_bubble(sw);
    zui_fill_rrect(s, b, 12, ZUI_PANEL, zui_motion_alpha(a * 235u / 255u));
    zui_stroke_rrect(s, b, 12, 1, ZUI_GOLD, a);
    if (t) words(s, t, b, sw->text);
    for (uint32_t i = 0; i < 3; i++) {
        zui_rect_t r = zui_swim_button(sw, i);
        zui_stroke_rrect(s, r, 6, 1, i == 0 ? ZUI_GOLD : ZUI_EDGE, a);
        if (!t) continue;
        const char *lb = i == 2 && sw->pinned ? "Unpin" : LABEL[i];
        int32_t tw = zui_text_width(t, lb);
        int32_t by = r.y + (r.h + zui_text_ascent(t) + zui_text_descent(t)) / 2;
        zui_text_fit(s, t, r.x + (r.w - tw) / 2, by, r.w - 4, i == 0 ? ZUI_GOLD : ZUI_TEXT, lb);
    }
}

void zui_swim_draw(zui_swim_t *sw, zui_surface_t *s, const zui_text_t *text,
                   const zui_depth_t *depth, int eye)
{
    if (!sw || !s || sw->state == ZUI_SWIM_HIDDEN || sw->alpha == 0) return;
    uint32_t a = zui_motion_alpha(sw->alpha);
    zui_chiglet_t look = sw->look;
    if (sw->state == ZUI_SWIM_OPEN) look.mood = ZUI_CHG_SPEAKING;
    uint32_t tick = zui_motion_style() == ZUI_MOTION_FULL ? sw->now / 250u : 0u;
    zui_surface_t sb, swh;
    zui_surface_init(&sb, sw->on_black, ZUI_SWIM_BOX, ZUI_SWIM_BOX, ZUI_SWIM_BOX);
    zui_surface_init(&swh, sw->on_white, ZUI_SWIM_BOX, ZUI_SWIM_BOX, ZUI_SWIM_BOX);
    zui_fill(&sb, (zui_rect_t){0, 0, ZUI_SWIM_BOX, ZUI_SWIM_BOX}, 0x000000u);
    zui_fill(&swh, (zui_rect_t){0, 0, ZUI_SWIM_BOX, ZUI_SWIM_BOX}, 0xFFFFFFu);
    zui_chiglet_draw(&sb, &look, ZUI_SWIM_BOX / 2, ZUI_SWIM_BOX / 2, (int32_t) ZUI_SWIM_R, tick);
    zui_chiglet_draw(&swh, &look, ZUI_SWIM_BOX / 2, ZUI_SWIM_BOX / 2, (int32_t) ZUI_SWIM_R, tick);
    int32_t z = zui_swim_depth(sw), dx = 0, dy = 0;
    if (depth) zui_layer_offset(depth, z, eye, &dx, &dy);
    int32_t ox = sw->x / Q8 - ZUI_SWIM_BOX / 2 + dx, oy = sw->y / Q8 - ZUI_SWIM_BOX / 2 + dy;
    for (int32_t j = 0; j < ZUI_SWIM_BOX; j++)
        for (int32_t i = 0; i < ZUI_SWIM_BOX; i++) {
            uint32_t k = (uint32_t) (j * ZUI_SWIM_BOX + i), pb = sw->on_black[k],
                     pw = sw->on_white[k];
            uint32_t gap = 0;
            for (uint32_t sh = 0; sh < 24; sh += 8) {
                uint32_t cb = (pb >> sh) & 255u, cw = (pw >> sh) & 255u;
                uint32_t d = cw > cb ? cw - cb : 0;
                if (d > gap) gap = d;
            }
            uint32_t cov = 255u - gap; /* the drawing's own coverage */
            if (cov == 0) continue;
            uint32_t rgb = 0;
            for (uint32_t sh = 0; sh < 24; sh += 8) {
                uint32_t c = ((pb >> sh) & 255u) * 255u / cov;
                rgb |= (c > 255u ? 255u : c) << sh;
            }
            if (depth) rgb = zui_chroma_px(depth, rgb, z);
            zui_blend_px(s, ox + i, oy + j, rgb, cov * a / 255u);
        }
    if (sw->state == ZUI_SWIM_PINNED && sw->has_offer) {
        /* SW4: the gold dot, an offer is waiting */
        int32_t cx = sw->x / Q8 + (int32_t) ZUI_SWIM_R - 4,
                cy = sw->y / Q8 - (int32_t) ZUI_SWIM_R + 4;
        zui_fill_rrect(s, (zui_rect_t){cx - 5, cy - 5, 10, 10}, 5, ZUI_GOLD, a);
    }
    if (sw->state == ZUI_SWIM_OPEN) bubble(sw, s, text, a);
}

/* ---- SW9 ---------------------------------------------------------------- */

static size_t put(char *buf, size_t cap, size_t n, const char *s)
{
    while (*s) {
        if (n + 1 < cap) buf[n] = *s;
        n++;
        s++;
    }
    if (cap) buf[n + 1 < cap ? n : cap - 1] = 0;
    return n;
}

size_t zui_swim_describe(const zui_swim_t *sw, char *buf, size_t cap)
{
    if (!buf && cap) return 0;
    size_t n = 0;
    if (cap) buf[0] = 0;
    if (!sw) return 0;
    if (sw->away) return put(buf, cap, n, "Your chiglet is away.");
    if (sw->state == ZUI_SWIM_OPEN) {
        n = put(buf, cap, n, "Your chiglet suggests: ");
        n = put(buf, cap, n, sw->text);
        return put(buf, cap, n,
                   sw->pinned ? " Accept, Not now, or Unpin." : " Accept, Not now, or Pin.");
    }
    if (sw->has_offer && sw->state != ZUI_SWIM_HIDDEN && sw->state != ZUI_SWIM_LEAVING)
        return put(buf, cap, n, "Your chiglet has a suggestion. Open it to read it.");
    if (sw->pinned) return put(buf, cap, n, "Your chiglet is pinned in the corner.");
    return put(buf, cap, n, "Your chiglet has nothing to suggest.");
}
