/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_lock.c — the lock screen and idle auto-lock (zui_lock.h). */
#include "zui_lock.h"
#include "zui_tokens.h"

/* zui_palette.h C6: every colour by role, in the active set */
#define C(role) zui_color(l->colors, ZUI_C_##role)

static uint32_t put(char *out, uint32_t cap, uint32_t n, const char *s);
static uint32_t put_num(char *out, uint32_t cap, uint32_t n, int32_t v, int width);

static int32_t imin(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

static int32_t imax(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

static bool inside(zui_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static void copy(char *d, uint32_t cap, const char *s)
{
    uint32_t i = 0;
    for (; s && s[i] && i + 1u < cap; i++) d[i] = s[i];
    if (s && s[i] && ((uint8_t) s[i] & 0xC0u) == 0x80u) { /* cut inside a character: drop it */
        while (i > 0 && ((uint8_t) d[i - 1] & 0xC0u) == 0x80u) i--;
        if (i > 0) i--;
    }
    if (cap) d[i] = 0;
}

/* L6: writes through a volatile pointer, so they are not optimised away. */
static void wipe(zui_lock_t *l)
{
    volatile uint8_t *p = l->secret;
    for (uint32_t i = 0; i < ZUI_LOCK_SECRET_MAX; i++) p[i] = 0;
    l->nsecret = 0;
}

void zui_lock_defaults(zui_lock_config_t *c)
{
    c->idle_ms = 5u * 60u * 1000u;
    c->dim_ms = 10u * 1000u;
    c->grace_ms = 5u * 1000u;
    c->free_tries = 3;
    c->backoff_ms = 30u * 1000u;
    c->backoff_max_ms = 15u * 60u * 1000u;
    c->lock_key = 'l';
    c->lock_mods = ZUI_MOD_CMD | ZUI_MOD_SHIFT;
    c->previews = false;
    c->h24 = true;
}

void zui_lock_init(zui_lock_t *l, const zui_lock_config_t *cfg, uint32_t now_ms)
{
    uint8_t *z = (uint8_t *) l;
    for (uint32_t i = 0; i < sizeof *l; i++) z[i] = 0;
    if (cfg)
        l->cfg = *cfg;
    else
        zui_lock_defaults(&l->cfg);
    if (l->cfg.dim_ms > l->cfg.idle_ms) l->cfg.dim_ms = l->cfg.idle_ms;
    l->state = ZUI_LOCK_UNLOCKED;
    l->now = l->last_input = now_ms;
}

void zui_lock_account(zui_lock_t *l, uint32_t account, const char *name)
{
    l->account = account;
    copy(l->account_name, sizeof l->account_name, name);
}

void zui_lock_set_area(zui_lock_t *l, zui_rect_t area)
{
    l->area = area;
}

void zui_lock_set_auth(zui_lock_t *l, zui_lock_auth_fn auth, void *ctx)
{
    l->auth = auth;
    l->auth_ctx = ctx;
}

bool zui_lock_failure_shown(const zui_lock_t *l)
{
    if (l->state == ZUI_LOCK_FAILED) return true;
    return l->state == ZUI_LOCK_LOCKED && l->said && l->now - l->said_at < ZUI_LOCK_SAY_MS;
}

static void go(zui_lock_t *l, zui_lock_state_t to)
{
    zui_lock_state_t from = l->state;
    if (from == to) return;
    l->state = to;
    if (to == ZUI_LOCK_UNLOCKED) {
        wipe(l);
        l->nnotice = 0; /* L7 */
        l->last_input = l->now;
    }
    if (l->on_state) l->on_state(l->on_state_ctx, from, to);
}

static void lock(zui_lock_t *l, bool by_idle)
{
    wipe(l);
    l->by_idle = by_idle;
    l->locked_at = l->now;
    go(l, ZUI_LOCK_LOCKED);
}

bool zui_lock_is_up(const zui_lock_t *l)
{
    return l->state >= ZUI_LOCK_LOCKED;
}

void zui_lock_now(zui_lock_t *l)
{
    if (zui_lock_is_up(l)) {
        wipe(l);
        l->by_idle = false;
        return;
    }
    lock(l, false);
}

void zui_lock_tick(zui_lock_t *l, uint32_t now_ms)
{
    l->now = now_ms;
    uint32_t idle = now_ms - l->last_input;
    switch (l->state) {
    case ZUI_LOCK_UNLOCKED:
    case ZUI_LOCK_DIMMING:
        if (!l->cfg.idle_ms) break;
        if (idle >= l->cfg.idle_ms)
            lock(l, true);
        else if (l->cfg.dim_ms && idle >= l->cfg.idle_ms - l->cfg.dim_ms)
            go(l, ZUI_LOCK_DIMMING);
        break;
    case ZUI_LOCK_FAILED:
        if ((int32_t) (now_ms - l->retry_at) >= 0) go(l, ZUI_LOCK_LOCKED);
        break;
    case ZUI_LOCK_LOCKED:
        if (l->nsecret && now_ms - l->last_typed >= ZUI_LOCK_TYPED_MS) wipe(l); /* L6 */
        break;
    default:
        break;
    }
}

uint32_t zui_lock_wait_ms(const zui_lock_t *l)
{
    if (l->state != ZUI_LOCK_FAILED) return 0;
    int32_t left = (int32_t) (l->retry_at - l->now);
    return left > 0 ? (uint32_t) left : 0u;
}

uint32_t zui_lock_dim(const zui_lock_t *l)
{
    if (l->state != ZUI_LOCK_DIMMING || !l->cfg.dim_ms)
        return l->state >= ZUI_LOCK_LOCKED ? 256 : 0;
    uint32_t into = (l->now - l->last_input) - (l->cfg.idle_ms - l->cfg.dim_ms);
    if (into > l->cfg.dim_ms) into = l->cfg.dim_ms;
    return (uint32_t) (((uint64_t) into * 256u) / l->cfg.dim_ms);
}

uint32_t zui_lock_typed_chars(const zui_lock_t *l)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < l->nsecret; i++) n += (l->secret[i] & 0xC0u) != 0x80u;
    return n;
}

const char *zui_lock_state_name(zui_lock_state_t s)
{
    static const char *const N[] = {"unlocked", "dimming", "locked", "authenticating", "failed"};
    return (uint32_t) s < 5u ? N[s] : "?";
}

/* L5: how long the n-th failure waits. */
static uint32_t backoff(const zui_lock_config_t *c, uint32_t fails)
{
    if (fails <= c->free_tries) return 0;
    uint64_t w = c->backoff_ms;
    for (uint32_t i = c->free_tries + 1u; i < fails && w < c->backoff_max_ms; i++) w *= 2u;
    return w > c->backoff_max_ms ? c->backoff_max_ms : (uint32_t) w;
}

static void failed(zui_lock_t *l)
{
    l->fails++;
    l->by_idle = false; /* L4: no grace after a failure */
    l->retry_at = l->now + backoff(&l->cfg, l->fails);
    l->said = true; /* L10 */
    l->said_at = l->now;
    go(l, ZUI_LOCK_FAILED);
}

static void answer(zui_lock_t *l, int rc)
{
    if (rc == ZUI_AUTH_OK) {
        l->fails = 0;
        l->said = false;
        go(l, ZUI_LOCK_UNLOCKED);
    } else if (rc == ZUI_AUTH_PENDING) {
        go(l, ZUI_LOCK_AUTHENTICATING);
    } else {
        failed(l);
    }
}

bool zui_lock_submit(zui_lock_t *l)
{
    if (l->state != ZUI_LOCK_LOCKED || !l->nsecret) return false;
    int rc = l->auth ? l->auth(l->auth_ctx, l->account, l->secret, l->nsecret) : ZUI_AUTH_FAIL;
    wipe(l); /* L6: right after the call, whatever it said */
    answer(l, rc == ZUI_AUTH_OK || rc == ZUI_AUTH_PENDING ? rc : ZUI_AUTH_FAIL);
    return true;
}

void zui_lock_auth_result(zui_lock_t *l, bool ok)
{
    if (l->state != ZUI_LOCK_AUTHENTICATING) return;
    answer(l, ok ? ZUI_AUTH_OK : ZUI_AUTH_FAIL);
}

void zui_lock_emergency(zui_lock_t *l)
{
    if (!zui_lock_is_up(l)) return;
    l->emergencies++;
    if (l->emergency) l->emergency(l->emergency_ctx);
}

static bool is_input(uint8_t t)
{
    switch (t) {
    case ZUI_EV_KEY_DOWN:
    case ZUI_EV_KEY_UP:
    case ZUI_EV_TEXT:
    case ZUI_EV_PREEDIT:
    case ZUI_EV_PTR_DOWN:
    case ZUI_EV_PTR_MOVE:
    case ZUI_EV_PTR_UP:
    case ZUI_EV_SCROLL:
    case ZUI_EV_TOUCH_BEGIN:
    case ZUI_EV_TOUCH_MOVE:
    case ZUI_EV_TOUCH_END:
    case ZUI_EV_TOUCH_CANCEL:
        return true;
    default:
        return false;
    }
}

static void type(zui_lock_t *l, const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    if (l->nsecret + n > ZUI_LOCK_SECRET_MAX) return; /* a whole event or none */
    for (uint32_t i = 0; i < n; i++) l->secret[l->nsecret++] = (uint8_t) s[i];
    l->last_typed = l->now;
}

static void backspace(zui_lock_t *l)
{
    if (!l->nsecret) return;
    uint32_t at = l->nsecret - 1u;
    while (at > 0 && (l->secret[at] & 0xC0u) == 0x80u) at--;
    volatile uint8_t *p = l->secret;
    for (uint32_t i = at; i < l->nsecret; i++) p[i] = 0;
    l->nsecret = at;
}

/* The lock screen's own handling (L1, L4..L8). */
static void locked_event(zui_lock_t *l, const zui_event_t *e)
{
    if (e->type == ZUI_EV_PTR_DOWN || e->type == ZUI_EV_TOUCH_BEGIN) {
        zui_lock_layout_t lay;
        zui_lock_layout(l, l->area, &lay);
        if (inside(lay.emergency, e->x, e->y)) {
            zui_lock_emergency(l);
            return;
        }
    }
    if (l->state == ZUI_LOCK_LOCKED && l->by_idle && is_input(e->type) &&
        l->now - l->locked_at < l->cfg.grace_ms) {
        go(l, ZUI_LOCK_UNLOCKED); /* L4 */
        return;
    }
    if (l->state == ZUI_LOCK_FAILED) {
        if (zui_lock_wait_ms(l)) return; /* L5: waiting; typing is ignored */
        go(l, ZUI_LOCK_LOCKED);
    }
    if (l->state != ZUI_LOCK_LOCKED) return; /* authenticating: wait */
    if (e->type == ZUI_EV_TEXT) {
        type(l, e->text);
    } else if (e->type == ZUI_EV_KEY_DOWN) {
        if (e->key == ZUI_KEY_BACKSPACE)
            backspace(l);
        else if (e->key == ZUI_KEY_ESCAPE)
            wipe(l);
        else if (e->key == ZUI_KEY_ENTER)
            zui_lock_submit(l);
    }
}

bool zui_lock_event(zui_lock_t *l, const zui_event_t *e)
{
    if (e->type == ZUI_EV_TICK) {
        zui_lock_tick(l, e->ms);
        return zui_lock_is_up(l);
    }
    zui_lock_tick(l, e->ms);
    bool input = is_input(e->type);
    if (!zui_lock_is_up(l)) {
        if (input) {
            l->last_input = l->now; /* L2 */
            if (l->state == ZUI_LOCK_DIMMING) go(l, ZUI_LOCK_UNLOCKED);
        }
        if (e->type == ZUI_EV_KEY_DOWN && l->cfg.lock_key &&
            (e->key == l->cfg.lock_key ||
             (e->key >= 'A' && e->key <= 'Z' && e->key + 32u == l->cfg.lock_key)) &&
            (e->mods & l->cfg.lock_mods) == l->cfg.lock_mods) {
            zui_lock_now(l); /* L3 */
            return true;
        }
        return false;
    }
    locked_event(l, e);
    return true; /* L1: nothing passes the lock */
}

/* ---- notifications (L7) ------------------------------------------------- */

void zui_lock_notify(zui_lock_t *l, const char *app, const char *text)
{
    if (!zui_lock_is_up(l)) return; /* unlocked: the apps show their own */
    if (l->nnotice == ZUI_LOCK_NOTICES) {
        for (uint32_t i = 1; i < ZUI_LOCK_NOTICES; i++) l->notice[i - 1] = l->notice[i];
        l->nnotice--;
    }
    zui_lock_notice_t *n = &l->notice[l->nnotice++];
    copy(n->app, sizeof n->app, app);
    copy(n->text, sizeof n->text, text);
}

uint32_t zui_lock_summary(const zui_lock_t *l, char *out, uint32_t cap)
{
    uint32_t n = put_num(out, cap, 0, (int32_t) l->nnotice, 1);
    return put(out, cap, n, l->nnotice == 1 ? " notification" : " notifications");
}

const char *zui_lock_notice_text(const zui_lock_t *l, uint32_t i)
{
    if (i >= l->nnotice) return "";
    return l->cfg.previews ? l->notice[i].text : "Notification";
}

/* ---- time and date (L9) ------------------------------------------------- */

void zui_civil(int64_t epoch_s, int32_t tz_min, zui_civil_t *o)
{
    int64_t t = epoch_s + (int64_t) tz_min * 60;
    int64_t days = t / 86400, secs = t % 86400;
    if (secs < 0) {
        secs += 86400;
        days--;
    }
    o->hour = (int32_t) (secs / 3600);
    o->min = (int32_t) (secs / 60 % 60);
    o->sec = (int32_t) (secs % 60);
    o->wday = (int32_t) (((days % 7) + 11) % 7); /* 1970-01-01 was a Thursday */
    /* days to a proleptic Gregorian date, in 400-year eras */
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    o->day = (int32_t) (doy - (153 * mp + 2) / 5 + 1);
    o->month = (int32_t) (mp < 10 ? mp + 3 : mp - 9);
    o->year = (int32_t) (yoe + era * 400 + (o->month <= 2 ? 1 : 0));
}

static uint32_t put(char *out, uint32_t cap, uint32_t n, const char *s)
{
    while (s && *s && n + 1u < cap) out[n++] = *s++;
    if (cap) out[n < cap ? n : cap - 1u] = 0;
    return n;
}

static uint32_t put_num(char *out, uint32_t cap, uint32_t n, int32_t v, int width)
{
    char d[12];
    int k = 0;
    bool neg = v < 0;
    uint32_t u = neg ? (uint32_t) (-(int64_t) v) : (uint32_t) v;
    do {
        d[k++] = (char) ('0' + u % 10u);
        u /= 10u;
    } while (u);
    while (k < width) d[k++] = '0';
    if (neg) d[k++] = '-';
    char s[13];
    for (int i = 0; i < k; i++) s[i] = d[k - 1 - i];
    s[k] = 0;
    return put(out, cap, n, s);
}

uint32_t zui_lock_time_text(char *out, uint32_t cap, int64_t epoch_s, int32_t tz_min, bool h24)
{
    zui_civil_t c;
    zui_civil(epoch_s, tz_min, &c);
    uint32_t n = 0;
    if (h24) {
        n = put_num(out, cap, n, c.hour, 2);
    } else {
        int32_t h = c.hour % 12;
        n = put_num(out, cap, n, h ? h : 12, 1);
    }
    n = put(out, cap, n, ":");
    n = put_num(out, cap, n, c.min, 2);
    if (!h24) n = put(out, cap, n, c.hour < 12 ? " AM" : " PM");
    return n;
}

uint32_t zui_lock_date_text(char *out, uint32_t cap, int64_t epoch_s, int32_t tz_min)
{
    static const char *const WD[] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                                     "Thursday", "Friday", "Saturday"};
    static const char *const MO[] = {"January",   "February", "March",    "April",
                                     "May",       "June",     "July",     "August",
                                     "September", "October",  "November", "December"};
    zui_civil_t c;
    zui_civil(epoch_s, tz_min, &c);
    uint32_t n = put(out, cap, 0, WD[c.wday]);
    n = put(out, cap, n, " ");
    n = put_num(out, cap, n, c.day, 1);
    n = put(out, cap, n, " ");
    n = put(out, cap, n, MO[c.month - 1]);
    n = put(out, cap, n, " ");
    return put_num(out, cap, n, c.year, 1);
}

/* ---- layout and drawing (L9) ------------------------------------------- */

void zui_lock_layout(const zui_lock_t *l, zui_rect_t a, zui_lock_layout_t *o)
{
    (void) l;
    int32_t cx = a.x + a.w / 2;
    int32_t m = imin(128, imax(48, imin(a.w, a.h) / 8));
    o->mark = (zui_rect_t){cx - m / 2, a.y + imax(ZUI_S4, a.h / 14), m, m};
    int32_t big = imax(48, imin(a.w / 6, a.h / 8)); /* the time's line */
    int32_t y = o->mark.y + m + ZUI_S5;
    int32_t tw = imin(a.w - 2 * ZUI_S4, big * 4);
    o->time = (zui_rect_t){cx - tw / 2, y, tw, big};
    y += big + ZUI_S2;
    int32_t dw = imin(a.w - 2 * ZUI_S4, 480);
    o->date = (zui_rect_t){cx - dw / 2, y, dw, 24};
    y += 24 + ZUI_S6;
    int32_t fw = imin(a.w - 2 * ZUI_S5, 320);
    o->account = (zui_rect_t){cx - fw / 2, y, fw, 22};
    y += 22 + ZUI_S2;
    o->field = (zui_rect_t){cx - fw / 2, y, fw, 40};
    y += 40 + ZUI_S2;
    o->message = (zui_rect_t){cx - fw / 2, y, fw, 20};
    y += 20 + ZUI_S4;
    int32_t nw = imin(a.w - 2 * ZUI_S5, 400);
    for (uint32_t i = 0; i < ZUI_LOCK_NOTICES; i++) {
        o->notice[i] = (zui_rect_t){cx - nw / 2, y, nw, 44};
        y += 44 + ZUI_S2;
    }
    int32_t ew = 140, eh = 36;
    o->emergency = (zui_rect_t){cx - ew / 2, a.y + a.h - eh - ZUI_S4, ew, eh};
}

static int32_t baseline(const zui_text_t *t, zui_rect_t r)
{
    return r.y + (r.h + zui_text_ascent(t) + zui_text_descent(t)) / 2;
}

static void centred(zui_surface_t *s, const zui_text_t *t, zui_rect_t r, uint32_t rgb,
                    const char *str)
{
    if (!t || !t->font) return;
    int32_t w = imin(zui_text_width(t, str), r.w);
    zui_text_fit(s, t, r.x + (r.w - w) / 2, baseline(t, r), r.w, rgb, str);
}

/* Engraved text: the shade of the cut above, the light lip below. */
static void engraved(const zui_lock_t *l, zui_surface_t *s, const zui_text_t *t, zui_rect_t r,
                     uint32_t rgb, const char *str)
{
    if (!t || !t->font) return;
    int32_t w = imin(zui_text_width(t, str), r.w), x = r.x + (r.w - w) / 2, b = baseline(t, r);
    zui_text_a(s, t, x, b - 1, C(ENGRAVE_LO), 255, str);
    zui_text_a(s, t, x, b + 1, C(ENGRAVE_HI), 50, str);
    zui_text(s, t, x, b, rgb, str);
}

static void field(zui_lock_t *l, zui_surface_t *s, zui_rect_t f, uint32_t tick)
{
    bool bad = zui_lock_failure_shown(l);
    zui_fill_rrect(s, f, ZUI_R_CONTROL, C(RAISED), 255);
    zui_engrave(s, f, ZUI_R_CONTROL, C(ENGRAVE_LO), C(ENGRAVE_HI), 70u);
    if (bad) zui_stroke_rrect(s, f, ZUI_R_CONTROL, 1, C(ALERT), 255);
    uint32_t n = zui_lock_typed_chars(l);
    int32_t dot = 8, step = 14, x = f.x + ZUI_S3, cy = f.y + f.h / 2;
    for (uint32_t i = 0; i < n && x + dot < f.x + f.w - ZUI_S3; i++, x += step)
        zui_fill_rrect(s, (zui_rect_t){x, cy - dot / 2, dot, dot}, dot / 2, C(TEXT), 255);
    if (!n && l->text && l->text->font)
        zui_text_fit(s, l->text, f.x + ZUI_S3, baseline(l->text, f), f.w - 2 * ZUI_S3, C(FAINT),
                     "Password");
    if (l->state == ZUI_LOCK_LOCKED && (tick / 500u) % 2u == 0) /* the caret, gold */
        zui_vline(s, n ? x : f.x + ZUI_S3, f.y + ZUI_S2, f.h - 2 * ZUI_S2, 2, C(GOLD));
}

void zui_lock_draw(zui_lock_t *l, zui_surface_t *s, zui_rect_t area, int64_t epoch_s,
                   int32_t tz_min, uint32_t tick)
{
    l->area = area;
    if (l->state == ZUI_LOCK_UNLOCKED) return;
    if (l->state == ZUI_LOCK_DIMMING) {
        zui_fill_a(s, area, 0x000000u, zui_lock_dim(l) * 200u / 256u);
        return;
    }
    zui_lock_layout_t lay;
    zui_lock_layout(l, area, &lay);
    zui_clip(s, area);
    zui_fill(s, area, C(VOID));
    if (l->mark) l->mark(s, lay.mark, 256, l->mark_ctx);
    char buf[64];
    zui_lock_time_text(buf, sizeof buf, epoch_s, tz_min, l->cfg.h24);
    engraved(l, s, l->big, lay.time, C(TEXT), buf);
    zui_lock_date_text(buf, sizeof buf, epoch_s, tz_min);
    engraved(l, s, l->text, lay.date, C(TEXT_2), buf);
    centred(s, l->text, lay.account, C(TEXT), l->account_name);
    field(l, s, lay.field, tick);
    if (l->state == ZUI_LOCK_AUTHENTICATING) {
        zui_rect_t bar = {lay.message.x, lay.message.y + lay.message.h / 2 - 1, lay.message.w, 3};
        zui_bar_draw_in(s, bar, ZUI_BAR_GATHER, ZUI_BAR_UNKNOWN, tick, 0, 0, l->colors);
    } else if (zui_lock_failure_shown(l)) { /* L10 */
        uint32_t wait = zui_lock_wait_ms(l);
        uint32_t n = put(buf, sizeof buf, 0, "Wrong password");
        if (wait) {
            n = put(buf, sizeof buf, n, ". Try again in ");
            n = put_num(buf, sizeof buf, n, (int32_t) ((wait + 999u) / 1000u), 1);
            put(buf, sizeof buf, n, " s");
        }
        centred(s, l->text, lay.message, C(ALERT), buf);
    }
    if (!l->cfg.previews && l->nnotice) { /* L7: how many, and from whom */
        zui_rect_t r = lay.notice[0];
        char apps[96];
        uint32_t n = 0;
        for (uint32_t i = 0; i < l->nnotice; i++) {
            bool seen = false;
            for (uint32_t j = 0; j < i && !seen; j++) {
                const char *a = l->notice[i].app, *b = l->notice[j].app;
                uint32_t k = 0;
                while (a[k] && a[k] == b[k]) k++;
                seen = a[k] == b[k];
            }
            if (seen) continue;
            if (n) n = put(apps, sizeof apps, n, ", ");
            n = put(apps, sizeof apps, n, l->notice[i].app);
        }
        if (!n) apps[0] = 0;
        zui_lock_summary(l, buf, sizeof buf);
        zui_fill_rrect(s, r, ZUI_R_CONTROL, C(PANEL), 255);
        zui_engrave(s, r, ZUI_R_CONTROL, C(ENGRAVE_LO), C(ENGRAVE_HI), 50u);
        if (l->text && l->text->font) {
            zui_text_fit(s, l->text, r.x + ZUI_S3, r.y + 18, r.w - 2 * ZUI_S3, C(TEXT_2), buf);
            zui_text_fit(s, l->text, r.x + ZUI_S3, r.y + 36, r.w - 2 * ZUI_S3, C(MUTED), apps);
        }
    }
    for (uint32_t i = 0; l->cfg.previews && i < l->nnotice; i++) {
        zui_rect_t r = lay.notice[i];
        if (r.y + r.h > lay.emergency.y - ZUI_S2) break;
        zui_fill_rrect(s, r, ZUI_R_CONTROL, C(PANEL), 255);
        zui_engrave(s, r, ZUI_R_CONTROL, C(ENGRAVE_LO), C(ENGRAVE_HI), 50u);
        if (l->text && l->text->font) {
            zui_text_fit(s, l->text, r.x + ZUI_S3, r.y + 18, r.w - 2 * ZUI_S3, C(TEXT_2),
                         l->notice[i].app);
            zui_text_fit(s, l->text, r.x + ZUI_S3, r.y + 36, r.w - 2 * ZUI_S3, C(MUTED),
                         zui_lock_notice_text(l, i));
        }
    }
    zui_engrave(s, lay.emergency, ZUI_R_CONTROL, C(ENGRAVE_LO), C(ENGRAVE_HI), 70u);
    zui_stroke_rrect(s, lay.emergency, ZUI_R_CONTROL, 1, C(EDGE), 255);
    centred(s, l->text, lay.emergency, C(TEXT_2), "Emergency");
    zui_unclip(s);
}
