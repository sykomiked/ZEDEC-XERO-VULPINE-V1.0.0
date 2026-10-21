/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* solstice_model.c — Solstice, the calendar (see solstice_model.h). */
#include <string.h>

#include "solstice_model.h"

/* ---- the rules ---------------------------------------------------------- */

bool solstice_event_valid(int64_t start, int64_t finish, int64_t every, int64_t alarm)
{
    if (start < 0 || finish <= start || finish > SOLSTICE_SECONDS_MAX) return false;
    if (every < 0 || every > SOLSTICE_DAY_MAX || alarm < 0 || alarm > SOLSTICE_ALARM_MAX)
        return false;
    if (every > 0 && finish - start > every * SOLSTICE_DAY_S) return false;
    return solstice_record_fits(1, start, finish, every, alarm);
}

bool solstice_title_ok(const char *title)
{
    if (!title) return false;
    size_t n = strlen(title);
    if (n < 1u || n > SOLSTICE_TITLE_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char) title[i];
        if (c < 0x20u || c == 0x7fu) return false;
    }
    return true;
}

int solstice_next_start(const solstice_event_t *e, int64_t now, int64_t *at)
{
    if (!e || !at || now < 0 || now > SOLSTICE_SECONDS_MAX) return ZXM_E_ARG;
    int64_t len = e->finish - e->start;
    if (!e->every) {
        if (e->finish <= now) return ZXM_E_STATE;
        *at = e->start;
        return ZXM_OK;
    }
    /* the first occurrence whose finish is after now starts at or after t */
    int64_t t = now - len + 1;
    if (t <= e->start) {
        *at = e->start;
        return ZXM_OK;
    }
    int64_t offset = e->start % SOLSTICE_DAY_S;
    int64_t from = (t - offset + SOLSTICE_DAY_S - 1) / SOLSTICE_DAY_S; /* t > start: positive */
    if (from > SOLSTICE_DAY_MAX) return ZXM_E_STATE;
    int64_t day = solstice_next(e->start / SOLSTICE_DAY_S, e->every, from); /* SO3 */
    if (day < 0 || day > SOLSTICE_DAY_MAX) return ZXM_E_STATE;
    int64_t s = day * SOLSTICE_DAY_S + offset;
    if (s > SOLSTICE_SECONDS_MAX - len) return ZXM_E_STATE; /* it would end past year 9999 */
    *at = s;
    return ZXM_OK;
}

int solstice_clash(const solstice_event_t *a, const solstice_event_t *b, int64_t now, bool *clash)
{
    int64_t as, bs;
    if (!a || !b || !clash) return ZXM_E_ARG;
    *clash = false;
    if (solstice_next_start(a, now, &as) || solstice_next_start(b, now, &bs)) return ZXM_OK;
    int64_t aover = as + (a->finish - a->start) - bs;
    int64_t bover = bs + (b->finish - b->start) - as;
    *clash = solstice_overlap(aover, bover); /* SO4 */
    return ZXM_OK;
}

int solstice_month(int64_t year, int64_t month, solstice_month_t *m)
{
    if (!m || year < 1970 || year > 9999 || month < 1 || month > 12) return ZXM_E_ARG;
    m->days = solstice_month_days(year, month);          /* SO1 */
    m->first_weekday = solstice_weekday(year, month, 1); /* SO2 */
    m->first_day = solstice_day_of(year, month, 1);
    return ZXM_OK;
}

/* ---- an event as text (SM2) ------------------------------------------------ */

static const char HEAD[] = "solstice 1\n";

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

/* v >= 0 in decimal, at least `width` digits (zeros in front). */
static bool put_num(char *out, uint32_t cap, uint32_t *at, int64_t v, int width)
{
    char t[24];
    int k = 23;
    uint64_t u = v < 0 ? 0u : (uint64_t) v;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while ((u || 23 - k < width) && k > 0);
    return put(out, cap, at, t + k);
}

static bool path_of(int64_t id, char *out, uint32_t cap)
{
    uint32_t at = 0;
    return put(out, cap, &at, SOLSTICE_DIR "/") && put_num(out, cap, &at, id, 1) &&
           put(out, cap, &at, ".event");
}

static int32_t compose(const solstice_event_t *e, char *out, uint32_t cap)
{
    uint32_t at = 0;
    bool ok = put(out, cap, &at, HEAD) && put_num(out, cap, &at, e->start, 1) &&
              put(out, cap, &at, " ") && put_num(out, cap, &at, e->finish, 1) &&
              put(out, cap, &at, " ") && put_num(out, cap, &at, e->every, 1) &&
              put(out, cap, &at, " ") && put_num(out, cap, &at, e->alarm, 1) &&
              put(out, cap, &at, "\n") && put(out, cap, &at, e->title) && put(out, cap, &at, "\n");
    return ok ? (int32_t) at : -1;
}

/* A plain decimal of 1 to `most` digits, no leading zero, at s[*at],
 * ended by `end`. */
static bool get_num(const char *s, uint32_t n, uint32_t *at, char end, uint32_t most, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    int64_t x = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9' && digits <= most) {
        x = x * 10 + (s[i] - '0');
        i++;
        digits++;
    }
    if (!digits || digits > most || i >= n || s[i] != end) return false;
    if (digits > 1u && s[*at] == '0') return false;
    *v = x;
    *at = i + 1u;
    return true;
}

/* SM1: the number in "<n>.event", or 0 when the name is not an event's. */
static int64_t id_of(const char *name)
{
    uint32_t n = (uint32_t) strlen(name), at = 0;
    int64_t v = 0;
    if (n < 7u || strcmp(name + n - 6u, ".event")) return 0;
    if (!get_num(name, n - 5u, &at, '.', 9u, &v) || at != n - 5u) return 0;
    return v;
}

/* SM2, SM3: false when the text is not an event. */
static bool parse(solstice_event_t *e, const char *s, uint32_t n)
{
    uint32_t at = (uint32_t) strlen(HEAD);
    if (n < at || memcmp(s, HEAD, at)) return false;
    if (!get_num(s, n, &at, ' ', 12u, &e->start) || !get_num(s, n, &at, ' ', 12u, &e->finish) ||
        !get_num(s, n, &at, ' ', 7u, &e->every) || !get_num(s, n, &at, '\n', 4u, &e->alarm))
        return false;
    uint32_t end = at;
    while (end < n && s[end] != '\n') end++;
    if (end + 1u != n || end - at > SOLSTICE_TITLE_MAX) return false;
    for (uint32_t i = at; i < end; i++)
        if ((unsigned char) s[i] < 0x20u) return false; /* a NUL is no title byte */
    memcpy(e->title, s + at, end - at);
    e->title[end - at] = 0;
    return solstice_title_ok(e->title) &&
           solstice_event_valid(e->start, e->finish, e->every, e->alarm);
}

/* ---- zxvd ---------------------------------------------------------------- */

void solstice_init(solstice_t *s, zxm_link_t *link)
{
    memset(s, 0, sizeof *s);
    s->link = link;
}

/* Read one event file into e: 1 an event, 0 not one (SM2), or a ZXM_E_* code. */
static int read_event(zxm_link_t *l, int64_t id, solstice_event_t *e)
{
    static char text[SOLSTICE_TEXT_MAX + 1];
    char path[48];
    uint64_t size;
    bool eof;
    if (!path_of(id, path, sizeof path)) return ZXM_E_TOOBIG;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "max", SOLSTICE_TEXT_MAX);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE) return 0; /* gone, or not UTF-8: not an event, left alone */
    if (rc) return rc;
    int32_t t = zxm_get(l, l->result, "text");
    if (zxm_get_u64(l, l->result, "size", &size) || zxm_get_bool(l, l->result, "eof", &eof) ||
        t < 0)
        return ZXM_E_PROTOCOL;
    if (!eof || size > SOLSTICE_TEXT_MAX) return 0;
    int32_t n = w4_json_str(l->resp, &l->tok[t], text, sizeof text);
    if (n < 0) return 0;
    memset(e, 0, sizeof *e);
    e->id = id;
    return parse(e, text, (uint32_t) n) ? 1 : 0;
}

int solstice_load(solstice_t *s)
{
    static int64_t ids[SOLSTICE_LIST_MAX];
    static solstice_event_t next[SOLSTICE_EVENTS];
    char name[64];
    zxm_link_t *l = s->link;
    zxm_begin(l, "files.list");
    zxm_p_str(l, "path", SOLSTICE_DIR);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE && l->err_code == SOLSTICE_E_FILES && strstr(l->err, "no such")) {
        solstice_init(s, l); /* not made yet: no events */
        s->loaded = true;
        return ZXM_OK;
    }
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t total = zxm_len(l, arr), k = 0;
    if (total > SOLSTICE_LIST_MAX) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < total; i++) {
        int32_t e = zxm_at(l, arr, i);
        bool dir = false;
        if (zxm_get_bool(l, e, "dir", &dir)) return ZXM_E_PROTOCOL;
        if (dir) continue;
        rc = zxm_get_str(l, e, "name", name, sizeof name);
        if (rc == ZXM_E_TOOBIG) continue; /* a long name is not an event's */
        if (rc) return ZXM_E_PROTOCOL;
        int64_t id = id_of(name);
        if (id > 0) ids[k++] = id;
    }
    uint32_t n = 0, skipped = 0;
    int64_t last = 0;
    for (uint32_t i = 0; i < k; i++) {
        if (ids[i] > last) last = ids[i];
        if (n >= SOLSTICE_EVENTS) return ZXM_E_TOOBIG;
        rc = read_event(l, ids[i], &next[n]);
        if (rc < 0) return rc;
        if (rc)
            n++;
        else
            skipped++;
    }
    memcpy(s->e, next, (size_t) n * sizeof next[0]);
    s->n = n;
    s->skipped = skipped;
    s->last_id = last;
    s->folder = true;
    s->loaded = true;
    return ZXM_OK;
}

int solstice_add(solstice_t *s, int64_t start, int64_t finish, int64_t every, int64_t alarm,
                 const char *title, int64_t *id)
{
    static char text[SOLSTICE_TEXT_MAX];
    char path[48];
    if (!id || !solstice_event_valid(start, finish, every, alarm) || !solstice_title_ok(title))
        return ZXM_E_ARG;
    if (!s->loaded) return ZXM_E_STATE;
    if (s->n >= SOLSTICE_EVENTS || s->last_id >= SOLSTICE_ID_MAX) return ZXM_E_TOOBIG;
    solstice_event_t e = {s->last_id + 1, start, finish, every, alarm, {0}};
    memcpy(e.title, title, strlen(title) + 1u);
    int32_t len = compose(&e, text, sizeof text);
    if (len < 0 || !path_of(e.id, path, sizeof path)) return ZXM_E_TOOBIG;
    zxm_link_t *l = s->link;
    if (!s->folder) {
        zxm_begin(l, "files.mkdir");
        zxm_p_str(l, "path", SOLSTICE_DIR);
        int rc = zxm_call(l);
        if (rc) return rc;
        s->folder = true;
    }
    zxm_begin(l, "files.write");
    zxm_p_str(l, "path", path);
    zxm_p_strn(l, "text", text, (uint32_t) len);
    zxm_p_bool(l, "exclusive", true); /* SM4 */
    int rc = zxm_call(l);
    if (rc) return rc;
    s->e[s->n++] = e;
    s->last_id = e.id;
    *id = e.id;
    return ZXM_OK;
}

int solstice_remove(solstice_t *s, uint32_t index)
{
    char path[48];
    if (!s->loaded) return ZXM_E_STATE;
    if (index >= s->n) return ZXM_E_ARG;
    if (!path_of(s->e[index].id, path, sizeof path)) return ZXM_E_TOOBIG;
    zxm_begin(s->link, "files.trash");
    zxm_p_str(s->link, "path", path);
    int rc = zxm_call(s->link);
    if (rc) return rc;
    memmove(&s->e[index], &s->e[index + 1u], (size_t) (s->n - index - 1u) * sizeof s->e[0]);
    s->n--;
    return ZXM_OK;
}

/* ---- what the calendar says ------------------------------------------------ */

int solstice_clashes(const solstice_t *s, uint32_t index, int64_t now, uint32_t *count)
{
    if (!count || index >= s->n) return ZXM_E_ARG;
    *count = 0;
    for (uint32_t i = 0; i < s->n; i++) {
        bool c = false;
        if (i == index) continue;
        int rc = solstice_clash(&s->e[index], &s->e[i], now, &c);
        if (rc) return rc;
        *count += c;
    }
    return ZXM_OK;
}

int solstice_on_day(const solstice_t *s, int64_t day, uint32_t *idx, uint32_t cap, uint32_t *n)
{
    if (!n || (cap && !idx) || day < 0 || day > SOLSTICE_DAY_MAX) return ZXM_E_ARG;
    *n = 0;
    for (uint32_t i = 0; i < s->n; i++) {
        int64_t first = s->e[i].start / SOLSTICE_DAY_S;
        bool on = s->e[i].every ? day >= first && solstice_next(first, s->e[i].every, day) == day
                                : first == day;
        if (!on) continue;
        if (*n < cap) idx[*n] = i;
        (*n)++;
    }
    return ZXM_OK;
}

static const char *const WEEKDAY[8] = {"",         "Monday", "Tuesday",  "Wednesday",
                                       "Thursday", "Friday", "Saturday", "Sunday"};

/* "<title>: <weekday> YYYY-MM-DD HH:MM UTC\n" for a start at `at`. */
static bool put_line(char *out, uint32_t cap, uint32_t *at, const solstice_event_t *e, int64_t t)
{
    int64_t y, m, d, secs = t % SOLSTICE_DAY_S;
    if (!solstice_date_of(t / SOLSTICE_DAY_S, &y, &m, &d)) return false;
    int64_t wd = solstice_weekday(y, m, d);
    if (wd < 1) return false;
    return put(out, cap, at, e->title) && put(out, cap, at, ": ") &&
           put(out, cap, at, WEEKDAY[wd]) && put(out, cap, at, " ") &&
           put_num(out, cap, at, y, 4) && put(out, cap, at, "-") && put_num(out, cap, at, m, 2) &&
           put(out, cap, at, "-") && put_num(out, cap, at, d, 2) && put(out, cap, at, " ") &&
           put_num(out, cap, at, secs / 3600, 2) && put(out, cap, at, ":") &&
           put_num(out, cap, at, secs % 3600 / 60, 2) && put(out, cap, at, " UTC\n");
}

int solstice_due(solstice_t *s, int64_t now, uint32_t *n)
{
    uint32_t at = 0;
    if (!n) return ZXM_E_ARG;
    *n = 0;
    s->notice[0] = 0;
    for (uint32_t i = 0; i < s->n; i++) {
        int64_t t;
        if (solstice_next_start(&s->e[i], now, &t) || t < now) continue; /* none, or begun */
        if (t - s->e[i].alarm * 60 > now) continue;
        (*n)++;
        uint32_t keep = at;
        if (!put_line(s->notice, sizeof s->notice, &at, &s->e[i], t)) {
            at = keep; /* no room for the whole line: the count still says it */
            s->notice[at] = 0;
        }
    }
    return ZXM_OK;
}
