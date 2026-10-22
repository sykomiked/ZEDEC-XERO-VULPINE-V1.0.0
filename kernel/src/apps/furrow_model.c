/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* furrow_model.c — Furrow, spreadsheets (see furrow_model.h). Money is
 * shown and summed, never paid. */
#include <string.h>

#include "furrow_model.h"

/* ---- cells ------------------------------------------------------------------ */

static bool place_ok(int64_t row, int64_t col)
{
    return row >= 1 && row <= FURROW_ROW_MAX && col >= 1 && col <= FURROW_COL_MAX;
}

/* A cell may be kept: its place, its record (FW4), and for a formula a
 * range inside the sheet that references no more than formula_ok allows. */
static bool cell_ok(int32_t sheet, const furrow_cell_t *c)
{
    if (!place_ok(c->row, c->col) ||
        !furrow_cell_record_ok(sheet, c->row, c->col, c->kind, c->value))
        return false;
    if (c->kind == FURROW_NUMBER || c->kind == FURROW_MONEY) return true;
    if (c->kind != FURROW_FORMULA) return false;
    int64_t top = c->dir == FURROW_DOWN ? FURROW_ROW_MAX : FURROW_COL_MAX;
    int64_t side = c->dir == FURROW_DOWN ? FURROW_COL_MAX : FURROW_ROW_MAX;
    if (c->dir != FURROW_DOWN && c->dir != FURROW_ACROSS) return false;
    if (c->value < 1 || c->value > side || c->lo < 1 || c->hi > top || c->lo > c->hi) return false;
    return furrow_formula_ok(0, (int64_t) c->hi - c->lo + 1);
}

/* Where (row, col) is, or where it would go: the first cell not before it. */
static uint32_t find(const furrow_cell_t *c, uint32_t n, int64_t row, int64_t col)
{
    uint32_t a = 0, b = n;
    while (a < b) {
        uint32_t m = a + (b - a) / 2u;
        if (c[m].row < row || (c[m].row == row && c[m].col < col))
            a = m + 1u;
        else
            b = m;
    }
    return a;
}

static bool here(const furrow_cell_t *c, uint32_t n, uint32_t i, int64_t row, int64_t col)
{
    return i < n && c[i].row == row && c[i].col == col;
}

/* ---- the sheet as text (FM1-FM3) ------------------------------------------- */

static const char HEAD[] = "furrow 1\n";

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

static bool put_num(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char t[24];
    int k = 23;
    bool neg = v < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u && k > 1);
    if (neg) t[--k] = '-';
    return put(out, cap, at, t + k);
}

/* " <v>" */
static bool put_field(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    return put(out, cap, at, " ") && put_num(out, cap, at, v);
}

/* The whole sheet (FM1, FM2); its length, or -1 when it does not fit. */
static int32_t compose(const furrow_cell_t *c, uint32_t n, char *out, uint32_t cap)
{
    uint32_t at = 0;
    if (!put(out, cap, &at, HEAD)) return -1;
    for (uint32_t i = 0; i < n; i++) {
        bool ok = put_num(out, cap, &at, c[i].row) && put_field(out, cap, &at, c[i].col) &&
                  put_field(out, cap, &at, c[i].kind);
        if (ok && c[i].kind == FURROW_FORMULA)
            ok = put_field(out, cap, &at, c[i].dir) && put_field(out, cap, &at, c[i].value) &&
                 put_field(out, cap, &at, c[i].lo) && put_field(out, cap, &at, c[i].hi);
        else if (ok)
            ok = put_field(out, cap, &at, c[i].value);
        if (!ok || !put(out, cap, &at, "\n")) return -1;
    }
    return (int32_t) at;
}

/* A plain decimal of 1 to 18 digits at s[*at], a "-" allowed before it, no
 * leading zero and no "-0", ended by `end`. */
static bool get_num(const char *s, uint32_t n, uint32_t *at, char end, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    bool neg = i < n && s[i] == '-';
    int64_t x = 0;
    if (neg) i++;
    uint32_t first = i;
    while (i < n && s[i] >= '0' && s[i] <= '9' && digits < 18u) {
        x = x * 10 + (s[i] - '0');
        i++;
        digits++;
    }
    if (!digits || i >= n || s[i] != end) return false; /* a 19th digit is no end */
    if (s[first] == '0' && (digits > 1u || neg)) return false;
    *v = neg ? -x : x;
    *at = i + 1u;
    return true;
}

/* Parse text into c (FM1-FM3): false when it is not a sheet. */
static bool parse(int32_t sheet, furrow_cell_t *c, uint32_t *count, const char *s, uint32_t n)
{
    uint32_t at = (uint32_t) strlen(HEAD), k = 0;
    if (n < at || memcmp(s, HEAD, at)) return false;
    while (at < n) {
        int64_t row, col, kind, v[4] = {0, 0, 0, 0};
        if (k >= FURROW_CELLS || !get_num(s, n, &at, ' ', &row) || !get_num(s, n, &at, ' ', &col) ||
            !get_num(s, n, &at, ' ', &kind))
            return false;
        if (kind == FURROW_FORMULA) {
            if (!get_num(s, n, &at, ' ', &v[0]) || !get_num(s, n, &at, ' ', &v[1]) ||
                !get_num(s, n, &at, ' ', &v[2]) || !get_num(s, n, &at, '\n', &v[3]))
                return false;
        } else if (!get_num(s, n, &at, '\n', &v[1])) {
            return false;
        }
        /* a place past the sheet would not fit int32: refuse it before narrowing */
        if (!place_ok(row, col) || v[0] < 0 || v[0] > 2 || v[2] < 0 || v[2] > FURROW_ROW_MAX ||
            v[3] < 0 || v[3] > FURROW_ROW_MAX)
            return false;
        furrow_cell_t cell = {.value = v[1],
                              .row = (int32_t) row,
                              .col = (int32_t) col,
                              .kind = (int32_t) kind,
                              .dir = (int32_t) v[0],
                              .lo = (int32_t) v[2],
                              .hi = (int32_t) v[3]};
        if (kind < FURROW_NUMBER || kind > FURROW_FORMULA || !cell_ok(sheet, &cell)) return false;
        if (k && (c[k - 1u].row > row || (c[k - 1u].row == row && c[k - 1u].col >= col)))
            return false; /* FM2: in order, each cell once */
        c[k++] = cell;
    }
    *count = k;
    return true;
}

/* ---- zxvd ---------------------------------------------------------------- */

/* One request's text: whole lines of at most this many bytes. Each byte
 * escapes to at most two ("\n"), well inside the request line. */
#define PIECE 6000u

static int write_text(zxm_link_t *l, const char *path, const char *text, uint32_t len)
{
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at;
        if (n > PIECE) {
            n = PIECE;
            while (n && text[at + n - 1u] != '\n') n--;
            if (!n) return ZXM_E_TOOBIG;
        }
        bool last = at + n >= len;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", path);
        zxm_p_strn(l, "text", text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

static furrow_cell_t NEXT[FURROW_CELLS]; /* a change before it is written (FM4) */

/* Write NEXT (n cells) whole, then keep it (FM4). */
static int commit(furrow_t *f, uint32_t n)
{
    static char text[FURROW_TEXT_MAX];
    int32_t len = compose(NEXT, n, text, sizeof text);
    if (len < 0) return ZXM_E_TOOBIG;
    int rc = write_text(f->link, f->path, text, (uint32_t) len);
    if (rc) return rc;
    memcpy(f->c, NEXT, (size_t) n * sizeof f->c[0]);
    f->n = n;
    return ZXM_OK;
}

int furrow_init(furrow_t *f, zxm_link_t *link, const char *path, int64_t sheet)
{
    if (!f) return ZXM_E_ARG;
    memset(f, 0, sizeof *f);
    f->link = link;
    if (!path || sheet < 1 || sheet >= FURROW_SHEET_DIG) return ZXM_E_ARG;
    size_t n = strlen(path);
    if (!n || n > FURROW_PATH_MAX || strstr(path, "..")) return ZXM_E_ARG;
    memcpy(f->path, path, n + 1u);
    f->sheet = (int32_t) sheet;
    return ZXM_OK;
}

int furrow_load(furrow_t *f)
{
    static char text[FURROW_TEXT_MAX];
    static char chunk[FURROW_TEXT_MAX];
    static furrow_cell_t got[FURROW_CELLS];
    zxm_link_t *l = f->link;
    uint32_t len = 0, n = 0;
    uint64_t off = 0;
    if (!f->path[0]) return ZXM_E_ARG;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", f->path);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (rc == ZXM_E_REMOTE && l->err_code == FURROW_E_FILES && !off &&
            strstr(l->err, "no such file")) {
            f->n = 0; /* not made yet: an empty sheet */
            f->loaded = true;
            return ZXM_OK;
        }
        if (rc) return rc;
        uint64_t size, next_off;
        bool eof;
        int32_t t = zxm_get(l, l->result, "text");
        if (zxm_get_u64(l, l->result, "size", &size) ||
            zxm_get_u64(l, l->result, "next", &next_off) ||
            zxm_get_bool(l, l->result, "eof", &eof) || t < 0)
            return ZXM_E_PROTOCOL;
        if (size >= FURROW_TEXT_MAX) return ZXM_E_TOOBIG;
        int32_t k = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (k < 0 || len + (uint32_t) k >= FURROW_TEXT_MAX) return ZXM_E_TOOBIG;
        memcpy(text + len, chunk, (size_t) k);
        len += (uint32_t) k;
        if (eof) break;
        if (next_off <= off) return ZXM_E_PROTOCOL;
        off = next_off;
    }
    if (!parse(f->sheet, got, &n, text, len)) {
        f->loaded = false; /* FM3: never overwritten */
        return ZXM_E_PROTOCOL;
    }
    memcpy(f->c, got, (size_t) n * sizeof f->c[0]);
    f->n = n;
    f->loaded = true;
    return ZXM_OK;
}

/* Put cell into the sheet, replacing what was at its place (FM4). */
static int store(furrow_t *f, const furrow_cell_t *cell)
{
    if (!cell_ok(f->sheet, cell)) return ZXM_E_ARG;
    if (!f->loaded) return ZXM_E_STATE;
    uint32_t i = find(f->c, f->n, cell->row, cell->col), n = f->n;
    memcpy(NEXT, f->c, (size_t) n * sizeof NEXT[0]);
    if (here(NEXT, n, i, cell->row, cell->col)) {
        NEXT[i] = *cell;
    } else {
        if (n >= FURROW_CELLS) return ZXM_E_TOOBIG;
        memmove(&NEXT[i + 1u], &NEXT[i], (size_t) (n - i) * sizeof NEXT[0]);
        NEXT[i] = *cell;
        n++;
    }
    return commit(f, n);
}

int furrow_set_number(furrow_t *f, int64_t row, int64_t col, int64_t value)
{
    if (!place_ok(row, col)) return ZXM_E_ARG;
    furrow_cell_t c = {
        .value = value, .row = (int32_t) row, .col = (int32_t) col, .kind = FURROW_NUMBER};
    return store(f, &c);
}

int furrow_set_money(furrow_t *f, int64_t row, int64_t col, int64_t units, int64_t cents)
{
    int64_t minor;
    if (!place_ok(row, col) || !furrow_money_cell(units, cents, &minor)) return ZXM_E_ARG;
    furrow_cell_t c = {
        .value = minor, .row = (int32_t) row, .col = (int32_t) col, .kind = FURROW_MONEY};
    return store(f, &c);
}

int furrow_set_formula(furrow_t *f, int64_t row, int64_t col, int64_t dir, int64_t line, int64_t lo,
                       int64_t hi)
{
    if (!place_ok(row, col) || (dir != FURROW_DOWN && dir != FURROW_ACROSS) || lo < 1 ||
        hi > FURROW_ROW_MAX || lo > hi)
        return ZXM_E_ARG;
    furrow_cell_t c = {.value = line,
                       .row = (int32_t) row,
                       .col = (int32_t) col,
                       .kind = FURROW_FORMULA,
                       .dir = (int32_t) dir,
                       .lo = (int32_t) lo,
                       .hi = (int32_t) hi};
    return store(f, &c);
}

int furrow_clear(furrow_t *f, int64_t row, int64_t col)
{
    if (!place_ok(row, col)) return ZXM_E_ARG;
    if (!f->loaded) return ZXM_E_STATE;
    uint32_t i = find(f->c, f->n, row, col), n = f->n;
    if (!here(f->c, n, i, row, col)) return ZXM_OK;
    memcpy(NEXT, f->c, (size_t) n * sizeof NEXT[0]);
    memmove(&NEXT[i], &NEXT[i + 1u], (size_t) (n - i - 1u) * sizeof NEXT[0]);
    return commit(f, n - 1u);
}

/* ---- evaluation (FM5) ------------------------------------------------------ */

static int eval_range(const furrow_t *f, int32_t dir, int64_t line, int64_t lo, int64_t hi,
                      int64_t depth, int64_t *refs, int64_t *total, int32_t *kind, uint32_t *held);

/* One cell's value and kind at depth (a formula evaluated one deeper). */
static int eval_cell(const furrow_t *f, const furrow_cell_t *c, int64_t depth, int64_t *refs,
                     int64_t *value, int32_t *kind)
{
    uint32_t held;
    if (c->kind != FURROW_FORMULA) {
        *value = c->value;
        *kind = c->kind;
        return ZXM_OK;
    }
    return eval_range(f, c->dir, c->value, c->lo, c->hi, depth + 1, refs, value, kind, &held);
}

/* The total of a range (range_total, FW1): each cell it holds adds its
 * value when its position lies inside lo..hi, added from the first; held
 * counts the cells it holds. */
static int eval_range(const furrow_t *f, int32_t dir, int64_t line, int64_t lo, int64_t hi,
                      int64_t depth, int64_t *refs, int64_t *total, int32_t *kind, uint32_t *held)
{
    int64_t s = 0;
    int32_t k = 0;
    *held = 0;
    if (!furrow_formula_ok(depth, *refs)) return FURROW_E_REACH;
    for (uint32_t i = 0; i < f->n; i++) {
        const furrow_cell_t *c = &f->c[i];
        int64_t pos = dir == FURROW_DOWN ? c->row : c->col;
        int64_t on = dir == FURROW_DOWN ? c->col : c->row;
        if (on != line || pos < lo || pos > hi) continue;
        *refs += 1;
        if (!furrow_formula_ok(depth, *refs)) return FURROW_E_REACH;
        int64_t v, part, pair[2];
        int32_t vk;
        int rc = eval_cell(f, c, depth, refs, &v, &vk);
        if (rc) return rc;
        if (!furrow_range_total(&pos, &lo, &hi, &v, 1u, &part)) return ZXM_E_TOOBIG;
        pair[0] = s;
        pair[1] = part;
        if (!furrow_holdings(pair, 2u, &s)) return ZXM_E_TOOBIG; /* added from the first */
        (*held)++;
        if (!vk) continue; /* a formula over nothing: 0 of no kind */
        if (k && vk != k) return FURROW_E_MIXED;
        k = vk;
    }
    *total = s;
    *kind = k; /* 0: the range holds no number and no money */
    return ZXM_OK;
}

int furrow_value(const furrow_t *f, int64_t row, int64_t col, int64_t *value, int32_t *kind)
{
    int64_t refs = 0;
    if (!value || !kind || !place_ok(row, col)) return ZXM_E_ARG;
    uint32_t i = find(f->c, f->n, row, col);
    if (!here(f->c, f->n, i, row, col)) {
        *value = 0;
        *kind = FURROW_NUMBER;
        return ZXM_OK;
    }
    int rc = eval_cell(f, &f->c[i], 0, &refs, value, kind);
    if (!rc && !*kind) *kind = FURROW_NUMBER;
    return rc;
}

static bool range_ok(int64_t dir, int64_t line, int64_t lo, int64_t hi)
{
    if (dir == FURROW_DOWN)
        return line >= 1 && line <= FURROW_COL_MAX && lo >= 1 && hi <= FURROW_ROW_MAX && lo <= hi;
    if (dir == FURROW_ACROSS)
        return line >= 1 && line <= FURROW_ROW_MAX && lo >= 1 && hi <= FURROW_COL_MAX && lo <= hi;
    return false;
}

int furrow_sum(const furrow_t *f, int64_t dir, int64_t line, int64_t lo, int64_t hi, int64_t *total,
               int32_t *kind)
{
    int64_t refs = 0;
    uint32_t held;
    if (!total || !kind || !range_ok(dir, line, lo, hi)) return ZXM_E_ARG;
    int rc = eval_range(f, (int32_t) dir, line, lo, hi, 0, &refs, total, kind, &held);
    if (!rc && !*kind) *kind = FURROW_NUMBER;
    return rc;
}

int furrow_mean(const furrow_t *f, int64_t dir, int64_t line, int64_t lo, int64_t hi, rat_t *mean)
{
    int64_t refs = 0, total;
    int32_t kind;
    uint32_t held;
    if (!mean || !range_ok(dir, line, lo, hi)) return ZXM_E_ARG;
    int rc = eval_range(f, (int32_t) dir, line, lo, hi, 0, &refs, &total, &kind, &held);
    if (rc) return rc;
    if (!held) return ZXM_E_STATE;
    rat_t m = rat_make(total, (int64_t) held);
    if (!m.valid) return ZXM_E_TOOBIG;
    *mean = m;
    return ZXM_OK;
}

int furrow_money_total(const furrow_t *f, int64_t *total)
{
    static int64_t money[FURROW_CELLS];
    uint32_t n = 0;
    if (!total) return ZXM_E_ARG;
    for (uint32_t i = 0; i < f->n; i++)
        if (f->c[i].kind == FURROW_MONEY) money[n++] = f->c[i].value;
    return furrow_holdings(money, n, total) ? ZXM_OK : ZXM_E_TOOBIG;
}

int32_t furrow_money_text(int64_t minor, char *out, uint32_t cap)
{
    uint32_t at = 0;
    if (!out || !cap) return -1;
    out[0] = 0;
    uint64_t u = minor < 0 ? (uint64_t) 0 - (uint64_t) minor : (uint64_t) minor;
    uint64_t cents = u % FURROW_SCALE;
    char frac[4] = {'.', (char) ('0' + (int) (cents / 10u)), (char) ('0' + (int) (cents % 10u)), 0};
    bool ok = (minor >= 0 || put(out, cap, &at, "-")) &&
              put_num(out, cap, &at, (int64_t) (u / FURROW_SCALE)) && put(out, cap, &at, frac);
    if (!ok) {
        out[0] = 0;
        return -1;
    }
    return (int32_t) at;
}
