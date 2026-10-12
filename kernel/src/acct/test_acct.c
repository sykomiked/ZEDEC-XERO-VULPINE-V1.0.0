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
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
