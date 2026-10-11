/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxt.c — treaty parser, emitters, rules and PIC S9(18) arithmetic. */
#include "zxt.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static const char BASE[] = "TREATY CORRIDOR-7 USD SETL\n"
                           "PARTY ACME 5000 4000 1/2\n"
                           "PARTY BETA 3000 3000 1/2\n"
                           "OWE ACME BETA 1200\n"
                           "END\n";

static bool all_zero(const void *p, size_t n)
{
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++)
        if (b[i])
            return false;
    return true;
}

static zxt_status_t ps(const char *s, zxt_treaty_t *t, uint32_t *off)
{
    return zxt_parse_sutra(s, (uint32_t)strlen(s), t, off);
}

/* Sutra must refuse `s` with `want` at offset `off`, leaving the AST zero. */
static void reject(const char *s, zxt_status_t want, uint32_t off, int line)
{
    zxt_treaty_t t;
    uint32_t o = 12345;
    memset(&t, 0xA5, sizeof t);
    zxt_status_t st = ps(s, &t, &o);
    CHECK(st == want && o == off && all_zero(&t, sizeof t), "line %d: got %d@%u want %d@%u", line,
          st, o, want, off);
}
#define REJECT(s, st, off) reject((s), (st), (off), __LINE__)

/* Sutra must accept `s` and re-emit it byte for byte, and the record form
 * must carry the same AST. */
static void accept_rt(const char *s, int line)
{
    zxt_treaty_t t, u;
    static char out[ZXT_SUTRA_MAX];
    static uint8_t rec[ZXT_REC_LEN];
    uint32_t n = 0, off = 99;
    zxt_status_t st = ps(s, &t, &off);
    CHECK(st == ZXT_OK && off == 0, "line %d: parse %d@%u", line, st, off);
    if (st != ZXT_OK)
        return;
    CHECK(zxt_validate(&t) == ZXT_OK, "line %d: validate", line);
    st = zxt_emit_sutra(&t, out, sizeof out, &n);
    CHECK(st == ZXT_OK && n == strlen(s) && memcmp(out, s, n) == 0, "line %d: sutra round trip", line);
    CHECK(zxt_emit_record(&t, rec, sizeof rec) == ZXT_OK, "line %d: emit record", line);
    CHECK(zxt_parse_record(rec, sizeof rec, &u, &off) == ZXT_OK && off == 0 && zxt_equal(&t, &u),
          "line %d: record round trip", line);
}
#define ACCEPT(s) accept_rt((s), __LINE__)

/* --------------------------------------------------------------- PIC */

static void test_pic(void)
{
    const int64_t M = ZXT_PIC18_MAX;
    int64_t r = 77, q = 77, m = 77;
    CHECK(zxt_pic_ok(M) && zxt_pic_ok(-M) && zxt_pic_ok(0), "pic_ok inside");
    CHECK(!zxt_pic_ok(M + 1) && !zxt_pic_ok(-M - 1), "pic_ok outside");

    CHECK(zxt_pic_add(M - 1, 1, &r) && r == M, "add to max");
    r = 77;
    CHECK(!zxt_pic_add(M, 1, &r) && r == 77, "add past max leaves r");
    CHECK(!zxt_pic_add(-M, -1, &r) && r == 77, "add past min");
    CHECK(zxt_pic_add(-M, M, &r) && r == 0, "add -max+max");
    CHECK(zxt_pic_add(-5, 3, &r) && r == -2, "add signs");
    r = 77;
    CHECK(!zxt_pic_add(M + 1, -2, &r) && r == 77, "add operand a out of range");
    CHECK(!zxt_pic_add(-2, M + 1, &r) && r == 77, "add operand b out of range");
    CHECK(!zxt_pic_add(1, 1, NULL), "add NULL");

    CHECK(zxt_pic_sub(-M + 1, 1, &r) && r == -M, "sub to min");
    r = 77;
    CHECK(!zxt_pic_sub(-M, 1, &r) && r == 77, "sub past min");
    CHECK(!zxt_pic_sub(M, -1, &r) && r == 77, "sub past max");
    CHECK(!zxt_pic_sub(0, -M - 1, &r) && r == 77, "sub operand b out of range");
    CHECK(!zxt_pic_sub(0, INT64_MIN, &r) && r == 77, "sub INT64_MIN");
    CHECK(zxt_pic_sub(10, 3, &r) && r == 7, "sub plain");

    CHECK(zxt_pic_mul(999999999LL, 1000000001LL, &r) && r == M, "mul to max");
    r = 77;
    CHECK(!zxt_pic_mul(1000000000LL, 1000000000LL, &r) && r == 77, "mul 10^18 is a size error");
    CHECK(!zxt_pic_mul(M, M, &r) && r == 77, "mul overflowing 64 bits");
    CHECK(!zxt_pic_mul(-M, 2, &r) && r == 77, "mul past min");
    CHECK(zxt_pic_mul(-M, -1, &r) && r == M, "mul -max * -1");
    CHECK(zxt_pic_mul(0, M, &r) && r == 0, "mul zero");
    r = 77;
    CHECK(!zxt_pic_mul(M + 1, 0, &r) && r == 77, "mul operand a out of range");
    CHECK(!zxt_pic_mul(0, M + 1, &r) && r == 77, "mul operand b out of range");
    CHECK(!zxt_pic_mul(1, 1, NULL), "mul NULL");

    CHECK(zxt_pic_div(-7, 2, &q, &m) && q == -3 && m == -1, "div truncates toward zero");
    CHECK(zxt_pic_div(7, -2, &q, &m) && q == -3 && m == 1, "div remainder has dividend's sign");
    CHECK(zxt_pic_div(M, 1, &q, &m) && q == M && m == 0, "div by one");
    q = m = 77;
    CHECK(!zxt_pic_div(5, 0, &q, &m) && q == 77 && m == 77, "div by zero");
    CHECK(!zxt_pic_div(M + 1, 2, &q, &m) && q == 77, "div operand a out of range");
    CHECK(!zxt_pic_div(2, M + 1, &q, &m) && q == 77, "div operand b out of range");
    CHECK(!zxt_pic_div(4, 2, NULL, &m) && !zxt_pic_div(4, 2, &q, NULL), "div NULL");
}

/* --------------------------------------------------------------- Sutra */

static void test_sutra_base(void)
{
    zxt_treaty_t t;
    uint32_t off = 9;
    CHECK(ps(BASE, &t, &off) == ZXT_OK && off == 0, "base parses");
    CHECK(strcmp(t.name, "CORRIDOR-7") == 0 && strcmp(t.asset, "USD") == 0 && t.op == ZXT_OP_SETL,
          "header");
    CHECK(t.n_party == 2 && t.n_owe == 1, "counts");
    CHECK(strcmp(t.party[0].id, "ACME") == 0 && t.party[0].deposit == 5000 &&
              t.party[0].claim == 4000 && t.party[0].w_num == 1 && t.party[0].w_den == 2,
          "party 0");
    CHECK(strcmp(t.party[1].id, "BETA") == 0 && t.party[1].deposit == 3000 &&
              t.party[1].claim == 3000,
          "party 1");
    CHECK(t.owe[0].from == 0 && t.owe[0].to == 1 && t.owe[0].amount == 1200, "owe 0");
    ACCEPT(BASE);
    ACCEPT("TREATY N USD NET\nPARTY A 0 0 1/1\nEND\n");
    ACCEPT("TREATY N USD NET\nPARTY A 0 0 0/1\nPARTY B 1 1 1/1\nOWE B A 1\nOWE A B 999999999999999999\nEND\n");
    ACCEPT("TREATY ABCDEFGHIJKLMNOPQRSTUVWXYZ0123 EUR NET\n"
           "PARTY ABCDEFGH 999999999999999999 999999999999999999 1/999999937\n"
           "PARTY Z9-Z 1 0 999999936/999999937\nEND\n");
    ACCEPT("TREATY A-1 XAU SETL\nPARTY A 10 10 1/3\nPARTY B 10 9 1/3\nPARTY C 0 0 1/3\n"
           "OWE A B 5\nOWE B C 5\nOWE C A 5\nEND\n");
}

static void test_sutra_limits(void)
{
    static char buf[ZXT_SUTRA_MAX + 64];
    /* 16 parties and 64 obligations: the most a treaty can hold */
    int n = sprintf(buf, "TREATY BIG USD SETL\n");
    for (int i = 0; i < 16; i++)
        n += sprintf(buf + n, "PARTY P%02d 100 0 1/16\n", i);
    for (int j = 0; j < 64; j++)
        n += sprintf(buf + n, "OWE P%02d P%02d 1\n", j % 16, (j + 1) % 16);
    strcpy(buf + n, "END\n");
    ACCEPT(buf);

    /* a 17th party */
    n = sprintf(buf, "TREATY BIG USD SETL\n");
    for (int i = 0; i < 17; i++)
        n += sprintf(buf + n, "PARTY P%02d 100 0 %s\n", i, i < 16 ? "1/16" : "0/1");
    strcpy(buf + n, "END\n");
    REJECT(buf, ZXT_E_COUNT, 20u + 16u * 21u);

    /* a 65th obligation */
    n = sprintf(buf, "TREATY BIG USD SETL\nPARTY A 1 0 1/2\nPARTY B 1 0 1/2\n");
    int at = 0;
    for (int j = 0; j < 65; j++) {
        if (j == 64)
            at = n;
        n += sprintf(buf + n, "OWE A B 1\n");
    }
    strcpy(buf + n, "END\n");
    REJECT(buf, ZXT_E_COUNT, (uint32_t)at);

    /* the longest canonical text fits ZXT_SUTRA_MAX exactly */
    n = sprintf(buf, "TREATY ABCDEFGHIJKLMNOPQRSTUVWXYZ0123 USD SETL\n");
    for (int i = 0; i < 16; i++)
        n += sprintf(buf + n, "PARTY ABCDEF%02d 999999999999999999 999999999999999999 %s\n", i,
                     i < 15 ? "000000000/999999999" : "999999999/999999999");
    CHECK(n == 47 + 16 * 73, "max header+party length %d", n);
    /* (the zero weights above are not canonical, so only the length is
     * checked here; a canonical maximum would need 9-digit lowest-terms
     * weights summing to 1, which the fuzz covers) */
    CHECK(ZXT_SUTRA_MAX == 47u + 16u * 73u + 64u * 41u + 4u, "ZXT_SUTRA_MAX value");
    memset(buf, 'A', sizeof buf);
    zxt_treaty_t t;
    uint32_t off = 7;
    CHECK(zxt_parse_sutra(buf, ZXT_SUTRA_MAX + 1u, &t, &off) == ZXT_E_ARG && off == 0,
          "longer than ZXT_SUTRA_MAX");
    CHECK(zxt_parse_sutra(buf, ZXT_SUTRA_MAX, &t, &off) == ZXT_E_SYNTAX, "exactly max is read");
}

static void test_sutra_reject(void)
{
    REJECT("", ZXT_E_SYNTAX, 0);
    REJECT("TREATY", ZXT_E_SYNTAX, 0);
    REJECT("treaty N USD NET\nPARTY A 1 1 1/1\nEND\n", ZXT_E_SYNTAX, 0);
    REJECT("TREATY  N USD NET\nPARTY A 1 1 1/1\nEND\n", ZXT_E_NAME, 7);
    REJECT("TREATY n USD NET\nPARTY A 1 1 1/1\nEND\n", ZXT_E_NAME, 7);
    REJECT("TREATY 7N USD NET\nPARTY A 1 1 1/1\nEND\n", ZXT_E_NAME, 7);
    REJECT("TREATY N- USD NET\nPARTY A 1 1 1/1\nEND\n", ZXT_E_NAME, 7);
    REJECT("TREATY ABCDEFGHIJKLMNOPQRSTUVWXYZ01234 USD NET\nPARTY A 1 1 1/1\nEND\n", ZXT_E_NAME, 7);
    REJECT("TREATY N  USD NET\nPARTY A 1 1 1/1\nEND\n", ZXT_E_ASSET, 9);
    REJECT("TREATY N\tUSD NET\nPARTY A 1 1 1/1\nEND\n", ZXT_E_SYNTAX, 8);
    REJECT("TREATY N US NET\nPARTY A 1 1 1/1\nEND\n", ZXT_E_ASSET, 9);
    REJECT("TREATY N USDX NET\nPARTY A 1 1 1/1\nEND\n", ZXT_E_ASSET, 9);
    REJECT("TREATY N U1D NET\nPARTY A 1 1 1/1\nEND\n", ZXT_E_ASSET, 9);
    REJECT("TREATY N USD NETT\nPARTY A 1 1 1/1\nEND\n", ZXT_E_OP, 13);
    REJECT("TREATY N USD net\nPARTY A 1 1 1/1\nEND\n", ZXT_E_OP, 13);
    REJECT("TREATY N USD NET\r\nPARTY A 1 1 1/1\nEND\n", ZXT_E_OP, 13);
    REJECT("TREATY N USD SETL \nPARTY A 1 1 1/1\nEND\n", ZXT_E_OP, 13);
    /* lines */
    REJECT("TREATY N USD NET\nEND\n", ZXT_E_COUNT, 17);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/1\n", ZXT_E_SYNTAX, 33);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/1\nEND", ZXT_E_SYNTAX, 33);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/1\nEND\n\n", ZXT_E_SYNTAX, 37);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/1\nEND\nX", ZXT_E_SYNTAX, 37);
    REJECT("TREATY N USD NET\nOWE A B 1\nPARTY A 1 1 1/1\nEND\n", ZXT_E_SYNTAX, 17);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A B 1\nPARTY C 0 0 0/1\nEND\n",
           ZXT_E_SYNTAX, 59);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/1\n# note\nEND\n", ZXT_E_SYNTAX, 33);
    REJECT("TREATY N USD NET\nPARTY  A 1 1 1/1\nEND\n", ZXT_E_PARTY, 23);
    REJECT("TREATY N USD NET\nPARTY a 1 1 1/1\nEND\n", ZXT_E_PARTY, 23);
    REJECT("TREATY N USD NET\nPARTY 9A 1 1 1/1\nEND\n", ZXT_E_PARTY, 17);
    REJECT("TREATY N USD NET\nPARTY A- 1 1 1/1\nEND\n", ZXT_E_PARTY, 17);
    REJECT("TREATY N USD NET\nPARTY ABCDEFGHI 1 1 1/1\nEND\n", ZXT_E_PARTY, 23);
    REJECT("TREATY N USD NET\nPARTY A  1 1 1/1\nEND\n", ZXT_E_NUMBER, 25);
    REJECT("TREATY N USD NET\nPARTYA 1 1 1/1\nEND\n", ZXT_E_SYNTAX, 17);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/1 \nEND\n", ZXT_E_NUMBER, 29);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1 /1\nEND\n", ZXT_E_NUMBER, 29);
    /* numbers */
    REJECT("TREATY N USD NET\nPARTY A 01 1 1/1\nEND\n", ZXT_E_NUMBER, 25);
    REJECT("TREATY N USD NET\nPARTY A 00 0 1/1\nEND\n", ZXT_E_NUMBER, 25);
    REJECT("TREATY N USD NET\nPARTY A +1 1 1/1\nEND\n", ZXT_E_NUMBER, 25);
    REJECT("TREATY N USD NET\nPARTY A -1 0 1/1\nEND\n", ZXT_E_NUMBER, 25);
    REJECT("TREATY N USD NET\nPARTY A 1000000000000000000 1 1/1\nEND\n", ZXT_E_NUMBER, 25);
    REJECT("TREATY N USD NET\nPARTY A 5 1000000000000000000 1/1\nEND\n", ZXT_E_NUMBER, 27);
    REJECT("TREATY N USD NET\nPARTY A 5 01 1/1\nEND\n", ZXT_E_NUMBER, 27);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/1000000000\nEND\n", ZXT_E_NUMBER, 29);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1000000000/1\nEND\n", ZXT_E_NUMBER, 29);
    REJECT("TREATY N USD NET\nPARTY A 1 1 01/1\nEND\n", ZXT_E_NUMBER, 29);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/01\nEND\n", ZXT_E_NUMBER, 29);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1\nEND\n", ZXT_E_NUMBER, 29);
    /* R2 */
    REJECT("TREATY N USD NET\nPARTY A 5 6 1/1\nEND\n", ZXT_E_CLAIM, 17);
    REJECT("TREATY N USD NET\nPARTY A 0 1 1/1\nEND\n", ZXT_E_CLAIM, 17);
    ACCEPT("TREATY N USD NET\nPARTY A 5 5 1/1\nEND\n");
    /* R1 */
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nEND\n", ZXT_E_WEIGHT, 33);
    REJECT("TREATY N USD NET\nPARTY A 1 1 2/2\nEND\n", ZXT_E_WEIGHT, 17);
    REJECT("TREATY N USD NET\nPARTY A 1 1 0/2\nPARTY B 1 1 1/1\nEND\n", ZXT_E_WEIGHT, 17);
    REJECT("TREATY N USD NET\nPARTY A 1 1 3/2\nEND\n", ZXT_E_WEIGHT, 17);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/0\nEND\n", ZXT_E_WEIGHT, 17);
    REJECT("TREATY N USD NET\nPARTY A 1 1 0/0\nEND\n", ZXT_E_WEIGHT, 17);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/3\nEND\n", ZXT_E_WEIGHT, 49);
    REJECT("TREATY N USD NET\nPARTY A 1 1 2/3\nPARTY B 1 1 2/3\nEND\n", ZXT_E_WEIGHT, 49);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/1\nPARTY B 1 1 1/1\nEND\n", ZXT_E_WEIGHT, 49);
    REJECT("TREATY N USD NET\nPARTY A 1 1 0/1\nEND\n", ZXT_E_WEIGHT, 33);
    /* three large coprime denominators: their lcm leaves 64 bits */
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/999999937\nPARTY B 1 1 1/999999929\n"
           "PARTY C 1 1 1/999999893\nEND\n",
           ZXT_E_WEIGHT, 89);
    /* obligations */
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A A 1\nEND\n", ZXT_E_OWE, 49);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A B 0\nEND\n", ZXT_E_OWE, 49);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A C 1\nEND\n", ZXT_E_PARTY, 55);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE C B 1\nEND\n", ZXT_E_PARTY, 53);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A B 01\nEND\n", ZXT_E_NUMBER, 57);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A B -1\nEND\n", ZXT_E_NUMBER, 57);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A B 1000000000000000000\nEND\n",
           ZXT_E_NUMBER, 57);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A  B 1\nEND\n", ZXT_E_PARTY, 55);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A B 1 \nEND\n", ZXT_E_NUMBER, 57);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE  A B 1\nEND\n", ZXT_E_PARTY, 53);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY A 1 1 1/2\nEND\n", ZXT_E_PARTY, 33);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A B 5\x00\nEND\n", ZXT_E_NUMBER, 57);
}

static void test_args(void)
{
    zxt_treaty_t t;
    uint32_t off = 5;
    static char out[ZXT_SUTRA_MAX];
    static uint8_t rec[ZXT_REC_LEN + 1];
    uint32_t n = 77;
    CHECK(zxt_parse_sutra(BASE, (uint32_t)strlen(BASE), NULL, &off) == ZXT_E_ARG && off == 0,
          "sutra NULL out");
    memset(&t, 0x5A, sizeof t);
    CHECK(zxt_parse_sutra(NULL, 4, &t, &off) == ZXT_E_ARG && all_zero(&t, sizeof t),
          "sutra NULL src clears out");
    CHECK(zxt_parse_sutra(BASE, (uint32_t)strlen(BASE), &t, NULL) == ZXT_OK, "NULL err_off ok");
    CHECK(zxt_parse_sutra(BASE, (uint32_t)strlen(BASE) - 1u, &t, NULL) == ZXT_E_SYNTAX,
          "NULL err_off on failure");
    CHECK(zxt_parse_record(rec, ZXT_REC_LEN, NULL, &off) == ZXT_E_ARG, "record NULL out");
    memset(&t, 0x5A, sizeof t);
    CHECK(zxt_parse_record(NULL, ZXT_REC_LEN, &t, &off) == ZXT_E_ARG && all_zero(&t, sizeof t),
          "record NULL rec");
    CHECK(zxt_parse_record(rec, ZXT_REC_LEN - 1u, &t, &off) == ZXT_E_ARG, "record short");
    CHECK(zxt_parse_record(rec, ZXT_REC_LEN + 1u, &t, &off) == ZXT_E_ARG, "record long");
    CHECK(zxt_validate(NULL) == ZXT_E_ARG, "validate NULL");
    CHECK(!zxt_equal(NULL, &t) && !zxt_equal(&t, NULL), "equal NULL");
    zxt_clear(NULL);
    ps(BASE, &t, NULL);
    CHECK(zxt_emit_sutra(&t, NULL, 10, &n) == ZXT_E_ARG && n == 0, "emit sutra NULL dst");
    CHECK(zxt_emit_sutra(&t, out, sizeof out, NULL) == ZXT_E_ARG, "emit sutra NULL n");
    CHECK(zxt_emit_sutra(NULL, out, sizeof out, &n) == ZXT_E_ARG, "emit sutra NULL t");
    CHECK(zxt_emit_record(&t, NULL, ZXT_REC_LEN) == ZXT_E_ARG, "emit record NULL");
    CHECK(zxt_emit_record(NULL, rec, ZXT_REC_LEN) == ZXT_E_ARG, "emit record NULL t");
    /* capacity */
    uint32_t len = (uint32_t)strlen(BASE);
    CHECK(zxt_emit_sutra(&t, out, len, &n) == ZXT_OK && n == len, "emit sutra exact cap");
    n = 77;
    CHECK(zxt_emit_sutra(&t, out, len - 1u, &n) == ZXT_E_CAP && n == 0, "emit sutra cap-1");
    CHECK(zxt_emit_record(&t, rec, ZXT_REC_LEN - 1u) == ZXT_E_CAP, "emit record cap-1");
    CHECK(zxt_emit_record(&t, rec, ZXT_REC_LEN) == ZXT_OK, "emit record exact cap");
}

/* --------------------------------------------------------------- RECORD */

static void put(uint8_t *rec, uint32_t off, const char *s) { memcpy(rec + off, s, strlen(s)); }

static void base_record(uint8_t *rec)
{
    zxt_treaty_t t;
    ps(BASE, &t, NULL);
    zxt_emit_record(&t, rec, ZXT_REC_LEN);
}

static void rec_reject(const char *what, uint32_t off, const char *bytes, zxt_status_t want,
                       uint32_t want_off, int line)
{
    uint8_t rec[ZXT_REC_LEN];
    zxt_treaty_t t;
    uint32_t o = 999;
    base_record(rec);
    put(rec, off, bytes);
    memset(&t, 0xA5, sizeof t);
    zxt_status_t st = zxt_parse_record(rec, ZXT_REC_LEN, &t, &o);
    CHECK(st == want && o == want_off && (st == ZXT_OK || all_zero(&t, sizeof t)), "line %d %s: got %d@%u want %d@%u",
          line, what, st, o, want, want_off);
}
#define REC_REJECT(w, off, b, st, wo) rec_reject((w), (off), (b), (st), (wo), __LINE__)

#define P0 46u              /* party 0 */
#define P1 (46u + 64u)      /* party 1 */
#define P2 (46u + 128u)     /* party 2: unused in BASE */
#define W0 (46u + 1024u)    /* owe 0 */
#define W1 (W0 + 23u)       /* owe 1: unused in BASE */

static void test_record(void)
{
    uint8_t rec[ZXT_REC_LEN];
    zxt_treaty_t t, u;
    uint32_t off = 3;
    CHECK(ZXT_REC_LEN == 2542u, "record length");
    base_record(rec);
    CHECK(memcmp(rec, "ZXT1CORRIDOR-7                    USDSETL02001", 46) == 0, "header bytes");
    CHECK(memcmp(rec + P0, "ACME    +000000000000005000+000000000000004000000000001000000002", 64) == 0,
          "party 0 bytes");
    CHECK(memcmp(rec + P2, "        +000000000000000000+000000000000000000000000000000000000", 64) == 0,
          "unused party bytes (INITIALIZE)");
    CHECK(memcmp(rec + W0, "0102+000000000000001200", 23) == 0, "owe 0 bytes");
    CHECK(memcmp(rec + W1, "0000+000000000000000000", 23) == 0, "unused owe bytes");
    CHECK(memcmp(rec + ZXT_REC_LEN - 23u, "0000+000000000000000000", 23) == 0, "last owe slot");
    CHECK(zxt_parse_record(rec, ZXT_REC_LEN, &t, &off) == ZXT_OK && off == 0, "base record");
    CHECK(ps(BASE, &u, NULL) == ZXT_OK && zxt_equal(&t, &u), "record AST == sutra AST");

    rec[5] = 0; /* a NUL inside the name */
    CHECK(zxt_parse_record(rec, ZXT_REC_LEN, &t, &off) == ZXT_E_NAME && off == 4, "name NUL");
    rec[5] = 'O';
    rec[40] = 0; /* a NUL inside the op */
    CHECK(zxt_parse_record(rec, ZXT_REC_LEN, &t, &off) == ZXT_E_OP && off == 37, "op NUL");
    rec[40] = 'L';
    put(rec, 37, "NET ");
    CHECK(zxt_parse_record(rec, ZXT_REC_LEN, &t, &off) == ZXT_OK && t.op == ZXT_OP_NET, "op NET");
    /* negative numbers are representable but refused by the rules */
    REC_REJECT("magic", 0, "ZXT2", ZXT_E_MAGIC, 0);
    REC_REJECT("magic lower", 3, "t", ZXT_E_MAGIC, 0);
    REC_REJECT("name leading space", 4, " C", ZXT_E_NAME, 4);
    REC_REJECT("name gap", 14, " X", ZXT_E_NAME, 4);
    REC_REJECT("name lowercase", 5, "o", ZXT_E_NAME, 4);
    REC_REJECT("name blank", 4, "          ", ZXT_E_NAME, 4);
    REC_REJECT("name 30 chars", 4, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123", ZXT_OK, 0);
    REC_REJECT("asset", 34, "US ", ZXT_E_ASSET, 34);
    REC_REJECT("asset digit", 36, "1", ZXT_E_ASSET, 34);
    REC_REJECT("op", 37, "NETX", ZXT_E_OP, 37);
    REC_REJECT("op lower", 37, "setl", ZXT_E_OP, 37);
    REC_REJECT("op SETL prefix", 37, "SET ", ZXT_E_OP, 37);
    REC_REJECT("nparty 00", 41, "00", ZXT_E_COUNT, 41);
    REC_REJECT("nparty 17", 41, "17", ZXT_E_COUNT, 41);
    REC_REJECT("nparty space", 41, " 2", ZXT_E_COUNT, 41);
    REC_REJECT("nowe 065", 43, "065", ZXT_E_COUNT, 43);
    REC_REJECT("nowe x", 43, "00x", ZXT_E_COUNT, 43);
    REC_REJECT("nparty 03 meets blank slot", 41, "03", ZXT_E_PARTY, P2);
    REC_REJECT("nparty 01 leaves weight 1/2", 41, "01", ZXT_E_PAD, P1);
    REC_REJECT("nowe 000 leaves slot", 43, "000", ZXT_E_PAD, W0);
    REC_REJECT("nowe 002 meets blank slot", 43, "002", ZXT_E_PARTY, W1);
    /* party fields */
    REC_REJECT("id blank", P0, "        ", ZXT_E_PARTY, P0);
    REC_REJECT("id gap", P0 + 4, " X", ZXT_E_PARTY, P0);
    REC_REJECT("id digit first", P0, "9", ZXT_E_PARTY, P0);
    REC_REJECT("id dup", P1, "ACME", ZXT_E_PARTY, P1);
    REC_REJECT("id 8 chars", P0, "ACMEACME", ZXT_OK, 0);
    REC_REJECT("deposit sign", P0 + 8, "*", ZXT_E_NUMBER, P0 + 8);
    REC_REJECT("deposit space", P0 + 8, " ", ZXT_E_NUMBER, P0 + 8);
    REC_REJECT("deposit digit", P0 + 26, "x", ZXT_E_NUMBER, P0 + 8);
    REC_REJECT("deposit negative", P0 + 8, "-", ZXT_E_NUMBER, P0);
    REC_REJECT("deposit -0", P2 + 8, "-", ZXT_E_PAD, P2);
    REC_REJECT("claim -0", P0 + 27, "-000000000000000000", ZXT_E_NUMBER, P0 + 27);
    REC_REJECT("claim sign", P0 + 27, "?", ZXT_E_NUMBER, P0 + 27);
    REC_REJECT("claim > deposit", P0 + 27, "+000000000000005001", ZXT_E_CLAIM, P0);
    REC_REJECT("claim == deposit", P0 + 27, "+000000000000005000", ZXT_OK, 0);
    REC_REJECT("claim negative", P0 + 27, "-000000000000000001", ZXT_E_NUMBER, P0);
    REC_REJECT("deposit -0 used", P0 + 8, "-000000000000000000", ZXT_E_NUMBER, P0 + 8);
    REC_REJECT("wnum digit", P0 + 46, "x", ZXT_E_NUMBER, P0 + 46);
    REC_REJECT("wden digit", P0 + 63, "x", ZXT_E_NUMBER, P0 + 46);
    REC_REJECT("weight 2/4", P0 + 46, "000000002000000004", ZXT_E_WEIGHT, P0);
    REC_REJECT("weight sum", P1 + 46, "000000001000000003", ZXT_E_WEIGHT, 46);
    /* unused party slot pads */
    REC_REJECT("pad id", P2 + 7, "A", ZXT_E_PAD, P2);
    REC_REJECT("pad deposit", P2 + 26, "1", ZXT_E_PAD, P2);
    REC_REJECT("pad deposit sign", P2 + 8, " ", ZXT_E_PAD, P2);
    REC_REJECT("pad claim", P2 + 45, "1", ZXT_E_PAD, P2);
    REC_REJECT("pad claim sign", P2 + 27, "-", ZXT_E_PAD, P2);
    REC_REJECT("pad wnum", P2 + 46, "1", ZXT_E_PAD, P2);
    REC_REJECT("pad wden", P2 + 63, "1", ZXT_E_PAD, P2);
    REC_REJECT("pad last party", 46u + 15u * 64u + 63u, "1", ZXT_E_PAD, 46u + 15u * 64u);
    /* obligations */
    REC_REJECT("owe from 00", W0, "00", ZXT_E_PARTY, W0);
    REC_REJECT("owe to 00", W0 + 2, "00", ZXT_E_PARTY, W0);
    REC_REJECT("owe from 03", W0, "03", ZXT_E_PARTY, W0);
    REC_REJECT("owe to 03", W0 + 2, "03", ZXT_E_PARTY, W0);
    REC_REJECT("owe self", W0, "02", ZXT_E_OWE, W0);
    REC_REJECT("owe reverse", W0, "0201", ZXT_OK, 0);
    REC_REJECT("owe zero", W0 + 4, "+000000000000000000", ZXT_E_OWE, W0);
    REC_REJECT("owe negative", W0 + 4, "-000000000000000005", ZXT_E_OWE, W0);
    REC_REJECT("owe -0", W0 + 4, "-000000000000000000", ZXT_E_NUMBER, W0);
    REC_REJECT("owe digit", W0 + 1, "x", ZXT_E_NUMBER, W0);
    REC_REJECT("owe to digit", W0 + 3, "x", ZXT_E_NUMBER, W0);
    REC_REJECT("pad owe from", W1 + 1, "1", ZXT_E_PAD, W1);
    REC_REJECT("pad owe to", W1 + 3, "1", ZXT_E_PAD, W1);
    REC_REJECT("pad owe amount", W1 + 22, "1", ZXT_E_PAD, W1);
    REC_REJECT("pad owe sign", W1 + 4, "-", ZXT_E_PAD, W1);
    REC_REJECT("pad last owe", ZXT_REC_LEN - 1u, "1", ZXT_E_PAD, ZXT_REC_LEN - 23u);
}

/* --------------------------------------------------------------- validate */

static void test_validate(void)
{
    zxt_treaty_t t, u;
    ps(BASE, &t, NULL);
    CHECK(zxt_validate(&t) == ZXT_OK, "base valid");
#define V(mut, want)                                                                               \
    do {                                                                                           \
        u = t;                                                                                     \
        mut;                                                                                       \
        zxt_status_t s_ = zxt_validate(&u);                                                        \
        CHECK(s_ == (want), "validate %s: got %d want %d", #mut, s_, (want));                      \
    } while (0)
    V(u.name[12] = 'X', ZXT_E_NAME);
    V(u.name[0] = 0, ZXT_E_NAME);
    V(memset(u.name, 'A', sizeof u.name), ZXT_E_NAME);
    V(u.asset[3] = 'X', ZXT_E_ASSET);
    V(u.asset[0] = 'u', ZXT_E_ASSET);
    V(u.asset[1] = '@', ZXT_E_ASSET);
    V(u.asset[2] = '[', ZXT_E_ASSET);
    V(u.op = 0, ZXT_E_OP);
    V(u.op = 3, ZXT_E_OP);
    V(u.op = ZXT_OP_NET, ZXT_OK);
    V(u.n_party = 0, ZXT_E_COUNT);
    V(u.n_party = 17, ZXT_E_COUNT);
    V(u.n_owe = 65, ZXT_E_COUNT);
    V(u.n_owe = 64, ZXT_E_OWE);
    V(u.n_party = 1, ZXT_E_PAD);
    V(u.n_party = 3, ZXT_E_PARTY);
    V(u.party[2].w_den = 1, ZXT_E_PAD);
    V(u.party[2].w_num = 1, ZXT_E_PAD);
    V(u.party[2].deposit = 1, ZXT_E_PAD);
    V(u.party[2].claim = 1, ZXT_E_PAD);
    V(u.party[2].id[8] = 'A', ZXT_E_PAD);
    V(u.party[15].id[0] = 'A', ZXT_E_PAD);
    V(u.owe[1].from = 1, ZXT_E_PAD);
    V(u.owe[1].to = 1, ZXT_E_PAD);
    V(u.owe[1].amount = 1, ZXT_E_PAD);
    V(u.owe[63].amount = 1, ZXT_E_PAD);
    V(u.party[0].id[5] = 'Z', ZXT_E_PARTY);
    V(u.party[0].deposit = ZXT_PIC18_MAX + 1, ZXT_E_NUMBER);
    V(u.party[0].deposit = -1, ZXT_E_NUMBER);
    V((u.party[0].deposit = ZXT_PIC18_MAX, u.party[0].claim = ZXT_PIC18_MAX), ZXT_OK);
    V((u.party[0].deposit = ZXT_PIC18_MAX + 1, u.party[0].claim = 0), ZXT_E_NUMBER);
    V(u.party[0].claim = -1, ZXT_E_NUMBER);
    V(u.party[0].claim = 5001, ZXT_E_CLAIM);
    V(u.party[0].claim = 5000, ZXT_OK);
    V((u.party[0].w_num = 0, u.party[0].w_den = 0), ZXT_E_WEIGHT);
    V((u.party[0].w_num = 1000000000u, u.party[0].w_den = 2000000000u), ZXT_E_WEIGHT);
    V((u.party[0].w_num = 1u, u.party[0].w_den = 1000000000u), ZXT_E_WEIGHT);
    V((u.party[0].w_num = 499999999u, u.party[0].w_den = 999999999u, u.party[1].w_num = 500000000u,
       u.party[1].w_den = 999999999u),
      ZXT_OK);
    V((u.party[0].w_num = 3u, u.party[0].w_den = 2u), ZXT_E_WEIGHT);
    V(u.owe[0].from = 2, ZXT_E_PARTY);
    V(u.owe[0].to = 2, ZXT_E_PARTY);
    V(u.owe[0].to = 0, ZXT_E_OWE);
    V(u.owe[0].amount = 0, ZXT_E_OWE);
    V(u.owe[0].amount = -1, ZXT_E_OWE);
    V(u.owe[0].amount = ZXT_PIC18_MAX, ZXT_OK);
    V(u.owe[0].amount = ZXT_PIC18_MAX + 1, ZXT_E_NUMBER);
    V(u.owe[0].amount = -ZXT_PIC18_MAX - 1, ZXT_E_NUMBER);
#undef V
}

static void test_equal(void)
{
    zxt_treaty_t t, u;
    ps(BASE, &t, NULL);
#define NE(mut)                                                                                    \
    do {                                                                                           \
        u = t;                                                                                     \
        mut;                                                                                       \
        CHECK(!zxt_equal(&t, &u), "equal ignores %s", #mut);                                       \
    } while (0)
    u = t;
    CHECK(zxt_equal(&t, &u), "equal self");
    NE(u.name[30] = 1);
    NE(u.asset[3] = 1);
    NE(u.op = 1);
    NE(u.n_party = 3);
    NE(u.n_owe = 2);
    NE(u.party[15].id[8] = 1);
    NE(u.party[15].deposit = 1);
    NE(u.party[15].claim = 1);
    NE(u.party[15].w_num = 1);
    NE(u.party[15].w_den = 1);
    NE(u.owe[63].from = 1);
    NE(u.owe[63].to = 1);
    NE(u.owe[63].amount = 1);
#undef NE
    /* a parse of a different treaty into a used struct leaves no trace */
    CHECK(ps("TREATY N USD NET\nPARTY A 1 1 1/1\nEND\n", &u, NULL) == ZXT_OK, "reuse parse");
    CHECK(u.party[1].id[0] == 0 && u.party[1].deposit == 0 && u.owe[0].amount == 0, "reuse is clean");
}

int main(void)
{
    test_pic();
    test_sutra_base();
    test_sutra_limits();
    test_sutra_reject();
    test_args();
    test_record();
    test_validate();
    test_equal();
    printf("%s: %d passed, %d failed\n", g_fail ? "FAIL" : "PASS", g_pass, g_fail);
    printf("%d checks\n", g_pass + g_fail);
    return g_fail ? 1 : 0;
}
