/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cutwater_model.c — Cutwater's model (cutwater_model.h). */
#include <string.h>

#include "cutwater_model.h"

#define CW_U32_MAX INT64_C(4294967295) /* stream.plan's longest max_us */
#define CW_HEADER  "cutwater 1"

/* ---- small helpers, bounded ---- */

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (s && n < cap && s[n]) n++;
    return n;
}

static void say(cutwater_t *c, const char *a)
{
    uint32_t n = len_of(a, CUTWATER_NOTICE_MAX);
    memmove(c->notice, a, n);
    c->notice[n] = 0;
}

static bool path_ok(const char *path)
{
    uint32_t n = len_of(path, CUTWATER_PATH_MAX + 1u);
    return path && n > 0 && n <= CUTWATER_PATH_MAX;
}

static bool put_text(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = len_of(s, cap);
    if (*at + n > cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    return true;
}

/* v (0 or more) in decimal */
static bool put_num(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char d[20];
    uint32_t n = 0;
    if (v < 0) return false;
    do {
        d[n++] = (char) ('0' + v % 10);
        v /= 10;
    } while (v);
    if (*at + n > cap) return false;
    for (uint32_t i = 0; i < n; i++) out[*at + i] = d[n - 1u - i];
    *at += n;
    return true;
}

/* digits at *p up to a space or the end, at most 18 of them */
static bool get_num(const char **p, const char *end, int64_t *v)
{
    int64_t x = 0;
    uint32_t n = 0;
    while (*p < end && **p >= '0' && **p <= '9') {
        if (++n > 18u) return false;
        x = x * 10 + (**p - '0');
        (*p)++;
    }
    if (n == 0 || (*p < end && **p != ' ')) return false;
    if (*p < end) (*p)++;
    *v = x;
    return true;
}

/* does the line [s, end) start with word w? *rest is what follows */
static bool starts(const char *s, const char *end, const char *w, const char **rest)
{
    uint32_t n = len_of(w, 16u);
    if ((uint32_t) (end - s) < n || memcmp(s, w, n)) return false;
    *rest = s + n;
    return true;
}

static int64_t clip_end(const cutwater_clip_t *k)
{
    return k->place + (k->cutout - k->cutin);
}

/* ---- CM1: sources ---- */

void cutwater_init(cutwater_t *c, zxm_link_t *link)
{
    memset(c, 0, sizeof *c);
    c->link = link;
    kinetoscope_init(&c->kine, link);
}

int cutwater_source_add(cutwater_t *c, const char *path, int64_t *source)
{
    if (!path_ok(path) || !source) return ZXM_E_ARG;
    for (uint32_t i = 0; i < c->nsources; i++)
        if (!strcmp(c->src[i].path, path)) {
            say(c, "That video is already a source");
            return ZXM_E_STATE;
        }
    if (c->nsources >= CUTWATER_SOURCES) {
        say(c, "Every source place is taken");
        return ZXM_E_STATE;
    }
    kinetoscope_t *k = &c->kine;
    int rc = kinetoscope_open(k, path);
    if (rc == ZXM_E_STATE) say(c, k->notice);
    if (rc) return rc;
    if (cutwater_fps(k->fnum, k->fden) < 0) {
        say(c, "A frame rate past 999 a second has no timecode");
        return ZXM_E_STATE;
    }
    if (c->nsources && k->fnum * c->fden != c->fnum * k->fden) {
        say(c, "Another frame rate than the edit list's: nothing here converts it");
        return ZXM_E_STATE;
    }
    if (k->frames < 1 || k->frames > CUTWATER_FRAME_MAX) {
        say(c, "A source of no frames, or of more than 999999999");
        return ZXM_E_STATE;
    }
    cutwater_source_t *s = &c->src[c->nsources];
    memset(s, 0, sizeof *s);
    memcpy(s->path, path, len_of(path, CUTWATER_PATH_MAX) + 1u);
    s->frames = k->frames;
    s->width = k->width;
    s->height = k->height;
    if (!c->nsources) {
        c->fnum = k->fnum;
        c->fden = k->fden;
    }
    c->nsources++;
    *source = (int64_t) c->nsources;
    c->notice[0] = 0;
    return ZXM_OK;
}

/* ---- CM2: cut points ---- */

int cutwater_scan(cutwater_t *c, int64_t source)
{
    static uint8_t key[CUTWATER_SCAN_FRAMES / 8u];
    const int64_t most = (int64_t) CUTWATER_SCAN_FRAMES;
    if (source < 1 || source > (int64_t) c->nsources) return ZXM_E_ARG;
    cutwater_source_t *s = &c->src[source - 1];
    kinetoscope_t *k = &c->kine;
    int64_t points = 0;
    int rc = kinetoscope_open(k, s->path);
    if (rc == ZXM_E_STATE) say(c, k->notice);
    if (rc) return rc;
    if (k->fnum * c->fden != c->fnum * k->fden || k->frames != s->frames) {
        say(c, "The source changed since it was added");
        return ZXM_E_STATE;
    }
    memset(key, 0, sizeof key);
    while (!k->at_end && k->read_frames < most) {
        rc = kinetoscope_index(k);
        if (rc == ZXM_E_STATE) say(c, k->notice);
        if (rc) return rc;
        rc = kinetoscope_plan(k, 1, CW_U32_MAX);
        if (rc) return rc;
        for (uint32_t i = 0; i < k->nsegs; i++) {
            int64_t f = k->first_frame + (int64_t) k->seg[i].first_unit;
            if (!k->seg[i].key_start || f >= most) continue;
            key[f / 8] |= (uint8_t) (1u << (f % 8));
            points++;
        }
    }
    memcpy(s->key, key, sizeof key);
    s->scanned_frames = k->read_frames < most ? k->read_frames : most;
    s->cut_points = points;
    s->scanned = true;
    c->notice[0] = 0;
    return ZXM_OK;
}

bool cutwater_is_cut_point(const cutwater_t *c, int64_t source, int64_t frame)
{
    if (source < 1 || source > (int64_t) c->nsources) return false;
    const cutwater_source_t *s = &c->src[source - 1];
    if (!s->scanned || frame < 0 || frame >= s->scanned_frames) return false;
    return (s->key[frame / 8] >> (frame % 8) & 1u) != 0;
}

/* ---- CM3: the edit list ---- */

int cutwater_clip_add(cutwater_t *c, int64_t track, int64_t source, int64_t cutin, int64_t cutout,
                      int64_t place)
{
    if (track < 1 || track > CUTWATER_TRACKS || source < 1 || source > (int64_t) c->nsources ||
        cutin < 0 || cutin > CUTWATER_FRAME_MAX || cutout < 0 || cutout > CUTWATER_FRAME_MAX ||
        place < 0 || place > CUTWATER_FRAME_MAX)
        return ZXM_E_ARG;
    if (c->nclips >= CUTWATER_CLIPS) {
        say(c, "The edit list is full");
        return ZXM_E_STATE;
    }
    const cutwater_source_t *s = &c->src[source - 1];
    if (!s->scanned) {
        say(c, "Scan the source's cut points first");
        return ZXM_E_STATE;
    }
    if (!cutwater_is_cut_point(c, source, cutin)) {
        say(c, "A clip starts on a cut point (a keyframe)");
        return ZXM_E_ARG;
    }
    /* its place: after every clip of a lower track, or of its own placed at or before it */
    uint32_t at = 0;
    while (at < c->nclips && (c->clip[at].track < track ||
                              (c->clip[at].track == track && c->clip[at].place <= place)))
        at++;
    int64_t span = cutout - cutin;
    int64_t next =
        at < c->nclips && c->clip[at].track == track ? c->clip[at].place : CUTWATER_FRAME_MAX;
    if (!cutwater_clip_ok(span, s->frames - cutout, next - (place + span))) {
        say(c, "Not a valid clip: in before out, inside its source, clear of the next clip");
        return ZXM_E_ARG;
    }
    if (at > 0 && c->clip[at - 1u].track == track) {
        const cutwater_clip_t *p = &c->clip[at - 1u];
        if (!cutwater_clip_ok(p->cutout - p->cutin, c->src[p->source - 1].frames - p->cutout,
                              place - clip_end(p))) {
            say(c, "The clip overlaps the one before it on its track");
            return ZXM_E_ARG;
        }
    }
    memmove(&c->clip[at + 1u], &c->clip[at], (c->nclips - at) * sizeof c->clip[0]);
    c->clip[at].track = track;
    c->clip[at].source = source;
    c->clip[at].cutin = cutin;
    c->clip[at].cutout = cutout;
    c->clip[at].place = place;
    c->nclips++;
    c->notice[0] = 0;
    return ZXM_OK;
}

int cutwater_clip_remove(cutwater_t *c, uint32_t clip)
{
    if (clip >= c->nclips) return ZXM_E_ARG;
    memmove(&c->clip[clip], &c->clip[clip + 1u], (c->nclips - clip - 1u) * sizeof c->clip[0]);
    c->nclips--;
    memset(&c->clip[c->nclips], 0, sizeof c->clip[0]);
    return ZXM_OK;
}

/* ---- CM4: timecode ---- */

int64_t cutwater_length(const cutwater_t *c)
{
    int64_t len = 0;
    for (uint32_t i = 0; i < c->nclips; i++) {
        int64_t e = clip_end(&c->clip[i]);
        if (e > len) len = e;
    }
    return len;
}

int cutwater_timecode_text(const cutwater_t *c, int64_t frame, char *out, uint32_t cap)
{
    char d[CUTWATER_TC_TEXT];
    if (!out || cap == 0) return ZXM_E_ARG;
    if (!c->nsources) return ZXM_E_STATE;
    int64_t tc = cutwater_timecode(frame, c->fnum, c->fden);
    if (tc < 0) return ZXM_E_ARG;
    int64_t h = tc / 10000000, m = tc / 100000 % 100, s = tc / 1000 % 100, f = tc % 1000;
    uint32_t i = sizeof d, fd = cutwater_fps(c->fnum, c->fden) > 100 ? 3u : 2u;
    d[--i] = 0;
    for (uint32_t j = 0; j < fd; j++, f /= 10) d[--i] = (char) ('0' + f % 10);
    d[--i] = ':';
    d[--i] = (char) ('0' + s % 10);
    d[--i] = (char) ('0' + s / 10);
    d[--i] = ':';
    d[--i] = (char) ('0' + m % 10);
    d[--i] = (char) ('0' + m / 10);
    d[--i] = ':';
    do {
        d[--i] = (char) ('0' + h % 10);
        h /= 10;
    } while (h);
    uint32_t n = (uint32_t) sizeof d - i;
    if (n > cap) return ZXM_E_TOOBIG;
    memcpy(out, d + i, n);
    return ZXM_OK;
}

int64_t cutwater_playhead_frame(const cutwater_t *c, int64_t ms)
{
    if (!c->nsources) return CUTWATER_REFUSED;
    return cutwater_frame_at_ms(ms, c->fnum, c->fden);
}

/* ---- CM5: save ---- */

int cutwater_save(cutwater_t *c, const char *path, bool owner_confirmed)
{
    static char text[CUTWATER_TEXT_MAX + 1u];
    const uint32_t cap = CUTWATER_TEXT_MAX;
    uint32_t at = 0;
    if (!path_ok(path)) return ZXM_E_ARG;
    if (!c->nsources) {
        say(c, "Nothing to save: add a source first");
        return ZXM_E_STATE;
    }
    for (uint32_t i = 0; i < c->nclips; i++) {
        const cutwater_clip_t *k = &c->clip[i];
        if (!cutwater_clip_row_ok(i, k->track, k->source, k->cutin, k->cutout, k->place)) {
            say(c, "A clip's record does not fit");
            return ZXM_E_STATE;
        }
    }
    bool ok = put_text(text, cap, &at, CW_HEADER "\nrate ") && put_num(text, cap, &at, c->fnum) &&
              put_text(text, cap, &at, " ") && put_num(text, cap, &at, c->fden) &&
              put_text(text, cap, &at, "\n");
    for (uint32_t i = 0; ok && i < c->nsources; i++)
        ok = put_text(text, cap, &at, "source ") && put_text(text, cap, &at, c->src[i].path) &&
             put_text(text, cap, &at, "\n");
    for (uint32_t i = 0; ok && i < c->nclips; i++) {
        const cutwater_clip_t *k = &c->clip[i];
        ok = put_text(text, cap, &at, "clip ") && put_num(text, cap, &at, k->track) &&
             put_text(text, cap, &at, " ") && put_num(text, cap, &at, k->source) &&
             put_text(text, cap, &at, " ") && put_num(text, cap, &at, k->cutin) &&
             put_text(text, cap, &at, " ") && put_num(text, cap, &at, k->cutout) &&
             put_text(text, cap, &at, " ") && put_num(text, cap, &at, k->place) &&
             put_text(text, cap, &at, "\n");
    }
    if (!ok) return ZXM_E_TOOBIG;
    text[at] = 0;
    if (!owner_confirmed) {
        say(c, "Only the owner's confirmed word writes the edit list");
        return ZXM_E_STATE;
    }
    zxm_link_t *l = c->link;
    zxm_begin(l, "files.write");
    zxm_p_str(l, "path", path);
    zxm_p_strn(l, "text", text, at);
    int rc = zxm_call(l);
    if (rc) return rc;
    c->notice[0] = 0;
    return ZXM_OK;
}

/* ---- CM6: open ---- */

/* one line of a saved list into n; ZXM_OK, or the refusal (its notice in n) */
static int open_line(cutwater_t *n, const char *s, const char *end, int64_t rnum, int64_t rden)
{
    static char path[CUTWATER_PATH_MAX + 1u];
    const char *p = s;
    int64_t v[5];
    if (starts(s, end, "source ", &p)) {
        uint32_t len = (uint32_t) (end - p);
        int64_t id = 0;
        if (len == 0 || len > CUTWATER_PATH_MAX) return ZXM_E_ARG;
        memcpy(path, p, len);
        path[len] = 0;
        int rc = cutwater_source_add(n, path, &id);
        if (!rc) rc = cutwater_scan(n, id);
        if (rc) return rc;
        if (n->fnum * rden != rnum * n->fden) {
            say(n, "The list's rate line is not its sources' rate");
            return ZXM_E_STATE;
        }
        return ZXM_OK;
    }
    if (starts(s, end, "clip ", &p)) {
        for (uint32_t i = 0; i < 5u; i++)
            if (!get_num(&p, end, &v[i])) return ZXM_E_ARG;
        if (p != end) return ZXM_E_ARG;
        return cutwater_clip_add(n, v[0], v[1], v[2], v[3], v[4]);
    }
    say(n, "A line that is not a source or a clip");
    return ZXM_E_ARG;
}

int cutwater_open(cutwater_t *c, const char *path)
{
    static char text[CUTWATER_TEXT_MAX + 1u];
    static cutwater_t next;
    if (!path_ok(path)) return ZXM_E_ARG;
    zxm_link_t *l = c->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "max", CUTWATER_TEXT_MAX);
    int rc = zxm_call(l);
    if (rc) return rc;
    int64_t size = 0;
    bool eof = false;
    if (zxm_get_i64(l, l->result, "size", &size) || zxm_get_bool(l, l->result, "eof", &eof))
        return ZXM_E_PROTOCOL;
    if (size > (int64_t) CUTWATER_TEXT_MAX || !eof) {
        say(c, "Not an edit list: longer than any Cutwater writes");
        return ZXM_E_STATE;
    }
    if (zxm_get_str(l, l->result, "text", text, sizeof text)) return ZXM_E_PROTOCOL;
    const char *s = text, *end = text + len_of(text, CUTWATER_TEXT_MAX);
    const char *e = s, *p = s;
    int64_t rnum = 0, rden = 0;
    /* the first two lines: the header and the rate */
    while (e < end && *e != '\n') e++;
    bool ok =
        (uint32_t) (e - s) == sizeof CW_HEADER - 1u && !memcmp(s, CW_HEADER, e - s) && e < end;
    if (ok) {
        s = e + 1;
        for (e = s; e < end && *e != '\n';) e++;
        ok = starts(s, e, "rate ", &p) && get_num(&p, e, &rnum) && get_num(&p, e, &rden) &&
             p == e && cutwater_fps(rnum, rden) > 0;
    }
    if (!ok) {
        say(c, "Not an edit list: no Cutwater header and rate");
        return ZXM_E_STATE;
    }
    cutwater_init(&next, l);
    for (s = e < end ? e + 1 : end; s < end; s = e < end ? e + 1 : end) {
        for (e = s; e < end && *e != '\n';) e++;
        if (e == s) continue;
        rc = open_line(&next, s, e, rnum, rden);
        if (rc) {
            say(c, next.notice[0] ? next.notice : "A line of the edit list is refused");
            return rc;
        }
    }
    if (!next.nsources) {
        say(c, "Not an edit list: it has no source");
        return ZXM_E_STATE;
    }
    *c = next;
    return ZXM_OK;
}
