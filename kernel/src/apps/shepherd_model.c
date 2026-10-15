/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* shepherd_model.c — Shepherd (see shepherd_model.h). */
#include <stdio.h>
#include <string.h>

#include "shepherd_model.h"

const zxm_ai_action_t SHEPHERD_AI[SHEPHERD_AI_COUNT] = {
    {"Suggest a fair plan", "Companion", 400},
};

void shepherd_init(shepherd_t *s, zxm_link_t *link)
{
    memset(s, 0, sizeof *s);
    s->link = link;
}

static int u32(zxm_link_t *l, int32_t o, const char *key, uint32_t max, uint32_t *v)
{
    uint64_t x;
    if (zxm_get_u64(l, o, key, &x) || x > max) return ZXM_E_PROTOCOL;
    *v = (uint32_t) x;
    return ZXM_OK;
}

static bool good_app(const char *a)
{
    size_t n = a ? strlen(a) : 0;
    return n >= 1 && n <= SHEPHERD_APP;
}

static int read_limits(shepherd_t *s, zxm_link_t *l, int32_t arr)
{
    uint32_t n = zxm_len(l, arr);
    if (n > SHEPHERD_APPS) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < n; i++) {
        static shepherd_limit_t t;
        int32_t o = zxm_at(l, arr, i), m = o < 0 ? -1 : zxm_get(l, o, "minutes");
        uint32_t from, to;
        memset(&t, 0, sizeof t);
        if (m < 0 || zxm_get_str(l, o, "app", t.app, sizeof t.app) || !t.app[0] ||
            u32(l, o, "from", 1439, &from) || u32(l, o, "to", 1439, &to) ||
            u32(l, o, "extra", 1440, &t.extra))
            return ZXM_E_PROTOCOL;
        if (l->tok[m].type == W4_J_NULL) {
            t.minutes = -1;
        } else {
            uint32_t mm;
            if (u32(l, o, "minutes", 1440, &mm)) return ZXM_E_PROTOCOL;
            t.minutes = (int32_t) mm;
        }
        t.from = (uint16_t) from;
        t.to = (uint16_t) to;
        s->lim[i] = t;
    }
    s->nlim = n;
    return ZXM_OK;
}

static int read_days(shepherd_t *s, zxm_link_t *l, int32_t arr)
{
    uint32_t n = zxm_len(l, arr);
    if (n > SHEPHERD_DAYS) return ZXM_E_PROTOCOL;
    for (uint32_t d = 0; d < n; d++) {
        static shepherd_day_t y;
        int32_t o = zxm_at(l, arr, d), apps = o < 0 ? -1 : zxm_get(l, o, "apps");
        memset(&y, 0, sizeof y);
        if (apps < 0 || l->tok[apps].type != W4_J_ARR || u32(l, o, "date", 99991231, &y.date) ||
            u32(l, o, "total", 1440, &y.total) || zxm_len(l, apps) > SHEPHERD_APPS ||
            (d && y.date >= s->day[d - 1].date))
            return ZXM_E_PROTOCOL;
        y.n = zxm_len(l, apps);
        uint32_t sum = 0;
        for (uint32_t i = 0; i < y.n; i++) {
            int32_t a = zxm_at(l, apps, i);
            if (a < 0 || zxm_get_str(l, a, "app", y.a[i].app, sizeof y.a[i].app) ||
                !y.a[i].app[0] || u32(l, a, "minutes", 1440, &y.a[i].minutes))
                return ZXM_E_PROTOCOL;
            sum += y.a[i].minutes;
        }
        if (sum != y.total) return ZXM_E_PROTOCOL;
        s->day[d] = y;
    }
    s->ndays = n;
    return ZXM_OK;
}

int shepherd_refresh(shepherd_t *s)
{
    zxm_link_t *l = s->link;
    zxm_begin(l, "screen.status");
    zxm_p_u64(l, "days", SHEPHERD_DAYS);
    int rc = zxm_call(l);
    if (rc) return rc;
    bool locked, reset;
    uint32_t wait, today;
    int32_t lim = zxm_get(l, l->result, "limits"), days = zxm_get(l, l->result, "days");
    if (lim < 0 || days < 0 || l->tok[lim].type != W4_J_ARR || l->tok[days].type != W4_J_ARR ||
        zxm_get_bool(l, l->result, "locked", &locked) ||
        zxm_get_bool(l, l->result, "reset", &reset) || u32(l, l->result, "wait_s", 3600, &wait) ||
        u32(l, l->result, "today", 99991231, &today))
        return ZXM_E_PROTOCOL;
    static shepherd_t keep;
    keep = *s;
    if ((rc = read_limits(s, l, lim)) != ZXM_OK || (rc = read_days(s, l, days)) != ZXM_OK ||
        (s->ndays && s->day[0].date > today)) {
        *s = keep; /* what was shown stays */
        return ZXM_E_PROTOCOL;
    }
    s->locked = locked;
    s->reset = reset;
    s->wait_s = wait;
    s->today = today;
    return ZXM_OK;
}

static void p_pass(zxm_link_t *l, const char *key, const char *pass)
{
    if (pass && pass[0]) zxm_p_str(l, key, pass);
}

static int done(shepherd_t *s)
{
    int rc = zxm_call(s->link);
    return rc ? rc : shepherd_refresh(s);
}

int shepherd_set_limit(shepherd_t *s, const char *pass, const char *app, int32_t minutes,
                       uint16_t from, uint16_t to)
{
    if (!good_app(app) || minutes < -1 || minutes > 1440 || from > 1439 || to > 1439)
        return ZXM_E_ARG;
    zxm_link_t *l = s->link;
    zxm_begin(l, "screen.set_limit");
    zxm_p_str(l, "app", app);
    if (minutes >= 0) zxm_p_u64(l, "minutes", (uint64_t) minutes);
    if (from) zxm_p_u64(l, "from", from);
    if (to) zxm_p_u64(l, "to", to);
    p_pass(l, "passphrase", pass);
    return done(s);
}

int shepherd_clear_limit(shepherd_t *s, const char *pass, const char *app)
{
    if (!good_app(app)) return ZXM_E_ARG;
    zxm_begin(s->link, "screen.clear_limit");
    zxm_p_str(s->link, "app", app);
    p_pass(s->link, "passphrase", pass);
    return done(s);
}

int shepherd_grant(shepherd_t *s, const char *pass, const char *app, uint32_t minutes)
{
    if (!good_app(app) || minutes < 1 || minutes > 1440) return ZXM_E_ARG;
    zxm_begin(s->link, "screen.grant");
    zxm_p_str(s->link, "app", app);
    zxm_p_u64(s->link, "minutes", minutes);
    p_pass(s->link, "passphrase", pass);
    return done(s);
}

int shepherd_lock(shepherd_t *s, const char *old, const char *pass)
{
    if (pass && strlen(pass) > SHEPHERD_PASS) return ZXM_E_ARG;
    zxm_begin(s->link, "screen.lock");
    zxm_p_str(s->link, "passphrase", pass ? pass : "");
    p_pass(s->link, "old", old);
    return done(s);
}

const shepherd_limit_t *shepherd_limit(const shepherd_t *s, const char *app)
{
    for (uint32_t i = 0; app && i < s->nlim; i++)
        if (!strcmp(s->lim[i].app, app)) return &s->lim[i];
    return NULL;
}

uint32_t shepherd_week(const shepherd_t *s, const char *app)
{
    uint32_t sum = 0;
    for (uint32_t d = 0; app && d < s->ndays; d++)
        for (uint32_t i = 0; i < s->day[d].n; i++)
            if (!strcmp(s->day[d].a[i].app, app)) sum += s->day[d].a[i].minutes;
    return sum;
}

uint32_t shepherd_today_total(const shepherd_t *s)
{
    return s->ndays && s->day[0].date == s->today ? s->day[0].total : 0;
}

int shepherd_ai(shepherd_t *s, int32_t action)
{
    if (action < 0 || action >= SHEPHERD_AI_COUNT) return ZXM_E_ARG;
    if (!s->ndays) return ZXM_E_STATE; /* nothing used yet */
    static char in[ZXM_ASK_MAX];
    size_t n = 0;
    int k = snprintf(in, sizeof in, "Minutes used each day, newest first:\n");
    n = k > 0 ? (size_t) k : 0;
    for (uint32_t d = 0; d < s->ndays && n < sizeof in; d++) {
        const shepherd_day_t *y = &s->day[d];
        k = snprintf(in + n, sizeof in - n, "%u: %u in all", y->date, y->total);
        n = k >= 0 && (size_t) k < sizeof in - n ? n + (size_t) k : sizeof in;
        for (uint32_t i = 0; i < y->n && n < sizeof in; i++) {
            k = snprintf(in + n, sizeof in - n, ", %s %u", y->a[i].app, y->a[i].minutes);
            n = k >= 0 && (size_t) k < sizeof in - n ? n + (size_t) k : sizeof in;
        }
        if (n < sizeof in) in[n++] = '\n';
    }
    for (uint32_t i = 0; i < s->nlim && n < sizeof in; i++) {
        const shepherd_limit_t *t = &s->lim[i];
        k = snprintf(in + n, sizeof in - n, "Limit %s: %d minutes a day, hours %u-%u\n",
                     strcmp(t->app, SHEPHERD_ALL) ? t->app : "all apps", (int) t->minutes, t->from,
                     t->to);
        n = k >= 0 && (size_t) k < sizeof in - n ? n + (size_t) k : sizeof in;
    }
    if (n >= sizeof in) n = zxm_utf8_cut(in, sizeof in - 1);
    return zxm_ai_ask(s->link, &SHEPHERD_AI[action], action,
                      "This is a week of screen time on a family computer, and the limits set "
                      "so far (-1 minutes: no daily cap). Suggest a fair daily plan in plain "
                      "words: minutes per app and hours, leaving room for school and creative "
                      "work. The guardian decides; change nothing. Be brief.",
                      in, (uint32_t) n, &s->held);
}

int shepherd_ai_accept(shepherd_t *s)
{
    return zxm_held_accept(&s->held);
}

int shepherd_ai_reject(shepherd_t *s)
{
    return zxm_held_reject(&s->held);
}
