/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* joinery_model.c — Joinery, the code editor (joinery_model.h JM1-JM7). */
#include "joinery_model.h"

#include <string.h>

const zxm_ai_action_t JOINERY_AI[JOINERY_AI_COUNT] = {
    {"Explain this code", "Coder", 700},
    {"Suggest a fix", "Coder", 900},
    {"Complete this code", "Coder", 800},
};

static const char *const ASK[JOINERY_AI_COUNT] = {
    "Explain what this code does, briefly. Answer in plain text.",
    "Suggest a corrected version of this code. Answer with the code only.",
    "Continue this code where it stops. Answer with the new code only.",
};

/* ---- small helpers, bounded ---- */

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (s && n < cap && s[n]) n++;
    return n;
}

/* the notice: a, then v when it is not negative (JOINERY_REFUSED: none) */
static void say(joinery_t *j, const char *a, int64_t v)
{
    char d[24];
    uint32_t k = sizeof d - 1u, at = len_of(a, JOINERY_NOTICE - 1u);
    uint64_t u = (uint64_t) v;
    memcpy(j->notice, a, at);
    j->notice[at] = 0;
    if (v < 0) return;
    d[k] = 0;
    do {
        d[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u);
    uint32_t dn = (uint32_t) (sizeof d - 1u - k);
    if (at + 1u + dn + 1u > JOINERY_NOTICE) return;
    j->notice[at++] = ' ';
    memcpy(j->notice + at, d + k, dn + 1u);
}

/* strict UTF-8, no NUL (files.write text cannot carry one) */
static bool utf8_ok(const char *p, uint32_t n)
{
    const uint8_t *s = (const uint8_t *) p;
    for (uint32_t i = 0; i < n;) {
        uint8_t c = s[i];
        if (c == 0) return false;
        if (c < 0x80) {
            i++;
            continue;
        }
        uint32_t k = c >= 0xc2 && c <= 0xdf   ? 1
                     : c >= 0xe0 && c <= 0xef ? 2
                     : c >= 0xf0 && c <= 0xf4 ? 3
                                              : 0;
        if (!k) return false;
        uint32_t cp = c & (k == 1 ? 0x1f : k == 2 ? 0x0f : 0x07);
        for (uint32_t m = 1; m <= k; m++) {
            if (i + m >= n || (s[i + m] & 0xc0) != 0x80) return false;
            cp = cp << 6 | (s[i + m] & 0x3f);
        }
        if ((k == 2 && cp < 0x800) || (k == 3 && (cp < 0x10000 || cp > 0x10ffff)) ||
            (cp >= 0xd800 && cp <= 0xdfff))
            return false;
        i += k + 1;
    }
    return true;
}

/* a Hoard path as far as the model can tell (zxvd checks the rest, F2) */
static bool path_ok(const char *p)
{
    uint32_t n = len_of(p, JOINERY_PATH_MAX + 1u);
    if (!n || n > JOINERY_PATH_MAX || p[0] == '/') return false;
    for (uint32_t i = 0; i < n; i++)
        if ((uint8_t) p[i] < 0x20 || p[i] == 0x7f) return false;
    return utf8_ok(p, n);
}

static bool boundary(const joinery_t *j, uint32_t at)
{
    return at == 0 || at >= j->len || ((uint8_t) j->text[at] & 0xc0) != 0x80;
}

/* the longest line of t[0..n), its newline aside */
static int64_t longest_line(const char *t, uint32_t n)
{
    int64_t best = 0, seg = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (t[i] == '\n') {
            best = seg > best ? seg : best;
            seg = 0;
        } else {
            seg++;
        }
    }
    return seg > best ? seg : best;
}

static void drop_held(joinery_t *j)
{
    if (j->held.state == ZXM_HELD_PENDING) {
        j->held.state = ZXM_HELD_REJECTED;
        j->held.len = 0;
        j->held.text[0] = 0;
    }
}

static void clear_undo(joinery_t *j)
{
    j->nundo = 0;
    j->undo_len = 0;
}

void joinery_init(joinery_t *j, zxm_link_t *link)
{
    j->link = link;
    j->open = j->fresh = j->dirty = false;
    j->path[0] = 0;
    j->text[0] = 0;
    j->len = 0;
    j->longest = 0;
    clear_undo(j);
    j->undo_dropped = 0;
    j->nhits = 0;
    j->total_hits = 0;
    j->indexed = j->text_indexed = 0;
    j->template_id[0] = 0;
    j->verdict = LPRES_STATE_NEITHER;
    j->approval_required = false;
    j->headline[0] = 0;
    memset(&j->held, 0, sizeof j->held);
    j->sel_start = j->sel_end = 0;
    j->notice[0] = 0;
}

/* ===================== JM1 ===================== */

static void take(joinery_t *j, const char *path, const char *text, uint32_t n, bool fresh,
                 int64_t longest)
{
    memcpy(j->path, path, len_of(path, JOINERY_PATH_MAX) + 1u);
    if (n) memmove(j->text, text, n);
    j->text[n] = 0;
    j->len = n;
    j->longest = longest;
    j->open = true;
    j->fresh = fresh;
    j->dirty = false;
    clear_undo(j);
    drop_held(j);
    j->sel_start = j->sel_end = 0;
}

int joinery_open(joinery_t *j, const char *path)
{
    static char chunk[64u * 1024u];
    /* gathered apart, so a failed open leaves the buffer as it was */
    static char into[JOINERY_FILE_MAX + 1];
    zxm_link_t *l = j->link;
    uint32_t len = 0;
    uint64_t off = 0;
    if (!path_ok(path)) return ZXM_E_ARG;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", path);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (rc) return rc;
        uint64_t size, next;
        bool eof;
        int32_t t = zxm_get(l, l->result, "text");
        if (zxm_get_u64(l, l->result, "size", &size) || zxm_get_u64(l, l->result, "next", &next) ||
            zxm_get_bool(l, l->result, "eof", &eof) || t < 0)
            return ZXM_E_PROTOCOL;
        /* JR5 on the size before anything more is read */
        if (size > (uint64_t) JOINERY_FILE_MAX || !joinery_buffer_ok((int64_t) size, 0, 0)) {
            say(j, "Past the editor's 16 MiB, not opened: bytes", (int64_t) size);
            return ZXM_E_TOOBIG;
        }
        int32_t n = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (n < 0 || (uint64_t) len + (uint64_t) n > (uint64_t) JOINERY_FILE_MAX)
            return ZXM_E_TOOBIG;
        memcpy(into + len, chunk, (size_t) n);
        len += (uint32_t) n;
        zxm_progress(l, len, size);
        if (eof) break;
        if (next <= off) return ZXM_E_PROTOCOL;
        off = next;
    }
    int64_t longest = longest_line(into, len);
    if (!joinery_buffer_ok(len, longest, 0)) {
        say(j, "A line is past 65536 bytes, not opened: bytes", longest);
        return ZXM_E_STATE;
    }
    take(j, path, into, len, false, longest);
    say(j, "Opened, bytes", len);
    return ZXM_OK;
}

int joinery_new(joinery_t *j, const char *path)
{
    if (!path_ok(path)) return ZXM_E_ARG;
    take(j, path, "", 0, true, 0);
    say(j, "New file, not saved yet", JOINERY_REFUSED);
    return ZXM_OK;
}

/* ===================== JM2 ===================== */

/* the longest line an edit leaves among the lines it touches */
static int64_t touched_longest(const joinery_t *j, uint32_t offset, uint32_t removed,
                               const char *ins, uint32_t n)
{
    uint32_t ls = offset, le = offset + removed;
    while (ls > 0 && j->text[ls - 1u] != '\n') ls--;
    while (le < j->len && j->text[le] != '\n') le++;
    int64_t best = 0, seg = offset - ls;
    for (uint32_t i = 0; i < n; i++) {
        if (ins[i] == '\n') {
            best = seg > best ? seg : best;
            seg = 0;
        } else {
            seg++;
        }
    }
    seg += le - (offset + removed);
    return seg > best ? seg : best;
}

/* drop the oldest step, its bytes moved out of the history */
static void drop_oldest(joinery_t *j)
{
    uint32_t gone = (uint32_t) (j->undo[0].removed + j->undo[0].inserted);
    memmove(j->undo_bytes, j->undo_bytes + gone, j->undo_len - gone);
    j->undo_len -= gone;
    for (uint32_t i = 1; i < j->nundo; i++) {
        j->undo[i - 1u] = j->undo[i];
        j->undo[i - 1u].step = i;
        j->undo[i - 1u].at -= gone;
    }
    j->nundo--;
    j->undo_dropped++;
}

/* JM2 without touching a held suggestion */
static int edit_apply(joinery_t *j, uint32_t offset, uint32_t removed, const char *ins, uint32_t n)
{
    /* check: the arguments */
    if (!j->open) return ZXM_E_STATE;
    if ((n && !ins) || (!n && !removed) || offset > j->len || removed > j->len - offset)
        return ZXM_E_ARG;
    if (!boundary(j, offset) || !boundary(j, offset + removed) || !utf8_ok(ins, n))
        return ZXM_E_ARG;
    /* check: JR5 after it, and JR6 for its record */
    int64_t after = (int64_t) j->len - removed + n;
    uint32_t steps = j->nundo < JOINERY_UNDO_MAX ? j->nundo + 1u : JOINERY_UNDO_MAX;
    int64_t touched = touched_longest(j, offset, removed, ins, n);
    if (!joinery_buffer_ok(after, touched, steps)) {
        say(j, "Past the editor's limits, not changed: bytes", after);
        return ZXM_E_ARG;
    }
    if (!joinery_undo_fits(steps, offset, removed, n)) return ZXM_E_ARG;
    /* reserve: room in the history */
    uint64_t need = (uint64_t) removed + n;
    bool keep = need <= JOINERY_UNDO_BYTES;
    if (!keep) {
        j->undo_dropped += j->nundo;
        clear_undo(j);
    } else {
        while (j->nundo &&
               (j->nundo >= JOINERY_UNDO_MAX || j->undo_len + need > JOINERY_UNDO_BYTES))
            drop_oldest(j);
    }
    /* compute and mutate: the removed bytes kept, then the text changed */
    uint32_t at = j->undo_len;
    if (keep) {
        memcpy(j->undo_bytes + at, j->text + offset, removed);
        if (n) memcpy(j->undo_bytes + at + removed, ins, n);
    }
    memmove(j->text + offset + n, j->text + offset + removed, j->len - offset - removed);
    if (n) memcpy(j->text + offset, ins, n);
    j->len = (uint32_t) after;
    j->text[j->len] = 0;
    j->longest = longest_line(j->text, j->len);
    j->dirty = true;
    /* commit: the record */
    if (keep) {
        j->undo[j->nundo] = (joinery_undo_t){(int64_t) j->nundo + 1, offset, removed, n, at};
        j->nundo++;
        j->undo_len = at + (uint32_t) need;
    } else {
        say(j, "Too large to undo; the undo history was cleared: bytes", (int64_t) need);
    }
    return ZXM_OK;
}

int joinery_edit(joinery_t *j, uint32_t offset, uint32_t removed, const char *ins, uint32_t n)
{
    int rc = edit_apply(j, offset, removed, ins, n);
    if (!rc) drop_held(j);
    return rc;
}

int joinery_undo(joinery_t *j)
{
    if (!j->open || !j->nundo) return ZXM_E_STATE;
    const joinery_undo_t *r = &j->undo[j->nundo - 1u];
    uint32_t off = (uint32_t) r->offset, ins = (uint32_t) r->inserted, rem = (uint32_t) r->removed;
    if (off > j->len || ins > j->len - off) return ZXM_E_STATE;
    int64_t after = (int64_t) j->len - ins + rem;
    if (after > JOINERY_FILE_MAX) return ZXM_E_STATE;
    memmove(j->text + off + rem, j->text + off + ins, j->len - off - ins);
    if (rem) memcpy(j->text + off, j->undo_bytes + r->at, rem);
    j->len = (uint32_t) after;
    j->text[j->len] = 0;
    j->longest = longest_line(j->text, j->len);
    j->undo_len = r->at;
    j->nundo--;
    j->dirty = true;
    drop_held(j);
    return ZXM_OK;
}

/* ===================== JM3 ===================== */

/* The lines ended by a newline from byte p, at most JOINERY_LINE_BLOCK of
 * them, into j->lines; *q is where the next block starts. */
static uint32_t block_of(joinery_t *j, uint32_t p, uint32_t *q)
{
    uint32_t cnt = 0;
    while (cnt < JOINERY_LINE_BLOCK && p < j->len) {
        const char *e = memchr(j->text + p, '\n', j->len - p);
        if (!e) break;
        uint32_t end = (uint32_t) (e - j->text) + 1u;
        j->lines[cnt++] = end - p;
        p = end;
    }
    *q = p;
    return cnt;
}

/* the widths of the characters of the line starting at `start` */
static uint32_t widths_of(joinery_t *j, uint32_t start)
{
    uint32_t n = 0;
    for (uint32_t i = start; i < j->len && j->text[i] != '\n' && n < JOINERY_LINE_MAX;) {
        int64_t w = joinery_utf8_width((uint8_t) j->text[i]);
        if (!w) w = 1; /* never inside a valid buffer; counted, so the scan goes on */
        j->width[n++] = w;
        i += (uint32_t) w;
    }
    return n;
}

int joinery_place_offset(joinery_t *j, int64_t line, int64_t col, int64_t *offset)
{
    if (!j->open) return ZXM_E_STATE;
    if (!offset || line < 0 || col < 0) return ZXM_E_ARG;
    int64_t base = 0, first = 0, start = -1;
    uint32_t p = 0, q;
    for (;;) {
        uint32_t cnt = block_of(j, p, &q);
        if (line - first <= (int64_t) cnt) {
            int64_t s = joinery_line_start(j->lines, cnt, line - first); /* JR1 */
            if (s < 0) return ZXM_E_ARG;
            start = base + s;
            break;
        }
        if (cnt < JOINERY_LINE_BLOCK) return ZXM_E_ARG; /* no such line */
        base += joinery_line_start(j->lines, cnt, cnt);
        first += cnt;
        p = q;
    }
    uint32_t n = widths_of(j, (uint32_t) start);
    int64_t o = joinery_col_offset(j->width, n, col); /* JR3 */
    if (o < 0) return ZXM_E_ARG;
    *offset = start + o;
    return ZXM_OK;
}

int joinery_offset_place(joinery_t *j, int64_t offset, int64_t *line, int64_t *col)
{
    if (!j->open) return ZXM_E_STATE;
    if (!line || !col || offset < 0 || offset > (int64_t) j->len) return ZXM_E_ARG;
    int64_t base = 0, first = 0, start;
    uint32_t p = 0, q;
    for (;;) {
        uint32_t cnt = block_of(j, p, &q);
        int64_t k = joinery_offset_line(j->lines, cnt, offset - base); /* JR2 */
        if (k < 0) return ZXM_E_ARG;
        if (k < (int64_t) cnt || cnt < JOINERY_LINE_BLOCK) {
            *line = first + k;
            start = base + joinery_line_start(j->lines, cnt, k); /* JR1 */
            break;
        }
        base += joinery_line_start(j->lines, cnt, cnt);
        first += cnt;
        p = q;
    }
    uint32_t n = widths_of(j, (uint32_t) start);
    int64_t c = joinery_offset_col(j->width, n, offset - start); /* JR4 */
    if (c < 0) return ZXM_E_ARG;
    *col = c;
    return ZXM_OK;
}

int64_t joinery_fill(const joinery_t *j)
{
    return j->open ? joinery_fill_x100(j->len) : JOINERY_REFUSED; /* JR7, @num.used_x100 */
}

/* ===================== JM4 ===================== */

/* One request's text: whole characters whose escaped form fits the line. */
#define PIECE 9000u

static int write_all(joinery_t *j)
{
    static char esc[ZXM_REQ_MAX];
    zxm_link_t *l = j->link;
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = j->len - at < PIECE ? j->len - at : PIECE;
        n = at + n < j->len ? zxm_utf8_cut(j->text + at, n) : n;
        while (n && zxm_escape(j->text + at, n, esc, 12000u) < 0)
            n = zxm_utf8_cut(j->text + at, n / 2u);
        if (!n && at < j->len) return ZXM_E_TOOBIG;
        bool last = at + n >= j->len;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", j->path);
        zxm_p_strn(l, "text", j->text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        if (j->fresh) zxm_p_bool(l, "exclusive", true);
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
        zxm_progress(l, at, j->len);
    } while (at < j->len);
    return ZXM_OK;
}

int joinery_save(joinery_t *j, bool owner_confirmed)
{
    if (!j->open) return ZXM_E_STATE;
    if (!joinery_buffer_ok(j->len, j->longest, j->nundo)) return ZXM_E_STATE;
    if (!owner_confirmed) {
        say(j, "Saving waits for the owner's word", JOINERY_REFUSED);
        return ZXM_E_STATE;
    }
    int rc = write_all(j);
    if (rc) return rc;
    j->fresh = false;
    j->dirty = false;
    say(j, "Saved, bytes", j->len);
    return ZXM_OK;
}

int joinery_rename(joinery_t *j, const char *to, bool owner_confirmed)
{
    if (!j->open) return ZXM_E_STATE;
    if (!path_ok(to) || !strcmp(to, j->path)) return ZXM_E_ARG;
    if (!owner_confirmed) {
        say(j, "Renaming waits for the owner's word", JOINERY_REFUSED);
        return ZXM_E_STATE;
    }
    if (!j->fresh) {
        zxm_link_t *l = j->link;
        zxm_begin(l, "files.rename");
        zxm_p_str(l, "from", j->path);
        zxm_p_str(l, "to", to);
        int rc = zxm_call(l);
        if (rc) return rc;
    }
    memcpy(j->path, to, len_of(to, JOINERY_PATH_MAX) + 1u);
    say(j, "Renamed", JOINERY_REFUSED);
    return ZXM_OK;
}

/* ===================== JM5 ===================== */

int joinery_find(joinery_t *j, const char *query)
{
    uint32_t qn = len_of(query, JOINERY_QUERY_MAX + 1u);
    bool word = false;
    if (!qn || qn > JOINERY_QUERY_MAX || !utf8_ok(query, qn)) return ZXM_E_ARG;
    for (uint32_t i = 0; i < qn; i++) word = word || (query[i] != ' ' && query[i] != '\t');
    if (!word) return ZXM_E_ARG;
    zxm_link_t *l = j->link;
    zxm_begin(l, "search.query");
    zxm_p_str(l, "q", query);
    zxm_p_u64(l, "max", JOINERY_HITS);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "hits");
    int64_t total;
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > JOINERY_HITS ||
        zxm_get_i64(l, l->result, "total", &total) || total < 0)
        return ZXM_E_PROTOCOL;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < zxm_len(l, arr); i++) {
        joinery_hit_t *h = &j->hit[kept];
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || l->tok[o].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
        if (zxm_get_str(l, o, "path", h->path, sizeof h->path) ||
            zxm_get_i64(l, o, "size", &h->size) || zxm_get_i64(l, o, "score", &h->score) ||
            zxm_get_bool(l, o, "in_name", &h->in_name))
            return ZXM_E_PROTOCOL;
        if (zxm_get_str(l, o, "snippet", h->snippet, sizeof h->snippet)) h->snippet[0] = 0;
        kept++;
    }
    j->nhits = kept;
    j->total_hits = total;
    say(j, "Found, files", total);
    return ZXM_OK;
}

int joinery_reindex(joinery_t *j)
{
    zxm_link_t *l = j->link;
    zxm_begin(l, "search.refresh");
    int rc = zxm_call(l);
    if (rc) return rc;
    int64_t files, text;
    if (zxm_get_i64(l, l->result, "files", &files) ||
        zxm_get_i64(l, l->result, "text_files", &text) || files < 0 || text < 0 || text > files)
        return ZXM_E_PROTOCOL;
    j->indexed = files;
    j->text_indexed = text;
    say(j, "Index up to date, files", files);
    return ZXM_OK;
}

/* ===================== JM6 ===================== */

static bool name_ok(const char *s, uint32_t max)
{
    uint32_t n = len_of(s, max + 1u);
    if (!n || n > max) return false;
    for (uint32_t i = 0; i < n; i++)
        if ((uint8_t) s[i] < 0x20 || s[i] == 0x7f || s[i] == '/' || s[i] == '\\') return false;
    return utf8_ok(s, n);
}

static bool put(char *out, uint32_t cap, uint32_t *at, const char *s, uint32_t n)
{
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

static bool put_esc(char *out, uint32_t cap, uint32_t *at, const char *s, uint32_t n)
{
    if (*at >= cap) return false;
    int32_t k = zxm_escape(s, n, out + *at, cap - *at);
    if (k < 0 || *at + (uint32_t) k + 1u > cap) return false;
    *at += (uint32_t) k;
    out[*at] = 0;
    return true;
}

int joinery_run(joinery_t *j, const char *template_id, const char *name)
{
    static char params[ZXM_REQ_MAX];
    if (!j->open) return ZXM_E_STATE;
    if (!name_ok(template_id, JOINERY_TEMPLATE_MAX) || !name_ok(name, JOINERY_FILE_NAME))
        return ZXM_E_ARG;
    /* the whole request must fit zxvd's line: nothing is sent when it does not */
    uint32_t at = 0, cap = ZXM_REQ_MAX - 128u;
    if (!put(params, cap, &at, "{\"template\":\"", 13u) ||
        !put_esc(params, cap, &at, template_id, len_of(template_id, JOINERY_TEMPLATE_MAX)) ||
        !put(params, cap, &at, "\",\"files\":{\"", 12u) ||
        !put_esc(params, cap, &at, name, len_of(name, JOINERY_FILE_NAME)) ||
        !put(params, cap, &at, "\":\"", 3u) || !put_esc(params, cap, &at, j->text, j->len) ||
        !put(params, cap, &at, "\"}}", 3u)) {
        say(j, "Too large to run in one request: bytes", j->len);
        return ZXM_E_TOOBIG;
    }
    zxm_link_t *l = j->link;
    zxm_begin(l, "play.run");
    zxm_p_raw(l, params, at);
    int rc = zxm_call(l);
    if (rc) return rc;
    char verdict[16], approval[16];
    if (zxm_get_str(l, l->result, "verdict", verdict, sizeof verdict)) return ZXM_E_PROTOCOL;
    lpres_state_t v;
    if (!strcmp(verdict, "TRUE"))
        v = LPRES_STATE_TRUE;
    else if (!strcmp(verdict, "FALSE"))
        v = LPRES_STATE_FALSE;
    else if (!strcmp(verdict, "BOTH"))
        v = LPRES_STATE_BOTH;
    else if (!strcmp(verdict, "NEITHER"))
        v = LPRES_STATE_NEITHER;
    else
        return ZXM_E_PROTOCOL;
    j->verdict = v;
    j->approval_required = !zxm_get_str(l, l->result, "approval", approval, sizeof approval) &&
                           !strcmp(approval, "required");
    if (zxm_get_str(l, l->result, "headline", j->headline, sizeof j->headline)) j->headline[0] = 0;
    memcpy(j->template_id, template_id, len_of(template_id, JOINERY_TEMPLATE_MAX) + 1u);
    say(j,
        v == LPRES_STATE_TRUE    ? "The run passed"
        : v == LPRES_STATE_FALSE ? "The run refused it"
                                 : "The run is held for a person",
        JOINERY_REFUSED);
    return ZXM_OK;
}

/* ===================== JM7 ===================== */

int joinery_suggest(joinery_t *j, int32_t action, uint32_t start, uint32_t end)
{
    if (!j->open) return ZXM_E_STATE;
    if (action < 0 || action >= JOINERY_AI_COUNT || start > end || end > j->len ||
        !boundary(j, start) || !boundary(j, end))
        return ZXM_E_ARG;
    if (start == end) start = 0, end = j->len;
    int rc = zxm_ai_ask(j->link, &JOINERY_AI[action], action, ASK[action], j->text + start,
                        end - start, &j->held);
    if (rc) return rc;
    j->sel_start = start;
    j->sel_end = end;
    say(j, "A suggestion is held for you to accept or reject", JOINERY_REFUSED);
    return ZXM_OK;
}

int joinery_suggest_accept(joinery_t *j)
{
    if (j->held.state != ZXM_HELD_PENDING) return ZXM_E_STATE;
    int32_t a = j->held.action;
    int rc = ZXM_OK;
    if (a == JOINERY_AI_FIX)
        rc = edit_apply(j, j->sel_start, j->sel_end - j->sel_start, j->held.text, j->held.len);
    else if (a == JOINERY_AI_COMPLETE)
        rc = edit_apply(j, j->sel_end, 0, j->held.text, j->held.len);
    if (rc) return rc; /* still held: nothing changed */
    return zxm_held_accept(&j->held);
}

int joinery_suggest_reject(joinery_t *j)
{
    return zxm_held_reject(&j->held);
}
