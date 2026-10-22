/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cartulary_model.c — Cartulary's model (cartulary_model.h). */
#include <string.h>

#include "cartulary_model.h"

#define CARTULARY_E_FILES (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define CARTULARY_FIELDS  10u      /* integer fields of an index line (CM1) */
#define CARTULARY_LINE    (CARTULARY_FIELDS * 21u + CARTULARY_PATH_MAX + 2u)
#define CARTULARY_TEXT    (CARTULARY_ROWS * CARTULARY_LINE)
#define CARTULARY_PIECE   6000u /* bytes of text in one files.write, escaped at most twice */
#define CARTULARY_ASK     100u  /* hits asked of search.query */
#define CARTULARY_DAY_MS  INT64_C(86400000)
#define CARTULARY_KIND    1 /* witness.audit_ok's kind for a filing */

/* ---- small text helpers, bounded ---- */

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

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (s && n < cap && s[n]) n++;
    return n;
}

static void say(cartulary_t *c, const char *a, const char *b)
{
    uint32_t at = 0;
    c->notice[0] = 0;
    if (put(c->notice, sizeof c->notice, &at, a) && b) put(c->notice, sizeof c->notice, &at, b);
}

/* A decimal integer of s[*at..n) up to `end`, a sign allowed; false when
 * it is empty, not a number, past 18 digits or has no end. */
static bool get_i64(const char *s, uint32_t n, uint32_t *at, char end, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    bool neg = i < n && s[i] == '-';
    int64_t x = 0;
    if (neg) i++;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        if (++digits > 18u) return false;
        x = x * 10 + (s[i++] - '0');
    }
    if (!digits || i >= n || s[i] != end) return false;
    *v = neg ? -x : x;
    *at = i + 1u;
    return true;
}

/* CM1: a record's path is a Hoard path of 1..200 bytes with no control
 * byte or tab, no leading '/' or '.', no "..", and not Cartulary's own. */
static bool path_ok(const char *p)
{
    uint32_t n = len_of(p, CARTULARY_PATH_MAX + 1u);
    if (!p || !n || n > CARTULARY_PATH_MAX || p[0] == '/' || p[0] == '.') return false;
    if (!strncmp(p, CARTULARY_DIR "/", sizeof CARTULARY_DIR) || strstr(p, "..")) return false;
    for (uint32_t i = 0; i < n; i++)
        if ((unsigned char) p[i] < 0x20u || p[i] == 0x7F) return false;
    return true;
}

/* ---- civil dates (days since 1970), for the grid and the months ---- */

static void civil(int64_t z, int64_t *y, int64_t *m)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = yoe + era * 400 + (*m <= 2);
}

static int64_t from_civil(int64_t y, int64_t m)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

int64_t cartulary_year(int64_t day)
{
    int64_t y, m;
    civil(day, &y, &m);
    return y;
}

/* ---- zxvd ---- */

static bool no_such(const zxm_link_t *l, int rc)
{
    return rc == ZXM_E_REMOTE && l->err_code == CARTULARY_E_FILES && strstr(l->err, "no such");
}

/* files.read of a whole small file; its length, ZXM_E_STATE when it does
 * not exist, ZXM_E_TOOBIG when it does not fit. */
static int32_t read_file(cartulary_t *c, const char *path, char *text, uint32_t cap)
{
    zxm_link_t *l = c->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "max", cap - 1u);
    int rc = zxm_call(l);
    if (no_such(l, rc)) return ZXM_E_STATE;
    if (rc) return rc;
    bool eof = false;
    int32_t t = zxm_get(l, l->result, "text");
    if (t < 0 || zxm_get_bool(l, l->result, "eof", &eof)) return ZXM_E_PROTOCOL;
    if (!eof) return ZXM_E_TOOBIG;
    int32_t len = w4_json_str(l->resp, &l->tok[t], text, cap);
    return len < 0 ? ZXM_E_TOOBIG : len;
}

/* files.write of text whole, in pieces cut on character boundaries. */
static int write_file(cartulary_t *c, const char *path, const char *text, uint32_t len)
{
    zxm_link_t *l = c->link;
    zxm_begin(l, "files.mkdir");
    zxm_p_str(l, "path", CARTULARY_DIR);
    int rc = zxm_call(l);
    if (rc && !(rc == ZXM_E_REMOTE && l->err_code == CARTULARY_E_FILES &&
                strstr(l->err, "already exists")))
        return rc;
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at < CARTULARY_PIECE ? len - at : CARTULARY_PIECE;
        if (at + n < len) n = zxm_utf8_cut(text + at, n);
        bool last = at + n >= len;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", path);
        zxm_p_strn(l, "text", text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

/* The journal's newest seq (0 with no journal or no entry). */
static int journal_newest(cartulary_t *c, int64_t *seq)
{
    zxm_link_t *l = c->link;
    bool on = false;
    *seq = 0;
    zxm_begin(l, "journal.query");
    zxm_p_u64(l, "max", 1);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (zxm_get_bool(l, l->result, "on", &on) || arr < 0 || l->tok[arr].type != W4_J_ARR)
        return ZXM_E_PROTOCOL;
    if (!on || !zxm_len(l, arr)) return ZXM_OK;
    int32_t e = zxm_at(l, arr, 0);
    if (e < 0 || zxm_get_i64(l, e, "seq", seq) || *seq < 1 || *seq > WITNESS_SEQ_MAX)
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

/* ---- CM1: the index and the holds ---- */

static bool row_ok(const cartulary_row_t *r)
{
    return cartulary_node_ok(r->cls, r->record, r->version, r->filed, 0) &&
           cartulary_entry_ok(r->cls, r->record, r->size, r->filed) && r->cls >= 1 &&
           r->cls <= CARTULARY_CLASSES && r->record >= 1 && r->version >= 1 && r->filed >= 0 &&
           r->size >= 0 && r->content > 0 && r->source >= 1 && r->source <= 2 && r->ruled >= 0 &&
           r->ruled <= 2 && r->seq >= 0 && r->prev >= 0 &&
           witness_record_fits(r->seq, r->prev, CARTULARY_KIND, r->filed * CARTULARY_DAY_MS) &&
           path_ok(r->path);
}

static int read_index(cartulary_t *c)
{
    static char text[CARTULARY_TEXT + 1u];
    int32_t len = read_file(c, CARTULARY_INDEX, text, sizeof text);
    if (len == ZXM_E_STATE) return ZXM_OK;
    if (len < 0) return len;
    for (uint32_t at = 0; at < (uint32_t) len;) {
        if (c->n >= CARTULARY_ROWS) return ZXM_E_PROTOCOL;
        cartulary_row_t *r = &c->row[c->n];
        int64_t *f[CARTULARY_FIELDS] = {&r->cls,     &r->record, &r->version, &r->filed, &r->size,
                                        &r->content, &r->source, &r->ruled,   &r->seq,   &r->prev};
        for (uint32_t k = 0; k < CARTULARY_FIELDS; k++)
            if (!get_i64(text, (uint32_t) len, &at, '\t', f[k])) return ZXM_E_PROTOCOL;
        uint32_t k = 0;
        while (at < (uint32_t) len && text[at] != '\n') {
            if (k >= CARTULARY_PATH_MAX) return ZXM_E_PROTOCOL;
            r->path[k++] = text[at++];
        }
        if (at >= (uint32_t) len) return ZXM_E_PROTOCOL;
        at++;
        r->path[k] = 0;
        if (!row_ok(r) || cartulary_find(c, r->record, r->version, r->source) >= 0)
            return ZXM_E_PROTOCOL;
        c->n++;
    }
    return ZXM_OK;
}

static int write_index(cartulary_t *c)
{
    static char text[CARTULARY_TEXT + 1u];
    uint32_t at = 0;
    text[0] = 0;
    for (uint32_t i = 0; i < c->n; i++) {
        const cartulary_row_t *r = &c->row[i];
        const int64_t f[CARTULARY_FIELDS] = {r->cls,     r->record, r->version, r->filed, r->size,
                                             r->content, r->source, r->ruled,   r->seq,   r->prev};
        for (uint32_t k = 0; k < CARTULARY_FIELDS; k++)
            if (!put_i64(text, sizeof text, &at, f[k]) || !put(text, sizeof text, &at, "\t"))
                return ZXM_E_TOOBIG;
        if (!put(text, sizeof text, &at, r->path) || !put(text, sizeof text, &at, "\n"))
            return ZXM_E_TOOBIG;
    }
    return write_file(c, CARTULARY_INDEX, text, at);
}

static int read_holds(cartulary_t *c)
{
    static char text[CARTULARY_HELD * 21u + 1u];
    int32_t len = read_file(c, CARTULARY_HOLDS, text, sizeof text);
    if (len == ZXM_E_STATE) return ZXM_OK;
    if (len < 0) return len;
    for (uint32_t at = 0; at < (uint32_t) len;) {
        int64_t r = 0;
        if (c->nheld >= CARTULARY_HELD || !get_i64(text, (uint32_t) len, &at, '\n', &r) || r < 1 ||
            !witness_digits_fit(r, 9) || cartulary_held(c, r))
            return ZXM_E_PROTOCOL;
        c->held[c->nheld++] = r;
    }
    return ZXM_OK;
}

static int write_holds(cartulary_t *c)
{
    static char text[CARTULARY_HELD * 21u + 1u];
    uint32_t at = 0;
    text[0] = 0;
    for (uint32_t i = 0; i < c->nheld; i++)
        if (!put_i64(text, sizeof text, &at, c->held[i]) || !put(text, sizeof text, &at, "\n"))
            return ZXM_E_TOOBIG;
    return write_file(c, CARTULARY_HOLDS, text, at);
}

void cartulary_init(cartulary_t *c, zxm_link_t *link)
{
    memset(c, 0, sizeof *c);
    c->link = link;
}

int cartulary_set_dest(cartulary_t *c, const char *dest)
{
    uint32_t n = len_of(dest, CARTULARY_DEST_MAX + 1u);
    if (!dest || !n || n > CARTULARY_DEST_MAX || dest[0] != '/') return ZXM_E_ARG;
    memcpy(c->dest, dest, n + 1u);
    return ZXM_OK;
}

int cartulary_load(cartulary_t *c)
{
    c->n = c->nheld = c->nhits = 0;
    c->loaded = false;
    int rc = read_index(c);
    if (!rc) rc = read_holds(c);
    if (rc) {
        c->n = c->nheld = 0;
        say(c, "the index could not be read: ", c->link->err);
        return rc;
    }
    c->loaded = true;
    return ZXM_OK;
}

int32_t cartulary_find(const cartulary_t *c, int64_t record, int64_t version, int64_t source)
{
    for (uint32_t i = 0; i < c->n; i++)
        if (c->row[i].record == record && c->row[i].version == version &&
            c->row[i].source == source)
            return (int32_t) i;
    return -1;
}

bool cartulary_held(const cartulary_t *c, int64_t record)
{
    for (uint32_t i = 0; i < c->nheld; i++)
        if (c->held[i] == record) return true;
    return false;
}

/* The newest version of a record, any source (0: no such record). */
static int64_t newest(const cartulary_t *c, int64_t record, int32_t *row)
{
    int64_t v = 0;
    for (uint32_t i = 0; i < c->n; i++)
        if (c->row[i].record == record && c->row[i].version > v) {
            v = c->row[i].version;
            if (row) *row = (int32_t) i;
        }
    return v;
}

/* ---- CM3: filing ---- */

/* The first 15 hex digits of a SHA-256 as an integer (CM1). */
static int64_t content_of(const char *hex)
{
    int64_t v = 0;
    for (uint32_t i = 0; i < 15u; i++) {
        char h = hex[i];
        int d = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : -1;
        if (d < 0) return 0;
        v = v * 16 + d;
    }
    return v;
}

static int newest_content(cartulary_t *c, const char *path, int64_t *content)
{
    zxm_link_t *l = c->link;
    char hex[65];
    zxm_begin(l, "backup.versions");
    zxm_p_str(l, "dest", c->dest);
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "versions");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    if (!zxm_len(l, arr)) {
        say(c, "not backed up yet: back it up with Keepsake first: ", path);
        return ZXM_E_STATE;
    }
    int32_t v = zxm_at(l, arr, 0);
    if (v < 0 || zxm_get_str(l, v, "hash", hex, sizeof hex) || len_of(hex, sizeof hex) != 64u)
        return ZXM_E_PROTOCOL;
    *content = content_of(hex);
    return *content > 0 ? ZXM_OK : ZXM_E_PROTOCOL;
}

static int64_t record_of(const cartulary_t *c, const char *path)
{
    for (uint32_t i = 0; i < c->n; i++)
        if (!strcmp(c->row[i].path, path)) return c->row[i].record;
    return 0;
}

static int64_t next_record(const cartulary_t *c)
{
    int64_t r = 0;
    for (uint32_t i = 0; i < c->n; i++)
        if (c->row[i].record > r) r = c->row[i].record;
    return r + 1;
}

int cartulary_file(cartulary_t *c, int64_t cls, const char *path, int64_t today, int64_t *record)
{
    int64_t years, months, size = 0, mtime = 0, content = 0, seq = 0, left = 0;
    bool dir = false;
    if (!c->loaded || !c->dest[0]) return ZXM_E_STATE;
    if (!record || !path_ok(path) || !cartulary_schedule(cls, &years, &months) ||
        !cartulary_retention_ok(cartulary_span(years, months), 0) ||
        !cartulary_left(today, years, months, today, &left))
        return ZXM_E_ARG;
    zxm_link_t *l = c->link;
    zxm_begin(l, "files.stat");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "dir", &dir) || zxm_get_i64(l, l->result, "size", &size) ||
        zxm_get_i64(l, l->result, "mtime", &mtime))
        return ZXM_E_PROTOCOL;
    if (dir) return ZXM_E_ARG;
    rc = newest_content(c, path, &content);
    if (rc) return rc;
    int64_t rec = record_of(c, path);
    int32_t last = -1;
    int64_t version = rec ? newest(c, rec, &last) + 1 : 1;
    if (rec && c->row[last].cls != cls) return ZXM_E_ARG; /* a record keeps its class */
    if (rec && c->row[last].content == content) {
        *record = rec; /* the same content files nothing */
        return ZXM_OK;
    }
    int64_t prev = 0;
    for (int64_t v = version - 1; v >= 1 && !prev; v--) {
        int32_t i = cartulary_find(c, rec, v, 1);
        if (i >= 0) prev = c->row[i].seq;
    }
    if (c->n >= CARTULARY_ROWS) return ZXM_E_TOOBIG;
    rc = journal_newest(c, &seq);
    if (rc) return rc;
    cartulary_row_t r = {
        cls, rec ? rec : next_record(c), version, today, size, content, 1, 0, seq, prev, {0}};
    memcpy(r.path, path, len_of(path, CARTULARY_PATH_MAX) + 1u);
    if (!row_ok(&r) || !cartulary_node_ok(r.cls, r.record, r.version, r.filed, 0) ||
        !cartulary_entry_ok(r.cls, r.record, r.size, mtime))
        return ZXM_E_ARG;
    c->row[c->n++] = r;
    rc = write_index(c);
    if (rc) {
        c->n--;
        return rc;
    }
    *record = r.record;
    return ZXM_OK;
}

/* ---- CM4: two sources ---- */

int cartulary_arrive(cartulary_t *c, int64_t record, int64_t version, int64_t content,
                     int64_t today)
{
    int32_t last = -1;
    if (!c->loaded) return ZXM_E_STATE;
    int64_t top = newest(c, record, &last);
    if (!top || version < 1 || version > top + 1 || content < 1 ||
        !witness_digits_fit(content, 18) || today < 0 || today > CARTULARY_DAY_MAX ||
        cartulary_find(c, record, version, 2) >= 0)
        return ZXM_E_ARG;
    if (c->n >= CARTULARY_ROWS) return ZXM_E_TOOBIG;
    int32_t same = cartulary_find(c, record, version, 1);
    cartulary_row_t r = c->row[last];
    r.version = version;
    r.filed = today;
    r.size = same >= 0 ? c->row[same].size : 0;
    r.content = content;
    r.source = 2;
    r.ruled = 0;
    r.seq = r.prev = 0; /* not filed through this journal */
    if (!row_ok(&r)) return ZXM_E_ARG;
    c->row[c->n++] = r;
    int rc = write_index(c);
    if (rc) c->n--;
    return rc;
}

lpres_state_t cartulary_state(const cartulary_t *c, int64_t record, int64_t version)
{
    int32_t a = cartulary_find(c, record, version, 1), b = cartulary_find(c, record, version, 2);
    int64_t ruling = 0;
    if (a >= 0 && c->row[a].ruled == 1) ruling = 1;
    if (b >= 0 && c->row[b].ruled == 1) ruling = 2;
    return cartulary_version_state(a >= 0 ? c->row[a].content : 0, b >= 0 ? c->row[b].content : 0,
                                   ruling);
}

int cartulary_rule(cartulary_t *c, int64_t record, int64_t version, int64_t ruling, bool confirmed)
{
    if (!c->loaded || !confirmed) return ZXM_E_STATE; /* the owner's word */
    int32_t a = cartulary_find(c, record, version, 1), b = cartulary_find(c, record, version, 2);
    if (a < 0 || b < 0 || (ruling != 1 && ruling != 2)) return ZXM_E_ARG;
    int64_t was_a = c->row[a].ruled, was_b = c->row[b].ruled;
    c->row[a].ruled = ruling == 1 ? 1 : 2;
    c->row[b].ruled = ruling == 2 ? 1 : 2;
    int rc = write_index(c);
    if (rc) c->row[a].ruled = was_a, c->row[b].ruled = was_b;
    return rc;
}

int cartulary_hold(cartulary_t *c, int64_t record, int64_t on, bool confirmed)
{
    if (!c->loaded || !confirmed) return ZXM_E_STATE; /* the owner's word */
    if (!newest(c, record, 0) || !cartulary_retention_ok(CARTULARY_KEEP_MIN, on)) return ZXM_E_ARG;
    bool was = cartulary_held(c, record);
    if (was == (on == 1)) return ZXM_OK;
    if (on) {
        if (c->nheld >= CARTULARY_HELD) return ZXM_E_TOOBIG;
        c->held[c->nheld++] = record;
    } else {
        uint32_t k = 0;
        for (uint32_t i = 0; i < c->nheld; i++)
            if (c->held[i] != record) c->held[k++] = c->held[i];
        c->nheld = k;
    }
    int rc = write_holds(c);
    if (rc) { /* put the holds back as they were */
        if (on)
            c->nheld--;
        else
            c->held[c->nheld++] = record;
    }
    return rc;
}

/* ---- CM2, CM5 ---- */

int cartulary_days_left(const cartulary_t *c, uint32_t i, int64_t today, int64_t *left)
{
    int64_t years, months;
    if (i >= c->n || !left) return ZXM_E_ARG;
    const cartulary_row_t *r = &c->row[i];
    if (!cartulary_schedule(r->cls, &years, &months) ||
        !cartulary_left(r->filed, years, months, today, left))
        return ZXM_E_ARG;
    return ZXM_OK;
}

lpres_state_t cartulary_prunable(const cartulary_t *c, uint32_t i, int64_t today)
{
    int64_t left = 0;
    if (cartulary_days_left(c, i, today, &left)) return LPRES_STATE_NEITHER;
    const cartulary_row_t *r = &c->row[i];
    int64_t newer = newest(c, r->record, 0) - r->version; /* the versions that stay */
    int64_t keepok = cartulary_keep_ok(newer) ? 1 : 0;
    bool rule = cartulary_may_go(left, cartulary_held(c, r->record) ? 1 : 0, keepok);
    return lpres_conjoin(cartulary_state(c, r->record, r->version),
                         rule ? LPRES_STATE_TRUE : LPRES_STATE_FALSE);
}

uint32_t cartulary_due(const cartulary_t *c, int64_t today)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < c->n; i++) k += cartulary_prunable(c, i, today) == LPRES_STATE_TRUE;
    return k;
}

int cartulary_prune_advice(cartulary_t *c, int64_t keep, int64_t *removed)
{
    uint64_t total = 0;
    if (!c->dest[0]) return ZXM_E_STATE;
    if (!removed || !cartulary_keep_ok(keep)) return ZXM_E_ARG; /* never all */
    zxm_link_t *l = c->link;
    zxm_begin(l, "backup.list");
    zxm_p_str(l, "dest", c->dest);
    zxm_p_u64(l, "max", 1);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "total", &total) || total > (uint64_t) CARTULARY_KEPT_MAX)
        return ZXM_E_PROTOCOL;
    *removed = cartulary_removed((int64_t) total, keep);
    return ZXM_OK;
}

/* ---- CM6: finding ---- */

int cartulary_search(cartulary_t *c, const char *query)
{
    uint32_t n = len_of(query, CARTULARY_QUERY + 1u);
    bool word = false;
    for (uint32_t i = 0; query && i < n; i++) word |= query[i] != ' ';
    if (!query || !word || n > CARTULARY_QUERY) return ZXM_E_ARG;
    if (!c->loaded) return ZXM_E_STATE;
    zxm_link_t *l = c->link;
    zxm_begin(l, "search.query");
    zxm_p_str(l, "q", query);
    zxm_p_u64(l, "max", CARTULARY_ASK);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "hits");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    c->nhits = 0;
    uint32_t count = zxm_len(l, arr);
    for (uint32_t i = 0; i < count && c->nhits < CARTULARY_HITS; i++) {
        char path[CARTULARY_PATH_MAX + 2];
        int64_t score = 0;
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || zxm_get_i64(l, o, "score", &score) || score < 0) return ZXM_E_PROTOCOL;
        if (zxm_get_str(l, o, "path", path, sizeof path)) continue; /* too long to be a record */
        int32_t row = -1;
        for (uint32_t k = 0; k < c->n && row < 0; k++)
            if (!strcmp(c->row[k].path, path)) row = (int32_t) k;
        if (row < 0) continue;
        uint32_t j = c->nhits++; /* best first, a tie in zxvd's order */
        while (j > 0u && c->score[j - 1u] < score) {
            c->hit[j] = c->hit[j - 1u];
            c->score[j] = c->score[j - 1u];
            j--;
        }
        c->hit[j] = (uint32_t) row;
        c->score[j] = score;
    }
    return ZXM_OK;
}

/* ---- CM7: measures ---- */

/* banker.total of the sizes of class cls's versions filed by `until` */
static int bytes_until(const cartulary_t *c, int64_t cls, int64_t until, int64_t *bytes)
{
    static int64_t size[CARTULARY_ROWS];
    uint32_t k = 0;
    for (uint32_t i = 0; i < c->n; i++)
        if (c->row[i].cls == cls && c->row[i].filed <= until) size[k++] = c->row[i].size;
    return cartulary_total(size, k, bytes) ? ZXM_OK : ZXM_E_TOOBIG;
}

int cartulary_class_bytes(const cartulary_t *c, int64_t cls, int64_t *bytes)
{
    if (cls < 1 || cls > CARTULARY_CLASSES || !bytes) return ZXM_E_ARG;
    return bytes_until(c, cls, INT64_MAX, bytes);
}

int64_t cartulary_grid(const cartulary_t *c, uint32_t i)
{
    static int64_t cls[CARTULARY_ROWS], year[CARTULARY_ROWS];
    for (uint32_t k = 0; k < c->n; k++) {
        cls[k] = c->row[k].cls;
        year[k] = cartulary_year(c->row[k].filed);
    }
    return cartulary_cell(cls, year, c->n, i);
}

int cartulary_growth(const cartulary_t *c, int64_t cls, int64_t today, uint32_t months,
                     int64_t ahead, int64_t *projected)
{
    int64_t bytes[CARTULARY_MONTHS], y, m;
    if (cls < 1 || cls > CARTULARY_CLASSES || today < 0 || today > CARTULARY_DAY_MAX ||
        months < 2u || months > CARTULARY_MONTHS || !projected)
        return ZXM_E_ARG;
    civil(today, &y, &m);
    int64_t now = y * 12 + (m - 1); /* months since year 0 */
    for (uint32_t k = 0; k < months; k++) {
        int64_t at = now - (int64_t) (months - 1u - k) + 1; /* the month after month k */
        int64_t end = k + 1u == months ? today : from_civil(at / 12, at % 12 + 1) - 1;
        int rc = bytes_until(c, cls, end, &bytes[k]);
        if (rc) return rc;
    }
    return cartulary_projected(bytes, months, ahead, projected) ? ZXM_OK : ZXM_E_ARG;
}

/* ---- CM8: proof ---- */

static lpres_state_t journal_has(cartulary_t *c, int64_t seq, int64_t first, int64_t last)
{
    if (!seq || seq < first) return LPRES_STATE_NEITHER; /* never in it, or no longer kept */
    if (seq > last) return LPRES_STATE_FALSE;
    zxm_link_t *l = c->link;
    int64_t got = 0;
    zxm_begin(l, "journal.query");
    zxm_p_u64(l, "before", (uint64_t) seq + 1u);
    zxm_p_u64(l, "max", 1);
    if (zxm_call(l)) return LPRES_STATE_NEITHER;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || !zxm_len(l, arr)) return LPRES_STATE_FALSE;
    int32_t e = zxm_at(l, arr, 0);
    if (e < 0 || zxm_get_i64(l, e, "seq", &got)) return LPRES_STATE_NEITHER;
    return got == seq ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
}

lpres_state_t cartulary_prove(cartulary_t *c, int64_t record)
{
    int64_t top = newest(c, record, 0), last = 0, first_seq = 0, last_seq = 0;
    if (!top) return LPRES_STATE_NEITHER;
    /* its history: witness.chain_ok over its own versions, each witness.audit_ok */
    lpres_state_t chain = LPRES_STATE_TRUE;
    for (int64_t v = 1; v <= top; v++) {
        int32_t i = cartulary_find(c, record, v, 1);
        if (i < 0) continue; /* a version only another source gave */
        const cartulary_row_t *r = &c->row[i];
        if (!r->seq) {
            chain = lpres_conjoin(chain, LPRES_STATE_NEITHER);
            continue;
        }
        bool fits =
            witness_record_fits(r->seq, r->prev, CARTULARY_KIND, r->filed * CARTULARY_DAY_MS);
        chain =
            lpres_conjoin(chain, fits && witness_linked(r->seq, r->prev, last) ? LPRES_STATE_TRUE
                                                                               : LPRES_STATE_FALSE);
        last = r->seq;
    }
    /* the journal: its hash chain intact, and each version's seq in it */
    zxm_link_t *l = c->link;
    bool on = false, ok = false;
    zxm_begin(l, "journal.verify");
    if (zxm_call(l) || zxm_get_bool(l, l->result, "on", &on)) return LPRES_STATE_NEITHER;
    lpres_state_t journal = LPRES_STATE_NEITHER;
    if (on) {
        if (zxm_get_bool(l, l->result, "ok", &ok) ||
            zxm_get_i64(l, l->result, "first_seq", &first_seq) ||
            zxm_get_i64(l, l->result, "last_seq", &last_seq))
            return LPRES_STATE_NEITHER;
        journal = ok ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
        for (int64_t v = 1; ok && v <= top; v++) {
            int32_t i = cartulary_find(c, record, v, 1);
            if (i >= 0)
                journal =
                    lpres_conjoin(journal, journal_has(c, c->row[i].seq, first_seq, last_seq));
        }
    }
    lpres_state_t s = lpres_kjoin(chain, journal);
    say(c,
        s == LPRES_STATE_TRUE && journal == LPRES_STATE_NEITHER
            ? "its history holds; no journal to prove it against"
        : s == LPRES_STATE_TRUE  ? "proven against the journal"
        : s == LPRES_STATE_BOTH  ? "held: its history and the journal disagree"
        : s == LPRES_STATE_FALSE ? "not proven: the record or the journal is broken"
                                 : "nothing to prove it against",
        0);
    return s;
}
