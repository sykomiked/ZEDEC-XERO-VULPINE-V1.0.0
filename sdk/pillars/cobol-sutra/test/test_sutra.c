/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_sutra.c — Sutra: the same records as copybooks (equivalence by
 * canonical text, digest and every field), the canonical form, spelling
 * variants, and every refusal with its position. */
#include "zs_test.h"

/* Both forms must give the same digest, canonical text and field table. */
static void same(const char *what, const char *cobol, uint32_t format, const char *sutra_text) {
    zs_layout *a = NULL, *b = NULL;
    zs_diag d;
    uint8_t da[32], db[32];
    uint32_t na = 0, nb = 0;
    int32_t eq = 0;
    CHECK(zs_copybook_parse(cobol, strlen(cobol), format, &a, &d) == ZXV_OK, "%s copybook: %s", what,
          d.msg);
    CHECK(zs_sutra_parse(sutra_text, strlen(sutra_text), &b, &d) == ZXV_OK, "%s sutra: %s", what,
          d.msg);
    if (!a || !b) goto out;
    zs_layout_digest(a, da);
    zs_layout_digest(b, db);
    CHECK(memcmp(da, db, 32) == 0, "%s: digests differ", what);
    CHECK(zs_layout_same(a, b, &eq) == ZXV_OK && eq == 1, "%s: zs_layout_same", what);
    {
        char ca[8192];
        snprintf(ca, sizeof ca, "%s", canon(a));
        CHECK(strcmp(ca, canon(b)) == 0, "%s: canonical text differs:\n%s---\n%s", what, ca, canon(b));
    }
    zs_field_count(a, &na);
    zs_field_count(b, &nb);
    CHECK(na == nb, "%s: %u vs %u fields", what, (unsigned) na, (unsigned) nb);
    for (uint32_t i = 0; i < na && i < nb; i++) {
        zs_field fa, fb;
        zs_field_at(a, i, &fa);
        zs_field_at(b, i, &fb);
        CHECK(memcmp(&fa, &fb, sizeof fa) == 0, "%s: field %u (%s) differs", what, (unsigned) i,
              fa.name);
    }
out:
    zs_layout_destroy(a);
    zs_layout_destroy(b);
}

static void test_equivalence(void) {
    same("payment", k_payment_cpy, ZS_CB_FIXED, k_payment_sutra);
    same("statement",
         "       01  STATEMENT-REC.\n"
         "           05  ST-ACCOUNT          PIC 9(10).\n"
         "           05  ST-PERIOD.\n"
         "               10  ST-YEAR         PIC 9(4).\n"
         "               10  ST-MONTH        PIC 99.\n"
         "           05  ST-TOTALS           COMP-3.\n"
         "               10  ST-OPENING      PIC S9(11)V99.\n"
         "               10  ST-CLOSING      PIC S9(11)V99.\n"
         "           05  ST-LINE-COUNT       PIC 9(4) COMP.\n"
         "           05  ST-LINE OCCURS 5 TIMES INDEXED BY ST-IX.\n"
         "               10  ST-LINE-DATE    PIC 9(8).\n"
         "               10  ST-LINE-AMOUNT  PIC S9(9)V99\n"
         "                                   SIGN IS TRAILING SEPARATE CHARACTER.\n"
         "               10  ST-LINE-RATE    PIC SV9(6) SIGN LEADING.\n"
         "               10  ST-LINE-TEXT    PIC X(20).\n"
         "           05  FILLER              PIC X(10) VALUE SPACES.\n",
         ZS_CB_FIXED,
         "record statement-rec\n"
         "  st-account: 9(10)\n"
         "  st-period:\n"
         "    st-year: 9(4)\n"
         "    st-month: 99\n"
         "  st-totals:\n"
         "    st-opening: s9(11)v99 comp-3\n"
         "    st-closing: s9(11)v99 comp-3\n"
         "  st-line-count: 9(4) comp\n"
         "  st-line[5]:\n"
         "    st-line-date: 9(8)\n"
         "    st-line-amount: s9(9)v99 sign trailing separate\n"
         "    st-line-rate: sv9(6) sign leading\n"
         "    st-line-text: x(20)\n"
         "  filler: x(10)\n");
    /* spellings: 9(2) = 99, binary = comp, packed = comp-3, explicit
     * display and "sign trailing" are the defaults, any case, any indent
     * width, comments and blank lines */
    same("spellings",
         "01 R.\n 05 A PIC 99 BINARY.\n 05 B PIC S9(3)V9(2) PACKED-DECIMAL.\n"
         " 05 C PIC S999 DISPLAY SIGN TRAILING.\n 05 D PIC X(02).\n",
         ZS_CB_FREE,
         "\n# leading comment\nRECORD r   # trailing comment\n\n"
         "     a: 9(2) comp\n     B: S999V99 PACKED\n"
         "     c: s9(3) display sign trailing\n     d: XX\n");
    same("deep tables",
         "01 T.\n 03 A OCCURS 2.\n  05 B OCCURS 3.\n   07 C PIC 9 COMP-3 OCCURS 4.\n"
         "   07 D PIC X.\n 03 E PIC SV99 SIGN LEADING SEPARATE.\n",
         ZS_CB_FREE,
         "record t\n a[2]:\n   b[3]:\n      c[4]: 9 comp-3\n      d: x\n e: sv99 sign leading separate\n");
}

static void test_canonical(void) {
    zs_layout *a = sutra("record R\n  Amount: S9(13)V99 COMP-3\n  rows[2]:\n    q: 9(5)\n"), *b;
    const char *want = "record r\n  amount: s9(13)v9(2) comp-3\n  rows[2]:\n    q: 9(5)\n";
    size_t n = 0;
    char small[8];
    CHECK(a != NULL, "parses");
    if (!a) return;
    CHECK(strcmp(canon(a), want) == 0, "canonical text:\n%s", canon(a));
    b = sutra(canon(a));
    CHECK(b && strcmp(canon(b), want) == 0, "the canonical text is a fixed point");
    CHECK(zs_layout_sutra(a, NULL, 0, &n) == ZXV_E_BUFFER && n == strlen(want), "R4 size");
    CHECK(zs_layout_sutra(a, small, sizeof small, &n) == ZXV_E_BUFFER, "R4 small buffer");
    CHECK(zs_layout_sutra(a, NULL, 0, NULL) == ZXV_E_ARG, "R4 NULL out_len");
    zs_layout_destroy(a);
    zs_layout_destroy(b);
}

static void refuse(const char *what, const char *text, int32_t want, uint32_t line, uint32_t col) {
    zs_layout *l = NULL;
    zs_diag d;
    int32_t st = zs_sutra_parse(text, strlen(text), &l, &d);
    CHECK(st == want, "%s: status %s, expected %s (%s)", what, zs_status_name(st),
          zs_status_name(want), d.msg);
    CHECK(l == NULL, "%s: no layout", what);
    CHECK(d.line == line && d.col == col, "%s: at %u:%u, expected %u:%u (%s)", what,
          (unsigned) d.line, (unsigned) d.col, (unsigned) line, (unsigned) col, d.msg);
    zs_layout_destroy(l);
}

static void test_refusals(void) {
    refuse("empty", "", ZS_E_SYNTAX, 1, 1);
    refuse("only comments", "# nothing\n", ZS_E_SYNTAX, 2, 1);
    refuse("no header", "  a: x\n", ZS_E_SYNTAX, 1, 3);
    refuse("header with junk", "record r x\n", ZS_E_SYNTAX, 1, 8);
    refuse("empty record", "record r\n", ZS_E_SYNTAX, 1, 1);
    refuse("two records", "record r\n  a: x\nrecord s\n", ZS_E_SYNTAX, 3, 1);
    refuse("field in column 1", "record r\na: x\n", ZS_E_SYNTAX, 2, 1);
    refuse("tab", "record r\n\ta: x\n", ZS_E_SYNTAX, 2, 1);
    refuse("indent mismatch", "record r\n  g:\n    a: x\n   b: x\n", ZS_E_SYNTAX, 4, 4);
    refuse("under a picture", "record r\n  a: x\n    b: x\n", ZS_E_SYNTAX, 3, 5);
    refuse("empty group", "record r\n  g:\n  a: x\n", ZS_E_SYNTAX, 2, 3);
    refuse("missing colon", "record r\n  a x\n", ZS_E_SYNTAX, 2, 5);
    refuse("bad name", "record r\n  -a: x\n", ZS_E_SYNTAX, 2, 3);
    refuse("reserved word", "record r\n  to: x\n", ZS_E_SYNTAX, 2, 3);
    refuse("reserved record name", "record comp\n  a: x\n", ZS_E_SYNTAX, 1, 8);
    refuse("count 0", "record r\n  a[0]: x\n", ZS_E_SYNTAX, 2, 5);
    refuse("count too big", "record r\n  a[65536]: x\n", ZS_E_SYNTAX, 2, 5);
    refuse("count unclosed", "record r\n  a[3: x\n", ZS_E_SYNTAX, 2, 5);
    refuse("unknown word", "record r\n  a: 9(3) fast\n", ZS_E_SYNTAX, 2, 11);
    refuse("usage twice", "record r\n  a: 9(3) comp comp-3\n", ZS_E_SYNTAX, 2, 16);
    refuse("sign twice", "record r\n  a: s9 sign leading sign trailing\n", ZS_E_SYNTAX, 2, 22);
    refuse("sign without side", "record r\n  a: s9 sign\n", ZS_E_SYNTAX, 2, 13);
    refuse("sign on unsigned", "record r\n  a: 9 sign leading\n", ZS_E_SYNTAX, 2, 3);
    refuse("sign leading on comp", "record r\n  a: s9 comp sign leading\n", ZS_E_SYNTAX, 2, 3);
    refuse("x comp", "record r\n  a: x(2) comp\n", ZS_E_SYNTAX, 2, 3);
    refuse("comp-1", "record r\n  a: 9v9 comp-1\n", ZS_E_UNSUPPORTED, 2, 10);
    refuse("comp-2", "record r\n  a: 9v9 COMP-2\n", ZS_E_UNSUPPORTED, 2, 10);
    refuse("edited picture", "record r\n  a: zz9\n", ZS_E_UNSUPPORTED, 2, 6);
    refuse("19 digits", "record r\n  a: 9(19)\n", ZS_E_UNSUPPORTED, 2, 6);
    refuse("duplicate", "record r\n  a: x\n  A: x\n", ZS_E_UNSUPPORTED, 3, 3);
    refuse("non-ASCII", "record r\n  \xc3\xa9: x\n", ZS_E_SYNTAX, 2, 3);
    refuse("bad UTF-8 in a comment", "record r # \xc3\n  a: x\n", ZS_E_SYNTAX, 1, 12);
    refuse("record too large", "record r\n  a[2]: x(1048576)\n", ZS_E_UNSUPPORTED, 2, 3);
    {
        zs_layout *l = NULL;
        CHECK(zs_sutra_parse(NULL, 3, &l, NULL) == ZXV_E_ARG, "NULL text");
        CHECK(zs_sutra_parse("record r\n a: x\n", 15, NULL, NULL) == ZXV_E_ARG, "NULL out");
        CHECK(zs_sutra_parse("record r\n a: x\n", 15, &l, NULL) == ZXV_OK, "diag may be NULL");
        zs_layout_destroy(l);
    }
}

int main(void) {
    test_equivalence();
    test_canonical();
    test_refusals();
    return report("test_sutra");
}
