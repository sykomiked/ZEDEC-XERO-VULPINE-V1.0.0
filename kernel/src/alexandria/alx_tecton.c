/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_tecton.c — TECTON, a plan of blocks checked before it runs (alx.h).
 *
 *   plan  = "BIND" name "{" "SOURCE" ":" ref { "PIPE" ":" ref }
 *           [ "SINK" ":" ref ] "}"
 *   name  = a-z, then a-z 0-9 _ . (at most 31 bytes)
 *   ref   = a reference (alx_resolve), ending at a blank, "}" or a comment
 * Blanks and C comments may go between any two tokens. */
#include "alx_int.h"

typedef struct {
    const char *s;
    uint32_t n, i;
} lex_t;

static bool blank(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Skip blanks and comments; false on a comment with no end. */
static bool skip(lex_t *x)
{
    for (;;) {
        while (x->i < x->n && blank(x->s[x->i])) x->i++;
        if (x->i + 1u < x->n && x->s[x->i] == '/' && x->s[x->i + 1u] == '*') {
            uint32_t j = x->i + 2u;
            while (j + 1u < x->n && !(x->s[j] == '*' && x->s[j + 1u] == '/')) j++;
            if (j + 1u >= x->n) return false;
            x->i = j + 2u;
            continue;
        }
        return true;
    }
}

/* The word w here, then a token boundary. */
static bool keyword(lex_t *x, const char *w)
{
    uint32_t k = 0;
    while (w[k]) {
        if (x->i + k >= x->n || x->s[x->i + k] != w[k]) return false;
        k++;
    }
    if (x->i + k < x->n && !blank(x->s[x->i + k]) && x->s[x->i + k] != ':' &&
        x->s[x->i + k] != '{' && x->s[x->i + k] != '/')
        return false;
    x->i += k;
    return true;
}

static bool punct(lex_t *x, char c)
{
    if (!skip(x) || x->i >= x->n || x->s[x->i] != c) return false;
    x->i++;
    return true;
}

static bool ref_end(const lex_t *x, uint32_t j)
{
    return j >= x->n || blank(x->s[j]) || x->s[j] == '}' ||
           (x->s[j] == '/' && j + 1u < x->n && x->s[j + 1u] == '*');
}

int alx_tecton(const alx_t *t, const char *s, uint32_t n, alx_plan_t *p, uint32_t *at)
{
    uint32_t dummy, refat[ALX_TECTON_MAX];
    if (!at) at = &dummy;
    *at = 0;
    if (!p) return ALX_E_SYNTAX;
    p->n = 0;
    p->name[0] = 0;
    if (!t || !t->tbl) return ALX_E_SLOT;
    if (!s && n) return ALX_E_SYNTAX;
    lex_t x = {s, n, 0};
#define BAD(st)                                                                                    \
    do {                                                                                           \
        *at = x.i;                                                                                 \
        p->n = 0;                                                                                  \
        return (st);                                                                               \
    } while (0)
    if (!skip(&x) || !keyword(&x, "BIND")) BAD(ALX_E_SYNTAX);
    if (!skip(&x)) BAD(ALX_E_SYNTAX);
    uint32_t k = 0;
    while (x.i < n &&
           ((s[x.i] >= 'a' && s[x.i] <= 'z') ||
            (k && ((s[x.i] >= '0' && s[x.i] <= '9') || s[x.i] == '_' || s[x.i] == '.')))) {
        if (k == 31) BAD(ALX_E_LIMIT);
        p->name[k++] = s[x.i++];
    }
    p->name[k] = 0;
    if (!k || p->name[k - 1u] == '.') BAD(ALX_E_SYNTAX);
    if (!punct(&x, '{')) BAD(ALX_E_SYNTAX);
    int stage = 0; /* 0 before SOURCE, 1 after SOURCE or a PIPE, 2 after SINK */
    for (;;) {
        if (!skip(&x)) BAD(ALX_E_SYNTAX);
        if (x.i < n && s[x.i] == '}') {
            if (stage == 0) BAD(ALX_E_SYNTAX);
            x.i++;
            break;
        }
        if (stage == 2) BAD(ALX_E_SYNTAX);
        if (stage == 0 ? !keyword(&x, "SOURCE")
                       : !keyword(&x, "PIPE") && !(keyword(&x, "SINK") && (stage = 2)))
            BAD(ALX_E_SYNTAX);
        if (stage == 0) stage = 1;
        if (!punct(&x, ':') || !skip(&x)) BAD(ALX_E_SYNTAX);
        uint32_t st = x.i;
        while (!ref_end(&x, x.i)) x.i++;
        if (x.i == st) BAD(ALX_E_SYNTAX);
        if (p->n == ALX_TECTON_MAX) {
            x.i = st;
            BAD(ALX_E_LIMIT);
        }
        alx_ref_t r;
        int rs = alx_resolve(t, s + st, x.i - st, &r);
        if (rs) {
            x.i = st;
            BAD(rs);
        }
        refat[p->n] = st;
        p->slot[p->n] = r.slot;
        p->language[p->n++] = r.language;
    }
    if (!skip(&x) || x.i != n) BAD(ALX_E_SYNTAX);
    uint32_t link;
    int ds = alx_pipeline(t, p->slot, p->n, &link);
    if (ds) {
        x.i = refat[link];
        BAD(ds);
    }
#undef BAD
    return ALX_OK;
}
