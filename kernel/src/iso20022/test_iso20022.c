/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_iso20022.c — host test. Asserts STRUCTURE and NUMBERS against
 * hand-verified answers, and proves the serializer never overruns.
 * stdio/string are used ONLY here, under TEST_HOST — never in the module. */

#include "iso20022.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int g_asserts = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        g_asserts++;                                                                               \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

/* Assert `needle` occurs in `hay`, and at or after position *from; advance
 * *from past it. This proves substrings appear IN ORDER. */
static int in_order(const char *hay, const char *needle, size_t *from)
{
    const char *p = strstr(hay + *from, needle);
    if (!p) {
        fprintf(stderr, "MISSING in-order: '%s'\n", needle);
        return 0;
    }
    *from = (size_t) (p - hay) + strlen(needle);
    return 1;
}
#define ORDER(hay, needle, from)                                                                   \
    do {                                                                                           \
        g_asserts++;                                                                               \
        if (!in_order(hay, needle, &from)) return 1;                                               \
    } while (0)
#define CONTAINS(hay, needle)                                                                      \
    do {                                                                                           \
        g_asserts++;                                                                               \
        if (!strstr(hay, needle)) {                                                                \
            fprintf(stderr, "MISSING: '%s'\n", needle);                                            \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

/* ---- (1) rail -> ccy and the 999->XXX caveat ---- */
static int test_rail_ccy(void)
{
    CHECK(iso20022_rail_ccy(ISO_RAIL_DEBIT) == 846);
    CHECK(iso20022_rail_ccy(ISO_RAIL_CREDIT) == 810);
    CHECK(iso20022_rail_ccy(ISO_RAIL_EQUITY) == 888);

    /* 999 is a REAL code (XXX) but means "no currency" — the caveat. */
    CHECK(strcmp(iso20022_ccy_alpha(999), "XXX") == 0);
    CHECK(strlen(iso20022_ccy_caveat(999)) > 0);
    CHECK(strstr(iso20022_ccy_caveat(999), "XXX") != NULL);

    /* 846/810/888 are Vino-internal, NOT conventional currencies: empty alpha,
     * non-empty caveat so nobody silently wires an "846" amount. */
    CHECK(strcmp(iso20022_ccy_alpha(846), "") == 0);
    CHECK(strcmp(iso20022_ccy_alpha(810), "") == 0);
    CHECK(strcmp(iso20022_ccy_alpha(888), "") == 0);
    CHECK(strstr(iso20022_ccy_caveat(810), "RUR") != NULL);
    CHECK(strlen(iso20022_ccy_caveat(846)) > 0);
    CHECK(strlen(iso20022_ccy_caveat(888)) > 0);

    /* Real settlement currencies still map correctly. */
    CHECK(strcmp(iso20022_ccy_alpha(840), "USD") == 0);
    CHECK(strcmp(iso20022_ccy_alpha(978), "EUR") == 0);
    CHECK(strcmp(iso20022_ccy_alpha(356), "INR") == 0);
    /* A real currency carries no caveat. */
    CHECK(strcmp(iso20022_ccy_caveat(840), "") == 0);

    printf("[ok] rail->ccy + 999/XXX caveat\n");
    return 0;
}

/* ---- (5) fixed-point amount formatting for known values ---- */
static int test_amount_format(void)
{
    char b[32];
    CHECK(iso20022_format_amount(b, sizeof b, 123456, 2) > 0);
    CHECK(strcmp(b, "1234.56") == 0);

    CHECK(iso20022_format_amount(b, sizeof b, 5, 2) > 0);
    CHECK(strcmp(b, "0.05") == 0); /* zero-padded fraction */

    CHECK(iso20022_format_amount(b, sizeof b, 100, 0) > 0);
    CHECK(strcmp(b, "100") == 0);

    CHECK(iso20022_format_amount(b, sizeof b, 1000000, 2) > 0);
    CHECK(strcmp(b, "10000.00") == 0);

    CHECK(iso20022_format_amount(b, sizeof b, -4200, 2) > 0);
    CHECK(strcmp(b, "42.00") == 0); /* magnitude only, no sign */

    CHECK(iso20022_format_amount(b, sizeof b, 0, 2) > 0);
    CHECK(strcmp(b, "0.00") == 0);

    /* Too-small buffer -> truncation error, never overrun. */
    char tiny[4];
    CHECK(iso20022_format_amount(tiny, sizeof tiny, 123456, 2) == ISO_ERR_TRUNC);
    CHECK(tiny[3] == '\0'); /* still NUL-terminated */
    CHECK(iso20022_format_amount(b, 0, 1, 0) == ISO_ERR_TRUNC);
    CHECK(iso20022_format_amount(NULL, 8, 1, 0) == ISO_ERR_NULL);

    printf("[ok] fixed-point amount formatting\n");
    return 0;
}

static pacs008_t sample_pacs(void)
{
    pacs008_t m;
    memset(&m, 0, sizeof m);
    strcpy(m.msg_id, "ZXV-MSG-0001");
    strcpy(m.cre_dt_tm, "2026-08-05T12:00:00");
    m.nb_of_txs = 1;
    strcpy(m.debtor_name, "House of Vulpine");
    strcpy(m.debtor_acct, "ZXV000123456789");
    strcpy(m.creditor_name, "Bank of Legacy");
    strcpy(m.creditor_acct, "GB29NWBK60161331926819");
    m.amount_units = 1234567; /* 12345.67 */
    m.amount_frac = 2;
    strcpy(m.ccy, "USD");
    strcpy(m.end_to_end_id, "E2E-ABCDEF-0001");
    return m;
}

/* ---- (2) PACS.008 structure + numbers, IN ORDER ---- */
static int test_pacs008(void)
{
    pacs008_t m = sample_pacs();
    char out[2048];
    int32_t n = iso20022_pacs008_build(&m, out, sizeof out);
    CHECK(n > 0);
    CHECK((size_t) n == strlen(out));

    size_t pos = 0;
    ORDER(out, "pacs.008.001.09", pos); /* MsgDefIdr namespace */
    ORDER(out, "<GrpHdr>", pos);
    ORDER(out, "<MsgId>ZXV-MSG-0001</MsgId>", pos);
    ORDER(out, "<NbOfTxs>1</NbOfTxs>", pos);
    ORDER(out, "<IntrBkSttlmAmt Ccy=\"USD\">12345.67</IntrBkSttlmAmt>", pos);
    ORDER(out, "House of Vulpine", pos); /* debtor name */
    ORDER(out, "Bank of Legacy", pos);   /* creditor name */

    /* EndToEndId lives in PmtId (early, by real MX order) — assert presence.
     * Accounts present, amount currency correct. */
    CONTAINS(out, "<EndToEndId>E2E-ABCDEF-0001</EndToEndId>");
    CONTAINS(out, "ZXV000123456789");
    CONTAINS(out, "GB29NWBK60161331926819");
    CONTAINS(out, "<InstdAmt Ccy=\"USD\">12345.67</InstdAmt>");

    printf("[ok] PACS.008 structure + numbers (%d bytes)\n", n);
    return 0;
}

/* ---- (4) serializer NEVER overruns: one-byte-too-small -> truncation ---- */
static int test_pacs008_truncation(void)
{
    pacs008_t m = sample_pacs();
    char out[4096];
    int32_t full = iso20022_pacs008_build(&m, out, sizeof out);
    CHECK(full > 0);

    /* Exact fit works. */
    char exact[4096];
    int32_t n2 = iso20022_pacs008_build(&m, exact, (uint32_t) full + 1);
    CHECK(n2 == full);

    /* One byte too small -> truncation error, NUL-terminated, no overrun.
     * ASan/UBSan will catch any out-of-bounds write here. */
    char *snug = (char *) malloc((size_t) full); /* exactly one short */
    CHECK(snug != NULL);
    int32_t n3 = iso20022_pacs008_build(&m, snug, (uint32_t) full);
    CHECK(n3 == ISO_ERR_TRUNC);
    CHECK(snug[full - 1] == '\0');
    free(snug);

    /* Sweep every capacity from 0..full to exercise the bound at each byte. */
    for (uint32_t cap = 0; cap <= (uint32_t) full; cap++) {
        char *b = (char *) malloc(cap ? cap : 1);
        int32_t r = iso20022_pacs008_build(&m, b, cap);
        CHECK(r == ISO_ERR_TRUNC); /* every short cap truncates */
        if (cap > 0) CHECK(b[cap - 1] == '\0');
        free(b);
    }
    g_asserts++; /* count the sweep as one logical assertion group */

    printf("[ok] PACS.008 truncation is safe at every capacity\n");
    return 0;
}

/* ---- (3) CAMT.053 entries + opening/closing balances ---- */
static int test_camt053(void)
{
    camt053_t m;
    memset(&m, 0, sizeof m);
    strcpy(m.msg_id, "ZXV-STMT-0007");
    strcpy(m.cre_dt_tm, "2026-08-05T23:59:00");
    strcpy(m.acct_id, "ZXV-ACCT-42");
    strcpy(m.ccy, "EUR");
    m.opening_units = 100000;
    m.opening_frac = 2; /* 1000.00 */
    m.closing_units = 125050;
    m.closing_frac = 2; /* 1250.50 */

    m.nb_entries = 3;
    m.entries[0] = (camt_entry_t){25050, 2, ISO_CRDT, ISO_STS_BOOK}; /* +250.50 */
    m.entries[1] = (camt_entry_t){10000, 2, ISO_DBIT, ISO_STS_BOOK}; /* -100.00 */
    m.entries[2] = (camt_entry_t){5000, 2, ISO_CRDT, ISO_STS_PDNG};  /* +50.00 pending */

    char out[4096];
    int32_t n = iso20022_camt053_build(&m, out, sizeof out);
    CHECK(n > 0);
    CHECK((size_t) n == strlen(out));

    CONTAINS(out, "camt.053.001.08");
    CONTAINS(out, "<MsgId>ZXV-STMT-0007</MsgId>");
    CONTAINS(out, "ZXV-ACCT-42");

    /* Opening/closing balances, exact numbers. */
    CONTAINS(out, "<Cd>OPBD</Cd></CdOrPrtry></Tp><Amt Ccy=\"EUR\">1000.00</Amt>");
    CONTAINS(out, "<Cd>CLBD</Cd></CdOrPrtry></Tp><Amt Ccy=\"EUR\">1250.50</Amt>");

    /* Each entry: amount + CdtDbtInd. */
    size_t pos = 0;
    ORDER(out, "<Amt Ccy=\"EUR\">250.50</Amt>", pos);
    ORDER(out, "<CdtDbtInd>CRDT</CdtDbtInd>", pos);
    ORDER(out, "<Amt Ccy=\"EUR\">100.00</Amt>", pos);
    ORDER(out, "<CdtDbtInd>DBIT</CdtDbtInd>", pos);
    ORDER(out, "<Amt Ccy=\"EUR\">50.00</Amt>", pos);
    ORDER(out, "<Sts><Cd>PDNG</Cd></Sts>", pos);

    /* Truncation on CAMT too. */
    char *snug = (char *) malloc((size_t) n);
    int32_t r = iso20022_camt053_build(&m, snug, (uint32_t) n);
    CHECK(r == ISO_ERR_TRUNC);
    CHECK(snug[n - 1] == '\0');
    free(snug);

    printf("[ok] CAMT.053 entries + OPBD/CLBD balances (%d bytes)\n", n);
    return 0;
}

/* ---- XML escaping of a hostile name ---- */
static int test_escaping(void)
{
    pacs008_t m = sample_pacs();
    strcpy(m.debtor_name, "Tom & <Jerry>");
    char out[2048];
    int32_t n = iso20022_pacs008_build(&m, out, sizeof out);
    CHECK(n > 0);
    CONTAINS(out, "Tom &amp; &lt;Jerry&gt;");
    CHECK(strstr(out, "Tom & <Jerry>") == NULL); /* raw form must NOT appear */
    printf("[ok] XML escaping\n");
    return 0;
}

/* ---- NULL guards ---- */
static int test_null_guards(void)
{
    char out[16];
    CHECK(iso20022_pacs008_build(NULL, out, sizeof out) == ISO_ERR_NULL);
    CHECK(iso20022_camt053_build(NULL, out, sizeof out) == ISO_ERR_NULL);
    pacs008_t m = sample_pacs();
    CHECK(iso20022_pacs008_build(&m, NULL, 16) == ISO_ERR_NULL);
    printf("[ok] NULL guards\n");
    return 0;
}

int main(void)
{
    if (test_rail_ccy()) return 1;
    if (test_amount_format()) return 1;
    if (test_pacs008()) return 1;
    if (test_pacs008_truncation()) return 1;
    if (test_camt053()) return 1;
    if (test_escaping()) return 1;
    if (test_null_guards()) return 1;
    printf("\nALL PASS — %d assertions\n", g_asserts);
    return 0;
}
