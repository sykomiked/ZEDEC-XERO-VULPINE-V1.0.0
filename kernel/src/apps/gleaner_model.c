/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gleaner_model.c — Gleaner, datasets (see gleaner_model.h). A row with
 * personal data leaves the Hoard only with the owner's consent (GM5). */
#include <string.h>

#include "gleaner_model.h"

void gleaner_init(gleaner_t *g, zxm_link_t *link)
{
    memset(g, 0, sizeof *g);
    g->link = link;
    g->share = -1;
}

/* ---- reading a dataset (GM1) ------------------------------------------------- */

static bool same(const char *s, uint32_t n, const char *word)
{
    return strlen(word) == n && !memcmp(s, word, n);
}

/* A field holds no '"' and no control byte. */
static bool plain(const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (s[i] == '"' || (unsigned char) s[i] < 0x20u || s[i] == 0x7f) return false;
    return true;
}

/* The index of s[0..n) in a table of names, added when it is new; -1 when
 * it is not a name, -2 when the table is full. */
static int64_t intern(char tab[][GLEANER_NAME + 1], uint32_t *count, const char *s, uint32_t n)
{
    if (!n || n > GLEANER_NAME) return -1;
    for (uint32_t i = 0; i < *count; i++)
        if (same(s, n, tab[i])) return i;
    if (*count >= GLEANER_CLASSES) return -2;
    memcpy(tab[*count], s, n);
    tab[*count][n] = 0;
    return (*count)++;
}

/* YYYYMMDD, month 1..12, day 1..31; -1 when it is not. */
static int64_t parse_date(const char *s, uint32_t n)
{
    int64_t v = 0;
    if (n != 8u) return -1;
    for (uint32_t i = 0; i < 8u; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10 + (s[i] - '0');
    }
    int64_t month = v / 100 % 100, day = v % 100;
    return month >= 1 && month <= 12 && day >= 1 && day <= 31 ? v : -1;
}

/* Where each line ends: *end its last byte + 1 (a CR before the LF
 * dropped), *next the first byte of the line after it. */
static void line_at(const char *t, uint32_t n, uint32_t at, uint32_t *end, uint32_t *next)
{
    uint32_t e = at;
    while (e < n && t[e] != '\n') e++;
    *next = e < n ? e + 1u : e;
    if (e > at && t[e - 1u] == '\r') e--;
    *end = e;
}

/* The fields of t[at..end), as offsets and lengths; how many (at most
 * cap + 1, so a line with too many fields is seen). */
static uint32_t fields(const char *t, uint32_t at, uint32_t end, uint32_t *off, uint32_t *len,
                       uint32_t cap)
{
    uint32_t k = 0, s = at;
    for (uint32_t i = at;; i++) {
        if (i == end || t[i] == ',') {
            if (k < cap) {
                off[k] = s;
                len[k] = i - s;
            }
            k++;
            if (i == end || k > cap) break;
            s = i + 1u;
        }
    }
    return k;
}

static void recount(gleaner_t *g)
{
    static int64_t kept[GLEANER_ROWS], test[GLEANER_ROWS];
    uint32_t nk = 0, nt = 0, personal = 0;
    for (uint32_t i = 0; i < g->nrows; i++) {
        const gleaner_row_t *r = &g->row[i];
        if (!r->kept) continue;
        kept[nk++] = r->label;
        if (r->test) test[nt++] = r->label;
        personal += r->pii;
    }
    memset(g->count, 0, sizeof g->count);
    memset(g->count_test, 0, sizeof g->count_test);
    for (uint32_t c = 0; c < g->nlabels; c++) { /* witness.kind_count (WR2) */
        g->count[c] = witness_kind_count(kept, nk, c);
        g->count_test[c] = witness_kind_count(test, nt, c);
    }
    g->nkept = nk;
    g->nkept_test = nt;
    g->personal = personal;
}

/* The selection changed: whatever was said about the old one goes (GM5). */
static void forget_leaving(gleaner_t *g)
{
    g->consent = false;
    g->cid[0] = 0;
    g->block[0] = 0;
    g->sealed = false;
}

static int refuse(gleaner_t *g, int32_t fault, uint32_t line)
{
    g->fault = fault;
    g->fault_line = line;
    return ZXM_E_ARG;
}

int gleaner_parse(gleaner_t *g, const char *path, const char *text, uint32_t n)
{
    static gleaner_t nx;
    uint32_t off[GLEANER_COLS], len[GLEANER_COLS], end, next, line = 1;
    int32_t col[4] = {-1, -1, -1, -1}; /* label, source, date, pii */
    static const char *const NAMES[4] = {"label", "source", "date", "pii"};
    if (!path || strlen(path) > GLEANER_PATH_MAX || (!text && n)) return ZXM_E_ARG;
    if (!n || n > GLEANER_BYTES) return refuse(g, GLEANER_F_SIZE, 0);
    nx = *g;
    nx.nrows = nx.nlabels = nx.nsources = 0;
    /* the header */
    line_at(text, n, 0, &end, &next);
    uint32_t k = fields(text, 0, end, off, len, GLEANER_COLS);
    if (k > GLEANER_COLS) return refuse(g, GLEANER_F_SIZE, line);
    for (uint32_t i = 0; i < k; i++) {
        if (!len[i] || !plain(text + off[i], len[i])) return refuse(g, GLEANER_F_HEADER, line);
        for (uint32_t j = 0; j < i; j++)
            if (len[j] == len[i] && !memcmp(text + off[j], text + off[i], len[i]))
                return refuse(g, GLEANER_F_HEADER, line);
        for (uint32_t c = 0; c < 4u; c++)
            if (same(text + off[i], len[i], NAMES[c])) col[c] = (int32_t) i;
    }
    if (col[0] < 0 || col[1] < 0 || col[2] < 0) return refuse(g, GLEANER_F_HEADER, line);
    nx.cols = k;
    nx.head_len = (uint16_t) end;
    nx.has_pii = col[3] >= 0;
    /* the rows */
    for (uint32_t at = next; at < n; at = next) {
        line++;
        line_at(text, n, at, &end, &next);
        if (nx.nrows >= GLEANER_ROWS) return refuse(g, GLEANER_F_SIZE, line);
        if (fields(text, at, end, off, len, GLEANER_COLS) != k)
            return refuse(g, GLEANER_F_RAGGED, line);
        for (uint32_t i = 0; i < k; i++)
            if (!plain(text + off[i], len[i])) return refuse(g, GLEANER_F_FIELD, line);
        gleaner_row_t *r = &nx.row[nx.nrows];
        memset(r, 0, sizeof *r);
        r->label = intern(nx.label, &nx.nlabels, text + off[col[0]], len[col[0]]);
        r->source = intern(nx.source, &nx.nsources, text + off[col[1]], len[col[1]]);
        if (r->label == -2 || r->source == -2) return refuse(g, GLEANER_F_CLASSES, line);
        if (r->label < 0 || r->source < 0) return refuse(g, GLEANER_F_FIELD, line);
        if ((r->date = parse_date(text + off[col[2]], len[col[2]])) < 0)
            return refuse(g, GLEANER_F_DATE, line);
        r->pii = true; /* GM2: personal unless the row says it is not */
        if (col[3] >= 0) {
            const char *p = text + off[col[3]];
            if (len[col[3]] != 1u || (p[0] != '0' && p[0] != '1'))
                return refuse(g, GLEANER_F_PII, line);
            r->pii = p[0] == '1';
        }
        if (!gleaner_row_fits(nx.nrows, r->label, r->source, r->date, r->pii)) /* GR4 */
            return refuse(g, GLEANER_F_FIELD, line);
        r->kept = true;
        r->off = (uint16_t) at;
        r->len = (uint16_t) (end - at);
        nx.nrows++;
    }
    if (!gleaner_dataset_ok(nx.nrows, nx.cols, n)) return refuse(g, GLEANER_F_SIZE, line); /* GR3 */
    memcpy(nx.text, text, n);
    nx.text[n] = 0;
    nx.len = n;
    memcpy(nx.path, path, strlen(path) + 1u);
    nx.fault = GLEANER_PARSED;
    nx.fault_line = 0;
    nx.share = -1;
    forget_leaving(&nx);
    recount(&nx);
    *g = nx;
    return ZXM_OK;
}

/* ---- finding datasets: files.list, search.query ------------------------------ */

static bool is_csv(const char *name)
{
    size_t n = strlen(name);
    if (n < 5u) return false;
    const char *e = name + n - 4u;
    return e[0] == '.' && (e[1] | 0x20) == 'c' && (e[2] | 0x20) == 's' && (e[3] | 0x20) == 'v';
}

int gleaner_list(gleaner_t *g, const char *folder)
{
    static gleaner_file_t next[GLEANER_LIST];
    char name[GLEANER_PATH_MAX + 1];
    zxm_link_t *l = g->link;
    size_t fl = folder ? strlen(folder) : 0u;
    if (!folder || fl >= GLEANER_PATH_MAX) return ZXM_E_ARG;
    zxm_begin(l, "files.list");
    zxm_p_str(l, "path", folder);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = 0, total = zxm_len(l, arr);
    for (uint32_t i = 0; i < total && n < GLEANER_LIST; i++) {
        int32_t e = zxm_at(l, arr, i);
        bool dir = false;
        gleaner_file_t f;
        memset(&f, 0, sizeof f);
        if (zxm_get_bool(l, e, "dir", &dir)) return ZXM_E_PROTOCOL;
        if (dir) continue;
        rc = zxm_get_str(l, e, "name", name, sizeof name);
        if (rc == ZXM_E_TOOBIG) continue; /* a name too long to keep */
        if (rc || zxm_get_u64(l, e, "size", &f.size) || zxm_get_i64(l, e, "mtime", &f.mtime))
            return ZXM_E_PROTOCOL;
        size_t nl = strlen(name);
        if (!is_csv(name) || fl + (fl ? 1u : 0u) + nl > GLEANER_PATH_MAX) continue;
        memcpy(f.path, folder, fl);
        if (fl) f.path[fl] = '/';
        memcpy(f.path + fl + (fl ? 1u : 0u), name, nl + 1u);
        next[n++] = f;
    }
    memcpy(g->file, next, (size_t) n * sizeof next[0]);
    g->nfiles = n;
    return ZXM_OK;
}

int gleaner_find(gleaner_t *g, const char *words)
{
    static gleaner_file_t next[GLEANER_LIST];
    zxm_link_t *l = g->link;
    size_t wl = words ? strlen(words) : 0u;
    bool blank = true;
    for (size_t i = 0; i < wl; i++) blank = blank && (words[i] == ' ' || words[i] == '\t');
    if (blank || wl > 256u) return ZXM_E_ARG;
    zxm_begin(l, "search.query");
    zxm_p_str(l, "q", words);
    zxm_p_u64(l, "max", GLEANER_HITS);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "hits");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = 0, total = zxm_len(l, arr);
    for (uint32_t i = 0; i < total && n < GLEANER_LIST; i++) {
        int32_t e = zxm_at(l, arr, i);
        gleaner_file_t f;
        memset(&f, 0, sizeof f);
        rc = zxm_get_str(l, e, "path", f.path, sizeof f.path);
        if (rc == ZXM_E_TOOBIG) continue;
        if (rc || zxm_get_u64(l, e, "size", &f.size) || zxm_get_i64(l, e, "mtime", &f.mtime))
            return ZXM_E_PROTOCOL;
        if (is_csv(f.path)) next[n++] = f;
    }
    memcpy(g->file, next, (size_t) n * sizeof next[0]);
    g->nfiles = n;
    return ZXM_OK;
}

int gleaner_open(gleaner_t *g, const char *path)
{
    static char buf[GLEANER_BYTES + 1];
    zxm_link_t *l = g->link;
    uint64_t size = 0;
    if (!path || !path[0] || strlen(path) > GLEANER_PATH_MAX) return ZXM_E_ARG;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "max", GLEANER_BYTES);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "size", &size)) return ZXM_E_PROTOCOL;
    if (size > GLEANER_BYTES) {
        g->fault = GLEANER_F_SIZE;
        g->fault_line = 0;
        return ZXM_E_TOOBIG;
    }
    if (zxm_get_str(l, l->result, "text", buf, sizeof buf)) return ZXM_E_PROTOCOL;
    if (strlen(buf) != size) return refuse(g, GLEANER_F_FIELD, 0); /* a NUL byte */
    return gleaner_parse(g, path, buf, (uint32_t) size);
}

/* ---- the split, the filter and the selection (GM3, GM4) ---------------------- */

int gleaner_split(gleaner_t *g, int64_t share)
{
    if (!g->nrows) return ZXM_E_STATE;
    if (share < 0 || share > GLEANER_SHARE_MAX) return ZXM_E_ARG;
    for (uint32_t i = 0; i < g->nrows; i++) g->row[i].test = gleaner_holdout(i, share) == 1;
    g->share = share;
    recount(g);
    return ZXM_OK;
}

int gleaner_filter(gleaner_t *g, int64_t lab, int64_t src, int64_t from, int64_t to)
{
    if (!g->nrows) return ZXM_E_STATE;
    for (uint32_t i = 0; i < g->nrows; i++) {
        gleaner_row_t *r = &g->row[i];
        r->kept = gleaner_row_match(r->label, r->source, r->date, lab, src, from, to);
    }
    forget_leaving(g);
    recount(g);
    return ZXM_OK;
}

/* The index of a label (labels true) or a source by its name, or -1. */
static int64_t index_of(const gleaner_t *g, bool labels, const char *name)
{
    uint32_t count = labels ? g->nlabels : g->nsources;
    for (uint32_t i = 0; name && i < count; i++)
        if (!strcmp(labels ? g->label[i] : g->source[i], name)) return i;
    return -1;
}

int64_t gleaner_label_of(const gleaner_t *g, const char *name)
{
    return index_of(g, true, name);
}

int64_t gleaner_source_of(const gleaner_t *g, const char *name)
{
    return index_of(g, false, name);
}

/* The header and the kept rows whose test mark is `test` (any when test is
 * -1), each line ending in LF; its length, or -1 when it does not fit. */
static int32_t gather(const gleaner_t *g, int32_t test, char *out, uint32_t cap)
{
    uint32_t o = 0;
    if (!g->nrows || !out) return -1;
    for (int32_t i = -1; i < (int32_t) g->nrows; i++) {
        uint32_t at = 0, n = g->head_len;
        if (i >= 0) {
            const gleaner_row_t *r = &g->row[i];
            if (!r->kept || (test >= 0 && r->test != (test == 1))) continue;
            at = r->off;
            n = r->len;
        }
        if (o + n + 1u > cap) return -1;
        memcpy(out + o, g->text + at, n);
        o += n;
        out[o++] = '\n';
    }
    return (int32_t) o;
}

int32_t gleaner_selection(const gleaner_t *g, char *out, uint32_t cap)
{
    return gather(g, -1, out, cap);
}

/* ---- writing the sets: files.write (GM4) ------------------------------------- */

/* Bytes of text in one request: escaped (at most 2 bytes each, GM1 has no
 * control byte but LF) they stay well inside ZXM_REQ_MAX. */
#define PIECE 4000u

static int write_text(zxm_link_t *l, const char *path, const char *s, uint32_t len)
{
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at < PIECE ? len - at : zxm_utf8_cut(s + at, PIECE);
        bool last = at + n >= len;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", path);
        zxm_p_strn(l, "text", s + at, n);
        zxm_p_bool(l, "exclusive", true); /* never over a file that is there */
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
        zxm_progress(l, at, len);
    } while (at < len);
    return ZXM_OK;
}

int gleaner_save(gleaner_t *g, const char *train_path, const char *test_path)
{
    static char train[GLEANER_BYTES + 2], test[GLEANER_BYTES + 2];
    if (!g->nrows || g->share < 0) return ZXM_E_STATE;
    if (!train_path || !test_path || !train_path[0] || !test_path[0] ||
        strlen(train_path) > GLEANER_PATH_MAX || strlen(test_path) > GLEANER_PATH_MAX ||
        !strcmp(train_path, test_path))
        return ZXM_E_ARG;
    int32_t a = gather(g, 0, train, sizeof train), b = gather(g, 1, test, sizeof test);
    if (a < 0 || b < 0) return ZXM_E_TOOBIG;
    int rc = write_text(g->link, train_path, train, (uint32_t) a);
    return rc ? rc : write_text(g->link, test_path, test, (uint32_t) b);
}

/* ---- naming and leaving: web4.cid, aipi.put (GM5) ---------------------------- */

/* The selection as hex into the link's own request (one parameter). */
static int put_hex(gleaner_t *g, const char *key)
{
    static const char HEX[] = "0123456789abcdef";
    static char sel[GLEANER_BYTES + 2], hex[2u * GLEANER_BYTES + 1u];
    int32_t n = gleaner_selection(g, sel, sizeof sel);
    if (n < 0 || n > (int32_t) GLEANER_BYTES) return ZXM_E_TOOBIG;
    for (int32_t i = 0; i < n; i++) {
        hex[2 * i] = HEX[(unsigned char) sel[i] >> 4];
        hex[2 * i + 1] = HEX[(unsigned char) sel[i] & 15u];
    }
    zxm_p_strn(g->link, key, hex, 2u * (uint32_t) n);
    return n;
}

int gleaner_name(gleaner_t *g)
{
    static char cid[GLEANER_CID_MAX + 1];
    zxm_link_t *l = g->link;
    if (!g->nrows) return ZXM_E_STATE;
    zxm_begin(l, "web4.cid");
    int n = put_hex(g, "data_hex");
    if (n < 0) return n;
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "cid", cid, sizeof cid) || !cid[0]) return ZXM_E_PROTOCOL;
    memcpy(g->cid, cid, strlen(cid) + 1u);
    return ZXM_OK;
}

void gleaner_consent(gleaner_t *g, bool yes)
{
    g->consent = yes && g->nrows > 0;
}

int gleaner_publish(gleaner_t *g)
{
    static char cid[GLEANER_CID_MAX + 1];
    zxm_link_t *l = g->link;
    uint64_t bytes = 0;
    bool sealed = false;
    if (!g->nrows) return ZXM_E_STATE;
    if (!gleaner_leave_ok(g->personal, g->consent)) return ZXM_E_STATE; /* GR5: nothing sent */
    zxm_begin(l, "aipi.put");
    int n = put_hex(g, "hex");
    if (n < 0) return n;
    zxm_p_bool(l, "private", g->personal > 0);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "cid", cid, sizeof cid) || cid[0] != 'z' ||
        zxm_get_u64(l, l->result, "bytes", &bytes) || bytes != (uint64_t) n ||
        zxm_get_bool(l, l->result, "private", &sealed) || sealed != (g->personal > 0))
        return ZXM_E_PROTOCOL;
    memcpy(g->block, cid, strlen(cid) + 1u);
    g->sealed = sealed;
    return ZXM_OK;
}
