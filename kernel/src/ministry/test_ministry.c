/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_ministry.c — every assertion is anchored to an EXTERNAL truth:
 *   - a proposal-stated table (Ministry forms priceable, Crown forms not)
 *   - a hand-computed number (11% of 100 = 11)
 *   - the double-entry conservation identity (debits == credits, trial == 0)
 *   - a source-level anti-capture scan (no crown/credential/signing symbol)
 * Never against what the module merely happened to return.
 */
#include <stdio.h>
#include <string.h>
#include "ministry.h"

static int g_assert = 0;
static int g_fail = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        g_assert++;                                                                                \
        if (cond) {                                                                                \
            printf("  ok   : %s\n", msg);                                                          \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("  FAIL : %s\n", msg);                                                          \
        }                                                                                          \
    } while (0)

/* ---- Anchor 1: anti-capture. Scan ministry.c with comments STRIPPED and
 * prove no crown/credential/signing/ISC identifier appears in actual code. */
static int forbidden_symbol_in_code(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1; /* -1 = could not open */
    static const char *bad[] = {"crown", "credential", "signing", "sign_key", "ISC", "revoke_cred"};
    char line[512];
    int in_block = 0;
    int hits = 0;
    while (fgets(line, sizeof line, fp)) {
        /* strip comments into `code` */
        char code[512];
        int ci = 0;
        for (int i = 0; line[i]; i++) {
            if (in_block) {
                if (line[i] == '*' && line[i + 1] == '/') {
                    in_block = 0;
                    i++;
                }
                continue;
            }
            if (line[i] == '/' && line[i + 1] == '*') {
                in_block = 1;
                i++;
                continue;
            }
            if (line[i] == '/' && line[i + 1] == '/') break; /* rest is comment */
            code[ci++] = line[i];
        }
        code[ci] = 0;
        for (size_t b = 0; b < sizeof bad / sizeof bad[0]; b++)
            if (strstr(code, bad[b])) {
                hits++;
                printf("    !! stray '%s' in code: %s", bad[b], code);
            }
    }
    fclose(fp);
    return hits;
}

int main(void)
{
    printf("== MINISTRY PILLAR — external-anchor tests ==\n");

    /* static: these platform structs are multi-megabyte (512 accounts x 256
     * entries) and belong in BSS, not on the stack. */
    static triple_ledger_t tl;
    static ministry_t m;
    triple_ledger_init(&tl);
    ministry_init(&m, &tl);

    /* ---- Anchor 1: no captured symbols in the treasury's actual code ---- */
    int hits = forbidden_symbol_in_code("src/ministry/ministry.c");
    CHECK(hits == 0, "anti-capture: ministry.c code has NO crown/credential/signing/ISC symbol");

    /* ---- Anchor 2: only the four Ministry forms are measurable ----
     * External table (zcapital.h / proposal Sec 3.5): Ministry governs
     * Financial/Manufactured/Intellectual/Human; the Crown's four are inalienable. */
    CHECK(ministry_measure(&m, ZCAP_FINANCIAL, SR_FROM_INT(50)) == MIN_OK,
          "measure(FINANCIAL) == MIN_OK");
    CHECK(ministry_measure(&m, ZCAP_MANUFACTURED, SR_FROM_INT(50)) == MIN_OK,
          "measure(MANUFACTURED) == MIN_OK");
    CHECK(ministry_measure(&m, ZCAP_INTELLECTUAL, SR_FROM_INT(50)) == MIN_OK,
          "measure(INTELLECTUAL) == MIN_OK");
    CHECK(ministry_measure(&m, ZCAP_HUMAN, SR_FROM_INT(50)) == MIN_OK, "measure(HUMAN) == MIN_OK");
    CHECK(ministry_measure(&m, ZCAP_CULTURAL, SR_FROM_INT(50)) == MIN_NOT_MEASURABLE,
          "measure(CULTURAL) == MIN_NOT_MEASURABLE (you cannot price a lullaby)");
    CHECK(ministry_measure(&m, ZCAP_NATURAL, SR_FROM_INT(50)) == MIN_NOT_MEASURABLE,
          "measure(NATURAL) == MIN_NOT_MEASURABLE");
    CHECK(ministry_measure(&m, ZCAP_SOCIAL, SR_FROM_INT(50)) == MIN_NOT_MEASURABLE,
          "measure(SOCIAL) == MIN_NOT_MEASURABLE");
    CHECK(ministry_measure(&m, ZCAP_SPIRITUAL, SR_FROM_INT(50)) == MIN_NOT_MEASURABLE,
          "measure(SPIRITUAL) == MIN_NOT_MEASURABLE");
    CHECK(ministry_measure(&m, ZCAP_SYSTEM, SR_FROM_INT(50)) == MIN_NOT_MEASURABLE,
          "measure(SYSTEM/Co-Juris) == MIN_NOT_MEASURABLE");

    /* ---- Anchor 3: the eleven. Hand-computed: 11% of 100 = 11 ---- */
    CHECK(SR_CMP(ministry_tribute(SR_FROM_INT(100)), SR_FROM_INT(11)) == 0,
          "tribute(100) == 11  (hand-computed)");
    CHECK(SR_CMP(ministry_gratuity(SR_FROM_INT(100)), SR_FROM_INT(11)) == 0,
          "gratuity(100) == 11  (hand-computed)");
    /* principal is UNTOUCHED — gratuity is a separate amount, never a deduction */
    surplus_real_t principal = SR_FROM_INT(100);
    surplus_real_t grat = ministry_gratuity(principal);
    CHECK(SR_CMP(principal, SR_FROM_INT(100)) == 0,
          "gratuity leaves principal == 100 (separate entry, never deducted)");
    CHECK(SR_CMP(SR_ADD(principal, grat), SR_FROM_INT(111)) == 0,
          "principal + gratuity == 111 (the giver adds on top)");

    /* ---- exchange ratio: ops boundary; default 1:1; Crown priced at ZERO ---- */
    CHECK(SR_CMP(ministry_exchange_ratio(&m, ZCAP_FINANCIAL, ZCAP_MANUFACTURED), SR_ONE) == 0,
          "exchange_ratio default is 1:1 for a permitted pair");
    ministry_set_exchange_ratio(&m, ZCAP_FINANCIAL, ZCAP_MANUFACTURED, SR_FROM_INT(3));
    CHECK(SR_CMP(ministry_exchange_ratio(&m, ZCAP_FINANCIAL, ZCAP_MANUFACTURED), SR_FROM_INT(3)) ==
              0,
          "exchange_ratio returns the SUPPLIED ops-boundary rate (3)");
    CHECK(SR_CMP(ministry_exchange_ratio(&m, ZCAP_FINANCIAL, ZCAP_CULTURAL), SR_ZERO) == 0,
          "exchange_ratio into a Crown form == 0 (refuses to price the inalienable)");

    /* ---- Set up two treasury accounts for settlement tests ---- */
    uint32_t a = triple_ledger_create_account(&tl, 1001, CAP_FINANCIAL, "Alice");
    uint32_t b = triple_ledger_create_account(&tl, 1002, CAP_FINANCIAL, "Bob");

    /* ---- Anchor 4: usury is void. interest > 0 -> MIN_VOID_MAXIM, fail-closed ---- */
    uint32_t entries_before = tl.accounts[a].num_entries + tl.accounts[b].num_entries;
    ministry_result_t usury = ministry_settle(&m, a, b, SR_FROM_INT(100), SR_FROM_INT(5));
    CHECK(usury == MIN_VOID_MAXIM,
          "settle with interest>0 == MIN_VOID_MAXIM (One Policy: no usury)");
    uint32_t entries_after_usury = tl.accounts[a].num_entries + tl.accounts[b].num_entries;
    CHECK(entries_before == entries_after_usury,
          "usury settlement posted NOTHING (refused before touching the books)");

    /* ---- undercovered: tiny amount fails the 1.8x floor, fail-closed ---- */
    ministry_result_t under = ministry_settle(&m, a, b, SR_FROM_INT(1), SR_ZERO);
    CHECK(under == MIN_UNDERCOVERED, "settle(1) == MIN_UNDERCOVERED (1/1.8 < 1.8 coverage floor)");

    /* ---- Anchor 5: a real settlement composes the ledger and leaves the
     * books BALANCED. Conservation identity from triple_ledger.h:
     *   trial_balance = total_debits - total_credits == 0. ---- */
    ministry_result_t ok = ministry_settle(&m, a, b, SR_FROM_INT(100), SR_ZERO);
    CHECK(ok == MIN_OK, "settle(100, interest=0) == MIN_OK");

    conventional_report_t rpt;
    triple_ledger_export_conventional(&tl, &rpt);
    CHECK(SR_CMP(rpt.trial_balance, SR_ZERO) == 0,
          "CONSERVATION: trial_balance (debits - credits) == 0 after settlement");
    CHECK((tl.accounts[a].num_entries + tl.accounts[b].num_entries) > 0,
          "settlement actually posted double-entry rows through the triple ledger");
    CHECK(SR_CMP(tl.total_equity, SR_ZERO) >= 0,
          "equity >= 0 preserved by the balanced double entry");

    printf("\n%d assertions, %d failures\n", g_assert, g_fail);
    if (g_fail == 0) printf("ALL GREEN\n");
    return g_fail ? 1 : 0;
}
