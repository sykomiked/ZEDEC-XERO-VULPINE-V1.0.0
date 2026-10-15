/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* flint_model.c — Flint, the terminal (see flint_model.h). */
#include <string.h>

#include "flint_model.h"

/* ---------------- the rules the blocks witness ---------------- */

int64_t flint_cells(int64_t pixels, int64_t cell)
{
    if (pixels < 0 || pixels > FLINT_PIXELS_MAX) return FLINT_REFUSED;
    if (cell < 1 || cell > FLINT_CELL_MAX) return FLINT_REFUSED;
    return pixels / cell;
}

bool flint_scroll_ok(int64_t lines, int64_t bytes)
{
    return lines >= FLINT_LINES_MIN && lines <= FLINT_LINES_MAX && bytes >= FLINT_BYTES_MIN &&
           bytes <= FLINT_BYTES_MAX;
}

/* MYRA's iN: a signed value of at most N digits */
static bool digits(int64_t v, int64_t top)
{
    return v > -top && v < top;
}

bool flint_history_ok(int64_t seq, int64_t stamp, int64_t status, int64_t bytes)
{
    return digits(seq, INT64_C(1000000000)) && digits(stamp, INT64_C(1000000000000)) &&
           digits(status, 1000) && digits(bytes, 10000);
}

int64_t flint_used_x100(uint64_t total, uint64_t left)
{
    if (!total || left > total) return FLINT_REFUSED;
    if (total > UINT64_MAX / 10000u) return FLINT_REFUSED;
    return (int64_t) ((total - left) * 10000u / total);
}

/* ---------------- words and paths (text work: C) ---------------- */

static bool is_op(char c)
{
    return c == '|' || c == ';' || c == '&' || c == '<' || c == '>' || c == '(' || c == ')';
}

int32_t flint_parse(const char *line, uint32_t n, flint_words_t *w)
{
    if (n > FLINT_CMD_MAX) return FLINT_P_LONG;
    w->argc = 0;
    uint32_t o = 0;
    bool in_word = false;
    char quote = 0;
    for (uint32_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char) line[i];
        if ((c < 0x20 && c != '\t') || c == 0x7f) return FLINT_P_CTRL;
        if (quote == '\'') {
            if (c == '\'')
                quote = 0;
            else
                w->buf[o++] = (char) c;
            continue;
        }
        if (quote == '"') {
            if (c == '"') {
                quote = 0;
            } else if (c == '\\' && i + 1 < n &&
                       (line[i + 1] == '"' || line[i + 1] == '\\' || line[i + 1] == '$' ||
                        line[i + 1] == '`')) {
                w->buf[o++] = line[++i];
            } else {
                w->buf[o++] = (char) c;
            }
            continue;
        }
        if (c == ' ' || c == '\t') {
            if (in_word) {
                w->buf[o++] = 0;
                in_word = false;
            }
            continue;
        }
        if (!in_word && c == '#') break; /* a comment to the end */
        if (is_op((char) c)) return FLINT_P_OP;
        if (!in_word) {
            if (w->argc == FLINT_ARGS_MAX) return FLINT_P_MANY;
            w->argv[w->argc++] = &w->buf[o];
            in_word = true;
        }
        if (c == '\\') {
            if (i + 1 >= n) return FLINT_P_QUOTE;
            c = (unsigned char) line[++i];
            if ((c < 0x20 && c != '\t') || c == 0x7f) return FLINT_P_CTRL;
            w->buf[o++] = (char) c;
        } else if (c == '\'' || c == '"') {
            quote = (char) c;
        } else {
            w->buf[o++] = (char) c;
        }
    }
    if (quote) return FLINT_P_QUOTE;
    if (in_word) w->buf[o++] = 0;
    return (int32_t) w->argc;
}

int flint_resolve(const char *cwd, const char *arg, char *out, uint32_t cap)
{
    uint32_t o = 0;
    if (!cap) return ZXM_E_ARG;
    out[0] = 0;
    if (arg[0] != '/') {
        uint32_t c = (uint32_t) strlen(cwd);
        if (c >= cap) return ZXM_E_ARG;
        memcpy(out, cwd, c + 1);
        o = c;
    }
    const char *p = arg;
    while (*p) {
        while (*p == '/') p++;
        const char *s = p;
        while (*p && *p != '/') p++;
        uint32_t k = (uint32_t) (p - s);
        if (!k || (k == 1 && s[0] == '.')) continue;
        if (k == 2 && s[0] == '.' && s[1] == '.') {
            while (o && out[o - 1] != '/') o--;
            if (o) o--; /* the '/' before it */
            out[o] = 0;
            continue;
        }
        if (o + (o ? 1u : 0u) + k >= cap) return ZXM_E_ARG;
        if (o) out[o++] = '/';
        memcpy(out + o, s, k);
        o += k;
        out[o] = 0;
    }
    return ZXM_OK;
}

/* ---------------- FL2: the scrollback ---------------- */

static void drop_oldest(flint_t *f)
{
    const flint_line_t *l = &f->line[f->first];
    f->tail = l->at + l->len;
    f->first = (f->first + 1u) % FLINT_LINES_MAX;
    f->nlines--;
    if (!f->nlines) f->tail = f->head;
}

static void push_line(flint_t *f, const char *s, uint32_t n)
{
    if (n > FLINT_LINE_MAX) n = zxm_utf8_cut(s, FLINT_LINE_MAX);
    /* check and make room first: the oldest lines go until this one fits */
    while (f->nlines && (f->nlines >= f->lines_limit || f->head - f->tail + n > f->bytes_limit))
        drop_oldest(f);
    flint_line_t *l = &f->line[(f->first + f->nlines) % FLINT_LINES_MAX];
    l->at = f->head;
    l->len = n;
    for (uint32_t i = 0; i < n; i++) f->ring[(f->head + i) % FLINT_BYTES_MAX] = s[i];
    f->head += n;
    f->nlines++;
}

void flint_print(flint_t *f, const char *s, uint32_t n)
{
    uint32_t start = 0;
    for (uint32_t i = 0; i <= n; i++) {
        if (i < n && s[i] != '\n') continue;
        uint32_t end = i;
        if (end > start && s[end - 1] == '\r') end--;
        if (i < n || end > start) push_line(f, s + start, end - start);
        start = i + 1;
    }
}

void flint_clear(flint_t *f)
{
    f->nlines = 0;
    f->first = 0;
    f->tail = f->head;
}

uint32_t flint_lines(const flint_t *f)
{
    return f->nlines;
}

uint64_t flint_bytes(const flint_t *f)
{
    return f->head - f->tail;
}

int32_t flint_line(const flint_t *f, uint32_t i, char *out, uint32_t cap)
{
    if (i >= f->nlines) return -1;
    const flint_line_t *l = &f->line[(f->first + i) % FLINT_LINES_MAX];
    if (l->len >= cap) return -1;
    for (uint32_t k = 0; k < l->len; k++) out[k] = f->ring[(l->at + k) % FLINT_BYTES_MAX];
    out[l->len] = 0;
    return (int32_t) l->len;
}

uint32_t flint_top(const flint_t *f)
{
    if (f->rows <= 0 || (int64_t) f->nlines <= f->rows) return 0;
    return f->nlines - (uint32_t) f->rows;
}

int flint_set_limits(flint_t *f, int64_t lines, int64_t bytes)
{
    if (!flint_scroll_ok(lines, bytes)) return ZXM_E_ARG;
    f->lines_limit = (uint32_t) lines;
    f->bytes_limit = (uint32_t) bytes;
    while (f->nlines > f->lines_limit || f->head - f->tail > f->bytes_limit) drop_oldest(f);
    return ZXM_OK;
}

/* ---------------- FL1: the grid ---------------- */

int flint_resize(flint_t *f, int64_t width, int64_t height, int64_t cell_w, int64_t cell_h)
{
    int64_t c = flint_cells(width, cell_w), r = flint_cells(height, cell_h);
    if (c < 1 || r < 1) return ZXM_E_ARG;
    f->cols = c;
    f->rows = r;
    return ZXM_OK;
}

void flint_init(flint_t *f, zxm_link_t *link)
{
    memset(f, 0, sizeof *f);
    f->link = link;
    f->lines_limit = FLINT_LINES_DEF;
    f->bytes_limit = FLINT_BYTES_DEF;
    f->next_seq = 1;
}

/* ---------------- one output line at a time ---------------- */

static void put_n(flint_t *f, const char *s, uint32_t n)
{
    if (n > FLINT_LINE_MAX - f->outn) n = FLINT_LINE_MAX - f->outn;
    memcpy(f->out + f->outn, s, n);
    f->outn += n;
}

static void put_s(flint_t *f, const char *s)
{
    put_n(f, s, (uint32_t) strlen(s));
}

static void put_u(flint_t *f, uint64_t v)
{
    char d[24];
    uint32_t k = sizeof d;
    do {
        d[--k] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    put_n(f, d + k, (uint32_t) sizeof d - k);
}

static void put_x100(flint_t *f, int64_t x) /* 4912 -> 49.12% */
{
    if (x < 0) {
        put_s(f, "unknown");
        return;
    }
    put_u(f, (uint64_t) x / 100u);
    put_s(f, ".");
    if (x % 100 < 10) put_s(f, "0");
    put_u(f, (uint64_t) x % 100u);
    put_s(f, "%");
}

static void flush(flint_t *f)
{
    push_line(f, f->out, f->outn);
    f->outn = 0;
}

/* "flint: <cmd>: <why>", and the status it gives */
static int say(flint_t *f, const char *cmd, const char *why, int status)
{
    f->outn = 0;
    put_s(f, "flint: ");
    if (cmd) {
        put_s(f, cmd);
        put_s(f, ": ");
    }
    put_s(f, why);
    flush(f);
    return status;
}

/* zxm_call's failure: zxvd's refusal, or what went wrong on the way (the
 * link's message either way) */
static int failed(flint_t *f, const char *cmd, int rc)
{
    return say(f, cmd,
               f->link->err[0]      ? f->link->err
               : rc == ZXM_E_REMOTE ? "refused"
                                    : "no answer",
               FLINT_ST_REFUSED);
}

/* an answer without the fields its method promises */
static int garbled(flint_t *f, const char *cmd)
{
    return say(f, cmd, "zxvd's answer is not what its method promises", FLINT_ST_REFUSED);
}

/* ---------------- the builtins ---------------- */

static int path_of(flint_t *f, const char *cmd, const char *arg, char *out)
{
    if (flint_resolve(f->cwd, arg, out, FLINT_PATH_MAX + 1) != ZXM_OK)
        return say(f, cmd, "the path is too long", FLINT_ST_MISUSE);
    return FLINT_ST_OK;
}

static int b_cd(flint_t *f, const flint_words_t *w)
{
    char p[FLINT_PATH_MAX + 1];
    if (w->argc > 2) return say(f, "cd", "one folder at most", FLINT_ST_MISUSE);
    int st = path_of(f, "cd", w->argc == 2 ? w->argv[1] : "/", p);
    if (st) return st;
    if (p[0]) {
        zxm_link_t *l = f->link;
        bool dir = false;
        zxm_begin(l, "files.stat");
        zxm_p_str(l, "path", p);
        int rc = zxm_call(l);
        if (rc) return failed(f, "cd", rc);
        if (zxm_get_bool(l, l->result, "dir", &dir)) return garbled(f, "cd");
        if (!dir) return say(f, "cd", "not a folder", FLINT_ST_REFUSED);
    }
    memcpy(f->cwd, p, strlen(p) + 1);
    return FLINT_ST_OK;
}

static int b_ls(flint_t *f, const flint_words_t *w)
{
    char p[FLINT_PATH_MAX + 1], name[FLINT_PATH_MAX + 1];
    if (w->argc > 2) return say(f, "ls", "one folder at most", FLINT_ST_MISUSE);
    int st = path_of(f, "ls", w->argc == 2 ? w->argv[1] : ".", p);
    if (st) return st;
    zxm_link_t *l = f->link;
    zxm_begin(l, "files.list");
    zxm_p_str(l, "path", p);
    int rc = zxm_call(l);
    if (rc) return failed(f, "ls", rc);
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return garbled(f, "ls");
    uint32_t n = zxm_len(l, arr);
    for (uint32_t i = 0; i < n; i++) {
        int32_t e = zxm_at(l, arr, i);
        bool dir = false;
        uint64_t size = 0;
        if (e < 0 || zxm_get_str(l, e, "name", name, sizeof name) ||
            zxm_get_bool(l, e, "dir", &dir))
            return garbled(f, "ls");
        f->outn = 0;
        put_s(f, name);
        if (dir) {
            put_s(f, "/");
        } else if (!zxm_get_u64(l, e, "size", &size)) {
            put_s(f, "  ");
            put_u(f, size);
        }
        flush(f);
    }
    return FLINT_ST_OK;
}

static int b_stat(flint_t *f, const flint_words_t *w)
{
    char p[FLINT_PATH_MAX + 1];
    if (w->argc != 2) return say(f, "stat", "one path", FLINT_ST_MISUSE);
    int st = path_of(f, "stat", w->argv[1], p);
    if (st) return st;
    zxm_link_t *l = f->link;
    zxm_begin(l, "files.stat");
    zxm_p_str(l, "path", p);
    int rc = zxm_call(l);
    if (rc) return failed(f, "stat", rc);
    bool dir = false;
    uint64_t size = 0, mtime = 0;
    if (zxm_get_bool(l, l->result, "dir", &dir) || zxm_get_u64(l, l->result, "size", &size) ||
        zxm_get_u64(l, l->result, "mtime", &mtime))
        return garbled(f, "stat");
    f->outn = 0;
    put_s(f, "/");
    put_s(f, p);
    if (dir) {
        put_s(f, ": folder");
    } else {
        put_s(f, ": ");
        put_u(f, size);
        put_s(f, " bytes");
    }
    put_s(f, ", modified ");
    put_u(f, mtime);
    flush(f);
    return FLINT_ST_OK;
}

static int cat_one(flint_t *f, const char *arg)
{
    char p[FLINT_PATH_MAX + 1];
    int st = path_of(f, "cat", arg, p);
    if (st) return st;
    zxm_link_t *l = f->link;
    uint64_t off = 0;
    bool eof = false;
    while (!eof && off < FLINT_CAT_MAX) {
        uint64_t want = FLINT_CAT_MAX - off < FLINT_CHUNK ? FLINT_CAT_MAX - off : FLINT_CHUNK;
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", p);
        zxm_p_u64(l, "offset", off);
        zxm_p_u64(l, "max", want);
        zxm_p_str(l, "encoding", "utf8");
        int rc = zxm_call(l);
        if (rc) return failed(f, "cat", rc);
        uint64_t next = 0;
        if (zxm_get_u64(l, l->result, "next", &next) || zxm_get_bool(l, l->result, "eof", &eof) ||
            next < off || next - off > want ||
            zxm_get_str(l, l->result, "text", f->text + off, (uint32_t) (want + 1u)))
            return garbled(f, "cat");
        if (next == off && !eof) return garbled(f, "cat");
        off = next;
    }
    flint_print(f, f->text, (uint32_t) off);
    if (!eof) {
        f->outn = 0;
        put_s(f, "flint: cat: /");
        put_s(f, p);
        put_s(f, ": showing its first ");
        put_u(f, off);
        put_s(f, " bytes");
        flush(f);
    }
    return FLINT_ST_OK;
}

static int b_cat(flint_t *f, const flint_words_t *w)
{
    if (w->argc < 2) return say(f, "cat", "a file to show", FLINT_ST_MISUSE);
    int worst = FLINT_ST_OK;
    for (uint32_t i = 1; i < w->argc; i++) {
        int st = cat_one(f, w->argv[i]);
        if (st > worst) worst = st;
    }
    return worst;
}

static int b_df(flint_t *f, const flint_words_t *w)
{
    if (w->argc != 1) return say(f, "df", "takes no arguments", FLINT_ST_MISUSE);
    zxm_link_t *l = f->link;
    zxm_begin(l, "sys.disk");
    int rc = zxm_call(l);
    if (rc) return failed(f, "df", rc);
    uint64_t total = 0, fr = 0, avail = 0;
    if (zxm_get_u64(l, l->result, "total", &total) || zxm_get_u64(l, l->result, "free", &fr) ||
        zxm_get_u64(l, l->result, "avail", &avail))
        return garbled(f, "df");
    f->outn = 0;
    put_s(f, "the Hoard's volume: ");
    put_u(f, total);
    put_s(f, " bytes, ");
    put_u(f, fr);
    put_s(f, " free, ");
    put_u(f, avail);
    put_s(f, " yours to write, used ");
    put_x100(f, flint_used_x100(total, fr));
    flush(f);
    return FLINT_ST_OK;
}

static int b_info(flint_t *f, const char *cmd, bool mem)
{
    char s[128];
    zxm_link_t *l = f->link;
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc) return failed(f, cmd, rc);
    f->outn = 0;
    if (mem) {
        uint64_t total = 0, avail = 0;
        if (zxm_get_u64(l, l->result, "mem_total", &total) ||
            zxm_get_u64(l, l->result, "mem_avail", &avail))
            return garbled(f, cmd);
        put_s(f, "memory: ");
        put_u(f, total);
        put_s(f, " bytes, ");
        put_u(f, avail);
        put_s(f, " available, used ");
        put_x100(f, flint_used_x100(total, avail));
    } else {
        static const char *const keys[4] = {"os", "os_version", "arch", "host"};
        for (uint32_t i = 0; i < 4; i++) {
            if (zxm_get_str(l, l->result, keys[i], s, sizeof s)) s[0] = 0; /* unknown: left out */
            if (!s[0]) continue;
            if (f->outn) put_s(f, " ");
            put_s(f, s);
        }
        if (!f->outn) put_s(f, "unknown");
    }
    flush(f);
    return FLINT_ST_OK;
}

static int b_net(flint_t *f, const flint_words_t *w)
{
    char s[128];
    if (w->argc != 1) return say(f, "net", "takes no arguments", FLINT_ST_MISUSE);
    zxm_link_t *l = f->link;
    zxm_begin(l, "sys.net");
    int rc = zxm_call(l);
    if (rc) return failed(f, "net", rc);
    bool sup = false;
    if (zxm_get_bool(l, l->result, "supported", &sup)) return garbled(f, "net");
    if (!sup) return say(f, "net", "this platform's interfaces are not listed", FLINT_ST_REFUSED);
    int32_t arr = zxm_get(l, l->result, "interfaces");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return garbled(f, "net");
    for (uint32_t i = 0; i < zxm_len(l, arr); i++) {
        int32_t e = zxm_at(l, arr, i);
        bool up = false;
        if (e < 0 || zxm_get_str(l, e, "name", s, sizeof s) || zxm_get_bool(l, e, "up", &up))
            return garbled(f, "net");
        f->outn = 0;
        put_s(f, s);
        put_s(f, up ? " up" : " down");
        int32_t ad = zxm_get(l, e, "addresses");
        for (uint32_t k = 0; ad >= 0 && l->tok[ad].type == W4_J_ARR && k < zxm_len(l, ad); k++) {
            int32_t a = zxm_at(l, ad, k);
            if (a < 0 || l->tok[a].type != W4_J_STR ||
                w4_json_str(l->resp, &l->tok[a], s, sizeof s) < 0)
                continue;
            put_s(f, " ");
            put_s(f, s);
        }
        flush(f);
    }
    return FLINT_ST_OK;
}

static int b_history(flint_t *f)
{
    for (uint32_t i = 0; i < f->nhist; i++) {
        const flint_hist_t *h = flint_history_at(f, i);
        f->outn = 0;
        put_s(f, "  ");
        put_u(f, (uint64_t) h->seq);
        put_s(f, "  ");
        put_n(f, h->text, h->len);
        flush(f);
    }
    return FLINT_ST_OK;
}

static int b_help(flint_t *f)
{
    static const char *const H[] = {
        "Flint runs these over zxvd; pipes, redirection and programs run in zxsh:",
        "  pwd, cd [folder]          the working folder (the Hoard is /)",
        "  ls [folder], stat path    a folder's entries, one entry",
        "  cat file...               a file's text (its first 65536 bytes)",
        "  df, free, uname, net      the disk, memory, system and network",
        "  echo words, history, clear, help",
    };
    for (uint32_t i = 0; i < sizeof H / sizeof H[0]; i++)
        flint_print(f, H[i], (uint32_t) strlen(H[i]));
    return FLINT_ST_OK;
}

static int dispatch(flint_t *f, const flint_words_t *w)
{
    const char *c = w->argv[0];
    if (!strcmp(c, "pwd")) {
        if (w->argc != 1) return say(f, "pwd", "takes no arguments", FLINT_ST_MISUSE);
        f->outn = 0;
        put_s(f, "/");
        put_s(f, f->cwd);
        flush(f);
        return FLINT_ST_OK;
    }
    if (!strcmp(c, "echo")) {
        f->outn = 0;
        for (uint32_t i = 1; i < w->argc; i++) {
            if (i > 1) put_s(f, " ");
            put_s(f, w->argv[i]);
        }
        flush(f);
        return FLINT_ST_OK;
    }
    if (!strcmp(c, "cd")) return b_cd(f, w);
    if (!strcmp(c, "ls")) return b_ls(f, w);
    if (!strcmp(c, "stat")) return b_stat(f, w);
    if (!strcmp(c, "cat")) return b_cat(f, w);
    if (!strcmp(c, "df")) return b_df(f, w);
    if (!strcmp(c, "free") || !strcmp(c, "uname")) {
        if (w->argc != 1) return say(f, c, "takes no arguments", FLINT_ST_MISUSE);
        return b_info(f, c, c[0] == 'f');
    }
    if (!strcmp(c, "net")) return b_net(f, w);
    if (!strcmp(c, "history")) return b_history(f);
    if (!strcmp(c, "help")) return b_help(f);
    if (!strcmp(c, "clear")) {
        flint_clear(f);
        return FLINT_ST_OK;
    }
    return say(f, c, "no such command (programs run in zxsh)", FLINT_ST_NOCMD);
}

/* ---------------- FL3: one command ---------------- */

static void record(flint_t *f, const char *line, uint32_t n, int64_t now, int status)
{
    if (f->nhist == FLINT_HISTORY) {
        f->hfirst = (f->hfirst + 1u) % FLINT_HISTORY;
        f->nhist--;
    }
    flint_hist_t *h = &f->hist[(f->hfirst + f->nhist) % FLINT_HISTORY];
    h->seq = f->next_seq++;
    h->stamp = now;
    h->status = status;
    h->len = n;
    memcpy(h->text, line, n);
    h->text[n] = 0;
    f->nhist++;
}

int flint_run(flint_t *f, const char *line, int64_t now)
{
    if (!f || !line) return ZXM_E_ARG;
    uint32_t n = 0;
    while (n <= FLINT_CMD_MAX && line[n]) n++;
    /* check first: a line too long, or whose record would not fit, runs nothing */
    if (n > FLINT_CMD_MAX || now < 0) return ZXM_E_ARG;
    if (!flint_history_ok(f->next_seq, now, FLINT_ST_NOCMD, n)) return ZXM_E_STATE;
    f->outn = 0;
    put_s(f, "/");
    put_s(f, f->cwd);
    put_s(f, " $ ");
    put_n(f, line, n);
    flush(f);
    int32_t argc = flint_parse(line, n, &f->words);
    int st;
    if (argc == 0) return f->last_status = FLINT_ST_OK; /* a blank line is not recorded */
    if (argc == FLINT_P_OP)
        st = say(f, 0, "pipes, redirection, lists and jobs run in zxsh, not here", FLINT_ST_MISUSE);
    else if (argc == FLINT_P_QUOTE)
        st = say(f, 0, "a quote or a backslash is not closed", FLINT_ST_MISUSE);
    else if (argc == FLINT_P_MANY)
        st = say(f, 0, "more than 16 words", FLINT_ST_MISUSE);
    else if (argc < 0)
        st = say(f, 0, "a control character in the line", FLINT_ST_MISUSE);
    else
        st = dispatch(f, &f->words);
    record(f, line, n, now, st);
    f->last_status = st;
    return st;
}

uint32_t flint_history_count(const flint_t *f)
{
    return f->nhist;
}

const flint_hist_t *flint_history_at(const flint_t *f, uint32_t i)
{
    return i < f->nhist ? &f->hist[(f->hfirst + i) % FLINT_HISTORY] : NULL;
}
