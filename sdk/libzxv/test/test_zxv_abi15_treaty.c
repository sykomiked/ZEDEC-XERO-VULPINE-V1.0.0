/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_abi15_treaty.c — conformance test of the ABI 1.5 treaty group,
 * linked against the shared library only (no kernel headers). The known
 * answers are the kernel's own (kernel/src/treaty/test_zxt.c and
 * test_zxt_exec.c): every Sutra and record refusal with its fault and byte
 * offset, the copybook bytes laid out by copybooks/ZXVTRTY.cpy and
 * ZXVRSLT.cpy, the PIC S9(18) edges, and the settlement, fee and dividend
 * numbers, with the dividend rule recomputed here in 128-bit arithmetic. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "zxv_api.h"

static int checks, failures;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)
#define OK(call)                                                                                   \
    do {                                                                                           \
        int32_t rc_ = (call);                                                                      \
        CHECK(rc_ == ZXV_OK, "%s -> %s", #call, zxv_status_name(rc_));                             \
    } while (0)
#define IS(call, want)                                                                             \
    do {                                                                                           \
        int32_t rc_ = (call);                                                                      \
        CHECK(rc_ == (want), "%s -> %s, want %s", #call, zxv_status_name(rc_), #want);             \
    } while (0)

#define PIC_MAX ZXV_TREATY_PIC18_MAX

static const char BASE[] = "TREATY CORRIDOR-7 USD SETL\n"
                           "PARTY ACME 5000 4000 1/2\n"
                           "PARTY BETA 3000 3000 1/2\n"
                           "OWE ACME BETA 1200\n"
                           "END\n";

static const char TRI[] = "TREATY CORRIDOR-7 USD SETL\n"
                          "PARTY ACME 5000000 4000 1/3\n"
                          "PARTY BETA 3000000 3000 1/7\n"
                          "PARTY GAMMA 900000 0 11/21\n"
                          "OWE ACME BETA 1200000\n"
                          "OWE BETA GAMMA 700000\n"
                          "OWE GAMMA ACME 100000\n"
                          "END\n";

static bool last_error_has(const char *needle)
{
    char buf[512];
    size_t n = 0;
    if (zxv_last_error(buf, sizeof buf, &n) != ZXV_OK) return false;
    return strstr(buf, needle) != NULL;
}

static zxv_treaty *make(const char *text)
{
    zxv_treaty *t = NULL;
    uint32_t off = 77;
    int32_t fault = 77;
    OK(zxv_treaty_create(&t));
    int32_t rc = zxv_treaty_parse_sutra(t, text, &off, &fault);
    CHECK(rc == ZXV_OK && off == 0 && fault == ZXV_TREATY_FAULT_NONE, "fixture parses: %s %d@%u",
          zxv_status_name(rc), fault, off);
    return t;
}

static void base_record(uint8_t rec[ZXV_TREATY_REC_LEN])
{
    zxv_treaty *t = make(BASE);
    size_t n = 0;
    OK(zxv_treaty_emit_record(t, rec, ZXV_TREATY_REC_LEN, &n));
    zxv_treaty_destroy(t);
}

/* ------------------------------------------------------------ lifecycle */

static void test_lifecycle(void)
{
    uint32_t m = 0;
    OK(zxv_features2(&m));
    CHECK(m & ZXV_FEAT2_TREATY, "ZXV_FEAT2_TREATY live");
    CHECK(ZXV_TREATY_REC_LEN == 2542u && ZXV_TREATY_RESULT_LEN == 2422u, "record lengths");
    CHECK(ZXV_TREATY_SUTRA_MAX == 47u + 16u * 73u + 64u * 41u + 4u, "Sutra max");
    IS(zxv_status_class(ZXV_E_TREATY), ZXV_CLASS_USAGE);

    IS(zxv_treaty_create(NULL), ZXV_E_ARG);
    zxv_treaty_destroy(NULL);
    zxv_treaty_result_destroy(NULL);
    IS(zxv_treaty_clear(NULL), ZXV_E_ARG);

    zxv_treaty *t = NULL, *u = NULL;
    OK(zxv_treaty_create(&t));
    OK(zxv_treaty_create(&u));
    CHECK(t && u && t != u, "distinct handles");

    /* empty: fails validation with COUNT, emits nothing, getters read zeros */
    int32_t fault = 77, op = 77, eq = 77;
    uint32_t np = 77, nw = 77;
    IS(zxv_treaty_validate(t, &fault), ZXV_E_TREATY);
    CHECK(fault == ZXV_TREATY_FAULT_NAME, "empty fault %d", fault);
    IS(zxv_treaty_validate(t, NULL), ZXV_E_TREATY);
    IS(zxv_treaty_validate(NULL, &fault), ZXV_E_ARG);
    char buf[64];
    size_t n = 99;
    IS(zxv_treaty_emit_sutra(t, buf, sizeof buf, &n), ZXV_E_TREATY);
    CHECK(n == 99, "emit_sutra on empty leaves out_len");
    uint8_t rec[ZXV_TREATY_REC_LEN];
    IS(zxv_treaty_emit_record(t, rec, sizeof rec, &n), ZXV_E_TREATY);
    CHECK(n == 99, "emit_record on empty leaves out_len");
    OK(zxv_treaty_name(t, buf, sizeof buf, &n));
    CHECK(n == 0 && buf[0] == 0, "empty name");
    OK(zxv_treaty_asset(t, buf, sizeof buf, &n));
    CHECK(n == 0 && buf[0] == 0, "empty asset");
    OK(zxv_treaty_header(t, &op, &np, &nw));
    CHECK(op == 0 && np == 0 && nw == 0, "empty header");
    IS(zxv_treaty_party_id(t, 0, buf, sizeof buf, &n), ZXV_E_NOT_FOUND);
    OK(zxv_treaty_equal(t, u, &eq));
    CHECK(eq == 1, "two empties are equal");
    zxv_treaty_result *r = (zxv_treaty_result *) 1;
    IS(zxv_treaty_run(t, &r), ZXV_E_TREATY);
    CHECK(r == (zxv_treaty_result *) 1, "run on empty leaves out");

    /* parse, then clear: equal to a fresh one again */
    OK(zxv_treaty_parse_sutra(t, BASE, NULL, NULL));
    OK(zxv_treaty_equal(t, u, &eq));
    CHECK(eq == 0, "parsed != empty");
    OK(zxv_treaty_clear(t));
    OK(zxv_treaty_equal(t, u, &eq));
    CHECK(eq == 1, "clear == create");
    zxv_treaty_destroy(t);
    zxv_treaty_destroy(u);
}

/* ------------------------------------------------------------ Sutra */

static void test_sutra_base(void)
{
    zxv_treaty *t = make(BASE);
    char buf[ZXV_TREATY_SUTRA_MAX + 1];
    size_t n = 0;
    int32_t op = 0;
    uint32_t np = 0, nw = 0;
    OK(zxv_treaty_validate(t, NULL));
    int32_t fault = 77;
    OK(zxv_treaty_validate(t, &fault));
    CHECK(fault == ZXV_TREATY_FAULT_NONE, "valid fault NONE");
    OK(zxv_treaty_name(t, buf, sizeof buf, &n));
    CHECK(n == 10 && strcmp(buf, "CORRIDOR-7") == 0, "name %s", buf);
    OK(zxv_treaty_asset(t, buf, sizeof buf, &n));
    CHECK(n == 3 && strcmp(buf, "USD") == 0, "asset");
    OK(zxv_treaty_header(t, &op, &np, &nw));
    CHECK(op == ZXV_TREATY_OP_SETL && np == 2 && nw == 1, "header %d %u %u", op, np, nw);
    OK(zxv_treaty_party_id(t, 0, buf, sizeof buf, &n));
    CHECK(n == 4 && strcmp(buf, "ACME") == 0, "party 0 id");
    OK(zxv_treaty_party_id(t, 1, buf, sizeof buf, &n));
    CHECK(strcmp(buf, "BETA") == 0, "party 1 id");
    IS(zxv_treaty_party_id(t, 2, buf, sizeof buf, &n), ZXV_E_NOT_FOUND);
    int64_t dep = 0, claim = 0, amt = 0;
    uint32_t wn = 0, wd = 0, from = 9, to = 9;
    OK(zxv_treaty_party(t, 0, &dep, &claim, &wn, &wd));
    CHECK(dep == 5000 && claim == 4000 && wn == 1 && wd == 2, "party 0");
    OK(zxv_treaty_party(t, 1, &dep, &claim, &wn, &wd));
    CHECK(dep == 3000 && claim == 3000 && wn == 1 && wd == 2, "party 1");
    IS(zxv_treaty_party(t, 2, &dep, &claim, &wn, &wd), ZXV_E_NOT_FOUND);
    IS(zxv_treaty_party(t, UINT32_MAX, &dep, &claim, &wn, &wd), ZXV_E_NOT_FOUND);
    OK(zxv_treaty_owe(t, 0, &from, &to, &amt));
    CHECK(from == 0 && to == 1 && amt == 1200, "owe 0");
    IS(zxv_treaty_owe(t, 1, &from, &to, &amt), ZXV_E_NOT_FOUND);

    /* NULL arguments on every getter */
    IS(zxv_treaty_name(NULL, buf, sizeof buf, &n), ZXV_E_ARG);
    IS(zxv_treaty_name(t, buf, sizeof buf, NULL), ZXV_E_ARG);
    IS(zxv_treaty_asset(NULL, buf, sizeof buf, &n), ZXV_E_ARG);
    IS(zxv_treaty_asset(t, buf, sizeof buf, NULL), ZXV_E_ARG);
    IS(zxv_treaty_header(NULL, &op, &np, &nw), ZXV_E_ARG);
    IS(zxv_treaty_header(t, NULL, &np, &nw), ZXV_E_ARG);
    IS(zxv_treaty_header(t, &op, NULL, &nw), ZXV_E_ARG);
    IS(zxv_treaty_header(t, &op, &np, NULL), ZXV_E_ARG);
    IS(zxv_treaty_party_id(NULL, 0, buf, sizeof buf, &n), ZXV_E_ARG);
    IS(zxv_treaty_party_id(t, 0, buf, sizeof buf, NULL), ZXV_E_ARG);
    IS(zxv_treaty_party(NULL, 0, &dep, &claim, &wn, &wd), ZXV_E_ARG);
    IS(zxv_treaty_party(t, 0, NULL, &claim, &wn, &wd), ZXV_E_ARG);
    IS(zxv_treaty_party(t, 0, &dep, NULL, &wn, &wd), ZXV_E_ARG);
    IS(zxv_treaty_party(t, 0, &dep, &claim, NULL, &wd), ZXV_E_ARG);
    IS(zxv_treaty_party(t, 0, &dep, &claim, &wn, NULL), ZXV_E_ARG);
    IS(zxv_treaty_owe(NULL, 0, &from, &to, &amt), ZXV_E_ARG);
    IS(zxv_treaty_owe(t, 0, NULL, &to, &amt), ZXV_E_ARG);
    IS(zxv_treaty_owe(t, 0, &from, NULL, &amt), ZXV_E_ARG);
    IS(zxv_treaty_owe(t, 0, &from, &to, NULL), ZXV_E_ARG);

    /* R4 text out: two-call */
    size_t len = strlen(BASE);
    n = 0;
    IS(zxv_treaty_emit_sutra(t, NULL, 0, &n), ZXV_E_BUFFER);
    CHECK(n == len, "needed %zu", n);
    memset(buf, 'Z', sizeof buf);
    IS(zxv_treaty_emit_sutra(t, buf, len, &n), ZXV_E_BUFFER);
    CHECK(buf[0] == 'Z', "BUFFER writes nothing");
    OK(zxv_treaty_emit_sutra(t, buf, len + 1, &n));
    CHECK(n == len && strcmp(buf, BASE) == 0, "Sutra round trip");
    IS(zxv_treaty_emit_sutra(NULL, buf, sizeof buf, &n), ZXV_E_ARG);
    IS(zxv_treaty_emit_sutra(t, buf, sizeof buf, NULL), ZXV_E_ARG);
    OK(zxv_treaty_name(t, buf, 11, &n));
    IS(zxv_treaty_name(t, buf, 10, &n), ZXV_E_BUFFER);
    CHECK(n == 10, "name needed");

    /* the copybook record (bytes the kernel test pins) */
    static uint8_t rec[ZXV_TREATY_REC_LEN + 4];
    n = 0;
    IS(zxv_treaty_emit_record(t, NULL, 0, &n), ZXV_E_BUFFER);
    CHECK(n == ZXV_TREATY_REC_LEN, "record needed %zu", n);
    memset(rec, 0xEE, sizeof rec);
    IS(zxv_treaty_emit_record(t, rec, ZXV_TREATY_REC_LEN - 1, &n), ZXV_E_BUFFER);
    CHECK(rec[0] == 0xEE && n == ZXV_TREATY_REC_LEN, "record BUFFER writes nothing");
    IS(zxv_treaty_emit_record(NULL, rec, sizeof rec, &n), ZXV_E_ARG);
    IS(zxv_treaty_emit_record(t, rec, sizeof rec, NULL), ZXV_E_ARG);
    OK(zxv_treaty_emit_record(t, rec, sizeof rec, &n));
    CHECK(n == ZXV_TREATY_REC_LEN && rec[ZXV_TREATY_REC_LEN] == 0xEE, "record exactly 2542 bytes");
    CHECK(memcmp(rec, "ZXT1CORRIDOR-7                    USDSETL02001", 46) == 0, "header bytes");
    CHECK(memcmp(rec + 46, "ACME    +000000000000005000+000000000000004000000000001000000002", 64) ==
              0,
          "party 0 bytes");
    CHECK(memcmp(rec + 46 + 128, "        +000000000000000000+000000000000000000000000000000000000",
                 64) == 0,
          "unused party bytes (INITIALIZE)");
    CHECK(memcmp(rec + 1070, "0102+000000000000001200", 23) == 0, "owe 0 bytes");
    CHECK(memcmp(rec + 1093, "0000+000000000000000000", 23) == 0, "unused owe bytes");
    CHECK(memcmp(rec + ZXV_TREATY_REC_LEN - 23, "0000+000000000000000000", 23) == 0,
          "last owe slot");
    OK(zxv_treaty_emit_record(t, rec, ZXV_TREATY_REC_LEN, &n));

    /* record -> treaty == Sutra -> treaty */
    zxv_treaty *u = NULL;
    int32_t eq = 0;
    uint32_t off = 5;
    fault = 5;
    OK(zxv_treaty_create(&u));
    OK(zxv_treaty_parse_record(u, rec, ZXV_TREATY_REC_LEN, &off, &fault));
    CHECK(off == 0 && fault == 0, "record parse clears fault outs");
    OK(zxv_treaty_equal(t, u, &eq));
    CHECK(eq == 1, "record AST == Sutra AST");
    IS(zxv_treaty_equal(NULL, u, &eq), ZXV_E_ARG);
    IS(zxv_treaty_equal(t, NULL, &eq), ZXV_E_ARG);
    IS(zxv_treaty_equal(t, u, NULL), ZXV_E_ARG);
    OK(zxv_treaty_parse_sutra(u, "TREATY CORRIDOR-7 USD NET\nPARTY ACME 5000 4000 1/2\n"
                                 "PARTY BETA 3000 3000 1/2\nOWE ACME BETA 1200\nEND\n",
                              NULL, NULL));
    OK(zxv_treaty_equal(t, u, &eq));
    CHECK(eq == 0, "NET != SETL");
    OK(zxv_treaty_header(u, &op, &np, &nw));
    CHECK(op == ZXV_TREATY_OP_NET, "op NET");
    /* a parse of a smaller treaty into a used handle leaves no trace */
    OK(zxv_treaty_parse_sutra(u, "TREATY N USD NET\nPARTY A 1 1 1/1\nEND\n", NULL, NULL));
    OK(zxv_treaty_header(u, &op, &np, &nw));
    CHECK(np == 1 && nw == 0, "reuse is clean");
    IS(zxv_treaty_party(u, 1, &dep, &claim, &wn, &wd), ZXV_E_NOT_FOUND);
    zxv_treaty_destroy(u);
    zxv_treaty_destroy(t);
}

/* Accept `s`, re-emit it byte for byte, and carry the same treaty through
 * the record form. */
static void accept_rt(const char *s, int line)
{
    zxv_treaty *t = NULL, *u = NULL;
    static char out[ZXV_TREATY_SUTRA_MAX + 1];
    static uint8_t rec[ZXV_TREATY_REC_LEN];
    size_t n = 0;
    uint32_t off = 9;
    int32_t fault = 9, eq = 0;
    OK(zxv_treaty_create(&t));
    OK(zxv_treaty_create(&u));
    int32_t rc = zxv_treaty_parse_sutra(t, s, &off, &fault);
    CHECK(rc == ZXV_OK && off == 0 && fault == 0, "line %d: parse %s %d@%u", line,
          zxv_status_name(rc), fault, off);
    OK(zxv_treaty_validate(t, NULL));
    rc = zxv_treaty_emit_sutra(t, out, sizeof out, &n);
    CHECK(rc == ZXV_OK && n == strlen(s) && strcmp(out, s) == 0, "line %d: Sutra round trip",
          line);
    OK(zxv_treaty_emit_record(t, rec, sizeof rec, &n));
    OK(zxv_treaty_parse_record(u, rec, sizeof rec, NULL, NULL));
    OK(zxv_treaty_equal(t, u, &eq));
    CHECK(eq == 1, "line %d: record round trip", line);
    zxv_treaty_destroy(t);
    zxv_treaty_destroy(u);
}
#define ACCEPT(s) accept_rt((s), __LINE__)

/* Refuse `s` with ZXV_E_TREATY, `fault` at `want_off`, leaving the handle
 * holding what it held before (BASE). */
static void reject(const char *s, int32_t want_fault, uint32_t want_off, int line)
{
    zxv_treaty *t = make(BASE), *ref = make(BASE);
    uint32_t off = 12345;
    int32_t fault = 12345, eq = 0;
    int32_t rc = zxv_treaty_parse_sutra(t, s, &off, &fault);
    CHECK(rc == ZXV_E_TREATY && fault == want_fault && off == want_off,
          "line %d: got %s %d@%u want %d@%u", line, zxv_status_name(rc), fault, off, want_fault,
          want_off);
    OK(zxv_treaty_equal(t, ref, &eq));
    CHECK(eq == 1, "line %d: refused parse left the handle unchanged", line);
    zxv_treaty_destroy(t);
    zxv_treaty_destroy(ref);
}
#define REJECT(s, f, off) reject((s), ZXV_TREATY_FAULT_##f, (off), __LINE__)

static void test_sutra_accept(void)
{
    ACCEPT(BASE);
    ACCEPT(TRI);
    ACCEPT("TREATY N USD NET\nPARTY A 0 0 1/1\nEND\n");
    ACCEPT("TREATY N USD NET\nPARTY A 0 0 0/1\nPARTY B 1 1 1/1\nOWE B A 1\nOWE A B "
           "999999999999999999\nEND\n");
    ACCEPT("TREATY ABCDEFGHIJKLMNOPQRSTUVWXYZ0123 EUR NET\n"
           "PARTY ABCDEFGH 999999999999999999 999999999999999999 1/999999937\n"
           "PARTY Z9-Z 1 0 999999936/999999937\nEND\n");
    ACCEPT("TREATY A-1 XAU SETL\nPARTY A 10 10 1/3\nPARTY B 10 9 1/3\nPARTY C 0 0 1/3\n"
           "OWE A B 5\nOWE B C 5\nOWE C A 5\nEND\n");
    ACCEPT("TREATY N USD NET\nPARTY A 5 5 1/1\nEND\n");

    /* 16 parties and 64 obligations: the most a treaty holds */
    static char buf[ZXV_TREATY_SUTRA_MAX + 64];
    int n = sprintf(buf, "TREATY BIG USD SETL\n");
    for (int i = 0; i < 16; i++) n += sprintf(buf + n, "PARTY P%02d 100 0 1/16\n", i);
    for (int j = 0; j < 64; j++) n += sprintf(buf + n, "OWE P%02d P%02d 1\n", j % 16, (j + 1) % 16);
    strcpy(buf + n, "END\n");
    ACCEPT(buf);
    zxv_treaty *t = make(buf);
    int32_t op;
    uint32_t np, nw, from, to;
    int64_t amt;
    OK(zxv_treaty_header(t, &op, &np, &nw));
    CHECK(np == 16 && nw == 64, "16/64");
    OK(zxv_treaty_owe(t, 63, &from, &to, &amt));
    CHECK(from == 15 && to == 0 && amt == 1, "owe 63");
    IS(zxv_treaty_owe(t, 64, &from, &to, &amt), ZXV_E_NOT_FOUND);
    zxv_treaty_destroy(t);

    /* a 17th party */
    n = sprintf(buf, "TREATY BIG USD SETL\n");
    for (int i = 0; i < 17; i++)
        n += sprintf(buf + n, "PARTY P%02d 100 0 %s\n", i, i < 16 ? "1/16" : "0/1");
    strcpy(buf + n, "END\n");
    REJECT(buf, COUNT, 20u + 16u * 21u);
    /* a 65th obligation */
    n = sprintf(buf, "TREATY BIG USD SETL\nPARTY A 1 0 1/2\nPARTY B 1 0 1/2\n");
    int at = 0;
    for (int j = 0; j < 65; j++) {
        if (j == 64) at = n;
        n += sprintf(buf + n, "OWE A B 1\n");
    }
    strcpy(buf + n, "END\n");
    REJECT(buf, COUNT, (uint32_t) at);

    /* length limits: exactly the maximum is read (and refused by the
     * grammar), one more byte is TOO_LONG before any parse */
    memset(buf, 'A', ZXV_TREATY_SUTRA_MAX);
    buf[ZXV_TREATY_SUTRA_MAX] = 0;
    REJECT(buf, SYNTAX, 0);
    buf[ZXV_TREATY_SUTRA_MAX] = 'A';
    buf[ZXV_TREATY_SUTRA_MAX + 1] = 0;
    t = make(BASE);
    uint32_t off = 7;
    int32_t fault = 7;
    IS(zxv_treaty_parse_sutra(t, buf, &off, &fault), ZXV_E_TOO_LONG);
    CHECK(off == 7 && fault == 7, "TOO_LONG leaves the fault outs");
    IS(zxv_treaty_parse_sutra(t, "TREATY N USD NET\nPARTY A 1 1 1/1\nEND\n\xff", &off, &fault),
       ZXV_E_UTF8);
    CHECK(off == 7 && fault == 7, "UTF8 leaves the fault outs");
    IS(zxv_treaty_parse_sutra(NULL, BASE, &off, &fault), ZXV_E_ARG);
    IS(zxv_treaty_parse_sutra(t, NULL, &off, &fault), ZXV_E_ARG);
    zxv_treaty_destroy(t);
    /* valid UTF-8 outside ASCII is ZXV_E_TREATY: SYNTAX right after a token... */
    REJECT("TREATY N\xc3\x89 USD NET\nPARTY A 1 1 1/1\nEND\n", SYNTAX, 8);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/1\nEND\n\xc3\x89", SYNTAX, 37);
    /* ...but where a field is read whole, the field's own fault */
    REJECT("TREATY N USD NET\nPARTY \xc3\x89 1 1 1/1\nEND\n", PARTY, 23);
    REJECT("TREATY N US\xc3\x89 NET\nPARTY A 1 1 1/1\nEND\n", ASSET, 9);
    REJECT("TREATY \xc3\x89 USD NET\nPARTY A 1 1 1/1\nEND\n", NAME, 7);

    /* the diagnostic names the fault and the byte */
    t = make(BASE);
    IS(zxv_treaty_parse_sutra(t, "TREATY N USD NET\nPARTY A 5 6 1/1\nEND\n", NULL, NULL),
       ZXV_E_TREATY);
    CHECK(last_error_has("R2") && last_error_has("byte 17"), "last_error names R2 at byte 17");
    zxv_treaty_destroy(t);
}

static void test_sutra_reject(void)
{
    REJECT("", SYNTAX, 0);
    REJECT("TREATY", SYNTAX, 0);
    REJECT("treaty N USD NET\nPARTY A 1 1 1/1\nEND\n", SYNTAX, 0);
    REJECT("TREATY  N USD NET\nPARTY A 1 1 1/1\nEND\n", NAME, 7);
    REJECT("TREATY n USD NET\nPARTY A 1 1 1/1\nEND\n", NAME, 7);
    REJECT("TREATY 7N USD NET\nPARTY A 1 1 1/1\nEND\n", NAME, 7);
    REJECT("TREATY N- USD NET\nPARTY A 1 1 1/1\nEND\n", NAME, 7);
    REJECT("TREATY ABCDEFGHIJKLMNOPQRSTUVWXYZ01234 USD NET\nPARTY A 1 1 1/1\nEND\n", NAME, 7);
    REJECT("TREATY N  USD NET\nPARTY A 1 1 1/1\nEND\n", ASSET, 9);
    REJECT("TREATY N\tUSD NET\nPARTY A 1 1 1/1\nEND\n", SYNTAX, 8);
    REJECT("TREATY N US NET\nPARTY A 1 1 1/1\nEND\n", ASSET, 9);
    REJECT("TREATY N USDX NET\nPARTY A 1 1 1/1\nEND\n", ASSET, 9);
    REJECT("TREATY N U1D NET\nPARTY A 1 1 1/1\nEND\n", ASSET, 9);
    REJECT("TREATY N USD NETT\nPARTY A 1 1 1/1\nEND\n", OP, 13);
    REJECT("TREATY N USD net\nPARTY A 1 1 1/1\nEND\n", OP, 13);
    REJECT("TREATY N USD NET\r\nPARTY A 1 1 1/1\nEND\n", OP, 13);
    REJECT("TREATY N USD SETL \nPARTY A 1 1 1/1\nEND\n", OP, 13);
    /* lines */
    REJECT("TREATY N USD NET\nEND\n", COUNT, 17);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/1\n", SYNTAX, 33);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/1\nEND", SYNTAX, 33);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/1\nEND\n\n", SYNTAX, 37);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/1\nEND\nX", SYNTAX, 37);
    REJECT("TREATY N USD NET\nOWE A B 1\nPARTY A 1 1 1/1\nEND\n", SYNTAX, 17);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A B 1\nPARTY C 0 0 0/1\nEND\n",
           SYNTAX, 59);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/1\n# note\nEND\n", SYNTAX, 33);
    REJECT("TREATY N USD NET\nPARTY  A 1 1 1/1\nEND\n", PARTY, 23);
    REJECT("TREATY N USD NET\nPARTY a 1 1 1/1\nEND\n", PARTY, 23);
    REJECT("TREATY N USD NET\nPARTY 9A 1 1 1/1\nEND\n", PARTY, 17);
    REJECT("TREATY N USD NET\nPARTY A- 1 1 1/1\nEND\n", PARTY, 17);
    REJECT("TREATY N USD NET\nPARTY ABCDEFGHI 1 1 1/1\nEND\n", PARTY, 23);
    REJECT("TREATY N USD NET\nPARTY A  1 1 1/1\nEND\n", NUMBER, 25);
    REJECT("TREATY N USD NET\nPARTYA 1 1 1/1\nEND\n", SYNTAX, 17);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/1 \nEND\n", NUMBER, 29);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1 /1\nEND\n", NUMBER, 29);
    /* numbers */
    REJECT("TREATY N USD NET\nPARTY A 01 1 1/1\nEND\n", NUMBER, 25);
    REJECT("TREATY N USD NET\nPARTY A 00 0 1/1\nEND\n", NUMBER, 25);
    REJECT("TREATY N USD NET\nPARTY A +1 1 1/1\nEND\n", NUMBER, 25);
    REJECT("TREATY N USD NET\nPARTY A -1 0 1/1\nEND\n", NUMBER, 25);
    REJECT("TREATY N USD NET\nPARTY A 1000000000000000000 1 1/1\nEND\n", NUMBER, 25);
    REJECT("TREATY N USD NET\nPARTY A 5 1000000000000000000 1/1\nEND\n", NUMBER, 27);
    REJECT("TREATY N USD NET\nPARTY A 5 01 1/1\nEND\n", NUMBER, 27);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/1000000000\nEND\n", NUMBER, 29);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1000000000/1\nEND\n", NUMBER, 29);
    REJECT("TREATY N USD NET\nPARTY A 1 1 01/1\nEND\n", NUMBER, 29);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/01\nEND\n", NUMBER, 29);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1\nEND\n", NUMBER, 29);
    /* R2 */
    REJECT("TREATY N USD NET\nPARTY A 5 6 1/1\nEND\n", CLAIM, 17);
    REJECT("TREATY N USD NET\nPARTY A 0 1 1/1\nEND\n", CLAIM, 17);
    /* R1 */
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nEND\n", WEIGHT, 33);
    REJECT("TREATY N USD NET\nPARTY A 1 1 2/2\nEND\n", WEIGHT, 17);
    REJECT("TREATY N USD NET\nPARTY A 1 1 0/2\nPARTY B 1 1 1/1\nEND\n", WEIGHT, 17);
    REJECT("TREATY N USD NET\nPARTY A 1 1 3/2\nEND\n", WEIGHT, 17);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/0\nEND\n", WEIGHT, 17);
    REJECT("TREATY N USD NET\nPARTY A 1 1 0/0\nEND\n", WEIGHT, 17);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/3\nEND\n", WEIGHT, 49);
    REJECT("TREATY N USD NET\nPARTY A 1 1 2/3\nPARTY B 1 1 2/3\nEND\n", WEIGHT, 49);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/1\nPARTY B 1 1 1/1\nEND\n", WEIGHT, 49);
    REJECT("TREATY N USD NET\nPARTY A 1 1 0/1\nEND\n", WEIGHT, 33);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/999999937\nPARTY B 1 1 1/999999929\n"
           "PARTY C 1 1 1/999999893\nEND\n",
           WEIGHT, 89);
    /* obligations */
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A A 1\nEND\n", OWE, 49);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A B 0\nEND\n", OWE, 49);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A C 1\nEND\n", PARTY, 55);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE C B 1\nEND\n", PARTY, 53);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A B 01\nEND\n", NUMBER, 57);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A B -1\nEND\n", NUMBER, 57);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A B 1000000000000000000\nEND\n",
           NUMBER, 57);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A  B 1\nEND\n", PARTY, 55);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE A B 1 \nEND\n", NUMBER, 57);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY B 1 1 1/2\nOWE  A B 1\nEND\n", PARTY, 53);
    REJECT("TREATY N USD NET\nPARTY A 1 1 1/2\nPARTY A 1 1 1/2\nEND\n", PARTY, 33);
}

/* ------------------------------------------------------------ RECORD */

static void put(uint8_t *rec, uint32_t off, const char *s)
{
    memcpy(rec + off, s, strlen(s));
}

static void rec_case(const char *what, uint32_t at, const char *bytes, int32_t want_fault,
                     uint32_t want_off, int line)
{
    static uint8_t rec[ZXV_TREATY_REC_LEN];
    zxv_treaty *t = make(BASE), *ref = make(BASE);
    uint32_t off = 999;
    int32_t fault = 999, eq = 0;
    base_record(rec);
    put(rec, at, bytes);
    int32_t rc = zxv_treaty_parse_record(t, rec, sizeof rec, &off, &fault);
    int32_t want_rc = want_fault == 0 ? ZXV_OK : ZXV_E_TREATY;
    CHECK(rc == want_rc && fault == want_fault && off == want_off,
          "line %d %s: got %s %d@%u want %d@%u", line, what, zxv_status_name(rc), fault, off,
          want_fault, want_off);
    if (rc != ZXV_OK) {
        OK(zxv_treaty_equal(t, ref, &eq));
        CHECK(eq == 1, "line %d %s: refused record left the handle unchanged", line, what);
    } else {
        /* an accepted record re-emits byte for byte */
        static uint8_t again[ZXV_TREATY_REC_LEN];
        size_t n = 0;
        OK(zxv_treaty_emit_record(t, again, sizeof again, &n));
        CHECK(memcmp(rec, again, sizeof rec) == 0, "line %d %s: record round trip", line, what);
    }
    zxv_treaty_destroy(t);
    zxv_treaty_destroy(ref);
}
#define REC(w, at, b, f, wo) rec_case((w), (at), (b), ZXV_TREATY_FAULT_##f, (wo), __LINE__)

#define P0 46u
#define P1 (46u + 64u)
#define P2 (46u + 128u)
#define W0 (46u + 1024u)
#define W1 (W0 + 23u)

static void test_record(void)
{
    static uint8_t rec[ZXV_TREATY_REC_LEN + 1];
    zxv_treaty *t = make(BASE);
    uint32_t off = 3;
    int32_t fault = 3, op = 0;
    uint32_t np, nw;
    base_record(rec);
    IS(zxv_treaty_parse_record(t, rec, ZXV_TREATY_REC_LEN - 1, &off, &fault), ZXV_E_ARG);
    IS(zxv_treaty_parse_record(t, rec, ZXV_TREATY_REC_LEN + 1, &off, &fault), ZXV_E_ARG);
    IS(zxv_treaty_parse_record(t, rec, 0, &off, &fault), ZXV_E_ARG);
    CHECK(off == 3 && fault == 3, "ARG leaves the fault outs");
    IS(zxv_treaty_parse_record(NULL, rec, ZXV_TREATY_REC_LEN, &off, &fault), ZXV_E_ARG);
    IS(zxv_treaty_parse_record(t, NULL, ZXV_TREATY_REC_LEN, &off, &fault), ZXV_E_ARG);

    rec[5] = 0; /* a NUL inside the name */
    IS(zxv_treaty_parse_record(t, rec, ZXV_TREATY_REC_LEN, &off, &fault), ZXV_E_TREATY);
    CHECK(fault == ZXV_TREATY_FAULT_NAME && off == 4, "name NUL %d@%u", fault, off);
    rec[5] = 'O';
    rec[40] = 0; /* a NUL inside the op */
    IS(zxv_treaty_parse_record(t, rec, ZXV_TREATY_REC_LEN, &off, &fault), ZXV_E_TREATY);
    CHECK(fault == ZXV_TREATY_FAULT_OP && off == 37, "op NUL %d@%u", fault, off);
    rec[40] = 'L';
    put(rec, 37, "NET ");
    OK(zxv_treaty_parse_record(t, rec, ZXV_TREATY_REC_LEN, NULL, NULL));
    OK(zxv_treaty_header(t, &op, &np, &nw));
    CHECK(op == ZXV_TREATY_OP_NET, "op NET from record");
    zxv_treaty_destroy(t);

    REC("magic", 0, "ZXT2", MAGIC, 0);
    REC("magic lower", 3, "t", MAGIC, 0);
    REC("name leading space", 4, " C", NAME, 4);
    REC("name gap", 14, " X", NAME, 4);
    REC("name lowercase", 5, "o", NAME, 4);
    REC("name blank", 4, "          ", NAME, 4);
    REC("name 30 chars", 4, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123", NONE, 0);
    REC("asset", 34, "US ", ASSET, 34);
    REC("asset digit", 36, "1", ASSET, 34);
    REC("op", 37, "NETX", OP, 37);
    REC("op lower", 37, "setl", OP, 37);
    REC("op SETL prefix", 37, "SET ", OP, 37);
    REC("nparty 00", 41, "00", COUNT, 41);
    REC("nparty 17", 41, "17", COUNT, 41);
    REC("nparty space", 41, " 2", COUNT, 41);
    REC("nowe 065", 43, "065", COUNT, 43);
    REC("nowe x", 43, "00x", COUNT, 43);
    REC("nparty 03 meets blank slot", 41, "03", PARTY, P2);
    REC("nparty 01 leaves weight 1/2", 41, "01", PAD, P1);
    REC("nowe 000 leaves slot", 43, "000", PAD, W0);
    REC("nowe 002 meets blank slot", 43, "002", PARTY, W1);
    REC("id blank", P0, "        ", PARTY, P0);
    REC("id gap", P0 + 4, " X", PARTY, P0);
    REC("id digit first", P0, "9", PARTY, P0);
    REC("id dup", P1, "ACME", PARTY, P1);
    REC("id 8 chars", P0, "ACMEACME", NONE, 0);
    REC("deposit sign", P0 + 8, "*", NUMBER, P0 + 8);
    REC("deposit space", P0 + 8, " ", NUMBER, P0 + 8);
    REC("deposit digit", P0 + 26, "x", NUMBER, P0 + 8);
    REC("deposit negative", P0 + 8, "-", NUMBER, P0);
    REC("deposit -0", P2 + 8, "-", PAD, P2);
    REC("claim -0", P0 + 27, "-000000000000000000", NUMBER, P0 + 27);
    REC("claim sign", P0 + 27, "?", NUMBER, P0 + 27);
    REC("claim > deposit", P0 + 27, "+000000000000005001", CLAIM, P0);
    REC("claim == deposit", P0 + 27, "+000000000000005000", NONE, 0);
    REC("claim negative", P0 + 27, "-000000000000000001", NUMBER, P0);
    REC("deposit -0 used", P0 + 8, "-000000000000000000", NUMBER, P0 + 8);
    REC("wnum digit", P0 + 46, "x", NUMBER, P0 + 46);
    REC("wden digit", P0 + 63, "x", NUMBER, P0 + 46);
    REC("weight 2/4", P0 + 46, "000000002000000004", WEIGHT, P0);
    REC("weight sum", P1 + 46, "000000001000000003", WEIGHT, 46);
    REC("pad id", P2 + 7, "A", PAD, P2);
    REC("pad deposit", P2 + 26, "1", PAD, P2);
    REC("pad deposit sign", P2 + 8, " ", PAD, P2);
    REC("pad claim", P2 + 45, "1", PAD, P2);
    REC("pad claim sign", P2 + 27, "-", PAD, P2);
    REC("pad wnum", P2 + 46, "1", PAD, P2);
    REC("pad wden", P2 + 63, "1", PAD, P2);
    REC("pad last party", 46u + 15u * 64u + 63u, "1", PAD, 46u + 15u * 64u);
    REC("owe from 00", W0, "00", PARTY, W0);
    REC("owe to 00", W0 + 2, "00", PARTY, W0);
    REC("owe from 03", W0, "03", PARTY, W0);
    REC("owe to 03", W0 + 2, "03", PARTY, W0);
    REC("owe self", W0, "02", OWE, W0);
    REC("owe reverse", W0, "0201", NONE, 0);
    REC("owe zero", W0 + 4, "+000000000000000000", OWE, W0);
    REC("owe negative", W0 + 4, "-000000000000000005", OWE, W0);
    REC("owe -0", W0 + 4, "-000000000000000000", NUMBER, W0);
    REC("owe digit", W0 + 1, "x", NUMBER, W0);
    REC("owe to digit", W0 + 3, "x", NUMBER, W0);
    REC("pad owe from", W1 + 1, "1", PAD, W1);
    REC("pad owe to", W1 + 3, "1", PAD, W1);
    REC("pad owe amount", W1 + 22, "1", PAD, W1);
    REC("pad owe sign", W1 + 4, "-", PAD, W1);
    REC("pad last owe", ZXV_TREATY_REC_LEN - 1u, "1", PAD, ZXV_TREATY_REC_LEN - 23u);
}

/* ------------------------------------------------------------ PIC */

static void test_pic(void)
{
    const int64_t M = PIC_MAX;
    int64_t r = 77, q = 77, m = 77;
    OK(zxv_treaty_pic_add(M - 1, 1, &r));
    CHECK(r == M, "add to max");
    r = 77;
    IS(zxv_treaty_pic_add(M, 1, &r), ZXV_E_RANGE);
    CHECK(r == 77, "add past max leaves r");
    CHECK(last_error_has("result"), "size error names the result");
    IS(zxv_treaty_pic_add(-M, -1, &r), ZXV_E_RANGE);
    OK(zxv_treaty_pic_add(-M, M, &r));
    CHECK(r == 0, "add -max+max");
    OK(zxv_treaty_pic_add(-5, 3, &r));
    CHECK(r == -2, "add signs");
    r = 77;
    IS(zxv_treaty_pic_add(M + 1, -2, &r), ZXV_E_RANGE);
    CHECK(r == 77 && last_error_has("operand"), "add operand a out of range");
    IS(zxv_treaty_pic_add(-2, M + 1, &r), ZXV_E_RANGE);
    IS(zxv_treaty_pic_add(1, 1, NULL), ZXV_E_ARG);

    OK(zxv_treaty_pic_sub(-M + 1, 1, &r));
    CHECK(r == -M, "sub to min");
    r = 77;
    IS(zxv_treaty_pic_sub(-M, 1, &r), ZXV_E_RANGE);
    IS(zxv_treaty_pic_sub(M, -1, &r), ZXV_E_RANGE);
    IS(zxv_treaty_pic_sub(0, -M - 1, &r), ZXV_E_RANGE);
    IS(zxv_treaty_pic_sub(0, INT64_MIN, &r), ZXV_E_RANGE);
    IS(zxv_treaty_pic_sub(M + 1, 0, &r), ZXV_E_RANGE);
    CHECK(r == 77, "sub refusals leave r");
    OK(zxv_treaty_pic_sub(10, 3, &r));
    CHECK(r == 7, "sub plain");
    IS(zxv_treaty_pic_sub(1, 1, NULL), ZXV_E_ARG);

    OK(zxv_treaty_pic_mul(999999999LL, 1000000001LL, &r));
    CHECK(r == M, "mul to max");
    r = 77;
    IS(zxv_treaty_pic_mul(1000000000LL, 1000000000LL, &r), ZXV_E_RANGE);
    IS(zxv_treaty_pic_mul(M, M, &r), ZXV_E_RANGE);
    IS(zxv_treaty_pic_mul(-M, 2, &r), ZXV_E_RANGE);
    IS(zxv_treaty_pic_mul(M + 1, 0, &r), ZXV_E_RANGE);
    IS(zxv_treaty_pic_mul(0, M + 1, &r), ZXV_E_RANGE);
    CHECK(r == 77, "mul refusals leave r");
    OK(zxv_treaty_pic_mul(-M, -1, &r));
    CHECK(r == M, "mul -max * -1");
    OK(zxv_treaty_pic_mul(0, M, &r));
    CHECK(r == 0, "mul zero");
    OK(zxv_treaty_pic_mul(-3, 7, &r));
    CHECK(r == -21, "mul signs");
    IS(zxv_treaty_pic_mul(1, 1, NULL), ZXV_E_ARG);

    OK(zxv_treaty_pic_div(-7, 2, &q, &m));
    CHECK(q == -3 && m == -1, "div truncates toward zero");
    OK(zxv_treaty_pic_div(7, -2, &q, &m));
    CHECK(q == -3 && m == 1, "div remainder has the dividend's sign");
    OK(zxv_treaty_pic_div(-7, -2, &q, &m));
    CHECK(q == 3 && m == -1, "div both negative");
    OK(zxv_treaty_pic_div(M, 1, &q, &m));
    CHECK(q == M && m == 0, "div by one");
    OK(zxv_treaty_pic_div(-M, -1, &q, &m));
    CHECK(q == M && m == 0, "div -max by -1");
    q = m = 77;
    IS(zxv_treaty_pic_div(5, 0, &q, &m), ZXV_E_RANGE);
    CHECK(q == 77 && m == 77 && last_error_has("zero"), "div by zero");
    IS(zxv_treaty_pic_div(M + 1, 2, &q, &m), ZXV_E_RANGE);
    IS(zxv_treaty_pic_div(M + 1, 0, &q, &m), ZXV_E_RANGE);
    CHECK(last_error_has("operand"), "an out-of-range dividend outranks division by zero");
    IS(zxv_treaty_pic_div(2, M + 1, &q, &m), ZXV_E_RANGE);
    CHECK(q == 77 && m == 77, "div refusals leave q and rem");
    IS(zxv_treaty_pic_div(4, 2, NULL, &m), ZXV_E_ARG);
    IS(zxv_treaty_pic_div(4, 2, &q, NULL), ZXV_E_ARG);
}

/* ------------------------------------------------------------ run */

/* The dividend rule, computed a second way (128-bit arithmetic): floor
 * shares, then the leftover units to the largest remainders, ties to the
 * lowest index. */
static void ref_share(const uint32_t *wn, const uint32_t *wd, uint32_t np, uint64_t total,
                      int64_t *out)
{
    unsigned __int128 rem[ZXV_TREATY_MAX_PARTIES];
    uint64_t given = 0;
    bool got[ZXV_TREATY_MAX_PARTIES] = {0};
    for (uint32_t i = 0; i < np; i++) {
        unsigned __int128 p = (unsigned __int128) total * wn[i];
        out[i] = (int64_t) (p / wd[i]);
        rem[i] = p % wd[i];
        given += (uint64_t) out[i];
    }
    while (given < total) {
        int best = -1;
        for (uint32_t i = 0; i < np; i++)
            if (!got[i] && (best < 0 || rem[i] * wd[best] > rem[best] * wd[i])) best = (int) i;
        got[best] = true;
        out[best]++;
        given++;
    }
}

/* Check the dividends of r against the reference rule; returns the bucket
 * total (the sum of the shares). */
static uint64_t check_dividend(const zxv_treaty *t, const zxv_treaty_result *r, int line)
{
    int32_t op;
    uint32_t np, nw, wn[16], wd[16];
    int64_t div[16], ref[16], dep, claim, net, bal;
    uint64_t total = 0;
    OK(zxv_treaty_header(t, &op, &np, &nw));
    for (uint32_t i = 0; i < np; i++) {
        OK(zxv_treaty_party(t, i, &dep, &claim, &wn[i], &wd[i]));
        OK(zxv_treaty_result_party(r, i, &net, &bal, &div[i]));
        total += (uint64_t) div[i];
    }
    ref_share(wn, wd, np, total, ref);
    CHECK(memcmp(ref, div, np * sizeof ref[0]) == 0, "line %d: dividend == reference", line);
    return total;
}

static zxv_treaty_result *run_ok(const zxv_treaty *t, int line)
{
    zxv_treaty_result *r = NULL;
    int32_t rc = zxv_treaty_run(t, &r);
    CHECK(rc == ZXV_OK && r, "line %d: run %s", line, zxv_status_name(rc));
    return r;
}

static void run_fails(const char *text, int32_t want, int line)
{
    zxv_treaty *t = make(text);
    zxv_treaty_result *r = (zxv_treaty_result *) 1;
    int32_t rc = zxv_treaty_run(t, &r);
    CHECK(rc == want && r == (zxv_treaty_result *) 1, "line %d: run %s want %s", line,
          zxv_status_name(rc), zxv_status_name(want));
    zxv_treaty_destroy(t);
}

static void test_setl(void)
{
    zxv_treaty *t = make(TRI);
    zxv_treaty_result *r = run_ok(t, __LINE__), *r2 = run_ok(t, __LINE__);
    uint32_t np = 0, nl = 0, from, to;
    int64_t fees = 0, net[3], bal[3], div[3], amt;
    OK(zxv_treaty_result_summary(r, &np, &nl, &fees));
    CHECK(np == 3 && nl == 2, "counts %u %u", np, nl);
    /* 0.08889% of 1 100 000 = 977.79: 977 charged */
    CHECK(fees == 977, "fees %lld", (long long) fees);
    for (uint32_t i = 0; i < 3; i++) OK(zxv_treaty_result_party(r, i, &net[i], &bal[i], &div[i]));
    CHECK(net[0] == -1100000 && net[1] == 500000 && net[2] == 600000, "net positions");
    CHECK(bal[0] == 5000000 - 1100000 - 977 - 4000 && bal[1] == 3000000 + 500000 - 3000 &&
              bal[2] == 900000 + 600000,
          "balances");
    /* 244 into the dividend bucket: 81.33, 34.86, 127.81 -> 81, 35, 128 */
    CHECK(div[0] == 81 && div[1] == 35 && div[2] == 128, "dividend shares");
    CHECK(check_dividend(t, r, __LINE__) == 244, "dividend bucket 244");
    OK(zxv_treaty_result_leg(r, 0, &from, &to, &amt));
    CHECK(from == 0 && to == 2 && amt == 600000, "leg 0");
    OK(zxv_treaty_result_leg(r, 1, &from, &to, &amt));
    CHECK(from == 0 && to == 1 && amt == 500000, "leg 1");
    IS(zxv_treaty_result_leg(r, 2, &from, &to, &amt), ZXV_E_NOT_FOUND);
    IS(zxv_treaty_result_party(r, 3, &net[0], &bal[0], &div[0]), ZXV_E_NOT_FOUND);
    /* conservation: deposits = balances + claims + fees */
    CHECK(5000000 + 3000000 + 900000 == bal[0] + bal[1] + bal[2] + 4000 + 3000 + fees,
          "conservation");

    /* the ZXVRSLT result record, byte for byte as the kernel test pins it */
    static uint8_t rec[ZXV_TREATY_RESULT_LEN + 4], rec2[ZXV_TREATY_RESULT_LEN];
    size_t n = 0;
    IS(zxv_treaty_result_record(r, NULL, 0, &n), ZXV_E_BUFFER);
    CHECK(n == ZXV_TREATY_RESULT_LEN, "result needed %zu", n);
    memset(rec, 0xEE, sizeof rec);
    IS(zxv_treaty_result_record(r, rec, ZXV_TREATY_RESULT_LEN - 1, &n), ZXV_E_BUFFER);
    CHECK(rec[0] == 0xEE, "result BUFFER writes nothing");
    OK(zxv_treaty_result_record(r, rec, sizeof rec, &n));
    CHECK(n == ZXV_TREATY_RESULT_LEN && rec[ZXV_TREATY_RESULT_LEN] == 0xEE, "result length");
    CHECK(memcmp(rec, "ZXR1+00000000003002+000000000000000977", 38) == 0, "result head %.38s",
          rec);
    CHECK(memcmp(rec + 38, "-000000000001100000+000000000003895023+000000000000000081", 57) == 0,
          "result party 0");
    CHECK(memcmp(rec + 38 + 2 * 57, "+000000000000600000+000000000001500000+000000000000000128",
                 57) == 0,
          "result party 2");
    CHECK(memcmp(rec + 38 + 3 * 57, "+000000000000000000+000000000000000000+000000000000000000",
                 57) == 0,
          "result unused party");
    const uint8_t *legs = rec + 38 + 16 * 57;
    CHECK(memcmp(legs, "0103+000000000000600000", 23) == 0 &&
              memcmp(legs + 23, "0102+000000000000500000", 23) == 0 &&
              memcmp(legs + 46, "0000+000000000000000000", 23) == 0 &&
              memcmp(legs + 63 * 23, "0000+000000000000000000", 23) == 0,
          "result legs");
    IS(zxv_treaty_result_record(NULL, rec, sizeof rec, &n), ZXV_E_ARG);
    IS(zxv_treaty_result_record(r, rec, sizeof rec, NULL), ZXV_E_ARG);
    /* deterministic: a second run gives the same bytes */
    OK(zxv_treaty_result_record(r2, rec2, sizeof rec2, &n));
    CHECK(memcmp(rec, rec2, sizeof rec2) == 0, "deterministic");

    /* the same treaty spelled as a record runs the same */
    static uint8_t trec[ZXV_TREATY_REC_LEN];
    zxv_treaty *u = NULL;
    OK(zxv_treaty_emit_record(t, trec, sizeof trec, &n));
    OK(zxv_treaty_create(&u));
    OK(zxv_treaty_parse_record(u, trec, sizeof trec, NULL, NULL));
    zxv_treaty_result *r3 = run_ok(u, __LINE__);
    OK(zxv_treaty_result_record(r3, rec2, sizeof rec2, &n));
    CHECK(memcmp(rec, rec2, sizeof rec2) == 0, "record form runs the same");
    /* the result outlives its treaty */
    zxv_treaty_destroy(u);
    OK(zxv_treaty_result_summary(r3, &np, &nl, &fees));
    CHECK(fees == 977, "result independent of the treaty handle");

    IS(zxv_treaty_result_summary(NULL, &np, &nl, &fees), ZXV_E_ARG);
    IS(zxv_treaty_result_summary(r, NULL, &nl, &fees), ZXV_E_ARG);
    IS(zxv_treaty_result_summary(r, &np, NULL, &fees), ZXV_E_ARG);
    IS(zxv_treaty_result_summary(r, &np, &nl, NULL), ZXV_E_ARG);
    IS(zxv_treaty_result_party(NULL, 0, &net[0], &bal[0], &div[0]), ZXV_E_ARG);
    IS(zxv_treaty_result_party(r, 0, NULL, &bal[0], &div[0]), ZXV_E_ARG);
    IS(zxv_treaty_result_party(r, 0, &net[0], NULL, &div[0]), ZXV_E_ARG);
    IS(zxv_treaty_result_party(r, 0, &net[0], &bal[0], NULL), ZXV_E_ARG);
    IS(zxv_treaty_result_leg(NULL, 0, &from, &to, &amt), ZXV_E_ARG);
    IS(zxv_treaty_result_leg(r, 0, NULL, &to, &amt), ZXV_E_ARG);
    IS(zxv_treaty_result_leg(r, 0, &from, NULL, &amt), ZXV_E_ARG);
    IS(zxv_treaty_result_leg(r, 0, &from, &to, NULL), ZXV_E_ARG);
    IS(zxv_treaty_run(NULL, &r3), ZXV_E_ARG);
    IS(zxv_treaty_run(t, NULL), ZXV_E_ARG);

    zxv_treaty_result_destroy(r);
    zxv_treaty_result_destroy(r2);
    zxv_treaty_result_destroy(r3);
    zxv_treaty_destroy(t);
}

static void test_net(void)
{
    zxv_treaty *t = make("TREATY CORRIDOR-7 USD NET\n"
                         "PARTY ACME 5000000 4000 1/3\n"
                         "PARTY BETA 3000000 3000 1/7\n"
                         "PARTY GAMMA 900000 0 11/21\n"
                         "OWE ACME BETA 1200000\n"
                         "OWE BETA GAMMA 700000\n"
                         "OWE GAMMA ACME 100000\n"
                         "END\n");
    zxv_treaty_result *r = run_ok(t, __LINE__);
    uint32_t np, nl, from, to;
    int64_t fees, net, bal, div, amt;
    OK(zxv_treaty_result_summary(r, &np, &nl, &fees));
    CHECK(fees == 0 && nl == 2, "NET: legs, no fee");
    OK(zxv_treaty_result_leg(r, 0, &from, &to, &amt));
    CHECK(amt == 600000, "NET leg 0");
    const int64_t want_bal[3] = {5000000, 3000000, 900000};
    for (uint32_t i = 0; i < 3; i++) {
        OK(zxv_treaty_result_party(r, i, &net, &bal, &div));
        CHECK(bal == want_bal[i] && div == 0, "NET posts nothing, claims stay (%u)", i);
    }
    OK(zxv_treaty_result_party(r, 2, &net, &bal, &div));
    CHECK(net == 600000, "NET position reported");
    zxv_treaty_result_destroy(r);
    zxv_treaty_destroy(t);

    /* a cycle nets to nothing: no legs, no fee even under SETL */
    t = make("TREATY C USD SETL\nPARTY A 10 10 1/2\nPARTY B 10 10 1/2\n"
             "OWE A B 7\nOWE B A 7\nEND\n");
    r = run_ok(t, __LINE__);
    OK(zxv_treaty_result_summary(r, &np, &nl, &fees));
    CHECK(nl == 0 && fees == 0, "cycle");
    OK(zxv_treaty_result_party(r, 0, &net, &bal, &div));
    CHECK(bal == 0 && net == 0, "claims paid out in full");
    zxv_treaty_result_destroy(r);
    zxv_treaty_destroy(t);

    t = make("TREATY Z USD SETL\nPARTY A 0 0 1/1\nEND\n");
    r = run_ok(t, __LINE__);
    OK(zxv_treaty_result_summary(r, &np, &nl, &fees));
    OK(zxv_treaty_result_party(r, 0, &net, &bal, &div));
    CHECK(np == 1 && bal == 0, "empty treaty runs");
    zxv_treaty_result_destroy(r);
    zxv_treaty_destroy(t);
}

static void test_fail_closed(void)
{
    char buf[512];
    /* a claim larger than what is left after settlement */
    const char *fmt = "TREATY CORRIDOR-7 USD SETL\n"
                      "PARTY ACME 5000000 %lld 1/3\n"
                      "PARTY BETA 3000000 3000 1/7\n"
                      "PARTY GAMMA 900000 0 11/21\n"
                      "OWE ACME BETA 1200000\n"
                      "OWE BETA GAMMA 700000\n"
                      "OWE GAMMA ACME 100000\n"
                      "END\n";
    sprintf(buf, fmt, 4000000LL);
    run_fails(buf, ZXV_E_FUNDS, __LINE__);
    sprintf(buf, fmt, 5000000LL - 1100000 - 977); /* exactly what is left */
    zxv_treaty *t = make(buf);
    zxv_treaty_result *r = run_ok(t, __LINE__);
    int64_t net, bal, div;
    OK(zxv_treaty_result_party(r, 0, &net, &bal, &div));
    CHECK(bal == 0, "claim of the exact rest");
    zxv_treaty_result_destroy(r);
    zxv_treaty_destroy(t);
    sprintf(buf, fmt, 5000000LL - 1100000 - 976);
    run_fails(buf, ZXV_E_FUNDS, __LINE__);

    /* a payer that cannot cover its fee */
    run_fails("TREATY P USD SETL\nPARTY A 100 0 1/2\nPARTY B 0 0 1/2\nOWE A B 100\nEND\n",
              ZXV_E_FUNDS, __LINE__);
    t = make("TREATY P USD NET\nPARTY A 100 0 1/2\nPARTY B 0 0 1/2\nOWE A B 100\nEND\n");
    r = run_ok(t, __LINE__);
    OK(zxv_treaty_result_party(r, 0, &net, &bal, &div));
    CHECK(bal == 100, "same treaty nets");
    zxv_treaty_result_destroy(r);
    zxv_treaty_destroy(t);

    /* a net position past PIC S9(18) */
    run_fails("TREATY S USD NET\nPARTY A 0 0 1/3\nPARTY B 0 0 1/3\nPARTY C 0 0 1/3\n"
              "OWE A C 600000000000000000\nOWE B C 400000000000000000\nEND\n",
              ZXV_E_RANGE, __LINE__);
    CHECK(last_error_has("PIC"), "size error detail");
    /* exactly at the PIC limit reaches the engine, whose 2^59 line limit
     * refuses it */
    run_fails("TREATY S USD NET\nPARTY A 0 0 1/3\nPARTY B 0 0 1/3\nPARTY C 0 0 1/3\n"
              "OWE A C 600000000000000000\nOWE B C 399999999999999999\nEND\n",
              ZXV_E_RANGE, __LINE__);
    CHECK(last_error_has("2^59"), "line limit detail");
    run_fails("TREATY S USD NET\nPARTY A 0 0 1/3\nPARTY B 0 0 1/3\nPARTY C 0 0 1/3\n"
              "OWE C A 600000000000000000\nOWE C B 400000000000000000\nEND\n",
              ZXV_E_RANGE, __LINE__);
    /* a deposit above the ledger's per-line limit (2^59) */
    run_fails("TREATY L USD NET\nPARTY A 576460752303423489 0 1/1\nEND\n", ZXV_E_RANGE, __LINE__);
    t = make("TREATY L USD NET\nPARTY A 576460752303423488 0 1/1\nEND\n");
    r = run_ok(t, __LINE__);
    OK(zxv_treaty_result_party(r, 0, &net, &bal, &div));
    CHECK(bal == 576460752303423488LL, "deposit of 2^59");
    zxv_treaty_result_destroy(r);
    zxv_treaty_destroy(t);
}

static void test_dividend_rule(void)
{
    /* sixteen parties with weights 1/2, 1/4, ..., 1/2^15, 1/2^15 */
    char buf[4096];
    int n = sprintf(buf, "TREATY W USD SETL\n");
    for (int i = 0; i < 16; i++)
        n += sprintf(buf + n, "PARTY P%02d 100000000000000 0 1/%u\n", i,
                     1u << (i < 15 ? i + 1 : 15));
    for (int j = 0; j < 15; j++)
        n += sprintf(buf + n, "OWE P%02d P%02d %llu\n", j, j + 1,
                     12345678901ULL * (unsigned) (j + 3));
    strcpy(buf + n, "END\n");
    zxv_treaty *t = make(buf);
    zxv_treaty_result *r = run_ok(t, __LINE__);
    CHECK(check_dividend(t, r, __LINE__) > 1000, "16-party dividend is large");
    zxv_treaty_result_destroy(r);
    zxv_treaty_destroy(t);

    /* ties to the lowest index: thirds of a bucket that is 2 mod 3 */
    uint64_t total = 0;
    int64_t d[3], net, bal;
    for (int k = 0; k < 64; k++) {
        sprintf(buf,
                "TREATY T USD SETL\nPARTY A 100000000 0 1/3\nPARTY B 100000000 0 1/3\n"
                "PARTY C 100000000 0 1/3\nOWE A B %d\nEND\n",
                3000000 + 1000 * k);
        t = make(buf);
        r = run_ok(t, __LINE__);
        total = check_dividend(t, r, __LINE__);
        for (uint32_t i = 0; i < 3; i++) OK(zxv_treaty_result_party(r, i, &net, &bal, &d[i]));
        zxv_treaty_result_destroy(r);
        zxv_treaty_destroy(t);
        if (total % 3 == 2) break;
    }
    CHECK(total % 3 == 2, "a bucket 2 mod 3 (%llu)", (unsigned long long) total);
    CHECK(d[0] == (int64_t) (total / 3 + 1) && d[1] == (int64_t) (total / 3 + 1) &&
              d[2] == (int64_t) (total / 3),
          "ties to the lowest index: %lld %lld %lld", (long long) d[0], (long long) d[1],
          (long long) d[2]);

    /* near the 2^59 line limit with 9-digit weights: 128-bit share math */
    t = make("TREATY H USD SETL\nPARTY A 576460752303423488 0 999999936/999999937\n"
             "PARTY B 0 0 1/999999937\nOWE A B 500000000000000000\nEND\n");
    r = run_ok(t, __LINE__);
    CHECK(check_dividend(t, r, __LINE__) > (1ULL << 40), "huge dividend");
    zxv_treaty_result_destroy(r);
    zxv_treaty_destroy(t);
    t = make("TREATY H USD SETL\nPARTY A 576460752303423488 0 1/999999937\n"
             "PARTY B 0 0 999999936/999999937\nOWE A B 500000000000000000\nEND\n");
    r = run_ok(t, __LINE__);
    check_dividend(t, r, __LINE__);
    zxv_treaty_result_destroy(r);
    zxv_treaty_destroy(t);

    /* a zero weight gets nothing even with remainder units about */
    t = make("TREATY T USD SETL\nPARTY A 100000000 0 0/1\nPARTY B 100000000 0 1/3\n"
             "PARTY C 100000000 0 2/3\nOWE A B 3000000\nEND\n");
    r = run_ok(t, __LINE__);
    check_dividend(t, r, __LINE__);
    OK(zxv_treaty_result_party(r, 0, &net, &bal, &d[0]));
    CHECK(d[0] == 0, "zero weight");
    zxv_treaty_result_destroy(r);
    zxv_treaty_destroy(t);
}

int main(void)
{
    test_lifecycle();
    test_sutra_base();
    test_sutra_accept();
    test_sutra_reject();
    test_record();
    test_pic();
    test_setl();
    test_net();
    test_fail_closed();
    test_dividend_rule();
    printf("test_zxv_abi15_treaty: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
