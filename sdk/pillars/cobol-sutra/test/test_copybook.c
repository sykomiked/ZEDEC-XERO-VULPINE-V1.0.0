/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_copybook.c — the copybook front end: the supported subset, sizes
 * and offsets, fixed and free format, every refusal with its position,
 * references, and the copybook printer round trip. */
#include "zs_test.h"

/* A field by name (case-insensitive), tables included; zeros if absent. */
static zs_field field(const zs_layout *l, const char *name) {
    zs_field f;
    uint32_t n = 0;
    zs_field_count(l, &n);
    for (uint32_t i = 0; i < n; i++) {
        size_t k = 0;
        zs_field_at(l, i, &f);
        while (name[k] && f.name[k] &&
               (name[k] == f.name[k] || (name[k] >= 'a' && name[k] - 32 == f.name[k])))
            k++;
        if (!name[k] && !f.name[k]) return f;
    }
    memset(&f, 0, sizeof f);
    return f;
}

/* Expect a refusal with this status at this line and column. */
static void refuse(const char *what, const char *text, uint32_t format, int32_t want, uint32_t line,
                   uint32_t col) {
    zs_layout *l = NULL;
    zs_diag d;
    int32_t st = zs_copybook_parse(text, strlen(text), format, &l, &d);
    CHECK(st == want, "%s: status %s, expected %s (%s)", what, zs_status_name(st),
          zs_status_name(want), d.msg);
    CHECK(l == NULL, "%s: no layout on refusal", what);
    if (line) CHECK(d.line == line && d.col == col, "%s: at %u:%u, expected %u:%u (%s)", what,
                    (unsigned) d.line, (unsigned) d.col, (unsigned) line, (unsigned) col, d.msg);
    CHECK(strncmp(d.msg, "line ", 5) == 0, "%s: message '%s'", what, d.msg);
    zs_layout_destroy(l);
}

static void test_payment(void) {
    zs_layout *l = cpy(k_payment_cpy);
    uint64_t size = 0;
    uint32_t n = 0;
    zs_field f;
    CHECK(l != NULL, "payment.cpy parses");
    if (!l) return;
    zs_layout_size(l, &size);
    CHECK(size == 55, "payment is %llu bytes, expected 55", (unsigned long long) size);
    zs_field_count(l, &n);
    CHECK(n == 10, "10 fields (88s take no storage), got %u", (unsigned) n);
    f = field(l, "PAY-AMOUNT");
    CHECK(f.kind == ZS_K_NUM && f.usage == ZS_U_COMP3 && f.digits == 15 && f.scale == 2 &&
              f.sign == ZS_SIGN_TRAILING && f.offset == 16 && f.size == 8,
          "PAY-AMOUNT: S9(13)V99 COMP-3 is 8 bytes at 16");
    f = field(l, "PAY-FROM");
    CHECK(f.usage == ZS_U_COMP && f.size == 4 && f.offset == 27 && f.sign == ZS_SIGN_NONE,
          "PAY-FROM: 9(9) COMP is 4 bytes at 27");
    f = field(l, "PAY-FEE");
    CHECK(f.usage == ZS_U_DISPLAY && f.size == 7 && f.offset == 43 && f.scale == 2,
          "PAY-FEE: S9(5)V99 is 7 bytes at 43");
    f = field(l, "pay-status");
    CHECK(f.kind == ZS_K_TEXT && f.size == 1 && f.offset == 50, "names are case-insensitive");
    CHECK(zs_find(l, "FILLER", NULL, NULL) == ZXV_E_NOT_FOUND, "FILLER is never found");
    CHECK(zs_find(l, "PAY-NEW", NULL, NULL) == ZXV_E_NOT_FOUND, "88 names are not data items");
    zs_layout_destroy(l);
}

static void test_sizes(void) {
    /* COMP 2/4/8, COMP-3 digits/2+1, separate sign +1 */
    static const char t[] =
        "       01 R.\n"
        "          05 B1 PIC 9 COMP.\n"
        "          05 B4 PIC S9(4) BINARY.\n"
        "          05 B5 PIC 9(5) COMPUTATIONAL.\n"
        "          05 B9 PIC S9(9) COMP-4.\n"
        "          05 B10 PIC 9(10) USAGE IS COMP.\n"
        "          05 B18 PIC S9(18) COMP.\n"
        "          05 P1 PIC 9 COMP-3.\n"
        "          05 P2 PIC S99 PACKED-DECIMAL.\n"
        "          05 P18 PIC S9(18) COMPUTATIONAL-3.\n"
        "          05 Z1 PIC S9(3) SIGN LEADING SEPARATE.\n"
        "          05 Z2 PIC S9(3) SIGN IS TRAILING SEPARATE CHARACTER.\n"
        "          05 Z3 PIC S999 LEADING.\n"
        "          05 Z4 PIC SV9(4).\n"
        "          05 X1 PIC XXX.\n"
        "          05 X2 PICTURE IS X(10) USAGE DISPLAY.\n";
    static const struct {
        const char *name;
        uint64_t size;
    } want[] = {{"B1", 2},  {"B4", 2}, {"B5", 4}, {"B9", 4}, {"B10", 8}, {"B18", 8},
                {"P1", 1},  {"P2", 2}, {"P18", 10}, {"Z1", 4}, {"Z2", 4}, {"Z3", 3},
                {"Z4", 4},  {"X1", 3}, {"X2", 10}};
    zs_layout *l = cpy(t);
    CHECK(l != NULL, "sizes copybook parses");
    if (!l) return;
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        zs_field f = field(l, want[i].name);
        CHECK(f.size == want[i].size, "%s is %llu bytes, expected %llu", want[i].name,
              (unsigned long long) f.size, (unsigned long long) want[i].size);
    }
    CHECK(field(l, "Z1").sign == ZS_SIGN_LEADING_SEP && field(l, "Z2").sign == ZS_SIGN_TRAILING_SEP &&
              field(l, "Z3").sign == ZS_SIGN_LEADING && field(l, "Z4").sign == ZS_SIGN_TRAILING,
          "sign clauses");
    CHECK(field(l, "Z4").digits == 4 && field(l, "Z4").scale == 4, "SV9(4) has 4 decimals");
    zs_layout_destroy(l);
}

/* A fixed-format line: sequence number in 1-6, code from column 8,
 * padded to column 72, then an identification area in 73-80. */
static void card(char *out, size_t cap, int seq, const char *code) {
    size_t n = strlen(out);
    snprintf(out + n, cap - n, "%06d%-66.66sSEQ%05d\n", seq, code, seq);
}

static void test_groups_and_tables(void) {
    static const char *const code[] = {
        " 01  ST.",
        "     05  HEAD.",
        "         10  H-A     PIC 9(4).",
        "         10  H-B     PIC X(2).",
        "     05  NUMS        COMP-3.",
        "         10  N-1     PIC S9(5).",
        "         10  N-2     PIC S9(5) COMP-3.",
        "     05  ROW OCCURS 3 TIMES INDEXED BY RX RY.",
        "         10  CELL    PIC 9(2) OCCURS 4.",
        "         10  TAG     PIC X.",
        "*    a comment line",
        "/    a page eject is a comment too",
        "D    DISPLAY 'debug lines are skipped'.",
        "     05  TAIL        PIC X(3) VALUE 'A.B'.",
    };
    static char t[4096];
    t[0] = '\0';
    for (size_t i = 0; i < sizeof code / sizeof code[0]; i++) card(t, sizeof t, (int) (i + 1u) * 100, code[i]);
    zs_layout *l = cpy(t);
    zs_field f;
    uint64_t off = 0, size = 0;
    CHECK(l != NULL, "sequence numbers in 1-6 and 73-80 are ignored");
    if (!l) return;
    f = field(l, "N-1");
    CHECK(f.usage == ZS_U_COMP3 && f.size == 3, "a COMP-3 group hands its usage down");
    f = field(l, "ROW");
    CHECK(f.occurs == 3 && f.size == 9 && f.offset == 12 && f.dims == 1, "ROW OCCURS 3 of 9 bytes at 12");
    f = field(l, "CELL");
    CHECK(f.occurs == 4 && f.dims == 2 && f.size == 2, "CELL is a 2-dimensional table");
    CHECK(zs_find(l, "CELL(1,1)", NULL, &off) == ZXV_OK && off == 12, "CELL(1,1) at 12");
    CHECK(zs_find(l, "CELL(2, 3)", NULL, &off) == ZXV_OK && off == 12 + 9 + 4, "CELL(2,3) at 25");
    CHECK(zs_find(l, "cell (3 4)", NULL, &off) == ZXV_OK && off == 12 + 18 + 6, "CELL(3,4) at 36");
    CHECK(zs_find(l, "TAG(3)", NULL, &off) == ZXV_OK && off == 12 + 18 + 8, "TAG(3) at 38");
    CHECK(zs_find(l, "CELL(4,1)", NULL, NULL) == ZXV_E_RANGE, "subscript above OCCURS");
    CHECK(zs_find(l, "CELL(0,1)", NULL, NULL) == ZXV_E_RANGE, "subscripts start at 1");
    CHECK(zs_find(l, "CELL(1)", NULL, NULL) == ZXV_E_RANGE, "too few subscripts");
    CHECK(zs_find(l, "TAIL(1)", NULL, NULL) == ZXV_E_RANGE, "a subscript on a non-table");
    CHECK(zs_find(l, "CELL(1,", NULL, NULL) == ZXV_E_FORMAT, "malformed reference");
    CHECK(zs_find(l, "CELL(1)x", NULL, NULL) == ZXV_E_FORMAT, "trailing junk");
    CHECK(zs_find(l, "NOPE", NULL, NULL) == ZXV_E_NOT_FOUND, "unknown name");
    CHECK(zs_find(l, "", NULL, NULL) == ZXV_E_FORMAT, "empty reference");
    zs_layout_size(l, &size);
    CHECK(size == 12 + 27 + 3, "record is 42 bytes, got %llu", (unsigned long long) size);
    zs_layout_destroy(l);
}

static void test_free_format(void) {
    static const char t[] =
        "*> free format: the whole line is code\n"
        "01 payment-rec.  *> lower case works\n"
        "  05 pay-ref pic x(16).\n"
        "  05 pay-amount pic s9(13)v99 comp-3.\n"
        "\t05 pay-ccy pic x(3).\n"
        "  05 pay-from pic 9(9) comp. 05 pay-to pic 9(9) comp.\n"
        "  05 pay-value-date pic 9(8).\n"
        "  05 pay-fee pic s9(5)v99.\n"
        "  05 pay-status pic x.\n"
        "     88 pay-new value 'N'.\n"
        "  05 pic x(4).\n";
    zs_layout *a = NULL, *b = cpy(k_payment_cpy);
    zs_diag d;
    int32_t same = 0;
    CHECK(zs_copybook_parse(t, strlen(t), ZS_CB_FREE, &a, &d) == ZXV_OK, "free format: %s", d.msg);
    if (a && b) {
        zs_layout_same(a, b, &same);
        CHECK(same == 1, "free and fixed format give the same layout (an omitted name is FILLER)");
    }
    zs_layout_destroy(a);
    zs_layout_destroy(b);
}

static void test_round_trip(void) {
    static const char *const texts[] = {
        k_payment_cpy,
        "       01 R.\n"
        "          05 A OCCURS 2.\n"
        "             10 B PIC S9(3)V9(2) SIGN LEADING SEPARATE.\n"
        "             10 C OCCURS 3.\n"
        "                15 D PIC 9 COMP-3.\n"
        "                15 E PIC SV99 COMP.\n"
        "          05 F PIC X(70).\n"};
    for (size_t i = 0; i < sizeof texts / sizeof texts[0]; i++) {
        zs_layout *a = cpy(texts[i]), *b = NULL;
        char buf[8192];
        size_t n = 0;
        int32_t same = 0;
        CHECK(a != NULL, "round trip %zu parses", i);
        if (!a) continue;
        CHECK(zs_layout_copybook(a, NULL, 0, &n) == ZXV_E_BUFFER && n > 0, "R4 size query");
        CHECK(zs_layout_copybook(a, buf, n, &n) == ZXV_E_BUFFER, "R4: cap == len is too small");
        CHECK(zs_layout_copybook(a, buf, sizeof buf, &n) == ZXV_OK && strlen(buf) == n, "printed");
        b = cpy(buf);
        CHECK(b != NULL, "the printed copybook parses:\n%s", buf);
        if (b) {
            zs_layout_same(a, b, &same);
            CHECK(same == 1, "printed copybook declares the same record");
        }
        /* every printed line fits in columns 1-72 */
        for (char *p = buf, *nl; *p; p = nl + 1) {
            nl = strchr(p, '\n');
            if (!nl) break;
            CHECK(nl - p <= 72, "line longer than 72 columns: %.*s", (int) (nl - p), p);
        }
        zs_layout_destroy(a);
        zs_layout_destroy(b);
    }
}

#define H "       01 R.\n"

static void test_refusals(void) {
    /* unsupported COBOL: the reason, at the clause */
    refuse("REDEFINES", H "          05 A PIC X(4).\n          05 B REDEFINES A PIC 9(4).\n",
           ZS_CB_FIXED, ZS_E_UNSUPPORTED, 3, 16);
    refuse("COMP-1", H "          05 A COMP-1.\n", ZS_CB_FIXED, ZS_E_UNSUPPORTED, 2, 16);
    refuse("COMP-2", H "          05 A USAGE IS COMP-2.\n", ZS_CB_FIXED, ZS_E_UNSUPPORTED, 2, 25);
    refuse("COMP-5", H "          05 A PIC 9(4) COMP-5.\n", ZS_CB_FIXED, ZS_E_UNSUPPORTED, 2, 25);
    refuse("POINTER", H "          05 A USAGE POINTER.\n", ZS_CB_FIXED, ZS_E_UNSUPPORTED, 2, 22);
    refuse("OCCURS DEPENDING",
           H "          05 N PIC 9.\n          05 A PIC X OCCURS 5 DEPENDING ON N.\n", ZS_CB_FIXED,
           ZS_E_UNSUPPORTED, 3, 31);
    refuse("OCCURS TO", H "          05 A PIC X OCCURS 1 TO 5.\n", ZS_CB_FIXED, ZS_E_UNSUPPORTED, 2, 31);
    refuse("66 RENAMES", H "          05 A PIC X.\n          66 B RENAMES A.\n", ZS_CB_FIXED,
           ZS_E_UNSUPPORTED, 3, 11);
    refuse("77", "       77 A PIC X.\n", ZS_CB_FIXED, ZS_E_UNSUPPORTED, 1, 8);
    refuse("second 01", H "          05 A PIC X.\n       01 S.\n          05 B PIC X.\n",
           ZS_CB_FIXED, ZS_E_UNSUPPORTED, 3, 8);
    refuse("starts at 05", "       05 A PIC X.\n", ZS_CB_FIXED, ZS_E_UNSUPPORTED, 1, 8);
    refuse("SYNC", H "          05 A PIC S9(4) COMP SYNC.\n", ZS_CB_FIXED, ZS_E_UNSUPPORTED, 2, 31);
    refuse("JUSTIFIED", H "          05 A PIC X(4) JUST RIGHT.\n", ZS_CB_FIXED, ZS_E_UNSUPPORTED, 2,
           25);
    refuse("BLANK WHEN ZERO", H "          05 A PIC 9(4) BLANK WHEN ZERO.\n", ZS_CB_FIXED,
           ZS_E_UNSUPPORTED, 2, 25);
    refuse("edited picture", H "          05 A PIC ZZ9.99.\n", ZS_CB_FIXED, ZS_E_UNSUPPORTED, 2, 20);
    refuse("P scaling", H "          05 A PIC 9(3)PP.\n", ZS_CB_FIXED, ZS_E_UNSUPPORTED, 2, 24);
    refuse("A picture", H "          05 A PIC A(3).\n", ZS_CB_FIXED, ZS_E_UNSUPPORTED, 2, 20);
    refuse("19 digits", H "          05 A PIC S9(10)V9(9).\n", ZS_CB_FIXED, ZS_E_UNSUPPORTED, 2, 27);
    refuse("X and 9 mixed", H "          05 A PIC X(2)9.\n", ZS_CB_FIXED, ZS_E_UNSUPPORTED, 2, 20);
    refuse("continuation", H "      -    'ABC'.\n", ZS_CB_FIXED, ZS_E_UNSUPPORTED, 2, 7);
    refuse("open literal", H "          05 A PIC X VALUE 'AB\n", ZS_CB_FIXED, ZS_E_UNSUPPORTED, 2, 28);
    refuse("8 nested OCCURS",
           H "        02 A1 OCCURS 2.\n         03 A2 OCCURS 2.\n          04 A3 OCCURS 2.\n"
             "           05 A4 OCCURS 2.\n            06 A5 OCCURS 2.\n"
             "             07 A6 OCCURS 2.\n              08 A7 OCCURS 2.\n"
             "               09 A8 PIC X OCCURS 2.\n",
           ZS_CB_FIXED, ZS_E_UNSUPPORTED, 9, 16);
    refuse("duplicate name", H "          05 A PIC X.\n          05 A PIC X.\n", ZS_CB_FIXED,
           ZS_E_UNSUPPORTED, 3, 11);
    /* not COBOL at all: syntax, at the spot */
    refuse("tab in 1-7", "\t01 R.\n", ZS_CB_FIXED, ZS_E_SYNTAX, 1, 1);
    refuse("bad indicator", "      X01 R.\n", ZS_CB_FIXED, ZS_E_SYNTAX, 1, 7);
    refuse("missing period", H "          05 A PIC X\n", ZS_CB_FIXED, ZS_E_SYNTAX, 2, 21);
    refuse("no level", "       R PIC X.\n", ZS_CB_FIXED, ZS_E_SYNTAX, 1, 8);
    refuse("level 50", H "          50 A PIC X.\n", ZS_CB_FIXED, ZS_E_SYNTAX, 2, 11);
    refuse("level mismatch",
           H "          05 G.\n            10 A PIC X.\n            07 B PIC X.\n", ZS_CB_FIXED,
           ZS_E_SYNTAX, 4, 13);
    refuse("items under a PIC", H "          05 A PIC X.\n            10 B PIC X.\n", ZS_CB_FIXED,
           ZS_E_SYNTAX, 3, 13);
    refuse("empty group", H "          05 G.\n          05 A PIC X.\n", ZS_CB_FIXED, ZS_E_SYNTAX,
           2, 11);
    refuse("empty record", H, ZS_CB_FIXED, ZS_E_SYNTAX, 1, 8);
    refuse("elementary 01", "       01 R PIC X(4).\n", ZS_CB_FIXED, ZS_E_UNSUPPORTED, 1, 8);
    refuse("nothing", "", ZS_CB_FIXED, ZS_E_SYNTAX, 1, 1);
    refuse("SIGN on unsigned", H "          05 A PIC 9(3) SIGN LEADING.\n", ZS_CB_FIXED,
           ZS_E_SYNTAX, 2, 11);
    refuse("SIGN LEADING on COMP-3", H "          05 A PIC S9(3) COMP-3 LEADING.\n", ZS_CB_FIXED,
           ZS_E_SYNTAX, 2, 11);
    refuse("PIC X COMP-3", H "          05 A PIC X(3) COMP-3.\n", ZS_CB_FIXED, ZS_E_SYNTAX, 2, 11);
    refuse("usage conflict", H "          05 G COMP.\n            10 A PIC 9 COMP-3.\n", ZS_CB_FIXED,
           ZS_E_SYNTAX, 3, 13);
    refuse("PIC X under COMP group", H "          05 G COMP-3.\n            10 A PIC X.\n",
           ZS_CB_FIXED, ZS_E_SYNTAX, 3, 13);
    refuse("unknown clause", H "          05 A PIC X FROBNICATE.\n", ZS_CB_FIXED, ZS_E_SYNTAX, 2, 22);
    refuse("PIC twice", H "          05 A PIC X PIC X.\n", ZS_CB_FIXED, ZS_E_SYNTAX, 2, 22);
    refuse("bad name", H "          05 -A PIC X.\n", ZS_CB_FIXED, ZS_E_SYNTAX, 2, 14);
    refuse("name too long", H "          05 ABCDEFGHIJABCDEFGHIJABCDEFGHIJK PIC X.\n", ZS_CB_FIXED,
           ZS_E_SYNTAX, 2, 14);
    refuse("OCCURS 0", H "          05 A PIC X OCCURS 0.\n", ZS_CB_FIXED, ZS_E_SYNTAX, 2, 29);
    refuse("OCCURS on 01", "       01 R OCCURS 2.\n          05 A PIC X.\n", ZS_CB_FIXED, ZS_E_SYNTAX,
           1, 8);
    refuse("non-ASCII word", H "          05 \xc3\xa9 PIC X.\n", ZS_CB_FIXED, ZS_E_SYNTAX, 2, 14);
    refuse("bad UTF-8", H "      * \xff\n", ZS_CB_FIXED, ZS_E_SYNTAX, 2, 9);
    refuse("88 first", "       88 A VALUE 1.\n", ZS_CB_FIXED, ZS_E_SYNTAX, 1, 8);
    refuse("bad picture", H "          05 A PIC 9(.\n", ZS_CB_FIXED, ZS_E_SYNTAX, 2, 21);
    refuse("PIC S twice", H "          05 A PIC S9S.\n", ZS_CB_FIXED, ZS_E_SYNTAX, 2, 22);
    refuse("PIC V twice", H "          05 A PIC 9V9V9.\n", ZS_CB_FIXED, ZS_E_SYNTAX, 2, 23);
    refuse("free: tab in code is fine, '-' indicator is not special",
           "01 R.\n\t05 A PIC X.\n- 05 B PIC X.\n", ZS_CB_FREE, ZS_E_SYNTAX, 3, 1);
    /* API misuse */
    {
        zs_layout *l = NULL;
        CHECK(zs_copybook_parse(H, strlen(H), 7u, &l, NULL) == ZXV_E_ARG, "bad format");
        CHECK(zs_copybook_parse(NULL, 5, ZS_CB_FIXED, &l, NULL) == ZXV_E_ARG, "NULL text");
        CHECK(zs_copybook_parse(H, strlen(H), ZS_CB_FIXED, NULL, NULL) == ZXV_E_ARG, "NULL out");
    }
}

static void test_accepted_oddities(void) {
    /* things real copybooks contain that change nothing */
    static const char t[] =
        "       01 R.\n"
        "          05 A PIC X(3) VALUE SPACES.\n"
        "          05 B PIC S9(3)V99 VALUE -1.50.\n"
        "          05 C PIC X VALUE ALL '*'.\n"
        "             88 C-STAR VALUE '*'.\n"
        "             88 C-ANY VALUES 'A' THRU 'Z', '0'.\n"
        "          05 D PIC 9(4), COMP; VALUE ZERO.\n"
        "          05 E PIC X(2) VALUE 'it''s'.\n";
    zs_layout *l = cpy(t);
    CHECK(l != NULL, "VALUE clauses, 88s with THRU, commas and semicolons are accepted");
    if (l) CHECK(field(l, "D").usage == ZS_U_COMP, "'COMP;' is COMP");
    zs_layout_destroy(l);
}

int main(void) {
    test_payment();
    test_sizes();
    test_groups_and_tables();
    test_free_format();
    test_round_trip();
    test_refusals();
    test_accepted_oddities();
    return report("test_copybook");
}
