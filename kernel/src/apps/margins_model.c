/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* margins_model.c — Margins' model (margins_model.h). */
#include <string.h>

#include "margins_model.h"

#define MARGINS_E_FILES  (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define MARGINS_NOT_TEXT (-100)   /* read_file: zxvd says the file is not UTF-8 */
#define MARGINS_NAME_MAX 256u     /* one listed name (zxv_files.h ZXF_NAME_MAX + 1) */
#define MARGINS_LOG_MAX  (MARGINS_TRASHED * (24u + MARGINS_TITLE_MAX + MARGINS_PATH_MAX))
#define MARGINS_PINS_MAX (MARGINS_NOTES * (MARGINS_TITLE_MAX + 1u))
#define MARGINS_ASK      100u /* hits asked of search.query, notes kept of them */

/* ---- small text helpers, bounded ---- */

/* Append s to out (cap bytes) at *at; false when it does not fit. */
static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    size_t n = strlen(s);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n + 1u);
    *at += (uint32_t) n;
    return true;
}

static bool put_i64(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char d[24];
    uint32_t k = sizeof d - 1u;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    d[k] = 0;
    do {
        d[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u);
    if (v < 0) d[--k] = '-';
    return put(out, cap, at, d + k);
}

static void say(margins_t *m, const char *a, const char *b)
{
    uint32_t at = 0;
    m->notice[0] = 0;
    if (put(m->notice, sizeof m->notice, &at, a) && b) put(m->notice, sizeof m->notice, &at, b);
}

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (s && n < cap && s[n]) n++;
    return n;
}

/* n bytes of s are valid UTF-8 (no overlong form, no surrogate) with no
 * control byte, but newline and tab when lines (MM1). */
static bool utf8_ok(const char *s, uint32_t n, bool lines)
{
    for (uint32_t i = 0; i < n;) {
        unsigned char c = (unsigned char) s[i];
        if (c < 0x80u) {
            if ((c < 0x20u && !(lines && (c == '\n' || c == '\t'))) || c == 0x7Fu) return false;
            i++;
            continue;
        }
        uint32_t more, cp;
        if (c >= 0xC2u && c <= 0xDFu)
            more = 1u, cp = c & 0x1Fu;
        else if ((c & 0xF0u) == 0xE0u)
            more = 2u, cp = c & 0x0Fu;
        else if (c >= 0xF0u && c <= 0xF4u)
            more = 3u, cp = c & 0x07u;
        else
            return false;
        if (n - i <= more) return false;
        for (uint32_t j = 1; j <= more; j++) {
            unsigned char b = (unsigned char) s[i + j];
            if ((b & 0xC0u) != 0x80u) return false;
            cp = (cp << 6) | (b & 0x3Fu);
        }
        if ((more == 2u && (cp < 0x800u || (cp >= 0xD800u && cp <= 0xDFFFu))) ||
            (more == 3u && (cp < 0x10000u || cp > 0x10FFFFu)))
            return false;
        i += more + 1u;
    }
    return true;
}

/* MM5: a title is one Hoard file name once ".txt" is added (zxv_files.h
 * F2): UTF-8 with no control byte, '/', '\', ':', no leading '.' and no
 * trailing '.' or space, and 1..120 bytes (MR1). */
static bool title_ok(const char *t)
{
    uint32_t n = len_of(t, MARGINS_TITLE_MAX + 1u);
    if (!t || n > (uint32_t) MARGINS_TITLE_MAX || !margins_size_ok(n, 0, 0)) return false;
    if (t[0] == '.' || t[n - 1u] == '.' || t[n - 1u] == ' ' || !utf8_ok(t, n, false)) return false;
    for (uint32_t i = 0; i < n; i++)
        if (t[i] == '/' || t[i] == '\\' || t[i] == ':') return false;
    return true;
}

static bool path_of(const char *title, char *out, uint32_t cap)
{
    uint32_t at = 0;
    return put(out, cap, &at, MARGINS_DIR "/") && put(out, cap, &at, title) &&
           put(out, cap, &at, ".txt");
}

/* ---- MM3: tags ---- */

static bool tag_byte(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-';
}

/* The distinct tags of s[0..n), lower case, into out (up to cap of them);
 * the count found, which stops at cap. */
static uint32_t read_tags(const char *s, uint32_t n, char (*out)[MARGINS_TAG_LEN + 1u],
                          uint32_t cap)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < n && k < cap; i++) {
        if (s[i] != '#' || (i > 0u && s[i - 1u] != ' ' && s[i - 1u] != '\t' && s[i - 1u] != '\n'))
            continue;
        uint32_t j = i + 1u;
        while (j < n && tag_byte(s[j])) j++;
        uint32_t len = j - i - 1u;
        if (len >= 1u && len <= MARGINS_TAG_LEN) {
            char t[MARGINS_TAG_LEN + 1u];
            for (uint32_t q = 0; q < len; q++) {
                char c = s[i + 1u + q];
                t[q] = c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
            }
            t[len] = 0;
            bool seen = false;
            for (uint32_t q = 0; q < k && !seen; q++) seen = !strcmp(out[q], t);
            if (!seen) memcpy(out[k++], t, len + 1u);
        }
        i = j - 1u;
    }
    return k;
}

/* ---- zxvd ---- */

static bool no_such(const zxm_link_t *l, int rc)
{
    return rc == ZXM_E_REMOTE && l->err_code == MARGINS_E_FILES && strstr(l->err, "no such");
}

/* files.read of a whole small file into text (cap bytes); its length, or
 * a negative ZXM_ error; ZXM_E_STATE when it does not exist,
 * MARGINS_NOT_TEXT when it is not UTF-8, ZXM_E_TOOBIG when it does not
 * fit. */
static int32_t read_file(margins_t *m, const char *path, char *text, uint32_t cap)
{
    zxm_link_t *l = m->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "max", cap - 1u);
    int rc = zxm_call(l);
    if (no_such(l, rc)) return ZXM_E_STATE;
    if (rc == ZXM_E_REMOTE && l->err_code == MARGINS_E_FILES && strstr(l->err, "not text"))
        return MARGINS_NOT_TEXT;
    if (rc) return rc;
    bool eof = false;
    int32_t t = zxm_get(l, l->result, "text");
    if (t < 0 || zxm_get_bool(l, l->result, "eof", &eof)) return ZXM_E_PROTOCOL;
    if (!eof) return ZXM_E_TOOBIG;
    int32_t len = w4_json_str(l->resp, &l->tok[t], text, cap);
    return len < 0 ? ZXM_E_TOOBIG : len;
}

static int write_text(margins_t *m, const char *path, const char *text, uint32_t n, bool fresh)
{
    zxm_link_t *l = m->link;
    zxm_begin(l, "files.write");
    zxm_p_str(l, "path", path);
    zxm_p_strn(l, "text", text, n);
    if (fresh) zxm_p_bool(l, "exclusive", true);
    return zxm_call(l);
}

static int make_dir(margins_t *m, const char *path)
{
    zxm_link_t *l = m->link;
    zxm_begin(l, "files.mkdir");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE && l->err_code == MARGINS_E_FILES && strstr(l->err, "already exists"))
        rc = ZXM_OK;
    return rc;
}

/* ---- MM2-MM4: loading ---- */

int32_t margins_find(const margins_t *m, const char *title)
{
    if (!title) return -1;
    for (uint32_t i = 0; i < m->n; i++)
        if (!strcmp(m->note[i].title, title)) return (int32_t) i;
    return -1;
}

/* MM4: keys (MR2), places (MR3), the order and the pinned count (MR5). */
static int reorder(margins_t *m)
{
    static int64_t key[MARGINS_NOTES], pin[MARGINS_NOTES];
    for (uint32_t i = 0; i < m->n; i++) {
        key[i] = margins_key(m->note[i].pinned, m->note[i].mtime);
        if (key[i] < 0) return ZXM_E_PROTOCOL;
        pin[i] = m->note[i].pinned;
        m->note[i].key = key[i];
    }
    for (uint32_t i = 0; i < m->n; i++) {
        m->note[i].place = assay_score_rank(key, m->n, i);
        uint32_t j = i;
        while (j > 0u && m->note[m->order[j - 1u]].place > m->note[i].place) {
            m->order[j] = m->order[j - 1u];
            j--;
        }
        m->order[j] = i;
    }
    m->npinned = witness_kind_count(pin, m->n, 1);
    return ZXM_OK;
}

/* MM2: the Notes folder's listing into m->note. */
static int list_notes(margins_t *m)
{
    zxm_link_t *l = m->link;
    zxm_begin(l, "files.list");
    zxm_p_str(l, "path", MARGINS_DIR);
    int rc = zxm_call(l);
    if (no_such(l, rc)) return ZXM_OK; /* no folder yet: no notes */
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t count = zxm_len(l, arr);
    for (uint32_t i = 0; i < count; i++) {
        char name[MARGINS_NAME_MAX];
        bool dir = false;
        uint64_t size = 0;
        int64_t mtime = 0;
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || zxm_get_str(l, o, "name", name, sizeof name) ||
            zxm_get_bool(l, o, "dir", &dir) || zxm_get_u64(l, o, "size", &size) ||
            zxm_get_i64(l, o, "mtime", &mtime))
            return ZXM_E_PROTOCOL;
        uint32_t n = len_of(name, sizeof name);
        if (dir || n < 5u || strcmp(name + n - 4u, ".txt") || m->n >= MARGINS_NOTES || mtime < 0 ||
            mtime > MARGINS_STAMP_MAX || size > (uint64_t) INT64_MAX) {
            m->skipped++;
            continue;
        }
        name[n - 4u] = 0;
        if (!title_ok(name)) {
            m->skipped++;
            continue;
        }
        margins_note_t *e = &m->note[m->n++];
        memset(e, 0, sizeof *e);
        memcpy(e->title, name, n - 3u);
        e->bytes = (int64_t) size;
        e->mtime = mtime;
    }
    return ZXM_OK;
}

/* Margins/pins.txt: one pinned title a line; a title no longer listed is
 * dropped (the next pin writes the file without it). */
static int read_pins(margins_t *m)
{
    static char text[MARGINS_PINS_MAX + 1u];
    int32_t len = read_file(m, MARGINS_PINS, text, sizeof text);
    if (len == ZXM_E_STATE) return ZXM_OK;
    if (len < 0) return len == MARGINS_NOT_TEXT ? ZXM_E_PROTOCOL : len;
    for (uint32_t at = 0; at < (uint32_t) len;) {
        char t[MARGINS_TITLE_MAX + 1];
        uint32_t k = 0;
        while (at < (uint32_t) len && text[at] != '\n') {
            if (k >= (uint32_t) MARGINS_TITLE_MAX) return ZXM_E_PROTOCOL;
            t[k++] = text[at++];
        }
        if (at >= (uint32_t) len) return ZXM_E_PROTOCOL;
        at++;
        t[k] = 0;
        if (!title_ok(t)) return ZXM_E_PROTOCOL;
        int32_t i = margins_find(m, t);
        if (i >= 0) m->note[i].pinned = 1;
    }
    return ZXM_OK;
}

/* One field of s[*at..n) up to `end` into out (cap bytes); false when it is
 * empty, does not fit or has no end. */
static bool get_field(const char *s, uint32_t n, uint32_t *at, char end, char *out, uint32_t cap)
{
    uint32_t i = *at, k = 0;
    while (i < n && s[i] != end && s[i] != '\n' && s[i] != '\t') {
        if (k + 1u >= cap) return false;
        out[k++] = s[i++];
    }
    if (!k || i >= n || s[i] != end) return false;
    out[k] = 0;
    *at = i + 1u;
    return true;
}

/* Margins/trash.txt: stamp, title and its path in the Trash a line. */
static int read_trash(margins_t *m)
{
    static char text[MARGINS_LOG_MAX + 1u];
    int32_t len = read_file(m, MARGINS_TRASHLOG, text, sizeof text);
    if (len == ZXM_E_STATE) return ZXM_OK;
    if (len < 0) return len == MARGINS_NOT_TEXT ? ZXM_E_PROTOCOL : len;
    for (uint32_t at = 0; at < (uint32_t) len;) {
        char num[24];
        if (m->ntrash >= MARGINS_TRASHED) return ZXM_E_PROTOCOL;
        margins_trashed_t *t = &m->trash[m->ntrash];
        if (!get_field(text, (uint32_t) len, &at, '\t', num, sizeof num) ||
            !get_field(text, (uint32_t) len, &at, '\t', t->title, sizeof t->title) ||
            !get_field(text, (uint32_t) len, &at, '\n', t->trashed, sizeof t->trashed) ||
            !title_ok(t->title) || strncmp(t->trashed, ".Trash/", 7u))
            return ZXM_E_PROTOCOL;
        int64_t v = 0;
        for (const char *c = num; *c; c++) {
            if (*c < '0' || *c > '9' || v > (MARGINS_STAMP_MAX - 9) / 10) return ZXM_E_PROTOCOL;
            v = v * 10 + (*c - '0');
        }
        t->stamp = v;
        m->ntrash++;
    }
    return ZXM_OK;
}

/* MM2, MM3: each note within MR1's bytes read, its tags into the table,
 * every index row checked (MR4) and the notes of each tag counted (MR5). */
static int read_notes(margins_t *m)
{
    static char text[MARGINS_BODY_MAX + 1];
    static char found[MARGINS_TAGS_MAX + 1][MARGINS_TAG_LEN + 1u];
    static int64_t rows[MARGINS_NOTES * MARGINS_TAGS_MAX];
    char path[MARGINS_PATH_MAX];
    uint32_t nrows = 0;
    for (uint32_t i = 0; i < m->n; i++) {
        margins_note_t *e = &m->note[i];
        zxm_progress(m->link, i + 1u, m->n);
        if (e->bytes > MARGINS_BODY_MAX) continue;
        if (!path_of(e->title, path, sizeof path)) return ZXM_E_TOOBIG;
        int32_t len = read_file(m, path, text, sizeof text);
        if (len == MARGINS_NOT_TEXT || len == ZXM_E_TOOBIG) continue;
        if (len < 0) return len;
        uint32_t k = read_tags(text, (uint32_t) len, found, MARGINS_TAGS_MAX + 1u);
        if (!utf8_ok(text, (uint32_t) len, true) || !margins_size_ok(1, len, k)) continue;
        e->readable = true;
        for (uint32_t q = 0; q < k; q++) {
            uint32_t t = 0;
            while (t < m->ntags && strcmp(m->tag[t], found[q])) t++;
            if (t == m->ntags) {
                if (m->ntags >= MARGINS_TAG_TABLE) {
                    m->tags_dropped++;
                    continue;
                }
                memcpy(m->tag[m->ntags++], found[q], MARGINS_TAG_LEN + 1u);
            }
            if (!margins_tag_record_ok(t + 1, i + 1, e->mtime)) return ZXM_E_PROTOCOL;
            e->tag[e->ntags++] = (uint16_t) (t + 1u);
            rows[nrows++] = t + 1;
        }
    }
    for (uint32_t t = 0; t < m->ntags; t++)
        m->tag_notes[t] = witness_kind_count(rows, nrows, t + 1);
    return ZXM_OK;
}

void margins_init(margins_t *m, zxm_link_t *link)
{
    memset(m, 0, sizeof *m);
    m->link = link;
    m->open = -1;
}

int margins_load(margins_t *m)
{
    static margins_t nx;
    char open[MARGINS_TITLE_MAX + 1];
    open[0] = 0;
    if (m->open >= 0 && (uint32_t) m->open < m->n)
        memcpy(open, m->note[m->open].title, sizeof open);
    nx = *m;
    nx.n = nx.skipped = nx.ntags = nx.tags_dropped = nx.ntrash = nx.nhits = 0;
    nx.npinned = 0;
    int rc = list_notes(&nx);
    if (!rc) rc = read_pins(&nx);
    if (!rc) rc = read_trash(&nx);
    if (!rc) rc = read_notes(&nx);
    if (!rc) rc = reorder(&nx);
    if (rc) return rc;
    nx.loaded = true;
    nx.open = open[0] ? margins_find(&nx, open) : -1;
    if (nx.open < 0) nx.len = 0, nx.text[0] = 0;
    *m = nx;
    return ZXM_OK;
}

int margins_open(margins_t *m, const char *title)
{
    char path[MARGINS_PATH_MAX];
    if (!title_ok(title)) return ZXM_E_ARG;
    if (!m->loaded) return ZXM_E_STATE;
    int32_t i = margins_find(m, title);
    if (i < 0) return ZXM_E_ARG;
    if (!m->note[i].readable) {
        say(m, "This note is not text Margins can open, or is past its 8000 bytes: ", title);
        return ZXM_E_STATE;
    }
    if (!path_of(title, path, sizeof path)) return ZXM_E_TOOBIG;
    static char text[MARGINS_BODY_MAX + 1];
    int32_t len = read_file(m, path, text, sizeof text);
    if (len == ZXM_E_STATE) return ZXM_E_STATE; /* gone since the load */
    if (len == MARGINS_NOT_TEXT || len == ZXM_E_TOOBIG ||
        (len >= 0 && !utf8_ok(text, (uint32_t) len, true))) {
        m->note[i].readable = false; /* changed since the load */
        return ZXM_E_STATE;
    }
    if (len < 0) return len;
    memcpy(m->text, text, (size_t) len + 1u);
    m->len = (uint32_t) len;
    m->open = i;
    m->notice[0] = 0;
    return ZXM_OK;
}

/* ---- MM5: writing ---- */

/* Margins/pins.txt from the pinned notes of m. */
static int write_pins(margins_t *m)
{
    static char text[MARGINS_PINS_MAX + 1u];
    uint32_t at = 0;
    text[0] = 0;
    for (uint32_t i = 0; i < m->n; i++)
        if (m->note[i].pinned &&
            (!put(text, sizeof text, &at, m->note[i].title) || !put(text, sizeof text, &at, "\n")))
            return ZXM_E_TOOBIG;
    int rc = make_dir(m, MARGINS_OWN);
    return rc ? rc : write_text(m, MARGINS_PINS, text, at, false);
}

static int write_trash(margins_t *m)
{
    static char text[MARGINS_LOG_MAX + 1u];
    uint32_t at = 0;
    text[0] = 0;
    for (uint32_t i = 0; i < m->ntrash; i++) {
        const margins_trashed_t *t = &m->trash[i];
        if (!put_i64(text, sizeof text, &at, t->stamp) || !put(text, sizeof text, &at, "\t") ||
            !put(text, sizeof text, &at, t->title) || !put(text, sizeof text, &at, "\t") ||
            !put(text, sizeof text, &at, t->trashed) || !put(text, sizeof text, &at, "\n"))
            return ZXM_E_TOOBIG;
    }
    int rc = make_dir(m, MARGINS_OWN);
    return rc ? rc : write_text(m, MARGINS_TRASHLOG, text, at, false);
}

int margins_save(margins_t *m, const char *title, const char *text, uint32_t len, bool fresh)
{
    static char found[MARGINS_TAGS_MAX + 1][MARGINS_TAG_LEN + 1u];
    char path[MARGINS_PATH_MAX];
    if (!title_ok(title) || !text || len_of(text, len) != len || !utf8_ok(text, len, true))
        return ZXM_E_ARG;
    uint32_t k = read_tags(text, len, found, MARGINS_TAGS_MAX + 1u);
    if (!margins_size_ok(len_of(title, MARGINS_TITLE_MAX + 1u), len, k)) return ZXM_E_ARG;
    if (!m->loaded) return ZXM_E_STATE;
    int32_t i = margins_find(m, title);
    if (fresh && i >= 0) {
        say(m, "A note already has this title: ", title);
        return ZXM_E_STATE;
    }
    if (!fresh && i < 0) return ZXM_E_ARG;
    if (fresh && m->n >= MARGINS_NOTES) return ZXM_E_TOOBIG;
    if (!path_of(title, path, sizeof path)) return ZXM_E_TOOBIG;
    int rc = make_dir(m, MARGINS_DIR);
    if (!rc) rc = write_text(m, path, text, len, fresh);
    if (rc) return rc;
    rc = margins_load(m);
    if (rc) return rc;
    m->open = margins_find(m, title);
    if (m->open < 0) return ZXM_E_PROTOCOL;
    memcpy(m->text, text, len);
    m->text[len] = 0;
    m->len = len;
    m->notice[0] = 0;
    return ZXM_OK;
}

int margins_rename(margins_t *m, const char *from, const char *to)
{
    char pf[MARGINS_PATH_MAX], pt[MARGINS_PATH_MAX];
    if (!title_ok(from) || !title_ok(to) || !path_of(from, pf, sizeof pf) ||
        !path_of(to, pt, sizeof pt))
        return ZXM_E_ARG;
    if (!m->loaded) return ZXM_E_STATE;
    int32_t i = margins_find(m, from);
    if (i < 0) return ZXM_E_ARG;
    if (margins_find(m, to) >= 0) {
        say(m, "A note already has this title: ", to);
        return ZXM_E_STATE;
    }
    zxm_link_t *l = m->link;
    zxm_begin(l, "files.rename");
    zxm_p_str(l, "from", pf);
    zxm_p_str(l, "to", pt);
    int rc = zxm_call(l);
    if (rc) return rc;
    memcpy(m->note[i].title, to, len_of(to, MARGINS_TITLE_MAX + 1u) + 1u);
    if (m->note[i].pinned) rc = write_pins(m);
    int lr = margins_load(m);
    return rc ? rc : lr;
}

int margins_pin(margins_t *m, const char *title, int64_t pinned)
{
    if (!title_ok(title) || pinned < 0 || pinned > 1) return ZXM_E_ARG;
    if (!m->loaded) return ZXM_E_STATE;
    int32_t i = margins_find(m, title);
    if (i < 0) return ZXM_E_ARG;
    int64_t was = m->note[i].pinned;
    if (was == pinned) return ZXM_OK;
    m->note[i].pinned = pinned;
    int rc = write_pins(m);
    if (rc) {
        m->note[i].pinned = was;
        return rc;
    }
    return reorder(m);
}

/* ---- MM6: the Trash ---- */

int margins_trash(margins_t *m, const char *title, int64_t now)
{
    char path[MARGINS_PATH_MAX], trashed[MARGINS_PATH_MAX];
    if (!title_ok(title) || margins_trash_days(now, now) < 0 || !path_of(title, path, sizeof path))
        return ZXM_E_ARG;
    if (!m->loaded) return ZXM_E_STATE;
    int32_t i = margins_find(m, title);
    if (i < 0) return ZXM_E_ARG;
    zxm_link_t *l = m->link;
    zxm_begin(l, "files.trash");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "trashed", trashed, sizeof trashed) ||
        strncmp(trashed, ".Trash/", 7u))
        return ZXM_E_PROTOCOL;
    if (m->ntrash >= MARGINS_TRASHED) { /* the oldest is dropped from the log, not removed */
        memmove(&m->trash[0], &m->trash[1], (MARGINS_TRASHED - 1u) * sizeof m->trash[0]);
        m->ntrash--;
    }
    margins_trashed_t *t = &m->trash[m->ntrash++];
    t->stamp = now;
    memcpy(t->title, title, len_of(title, MARGINS_TITLE_MAX + 1u) + 1u);
    memcpy(t->trashed, trashed, sizeof trashed);
    rc = write_trash(m);
    if (!rc && m->note[i].pinned) {
        m->note[i].pinned = 0;
        rc = write_pins(m);
    }
    int lr = margins_load(m);
    return rc ? rc : lr;
}

int64_t margins_trash_left(const margins_t *m, uint32_t i, int64_t now)
{
    if (i >= m->ntrash) return MARGINS_REFUSED;
    return margins_trash_days(m->trash[i].stamp, now);
}

uint32_t margins_trash_due(const margins_t *m, int64_t now)
{
    uint32_t due = 0;
    for (uint32_t i = 0; i < m->ntrash; i++) due += margins_trash_left(m, i, now) == 0;
    return due;
}

static int drop_trashed(margins_t *m, uint32_t i)
{
    memmove(&m->trash[i], &m->trash[i + 1u], (size_t) (m->ntrash - i - 1u) * sizeof m->trash[0]);
    m->ntrash--;
    int rc = write_trash(m);
    int lr = margins_load(m);
    return rc ? rc : lr;
}

int margins_restore(margins_t *m, uint32_t i, bool confirmed)
{
    char path[MARGINS_PATH_MAX];
    if (!m->loaded) return ZXM_E_STATE;
    if (i >= m->ntrash || !path_of(m->trash[i].title, path, sizeof path)) return ZXM_E_ARG;
    if (!confirmed) {
        say(m, "Put back only on the owner's word: ", m->trash[i].title);
        return ZXM_E_STATE;
    }
    if (margins_find(m, m->trash[i].title) >= 0) {
        say(m, "A note already has this title: ", m->trash[i].title);
        return ZXM_E_STATE;
    }
    int rc = make_dir(m, MARGINS_DIR);
    if (rc) return rc;
    zxm_link_t *l = m->link;
    zxm_begin(l, "files.rename");
    zxm_p_str(l, "from", m->trash[i].trashed);
    zxm_p_str(l, "to", path);
    rc = zxm_call(l);
    if (rc) return rc;
    return drop_trashed(m, i);
}

int margins_forget(margins_t *m, uint32_t i, bool confirmed)
{
    bool removed = false;
    if (!m->loaded) return ZXM_E_STATE;
    if (i >= m->ntrash) return ZXM_E_ARG;
    if (!confirmed) {
        say(m, "Removed for good only on the owner's word: ", m->trash[i].title);
        return ZXM_E_STATE;
    }
    zxm_link_t *l = m->link;
    zxm_begin(l, "files.trash");
    zxm_p_str(l, "path", m->trash[i].trashed);
    int rc = zxm_call(l);
    if (no_such(l, rc)) return drop_trashed(m, i); /* already gone: the log forgets it */
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "removed", &removed) || !removed) return ZXM_E_PROTOCOL;
    return drop_trashed(m, i);
}

/* ---- MM7: finding ---- */

int margins_search(margins_t *m, const char *query)
{
    static uint32_t got[MARGINS_HITS];
    uint32_t n = len_of(query, MARGINS_QUERY_MAX + 1u), kept = 0;
    bool word = false;
    if (!query || n > MARGINS_QUERY_MAX || !utf8_ok(query, n, false)) return ZXM_E_ARG;
    for (uint32_t i = 0; i < n && !word; i++) word = query[i] != ' ';
    if (!word) return ZXM_E_ARG;
    if (!m->loaded) return ZXM_E_STATE;
    zxm_link_t *l = m->link;
    zxm_begin(l, "search.query");
    zxm_p_str(l, "q", query);
    zxm_p_u64(l, "max", MARGINS_ASK);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "hits");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > MARGINS_ASK)
        return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < zxm_len(l, arr) && kept < MARGINS_HITS; i++) {
        char path[MARGINS_PATH_MAX];
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || l->tok[o].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
        if (zxm_get_str(l, o, "path", path, sizeof path)) continue; /* longer than any note's */
        uint32_t pn = len_of(path, sizeof path);
        if (pn < 11u || strncmp(path, MARGINS_DIR "/", 6u) || strcmp(path + pn - 4u, ".txt"))
            continue;
        path[pn - 4u] = 0;
        int32_t at = margins_find(m, path + 6);
        if (at >= 0) got[kept++] = (uint32_t) at;
    }
    memcpy(m->hit, got, kept * sizeof got[0]);
    m->nhits = kept;
    return ZXM_OK;
}

int margins_tagged(margins_t *m, const char *tag)
{
    static char one[1][MARGINS_TAG_LEN + 1u];
    char buf[MARGINS_TAG_LEN + 3u];
    uint32_t at = 0;
    if (!tag) return ZXM_E_ARG;
    if (!put(buf, sizeof buf, &at, tag[0] == '#' ? "" : "#") || !put(buf, sizeof buf, &at, tag))
        return ZXM_E_ARG;
    if (read_tags(buf, at, one, 1u) != 1u || len_of(one[0], MARGINS_TAG_LEN + 1u) + 1u != at)
        return ZXM_E_ARG;
    if (!m->loaded) return ZXM_E_STATE;
    m->nhits = 0;
    uint32_t t = 0;
    while (t < m->ntags && strcmp(m->tag[t], one[0])) t++;
    if (t == m->ntags) return ZXM_OK;
    for (uint32_t q = 0; q < m->n && m->nhits < MARGINS_HITS; q++) {
        const margins_note_t *e = &m->note[m->order[q]];
        for (uint32_t j = 0; j < e->ntags; j++)
            if (e->tag[j] == t + 1u) {
                m->hit[m->nhits++] = m->order[q];
                break;
            }
    }
    return ZXM_OK;
}

int64_t margins_fill(const margins_t *m)
{
    if (m->open < 0) return MARGINS_REFUSED;
    return margins_fill_x100(m->len);
}
