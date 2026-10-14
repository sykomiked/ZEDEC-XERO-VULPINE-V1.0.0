/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_privacy.c — sensor in-use tracking and the indicator (zui_privacy.h). */
#include "zui_privacy.h"

#include "zui_tokens.h"

static bool same(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; a[i] && a[i] == b[i]; i++) {
    }
    return a[i] == b[i];
}

static void copy(char *d, uint32_t cap, const char *s)
{
    uint32_t i = 0;
    for (; s[i] && i + 1u < cap; i++) d[i] = s[i];
    /* never cut inside a UTF-8 character */
    while (i > 0 && s[i] && ((uint8_t) s[i] & 0xC0u) == 0x80u) i--;
    d[i] = 0;
}

static void zero(void *v, uint32_t n)
{
    uint8_t *z = v;
    for (uint32_t i = 0; i < n; i++) z[i] = 0;
}

void zui_privacy_init(zui_privacy_t *p)
{
    zero(p, sizeof *p);
    p->next_seq = 1;
    p->linger_ms = ZUI_PRIV_LINGER_MS;
}

/* PV5: time never runs back. */
static uint64_t clock_at(zui_privacy_t *p, uint64_t now)
{
    if (now > p->now) p->now = now;
    return p->now;
}

static bool valid(const char *app, zui_sensor_t s)
{
    return app && app[0] && (uint32_t) s < (uint32_t) ZUI_SENSOR_COUNT;
}

static int32_t find(const zui_privacy_t *p, const char *app, zui_sensor_t s)
{
    char key[ZUI_PRIV_APP_MAX];
    copy(key, sizeof key, app); /* compare as stored (truncated) */
    for (uint32_t i = 0; i < ZUI_PRIV_USERS; i++)
        if (p->user[i].refs && p->user[i].sensor == (uint8_t) s && same(p->user[i].app, key))
            return (int32_t) i;
    return -1;
}

static zui_priv_log_t *log_by_seq(zui_privacy_t *p, uint32_t seq)
{
    if (!seq) return 0;
    for (uint32_t i = 0; i < p->log_n; i++) {
        zui_priv_log_t *L = &p->log[(p->log_head + ZUI_PRIV_LOG - 1u - i) % ZUI_PRIV_LOG];
        if (L->seq == seq) return L;
    }
    return 0;
}

uint32_t zui_privacy_begin(zui_privacy_t *p, const char *app, zui_sensor_t s, uint64_t now_ms)
{
    if (!p || !valid(app, s)) return 0;
    uint64_t now = clock_at(p, now_ms);
    int32_t at = find(p, app, s);
    if (at >= 0) {
        zui_priv_user_t *U = &p->user[at];
        if (U->refs == 0xFFFFFFFFu) return 0;
        U->refs++;
        zui_priv_log_t *L = log_by_seq(p, U->log);
        if (L) L->uses++;
        return U->refs;
    }
    zui_priv_user_t *U = 0;
    for (uint32_t i = 0; i < ZUI_PRIV_USERS && !U; i++)
        if (!p->user[i].refs) U = &p->user[i];
    if (!U) { /* PV4 */
        p->refused++;
        return 0;
    }
    zero(U, sizeof *U);
    copy(U->app, sizeof U->app, app);
    U->sensor = (uint8_t) s;
    U->refs = 1;
    U->since = now;
    /* PV3: a new log entry, dropping the oldest when the ring is full */
    zui_priv_log_t *L = &p->log[p->log_head];
    if (p->log_n == ZUI_PRIV_LOG)
        p->dropped++;
    else
        p->log_n++;
    p->log_head = (p->log_head + 1u) % ZUI_PRIV_LOG;
    zero(L, sizeof *L);
    L->seq = p->next_seq++;
    if (!p->next_seq) p->next_seq = 1;
    copy(L->app, sizeof L->app, app);
    L->sensor = (uint8_t) s;
    L->start = now;
    L->uses = 1;
    L->live = true;
    U->log = L->seq;
    return 1;
}

static void close_use(zui_privacy_t *p, zui_priv_user_t *U, uint64_t now)
{
    zui_priv_log_t *L = log_by_seq(p, U->log);
    if (L) {
        L->end = now;
        L->live = false;
    }
    p->last_end[U->sensor] = now;
    p->ended[U->sensor] = true;
    zero(U, sizeof *U);
}

bool zui_privacy_end(zui_privacy_t *p, const char *app, zui_sensor_t s, uint64_t now_ms)
{
    if (!p || !valid(app, s)) return false;
    uint64_t now = clock_at(p, now_ms);
    int32_t at = find(p, app, s);
    if (at < 0) return false;
    zui_priv_user_t *U = &p->user[at];
    if (--U->refs == 0) close_use(p, U, now);
    return true;
}

uint32_t zui_privacy_end_app(zui_privacy_t *p, const char *app, uint64_t now_ms)
{
    uint32_t n = 0;
    if (!p || !app || !app[0]) return 0;
    uint64_t now = clock_at(p, now_ms);
    char key[ZUI_PRIV_APP_MAX];
    copy(key, sizeof key, app);
    for (uint32_t i = 0; i < ZUI_PRIV_USERS; i++)
        if (p->user[i].refs && same(p->user[i].app, key)) {
            close_use(p, &p->user[i], now);
            n++;
        }
    return n;
}

uint32_t zui_privacy_refs(const zui_privacy_t *p, const char *app, zui_sensor_t s)
{
    if (!p || !valid(app, s)) return 0;
    int32_t at = find(p, app, s);
    return at < 0 ? 0 : p->user[at].refs;
}

zui_priv_state_t zui_privacy_state(const zui_privacy_t *p, uint64_t now_ms)
{
    zui_priv_state_t st;
    zero(&st, sizeof st);
    if (!p) return st;
    uint64_t now = now_ms > p->now ? now_ms : p->now;
    for (uint32_t i = 0; i < ZUI_PRIV_USERS; i++)
        if (p->user[i].refs) {
            st.live |= (uint8_t) ZUI_SENSOR_BIT(p->user[i].sensor);
            st.apps[p->user[i].sensor]++;
        }
    st.lit = st.live;
    for (uint32_t s = 0; s < (uint32_t) ZUI_SENSOR_COUNT; s++)
        if (p->ended[s] && now - p->last_end[s] < (uint64_t) p->linger_ms)
            st.lit |= (uint8_t) ZUI_SENSOR_BIT(s);
    return st;
}

uint32_t zui_privacy_users(const zui_privacy_t *p, zui_sensor_t s, const char **out, uint32_t cap)
{
    if (!p || (uint32_t) s >= (uint32_t) ZUI_SENSOR_COUNT || (!out && cap)) return 0;
    /* in the order they began: by their log seq, which only grows */
    uint32_t n = 0;
    uint32_t done[ZUI_PRIV_USERS] = {0};
    for (;;) {
        int32_t best = -1;
        for (uint32_t i = 0; i < ZUI_PRIV_USERS; i++) {
            const zui_priv_user_t *U = &p->user[i];
            if (!U->refs || U->sensor != (uint8_t) s || done[i]) continue;
            if (best < 0 || U->log < p->user[best].log) best = (int32_t) i;
        }
        if (best < 0) break;
        done[best] = 1;
        if (n < cap) out[n] = p->user[best].app;
        n++;
    }
    return n;
}

const zui_priv_log_t *zui_privacy_log(const zui_privacy_t *p, uint32_t i)
{
    if (!p || i >= p->log_n) return 0;
    return &p->log[(p->log_head + ZUI_PRIV_LOG - 1u - i) % ZUI_PRIV_LOG];
}

uint32_t zui_privacy_log_count(const zui_privacy_t *p)
{
    return p ? p->log_n : 0;
}

const char *zui_sensor_name(zui_sensor_t s)
{
    static const char *const N[ZUI_SENSOR_COUNT] = {"camera", "microphone", "screen capture",
                                                    "location"};
    return (uint32_t) s < (uint32_t) ZUI_SENSOR_COUNT ? N[s] : "?";
}

uint32_t zui_sensor_rgb(zui_sensor_t s)
{
    static const uint32_t C[ZUI_SENSOR_COUNT] = {ZUI_OK, ZUI_INFO, ZUI_ALERT, ZUI_TEXT_2};
    return (uint32_t) s < (uint32_t) ZUI_SENSOR_COUNT ? C[s] : ZUI_MUTED;
}

static zui_rect_t isect(zui_rect_t a, zui_rect_t b)
{
    int32_t x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
    int32_t x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int32_t y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    zui_rect_t r = {x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0};
    return r;
}

int32_t zui_privacy_draw(const zui_privacy_t *p, zui_surface_t *s, zui_rect_t r, int32_t dot,
                         uint64_t now_ms)
{
    if (!p || !s) return 0;
    zui_priv_state_t st = zui_privacy_state(p, now_ms);
    uint32_t n = 0;
    for (uint32_t i = 0; i < (uint32_t) ZUI_SENSOR_COUNT; i++)
        if (st.lit & ZUI_SENSOR_BIT(i)) n++;
    if (!n) return 0;
    if (dot < 4) dot = 4;
    int32_t gap = dot / 2 > 2 ? dot / 2 : 2, pad = dot / 2 + 1;
    int32_t w = pad * 2 + (int32_t) n * dot + ((int32_t) n - 1) * gap, h = dot + pad * 2;
    if (w > r.w) w = r.w;
    if (h > r.h) h = r.h;
    zui_rect_t pill = {r.x + r.w - w, r.y + (r.h - h) / 2, w, h};
    zui_rect_t keep = s->clip; /* a small rectangle is never overrun */
    s->clip = isect(keep, pill);
    zui_fill_rrect(s, pill, h / 2, ZUI_PANEL, 255);
    zui_engrave(s, pill, h / 2, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 48);
    int32_t x = pill.x + pad, y = pill.y + (pill.h - dot) / 2;
    for (uint32_t i = 0; i < (uint32_t) ZUI_SENSOR_COUNT; i++) {
        if (!(st.lit & ZUI_SENSOR_BIT(i))) continue;
        bool live = (st.live & ZUI_SENSOR_BIT(i)) != 0;
        zui_rect_t d = {x, y, dot, dot};
        zui_fill_rrect(s, d, dot / 2, zui_sensor_rgb((zui_sensor_t) i), live ? 255u : 110u);
        x += dot + gap;
    }
    s->clip = keep;
    return w;
}

typedef struct {
    char *out;
    uint32_t cap, n;
} buf_t;

static void put(buf_t *b, const char *t)
{
    for (; *t; t++)
        if (b->n + 1u < b->cap) b->out[b->n++] = *t;
    if (b->cap) b->out[b->n < b->cap ? b->n : b->cap - 1u] = 0;
}

uint32_t zui_privacy_describe(const zui_privacy_t *p, uint64_t now_ms, char *out, uint32_t cap)
{
    buf_t b = {out, cap, 0};
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!p) return 0;
    zui_priv_state_t st = zui_privacy_state(p, now_ms);
    if (!st.lit) {
        put(&b, "no sensors in use");
        return b.n;
    }
    bool first = true;
    for (uint32_t i = 0; i < (uint32_t) ZUI_SENSOR_COUNT; i++) {
        if (!(st.lit & ZUI_SENSOR_BIT(i))) continue;
        if (!first) put(&b, "; ");
        first = false;
        put(&b, zui_sensor_name((zui_sensor_t) i));
        if (st.live & ZUI_SENSOR_BIT(i)) {
            const char *who[ZUI_PRIV_USERS];
            uint32_t k = zui_privacy_users(p, (zui_sensor_t) i, who, ZUI_PRIV_USERS);
            put(&b, " in use by ");
            for (uint32_t j = 0; j < k && j < ZUI_PRIV_USERS; j++) {
                if (j) put(&b, ", ");
                put(&b, who[j]);
            }
        } else {
            put(&b, " used recently");
        }
    }
    return b.n;
}
