/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fireglow_model.c — Fireglow's model (fireglow_model.h). */
#include <string.h>

#include "fireglow_model.h"

#define FIREGLOW_E_FILES (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define FIREGLOW_DIGITS  11u      /* FM2: the most digits a saved number has */

static const char *const AUDIO[] = {".mp3", ".m4a",  ".m4b",  ".aac",
                                    ".ogg", ".opus", ".flac", ".wav"};
static const char *const ROOT[] = {FIREGLOW_PODCASTS, FIREGLOW_BOOKS};

/* ---- small helpers, bounded ---- */

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (s && n < cap && s[n]) n++;
    return n;
}

static void say(fireglow_t *f, const char *a, const char *b)
{
    uint32_t na = len_of(a, FIREGLOW_NOTICE_MAX), nb = len_of(b, FIREGLOW_NOTICE_MAX - na);
    memcpy(f->notice, a, na);
    memcpy(f->notice + na, b, nb);
    f->notice[na + nb] = 0;
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
}

/* FM1: a name ending in a known audio extension, any case */
static bool audio_name(const char *name, uint32_t n)
{
    for (uint32_t e = 0; e < sizeof AUDIO / sizeof AUDIO[0]; e++) {
        uint32_t k = len_of(AUDIO[e], 8u), i = 0;
        if (n <= k) continue;
        while (i < k && lower(name[n - k + i]) == AUDIO[e][i]) i++;
        if (i == k) return true;
    }
    return false;
}

/* a / b / c (b and c may be NULL) into out, with tail after the last part */
static bool path_of(const char *a, const char *b, const char *c, const char *tail, char *out,
                    uint32_t cap)
{
    const char *part[3] = {a, b, c};
    uint32_t k = 0;
    for (uint32_t i = 0; i < 3u; i++) {
        if (!part[i]) break;
        uint32_t n = len_of(part[i], FIREGLOW_NAME_MAX + 1u);
        if (n == 0 || n > FIREGLOW_NAME_MAX || k + n + 2u > cap) return false;
        if (k) out[k++] = '/';
        memcpy(out + k, part[i], n);
        k += n;
    }
    uint32_t t = len_of(tail, 16u);
    if (k + t + 1u > cap) return false;
    memcpy(out + k, tail, t);
    out[k + t] = 0;
    return true;
}

static bool no_such(const zxm_link_t *l, int rc)
{
    return rc == ZXM_E_REMOTE && l->err_code == FIREGLOW_E_FILES && strstr(l->err, "no such");
}

/* a whole number of 1..FIREGLOW_DIGITS digits, or -1 */
static int64_t number_of(const char *s, uint32_t n)
{
    int64_t v = 0;
    if (n == 0 || n > FIREGLOW_DIGITS) return -1;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

/* v (0 or more) in decimal at out + k; the new end, or cap when it does not fit */
static uint32_t put_number(char *out, uint32_t k, uint32_t cap, int64_t v)
{
    char d[24];
    uint32_t m = 0;
    do {
        d[m++] = (char) ('0' + v % 10);
        v /= 10;
    } while (v && m < sizeof d);
    if (k + m > cap) return cap + 1u;
    while (m) out[k++] = d[--m];
    return k;
}

static uint32_t put_bytes(char *out, uint32_t k, uint32_t cap, const char *s, uint32_t n)
{
    if (k + n > cap) return cap + 1u;
    memcpy(out + k, s, n);
    return k + n;
}

/* A text file read whole (files.read, utf8) into buf (cap bytes, NUL
 * written): *missing when there is no such file; ZXM_E_TOOBIG when it is
 * longer than cap - 1. */
static int read_text(zxm_link_t *l, const char *path, char *buf, uint32_t cap, uint32_t *n,
                     bool *missing)
{
    *n = 0;
    *missing = false;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "max", cap - 1u);
    zxm_p_str(l, "encoding", "utf8");
    int rc = zxm_call(l);
    if (no_such(l, rc)) {
        *missing = true;
        return ZXM_OK;
    }
    if (rc) return rc;
    bool eof = false;
    int32_t t = zxm_get(l, l->result, "text");
    if (t < 0 || zxm_get_bool(l, l->result, "eof", &eof)) return ZXM_E_PROTOCOL;
    if (!eof) return ZXM_E_TOOBIG;
    int32_t k = w4_json_str(l->resp, &l->tok[t], buf, cap);
    if (k < 0) return ZXM_E_TOOBIG;
    *n = (uint32_t) k;
    return ZXM_OK;
}

static int32_t find_in(const fireglow_t *f, uint32_t show, const char *name, uint32_t n)
{
    for (uint32_t i = 0; i < f->n; i++)
        if (f->ep[i].show == show && len_of(f->ep[i].name, FIREGLOW_NAME_MAX + 1u) == n &&
            !memcmp(f->ep[i].name, name, n))
            return (int32_t) i;
    return -1;
}

/* ---- FM1, FM2: the library ---- */

void fireglow_init(fireglow_t *f, zxm_link_t *link)
{
    memset(f, 0, sizeof *f);
    f->link = link;
    f->rate = FIREGLOW_RATE_UNIT;
    f->chapters_of = -1;
}

int32_t fireglow_find_show(const fireglow_t *f, const char *name)
{
    if (!name) return -1;
    for (uint32_t i = 0; i < f->nshows; i++)
        if (!strcmp(f->show[i].name, name)) return (int32_t) i;
    return -1;
}

int32_t fireglow_find(const fireglow_t *f, uint32_t show, const char *name)
{
    if (!name || show >= f->nshows) return -1;
    return find_in(f, show, name, len_of(name, FIREGLOW_NAME_MAX + 1u));
}

/* FM1: the shows of one root, or its episodes when show is not -1 */
static int list_into(fireglow_t *t, const char *path, int32_t kind, int32_t show)
{
    zxm_link_t *l = t->link;
    zxm_begin(l, "files.list");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (no_such(l, rc)) return ZXM_OK;
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t count = zxm_len(l, arr);
    for (uint32_t i = 0; i < count; i++) {
        char name[FIREGLOW_NAME_MAX + 2u];
        bool dir = false;
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || zxm_get_bool(l, o, "dir", &dir)) return ZXM_E_PROTOCOL;
        if (zxm_get_str(l, o, "name", name, sizeof name)) {
            t->skipped++; /* longer than any kept name */
            continue;
        }
        uint32_t nn = len_of(name, sizeof name);
        if (show < 0) {
            if (!dir || nn > FIREGLOW_NAME_MAX || t->nshows >= FIREGLOW_SHOWS) {
                t->skipped++;
                continue;
            }
            memcpy(t->show[t->nshows].name, name, nn + 1u);
            t->show[t->nshows].kind = kind;
            t->nshows++;
            continue;
        }
        if (!dir && !audio_name(name, nn)) continue; /* the show's own files: not counted */
        if (dir || nn > FIREGLOW_NAME_MAX || t->n >= FIREGLOW_EPISODES) {
            t->skipped++;
            continue;
        }
        fireglow_episode_t *e = &t->ep[t->n++];
        memset(e, 0, sizeof *e);
        memcpy(e->name, name, nn + 1u);
        e->show = (uint32_t) show;
    }
    return ZXM_OK;
}

/* FM2: one line of a saved file */
static void place_line(fireglow_t *t, uint32_t show, const char *s, uint32_t n)
{
    uint32_t cut[3], k = 0;
    for (uint32_t i = 0; i < n && k < 3u; i++)
        if (s[i] == '\t') cut[k++] = i;
    if (k < 3u) {
        t->unsaved++;
        return;
    }
    int32_t ep = find_in(t, show, s, cut[0]);
    int64_t pos = number_of(s + cut[0] + 1u, cut[1] - cut[0] - 1u),
            length = number_of(s + cut[1] + 1u, cut[2] - cut[1] - 1u),
            heard = number_of(s + cut[2] + 1u, n - cut[2] - 1u);
    if (ep < 0 || pos < 0 || length < 0 || heard < 0 ||
        !fireglow_episode_row_ok((int64_t) show, ep, pos, length, heard) ||
        (length > 0 && pos > length)) {
        t->unsaved++;
        return;
    }
    t->ep[ep].pos = pos;
    t->ep[ep].length = length;
    t->ep[ep].heard = heard;
}

static int read_places(fireglow_t *t, uint32_t show)
{
    static char text[FIREGLOW_FILE_MAX + 1u];
    char path[FIREGLOW_PATH_MAX + 1u];
    uint32_t n = 0;
    bool missing = false;
    const fireglow_show_t *s = &t->show[show];
    if (!path_of(ROOT[s->kind], s->name, FIREGLOW_SAVED, "", path, sizeof path)) return ZXM_E_ARG;
    int rc = read_text(t->link, path, text, sizeof text, &n, &missing);
    if (rc == ZXM_E_TOOBIG) {
        t->unsaved++;
        return ZXM_OK;
    }
    if (rc || missing) return rc;
    for (uint32_t i = 0, start = 0; i <= n; i++) {
        if (i < n && text[i] != '\n') continue;
        uint32_t len = i - start;
        if (len && text[start + len - 1u] == '\r') len--;
        if (len) place_line(t, show, text + start, len);
        start = i + 1u;
    }
    return ZXM_OK;
}

int fireglow_load(fireglow_t *f)
{
    static fireglow_t t;
    char path[FIREGLOW_PATH_MAX + 1u];
    t = *f;
    t.nshows = t.n = t.skipped = t.unsaved = 0;
    t.nstarts = 0;
    t.chapters_of = -1;
    for (int32_t k = 0; k < 2; k++) {
        uint32_t from = t.nshows;
        int rc = list_into(&t, ROOT[k], k, -1);
        if (rc) return rc;
        for (uint32_t s = from; s < t.nshows; s++) {
            if (!path_of(ROOT[k], t.show[s].name, NULL, "", path, sizeof path)) return ZXM_E_ARG;
            rc = list_into(&t, path, k, (int32_t) s);
            if (rc) return rc;
        }
    }
    for (uint32_t s = 0; s < t.nshows; s++) {
        int rc = read_places(&t, s);
        if (rc) return rc;
        zxm_progress(t.link, s + 1u, t.nshows);
    }
    t.loaded = true;
    *f = t;
    return ZXM_OK;
}

int fireglow_save(fireglow_t *f, uint32_t show)
{
    static char text[FIREGLOW_FILE_MAX + 1u];
    char path[FIREGLOW_PATH_MAX + 1u];
    uint32_t k = 0;
    if (!f->loaded) return ZXM_E_STATE;
    if (show >= f->nshows) return ZXM_E_ARG;
    const fireglow_show_t *s = &f->show[show];
    if (!path_of(ROOT[s->kind], s->name, FIREGLOW_SAVED, "", path, sizeof path)) return ZXM_E_ARG;
    for (uint32_t i = 0; i < f->n; i++) {
        const fireglow_episode_t *e = &f->ep[i];
        if (e->show != show || (e->pos == 0 && e->length == 0 && e->heard == 0)) continue;
        k = put_bytes(text, k, FIREGLOW_FILE_MAX, e->name, len_of(e->name, FIREGLOW_NAME_MAX));
        k = put_bytes(text, k, FIREGLOW_FILE_MAX, "\t", 1u);
        k = put_number(text, k, FIREGLOW_FILE_MAX, e->pos);
        k = put_bytes(text, k, FIREGLOW_FILE_MAX, "\t", 1u);
        k = put_number(text, k, FIREGLOW_FILE_MAX, e->length);
        k = put_bytes(text, k, FIREGLOW_FILE_MAX, "\t", 1u);
        k = put_number(text, k, FIREGLOW_FILE_MAX, e->heard);
        k = put_bytes(text, k, FIREGLOW_FILE_MAX, "\n", 1u);
        if (k > FIREGLOW_FILE_MAX) {
            say(f, "Too many places to save for ", s->name);
            return ZXM_E_TOOBIG;
        }
    }
    zxm_link_t *l = f->link;
    zxm_begin(l, "files.write");
    zxm_p_str(l, "path", path);
    zxm_p_strn(l, "text", text, k);
    zxm_p_str(l, "mode", "replace");
    return zxm_call(l);
}

/* ---- FM3: chapters ---- */

int fireglow_chapters(fireglow_t *f, uint32_t ep)
{
    static char text[FIREGLOW_FILE_MAX + 1u];
    int64_t got[FIREGLOW_CHAPTERS];
    char path[FIREGLOW_PATH_MAX + 1u];
    uint32_t n = 0, m = 0;
    bool missing = false, bad = false;
    if (!f->loaded) return ZXM_E_STATE;
    if (ep >= f->n) return ZXM_E_ARG;
    const fireglow_episode_t *e = &f->ep[ep];
    const fireglow_show_t *s = &f->show[e->show];
    if (!path_of(ROOT[s->kind], s->name, e->name, FIREGLOW_CHAPTER_EXT, path, sizeof path))
        return ZXM_E_ARG;
    int rc = read_text(f->link, path, text, sizeof text, &n, &missing);
    if (rc == ZXM_E_TOOBIG)
        bad = true;
    else if (rc)
        return rc;
    for (uint32_t i = 0, start = 0; !bad && !missing && i <= n; i++) {
        if (i < n && text[i] != '\n') continue;
        uint32_t len = i - start;
        if (len && text[start + len - 1u] == '\r') len--;
        if (len) {
            int64_t v = number_of(text + start, len);
            if (v < 0 || v > FIREGLOW_LEN_MAX || m >= FIREGLOW_CHAPTERS || (m == 0 && v != 0) ||
                (m > 0 && v <= got[m - 1u]))
                bad = true;
            else
                got[m++] = v;
        }
        start = i + 1u;
    }
    if (bad) {
        say(f, "Not a chapter list: ", path);
        return ZXM_E_STATE;
    }
    memcpy(f->starts, got, m * sizeof got[0]);
    f->nstarts = m;
    f->chapters_of = (int32_t) ep;
    return ZXM_OK;
}

int64_t fireglow_chapter(const fireglow_t *f, uint32_t ep)
{
    if (ep >= f->n || f->chapters_of != (int32_t) ep) return FIREGLOW_REFUSED;
    return fireglow_chapter_at_pos(f->starts, f->nstarts, f->ep[ep].pos);
}

/* ---- FM4: playing ---- */

int fireglow_heard(fireglow_t *f, uint32_t ep, int64_t pos, int64_t length, int64_t listened)
{
    if (!f->loaded) return ZXM_E_STATE;
    if (ep >= f->n || listened < 0 || listened > FIREGLOW_HEARD_MAX) return ZXM_E_ARG;
    fireglow_episode_t *e = &f->ep[ep];
    int64_t heard = e->heard + listened;
    if (!fireglow_episode_row_ok((int64_t) e->show, (int64_t) ep, pos, length, heard) ||
        (length > 0 && pos > length))
        return ZXM_E_ARG;
    e->pos = pos;
    e->length = length;
    e->heard = heard;
    return ZXM_OK;
}

/* ---- FM5: the speed and the time left ---- */

int fireglow_set_speed(fireglow_t *f, int64_t rate)
{
    if (!fireglow_play_speed_ok(rate)) {
        say(f, "A speed from half to three times the normal one", "");
        return ZXM_E_ARG;
    }
    f->rate = rate;
    return ZXM_OK;
}

int fireglow_left(const fireglow_t *f, uint32_t ep, int64_t *ms)
{
    if (!ms) return ZXM_E_ARG;
    if (ep >= f->n) return ZXM_E_ARG;
    const fireglow_episode_t *e = &f->ep[ep];
    if (e->length == 0) return ZXM_E_STATE; /* not known yet */
    int64_t v = fireglow_speed_left_floor(e->length, e->pos, f->rate);
    if (v < 0) return ZXM_E_STATE;
    *ms = v;
    return ZXM_OK;
}

int fireglow_left_text(const fireglow_t *f, uint32_t ep, char *out, uint32_t cap)
{
    int64_t ms = 0;
    char d[24];
    if (!out || cap == 0) return ZXM_E_ARG;
    int rc = fireglow_left(f, ep, &ms);
    if (rc) return rc;
    int64_t hms = fireglow_left_hms(ms);
    if (hms < 0) return ZXM_E_STATE;
    int64_t h = hms / 10000, m = hms / 100 % 100, s = hms % 100;
    uint32_t k = sizeof d;
    d[--k] = 0;
    d[--k] = (char) ('0' + s % 10);
    d[--k] = (char) ('0' + s / 10);
    d[--k] = ':';
    d[--k] = (char) ('0' + m % 10);
    d[--k] = (char) ('0' + m / 10);
    d[--k] = ':';
    do {
        d[--k] = (char) ('0' + h % 10);
        h /= 10;
    } while (h);
    uint32_t at = (uint32_t) sizeof d - k;
    if (at > cap) return ZXM_E_TOOBIG;
    memcpy(out, d + k, at);
    return ZXM_OK;
}

/* ---- FM6: listening shares ---- */

int fireglow_shares(const fireglow_t *f, int64_t *share, uint32_t cap)
{
    int64_t minutes[FIREGLOW_SHOWS] = {0}, total = 0;
    if (!share || cap < f->nshows) return ZXM_E_ARG;
    for (uint32_t s = 0; s < f->nshows; s++) {
        int64_t heard = 0;
        for (uint32_t i = 0; i < f->n; i++)
            if (f->ep[i].show == s) heard += f->ep[i].heard; /* FR5 bounds each: no overflow */
        minutes[s] = heard / FIREGLOW_MINUTE_MS;
        total += minutes[s];
    }
    for (uint32_t s = 0; s < f->nshows; s++) {
        int64_t v = fireglow_listen_share(minutes[s], total);
        if (v < 0) return ZXM_E_STATE;
        share[s] = v;
    }
    return ZXM_OK;
}
