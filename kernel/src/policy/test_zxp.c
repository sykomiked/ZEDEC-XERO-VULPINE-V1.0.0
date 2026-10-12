/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxp.c — contract DSL: grammar, rules with offsets, both forms,
 * round trips, dates, jurisdiction tags, drafting templates. */
#include "zxp.h"
#include "zxp_draft.h"

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

#define DOCL "DOC 9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08\n"
#define HEAD "CONTRACT SUPPLY-2027 20261015\n" DOCL "LAW KE\nPARTY ACME US-DE\nPARTY BETA KE\n"

static const char FULL[] = HEAD "SIGN 2\n"
                                "WINDOW 20261015 20271231\n"
                                "LOCK 20261224 20261226\n"
                                "LOCK 20261231 20270101\n"
                                "CAP EUR 500\n"
                                "CAP USD 2000000\n"
                                "LIMIT ACME USD 1500000\n"
                                "LIMIT BETA EUR 900\n"
                                "ALLOW KE\n"
                                "ALLOW US\n"
                                "DENY KP\n"
                                "DUE T1 ACME BETA USD 1200000 20261231 PENALTY 12000\n"
                                "DUE T2 ACME BETA USD 300000 20270331 AFTER T1\n"
                                "GATE G1 ALL T1 T2\n"
                                "DUE T3 BETA ACME EUR 400 20270401 AFTER G1\n"
                                "GATE G2 ANY G1 T3\n"
                                "END\n";

static const char MIN[] = HEAD "SIGN 1\nWINDOW 20261015 20261015\nEND\n";

static bool all_zero(const void *p, size_t n)
{
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++)
        if (b[i]) return false;
    return true;
}

static void reject(const char *s, zxp_status_t want, uint32_t off, int line)
{
    static zxp_contract_t c;
    uint32_t o = 12345;
    memset(&c, 0xA5, sizeof c);
    zxp_status_t st = zxp_parse_sutra(s, (uint32_t) strlen(s), &c, &o);
    CHECK(st == want && o == off && all_zero(&c, sizeof c), "line %d: got %d@%u want %d@%u", line,
          st, o, want, off);
}
#define REJECT(s, st, off) reject((s), (st), (off), __LINE__)

static void accept_rt(const char *s, int line)
{
    static zxp_contract_t c, d;
    static char out[ZXP_SUTRA_MAX];
    static uint8_t rec[ZXP_REC_LEN], rec2[ZXP_REC_LEN];
    uint32_t n = 0, off = 99;
    zxp_status_t st = zxp_parse_sutra(s, (uint32_t) strlen(s), &c, &off);
    CHECK(st == ZXP_OK && off == 0, "line %d: parse %d@%u", line, st, off);
    CHECK(zxp_validate(&c) == ZXP_OK, "line %d: validate", line);
    CHECK(zxp_emit_sutra(&c, out, sizeof out, &n) == ZXP_OK && n == strlen(s) &&
              memcmp(out, s, n) == 0,
          "line %d: re-emit", line);
    CHECK(zxp_emit_record(&c, rec, sizeof rec) == ZXP_OK, "line %d: emit record", line);
    CHECK(zxp_parse_record(rec, sizeof rec, &d, &off) == ZXP_OK && zxp_equal(&c, &d),
          "line %d: record round trip", line);
    CHECK(zxp_emit_record(&d, rec2, sizeof rec2) == ZXP_OK && memcmp(rec, rec2, sizeof rec) == 0,
          "line %d: record re-emit", line);
    for (uint32_t i = 0; i < ZXP_REC_LEN; i++)
        if (rec[i] < 0x20 || rec[i] > 0x7e) {
            CHECK(0, "line %d: record byte %u not printable ASCII", line, i);
            break;
        }
}
#define ACCEPT(s) accept_rt((s), __LINE__)

/* Replace the first `from` in FULL with `to` (into a static buffer). */
static const char *edit(const char *base, const char *from, const char *to)
{
    static char buf[4096];
    const char *p = strstr(base, from);
    if (!p) {
        printf("edit: '%s' not found\n", from);
        return base;
    }
    size_t a = (size_t) (p - base);
    memcpy(buf, base, a);
    strcpy(buf + a, to);
    strcat(buf, p + strlen(from));
    return buf;
}
static uint32_t at(const char *base, const char *needle)
{
    const char *p = strstr(base, needle);
    return p ? (uint32_t) (p - base) : 0xFFFFFFFFu;
}

static void test_grammar(void)
{
    ACCEPT(FULL);
    ACCEPT(MIN);
    /* header */
    REJECT("", ZXP_E_SYNTAX, 0);
    REJECT("CONTRACT X 20261015", ZXP_E_SYNTAX, 0);
    REJECT(edit(FULL, "CONTRACT ", "CONTRAKT "), ZXP_E_SYNTAX, 0);
    REJECT(edit(FULL, "SUPPLY-2027", "supply"), ZXP_E_NAME, 0);
    REJECT(edit(FULL, "SUPPLY-2027", "SUPPLY-"), ZXP_E_NAME, 0);
    REJECT(edit(FULL, "SUPPLY-2027", "S234567890123456789012345678901"), ZXP_E_NAME, 0);
    REJECT(edit(FULL, "SUPPLY-2027 20261015", "SUPPLY-2027 20260230"), ZXP_E_DATE, 0);
    REJECT(edit(FULL, "SUPPLY-2027 20261015", "SUPPLY-2027  20261015"), ZXP_E_DATE, 0);
    REJECT(edit(FULL, "SUPPLY-2027 20261015\n", "SUPPLY-2027 20261015 \n"), ZXP_E_DATE, 0);
    /* DOC */
    REJECT(edit(FULL, "DOC 9f", "DOC 9F"), ZXP_E_DOC, at(FULL, "DOC"));
    REJECT(edit(FULL, "a08\n", "a0\n"), ZXP_E_DOC, at(FULL, "DOC"));
    REJECT(edit(FULL, "a08\n", "a08 x\n"), ZXP_E_SYNTAX, at(FULL, "DOC"));
    /* LAW */
    REJECT(edit(FULL, "LAW KE\n", ""), ZXP_E_COUNT, at(FULL, "LAW"));
    REJECT(edit(FULL, "LAW KE\n", "LAW ke\n"), ZXP_E_JUR, at(FULL, "LAW"));
    REJECT(edit(FULL, "LAW KE\n", "LAW KE\nLAW GB\n"), ZXP_E_ORDER, at(FULL, "LAW") + 7);
    REJECT(edit(FULL, "LAW KE\n", "LAW KE\nLAW KE\n"), ZXP_E_ORDER, at(FULL, "LAW") + 7);
    REJECT(edit(FULL, "LAW KE\n", "LAW AA\nLAW BB\nLAW CC\nLAW DD\nLAW EE\n"), ZXP_E_COUNT,
           at(FULL, "LAW") + 28);
    ACCEPT(edit(FULL, "LAW KE\n", "LAW GB-ENG\nLAW KE\nLAW US-NY\n"));
    /* PARTY */
    REJECT(edit(FULL, "PARTY BETA KE\n", "PARTY ACME KE\n"), ZXP_E_PARTY, at(FULL, "PARTY BETA"));
    REJECT(edit(FULL, "PARTY BETA KE\n", "PARTY ESCROW KE\n"), ZXP_E_PARTY, at(FULL, "PARTY BETA"));
    REJECT(edit(FULL, "PARTY BETA KE\n", "PARTY BETA K\n"), ZXP_E_JUR, at(FULL, "PARTY BETA"));
    REJECT(edit(FULL, "PARTY BETA KE\n", "PARTY BETA KE-\n"), ZXP_E_JUR, at(FULL, "PARTY BETA"));
    REJECT(edit(FULL, "PARTY BETA KE\n", "PARTY BETA KE-ABCD\n"), ZXP_E_JUR,
           at(FULL, "PARTY BETA"));
    REJECT(edit(FULL, "PARTY BETA KE\n", "PARTY BETA90123 KE\n"), ZXP_E_NAME,
           at(FULL, "PARTY BETA"));
    REJECT(edit(FULL, "PARTY BETA KE\n", "PARTY BETA KE X\n"), ZXP_E_SYNTAX,
           at(FULL, "PARTY BETA"));
    REJECT(edit(MIN, "PARTY ACME US-DE\nPARTY BETA KE\n", ""), ZXP_E_COUNT, at(MIN, "PARTY"));
    /* SIGN and WINDOW */
    REJECT(edit(FULL, "SIGN 2", "SIGN 3"), ZXP_E_SIGN, at(FULL, "SIGN"));
    REJECT(edit(FULL, "SIGN 2", "SIGN 0"), ZXP_E_SIGN, at(FULL, "SIGN"));
    REJECT(edit(FULL, "SIGN 2", "SIGN 02"), ZXP_E_SIGN, at(FULL, "SIGN"));
    REJECT(edit(FULL, "SIGN 2\n", ""), ZXP_E_SYNTAX, at(FULL, "SIGN"));
    REJECT(edit(FULL, "WINDOW 20261015 20271231", "WINDOW 20271231 20261015"), ZXP_E_DATE,
           at(FULL, "WINDOW"));
    REJECT(edit(FULL, "WINDOW 20261015 20271231", "WINDOW 20261015"), ZXP_E_DATE,
           at(FULL, "WINDOW"));
    /* LOCK */
    REJECT(edit(FULL, "LOCK 20261231 20270101", "LOCK 20261226 20270101"), ZXP_E_ORDER,
           at(FULL, "LOCK 20261231"));
    REJECT(edit(FULL, "LOCK 20261224 20261226", "LOCK 20261227 20261226"), ZXP_E_DATE,
           at(FULL, "LOCK"));
    /* CAP */
    REJECT(edit(FULL, "CAP EUR 500", "CAP XYZ 500\nCAP EUR 500"), ZXP_E_ORDER,
           at(FULL, "CAP EUR") + 12);
    REJECT(edit(FULL, "CAP EUR 500", "CAP EUR 0"), ZXP_E_NUMBER, at(FULL, "CAP EUR"));
    REJECT(edit(FULL, "CAP EUR 500", "CAP EUR 0500"), ZXP_E_NUMBER, at(FULL, "CAP EUR"));
    REJECT(edit(FULL, "CAP EUR 500", "CAP EUR 1000000000000000000"), ZXP_E_NUMBER,
           at(FULL, "CAP EUR"));
    ACCEPT(edit(FULL, "CAP EUR 500", "CAP EUR 999999999999999999"));
    REJECT(edit(FULL, "CAP EUR 500", "CAP EU 500"), ZXP_E_ASSET, at(FULL, "CAP EUR"));
    REJECT(edit(FULL, "CAP EUR 500", "CAP Eur 500"), ZXP_E_ASSET, at(FULL, "CAP EUR"));
    /* LIMIT */
    REJECT(edit(FULL, "LIMIT ACME USD", "LIMIT GAMMA USD"), ZXP_E_PARTY, at(FULL, "LIMIT ACME"));
    REJECT(edit(FULL, "LIMIT BETA EUR 900\n", "LIMIT ACME EUR 900\n"), ZXP_E_ORDER,
           at(FULL, "LIMIT BETA"));
    ACCEPT(edit(FULL, "LIMIT BETA EUR 900\n", "LIMIT ACME USE 900\n"));
    /* ALLOW / DENY: sorted; the same tag in both is accepted (BOTH later) */
    REJECT(edit(FULL, "ALLOW KE\nALLOW US\n", "ALLOW US\nALLOW KE\n"), ZXP_E_ORDER,
           at(FULL, "ALLOW US"));
    ACCEPT(edit(FULL, "DENY KP\n", "DENY KE\nDENY KP\n"));
    REJECT(edit(FULL, "DENY KP\n", "DENY KP\nDENY KP\n"), ZXP_E_ORDER, at(FULL, "DENY") + 8);
    /* sections out of order */
    REJECT(edit(FULL, "ALLOW KE\nALLOW US\nDENY KP\n", "DENY KP\nALLOW KE\nALLOW US\n"),
           ZXP_E_SYNTAX, at(FULL, "ALLOW KE") + 8);
    /* DUE */
    REJECT(edit(FULL, "DUE T1 ACME BETA", "DUE T1 ACME ACME"), ZXP_E_CLAUSE, at(FULL, "DUE T1"));
    REJECT(edit(FULL, "DUE T1 ACME BETA", "DUE T1 ACME ZETA"), ZXP_E_PARTY, at(FULL, "DUE T1"));
    REJECT(edit(FULL, "USD 1200000 20261231", "USD 0 20261231"), ZXP_E_NUMBER, at(FULL, "DUE T1"));
    REJECT(edit(FULL, "USD 1200000 20261231", "USD 1200000 20261014"), ZXP_E_DATE,
           at(FULL, "DUE T1"));
    REJECT(edit(FULL, "PENALTY 12000", "PENALTY 0"), ZXP_E_NUMBER, at(FULL, "DUE T1"));
    REJECT(edit(FULL, "AFTER T1", "AFTER T2"), ZXP_E_REF, at(FULL, "DUE T2"));
    REJECT(edit(FULL, "AFTER T1", "AFTER T9"), ZXP_E_REF, at(FULL, "DUE T2"));
    REJECT(edit(FULL, "20270331 AFTER T1", "20270331 PENALTY 5 AFTER T1 PENALTY 5"), ZXP_E_SYNTAX,
           at(FULL, "DUE T2"));
    REJECT(edit(FULL, "20270331 AFTER T1", "20270331 AFTER T1 PENALTY 5"), ZXP_E_SYNTAX,
           at(FULL, "DUE T2"));
    REJECT(edit(FULL, "20270331 AFTER T1", "20270331 AFTER T1 AFTER T1"), ZXP_E_SYNTAX,
           at(FULL, "DUE T2"));
    REJECT(edit(FULL, "DUE T2 ACME", "DUE T1 ACME"), ZXP_E_NAME, at(FULL, "DUE T2"));
    /* GATE */
    REJECT(edit(FULL, "GATE G1 ALL T1 T2", "GATE G1 ALL T1"), ZXP_E_CLAUSE, at(FULL, "GATE G1"));
    REJECT(edit(FULL, "GATE G1 ALL T1 T2", "GATE G1 ALL T1 T1"), ZXP_E_REF, at(FULL, "GATE G1"));
    REJECT(edit(FULL, "GATE G1 ALL T1 T2", "GATE G1 ONE T1 T2"), ZXP_E_CLAUSE, at(FULL, "GATE G1"));
    REJECT(edit(FULL, "GATE G1 ALL T1 T2", "GATE G1 ALL T1 G1"), ZXP_E_REF, at(FULL, "GATE G1"));
    REJECT(edit(FULL, "GATE G1 ALL T1 T2", "GATE G1 ALL T1 T2 T1 T2 T1 T2 T1 T2 T1"), ZXP_E_CLAUSE,
           at(FULL, "GATE G1"));
    /* END */
    REJECT(edit(FULL, "END\n", "END\nX"), ZXP_E_SYNTAX, (uint32_t) strlen(FULL));
    REJECT(edit(FULL, "END\n", "END"), ZXP_E_SYNTAX, at(FULL, "END\n"));
    REJECT(edit(FULL, "END\n", "END \n"), ZXP_E_SYNTAX, at(FULL, "END\n"));
    REJECT(edit(FULL, "\nEND\n", "\nFIN\n"), ZXP_E_SYNTAX, at(FULL, "END\n"));
}

static void test_clause_offset(void)
{
    static zxp_contract_t c;
    CHECK(zxp_parse_sutra(FULL, (uint32_t) strlen(FULL), &c, NULL) == ZXP_OK, "parse");
    CHECK(zxp_clause_offset(&c, 0) == (int32_t) at(FULL, "DUE T1"), "offset T1");
    CHECK(zxp_clause_offset(&c, 2) == (int32_t) at(FULL, "GATE G1"), "offset G1");
    CHECK(zxp_clause_offset(&c, 4) == (int32_t) at(FULL, "GATE G2"), "offset G2");
    CHECK(zxp_clause_offset(&c, 5) == -1, "offset past end");
    CHECK(zxp_find_clause(&c, "T3") == 3 && zxp_find_clause(&c, "T9") == -1, "find");
}

static void test_record(void)
{
    static zxp_contract_t c, d;
    static uint8_t rec[ZXP_REC_LEN], bad[ZXP_REC_LEN];
    uint32_t off = 0;
    CHECK(ZXP_REC_LEN == 4419u, "record length %u", ZXP_REC_LEN);
    CHECK(zxp_parse_sutra(FULL, (uint32_t) strlen(FULL), &c, NULL) == ZXP_OK, "parse");
    CHECK(zxp_emit_record(&c, rec, sizeof rec) == ZXP_OK, "emit");
    CHECK(zxp_emit_record(&c, rec, ZXP_REC_LEN - 1u) == ZXP_E_CAP, "short cap");
    CHECK(zxp_parse_record(rec, ZXP_REC_LEN - 1u, &d, &off) == ZXP_E_ARG, "short len");
    /* Every single-byte change to the record is either refused or gives a
     * different, still valid contract (never the same AST). */
    uint32_t refused = 0, changed = 0;
    for (uint32_t i = 0; i < ZXP_REC_LEN; i++) {
        memcpy(bad, rec, sizeof rec);
        bad[i] = bad[i] == '9' ? '8' : (bad[i] == ' ' ? 'Q' : '9');
        zxp_status_t st = zxp_parse_record(bad, sizeof bad, &d, &off);
        if (st != ZXP_OK) {
            refused++;
            if (!all_zero(&d, sizeof d)) CHECK(0, "refused parse left data at %u", i);
        } else {
            changed++;
            if (zxp_equal(&c, &d)) CHECK(0, "byte %u changed but AST equal", i);
        }
    }
    CHECK(refused + changed == ZXP_REC_LEN && refused > 4000u, "refused %u changed %u", refused,
          changed);
    memcpy(bad, rec, sizeof rec);
    bad[0] = 'Y';
    CHECK(zxp_parse_record(bad, sizeof bad, &d, &off) == ZXP_E_MAGIC && off == 0, "magic");
    /* negative zero and a negative amount are refused */
    memcpy(bad, rec, sizeof rec);
    uint32_t cap0 = ZXP_REC_HEAD + ZXP_REC_LAWS + ZXP_MAX_PARTIES * ZXP_REC_PARTY +
                    ZXP_MAX_LOCKS * ZXP_REC_LOCK + 3u;
    bad[cap0] = '-';
    CHECK(zxp_parse_record(bad, sizeof bad, &d, &off) == ZXP_E_NUMBER, "negative cap");
}

static void test_validate_hand(void)
{
    static zxp_contract_t c;
    CHECK(zxp_parse_sutra(FULL, (uint32_t) strlen(FULL), &c, NULL) == ZXP_OK, "parse");
    c.party[5].id[0] = 'X';
    CHECK(zxp_validate(&c) == ZXP_E_PAD, "unused party slot");
    c.party[5].id[0] = 0;
    c.clause[0].op = ZXP_GATE_ALL;
    CHECK(zxp_validate(&c) == ZXP_E_CLAUSE, "DUE with gate op");
    c.clause[0].op = 0;
    c.clause[2].amount = 1;
    CHECK(zxp_validate(&c) == ZXP_E_CLAUSE, "GATE with amount");
    c.clause[2].amount = 0;
    c.clause[0].penalty = -1;
    CHECK(zxp_validate(&c) == ZXP_E_NUMBER, "negative penalty");
    c.clause[0].penalty = 12000;
    c.clause[0].kind = 3;
    CHECK(zxp_validate(&c) == ZXP_E_CLAUSE, "bad kind");
    c.clause[0].kind = ZXP_CL_DUE;
    c.name[29] = 'Z';
    CHECK(zxp_validate(&c) == ZXP_E_NAME, "name not terminated");
    c.name[29] = 0;
    CHECK(zxp_validate(&c) == ZXP_OK, "restored");
    CHECK(zxp_validate(NULL) == ZXP_E_ARG, "null");
}

static void test_dates_and_tags(void)
{
    CHECK(zxp_date_ok(20240229) && !zxp_date_ok(20230229) && !zxp_date_ok(21000229) &&
              zxp_date_ok(20000229),
          "leap years");
    CHECK(!zxp_date_ok(20261301) && !zxp_date_ok(20261100) && !zxp_date_ok(20260431) &&
              zxp_date_ok(20261231) && !zxp_date_ok(18991231),
          "month and day bounds");
    CHECK(zxp_days(19700101) == 0 && zxp_days(20000101) == 10957 && zxp_days(20261015) == 20741,
          "days since epoch %d", zxp_days(20261015));
    CHECK(zxp_date_add(20261231, 1) == 20270101 && zxp_date_add(20240228, 1) == 20240229 &&
              zxp_date_add(20240301, -1) == 20240229 && zxp_date_add(20261015, 365) == 20271015,
          "date arithmetic");
    for (uint32_t d = 19000101; d < 21000000; d = zxp_date_add(d, 37)) {
        if (zxp_date_add(d, 1) == 0 || zxp_days(zxp_date_add(d, 1)) != zxp_days(d) + 1) {
            CHECK(0, "day after %u", d);
            break;
        }
    }
    CHECK(zxp_jur_ok("KE") && zxp_jur_ok("US-NY") && zxp_jur_ok("GB-ENG") && zxp_jur_ok("FR-75"),
          "tags ok");
    CHECK(!zxp_jur_ok("K") && !zxp_jur_ok("KEN") && !zxp_jur_ok("ke") && !zxp_jur_ok("US-") &&
              !zxp_jur_ok("US-ny") && !zxp_jur_ok("US-ABCD") && !zxp_jur_ok("US_NY"),
          "tags refused");
    CHECK(zxp_jur_covers("US", "US-NY") && zxp_jur_covers("US", "US") &&
              zxp_jur_covers("US-NY", "US-NY") && !zxp_jur_covers("US-NY", "US") &&
              !zxp_jur_covers("US", "UY") && !zxp_jur_covers("US-NY", "US-NJ"),
          "prefix cover");
}

static void test_draft(void)
{
    static zxp_contract_t c;
    static char text[ZXP_SUTRA_MAX], prose[16384];
    uint32_t n;
    zxp_draft_params_t p = {"LOAN-1", 20261015, "GB-ENG", "BANK",  "GB-ENG",
                            "FARM",   "KE",     "KES",    1000000, 1100000,
                            4,        20261115, 30,       500,     20271231};
    CHECK(zxp_draft(ZXP_TPL_LOAN, &p, &c) == ZXP_OK, "loan");
    CHECK(c.n_clause == 5 && c.clause[0].amount == 1000000 && c.clause[4].after == 1,
          "loan clauses");
    int64_t sum = 0;
    for (uint32_t i = 1; i < 5; i++) sum += c.clause[i].amount;
    CHECK(sum == 1100000, "repayments add up %lld", (long long) sum);
    CHECK(c.n_limit == 2 && c.limit[1].amount == 1100000 && c.n_allow == 2 &&
              strcmp(c.allow[0], "GB") == 0 && strcmp(c.allow[1], "KE") == 0,
          "loan limits and allow list");
    CHECK(zxp_emit_sutra(&c, text, sizeof text, &n) == ZXP_OK, "emit");
    n = zxp_draft_prose(ZXP_TPL_LOAN, &c, prose, sizeof prose);
    CHECK(n > 0 && strstr(prose, "not legal advice") &&
              strstr(prose, "[TO BE COMPLETED BY COUNSEL]") && strstr(prose, "common law") &&
              strstr(prose, "R4: FARM pays BANK 275000 KES"),
          "loan prose");
    CHECK(zxp_draft_prose(ZXP_TPL_LOAN, &c, prose, 100) == 0 && prose[0] == 0, "short buffer");

    zxp_draft_params_t s = {"FIT-OUT", 20261015, "KE", "ACME",   "US-DE", "BETA", "KE",    "USD",
                            1000001,   0,        3,    20261101, 31,      0,      20270630};
    CHECK(zxp_draft(ZXP_TPL_SERVICES, &s, &c) == ZXP_OK, "services");
    CHECK(c.n_clause == 3 && c.clause[0].amount == 333333 && c.clause[2].amount == 333335 &&
              c.cap[0].amount == 333335 && c.clause[1].date == 20261202,
          "services split %lld", (long long) c.clause[2].amount);
    n = zxp_draft_prose(ZXP_TPL_SERVICES, &c, prose, sizeof prose);
    CHECK(n > 0 && strstr(prose, "Muslim law") && strstr(prose, "customary law"),
          "KE family label");
    CHECK(zxp_draft(ZXP_TPL_SALE, &s, &c) == ZXP_OK, "sale");

    zxp_draft_params_t m = {
        "JOINT-STUDY", 20261015, "FR", "UNI", "FR", "LAB", "SN", "", 0, 0, 0, 0, 0, 0, 20271015};
    CHECK(zxp_draft(ZXP_TPL_MOU, &m, &c) == ZXP_OK && c.n_clause == 0 && c.n_cap == 0, "mou");
    n = zxp_draft_prose(ZXP_TPL_MOU, &c, prose, sizeof prose);
    CHECK(n > 0 && strstr(prose, "No payment terms") &&
              strstr(prose, "nothing in this memorandum is intended"),
          "mou prose");
    CHECK(zxp_draft(ZXP_TPL_BILATERAL, &m, &c) == ZXP_OK, "bilateral without payments");
    m.amount = 5000;
    m.asset = "XOF";
    m.installments = 2;
    m.first_due = 20261201;
    m.interval_days = 180;
    CHECK(zxp_draft(ZXP_TPL_BILATERAL, &m, &c) == ZXP_OK && c.n_clause == 2,
          "bilateral contributions");

    /* refusals */
    s.installments = 13;
    CHECK(zxp_draft(ZXP_TPL_SERVICES, &s, &c) == ZXP_E_COUNT && all_zero(&c, sizeof c),
          "13 installments");
    s.installments = 3;
    s.first_due = 20261001;
    CHECK(zxp_draft(ZXP_TPL_SERVICES, &s, &c) == ZXP_E_DATE, "due before contract");
    s.first_due = 20261101;
    s.amount = 2;
    CHECK(zxp_draft(ZXP_TPL_SERVICES, &s, &c) == ZXP_E_DATE, "installment of zero");
    s.amount = 1000001;
    s.window_to = 20261001;
    CHECK(zxp_draft(ZXP_TPL_SERVICES, &s, &c) == ZXP_E_DATE, "window before contract");
    s.window_to = 20270630;
    p.repay = 999999;
    CHECK(zxp_draft(ZXP_TPL_LOAN, &p, &c) == ZXP_E_NUMBER, "repay below advance");
    CHECK(zxp_draft((zxp_template_t) 9, &s, &c) == ZXP_E_ARG, "unknown template");
    s.b_id = "ACME";
    CHECK(zxp_draft(ZXP_TPL_SERVICES, &s, &c) == ZXP_E_PARTY, "same party twice");
}

static void test_family(void)
{
    char t[96];
    CHECK(zxp_family("FR") == ZXP_FAM_CIVIL && zxp_family("US-NY") == ZXP_FAM_COMMON &&
              zxp_family("US-LA") == (ZXP_FAM_COMMON | ZXP_FAM_CIVIL) &&
              zxp_family("CA-QC") == ZXP_FAM_CIVIL && zxp_family("CA-ON") == ZXP_FAM_COMMON &&
              zxp_family("SA") == ZXP_FAM_MUSLIM && zxp_family("ZZ") == 0 && zxp_family("x") == 0,
          "family lookup");
    CHECK(zxp_family_text(ZXP_FAM_CIVIL | ZXP_FAM_CUSTOMARY, t, sizeof t) > 0 &&
              strcmp(t, "civil law, customary law") == 0,
          "family text '%s'", t);
    CHECK(zxp_family_text(0, t, sizeof t) > 0 && strstr(t, "not in"), "unknown family");
    CHECK(zxp_family_text(31, t, 8) == 0 && t[0] == 0, "short buffer");
}

int main(void)
{
    test_grammar();
    test_clause_offset();
    test_record();
    test_validate_hand();
    test_dates_and_tags();
    test_draft();
    test_family();
    printf("%s: %d passed, %d failed\n", g_fail ? "FAIL" : "PASS", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
