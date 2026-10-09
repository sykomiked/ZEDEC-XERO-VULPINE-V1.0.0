/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_cb_ccy.c — ISO 4217 / ISO 3166 / AU table tests. */
#include "cb_ccy.h"
#include "cb_util.h"
#include "cb_test.h"

/* Every AU member: alpha-2, alpha-3, ISO 3166 numeric, and one currency it
 * must carry (written independently of the generator from ISO 4217 and
 * ISO 3166 as the test author knows them). */
static const struct {
    const char *a2, *a3;
    unsigned num;
    const char *ccy;
    unsigned ccynum;
    unsigned minor;
} au[] = {
    {"DZ", "DZA", 12, "DZD", 12, 2},   {"AO", "AGO", 24, "AOA", 973, 2},
    {"BJ", "BEN", 204, "XOF", 952, 0}, {"BW", "BWA", 72, "BWP", 72, 2},
    {"BF", "BFA", 854, "XOF", 952, 0}, {"BI", "BDI", 108, "BIF", 108, 0},
    {"CV", "CPV", 132, "CVE", 132, 2}, {"CM", "CMR", 120, "XAF", 950, 0},
    {"CF", "CAF", 140, "XAF", 950, 0}, {"TD", "TCD", 148, "XAF", 950, 0},
    {"KM", "COM", 174, "KMF", 174, 0}, {"CG", "COG", 178, "XAF", 950, 0},
    {"CD", "COD", 180, "CDF", 976, 2}, {"CI", "CIV", 384, "XOF", 952, 0},
    {"DJ", "DJI", 262, "DJF", 262, 0}, {"EG", "EGY", 818, "EGP", 818, 2},
    {"GQ", "GNQ", 226, "XAF", 950, 0}, {"ER", "ERI", 232, "ERN", 232, 2},
    {"SZ", "SWZ", 748, "SZL", 748, 2}, {"ET", "ETH", 231, "ETB", 230, 2},
    {"GA", "GAB", 266, "XAF", 950, 0}, {"GM", "GMB", 270, "GMD", 270, 2},
    {"GH", "GHA", 288, "GHS", 936, 2}, {"GN", "GIN", 324, "GNF", 324, 0},
    {"GW", "GNB", 624, "XOF", 952, 0}, {"KE", "KEN", 404, "KES", 404, 2},
    {"LS", "LSO", 426, "LSL", 426, 2}, {"LR", "LBR", 430, "LRD", 430, 2},
    {"LY", "LBY", 434, "LYD", 434, 3}, {"MG", "MDG", 450, "MGA", 969, 2},
    {"MW", "MWI", 454, "MWK", 454, 2}, {"ML", "MLI", 466, "XOF", 952, 0},
    {"MR", "MRT", 478, "MRU", 929, 2}, {"MU", "MUS", 480, "MUR", 480, 2},
    {"MA", "MAR", 504, "MAD", 504, 2}, {"MZ", "MOZ", 508, "MZN", 943, 2},
    {"NA", "NAM", 516, "NAD", 516, 2}, {"NE", "NER", 562, "XOF", 952, 0},
    {"NG", "NGA", 566, "NGN", 566, 2}, {"RW", "RWA", 646, "RWF", 646, 0},
    {"EH", "ESH", 732, "MAD", 504, 2}, {"ST", "STP", 678, "STN", 930, 2},
    {"SN", "SEN", 686, "XOF", 952, 0}, {"SC", "SYC", 690, "SCR", 690, 2},
    {"SL", "SLE", 694, "SLE", 925, 2}, {"SO", "SOM", 706, "SOS", 706, 2},
    {"ZA", "ZAF", 710, "ZAR", 710, 2}, {"SS", "SSD", 728, "SSP", 728, 2},
    {"SD", "SDN", 729, "SDG", 938, 2}, {"TZ", "TZA", 834, "TZS", 834, 2},
    {"TG", "TGO", 768, "XOF", 952, 0}, {"TN", "TUN", 788, "TND", 788, 3},
    {"UG", "UGA", 800, "UGX", 800, 0}, {"ZM", "ZMB", 894, "ZMW", 967, 2},
    {"ZW", "ZWE", 716, "ZWG", 924, 2},
};

int main(void)
{
    char nm[96];
    unsigned n_au = (unsigned) (sizeof au / sizeof au[0]);
    CHECK(n_au == 55 && CB_AU_COUNT == 55, "55 AU member states in table and test");
    for (unsigned i = 0; i < n_au; i++) {
        const cb_country_t *c = cb_country_by_a2(au[i].a2);
        const cb_ccy_t *k = cb_ccy_by_alpha(au[i].ccy);
        bool ok = c && strcmp(c->a3, au[i].a3) == 0 && c->num == au[i].num && k &&
                  k->num == au[i].ccynum && k->minor == au[i].minor &&
                  cb_ccy_used_in(au[i].a2, au[i].ccy) && cb_is_au_member(au[i].a2) &&
                  cb_country_by_a3(au[i].a3) == c && cb_country_by_num((uint16_t) au[i].num) == c &&
                  cb_ccy_by_num((uint16_t) au[i].ccynum) == k && cb_ccy_payable(k);
        snprintf(nm, sizeof nm, "AU %s/%s/%03u uses %s (%03u, %u dp)", au[i].a2, au[i].a3,
                 au[i].num, au[i].ccy, au[i].ccynum, au[i].minor);
        CHECK(ok, nm);
    }
    /* Every row of the generated AU table is in the test list too. */
    unsigned seen = 0;
    for (unsigned i = 0; i < CB_AU_COUNT; i++)
        for (unsigned j = 0; j < n_au; j++)
            if (strcmp(cb_au_tbl[i].a2, au[j].a2) == 0) seen++;
    CHECK(seen == 55, "generated AU table matches the independent list");

    /* Regions: 7 North, 15 West, 9 Central, 14 East, 10 Southern. */
    unsigned r[6] = {0};
    for (unsigned i = 0; i < CB_AU_COUNT; i++) r[cb_au_tbl[i].region]++;
    CHECK(r[CB_AU_NORTH] == 7 && r[CB_AU_WEST] == 15 && r[CB_AU_CENTRAL] == 9 &&
              r[CB_AU_EAST] == 14 && r[CB_AU_SOUTHERN] == 10,
          "AU five regions sum 7+15+9+14+10");
    unsigned waemu = 0, cemac = 0;
    for (unsigned i = 0; i < CB_AU_COUNT; i++) {
        if (cb_au_tbl[i].mu == CB_MU_WAEMU) waemu += cb_ccy_used_in(cb_au_tbl[i].a2, "XOF");
        if (cb_au_tbl[i].mu == CB_MU_CEMAC) cemac += cb_ccy_used_in(cb_au_tbl[i].a2, "XAF");
    }
    CHECK(waemu == 8, "WAEMU: 8 members, all XOF");
    CHECK(cemac == 6, "CEMAC: 6 members, all XAF");
    CHECK(cb_ccy_used_in("LS", "ZAR") && cb_ccy_used_in("NA", "ZAR"),
          "CMA: ZAR also listed for Lesotho and Namibia");
    const cb_ccy_t *two[4];
    CHECK(cb_ccy_for_country("LS", two, 4) == 2, "Lesotho has two currencies in list one");
    CHECK(!cb_is_au_member("FR") && !cb_is_au_member("US") && !cb_is_au_member("xx"),
          "non-members are not AU members");

    /* Table integrity: sorted, unique alpha and numeric. */
    bool sorted = true, uniq = true;
    for (unsigned i = 1; i < CB_CCY_COUNT; i++) {
        if (strcmp(cb_ccy_tbl[i - 1].alpha, cb_ccy_tbl[i].alpha) >= 0) sorted = false;
        if (cb_ccy_tbl[cb_ccy_by_num_idx[i - 1]].num >= cb_ccy_tbl[cb_ccy_by_num_idx[i]].num)
            uniq = false;
    }
    CHECK(sorted, "currency table strictly sorted by alpha");
    CHECK(uniq, "numeric codes unique and index sorted");
    bool rt = true;
    for (unsigned i = 0; i < CB_CCY_COUNT; i++) {
        if (cb_ccy_by_alpha(cb_ccy_tbl[i].alpha) != &cb_ccy_tbl[i]) rt = false;
        if (cb_ccy_by_num(cb_ccy_tbl[i].num) != &cb_ccy_tbl[i]) rt = false;
    }
    CHECK(rt, "every currency found by alpha and by numeric");
    bool crt = true;
    for (unsigned i = 0; i < CB_COUNTRY_COUNT; i++)
        if (cb_country_by_a2(cb_country_tbl[i].a2) != &cb_country_tbl[i]) crt = false;
    CHECK(crt, "every country found by alpha-2");

    /* Spot checks across the world and the special codes. */
    const cb_ccy_t *c;
    c = cb_ccy_by_alpha("USD");
    CHECK(c && c->num == 840 && c->minor == 2, "USD 840, 2 dp");
    c = cb_ccy_by_alpha("JPY");
    CHECK(c && c->num == 392 && c->minor == 0, "JPY 392, 0 dp");
    c = cb_ccy_by_alpha("KWD");
    CHECK(c && c->minor == 3, "KWD 3 dp");
    c = cb_ccy_by_alpha("CLF");
    CHECK(c && c->minor == 4 && (c->flags & CB_CCYF_FUND), "CLF 4 dp, fund code");
    c = cb_ccy_by_alpha("XAU");
    CHECK(c && (c->flags & CB_CCYF_NA) && !cb_ccy_payable(c), "XAU minor N.A., not payable");
    c = cb_ccy_by_alpha("XUA");
    CHECK(c && c->num == 965 && (c->flags & CB_CCYF_NOCTRY), "XUA (AfDB unit of account) 965");
    CHECK(cb_ccy_by_alpha("XDR") && !cb_ccy_payable(cb_ccy_by_alpha("XXX")) &&
              !cb_ccy_payable(cb_ccy_by_alpha("XTS")),
          "XDR present; XXX and XTS not payable");
    CHECK(cb_ccy_by_alpha("XCG") && !cb_ccy_by_alpha("ANG"), "XCG replaced ANG");
    CHECK(cb_ccy_by_alpha("SLE") && !cb_ccy_by_alpha("SLL"), "SLE current, SLL withdrawn");
    CHECK(cb_ccy_by_alpha("ZWG") && !cb_ccy_by_alpha("ZWL"), "ZWG current, ZWL withdrawn");
    CHECK(cb_ccy_used_in("BG", "EUR") && !cb_ccy_by_alpha("BGN"), "Bulgaria on EUR (2026 list)");

    /* VFV and the Vino rails are not ISO 4217. */
    CHECK(cb_ccy_by_alpha("VFV") == 0, "VFV is not an ISO 4217 code");
    CHECK(!cb_ccy_by_num(846) && !cb_ccy_by_num(810) && !cb_ccy_by_num(888),
          "rail numerics 846/810/888 are not active ISO 4217 codes");
    CHECK(!cb_ccy_by_alpha("usd") && !cb_ccy_by_alpha("US") && !cb_ccy_by_alpha("USDX") &&
              !cb_ccy_by_alpha(0),
          "malformed alpha codes rejected");
    CHECK(cb_country_by_a2("NC") && strcmp(cb_country_by_a2("NC")->a3, "NCL") == 0,
          "NC is New Caledonia (never a VFV country)");
    CHECK(strcmp(cb_iso4217_published, "2026-01-01") == 0, "list one publication date recorded");
    CB_TEST_DONE("test_cb_ccy");
}
