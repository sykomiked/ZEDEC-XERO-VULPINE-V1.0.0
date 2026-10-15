/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_axion.c — AXION, loop-free truth checks over readings (alx.h).
 *
 * The parser checks the stack's shape word by word, so a program that
 * parses can only fail at run time for a reading the caller did not give.
 * Words are separated by blanks; "#" starts a comment that runs to the end
 * of the line. */
#include "alx_int.h"

enum { OP_IN, OP_INT, OP_TRUTH, OP_LT, OP_LE, OP_EQ, OP_NOT, OP_AND, OP_OR, OP_JOIN };
enum { T_NEITHER = 0, T_TRUE = 1, T_FALSE = 2, T_BOTH = 3 };

static const struct {
    const char *w;
    uint8_t op;
    int8_t arg;                      /* -1: takes a number; else the constant */
    int8_t pop_int, pop_truth, push; /* push: 0 integer, 1 truth */
} WORDS[] = {
    {"in", OP_IN, -1, 0, 0, 0},          {"int", OP_INT, -1, 0, 0, 0},
    {"true", OP_TRUTH, T_TRUE, 0, 0, 1}, {"false", OP_TRUTH, T_FALSE, 0, 0, 1},
    {"both", OP_TRUTH, T_BOTH, 0, 0, 1}, {"neither", OP_TRUTH, T_NEITHER, 0, 0, 1},
    {"lt", OP_LT, 0, 2, 0, 1},           {"le", OP_LE, 0, 2, 0, 1},
    {"eq", OP_EQ, 0, 2, 0, 1},           {"not", OP_NOT, 0, 0, 1, 1},
    {"and", OP_AND, 0, 0, 2, 1},         {"or", OP_OR, 0, 0, 2, 1},
    {"join", OP_JOIN, 0, 0, 2, 1},
};
#define NWORDS (sizeof WORDS / sizeof WORDS[0])

static bool blank(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* A decimal integer token: true and *v, or false. */
static bool number(const char *s, uint32_t n, int64_t *v)
{
    uint32_t i = 0;
    bool neg = false;
    if (n && s[0] == '-') neg = true, i = 1;
    if (i == n || (n - i > 1 && s[i] == '0')) return false;
    uint64_t m = 0;
    for (; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        uint64_t d = (uint64_t) (s[i] - '0');
        if (m > (0x8000000000000000ull - d) / 10u) return false;
        m = m * 10u + d;
    }
    if (neg ? m > 0x8000000000000000ull : m > 0x7fffffffffffffffull) return false;
    if (neg && m == 0) return false;
    *v = neg ? (int64_t) (0u - m) : (int64_t) m;
    return true;
}

int alx_axion_parse(const char *s, uint32_t n, alx_axion_t *p, uint32_t *at)
{
    uint32_t dummy, i = 0;
    if (!at) at = &dummy;
    *at = 0;
    if (!p) return ALX_E_SYNTAX;
    p->n = 0;
    if (!s && n) return ALX_E_SYNTAX;
    /* the stack's shape: bit k set when cell k holds a truth */
    uint32_t depth = 0;
    uint64_t truths = 0;
    int64_t pending = -1; /* the word waiting for its number */
    uint32_t pending_at = 0;
    for (;;) {
        while (i < n && (blank(s[i]) || s[i] == '#')) {
            if (s[i] == '#')
                while (i < n && s[i] != '\n') i++;
            else
                i++;
        }
        if (i >= n) break;
        uint32_t st = i;
        while (i < n && !blank(s[i]) && s[i] != '#') i++;
        const char *w = s + st;
        uint32_t len = i - st;
        if (pending >= 0) {
            int64_t v;
            if (!number(w, len, &v) || (WORDS[pending].op == OP_IN && (v < 0 || v > 63))) {
                *at = st;
                p->n = 0;
                return ALX_E_SYNTAX;
            }
            p->arg[p->n - 1u] = v;
            pending = -1;
            continue;
        }
        uint32_t k = 0;
        for (; k < NWORDS; k++) {
            uint32_t j = 0;
            while (j < len && WORDS[k].w[j] && WORDS[k].w[j] == w[j]) j++;
            if (j == len && !WORDS[k].w[j]) break;
        }
        int st_err = ALX_OK;
        if (k == NWORDS)
            st_err = ALX_E_SYNTAX;
        else if (p->n == ALX_AXION_WORDS)
            st_err = ALX_E_LIMIT;
        else {
            uint32_t pops = (uint32_t) (WORDS[k].pop_int + WORDS[k].pop_truth);
            if (depth < pops) st_err = ALX_E_SYNTAX;
            for (uint32_t c = 0; !st_err && c < pops; c++) {
                bool is_truth = (truths >> (depth - 1u - c)) & 1u;
                if (is_truth != (WORDS[k].pop_truth > 0)) st_err = ALX_E_SYNTAX;
            }
            if (!st_err) {
                depth -= pops;
                truths &= (1ull << depth) - 1u;
                if (depth == ALX_AXION_STACK) st_err = ALX_E_LIMIT;
            }
            if (!st_err) {
                if (WORDS[k].push) truths |= 1ull << depth;
                depth++;
            }
        }
        if (st_err) {
            *at = st;
            p->n = 0;
            return st_err;
        }
        p->op[p->n] = WORDS[k].op;
        p->arg[p->n++] = WORDS[k].arg;
        if (WORDS[k].arg < 0) {
            pending = (int64_t) k;
            pending_at = st;
        }
    }
    if (pending >= 0 || depth != 1 || truths != 1u) {
        *at = pending >= 0 ? pending_at : n;
        p->n = 0;
        return ALX_E_SYNTAX;
    }
    return ALX_OK;
}

/* the order the gates use: FALSE < NEITHER < BOTH < TRUE */
static const uint8_t RANK[4] = {1, 3, 0, 2}, OF_RANK[4] = {T_FALSE, T_NEITHER, T_BOTH, T_TRUE};

/* The word's row in WORDS, by opcode and constant. */
static int row_of(uint8_t op, int64_t arg)
{
    for (uint32_t k = 0; k < NWORDS; k++)
        if (WORDS[k].op == op && (WORDS[k].arg < 0 || WORDS[k].arg == arg)) return (int) k;
    return -1;
}

int alx_axion_run(const alx_axion_t *p, const int64_t *readings, uint32_t nreadings,
                  uint64_t present)
{
    if (!p || p->n == 0 || p->n > ALX_AXION_WORDS) return ALX_E_SYNTAX;
    int64_t v[ALX_AXION_STACK];
    bool missing[ALX_AXION_STACK], truth[ALX_AXION_STACK];
    uint32_t sp = 0;
    for (uint32_t i = 0; i < p->n; i++) {
        /* a program built by hand is checked here as the parser checks text */
        int k = row_of(p->op[i], p->arg[i]);
        if (k < 0) return ALX_E_SYNTAX;
        uint32_t pops = (uint32_t) (WORDS[k].pop_int + WORDS[k].pop_truth);
        if (sp < pops) return ALX_E_SYNTAX;
        for (uint32_t c = 1; c <= pops; c++)
            if (truth[sp - c] != (WORDS[k].pop_truth > 0)) return ALX_E_SYNTAX;
        if (sp - pops == ALX_AXION_STACK) return ALX_E_LIMIT;
        int64_t a = p->arg[i];
        switch (p->op[i]) {
        case OP_IN:
            if (a < 0 || a > 63) return ALX_E_SYNTAX;
            if ((uint64_t) a >= nreadings || !readings) return ALX_E_LIMIT;
            v[sp] = readings[a];
            missing[sp] = !((present >> a) & 1u);
            break;
        case OP_INT:
        case OP_TRUTH:
            v[sp] = a;
            missing[sp] = false;
            break;
        case OP_LT:
        case OP_LE:
        case OP_EQ: {
            sp -= 2;
            bool gap = missing[sp] || missing[sp + 1u];
            int64_t x = v[sp], y = v[sp + 1u];
            bool r = p->op[i] == OP_LT ? x < y : p->op[i] == OP_LE ? x <= y : x == y;
            v[sp] = gap ? T_NEITHER : r ? T_TRUE : T_FALSE;
            break;
        }
        case OP_NOT:
            sp--;
            if (v[sp] == T_TRUE || v[sp] == T_FALSE) v[sp] ^= 3;
            break;
        case OP_AND:
        case OP_OR: {
            sp -= 2;
            uint8_t x = RANK[v[sp]], y = RANK[v[sp + 1u]];
            v[sp] = OF_RANK[p->op[i] == OP_AND ? (x < y ? x : y) : (x > y ? x : y)];
            break;
        }
        default: /* OP_JOIN */
            sp -= 2;
            v[sp] |= v[sp + 1u];
            break;
        }
        missing[sp] = missing[sp] && WORDS[k].push == 0;
        truth[sp++] = WORDS[k].push != 0;
    }
    return sp == 1 && truth[0] ? (int) v[0] : ALX_E_SYNTAX;
}
