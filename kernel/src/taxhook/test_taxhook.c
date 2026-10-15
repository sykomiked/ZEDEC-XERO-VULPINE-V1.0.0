/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_taxhook.c — tax-calculation hooks (taxhook.h X1-X8) against worked
 * examples derived by hand, with real ML-DSA-87 signatures.
 *
 * NOT TAX ADVICE. The tables used are the SAMPLE tables of taxhook_sample.c
 * (sample data, dated, a tax adviser signs off per country) and copies of
 * them changed for a test. Each expected value is derived in the comment
 * next to it from the rate and the rounding rule, so the arithmetic can be
 * checked by hand. */
#include "../capctl/cp_test.h"
#include "taxhook.h"
#include "taxhook_sample.h"
#include "../cbank/cb_ccy.h"

#define T0  1767225600ull /* 2026-01-01T00:00:00Z */
#define DAY 86400ull

static cp_t_key_t K_GB, K_DE, K_US, K_CAP;
static cpk_ring_t RING;
static uint8_t M[TAXHOOK_MANIFEST_MAX];
static uint8_t SIG[CPK_SIG_LEN];

static void seal(taxhook_table_t *t, const cp_t_key_t *k)
{
    t->hdr.not_before = T0;
    t->hdr.not_after = T0 + 365 * DAY;
    memcpy(t->hdr.key_id, k->id, CP_KEYID_LEN);
}

static taxhook_status_t install(taxhook_t *h, const taxhook_table_t *t, const cp_t_key_t *k,
                                uint64_t now)
{
    size_t n = taxhook_encode(t, M, sizeof M);
    if (!n) return TAXHOOK_E_DECODE;
    cp_t_sign(k, M, n, SIG);
    return taxhook_install(h, M, n, SIG, sizeof SIG, now);
}

static taxhook_invoice_t inv_of(const taxhook_line_t *l, uint32_t n, uint32_t date)
{
    taxhook_invoice_t v;
    memset(&v, 0, sizeof v);
    v.lines = l;
    v.n_lines = n;
    v.date = date;
    memcpy(v.seller, "GB", 2);
    memcpy(v.buyer, "GB", 2);
    return v;
}

static const char NOREG[CP_REGION_LEN] = {0};
static const char NYREG[CP_REGION_LEN] = {'N', 'Y'};

static void test_round(void)
{
    CHECK(taxhook_round_div(5, 10, TAXHOOK_HALF_UP) == 1 &&
              taxhook_round_div(5, 10, TAXHOOK_HALF_EVEN) == 0,
          "0.5: half-up 1, half-even 0");
    CHECK(taxhook_round_div(15, 10, TAXHOOK_HALF_EVEN) == 2 &&
              taxhook_round_div(25, 10, TAXHOOK_HALF_EVEN) == 2 &&
              taxhook_round_div(25, 10, TAXHOOK_HALF_UP) == 3,
          "1.5 -> 2 and 2.5 -> 2 half-even; 2.5 -> 3 half-up");
    CHECK(taxhook_round_div(14, 10, TAXHOOK_HALF_UP) == 1 &&
              taxhook_round_div(16, 10, TAXHOOK_HALF_EVEN) == 2 &&
              taxhook_round_div(20, 10, TAXHOOK_HALF_EVEN) == 2,
          "below and above a half; exact quotient");
    CHECK(taxhook_round_div(UINT64_MAX, 2, TAXHOOK_HALF_UP) == (1ull << 63) &&
              taxhook_round_div(UINT64_MAX, 2, TAXHOOK_HALF_EVEN) == (1ull << 63) &&
              taxhook_round_div(UINT64_MAX - 2, 2, TAXHOOK_HALF_EVEN) == (1ull << 63) - 2,
          "2^64-1 / 2 ties at the top of the range");
    CHECK(taxhook_round_div(7, 0, TAXHOOK_HALF_UP) == 0, "division by zero gives 0");
}

static void test_codec(void)
{
    taxhook_table_t t, q;
    taxhook_sample_de_vat(&t);
    seal(&t, &K_DE);
    size_t n = taxhook_encode(&t, M, sizeof M);
    CHECK(n == CP_HDR_LEN + 6 + TAXHOOK_LABEL_LEN + 1 + 7 * 19, "DE sample encodes to %zu bytes",
          n);
    CHECK(taxhook_decode(M, n, &q) == TAXHOOK_OK && q.n_rates == 7 &&
              q.rate[1].rate_ppm == 160000 && q.rate[1].from == 20200701 &&
              q.rate[1].to == 20201231 &&
              q.flags == (TAXHOOK_F_SAMPLE | TAXHOOK_F_REVERSE_CHARGE) &&
              !memcmp(q.label, "SAMPLE DE USt 2026-10-12", 25),
          "decode round-trips rates, dates, flags and the SAMPLE label");
    uint8_t big[TAXHOOK_MANIFEST_MAX + 1];
    memcpy(big, M, n);
    big[n] = 0;
    CHECK(taxhook_decode(big, n + 1, &q) == TAXHOOK_E_DECODE, "trailing byte rejected");
    bool all = true;
    for (size_t k = 0; k < n; k++)
        if (taxhook_decode(M, k, &q) != TAXHOOK_E_DECODE) all = false;
    CHECK(all, "every truncation rejected");

    for (int k = 0; k < 14; k++) {
        taxhook_table_t x = t;
        const char *what = "";
        switch (k) {
        case 0:
            what = "overlapping date ranges";
            x.rate[1].from = 20200630;
            break;
        case 1:
            what = "open range overlapping a later one";
            x.rate[0].to = 0;
            break;
        case 2:
            what = "ZERO category with a rate";
            x.rate[6].rate_ppm = 1;
            break;
        case 3:
            what = "rate above 100%";
            x.rate[2].rate_ppm = 1000001;
            break;
        case 4:
            what = "29 February in a non-leap year";
            x.rate[2].from = 20210229;
            break;
        case 5:
            what = "month 13";
            x.rate[2].from = 20211301;
            break;
        case 6:
            what = "to before from";
            x.rate[1].to = 20200601;
            break;
        case 7:
            what = "EXEMPT given a rate entry";
            x.rate[6].category = TAXHOOK_EXEMPT;
            break;
        case 8:
            what = "unknown flag";
            x.flags |= 0x04;
            break;
        case 9:
            what = "rounding mode 3";
            x.rounding = 3;
            break;
        case 10:
            what = "level 0";
            x.level = 0;
            break;
        case 11:
            what = "control character in the label";
            x.label[3] = '\n';
            break;
        case 12:
            what = "lowercase region";
            x.rate[0].region[0] = 'b';
            break;
        case 13:
            what = "tax kind 4";
            x.kind = 4;
            break;
        }
        CHECK(taxhook_encode(&x, M, sizeof M) == 0, "encode refuses: %s", what);
    }
    taxhook_table_t lp = t;
    lp.rate[6].from = 20200229;
    CHECK(taxhook_encode(&lp, M, sizeof M) != 0, "29 February 2020 (leap year) accepted");
    lp.rate[6].from = 20000229;
    CHECK(taxhook_encode(&lp, M, sizeof M) != 0, "29 February 2000 (400-year rule) accepted");
    lp.rate[6].from = 21000229;
    CHECK(taxhook_encode(&lp, M, sizeof M) == 0, "29 February 2100 (100-year rule) refused");
    taxhook_table_t ny;
    taxhook_sample_us_ny_sales(&ny);
    seal(&ny, &K_US);
    ny.rate[1].from = 20050601; /* same category, different regions: no overlap */
    CHECK(taxhook_encode(&ny, M, sizeof M) != 0, "same dates in different regions are fine");
}

static void test_install(taxhook_t *h)
{
    taxhook_table_t gb, de, ny;
    taxhook_sample_gb_vat(&gb);
    taxhook_sample_de_vat(&de);
    taxhook_sample_us_ny_sales(&ny);
    seal(&gb, &K_GB);
    seal(&de, &K_DE);
    seal(&ny, &K_US);

    taxhook_t off;
    taxhook_init(&off, cpk_verify, &RING);
    CHECK(install(&off, &gb, &K_GB, T0) == TAXHOOK_OK && off.enabled == false,
          "a table installs while the hook stays off");
    taxhook_line_t l1 = {TAXHOOK_STANDARD, 1, 100};
    taxhook_invoice_t v = inv_of(&l1, 1, 20261012);
    taxhook_receipt_t r;
    CHECK(taxhook_compute(&off, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_E_DISABLED &&
              r.tax == 0,
          "off by default: compute returns disabled, computes nothing");

    CHECK(install(h, &de, &K_GB, T0) == TAXHOOK_E_SIG, "GB-scoped key cannot sign DE's table");
    seal(&de, &K_CAP);
    CHECK(install(h, &de, &K_CAP, T0) == TAXHOOK_E_SIG,
          "capctl-scoped key cannot sign a tax table");
    seal(&de, &K_DE);
    size_t n = taxhook_encode(&de, M, sizeof M);
    cp_t_sign(&K_DE, M, n, SIG);
    M[CP_HDR_LEN + 6 + TAXHOOK_LABEL_LEN + 1 + 7] ^= 1; /* a rate byte: still decodes */
    CHECK(taxhook_install(h, M, n, SIG, sizeof SIG, T0) == TAXHOOK_E_SIG,
          "tampered rate that still decodes: signature fails");
    CHECK(install(h, &de, &K_DE, T0 - 1) == TAXHOOK_E_WINDOW, "before not_before refused");
    CHECK(install(h, &gb, &K_GB, T0) == TAXHOOK_OK && install(h, &de, &K_DE, T0) == TAXHOOK_OK &&
              install(h, &ny, &K_US, T0) == TAXHOOK_OK,
          "GB, DE and US-NY sample tables installed (signed by their scoped keys)");
    CHECK(install(h, &gb, &K_GB, T0) == TAXHOOK_E_VERSION, "same version again refused");
    gb.hdr.version = 2;
    gb.rounding = TAXHOOK_HALF_EVEN;
    CHECK(install(h, &gb, &K_GB, T0) == TAXHOOK_OK &&
              taxhook_table(h, "GB", NOREG, TAXHOOK_VAT)->rounding == TAXHOOK_HALF_EVEN,
          "version 2 replaces version 1");
    gb.hdr.version = 1;
    gb.rounding = TAXHOOK_HALF_UP;
    CHECK(install(h, &gb, &K_GB, T0) == TAXHOOK_E_VERSION &&
              taxhook_table(h, "GB", NOREG, TAXHOOK_VAT)->hdr.version == 2,
          "rollback to version 1 refused");
    gb.hdr.version = 3;
    CHECK(install(h, &gb, &K_GB, T0) == TAXHOOK_OK, "version 3 (half-up again) installed");
    CHECK(taxhook_table(h, "US", NOREG, TAXHOOK_SALES) == NULL &&
              taxhook_table(h, "US", NYREG, TAXHOOK_SALES) != NULL &&
              taxhook_table(h, "US", NYREG, TAXHOOK_VAT) == NULL,
          "tables are keyed by country, region and kind");
}

static void test_examples(taxhook_t *h)
{
    taxhook_receipt_t r;
    char txt[1024];
    taxhook_enable(h, true);

    /* E1 GB, prices exclude tax, half-up, per invoice, on 2026-10-12.
     *   standard 3 x 0.99 = 2.97; 2.97 x 20% = 0.594 -> 0.59
     *   reduced 10.00 x 5% = 0.50; zero 4.50 -> 0.00
     *   net 17.47, tax 1.09, gross 18.56 */
    taxhook_line_t e1[3] = {
        {TAXHOOK_STANDARD, 3, 99}, {TAXHOOK_REDUCED, 1, 1000}, {TAXHOOK_ZERO, 1, 450}};
    taxhook_invoice_t v = inv_of(e1, 3, 20261012);
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_OK &&
              r.n_groups == 3 && r.group[0].net == 297 && r.group[0].tax == 59 &&
              r.group[0].gross == 356 && r.group[1].tax == 50 && r.group[2].tax == 0 &&
              r.net == 1747 && r.tax == 109 && r.gross == 1856,
          "E1 GB exclusive: 2.97 @20%% -> 0.59, 10.00 @5%% -> 0.50; total 18.56");
    taxhook_receipt_text(&r, 2, txt, sizeof txt);
    CHECK(!strcmp(txt, "SAMPLE DATA - NOT TAX ADVICE - a tax adviser signs off per country\n"
                       "VAT GB table v3, half-up, per invoice, prices exclude tax\n"
                       "standard 20%: net 2.97 tax 0.59 gross 3.56\n"
                       "reduced 5%: net 10.00 tax 0.50 gross 10.50\n"
                       "zero 0%: net 4.50 tax 0.00 gross 4.50\n"
                       "TOTAL net 17.47 tax 1.09 gross 18.56\n"),
          "E1 receipt text, starting with the SAMPLE warning");
    CHECK(taxhook_receipt_text(&r, 2, txt, 40) == 0 && txt[0] == 0,
          "receipt text refuses a buffer that is too small");

    /* E2 per line vs per invoice: ten lines of 0.02 at 20%.
     *   per line: 0.004 -> 0.00 each, tax 0.00
     *   per invoice: 0.20 x 20% = 0.04 */
    const taxhook_table_t *gb = taxhook_table(h, "GB", NOREG, TAXHOOK_VAT);
    taxhook_table_t pl = *gb;
    pl.level = TAXHOOK_PER_LINE;
    taxhook_line_t e2[10];
    for (int i = 0; i < 10; i++) e2[i] = (taxhook_line_t){TAXHOOK_STANDARD, 1, 2};
    v = inv_of(e2, 10, 20261012);
    CHECK(taxhook_compute_table(&pl, &v, &r) == TAXHOOK_OK && r.tax == 0 && r.gross == 20 &&
              r.n_groups == 1 && r.line[9].tax == 0,
          "E2 per line: ten x 0.004 round to 0.00");
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_OK && r.tax == 4 &&
              r.gross == 24 && r.line[0].tax == 0,
          "E2 per invoice: 0.20 x 20%% = 0.04");

    /* E3 half-up vs half-even at 5%: 0.10 -> 0.005, 0.50 -> 0.025,
     * 0.30 -> 0.015, 0.70 -> 0.035. */
    taxhook_table_t he = *gb;
    he.rounding = TAXHOOK_HALF_EVEN;
    struct {
        uint64_t net, up, even;
    } e3[] = {{10, 1, 0}, {50, 3, 2}, {30, 2, 2}, {70, 4, 4}, {11, 1, 1}, {9, 0, 0}};
    for (size_t i = 0; i < sizeof e3 / sizeof e3[0]; i++) {
        taxhook_line_t l = {TAXHOOK_REDUCED, 1, e3[i].net};
        v = inv_of(&l, 1, 20261012);
        taxhook_receipt_t ru, re;
        CHECK(taxhook_compute_table(gb, &v, &ru) == TAXHOOK_OK &&
                  taxhook_compute_table(&he, &v, &re) == TAXHOOK_OK && ru.tax == e3[i].up &&
                  re.tax == e3[i].even,
              "E3 %llu x 5%%: half-up %llu, half-even %llu", (unsigned long long) e3[i].net,
              (unsigned long long) e3[i].up, (unsigned long long) e3[i].even);
    }

    /* E4 GB, prices include tax: tax = gross x 20/120.
     *   1.20 -> 0.20 exactly, net 1.00; 1.00 -> 0.1666.. -> 0.17, net 0.83
     *   two lines of 1.00: per invoice 2.00 -> 0.333.. -> 0.33 (net 1.67);
     *   per line 0.17 + 0.17 = 0.34 (net 1.66) */
    taxhook_line_t e4a = {TAXHOOK_STANDARD, 1, 120};
    v = inv_of(&e4a, 1, 20261012);
    v.inclusive = true;
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_OK && r.tax == 20 &&
              r.net == 100 && r.gross == 120,
          "E4 inclusive 1.20 @20%%: tax 0.20, net 1.00");
    taxhook_line_t e4b = {TAXHOOK_STANDARD, 2, 100};
    v = inv_of(&e4b, 1, 20261012);
    v.inclusive = true;
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_OK && r.tax == 33 &&
              r.net == 167 && r.gross == 200,
          "E4 inclusive 2 x 1.00 per invoice: tax 0.33, net 1.67");
    taxhook_line_t e4c[2] = {{TAXHOOK_STANDARD, 1, 100}, {TAXHOOK_STANDARD, 1, 100}};
    v = inv_of(e4c, 2, 20261012);
    v.inclusive = true;
    CHECK(taxhook_compute_table(&pl, &v, &r) == TAXHOOK_OK && r.tax == 34 && r.net == 166 &&
              r.gross == 200 && r.line[0].tax == 17,
          "E4 inclusive 1.00 + 1.00 per line: tax 0.34, net 1.66");
    taxhook_receipt_text(&r, 2, txt, sizeof txt);
    CHECK(strstr(txt, "per line, prices include tax\n") &&
              strstr(txt, "standard 20%: net 1.66 tax 0.34 gross 2.00\n"),
          "E4 receipt text says prices include tax");

    /* E5 DE effective dates: 100.00 net standard.
     *   2020-06-30 19% -> 19.00; 2020-07-01 16% -> 16.00;
     *   2020-12-31 16% -> 16.00; 2021-01-01 19% -> 19.00;
     *   reduced on 2020-07-01: 5% -> 5.00; 2006-12-31: no entry. */
    struct {
        uint8_t cat;
        uint32_t date;
        uint64_t tax;
    } e5[] = {{TAXHOOK_STANDARD, 20200630, 1900}, {TAXHOOK_STANDARD, 20200701, 1600},
              {TAXHOOK_STANDARD, 20201231, 1600}, {TAXHOOK_STANDARD, 20210101, 1900},
              {TAXHOOK_REDUCED, 20200701, 500},   {TAXHOOK_REDUCED, 20210101, 700}};
    for (size_t i = 0; i < sizeof e5 / sizeof e5[0]; i++) {
        taxhook_line_t l = {e5[i].cat, 1, 10000};
        v = inv_of(&l, 1, e5[i].date);
        memcpy(v.seller, "DE", 2);
        memcpy(v.buyer, "DE", 2);
        CHECK(taxhook_compute(h, "DE", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_OK &&
                  r.tax == e5[i].tax,
              "E5 DE category %u on %u: tax %llu", e5[i].cat, e5[i].date,
              (unsigned long long) e5[i].tax);
    }
    taxhook_line_t l5 = {TAXHOOK_STANDARD, 1, 10000};
    v = inv_of(&l5, 1, 20061231);
    CHECK(taxhook_compute(h, "DE", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_E_NO_RATE,
          "E5 before the first entry: no_rate (never guessed)");

    /* E6 US-NY sales tax, regions NYC + MCTD: 4% + 4.5% + 0.375% = 8.875%.
     *   100.00 -> 8.875 -> 8.88; 10.00 -> 0.8875 -> 0.89;
     *   state only 10.00 -> 0.40; NYC only (8.5%) -> 0.85;
     *   2009-07-31 (before the 4.5% city rate) 100.00 at 4.375% -> 4.375 -> 4.38;
     *   300.00 -> 26.625: half-up 26.63, half-even 26.62 */
    taxhook_line_t e6 = {TAXHOOK_STANDARD, 1, 10000};
    v = inv_of(&e6, 1, 20261012);
    memcpy(v.seller, "US", 2);
    memcpy(v.buyer, "US", 2);
    memcpy(v.regions[0], "NYC", 3);
    memcpy(v.regions[1], "MCTD", 4);
    CHECK(taxhook_compute(h, "US", NYREG, TAXHOOK_SALES, &v, T0, &r) == TAXHOOK_OK &&
              r.group[0].rate_ppm == 88750 && r.tax == 888 && r.gross == 10888,
          "E6 NYC 100.00 @8.875%%: tax 8.88");
    taxhook_receipt_text(&r, 2, txt, sizeof txt);
    CHECK(strstr(txt, "Sales tax US-NY table v1, half-up, per invoice, prices exclude tax\n") &&
              strstr(txt, "standard 8.875%: net 100.00 tax 8.88 gross 108.88\n"),
          "E6 receipt text names US-NY and 8.875%%");
    e6.unit_price = 1000;
    CHECK(taxhook_compute(h, "US", NYREG, TAXHOOK_SALES, &v, T0, &r) == TAXHOOK_OK && r.tax == 89,
          "E6 NYC 10.00: 0.8875 -> 0.89");
    memset(v.regions, 0, sizeof v.regions);
    CHECK(taxhook_compute(h, "US", NYREG, TAXHOOK_SALES, &v, T0, &r) == TAXHOOK_OK && r.tax == 40,
          "E6 state only 10.00 @4%%: 0.40");
    memcpy(v.regions[2], "NYC", 3);
    CHECK(taxhook_compute(h, "US", NYREG, TAXHOOK_SALES, &v, T0, &r) == TAXHOOK_OK && r.tax == 85,
          "E6 NYC without MCTD (third region slot) 10.00 @8.5%%: 0.85");
    memcpy(v.regions[0], "MCTD", 4);
    e6.unit_price = 10000;
    v.date = 20090731;
    CHECK(taxhook_compute(h, "US", NYREG, TAXHOOK_SALES, &v, T0, &r) == TAXHOOK_OK &&
              r.group[0].rate_ppm == 43750 && r.tax == 438,
          "E6 2009-07-31, before the city rate: 4.375%% -> 4.38");
    v.date = 20261012;
    e6.unit_price = 30000;
    const taxhook_table_t *nyt = taxhook_table(h, "US", NYREG, TAXHOOK_SALES);
    taxhook_table_t nye = *nyt;
    nye.rounding = TAXHOOK_HALF_EVEN;
    CHECK(taxhook_compute_table(nyt, &v, &r) == TAXHOOK_OK && r.tax == 2663 &&
              taxhook_compute_table(&nye, &v, &r) == TAXHOOK_OK && r.tax == 2662,
          "E6 300.00 @8.875%% = 26.625: half-up 26.63, half-even 26.62");

    /* E7 reverse charge (DE table has the flag). */
    taxhook_line_t e7 = {TAXHOOK_STANDARD, 1, 10000};
    v = inv_of(&e7, 1, 20261012);
    memcpy(v.seller, "DE", 2);
    memcpy(v.buyer, "FR", 2);
    v.buyer_business = true;
    CHECK(taxhook_compute(h, "DE", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_OK &&
              r.reverse_charge && r.tax == 0 && r.gross == 10000 && r.group[0].rate_ppm == 0,
          "E7 DE seller, FR business buyer: reverse charge, no tax");
    taxhook_receipt_text(&r, 2, txt, sizeof txt);
    CHECK(strstr(txt, "REVERSE CHARGE: no tax charged; the buyer accounts for it\n") != NULL,
          "E7 receipt says reverse charge");
    v.inclusive = true;
    CHECK(taxhook_compute(h, "DE", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_OK &&
              r.reverse_charge && r.tax == 0 && r.net == 10000,
          "E7 reverse charge treats an inclusive price as net");
    v.inclusive = false;
    v.buyer_business = false;
    CHECK(taxhook_compute(h, "DE", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_OK &&
              !r.reverse_charge && r.tax == 1900,
          "E7 FR consumer buyer: DE VAT 19.00 charged");
    v.buyer_business = true;
    memcpy(v.buyer, "DE", 2);
    CHECK(taxhook_compute(h, "DE", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_OK &&
              !r.reverse_charge && r.tax == 1900,
          "E7 domestic B2B: no reverse charge");
    v = inv_of(&e7, 1, 20261012);
    memcpy(v.seller, "US", 2);
    memcpy(v.buyer, "CA", 2);
    v.buyer_business = true;
    CHECK(taxhook_compute(h, "US", NYREG, TAXHOOK_SALES, &v, T0, &r) == TAXHOOK_OK &&
              !r.reverse_charge && r.tax == 400,
          "E7 sales tax table has no reverse charge");
    taxhook_table_t nyrc = *taxhook_table(h, "US", NYREG, TAXHOOK_SALES);
    nyrc.flags |= TAXHOOK_F_REVERSE_CHARGE;
    CHECK(taxhook_compute_table(&nyrc, &v, &r) == TAXHOOK_OK && !r.reverse_charge && r.tax == 400,
          "E7 reverse charge never applies to sales tax, even with the flag");
    taxhook_table_t gst = nyrc;
    gst.kind = TAXHOOK_GST;
    CHECK(taxhook_compute_table(&gst, &v, &r) == TAXHOOK_OK && r.reverse_charge && r.tax == 0,
          "E7 the same table as GST: reverse charge applies");

    /* E8 exempt lines need no rate entry and carry no tax. */
    taxhook_line_t e8[2] = {{TAXHOOK_EXEMPT, 2, 250}, {TAXHOOK_STANDARD, 1, 1000}};
    v = inv_of(e8, 2, 20261012);
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_OK &&
              r.n_groups == 2 && r.group[0].category == TAXHOOK_EXEMPT && r.group[0].tax == 0 &&
              r.group[0].net == 500 && r.tax == 200 && r.gross == 1700,
          "E8 exempt 5.00 + standard 10.00: tax 2.00");
    taxhook_receipt_text(&r, 2, txt, sizeof txt);
    CHECK(strstr(txt, "exempt 0%: net 5.00 tax 0.00 gross 5.00\n") != NULL, "E8 exempt on receipt");
}

static void test_errors(taxhook_t *h)
{
    taxhook_receipt_t r;
    taxhook_line_t l = {TAXHOOK_SUPER_REDUCED, 1, 100};
    taxhook_invoice_t v = inv_of(&l, 1, 20261012);
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_E_NO_RATE,
          "category with no entry: no_rate");
    l.category = 6;
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_E_ARG, "category 6");
    l = (taxhook_line_t){TAXHOOK_STANDARD, 0, 100};
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_E_ARG, "qty 0");
    l.qty = TAXHOOK_QTY_MAX + 1;
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_E_RANGE,
          "qty 2^20+1");
    l.qty = 1;
    l.unit_price = TAXHOOK_AMOUNT_MAX + 1;
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_E_RANGE,
          "unit price 2^40+1");
    l.qty = TAXHOOK_QTY_MAX;
    l.unit_price = (1u << 20) + 1;
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_E_RANGE,
          "line amount just over 2^40");
    l.unit_price = 1u << 20;
    /* 2^40 x 20% = 219902325555.2 -> 219902325555 */
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_OK &&
              r.tax == 219902325555ull && r.gross == (1ull << 40) + 219902325555ull,
          "line amount exactly 2^40: exact tax at the top of the range");
    taxhook_line_t two[2] = {{TAXHOOK_STANDARD, 1, 1ull << 40}, {TAXHOOK_STANDARD, 1, 1}};
    v = inv_of(two, 2, 20261012);
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_E_RANGE,
          "group amount over 2^40");
    taxhook_line_t many[TAXHOOK_MAX_LINES + 1];
    for (unsigned i = 0; i <= TAXHOOK_MAX_LINES; i++)
        many[i] = (taxhook_line_t){TAXHOOK_ZERO, 1, 1};
    v = inv_of(many, TAXHOOK_MAX_LINES + 1, 20261012);
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_E_RANGE, "65 lines");
    v.n_lines = TAXHOOK_MAX_LINES;
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_OK && r.net == 64,
          "64 lines accepted");
    l = (taxhook_line_t){TAXHOOK_STANDARD, 1, 100};
    v = inv_of(&l, 1, 20260230);
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_E_ARG,
          "tax point 30 February: bad_arg");
    v = inv_of(&l, 1, 20261012);
    memcpy(v.buyer, "gb", 2);
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_E_ARG,
          "lowercase buyer country");
    v = inv_of(&l, 1, 20261012);
    v.n_lines = 0;
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_E_ARG, "no lines");
    v.n_lines = 1;
    v.regions[0][0] = 'n';
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_E_ARG,
          "malformed invoice region");
    v.regions[0][0] = 0;
    CHECK(taxhook_compute(h, "FR", NOREG, TAXHOOK_VAT, &v, T0, &r) == TAXHOOK_E_NO_TABLE,
          "no table for FR");
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0 + 365 * DAY + 1, &r) ==
              TAXHOOK_E_EXPIRED,
          "table past its validity window: expired (fail closed)");
    CHECK(taxhook_compute(h, "GB", NOREG, TAXHOOK_VAT, &v, T0 + 365 * DAY, &r) == TAXHOOK_OK,
          "at exactly not_after: still valid");
    CHECK(!strcmp(taxhook_status_name(TAXHOOK_E_NO_RATE), "no_rate") &&
              !strcmp(taxhook_status_name((taxhook_status_t) 99), "unknown"),
          "status names");
}

int main(void)
{
    printf("=== test_taxhook: tax tables (SAMPLE DATA, NOT TAX ADVICE), ML-DSA-87 ===\n");
    cp_t_keygen(&K_GB, 11);
    cp_t_keygen(&K_DE, 12);
    cp_t_keygen(&K_US, 13);
    cp_t_keygen(&K_CAP, 14);
    cpk_init(&RING);
    CHECK(cpk_add(&RING, K_GB.pk, 1u << CP_KIND_TAXTABLE, "GB") &&
              cpk_add(&RING, K_DE.pk, 1u << CP_KIND_TAXTABLE, "DE") &&
              cpk_add(&RING, K_US.pk, 1u << CP_KIND_TAXTABLE, "US") &&
              cpk_add(&RING, K_CAP.pk, 1u << CP_KIND_CAPCTL, "**"),
          "keyring: three tax keys scoped to one country each, one capctl key");
    const cb_ccy_t *gbp = cb_ccy_by_alpha("GBP"), *eur = cb_ccy_by_alpha("EUR"),
                   *usd = cb_ccy_by_alpha("USD");
    taxhook_table_t s;
    taxhook_sample_gb_vat(&s);
    bool ok =
        gbp && s.ccy == gbp->num && (s.flags & TAXHOOK_F_SAMPLE) && !memcmp(s.label, "SAMPLE", 6);
    taxhook_sample_de_vat(&s);
    ok = ok && eur && s.ccy == eur->num && (s.flags & TAXHOOK_F_SAMPLE) &&
         !memcmp(s.label, "SAMPLE", 6);
    taxhook_sample_us_ny_sales(&s);
    ok = ok && usd && s.ccy == usd->num && (s.flags & TAXHOOK_F_SAMPLE) &&
         !memcmp(s.label, "SAMPLE", 6) && cb_country_by_a2("US") && cb_country_by_a2("DE") &&
         cb_country_by_a2("GB");
    CHECK(ok, "sample tables: SAMPLE flag and label; currencies match the cbank ISO 4217 table");
    test_round();
    test_codec();
    static taxhook_t h;
    taxhook_init(&h, cpk_verify, &RING);
    test_install(&h);
    test_examples(&h);
    test_errors(&h);
    return cp_test_done("test_taxhook");
}
