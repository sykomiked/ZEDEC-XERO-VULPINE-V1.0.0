/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ze_lex.c — the tokenizer both front ends share, and static expressions.
 *
 * Ada mode follows RM 2: identifiers (letters, digits, single
 * underscores), "--" comments, decimal literals with underscores and
 * exponents, based literals (16#FF#), string and character literals, and
 * the compound delimiters .. ** := => <> /= <= >=. Eve mode is the same
 * without based literals, with "#" comments, and with line ends as tokens.
 *
 * Literals are kept exact: an integer as a 128-bit value, a real as a
 * 128-bit mantissa and a decimal exponent. Nothing here rounds; a literal
 * too long to hold exactly is marked and refused where it is used.
 * Integer code only (R11). */
#include <stdlib.h>
#include <string.h>

#include "ze_internal.h"

#define LIM ((ze_i128) 1 << 120) /* magnitude limit inside expressions */

static int is_letter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static int is_digit(char c) { return c >= '0' && c <= '9'; }

static int push(ze_lexer *lx, const ze_tok *t) {
    if (lx->n == lx->cap) {
        size_t nc = lx->cap ? lx->cap * 2u : 256u;
        ze_tok *nt = (ze_tok *) realloc(lx->t, nc * sizeof *nt);
        if (!nt) return 0;
        lx->t = nt;
        lx->cap = nc;
    }
    lx->t[lx->n++] = *t;
    return 1;
}

static int lex_fail(ze_lexer *lx, int32_t status, size_t off, const char *msg) {
    ze_tok t;
    size_t n = strlen(msg);
    memset(&t, 0, sizeof t);
    t.kind = ZT_ERR;
    t.off = off;
    lx->err_status = status;
    if (n >= sizeof lx->err_msg) n = sizeof lx->err_msg - 1u;
    memcpy(lx->err_msg, msg, n);
    lx->err_msg[n] = '\0';
    return push(lx, &t) ? 0 : -1;
}

static int digit_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 99;
}

/* Digits of `base` with single underscores between them, from *pi.
 * Accumulates into *v (marking *ov past 2^100); *nd counts digits.
 * Returns 0, or -1 with *bad at the offending byte. */
static int digits(const char *s, size_t len, size_t *pi, int base, ze_i128 *v, int *ov, int *nd,
                  size_t *bad) {
    size_t i = *pi;
    int last_us = 1; /* a literal may not start with '_' */
    *nd = 0;
    while (i < len) {
        char c = s[i];
        int dv;
        if (c == '_') {
            if (last_us) {
                *bad = i;
                return -1;
            }
            last_us = 1;
            i++;
            continue;
        }
        dv = digit_val(c);
        if (dv >= base) break;
        if (!*ov) {
            *v = *v * base + dv;
            if (*v > ((ze_i128) 1 << 100)) *ov = 1;
        }
        (*nd)++;
        last_us = 0;
        i++;
    }
    if (*nd == 0 || last_us) {
        *bad = i;
        return -1;
    }
    *pi = i;
    return 0;
}

/* A numeric literal at s[*pi]. Returns 1 pushed, 0 lexical error pushed,
 * -1 out of memory. */
static int lex_number(ze_lexer *lx, size_t *pi) {
    const char *s = lx->text;
    size_t len = lx->len, i = *pi, bad = 0;
    ze_tok t;
    int nd = 0;
    memset(&t, 0, sizeof t);
    t.kind = ZT_INT;
    t.off = i;
    if (digits(s, len, &i, 10, &t.v, &t.ov, &nd, &bad) < 0)
        return lex_fail(lx, ZE_E_SYNTAX, bad, "malformed number: digits with single underscores between them");
    if (lx->mode == ZE_LEX_ADA && i < len && s[i] == '#') {
        int base = t.ov || t.v < 2 || t.v > 16 ? 0 : (int) t.v;
        if (!base) return lex_fail(lx, ZE_E_SYNTAX, t.off, "the base of a based literal must be 2 .. 16");
        t.v = 0;
        t.ov = 0;
        t.based = 1;
        i++;
        if (digits(s, len, &i, base, &t.v, &t.ov, &nd, &bad) < 0)
            return lex_fail(lx, ZE_E_SYNTAX, bad, "malformed based literal");
        if (i < len && s[i] == '.')
            return lex_fail(lx, ZE_E_UNSUPPORTED, i, "based real literals are not supported; write the value in decimal");
        if (i >= len || s[i] != '#') return lex_fail(lx, ZE_E_SYNTAX, i, "a based literal ends with '#'");
        i++;
        if (i < len && (s[i] == 'e' || s[i] == 'E')) {
            ze_i128 e = 0;
            int eov = 0;
            i++;
            if (i < len && s[i] == '+') i++;
            else if (i < len && s[i] == '-')
                return lex_fail(lx, ZE_E_SYNTAX, i, "an integer literal cannot have a negative exponent");
            if (digits(s, len, &i, 10, &e, &eov, &nd, &bad) < 0) return lex_fail(lx, ZE_E_SYNTAX, bad, "malformed exponent");
            if (eov || e > 128) t.ov = 1;
            for (ze_i128 k = 0; k < e && !t.ov; k++) {
                t.v *= base;
                if (t.v > ((ze_i128) 1 << 100)) t.ov = 1;
            }
        }
    } else {
        if (i + 1u < len && s[i] == '.' && is_digit(s[i + 1u])) {
            ze_i128 frac = 0;
            int fov = 0, fd = 0;
            i++;
            if (digits(s, len, &i, 10, &frac, &fov, &fd, &bad) < 0)
                return lex_fail(lx, ZE_E_SYNTAX, bad, "malformed number: digits with single underscores between them");
            /* the mantissa is integer part and fraction as one integer */
            t.kind = ZT_REAL;
            if (fd > 36 || fov || t.ov) t.ov = 1;
            else {
                ze_i128 m = t.v;
                for (int k = 0; k < fd; k++) m *= 10;
                t.v = m + frac;
                if (t.v > ((ze_i128) 1 << 120)) t.ov = 1;
            }
            t.exp = -fd;
        }
        if (i < len && (s[i] == 'e' || s[i] == 'E')) {
            ze_i128 e = 0;
            int eov = 0, neg = 0;
            size_t at = i;
            i++;
            if (i < len && (s[i] == '+' || s[i] == '-')) {
                neg = s[i] == '-';
                i++;
            }
            if (neg && t.kind == ZT_INT)
                return lex_fail(lx, ZE_E_SYNTAX, at, "an integer literal cannot have a negative exponent");
            if (digits(s, len, &i, 10, &e, &eov, &nd, &bad) < 0) return lex_fail(lx, ZE_E_SYNTAX, bad, "malformed exponent");
            if (eov || e > 400) t.ov = 1;
            else if (t.kind == ZT_INT) {
                for (ze_i128 k = 0; k < e && !t.ov; k++) {
                    t.v *= 10;
                    if (t.v > ((ze_i128) 1 << 100)) t.ov = 1;
                }
            } else {
                t.exp += neg ? -(int32_t) e : (int32_t) e;
            }
        }
    }
    if (i < len && (is_letter(s[i]) || is_digit(s[i]) || s[i] == '_' || s[i] == '.'))
        if (!(s[i] == '.' && i + 1u < len && s[i + 1u] == '.'))
            return lex_fail(lx, ZE_E_SYNTAX, i, "a number must be followed by a space or a delimiter");
    t.len = i - t.off;
    *pi = i;
    return push(lx, &t) ? 1 : -1;
}

int32_t ze_lex(ze_lexer *lx, const char *text, size_t len, int mode, ze_diag *d) {
    size_t i = 0;
    memset(lx, 0, sizeof *lx);
    lx->text = text;
    lx->len = len;
    lx->d = d;
    lx->mode = mode;
    for (;;) {
        ze_tok t;
        char c;
        int r;
        memset(&t, 0, sizeof t);
        while (i < len && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\f' ||
                           text[i] == '\v' || (text[i] == '\n' && mode == ZE_LEX_ADA)))
            i++;
        if (i >= len) break;
        c = text[i];
        t.off = i;
        if ((mode == ZE_LEX_ADA && c == '-' && i + 1u < len && text[i + 1u] == '-') ||
            (mode == ZE_LEX_EVE && c == '#')) {
            while (i < len && text[i] != '\n') i++;
            continue;
        }
        if (c == '\n') {
            t.kind = ZT_NL;
            t.len = 1;
            i++;
            r = push(lx, &t) ? 1 : -1;
        } else if (is_letter(c)) {
            size_t j = i;
            int us = 0;
            while (j < len && (is_letter(text[j]) || is_digit(text[j]) || text[j] == '_')) {
                if (text[j] == '_' && us) break;
                us = text[j] == '_';
                j++;
            }
            if (us || (j < len && text[j] == '_'))
                r = lex_fail(lx, ZE_E_SYNTAX, j - (us ? 1u : 0u),
                             "an identifier has single underscores between letters or digits");
            else if (j < len && ((unsigned char) text[j]) >= 0x80u)
                r = lex_fail(lx, ZE_E_SYNTAX, j, "identifiers are ASCII letters, digits and underscores");
            else if (j - i > ZE_MAX_NAME)
                r = lex_fail(lx, ZE_E_LIMIT, i, "a name is longer than 63 characters");
            else {
                t.kind = ZT_ID;
                t.len = j - i;
                i = j;
                r = push(lx, &t) ? 1 : -1;
            }
        } else if (is_digit(c)) {
            r = lex_number(lx, &i);
        } else if (c == '"') {
            size_t j = i + 1u;
            r = 2;
            for (;;) {
                if (j >= len || text[j] == '\n') {
                    r = lex_fail(lx, ZE_E_SYNTAX, i, "a string literal ends on its line with '\"'");
                    break;
                }
                if (text[j] == '"') {
                    if (j + 1u < len && text[j + 1u] == '"') {
                        j += 2u;
                        continue;
                    }
                    break;
                }
                j++;
            }
            if (r == 2) {
                t.kind = ZT_STR;
                t.len = j + 1u - i;
                i = j + 1u;
                r = push(lx, &t) ? 1 : -1;
            }
        } else if (mode == ZE_LEX_ADA && c == '\'' && i + 2u < len && text[i + 2u] == '\'' &&
                   (unsigned char) text[i + 1u] >= 0x20u && (unsigned char) text[i + 1u] < 0x7Fu) {
            t.kind = ZT_CHAR;
            t.len = 3;
            i += 3u;
            r = push(lx, &t) ? 1 : -1;
        } else {
            static const struct {
                char a, b;
                int sym;
            } two[] = {{'.', '.', ZS_DOTDOT}, {'*', '*', ZS_POW}, {':', '=', ZS_ASSIGN}, {'=', '>', ZS_ARROW},
                       {'<', '>', ZS_BOX},    {'/', '=', ZS_NE},  {'<', '=', ZS_LE},     {'>', '=', ZS_GE}};
            t.kind = ZT_SYM;
            for (size_t k = 0; k < sizeof two / sizeof two[0]; k++)
                if (c == two[k].a && i + 1u < len && text[i + 1u] == two[k].b) {
                    t.sym = two[k].sym;
                    t.len = 2;
                }
            if (!t.sym && strchr("(),;:.+-*/=<>&|'[]{}", c) && c) {
                t.sym = (unsigned char) c;
                t.len = 1;
            }
            if (!t.sym) {
                r = lex_fail(lx, ZE_E_SYNTAX, i,
                             ((unsigned char) c) >= 0x80u ? "non-ASCII text outside a comment"
                                                          : "unexpected character");
            } else {
                i += t.len;
                r = push(lx, &t) ? 1 : -1;
            }
        }
        if (r < 0) {
            ze_lex_free(lx);
            return ZXV_E_NOMEM;
        }
        if (r == 0) return ZXV_OK; /* the ZT_ERR token ends the stream */
    }
    {
        ze_tok t;
        memset(&t, 0, sizeof t);
        t.kind = ZT_EOF;
        t.off = len;
        if (!push(lx, &t)) {
            ze_lex_free(lx);
            return ZXV_E_NOMEM;
        }
    }
    return ZXV_OK;
}

void ze_lex_free(ze_lexer *lx) {
    free(lx->t);
    lx->t = NULL;
    lx->n = lx->cap = 0;
}

int32_t ze_lex_error_at(ze_lexer *lx, const ze_tok *t, const char *expected) {
    char found[96];
    if (t->kind == ZT_ERR) return ZE_ERR(lx, lx->err_status, t->off, "%s", lx->err_msg);
    switch (t->kind) {
    case ZT_EOF: strcpy(found, "the end of the text"); break;
    case ZT_NL: strcpy(found, "the end of the line"); break;
    case ZT_ID:
    case ZT_INT:
    case ZT_REAL:
    case ZT_SYM: {
        size_t n = t->len < 40u ? t->len : 40u;
        found[0] = '\'';
        memcpy(found + 1, lx->text + t->off, n);
        found[n + 1u] = '\'';
        found[n + 2u] = '\0';
        break;
    }
    case ZT_STR: strcpy(found, "a string literal"); break;
    default: strcpy(found, "a character literal"); break;
    }
    return ZE_ERR(lx, ZE_E_SYNTAX, t->off, "expected %s, found %s", expected, found);
}

/* ---- static integer expressions ---- */
static int32_t e_sum(ze_lexer *lx, ze_i128 *out);

static const ze_tok *cur(ze_lexer *lx) { return &lx->t[lx->i]; }
static int is_sym(const ze_tok *t, int s) { return t->kind == ZT_SYM && t->sym == s; }
static int is_word(ze_lexer *lx, const ze_tok *t, const char *w) {
    size_t n = strlen(w);
    if (t->kind != ZT_ID || t->len != n) return 0;
    for (size_t k = 0; k < n; k++) {
        char c = lx->text[t->off + k];
        if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
        if (c != w[k]) return 0;
    }
    return 1;
}

static int32_t too_big(ze_lexer *lx, size_t off) {
    return ZE_ERR(lx, ZE_E_CONSTRAINT, off, "the value is too large to evaluate exactly (beyond 2**120)");
}

static int32_t e_primary(ze_lexer *lx, ze_i128 *out) {
    const ze_tok *t = cur(lx);
    if (t->kind == ZT_INT) {
        if (t->ov) return too_big(lx, t->off);
        *out = t->v;
        lx->i++;
        return ZXV_OK;
    }
    if (t->kind == ZT_REAL)
        return ZE_ERR(lx, ZE_E_CONSTRAINT, t->off, "a real literal where an integer is expected");
    if (is_sym(t, '(')) {
        int32_t st;
        lx->i++;
        if ((st = e_sum(lx, out)) != ZXV_OK) return st;
        if (!is_sym(cur(lx), ')')) return ze_lex_error_at(lx, cur(lx), "')'");
        lx->i++;
        return ZXV_OK;
    }
    if (t->kind == ZT_ID)
        return ZE_ERR(lx, ZE_E_UNSUPPORTED, t->off,
                      "only literals and + - * / mod rem ** are allowed in a bound; names and attributes are not");
    if (is_sym(t, '\''))
        return ZE_ERR(lx, ZE_E_UNSUPPORTED, t->off, "attributes are not supported in a bound");
    return ze_lex_error_at(lx, t, "an integer");
}

static int32_t e_factor(ze_lexer *lx, ze_i128 *out) {
    int32_t st = e_primary(lx, out);
    if (st != ZXV_OK) return st;
    if (is_sym(cur(lx), ZS_POW)) {
        size_t at = cur(lx)->off;
        ze_i128 e, r = 1;
        lx->i++;
        if ((st = e_primary(lx, &e)) != ZXV_OK) return st;
        if (e < 0) return ZE_ERR(lx, ZE_E_CONSTRAINT, at, "a negative exponent of an integer");
        for (ze_i128 k = 0; k < e; k++) {
            ze_i128 m = *out < 0 ? -*out : *out;
            if (m != 0 && (r > LIM / m || r < -LIM / m)) return too_big(lx, at);
            r *= *out;
            if (r == 0 || r == 1) break;
            if (r == -1) {
                if (((e - k - 1) & 1) != 0) r = 1;
                break;
            }
        }
        *out = r;
        if (is_sym(cur(lx), ZS_POW))
            return ZE_ERR(lx, ZE_E_SYNTAX, cur(lx)->off, "a ** b ** c needs parentheses in Ada");
    }
    return ZXV_OK;
}

static int32_t e_term(ze_lexer *lx, ze_i128 *out) {
    int32_t st = e_factor(lx, out);
    if (st != ZXV_OK) return st;
    for (;;) {
        const ze_tok *t = cur(lx);
        int op = is_sym(t, '*') ? 1 : is_sym(t, '/') ? 2 : is_word(lx, t, "mod") ? 3 : is_word(lx, t, "rem") ? 4 : 0;
        size_t at = t->off;
        ze_i128 b;
        if (!op) return ZXV_OK;
        lx->i++;
        if ((st = e_factor(lx, &b)) != ZXV_OK) return st;
        if (op == 1) {
            if (b != 0 && (*out > LIM / (b < 0 ? -b : b) || *out < -LIM / (b < 0 ? -b : b))) return too_big(lx, at);
            *out *= b;
        } else {
            if (b == 0) return ZE_ERR(lx, ZE_E_CONSTRAINT, at, "division by zero");
            if (op == 2) *out /= b;
            else if (op == 4) *out %= b;
            else {
                ze_i128 r = *out % b;
                if (r != 0 && ((r < 0) != (b < 0))) r += b;
                *out = r;
            }
        }
    }
}

static int32_t e_sum(ze_lexer *lx, ze_i128 *out) {
    int neg = 0;
    int32_t st;
    if (is_sym(cur(lx), '+') || is_sym(cur(lx), '-')) {
        neg = is_sym(cur(lx), '-');
        lx->i++;
    }
    if ((st = e_term(lx, out)) != ZXV_OK) return st;
    if (neg) *out = -*out;
    for (;;) {
        const ze_tok *t = cur(lx);
        ze_i128 b;
        int sub;
        if (!is_sym(t, '+') && !is_sym(t, '-')) return ZXV_OK;
        sub = is_sym(t, '-');
        lx->i++;
        if ((st = e_term(lx, &b)) != ZXV_OK) return st;
        *out = sub ? *out - b : *out + b;
        if (*out > LIM || *out < -LIM) return too_big(lx, t->off);
    }
}

int32_t ze_expr(ze_lexer *lx, ze_i128 *out) { return e_sum(lx, out); }

int32_t ze_real_exact(ze_lexer *lx, const ze_tok *t, int64_t *out_num, uint32_t *out_scale) {
    ze_i128 v = t->v;
    int32_t e = t->exp;
    if (t->ov) return ZE_ERR(lx, ZE_E_CONSTRAINT, t->off, "the literal is too long to hold exactly");
    if (v == 0) {
        *out_num = 0;
        *out_scale = 0;
        return ZXV_OK;
    }
    while (v % 10 == 0 && e < 0) {
        v /= 10;
        e++;
    }
    while (e > 0) {
        v *= 10;
        e--;
        if (v > (ze_i128) INT64_MAX) break;
    }
    if (v > (ze_i128) INT64_MAX)
        return ZE_ERR(lx, ZE_E_CONSTRAINT, t->off, "the literal does not fit 64 bits exactly");
    if (-e > (int32_t) ZE_MAX_DIGITS)
        return ZE_ERR(lx, ZE_E_CONSTRAINT, t->off, "the literal needs more than 18 decimals");
    *out_num = (int64_t) v;
    *out_scale = (uint32_t) -e;
    return ZXV_OK;
}

int32_t ze_dec_bound(ze_lexer *lx, uint32_t scale, int64_t *out_units) {
    const ze_tok *t;
    int neg = 0;
    size_t at = cur(lx)->off;
    ze_i128 u;
    if (is_sym(cur(lx), '+') || is_sym(cur(lx), '-')) {
        neg = is_sym(cur(lx), '-');
        lx->i++;
    }
    t = cur(lx);
    if (t->kind == ZT_INT && lx->mode == ZE_LEX_ADA)
        return ZE_ERR(lx, ZE_E_SYNTAX, t->off,
                      "a bound of a decimal type is a real literal in Ada (write %.*s.0)", (int) t->len,
                      lx->text + t->off);
    if (t->kind == ZT_INT) {
        if (t->ov) return too_big(lx, t->off);
        u = t->v;
        for (uint32_t k = 0; k < scale; k++) {
            u *= 10;
            if (u > (ze_i128) INT64_MAX) break;
        }
    } else if (t->kind == ZT_REAL) {
        int64_t num;
        uint32_t s;
        int32_t st = ze_real_exact(lx, t, &num, &s);
        if (st != ZXV_OK) return st;
        if (s > scale)
            return ZE_ERR(lx, ZE_E_CONSTRAINT, t->off,
                          "%.*s is not a multiple of the delta (it has %u decimals, the type %u; never rounded)",
                          (int) t->len, lx->text + t->off, (unsigned) s, (unsigned) scale);
        u = num;
        for (uint32_t k = s; k < scale; k++) {
            u *= 10;
            if (u > (ze_i128) INT64_MAX) break;
        }
    } else if (t->kind == ZT_ID || is_sym(t, '(')) {
        return ZE_ERR(lx, ZE_E_UNSUPPORTED, t->off, "a decimal bound is a literal, such as -999.99");
    } else {
        return ze_lex_error_at(lx, t, "a number");
    }
    if (u > (ze_i128) INT64_MAX)
        return ZE_ERR(lx, ZE_E_CONSTRAINT, at, "the bound does not fit 64 bits in units of the delta");
    lx->i++;
    *out_units = (int64_t) (neg ? -u : u);
    return ZXV_OK;
}
