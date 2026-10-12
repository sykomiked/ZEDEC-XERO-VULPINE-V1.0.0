/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* acct.c — accounting adapters (acct.h A1-A8). */
#include "acct.h"

/* ---- frameworks ---- */
static const char *const STD_NAME[ACCT_STD_COUNT] = {
    "", "IFRS", "US_GAAP", "AAOIFI", "IPSAS", "OHADA", "CAS", "SAP_INSURER",
};

static bool streq(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

acct_std_t acct_std_from_name(const char *s)
{
    if (!s) return ACCT_STD_COUNT;
    for (unsigned i = 0; i < ACCT_STD_COUNT; i++)
        if (streq(s, STD_NAME[i])) return (acct_std_t) i;
    return ACCT_STD_COUNT;
}

const char *acct_std_name(acct_std_t s)
{
    return (unsigned) s < ACCT_STD_COUNT ? STD_NAME[s] : "?";
}

bool acct_std_supported(acct_std_t s)
{
    return s == ACCT_STD_IFRS || s == ACCT_STD_US_GAAP || s == ACCT_STD_AAOIFI;
}

const char *acct_status_name(acct_status_t s)
{
    switch (s) {
    case ACCT_OK:
        return "ok";
    case ACCT_E_ARG:
        return "bad argument";
    case ACCT_E_UNSUPPORTED:
        return "framework not supported";
    case ACCT_E_NOT_PERMITTED:
        return "not permitted under this framework";
    case ACCT_E_RIBA:
        return "interest (riba) refused under AAOIFI";
    case ACCT_E_OVERFLOW:
        return "overflow";
    case ACCT_E_UNBALANCED:
        return "unbalanced";
    case ACCT_E_FULL:
        return "full";
    case ACCT_E_KIND:
        return "unknown event kind";
    }
    return "?";
}

/* ---- captions ---- */
typedef struct {
    const char *key;
    uint8_t element;
    bool current;
    const char *label[3]; /* IFRS, US_GAAP, AAOIFI */
} cap_info_t;

#define A ACCT_EL_ASSET
#define L ACCT_EL_LIABILITY
#define E ACCT_EL_EQUITY
#define I ACCT_EL_INCOME
#define X ACCT_EL_EXPENSE
static const cap_info_t CAP[ACCT_C_COUNT] = {
    [ACCT_C_CASH] = {"cash",
                     A,
                     true,
                     {"Cash and cash equivalents", "Cash and cash equivalents",
                      "Cash and balances with banks"}},
    [ACCT_C_RECEIVABLES] = {"receivables",
                            A,
                            true,
                            {"Trade and other receivables", "Accounts receivable, net",
                             "Receivables"}},
    [ACCT_C_FINANCING] = {"financing_receivable",
                          A,
                          true,
                          {"Financing receivables", "Financing receivables",
                           "Murabaha receivables"}},
    [ACCT_C_DEFERRED_PROFIT] = {"deferred_profit",
                                A,
                                true,
                                {"", "", "Less: deferred murabaha profits"}},
    [ACCT_C_OTHER_CURRENT_ASSETS] = {"other_current_assets",
                                     A,
                                     true,
                                     {"Other current assets",
                                      "Prepaid expenses and other current assets", "Other assets"}},
    [ACCT_C_INVENTORY] = {"inventory", A, true, {"Inventories", "Inventories", "Inventories"}},
    [ACCT_C_PPE] = {"ppe",
                    A,
                    false,
                    {"Property, plant and equipment", "Property, plant and equipment, net",
                     "Property and equipment"}},
    [ACCT_C_GOODWILL] = {"goodwill", A, false, {"Goodwill", "Goodwill", "Goodwill"}},
    [ACCT_C_INTANGIBLE] = {"intangible",
                           A,
                           false,
                           {"Other intangible assets", "Intangible assets, net",
                            "Intangible assets"}},
    [ACCT_C_INVESTMENTS] = {"investments",
                            A,
                            false,
                            {"Investments in associates and equity instruments", "Investments",
                             "Investments"}},
    [ACCT_C_DEFERRED_TAX_ASSET] = {"deferred_tax_asset",
                                   A,
                                   false,
                                   {"Deferred tax assets", "Deferred tax assets",
                                    "Deferred tax assets"}},
    [ACCT_C_PAYABLES] = {"payables",
                         L,
                         true,
                         {"Trade and other payables", "Accounts payable", "Payables"}},
    [ACCT_C_SHORT_TERM_BORROWINGS] = {"short_term_borrowings",
                                      L,
                                      true,
                                      {"Short-term borrowings",
                                       "Short-term debt and current portion of long-term debt",
                                       "Short-term financing"}},
    [ACCT_C_PROVISIONS] = {"provisions",
                           L,
                           true,
                           {"Short-term provisions", "Accrued liabilities and provisions",
                            "Provisions"}},
    [ACCT_C_CONTRACT_LIABILITY] = {"contract_liability",
                                   L,
                                   true,
                                   {"Contract liabilities",
                                    "Deferred revenue (contract liabilities)",
                                    "Advances from customers (store credit)"}},
    [ACCT_C_TAX_PAYABLE] = {"tax_payable",
                            L,
                            true,
                            {"Current tax liabilities", "Income taxes payable",
                             "Income tax payable"}},
    [ACCT_C_ZAKAH_PAYABLE] = {"zakah_payable",
                              L,
                              true,
                              {"Zakah payable", "Zakah payable", "Zakah payable"}},
    [ACCT_C_CHARITY_PAYABLE] = {"charity_payable",
                                L,
                                true,
                                {"", "", "Non-permissible income payable to charity"}},
    [ACCT_C_BORROWINGS] = {"borrowings",
                           L,
                           false,
                           {"Long-term borrowings", "Long-term debt", "Financing obtained"}},
    [ACCT_C_REDEEMABLE_UNITS] = {"redeemable_units",
                                 L,
                                 false,
                                 {"Redeemable units (financial liability)", "", ""}},
    [ACCT_C_DEFERRED_TAX_LIABILITY] = {"deferred_tax_liability",
                                       L,
                                       false,
                                       {"Deferred tax liabilities", "Deferred tax liabilities",
                                        "Deferred tax liabilities"}},
    [ACCT_C_PROVISIONS_NONCURRENT] = {"provisions_noncurrent",
                                      L,
                                      false,
                                      {"Long-term provisions", "Other long-term liabilities",
                                       "Provisions (non-current)"}},
    [ACCT_C_TEMP_EQUITY] = {"temporary_equity",
                            ACCT_EL_TEMP_EQUITY,
                            false,
                            {"", "Redeemable units (temporary equity)", ""}},
    [ACCT_C_IAH] = {"iah",
                    ACCT_EL_IAH,
                    false,
                    {"", "", "Equity of unrestricted investment account holders"}},
    [ACCT_C_SHARE_CAPITAL] = {"share_capital",
                              E,
                              false,
                              {"Share capital", "Common stock and additional paid-in capital",
                               "Paid-up capital"}},
    [ACCT_C_RETAINED_EARNINGS] = {"retained_earnings",
                                  E,
                                  false,
                                  {"Retained earnings", "Retained earnings", "Retained earnings"}},
    [ACCT_C_REVALUATION_SURPLUS] = {"revaluation_surplus",
                                    E,
                                    false,
                                    {"Revaluation surplus", "", "Revaluation surplus"}},
    [ACCT_C_OTHER_EQUITY] = {"other_equity",
                             E,
                             false,
                             {"Other components of equity",
                              "Accumulated other comprehensive income", "Other reserves"}},
    [ACCT_C_NCI] = {"nci",
                    E,
                    false,
                    {"Non-controlling interests", "Noncontrolling interests",
                     "Non-controlling interests"}},
    [ACCT_C_REVENUE] = {"revenue", I, false, {"Revenue", "Revenue", "Revenue"}},
    [ACCT_C_FINANCE_INCOME] = {"finance_income",
                               I,
                               false,
                               {"Finance income", "Interest income", "Income from financing"}},
    [ACCT_C_OTHER_INCOME] = {"other_income",
                             I,
                             false,
                             {"Other income", "Other income", "Other income"}},
    [ACCT_C_COST_OF_SALES] = {"cost_of_sales",
                              X,
                              false,
                              {"Cost of sales", "Cost of goods sold", "Cost of sales"}},
    [ACCT_C_OPERATING_EXPENSE] = {"operating_expense",
                                  X,
                                  false,
                                  {"Operating expenses", "Operating expenses",
                                   "Operating expenses"}},
    [ACCT_C_DEVELOPMENT_EXPENSE] = {"development_expense",
                                    X,
                                    false,
                                    {"Research and development expense",
                                     "Research and development expense",
                                     "Research and development expense"}},
    [ACCT_C_DEPRECIATION] = {"depreciation",
                             X,
                             false,
                             {"Depreciation", "Depreciation", "Depreciation"}},
    [ACCT_C_FINANCE_COST] = {"finance_cost",
                             X,
                             false,
                             {"Finance costs", "Interest expense", "Financing costs"}},
    [ACCT_C_TAX_EXPENSE] = {"tax_expense",
                            X,
                            false,
                            {"Income tax expense", "Income tax expense", "Income tax expense"}},
    [ACCT_C_ZAKAH_EXPENSE] = {"zakah_expense", X, false, {"Zakah", "Zakah", "Zakah"}},
};
#undef A
#undef L
#undef E
#undef I
#undef X

static int std_ix(acct_std_t s)
{
    switch (s) {
    case ACCT_STD_IFRS:
        return 0;
    case ACCT_STD_US_GAAP:
        return 1;
    case ACCT_STD_AAOIFI:
        return 2;
    default:
        return -1;
    }
}

acct_element_t acct_cap_element(acct_cap_t c)
{
    return (unsigned) c < ACCT_C_COUNT ? (acct_element_t) CAP[c].element : ACCT_EL_ASSET;
}

bool acct_cap_current(acct_cap_t c)
{
    return (unsigned) c < ACCT_C_COUNT && CAP[c].current;
}

const char *acct_cap_label(acct_std_t s, acct_cap_t c)
{
    int i = std_ix(s);
    if (i < 0 || (unsigned) c >= ACCT_C_COUNT) return "";
    return CAP[c].label[i];
}

const char *acct_cap_key(acct_cap_t c)
{
    return (unsigned) c < ACCT_C_COUNT ? CAP[c].key : "?";
}

/* ---- translation (A2-A5) ---- */
static void j_zero(acct_journal_t *j)
{
    for (unsigned i = 0; i < ACCT_MAX_LINES; i++) {
        j->line[i].cap = 0;
        j->line[i].dr = 0;
        j->line[i].cr = 0;
    }
    j->n = 0;
    j->flags = 0;
    j->ref = "";
}

static void dr(acct_journal_t *j, acct_cap_t c, int64_t a)
{
    if (a == 0) return;
    j->line[j->n].cap = (uint16_t) c;
    j->line[j->n].dr = a;
    j->line[j->n].cr = 0;
    j->n++;
}

static void cr(acct_journal_t *j, acct_cap_t c, int64_t a)
{
    if (a == 0) return;
    j->line[j->n].cap = (uint16_t) c;
    j->line[j->n].dr = 0;
    j->line[j->n].cr = a;
    j->n++;
}

static void pair(acct_journal_t *j, acct_cap_t d, acct_cap_t c, int64_t a)
{
    dr(j, d, a);
    cr(j, c, a);
}

/* by framework: IFRS, US_GAAP, AAOIFI */
#define REF(ifrs, gaap, aaoifi) (ix == 0 ? (ifrs) : ix == 1 ? (gaap) : (aaoifi))

static bool journal_balanced(const acct_journal_t *j)
{
    int64_t d = 0, c = 0;
    if (j->n == 0 || j->n > ACCT_MAX_LINES) return false;
    for (unsigned i = 0; i < j->n; i++) {
        const acct_line_t *l = &j->line[i];
        if (l->cap >= ACCT_C_COUNT || l->dr < 0 || l->cr < 0) return false;
        if ((l->dr == 0) == (l->cr == 0)) return false; /* exactly one side */
        if (__builtin_add_overflow(d, l->dr, &d) || __builtin_add_overflow(c, l->cr, &c))
            return false;
    }
    return d == c;
}

acct_status_t acct_translate_transaction(acct_std_t std, const acct_policy_t *pol,
                                         const acct_txn_t *t, acct_journal_t *out)
{
    if (!out) return ACCT_E_ARG;
    j_zero(out);
    if (!t || (unsigned) std >= ACCT_STD_COUNT) return ACCT_E_ARG;
    if (!acct_std_supported(std)) return ACCT_E_UNSUPPORTED;
    if (t->amount <= 0 || t->amount2 < 0) return ACCT_E_ARG;
    const int ix = std_ix(std);
    const bool aaoifi = std == ACCT_STD_AAOIFI;
    const bool flag_riba = pol && pol->riba == ACCT_RIBA_FLAG;
    const bool credit = (t->flags & ACCT_TF_ON_CREDIT) != 0;
    const int64_t a = t->amount, b = t->amount2;
    const acct_cap_t settle_in = credit ? ACCT_C_RECEIVABLES : ACCT_C_CASH;
    const acct_cap_t settle_out = credit ? ACCT_C_PAYABLES : ACCT_C_CASH;
    acct_journal_t *j = out;

    /* A4: any event the caller marks as interest-bearing */
    bool riba = (t->flags & ACCT_TF_RIBA) != 0 || t->kind == ACCT_K_INTEREST_INCOME ||
                t->kind == ACCT_K_INTEREST_EXPENSE;
    if (aaoifi && riba) {
        if (!flag_riba) return ACCT_E_RIBA;
        j->flags |= ACCT_JF_RIBA;
    }

    switch ((acct_kind_t) t->kind) {
    case ACCT_K_SALE:
        pair(j, settle_in, ACCT_C_REVENUE, a);
        pair(j, ACCT_C_COST_OF_SALES, ACCT_C_INVENTORY, b);
        j->ref = REF("IFRS 15.31, IAS 2.34", "ASC 606-10-25-23, ASC 330-10-35",
                     "IFRS 15.31, IAS 2.34 (A1)");
        break;
    case ACCT_K_PURCHASE_INVENTORY:
        pair(j, ACCT_C_INVENTORY, settle_out, a);
        j->ref = REF("IAS 2.10", "ASC 330-10-30-1", "IAS 2.10 (A1)");
        break;
    case ACCT_K_COST_OF_SALES:
        if (t->cost_method > ACCT_COST_LIFO) return ACCT_E_ARG;
        if (t->cost_method == ACCT_COST_LIFO && std != ACCT_STD_US_GAAP) {
            j_zero(j);
            j->ref = "IAS 2.25";
            return ACCT_E_NOT_PERMITTED;
        }
        pair(j, ACCT_C_COST_OF_SALES, ACCT_C_INVENTORY, a);
        j->ref = REF("IAS 2.25, IAS 2.34", "ASC 330-10-30-9", "IAS 2.25, IAS 2.34 (A1)");
        break;
    case ACCT_K_OPEX:
        pair(j, ACCT_C_OPERATING_EXPENSE, settle_out, a);
        j->ref = REF("IAS 1.99", "ASC 220", "AAOIFI FAS 1");
        break;
    case ACCT_K_DEVELOPMENT_COST:
        if (std == ACCT_STD_US_GAAP) {
            pair(j, ACCT_C_DEVELOPMENT_EXPENSE, settle_out, a);
            j->ref = "ASC 730-10-25-1";
        } else {
            pair(j, ACCT_C_INTANGIBLE, settle_out, a);
            j->ref = REF("IAS 38.57", "", "IAS 38.57 (A1)");
        }
        break;
    case ACCT_K_PPE_PURCHASE:
        pair(j, ACCT_C_PPE, settle_out, a);
        j->ref = REF("IAS 16.15", "ASC 360-10-30-1", "IAS 16.15 (A1)");
        break;
    case ACCT_K_DEPRECIATION:
        pair(j, ACCT_C_DEPRECIATION, ACCT_C_PPE, a);
        j->ref = REF("IAS 16.48", "ASC 360-10-35-4", "IAS 16.48 (A1)");
        break;
    case ACCT_K_PPE_REVALUATION:
        if (std == ACCT_STD_US_GAAP) {
            j_zero(j);
            j->ref = "ASC 360 (cost basis; no upward revaluation)";
            return ACCT_E_NOT_PERMITTED;
        }
        pair(j, ACCT_C_PPE, ACCT_C_REVALUATION_SURPLUS, a);
        j->ref = REF("IAS 16.31, IAS 16.39", "", "IAS 16.31, IAS 16.39 (A1)");
        break;
    case ACCT_K_VOUCHER_ISSUE:
        pair(j, ACCT_C_CASH, ACCT_C_CONTRACT_LIABILITY, a);
        j->ref = REF("IFRS 15.106", "ASC 606-10-45-2", "IFRS 15.106 (A1)");
        break;
    case ACCT_K_VOUCHER_REDEEM:
        pair(j, ACCT_C_CONTRACT_LIABILITY, ACCT_C_REVENUE, a);
        j->ref = REF("IFRS 15.B44", "ASC 606-10-55-46", "IFRS 15.B44 (A1)");
        break;
    case ACCT_K_VOUCHER_BREAKAGE:
        pair(j, ACCT_C_CONTRACT_LIABILITY, ACCT_C_REVENUE, a);
        j->ref = REF("IFRS 15.B46", "ASC 606-10-55-48", "IFRS 15.B46 (A1)");
        break;
    case ACCT_K_VBILL_ISSUE: {
        acct_cap_t to = ACCT_C_SHARE_CAPITAL;
        const char *ref = REF("IAS 32.16", "ASC 505-10", "AAOIFI FAS 1");
        if (t->flags & ACCT_TF_HOLDER_REDEEMABLE) {
            if (std == ACCT_STD_IFRS) {
                if (t->flags & ACCT_TF_PUTTABLE_EXCEPTION) {
                    ref = "IAS 32.16A-16B";
                } else {
                    to = ACCT_C_REDEEMABLE_UNITS;
                    ref = "IAS 32.18(b)";
                }
            } else if (std == ACCT_STD_US_GAAP) {
                to = ACCT_C_TEMP_EQUITY;
                ref = "ASC 480-10-S99-3A";
            } else {
                to = ACCT_C_IAH;
                ref = "AAOIFI FAS 1 (equity of investment account holders)";
            }
        }
        pair(j, ACCT_C_CASH, to, a);
        j->ref = ref;
        break;
    }
    case ACCT_K_FEE_INCOME:
        pair(j, ACCT_C_CASH, ACCT_C_REVENUE, a);
        j->ref = REF("IFRS 15.31", "ASC 606-10-25-23", "IFRS 15.31 (A1)");
        break;
    case ACCT_K_INTEREST_INCOME:
        if (aaoifi) {
            pair(j, ACCT_C_CASH, ACCT_C_CHARITY_PAYABLE, a);
            j->ref = "AAOIFI: non-permissible income set aside for charity";
        } else {
            pair(j, ACCT_C_CASH, ACCT_C_FINANCE_INCOME, a);
            j->ref = REF("IFRS 9.5.4.1", "ASC 310-20-35", "");
        }
        break;
    case ACCT_K_INTEREST_EXPENSE:
        pair(j, ACCT_C_FINANCE_COST, ACCT_C_CASH, a);
        j->ref = REF("IAS 1.82(b)", "ASC 835-30-45", "flagged for the Shariah board (A4)");
        break;
    case ACCT_K_BORROW:
        pair(j, ACCT_C_CASH, ACCT_C_BORROWINGS, a);
        j->ref = REF("IFRS 9.5.1.1", "ASC 470-10", "AAOIFI FAS 1");
        break;
    case ACCT_K_REPAY:
        pair(j, ACCT_C_BORROWINGS, ACCT_C_CASH, a);
        j->ref = REF("IFRS 9.3.3.1", "ASC 405-20-40-1", "AAOIFI FAS 1");
        break;
    case ACCT_K_MURABAHA_SALE:
        if (aaoifi) {
            int64_t gross;
            if (__builtin_add_overflow(a, b, &gross)) return ACCT_E_OVERFLOW;
            dr(j, ACCT_C_FINANCING, gross);
            cr(j, ACCT_C_INVENTORY, a);
            cr(j, ACCT_C_DEFERRED_PROFIT, b);
            j->ref = "AAOIFI FAS 28";
        } else {
            pair(j, ACCT_C_FINANCING, ACCT_C_INVENTORY, a);
            j->ref = REF("IFRS 9.5.1.1", "ASC 310-10", "");
        }
        break;
    case ACCT_K_MURABAHA_PROFIT:
        if (aaoifi) {
            pair(j, ACCT_C_DEFERRED_PROFIT, ACCT_C_FINANCE_INCOME, a);
            j->ref = "AAOIFI FAS 28";
        } else {
            pair(j, ACCT_C_FINANCING, ACCT_C_FINANCE_INCOME, a);
            j->ref = REF("IFRS 9.5.4.1", "ASC 310-20-35", "");
        }
        break;
    case ACCT_K_MURABAHA_COLLECT:
        pair(j, ACCT_C_CASH, ACCT_C_FINANCING, a);
        j->ref = REF("IFRS 9.3.2.3", "ASC 310-10", "AAOIFI FAS 28");
        break;
    case ACCT_K_TAX_CURRENT:
        pair(j, ACCT_C_TAX_EXPENSE, ACCT_C_TAX_PAYABLE, a);
        j->ref = REF("IAS 12.12, IAS 12.58", "ASC 740-10-25-2", "IAS 12.12 (A1)");
        break;
    case ACCT_K_TAX_DEFERRED:
        if (t->flags & ACCT_TF_DEFERRED_ASSET)
            pair(j, ACCT_C_DEFERRED_TAX_ASSET, ACCT_C_TAX_EXPENSE, a);
        else
            pair(j, ACCT_C_TAX_EXPENSE, ACCT_C_DEFERRED_TAX_LIABILITY, a);
        j->ref = REF("IAS 12.15, IAS 12.24", "ASC 740-10-25-2(b)", "IAS 12.15 (A1)");
        break;
    case ACCT_K_ZAKAH:
        pair(j, ACCT_C_ZAKAH_EXPENSE, ACCT_C_ZAKAH_PAYABLE, a);
        j->ref = REF("zakah as a levy (preparer's policy)", "zakah as a levy (preparer's policy)",
                     "AAOIFI FAS 9");
        break;
    case ACCT_K_CAPITAL:
        pair(j, ACCT_C_CASH, ACCT_C_SHARE_CAPITAL, a);
        j->ref = REF("IAS 32.16", "ASC 505-10", "AAOIFI FAS 1");
        break;
    case ACCT_K_COLLECT_RECEIVABLE:
        pair(j, ACCT_C_CASH, ACCT_C_RECEIVABLES, a);
        j->ref = REF("IFRS 9.3.2.3", "ASC 310-10", "IFRS 9.3.2.3 (A1)");
        break;
    case ACCT_K_PAY_PAYABLE:
        pair(j, ACCT_C_PAYABLES, ACCT_C_CASH, a);
        j->ref = REF("IFRS 9.3.3.1", "ASC 405-20-40-1", "IFRS 9.3.3.1 (A1)");
        break;
    case ACCT_K_PAY_TAX:
        pair(j, ACCT_C_TAX_PAYABLE, ACCT_C_CASH, a);
        j->ref = REF("IAS 12.12", "ASC 740-10-25-2", "IAS 12.12 (A1)");
        break;
    default:
        j_zero(j);
        return ACCT_E_KIND;
    }
    if (t->flags & ACCT_TF_NONDEDUCTIBLE) j->flags |= ACCT_JF_NONDEDUCTIBLE;
    if (!journal_balanced(j)) {
        j_zero(j);
        return ACCT_E_UNBALANCED;
    }
    return ACCT_OK;
}
#undef REF

/* ---- the book ---- */
void acct_book_init(acct_book_t *b, acct_std_t std)
{
    if (!b) return;
    b->std = std;
    for (unsigned i = 0; i < ACCT_C_COUNT; i++) b->bal[i] = 0;
    b->postings = 0;
    b->riba_flags = 0;
    b->nondeductible = 0;
}

acct_status_t acct_post(acct_book_t *b, const acct_journal_t *j)
{
    if (!b || !j) return ACCT_E_ARG;
    if (!journal_balanced(j)) return ACCT_E_UNBALANCED;
    int64_t nb[ACCT_C_COUNT];
    for (unsigned i = 0; i < ACCT_C_COUNT; i++) nb[i] = b->bal[i];
    int64_t nd = b->nondeductible;
    for (unsigned i = 0; i < j->n; i++) {
        const acct_line_t *l = &j->line[i];
        if (__builtin_add_overflow(nb[l->cap], l->dr, &nb[l->cap]) ||
            __builtin_sub_overflow(nb[l->cap], l->cr, &nb[l->cap]))
            return ACCT_E_OVERFLOW;
        if ((j->flags & ACCT_JF_NONDEDUCTIBLE) && l->dr > 0 &&
            acct_cap_element((acct_cap_t) l->cap) == ACCT_EL_EXPENSE &&
            __builtin_add_overflow(nd, l->dr, &nd))
            return ACCT_E_OVERFLOW;
    }
    if (b->postings == UINT32_MAX) return ACCT_E_FULL;
    for (unsigned i = 0; i < ACCT_C_COUNT; i++) b->bal[i] = nb[i];
    b->nondeductible = nd;
    b->postings++;
    if (j->flags & ACCT_JF_RIBA) b->riba_flags++;
    return ACCT_OK;
}

acct_status_t acct_record(acct_book_t *b, const acct_policy_t *pol, const acct_txn_t *t)
{
    if (!b) return ACCT_E_ARG;
    acct_journal_t j;
    acct_status_t s = acct_translate_transaction(b->std, pol, t, &j);
    if (s != ACCT_OK) return s;
    return acct_post(b, &j);
}

/* ---- the balance sheet (A6) ---- */
typedef struct {
    uint8_t section;
    uint8_t cap;
} bs_slot_t;

#define SL(s, c)                                                                                   \
    {                                                                                              \
        ACCT_S_##s, ACCT_C_##c                                                                     \
    }
static const bs_slot_t BS_IFRS[] = {
    SL(NONCURRENT_ASSETS, PPE),
    SL(NONCURRENT_ASSETS, GOODWILL),
    SL(NONCURRENT_ASSETS, INTANGIBLE),
    SL(NONCURRENT_ASSETS, INVESTMENTS),
    SL(NONCURRENT_ASSETS, DEFERRED_TAX_ASSET),
    SL(CURRENT_ASSETS, INVENTORY),
    SL(CURRENT_ASSETS, RECEIVABLES),
    SL(CURRENT_ASSETS, FINANCING),
    SL(CURRENT_ASSETS, OTHER_CURRENT_ASSETS),
    SL(CURRENT_ASSETS, CASH),
    SL(EQUITY, SHARE_CAPITAL),
    SL(EQUITY, RETAINED_EARNINGS),
    SL(EQUITY, OTHER_EQUITY),
    SL(EQUITY, REVALUATION_SURPLUS),
    SL(EQUITY, NCI),
    SL(NONCURRENT_LIABILITIES, BORROWINGS),
    SL(NONCURRENT_LIABILITIES, REDEEMABLE_UNITS),
    SL(NONCURRENT_LIABILITIES, DEFERRED_TAX_LIABILITY),
    SL(NONCURRENT_LIABILITIES, PROVISIONS_NONCURRENT),
    SL(CURRENT_LIABILITIES, PAYABLES),
    SL(CURRENT_LIABILITIES, SHORT_TERM_BORROWINGS),
    SL(CURRENT_LIABILITIES, CONTRACT_LIABILITY),
    SL(CURRENT_LIABILITIES, TAX_PAYABLE),
    SL(CURRENT_LIABILITIES, PROVISIONS),
    SL(CURRENT_LIABILITIES, ZAKAH_PAYABLE),
};
static const bs_slot_t BS_GAAP[] = {
    SL(CURRENT_ASSETS, CASH),
    SL(CURRENT_ASSETS, RECEIVABLES),
    SL(CURRENT_ASSETS, FINANCING),
    SL(CURRENT_ASSETS, INVENTORY),
    SL(CURRENT_ASSETS, OTHER_CURRENT_ASSETS),
    SL(NONCURRENT_ASSETS, PPE),
    SL(NONCURRENT_ASSETS, GOODWILL),
    SL(NONCURRENT_ASSETS, INTANGIBLE),
    SL(NONCURRENT_ASSETS, INVESTMENTS),
    SL(NONCURRENT_ASSETS, DEFERRED_TAX_ASSET),
    SL(CURRENT_LIABILITIES, PAYABLES),
    SL(CURRENT_LIABILITIES, SHORT_TERM_BORROWINGS),
    SL(CURRENT_LIABILITIES, CONTRACT_LIABILITY),
    SL(CURRENT_LIABILITIES, TAX_PAYABLE),
    SL(CURRENT_LIABILITIES, ZAKAH_PAYABLE),
    SL(CURRENT_LIABILITIES, PROVISIONS),
    SL(NONCURRENT_LIABILITIES, BORROWINGS),
    SL(NONCURRENT_LIABILITIES, DEFERRED_TAX_LIABILITY),
    SL(NONCURRENT_LIABILITIES, PROVISIONS_NONCURRENT),
    SL(TEMP_EQUITY, TEMP_EQUITY),
    SL(EQUITY, SHARE_CAPITAL),
    SL(EQUITY, RETAINED_EARNINGS),
    SL(EQUITY, OTHER_EQUITY),
    SL(EQUITY, NCI),
};
static const bs_slot_t BS_AAOIFI[] = {
    SL(ASSETS, CASH),
    SL(ASSETS, RECEIVABLES),
    SL(ASSETS, FINANCING),
    SL(ASSETS, DEFERRED_PROFIT),
    SL(ASSETS, OTHER_CURRENT_ASSETS),
    SL(ASSETS, INVENTORY),
    SL(ASSETS, INVESTMENTS),
    SL(ASSETS, INTANGIBLE),
    SL(ASSETS, GOODWILL),
    SL(ASSETS, PPE),
    SL(ASSETS, DEFERRED_TAX_ASSET),
    SL(LIABILITIES, PAYABLES),
    SL(LIABILITIES, SHORT_TERM_BORROWINGS),
    SL(LIABILITIES, CONTRACT_LIABILITY),
    SL(LIABILITIES, CHARITY_PAYABLE),
    SL(LIABILITIES, ZAKAH_PAYABLE),
    SL(LIABILITIES, TAX_PAYABLE),
    SL(LIABILITIES, PROVISIONS),
    SL(LIABILITIES, BORROWINGS),
    SL(LIABILITIES, DEFERRED_TAX_LIABILITY),
    SL(LIABILITIES, PROVISIONS_NONCURRENT),
    SL(IAH, IAH),
    SL(EQUITY, SHARE_CAPITAL),
    SL(EQUITY, RETAINED_EARNINGS),
    SL(EQUITY, REVALUATION_SURPLUS),
    SL(EQUITY, OTHER_EQUITY),
    SL(EQUITY, NCI),
};
#undef SL

const char *acct_section_label(acct_std_t s, acct_section_t sec)
{
    static const char *const ifrs[ACCT_S_COUNT] = {
        "Non-current assets",  "Current assets", "Assets", "Equity", "Non-current liabilities",
        "Current liabilities", "Liabilities",    "",       ""};
    static const char *const gaap[ACCT_S_COUNT] = {
        "Non-current assets",   "Current assets",          "Assets",
        "Stockholders' equity", "Non-current liabilities", "Current liabilities",
        "Liabilities",          "Temporary equity",        ""};
    static const char *const aaoifi[ACCT_S_COUNT] = {
        "",
        "",
        "Assets",
        "Owners' equity",
        "",
        "",
        "Liabilities",
        "",
        "Equity of unrestricted investment account holders"};
    if ((unsigned) sec >= ACCT_S_COUNT) return "";
    switch (s) {
    case ACCT_STD_IFRS:
        return ifrs[sec];
    case ACCT_STD_US_GAAP:
        return gaap[sec];
    case ACCT_STD_AAOIFI:
        return aaoifi[sec];
    default:
        return "";
    }
}

static int64_t presented(acct_cap_t c, int64_t bal)
{
    /* assets show their debit balance; everything else its credit balance */
    return acct_cap_element(c) == ACCT_EL_ASSET ? bal : -bal;
}

acct_status_t acct_generate_balance_sheet(acct_std_t std, const acct_book_t *b, acct_bs_t *out)
{
    if (!out) return ACCT_E_ARG;
    for (unsigned i = 0; i < ACCT_BS_MAX; i++) {
        out->line[i].cap = 0;
        out->line[i].section = 0;
        out->line[i].amount = 0;
    }
    out->std = std;
    out->n = 0;
    out->total_assets = out->total_liabilities = out->total_temp_equity = 0;
    out->total_iah = out->total_equity = out->profit = 0;
    if (!b || (unsigned) std >= ACCT_STD_COUNT) return ACCT_E_ARG;
    if (!acct_std_supported(std)) return ACCT_E_UNSUPPORTED;
    if (b->std != std) return ACCT_E_ARG;

    /* the period's profit: income (credit) less expense (debit) */
    int64_t profit = 0;
    for (unsigned c = 0; c < ACCT_C_COUNT; c++) {
        acct_element_t el = acct_cap_element((acct_cap_t) c);
        if ((el == ACCT_EL_INCOME || el == ACCT_EL_EXPENSE) &&
            __builtin_sub_overflow(profit, b->bal[c], &profit))
            return ACCT_E_OVERFLOW;
    }

    const bs_slot_t *slots;
    unsigned ns;
    if (std == ACCT_STD_IFRS) {
        slots = BS_IFRS;
        ns = sizeof BS_IFRS / sizeof BS_IFRS[0];
    } else if (std == ACCT_STD_US_GAAP) {
        slots = BS_GAAP;
        ns = sizeof BS_GAAP / sizeof BS_GAAP[0];
    } else {
        slots = BS_AAOIFI;
        ns = sizeof BS_AAOIFI / sizeof BS_AAOIFI[0];
    }

    /* every balance-sheet caption with a balance must have a slot */
    for (unsigned c = 0; c < ACCT_C_COUNT; c++) {
        acct_element_t el = acct_cap_element((acct_cap_t) c);
        if (el == ACCT_EL_INCOME || el == ACCT_EL_EXPENSE || b->bal[c] == 0) continue;
        bool found = false;
        for (unsigned s = 0; s < ns; s++)
            if (slots[s].cap == c) found = true;
        if (!found) return ACCT_E_NOT_PERMITTED;
    }

    int64_t ta = 0, tl = 0, tt = 0, ti = 0, te = 0;
    for (unsigned s = 0; s < ns; s++) {
        acct_cap_t c = (acct_cap_t) slots[s].cap;
        int64_t amt = presented(c, b->bal[c]);
        if (c == ACCT_C_RETAINED_EARNINGS && __builtin_add_overflow(amt, profit, &amt))
            return ACCT_E_OVERFLOW;
        if (amt == 0) continue;
        if (out->n >= ACCT_BS_MAX) return ACCT_E_FULL;
        out->line[out->n].cap = (uint16_t) c;
        out->line[out->n].section = slots[s].section;
        out->line[out->n].amount = amt;
        out->n++;
        int64_t *tot;
        switch (acct_cap_element(c)) {
        case ACCT_EL_ASSET:
            tot = &ta;
            break;
        case ACCT_EL_LIABILITY:
            tot = &tl;
            break;
        case ACCT_EL_TEMP_EQUITY:
            tot = &tt;
            break;
        case ACCT_EL_IAH:
            tot = &ti;
            break;
        default:
            tot = &te;
            break;
        }
        if (__builtin_add_overflow(*tot, amt, tot)) return ACCT_E_OVERFLOW;
    }
    int64_t rhs;
    if (__builtin_add_overflow(tl, tt, &rhs) || __builtin_add_overflow(rhs, ti, &rhs) ||
        __builtin_add_overflow(rhs, te, &rhs))
        return ACCT_E_OVERFLOW;
    out->total_assets = ta;
    out->total_liabilities = tl;
    out->total_temp_equity = tt;
    out->total_iah = ti;
    out->total_equity = te;
    out->profit = profit;
    if (ta != rhs) return ACCT_E_UNBALANCED;
    return ACCT_OK;
}

int64_t acct_bs_section_total(const acct_bs_t *bs, acct_section_t sec)
{
    int64_t t = 0;
    if (!bs) return 0;
    for (unsigned i = 0; i < bs->n && i < ACCT_BS_MAX; i++)
        if (bs->line[i].section == sec) t += bs->line[i].amount; /* each total fit, so this does */
    return t;
}

int64_t acct_bs_amount(const acct_bs_t *bs, acct_cap_t c)
{
    if (!bs) return 0;
    for (unsigned i = 0; i < bs->n && i < ACCT_BS_MAX; i++)
        if (bs->line[i].cap == c) return bs->line[i].amount;
    return 0;
}

/* ---- JSON ---- */
typedef struct {
    char *p;
    size_t cap, len;
    bool over;
} jw_t;

static void jw_c(jw_t *w, char c)
{
    if (w->len + 1 < w->cap)
        w->p[w->len] = c;
    else
        w->over = true;
    w->len++;
}

static void jw_s(jw_t *w, const char *s)
{
    while (*s) jw_c(w, *s++);
}

static void jw_str(jw_t *w, const char *s)
{
    jw_c(w, '"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') jw_c(w, '\\');
        jw_c(w, *s);
    }
    jw_c(w, '"');
}

static void jw_i(jw_t *w, int64_t v)
{
    char t[24];
    int n = 0;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    do {
        t[n++] = (char) ('0' + u % 10u);
        u /= 10u;
    } while (u);
    if (v < 0) jw_c(w, '-');
    while (n) jw_c(w, t[--n]);
}

static const char *const SECTION_KEY[ACCT_S_COUNT] = {
    "noncurrent_assets",   "current_assets", "assets",           "equity", "noncurrent_liabilities",
    "current_liabilities", "liabilities",    "temporary_equity", "iah"};

int acct_bs_json(const acct_bs_t *bs, char *out, size_t cap)
{
    if (!bs || !out || cap == 0) return -1;
    jw_t w = {out, cap, 0, false};
    jw_s(&w, "{\"standard\":");
    jw_str(&w, acct_std_name(bs->std));
    jw_s(&w, ",\"lines\":[");
    for (unsigned i = 0; i < bs->n && i < ACCT_BS_MAX; i++) {
        const acct_bs_line_t *l = &bs->line[i];
        if (i) jw_c(&w, ',');
        jw_s(&w, "{\"section\":");
        jw_str(&w, l->section < ACCT_S_COUNT ? SECTION_KEY[l->section] : "?");
        jw_s(&w, ",\"key\":");
        jw_str(&w, acct_cap_key((acct_cap_t) l->cap));
        jw_s(&w, ",\"label\":");
        jw_str(&w, acct_cap_label(bs->std, (acct_cap_t) l->cap));
        jw_s(&w, ",\"amount\":");
        jw_i(&w, l->amount);
        jw_c(&w, '}');
    }
    jw_s(&w, "],\"total_assets\":");
    jw_i(&w, bs->total_assets);
    jw_s(&w, ",\"total_liabilities\":");
    jw_i(&w, bs->total_liabilities);
    jw_s(&w, ",\"total_temporary_equity\":");
    jw_i(&w, bs->total_temp_equity);
    jw_s(&w, ",\"total_iah\":");
    jw_i(&w, bs->total_iah);
    jw_s(&w, ",\"total_equity\":");
    jw_i(&w, bs->total_equity);
    jw_s(&w, ",\"profit\":");
    jw_i(&w, bs->profit);
    jw_c(&w, '}');
    if (w.over) {
        out[0] = '\0';
        return -1;
    }
    out[w.len] = '\0';
    return (int) w.len;
}

/* ---- exact arithmetic ---- */
__extension__ typedef __int128 i128;

bool acct_mul_rate(int64_t a, uint64_t num, uint64_t den, int64_t *out)
{
    if (!out || den == 0 || num > (uint64_t) INT64_MAX || den > (uint64_t) INT64_MAX) return false;
    i128 p = (i128) a * (i128) num; /* |a| < 2^63, num < 2^63: fits */
    i128 q = p / (i128) den, r = p % (i128) den;
    i128 r2 = r < 0 ? -r : r;
    if (r2 * 2 >= (i128) den) q += p < 0 ? -1 : 1;
    if (q > INT64_MAX || q < INT64_MIN) return false;
    *out = (int64_t) q;
    return true;
}

/* round(n / d) half away from zero; d > 0 */
static i128 div_round(i128 n, i128 d)
{
    i128 q = n / d, r = n % d;
    if (r < 0) r = -r;
    if (r * 2 >= d) q += n < 0 ? -1 : 1;
    return q;
}

bool acct_ratio(int64_t num, int64_t den, int64_t scale, int64_t *out)
{
    if (!out || den == 0) return false;
    i128 n = (i128) num * scale, d = den;
    if (d < 0) {
        n = -n;
        d = -d;
    }
    i128 q = div_round(n, d);
    if (q > INT64_MAX || q < INT64_MIN) return false;
    *out = (int64_t) q;
    return true;
}

acct_status_t acct_pattern_revenue(int64_t amount, int64_t exercised, int64_t expected,
                                   int64_t recognised, int64_t *now)
{
    if (!now) return ACCT_E_ARG;
    *now = 0;
    if (amount < 0 || exercised < 0 || expected < 0 || recognised < 0 || recognised > amount)
        return ACCT_E_ARG;
    int64_t cum = amount;
    if (exercised < expected) cum = (int64_t) div_round((i128) amount * exercised, expected);
    *now = cum - recognised;
    return ACCT_OK;
}

acct_status_t acct_breakage(int64_t issued, int64_t breakage, int64_t redeemed, int64_t recognised,
                            int64_t *now)
{
    if (!now) return ACCT_E_ARG;
    *now = 0;
    if (issued <= 0 || breakage < 0 || breakage > issued) return ACCT_E_ARG;
    return acct_pattern_revenue(breakage, redeemed, issued - breakage, recognised, now);
}

#define ALLOC_MAX 16u
acct_status_t acct_allocate(int64_t price, const int64_t *ssp, uint32_t n, int64_t *out)
{
    if (!ssp || !out || n == 0 || n > ALLOC_MAX || price < 0) return ACCT_E_ARG;
    i128 total = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (ssp[i] < 0) return ACCT_E_ARG;
        total += ssp[i];
    }
    if (total == 0) return ACCT_E_ARG;
    i128 rem[ALLOC_MAX];
    int64_t left = price;
    for (uint32_t i = 0; i < n; i++) {
        i128 p = (i128) price * ssp[i];
        out[i] = (int64_t) (p / total);
        rem[i] = p % total;
        left -= out[i];
    }
    while (left > 0) { /* fewer than n units remain */
        uint32_t best = 0;
        for (uint32_t i = 1; i < n; i++)
            if (rem[i] > rem[best]) best = i;
        out[best]++;
        rem[best] = -1;
        left--;
    }
    return ACCT_OK;
}

acct_status_t acct_cost_flow(acct_std_t std, const acct_layer_t *layers, uint32_t n, int64_t sold,
                             uint8_t method, int64_t *cost_of_sales, int64_t *ending)
{
    if (!cost_of_sales || !ending) return ACCT_E_ARG;
    *cost_of_sales = *ending = 0;
    if (!layers || n == 0 || sold < 0 || method > ACCT_COST_LIFO) return ACCT_E_ARG;
    if (!acct_std_supported(std)) return ACCT_E_UNSUPPORTED;
    if (method == ACCT_COST_LIFO && std != ACCT_STD_US_GAAP) return ACCT_E_NOT_PERMITTED;
    int64_t units = 0, cost = 0;
    for (uint32_t i = 0; i < n; i++) {
        int64_t c;
        if (layers[i].units < 0 || layers[i].unit_cost < 0) return ACCT_E_ARG;
        if (__builtin_mul_overflow(layers[i].units, layers[i].unit_cost, &c) ||
            __builtin_add_overflow(cost, c, &cost) ||
            __builtin_add_overflow(units, layers[i].units, &units))
            return ACCT_E_OVERFLOW;
    }
    if (sold > units) return ACCT_E_ARG;
    int64_t cogs = 0;
    if (method == ACCT_COST_WAVG) {
        if (units > 0) cogs = (int64_t) div_round((i128) cost * sold, units);
    } else {
        int64_t want = sold;
        for (uint32_t k = 0; k < n && want > 0; k++) {
            const acct_layer_t *l = &layers[method == ACCT_COST_FIFO ? k : n - 1 - k];
            int64_t take = l->units < want ? l->units : want;
            cogs += take * l->unit_cost; /* <= cost, which fit */
            want -= take;
        }
    }
    *cost_of_sales = cogs;
    *ending = cost - cogs;
    return ACCT_OK;
}

acct_status_t acct_murabaha_profit(int64_t markup, uint32_t days, uint32_t from, uint32_t to,
                                   int64_t *out)
{
    if (!out) return ACCT_E_ARG;
    *out = 0;
    if (markup < 0 || days == 0 || from > to || to > days) return ACCT_E_ARG;
    i128 hi = ((i128) markup * to) / days, lo = ((i128) markup * from) / days;
    *out = (int64_t) (hi - lo);
    return ACCT_OK;
}

acct_status_t acct_murabaha_schedule(int64_t cost, const int64_t *payment, uint32_t n,
                                     uint64_t rate_num, uint64_t rate_den, int64_t *profit)
{
    if (!payment || !profit || n == 0 || cost <= 0 || rate_den == 0) return ACCT_E_ARG;
    int64_t open = cost;
    for (uint32_t i = 0; i < n; i++) {
        int64_t p;
        if (payment[i] < 0) return ACCT_E_ARG;
        if (i + 1 < n) {
            if (!acct_mul_rate(open, rate_num, rate_den, &p)) return ACCT_E_OVERFLOW;
            if (p < 0 || p > payment[i]) return ACCT_E_ARG; /* no negative amortisation */
            open -= payment[i] - p;
            if (open < 0) return ACCT_E_ARG;
        } else {
            p = payment[i] - open; /* the last instalment clears the balance */
            if (p < 0) return ACCT_E_ARG;
        }
        profit[i] = p;
    }
    return ACCT_OK;
}

/* ---- the tax schedule (A7, A8) ---- */
acct_status_t acct_reconcile_tax_schedule(acct_std_t std, const acct_tax_input_t *in,
                                          acct_tax_recon_t *out)
{
    if (!out) return ACCT_E_ARG;
    out->tax_at_rate = out->expected_tax = out->difference = 0;
    for (unsigned i = 0; i < ACCT_TAX_MAX_ITEMS; i++) out->effect[i] = 0;
    out->n = 0;
    out->reconciled = false;
    out->eff_rate_num = 0;
    out->eff_rate_den = 0;
    out->has_zakah = false;
    out->zakah_base = out->zakah = out->zakah_difference = 0;
    out->zakah_reconciled = false;
    if (!in || (unsigned) std >= ACCT_STD_COUNT) return ACCT_E_ARG;
    if (!acct_std_supported(std)) return ACCT_E_UNSUPPORTED;
    if (in->rate_den == 0 || in->rate_num > in->rate_den) return ACCT_E_ARG;
    if (in->n_items > ACCT_TAX_MAX_ITEMS || (in->n_items && !in->items)) return ACCT_E_ARG;

    int64_t exp;
    if (!acct_mul_rate(in->profit_before_tax, in->rate_num, in->rate_den, &out->tax_at_rate))
        return ACCT_E_OVERFLOW;
    exp = out->tax_at_rate;
    for (uint32_t i = 0; i < in->n_items; i++) {
        const acct_tax_item_t *it = &in->items[i];
        int64_t e;
        if (it->kind == ACCT_TAX_ITEM_BASE) {
            if (!acct_mul_rate(it->amount, in->rate_num, in->rate_den, &e)) return ACCT_E_OVERFLOW;
        } else if (it->kind == ACCT_TAX_ITEM_EFFECT) {
            e = it->amount;
        } else {
            return ACCT_E_ARG;
        }
        out->effect[i] = e;
        if (__builtin_add_overflow(exp, e, &exp)) return ACCT_E_OVERFLOW;
    }
    out->n = in->n_items;
    out->expected_tax = exp;
    if (__builtin_sub_overflow(in->reported_tax_expense, exp, &out->difference))
        return ACCT_E_OVERFLOW;
    out->reconciled = out->difference == 0;
    if (in->profit_before_tax != 0) {
        out->eff_rate_num = in->reported_tax_expense;
        out->eff_rate_den = in->profit_before_tax;
        if (out->eff_rate_den < 0) {
            if (out->eff_rate_num == INT64_MIN || out->eff_rate_den == INT64_MIN)
                return ACCT_E_OVERFLOW;
            out->eff_rate_num = -out->eff_rate_num;
            out->eff_rate_den = -out->eff_rate_den;
        }
    }

    if (std == ACCT_STD_AAOIFI && in->n_zakah_items) {
        if (!in->zakah_items || in->zakah_rate_den == 0 || in->zakah_rate_num > in->zakah_rate_den)
            return ACCT_E_ARG;
        int64_t base = 0;
        for (uint32_t i = 0; i < in->n_zakah_items; i++) {
            const acct_zakah_item_t *z = &in->zakah_items[i];
            if (z->amount < 0) return ACCT_E_ARG;
            if (z->zakatable ? __builtin_add_overflow(base, z->amount, &base)
                             : __builtin_sub_overflow(base, z->amount, &base))
                return ACCT_E_OVERFLOW;
        }
        if (base < 0) base = 0;
        out->has_zakah = true;
        out->zakah_base = base;
        if (!acct_mul_rate(base, in->zakah_rate_num, in->zakah_rate_den, &out->zakah))
            return ACCT_E_OVERFLOW;
        if (__builtin_sub_overflow(in->reported_zakah, out->zakah, &out->zakah_difference))
            return ACCT_E_OVERFLOW;
        out->zakah_reconciled = out->zakah_difference == 0;
    }
    return ACCT_OK;
}
