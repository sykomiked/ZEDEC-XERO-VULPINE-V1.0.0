/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zkron_gnuapl.cc — KRON as a GNU APL native function.
 *
 *       'libzkron_apl.so' ⎕FX 'KRON'
 *       KRON '+/1r3 1r6 1r2'          ⍝ 1        (whole numbers: an integer array)
 *       KRON '2 3⍴⍳6'                 ⍝ 0 1 2 / 3 4 5
 *       KRON '1 2 3<2'                ⍝ 1 0 0    (true/false: booleans)
 *       0 KRON '+/1r3 1r6'            ⍝ 1r2      the value text
 *       1 KRON '2 3⍴1r2'              ⍝ display text, one row per line
 *       2 KRON 'x←1 ⋄ x+1'            ⍝ the canonical program text
 *       3 KRON '1+1'                  ⍝ the SHA3-256 digest, 64 hex chars
 *       4 KRON '1r3 2'                ⍝ 1 2      numerators, shaped like the value
 *       5 KRON '1r3 2'                ⍝ 3 1      denominators (0 for truth values,
 *                                     ⍝          whose numerators are the libzxv codes)
 *       6 KRON '1÷0'                  ⍝ the refusal as text, never an error:
 *                                     ⍝ 'line 1, column 2: ... [KR_E_DIVZERO]',
 *                                     ⍝ or '' when the program runs
 *
 * B is a character vector (or scalar) in the APL spelling of KRON (⎕IO 0,
 * glyphs), or canonical KRON when it starts with the word kron. It is
 * always read as APL here, even when it is pure ASCII: the AUTO rule of
 * kr_parse would read ASCII-only text as K.
 *
 * Monadic KRON gives integers only when every element is whole and
 * booleans only when every truth value is true or false; otherwise it is
 * a DOMAIN ERROR that says which element (use 4 KRON and 5 KRON). A KRON
 * refusal is the matching APL error (LENGTH, RANK, INDEX, SYNTAX, LIMIT,
 * else DOMAIN) and )MORE shows KRON's diagnostic with line, column and
 * status (6 KRON B gives the same text without raising, for ⎕EA-free
 * checks). Never a float (R11).
 *
 * GNU APL's headers are GPLv3 and are not copied here; they are included
 * from a configured GNU APL source tree at build time (GNUAPL_SRC). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "Native_interface.hh"
#include "Value.hh"

#include "zkron.h"

class NativeFunction;

extern "C" void *get_function_mux(const char *function_name);
static Fun_signature get_signature() { return SIG_Z_A_F2_B; }

namespace {

/* The program text of B, as UTF-8 bytes. */
UTF8_string program_text(Value_P B) {
    if (B->get_rank() > 1) {
        MORE_ERROR() << "KRON: the program is a character vector";
        RANK_ERROR;
    }
    const ShapeItem n = B->element_count();
    for (ShapeItem i = 0; i < n; i++) {
        if (!B->get_cravel(i).is_character_cell()) {
            MORE_ERROR() << "KRON: the program is a character vector";
            DOMAIN_ERROR;
        }
    }
    const UCS_string ucs(*B);
    return UTF8_string(ucs);
}

int32_t spelling_of(const UTF8_string &s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
    if (s.compare(i, 4, "kron") == 0 &&
        (i + 4 == s.size() || s[i + 4] == ' ' || s[i + 4] == '\n' || s[i + 4] == '\t' || s[i + 4] == '\r'))
        return KR_SPELL_CANON;
    return KR_SPELL_APL;
}

[[noreturn]] void refuse(int32_t st, const char *msg) {
    MORE_ERROR() << "KRON: " << msg;
    switch (st) {
    case KR_E_LENGTH: LENGTH_ERROR;
    case KR_E_RANK: RANK_ERROR;
    case KR_E_INDEX: INDEX_ERROR;
    case KR_E_SYNTAX: SYNTAX_ERROR;
    default: DOMAIN_ERROR;
    }
}

/* Owns a parsed program and its value. */
struct Run {
    kr_program *p = nullptr;
    kr_value *v = nullptr;
    ~Run() {
        kr_value_destroy(v);
        kr_program_destroy(p);
    }
};

void parse(Run &r, const UTF8_string &s) {
    kr_diag d;
    int32_t st = kr_parse(s.data(), s.size(), spelling_of(s), &r.p, &d);
    if (st != ZXV_OK) refuse(st, d.msg[0] ? d.msg : kr_status_name(st));
}

void eval(Run &r) {
    kr_diag d;
    int32_t st = kr_eval(r.p, nullptr, 0, 0, &r.v, &d);
    if (st != ZXV_OK) refuse(st, d.msg[0] ? d.msg : kr_status_name(st));
}

Token chars(const char *s, size_t n) {
    UTF8_string utf((const UTF8 *) s, n);
    UCS_string ucs(utf);
    Value_P Z(ucs, LOC);
    Z->check_value(LOC);
    return Token(TOK_APL_VALUE1, Z);
}

/* An integer array shaped like v, from the given elements. */
Token ints(const kr_value *v, const int64_t *e) {
    int32_t kind = 0;
    uint32_t rank = 0;
    int64_t shape[2] = {0, 0};
    uint64_t n = 0;
    kr_value_info(v, &kind, &rank, shape, &n);
    if (rank == 0) return Token(TOK_APL_VALUE1, IntScalar(e[0], LOC));
    Value_P Z = rank == 1 ? Value_P(ShapeItem(shape[0]), LOC) : Value_P(ShapeItem(shape[0]), ShapeItem(shape[1]), LOC);
    for (uint64_t i = 0; i < n; i++) Z->next_ravel_Int(e[i]);
    if (n == 0) Z->set_proto_Int();
    Z->check_value(LOC);
    return Token(TOK_APL_VALUE1, Z);
}

struct Elements {
    int64_t *num = nullptr, *den = nullptr;
    ~Elements() {
        free(num);
        free(den);
    }
};

/* Numerators and denominators; truth values give their codes over 0. */
void elements(const kr_value *v, Elements &el, uint64_t &n, int32_t &kind) {
    uint32_t rank = 0;
    int64_t shape[2];
    kr_value_info(v, &kind, &rank, shape, &n);
    el.num = (int64_t *) malloc((n ? n : 1u) * sizeof(int64_t));
    el.den = (int64_t *) malloc((n ? n : 1u) * sizeof(int64_t));
    if (!el.num || !el.den) WS_FULL;
    if (kind == KR_KIND_NUM) {
        kr_value_rats(v, el.num, el.den, n);
    } else {
        int32_t *t = (int32_t *) malloc((n ? n : 1u) * sizeof(int32_t));
        if (!t) WS_FULL;
        kr_value_truths(v, t, n);
        for (uint64_t i = 0; i < n; i++) {
            el.num[i] = t[i];
            el.den[i] = 0;
        }
        free(t);
    }
}

Token text_of(const kr_value *v, int32_t (*fn)(const kr_value *, char *, size_t, size_t *)) {
    size_t need = 0;
    char one[1];
    int32_t st = fn(v, one, sizeof one, &need);
    if (st == ZXV_OK) return chars(one, need);
    if (st != ZXV_E_BUFFER) refuse(st, kr_status_name(st));
    char *b = (char *) malloc(need + 1u);
    if (!b) WS_FULL;
    st = fn(v, b, need + 1u, &need);
    if (st != ZXV_OK) {
        free(b);
        refuse(st, kr_status_name(st));
    }
    Token t = chars(b, need);
    free(b);
    return t;
}

}  // namespace

static Token eval_B(Value_P B, const NativeFunction *caller) {
    (void) caller;
    const UTF8_string s = program_text(B);
    Run r;
    Elements el;
    uint64_t n = 0;
    int32_t kind = 0;
    parse(r, s);
    eval(r);
    elements(r.v, el, n, kind);
    for (uint64_t i = 0; i < n; i++) {
        if (kind == KR_KIND_NUM && el.den[i] != 1) {
            MORE_ERROR() << "KRON: element " << ShapeItem(i) << " is " << ShapeItem(el.num[i]) << "r"
                         << ShapeItem(el.den[i]) << ", not whole; 4 KRON and 5 KRON give numerators and "
                         << "denominators";
            DOMAIN_ERROR;
        }
        if (kind == KR_KIND_TRUTH) {
            if (el.num[i] != ZXV_LP_TRUE && el.num[i] != ZXV_LP_FALSE) {
                MORE_ERROR() << "KRON: element " << ShapeItem(i) << " is "
                             << (el.num[i] == ZXV_LP_BOTH ? "both" : "neither")
                             << ", not true or false; 4 KRON gives the libzxv codes";
                DOMAIN_ERROR;
            }
            el.num[i] = el.num[i] == ZXV_LP_TRUE ? 1 : 0;
        }
    }
    return ints(r.v, el.num);
}

static Token eval_AB(Value_P A, Value_P B, const NativeFunction *caller) {
    (void) caller;
    if (!A->is_scalar_or_len1_vector() || !A->get_cfirst().is_integer_cell()) {
        MORE_ERROR() << "KRON: the left argument is 0 (text), 1 (display), 2 (program), 3 (digest), "
                     << "4 (numerators), 5 (denominators) or 6 (refusal)";
        DOMAIN_ERROR;
    }
    const APL_Integer what = A->get_cfirst().get_int_value();
    if (what < 0 || what > 6) {
        MORE_ERROR() << "KRON: the left argument is 0..6";
        DOMAIN_ERROR;
    }
    const UTF8_string s = program_text(B);
    Run r;
    if (what == 6) {
        kr_diag d;
        int32_t st = kr_parse(s.data(), s.size(), spelling_of(s), &r.p, &d);
        if (st == ZXV_OK) st = kr_eval(r.p, nullptr, 0, 0, &r.v, &d);
        if (st == ZXV_OK) return chars("", 0);
        const char *m = d.msg[0] ? d.msg : kr_status_name(st);
        return chars(m, strlen(m));
    }
    parse(r, s);
    if (what == 2) {
        size_t need = 0;
        kr_program_text(r.p, nullptr, 0, &need);
        char *b = (char *) malloc(need + 1u);
        if (!b) WS_FULL;
        /* One free on every path: DOMAIN_ERROR throws, after the buffer is gone. */
        const bool ok = kr_program_text(r.p, b, need + 1u, &need) == ZXV_OK;
        Token t = chars(ok ? b : "", ok ? need : 0);
        free(b);
        if (!ok) DOMAIN_ERROR;
        return t;
    }
    if (what == 3) {
        static const char hx[] = "0123456789abcdef";
        uint8_t d[32];
        char b[64];
        kr_program_digest(r.p, d);
        for (int i = 0; i < 32; i++) {
            b[2 * i] = hx[d[i] >> 4];
            b[2 * i + 1] = hx[d[i] & 15];
        }
        return chars(b, 64);
    }
    eval(r);
    if (what == 0) return text_of(r.v, kr_value_text);
    if (what == 1) return text_of(r.v, kr_value_display);
    {
        Elements el;
        uint64_t n = 0;
        int32_t kind = 0;
        elements(r.v, el, n, kind);
        return ints(r.v, what == 4 ? el.num : el.den);
    }
}

static Token eval_XB(Value_P X, Value_P B, const NativeFunction *caller) {
    (void) X;
    (void) B;
    (void) caller;
    MORE_ERROR() << "KRON takes no axis";
    AXIS_ERROR;
}

static Token eval_AXB(Value_P A, Value_P X, Value_P B, const NativeFunction *caller) {
    (void) A;
    (void) X;
    (void) B;
    (void) caller;
    MORE_ERROR() << "KRON takes no axis";
    AXIS_ERROR;
}

static Token eval_fill_B(Value_P B, const NativeFunction *caller) {
    (void) B;
    (void) caller;
    return Token(TOK_APL_VALUE1, IntScalar(0, LOC));
}

static Token eval_fill_AB(Value_P A, Value_P B, const NativeFunction *caller) {
    (void) A;
    (void) B;
    (void) caller;
    return Token(TOK_APL_VALUE1, IntScalar(0, LOC));
}

static Token eval_ident_Bx(Value_P B, sAxis x, const NativeFunction *caller) {
    (void) B;
    (void) x;
    (void) caller;
    MORE_ERROR() << "KRON has no identity";
    DOMAIN_ERROR;
}

static bool close_fun(Cause cause, const NativeFunction *caller) {
    (void) cause;
    (void) caller;
    return true;
}

extern "C" void *get_function_mux(const char *function_name) {
    if (!strcmp(function_name, "get_signature")) return reinterpret_cast<void *>(&get_signature);
    if (!strcmp(function_name, "eval_B")) return reinterpret_cast<void *>(&eval_B);
    if (!strcmp(function_name, "eval_AB")) return reinterpret_cast<void *>(&eval_AB);
    if (!strcmp(function_name, "eval_XB")) return reinterpret_cast<void *>(&eval_XB);
    if (!strcmp(function_name, "eval_AXB")) return reinterpret_cast<void *>(&eval_AXB);
    if (!strcmp(function_name, "eval_fill_B")) return reinterpret_cast<void *>(&eval_fill_B);
    if (!strcmp(function_name, "eval_fill_AB")) return reinterpret_cast<void *>(&eval_fill_AB);
    if (!strcmp(function_name, "eval_ident_Bx")) return reinterpret_cast<void *>(&eval_ident_Bx);
    if (!strcmp(function_name, "close_fun")) return reinterpret_cast<void *>(&close_fun);
    return 0;
}
