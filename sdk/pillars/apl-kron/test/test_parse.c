/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_parse.c — the three front ends: accepted programs with their exact
 * canonical text, APL/K equivalence by digest (and the pairs that must
 * differ), refusals with line and column, spelling detection, limits,
 * free names and the R4 buffers. */
#include "kr_test.h"

typedef struct {
    int32_t spell;
    const char *text;
    const char *canon; /* without the "kron\n" line and the final newline */
} acc_case;

#define A KR_SPELL_APL
#define K KR_SPELL_K
#define C KR_SPELL_CANON

static const acc_case k_accept[] = {
    {A, "+/⍳5", "(reduce add (iota 5))"},
    {K, "+/!5", "(reduce add (iota 5))"},
    {A, "1 2 3", "(vec 1 2 3)"},
    {A, "¯1r3", "-1r3"},
    {K, "-1r3", "-1r3"},
    {A, "0.25 0.5", "(vec 1r4 1r2)"},
    {A, "007", "7"},
    {A, "1.50", "3r2"},
    {A, "2r4", "1r2"},
    {A, "⍬", "(vec)"},
    {K, "()", "(vec)"},
    {A, "x←3", "(let x 3)"},
    {K, "x:3", "(let x 3)"},
    {A, "x+y", "(add x y)"},
    {A, "-x", "(neg x)"},
    {A, "3-1", "(sub 3 1)"},
    {A, "3 -1", "(sub 3 1)"},
    {K, "3 -1", "(vec 3 -1)"},
    {K, "3-1", "(sub 3 1)"},
    {K, "3 - 1", "(sub 3 1)"},
    {K, "x-1", "(sub x 1)"},
    {K, "(-1)", "-1"},
    {K, "-x", "(neg x)"},
    {A, "+/x", "(reduce add x)"},
    {A, "+⌿x", "(reducef add x)"},
    {K, "+/x", "(reducef add x)"},
    {K, "-/x", "(foldl sub x)"},
    {A, "-/x", "(reduce sub x)"},
    {A, "-⌿x", "(reducef sub x)"},
    {K, "-/1 2", "(foldl sub (vec 1 2))"},
    {A, "+\\x", "(scan add x)"},
    {A, "+⍀x", "(scanf add x)"},
    {K, "+\\x", "(scanf add x)"},
    {K, "-\\x", "(scanl sub x)"},
    {K, "+\\1 2", "(scan add (vec 1 2))"},
    {A, "x∘.×y", "(outer mul x y)"},
    {K, "x*\\:y", "(outer mul x y)"},
    {A, "x+.×y", "(inner add mul x y)"},
    {A, "⌈.⌊", NULL},
    {A, "x[1]", "(index x 1)"},
    {K, "x@1", "(index x 1)"},
    {K, "x[1]", "(index x 1)"},
    {A, "x[1;2]", "(index2 x 1 2)"},
    {A, "x[;2]", "(index2 x _ 2)"},
    {A, "x[1;]", "(index2 x 1 _)"},
    {A, "x[1][0]", "(index (index x 1) 0)"},
    {A, "⌽x", "(rev x)"},
    {A, "⊖x", "(revf x)"},
    {K, "|x", "(revf x)"},
    {K, "|1 2", "(rev (vec 1 2))"},
    {A, "⊖1 2", "(rev (vec 1 2))"},
    {A, "⍉x", "(flip x)"},
    {K, "+x", "(flip x)"},
    {A, "+x", "(id x)"},
    {A, "x,y", "(cat x y)"},
    {A, "x⍪y", "(catf x y)"},
    {K, "x,y", "(catf x y)"},
    {K, "1,2", "(cat 1 2)"},
    {A, "1⍪2", "(cat 1 2)"},
    {A, "2 3⍴x", "(reshape (vec 2 3) x)"},
    {K, "2 3#x", "(reshape (vec 2 3) x)"},
    {K, "#x", "(tally x)"},
    {A, "≢x", "(tally x)"},
    {A, "x≢y", "(not (match x y))"},
    {A, "x≡y", "(match x y)"},
    {K, "x~y", "(match x y)"},
    {K, "~x", "(not x)"},
    {A, "~x", "(not x)"},
    {A, "⍋x", "(gup x)"},
    {K, "<x", "(gup x)"},
    {K, ">x", "(gdown x)"},
    {K, "sort x", "(sort x)"},
    {A, "x⍳y", "(find x y)"},
    {K, "x?y", "(find x y)"},
    {A, "3↑x", "(take 3 x)"},
    {A, "3↓x", "(drop 3 x)"},
    {K, "3_x", "(drop 3 x)"},
    {K, "_x", "(floor x)"},
    {A, "2⌽x", "(rotate 2 x)"},
    {K, "2 rotate x", "(rotate 2 x)"},
    {K, "x le y", "(le x y)"},
    {K, "~x>y", "(not (gt x y))"},
    {A, "x≤y", "(le x y)"},
    {A, "x∧y", "(and x y)"},
    {K, "x&y", "(min x y)"},
    {A, "x⌊y", "(min x y)"},
    {A, "true both", "(tvec true both)"},
    {A, "neither", "neither"},
    {A, "x←1 ⋄ x", "(let x 1)\nx"},
    {K, "x:1;x", "(let x 1)\nx"},
    {K, "x:1\nx", "(let x 1)\nx"},
    {A, "x←1 ⍝ comment\nx", "(let x 1)\nx"},
    {K, "x:1 / comment\nx", "(let x 1)\nx"},
    {K, "/ whole line\n1", "1"},
    {A, "y+y←3", "(add y (let y 3))"},
    {A, "a←b←2", "(let a (let b 2))"},
    {A, "(1+2)×3", "(mul (add 1 2) 3)"},
    {A, "1+2×3", "(add 1 (mul 2 3))"},
    {A, "x[⍋x←3 1 2]", "(index x (gup (let x (vec 3 1 2))))"},
    {K, "x@<x:3 1 2", "(index x (gup (let x (vec 3 1 2))))"},
    {A, "x←1 2 ⋄ +⌿x", "(let x (vec 1 2))\n(reduce add x)"},
    {A, "x←2 2⍴1 ⋄ +⌿x", "(let x (reshape (vec 2 2) 1))\n(reducef add x)"},
    {A, "x←⍳3 ⋄ ⊖x", "(let x (iota 3))\n(rev x)"},
    {C, "kron\n(reduce add (vec 1 2))", "(reduce add (vec 1 2))"},
    {C, "kron (reducef add (vec 1 2))", "(reduce add (vec 1 2))"},
    {C, "kron\n(foldl add x)", "(reducef add x)"},
    {C, "kron\n(mat 2 2 1 2 3 4)", "(mat 2 2 1 2 3 4)"},
    {C, "kron\n(mat 0 3)", "(mat 0 3)"},
    {C, "kron\n(tmat 1 1 both)", "(tmat 1 1 both)"},
    {C, "kron\n(index2 m _ 1)", "(index2 m _ 1)"},
    {C, "kron\n(let x 1)\n(add x 1)", "(let x 1)\n(add x 1)"},
    {C, "kron\n-4r6", "-2r3"},
    {C, "kron\n(vec)", "(vec)"},
    {C, "kron\n(tvec)", "(tvec)"},
};

typedef struct {
    int32_t spell;
    const char *text;
    int32_t status;
    uint32_t line, col;
} ref_case;

static const ref_case k_refuse[] = {
    {A, "", KR_E_SYNTAX, 1, 1},
    {A, "⋄⋄", KR_E_SYNTAX, 1, 1},
    {A, "1+", KR_E_SYNTAX, 1, 2},
    {A, "(1+2", KR_E_SYNTAX, 1, 5},
    {A, "1+2)", KR_E_SYNTAX, 1, 4},
    {A, "⍺", KR_E_SYNTAX, 1, 1},
    {A, "1+⍵", KR_E_SYNTAX, 1, 3},
    {A, "*2", KR_E_SYNTAX, 1, 1},
    {A, "1 0/x", KR_E_SYNTAX, 1, 4},
    {A, "2+/x", KR_E_SYNTAX, 1, 2},
    {A, "+/\\x", KR_E_SYNTAX, 1, 3},
    {A, "⍴/x", KR_E_SYNTAX, 1, 4},
    {A, "x 2", KR_E_SYNTAX, 1, 3},
    {A, "1;2", KR_E_SYNTAX, 1, 2},
    {A, "x[]", KR_E_SYNTAX, 1, 2},
    {A, "x[1;2;3]", KR_E_RANK, 1, 2},
    {A, "←1", KR_E_SYNTAX, 1, 1},
    {A, "∘.1", KR_E_SYNTAX, 1, 1},
    {A, ".×", KR_E_SYNTAX, 1, 1},
    {A, "1e3", KR_E_SYNTAX, 1, 1},
    {A, "1E¯3", KR_E_SYNTAX, 1, 1},
    {A, "3j4", KR_E_SYNTAX, 1, 1},
    {A, "1r", KR_E_SYNTAX, 1, 1},
    {A, "1r0", KR_E_DIVZERO, 1, 1},
    {A, "¯", KR_E_SYNTAX, 1, 1},
    {A, "9223372036854775808", ZXV_E_RANGE, 1, 1},
    {A, "1r9223372036854775808", ZXV_E_RANGE, 1, 1},
    {A, "x←1\ny←", KR_E_SYNTAX, 2, 1},
    {A, "x←1\n  1 2 3 4 +", KR_E_SYNTAX, 2, 11},
    {A, "vec←1", KR_E_SYNTAX, 1, 1},
    {A, "true←1", KR_E_SYNTAX, 1, 5},
    {A, "abcdefghijklmnopqrstuvwxyzABCDEF←1", KR_E_NAME, 1, 1},
    {A, "1 true", KR_E_SYNTAX, 1, 3},
    {A, "≡1", KR_E_SYNTAX, 1, 1},
    {A, "1↑", KR_E_SYNTAX, 1, 2},
    {A, "↑1", KR_E_SYNTAX, 1, 1},
    {K, "*1", KR_E_SYNTAX, 1, 1},
    {K, "%1", KR_E_SYNTAX, 1, 1},
    {K, "&1", KR_E_SYNTAX, 1, 1},
    {K, "=1", KR_E_SYNTAX, 1, 1},
    {K, ",1", KR_E_SYNTAX, 1, 1},
    {K, "1!2", KR_E_SYNTAX, 1, 2},
    {K, "?1", KR_E_SYNTAX, 1, 1},
    {K, "@1", KR_E_SYNTAX, 1, 1},
    {K, "1^2", KR_E_SYNTAX, 1, 2},
    {K, "^x", KR_E_SYNTAX, 1, 1},       /* null in ngn/k, sort in k9 */
    {K, "x<=y", KR_E_SYNTAX, 1, 2},     /* x<(=y) in ngn/k */
    {K, "x>=y", KR_E_SYNTAX, 1, 2},
    {K, "x<>y", KR_E_SYNTAX, 1, 2},
    {K, "+'x", KR_E_SYNTAX, 1, 2},
    {K, "1+/:2", KR_E_SYNTAX, 1, 3},
    {K, "{x}", KR_E_SYNTAX, 1, 1},
    {K, "\"a\"", KR_E_SYNTAX, 1, 1},
    {K, "`a", KR_E_SYNTAX, 1, 1},
    {K, "$x", KR_E_SYNTAX, 1, 1},
    {K, "x .y", KR_E_SYNTAX, 1, 3},
    {K, "0N", KR_E_SYNTAX, 1, 1},
    {K, "0w", KR_E_SYNTAX, 1, 1},
    {K, "1b", KR_E_SYNTAX, 1, 1},
    {K, "x -1", KR_E_SYNTAX, 1, 3},
    {K, "(x) -1", KR_E_SYNTAX, 1, 5},
    {K, "x+:1", KR_E_SYNTAX, 1, 3},
    {K, "1:2", KR_E_SYNTAX, 1, 2},
    {K, "x[1;2;3]", KR_E_RANK, 1, 2},
    {K, "¯1", KR_E_SYNTAX, 1, 1},
    {C, "", KR_E_SYNTAX, 1, 1},
    {C, "kron", KR_E_SYNTAX, 1, 5},
    {C, "kron\n(add 1)", KR_E_SYNTAX, 2, 7},
    {C, "kron\n(neg 1 2)", KR_E_SYNTAX, 2, 8},
    {C, "kron\n(frob 1)", KR_E_SYNTAX, 2, 2},
    {C, "kron\n(reduce iota x)", KR_E_SYNTAX, 2, 9},
    {C, "kron\n(mat 2 2 1)", KR_E_LENGTH, 2, 11},
    {C, "kron\n(mat -1 2)", KR_E_DOMAIN, 2, 6},
    {C, "kron\n(vec 1 true)", KR_E_SYNTAX, 2, 8},
    {C, "kron\n(tvec 1)", KR_E_SYNTAX, 2, 7},
    {C, "kron\n(let vec 1)", KR_E_NAME, 2, 6},
    {C, "kron\nadd", KR_E_SYNTAX, 2, 1},
    {C, "kron\n(index2 x _ _)", KR_E_SYNTAX, 2, 2},
    {C, "kron\n1 + 2", KR_E_SYNTAX, 2, 3},
    {C, "kron\n(vec 1e3)", KR_E_SYNTAX, 2, 6},
};

/* APL and K programs that must have one canonical form, and pairs that
 * must not (they differ on some input). */
static const char *const k_same[][2] = {
    {"+/⍳10", "+/!10"},
    {"2 3⍴⍳6", "2 3#!6"},
    {"x←3 1 2 ⋄ x[⍋x]", "x:3 1 2; x[<x]"},
    {"x[⍋x←3 1 2]", "x@<x:3 1 2"},
    {"(1+⍳3)∘.×1+⍳4", "(1+!3)*\\:1+!4"},
    {"+⌿x", "+/x"},
    {"×⌿x", "*/x"},
    {"⌈⌿x", "|/x"},
    {"⌊⌿x", "&/x"},
    {"+⍀x", "+\\x"},
    {"⊖x", "|x"},
    {"⍉x", "+x"},
    {"x⍪y", "x,y"},
    {"≢x", "#x"},
    {"x≡y", "x~y"},
    {"⍋x", "<x"},
    {"⍒x", ">x"},
    {"x⍳y", "x?y"},
    {"3↓x", "3_x"},
    {"⌊x", "_x"},
    {"x≤y", "x le y"},
    {"x≥y", "x ge y"},
    {"x≠y", "x ne y"},
    {"x×y", "x*y"},
    {"x÷y", "x%y"},
    {"x⌈y", "x|y"},
    {"x⌊y", "x&y"},
    {"¯3", "-3"},
    {"1 ¯2 3r4", "1 -2 3r4"},
    {"-/1 2 3", "-/1 2 3"}, /* compared below with K's own reading */
    {"2⌽x", "2 rotate x"},
    {"⌈x", "ceil x"},
    {"|x", "abs x"},
    {"×x", "sgn x"},
    {"÷x", "recip x"},
    {"⍴x", "shape x"},
    {",x", "ravel x"},
    {"3↑x", "3 take x"},
    {"x∧y", "x and y"},
    {"x∨y", "x or y"},
    {"x*y", "x pow y"},
    {"3|x", "3 res x"},
    {"⌽x", "rev x"},
    {"y[1]", "y@1"},
};

static const char *const k_differ[][2] = {
    {"-/1 2 3", "-/1 2 3"},       /* APL right fold, K left fold          */
    {"÷/1 2 3", "%/1 2 3"},
    {"-\\1 2 3", "-\\1 2 3"},
    {"+/x", "+/x"},               /* APL last axis, K first axis (x free) */
    {"⌽x", "|x"},                 /* APL last axis, K first axis          */
    {"x,y", "x,y"},               /* APL last axis, K first axis          */
    {"+x", "+x"},                 /* APL identity, K flip                 */
    {"3 -1", "3 -1"},             /* APL 2, K the vector 3 -1             */
    {"x*y", "x*y"},               /* APL power, K multiply                */
};

static void accept_cases(void) {
    for (size_t i = 0; i < sizeof k_accept / sizeof k_accept[0]; i++) {
        const acc_case *c = &k_accept[i];
        kr_program *p = NULL, *q = NULL;
        kr_diag d;
        int32_t st = kr_parse(c->text, strlen(c->text), c->spell, &p, &d);
        if (!c->canon) {
            CHECK(st == KR_E_SYNTAX, "%s: expected a syntax refusal, got %s", c->text, kr_status_name(st));
            kr_program_destroy(p);
            continue;
        }
        CHECK(st == ZXV_OK, "%s: %s", c->text, d.msg);
        if (st != ZXV_OK) continue;
        {
            char want[1024];
            snprintf(want, sizeof want, "kron\n%s\n", c->canon);
            CHECK(strcmp(canon(p), want) == 0, "%s: canonical\n%s\nexpected\n%s", c->text, canon(p), want);
        }
        /* fixed point and digest */
        {
            const char *t = canon(p);
            static char copy[1 << 16];
            uint8_t d1[32], d2[32];
            memcpy(copy, t, strlen(t) + 1u);
            st = kr_parse(copy, strlen(copy), KR_SPELL_AUTO, &q, &d);
            CHECK(st == ZXV_OK, "%s: canonical text does not parse: %s", c->text, d.msg);
            if (q) {
                CHECK(strcmp(canon(q), copy) == 0, "%s: canonical text is not a fixed point", c->text);
                kr_program_digest(p, d1);
                kr_program_digest(q, d2);
                CHECK(memcmp(d1, d2, 32) == 0, "%s: digest", c->text);
                {
                    int32_t sp = 0;
                    kr_program_spelling(q, &sp);
                    CHECK(sp == KR_SPELL_CANON, "detected as canonical");
                }
            }
        }
        kr_program_destroy(p);
        kr_program_destroy(q);
    }
}

static void refuse_cases(void) {
    for (size_t i = 0; i < sizeof k_refuse / sizeof k_refuse[0]; i++) {
        const ref_case *c = &k_refuse[i];
        kr_program *p = (kr_program *) 1;
        kr_diag d;
        int32_t st = kr_parse(c->text, strlen(c->text), c->spell, &p, &d);
        char head[64];
        CHECK(st == c->status, "'%s': %s, expected %s (%s)", c->text, kr_status_name(st),
              kr_status_name(c->status), d.msg);
        CHECK(p == NULL, "'%s': no program on refusal", c->text);
        CHECK(d.line == c->line && d.col == c->col, "'%s': at %u:%u, expected %u:%u (%s)", c->text,
              (unsigned) d.line, (unsigned) d.col, (unsigned) c->line, (unsigned) c->col, d.msg);
        snprintf(head, sizeof head, "line %u, column %u: ", (unsigned) c->line, (unsigned) c->col);
        CHECK(strncmp(d.msg, head, strlen(head)) == 0, "'%s': message starts with the place: %s", c->text, d.msg);
        CHECK(strstr(d.msg, kr_status_name(c->status)) != NULL, "'%s': message names the status: %s", c->text,
              d.msg);
        CHECK(strcmp(last_err(), d.msg) == 0, "'%s': kr_last_error is the diagnostic", c->text);
        kr_program_destroy(p);
    }
}

static void equivalence(void) {
    for (size_t i = 0; i < sizeof k_same / sizeof k_same[0]; i++) {
        if (strcmp(k_same[i][0], "-/1 2 3") == 0) continue;
        CHECK(same_digest(k_same[i][0], A, k_same[i][1], K) == 1, "APL %s and K %s must be one program",
              k_same[i][0], k_same[i][1]);
    }
    for (size_t i = 0; i < sizeof k_differ / sizeof k_differ[0]; i++)
        CHECK(same_digest(k_differ[i][0], A, k_differ[i][1], K) == 0, "APL %s and K %s must differ",
              k_differ[i][0], k_differ[i][1]);
    /* the same text in one spelling, written differently */
    CHECK(same_digest("+/ ⍳ 5", A, "+/⍳5 ⍝ sum", A) == 1, "spaces and comments");
    CHECK(same_digest("0.5", A, "1r2", A) == 1, "0.5 is 1r2");
    CHECK(same_digest("2r4", K, "1r2", K) == 1, "2r4 is 1r2");
    CHECK(same_digest("x←1\nx", A, "x←1 ⋄ x", A) == 1, "newline and diamond");
    CHECK(same_digest("x:1\nx", K, "x:1;x", K) == 1, "newline and semicolon");
    CHECK(same_digest("1+2", A, "2+1", A) == 0, "argument order is kept");
    CHECK(same_digest("x", A, "y", A) == 0, "names are kept");
}

static void detection(void) {
    static const struct {
        const char *t;
        int32_t sp;
    } dt[] = {{"+/!5", K}, {"+/⍳5", A}, {"1+2", K}, {"kron\n1", C}, {"  kron (vec)", C},
              {"kronx:1", K}, {"x←1", A}, {"1 ⍝ é", A}};
    for (size_t i = 0; i < sizeof dt / sizeof dt[0]; i++) {
        kr_program *p = prog(dt[i].t);
        int32_t sp = -1;
        CHECK(p != NULL, "%s parses", dt[i].t);
        if (p) kr_program_spelling(p, &sp);
        CHECK(sp == dt[i].sp, "%s detected as %d, expected %d", dt[i].t, (int) sp, (int) dt[i].sp);
        kr_program_destroy(p);
    }
    /* an ASCII-only APL program is read as K unless the caller says APL */
    CHECK(strcmp(ev("2*3", KR_SPELL_AUTO), "6") == 0, "auto: K multiply");
    CHECK(strcmp(ev("2*3", KR_SPELL_APL), "8") == 0, "APL power");
    CHECK(strcmp(ev("3|7", KR_SPELL_APL), "1") == 0, "APL residue");
    CHECK(strcmp(ev("3|7", KR_SPELL_K), "7") == 0, "K max");
}

static void limits(void) {
    kr_program *p = NULL;
    kr_diag d;
    int32_t st;
    char *big;
    size_t n;
    /* deep nesting */
    n = 2u * KR_MAX_DEPTH + 10u;
    big = (char *) malloc(2u * n + 4u);
    if (big) {
        size_t k = 0;
        for (size_t i = 0; i < n; i++) big[k++] = '(';
        big[k++] = '1';
        for (size_t i = 0; i < n; i++) big[k++] = ')';
        st = kr_parse(big, k, A, &p, &d);
        CHECK(st == KR_E_LIMIT && p == NULL, "deep parentheses: %s", kr_status_name(st));
        /* a long right-to-left chain is deep too */
        k = 0;
        for (size_t i = 0; i < KR_MAX_DEPTH + 5u; i++) {
            big[k++] = '1';
            big[k++] = '+';
        }
        big[k++] = '1';
        st = kr_parse(big, k, A, &p, &d);
        CHECK(st == KR_E_LIMIT, "a chain of %u functions: %s", (unsigned) KR_MAX_DEPTH + 5u, kr_status_name(st));
        k = 0;
        for (size_t i = 0; i + 5u < KR_MAX_DEPTH; i++) {
            big[k++] = '1';
            big[k++] = '+';
        }
        big[k++] = '1';
        st = kr_parse(big, k, A, &p, &d);
        CHECK(st == ZXV_OK, "a chain just under the depth limit: %s", d.msg);
        if (p) {
            kr_value *v = NULL;
            CHECK(kr_eval(p, NULL, 0, 0, &v, &d) == ZXV_OK, "evaluates");
            CHECK(strcmp(vtext(v), "508") == 0, "1+1+...+1 = %s", vtext(v));
            kr_value_destroy(v);
        }
        kr_program_destroy(p);
        p = NULL;
        free(big);
    }
    /* too long */
    big = (char *) malloc(KR_MAX_TEXT + 2u);
    if (big) {
        memset(big, ' ', KR_MAX_TEXT + 1u);
        big[0] = '1';
        st = kr_parse(big, KR_MAX_TEXT + 1u, A, &p, &d);
        CHECK(st == KR_E_LIMIT, "text above the limit");
        st = kr_parse(big, KR_MAX_TEXT, A, &p, &d);
        CHECK(st == ZXV_OK, "text at the limit: %s", d.msg);
        kr_program_destroy(p);
        p = NULL;
        free(big);
    }
    /* a long strand */
    {
        size_t cnt = 20000, k = 0;
        big = (char *) malloc(cnt * 3u + 4u);
        if (big) {
            for (size_t i = 0; i < cnt; i++) {
                big[k++] = (char) ('0' + (int) (i % 10u));
                big[k++] = ' ';
            }
            st = kr_parse(big, k, K, &p, &d);
            CHECK(st == ZXV_OK, "a 20000-element literal");
            if (p) {
                kr_value *v = NULL;
                int64_t sh[2];
                CHECK(kr_eval(p, NULL, 0, 0, &v, &d) == ZXV_OK, "evaluates");
                kr_value_info(v, NULL, NULL, sh, NULL);
                CHECK(sh[0] == 20000, "20000 elements");
                kr_value_destroy(v);
            }
            kr_program_destroy(p);
            p = NULL;
            free(big);
        }
    }
    /* invalid UTF-8 */
    st = kr_parse("1+\n2\xC3", 5, A, &p, &d);
    CHECK(st == ZXV_E_UTF8 && d.line == 2 && d.col == 2, "invalid UTF-8 at 2:2 (%u:%u)", (unsigned) d.line,
          (unsigned) d.col);
    st = kr_parse("\xED\xA0\x80", 3, A, &p, &d);
    CHECK(st == ZXV_E_UTF8, "a surrogate is not UTF-8");
    st = kr_parse("\xC0\x80", 2, A, &p, &d);
    CHECK(st == ZXV_E_UTF8, "an overlong form is not UTF-8");
    /* NUL inside */
    st = kr_parse("1\0002", 3, A, &p, &d);
    CHECK(st == KR_E_SYNTAX, "a NUL byte is refused: %s", kr_status_name(st));
}

static void api(void) {
    kr_program *p = prog_s("x+y×z←2", A);
    char b[64];
    size_t n = 0;
    uint32_t nf = 0;
    uint8_t dg[32];
    CHECK(p != NULL, "parse");
    if (!p) return;
    CHECK(kr_free_count(p, &nf) == ZXV_OK && nf == 2, "two free names (z is assigned first): %u", (unsigned) nf);
    CHECK(kr_free_at(p, 0, b, sizeof b, &n) == ZXV_OK && strcmp(b, "y") == 0, "y first (right to left): %s", b);
    CHECK(kr_free_at(p, 1, b, sizeof b, &n) == ZXV_OK && strcmp(b, "x") == 0, "then x");
    CHECK(kr_free_at(p, 2, b, sizeof b, &n) == ZXV_E_NOT_FOUND, "no third");
    CHECK(kr_free_at(p, 0, b, 1, &n) == ZXV_E_BUFFER && n == 1, "R4: too small");
    CHECK(kr_program_text(p, NULL, 0, &n) == ZXV_E_BUFFER && n == strlen(canon(p)), "R4: size query %zu", n);
    CHECK(kr_program_text(p, b, n, &n) == ZXV_E_BUFFER, "R4: no room for the NUL");
    CHECK(kr_program_text(p, b, n + 1u, &n) == ZXV_OK && strcmp(b, canon(p)) == 0, "R4: exact fit");
    CHECK(kr_program_text(p, b, sizeof b, NULL) == ZXV_E_ARG, "R4: out_len required");
    CHECK(kr_program_digest(p, dg) == ZXV_OK, "digest");
    {
        uint8_t d2[32];
        const char *t = canon(p);
        CHECK(zxv_sha3_256((const uint8_t *) t, strlen(t), d2) == ZXV_OK && memcmp(dg, d2, 32) == 0,
              "the digest is SHA3-256 of the canonical text");
    }
    CHECK(kr_program_digest(NULL, dg) == ZXV_E_ARG, "NULL program");
    kr_program_destroy(p);
    p = NULL;
    CHECK(kr_parse("1", 1, A, NULL, NULL) == ZXV_E_ARG, "NULL out");
    CHECK(kr_parse(NULL, 1, A, &p, NULL) == ZXV_E_ARG && p == NULL, "NULL text");
    CHECK(kr_parse("1", 1, 9, &p, NULL) == ZXV_E_ARG && p == NULL, "bad spelling");
    kr_program_destroy(NULL);
    CHECK(strcmp(kr_status_name(KR_E_DOMAIN), "KR_E_DOMAIN") == 0, "names");
    CHECK(strcmp(kr_status_name(ZXV_E_RANGE), "ZXV_E_RANGE") == 0, "libzxv names pass through");
    for (int32_t s = KR_E_LIMIT; s <= KR_E_SYNTAX; s++) {
        CHECK(strncmp(kr_status_name(s), "KR_E_", 5) == 0, "%d named", (int) s);
        CHECK(kr_status_class(s) == (s == KR_E_LIMIT ? ZXV_CLASS_REFUSED : ZXV_CLASS_USAGE), "%d class", (int) s);
    }
    CHECK(kr_status_class(ZXV_E_RANGE) == zxv_status_class(ZXV_E_RANGE), "libzxv class passes through");
    {
        char e[8];
        CHECK(kr_last_error(e, sizeof e, NULL) == ZXV_E_ARG, "last_error R4");
    }
}

int main(void) {
    accept_cases();
    refuse_cases();
    equivalence();
    detection();
    limits();
    api();
    return report("test_parse");
}
