/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* wake_model.c — Wake (see wake_model.h). */
#include <stdio.h>
#include <string.h>

#include "wake_model.h"

const zxm_ai_action_t WAKE_AI[WAKE_AI_COUNT] = {
    {"Explain these warnings", "Companion", 600},
};

void wake_init(wake_t *w, zxm_link_t *link)
{
    memset(w, 0, sizeof *w);
    w->link = link;
}

static void push(wake_t *w, const wake_event_t *e)
{
    if (w->n == WAKE_KEEP) { /* full: the oldest goes */
        w->first = (w->first + 1) % WAKE_KEEP;
        w->n--;
    }
    w->ev[(w->first + w->n) % WAKE_KEEP] = *e;
    w->n++;
}

static int level_of(const char *s)
{
    return !strcmp(s, "error")  ? WAKE_ERROR
           : !strcmp(s, "warn") ? WAKE_WARN
           : !strcmp(s, "info") ? WAKE_INFO
                                : -1;
}

/* one entry of log.tail or journal.query */
static int read_event(zxm_link_t *l, int32_t o, wake_event_t *e)
{
    char lv[8];
    memset(e, 0, sizeof *e);
    if (o < 0 || zxm_get_u64(l, o, "seq", &e->seq) || zxm_get_u64(l, o, "time_ms", &e->time_ms) ||
        zxm_get_str(l, o, "level", lv, sizeof lv) || level_of(lv) < 0 ||
        zxm_get_str(l, o, "source", e->source, sizeof e->source) ||
        zxm_get_str(l, o, "text", e->text, sizeof e->text))
        return ZXM_E_PROTOCOL;
    e->level = (uint8_t) level_of(lv);
    return ZXM_OK;
}

int wake_follow(wake_t *w, uint32_t *added)
{
    zxm_link_t *l = w->link;
    *added = 0;
    if (w->paused) return ZXM_OK;
    for (int round = 0; round < 8; round++) { /* 8 x 256 covers zxvd's whole ring */
        zxm_begin(l, "log.tail");
        zxm_p_u64(l, "since", w->next);
        zxm_p_u64(l, "max", 256);
        int rc = zxm_call(l);
        if (rc) return rc;
        uint64_t next, dropped;
        int32_t arr = zxm_get(l, l->result, "entries");
        if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_get_u64(l, l->result, "next", &next) ||
            zxm_get_u64(l, l->result, "dropped", &dropped) || next < w->next)
            return ZXM_E_PROTOCOL;
        uint32_t n = zxm_len(l, arr);
        for (uint32_t i = 0; i < n; i++) {
            static wake_event_t e;
            if (read_event(l, zxm_at(l, arr, i), &e) || e.seq <= w->next || e.seq > next)
                return ZXM_E_PROTOCOL;
            push(w, &e);
            (*added)++;
        }
        w->missed += dropped;
        bool more = n == 256 && next > w->next;
        w->next = next;
        if (!more) break;
    }
    return ZXM_OK;
}

static const char *const LEVEL_NAME[] = {"info", "warn", "error"};

static int history_page(wake_t *w, uint64_t before)
{
    zxm_link_t *l = w->link;
    wake_history_t *h = &w->hist;
    zxm_begin(l, "journal.query");
    if (h->since_ms) zxm_p_u64(l, "since_ms", h->since_ms);
    if (h->until_ms) zxm_p_u64(l, "until_ms", h->until_ms);
    if (before) zxm_p_u64(l, "before", before);
    zxm_p_u64(l, "max", WAKE_PAGE);
    zxm_p_str(l, "level", LEVEL_NAME[w->min_level]);
    if (w->find[0]) zxm_p_str(l, "text", w->find);
    int rc = zxm_call(l);
    if (rc) return rc;
    bool on, more;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_get_bool(l, l->result, "on", &on) ||
        zxm_get_bool(l, l->result, "more", &more) || zxm_len(l, arr) > WAKE_PAGE)
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    for (uint32_t i = 0; i < n; i++) { /* newest first, all older than `before` */
        static wake_event_t e;
        if (read_event(l, zxm_at(l, arr, i), &e) || (before && e.seq >= before) ||
            (i && e.seq >= h->ev[i - 1].seq) || e.level < w->min_level)
            return ZXM_E_PROTOCOL;
        h->ev[i] = e;
    }
    h->n = n;
    h->on = on;
    h->more = more && n > 0;
    return ZXM_OK;
}

int wake_history(wake_t *w, uint64_t since_ms, uint64_t until_ms)
{
    if (until_ms && since_ms > until_ms) return ZXM_E_ARG;
    memset(&w->hist, 0, sizeof w->hist);
    w->hist.since_ms = since_ms;
    w->hist.until_ms = until_ms;
    int rc = history_page(w, 0);
    if (rc) {
        memset(&w->hist, 0, sizeof w->hist);
        return rc;
    }
    w->hist.page = 1;
    return ZXM_OK;
}

int wake_history_older(wake_t *w)
{
    wake_history_t *h = &w->hist;
    if (!h->page || !h->more || !h->n) return ZXM_E_ARG;
    static wake_history_t keep;
    keep = *h;
    int rc = history_page(w, h->ev[h->n - 1].seq);
    if (rc) {
        *h = keep; /* the page shown stays */
        return rc;
    }
    h->page++;
    return ZXM_OK;
}

int wake_check(wake_t *w)
{
    zxm_link_t *l = w->link;
    memset(&w->check, 0, sizeof w->check);
    zxm_begin(l, "journal.verify");
    int rc = zxm_call(l);
    if (rc) return rc;
    wake_check_t c;
    memset(&c, 0, sizeof c);
    uint64_t segs;
    if (zxm_get_bool(l, l->result, "on", &c.on) || zxm_get_bool(l, l->result, "ok", &c.ok) ||
        zxm_get_u64(l, l->result, "segments", &segs) || segs > UINT32_MAX ||
        zxm_get_u64(l, l->result, "entries", &c.entries) ||
        zxm_get_u64(l, l->result, "first_seq", &c.first_seq) ||
        zxm_get_u64(l, l->result, "last_seq", &c.last_seq) ||
        zxm_get_u64(l, l->result, "broken_at", &c.broken_at) ||
        zxm_get_str(l, l->result, "why", c.why, sizeof c.why) ||
        (c.ok && (c.broken_at || c.why[0])) || (!c.ok && c.on && !c.why[0]))
        return ZXM_E_PROTOCOL;
    c.segments = (uint32_t) segs;
    c.done = true;
    w->check = c;
    return ZXM_OK;
}

void wake_pause(wake_t *w, bool paused)
{
    w->paused = paused;
}

void wake_clear(wake_t *w)
{
    w->first = w->n = 0;
    w->missed = 0;
}

int wake_filter(wake_t *w, uint8_t min_level, const char *find)
{
    if (min_level > WAKE_ERROR || strlen(find) > WAKE_FIND) return ZXM_E_ARG;
    w->min_level = min_level;
    memcpy(w->find, find, strlen(find) + 1);
    return ZXM_OK;
}

static char low(char c)
{
    return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
}

static bool holds(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    if (!nl) return true;
    for (const char *h = hay; *h; h++) {
        size_t i = 0;
        while (i < nl && h[i] && low(h[i]) == low(needle[i])) i++;
        if (i == nl) return true;
    }
    return false;
}

static bool passes(const wake_t *w, const wake_event_t *e)
{
    return e->level >= w->min_level && (holds(e->text, w->find) || holds(e->source, w->find));
}

uint32_t wake_count(const wake_t *w)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < w->n; i++) k += passes(w, &w->ev[(w->first + i) % WAKE_KEEP]);
    return k;
}

const wake_event_t *wake_event(const wake_t *w, uint32_t want)
{
    for (uint32_t i = 0, k = 0; i < w->n; i++) {
        const wake_event_t *e = &w->ev[(w->first + i) % WAKE_KEEP];
        if (passes(w, e) && k++ == want) return e;
    }
    return NULL;
}

int wake_ai(wake_t *w, int32_t action)
{
    if (action < 0 || action >= WAKE_AI_COUNT) return ZXM_E_ARG;
    static char in[ZXM_ASK_MAX];
    uint32_t n = 0;
    /* the newest warnings and errors that fit, oldest of them first */
    uint32_t i = w->n;
    while (i > 0) {
        const wake_event_t *e = &w->ev[(w->first + i - 1) % WAKE_KEEP];
        if (e->level >= WAKE_WARN) {
            size_t need = strlen(e->source) + strlen(e->text) + 4;
            if (n + need >= sizeof in) break;
            n += need;
        }
        i--;
    }
    uint32_t k = 0;
    for (; i < w->n; i++) {
        const wake_event_t *e = &w->ev[(w->first + i) % WAKE_KEEP];
        if (e->level < WAKE_WARN) continue;
        int m = snprintf(in + k, sizeof in - k, "%s: %s\n", e->source, e->text);
        if (m < 0 || (size_t) m >= sizeof in - k) break;
        k += (uint32_t) m;
    }
    if (!k) return ZXM_E_STATE; /* nothing to explain */
    return zxm_ai_ask(w->link, &WAKE_AI[action], action,
                      "These are recent warnings from ZXV's daemon. Explain in plain words what "
                      "went wrong and what the owner could do. Be brief.",
                      in, k, &w->held);
}

int wake_ai_accept(wake_t *w)
{
    return zxm_held_accept(&w->held);
}

int wake_ai_reject(wake_t *w)
{
    return zxm_held_reject(&w->held);
}
