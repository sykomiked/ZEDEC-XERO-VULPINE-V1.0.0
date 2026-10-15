/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* wellspring_model.c — Wellspring, the health and fitness log (see
 * wellspring_model.h). Advice only, never a diagnosis. */
#include <string.h>

#include "wellspring_model.h"

/* ---- the rules ---------------------------------------------------------- */

int32_t wellspring_band(int64_t reading, int64_t lo, int64_t hi)
{
    if (lo > hi) return WELLSPRING_NO_RANGE;
    if (reading < lo) return -1;
    if (reading > hi) return 1;
    return 0;
}

bool wellspring_safe(int64_t bpm, int64_t sleep, int64_t water)
{
    return wellspring_band(bpm, WELLSPRING_PULSE_LO, WELLSPRING_PULSE_HI) == 0 &&
           wellspring_band(sleep, WELLSPRING_SLEEP_LO, WELLSPRING_SLEEP_HI) == 0 &&
           wellspring_band(water, WELLSPRING_WATER_LO, WELLSPRING_WATER_HI) == 0;
}

int64_t wellspring_week_total(const int64_t *amount, uint32_t n)
{
    int64_t s = 0;
    if (!amount && n) return -1;
    for (uint32_t i = 0; i < n; i++) {
        if (amount[i] < 0 || s > INT64_MAX - amount[i]) return -1;
        s += amount[i];
    }
    return s;
}

int64_t wellspring_progress_x100(int64_t done, int64_t goal)
{
    if (goal <= 0 || done < 0 || done > INT64_MAX / 10000) return -1;
    return done * 10000 / goal;
}

bool wellspring_log_ok(int64_t day, int64_t kind, int64_t amount)
{
    return day >= 0 && day <= WELLSPRING_DAY_MAX && kind >= WELLSPRING_ACTIVITY &&
           kind <= WELLSPRING_PULSE && amount >= 0 && amount <= WELLSPRING_AMOUNT_MAX;
}

static bool goal_kind(int64_t kind)
{
    return kind >= WELLSPRING_ACTIVITY && kind <= WELLSPRING_WATER;
}

/* ---- the log as text (WL1-WL4) --------------------------------------------- */

static const char HEAD[] = "wellspring 1\n";

/* Append s to out at *at, bounded by cap (room for a NUL is kept). */
static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = (uint32_t) strlen(s);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

static bool put_num(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char t[24];
    int k = 23;
    bool neg = v < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u && k > 1);
    if (neg) t[--k] = '-';
    return put(out, cap, at, t + k);
}

/* "<a> <b>\n" after lead ("goal " for a goal line, WL2; "" for an entry). */
static bool put_line(char *out, uint32_t cap, uint32_t *at, const char *lead, int64_t a, int64_t b)
{
    return put(out, cap, at, lead) && put_num(out, cap, at, a) && put(out, cap, at, " ") &&
           put_num(out, cap, at, b) && put(out, cap, at, "\n");
}

/* The whole log for w (WL1-WL3); its length, or -1 when it does not fit. */
static int32_t compose(const wellspring_t *w, char *out, uint32_t cap)
{
    uint32_t at = 0;
    if (!put(out, cap, &at, HEAD)) return -1;
    for (int32_t k = WELLSPRING_ACTIVITY; k <= WELLSPRING_WATER; k++)
        if (w->goal[k] && !put_line(out, cap, &at, "goal ", k, w->goal[k])) return -1;
    for (uint32_t i = 0; i < w->n; i++)
        if (!put_num(out, cap, &at, w->e[i].day) ||
            !put_line(out, cap, &at, " ", w->e[i].kind, w->e[i].amount))
            return -1;
    return (int32_t) at;
}

/* A plain decimal of 1 to 9 digits, no leading zero, at s[*at], ended by `end`. */
static bool get_num(const char *s, uint32_t n, uint32_t *at, char end, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    int64_t x = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9' && digits < 10u) {
        x = x * 10 + (s[i] - '0');
        i++;
        digits++;
    }
    if (!digits || digits > 9u || i >= n || s[i] != end) return false;
    if (digits > 1u && s[*at] == '0') return false;
    *v = x;
    *at = i + 1u;
    return true;
}

/* Parse text into next (WL1-WL4): false when it is not a log. */
static bool parse(wellspring_t *next, const char *s, uint32_t n)
{
    uint32_t at = (uint32_t) strlen(HEAD);
    bool goals = true;
    if (n < at || memcmp(s, HEAD, at)) return false;
    memset(next->goal, 0, sizeof next->goal);
    next->n = 0;
    while (at < n) {
        int64_t a = 0, b, c;
        if (goals && n - at >= 5u && !memcmp(s + at, "goal ", 5)) {
            at += 5u;
            if (!get_num(s, n, &at, ' ', &b) || !get_num(s, n, &at, '\n', &c) || !goal_kind(b) ||
                c > WELLSPRING_AMOUNT_MAX || next->goal[b])
                return false;
            next->goal[b] = c;
            continue;
        }
        goals = false;
        if (next->n >= WELLSPRING_ENTRIES || !get_num(s, n, &at, ' ', &a) ||
            !get_num(s, n, &at, ' ', &b) || !get_num(s, n, &at, '\n', &c) ||
            !wellspring_log_ok(a, b, c))
            return false;
        next->e[next->n++] = (wellspring_entry_t){(int32_t) a, (int32_t) b, c};
    }
    return true;
}

/* ---- zxvd ---------------------------------------------------------------- */

/* One request's text: whole lines of at most this many bytes. Each byte
 * escapes to at most two ("\n"), well inside the request line. */
#define PIECE 6000u

static int write_text(zxm_link_t *l, const char *text, uint32_t len)
{
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at;
        if (n > PIECE) {
            n = PIECE;
            while (n && text[at + n - 1u] != '\n') n--;
            if (!n) return ZXM_E_TOOBIG;
        }
        bool last = at + n >= len;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", WELLSPRING_LOG);
        zxm_p_strn(l, "text", text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

/* Write next whole, then keep it (WL5). */
static int commit(wellspring_t *w, const wellspring_t *next)
{
    static char text[WELLSPRING_TEXT_MAX];
    if (!w->loaded) return ZXM_E_STATE;
    int32_t len = compose(next, text, sizeof text);
    if (len < 0) return ZXM_E_TOOBIG;
    int rc = write_text(w->link, text, (uint32_t) len);
    if (rc) return rc;
    memcpy(w->goal, next->goal, sizeof w->goal);
    memcpy(w->e, next->e, (size_t) next->n * sizeof w->e[0]);
    w->n = next->n;
    return ZXM_OK;
}

void wellspring_init(wellspring_t *w, zxm_link_t *link)
{
    memset(w, 0, sizeof *w);
    w->link = link;
    w->goal[WELLSPRING_ACTIVITY] = WELLSPRING_GOAL_ACTIVITY;
    w->goal[WELLSPRING_SLEEP] = WELLSPRING_GOAL_SLEEP;
    w->goal[WELLSPRING_WATER] = WELLSPRING_GOAL_WATER;
}

int wellspring_load(wellspring_t *w)
{
    static char text[WELLSPRING_TEXT_MAX];
    static char chunk[WELLSPRING_TEXT_MAX];
    static wellspring_t next;
    zxm_link_t *l = w->link;
    uint32_t len = 0;
    uint64_t off = 0;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", WELLSPRING_LOG);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (rc == ZXM_E_REMOTE && l->err_code == WELLSPRING_E_FILES && !off &&
            strstr(l->err, "no such file")) {
            wellspring_init(w, l); /* not made yet: an empty log */
            w->loaded = true;
            return ZXM_OK;
        }
        if (rc) return rc;
        uint64_t size, next_off;
        bool eof;
        int32_t t = zxm_get(l, l->result, "text");
        if (zxm_get_u64(l, l->result, "size", &size) ||
            zxm_get_u64(l, l->result, "next", &next_off) ||
            zxm_get_bool(l, l->result, "eof", &eof) || t < 0)
            return ZXM_E_PROTOCOL;
        if (size >= WELLSPRING_TEXT_MAX) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (n < 0 || len + (uint32_t) n >= WELLSPRING_TEXT_MAX) return ZXM_E_TOOBIG;
        memcpy(text + len, chunk, (size_t) n);
        len += (uint32_t) n;
        if (eof) break;
        if (next_off <= off) return ZXM_E_PROTOCOL;
        off = next_off;
    }
    next.n = 0;
    if (!parse(&next, text, len)) {
        w->loaded = false; /* WL4: never overwritten */
        return ZXM_E_PROTOCOL;
    }
    memcpy(w->goal, next.goal, sizeof w->goal);
    memcpy(w->e, next.e, (size_t) next.n * sizeof w->e[0]);
    w->n = next.n;
    w->loaded = true;
    return ZXM_OK;
}

static wellspring_t NEXT; /* a change before it is written (WL5) */

static void begin_change(const wellspring_t *w)
{
    memcpy(NEXT.goal, w->goal, sizeof NEXT.goal);
    memcpy(NEXT.e, w->e, (size_t) w->n * sizeof w->e[0]);
    NEXT.n = w->n;
}

int wellspring_add(wellspring_t *w, int64_t day, int64_t kind, int64_t amount)
{
    if (!wellspring_log_ok(day, kind, amount)) return ZXM_E_ARG;
    if (!w->loaded) return ZXM_E_STATE;
    if (w->n >= WELLSPRING_ENTRIES) return ZXM_E_TOOBIG;
    begin_change(w);
    NEXT.e[NEXT.n++] = (wellspring_entry_t){(int32_t) day, (int32_t) kind, amount};
    return commit(w, &NEXT);
}

int wellspring_remove(wellspring_t *w, uint32_t index)
{
    if (!w->loaded) return ZXM_E_STATE;
    if (index >= w->n) return ZXM_E_ARG;
    begin_change(w);
    memmove(&NEXT.e[index], &NEXT.e[index + 1u], (size_t) (NEXT.n - index - 1u) * sizeof NEXT.e[0]);
    NEXT.n--;
    return commit(w, &NEXT);
}

int wellspring_set_goal(wellspring_t *w, int64_t kind, int64_t weekly)
{
    if (!goal_kind(kind) || weekly < 0 || weekly > WELLSPRING_AMOUNT_MAX) return ZXM_E_ARG;
    if (!w->loaded) return ZXM_E_STATE;
    begin_change(w);
    NEXT.goal[kind] = weekly;
    return commit(w, &NEXT);
}

/* ---- what the log says ----------------------------------------------------- */

int wellspring_week(const wellspring_t *w, int64_t first, int64_t kind, int64_t *total)
{
    int64_t day[7] = {0};
    if (!total || !goal_kind(kind) || first < 0 || first > WELLSPRING_DAY_MAX) return ZXM_E_ARG;
    /* each day's total: at most WELLSPRING_ENTRIES amounts of nine digits */
    for (uint32_t i = 0; i < w->n; i++)
        if (w->e[i].kind == kind && w->e[i].day >= first && w->e[i].day - first < 7)
            day[w->e[i].day - first] += w->e[i].amount;
    int64_t s = wellspring_week_total(day, 7);
    if (s < 0) return ZXM_E_TOOBIG;
    *total = s;
    return ZXM_OK;
}

int wellspring_progress(const wellspring_t *w, int64_t first, int64_t kind, int64_t *x100)
{
    int64_t total;
    if (!x100) return ZXM_E_ARG;
    int rc = wellspring_week(w, first, kind, &total);
    if (rc) return rc;
    if (!w->goal[kind]) return ZXM_E_STATE;
    int64_t p = wellspring_progress_x100(total, w->goal[kind]);
    if (p < 0) return ZXM_E_TOOBIG;
    *x100 = p;
    return ZXM_OK;
}

static const struct {
    const char *what, *unit;
    int64_t lo, hi;
} RANGE[WELLSPRING_KINDS + 1] = {
    [WELLSPRING_SLEEP] = {"sleep", "minutes", WELLSPRING_SLEEP_LO, WELLSPRING_SLEEP_HI},
    [WELLSPRING_WATER] = {"water", "ml", WELLSPRING_WATER_LO, WELLSPRING_WATER_HI},
    [WELLSPRING_PULSE] = {"resting heart rate", "bpm", WELLSPRING_PULSE_LO, WELLSPRING_PULSE_HI},
};

/* The advice: what was outside its range, and that a clinician, not this
 * app, can say what it means. */
static void advise(wellspring_t *w, const wellspring_day_t *d)
{
    char *s = w->notice;
    uint32_t cap = sizeof w->notice, at = 0;
    s[0] = 0;
    bool ok = put(s, cap, &at, "Day ") && put_num(s, cap, &at, d->day) && put(s, cap, &at, ": ");
    if (!d->outside) {
        bool any = d->has[WELLSPRING_SLEEP] || d->has[WELLSPRING_WATER] || d->has[WELLSPRING_PULSE];
        ok = ok && put(s, cap, &at,
                       any ? "every logged reading is inside its usual range."
                           : "no heart rate, sleep or water logged.");
        if (!ok) s[0] = 0;
        return;
    }
    for (int32_t k = WELLSPRING_SLEEP; k <= WELLSPRING_PULSE && ok; k++) {
        if (!d->band[k]) continue;
        ok = put(s, cap, &at, RANGE[k].what) && put(s, cap, &at, " ") &&
             put_num(s, cap, &at, d->value[k]) && put(s, cap, &at, " ") &&
             put(s, cap, &at, RANGE[k].unit) &&
             put(s, cap, &at, d->band[k] < 0 ? " is below" : " is above") &&
             put(s, cap, &at, " the usual ") && put_num(s, cap, &at, RANGE[k].lo) &&
             put(s, cap, &at, "-") && put_num(s, cap, &at, RANGE[k].hi) &&
             put(s, cap, &at, " range. ");
    }
    ok = ok && put(s, cap, &at,
                   "This is not a diagnosis. If readings like this continue, or you feel "
                   "unwell, talk to a clinician.");
    if (!ok) s[0] = 0;
}

int wellspring_day(wellspring_t *w, int64_t day, wellspring_day_t *d)
{
    if (!d || day < 0 || day > WELLSPRING_DAY_MAX) return ZXM_E_ARG;
    memset(d, 0, sizeof *d);
    d->day = (int32_t) day;
    for (uint32_t i = 0; i < w->n; i++) {
        const wellspring_entry_t *e = &w->e[i];
        if (e->day != day) continue;
        d->has[e->kind] = true;
        /* the last heart rate of the day; amounts add (bounded as above) */
        d->value[e->kind] = e->kind == WELLSPRING_PULSE ? e->amount : d->value[e->kind] + e->amount;
    }
    for (int32_t k = WELLSPRING_SLEEP; k <= WELLSPRING_PULSE; k++) {
        if (!d->has[k]) continue;
        d->band[k] = wellspring_band(d->value[k], RANGE[k].lo, RANGE[k].hi);
        d->outside += d->band[k] != 0;
    }
    d->safe = d->has[WELLSPRING_SLEEP] && d->has[WELLSPRING_WATER] && d->has[WELLSPRING_PULSE] &&
              wellspring_safe(d->value[WELLSPRING_PULSE], d->value[WELLSPRING_SLEEP],
                              d->value[WELLSPRING_WATER]);
    advise(w, d);
    return ZXM_OK;
}
