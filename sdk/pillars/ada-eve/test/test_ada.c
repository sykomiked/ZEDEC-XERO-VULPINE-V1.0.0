/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_ada.c — the Ada front end: what the subset accepts, how it reads
 * it (ranges, deltas, enumerations, arrays, records, subtypes), and that
 * everything outside the subset is refused with a status, a line and a
 * column, never guessed at. */
#include "ze_test.h"

static void test_accept(void) {
    ze_pkg *p = parse(1,
                      "--  a comment\n"
                      "package Shapes with Pure is\n"
                      "   pragma Preelaborate;\n"
                      "   type Small is range -(2 ** 7) .. 2 ** 7 - 1;\n"
                      "   type Count is range 0 .. 16#FFFF#;\n"
                      "   type Rate is delta 1.0E-4 digits 9 range -1.0 .. 1.0;\n"
                      "   type Color is (Red, Green, Blue);\n"
                      "   subtype Warm is Color range Red .. Green;\n"
                      "   subtype Name is String (1 .. 8);\n"
                      "   type Grid is array (Color, 1 .. 3) of Small;\n"
                      "   type Flags is array (Boolean) of Boolean;\n"
                      "   type Cell is record\n"
                      "      Hue  : Warm := Red;\n"
                      "      Qty  : Count range 1 .. 10;\n"
                      "      Tag  : Name;\n"
                      "      Hits : Grid;\n"
                      "   end record;\n"
                      "end Shapes;\n");
    ze_type t;
    ze_dim d;
    char nb[80];
    size_t n = 0;
    CHECK(p != NULL, "the subset parses");
    if (!p) return;
    CHECK(ze_pkg_name(p, nb, sizeof nb, &n) == ZXV_OK && strcmp(nb, "shapes") == 0, "name %s", nb);
    CHECK(ze_type_at(p, type_of(p, "small"), &t) == ZXV_OK && t.kind == ZE_K_INT && t.lo == -128 && t.hi == 127 &&
              t.size == 1,
          "Small is -128 .. 127 in one byte");
    CHECK(ze_type_at(p, type_of(p, "COUNT"), &t) == ZXV_OK && t.lo == 0 && t.hi == 65535 && t.size == 2,
          "based literal 16#FFFF#, two bytes");
    CHECK(ze_type_at(p, type_of(p, "rate"), &t) == ZXV_OK && t.kind == ZE_K_DEC && t.scale == 4 && t.digits == 9 &&
              t.lo == -10000 && t.hi == 10000 && t.size == 2,
          "Rate: scale 4, -1.0 .. 1.0 is -10000 .. 10000 units");
    CHECK(ze_type_at(p, type_of(p, "warm"), &t) == ZXV_OK && t.is_subtype && t.base == type_of(p, "color") &&
              t.lo == 0 && t.hi == 1,
          "Warm is Red .. Green");
    CHECK(ze_type_at(p, type_of(p, "name"), &t) == ZXV_OK && t.kind == ZE_K_TEXT && t.count == 8, "Name");
    CHECK(ze_type_at(p, type_of(p, "grid"), &t) == ZXV_OK && t.kind == ZE_K_ARRAY && t.dims == 2 && t.count == 9 &&
              t.size == 9,
          "Grid is 3 x 3 bytes");
    CHECK(ze_array_dim(p, type_of(p, "grid"), 1, &d) == ZXV_OK && d.index == UINT32_MAX && d.lo == 1 && d.hi == 3,
          "a literal index range");
    CHECK(ze_array_dim(p, type_of(p, "flags"), 0, &d) == ZXV_OK && d.index == 0, "Boolean is type 0");
    CHECK(ze_type_at(p, type_of(p, "cell"), &t) == ZXV_OK && t.kind == ZE_K_RECORD && t.count == 4 &&
              t.size == 1 + 1 + 8 + 9,
          "Cell is 19 bytes (Qty 1 .. 10 in one byte)");
    CHECK(ze_enum_literal(p, type_of(p, "warm"), 2, nb, sizeof nb, &n) == ZXV_OK && strcmp(nb, "blue") == 0,
          "a subtype's literals are its base's");
    CHECK(strstr(canon(p), "rate = dec(9, 4) -1.0000 .. 1.0000\n") != NULL, "canonical decimal: %s", canon(p));
    CHECK(strstr(canon(p), "  qty: count 1 .. 10\n") != NULL, "anonymous constraint");
    CHECK(strstr(canon(p), "grid = [color, 1 .. 3] small\n") != NULL, "array");
    {
        ze_pkg *q = parse(1, as_ada(p));
        int32_t same = 0;
        CHECK(q && ze_pkg_same(p, q, &same) == ZXV_OK && same == 1, "printed Ada re-parses to the same layout");
        ze_pkg_destroy(q);
    }
    ze_pkg_destroy(p);

    /* spelling does not matter: case, layout, comments, expressions */
    {
        ze_pkg *a = parse(1, "package P is type T is range 0 .. 99; end P;");
        ze_pkg *b = parse(1, "PACKAGE p IS\n  -- x\n  TYPE t IS RANGE 0 .. 9_9 ;\nEND p ;\n");
        ze_pkg *c = parse(1, "package P is\n  type T is range 0 .. (10 ** 2) - 1;\nend P;\n");
        int32_t s1 = 0, s2 = 0;
        CHECK(a && b && c && ze_pkg_same(a, b, &s1) == ZXV_OK && ze_pkg_same(a, c, &s2) == ZXV_OK && s1 && s2,
              "spelling does not change the layout");
        ze_pkg_destroy(a);
        ze_pkg_destroy(b);
        ze_pkg_destroy(c);
    }
    /* int64 limits */
    {
        ze_pkg *a = parse(1, "package L is type Wide is range -2 ** 63 .. 2 ** 63 - 1; end L;");
        CHECK(a && ze_type_at(a, 1, &t) == ZXV_OK && t.lo == INT64_MIN && t.hi == INT64_MAX && t.size == 8,
              "the whole int64 range");
        ze_pkg_destroy(a);
    }
}

typedef struct {
    const char *text;
    int32_t status;
    uint32_t line, col;
    const char *why; /* a substring of the message */
} refusal;

static const refusal k_refusals[] = {
    {"package P is\n   type T is mod 256;\nend P;\n", ZE_E_UNSUPPORTED, 2, 14, "modular"},
    {"package P is\n   type F is digits 6;\nend P;\n", ZE_E_UNSUPPORTED, 2, 14, "float"},
    {"package P is\n   type T is range 0 .. 9;\n   type D is new T;\nend P;\n", ZE_E_UNSUPPORTED, 3, 14,
     "derived"},
    {"package P is\n   type A is access Integer;\nend P;\n", ZE_E_UNSUPPORTED, 2, 14, "access"},
    {"package P is\n   type R is tagged record X : Boolean; end record;\nend P;\n", ZE_E_UNSUPPORTED, 2, 14,
     "tagged"},
    {"package P is\n   type R (N : Natural) is record X : Boolean; end record;\nend P;\n", ZE_E_UNSUPPORTED, 2,
     11, "discriminant"},
    {"package P is\n   type T is range 0 .. 9 with Size => 8;\nend P;\n", ZE_E_UNSUPPORTED, 2, 27, "aspect"},
    {"package P is\n   type T is range 0 .. 9;\n   for T'Size use 8;\nend P;\n", ZE_E_UNSUPPORTED, 3, 4,
     "representation"},
    {"package P is\n   type T is range 0 .. 9;\nprivate\nend P;\n", ZE_E_UNSUPPORTED, 3, 1, "private"},
    {"package P is\n   X : Integer;\nend P;\n", ZE_E_UNSUPPORTED, 2, 4, "object"},
    {"package P.Q is\nend P.Q;\n", ZE_E_UNSUPPORTED, 1, 10, "child"},
    {"with Ada.Text_IO;\npackage P is\nend P;\n", ZE_E_UNSUPPORTED, 1, 1, "context"},
    {"package P is\n   type R is null record;\nend P;\n", ZE_E_UNSUPPORTED, 2, 14, "null record"},
    {"package P is\n   pragma Inline (X);\nend P;\n", ZE_E_UNSUPPORTED, 2, 11, "pragma"},
    {"package P is\n   type T is range 9 .. 0;\nend P;\n", ZE_E_CONSTRAINT, 2, 9, "empty"},
    {"package P is\n   type T is range 0 .. 9;\n   subtype S is T range 5 .. 10;\nend P;\n", ZE_E_CONSTRAINT, 3, 17,
     "outside"},
    {"package P is\n   type M is delta 0.03 digits 5;\nend P;\n", ZE_E_CONSTRAINT, 2, 20, "power of ten"},
    {"package P is\n   type M is delta 0.01 digits 19;\nend P;\n", ZE_E_CONSTRAINT, 2, 32, "digits"},
    {"package P is\n   type M is delta 0.01 digits 5 range 0 .. 9;\nend P;\n", ZE_E_SYNTAX, 2, 40,
     "real literal"},
    {"package P is\n   type M is delta 0.01 digits 3 range 0.0 .. 10.0;\nend P;\n", ZE_E_CONSTRAINT, 2, 9,
     "digits"},
    {"package P is\n   type M is delta 0.01 digits 5 range 0.0 .. 0.005;\nend P;\n", ZE_E_CONSTRAINT, 2, 47,
     "never rounded"},
    {"package P is\n   type T is range 0 .. 2 ** 64;\nend P;\n", ZE_E_CONSTRAINT, 2, 25, "int64"},
    {"package P is\n   type T is range 0 .. 9;\n   type t is range 0 .. 9;\nend P;\n", ZE_E_NAME, 3, 9,
     "declared twice"},
    {"package P is\n   type Integer is range 0 .. 9;\nend P;\n", ZE_E_NAME, 2, 9, "predefined"},
    {"package P is\n   type C is (A, B, a);\nend P;\n", ZE_E_NAME, 2, 21, "appears twice"},
    {"package P is\n   type R is record\n      X : Undefined;\n   end record;\nend P;\n", ZE_E_NAME, 3, 11,
     "unknown"},
    {"package P is\n   type R is record\n      X, x : Boolean;\n   end record;\nend P;\n", ZE_E_NAME, 3, 10,
     "appears twice"},
    {"package P is\n   type T is range 0 .. 9\nend P;\n", ZE_E_SYNTAX, 3, 1, "expected"},
    {"package P is\n   type T is range 0 .. 9;\nend Q;\n", ZE_E_NAME, 3, 5, "end"},
    {"package P is\n   type T is range 0 .. 1 / 0;\nend P;\n", ZE_E_CONSTRAINT, 2, 27, "zero"},
    {"package P is\n   type T is range 0 .. 9; @\nend P;\n", ZE_E_SYNTAX, 2, 28, ""},
    {"package P is\n   type A is array (Positive) of Boolean;\nend P;\n", ZE_E_UNSUPPORTED, 2, 21,
     "implementation-defined"},
    {"package P is\n   type A is array (1 .. 2, 1 .. 2, 1 .. 2, 1 .. 2, 1 .. 2) of Boolean;\nend P;\n",
     ZE_E_LIMIT, 2, 0, "dimensions"},
    {"package P is\n   type S is array (1 .. 1048577) of Boolean;\nend P;\n", ZE_E_LIMIT, 2, 9, "2**20 elements"},
    {"", ZE_E_SYNTAX, 1, 1, ""},
};

static void test_refusals(void) {
    char text[512];
    for (size_t i = 0; i < sizeof k_refusals / sizeof k_refusals[0]; i++) {
        const refusal *r = &k_refusals[i];
        ze_diag d;
        size_t n;
        int32_t st;
        memset(&d, 0, sizeof d);
        st = refuse(1, r->text, &d);
        CHECK(st == r->status, "case %zu: %s, want %s: %s", i, ze_status_name(st), ze_status_name(r->status), d.msg);
        if (r->col) CHECK(d.line == r->line && d.col == r->col, "case %zu at %u:%u, want %u:%u: %s", i, d.line,
                          d.col, r->line, r->col, d.msg);
        else CHECK(d.line == r->line, "case %zu on line %u, want %u: %s", i, d.line, r->line, d.msg);
        n = append(text, sizeof text, 0, "line %u, column %u: ", d.line, d.col);
        CHECK(strncmp(d.msg, text, n) == 0, "case %zu: the message starts with its position: %s", i, d.msg);
        CHECK(strstr(d.msg, r->why) != NULL, "case %zu: '%s' not in: %s", i, r->why, d.msg);
    }
    /* not UTF-8, NUL bytes, NULL arguments */
    {
        ze_diag d;
        ze_pkg *p = NULL;
        CHECK(ze_ada_parse("package P is\xff end P;", 20, &p, &d) == ZE_E_SYNTAX && p == NULL && d.col == 13,
              "invalid UTF-8 is refused at its byte");
        CHECK(ze_ada_parse("package P is\0 end P;", 20, &p, &d) == ZE_E_SYNTAX, "a NUL byte");
        CHECK(ze_ada_parse(NULL, 3, &p, &d) == ZXV_E_ARG, "NULL text");
        CHECK(ze_ada_parse("x", 1, NULL, &d) == ZXV_E_ARG, "NULL out");
        CHECK(ze_ada_parse("package P is end P;", ZE_MAX_TEXT + 1u, &p, NULL) == ZE_E_LIMIT, "too long");
    }
}

static void test_status(void) {
    CHECK(strcmp(ze_status_name(ZE_E_FIT), "ZE_E_FIT") == 0 && ze_status_class(ZE_E_FIT) == ZXV_CLASS_REFUSED,
          "ZE_E_FIT");
    CHECK(ze_status_class(ZE_E_SYNTAX) == ZXV_CLASS_USAGE && ze_status_class(ZE_E_UNSUPPORTED) == ZXV_CLASS_USAGE &&
              ze_status_class(ZE_E_NAME) == ZXV_CLASS_USAGE && ze_status_class(ZE_E_CONSTRAINT) == ZXV_CLASS_USAGE,
          "parse refusals are USAGE");
    CHECK(ze_status_class(ZE_E_DATA) == ZXV_CLASS_REFUSED && ze_status_class(ZE_E_LIMIT) == ZXV_CLASS_REFUSED,
          "DATA and LIMIT are REFUSED");
    CHECK(strcmp(ze_status_name(ZXV_E_FUNDS), "ZXV_E_FUNDS") == 0 && ze_status_class(ZXV_E_POISONED) ==
              ZXV_CLASS_FATAL,
          "libzxv codes pass through");
}

int main(void) {
    test_status();
    test_accept();
    test_refusals();
    return report("test_ada");
}
