/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Fairground's languages against the C its model runs (fairground_rules.h).
 * Slots: @num.fairground_assurance_fee, @logic.fairground_offer_allowed,
 * @num.fairground_offer_unit_price, @logic.fairground_offer_rec_ok, and
 * Coffer's @num.coffer_balance for the wallet after a settlement. */
#include "fairground_rules.h"

/* one witnessed call that must be refused: no rows answered */
static bool fgt_refused(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t nargs)
{
    zxapp_num_t out[WAVE_ROWS];
    uint32_t n = 0;
    int32_t lp = -1;
    return zxapp_call_witnessed(a, &H, entry, args, nargs, out, WAVE_ROWS, &n, &lp) != ZXAPP_OK;
}

/* offer_unit_price over rows: one answer, the C's reduced fraction */
static bool fgt_unit(zxapp_t *a, const int64_t *price, const int64_t *units, uint32_t rows)
{
    zxapp_arg_t ua[2] = {{"price", price, rows}, {"units", units, rows}};
    zxapp_num_t o[WAVE_ROWS];
    uint32_t k = 0;
    int32_t lp = -1;
    int64_t num = 0, den = 0;
    int rc = zxapp_call_witnessed(a, &H, "offer_unit_price", ua, 2, o, WAVE_ROWS, &k, &lp);
    if (rc != ZXAPP_OK || k != 1 || !fairground_unit_price(price, units, rows, &num, &den)) {
        printf("    offer_unit_price: rc %d n %u (%s)\n", rc, k, a->err);
        return false;
    }
    if (o[0].num != num || o[0].den != den) {
        printf("    offer_unit_price: %lld/%lld, want %lld/%lld\n", (long long) o[0].num,
               (long long) o[0].den, (long long) num, (long long) den);
        return false;
    }
    return true;
}

APP_TEST(fairground, 11, 79)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], want[WAVE_ROWS];

    /* assurance_fee (FR1): Sutra = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        a[i] = wrand(0, i < 32 ? INT64_C(100000000) : FAIRGROUND_FEE_WITNESSED);
    a[0] = 0;                        /* nothing sold */
    a[1] = FAIRGROUND_FEE_WITNESSED; /* the max both hold */
    a[2] = 1124;                     /* the most that pays no fee */
    a[3] = 1125;                     /* the least that pays 1 */
    a[4] = 10000000;                 /* exactly 8889 */
    a[5] = 180000;                   /* 160 (royalty.h R7's worked case) */
    a[6] = 9999999;                  /* 10^7 - 1: the low part at its most */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = fairground_assurance_fee(a[i]);
    zxapp_arg_t fa = {"amount", a, WAVE_ROWS};
    CHECK(wave_same(app, "assurance_fee", &fa, 1, want, WAVE_ROWS, false),
          "Fairground: assurance_fee, Sutra = Trebago = C");
    {
        /* max + 1: past Sutra's 9(15), refused there; Trebago answers */
        static const int64_t x[2] = {1, FAIRGROUND_FEE_WITNESSED + 1};
        zxapp_arg_t xa = {"amount", x, 2};
        CHECK(wave_lp(app, "assurance_fee", &xa, 1) == ZXAPP_BOTH,
              "Fairground: an amount past 9(15): Sutra refuses, Trebago answers: BOTH");
        /* 2^53 - 1, the most zxvd posts: still only Trebago's */
        static const int64_t y[2] = {1, FAIRGROUND_AMOUNT_MAX};
        zxapp_arg_t ya = {"amount", y, 2};
        CHECK(wave_lp(app, "assurance_fee", &ya, 1) == ZXAPP_BOTH,
              "Fairground: an amount of 2^53 - 1: BOTH");
        /* a negative amount and one past 2^53 - 1: both refuse */
        static const int64_t n[2] = {1, -1}, m[2] = {1, FAIRGROUND_AMOUNT_MAX + 1};
        zxapp_arg_t na = {"amount", n, 2}, ma = {"amount", m, 2};
        CHECK(wave_lp(app, "assurance_fee", &na, 1) == ZXAPP_NEITHER &&
                  fgt_refused(app, "assurance_fee", &na, 1),
              "Fairground: a negative amount: both refuse: NEITHER");
        CHECK(wave_lp(app, "assurance_fee", &ma, 1) == ZXAPP_NEITHER,
              "Fairground: an amount past 2^53 - 1: both refuse: NEITHER");
        CHECK(fairground_assurance_fee(FAIRGROUND_AMOUNT_MAX) == INT64_C(8006499417539) &&
                  fairground_assurance_fee(-1) == FAIRGROUND_REFUSED &&
                  fairground_assurance_fee(FAIRGROUND_AMOUNT_MAX + 1) == FAIRGROUND_REFUSED,
              "Fairground: the C's fee at its edges");
    }

    /* offer_allowed (FR2): Eve = Sutra = C, every value inside 9 digits */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(i < 32 ? -5 : 0, i < 32 ? 2000 : FAIRGROUND_NINE);
        b[i] = wrand(i < 32 ? -5 : 0, i < 32 ? 100 : FAIRGROUND_NINE);
        c[i] = wrand(-50, i < 32 ? 2000 : FAIRGROUND_NINE);
        d[i] = wrand(-50, i < 32 ? 100 : FAIRGROUND_NINE);
    }
    a[0] = 1, b[0] = 1, c[0] = 0, d[0] = 0; /* every least that is allowed */
    a[1] = FAIRGROUND_NINE, b[1] = FAIRGROUND_NINE, c[1] = FAIRGROUND_NINE;
    d[1] = FAIRGROUND_NINE;                                /* every max */
    a[2] = 0, b[2] = 1, c[2] = 0, d[2] = 0;                /* no price */
    a[3] = 1, b[3] = 0, c[3] = 0, d[3] = 0;                /* no units */
    a[4] = 5, b[4] = 1, c[4] = -1, d[4] = 0;               /* past the owner's price limit */
    a[5] = 5, b[5] = 1, c[5] = 0, d[5] = -1;               /* more units than the owner's most */
    a[6] = -FAIRGROUND_NINE, b[6] = 1, c[6] = 0, d[6] = 0; /* negative, inside 9 digits */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(fairground_offer_allowed(a[i], b[i], c[i], d[i]));
    zxapp_arg_t oa[4] = {{"price", a, WAVE_ROWS},
                         {"units", b, WAVE_ROWS},
                         {"priceroom", c, WAVE_ROWS},
                         {"unitsroom", d, WAVE_ROWS}};
    CHECK(wave_same(app, "offer_allowed", oa, 4, want, WAVE_ROWS, true),
          "Fairground: offer_allowed, Eve = Sutra = C");
    {
        /* max + 1 in each field: Eve answers no, Sutra refuses: BOTH */
        static const int64_t one[2] = {1, 1}, zero[2] = {0, 0}, big[2] = {1, FAIRGROUND_NINE + 1},
                             bigr[2] = {0, FAIRGROUND_NINE + 1}, neg[2] = {0, -FAIRGROUND_NINE - 1};
        zxapp_arg_t pa[4] = {
            {"price", big, 2}, {"units", one, 2}, {"priceroom", zero, 2}, {"unitsroom", zero, 2}};
        zxapp_arg_t ua[4] = {
            {"price", one, 2}, {"units", big, 2}, {"priceroom", zero, 2}, {"unitsroom", zero, 2}};
        zxapp_arg_t ra[4] = {
            {"price", one, 2}, {"units", one, 2}, {"priceroom", bigr, 2}, {"unitsroom", zero, 2}};
        zxapp_arg_t sa[4] = {
            {"price", one, 2}, {"units", one, 2}, {"priceroom", zero, 2}, {"unitsroom", bigr, 2}};
        zxapp_arg_t na[4] = {
            {"price", one, 2}, {"units", one, 2}, {"priceroom", neg, 2}, {"unitsroom", zero, 2}};
        CHECK(wave_lp(app, "offer_allowed", pa, 4) == ZXAPP_BOTH,
              "Fairground: a price past 9 digits: Eve says no, Sutra refuses: BOTH");
        CHECK(wave_lp(app, "offer_allowed", ua, 4) == ZXAPP_BOTH,
              "Fairground: units past 9 digits: BOTH");
        CHECK(wave_lp(app, "offer_allowed", ra, 4) == ZXAPP_BOTH,
              "Fairground: a price room past 9 digits: BOTH");
        CHECK(wave_lp(app, "offer_allowed", sa, 4) == ZXAPP_BOTH,
              "Fairground: a units room past 9 digits: BOTH");
        CHECK(wave_lp(app, "offer_allowed", na, 4) == ZXAPP_BOTH,
              "Fairground: a room below -999999999: BOTH");
    }

    /* offer_unit_price (FR3): Trebago = C, exact fractions */
    for (uint32_t r = 0; r < 8u; r++) {
        uint32_t rows = 2u + r * 8u;
        for (uint32_t i = 0; i < rows; i++) {
            a[i] = wrand(0, r < 4 ? 5000 : INT64_C(999999999999));
            b[i] = wrand(1, r < 4 ? 40 : FAIRGROUND_NINE);
        }
        CHECK(fgt_unit(app, a, b, rows), "Fairground: offer_unit_price over %u offers, Trebago = C",
              rows);
    }
    {
        static const int64_t p[2] = {300, 400}, u[2] = {2, 4}, z[2] = {0, 0}, one[2] = {1, 1},
                             s[2] = {7, 0}, max[2] = {INT64_MAX, 0};
        int64_t num = 0, den = 0;
        CHECK(fgt_unit(app, p, u, 2) && fgt_unit(app, z, one, 2) && fgt_unit(app, s, one, 2) &&
                  fgt_unit(app, max, one, 2) && fairground_unit_price(p, u, 2, &num, &den) &&
                  num == 350 && den == 3,
              "Fairground: 700 over 6 units is 350/3; nothing is 0; the most price");
        static const int64_t np[2] = {5, -1}, nu[2] = {1, 0}, over[2] = {INT64_MAX, 1};
        zxapp_arg_t na[2] = {{"price", np, 2}, {"units", one, 2}};
        zxapp_arg_t ua[2] = {{"price", p, 2}, {"units", nu, 2}};
        zxapp_arg_t oa2[2] = {{"price", over, 2}, {"units", one, 2}};
        CHECK(fgt_refused(app, "offer_unit_price", na, 2) &&
                  !fairground_unit_price(np, one, 2, &num, &den),
              "Fairground: a negative price is refused");
        CHECK(fgt_refused(app, "offer_unit_price", ua, 2) &&
                  !fairground_unit_price(p, nu, 2, &num, &den),
              "Fairground: an offer of 0 units is refused");
        CHECK(fgt_refused(app, "offer_unit_price", oa2, 2) &&
                  !fairground_unit_price(over, one, 2, &num, &den),
              "Fairground: a price sum past 64 bits is refused, never wrapped");
    }

    /* offer_rec_ok (FR4): Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, i < 32 ? 9999 : 10002);
        b[i] = wrand(0, 4);
        c[i] = wrand(0, 3);
        d[i] = wrand(-2, i < 32 ? 100000 : FAIRGROUND_MINOR_MAX);
        e[i] = wrand(-2, i < 32 ? 100000 : FAIRGROUND_MINOR_MAX);
        f[i] = wrand(-1, 4);
    }
    a[0] = 0, b[0] = 1, c[0] = 1, d[0] = 0, e[0] = 0, f[0] = 0; /* every least */
    a[1] = FAIRGROUND_OFFER_MAX, b[1] = 3, c[1] = 2, d[1] = FAIRGROUND_MINOR_MAX;
    e[1] = FAIRGROUND_MINOR_MAX, f[1] = 3; /* every max */
    a[2] = FAIRGROUND_OFFER_MAX + 1, b[2] = 1, c[2] = 1, d[2] = 0, e[2] = 0, f[2] = 0;
    a[3] = 0, b[3] = 4, c[3] = 1, d[3] = 0, e[3] = 0, f[3] = 0;  /* kind max + 1 */
    a[4] = 0, b[4] = 1, c[4] = 3, d[4] = 0, e[4] = 0, f[4] = 0;  /* resource max + 1 */
    a[5] = 0, b[5] = 1, c[5] = 1, d[5] = 0, e[5] = 0, f[5] = 4;  /* status max + 1 */
    a[6] = -1, b[6] = 1, c[6] = 1, d[6] = 0, e[6] = 0, f[6] = 0; /* a negative offer */
    a[7] = 0, b[7] = 1, c[7] = 1, d[7] = -1, e[7] = 0, f[7] = 0; /* negative units */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(fairground_record_fits(a[i], b[i], c[i], d[i], e[i], f[i]));
    zxapp_arg_t ra2[6] = {{"offer", a, WAVE_ROWS},    {"kind", b, WAVE_ROWS},
                          {"resource", c, WAVE_ROWS}, {"units", d, WAVE_ROWS},
                          {"price", e, WAVE_ROWS},    {"status", f, WAVE_ROWS}};
    CHECK(wave_same(app, "offer_rec_ok", ra2, 6, want, WAVE_ROWS, true),
          "Fairground: offer_rec_ok, Sutra = C");
    {
        /* past 18 digits: Sutra refuses the record outright */
        static const int64_t z[2] = {0, 0}, one[2] = {1, 1}, big[2] = {0, FAIRGROUND_MINOR_MAX + 1};
        zxapp_arg_t ba[6] = {{"offer", z, 2},   {"kind", one, 2}, {"resource", one, 2},
                             {"units", big, 2}, {"price", z, 2},  {"status", z, 2}};
        CHECK(fgt_refused(app, "offer_rec_ok", ba, 6) &&
                  !fairground_record_fits(0, 1, 1, FAIRGROUND_MINOR_MAX + 1, 0, 0),
              "Fairground: units past 18 digits are refused");
    }

    /* FR5: the fee's four parts sum to the fee */
    {
        int64_t p[FAIRGROUND_PARTS];
        CHECK(fairground_fee_parts(8889, p) && p[0] == 4446 && p[1] == 2222 && p[2] == 1333 &&
                  p[3] == 888 && fairground_fee_parts(1, p) && p[0] == 1 && p[1] == 0 &&
                  !fairground_fee_parts(-1, p),
              "Fairground: 8889 split 4446, 2222, 1333, 888");
    }

    /* coffer.balance, used for the wallet after a settlement: Sutra = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, INT64_C(999999999999));
        b[i] = wrand(1, FAIRGROUND_NINE);
        b[i] = i % 2 ? b[i] : -(b[i] + fairground_assurance_fee(b[i])); /* sold, or bought */
        want[i] = a[i] + b[i];
    }
    zxapp_arg_t ca[2] = {{"before", a, WAVE_ROWS}, {"moved", b, WAVE_ROWS}};
    CHECK(wave_same(app, "coffer.balance", ca, 2, want, WAVE_ROWS, false),
          "Fairground: the wallet after a settlement, through coffer.balance");
}
