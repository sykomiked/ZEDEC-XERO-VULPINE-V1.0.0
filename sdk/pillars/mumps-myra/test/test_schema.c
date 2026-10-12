/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_schema.c — the global map and MYRA front ends: both give the same
 * schema (digest, canonical text, every field), spellings that mean the
 * same thing, the printers' round trips, the accessors, and every refusal
 * at its line and column. */
#include "zm_test.h"

static void same_schema(void) {
    zm_schema *a = myra(k_pay_myra), *b = gmap(k_pay_gmap), *c = NULL;
    uint8_t da[32], db[32];
    int32_t same = 0;
    uint32_t n = 0;
    CHECK(a && b, "both payment forms parse");
    if (!a || !b) return;
    CHECK(zm_schema_digest(a, da) == ZXV_OK && zm_schema_digest(b, db) == ZXV_OK &&
              memcmp(da, db, 32) == 0,
          "same digest");
    CHECK(zm_schema_same(a, b, &same) == ZXV_OK && same == 1, "zm_schema_same");
    CHECK(strcmp(canon(a), k_pay_myra) == 0, "the canonical MYRA is the source:\n%s", canon(a));
    CHECK(strcmp(canon(b), k_pay_myra) == 0, "and the global map prints it too");
    CHECK(zm_node_count(a, &n) == ZXV_OK && n == 3, "three nodes");
    for (uint32_t i = 0; i < n; i++) {
        zm_node_info ia, ib;
        zm_node_info_at(a, i, &ia);
        zm_node_info_at(b, i, &ib);
        CHECK(memcmp(&ia, &ib, sizeof ia) == 0, "node %u info", (unsigned) i);
        for (uint32_t k = 0; k < ia.keys + ia.pieces; k++) {
            zm_field fa, fb;
            CHECK(zm_field_at(a, i, k, &fa) == ZXV_OK && zm_field_at(b, i, k, &fb) == ZXV_OK &&
                      memcmp(&fa, &fb, sizeof fa) == 0,
                  "node %u field %u identical", (unsigned) i, (unsigned) k);
        }
    }
    /* the printed global map parses back to the same digest */
    {
        char t[8192];
        size_t len = 0;
        CHECK(zm_schema_gmap(a, t, sizeof t, &len) == ZXV_OK, "print the global map");
        c = gmap(t);
        CHECK(c && zm_schema_same(a, c, &same) == ZXV_OK && same == 1, "printed map round trip");
        zm_schema_destroy(c);
        CHECK(zm_schema_gmap(a, t, 10, &len) == ZXV_E_BUFFER && len > 10, "R4: too small");
        CHECK(zm_schema_myra(a, NULL, 0, &len) == ZXV_E_BUFFER && len == strlen(k_pay_myra),
              "R4: the length");
    }
    /* accessors */
    {
        zm_node_info info;
        zm_field f;
        uint32_t idx = 99;
        char b2[256];
        size_t len = 0;
        CHECK(zm_node_info_at(a, 0, &info) == ZXV_OK && info.subs == 2 && info.keys == 2 &&
                  info.pieces == 8 && info.width == 9,
              "node 1: 2 keys, 8 pieces, width 9");
        CHECK(zm_node_info_at(a, 2, &info) == ZXV_OK && info.subs == 4 && info.keys == 3 &&
                  info.pieces == 0 && info.width == 0,
              "the index node: 4 subscripts, 3 keys, no pieces");
        CHECK(zm_node_info_at(a, 3, &info) == ZXV_E_NOT_FOUND, "no node 4");
        CHECK(zm_field_find(a, 0, "VALUE-DATE", &idx) == ZXV_OK && idx == 7, "find VALUE-DATE: %u", (unsigned) idx);
        CHECK(zm_field_at(a, 0, idx, &f) == ZXV_OK && f.type == ZM_T_DATE && f.piece == 6 &&
                  f.sub == 0 && !f.required,
              "VALUE-DATE is piece 6");
        CHECK(zm_field_at(a, 0, 0, &f) == ZXV_OK && strcmp(f.name, "batch") == 0 && f.sub == 1 &&
                  f.required == 1 && f.type == ZM_T_INT && f.len == 9,
              "key batch");
        CHECK(zm_field_at(a, 0, 3, &f) == ZXV_OK && f.type == ZM_T_NUM && f.len == 15 &&
                  f.scale == 2 && f.piece == 2 && f.required,
              "amount n15.2!");
        CHECK(zm_field_find(a, 0, "nope", &idx) == ZXV_E_NOT_FOUND, "unknown field");
        CHECK(zm_field_find(a, 1, "seq", &idx) == ZXV_E_NOT_FOUND, "seq is not in node 2");
        CHECK(zm_node_pattern(a, 2, b2, sizeof b2, &len) == ZXV_OK &&
                  strcmp(b2, "^ZXPAY(\"B\",ref:t16,batch:i9,seq:i9)") == 0,
              "pattern: %s", b2);
        CHECK(zm_schema_global(a, b2, sizeof b2, &len) == ZXV_OK && strcmp(b2, "ZXPAY") == 0 &&
                  len == 5,
              "global name");
    }
    zm_schema_destroy(a);
    zm_schema_destroy(b);
}

/* Different spellings of one schema. */
static void spellings(void) {
    static const char *same_as_first[] = {
        "^ZXT(id:i9)=a:t5!^b:n9.2",
        "  ^ZXT( ID : I9 ) = A:T5 ! ^ B:N9.2   ; comment\n",
        "; header\n\n^ZXT(id:i9)=a:t5!^b:n9.2^^\n",
        "^ZXT(id:i9)=a:t5!^b:n9.2\r\n",
        "\t^ZXT(id:i9)\t=\ta:t5!\t^\tb:n9.2",
        "GLOBAL ^ZXT\nNODE ^ZXT(ID)\n KEY ID INTEGER(9)\n PIECE 2 B NUMBER(9,2)\n PIECE 1 A TEXT(5) REQUIRED\n",
        "global ^ZXT ; lower-case keywords\nnode ^ZXT(id)\npiece 1 a text( 5 ) required\nkey id integer (9)\npiece 2 b number( 9 , 2 )\n",
    };
    zm_schema *first = NULL;
    for (size_t i = 0; i < sizeof same_as_first / sizeof *same_as_first; i++) {
        zm_schema *s = NULL;
        zm_diag d;
        int32_t st = zm_schema_parse(same_as_first[i], strlen(same_as_first[i]), &s, &d);
        int32_t same = 0;
        CHECK(st == ZXV_OK, "spelling %zu parses: %s", i, d.msg);
        if (st != ZXV_OK) continue;
        if (!first) {
            first = s;
            CHECK(strcmp(canon(s), "^ZXT(id:i9)=a:t5!^b:n9.2\n") == 0, "canonical: %s", canon(s));
            continue;
        }
        CHECK(zm_schema_same(first, s, &same) == ZXV_OK && same, "spelling %zu is the same schema", i);
        zm_schema_destroy(s);
    }
    zm_schema_destroy(first);
    /* NUMBER(p,0) is INTEGER(p); "10" as a literal is the number 10 */
    {
        zm_schema *a = myra("^ZXT(\"10\",k:n7.0)=v:n4.0"), *b = myra("^ZXT(10,k:i7)=v:i4");
        int32_t same = 0;
        CHECK(a && b && zm_schema_same(a, b, &same) == ZXV_OK && same,
              "n7.0 is i7 and \"10\" is 10: %s", a ? canon(a) : "?");
        zm_schema_destroy(a);
        zm_schema_destroy(b);
    }
    /* quotes, literals and no subscripts */
    {
        zm_schema *s = myra("^%ZX(\"say \"\"hi\"\"\",-2.5,.5,k:t3)=x:d\n^%ZX=total:n18.2\n");
        CHECK(s && strcmp(canon(s), "^%ZX(\"say \"\"hi\"\"\",-2.5,.5,k:t3)=x:d\n^%ZX=total:n18.2\n") == 0,
              "literals and a top node: %s", s ? canon(s) : "?");
        if (s) {
            char t[4096];
            size_t n = 0;
            zm_schema *g;
            int32_t same = 0;
            zm_schema_gmap(s, t, sizeof t, &n);
            g = gmap(t);
            CHECK(g && zm_schema_same(s, g, &same) == ZXV_OK && same, "via the map:\n%s", t);
            zm_schema_destroy(g);
        }
        zm_schema_destroy(s);
    }
    /* different schemas have different digests */
    {
        static const char *v[] = {"^ZXT(id:i9)=a:t5", "^ZXT(id:i8)=a:t5", "^ZXT(id:i9)=a:t6",
                                  "^ZXT(id:i9)=a:t5!", "^ZXT(id:i9)=b:t5", "^ZXT(id:i9)=^a:t5",
                                  "^ZXT(id:t9)=a:t5", "^ZXT(id:d)=a:t5", "^zxt(id:i9)=a:t5",
                                  "^ZXT(1,id:i9)=a:t5", "^ZXT(id:i9)=a:n5.1", "^ZXT(id:i9)=a:d"};
        uint8_t dg[sizeof v / sizeof *v][32];
        for (size_t i = 0; i < sizeof v / sizeof *v; i++) {
            zm_schema *s = myra(v[i]);
            CHECK(s != NULL, "%s parses", v[i]);
            if (s) zm_schema_digest(s, dg[i]);
            zm_schema_destroy(s);
            for (size_t j = 0; j < i; j++)
                CHECK(memcmp(dg[i], dg[j], 32) != 0, "%s and %s differ", v[i], v[j]);
        }
    }
    /* edges of the limits */
    {
        char t[16384];
        size_t k = 0;
        zm_schema *s;
        app(t, sizeof t, &k, "^ZXT(");
        for (int i = 0; i < 31; i++) app(t, sizeof t, &k, "%sk%d:i18", i ? "," : "", i);
        app(t, sizeof t, &k, ")=");
        for (int p = 1; p <= 255; p++) app(t, sizeof t, &k, "%sp%d:t32767", p > 1 ? "^" : "", p);
        s = myra(t);
        CHECK(s != NULL, "31 subscripts and 255 pieces");
        zm_schema_destroy(s);
        s = myra("^ABCDEFGHIJKLMNOPQRSTUVWXYZ01234(abcdefghijklmnopqrstuvwxyz-abc:n18.18)=x:t1");
        CHECK(s != NULL, "31-byte global, 30-byte name, n18.18");
        zm_schema_destroy(s);
    }
}

typedef struct {
    int gmap; /* 0: MYRA, 1: global map */
    const char *text;
    int32_t status;
    uint32_t line, col;
    const char *msg; /* a substring of the message */
} refusal;

static const refusal k_refusals[] = {
    /* MYRA */
    {0, "", ZM_E_SYNTAX, 1, 1, "no nodes"},
    {0, "; only a comment\n", ZM_E_SYNTAX, 2, 1, "no nodes"},
    {0, "ZXT(id:i9)=a:t1", ZM_E_SYNTAX, 1, 1, "expected \"^\""},
    {0, "^(id:i9)=a:t1", ZM_E_SYNTAX, 1, 2, "not a global name"},
    {0, "^1X(id:i9)=a:t1", ZM_E_SYNTAX, 1, 2, "not a global name"},
    {0, "^ABCDEFGHIJKLMNOPQRSTUVWXYZ012345=a:t1", ZM_E_SYNTAX, 1, 2, "at most 31"},
    {0, "^ZXT(id:i9)", ZM_E_SYNTAX, 1, 12, "expected \"=\""},
    {0, "^ZXT(id:i9=a:t1", ZM_E_SYNTAX, 1, 11, "expected \",\" or \")\""},
    {0, "^ZXT(id)=a:t1", ZM_E_SYNTAX, 1, 8, "expected \":\""},
    {0, "^ZXT(id:)=a:t1", ZM_E_SYNTAX, 1, 9, "expected a type"},
    {0, "^ZXT(id:x9)=a:t1", ZM_E_SYNTAX, 1, 9, "expected a type"},
    {0, "^ZXT(id:i19)=a:t1", ZM_E_SYNTAX, 1, 10, "larger than 18"},
    {0, "^ZXT(id:i0)=a:t1", ZM_E_SYNTAX, 1, 6, "INTEGER(n) needs n in 1..18"},
    {0, "^ZXT(id:i9)=a:t0", ZM_E_SYNTAX, 1, 13, "TEXT(n) needs n in 1..32767"},
    {0, "^ZXT(id:i9)=a:t32768", ZM_E_SYNTAX, 1, 16, "larger than 32767"},
    {0, "^ZXT(id:i9)=a:n9", ZM_E_SYNTAX, 1, 17, "nP.S"},
    {0, "^ZXT(id:i9)=a:n9.10", ZM_E_SYNTAX, 1, 13, "s <= p"},
    {0, "^ZXT(id:i9)=a:t1x", ZM_E_SYNTAX, 1, 17, "after the type"},
    {0, "^ZXT(id:i9!)=a:t1", ZM_E_SYNTAX, 1, 11, "drop the \"!\""},
    {0, "^ZXT(id:i9)=a:t1 b:t1", ZM_E_SYNTAX, 1, 18, "expected \"^\" between"},
    {0, "^ZXT(id:i9)=a:t1^a:t2", ZM_E_SYNTAX, 1, 18, "used twice"},
    {0, "^ZXT(id:i9)=id:t2", ZM_E_SYNTAX, 1, 13, "used twice"},
    {0, "^ZXT(1x:i9)=a:t1", ZM_E_SYNTAX, 1, 6, "1x is not a number in M canonical form"},
    {0, "^ZXT(1E3,id:i9)=a:t1", ZM_E_SYNTAX, 1, 6, "1E3 is not a number"},
    {0, "^ZXT(-id:i9)=a:t1", ZM_E_SYNTAX, 1, 6, "not a number in M canonical form"},
    {0, "^ZXT(01,id:i9)=a:t1", ZM_E_SYNTAX, 1, 6, "not a number in M canonical form"},
    {0, "^ZXT(\"\",id:i9)=a:t1", ZM_E_SYNTAX, 1, 6, "null subscript"},
    {0, "^ZXT(\"abc,id:i9)=a:t1", ZM_E_SYNTAX, 1, 6, "not closed"},
    {0, "^ZXT(a-:i9)=a:t1", ZM_E_SYNTAX, 1, 6, "not a field name"},
    {0, "^ZXT(abcdefghijklmnopqrstuvwxyz-abcde:i9)=a:t1", ZM_E_SYNTAX, 1, 6, "not a field name"},
    {0, "^ZXT(id:i9)=a:t1\n^ZXU(id:i9)=b:t1", ZM_E_UNSUPPORTED, 2, 1, "one global"},
    {0, "^ZXT(id:i9)=a:t1\n^ZXT(k:i3)=b:t1", ZM_E_SYNTAX, 2, 1, "can match the same reference"},
    {0, "^ZXT(1,id:i9)=a:t1\n^ZXT(k:n5.2,j:i3)=b:t1", ZM_E_SYNTAX, 2, 1, "can match the same"},
    {0, "^ZXT(\"A\",id:i9)=a:t1\n^ZXT(k:t3,j:i3)=b:t1", ZM_E_SYNTAX, 2, 1, "can match the same"},
    {0, "^ZXT(id:i9)=a:s3", ZM_E_UNSUPPORTED, 1, 15, "set-of-codes"},
    {0, "^ZXT(id:i9)=a:p3", ZM_E_UNSUPPORTED, 1, 15, "pointer"},
    {0, "^ZXT(id:i9)=a:w", ZM_E_UNSUPPORTED, 1, 15, "word-processing"},
    {0, "^ZXT(id:i9)=a:h", ZM_E_UNSUPPORTED, 1, 15, "date-and-time"},
    {0, "^ZXT(id:i9)=a:f8", ZM_E_UNSUPPORTED, 1, 15, "floating point"},
    {0, "^ZXT(id:i9)=a:r8", ZM_E_UNSUPPORTED, 1, 15, "floating point"},
    {0, "^ZXT(id:i9)=a:t1\x01", ZM_E_SYNTAX, 1, 17, "expected \"^\" between"},
    {0, "^ZXT(id:i9)=a:t1\0", ZM_E_SYNTAX, 1, 17, "NUL"},
    {0, "^ZXT(id:i9)=a:t1 \xff", ZXV_E_UTF8, 1, 18, "UTF-8"},
    /* global map */
    {1, "NODE ^ZXT(ID)\n", ZM_E_SYNTAX, 1, 1, "starts with GLOBAL"},
    {1, "GLOBAL ^ZXT\nGLOBAL ^ZXT\n", ZM_E_UNSUPPORTED, 2, 1, "one GLOBAL"},
    {1, "GLOBAL ^ZXT\n", ZM_E_SYNTAX, 2, 1, "no nodes"},
    {1, "GLOBAL ^ZXT\nKEY ID INTEGER(9)\n", ZM_E_SYNTAX, 2, 1, "before the first NODE"},
    {1, "GLOBAL ^ZXT\nPIECE 1 A TEXT(1)\n", ZM_E_SYNTAX, 2, 1, "before the first NODE"},
    {1, "GLOBAL ^ZXT\nFIELD 1 A TEXT(1)\n", ZM_E_SYNTAX, 2, 1, "expected GLOBAL, NODE"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXU(ID)\n", ZM_E_UNSUPPORTED, 2, 6, "one global"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\n", ZM_E_SYNTAX, 2, 11, "has no KEY line"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\nNODE ^ZXT(1,ID)\nKEY ID INTEGER(9)\n", ZM_E_SYNTAX, 2, 11, "has no KEY line"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID,ID)\n", ZM_E_SYNTAX, 2, 14, "appears twice"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\nKEY ID INTEGER(9)\nKEY ID INTEGER(9)\n", ZM_E_SYNTAX, 4, 5, "declared twice"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\nKEY IX INTEGER(9)\n", ZM_E_SYNTAX, 3, 5, "not a subscript"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\nKEY ID INTEGER(9) REQUIRED\n", ZM_E_SYNTAX, 3, 19, "drop REQUIRED"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\nKEY ID INTEGER(9)\nPIECE 0 A TEXT(1)\n", ZM_E_SYNTAX, 4, 7, "from 1"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\nKEY ID INTEGER(9)\nPIECE 256 A TEXT(1)\n", ZM_E_SYNTAX, 4, 7, "larger than 255"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\nKEY ID INTEGER(9)\nPIECE 1 A TEXT(1)\nPIECE 1 B TEXT(1)\n", ZM_E_SYNTAX, 5, 9, "declared twice"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\nKEY ID INTEGER(9)\nPIECE 1 A TEXT\n", ZM_E_SYNTAX, 4, 15, "needs (n)"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\nKEY ID INTEGER(9)\nPIECE 1 A NUMBER(9)\n", ZM_E_SYNTAX, 4, 19, "(p,s)"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\nKEY ID INTEGER(9)\nPIECE 1 A TEXT(1) OPTIONAL\n", ZM_E_SYNTAX, 4, 19, "unexpected text"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\nKEY ID INTEGER(9)\nPIECE 1 A SET\n", ZM_E_UNSUPPORTED, 4, 11, "SET"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\nKEY ID INTEGER(9)\nPIECE 1 A POINTER\n", ZM_E_UNSUPPORTED, 4, 11, "POINTER"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\nKEY ID INTEGER(9)\nPIECE 1 A WP\n", ZM_E_UNSUPPORTED, 4, 11, "word-processing"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\nKEY ID INTEGER(9)\nPIECE 1 A DATETIME\n", ZM_E_UNSUPPORTED, 4, 11, "times"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\nKEY ID INTEGER(9)\nPIECE 1 A REAL\n", ZM_E_UNSUPPORTED, 4, 11, "floating point"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\nKEY ID INTEGER(9)\nPIECE 1 A FLOAT\n", ZM_E_UNSUPPORTED, 4, 11, "floating point"},
    {1, "GLOBAL ^ZXT\nNODE ^ZXT(ID)\nKEY ID TEXT(3)\nNODE ^ZXT(\"X\")\n", ZM_E_SYNTAX, 4, 6, "can match the same"},
    {1, "GLOBAL ^ZXT ^ZXU\n", ZM_E_SYNTAX, 1, 13, "unexpected text"},
};

static void refusals(void) {
    for (size_t i = 0; i < sizeof k_refusals / sizeof *k_refusals; i++) {
        const refusal *r = &k_refusals[i];
        zm_schema *s = NULL;
        zm_diag d;
        size_t len = strlen(r->text);
        int32_t st;
        if (strstr(r->msg, "NUL")) len++; /* the case with an embedded NUL */
        memset(&d, 0, sizeof d);
        st = r->gmap ? zm_gmap_parse(r->text, len, &s, &d) : zm_myra_parse(r->text, len, &s, &d);
        CHECK(st == r->status && s == NULL, "case %zu: %s, want %s (%s)", i, zm_status_name(st),
              zm_status_name(r->status), d.msg);
        CHECK(d.line == r->line && d.col == r->col, "case %zu: at %u:%u, want %u:%u (%s)", i,
              (unsigned) d.line, (unsigned) d.col, (unsigned) r->line, (unsigned) r->col, d.msg);
        CHECK(strstr(d.msg, r->msg) != NULL, "case %zu: message '%s' lacks '%s'", i, d.msg, r->msg);
        CHECK(strncmp(d.msg, "line ", 5) == 0, "case %zu: message starts 'line'", i);
        CHECK(strstr(last_err(), r->msg) != NULL, "case %zu: last error matches", i);
        zm_schema_destroy(s);
    }
    {
        zm_schema *s = NULL;
        CHECK(zm_myra_parse(NULL, 5, &s, NULL) == ZXV_E_ARG, "NULL text");
        CHECK(zm_myra_parse("^X=a:t1", 7, NULL, NULL) == ZXV_E_ARG, "NULL out");
        CHECK(zm_gmap_parse(NULL, 5, &s, NULL) == ZXV_E_ARG, "NULL map text");
        CHECK(zm_myra_parse("x", ZM_MAX_TEXT + 1u, &s, NULL) == ZXV_E_TOO_LONG, "too long");
        CHECK(zm_myra_parse("^X=a:t1^b:", 10, &s, NULL) == ZM_E_SYNTAX, "no diag is fine");
        CHECK(zm_schema_digest(NULL, NULL) == ZXV_E_ARG, "digest NULL");
        zm_schema_destroy(NULL);
    }
    /* 257 nodes */
    {
        char *t = (char *) malloc(257u * 32u);
        size_t k = 0;
        zm_schema *s = NULL;
        zm_diag d;
        for (int i = 0; i < 257; i++) app(t, 257u * 32u, &k, "^ZXT(%d)=a:t1\n", i);
        CHECK(zm_myra_parse(t, k, &s, &d) == ZXV_E_CAPACITY && d.line == 257, "257 nodes: %s", d.msg);
        CHECK(zm_myra_parse(t, k - strlen("^ZXT(256)=a:t1\n"), &s, &d) == ZXV_OK, "256 nodes");
        zm_schema_destroy(s);
        free(t);
    }
}

int main(void) {
    same_schema();
    spellings();
    refusals();
    return report("test_schema");
}
