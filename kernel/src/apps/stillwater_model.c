/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* stillwater_model.c — Stillwater (see stillwater_model.h). */
#include <string.h>

#include "stillwater_model.h"

void stillwater_init(stillwater_t *s, zxm_link_t *link)
{
    memset(s, 0, sizeof *s);
    s->link = link;
}

static bool good_name(const char *x, size_t max)
{
    size_t n = x ? strlen(x) : 0;
    return n >= 1 && n <= max;
}

int stillwater_set_schedule(stillwater_t *s, uint8_t mode, uint8_t days, uint16_t from, uint16_t to)
{
    if (!stillwater_schedule_fits(mode, days, from, to) ||
        !stillwater_record_fits(0, mode, days, from, to, s->sched.nallowed))
        return ZXM_E_ARG;
    s->sched.mode = mode;
    s->sched.days = days;
    s->sched.from = from;
    s->sched.to = to;
    return ZXM_OK;
}

static int32_t allowed_at(const stillwater_schedule_t *sc, const char *source)
{
    for (uint32_t i = 0; source && i < sc->nallowed; i++)
        if (!strcmp(sc->allowed[i], source)) return (int32_t) i;
    return -1;
}

bool stillwater_allowed(const stillwater_t *s, const char *source)
{
    return allowed_at(&s->sched, source) >= 0;
}

int stillwater_allow(stillwater_t *s, const char *source)
{
    if (!good_name(source, STILLWATER_SOURCE)) return ZXM_E_ARG;
    if (allowed_at(&s->sched, source) >= 0) return ZXM_OK;
    if (s->sched.nallowed >= STILLWATER_ALLOWED) return ZXM_E_STATE;
    memcpy(s->sched.allowed[s->sched.nallowed], source, strlen(source) + 1);
    s->sched.nallowed++;
    return ZXM_OK;
}

int stillwater_disallow(stillwater_t *s, const char *source)
{
    int32_t i = good_name(source, STILLWATER_SOURCE) ? allowed_at(&s->sched, source) : -1;
    if (i < 0) return ZXM_E_ARG;
    stillwater_schedule_t *sc = &s->sched;
    for (uint32_t k = (uint32_t) i; k + 1 < sc->nallowed; k++)
        memcpy(sc->allowed[k], sc->allowed[k + 1], sizeof sc->allowed[k]);
    sc->nallowed--;
    memset(sc->allowed[sc->nallowed], 0, sizeof sc->allowed[sc->nallowed]);
    return ZXM_OK;
}

int stillwater_quiet_at(const stillwater_schedule_t *sc, uint32_t weekday, uint32_t minute)
{
    if (weekday > 6 || minute >= STILLWATER_DAY_MIN) return -1;
    if (sc->mode == STILLWATER_FOCUS) return 1;
    if (sc->mode != STILLWATER_QUIET) return 0;
    if (!stillwater_window(sc->from, sc->to, minute)) return 0;
    /* past midnight in a window that crossed it: the day it began */
    uint32_t day = sc->from > sc->to && minute < sc->to ? (weekday + 6u) % 7u : weekday;
    return (sc->days >> day) & 1u;
}

static int u32(zxm_link_t *l, int32_t o, const char *key, uint32_t max, uint32_t *v)
{
    uint64_t x;
    if (zxm_get_u64(l, o, key, &x) || x > max) return ZXM_E_PROTOCOL;
    *v = (uint32_t) x;
    return ZXM_OK;
}

static const stillwater_note_t *note_in(const stillwater_note_t *n, uint32_t count, uint32_t seq)
{
    for (uint32_t i = 0; i < count; i++)
        if (n[i].seq == seq) return &n[i];
    return NULL;
}

const stillwater_note_t *stillwater_note(const stillwater_t *s, uint32_t seq)
{
    return note_in(s->note, s->nnote, seq);
}

static int read_note(zxm_link_t *l, int32_t o, stillwater_note_t *n)
{
    uint32_t pri;
    memset(n, 0, sizeof *n);
    if (o < 0 || u32(l, o, "seq", UINT32_MAX, &n->seq) || !n->seq ||
        u32(l, o, "pri", STILLWATER_PRI_URGENT, &pri) ||
        u32(l, o, "count", UINT32_MAX, &n->count) || !n->count ||
        zxm_get_bool(l, o, "read", &n->read) ||
        zxm_get_str(l, o, "kind", n->kind, sizeof n->kind) ||
        zxm_get_str(l, o, "source", n->source, sizeof n->source) ||
        zxm_get_str(l, o, "title", n->title, sizeof n->title))
        return ZXM_E_PROTOCOL;
    n->pri = (uint8_t) pri;
    n->call = !strcmp(n->kind, "call-incoming") || !strcmp(n->kind, "call-missed");
    n->emergency = n->pri == STILLWATER_PRI_URGENT && !strcmp(n->source, STILLWATER_EMERGENCY);
    n->gap_s = -1;
    return ZXM_OK;
}

/* ST3: time the calls this note stands for against its caller's last one. */
static void time_call(stillwater_t *t, const stillwater_t *was, stillwater_note_t *n,
                      const stillwater_note_t *before, int64_t now)
{
    uint32_t fresh = before ? (n->count > before->count ? n->count - before->count : 0) : n->count;
    if (before) n->gap_s = before->gap_s;
    if (!n->call || !fresh || !n->source[0]) return;
    stillwater_caller_t *c = NULL;
    for (uint32_t i = 0; i < t->ncaller && !c; i++)
        if (!strcmp(t->caller[i].source, n->source)) c = &t->caller[i];
    if (fresh >= 2 || (c && c->at_s == now)) /* two calls since the refresh before */
        n->gap_s = was->seen ? now - was->at_s : -1;
    else
        n->gap_s = c ? now - c->at_s : -1;
    if (!c) {
        uint32_t k = 0;
        if (t->ncaller < STILLWATER_CALLERS)
            k = t->ncaller++;
        else
            for (uint32_t i = 1; i < STILLWATER_CALLERS; i++)
                if (t->caller[i].at_s < t->caller[k].at_s) k = i; /* the longest quiet goes */
        c = &t->caller[k];
        memcpy(c->source, n->source, sizeof c->source);
    }
    c->at_s = now;
}

static int read_notes(stillwater_t *t, const stillwater_t *was, zxm_link_t *l, int64_t now)
{
    uint32_t unread;
    int32_t arr = zxm_get(l, l->result, "notes");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > STILLWATER_NOTES ||
        u32(l, l->result, "unread", UINT32_MAX, &unread))
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    for (uint32_t i = 0; i < n; i++) {
        if (read_note(l, zxm_at(l, arr, i), &t->note[i])) return ZXM_E_PROTOCOL;
        if (note_in(t->note, i, t->note[i].seq)) return ZXM_E_PROTOCOL; /* one seq once */
    }
    t->nnote = n;
    t->unread = unread;
    t->held = 0;
    for (uint32_t i = n; i-- > 0;) { /* oldest first, so each call follows the one before */
        stillwater_note_t *x = &t->note[i];
        time_call(t, was, x, note_in(was->note, was->nnote, x->seq), now);
        x->allowed = stillwater_allowed(t, x->source);
        x->through = stillwater_through(t->quiet, x->allowed, x->gap_s, x->emergency);
        t->held += !x->read && !x->through;
    }
    return ZXM_OK;
}

static int read_limits(stillwater_t *t, zxm_link_t *l)
{
    int32_t arr = zxm_get(l, l->result, "limits");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > STILLWATER_APPS ||
        zxm_get_bool(l, l->result, "locked", &t->locked))
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    for (uint32_t i = 0; i < n; i++) {
        stillwater_limit_t *m = &t->lim[i];
        int32_t o = zxm_at(l, arr, i), mm = o < 0 ? -1 : zxm_get(l, o, "minutes");
        uint32_t from, to, cap = 0;
        memset(m, 0, sizeof *m);
        if (mm < 0 || zxm_get_str(l, o, "app", m->app, sizeof m->app) || !m->app[0] ||
            u32(l, o, "from", STILLWATER_DAY_MIN - 1, &from) ||
            u32(l, o, "to", STILLWATER_DAY_MIN - 1, &to) ||
            (l->tok[mm].type != W4_J_NULL && u32(l, o, "minutes", STILLWATER_DAY_MIN, &cap)))
            return ZXM_E_PROTOCOL;
        m->minutes = l->tok[mm].type == W4_J_NULL ? -1 : (int32_t) cap;
        m->from = (uint16_t) from;
        m->to = (uint16_t) to;
    }
    t->nlim = n;
    return ZXM_OK;
}

int stillwater_refresh(stillwater_t *s, int64_t now_s, uint32_t weekday, uint32_t minute)
{
    int q = stillwater_quiet_at(&s->sched, weekday, minute);
    if (q < 0 || now_s < 0 || (s->seen && now_s < s->at_s)) return ZXM_E_ARG;
    static stillwater_t t; /* the new view, kept apart until it is whole (ST5) */
    t = *s;
    t.quiet = q == 1;
    zxm_link_t *l = s->link;
    zxm_begin(l, "notes.list");
    int rc = zxm_call(l);
    if (rc) return rc;
    if ((rc = read_notes(&t, s, l, now_s)) != ZXM_OK) return rc;
    zxm_begin(l, "screen.status");
    zxm_p_u64(l, "days", 1);
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    if ((rc = read_limits(&t, l)) != ZXM_OK) return rc;
    t.seen = true;
    t.at_s = now_s;
    *s = t;
    return ZXM_OK;
}

int stillwater_check_app(stillwater_t *s, const char *app)
{
    if (!good_name(app, STILLWATER_APP)) return ZXM_E_ARG;
    zxm_link_t *l = s->link;
    zxm_begin(l, "screen.check");
    zxm_p_str(l, "app", app);
    int rc = zxm_call(l);
    if (rc) return rc;
    stillwater_check_t c;
    uint32_t used;
    int64_t left;
    memset(&c, 0, sizeof c);
    if (zxm_get_str(l, l->result, "app", c.app, sizeof c.app) || strcmp(c.app, app) ||
        zxm_get_bool(l, l->result, "allowed", &c.allowed) ||
        u32(l, l->result, "used", STILLWATER_DAY_MIN, &used) ||
        zxm_get_i64(l, l->result, "left", &left) || left < -1 || left > INT32_MAX ||
        zxm_get_str(l, l->result, "why", c.why, sizeof c.why) || (c.allowed && c.why[0]))
        return ZXM_E_PROTOCOL;
    c.used = used;
    c.left = (int32_t) left;
    s->check = c;
    return ZXM_OK;
}

int stillwater_mark_read(stillwater_t *s, uint32_t *marked)
{
    zxm_link_t *l = s->link;
    uint32_t n;
    zxm_begin(l, "notes.mark_read");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (u32(l, l->result, "marked", UINT32_MAX, &n)) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < s->nnote; i++) s->note[i].read = true;
    s->unread = 0;
    s->held = 0;
    if (marked) *marked = n;
    return ZXM_OK;
}
