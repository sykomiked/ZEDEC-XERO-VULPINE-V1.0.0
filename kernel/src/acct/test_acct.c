/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_acct.c — the accounting adapters (acct.h A1-A8), checked against
 * published worked examples wherever one exists:
 *
 *   IAS 12 Illustrative Example 2, 81(c)(i) and (ii): IFRS Foundation,
 *     IFRS Accounting Taxonomy 2024 illustrative example 10
 *     (ifrs.org/.../ixbrl-example10-2024-03-27.xhtml)
 *   IAS 1 IG6 XYZ Group statement of financial position, 20X7 and 20X6:
 *     IFRS Accounting Taxonomy 2024 illustrative example 2
 *   ASC 740 rate reconciliation: RSM, "ASU 2023-09 Expanded Income Tax
 *     Disclosure Requirements" (June 2025), Example 7-2; BDO, "ASC 740
 *     ASU 2023-09" (June 2025), Example 2-4 (the 5% threshold)
 *   ASC 606 Example 52, customer loyalty programme (606-10-55-353 to 55-356):
 *     FASB ASU 2014-09 Section A
 *   Gift-card breakage: Journal of Accountancy, "Accounting for gift cards"
 *     (February 2015), Example 2
 *   Murabaha under FAS 28: R. Moosa, "An Overview of Islamic Accounting: The
 *     Murabaha Contract", J. Risk Financial Manag. 16 (2023) 335, Tables 5-10
 *   Zakah rate 2.5775% (Gregorian year): Emirates NBD, "Due Zakat on
 *     Emirates NBD Shareholders for the year ended 31 December 2024"
 *   Zakah base (net working capital): El-Badawi and Al-Sultan, American
 *     Journal of Islamic Social Sciences 9:1 (1992), Figure III
 *   LIFO: Cleverence, "How to calculate cost of goods sold using LIFO"
 *
 * Numbers marked "ours" are this adapter's own arithmetic on published facts
 * (no published figure exists for them). Integer only. */
#include <stdio.h>
#include <string.h>
#include "acct.h"

static int pass, fail;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static acct_txn_t tx(acct_kind_t k, int64_t a, int64_t b, uint16_t flags)
{
    acct_txn_t t;
    memset(&t, 0, sizeof t);
    t.kind = (uint16_t) k;
    t.amount = a;
    t.amount2 = b;
    t.flags = flags;
    return t;
}

static int64_t sum_bal(const acct_book_t *b)
{
    int64_t s = 0;
    for (unsigned i = 0; i < ACCT_C_COUNT; i++) s += b->bal[i];
    return s;
}

static void test_names(void)
{
    CHECK(acct_std_from_name("IFRS") == ACCT_STD_IFRS, "IFRS");
    CHECK(acct_std_from_name("US_GAAP") == ACCT_STD_US_GAAP, "US_GAAP");
    CHECK(acct_std_from_name("AAOIFI") == ACCT_STD_AAOIFI, "AAOIFI");
    CHECK(acct_std_from_name("") == ACCT_STD_NONE, "empty is not chosen");
    CHECK(acct_std_from_name("ifrs") == ACCT_STD_COUNT && acct_std_from_name("GAAP") == ACCT_STD_COUNT &&
              acct_std_from_name(NULL) == ACCT_STD_COUNT,
          "names are exact (zxvd's jurisdiction.set rejects the same ones)");
    for (unsigned s = 0; s < ACCT_STD_COUNT; s++)
        CHECK(acct_std_from_name(acct_std_name((acct_std_t) s)) == (acct_std_t) s, "round trip %u", s);
    CHECK(acct_std_supported(ACCT_STD_IFRS) && acct_std_supported(ACCT_STD_US_GAAP) &&
              acct_std_supported(ACCT_STD_AAOIFI) && !acct_std_supported(ACCT_STD_OHADA) &&
              !acct_std_supported(ACCT_STD_IPSAS) && !acct_std_supported(ACCT_STD_CAS) &&
              !acct_std_supported(ACCT_STD_SAP_INSURER) && !acct_std_supported(ACCT_STD_NONE),
          "A1: three frameworks built");
    for (unsigned c = 0; c < ACCT_C_COUNT; c++)
        CHECK(acct_cap_key((acct_cap_t) c)[0] != '\0' && acct_cap_key((acct_cap_t) c)[0] != '?',
              "caption %u has a key", c);
}

/* IAS 12 Example 2, 81(c)(i): X5 at 40%, X6 at 35% */
static void test_ias12(void)
{
    acct_tax_recon_t r;
    /* X5: the published tax effects are 200 (donations) and 280 (fines) at
     * 40%; their pre-tax bases (500, 700) are given here so the adapter
     * computes the effects itself. */
    acct_tax_item_t x5[2] = {{"Charitable donations", ACCT_TAX_ITEM_BASE, 500},
                             {"Fines for environmental pollution", ACCT_TAX_ITEM_BASE, 700}};
    acct_tax_input_t in;
    memset(&in, 0, sizeof in);
    in.profit_before_tax = 8775;
    in.rate_num = 40;
    in.rate_den = 100;
    in.items = x5;
    in.n_items = 2;
    in.reported_tax_expense = 3990;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_IFRS, &in, &r) == ACCT_OK, "X5 runs");
    CHECK(r.tax_at_rate == 3510, "X5 tax at 40%%: %lld", (long long) r.tax_at_rate);
    CHECK(r.effect[0] == 200 && r.effect[1] == 280, "X5 effects 200, 280");
    CHECK(r.expected_tax == 3990 && r.reconciled && r.difference == 0, "X5 tax expense 3,990");
    int64_t pm;
    CHECK(acct_ratio(r.effect[0], 8775, 1000, &pm) && pm == 23, "X5 donations 2.3%%");
    CHECK(acct_ratio(r.effect[1], 8775, 1000, &pm) && pm == 32, "X5 fines 3.2%%");
    CHECK(acct_ratio(r.eff_rate_num, r.eff_rate_den, 1000, &pm) && pm == 455,
          "X5 average effective rate 45.5%%: %lld", (long long) pm);

    /* X6: the published effects are given directly */
    acct_tax_item_t x6[2] = {{"Charitable donations", ACCT_TAX_ITEM_EFFECT, 122},
                             {"Reduction in opening deferred taxes", ACCT_TAX_ITEM_EFFECT, -1127}};
    in.profit_before_tax = 8740;
    in.rate_num = 35;
    in.items = x6;
    in.reported_tax_expense = 2054;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_IFRS, &in, &r) == ACCT_OK, "X6 runs");
    CHECK(r.tax_at_rate == 3059 && r.expected_tax == 2054 && r.reconciled, "X6 tax expense 2,054");
    CHECK(acct_ratio(122, 8740, 1000, &pm) && pm == 14, "X6 donations 1.4%%");
    CHECK(acct_ratio(-1127, 8740, 1000, &pm) && pm == -129, "X6 rate change (12.9)%%");
    CHECK(acct_ratio(r.eff_rate_num, r.eff_rate_den, 1000, &pm) && pm == 235, "X6 effective 23.5%%");

    /* a reported figure one unit off does not reconcile */
    in.reported_tax_expense = 2055;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_IFRS, &in, &r) == ACCT_OK && !r.reconciled &&
              r.difference == 1,
          "X6 off by one is reported, not hidden");
    /* the same schedule under AAOIFI (A1 fallback) and US GAAP reconciles too */
    in.reported_tax_expense = 2054;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_AAOIFI, &in, &r) == ACCT_OK && r.reconciled &&
              !r.has_zakah,
          "AAOIFI without zakah items");
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_OHADA, &in, &r) == ACCT_E_UNSUPPORTED,
          "A1: OHADA not built");
    in.rate_den = 0;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_IFRS, &in, &r) == ACCT_E_ARG, "rate den 0");
    in.rate_den = 100;
    in.rate_num = 101;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_IFRS, &in, &r) == ACCT_E_ARG, "rate above 100%%");
}

/* RSM Example 7-2 (ASC 740 after ASU 2023-09) */
static void test_asc740(void)
{
    static const acct_tax_item_t it[] = {
        {"State and local income taxes, net of federal income tax benefit", 1, 420000},
        {"Foreign tax effects - Macau: statutory tax rate differential", 1, -166500},
        {"Foreign tax effects - Macau: change in valuation allowance", 1, -125000},
        {"Other foreign jurisdictions", 1, 52000},
        {"Effect of changes in tax laws and rates enacted in the current period", 1, -150000},
        {"Effect of cross-border tax laws", 1, 35000},
        {"Tax credits", 1, -197050},
        {"Changes in valuation allowances", 1, -700000},
        {"Nontaxable or nondeductible items - tax exempt interest income", 1, -210000},
        {"Nontaxable or nondeductible items - meals and entertainment", 1, 147000},
        {"Changes in unrecognized tax benefits", 1, -124950},
        {"Other adjustments", 1, 210000},
    };
    acct_tax_input_t in;
    acct_tax_recon_t r;
    memset(&in, 0, sizeof in);
    in.profit_before_tax = 10500000;
    in.rate_num = 21;
    in.rate_den = 100;
    in.items = it;
    in.n_items = sizeof it / sizeof it[0];
    in.reported_tax_expense = 1395500;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_US_GAAP, &in, &r) == ACCT_OK, "RSM runs");
    CHECK(r.tax_at_rate == 2205000, "21%% of $10.5m = $2,205,000");
    CHECK(r.expected_tax == 1395500 && r.reconciled, "total $1,395,500");
    int64_t pm;
    CHECK(acct_ratio(r.eff_rate_num, r.eff_rate_den, 1000, &pm) && pm == 133, "13.3%%");
    /* RSM prints (6.6)% for this row; computed independently it is (6.7)%
     * (700,000 / 10,500,000 = 6.667%). The adapter reports the exact one. */
    CHECK(acct_ratio(-700000, 10500000, 1000, &pm) && pm == -67, "valuation allowance (6.7)%%");
    /* BDO Example 2-4: 21% x 5% threshold = 1.05% of $3.32m = $34,860 */
    int64_t th;
    CHECK(acct_mul_rate(3320000, 21 * 5, 100 * 100, &th) && th == 34860, "BDO threshold $34,860");
}

/* IAS 1 IG6 XYZ Group (thousands of currency units) */
static void xyz(acct_book_t *b, bool x7)
{
    /* an opening trial balance: debit positive, credit negative */
    b->bal[ACCT_C_PPE] = x7 ? 350700 : 360020;
    b->bal[ACCT_C_GOODWILL] = x7 ? 80800 : 91200;
    b->bal[ACCT_C_INTANGIBLE] = 227470;
    b->bal[ACCT_C_INVESTMENTS] = x7 ? 100150 + 142500 : 110770 + 156000;
    b->bal[ACCT_C_INVENTORY] = x7 ? 135230 : 132500;
    b->bal[ACCT_C_RECEIVABLES] = x7 ? 91600 : 110800;
    b->bal[ACCT_C_OTHER_CURRENT_ASSETS] = x7 ? 25650 : 12540;
    b->bal[ACCT_C_CASH] = x7 ? 312400 : 322900;
    b->bal[ACCT_C_SHARE_CAPITAL] = x7 ? -650000 : -600000;
    b->bal[ACCT_C_RETAINED_EARNINGS] = x7 ? -243500 : -161700;
    b->bal[ACCT_C_OTHER_EQUITY] = x7 ? -10200 : -21200;
    b->bal[ACCT_C_NCI] = x7 ? -70050 : -48600;
    b->bal[ACCT_C_BORROWINGS] = x7 ? -120000 : -160000;
    b->bal[ACCT_C_DEFERRED_TAX_LIABILITY] = x7 ? -28800 : -26040;
    b->bal[ACCT_C_PROVISIONS_NONCURRENT] = x7 ? -28850 : -52240;
    b->bal[ACCT_C_PAYABLES] = x7 ? -115100 : -187620;
    b->bal[ACCT_C_SHORT_TERM_BORROWINGS] = x7 ? -(150000 + 10000) : -(200000 + 20000);
    b->bal[ACCT_C_TAX_PAYABLE] = x7 ? -35000 : -42000;
    b->bal[ACCT_C_PROVISIONS] = x7 ? -5000 : -4800;
}

static void test_ias1(void)
{
    for (int y = 0; y < 2; y++) {
        bool x7 = y == 0;
        acct_book_t b;
        acct_bs_t bs;
        acct_book_init(&b, ACCT_STD_IFRS);
        xyz(&b, x7);
        CHECK(sum_bal(&b) == 0, "XYZ trial balance balances");
        CHECK(acct_generate_balance_sheet(ACCT_STD_IFRS, &b, &bs) == ACCT_OK, "XYZ IFRS");
        CHECK(bs.total_assets == (x7 ? 1466500 : 1524200), "total assets %lld",
              (long long) bs.total_assets);
        CHECK(acct_bs_section_total(&bs, ACCT_S_NONCURRENT_ASSETS) == (x7 ? 901620 : 945460),
              "non-current assets");
        CHECK(acct_bs_section_total(&bs, ACCT_S_CURRENT_ASSETS) == (x7 ? 564880 : 578740),
              "current assets");
        CHECK(bs.total_equity == (x7 ? 973750 : 831500), "total equity %lld",
              (long long) bs.total_equity);
        CHECK(acct_bs_section_total(&bs, ACCT_S_NONCURRENT_LIABILITIES) == (x7 ? 177650 : 238280),
              "non-current liabilities");
        CHECK(acct_bs_section_total(&bs, ACCT_S_CURRENT_LIABILITIES) == (x7 ? 315100 : 454420),
              "current liabilities");
        CHECK(bs.total_liabilities == (x7 ? 492750 : 692700), "total liabilities");
        /* IAS 1 IG order: PPE first, cash last of the assets */
        CHECK(bs.line[0].cap == ACCT_C_PPE && bs.line[7].cap == ACCT_C_CASH &&
                  bs.line[8].cap == ACCT_C_SHARE_CAPITAL,
              "IG layout order");
        CHECK(acct_bs_amount(&bs, ACCT_C_NCI) == (x7 ? 70050 : 48600), "NCI");

        /* the same balances under US GAAP and AAOIFI: same totals, their layouts */
        acct_book_t g = b;
        g.std = ACCT_STD_US_GAAP;
        CHECK(acct_generate_balance_sheet(ACCT_STD_US_GAAP, &g, &bs) == ACCT_OK &&
                  bs.total_assets == (x7 ? 1466500 : 1524200) && bs.line[0].cap == ACCT_C_CASH,
              "US GAAP layout: cash first");
        g.std = ACCT_STD_AAOIFI;
        CHECK(acct_generate_balance_sheet(ACCT_STD_AAOIFI, &g, &bs) == ACCT_OK &&
                  bs.total_assets == (x7 ? 1466500 : 1524200) &&
                  acct_bs_section_total(&bs, ACCT_S_ASSETS) == bs.total_assets &&
                  acct_bs_section_total(&bs, ACCT_S_CURRENT_ASSETS) == 0,
              "AAOIFI layout: no current / non-current split");
        if (x7) {
            char js[4096];
            acct_book_t b2 = b;
            CHECK(acct_generate_balance_sheet(ACCT_STD_IFRS, &b2, &bs) == ACCT_OK, "again");
            int n = acct_bs_json(&bs, js, sizeof js);
            CHECK(n > 0 && strstr(js, "\"total_assets\":1466500") &&
                      strstr(js, "\"label\":\"Property, plant and equipment\",\"amount\":350700") &&
                      strstr(js, "{\"standard\":\"IFRS\""),
                  "JSON: %s", js);
            CHECK(acct_bs_json(&bs, js, 40) == -1 && js[0] == '\0', "JSON too small");
        }
    }
    /* a book from one framework is not presented in another */
    acct_book_t b;
    acct_bs_t bs;
    acct_book_init(&b, ACCT_STD_IFRS);
    CHECK(acct_generate_balance_sheet(ACCT_STD_US_GAAP, &b, &bs) == ACCT_E_ARG, "framework mismatch");
    acct_book_init(&b, ACCT_STD_CAS);
    CHECK(acct_generate_balance_sheet(ACCT_STD_CAS, &b, &bs) == ACCT_E_UNSUPPORTED, "CAS not built");
    /* an unbalanced trial balance is refused */
    acct_book_init(&b, ACCT_STD_IFRS);
    b.bal[ACCT_C_CASH] = 5;
    CHECK(acct_generate_balance_sheet(ACCT_STD_IFRS, &b, &bs) == ACCT_E_UNBALANCED, "A6 unbalanced");
    /* a caption the framework has no line for is refused, not dropped */
    acct_book_init(&b, ACCT_STD_IFRS);
    b.bal[ACCT_C_CASH] = 5;
    b.bal[ACCT_C_IAH] = -5;
    CHECK(acct_generate_balance_sheet(ACCT_STD_IFRS, &b, &bs) == ACCT_E_NOT_PERMITTED,
          "IAH has no place in an IFRS statement");
}

/* Moosa (2023): cost $25,000, price $30,000, 5 x $6,000, in cents */
static void test_murabaha(void)
{
    const int64_t pay[5] = {600000, 600000, 600000, 600000, 600000};
    int64_t prof[5];
    /* the schedule reproduces only at the contract's IRR, 6.40224% (the
     * paper rounds it to "6.4 percent") */
    CHECK(acct_murabaha_schedule(2500000, pay, 5, 640224, 10000000, prof) == ACCT_OK, "schedule");
    const int64_t pub[5] = {160056, 131890, 101920, 70032, 36102}; /* Table 5 */
    for (int i = 0; i < 5; i++)
        CHECK(prof[i] == pub[i], "year %d profit %lld", i + 1, (long long) prof[i]);
    CHECK(prof[0] + prof[1] + prof[2] + prof[3] + prof[4] == 500000, "profit adds up to the markup");

    /* Table 10: deferred profit 3,399.44 / 2,080.54 / 1,061.34 / 361.02 / 0
     * (the paper prints 2,080.52 for year 2, a typo: 3,399.44 - 1,318.90 =
     * 2,080.54, and its own year 3 figure follows from 2,080.54) and the net
     * receivable 20,601 / 15,919 / 10,939 / 5,639 / 0 rounded to dollars. */
    const int64_t defp[5] = {339944, 208054, 106134, 36102, 0};
    const int64_t net_dollars[5] = {20601, 15919, 10939, 5639, 0};
    acct_book_t a, f;
    acct_book_init(&a, ACCT_STD_AAOIFI);
    acct_book_init(&f, ACCT_STD_IFRS);
    acct_txn_t t = tx(ACCT_K_CAPITAL, 2500000, 0, 0);
    CHECK(acct_record(&a, NULL, &t) == ACCT_OK && acct_record(&f, NULL, &t) == ACCT_OK, "capital");
    t = tx(ACCT_K_PURCHASE_INVENTORY, 2500000, 0, 0); /* Table 6 */
    CHECK(acct_record(&a, NULL, &t) == ACCT_OK && acct_record(&f, NULL, &t) == ACCT_OK, "buy");
    t = tx(ACCT_K_MURABAHA_SALE, 2500000, 500000, 0);
    acct_journal_t j;
    CHECK(acct_translate_transaction(ACCT_STD_AAOIFI, NULL, &t, &j) == ACCT_OK && j.n == 3 &&
              j.line[0].cap == ACCT_C_FINANCING && j.line[0].dr == 3000000 &&
              j.line[2].cap == ACCT_C_DEFERRED_PROFIT && j.line[2].cr == 500000,
          "FAS 28: receivable at the selling price, deferred profit as a contra-asset");
    CHECK(acct_record(&a, NULL, &t) == ACCT_OK && acct_record(&f, NULL, &t) == ACCT_OK, "sale");
    for (int y = 0; y < 5; y++) {
        acct_txn_t c = tx(ACCT_K_MURABAHA_COLLECT, pay[y], 0, 0);
        acct_txn_t p = tx(ACCT_K_MURABAHA_PROFIT, prof[y], 0, 0);
        CHECK(acct_record(&a, NULL, &c) == ACCT_OK && acct_record(&a, NULL, &p) == ACCT_OK &&
                  acct_record(&f, NULL, &c) == ACCT_OK && acct_record(&f, NULL, &p) == ACCT_OK,
              "year %d", y + 1);
        int64_t gross = a.bal[ACCT_C_FINANCING], dp = -a.bal[ACCT_C_DEFERRED_PROFIT];
        CHECK(gross == 3000000 - 600000 * (y + 1), "gross receivable year %d", y + 1);
        CHECK(dp == defp[y], "deferred profit year %d: %lld", y + 1, (long long) dp);
        int64_t net = gross - dp;
        CHECK((net + 50) / 100 == net_dollars[y], "net receivable year %d: %lld", y + 1,
              (long long) net);
        CHECK(f.bal[ACCT_C_FINANCING] == net, "IFRS amortised cost = AAOIFI net, year %d", y + 1);
    }
    acct_bs_t bs;
    CHECK(acct_generate_balance_sheet(ACCT_STD_AAOIFI, &a, &bs) == ACCT_OK && bs.profit == 500000 &&
              bs.total_assets == 3000000,
          "AAOIFI after five years: cash 30,000, profit 5,000");
    /* refusals */
    int64_t bad[2] = {100, 100};
    CHECK(acct_murabaha_schedule(1000, bad, 2, 1, 10, prof) == ACCT_E_ARG,
          "payments that do not clear the balance");
    /* straight-line split adds up exactly */
    int64_t s1, s2, s3;
    CHECK(acct_murabaha_profit(500000, 365, 0, 100, &s1) == ACCT_OK &&
              acct_murabaha_profit(500000, 365, 100, 200, &s2) == ACCT_OK &&
              acct_murabaha_profit(500000, 365, 200, 365, &s3) == ACCT_OK && s1 + s2 + s3 == 500000,
          "time-proportion parts add up");
    CHECK(acct_murabaha_profit(500000, 365, 200, 100, &s1) == ACCT_E_ARG, "from > to");
}

/* ASC 606 Example 52 and the Journal of Accountancy gift-card example */
static void test_breakage(void)
{
    int64_t ssp[2] = {100000, 9500}, alloc[2];
    CHECK(acct_allocate(100000, ssp, 2, alloc) == ACCT_OK && alloc[0] == 91324 && alloc[1] == 8676,
          "Example 52 allocation 91,324 / 8,676");
    int64_t now;
    CHECK(acct_pattern_revenue(8676, 4500, 9500, 0, &now) == ACCT_OK && now == 4110,
          "period 1 revenue 4,110");
    CHECK(8676 - now == 4566, "period 1 contract liability 4,566");
    CHECK(acct_pattern_revenue(8676, 8500, 9700, 4110, &now) == ACCT_OK && now == 3493,
          "period 2 revenue 3,493");
    CHECK(8676 - 4110 - now == 1073, "period 2 contract liability 1,073");
    CHECK(acct_pattern_revenue(8676, 9700, 9700, 7603, &now) == ACCT_OK && now == 1073,
          "all redeemed: the rest");
    CHECK(acct_pattern_revenue(100, 1, 0, 0, &now) == ACCT_OK && now == 100, "nothing expected");
    CHECK(acct_pattern_revenue(100, 1, 2, 101, &now) == ACCT_E_ARG, "recognised above amount");

    /* JoA: $2,400 sold, 10% breakage, a $162 redemption -> $18 (in cents) */
    CHECK(acct_breakage(240000, 24000, 16200, 0, &now) == ACCT_OK && now == 1800, "$18 breakage");
    CHECK(acct_breakage(240000, 24000, 216000, 1800, &now) == ACCT_OK && now == 22200,
          "the rest once all expected redemptions happen");
    CHECK(acct_breakage(240000, 240001, 0, 0, &now) == ACCT_E_ARG, "breakage above issued");

    /* the same events through the book: liability, redemption, breakage */
    acct_book_t b;
    acct_book_init(&b, ACCT_STD_IFRS);
    acct_txn_t t = tx(ACCT_K_VOUCHER_ISSUE, 240000, 0, 0);
    CHECK(acct_record(&b, NULL, &t) == ACCT_OK, "issue");
    t = tx(ACCT_K_VOUCHER_REDEEM, 16200, 0, 0);
    CHECK(acct_record(&b, NULL, &t) == ACCT_OK, "redeem");
    t = tx(ACCT_K_VOUCHER_BREAKAGE, 1800, 0, 0);
    CHECK(acct_record(&b, NULL, &t) == ACCT_OK, "breakage");
    CHECK(-b.bal[ACCT_C_CONTRACT_LIABILITY] == 222000 && -b.bal[ACCT_C_REVENUE] == 18000,
          "A5: liability 2,220, revenue 180");
    acct_journal_t j;
    CHECK(acct_translate_transaction(ACCT_STD_US_GAAP, NULL, &t, &j) == ACCT_OK &&
              strcmp(j.ref, "ASC 606-10-55-48") == 0,
          "US GAAP reference");

    /* allocation edge cases */
    int64_t z[3] = {1, 1, 1}, o[3];
    CHECK(acct_allocate(100, z, 3, o) == ACCT_OK && o[0] == 34 && o[1] == 33 && o[2] == 33,
          "largest remainder, ties to the earlier one");
    int64_t zero[2] = {0, 0};
    CHECK(acct_allocate(100, zero, 2, o) == ACCT_E_ARG, "no selling prices");
}

/* Cleverence LIFO example; IAS 2.25 refuses LIFO */
static void test_inventory(void)
{
    const acct_layer_t l[4] = {{100, 10}, {80, 11}, {120, 12}, {100, 13}};
    int64_t cogs, end;
    CHECK(acct_cost_flow(ACCT_STD_US_GAAP, l, 4, 270, ACCT_COST_LIFO, &cogs, &end) == ACCT_OK &&
              cogs == 3290 && end == 1330,
          "LIFO cost of sales $3,290, ending $1,330");
    CHECK(acct_cost_flow(ACCT_STD_IFRS, l, 4, 270, ACCT_COST_LIFO, &cogs, &end) ==
              ACCT_E_NOT_PERMITTED,
          "IAS 2.25: no LIFO under IFRS");
    CHECK(acct_cost_flow(ACCT_STD_AAOIFI, l, 4, 270, ACCT_COST_LIFO, &cogs, &end) ==
              ACCT_E_NOT_PERMITTED,
          "nor under AAOIFI (A1)");
    /* ours: FIFO on the same layers */
    CHECK(acct_cost_flow(ACCT_STD_IFRS, l, 4, 270, ACCT_COST_FIFO, &cogs, &end) == ACCT_OK &&
              cogs == 2960 && end == 1660,
          "FIFO 2,960 / 1,660 (ours)");
    CHECK(acct_cost_flow(ACCT_STD_IFRS, l, 4, 270, ACCT_COST_WAVG, &cogs, &end) == ACCT_OK &&
              cogs + end == 4620 && cogs == 3119,
          "weighted average 3,118.5 rounds to 3,119 (ours)");
    CHECK(acct_cost_flow(ACCT_STD_IFRS, l, 4, 401, ACCT_COST_FIFO, &cogs, &end) == ACCT_E_ARG,
          "cannot sell more than held");
    acct_txn_t t = tx(ACCT_K_COST_OF_SALES, 3290, 0, 0);
    t.cost_method = ACCT_COST_LIFO;
    acct_journal_t j;
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, &t, &j) == ACCT_E_NOT_PERMITTED && j.n == 0,
          "LIFO cost of sales refused under IFRS");
    CHECK(acct_translate_transaction(ACCT_STD_US_GAAP, NULL, &t, &j) == ACCT_OK && j.n == 2,
          "and posted under US GAAP");
}

/* zakah: rate (Emirates NBD) and base (El-Badawi and Al-Sultan) */
static void test_zakah(void)
{
    int64_t z;
    /* AED 22.45 x 2.5775% = 0.5786 (in units of AED 0.0001) */
    CHECK(acct_mul_rate(224500, 25775, 1000000, &z) && z == 5786, "ENBD zakat per share 0.5786");
    CHECK(z - 24 == 5762, "net of zakat paid by the bank: 0.5762");

    const acct_zakah_item_t items[2] = {{239500, true}, {111000, false}};
    acct_tax_input_t in;
    acct_tax_recon_t r;
    memset(&in, 0, sizeof in);
    in.rate_num = 0;
    in.rate_den = 1;
    in.zakah_items = items;
    in.n_zakah_items = 2;
    in.zakah_rate_num = 25;
    in.zakah_rate_den = 1000;
    in.reported_zakah = 3213;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_AAOIFI, &in, &r) == ACCT_OK && r.has_zakah,
          "zakah runs");
    CHECK(r.zakah_base == 128500, "net working capital 128,500");
    CHECK(r.zakah == 3213 && r.zakah_reconciled, "2.5%% of 128,500 = 3,212.5 -> 3,213 (ours)");
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_IFRS, &in, &r) == ACCT_OK && !r.has_zakah,
          "zakah only under AAOIFI");
    const acct_zakah_item_t neg[2] = {{100, true}, {500, false}};
    in.zakah_items = neg;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_AAOIFI, &in, &r) == ACCT_OK && r.zakah_base == 0 &&
              r.zakah == 0,
          "a negative base is zero");
    in.zakah_rate_den = 0;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_AAOIFI, &in, &r) == ACCT_E_ARG, "zakah rate den 0");
}

/* A4 */
static void test_riba(void)
{
    acct_book_t b;
    acct_journal_t j;
    acct_book_init(&b, ACCT_STD_AAOIFI);
    acct_txn_t t = tx(ACCT_K_INTEREST_INCOME, 1000, 0, 0);
    CHECK(acct_record(&b, NULL, &t) == ACCT_E_RIBA && b.postings == 0, "blocked by default");
    acct_policy_t flag = {ACCT_RIBA_FLAG};
    CHECK(acct_translate_transaction(ACCT_STD_AAOIFI, &flag, &t, &j) == ACCT_OK &&
              (j.flags & ACCT_JF_RIBA) && j.line[1].cap == ACCT_C_CHARITY_PAYABLE,
          "flagged: to charity, never income");
    CHECK(acct_record(&b, &flag, &t) == ACCT_OK && b.riba_flags == 1 &&
              b.bal[ACCT_C_FINANCE_INCOME] == 0 && b.bal[ACCT_C_CHARITY_PAYABLE] == -1000,
          "book keeps the flag count");
    t = tx(ACCT_K_INTEREST_EXPENSE, 50, 0, 0);
    CHECK(acct_record(&b, NULL, &t) == ACCT_E_RIBA, "interest paid blocked");
    CHECK(acct_record(&b, &flag, &t) == ACCT_OK && b.riba_flags == 2, "or flagged");
    t = tx(ACCT_K_BORROW, 5000, 0, ACCT_TF_RIBA);
    CHECK(acct_record(&b, NULL, &t) == ACCT_E_RIBA, "an interest-bearing loan is blocked");
    t = tx(ACCT_K_BORROW, 5000, 0, 0);
    CHECK(acct_record(&b, NULL, &t) == ACCT_OK, "a loan not marked as interest-bearing posts");
    acct_bs_t bs;
    CHECK(acct_generate_balance_sheet(ACCT_STD_AAOIFI, &b, &bs) == ACCT_OK &&
              acct_bs_amount(&bs, ACCT_C_CHARITY_PAYABLE) == 1000,
          "charity payable on the statement");
    /* the same interest under IFRS and US GAAP is ordinary finance income */
    t = tx(ACCT_K_INTEREST_INCOME, 1000, 0, 0);
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, &t, &j) == ACCT_OK &&
              j.line[1].cap == ACCT_C_FINANCE_INCOME && j.flags == 0,
          "IFRS finance income");
    t = tx(ACCT_K_BORROW, 5000, 0, ACCT_TF_RIBA);
    CHECK(acct_translate_transaction(ACCT_STD_US_GAAP, NULL, &t, &j) == ACCT_OK, "US GAAP loan");
}

/* A3: the framework differences */
static void test_differences(void)
{
    acct_journal_t j;
    acct_txn_t t = tx(ACCT_K_DEVELOPMENT_COST, 700, 0, 0);
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, &t, &j) == ACCT_OK &&
              j.line[0].cap == ACCT_C_INTANGIBLE && strcmp(j.ref, "IAS 38.57") == 0,
          "IFRS capitalises development");
    CHECK(acct_translate_transaction(ACCT_STD_US_GAAP, NULL, &t, &j) == ACCT_OK &&
              j.line[0].cap == ACCT_C_DEVELOPMENT_EXPENSE,
          "US GAAP expenses it");
    CHECK(acct_translate_transaction(ACCT_STD_AAOIFI, NULL, &t, &j) == ACCT_OK &&
              j.line[0].cap == ACCT_C_INTANGIBLE,
          "AAOIFI follows IFRS (A1)");
    t = tx(ACCT_K_PPE_REVALUATION, 900, 0, 0);
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, &t, &j) == ACCT_OK &&
              j.line[1].cap == ACCT_C_REVALUATION_SURPLUS,
          "IFRS revaluation surplus");
    CHECK(acct_translate_transaction(ACCT_STD_US_GAAP, NULL, &t, &j) == ACCT_E_NOT_PERMITTED,
          "no upward revaluation under US GAAP");

    /* V-Bills */
    const uint16_t red = ACCT_TF_HOLDER_REDEEMABLE;
    t = tx(ACCT_K_VBILL_ISSUE, 1000, 0, red);
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, &t, &j) == ACCT_OK &&
              j.line[1].cap == ACCT_C_REDEEMABLE_UNITS,
          "IFRS: redeemable -> liability (IAS 32.18(b))");
    t.flags = red | ACCT_TF_PUTTABLE_EXCEPTION;
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, &t, &j) == ACCT_OK &&
              j.line[1].cap == ACCT_C_SHARE_CAPITAL,
          "IFRS: puttable exception -> equity");
    t.flags = red;
    CHECK(acct_translate_transaction(ACCT_STD_US_GAAP, NULL, &t, &j) == ACCT_OK &&
              j.line[1].cap == ACCT_C_TEMP_EQUITY,
          "US GAAP: temporary equity");
    CHECK(acct_translate_transaction(ACCT_STD_AAOIFI, NULL, &t, &j) == ACCT_OK &&
              j.line[1].cap == ACCT_C_IAH,
          "AAOIFI: equity of investment account holders");
    t.flags = 0;
    for (int s = ACCT_STD_IFRS; s <= ACCT_STD_AAOIFI; s++)
        CHECK(acct_translate_transaction((acct_std_t) s, NULL, &t, &j) == ACCT_OK &&
                  j.line[1].cap == ACCT_C_SHARE_CAPITAL,
              "not redeemable: equity in %d", s);

    /* the statements put each in its section */
    acct_book_t b;
    acct_bs_t bs;
    acct_book_init(&b, ACCT_STD_AAOIFI);
    t = tx(ACCT_K_CAPITAL, 5000, 0, 0);
    acct_record(&b, NULL, &t);
    t = tx(ACCT_K_VBILL_ISSUE, 3000, 0, red);
    acct_record(&b, NULL, &t);
    CHECK(acct_generate_balance_sheet(ACCT_STD_AAOIFI, &b, &bs) == ACCT_OK && bs.total_iah == 3000 &&
              bs.total_equity == 5000 && bs.total_assets == 8000 &&
              acct_bs_section_total(&bs, ACCT_S_IAH) == 3000,
          "AAOIFI: IAH between liabilities and owners' equity");
    acct_book_init(&b, ACCT_STD_US_GAAP);
    t = tx(ACCT_K_VBILL_ISSUE, 3000, 0, red);
    acct_record(&b, NULL, &t);
    CHECK(acct_generate_balance_sheet(ACCT_STD_US_GAAP, &b, &bs) == ACCT_OK &&
              bs.total_temp_equity == 3000 && bs.total_liabilities == 0,
          "US GAAP: temporary equity section");

    /* frameworks not built */
    t = tx(ACCT_K_SALE, 10, 0, 0);
    CHECK(acct_translate_transaction(ACCT_STD_OHADA, NULL, &t, &j) == ACCT_E_UNSUPPORTED && j.n == 0,
          "OHADA");
    CHECK(acct_translate_transaction(ACCT_STD_NONE, NULL, &t, &j) == ACCT_E_UNSUPPORTED, "none");
    CHECK(acct_translate_transaction(ACCT_STD_COUNT, NULL, &t, &j) == ACCT_E_ARG, "bad framework");
    t.amount = 0;
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, &t, &j) == ACCT_E_ARG, "zero amount");
    t = tx((acct_kind_t) 999, 10, 0, 0);
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, &t, &j) == ACCT_E_KIND, "unknown kind");
    t = tx(ACCT_K_OPEX, 70, 0, ACCT_TF_NONDEDUCTIBLE);
    acct_book_init(&b, ACCT_STD_IFRS);
    CHECK(acct_record(&b, NULL, &t) == ACCT_OK && b.nondeductible == 70,
          "non-deductible expense kept for the tax schedule");
}

/* A2: every translation balances; posting is all or nothing */
static uint64_t rng = 88172645463325252ull;
static uint64_t nx(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

static void test_invariants(void)
{
    for (int s = ACCT_STD_IFRS; s <= ACCT_STD_AAOIFI; s++) {
        acct_book_t b;
        acct_book_init(&b, (acct_std_t) s);
        acct_policy_t pol = {ACCT_RIBA_FLAG};
        unsigned ok = 0, bad = 0;
        for (int i = 0; i < 20000; i++) {
            acct_txn_t t = tx((acct_kind_t) (1 + nx() % (ACCT_K_COUNT - 1)),
                              (int64_t) (1 + nx() % 1000000), (int64_t) (nx() % 1000), 0);
            t.flags = (uint16_t) (nx() & 0x3Fu);
            t.cost_method = (uint8_t) (nx() % 3);
            acct_journal_t j;
            acct_status_t st = acct_translate_transaction((acct_std_t) s, &pol, &t, &j);
            if (st != ACCT_OK) {
                bad += j.n != 0;
                continue;
            }
            int64_t d = 0, c = 0;
            for (unsigned k = 0; k < j.n; k++) {
                d += j.line[k].dr;
                c += j.line[k].cr;
            }
            bad += d != c || j.ref == NULL || j.ref[0] == '\0';
            bad += acct_post(&b, &j) != ACCT_OK;
            ok++;
        }
        CHECK(bad == 0 && ok > 15000, "std %d: %u balanced entries, %u bad", s, ok, bad);
        CHECK(sum_bal(&b) == 0, "std %d: the trial balance sums to zero", s);
        acct_bs_t bs;
        CHECK(acct_generate_balance_sheet((acct_std_t) s, &b, &bs) == ACCT_OK,
              "std %d: every caption it posts has a line", s);
    }
    /* post is all or nothing */
    acct_book_t b;
    acct_book_init(&b, ACCT_STD_IFRS);
    acct_txn_t t = tx(ACCT_K_CAPITAL, INT64_MAX, 0, 0);
    CHECK(acct_record(&b, NULL, &t) == ACCT_OK, "a max posting");
    acct_book_t before = b;
    CHECK(acct_record(&b, NULL, &t) == ACCT_E_OVERFLOW && memcmp(&b, &before, sizeof b) == 0,
          "overflow changes nothing");
    acct_journal_t j;
    memset(&j, 0, sizeof j);
    j.n = 2;
    j.line[0].cap = ACCT_C_CASH;
    j.line[0].dr = 5;
    j.line[1].cap = ACCT_C_REVENUE;
    j.line[1].cr = 4;
    CHECK(acct_post(&b, &j) == ACCT_E_UNBALANCED, "unbalanced journal");
    j.line[1].cr = 5;
    j.line[1].cap = ACCT_C_COUNT;
    CHECK(acct_post(&b, &j) == ACCT_E_UNBALANCED, "unknown caption");
    j.line[1].cap = ACCT_C_REVENUE;
    j.line[1].dr = 1;
    CHECK(acct_post(&b, &j) == ACCT_E_UNBALANCED, "a line with both sides");
    t = tx(ACCT_K_MURABAHA_SALE, INT64_MAX, 1, 0);
    CHECK(acct_translate_transaction(ACCT_STD_AAOIFI, NULL, &t, &j) == ACCT_E_OVERFLOW,
          "murabaha gross overflow");
    int64_t r;
    CHECK(acct_mul_rate(-5, 1, 2, &r) && r == -3, "half away from zero, negative");
    CHECK(acct_mul_rate(5, 1, 2, &r) && r == 3, "half away from zero");
    CHECK(!acct_mul_rate(INT64_MAX, 2, 1, &r), "overflow refused");
    CHECK(!acct_mul_rate(1, 1, 0, &r), "den 0 refused");
    CHECK(acct_ratio(1, -3, 1000, &r) && r == -333, "negative denominator");
}

/* names, captions and section labels at and past their bounds */
static void test_labels(void)
{
    CHECK(strcmp(acct_std_name(ACCT_STD_COUNT), "?") == 0 &&
              strcmp(acct_std_name((acct_std_t) 99), "?") == 0,
          "an invalid framework has no name");
    CHECK(strcmp(acct_std_name(ACCT_STD_SAP_INSURER), "SAP_INSURER") == 0, "last name");
    CHECK(acct_cap_element(ACCT_C_COUNT) == ACCT_EL_ASSET && !acct_cap_current(ACCT_C_COUNT) &&
              strcmp(acct_cap_key(ACCT_C_COUNT), "?") == 0,
          "an invalid caption: defaults, never out of the table");
    CHECK(acct_cap_element(ACCT_C_ZAKAH_EXPENSE) == ACCT_EL_EXPENSE &&
              acct_cap_element(ACCT_C_TEMP_EQUITY) == ACCT_EL_TEMP_EQUITY &&
              acct_cap_element(ACCT_C_IAH) == ACCT_EL_IAH &&
              acct_cap_element(ACCT_C_PAYABLES) == ACCT_EL_LIABILITY,
          "caption elements");
    CHECK(acct_cap_current(ACCT_C_CASH) && acct_cap_current(ACCT_C_PROVISIONS) &&
              !acct_cap_current(ACCT_C_BORROWINGS) && !acct_cap_current(ACCT_C_DEFERRED_TAX_LIABILITY),
          "A6: current / non-current from the caption");
    CHECK(strcmp(acct_cap_key(ACCT_C_ZAKAH_EXPENSE), "zakah_expense") == 0 &&
              strcmp(acct_cap_key(ACCT_C_CONTRACT_LIABILITY), "contract_liability") == 0,
          "keys");
    /* each framework's own label */
    CHECK(strcmp(acct_cap_label(ACCT_STD_IFRS, ACCT_C_CASH), "Cash and cash equivalents") == 0, "IFRS cash");
    CHECK(strcmp(acct_cap_label(ACCT_STD_US_GAAP, ACCT_C_RECEIVABLES), "Accounts receivable, net") == 0,
          "US GAAP receivables");
    CHECK(strcmp(acct_cap_label(ACCT_STD_AAOIFI, ACCT_C_CASH), "Cash and balances with banks") == 0,
          "AAOIFI cash");
    CHECK(strcmp(acct_cap_label(ACCT_STD_AAOIFI, ACCT_C_ZAKAH_EXPENSE), "Zakah") == 0, "AAOIFI last caption");
    CHECK(strcmp(acct_cap_label(ACCT_STD_IFRS, ACCT_C_IAH), "") == 0 &&
              strcmp(acct_cap_label(ACCT_STD_US_GAAP, ACCT_C_TEMP_EQUITY),
                     "Redeemable units (temporary equity)") == 0,
          "a caption a framework never uses has no label");
    CHECK(strcmp(acct_cap_label(ACCT_STD_OHADA, ACCT_C_CASH), "") == 0 &&
              strcmp(acct_cap_label(ACCT_STD_NONE, ACCT_C_CASH), "") == 0 &&
              strcmp(acct_cap_label(ACCT_STD_COUNT, ACCT_C_CASH), "") == 0,
          "no labels for frameworks not built");
    CHECK(strcmp(acct_cap_label(ACCT_STD_IFRS, ACCT_C_COUNT), "") == 0 &&
              strcmp(acct_cap_label(ACCT_STD_AAOIFI, (acct_cap_t) 200), "") == 0,
          "no label for an invalid caption");
    /* section labels (A6) */
    CHECK(strcmp(acct_section_label(ACCT_STD_IFRS, ACCT_S_NONCURRENT_ASSETS), "Non-current assets") == 0 &&
              strcmp(acct_section_label(ACCT_STD_IFRS, ACCT_S_EQUITY), "Equity") == 0 &&
              strcmp(acct_section_label(ACCT_STD_IFRS, ACCT_S_CURRENT_LIABILITIES), "Current liabilities") == 0 &&
              strcmp(acct_section_label(ACCT_STD_IFRS, ACCT_S_TEMP_EQUITY), "") == 0 &&
              strcmp(acct_section_label(ACCT_STD_IFRS, ACCT_S_IAH), "") == 0,
          "IFRS sections");
    CHECK(strcmp(acct_section_label(ACCT_STD_US_GAAP, ACCT_S_EQUITY), "Stockholders' equity") == 0 &&
              strcmp(acct_section_label(ACCT_STD_US_GAAP, ACCT_S_TEMP_EQUITY), "Temporary equity") == 0 &&
              strcmp(acct_section_label(ACCT_STD_US_GAAP, ACCT_S_CURRENT_ASSETS), "Current assets") == 0,
          "US GAAP sections");
    CHECK(strcmp(acct_section_label(ACCT_STD_AAOIFI, ACCT_S_ASSETS), "Assets") == 0 &&
              strcmp(acct_section_label(ACCT_STD_AAOIFI, ACCT_S_LIABILITIES), "Liabilities") == 0 &&
              strcmp(acct_section_label(ACCT_STD_AAOIFI, ACCT_S_EQUITY), "Owners' equity") == 0 &&
              strcmp(acct_section_label(ACCT_STD_AAOIFI, ACCT_S_IAH),
                     "Equity of unrestricted investment account holders") == 0 &&
              strcmp(acct_section_label(ACCT_STD_AAOIFI, ACCT_S_CURRENT_ASSETS), "") == 0,
          "AAOIFI sections: no current / non-current split");
    CHECK(strcmp(acct_section_label(ACCT_STD_IFRS, ACCT_S_COUNT), "") == 0 &&
              strcmp(acct_section_label(ACCT_STD_AAOIFI, (acct_section_t) 50), "") == 0 &&
              strcmp(acct_section_label(ACCT_STD_CAS, ACCT_S_ASSETS), "") == 0,
          "no label for an invalid section or a framework not built");
}

/* translation: argument checks, zero amounts, flags, error output (A2) */
static void test_translate_edges(void)
{
    acct_journal_t j;
    acct_txn_t t = tx(ACCT_K_SALE, 10, 0, 0);
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, &t, NULL) == ACCT_E_ARG, "no output");
    memset(&j, 0xA5, sizeof j);
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, NULL, &j) == ACCT_E_ARG, "no event");
    {
        unsigned dirty = 0;
        for (unsigned i = 0; i < ACCT_MAX_LINES; i++)
            dirty += j.line[i].cap != 0 || j.line[i].dr != 0 || j.line[i].cr != 0;
        CHECK(dirty == 0 && j.n == 0 && j.flags == 0 && j.ref && j.ref[0] == '\0',
              "on error *out is zeroed (%u dirty lines)", dirty);
    }
    /* a successful entry leaves the unused lines zero */
    memset(&j, 0xA5, sizeof j);
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, &t, &j) == ACCT_OK && j.n == 2, "sale, no cost");
    {
        unsigned dirty = 0;
        for (unsigned i = 2; i < ACCT_MAX_LINES; i++)
            dirty += j.line[i].cap != 0 || j.line[i].dr != 0 || j.line[i].cr != 0;
        CHECK(dirty == 0, "unused lines zero (%u dirty)", dirty);
    }
    CHECK(j.line[0].cap == ACCT_C_CASH && j.line[0].dr == 10 && j.line[0].cr == 0 &&
              j.line[1].cap == ACCT_C_REVENUE && j.line[1].cr == 10 && j.line[1].dr == 0 &&
              strcmp(j.ref, "IFRS 15.31, IAS 2.34") == 0,
          "sale: Dr cash Cr revenue");
    /* the smallest amounts post */
    t = tx(ACCT_K_SALE, 1, 1, ACCT_TF_ON_CREDIT);
    CHECK(acct_translate_transaction(ACCT_STD_US_GAAP, NULL, &t, &j) == ACCT_OK && j.n == 4 &&
              j.line[0].cap == ACCT_C_RECEIVABLES && j.line[0].dr == 1 &&
              j.line[2].cap == ACCT_C_COST_OF_SALES && j.line[2].dr == 1 &&
              j.line[3].cap == ACCT_C_INVENTORY && j.line[3].cr == 1,
          "sale of 1 at cost 1 on credit");
    t = tx(ACCT_K_SALE, 1, -1, 0);
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, &t, &j) == ACCT_E_ARG, "negative amount2");
    t = tx(ACCT_K_SALE, -1, 0, 0);
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, &t, &j) == ACCT_E_ARG, "negative amount");
    /* AAOIFI murabaha without markup: no deferred profit line */
    t = tx(ACCT_K_MURABAHA_SALE, 500, 0, 0);
    CHECK(acct_translate_transaction(ACCT_STD_AAOIFI, NULL, &t, &j) == ACCT_OK && j.n == 2 &&
              j.line[0].cap == ACCT_C_FINANCING && j.line[0].dr == 500 &&
              j.line[1].cap == ACCT_C_INVENTORY && j.line[1].cr == 500,
          "murabaha at cost: two lines");
    /* a translation whose sides overflow cannot balance */
    t = tx(ACCT_K_SALE, INT64_MAX, 1, 0);
    memset(&j, 0xA5, sizeof j);
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, &t, &j) == ACCT_E_UNBALANCED && j.n == 0 &&
              j.line[0].dr == 0 && j.ref[0] == '\0',
          "A2: a translation that cannot balance is refused");
    /* cost method out of range */
    t = tx(ACCT_K_COST_OF_SALES, 10, 0, 0);
    t.cost_method = 3;
    CHECK(acct_translate_transaction(ACCT_STD_US_GAAP, NULL, &t, &j) == ACCT_E_ARG && j.n == 0,
          "unknown cost method");
    t.cost_method = ACCT_COST_WAVG;
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, &t, &j) == ACCT_OK &&
              j.line[0].cap == ACCT_C_COST_OF_SALES && j.line[1].cap == ACCT_C_INVENTORY,
          "weighted average cost of sales under IFRS");
    /* deferred tax: asset or liability */
    t = tx(ACCT_K_TAX_DEFERRED, 40, 0, ACCT_TF_DEFERRED_ASSET);
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, &t, &j) == ACCT_OK &&
              j.line[0].cap == ACCT_C_DEFERRED_TAX_ASSET && j.line[0].dr == 40 &&
              j.line[1].cap == ACCT_C_TAX_EXPENSE && j.line[1].cr == 40,
          "deferred tax asset up: Dr DTA Cr tax expense");
    t = tx(ACCT_K_TAX_DEFERRED, 40, 0, 0);
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, &t, &j) == ACCT_OK &&
              j.line[0].cap == ACCT_C_TAX_EXPENSE && j.line[0].dr == 40 &&
              j.line[1].cap == ACCT_C_DEFERRED_TAX_LIABILITY && j.line[1].cr == 40,
          "deferred tax liability up: Dr tax expense Cr DTL");
    /* AAOIFI references come from the AAOIFI column */
    t = tx(ACCT_K_ZAKAH, 25, 0, 0);
    CHECK(acct_translate_transaction(ACCT_STD_AAOIFI, NULL, &t, &j) == ACCT_OK &&
              strcmp(j.ref, "AAOIFI FAS 9") == 0,
          "zakah under AAOIFI: FAS 9");
    t = tx(ACCT_K_OPEX, 25, 0, 0);
    CHECK(acct_translate_transaction(ACCT_STD_US_GAAP, NULL, &t, &j) == ACCT_OK &&
              strcmp(j.ref, "ASC 220") == 0,
          "opex under US GAAP");
}

/* the book: argument checks, all-or-nothing, line limits (A2) */
static void test_post_edges(void)
{
    acct_book_t b;
    acct_journal_t j;
    acct_book_init(NULL, ACCT_STD_IFRS); /* harmless */
    acct_book_init(&b, ACCT_STD_IFRS);
    acct_txn_t t = tx(ACCT_K_CAPITAL, 100, 0, 0);
    CHECK(acct_record(NULL, NULL, &t) == ACCT_E_ARG, "record into no book");
    CHECK(acct_record(&b, NULL, NULL) == ACCT_E_ARG && b.postings == 0, "record no event");
    CHECK(acct_translate_transaction(ACCT_STD_IFRS, NULL, &t, &j) == ACCT_OK, "capital");
    CHECK(acct_post(NULL, &j) == ACCT_E_ARG, "post into no book");
    CHECK(acct_post(&b, NULL) == ACCT_E_ARG && b.postings == 0, "post no journal");
    CHECK(acct_post(&b, &j) == ACCT_OK && b.postings == 1 && b.riba_flags == 0 &&
              b.bal[ACCT_C_CASH] == 100 && b.bal[ACCT_C_SHARE_CAPITAL] == -100,
          "a posting that is not riba is not counted as one");

    /* lines past n are ignored */
    memset(&j, 0, sizeof j);
    j.n = 2;
    j.line[0].cap = ACCT_C_CASH;
    j.line[0].dr = 7;
    j.line[1].cap = ACCT_C_SHARE_CAPITAL;
    j.line[1].cr = 7;
    j.line[2].cap = ACCT_C_CASH;
    j.line[2].dr = 1000;
    CHECK(acct_post(&b, &j) == ACCT_OK && b.bal[ACCT_C_CASH] == 107 && sum_bal(&b) == 0,
          "only the first n lines post");
    /* a journal of no lines, or more than ACCT_MAX_LINES */
    acct_book_t before = b;
    j.n = 0;
    CHECK(acct_post(&b, &j) == ACCT_E_UNBALANCED && memcmp(&b, &before, sizeof b) == 0, "empty journal");
    j.n = ACCT_MAX_LINES + 1;
    CHECK(acct_post(&b, &j) == ACCT_E_UNBALANCED && memcmp(&b, &before, sizeof b) == 0,
          "more lines than a journal holds");
    /* exactly ACCT_MAX_LINES lines */
    memset(&j, 0, sizeof j);
    j.n = ACCT_MAX_LINES;
    for (unsigned i = 0; i < 3; i++) {
        j.line[i].cap = ACCT_C_CASH;
        j.line[i].dr = 1;
        j.line[3 + i].cap = ACCT_C_SHARE_CAPITAL;
        j.line[3 + i].cr = 1;
    }
    CHECK(acct_post(&b, &j) == ACCT_OK && b.bal[ACCT_C_CASH] == 110 && b.postings == 3,
          "a journal of ACCT_MAX_LINES lines posts");
    before = b;
    /* a line with no amount, or both sides, in a journal that adds up */
    memset(&j, 0, sizeof j);
    j.n = 3;
    j.line[0].cap = ACCT_C_CASH;
    j.line[0].dr = 5;
    j.line[1].cap = ACCT_C_SHARE_CAPITAL;
    j.line[1].cr = 5;
    j.line[2].cap = ACCT_C_REVENUE; /* dr 0, cr 0 */
    CHECK(acct_post(&b, &j) == ACCT_E_UNBALANCED && memcmp(&b, &before, sizeof b) == 0,
          "a line with no amount");
    j.n = 2;
    j.line[1].cr = 6;
    j.line[1].dr = 1;
    CHECK(acct_post(&b, &j) == ACCT_E_UNBALANCED && memcmp(&b, &before, sizeof b) == 0,
          "a line with both sides, even when the totals agree");
    j.line[1].dr = 0;
    j.line[1].cr = 5;
    j.line[0].dr = -5;
    j.line[0].cr = 0;
    CHECK(acct_post(&b, &j) == ACCT_E_UNBALANCED, "negative amount");
    /* debits that overflow */
    memset(&j, 0, sizeof j);
    j.n = 4;
    j.line[0].cap = ACCT_C_CASH;
    j.line[0].dr = INT64_MAX;
    j.line[1].cap = ACCT_C_INVENTORY;
    j.line[1].dr = 1;
    j.line[2].cap = ACCT_C_SHARE_CAPITAL;
    j.line[2].cr = INT64_MAX;
    j.line[3].cap = ACCT_C_SHARE_CAPITAL;
    j.line[3].cr = 1;
    CHECK(acct_post(&b, &j) == ACCT_E_UNBALANCED && memcmp(&b, &before, sizeof b) == 0,
          "a journal whose debits overflow does not balance");

    /* a full book refuses, and changes nothing */
    b.postings = UINT32_MAX;
    before = b;
    t = tx(ACCT_K_CAPITAL, 1, 0, 0);
    CHECK(acct_record(&b, NULL, &t) == ACCT_E_FULL && memcmp(&b, &before, sizeof b) == 0, "book full");
    b.postings = UINT32_MAX - 1;
    CHECK(acct_record(&b, NULL, &t) == ACCT_OK && b.postings == UINT32_MAX, "the last posting fits");

    /* non-deductible: a 1-unit expense counts, the credit side never does */
    acct_book_init(&b, ACCT_STD_IFRS);
    t = tx(ACCT_K_OPEX, 1, 0, ACCT_TF_NONDEDUCTIBLE);
    CHECK(acct_record(&b, NULL, &t) == ACCT_OK && b.nondeductible == 1, "non-deductible 1");
    t = tx(ACCT_K_PURCHASE_INVENTORY, 9, 0, ACCT_TF_NONDEDUCTIBLE);
    CHECK(acct_record(&b, NULL, &t) == ACCT_OK && b.nondeductible == 1, "not an expense: not counted");
    t = tx(ACCT_K_TAX_DEFERRED, 4, 0, ACCT_TF_NONDEDUCTIBLE | ACCT_TF_DEFERRED_ASSET);
    CHECK(acct_record(&b, NULL, &t) == ACCT_OK && b.nondeductible == 1,
          "an expense credit is not a non-deductible debit");
}

/* balance sheet: arguments, layouts, the statement helpers, JSON (A6) */
static void test_bs_edges(void)
{
    acct_book_t b;
    acct_bs_t bs;
    acct_book_init(&b, ACCT_STD_IFRS);
    CHECK(acct_generate_balance_sheet(ACCT_STD_IFRS, &b, NULL) == ACCT_E_ARG, "no output");
    memset(&bs, 0x5A, sizeof bs);
    CHECK(acct_generate_balance_sheet(ACCT_STD_IFRS, NULL, &bs) == ACCT_E_ARG, "no book");
    {
        unsigned dirty = 0;
        for (unsigned i = 0; i < ACCT_BS_MAX; i++)
            dirty += bs.line[i].cap != 0 || bs.line[i].section != 0 || bs.line[i].amount != 0;
        CHECK(dirty == 0 && bs.n == 0 && bs.std == ACCT_STD_IFRS && bs.total_assets == 0 &&
                  bs.total_liabilities == 0 && bs.total_temp_equity == 0 && bs.total_iah == 0 &&
                  bs.total_equity == 0 && bs.profit == 0,
              "on error the statement is empty (%u dirty lines)", dirty);
    }
    acct_book_init(&b, ACCT_STD_COUNT);
    CHECK(acct_generate_balance_sheet(ACCT_STD_COUNT, &b, &bs) == ACCT_E_ARG, "invalid framework");
    /* an empty book is an empty, balanced statement */
    acct_book_init(&b, ACCT_STD_US_GAAP);
    CHECK(acct_generate_balance_sheet(ACCT_STD_US_GAAP, &b, &bs) == ACCT_OK && bs.n == 0 &&
              bs.total_assets == 0,
          "empty book");

    /* a small IFRS book with a loss */
    acct_book_init(&b, ACCT_STD_IFRS);
    acct_txn_t t = tx(ACCT_K_CAPITAL, 1000, 0, 0);
    CHECK(acct_record(&b, NULL, &t) == ACCT_OK, "capital 1,000");
    t = tx(ACCT_K_OPEX, 1500, 0, ACCT_TF_ON_CREDIT);
    CHECK(acct_record(&b, NULL, &t) == ACCT_OK && b.bal[ACCT_C_PAYABLES] == -1500, "expense on credit");
    memset(&bs, 0x5A, sizeof bs);
    CHECK(acct_generate_balance_sheet(ACCT_STD_IFRS, &b, &bs) == ACCT_OK && bs.n == 4 &&
              bs.profit == -1500 && bs.total_equity == -500 && bs.total_assets == 1000 &&
              bs.total_liabilities == 1500 && bs.total_temp_equity == 0 && bs.total_iah == 0,
          "a loss: negative equity");
    {
        unsigned dirty = 0;
        for (unsigned i = bs.n; i < ACCT_BS_MAX; i++)
            dirty += bs.line[i].cap != 0 || bs.line[i].section != 0 || bs.line[i].amount != 0;
        CHECK(dirty == 0, "lines past n are zero (%u dirty)", dirty);
    }
    CHECK(bs.line[0].cap == ACCT_C_CASH && bs.line[0].section == ACCT_S_CURRENT_ASSETS &&
              bs.line[2].cap == ACCT_C_RETAINED_EARNINGS && bs.line[2].amount == -1500 &&
              bs.line[3].cap == ACCT_C_PAYABLES && bs.line[3].section == ACCT_S_CURRENT_LIABILITIES,
          "IFRS order");
    CHECK(acct_bs_amount(&bs, ACCT_C_CASH) == 1000 && acct_bs_amount(&bs, ACCT_C_PAYABLES) == 1500 &&
              acct_bs_amount(&bs, ACCT_C_GOODWILL) == 0 && acct_bs_amount(NULL, ACCT_C_CASH) == 0,
          "caption amounts: first line, last line, absent, no statement");
    CHECK(acct_bs_section_total(&bs, ACCT_S_EQUITY) == -500 &&
              acct_bs_section_total(&bs, ACCT_S_CURRENT_ASSETS) == 1000 &&
              acct_bs_section_total(&bs, ACCT_S_NONCURRENT_ASSETS) == 0 &&
              acct_bs_section_total(NULL, ACCT_S_EQUITY) == 0,
          "section totals");
    static const char want[] =
        "{\"standard\":\"IFRS\",\"lines\":["
        "{\"section\":\"current_assets\",\"key\":\"cash\",\"label\":\"Cash and cash equivalents\","
        "\"amount\":1000},"
        "{\"section\":\"equity\",\"key\":\"share_capital\",\"label\":\"Share capital\",\"amount\":1000},"
        "{\"section\":\"equity\",\"key\":\"retained_earnings\",\"label\":\"Retained earnings\","
        "\"amount\":-1500},"
        "{\"section\":\"current_liabilities\",\"key\":\"payables\",\"label\":\"Trade and other payables\","
        "\"amount\":1500}],"
        "\"total_assets\":1000,\"total_liabilities\":1500,\"total_temporary_equity\":0,\"total_iah\":0,"
        "\"total_equity\":-500,\"profit\":-1500}";
    const int L = (int) sizeof want - 1;
    char js[1024];
    int n = acct_bs_json(&bs, js, sizeof js);
    CHECK(n == L && strcmp(js, want) == 0, "JSON byte for byte: %s", js);
    /* the buffer must hold the text and its NUL, exactly */
    char *exact = js + 1; /* any buffer works; reuse with an offset */
    memset(js, 'x', sizeof js);
    n = acct_bs_json(&bs, exact, (size_t) L + 1);
    CHECK(n == L && strcmp(exact, want) == 0, "a buffer of length + 1 is enough");
    memset(js, 'x', sizeof js);
    n = acct_bs_json(&bs, exact, (size_t) L);
    CHECK(n == -1 && exact[0] == '\0' && exact[L] == 'x', "a buffer of length is too small");
    js[0] = 'x';
    CHECK(acct_bs_json(&bs, js, 1) == -1 && js[0] == '\0', "cap 1: empty string");
    js[0] = 'x';
    CHECK(acct_bs_json(&bs, js, 0) == -1 && js[0] == 'x', "cap 0: nothing written");
    CHECK(acct_bs_json(NULL, js, sizeof js) == -1 && acct_bs_json(&bs, NULL, 8) == -1, "NULLs");

    /* the same book's layouts under US GAAP and AAOIFI */
    acct_book_t g = b;
    g.std = ACCT_STD_US_GAAP;
    CHECK(acct_generate_balance_sheet(ACCT_STD_US_GAAP, &g, &bs) == ACCT_OK && bs.n == 4 &&
              bs.line[0].cap == ACCT_C_CASH && bs.line[1].cap == ACCT_C_PAYABLES &&
              bs.line[2].cap == ACCT_C_SHARE_CAPITAL && bs.line[3].cap == ACCT_C_RETAINED_EARNINGS,
          "US GAAP: assets, liabilities, equity");
    n = acct_bs_json(&bs, js, sizeof js);
    CHECK(n > 0 && strstr(js, "{\"standard\":\"US_GAAP\",\"lines\":[{\"section\":\"current_assets\"") &&
              strstr(js, "\"label\":\"Accounts payable\",\"amount\":1500}"),
          "US GAAP JSON: %s", js);
    g.std = ACCT_STD_AAOIFI;
    CHECK(acct_generate_balance_sheet(ACCT_STD_AAOIFI, &g, &bs) == ACCT_OK && bs.n == 4 &&
              bs.line[0].section == ACCT_S_ASSETS && bs.line[1].section == ACCT_S_LIABILITIES,
          "AAOIFI: by liquidity, no split");
    n = acct_bs_json(&bs, js, sizeof js);
    CHECK(n > 0 &&
              strstr(js, "{\"section\":\"assets\",\"key\":\"cash\",\"label\":\"Cash and balances with banks\"") &&
              strstr(js, "{\"section\":\"liabilities\",\"key\":\"payables\",\"label\":\"Payables\",\"amount\":1500}"),
          "AAOIFI JSON: %s", js);

    /* a hand-made statement: the helpers read only its first n lines */
    acct_bs_t h;
    memset(&h, 0, sizeof h);
    h.std = ACCT_STD_IFRS;
    h.n = 1;
    h.line[0].cap = ACCT_C_CASH;
    h.line[0].section = ACCT_S_CURRENT_ASSETS;
    h.line[0].amount = 5;
    h.line[1].cap = ACCT_C_GOODWILL;
    h.line[1].section = ACCT_S_CURRENT_ASSETS;
    h.line[1].amount = 7;
    CHECK(acct_bs_section_total(&h, ACCT_S_CURRENT_ASSETS) == 5 && acct_bs_amount(&h, ACCT_C_GOODWILL) == 0,
          "only the first n lines count");
    n = acct_bs_json(&h, js, sizeof js);
    CHECK(n > 0 && strcmp(js, "{\"standard\":\"IFRS\",\"lines\":[{\"section\":\"current_assets\",\"key\":"
                              "\"cash\",\"label\":\"Cash and cash equivalents\",\"amount\":5}],"
                              "\"total_assets\":0,\"total_liabilities\":0,\"total_temporary_equity\":0,"
                              "\"total_iah\":0,\"total_equity\":0,\"profit\":0}") == 0,
          "one-line JSON: %s", js);
    /* n beyond ACCT_BS_MAX is capped at the array */
    h.n = 200;
    for (unsigned i = 0; i < ACCT_BS_MAX; i++) {
        h.line[i].cap = ACCT_C_CASH;
        h.line[i].section = ACCT_S_CURRENT_ASSETS;
        h.line[i].amount = 1;
    }
    CHECK(acct_bs_section_total(&h, ACCT_S_CURRENT_ASSETS) == (int64_t) ACCT_BS_MAX &&
              acct_bs_amount(&h, ACCT_C_GOODWILL) == 0,
          "n is capped at ACCT_BS_MAX");
    static char big[8192];
    n = acct_bs_json(&h, big, sizeof big);
    {
        unsigned lines = 0;
        for (const char *p = big; (p = strstr(p, "{\"section\"")) != NULL; p++) lines++;
        CHECK(n > 0 && lines == ACCT_BS_MAX, "JSON caps the lines too: %u", lines);
    }
    /* an invalid section or caption is written as "?" */
    memset(&h, 0, sizeof h);
    h.std = ACCT_STD_COUNT;
    h.n = 1;
    h.line[0].cap = ACCT_C_COUNT;
    h.line[0].section = ACCT_S_COUNT;
    h.line[0].amount = INT64_MIN;
    h.total_assets = INT64_MAX;
    n = acct_bs_json(&h, js, sizeof js);
    CHECK(n > 0 && strcmp(js, "{\"standard\":\"?\",\"lines\":[{\"section\":\"?\",\"key\":\"?\",\"label\":\"\","
                              "\"amount\":-9223372036854775808}],\"total_assets\":9223372036854775807,"
                              "\"total_liabilities\":0,\"total_temporary_equity\":0,\"total_iah\":0,"
                              "\"total_equity\":0,\"profit\":0}") == 0,
          "invalid entries and the int64 extremes: %s", js);
}

/* exact arithmetic at its limits */
static void test_arith_edges(void)
{
    int64_t r;
    CHECK(acct_mul_rate(INT64_MAX, (uint64_t) INT64_MAX, (uint64_t) INT64_MAX, &r) && r == INT64_MAX,
          "rate num and den at INT64_MAX");
    CHECK(!acct_mul_rate(1, (uint64_t) INT64_MAX + 1u, 1, &r), "num above INT64_MAX");
    CHECK(!acct_mul_rate(1, 1, (uint64_t) INT64_MAX + 1u, &r), "den above INT64_MAX");
    CHECK(acct_mul_rate(INT64_MAX, 1, 1, &r) && r == INT64_MAX, "INT64_MAX fits");
    CHECK(acct_mul_rate(INT64_MIN, 1, 1, &r) && r == INT64_MIN, "INT64_MIN fits");
    CHECK(!acct_mul_rate(INT64_MIN, 2, 1, &r), "below INT64_MIN refused");
    CHECK(!acct_mul_rate(1, 1, 1, NULL), "no output");
    CHECK(acct_mul_rate(7, 1, 4, &r) && r == 2 && acct_mul_rate(-7, 1, 4, &r) && r == -2,
          "1.75 -> 2, -1.75 -> -2");
    CHECK(acct_mul_rate(5, 1, 4, &r) && r == 1 && acct_mul_rate(-5, 1, 4, &r) && r == -1,
          "1.25 -> 1, -1.25 -> -1");
    CHECK(acct_mul_rate(0, 3, 7, &r) && r == 0, "zero");

    CHECK(acct_ratio(5, 1, 1, &r) && r == 5, "den 1");
    CHECK(!acct_ratio(5, 0, 1, &r) && !acct_ratio(5, 1, 1, NULL), "den 0, no output");
    CHECK(acct_ratio(1, 2, 1, &r) && r == 1 && acct_ratio(-1, 2, 1, &r) && r == -1 &&
              acct_ratio(1, -2, 1, &r) && r == -1 && acct_ratio(-1, -2, 1, &r) && r == 1,
          "halves away from zero, any signs");
    CHECK(acct_ratio(1, 3, 1, &r) && r == 0 && acct_ratio(2, 3, 1, &r) && r == 1, "thirds");
    CHECK(acct_ratio(INT64_MAX, 1, 1, &r) && r == INT64_MAX, "INT64_MAX fits");
    CHECK(acct_ratio(INT64_MIN, 1, 1, &r) && r == INT64_MIN, "INT64_MIN fits");
    CHECK(!acct_ratio(INT64_MAX, 1, 2, &r), "above INT64_MAX refused");
    CHECK(!acct_ratio(INT64_MIN, -1, 1, &r), "-INT64_MIN refused");
    CHECK(!acct_ratio(INT64_MIN, 1, 2, &r), "below INT64_MIN refused");
}

/* revenue by pattern, breakage and allocation at their edges (A5) */
static void test_pattern_edges(void)
{
    int64_t now = 77;
    CHECK(acct_pattern_revenue(1, 1, 1, 0, NULL) == ACCT_E_ARG, "no output");
    CHECK(acct_pattern_revenue(0, 0, 0, 0, &now) == ACCT_OK && now == 0, "all zero");
    CHECK(acct_pattern_revenue(100, 0, 0, 0, &now) == ACCT_OK && now == 100,
          "nothing expected, nothing used: all of it");
    CHECK(acct_pattern_revenue(100, 0, 10, 0, &now) == ACCT_OK && now == 0, "nothing used yet");
    CHECK(acct_pattern_revenue(1, 1, 2, 0, &now) == ACCT_OK && now == 1, "0.5 rounds up");
    CHECK(acct_pattern_revenue(1, 1, 3, 0, &now) == ACCT_OK && now == 0, "0.33 rounds down");
    CHECK(acct_pattern_revenue(100, 1, 2, 100, &now) == ACCT_OK && now == -50,
          "recognised = amount: a reversal");
    CHECK(acct_pattern_revenue(100, 5, 2, 0, &now) == ACCT_OK && now == 100, "capped at amount");
    now = 77;
    CHECK(acct_pattern_revenue(-1, 0, 1, 0, &now) == ACCT_E_ARG && now == 0, "negative amount, *now zeroed");
    now = 77;
    CHECK(acct_pattern_revenue(1, -1, 1, 0, &now) == ACCT_E_ARG && now == 0, "negative exercised");
    CHECK(acct_pattern_revenue(1, 0, -1, 0, &now) == ACCT_E_ARG, "negative expected");
    CHECK(acct_pattern_revenue(1, 0, 1, -1, &now) == ACCT_E_ARG, "negative recognised");

    CHECK(acct_breakage(1, 1, 0, 0, NULL) == ACCT_E_ARG, "breakage: no output");
    CHECK(acct_breakage(1, 0, 0, 0, &now) == ACCT_OK && now == 0, "no breakage expected");
    CHECK(acct_breakage(10, 10, 0, 0, &now) == ACCT_OK && now == 10,
          "all breakage: nothing is expected to be redeemed");
    now = 77;
    CHECK(acct_breakage(0, 0, 0, 0, &now) == ACCT_E_ARG && now == 0, "nothing issued, *now zeroed");
    CHECK(acct_breakage(-5, 0, 0, 0, &now) == ACCT_E_ARG, "negative issued");
    CHECK(acct_breakage(10, -1, 0, 0, &now) == ACCT_E_ARG, "negative breakage");
    CHECK(acct_breakage(10, 11, 0, 0, &now) == ACCT_E_ARG, "breakage above issued");
    CHECK(acct_breakage(10, 2, 4, 0, &now) == ACCT_OK && now == 1, "2 x 4 / 8 = 1");
    CHECK(acct_breakage(10, 2, 2, 0, &now) == ACCT_OK && now == 1, "2 x 2 / 8 = 0.5 rounds up");

    int64_t ssp[17], o[17];
    for (unsigned i = 0; i < 17; i++) ssp[i] = 1;
    CHECK(acct_allocate(1, ssp, 16, o) == ACCT_OK && o[0] == 1 && o[1] == 0 && o[15] == 0,
          "sixteen ties: the earliest gets the unit");
    CHECK(acct_allocate(31, ssp, 16, o) == ACCT_OK && o[0] == 2 && o[14] == 2 && o[15] == 1,
          "31 over sixteen: the first fifteen get the extra unit");
    CHECK(acct_allocate(1, ssp, 17, o) == ACCT_E_ARG, "more than sixteen");
    CHECK(acct_allocate(1, ssp, 0, o) == ACCT_E_ARG, "none");
    CHECK(acct_allocate(1, NULL, 1, o) == ACCT_E_ARG && acct_allocate(1, ssp, 1, NULL) == ACCT_E_ARG, "NULLs");
    CHECK(acct_allocate(-1, ssp, 1, o) == ACCT_E_ARG, "negative price");
    CHECK(acct_allocate(9, ssp, 1, o) == ACCT_OK && o[0] == 9, "one obligation takes it all");
    CHECK(acct_allocate(0, ssp, 2, o) == ACCT_OK && o[0] == 0 && o[1] == 0, "price 0");
    int64_t s3[3] = {1, 1, 2};
    CHECK(acct_allocate(1, s3, 3, o) == ACCT_OK && o[0] == 0 && o[1] == 0 && o[2] == 1,
          "the largest remainder may be last");
    int64_t s4[4] = {1, 2, 1, 3};
    CHECK(acct_allocate(10, s4, 4, o) == ACCT_OK && o[0] == 2 && o[1] == 3 && o[2] == 1 && o[3] == 4,
          "10 over 1:2:1:3 = 1.43, 2.86, 1.43, 4.29 -> 2, 3, 1, 4 (tie 0.43 to the earlier)");
    int64_t sz[3] = {0, 3, 1};
    CHECK(acct_allocate(4, sz, 3, o) == ACCT_OK && o[0] == 0 && o[1] == 3 && o[2] == 1,
          "a zero selling price gets nothing");
    int64_t sn[2] = {5, -1};
    CHECK(acct_allocate(4, sn, 2, o) == ACCT_E_ARG, "negative selling price");
}

/* inventory cost formulas at their edges (A3) */
static void test_cost_flow_edges(void)
{
    const acct_layer_t l[2] = {{1, 10}, {5, 20}};
    int64_t cogs = 1, end = 1;
    CHECK(acct_cost_flow(ACCT_STD_IFRS, l, 2, 1, ACCT_COST_FIFO, NULL, &end) == ACCT_E_ARG &&
              acct_cost_flow(ACCT_STD_IFRS, l, 2, 1, ACCT_COST_FIFO, &cogs, NULL) == ACCT_E_ARG,
          "no outputs");
    cogs = end = 1;
    CHECK(acct_cost_flow(ACCT_STD_IFRS, NULL, 2, 1, ACCT_COST_FIFO, &cogs, &end) == ACCT_E_ARG &&
              cogs == 0 && end == 0,
          "no layers: outputs zeroed");
    CHECK(acct_cost_flow(ACCT_STD_IFRS, l, 0, 0, ACCT_COST_FIFO, &cogs, &end) == ACCT_E_ARG, "n 0");
    CHECK(acct_cost_flow(ACCT_STD_IFRS, l, 2, -1, ACCT_COST_FIFO, &cogs, &end) == ACCT_E_ARG, "sold < 0");
    CHECK(acct_cost_flow(ACCT_STD_US_GAAP, l, 2, 1, 3, &cogs, &end) == ACCT_E_ARG, "unknown method");
    CHECK(acct_cost_flow(ACCT_STD_OHADA, l, 2, 1, ACCT_COST_FIFO, &cogs, &end) == ACCT_E_UNSUPPORTED,
          "OHADA not built");
    CHECK(acct_cost_flow(ACCT_STD_NONE, l, 2, 1, ACCT_COST_WAVG, &cogs, &end) == ACCT_E_UNSUPPORTED,
          "no framework");
    CHECK(acct_cost_flow(ACCT_STD_IFRS, l, 2, 1, ACCT_COST_FIFO, &cogs, &end) == ACCT_OK && cogs == 10 &&
              end == 100,
          "FIFO one unit: the oldest");
    CHECK(acct_cost_flow(ACCT_STD_US_GAAP, l, 2, 1, ACCT_COST_LIFO, &cogs, &end) == ACCT_OK &&
              cogs == 20 && end == 90,
          "LIFO one unit: the newest");
    CHECK(acct_cost_flow(ACCT_STD_US_GAAP, l, 2, 6, ACCT_COST_LIFO, &cogs, &end) == ACCT_OK &&
              cogs == 110 && end == 0,
          "LIFO everything");
    CHECK(acct_cost_flow(ACCT_STD_IFRS, l, 2, 6, ACCT_COST_FIFO, &cogs, &end) == ACCT_OK && cogs == 110 &&
              end == 0,
          "FIFO everything");
    CHECK(acct_cost_flow(ACCT_STD_IFRS, l, 2, 0, ACCT_COST_FIFO, &cogs, &end) == ACCT_OK && cogs == 0 &&
              end == 110,
          "nothing sold");
    CHECK(acct_cost_flow(ACCT_STD_IFRS, l, 2, 7, ACCT_COST_WAVG, &cogs, &end) == ACCT_E_ARG,
          "more than held");
    const acct_layer_t one[1] = {{1, 7}};
    CHECK(acct_cost_flow(ACCT_STD_AAOIFI, one, 1, 1, ACCT_COST_WAVG, &cogs, &end) == ACCT_OK && cogs == 7 &&
              end == 0,
          "one layer, one unit, weighted average");
    const acct_layer_t empty[2] = {{0, 5}, {0, 9}};
    CHECK(acct_cost_flow(ACCT_STD_IFRS, empty, 2, 0, ACCT_COST_WAVG, &cogs, &end) == ACCT_OK && cogs == 0 &&
              end == 0,
          "no units held: weighted average of nothing");
    const acct_layer_t w[2] = {{1, 1}, {1, 2}};
    CHECK(acct_cost_flow(ACCT_STD_IFRS, w, 2, 1, ACCT_COST_WAVG, &cogs, &end) == ACCT_OK && cogs == 2 &&
              end == 1,
          "weighted average 1.5 rounds up");
    const acct_layer_t w3[2] = {{2, 1}, {1, 2}};
    CHECK(acct_cost_flow(ACCT_STD_IFRS, w3, 2, 1, ACCT_COST_WAVG, &cogs, &end) == ACCT_OK && cogs == 1 &&
              end == 3,
          "weighted average 1.33 rounds down");
    const acct_layer_t zc[2] = {{3, 0}, {2, 4}};
    CHECK(acct_cost_flow(ACCT_STD_IFRS, zc, 2, 4, ACCT_COST_FIFO, &cogs, &end) == ACCT_OK && cogs == 4 &&
              end == 4,
          "a free layer");
    const acct_layer_t nu[2] = {{-1, 5}, {2, 4}};
    CHECK(acct_cost_flow(ACCT_STD_IFRS, nu, 2, 0, ACCT_COST_FIFO, &cogs, &end) == ACCT_E_ARG,
          "negative units");
    const acct_layer_t nc[2] = {{1, 5}, {2, -4}};
    CHECK(acct_cost_flow(ACCT_STD_IFRS, nc, 2, 0, ACCT_COST_FIFO, &cogs, &end) == ACCT_E_ARG,
          "negative unit cost");
    const acct_layer_t ov[2] = {{INT64_MAX / 2, 3}, {1, 1}};
    CHECK(acct_cost_flow(ACCT_STD_IFRS, ov, 2, 0, ACCT_COST_FIFO, &cogs, &end) == ACCT_E_OVERFLOW,
          "layer cost overflow");
    const acct_layer_t mid[3] = {{2, 1}, {0, 50}, {3, 2}};
    CHECK(acct_cost_flow(ACCT_STD_US_GAAP, mid, 3, 4, ACCT_COST_LIFO, &cogs, &end) == ACCT_OK &&
              cogs == 7 && end == 1,
          "LIFO through an empty layer");
}

/* murabaha at its edges (FAS 28) */
static void test_murabaha_edges(void)
{
    int64_t out = 9;
    CHECK(acct_murabaha_profit(100, 10, 0, 10, NULL) == ACCT_E_ARG, "no output");
    CHECK(acct_murabaha_profit(100, 1, 0, 1, &out) == ACCT_OK && out == 100, "a one-day contract");
    CHECK(acct_murabaha_profit(0, 10, 0, 10, &out) == ACCT_OK && out == 0, "no markup");
    CHECK(acct_murabaha_profit(100, 10, 5, 5, &out) == ACCT_OK && out == 0, "an empty period");
    CHECK(acct_murabaha_profit(100, 3, 0, 1, &out) == ACCT_OK && out == 33 &&
              acct_murabaha_profit(100, 3, 1, 2, &out) == ACCT_OK && out == 33 &&
              acct_murabaha_profit(100, 3, 2, 3, &out) == ACCT_OK && out == 34,
          "floor differences: 33, 33, 34");
    out = 9;
    CHECK(acct_murabaha_profit(-1, 10, 0, 10, &out) == ACCT_E_ARG && out == 0, "negative markup, zeroed");
    CHECK(acct_murabaha_profit(100, 0, 0, 0, &out) == ACCT_E_ARG, "no days");
    CHECK(acct_murabaha_profit(100, 10, 0, 11, &out) == ACCT_E_ARG, "past the end");

    int64_t prof[3] = {-9, -9, -9};
    const int64_t one[1] = {1};
    CHECK(acct_murabaha_schedule(1, one, 1, 0, 1, prof) == ACCT_OK && prof[0] == 0,
          "one instalment of the cost: no profit");
    const int64_t p1[1] = {130};
    CHECK(acct_murabaha_schedule(100, p1, 1, 0, 1, prof) == ACCT_OK && prof[0] == 30,
          "one instalment: the profit is what clears the balance");
    CHECK(acct_murabaha_schedule(100, NULL, 1, 0, 1, prof) == ACCT_E_ARG &&
              acct_murabaha_schedule(100, p1, 1, 0, 1, NULL) == ACCT_E_ARG &&
              acct_murabaha_schedule(100, p1, 0, 0, 1, prof) == ACCT_E_ARG &&
              acct_murabaha_schedule(0, p1, 1, 0, 1, prof) == ACCT_E_ARG &&
              acct_murabaha_schedule(-1, p1, 1, 0, 1, prof) == ACCT_E_ARG &&
              acct_murabaha_schedule(100, p1, 1, 1, 0, prof) == ACCT_E_ARG,
          "schedule argument checks");
    const int64_t io[2] = {10, 110};
    CHECK(acct_murabaha_schedule(100, io, 2, 1, 10, prof) == ACCT_OK && prof[0] == 10 && prof[1] == 10,
          "an instalment that is all profit");
    const int64_t neg_am[2] = {5, 200};
    CHECK(acct_murabaha_schedule(100, neg_am, 2, 1, 10, prof) == ACCT_E_ARG,
          "an instalment below the period's profit (negative amortisation)");
    const int64_t z0[2] = {0, 100};
    CHECK(acct_murabaha_schedule(100, z0, 2, 0, 1, prof) == ACCT_OK && prof[0] == 0 && prof[1] == 0,
          "a zero instalment at a zero rate");
    const int64_t early[2] = {100, 0};
    CHECK(acct_murabaha_schedule(100, early, 2, 0, 1, prof) == ACCT_OK && prof[0] == 0 && prof[1] == 0,
          "paid off early, nothing left");
    const int64_t over[2] = {200, 0};
    CHECK(acct_murabaha_schedule(100, over, 2, 0, 1, prof) == ACCT_E_ARG, "an overpayment");
    const int64_t negp[2] = {-1, 101};
    CHECK(acct_murabaha_schedule(100, negp, 2, 0, 1, prof) == ACCT_E_ARG, "negative instalment");
    const int64_t negl[1] = {-1};
    CHECK(acct_murabaha_schedule(100, negl, 1, 0, 1, prof) == ACCT_E_ARG, "negative last instalment");
    const int64_t r3[3] = {50, 50, 20};
    /* 100 at 1/10: 10 (open 60), 6 (open 16), last 20 - 16 = 4 */
    CHECK(acct_murabaha_schedule(100, r3, 3, 1, 10, prof) == ACCT_OK && prof[0] == 10 && prof[1] == 6 &&
              prof[2] == 4,
          "three instalments");
    const int64_t h2[2] = {2, 17};
    CHECK(acct_murabaha_schedule(15, h2, 2, 1, 10, prof) == ACCT_OK && prof[0] == 2 && prof[1] == 2,
          "1.5 rounds to 2, the last clears the rest");
}

/* the tax schedule at its edges (A7, A8) */
static void test_tax_edges(void)
{
    acct_tax_input_t in;
    acct_tax_recon_t r;
    memset(&in, 0, sizeof in);
    in.rate_num = 1;
    in.rate_den = 1;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_IFRS, &in, NULL) == ACCT_E_ARG, "no output");
    memset(&r, 0x5A, sizeof r);
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_IFRS, NULL, &r) == ACCT_E_ARG, "no input");
    {
        unsigned dirty = 0;
        for (unsigned i = 0; i < ACCT_TAX_MAX_ITEMS; i++) dirty += r.effect[i] != 0;
        CHECK(dirty == 0 && r.n == 0 && r.tax_at_rate == 0 && r.expected_tax == 0 && r.difference == 0 &&
                  !r.reconciled && r.eff_rate_num == 0 && r.eff_rate_den == 0 && !r.has_zakah &&
                  r.zakah_base == 0 && r.zakah == 0 && r.zakah_difference == 0 && !r.zakah_reconciled,
              "on error the result is zeroed (%u dirty effects)", dirty);
    }
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_COUNT, &in, &r) == ACCT_E_ARG, "invalid framework");
    /* a 100% rate is a rate */
    in.profit_before_tax = 250;
    in.reported_tax_expense = 250;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_IFRS, &in, &r) == ACCT_OK && r.tax_at_rate == 250 &&
              r.reconciled && r.eff_rate_num == 250 && r.eff_rate_den == 250,
          "rate 1/1");
    /* item checks */
    acct_tax_item_t items[ACCT_TAX_MAX_ITEMS + 1];
    for (unsigned i = 0; i <= ACCT_TAX_MAX_ITEMS; i++) {
        items[i].label = "item";
        items[i].kind = ACCT_TAX_ITEM_EFFECT;
        items[i].amount = 1;
    }
    in.items = items;
    in.n_items = ACCT_TAX_MAX_ITEMS;
    in.reported_tax_expense = 250 + ACCT_TAX_MAX_ITEMS;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_US_GAAP, &in, &r) == ACCT_OK &&
              r.n == ACCT_TAX_MAX_ITEMS && r.effect[ACCT_TAX_MAX_ITEMS - 1] == 1 && r.reconciled,
          "sixteen items");
    in.n_items = ACCT_TAX_MAX_ITEMS + 1;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_US_GAAP, &in, &r) == ACCT_E_ARG, "seventeen items");
    in.items = NULL;
    in.n_items = 1;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_US_GAAP, &in, &r) == ACCT_E_ARG, "items missing");
    in.n_items = 0;
    in.reported_tax_expense = 250;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_US_GAAP, &in, &r) == ACCT_OK && r.reconciled && r.n == 0,
          "no items, no pointer");
    in.items = items;
    in.n_items = 1;
    items[0].kind = 2;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_IFRS, &in, &r) == ACCT_E_ARG, "unknown item kind");
    items[0].kind = ACCT_TAX_ITEM_BASE;
    items[0].amount = -3;
    in.rate_num = 1;
    in.rate_den = 2;
    in.profit_before_tax = 3;
    in.reported_tax_expense = 0; /* round(1.5) + round(-1.5) = 2 - 2 */
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_IFRS, &in, &r) == ACCT_OK && r.tax_at_rate == 2 &&
              r.effect[0] == -2 && r.expected_tax == 0 && r.reconciled,
          "each product rounded half away from zero");
    /* the effective rate: none at zero profit, a positive denominator */
    in.n_items = 0;
    in.profit_before_tax = 0;
    in.reported_tax_expense = 5;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_IFRS, &in, &r) == ACCT_OK && r.eff_rate_num == 0 &&
              r.eff_rate_den == 0 && r.difference == 5 && !r.reconciled,
          "no profit: no effective rate");
    in.profit_before_tax = 1;
    in.reported_tax_expense = 0;
    in.rate_num = 0;
    in.rate_den = 1;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_IFRS, &in, &r) == ACCT_OK && r.eff_rate_num == 0 &&
              r.eff_rate_den == 1,
          "profit 1");
    in.profit_before_tax = -1000;
    in.reported_tax_expense = -350;
    in.rate_num = 35;
    in.rate_den = 100;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_IFRS, &in, &r) == ACCT_OK && r.reconciled &&
              r.eff_rate_num == 350 && r.eff_rate_den == 1000,
          "a loss: the denominator is made positive");
    in.profit_before_tax = 1000;
    in.reported_tax_expense = 350;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_IFRS, &in, &r) == ACCT_OK && r.eff_rate_num == 350 &&
              r.eff_rate_den == 1000,
          "a profit keeps its signs");
    in.profit_before_tax = INT64_MIN;
    in.rate_num = 0;
    in.reported_tax_expense = 0;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_IFRS, &in, &r) == ACCT_E_OVERFLOW,
          "an INT64_MIN profit cannot be negated");
    in.profit_before_tax = -1;
    in.reported_tax_expense = INT64_MIN;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_IFRS, &in, &r) == ACCT_E_OVERFLOW,
          "an INT64_MIN tax expense cannot be negated");

    /* zakah edges */
    memset(&in, 0, sizeof in);
    in.rate_num = 0;
    in.rate_den = 1;
    acct_zakah_item_t zi[3] = {{100, true}, {0, true}, {0, false}};
    in.zakah_items = zi;
    in.n_zakah_items = 3;
    in.zakah_rate_num = 1;
    in.zakah_rate_den = 1;
    in.reported_zakah = 100;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_AAOIFI, &in, &r) == ACCT_OK && r.has_zakah &&
              r.zakah_base == 100 && r.zakah == 100 && r.zakah_reconciled && r.zakah_difference == 0,
          "zakah rate 1/1, zero items allowed");
    in.reported_zakah = 99;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_AAOIFI, &in, &r) == ACCT_OK && !r.zakah_reconciled &&
              r.zakah_difference == -1,
          "zakah off by one");
    in.reported_zakah = INT64_MIN;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_AAOIFI, &in, &r) == ACCT_E_OVERFLOW,
          "zakah difference overflow");
    in.reported_zakah = 100;
    in.zakah_rate_num = 2;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_AAOIFI, &in, &r) == ACCT_E_ARG, "zakah rate above 100%%");
    in.zakah_rate_num = 1;
    in.zakah_items = NULL;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_AAOIFI, &in, &r) == ACCT_E_ARG, "zakah items missing");
    in.zakah_items = zi;
    zi[1].amount = -1;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_AAOIFI, &in, &r) == ACCT_E_ARG, "negative zakah item");
    zi[1].amount = 0;
    zi[0].amount = INT64_MAX;
    zi[1].amount = 1;
    CHECK(acct_reconcile_tax_schedule(ACCT_STD_AAOIFI, &in, &r) == ACCT_E_OVERFLOW, "zakah base overflow");
}

int main(void)
{
    test_names();
    test_ias12();
    test_asc740();
    test_ias1();
    test_murabaha();
    test_breakage();
    test_inventory();
    test_zakah();
    test_riba();
    test_differences();
    test_invariants();
    test_labels();
    test_translate_edges();
    test_post_edges();
    test_bs_edges();
    test_arith_edges();
    test_pattern_edges();
    test_cost_flow_edges();
    test_murabaha_edges();
    test_tax_edges();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
