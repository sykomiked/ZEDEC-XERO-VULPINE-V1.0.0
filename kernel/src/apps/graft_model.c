/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* graft_model.c — Graft's model (graft_model.h). */
#include <string.h>

#include "graft_model.h"

#define GRAFT_E_FILES (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define GRAFT_FIELDS  6u       /* integer fields of a history line (GM1) */
#define GRAFT_FNV0    UINT64_C(14695981039346656037)
#define GRAFT_FNVP    UINT64_C(1099511628211)

/* ---- small text helpers, bounded ---- */

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (s && n < cap && s[n]) n++;
    return n;
}

static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = len_of(s, cap);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
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

static void say(graft_t *g, const char *a, const char *b)
{
    uint32_t at = 0;
    g->notice[0] = 0;
    if (put(g->notice, sizeof g->notice, &at, a) && b) put(g->notice, sizeof g->notice, &at, b);
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

/* A Hoard path Graft keeps: 1..cap bytes, no control byte or tab, no
 * leading '/' or '.', no "..". */
static bool path_ok(const char *p, uint32_t cap)
{
    uint32_t n = len_of(p, cap + 1u);
    if (!p || !n || n > cap || p[0] == '/' || p[0] == '.' || strstr(p, "..")) return false;
    for (uint32_t i = 0; i < n; i++)
        if ((unsigned char) p[i] < 0x20u || p[i] == 0x7F) return false;
    return true;
}

/* A path inside the open folder. */
static bool inside(const graft_t *g, const char *p)
{
    uint32_t f = len_of(g->folder, GRAFT_PATH + 1u);
    return path_ok(p, GRAFT_PATH) && !strncmp(p, g->folder, f) && p[f] == '/' && p[f + 1u];
}

static bool text_ok(const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < 0x20u || s[i] == 0x7F) return false;
    return true;
}

/* ---- zxvd ---- */

static bool refused(const zxm_link_t *l, int rc, const char *why)
{
    return rc == ZXM_E_REMOTE && l->err_code == GRAFT_E_FILES && strstr(l->err, why);
}

static void begin(graft_t *g, const char *method)
{
    zxm_begin(g->link, method);
    zxm_p_space(g->link, g->space);
}

/* files.read of a whole small text file into g->hist; its length,
 * ZXM_E_STATE when it does not exist, ZXM_E_TOOBIG when it does not fit. */
static int32_t read_small(graft_t *g, const char *path)
{
    zxm_link_t *l = g->link;
    uint64_t off = 0, size = 0, next = 0;
    bool eof = false;
    uint32_t at = 0;
    g->hist[0] = 0;
    do {
        begin(g, "files.read");
        zxm_p_str(l, "path", path);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (refused(l, rc, "no such")) return ZXM_E_STATE;
        if (rc) return rc;
        int32_t t = zxm_get(l, l->result, "text");
        if (t < 0 || zxm_get_bool(l, l->result, "eof", &eof) ||
            zxm_get_u64(l, l->result, "next", &next) || zxm_get_u64(l, l->result, "size", &size))
            return ZXM_E_PROTOCOL;
        if (size > GRAFT_HIST_MAX) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[t], g->hist + at, GRAFT_HIST_MAX + 1u - at);
        if (n < 0) return ZXM_E_TOOBIG;
        if (next != off + (uint64_t) n || (!eof && !n)) return ZXM_E_PROTOCOL;
        at += (uint32_t) n;
        off = next;
    } while (!eof);
    return (int32_t) at;
}

/* ---- the output of a write, in pieces (files.write begin, add, end) ---- */

static int out_flush(graft_t *g, const char *path, bool last)
{
    zxm_link_t *l = g->link;
    begin(g, "files.write");
    zxm_p_str(l, "path", path);
    zxm_p_strn(l, "text", g->out, g->out_len);
    zxm_p_bool(l, "exclusive", true);
    if (!(last && !g->out_begun))
        zxm_p_str(l, "mode", !g->out_begun ? "begin" : last ? "end" : "add");
    int rc = zxm_call(l);
    if (rc) return rc;
    g->out_begun = true;
    g->out_len = 0;
    return ZXM_OK;
}

/* Text cut on character boundaries by its source (files.read), so a
 * flush between two pieces never splits a character. */
static int out_put(graft_t *g, const char *path, const char *s, uint32_t n)
{
    if (g->out_nl) {
        g->out_nl = false;
        int rc = out_put(g, path, "\n", 1u);
        if (rc) return rc;
    }
    if (n > GRAFT_PIECE) return ZXM_E_TOOBIG;
    if (g->out_len + n > GRAFT_PIECE) {
        int rc = out_flush(g, path, false);
        if (rc) return rc;
    }
    memcpy(g->out + g->out_len, s, n);
    g->out_len += n;
    return ZXM_OK;
}

/* Copy lines [a, b) of file `src` (read as t) to the output. */
static int out_lines(graft_t *g, const char *out, const char *src, const graft_text_t *t,
                     uint32_t a, uint32_t b)
{
    if (a >= b) return ZXM_OK;
    zxm_link_t *l = g->link;
    uint64_t off = t->line[a].off, stop = t->line[b - 1u].end;
    while (off < stop) {
        uint64_t want = stop - off < GRAFT_PIECE ? stop - off : GRAFT_PIECE;
        uint64_t next = 0, size = 0;
        begin(g, "files.read");
        zxm_p_str(l, "path", src);
        zxm_p_u64(l, "offset", off);
        zxm_p_u64(l, "max", want);
        int rc = zxm_call(l);
        if (rc) return rc;
        int32_t s = zxm_get(l, l->result, "text");
        if (s < 0 || zxm_get_u64(l, l->result, "next", &next) ||
            zxm_get_u64(l, l->result, "size", &size))
            return ZXM_E_PROTOCOL;
        if (size != t->bytes) return ZXM_E_STATE; /* GM4: it changed */
        static char piece[GRAFT_PIECE + 1u];
        int32_t n = w4_json_str(l->resp, &l->tok[s], piece, sizeof piece);
        if (n <= 0 || next != off + (uint64_t) n || next > stop) return ZXM_E_PROTOCOL;
        if ((rc = out_put(g, out, piece, (uint32_t) n)) != 0) return rc;
        off = next;
    }
    const graft_line_t *last = &t->line[b - 1u];
    g->out_nl = last->end - last->off == last->len; /* the file's last line, no newline */
    return ZXM_OK;
}

/* ---- GM4: a file's lines ---- */

static void add_line(graft_text_t *t, uint64_t hash, uint32_t off, uint32_t end, uint32_t len)
{
    graft_line_t *ln = &t->line[t->n++];
    ln->hash = hash;
    ln->off = off;
    ln->end = end;
    ln->len = len;
}

static int read_lines(graft_t *g, const char *path, graft_text_t *t)
{
    zxm_link_t *l = g->link;
    bool dir = false;
    uint64_t size = 0;
    t->n = 0;
    t->bytes = 0;
    begin(g, "files.stat");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "dir", &dir) || zxm_get_u64(l, l->result, "size", &size))
        return ZXM_E_PROTOCOL;
    if (dir) return ZXM_E_ARG;
    if (size > (uint64_t) GRAFT_BYTES_MAX || !graft_diff_fits((int64_t) size, 0)) {
        say(g, "too large to compare (16 MiB at most): ", path);
        return ZXM_E_TOOBIG;
    }
    uint64_t off = 0, h = GRAFT_FNV0;
    uint32_t start = 0, len = 0;
    while (off < size) {
        uint64_t next = 0, got_size = 0;
        bool eof = false;
        begin(g, "files.read");
        zxm_p_str(l, "path", path);
        zxm_p_u64(l, "offset", off);
        if ((rc = zxm_call(l)) != 0) return rc;
        int32_t s = zxm_get(l, l->result, "text");
        if (s < 0 || zxm_get_u64(l, l->result, "next", &next) ||
            zxm_get_u64(l, l->result, "size", &got_size) || zxm_get_bool(l, l->result, "eof", &eof))
            return ZXM_E_PROTOCOL;
        if (got_size != size) return ZXM_E_STATE; /* it changed while read */
        int32_t n = w4_json_str(l->resp, &l->tok[s], g->io, sizeof g->io);
        if (n <= 0 || next != off + (uint64_t) n || next > size) return ZXM_E_PROTOCOL;
        for (int32_t i = 0; i < n; i++) {
            uint32_t at = (uint32_t) off + (uint32_t) i;
            if (g->io[i] == '\n') {
                if (!graft_diff_fits((int64_t) size, (int64_t) t->n + 1)) {
                    say(g, "too many lines to compare (65536 at most): ", path);
                    return ZXM_E_TOOBIG;
                }
                add_line(t, h, start, at + 1u, len);
                start = at + 1u;
                len = 0;
                h = GRAFT_FNV0;
                continue;
            }
            h = (h ^ (uint8_t) g->io[i]) * GRAFT_FNVP;
            len++;
        }
        off = next;
        if (eof && off < size) return ZXM_E_STATE;
    }
    if (start < size) {
        if (!graft_diff_fits((int64_t) size, (int64_t) t->n + 1)) {
            say(g, "too many lines to compare (65536 at most): ", path);
            return ZXM_E_TOOBIG;
        }
        add_line(t, h, start, (uint32_t) size, len);
    }
    t->bytes = size;
    return ZXM_OK;
}

static bool same_line(const graft_line_t *a, const graft_line_t *b)
{
    return a->hash == b->hash && a->len == b->len;
}

/* GM5: align a with b; ma[i] is b's line for a's line i, mb[j] a's line
 * for b's line j (-1 none). The lines kept. */
static int64_t align(graft_t *g, const graft_text_t *a, const graft_text_t *b, int32_t *ma,
                     int32_t *mb)
{
    uint32_t na = a->n, nb = b->n, p = 0, q = 0;
    int64_t kept = 0;
    for (uint32_t i = 0; i < na; i++) ma[i] = -1;
    for (uint32_t j = 0; j < nb; j++) mb[j] = -1;
    while (p < na && p < nb && same_line(&a->line[p], &b->line[p])) {
        ma[p] = (int32_t) p, mb[p] = (int32_t) p;
        p++, kept++;
    }
    while (q < na - p && q < nb - p && same_line(&a->line[na - 1u - q], &b->line[nb - 1u - q])) {
        ma[na - 1u - q] = (int32_t) (nb - 1u - q), mb[nb - 1u - q] = (int32_t) (na - 1u - q);
        q++, kept++;
    }
    uint32_t n = na - p - q, m = nb - p - q;
    if ((uint64_t) (n + 1u) * (uint64_t) (m + 1u) > GRAFT_DP_CELLS) return kept;
    uint32_t w = m + 1u;
    uint16_t *dp = g->dp;
    for (uint32_t i = n + 1u; i-- > 0;)
        for (uint32_t j = m + 1u; j-- > 0;) {
            uint16_t v = 0;
            if (i < n && j < m) {
                if (same_line(&a->line[p + i], &b->line[p + j]))
                    v = (uint16_t) (dp[(i + 1u) * w + j + 1u] + 1u);
                else {
                    uint16_t down = dp[(i + 1u) * w + j], right = dp[i * w + j + 1u];
                    v = down >= right ? down : right;
                }
            }
            dp[i * w + j] = v;
        }
    for (uint32_t i = 0, j = 0; i < n && j < m;) {
        if (same_line(&a->line[p + i], &b->line[p + j]) &&
            dp[i * w + j] == dp[(i + 1u) * w + j + 1u] + 1u) {
            ma[p + i] = (int32_t) (p + j), mb[p + j] = (int32_t) (p + i);
            i++, j++, kept++;
        } else if (dp[(i + 1u) * w + j] >= dp[i * w + j + 1u])
            i++;
        else
            j++;
    }
    return kept;
}

/* GM6: each unit's lines of a side: from[u]..to[u]. False when the
 * alignment does not cover the side (never, for one align made). */
static bool units(uint32_t nbase, uint32_t nside, const int32_t *ma, const int32_t *mb,
                  uint32_t *from, uint32_t *to)
{
    uint32_t j = 0;
    for (uint32_t u = 0; u <= nbase; u++) {
        from[u] = j;
        if (u == 0 || ma[u - 1u] >= 0)
            while (j < nside && mb[j] < 0) j++;
        if (u < nbase && ma[u] >= 0) {
            if ((uint32_t) ma[u] != j) return false;
            j++;
        }
        to[u] = j;
    }
    return j == nside;
}

static bool unit_changed(const graft_t *g, uint32_t side, uint32_t u, uint32_t nbase)
{
    uint32_t a = g->from[side][u], b = g->to[side][u];
    if (u == nbase) return a != b;
    return !(b - a == 1u && g->match[side][u] >= 0);
}

static bool unit_apart(const graft_t *g, uint32_t u)
{
    uint32_t a0 = g->from[0][u], b0 = g->to[0][u], a1 = g->from[1][u], b1 = g->to[1][u];
    if (b0 - a0 != b1 - a1) return true;
    for (uint32_t k = 0; k < b0 - a0; k++)
        if (!same_line(&g->text[1].line[a0 + k], &g->text[2].line[a1 + k])) return true;
    return false;
}

/* ---- the model ---- */

void graft_init(graft_t *g, zxm_link_t *link)
{
    memset(g, 0, sizeof *g);
    g->link = link;
}

static void forget(graft_t *g)
{
    g->opened = false;
    g->folder[0] = 0;
    g->n = g->broken = g->nvers = g->restored = 0;
    g->vpath[0] = g->landed[0] = 0;
}

int graft_set_space(graft_t *g, const char *space)
{
    if (!space) space = "";
    if (len_of(space, ZXM_SPACE_MAX + 1u) > ZXM_SPACE_MAX) return ZXM_E_ARG;
    uint32_t at = 0;
    g->space[0] = 0;
    put(g->space, sizeof g->space, &at, strcmp(space, "home") ? space : "");
    forget(g);
    return ZXM_OK;
}

int graft_set_dest(graft_t *g, const char *dest)
{
    uint32_t n = len_of(dest, GRAFT_DEST + 1u);
    if (!dest || !n || n > GRAFT_DEST || dest[0] != '/') return ZXM_E_ARG;
    memcpy(g->dest, dest, n + 1u);
    return ZXM_OK;
}

static bool history_path(const graft_t *g, char *out, uint32_t cap)
{
    uint32_t at = 0;
    out[0] = 0;
    return put(out, cap, &at, g->folder) && put(out, cap, &at, "/") &&
           put(out, cap, &at, GRAFT_HISTORY);
}

static uint32_t chain(const graft_t *g)
{
    int64_t last = 0;
    for (uint32_t i = 0; i < g->n; i++) {
        if (!graft_linked(g->c[i].commit, g->c[i].parent, last)) return i;
        last = g->c[i].commit;
    }
    return g->n;
}

static bool commit_ok(const graft_commit_t *c)
{
    return graft_record_fits(c->commit, c->parent, c->author, c->stamp, (int64_t) c->msglen) &&
           c->commit >= 1 && c->parent >= 0 && c->author >= 1 && c->author <= GRAFT_AUTHOR &&
           c->stamp >= 0 && c->stamp <= GRAFT_STAMP && c->snapshot >= 1 && c->msglen >= 1 &&
           c->msglen <= GRAFT_MSG_MAX && len_of(c->msg, GRAFT_MSG_MAX + 1u) == c->msglen &&
           text_ok(c->msg, c->msglen);
}

static int parse_history(graft_t *g, uint32_t len)
{
    const char *s = g->hist;
    for (uint32_t at = 0; at < len;) {
        if (g->n >= GRAFT_COMMITS) return ZXM_E_PROTOCOL;
        graft_commit_t *c = &g->c[g->n];
        int64_t snap = 0, msglen = 0;
        if (!get_i64(s, len, &at, '\t', &c->commit) || !get_i64(s, len, &at, '\t', &c->parent) ||
            !get_i64(s, len, &at, '\t', &c->author) || !get_i64(s, len, &at, '\t', &c->stamp) ||
            !get_i64(s, len, &at, '\t', &snap) || !get_i64(s, len, &at, '\t', &msglen) ||
            snap < 1 || snap > UINT32_MAX || msglen < 1 || msglen > (int64_t) GRAFT_MSG_MAX)
            return ZXM_E_PROTOCOL;
        uint32_t k = 0;
        while (at < len && s[at] != '\n') {
            if (k >= GRAFT_MSG_MAX) return ZXM_E_PROTOCOL;
            c->msg[k++] = s[at++];
        }
        if (at >= len) return ZXM_E_PROTOCOL;
        at++;
        c->msg[k] = 0;
        c->snapshot = (uint32_t) snap;
        c->msglen = (uint32_t) msglen;
        if (!commit_ok(c)) return ZXM_E_PROTOCOL;
        g->n++;
    }
    return ZXM_OK;
}

int graft_open(graft_t *g, const char *folder)
{
    if (!path_ok(folder, GRAFT_PATH - (uint32_t) sizeof GRAFT_HISTORY - 1u)) return ZXM_E_ARG;
    zxm_link_t *l = g->link;
    forget(g);
    begin(g, "files.stat");
    zxm_p_str(l, "path", folder);
    int rc = zxm_call(l);
    if (rc) return rc;
    bool dir = false;
    if (zxm_get_bool(l, l->result, "dir", &dir)) return ZXM_E_PROTOCOL;
    if (!dir) return ZXM_E_ARG;
    uint32_t at = 0;
    put(g->folder, sizeof g->folder, &at, folder);
    char hp[GRAFT_PATH + 1];
    if (!history_path(g, hp, sizeof hp)) return ZXM_E_ARG;
    int32_t len = read_small(g, hp);
    if (len == ZXM_E_STATE) len = 0; /* no history yet */
    if (len < 0 || (rc = parse_history(g, (uint32_t) len)) != 0) {
        g->n = 0;
        g->folder[0] = 0;
        say(g, "the history could not be read: ", hp);
        return len < 0 ? (int) len : rc;
    }
    g->broken = chain(g);
    g->opened = true;
    if (g->broken < g->n)
        say(g, "the history is broken: held for the owner, no commit is added", NULL);
    else
        say(g, "the history holds", NULL);
    return ZXM_OK;
}

const graft_commit_t *graft_find(const graft_t *g, int64_t commit)
{
    for (uint32_t i = 0; i < g->n; i++)
        if (g->c[i].commit == commit) return &g->c[i];
    return NULL;
}

static int write_history(graft_t *g, uint32_t n)
{
    uint32_t at = 0;
    g->hist[0] = 0;
    for (uint32_t i = 0; i < n; i++) {
        const graft_commit_t *c = &g->c[i];
        const int64_t f[GRAFT_FIELDS] = {c->commit, c->parent,   c->author,
                                         c->stamp,  c->snapshot, c->msglen};
        for (uint32_t k = 0; k < GRAFT_FIELDS; k++)
            if (!put_i64(g->hist, sizeof g->hist, &at, f[k]) ||
                !put(g->hist, sizeof g->hist, &at, "\t"))
                return ZXM_E_TOOBIG;
        if (!put(g->hist, sizeof g->hist, &at, c->msg) || !put(g->hist, sizeof g->hist, &at, "\n"))
            return ZXM_E_TOOBIG;
    }
    char hp[GRAFT_PATH + 1];
    if (!history_path(g, hp, sizeof hp)) return ZXM_E_ARG;
    zxm_link_t *l = g->link;
    uint32_t done = 0;
    bool first = true;
    do {
        uint32_t k = at - done < GRAFT_PIECE ? at - done : GRAFT_PIECE;
        if (done + k < at) k = zxm_utf8_cut(g->hist + done, k);
        bool last = done + k >= at;
        begin(g, "files.write");
        zxm_p_str(l, "path", hp);
        zxm_p_strn(l, "text", g->hist + done, k);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        done += k;
        first = false;
    } while (done < at);
    return ZXM_OK;
}

int graft_commit(graft_t *g, int64_t author, int64_t stamp, const char *message, int64_t *id)
{
    if (!g->opened || !g->dest[0]) return ZXM_E_STATE;
    if (g->broken < g->n) {
        say(g, "the history is broken: no commit is added until the owner repairs it", NULL);
        return ZXM_E_STATE;
    }
    if (g->n >= GRAFT_COMMITS) return ZXM_E_TOOBIG;
    uint32_t mlen = len_of(message, GRAFT_MSG_MAX + 1u);
    if (!id || !message || mlen > GRAFT_MSG_MAX) return ZXM_E_ARG;
    int64_t last = g->n ? g->c[g->n - 1u].commit : 0;
    graft_commit_t c;
    memset(&c, 0, sizeof c);
    c.commit = last + 1, c.parent = last, c.author = author, c.stamp = stamp;
    c.msglen = mlen, c.snapshot = 1;
    memcpy(c.msg, message, mlen);
    if (!commit_ok(&c) || !graft_linked(c.commit, c.parent, last)) return ZXM_E_ARG;
    /* checked; now the snapshot (backup.run), then the record */
    zxm_link_t *l = g->link;
    begin(g, "backup.run");
    zxm_p_str(l, "dest", g->dest);
    int rc = zxm_call(l);
    if (rc) return rc;
    uint64_t snap = 0;
    if (zxm_get_u64(l, l->result, "snapshot", &snap) || snap < 1 || snap > UINT32_MAX)
        return ZXM_E_PROTOCOL;
    if (g->n && snap <= g->c[g->n - 1u].snapshot) return ZXM_E_PROTOCOL;
    c.snapshot = (uint32_t) snap;
    g->c[g->n] = c;
    if ((rc = write_history(g, g->n + 1u)) != 0) return rc;
    g->n++;
    g->broken = g->n;
    *id = c.commit;
    say(g, "committed: ", c.msg);
    return ZXM_OK;
}

int graft_versions(graft_t *g, const char *path)
{
    if (!g->opened || !g->dest[0]) return ZXM_E_STATE;
    if (!inside(g, path)) return ZXM_E_ARG;
    zxm_link_t *l = g->link;
    begin(g, "backup.versions");
    zxm_p_str(l, "dest", g->dest);
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t a = zxm_get(l, l->result, "versions");
    if (a < 0 || l->tok[a].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, a) < GRAFT_VERS ? zxm_len(l, a) : GRAFT_VERS;
    static graft_version_t got[GRAFT_VERS];
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, a, i);
        graft_version_t *v = &got[i];
        uint64_t snap = 0;
        if (o < 0 || l->tok[o].type != W4_J_OBJ || zxm_get_u64(l, o, "snapshot", &snap) ||
            snap < 1 || snap > UINT32_MAX || zxm_get_i64(l, o, "time", &v->time) ||
            zxm_get_u64(l, o, "size", &v->size) || zxm_get_i64(l, o, "mtime", &v->mtime) ||
            zxm_get_str(l, o, "hash", v->hash, sizeof v->hash) || len_of(v->hash, 65u) != 64u)
            return ZXM_E_PROTOCOL;
        v->snapshot = (uint32_t) snap;
        v->commit = 0;
        for (uint32_t k = 0; k < g->n; k++)
            if (g->c[k].snapshot == v->snapshot) v->commit = g->c[k].commit;
    }
    memcpy(g->ver, got, n * sizeof *got);
    g->nvers = n;
    uint32_t at = 0;
    put(g->vpath, sizeof g->vpath, &at, path);
    return ZXM_OK;
}

int graft_restore(graft_t *g, int64_t commit, const char *path, const char *to)
{
    if (!g->opened || !g->dest[0]) return ZXM_E_STATE;
    const graft_commit_t *c = graft_find(g, commit);
    if (!c || !inside(g, path) || (to && to[0] && !path_ok(to, GRAFT_PATH))) return ZXM_E_ARG;
    zxm_link_t *l = g->link;
    begin(g, "backup.restore");
    zxm_p_str(l, "dest", g->dest);
    zxm_p_u64(l, "snapshot", c->snapshot);
    zxm_p_str(l, "path", path);
    if (to && to[0]) zxm_p_str(l, "to", to);
    int rc = zxm_call(l);
    if (rc) return rc;
    static char landed[GRAFT_PATH + 1];
    uint64_t files = 0;
    if (zxm_get_str(l, l->result, "to", landed, sizeof landed) || !landed[0] ||
        zxm_get_u64(l, l->result, "files", &files) || !files || files > UINT32_MAX)
        return ZXM_E_PROTOCOL;
    uint32_t at = 0;
    put(g->landed, sizeof g->landed, &at, landed);
    g->restored = (uint32_t) files;
    say(g, "brought back as a new item: ", g->landed);
    return ZXM_OK;
}

int graft_snapshots(graft_t *g, lpres_state_t *out)
{
    if (!out) return ZXM_E_ARG;
    if (!g->opened || !g->dest[0]) return ZXM_E_STATE;
    zxm_link_t *l = g->link;
    begin(g, "backup.list");
    zxm_p_str(l, "dest", g->dest);
    zxm_p_u64(l, "max", 100u);
    int rc = zxm_call(l);
    if (rc) return rc;
    uint64_t total = 0, oldest = UINT64_MAX;
    int32_t a = zxm_get(l, l->result, "snapshots");
    if (zxm_get_u64(l, l->result, "total", &total) || a < 0 || l->tok[a].type != W4_J_ARR ||
        zxm_len(l, a) > total)
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, a);
    static uint64_t ids[100];
    if (n > 100u) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, a, i);
        if (o < 0 || zxm_get_u64(l, o, "snapshot", &ids[i])) return ZXM_E_PROTOCOL;
        if (ids[i] < oldest) oldest = ids[i];
    }
    lpres_state_t acc = g->n ? LPRES_STATE_TRUE : LPRES_STATE_NEITHER;
    for (uint32_t k = 0; k < g->n; k++) {
        lpres_state_t s = LPRES_STATE_FALSE;
        for (uint32_t i = 0; i < n; i++)
            if (ids[i] == g->c[k].snapshot) s = LPRES_STATE_TRUE;
        if (s == LPRES_STATE_FALSE && total > n && g->c[k].snapshot < oldest)
            s = LPRES_STATE_NEITHER; /* older than those listed: not seen */
        acc = lpres_conjoin(acc, s);
    }
    *out = acc;
    return ZXM_OK;
}

int graft_diff(graft_t *g, const char *older, const char *newer)
{
    if (!path_ok(older, GRAFT_PATH) || !path_ok(newer, GRAFT_PATH)) return ZXM_E_ARG;
    int rc = read_lines(g, older, &g->text[0]);
    if (!rc) rc = read_lines(g, newer, &g->text[1]);
    if (rc) return rc;
    int64_t kept = align(g, &g->text[0], &g->text[1], g->match[0], g->back[0]), add = 0, rem = 0;
    if (!graft_lines_added(g->text[1].n, kept, &add) ||
        !graft_lines_removed(g->text[0].n, kept, &rem))
        return ZXM_E_PROTOCOL;
    g->kept = kept, g->added = add, g->removed = rem;
    return ZXM_OK;
}

static int write_merge(graft_t *g, const char *base, const char *ours, const char *theirs,
                       const char *out)
{
    uint32_t nb = g->text[0].n;
    g->out_len = 0;
    g->out_begun = g->out_nl = false;
    int rc = ZXM_OK;
    for (uint32_t u = 0; u <= nb && !rc; u++) {
        lpres_state_t s = g->state[u];
        if (s == LPRES_STATE_NEITHER) {
            if (u < nb) rc = out_lines(g, out, base, &g->text[0], u, u + 1u);
        } else if (s == LPRES_STATE_TRUE) {
            uint32_t side = unit_changed(g, 0, u, nb) ? 0u : 1u;
            rc = out_lines(g, out, side ? theirs : ours, &g->text[side + 1u], g->from[side][u],
                           g->to[side][u]);
        } else {
            /* BOTH: both kept for the person to pick (GM6) */
            static const char *const MARK[3] = {"<<<<<<< ours\n", "=======\n", ">>>>>>> theirs\n"};
            rc = out_put(g, out, MARK[0], (uint32_t) strlen(MARK[0]));
            if (!rc) rc = out_lines(g, out, ours, &g->text[1], g->from[0][u], g->to[0][u]);
            if (!rc) rc = out_put(g, out, MARK[1], (uint32_t) strlen(MARK[1]));
            if (!rc) rc = out_lines(g, out, theirs, &g->text[2], g->from[1][u], g->to[1][u]);
            if (!rc) rc = out_put(g, out, MARK[2], (uint32_t) strlen(MARK[2]));
        }
    }
    return rc ? rc : out_flush(g, out, true);
}

int graft_merge(graft_t *g, const char *base, const char *ours, const char *theirs, const char *out)
{
    if (!path_ok(base, GRAFT_PATH) || !path_ok(ours, GRAFT_PATH) || !path_ok(theirs, GRAFT_PATH) ||
        !path_ok(out, GRAFT_PATH) || !strcmp(out, base) || !strcmp(out, ours) ||
        !strcmp(out, theirs))
        return ZXM_E_ARG;
    g->merged = LPRES_STATE_NEITHER;
    g->units = g->untouched = g->ours_only = g->theirs_only = g->same = g->clashes = 0;
    int rc = read_lines(g, base, &g->text[0]);
    if (!rc) rc = read_lines(g, ours, &g->text[1]);
    if (!rc) rc = read_lines(g, theirs, &g->text[2]);
    if (rc) return rc;
    uint32_t nb = g->text[0].n;
    for (uint32_t s = 0; s < 2u; s++) {
        align(g, &g->text[0], &g->text[s + 1u], g->match[s], g->back[s]);
        if (!units(nb, g->text[s + 1u].n, g->match[s], g->back[s], g->from[s], g->to[s]))
            return ZXM_E_PROTOCOL;
    }
    for (uint32_t u = 0; u <= nb; u++) {
        bool o = unit_changed(g, 0, u, nb), t = unit_changed(g, 1, u, nb);
        bool apart = o && t && unit_apart(g, u);
        lpres_state_t s = graft_line_state(o, t, apart);
        g->state[u] = s;
        if (graft_line_untouched(o, t))
            g->untouched++;
        else if (graft_line_clash(o, t, apart))
            g->clashes++;
        else if (o && t)
            g->same++;
        else if (o)
            g->ours_only++;
        else
            g->theirs_only++;
    }
    g->units = nb + 1u;
    lpres_state_t merged = graft_merge_join(g->state, nb + 1u);
    if ((rc = write_merge(g, base, ours, theirs, out)) != 0) {
        if (refused(g->link, rc, "exists")) say(g, "never over what is there: ", out);
        return rc;
    }
    g->merged = merged;
    if (merged == LPRES_STATE_BOTH)
        say(g, "lines changed apart on both sides are kept between markers for you to pick: ", out);
    else
        say(g, "merged: ", out);
    return ZXM_OK;
}
