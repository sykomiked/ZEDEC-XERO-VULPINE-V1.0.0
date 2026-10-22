/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_tiamat.c — harness H1 (zxv_tiamat.h). Engine-free: the host passes
 * the function that asks one cell, so the tests run without a model. */
#include "zxv_tiamat.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#    include <windows.h>
#endif

/* ---- H1 config ---- */

/* The next token of line at *p (whitespace-separated; "..." keeps spaces).
 * 1 when one was copied, 0 at the end, -1 when it is too long or a quote
 * is not closed. */
static int next_tok(const char **p, char *out, size_t cap)
{
    const char *s = *p;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s || *s == '#') {
        *p = s;
        return 0;
    }
    size_t n = 0;
    if (*s == '"') {
        s++;
        while (*s && *s != '"') {
            if (n + 1 >= cap) return -1;
            out[n++] = *s++;
        }
        if (*s != '"') return -1;
        s++;
    } else {
        while (*s && *s != ' ' && *s != '\t') {
            if (n + 1 >= cap) return -1;
            out[n++] = *s++;
        }
    }
    out[n] = 0;
    *p = s;
    return 1;
}

static int fail(char *err, size_t cap, unsigned line, const char *why)
{
    if (err && cap) snprintf(err, cap, "line %u: %s", line, why);
    return -1;
}

static int parse_u32(const char *s, uint32_t max, uint32_t *v)
{
    if (!s[0] || strlen(s) > 3) return -1;
    uint32_t x = 0;
    for (const char *q = s; *q; q++) {
        if (*q < '0' || *q > '9') return -1;
        x = x * 10u + (uint32_t) (*q - '0');
    }
    if (x > max) return -1;
    *v = x;
    return 0;
}

static int parse_line(const char *s, unsigned ln, zxv_tiamat_conf_t *c, char *err, size_t ecap)
{
    char word[ZXV_TIAMAT_PATH];
    int r = next_tok(&s, word, sizeof word);
    if (r < 0) return fail(err, ecap, ln, "a word is too long or a quote is not closed");
    if (r == 0) return 0;
    if (!strcmp(word, "slot")) {
        char num[8];
        uint32_t n;
        if (next_tok(&s, num, sizeof num) != 1 || parse_u32(num, ZXV_TIAMAT_SLOTS - 1, &n) != 0)
            return fail(err, ecap, ln, "slot takes a number 0..11 and a path");
        r = next_tok(&s, c->slot_path[n], sizeof c->slot_path[n]);
        if (r < 0) return fail(err, ecap, ln, "the path is too long or a quote is not closed");
        if (r == 0) return fail(err, ecap, ln, "slot takes a number 0..11 and a path");
        char sha[ZXV_TIAMAT_PATH];
        r = next_tok(&s, sha, sizeof sha);
        if (r == 1) {
            size_t k = 0;
            while (sha[k] && isxdigit((unsigned char) sha[k])) k++;
            if (k != 64 || sha[k]) return fail(err, ecap, ln, "a slot's SHA-256 is 64 hex digits");
            memcpy(c->slot_sha[n], sha, 65);
        } else if (r < 0) {
            return fail(err, ecap, ln, "a slot's SHA-256 is 64 hex digits");
        }
    } else if (!strcmp(word, "log")) {
        if (next_tok(&s, c->log_path, sizeof c->log_path) != 1)
            return fail(err, ecap, ln, "log takes a path");
    } else if (!strcmp(word, "delegate")) {
        c->delegate = 1;
    } else if (!strcmp(word, "cell")) {
        if (c->n_cells >= ZXV_TIAMAT_CELLS) return fail(err, ecap, ln, "more than 16 cells");
        zxv_tiamat_cell_t *k = &c->cell[c->n_cells];
        memset(k, 0, sizeof *k);
        char num[8], ad[ZXV_TIAMAT_PATH];
        if (next_tok(&s, k->name, sizeof k->name) != 1 || next_tok(&s, num, sizeof num) != 1 ||
            parse_u32(num, ZXV_TIAMAT_SLOTS - 1, &k->slot) != 0 ||
            next_tok(&s, k->lineage, sizeof k->lineage) != 1 || next_tok(&s, ad, sizeof ad) != 1)
            return fail(err, ecap, ln, "cell takes NAME SLOT LINEAGE ADAPTER|- KEYWORD...");
        if (strcmp(ad, "-")) memcpy(k->adapter, ad, sizeof ad);
        for (uint32_t i = 0; i < c->n_cells; i++)
            if (!strcmp(c->cell[i].name, k->name)) return fail(err, ecap, ln, "a cell name twice");
        for (;;) {
            r = next_tok(&s, word, ZXV_TIAMAT_WORD);
            if (r < 0) return fail(err, ecap, ln, "a keyword is longer than 23 bytes");
            if (r == 0) break;
            if (k->n_kw >= ZXV_TIAMAT_KEYWORDS) return fail(err, ecap, ln, "more than 24 keywords");
            for (char *q = word; *q; q++) *q = (char) tolower((unsigned char) *q);
            memcpy(k->kw[k->n_kw++], word, ZXV_TIAMAT_WORD);
        }
        c->n_cells++;
    } else {
        return fail(err, ecap, ln, "unknown directive (slot, cell, log or delegate)");
    }
    if (next_tok(&s, word, sizeof word) != 0) return fail(err, ecap, ln, "extra words at the end");
    return 0;
}

int zxv_tiamat_parse(const char *text, zxv_tiamat_conf_t *c, char *err, size_t err_cap)
{
    memset(c, 0, sizeof *c);
    if (err && err_cap) err[0] = 0;
    char line[2048];
    unsigned ln = 0;
    const char *p = text;
    while (*p) {
        const char *e = p;
        while (*e && *e != '\n') e++;
        size_t n = (size_t) (e - p);
        ln++;
        if (n >= sizeof line) return fail(err, err_cap, ln, "line longer than 2047 bytes");
        memcpy(line, p, n);
        line[n] = 0;
        if (n && line[n - 1] == '\r') line[n - 1] = 0;
        if (parse_line(line, ln, c, err, err_cap) != 0) return -1;
        p = *e ? e + 1 : e;
    }
    if (!c->n_cells) return fail(err, err_cap, ln, "no cells");
    for (uint32_t i = 0; i < c->n_cells; i++)
        if (!c->slot_path[c->cell[i].slot][0]) {
            if (err && err_cap)
                snprintf(err, err_cap, "cell %s uses slot %u, which has no model", c->cell[i].name,
                         (unsigned) c->cell[i].slot);
            return -1;
        }
    return 0;
}

int zxv_tiamat_load(const char *path, zxv_tiamat_conf_t *c, char *err, size_t err_cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err && err_cap) snprintf(err, err_cap, "cannot open %s", path);
        return -1;
    }
    enum { MAX = 65536 };
    char *buf = malloc(MAX + 1);
    size_t n = buf ? fread(buf, 1, MAX + 1, f) : 0;
    fclose(f);
    if (!buf || n > MAX) {
        free(buf);
        if (err && err_cap) snprintf(err, err_cap, "%s is larger than 64 KB", path);
        return -1;
    }
    buf[n] = 0;
    int r = zxv_tiamat_parse(buf, c, err, err_cap);
    free(buf);
    return r;
}

/* ---- H2 route ---- */

/* How often w occurs in q, case-insensitively (non-overlapping). */
static uint32_t count_in(const char *q, const char *w)
{
    size_t wl = strlen(w);
    uint32_t n = 0;
    if (!wl) return 0;
    for (const char *s = q; *s;) {
        size_t i = 0;
        while (i < wl && s[i] && tolower((unsigned char) s[i]) == (unsigned char) w[i]) i++;
        if (i == wl) {
            n++;
            s += wl;
        } else {
            s++;
        }
    }
    return n;
}

static uint32_t score(const zxv_tiamat_cell_t *k, const char *q)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < k->n_kw; i++) s += count_in(q, k->kw[i]);
    return s;
}

/* the best-scoring cell of another lineage than lead, or -1 */
static int witness_for(const zxv_tiamat_conf_t *c, const char *q, uint32_t lead)
{
    int w = -1;
    uint32_t wbest = 0;
    for (uint32_t i = 0; i < c->n_cells; i++) {
        if (!strcmp(c->cell[i].lineage, c->cell[lead].lineage)) continue;
        uint32_t s = score(&c->cell[i], q);
        if (w < 0 || s > wbest) w = (int) i, wbest = s;
    }
    return w;
}

uint32_t zxv_tiamat_route(const zxv_tiamat_conf_t *c, const char *q, int *witness)
{
    uint32_t lead = 0, best = 0;
    for (uint32_t i = 0; i < c->n_cells; i++) {
        uint32_t s = score(&c->cell[i], q);
        if (s > best) best = s, lead = i;
    }
    if (witness) *witness = witness_for(c, q, lead);
    return lead;
}

/* ---- H3 witness and reckoner ---- */

static bool word_at(const char *s, const char *start, const char *w)
{
    size_t n = strlen(w);
    for (size_t i = 0; i < n; i++)
        if (toupper((unsigned char) s[i]) != (unsigned char) w[i]) return false;
    bool before = s == start || !isalnum((unsigned char) s[-1]);
    return before && !isalnum((unsigned char) s[n]);
}

int zxv_tiamat_verdict(const char *reply)
{
    static const struct {
        const char *w;
        int v;
    } W[] = {{"TRUE", ZXV_TV_TRUE},
             {"FALSE", ZXV_TV_FALSE},
             {"BOTH", ZXV_TV_BOTH},
             {"NEITHER", ZXV_TV_NEITHER}};
    for (const char *s = reply; *s; s++)
        for (size_t i = 0; i < sizeof W / sizeof W[0]; i++)
            if (word_at(s, reply, W[i].w)) return W[i].v;
    return ZXV_TV_NEITHER;
}

#define BIG 1000000000000000000LL /* operands and results stay below 10^18 */

/* A number at s (an optional '-' not after a letter or digit, digits with
 * optional ",ddd" groups, an optional ".ddd"). 1 with *v, *whole and *end
 * set; 0 when s does not start one. */
static int number_at(const char *s, const char *start, int64_t *v, bool *whole, const char **end)
{
    bool neg = false;
    const char *p = s;
    if (*p == '-' && isdigit((unsigned char) p[1]) &&
        (p == start || !isalnum((unsigned char) p[-1]))) {
        neg = true;
        p++;
    }
    if (!isdigit((unsigned char) *p) || (p > start && isdigit((unsigned char) p[-1]))) return 0;
    int64_t x = 0;
    for (;;) {
        while (isdigit((unsigned char) *p)) {
            if (x > (BIG - 9) / 10) return 0;
            x = x * 10 + (*p++ - '0');
        }
        if (p[0] == ',' && isdigit((unsigned char) p[1]) && isdigit((unsigned char) p[2]) &&
            isdigit((unsigned char) p[3]) && !isdigit((unsigned char) p[4]))
            p++;
        else
            break;
    }
    *whole = true;
    if (p[0] == '.' && isdigit((unsigned char) p[1])) {
        p++;
        while (isdigit((unsigned char) *p))
            if (*p++ != '0') *whole = false;
    }
    *v = neg ? -x : x;
    *end = p;
    return 1;
}

static const char *skip_sp(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* The operator at p: '*', '+', '-' or '/', with *end after it; 0 if none. */
static char op_at(const char *p, const char **end)
{
    static const struct {
        const char *w;
        char op;
    } O[] = {
        {"multiplied by", '*'}, {"times", '*'}, {"\xc3\x97", '*'}, {"*", '*'}, {"x", '*'},
        {"plus", '+'},          {"+", '+'},     {"minus", '-'},    {"-", '-'}, {"divided by", '/'},
        {"\xc3\xb7", '/'},      {"/", '/'}};
    for (size_t i = 0; i < sizeof O / sizeof O[0]; i++) {
        size_t n = strlen(O[i].w);
        size_t k = 0;
        while (k < n && tolower((unsigned char) p[k]) == (unsigned char) O[i].w[k]) k++;
        if (k < n) continue;
        if (isalpha((unsigned char) O[i].w[0]) && isalpha((unsigned char) p[n])) continue;
        *end = p + n;
        return O[i].op;
    }
    return 0;
}

static int compute(int64_t a, char op, int64_t b, int64_t *r)
{
    switch (op) {
    case '+':
        *r = a + b;
        break; /* |a|, |b| < 10^18: no overflow */
    case '-':
        *r = a - b;
        break;
    case '*': {
        int64_t ua = a < 0 ? -a : a, ub = b < 0 ? -b : b;
        if (ua && ub > BIG / ua) return -1;
        *r = a * b;
        break;
    }
    case '/':
        if (b == 0 || a % b) return -1; /* only whole results are checked */
        *r = a / b;
        break;
    default:
        return -1;
    }
    return (*r >= BIG || *r <= -BIG) ? -1 : 0;
}

int zxv_tiamat_reckon(const char *q, const char *answer, char *expr, size_t expr_cap)
{
    if (expr && expr_cap) expr[0] = 0;
    int64_t a = 0, b = 0, r = 0;
    char op = 0;
    bool found = false;
    for (const char *s = q; *s && !found; s++) {
        bool wa, wb;
        const char *p, *e;
        if (!number_at(s, q, &a, &wa, &p) || !wa) continue;
        p = skip_sp(p);
        op = op_at(p, &e);
        if (!op) continue;
        e = skip_sp(e);
        if (!number_at(e, q, &b, &wb, &p) || !wb) continue;
        if (compute(a, op, b, &r) == 0) found = true;
        break; /* the first "number operator number" decides */
    }
    if (!found) return -1;
    /* the answer's last number that is not an operand (an answer often
     * restates the sum after the result), unless the result is an operand */
    bool any = false, last_whole = true, cand_whole = true, have_cand = false;
    int64_t last = 0, cand = 0;
    for (const char *s = answer; *s;) {
        int64_t v;
        bool w;
        const char *e;
        if (number_at(s, answer, &v, &w, &e)) {
            any = true;
            last = v, last_whole = w;
            if (v != a && v != b) cand = v, cand_whole = w, have_cand = true;
            s = e;
        } else {
            s++;
        }
    }
    if (!any) return -1;
    int64_t got = last;
    bool got_whole = last_whole;
    if (r != a && r != b && have_cand) got = cand, got_whole = cand_whole;
    if (expr && expr_cap)
        snprintf(expr, expr_cap, "%lld %c %lld = %lld", (long long) a, op == '*' ? 'x' : op,
                 (long long) b, (long long) r);
    return got_whole && got == r ? ZXV_TV_TRUE : ZXV_TV_FALSE;
}

const char *zxv_tiamat_tv_name(int v)
{
    static const char *N[] = {"NEITHER", "TRUE", "FALSE", "BOTH"};
    return v >= 0 && v <= 3 ? N[v] : "?";
}

/* ---- H4 answer ---- */

static uint64_t now_ms(void)
{
#if defined(_WIN32)
    FILETIME f;
    GetSystemTimeAsFileTime(&f);
    uint64_t t = ((uint64_t) f.dwHighDateTime << 32) | f.dwLowDateTime; /* 100 ns since 1601 */
    return t / 10000u - 11644473600000ull;
#else
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) return 0;
    return (uint64_t) ts.tv_sec * 1000u + (uint64_t) ts.tv_nsec / 1000000u;
#endif
}

/* Copy at most n bytes of s, cut on a UTF-8 boundary, newlines as spaces. */
static void excerpt(char *out, size_t cap, const char *s, size_t n)
{
    size_t k = strlen(s);
    if (k > n) {
        k = n;
        while (k && ((unsigned char) s[k] & 0xC0) == 0x80) k--;
    }
    if (k >= cap) k = cap - 1;
    for (size_t i = 0; i < k; i++) out[i] = (s[i] == '\n' || s[i] == '\r') ? ' ' : s[i];
    out[k] = 0;
    if (k < strlen(s) && k + 4 < cap) memcpy(out + k, "...", 4);
}

static size_t put(char *out, size_t cap, size_t at, const char *fmt, const char *a, const char *b,
                  const char *c)
{
    if (at >= cap) return at;
    int n = snprintf(out + at, cap - at, fmt, a, b, c);
    return n < 0 ? at : at + (size_t) n;
}

static int check_lead(const zxv_tiamat_conf_t *c, zxv_tiamat_ask_fn ask, void *ctx, const char *q,
                      zxv_tiamat_trace_t *t, char *out, size_t cap, int *shown);

/* H2-H4 with the lead given */
static int answer_lead(const zxv_tiamat_conf_t *c, zxv_tiamat_ask_fn ask, void *ctx, const char *q,
                       uint32_t lead_cell, zxv_tiamat_trace_t *t, char *out, size_t cap, int *shown)
{
    memset(t, 0, sizeof *t);
    *shown = 0;
    out[0] = 0;
    t->lead = lead_cell;
    t->witness = witness_for(c, q, lead_cell);
    t->witness_tv = t->reckon_tv = -1;
    const char *lead = c->cell[t->lead].name;

    uint64_t t0 = now_ms();
    t->lead_rc = ask(ctx, t->lead, q, t->lead_reply, sizeof t->lead_reply);
    t->ms_lead = now_ms() - t0;
    if (t->lead_rc == ZXV_TIAMAT_INTEGRITY) {
        t->tv = ZXV_TV_BOTH;
        snprintf(out, cap, "[Tiamat] Held: %s's answer failed the engine's integrity check (%s).",
                 lead, t->lead_reply);
        return t->tv;
    }
    if (t->lead_rc != 1) {
        t->tv = ZXV_TV_NEITHER;
        snprintf(out, cap, "[Tiamat] %s could not answer: %s", lead, t->lead_reply);
        return t->tv;
    }
    return check_lead(c, ask, ctx, q, t, out, cap, shown);
}

int zxv_tiamat_answer(const zxv_tiamat_conf_t *c, zxv_tiamat_ask_fn ask, void *ctx, const char *q,
                      zxv_tiamat_trace_t *t, char *out, size_t cap, int *shown)
{
    return answer_lead(c, ask, ctx, q, zxv_tiamat_route(c, q, NULL), t, out, cap, shown);
}

int zxv_tiamat_check(const zxv_tiamat_conf_t *c, zxv_tiamat_ask_fn ask, void *ctx, const char *q,
                     const char *answer, zxv_tiamat_trace_t *t, char *out, size_t cap, int *shown)
{
    memset(t, 0, sizeof *t);
    *shown = 0;
    out[0] = 0;
    t->lead = zxv_tiamat_route(c, q, &t->witness);
    t->witness_tv = t->reckon_tv = -1;
    t->lead_rc = 1;
    snprintf(t->lead_reply, sizeof t->lead_reply, "%s", answer);
    return check_lead(c, ask, ctx, q, t, out, cap, shown);
}

/* H3-H4 on the lead's reply in t: the reckoner or the witness, the join and
 * what the user sees */
static int check_lead(const zxv_tiamat_conf_t *c, zxv_tiamat_ask_fn ask, void *ctx, const char *q,
                      zxv_tiamat_trace_t *t, char *out, size_t cap, int *shown)
{
    const char *lead = c->cell[t->lead].name;
    uint64_t t0;
    int tv = ZXV_TV_NEITHER;
    t->reckon_tv = zxv_tiamat_reckon(q, t->lead_reply, t->reckon_expr, sizeof t->reckon_expr);
    if (t->reckon_tv >= 0) tv = zxv_tiamat_join(tv, t->reckon_tv);
    /* an exact result is a proof: a model's opinion is not asked against it */
    if (t->reckon_tv >= 0) t->witness_skipped = 1;
    if (t->witness >= 0 && !t->witness_skipped) {
        char ans[1600], qx[500], *prompt = malloc(ZXV_TIAMAT_REPLY);
        excerpt(ans, sizeof ans, t->lead_reply, 1200);
        excerpt(qx, sizeof qx, q, 400);
        if (!prompt) {
            t->witness_rc = 0;
            snprintf(t->witness_reply, sizeof t->witness_reply, "out of memory");
        } else {
            /* one statement holding question and answer: the form small
             * witnesses judged best in the H1 trial (tools/tiamat/TEST_PLAN.md) */
            snprintf(prompt, ZXV_TIAMAT_REPLY,
                     "Is the following statement true or false?\n"
                     "\"The answer to the question '%s' is: %s\"\n"
                     "Answer with one word: true or false.",
                     qx, ans);
            t0 = now_ms();
            t->witness_rc =
                ask(ctx, (uint32_t) t->witness, prompt, t->witness_reply, sizeof t->witness_reply);
            t->ms_witness = now_ms() - t0;
            free(prompt);
        }
        if (t->witness_rc == 1) {
            t->witness_tv = zxv_tiamat_verdict(t->witness_reply);
            tv = zxv_tiamat_join(tv, t->witness_tv);
        }
    }
    t->tv = tv;

    const char *wname = t->witness >= 0 ? c->cell[t->witness].name : "";
    char why[400], line[200];
    excerpt(why, sizeof why, t->witness_reply, 300);
    line[0] = 0;
    if (t->reckon_tv >= 0)
        snprintf(line, sizeof line, "exact arithmetic: %s, so the answer is %s", t->reckon_expr,
                 t->reckon_tv == ZXV_TV_TRUE ? "right (TRUE)" : "wrong (FALSE)");

    size_t at = 0;
    if (tv == ZXV_TV_TRUE || tv == ZXV_TV_NEITHER) {
        *shown = 1;
        char foot[900];
        at = put(foot, sizeof foot, 0, "[Tiamat] Answered by %s", lead, "", "");
        if (tv == ZXV_TV_TRUE) {
            if (t->witness_tv == ZXV_TV_TRUE)
                at = put(foot, sizeof foot, at, ", checked by %s (TRUE)", wname, "", "");
            if (t->reckon_tv == ZXV_TV_TRUE) at = put(foot, sizeof foot, at, "; %s", line, "", "");
            if (t->witness >= 0 && !t->witness_skipped && t->witness_tv != ZXV_TV_TRUE)
                at = put(foot, sizeof foot, at,
                         t->witness_rc == 1 ? "; %s could not tell (NEITHER)"
                                            : "; %s could not answer",
                         wname, "", "");
            at = put(foot, sizeof foot, at, ".", "", "", "");
        } else if (t->witness < 0) {
            at = put(foot, sizeof foot, at, ". Not checked: no cell of another lineage is loaded.",
                     "", "", "");
        } else if (t->witness_rc != 1) {
            at = put(foot, sizeof foot, at, ". Not checked: %s could not answer (%s).", wname, why,
                     "");
        } else {
            at = put(foot, sizeof foot, at, ". Not checked: %s could not tell (NEITHER).", wname,
                     "", "");
        }
        /* the reply, cut if it must be so the verdict line always fits */
        size_t fl = strlen(foot), room = cap > fl + 8 ? cap - fl - 8 : 0;
        size_t rl = strlen(t->lead_reply);
        if (rl > room) {
            rl = room;
            while (rl && ((unsigned char) t->lead_reply[rl] & 0xC0) == 0x80) rl--;
        }
        if (cap > fl + 8) {
            memcpy(out, t->lead_reply, rl);
            snprintf(out + rl, cap - rl, "%s\n\n%s", rl < strlen(t->lead_reply) ? "..." : "", foot);
        } else {
            snprintf(out, cap, "%s", foot);
        }
        return tv;
    }
    if (t->reckon_tv == ZXV_TV_FALSE) {
        /* H4: the exact result is the answer; the lead's wrong draft stays held */
        t->exact_shown = 1;
        at = put(out, cap, at, "%s (exact arithmetic)\n\n", t->reckon_expr, "", "");
    }
    at = put(out, cap, at, "[Tiamat] Held: %s's answer is not shown because the check %s.", lead,
             tv == ZXV_TV_FALSE ? "found it wrong (FALSE)"
                                : "found evidence both for and against it (BOTH)",
             "");
    if (t->witness_rc == 1)
        at = put(out, cap, at, "\n- %s said %s: %s", wname, zxv_tiamat_tv_name(t->witness_tv), why);
    if (t->reckon_tv >= 0) at = put(out, cap, at, "\n- %s", line, "", "");
    at = put(out, cap, at, "\nThe draft is kept in the Tiamat log%s%s.", c->log_path[0] ? " " : "",
             c->log_path[0] ? c->log_path : "", "");
    (void) at;
    return tv;
}

/* ---- H5 log ---- */

static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *) s; *p; p++) {
        if (*p == '"' || *p == '\\')
            fprintf(f, "\\%c", *p);
        else if (*p == '\n')
            fputs("\\n", f);
        else if (*p < 0x20)
            fprintf(f, "\\u%04x", *p);
        else
            fputc(*p, f);
    }
    fputc('"', f);
}

int zxv_tiamat_log(const char *path, const char *q, const zxv_tiamat_conf_t *c,
                   const zxv_tiamat_trace_t *t)
{
    if (!path || !path[0]) return 0;
    FILE *f = fopen(path, "ab");
    if (!f) return -1;
    zxv_tiamat_write(f, q, c, t, NULL);
    return fclose(f) == 0 ? 0 : -1;
}

/* the trace's fields, from ',"lead"' to ',"exact_shown":...' */
static void write_trace(FILE *f, const zxv_tiamat_conf_t *c, const zxv_tiamat_trace_t *t)
{
    fputs(",\"lead\":", f);
    json_str(f, c->cell[t->lead].name);
    fprintf(f, ",\"lead_rc\":%d,\"lead_ms\":%llu,\"lead_reply\":", t->lead_rc,
            (unsigned long long) t->ms_lead);
    json_str(f, t->lead_reply);
    fputs(",\"witness\":", f);
    if (t->witness >= 0)
        json_str(f, c->cell[t->witness].name);
    else
        fputs("null", f);
    fprintf(f, ",\"witness_rc\":%d,\"witness_ms\":%llu,\"witness_reply\":", t->witness_rc,
            (unsigned long long) t->ms_witness);
    json_str(f, t->witness_reply);
    fputs(",\"witness_verdict\":", f);
    if (t->witness_tv >= 0)
        json_str(f, zxv_tiamat_tv_name(t->witness_tv));
    else
        fputs("null", f);
    fputs(",\"reckon\":", f);
    if (t->reckon_tv >= 0) {
        fputs("{\"expr\":", f);
        json_str(f, t->reckon_expr);
        fputs(",\"verdict\":", f);
        json_str(f, zxv_tiamat_tv_name(t->reckon_tv));
        fputc('}', f);
    } else {
        fputs("null", f);
    }
    fputs(",\"verdict\":", f);
    json_str(f, zxv_tiamat_tv_name(t->tv));
    fprintf(f, ",\"exact_shown\":%s", t->exact_shown ? "true" : "false");
}

void zxv_tiamat_write(FILE *f, const char *q, const zxv_tiamat_conf_t *c,
                      const zxv_tiamat_trace_t *t, const char *seen)
{
    fprintf(f, "{\"time\":%llu,\"q\":", (unsigned long long) now_ms());
    json_str(f, q);
    write_trace(f, c, t);
    if (seen) {
        fputs(",\"seen\":", f);
        json_str(f, seen);
    }
    fputs("}\n", f);
}

/* ---- H6 delegate ---- */

/* The interaction circle of text: every cell with its H2 score, in config
 * order (the lead triad first); a lineage is numbered by its first cell. */
static void circle(const zxv_tiamat_conf_t *c, const char *text, swd_cand_t *k)
{
    for (uint32_t i = 0; i < c->n_cells; i++) {
        uint32_t lin = i;
        for (uint32_t j = 0; j < i; j++)
            if (!strcmp(c->cell[j].lineage, c->cell[i].lineage)) {
                lin = j;
                break;
            }
        k[i].cell = i;
        k[i].score = score(&c->cell[i], text);
        k[i].lineage = lin;
        k[i].resident = true;
    }
}

/* One part per line of the holder's reply: list marks ("1.", "2)", "-",
 * "*") and spaces trimmed, empty lines and a lone NONE skipped. Stores up
 * to ZXV_TIAMAT_PARTS; returns how many lines were parts. */
static uint32_t split_lines(const char *r, char part[][ZXV_TIAMAT_PART_Q])
{
    uint32_t n = 0;
    while (*r) {
        const char *e = r;
        while (*e && *e != '\n') e++;
        const char *a = r, *b = e, *m;
        while (a < b && isspace((unsigned char) *a)) a++;
        for (m = a; m < b && isdigit((unsigned char) *m); m++) {
        }
        if (m > a && m < b &&
            (*m == ')' || (*m == '.' && (m + 1 == b || isspace((unsigned char) m[1])))))
            a = m + 1;
        else if (a < b && (*a == '-' || *a == '*'))
            a++;
        while (a < b && isspace((unsigned char) *a)) a++;
        while (b > a && isspace((unsigned char) b[-1])) b--;
        size_t len = (size_t) (b - a);
        bool none = len == 4;
        for (size_t i = 0; none && i < 4; i++) none = tolower((unsigned char) a[i]) == "none"[i];
        if (len && !none) {
            if (n < ZXV_TIAMAT_PARTS) {
                char tmp[ZXV_TIAMAT_REPLY];
                size_t k = len < sizeof tmp ? len : sizeof tmp - 1;
                memcpy(tmp, a, k);
                tmp[k] = 0;
                excerpt(part[n], ZXV_TIAMAT_PART_Q, tmp, ZXV_TIAMAT_PART_Q - 4);
            }
            n++;
        }
        r = *e ? e + 1 : e;
    }
    return n;
}

int zxv_tiamat_delegate(const zxv_tiamat_conf_t *c, zxv_tiamat_ask_fn ask, void *ctx, const char *q,
                        zxv_tiamat_deleg_t *d, char *out, size_t cap, int *shown)
{
    swd_cand_t k[ZXV_TIAMAT_CELLS];
    uint32_t root;
    memset(d, 0, sizeof *d);
    *shown = 0;
    out[0] = 0;
    circle(c, q, k);
    swd_position(k, c->n_cells, SWD_NONE, &d->take);
    d->holder = d->take.moved ? d->take.to : zxv_tiamat_route(c, q, NULL);
    swd_init(&d->tree, 1, 0);
    swd_open(&d->tree, SWD_NONE, d->holder, 0, SWD_CONJ, &root);

    char qx[ZXV_TIAMAT_REPLY - 400], *prompt = malloc(ZXV_TIAMAT_REPLY);
    excerpt(qx, sizeof qx, q, sizeof qx - 4);
    if (prompt) {
        snprintf(prompt, ZXV_TIAMAT_REPLY,
                 "Split this question into at most 4 separate questions that can each be "
                 "answered on their own, one per line. If it is one question, reply with the "
                 "one word NONE.\nQuestion: %s",
                 qx);
        d->split_rc = ask(ctx, d->holder, prompt, d->split_reply, sizeof d->split_reply);
        free(prompt);
    } else {
        snprintf(d->split_reply, sizeof d->split_reply, "out of memory");
    }
    uint32_t lines = d->split_rc == 1 ? split_lines(d->split_reply, d->part_q) : 0;

    if (lines < 2) { /* one part: H2-H4 as without H6 */
        d->n_parts = 0;
        swd_turn(&d->tree);
        d->tv = answer_lead(c, ask, ctx, q, d->holder, &d->part[0], out, cap, &d->part_shown[0]);
        swd_witness(&d->tree, root, (lpres_state_t) d->tv);
        swd_close(&d->tree, root);
        swd_seal(&d->tree);
        d->n_conflicts = swd_conflicts(&d->tree, NULL, 0);
        *shown = d->part_shown[0];
        return d->tv;
    }

    /* outward: each part to its circle's best match */
    d->n_parts = lines < ZXV_TIAMAT_PARTS ? lines : ZXV_TIAMAT_PARTS;
    for (uint32_t i = 0; i < lines; i++) {
        uint32_t id;
        if (i < d->n_parts) {
            circle(c, d->part_q[i], k);
            swd_position(k, c->n_cells, d->holder, &d->hand[i]);
            swd_open(&d->tree, root, d->hand[i].to, 0, SWD_CONJ, &d->node[i]);
        } else {
            swd_open(&d->tree, root, d->holder, 0, SWD_CONJ, &id); /* no slot: lost */
        }
    }
    d->n_lost = d->tree.node[root].n_lost;
    swd_turn(&d->tree);

    /* inward: each part answered and checked, then the whole */
    size_t pcap = ZXV_TIAMAT_REPLY + 1024, at = 0;
    char *pout = malloc(pcap);
    for (uint32_t i = 0; i < d->n_parts; i++) {
        int pv;
        if (pout) {
            pv = answer_lead(c, ask, ctx, d->part_q[i], d->hand[i].to, &d->part[i], pout, pcap,
                             &d->part_shown[i]);
            at = put(out, cap, at, "%s %s\n",
                     i == 0   ? "1."
                     : i == 1 ? "2."
                     : i == 2 ? "3."
                              : "4.",
                     d->part_q[i], "");
            at = put(out, cap, at, "%s\n\n", pout, "", "");
        } else {
            pv = ZXV_TV_NEITHER;
        }
        swd_witness(&d->tree, d->node[i], (lpres_state_t) pv);
        swd_close(&d->tree, d->node[i]);
    }
    free(pout);
    swd_close(&d->tree, root);
    swd_seal(&d->tree);
    d->tv = d->tree.root_tv;
    d->n_conflicts = swd_conflicts(&d->tree, NULL, 0);

    char nparts[16], nlost[16];
    snprintf(nparts, sizeof nparts, "%u", (unsigned) d->n_parts);
    snprintf(nlost, sizeof nlost, "%u", (unsigned) d->n_lost);
    at = put(out, cap, at,
             "[Tiamat] %s split the question into %s parts, each answered and checked on its "
             "own; the whole holds only if every part does: %s.",
             c->cell[d->holder].name, nparts, zxv_tiamat_tv_name(d->tv));
    if (d->n_lost)
        at = put(out, cap, at, " %s further part%s not run (NEITHER).", nlost,
                 d->n_lost == 1 ? " was" : "s were", "");
    (void) at;
    *shown = d->tv == ZXV_TV_TRUE || d->tv == ZXV_TV_NEITHER;
    return d->tv;
}

static void write_hand(FILE *f, const zxv_tiamat_conf_t *c, const swd_handoff_t *h)
{
    fputs("{\"from\":", f);
    if (h->from < c->n_cells)
        json_str(f, c->cell[h->from].name);
    else
        fputs("null", f);
    fputs(",\"to\":", f);
    if (h->to < c->n_cells)
        json_str(f, c->cell[h->to].name);
    else
        fputs("null", f);
    fprintf(f,
            ",\"score_from\":%u,\"score_to\":%u,\"moved\":%s,\"claim\":", (unsigned) h->score_from,
            (unsigned) h->score_to, h->moved ? "true" : "false");
    json_str(f, zxv_tiamat_tv_name(h->tv));
    fputc('}', f);
}

void zxv_tiamat_write_deleg(FILE *f, const char *q, const zxv_tiamat_conf_t *c,
                            const zxv_tiamat_deleg_t *d, const char *seen)
{
    fprintf(f, "{\"time\":%llu,\"q\":", (unsigned long long) now_ms());
    json_str(f, q);
    fputs(",\"position\":", f);
    write_hand(f, c, &d->take);
    fputs(",\"holder\":", f);
    json_str(f, c->cell[d->holder].name);
    fprintf(f, ",\"split_rc\":%d,\"split_reply\":", d->split_rc);
    json_str(f, d->split_reply);
    fprintf(f, ",\"lost\":%u,\"parts\":[", (unsigned) d->n_lost);
    uint32_t n = d->n_parts ? d->n_parts : 1;
    for (uint32_t i = 0; i < n; i++) {
        fputs(i ? ",{\"q\":" : "{\"q\":", f);
        json_str(f, d->n_parts ? d->part_q[i] : q);
        if (d->n_parts) {
            fputs(",\"hand\":", f);
            write_hand(f, c, &d->hand[i]);
        }
        write_trace(f, c, &d->part[i]);
        fputc('}', f);
    }
    fprintf(f, "],\"conflicts\":%u,\"verdict\":", (unsigned) d->n_conflicts);
    json_str(f, zxv_tiamat_tv_name(d->tv));
    if (seen) {
        fputs(",\"seen\":", f);
        json_str(f, seen);
    }
    fputs("}\n", f);
}

int zxv_tiamat_log_deleg(const char *path, const char *q, const zxv_tiamat_conf_t *c,
                         const zxv_tiamat_deleg_t *d)
{
    if (!path || !path[0]) return 0;
    FILE *f = fopen(path, "ab");
    if (!f) return -1;
    zxv_tiamat_write_deleg(f, q, c, d, NULL);
    return fclose(f) == 0 ? 0 : -1;
}
