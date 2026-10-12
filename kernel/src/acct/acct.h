/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* acct.h — accounting adapters: IFRS, US GAAP and AAOIFI.
 *
 * Three calls turn ZXV's economic events into reports a preparer can start
 * from. Each adapter is DESIGNED TO FOLLOW the named framework; none is
 * certified by, or affiliated with, the IFRS Foundation, the FASB or AAOIFI.
 *
 *   acct_translate_transaction  one economic event -> one balanced journal
 *                               entry in the chosen framework's captions
 *   acct_post                   the entry into a book (trial balance)
 *   acct_generate_balance_sheet the book -> a statement of financial
 *                               position in the framework's layout
 *   acct_reconcile_tax_schedule tax at the statutory rate -> tax expense,
 *                               item by item (IAS 12.81(c), ASC 740 rate
 *                               reconciliation); under AAOIFI also the
 *                               zakah base and zakah
 *
 *   A1 FRAMEWORKS. IFRS, US_GAAP and AAOIFI are built. IPSAS, OHADA, CAS and
 *      SAP_INSURER (the other names zxvd's jurisdiction.set accepts) return
 *      ACCT_E_UNSUPPORTED: no report is better than a wrong one. Where AAOIFI
 *      has no standard of its own (inventories, intangibles, property), the
 *      AAOIFI adapter uses the IFRS treatment, the fallback AAOIFI's
 *      preface describes for matters it does not cover.
 *   A2 BALANCED. Every journal entry's debits equal its credits, every
 *      amount is a positive int64 in minor units, and every sum is checked
 *      for overflow. A translation that cannot balance is refused.
 *   A3 FRAMEWORK DIFFERENCES ARE REAL. The same event can post differently
 *      or be refused:
 *        - LIFO cost of sales: allowed under US GAAP (ASC 330), refused
 *          under IFRS (IAS 2.25) and so under AAOIFI (A1).
 *        - Development costs that meet the recognition criteria: an
 *          intangible asset under IFRS (IAS 38.57) and AAOIFI (A1); an
 *          expense under US GAAP (ASC 730-10-25-1).
 *        - Upward revaluation of property, plant and equipment: to the
 *          revaluation surplus in equity under IFRS (IAS 16.31, 16.39);
 *          refused under US GAAP, which keeps cost.
 *        - V-Bills the holder can redeem: a financial liability under IFRS
 *          (IAS 32.18(b)) unless the preparer asserts the puttable exception
 *          (IAS 32.16A-16B); temporary equity under US GAAP (ASC 480-10-S99,
 *          for SEC registrants); equity of investment account holders under
 *          AAOIFI. Not redeemable: equity everywhere.
 *        - Murabaha: AAOIFI shows the receivable gross with deferred profit
 *          deducted; IFRS and US GAAP carry it at amortised cost. The net
 *          amount is the same in all three.
 *   A4 RIBA. Under AAOIFI an interest event (INTEREST_INCOME,
 *      INTEREST_EXPENSE, or any event the caller marks ACCT_TF_RIBA) is
 *      refused by default (ACCT_RIBA_BLOCK) with ACCT_E_RIBA. With
 *      ACCT_RIBA_FLAG it posts and carries ACCT_JF_RIBA: interest received
 *      goes to a liability for charity, never to income, and interest paid
 *      is flagged for the Shariah board. The adapter only flags: whether an
 *      arrangement is Shariah compliant is a ruling for a Shariah
 *      supervisory board, never for this code.
 *   A5 STORE CREDIT. Vino vouchers sold for cash are a contract liability
 *      (IFRS 15.106, ASC 606-10-45-2), revenue on redemption, and expected
 *      breakage in proportion to the pattern of redemptions (IFRS 15.B46,
 *      ASC 606-10-55-48); acct_breakage computes it exactly.
 *   A6 BALANCE SHEET. acct_generate_balance_sheet closes income and expense
 *      into the period's profit, groups captions in the framework's layout
 *      and checks assets = liabilities + temporary equity + equity of
 *      investment account holders + equity. A book that does not balance
 *      is refused (ACCT_E_UNBALANCED).
 *        IFRS     non-current then current assets; equity, then
 *                 non-current and current liabilities (IAS 1 IG layout)
 *        US_GAAP  current then non-current assets; current and non-current
 *                 liabilities; temporary equity; stockholders' equity
 *        AAOIFI   assets and liabilities by liquidity, with no current /
 *                 non-current split; equity of unrestricted investment
 *                 account holders between liabilities and owners' equity
 *                 (FAS 1)
 *      Current / non-current comes from the caption (borrowings and
 *      deferred tax are non-current, trading items are current); a
 *      preparer who needs a split by maturity posts to separate books.
 *   A7 TAX RECONCILIATION. Tax at the statutory rate on accounting profit,
 *      plus the tax effect of each reconciling item, must equal reported
 *      tax expense. An item is either a pre-tax base (taxed at the rate) or
 *      a direct tax effect (a rate change on opening deferred tax, prior
 *      year adjustments). Each product is rounded half away from zero to
 *      the minor unit, and the result says whether it reconciles. The
 *      average effective rate (IAS 12.81(c)(ii)) comes back as an exact
 *      fraction.
 *   A8 ZAKAH (AAOIFI). zakah = round(base * rate), base = zakatable assets
 *      less deductible liabilities, never below zero; the caller supplies
 *      which items count and the rate (2.5% on a lunar year, 2.5775% on a
 *      solar year are the usual ones). Which items are zakatable is a
 *      Shariah question; the code adds up what it is given.
 *
 * HONEST LIMITS. These adapters produce a starting point, not filed
 * statements: they do not replace an accountant, an auditor or a Shariah
 * board, and they carry no measurement models (fair value, expected credit
 * loss, impairment, leases, consolidation). Freestanding C11: no
 * allocation, no float.
 */
#ifndef ZXV_ACCT_H
#define ZXV_ACCT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- frameworks (A1); the names are zxvd's jurisdiction.set names ---- */
typedef enum {
    ACCT_STD_NONE = 0,
    ACCT_STD_IFRS = 1,
    ACCT_STD_US_GAAP = 2,
    ACCT_STD_AAOIFI = 3,
    ACCT_STD_IPSAS = 4,
    ACCT_STD_OHADA = 5,
    ACCT_STD_CAS = 6,
    ACCT_STD_SAP_INSURER = 7,
    ACCT_STD_COUNT = 8
} acct_std_t;

/* "IFRS", "US_GAAP", ... exactly (case-sensitive), "" -> NONE; anything
 * else -> ACCT_STD_COUNT (invalid). */
acct_std_t acct_std_from_name(const char *s);
const char *acct_std_name(acct_std_t s);
bool acct_std_supported(acct_std_t s);

typedef enum {
    ACCT_OK = 0,
    ACCT_E_ARG = -1,
    ACCT_E_UNSUPPORTED = -2,  /* A1: framework not built */
    ACCT_E_NOT_PERMITTED = -3, /* A3: the framework does not allow it */
    ACCT_E_RIBA = -4,         /* A4: interest refused under AAOIFI */
    ACCT_E_OVERFLOW = -5,
    ACCT_E_UNBALANCED = -6,   /* A2 / A6 */
    ACCT_E_FULL = -7,
    ACCT_E_KIND = -8          /* unknown event kind */
} acct_status_t;
const char *acct_status_name(acct_status_t s);

/* ---- elements and captions ---- */
typedef enum {
    ACCT_EL_ASSET = 0,
    ACCT_EL_LIABILITY = 1,
    ACCT_EL_TEMP_EQUITY = 2,  /* US GAAP mezzanine */
    ACCT_EL_IAH = 3,          /* AAOIFI equity of investment account holders */
    ACCT_EL_EQUITY = 4,
    ACCT_EL_INCOME = 5,
    ACCT_EL_EXPENSE = 6
} acct_element_t;

typedef enum {
    /* assets, most liquid first */
    ACCT_C_CASH = 0,
    ACCT_C_RECEIVABLES,
    ACCT_C_FINANCING,        /* murabaha / financing receivable */
    ACCT_C_DEFERRED_PROFIT,  /* AAOIFI contra-asset to ACCT_C_FINANCING */
    ACCT_C_OTHER_CURRENT_ASSETS,
    ACCT_C_INVENTORY,
    ACCT_C_PPE,
    ACCT_C_GOODWILL,
    ACCT_C_INTANGIBLE,
    ACCT_C_INVESTMENTS,      /* associates, equity instruments */
    ACCT_C_DEFERRED_TAX_ASSET,
    /* liabilities */
    ACCT_C_PAYABLES,
    ACCT_C_SHORT_TERM_BORROWINGS, /* incl. the current part of long-term */
    ACCT_C_PROVISIONS,       /* current */
    ACCT_C_CONTRACT_LIABILITY, /* store credit (Vino), deferred revenue */
    ACCT_C_TAX_PAYABLE,
    ACCT_C_ZAKAH_PAYABLE,
    ACCT_C_CHARITY_PAYABLE,  /* A4: interest received, owed to charity */
    ACCT_C_BORROWINGS,
    ACCT_C_REDEEMABLE_UNITS, /* IFRS: holder-redeemable V-Bills */
    ACCT_C_DEFERRED_TAX_LIABILITY,
    ACCT_C_PROVISIONS_NONCURRENT,
    /* between liabilities and equity */
    ACCT_C_TEMP_EQUITY,      /* US GAAP */
    ACCT_C_IAH,              /* AAOIFI */
    /* equity */
    ACCT_C_SHARE_CAPITAL,
    ACCT_C_RETAINED_EARNINGS,
    ACCT_C_REVALUATION_SURPLUS,
    ACCT_C_OTHER_EQUITY,     /* other components of equity (OCI reserves) */
    ACCT_C_NCI,              /* non-controlling interests */
    /* income and expense */
    ACCT_C_REVENUE,
    ACCT_C_FINANCE_INCOME,
    ACCT_C_OTHER_INCOME,
    ACCT_C_COST_OF_SALES,
    ACCT_C_OPERATING_EXPENSE,
    ACCT_C_DEVELOPMENT_EXPENSE,
    ACCT_C_DEPRECIATION,
    ACCT_C_FINANCE_COST,
    ACCT_C_TAX_EXPENSE,
    ACCT_C_ZAKAH_EXPENSE,
    ACCT_C_COUNT
} acct_cap_t;

acct_element_t acct_cap_element(acct_cap_t c);
bool acct_cap_current(acct_cap_t c);
/* The caption's label in a framework's statements ("" when the framework
 * never uses it). */
const char *acct_cap_label(acct_std_t s, acct_cap_t c);
/* Stable machine key, e.g. "cash", "contract_liability". */
const char *acct_cap_key(acct_cap_t c);

/* ---- economic events ---- */
typedef enum {
    ACCT_K_SALE = 1,            /* amount = revenue; amount2 = cost (0: none) */
    ACCT_K_PURCHASE_INVENTORY,  /* amount */
    ACCT_K_COST_OF_SALES,       /* amount; cost_method */
    ACCT_K_OPEX,                /* amount */
    ACCT_K_DEVELOPMENT_COST,    /* amount; meets IAS 38.57 criteria */
    ACCT_K_PPE_PURCHASE,        /* amount */
    ACCT_K_DEPRECIATION,        /* amount */
    ACCT_K_PPE_REVALUATION,     /* amount = upward revaluation */
    ACCT_K_VOUCHER_ISSUE,       /* amount: Vino store credit sold for cash */
    ACCT_K_VOUCHER_REDEEM,      /* amount */
    ACCT_K_VOUCHER_BREAKAGE,    /* amount (from acct_breakage) */
    ACCT_K_VBILL_ISSUE,         /* amount; ACCT_TF_HOLDER_REDEEMABLE ... */
    ACCT_K_FEE_INCOME,          /* amount: assurance fee received */
    ACCT_K_INTEREST_INCOME,     /* amount (riba under AAOIFI) */
    ACCT_K_INTEREST_EXPENSE,    /* amount (riba under AAOIFI) */
    ACCT_K_BORROW,              /* amount; ACCT_TF_RIBA if interest-bearing */
    ACCT_K_REPAY,               /* amount */
    ACCT_K_MURABAHA_SALE,       /* amount = cost, amount2 = markup */
    ACCT_K_MURABAHA_PROFIT,     /* amount = profit earned this period */
    ACCT_K_MURABAHA_COLLECT,    /* amount = cash instalment */
    ACCT_K_TAX_CURRENT,         /* amount = current tax for the period */
    ACCT_K_TAX_DEFERRED,        /* amount; ACCT_TF_DEFERRED_ASSET: DTA up,
                                   else DTL up (both against tax expense) */
    ACCT_K_ZAKAH,               /* amount */
    ACCT_K_CAPITAL,             /* amount: owners pay in share capital */
    ACCT_K_COLLECT_RECEIVABLE,  /* amount */
    ACCT_K_PAY_PAYABLE,         /* amount */
    ACCT_K_PAY_TAX,             /* amount */
    ACCT_K_COUNT
} acct_kind_t;

#define ACCT_TF_ON_CREDIT          0x0001u /* receivable / payable, not cash */
#define ACCT_TF_NONDEDUCTIBLE      0x0002u /* recorded for the tax schedule */
#define ACCT_TF_HOLDER_REDEEMABLE  0x0004u /* V-Bill the holder can redeem */
#define ACCT_TF_PUTTABLE_EXCEPTION 0x0008u /* preparer asserts IAS 32.16A-B */
#define ACCT_TF_RIBA               0x0010u /* caller marks interest-bearing */
#define ACCT_TF_DEFERRED_ASSET     0x0020u

typedef enum { ACCT_COST_FIFO = 0, ACCT_COST_WAVG = 1, ACCT_COST_LIFO = 2 } acct_cost_method_t;

typedef struct {
    uint16_t kind;   /* acct_kind_t */
    uint16_t flags;  /* ACCT_TF_* */
    uint8_t cost_method;
    int64_t amount;  /* > 0, minor units */
    int64_t amount2; /* >= 0 */
} acct_txn_t;

typedef enum { ACCT_RIBA_BLOCK = 0, ACCT_RIBA_FLAG = 1 } acct_riba_policy_t;
typedef struct {
    uint8_t riba; /* acct_riba_policy_t; AAOIFI only */
} acct_policy_t;

#define ACCT_MAX_LINES 6u
#define ACCT_JF_RIBA         0x01u /* A4: needs a Shariah board's attention */
#define ACCT_JF_NONDEDUCTIBLE 0x02u
typedef struct {
    uint16_t cap; /* acct_cap_t */
    int64_t dr;
    int64_t cr;
} acct_line_t;
typedef struct {
    acct_line_t line[ACCT_MAX_LINES];
    uint8_t n;
    uint8_t flags;   /* ACCT_JF_* */
    const char *ref; /* the paragraph the treatment follows */
} acct_journal_t;

/* pol may be NULL (ACCT_RIBA_BLOCK). On error *out is zeroed. */
acct_status_t acct_translate_transaction(acct_std_t std, const acct_policy_t *pol,
                                         const acct_txn_t *t, acct_journal_t *out);

/* ---- the book (A2) ---- */
typedef struct {
    acct_std_t std;
    int64_t bal[ACCT_C_COUNT]; /* debit positive, credit negative */
    uint32_t postings;
    uint32_t riba_flags;       /* entries posted with ACCT_JF_RIBA */
    int64_t nondeductible;     /* sum of ACCT_JF_NONDEDUCTIBLE debits */
} acct_book_t;

void acct_book_init(acct_book_t *b, acct_std_t std);
/* All or nothing: a journal that does not balance, overflows a balance or
 * names an unknown caption changes nothing. */
acct_status_t acct_post(acct_book_t *b, const acct_journal_t *j);
/* translate + post in one step */
acct_status_t acct_record(acct_book_t *b, const acct_policy_t *pol, const acct_txn_t *t);

/* ---- the balance sheet (A6) ---- */
typedef enum {
    ACCT_S_NONCURRENT_ASSETS = 0,
    ACCT_S_CURRENT_ASSETS,
    ACCT_S_ASSETS, /* AAOIFI: no split */
    ACCT_S_EQUITY,
    ACCT_S_NONCURRENT_LIABILITIES,
    ACCT_S_CURRENT_LIABILITIES,
    ACCT_S_LIABILITIES, /* AAOIFI: no split */
    ACCT_S_TEMP_EQUITY,
    ACCT_S_IAH,
    ACCT_S_COUNT
} acct_section_t;
const char *acct_section_label(acct_std_t s, acct_section_t sec);

#define ACCT_BS_MAX 32u
typedef struct {
    uint16_t cap;    /* acct_cap_t */
    uint8_t section; /* acct_section_t */
    int64_t amount;  /* as presented: positive for a normal balance */
} acct_bs_line_t;
typedef struct {
    acct_std_t std;
    acct_bs_line_t line[ACCT_BS_MAX];
    uint8_t n;
    int64_t total_assets;
    int64_t total_liabilities;
    int64_t total_temp_equity;
    int64_t total_iah;
    int64_t total_equity; /* includes the period's profit */
    int64_t profit;       /* income less expense, not yet closed */
} acct_bs_t;

acct_status_t acct_generate_balance_sheet(acct_std_t std, const acct_book_t *b, acct_bs_t *out);
/* Sum of one section's lines. */
int64_t acct_bs_section_total(const acct_bs_t *bs, acct_section_t sec);
/* Amount of one caption on the statement (0 when absent). */
int64_t acct_bs_amount(const acct_bs_t *bs, acct_cap_t c);
/* {"standard":"IFRS","lines":[{"section":..,"key":..,"label":..,"amount":..}],
 *  "total_assets":..,...}. Returns the length written (excluding the NUL), or
 * -1 when cap is too small (out is then "" if cap > 0). */
int acct_bs_json(const acct_bs_t *bs, char *out, size_t cap);

/* ---- revenue by pattern of use (A5) ----
 * The part of `amount` to recognise now when `exercised` of `expected`
 * rights have been used and `recognised` was recognised before:
 * round(amount * exercised / expected) - recognised, the cumulative figure
 * rounded half up and capped at amount (ASC 606-10-55-353 to 55-356, IFRS
 * 15 IE Example 52 work this way). Negative when a higher expectation
 * reverses revenue recognised earlier. */
acct_status_t acct_pattern_revenue(int64_t amount, int64_t exercised, int64_t expected,
                                   int64_t recognised, int64_t *now);
/* Breakage of store credit (IFRS 15.B46, ASC 606-10-55-48): issued = store
 * credit sold, breakage = the part not expected to be redeemed, redeemed =
 * cumulative redemptions, recognised = breakage recognised so far. The same
 * as acct_pattern_revenue(breakage, redeemed, issued - breakage, ...). */
acct_status_t acct_breakage(int64_t issued, int64_t breakage, int64_t redeemed,
                            int64_t recognised, int64_t *now);
/* Relative stand-alone selling price allocation (IFRS 15.76, ASC
 * 606-10-32-31): price split over n obligations in proportion to ssp[],
 * rounded by largest remainder so the parts add up to price exactly (ties
 * to the earlier index). n <= 16. */
acct_status_t acct_allocate(int64_t price, const int64_t *ssp, uint32_t n, int64_t *out);

/* ---- inventory cost formulas (A3) ----
 * Periodic cost of `sold` units from purchase layers (oldest first). FIFO
 * takes the oldest layers first, LIFO the newest; WAVG costs every unit at
 * total cost / total units, cost of sales rounded half up and ending
 * inventory the rest. Under IFRS and AAOIFI LIFO is refused (IAS 2.25). */
typedef struct {
    int64_t units;
    int64_t unit_cost; /* minor units */
} acct_layer_t;
acct_status_t acct_cost_flow(acct_std_t std, const acct_layer_t *layers, uint32_t n, int64_t sold,
                             uint8_t method, int64_t *cost_of_sales, int64_t *ending);

/* ---- murabaha profit (A3) ----
 * FAS 28: time-proportionate recognition; for a deferred-payment murabaha
 * longer than twelve months, the effective profit rate method.
 * acct_murabaha_profit splits markup straight-line over `days` days and
 * returns the part for days [from, to): floor(markup*to/days) -
 * floor(markup*from/days), so consecutive periods add up exactly.
 * acct_murabaha_schedule runs the effective rate method: profit[i] =
 * round(opening * rate) for each instalment, opening = cost less the
 * principal repaid; the last instalment's profit is whatever brings the
 * balance to zero (refused if that is negative). The rate is the caller's
 * (the contract's internal rate of return, as an exact fraction). */
acct_status_t acct_murabaha_profit(int64_t markup, uint32_t days, uint32_t from, uint32_t to,
                                   int64_t *out);
acct_status_t acct_murabaha_schedule(int64_t cost, const int64_t *payment, uint32_t n,
                                     uint64_t rate_num, uint64_t rate_den, int64_t *profit);

/* round(num * scale / den) half away from zero, e.g. scale 1000 gives a
 * rate in tenths of a percent (IAS 12.81(c)(ii) tables). */
bool acct_ratio(int64_t num, int64_t den, int64_t scale, int64_t *out);

/* ---- the tax schedule (A7, A8) ---- */
#define ACCT_TAX_MAX_ITEMS 16u
typedef enum {
    ACCT_TAX_ITEM_BASE = 0,   /* a pre-tax amount, taxed at the rate */
    ACCT_TAX_ITEM_EFFECT = 1  /* a tax effect given directly */
} acct_tax_item_kind_t;
typedef struct {
    const char *label;
    uint8_t kind;   /* acct_tax_item_kind_t */
    int64_t amount; /* signed: + raises tax, - lowers it */
} acct_tax_item_t;
typedef struct {
    int64_t amount; /* signed, minor units */
    bool zakatable; /* true: an asset in the base; false: a deduction */
} acct_zakah_item_t;
typedef struct {
    int64_t profit_before_tax;
    uint64_t rate_num, rate_den; /* statutory rate, e.g. 35/100 */
    const acct_tax_item_t *items;
    uint32_t n_items;
    int64_t reported_tax_expense;
    /* AAOIFI only (ignored elsewhere): */
    const acct_zakah_item_t *zakah_items;
    uint32_t n_zakah_items;
    uint64_t zakah_rate_num, zakah_rate_den;
    int64_t reported_zakah;
} acct_tax_input_t;
typedef struct {
    int64_t tax_at_rate;
    int64_t effect[ACCT_TAX_MAX_ITEMS];
    uint32_t n;
    int64_t expected_tax; /* tax_at_rate + sum of effects */
    int64_t difference;   /* reported - expected */
    bool reconciled;
    /* IAS 12.81(c)(ii): effective rate = reported / profit (den > 0) */
    int64_t eff_rate_num;
    int64_t eff_rate_den;
    /* AAOIFI */
    bool has_zakah;
    int64_t zakah_base;
    int64_t zakah;
    int64_t zakah_difference;
    bool zakah_reconciled;
} acct_tax_recon_t;

acct_status_t acct_reconcile_tax_schedule(acct_std_t std, const acct_tax_input_t *in,
                                          acct_tax_recon_t *out);

/* round(a * num / den), half away from zero; false on overflow or den 0 */
bool acct_mul_rate(int64_t a, uint64_t num, uint64_t den, int64_t *out);

#endif /* ZXV_ACCT_H */
