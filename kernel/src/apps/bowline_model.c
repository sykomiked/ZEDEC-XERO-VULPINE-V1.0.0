/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bowline_model.c — Bowline, tasks and reminders (see bowline_model.h). */
#include <string.h>

#include "bowline_model.h"

/* ---- the rules ---------------------------------------------------------- */

bool bowline_task_valid(int64_t list, int64_t start, int64_t due, int64_t every, int64_t unit,
                        int64_t skip, int64_t done)
{
    if (list < 1 || list > BOWLINE_LIST_MAX) return false;
    if (start < 0 || due < start || due > SOLSTICE_DAY_MAX) return false;
    if (every < 0 || every > BOWLINE_EVERY_MAX || (unit != 1 && unit != 7)) return false;
    if (skip < 0 || skip > BOWLINE_SKIP_MAX || done < 0 || done > 1) return false;
    if (every > 0 && done) return false; /* a repeating task moves on instead */
    return bowline_record_fits(1, list, start, due, every, unit, skip, done);
}

bool bowline_title_ok(const char *title)
{
    if (!title) return false;
    size_t n = strlen(title);
    if (n < 1u || n > BOWLINE_TITLE_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char) title[i];
        if (c < 0x20u || c == 0x7fu) return false;
    }
    return true;
}

int bowline_next_due(const bowline_task_t *t, int64_t today, int64_t *day)
{
    if (!t || !day || today < 0 || today > SOLSTICE_DAY_MAX) return ZXM_E_ARG;
    if (!t->every) return ZXM_E_STATE; /* once: done for good */
    int64_t from = (today > t->due ? today : t->due) + 1;
    if (from > SOLSTICE_DAY_MAX) return ZXM_E_STATE;
    int64_t occ = solstice_next(t->start, t->every * t->unit, from); /* SO3 */
    if (occ < 0 || occ > SOLSTICE_DAY_MAX) return ZXM_E_STATE;
    int64_t d = bowline_unskipped(occ, t->skip); /* BW1 */
    if (d < 0) return ZXM_E_STATE;
    *day = d;
    return ZXM_OK;
}

bool bowline_is_overdue(const bowline_task_t *t, int64_t today)
{
    if (!t || today < 0 || today > SOLSTICE_DAY_MAX) return false;
    return bowline_overdue(today - t->due, t->done ? 0 : 1); /* BW2 */
}

/* ---- a task as text (BM2) ---------------------------------------------------- */

static const char HEAD[] = "bowline 1\n";

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
    return put(out, cap, &at, BOWLINE_DIR "/") && put_num(out, cap, &at, id, 1) &&
           put(out, cap, &at, ".task");
}

static int32_t compose(const bowline_task_t *t, char *out, uint32_t cap)
{
    const int64_t v[7] = {t->list, t->start, t->due, t->every, t->unit, t->skip, t->done};
    uint32_t at = 0;
    bool ok = put(out, cap, &at, HEAD);
    for (uint32_t i = 0; i < 7u && ok; i++)
        ok = put_num(out, cap, &at, v[i], 1) && put(out, cap, &at, i < 6u ? " " : "\n");
    ok = ok && put(out, cap, &at, t->title) && put(out, cap, &at, "\n");
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

/* BM1: the number in "<n>.task", or 0 when the name is not a task's. */
static int64_t id_of(const char *name)
{
    uint32_t n = (uint32_t) strlen(name), at = 0;
    int64_t v = 0;
    if (n < 6u || strcmp(name + n - 5u, ".task")) return 0;
    if (!get_num(name, n - 4u, &at, '.', 9u, &v) || at != n - 4u) return 0;
    return v;
}

/* BM2, BM3: false when the text is not a task. */
static bool parse(bowline_task_t *t, const char *s, uint32_t n)
{
    static const uint32_t DIGITS[7] = {3u, 7u, 7u, 4u, 1u, 3u, 1u};
    int64_t v[7];
    uint32_t at = (uint32_t) strlen(HEAD);
    if (n < at || memcmp(s, HEAD, at)) return false;
    for (uint32_t i = 0; i < 7u; i++)
        if (!get_num(s, n, &at, i < 6u ? ' ' : '\n', DIGITS[i], &v[i])) return false;
    uint32_t end = at;
    while (end < n && s[end] != '\n') end++;
    if (end + 1u != n || end - at > BOWLINE_TITLE_MAX) return false;
    for (uint32_t i = at; i < end; i++)
        if ((unsigned char) s[i] < 0x20u) return false; /* a NUL is no title byte */
    memcpy(t->title, s + at, end - at);
    t->title[end - at] = 0;
    t->list = v[0], t->start = v[1], t->due = v[2], t->every = v[3];
    t->unit = v[4], t->skip = v[5], t->done = v[6];
    return bowline_title_ok(t->title) &&
           bowline_task_valid(t->list, t->start, t->due, t->every, t->unit, t->skip, t->done);
}

/* ---- zxvd ---------------------------------------------------------------- */

void bowline_init(bowline_t *b, zxm_link_t *link)
{
    memset(b, 0, sizeof *b);
    b->link = link;
}

/* Read one task file into t: 1 a task, 0 not one (BM2), or a ZXM_E_* code. */
static int read_task(zxm_link_t *l, int64_t id, bowline_task_t *t)
{
    static char text[BOWLINE_TEXT_MAX + 1];
    char path[48];
    uint64_t size;
    bool eof;
    if (!path_of(id, path, sizeof path)) return ZXM_E_TOOBIG;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "max", BOWLINE_TEXT_MAX);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE) return 0; /* gone, or not UTF-8: not a task, left alone */
    if (rc) return rc;
    int32_t x = zxm_get(l, l->result, "text");
    if (zxm_get_u64(l, l->result, "size", &size) || zxm_get_bool(l, l->result, "eof", &eof) ||
        x < 0)
        return ZXM_E_PROTOCOL;
    if (!eof || size > BOWLINE_TEXT_MAX) return 0;
    int32_t n = w4_json_str(l->resp, &l->tok[x], text, sizeof text);
    if (n < 0) return 0;
    memset(t, 0, sizeof *t);
    t->id = id;
    return parse(t, text, (uint32_t) n) ? 1 : 0;
}

int bowline_load(bowline_t *b)
{
    static int64_t ids[BOWLINE_LIST_NAMES];
    static bowline_task_t next[BOWLINE_TASKS];
    char name[64];
    zxm_link_t *l = b->link;
    zxm_begin(l, "files.list");
    zxm_p_str(l, "path", BOWLINE_DIR);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE && l->err_code == BOWLINE_E_FILES && strstr(l->err, "no such")) {
        bowline_init(b, l); /* not made yet: no tasks */
        b->loaded = true;
        return ZXM_OK;
    }
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t total = zxm_len(l, arr), k = 0;
    if (total > BOWLINE_LIST_NAMES) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < total; i++) {
        int32_t e = zxm_at(l, arr, i);
        bool dir = false;
        if (zxm_get_bool(l, e, "dir", &dir)) return ZXM_E_PROTOCOL;
        if (dir) continue;
        rc = zxm_get_str(l, e, "name", name, sizeof name);
        if (rc == ZXM_E_TOOBIG) continue; /* a long name is not a task's */
        if (rc) return ZXM_E_PROTOCOL;
        int64_t id = id_of(name);
        if (id > 0) ids[k++] = id;
    }
    uint32_t n = 0, skipped = 0;
    int64_t last = 0;
    for (uint32_t i = 0; i < k; i++) {
        if (ids[i] > last) last = ids[i];
        if (n >= BOWLINE_TASKS) return ZXM_E_TOOBIG;
        rc = read_task(l, ids[i], &next[n]);
        if (rc < 0) return rc;
        if (rc)
            n++;
        else
            skipped++;
    }
    memcpy(b->t, next, (size_t) n * sizeof next[0]);
    b->n = n;
    b->skipped = skipped;
    b->last_id = last;
    b->folder = true;
    b->loaded = true;
    return ZXM_OK;
}

/* Write t's file: a new one with exclusive set (BM5), else replaced. */
static int write_task(zxm_link_t *l, const bowline_task_t *t, bool fresh)
{
    static char text[BOWLINE_TEXT_MAX];
    char path[48];
    int32_t len = compose(t, text, sizeof text);
    if (len < 0 || !path_of(t->id, path, sizeof path)) return ZXM_E_TOOBIG;
    zxm_begin(l, "files.write");
    zxm_p_str(l, "path", path);
    zxm_p_strn(l, "text", text, (uint32_t) len);
    if (fresh) zxm_p_bool(l, "exclusive", true);
    return zxm_call(l);
}

int bowline_add(bowline_t *b, int64_t list, int64_t start, int64_t every, int64_t unit,
                int64_t skip, const char *title, int64_t *id)
{
    if (!id || !bowline_title_ok(title)) return ZXM_E_ARG;
    int64_t due = bowline_unskipped(start, skip); /* BM4, BW1 */
    if (due < 0 || !bowline_task_valid(list, start, due, every, unit, skip, 0)) return ZXM_E_ARG;
    if (!b->loaded) return ZXM_E_STATE;
    if (b->n >= BOWLINE_TASKS || b->last_id >= BOWLINE_ID_MAX) return ZXM_E_TOOBIG;
    bowline_task_t t = {b->last_id + 1, list, start, due, every, unit, skip, 0, {0}};
    memcpy(t.title, title, strlen(title) + 1u);
    zxm_link_t *l = b->link;
    if (!b->folder) {
        zxm_begin(l, "files.mkdir");
        zxm_p_str(l, "path", BOWLINE_DIR);
        int rc = zxm_call(l);
        if (rc) return rc;
        b->folder = true;
    }
    int rc = write_task(l, &t, true);
    if (rc) return rc;
    b->t[b->n++] = t;
    b->last_id = t.id;
    *id = t.id;
    return ZXM_OK;
}

int bowline_complete(bowline_t *b, uint32_t index, int64_t today)
{
    if (!b->loaded) return ZXM_E_STATE;
    if (index >= b->n || today < 0 || today > SOLSTICE_DAY_MAX) return ZXM_E_ARG;
    bowline_task_t t = b->t[index];
    if (t.done) return ZXM_E_STATE;
    if (t.every) {
        int rc = bowline_next_due(&t, today, &t.due); /* BM4 */
        if (rc) return rc;
    } else {
        t.done = 1;
    }
    int rc = write_task(b->link, &t, false);
    if (rc) return rc; /* BM6: refused, nothing kept */
    b->t[index] = t;
    return ZXM_OK;
}

/* ---- what the list says ------------------------------------------------------ */

int bowline_list_count(const bowline_t *b, int64_t list, uint32_t *count)
{
    static int64_t lists[BOWLINE_TASKS];
    if (!count || list < 1 || list > BOWLINE_LIST_MAX) return ZXM_E_ARG;
    uint32_t k = 0;
    for (uint32_t i = 0; i < b->n; i++)
        if (!b->t[i].done) lists[k++] = b->t[i].list;
    *count = (uint32_t) witness_kind_count(lists, k, list); /* WR2 */
    return ZXM_OK;
}

int bowline_overdue_count(const bowline_t *b, int64_t today, uint32_t *count)
{
    if (!count || today < 0 || today > SOLSTICE_DAY_MAX) return ZXM_E_ARG;
    *count = 0;
    for (uint32_t i = 0; i < b->n; i++) *count += bowline_is_overdue(&b->t[i], today);
    return ZXM_OK;
}

static const char *const WEEKDAY[7] = {"Monday", "Tuesday",  "Wednesday", "Thursday",
                                       "Friday", "Saturday", "Sunday"};

/* "<title>: due <weekday> YYYY-MM-DD\n", "overdue since" when it is late. */
static bool put_line(char *out, uint32_t cap, uint32_t *at, const bowline_task_t *t, bool late)
{
    int64_t y, m, d;
    if (!solstice_date_of(t->due, &y, &m, &d)) return false;
    return put(out, cap, at, t->title) && put(out, cap, at, late ? ": overdue since " : ": due ") &&
           put(out, cap, at, WEEKDAY[bowline_weekday0(t->due)]) && put(out, cap, at, " ") &&
           put_num(out, cap, at, y, 4) && put(out, cap, at, "-") && put_num(out, cap, at, m, 2) &&
           put(out, cap, at, "-") && put_num(out, cap, at, d, 2) && put(out, cap, at, "\n");
}

int bowline_reminders(bowline_t *b, int64_t today, uint32_t *n)
{
    uint32_t at = 0;
    if (!n || today < 0 || today > SOLSTICE_DAY_MAX) return ZXM_E_ARG;
    *n = 0;
    b->notice[0] = 0;
    for (uint32_t i = 0; i < b->n; i++) {
        const bowline_task_t *t = &b->t[i];
        if (t->done || t->due > today) continue;
        (*n)++;
        uint32_t keep = at;
        if (!put_line(b->notice, sizeof b->notice, &at, t, bowline_is_overdue(t, today))) {
            at = keep; /* no room for the whole line: the count still says it */
            b->notice[at] = 0;
        }
    }
    return ZXM_OK;
}

int bowline_notices(bowline_t *b)
{
    char source[32];
    uint64_t unread;
    zxm_link_t *l = b->link;
    zxm_begin(l, "notes.list");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "notes");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > BOWLINE_NOTES ||
        zxm_get_u64(l, l->result, "unread", &unread) || unread > UINT32_MAX)
        return ZXM_E_PROTOCOL;
    uint32_t total = zxm_len(l, arr), ours = 0;
    for (uint32_t i = 0; i < total; i++) {
        int32_t o = zxm_at(l, arr, i);
        bool read = false;
        if (o < 0 || zxm_get_bool(l, o, "read", &read)) return ZXM_E_PROTOCOL;
        rc = zxm_get_str(l, o, "source", source, sizeof source);
        if (rc == ZXM_E_TOOBIG) continue; /* a long source is not Bowline */
        if (rc) return ZXM_E_PROTOCOL;
        ours += !read && !strcmp(source, BOWLINE_SOURCE);
    }
    b->unread = (uint32_t) unread; /* kept only once the whole answer is read */
    b->ours = ours;
    return ZXM_OK;
}
