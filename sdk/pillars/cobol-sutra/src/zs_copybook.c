/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zs_copybook.c — the COBOL copybook front end: a line reader for fixed
 * and free format, a tokenizer, and a parser for the supported subset of
 * data description entries (README.md, "Supported COBOL"). Integer code
 * only (R11).
 *
 *   entry   = level [ name | "FILLER" ] { clause } "."
 *   level   = 01..49 | 88 (condition name: accepted, takes no storage)
 *   clause  = ( "PIC" | "PICTURE" ) [ "IS" ] picture
 *           | [ "USAGE" [ "IS" ] ] ( "DISPLAY" | "COMP" | "COMPUTATIONAL"
 *               | "COMP-4" | "COMPUTATIONAL-4" | "BINARY"
 *               | "COMP-3" | "COMPUTATIONAL-3" | "PACKED-DECIMAL" )
 *           | "OCCURS" uint [ "TIMES" ] [ "INDEXED" [ "BY" ] name { name } ]
 *           | [ "SIGN" [ "IS" ] ] ( "LEADING" | "TRAILING" ) [ "SEPARATE" [ "CHARACTER" ] ]
 *           | ( "VALUE" | "VALUES" ) [ "IS" | "ARE" ] [ "ALL" ] literal   (ignored)
 *
 * Refused with ZS_E_UNSUPPORTED and the reason: REDEFINES, RENAMES (66),
 * 77 items, a second 01, OCCURS DEPENDING ON / TO / KEY, COMP-1, COMP-2,
 * COMP-5, SYNCHRONIZED, JUSTIFIED, BLANK WHEN ZERO, EXTERNAL, GLOBAL,
 * INDEX, POINTER, NATIONAL, continuation lines, and the picture symbols
 * zs_picture.c refuses. */
#include <stdlib.h>
#include <string.h>

#include "zs_internal.h"

enum { TK_WORD, TK_LIT, TK_PERIOD };

typedef struct {
    const char *s;
    size_t n;
    uint64_t off;
    int kind;
} tok;

typedef struct {
    const char *text;
    size_t len;
    zs_diag *d;
    tok *t;
    size_t nt, cap;
} cb;

static int push_tok(cb *c, const char *s, size_t n, int kind) {
    if (c->nt == c->cap) {
        size_t nc = c->cap ? c->cap * 2u : 256u;
        tok *nt = (tok *) realloc(c->t, nc * sizeof *nt);
        if (!nt) return 0;
        c->t = nt;
        c->cap = nc;
    }
    c->t[c->nt].s = s;
    c->t[c->nt].n = n;
    c->t[c->nt].off = (uint64_t) (s - c->text);
    c->t[c->nt].kind = kind;
    c->nt++;
    return 1;
}

#define ERR(st, off, ...) zs_diag_set(c->d, (st), c->text, c->len, (off), __VA_ARGS__)

static int is_space(char ch) { return ch == ' ' || ch == '\t'; }

/* Tokenize one code area [a, e). */
static int32_t scan_area(cb *c, size_t a, size_t e) {
    const char *x = c->text;
    size_t i = a;
    while (i < e) {
        char ch = x[i];
        if (is_space(ch)) {
            i++;
            continue;
        }
        if ((ch == ',' || ch == ';') && (i + 1u == e || is_space(x[i + 1u]))) {
            i++;
            continue;
        }
        if (ch == '*' && i + 1u < e && x[i + 1u] == '>') break; /* inline comment */
        if (ch == '\'' || ch == '"') {
            size_t j = i + 1u;
            for (;;) {
                if (j >= e)
                    return ERR(ZS_E_UNSUPPORTED, i,
                               "literal is not closed on this line (continuation lines are not "
                               "supported)");
                if (x[j] == ch) {
                    if (j + 1u < e && x[j + 1u] == ch) {
                        j += 2u;
                        continue;
                    }
                    break;
                }
                j++;
            }
            if (!push_tok(c, x + i, j + 1u - i, TK_LIT)) return ZXV_E_NOMEM;
            i = j + 1u;
            if (i < e && x[i] == '.' && (i + 1u == e || is_space(x[i + 1u]))) {
                if (!push_tok(c, x + i, 1, TK_PERIOD)) return ZXV_E_NOMEM;
                i++;
            }
            continue;
        }
        {
            size_t j = i, n;
            while (j < e && !is_space(x[j]) && x[j] != '\'' && x[j] != '"') {
                uint8_t u = (uint8_t) x[j];
                if (u < 0x21u || u > 0x7Eu)
                    return ERR(ZS_E_SYNTAX, j, "only ASCII letters, digits and punctuation may "
                                               "appear outside comments and literals");
                j++;
            }
            n = j - i;
            /* a trailing separator: "." ends the entry, "," and ";" are spaces */
            if (n > 1 && (x[j - 1u] == ',' || x[j - 1u] == ';') && (j == e || is_space(x[j]))) n--;
            if (x[i + n - 1u] == '.' && (i + n == e || is_space(x[i + n]) || i + n < j)) {
                if (n > 1 && !push_tok(c, x + i, n - 1u, TK_WORD)) return ZXV_E_NOMEM;
                if (!push_tok(c, x + i + n - 1u, 1, TK_PERIOD)) return ZXV_E_NOMEM;
            } else if (!push_tok(c, x + i, n, TK_WORD)) {
                return ZXV_E_NOMEM;
            }
            i = j;
        }
    }
    return ZXV_OK;
}

static int32_t tokenize(cb *c, uint32_t format) {
    const char *x = c->text;
    size_t pos = 0;
    while (pos < c->len) {
        size_t ls = pos, le = pos, ce;
        int32_t st;
        while (le < c->len && x[le] != '\n') le++;
        pos = le < c->len ? le + 1u : le;
        ce = le;
        if (ce > ls && x[ce - 1u] == '\r') ce--;
        if (format == ZS_CB_FREE) {
            if ((st = scan_area(c, ls, ce)) != ZXV_OK) return st;
            continue;
        }
        for (size_t k = ls; k < ce && k < ls + 7u; k++)
            if (x[k] == '\t')
                return ERR(ZS_E_SYNTAX, k,
                           "a tab in columns 1-7 of a fixed-format copybook; expand tabs to spaces "
                           "(or parse as free format)");
        if (ce - ls <= 6u) continue; /* sequence area only */
        switch (x[ls + 6u]) {
        case '*': case '/': case 'D': case 'd': continue; /* comment or debug line */
        case ' ': break;
        case '-':
            return ERR(ZS_E_UNSUPPORTED, ls + 6u, "continuation lines ('-' in column 7) are not supported");
        default:
            return ERR(ZS_E_SYNTAX, ls + 6u,
                       "column 7 must be blank, '*', '/' or 'D' in a fixed-format copybook");
        }
        for (size_t k = ls + 7u; k < ce && k < ls + 72u; k++)
            if (x[k] == '\t')
                return ERR(ZS_E_SYNTAX, k,
                           "a tab in a fixed-format copybook; expand tabs to spaces (or parse as "
                           "free format)");
        if ((st = scan_area(c, ls + 7u, ce < ls + 72u ? ce : ls + 72u)) != ZXV_OK) return st;
    }
    return ZXV_OK;
}

/* ===== parser ===== */

static int teq(const tok *t, const char *w) {
    size_t k = strlen(w);
    if (t->kind != TK_WORD || t->n != k) return 0;
    for (size_t i = 0; i < k; i++) {
        char a = t->s[i];
        if (a >= 'a' && a <= 'z') a = (char) (a - 32);
        if (a != w[i]) return 0;
    }
    return 1;
}

static int tany(const tok *t, const char *const *ws) {
    for (; *ws; ws++)
        if (teq(t, *ws)) return 1;
    return 0;
}

static const char *const k_reserved[] = {
    "PIC", "PICTURE", "USAGE", "IS", "DISPLAY", "COMP", "COMPUTATIONAL", "BINARY", "COMP-1",
    "COMP-2", "COMP-3", "COMP-4", "COMP-5", "COMP-X", "COMPUTATIONAL-1", "COMPUTATIONAL-2",
    "COMPUTATIONAL-3", "COMPUTATIONAL-4", "COMPUTATIONAL-5", "PACKED-DECIMAL", "OCCURS", "TIMES",
    "SIGN", "LEADING", "TRAILING", "SEPARATE", "CHARACTER", "VALUE", "VALUES", "REDEFINES",
    "BLANK", "JUST", "JUSTIFIED", "SYNC", "SYNCHRONIZED", "EXTERNAL", "GLOBAL", "RENAMES", "INDEX",
    "POINTER", "NATIONAL", "INDEXED", "DEPENDING", "ASCENDING", "DESCENDING", "KEY", "TO", "BY",
    "ALL", "ARE", "THRU", "THROUGH", "FLOAT-SHORT", "FLOAT-LONG", "FILLER", NULL};

int zs_reserved(const char *s, size_t n) {
    tok t;
    t.s = s;
    t.n = n;
    t.off = 0;
    t.kind = TK_WORD;
    return tany(&t, k_reserved);
}

/* 0: a usage, written to *u; 1: an unsupported usage (message in *why);
 * -1: not a usage word. */
static int usage_of(const tok *t, int32_t *u, const char **why) {
    static const char *const comp[] = {"COMP", "COMPUTATIONAL", "BINARY", "COMP-4",
                                       "COMPUTATIONAL-4", NULL};
    static const char *const comp3[] = {"COMP-3", "COMPUTATIONAL-3", "PACKED-DECIMAL", NULL};
    static const char *const flt[] = {"COMP-1", "COMP-2", "COMPUTATIONAL-1", "COMPUTATIONAL-2",
                                      "FLOAT-SHORT", "FLOAT-LONG", NULL};
    static const char *const nat[] = {"COMP-5", "COMPUTATIONAL-5", "COMP-X", NULL};
    if (teq(t, "DISPLAY")) {
        *u = ZS_U_DISPLAY;
        return 0;
    }
    if (tany(t, comp)) {
        *u = ZS_U_COMP;
        return 0;
    }
    if (tany(t, comp3)) {
        *u = ZS_U_COMP3;
        return 0;
    }
    if (tany(t, flt)) {
        *why = "floating point (COMP-1, COMP-2) is refused by design: ZXV has no floats (R11)";
        return 1;
    }
    if (tany(t, nat)) {
        *why = "native-endian binary (COMP-5, COMP-X) is not supported; use COMP (big-endian)";
        return 1;
    }
    if (teq(t, "INDEX") || teq(t, "POINTER") || teq(t, "NATIONAL")) {
        *why = "USAGE INDEX, POINTER and NATIONAL are not supported";
        return 1;
    }
    return -1;
}

static int is_number(const tok *t) {
    size_t i = 0, digits = 0;
    if (t->kind != TK_WORD) return 0;
    if (i < t->n && (t->s[i] == '+' || t->s[i] == '-')) i++;
    for (; i < t->n; i++) {
        if (t->s[i] >= '0' && t->s[i] <= '9') digits++;
        else if (t->s[i] != '.') return 0;
    }
    return digits > 0;
}

static int is_literal(const tok *t) {
    static const char *const fig[] = {"ZERO", "ZEROS", "ZEROES", "SPACE", "SPACES",
                                      "HIGH-VALUE", "HIGH-VALUES", "LOW-VALUE", "LOW-VALUES",
                                      "QUOTE", "QUOTES", NULL};
    return t->kind == TK_LIT || is_number(t) || tany(t, fig);
}

#define MAX_STACK (ZS_MAX_DEPTH + 1u)

int32_t zs_copybook_parse(const char *text, size_t len, uint32_t format, zs_layout **out_layout,
                          zs_diag *diag) {
    cb cc = {0}, *c = &cc;
    zs_builder b = {0};
    uint32_t stack[MAX_STACK], depth = 0; /* open items; stack[0] is the 01 */
    size_t k = 0;
    int32_t st = ZXV_OK;
    if (diag) memset(diag, 0, sizeof *diag);
    if (!out_layout || (!text && len) || (format != ZS_CB_FIXED && format != ZS_CB_FREE))
        return ZXV_E_ARG;
    *out_layout = NULL;
    if (!text) text = "";
    c->text = text;
    c->len = len;
    c->d = diag;
    if (len > ZS_MAX_TEXT) return ERR(ZS_E_SYNTAX, 0, "copybook is longer than 1 MiB");
    if (zs_utf8_check((const uint8_t *) text, len) < len)
        return ERR(ZS_E_SYNTAX, zs_utf8_check((const uint8_t *) text, len), "not valid UTF-8");
    if ((st = tokenize(c, format)) != ZXV_OK) goto done;
    if ((st = zs_builder_init(&b)) != ZXV_OK) goto done;

    while (k < c->nt) {
        const tok *lt = &c->t[k];
        size_t end = k, q;
        uint32_t level = 0, idx;
        zs_item it;
        int has_pic = 0, has_usage = 0, has_occurs = 0, has_sign = 0, has_value = 0;
        while (end < c->nt && c->t[end].kind != TK_PERIOD) end++;
        if (lt->kind == TK_PERIOD) {
            st = ERR(ZS_E_SYNTAX, lt->off, "expected a level number before '.'");
            goto done;
        }
        if (lt->kind != TK_WORD || lt->n > 2 || lt->s[0] < '0' || lt->s[0] > '9' ||
            (lt->n == 2 && (lt->s[1] < '0' || lt->s[1] > '9'))) {
            st = ERR(ZS_E_SYNTAX, lt->off, "expected a level number (01-49 or 88), found '%.*s'",
                     (int) (lt->n > 40 ? 40 : lt->n), lt->s);
            goto done;
        }
        level = (uint32_t) (lt->s[0] - '0');
        if (lt->n == 2) level = level * 10u + (uint32_t) (lt->s[1] - '0');
        if (end == c->nt) {
            const tok *last = &c->t[c->nt - 1u];
            st = ERR(ZS_E_SYNTAX, last->off + last->n, "missing '.' at the end of this entry");
            goto done;
        }
        if (level == 88) {
            /* condition name: 88 name VALUE[S] ... ; no storage, skipped */
            if (b.l->n == 0) {
                st = ERR(ZS_E_SYNTAX, lt->off, "an 88 condition must follow a data item");
                goto done;
            }
            if (k + 2u >= end || !zs_valid_name(c->t[k + 1u].s, c->t[k + 1u].n) ||
                !(teq(&c->t[k + 2u], "VALUE") || teq(&c->t[k + 2u], "VALUES"))) {
                st = ERR(ZS_E_SYNTAX, lt->off, "expected 88 condition-name VALUE literal ...");
                goto done;
            }
            k = end + 1u;
            continue;
        }
        if (level == 66) {
            st = ERR(ZS_E_UNSUPPORTED, lt->off, "66 RENAMES is not supported");
            goto done;
        }
        if (level == 77) {
            st = ERR(ZS_E_UNSUPPORTED, lt->off,
                     "77-level items are not part of a record; declare them under a 01");
            goto done;
        }
        if (level < 1 || level > 49) {
            st = ERR(ZS_E_SYNTAX, lt->off, "level %02u is not a level number (01-49 or 88)",
                     (unsigned) level);
            goto done;
        }
        memset(&it, 0, sizeof it);
        it.level = (int32_t) level;
        it.pos.offset = lt->off;
        if (b.l->n == 0) {
            if (level != 1) {
                st = ERR(ZS_E_UNSUPPORTED, lt->off,
                         "the copybook starts at level %02u; wrap its items in a 01 record",
                         (unsigned) level);
                goto done;
            }
        } else if (level == 1) {
            st = ERR(ZS_E_UNSUPPORTED, lt->off,
                     "a second 01 record (an implicit REDEFINES) is not supported; one record "
                     "per copybook");
            goto done;
        } else {
            const zs_item *p;
            while (depth > 0 && (uint32_t) b.l->items[stack[depth]].level >= level) depth--;
            p = &b.l->items[stack[depth]];
            if (p->f.kind != ZS_K_GROUP) {
                st = ERR(ZS_E_SYNTAX, lt->off, "%s has a PICTURE, so no items can be under it",
                         p->f.name);
                goto done;
            }
            if (p->nchild > 0) {
                /* the first child is the item right after p's last sibling run;
                 * find it: the next item whose parent is p */
                uint32_t pi = stack[depth];
                for (uint32_t i = pi + 1u; i < b.l->n; i++)
                    if (b.l->items[i].f.parent == pi) {
                        if ((uint32_t) b.l->items[i].level != level) {
                            st = ERR(ZS_E_SYNTAX, lt->off,
                                     "level %02u does not match the level of the items before it "
                                     "in %s (%02u)",
                                     (unsigned) level, p->f.name, (unsigned) b.l->items[i].level);
                            goto done;
                        }
                        break;
                    }
            }
            it.f.parent = stack[depth];
            it.f.depth = p->f.depth + 1u;
        }
        /* the name */
        q = k + 1u;
        if (q < end && c->t[q].kind == TK_WORD && !tany(&c->t[q], k_reserved)) {
            const tok *nt = &c->t[q];
            if (!zs_valid_name(nt->s, nt->n)) {
                st = ERR(ZS_E_SYNTAX, nt->off,
                         "'%.*s' is not a valid data name (1-30 letters, digits and hyphens)",
                         (int) (nt->n > 40 ? 40 : nt->n), nt->s);
                goto done;
            }
            for (size_t i = 0; i < nt->n; i++)
                it.f.name[i] = (nt->s[i] >= 'a' && nt->s[i] <= 'z') ? (char) (nt->s[i] - 32) : nt->s[i];
            q++;
        } else {
            if (q < end && teq(&c->t[q], "FILLER")) q++;
            memcpy(it.f.name, "FILLER", 7);
        }
        it.f.kind = ZS_K_GROUP;
        /* clauses */
        while (q < end) {
            const tok *w = &c->t[q];
            const char *why = NULL;
            int32_t u = 0;
            int r = -1;
            if (w->kind != TK_WORD) {
                st = ERR(ZS_E_SYNTAX, w->off, "unexpected literal");
                goto done;
            }
            if (teq(w, "PIC") || teq(w, "PICTURE")) {
                size_t at = 0;
                char msg[160];
                int32_t sg = 0;
                if (has_pic++) {
                    st = ERR(ZS_E_SYNTAX, w->off, "PICTURE given twice");
                    goto done;
                }
                q++;
                if (q < end && teq(&c->t[q], "IS")) q++;
                if (q >= end || c->t[q].kind != TK_WORD) {
                    st = ERR(ZS_E_SYNTAX, q < end ? c->t[q].off : w->off, "expected a picture string after PIC");
                    goto done;
                }
                st = zs_picture_parse(c->t[q].s, c->t[q].n, &it.f.kind, &it.f.digits, &it.f.scale,
                                      &sg, &at, msg, sizeof msg);
                if (st != ZXV_OK) {
                    st = ERR(st, c->t[q].off + at, "%s", msg);
                    goto done;
                }
                it.is_signed = sg;
                q++;
                continue;
            }
            if (teq(w, "USAGE") || (r = usage_of(w, &u, &why)) >= 0) {
                const tok *uw = w;
                if (teq(w, "USAGE")) {
                    q++;
                    if (q < end && teq(&c->t[q], "IS")) q++;
                    if (q >= end || (r = usage_of(&c->t[q], &u, &why)) < 0) {
                        st = ERR(ZS_E_SYNTAX, q < end ? c->t[q].off : w->off, "expected a usage after USAGE");
                        goto done;
                    }
                    uw = &c->t[q];
                }
                if (r == 1) {
                    st = ERR(ZS_E_UNSUPPORTED, uw->off, "%s", why);
                    goto done;
                }
                if (has_usage++) {
                    st = ERR(ZS_E_SYNTAX, uw->off, "USAGE given twice");
                    goto done;
                }
                it.f.usage = u;
                it.usage_set = 1;
                q++;
                continue;
            }
            if (teq(w, "OCCURS")) {
                uint64_t n = 0;
                const tok *nt;
                if (has_occurs++) {
                    st = ERR(ZS_E_SYNTAX, w->off, "OCCURS given twice");
                    goto done;
                }
                q++;
                nt = q < end ? &c->t[q] : w;
                if (q >= end || nt->kind != TK_WORD || nt->n > 9) n = 0;
                else
                    for (size_t i = 0; i < nt->n; i++) {
                        if (nt->s[i] < '0' || nt->s[i] > '9') {
                            n = 0;
                            break;
                        }
                        n = n * 10u + (uint64_t) (nt->s[i] - '0');
                    }
                if (n < 1 || n > ZS_MAX_OCCURS) {
                    st = ERR(ZS_E_SYNTAX, nt->off, "expected an OCCURS count 1..65535");
                    goto done;
                }
                it.f.occurs = (uint32_t) n;
                q++;
                if (q < end && teq(&c->t[q], "TO")) {
                    st = ERR(ZS_E_UNSUPPORTED, c->t[q].off,
                             "OCCURS n TO m (variable-length tables) is not supported");
                    goto done;
                }
                if (q < end && teq(&c->t[q], "TIMES")) q++;
                if (q < end && (teq(&c->t[q], "DEPENDING") || teq(&c->t[q], "ASCENDING") ||
                                teq(&c->t[q], "DESCENDING") || teq(&c->t[q], "KEY"))) {
                    st = ERR(ZS_E_UNSUPPORTED, c->t[q].off,
                             "OCCURS DEPENDING ON and KEY phrases are not supported");
                    goto done;
                }
                if (q < end && teq(&c->t[q], "INDEXED")) {
                    size_t names = 0;
                    q++;
                    if (q < end && teq(&c->t[q], "BY")) q++;
                    while (q < end && c->t[q].kind == TK_WORD && !tany(&c->t[q], k_reserved) &&
                           zs_valid_name(c->t[q].s, c->t[q].n)) {
                        q++;
                        names++;
                    }
                    if (!names) {
                        st = ERR(ZS_E_SYNTAX, q < end ? c->t[q].off : w->off,
                                 "expected an index name after INDEXED BY");
                        goto done;
                    }
                }
                continue;
            }
            if (teq(w, "SIGN") || teq(w, "LEADING") || teq(w, "TRAILING")) {
                int lead;
                if (has_sign++) {
                    st = ERR(ZS_E_SYNTAX, w->off, "SIGN given twice");
                    goto done;
                }
                if (teq(w, "SIGN")) {
                    q++;
                    if (q < end && teq(&c->t[q], "IS")) q++;
                }
                if (q >= end || !(teq(&c->t[q], "LEADING") || teq(&c->t[q], "TRAILING"))) {
                    st = ERR(ZS_E_SYNTAX, q < end ? c->t[q].off : w->off,
                             "expected LEADING or TRAILING");
                    goto done;
                }
                lead = teq(&c->t[q], "LEADING");
                q++;
                if (q < end && teq(&c->t[q], "SEPARATE")) {
                    q++;
                    if (q < end && teq(&c->t[q], "CHARACTER")) q++;
                    it.sign_clause = lead ? ZS_SIGN_LEADING_SEP : ZS_SIGN_TRAILING_SEP;
                } else {
                    it.sign_clause = lead ? ZS_SIGN_LEADING : ZS_SIGN_TRAILING;
                }
                continue;
            }
            if (teq(w, "VALUE") || teq(w, "VALUES")) {
                if (has_value++) {
                    st = ERR(ZS_E_SYNTAX, w->off, "VALUE given twice");
                    goto done;
                }
                q++;
                if (q < end && (teq(&c->t[q], "IS") || teq(&c->t[q], "ARE"))) q++;
                if (q < end && teq(&c->t[q], "ALL")) q++;
                if (q >= end || !is_literal(&c->t[q])) {
                    st = ERR(ZS_E_SYNTAX, q < end ? c->t[q].off : w->off, "expected a literal after VALUE");
                    goto done;
                }
                q++;
                continue;
            }
            if (teq(w, "REDEFINES")) {
                st = ERR(ZS_E_UNSUPPORTED, w->off,
                         "REDEFINES is not supported: every byte of a record has one meaning here");
                goto done;
            }
            if (teq(w, "SYNC") || teq(w, "SYNCHRONIZED")) {
                st = ERR(ZS_E_UNSUPPORTED, w->off, "SYNCHRONIZED (slack bytes) is not supported");
                goto done;
            }
            if (teq(w, "JUST") || teq(w, "JUSTIFIED") || teq(w, "BLANK") || teq(w, "EXTERNAL") ||
                teq(w, "GLOBAL") || teq(w, "RENAMES")) {
                st = ERR(ZS_E_UNSUPPORTED, w->off, "the %.*s clause is not supported", (int) w->n, w->s);
                goto done;
            }
            st = ERR(ZS_E_SYNTAX, w->off, "unknown clause '%.*s'", (int) (w->n > 40 ? 40 : w->n), w->s);
            goto done;
        }
        if (b.l->n > 0 && depth + 1u >= MAX_STACK) {
            st = ERR(ZS_E_SYNTAX, lt->off, "nested more than 48 levels deep");
            goto done;
        }
        if ((st = zs_builder_add(&b, &it, &idx)) != ZXV_OK) {
            if (st == ZXV_E_CAPACITY)
                st = ERR(ZS_E_UNSUPPORTED, lt->off, "more than 4096 data items");
            goto done;
        }
        if (idx == 0) depth = 0;
        else depth++;
        stack[depth] = idx;
        k = end + 1u;
    }
    st = zs_builder_finish(&b, text, len, diag, out_layout);
done:
    zs_builder_free(&b);
    free(c->t);
    return st;
}
