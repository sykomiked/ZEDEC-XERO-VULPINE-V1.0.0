/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Vintner's languages, witnessed, row by row against the rules of
 * vintner_model.c (written out here, since this file cannot include the
 * model's header: it reaches zxm.h). Slots: @num.vintner_sale_value,
 * @num.vintner_flow_gap, @num.vintner_probe_tenths, @logic.vintner_lot_ok;
 * Coffer's @num.coffer_balance is used for the revenue. */

#define VN_NINE INT64_C(999999999) /* VINTNER_NINE */

static int64_t vn_tenths(int64_t raw) /* vintner_probe_tenths, inside the range */
{
    int64_t t = raw * 10, q = t / 16;
    return t % 16 != 0 && t < 0 ? q - 1 : q;
}

static bool vn_digits(int64_t v, int64_t max)
{
    return v >= -max && v <= max;
}

static bool vn_lot_fits(int64_t lot, int64_t vintage, int64_t stage, int64_t bottles,
                        int64_t volume) /* vintner_lot_fits */
{
    return vn_digits(lot, VN_NINE) && vn_digits(vintage, 9999) && vn_digits(stage, 9) &&
           vn_digits(bottles, VN_NINE) && vn_digits(volume, INT64_C(999999999999));
}

/* the call's own status: a single-language entry is NEITHER whether it
 * answers or refuses, so a refusal is read from the call itself */
static int vn_rc(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t nargs)
{
    zxapp_num_t out[WAVE_ROWS];
    uint32_t n = 0;
    int32_t lp = -1;
    return zxapp_call_witnessed(a, &H, entry, args, nargs, out, WAVE_ROWS, &n, &lp);
}

APP_TEST(vintner, 11, 91)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* sale_value: bottles * price, Sutra = C++ (wasm) = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, i < 32 ? 5000 : VN_NINE);
        b[i] = wrand(0, i < 32 ? 100000 : VN_NINE);
    }
    a[0] = 0, b[0] = 0;             /* nothing sold */
    a[1] = VN_NINE, b[1] = VN_NINE; /* every max: 18 digits */
    a[2] = 1, b[2] = 1;             /* the least sale */
    a[3] = 0, b[3] = VN_NINE;       /* none at the dearest price */
    a[4] = 12, b[4] = 2499;         /* a case of twelve */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = a[i] * b[i];
    zxapp_arg_t sa[2] = {{"bottles", a, WAVE_ROWS}, {"price", b, WAVE_ROWS}};
    CHECK(wave_same(app, "sale_value", sa, 2, want, WAVE_ROWS, false),
          "Vintner: sale_value, Sutra = C++ = C");
    {
        /* bottles max + 1: past Sutra's 9(9), refused there; C++ answers */
        static const int64_t x[2] = {1, VN_NINE + 1}, p[2] = {1, 1};
        zxapp_arg_t xa[2] = {{"bottles", x, 2}, {"price", p, 2}};
        CHECK(wave_lp(app, "sale_value", xa, 2) == ZXAPP_BOTH,
              "Vintner: bottles past 9(9): Sutra refuses, C++ answers: BOTH");
        /* price max + 1 too */
        static const int64_t q[2] = {1, VN_NINE + 1};
        zxapp_arg_t qa[2] = {{"bottles", p, 2}, {"price", q, 2}};
        CHECK(wave_lp(app, "sale_value", qa, 2) == ZXAPP_BOTH,
              "Vintner: price past 9(9): Sutra refuses, C++ answers: BOTH");
        /* a negative count: both refuse, nothing to show */
        static const int64_t n[2] = {1, -1};
        zxapp_arg_t na[2] = {{"bottles", n, 2}, {"price", p, 2}};
        CHECK(wave_lp(app, "sale_value", na, 2) == ZXAPP_NEITHER,
              "Vintner: a negative count: both refuse: NEITHER");
        /* past 64 bits: both refuse */
        static const int64_t m[2] = {INT64_C(4000000000), 1}, mp[2] = {INT64_C(4000000000), 1};
        zxapp_arg_t ma[2] = {{"bottles", m, 2}, {"price", mp, 2}};
        CHECK(wave_lp(app, "sale_value", ma, 2) == ZXAPP_NEITHER,
              "Vintner: a product past 64 bits: both refuse: NEITHER");
    }

    /* flow_gap: before - after - loss, one stage per row, Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, i < 32 ? 5000000 : INT64_C(999999999999));
        b[i] = wrand(0, a[i]);
        c[i] = i % 2 ? a[i] - b[i] : wrand(0, a[i] - b[i] + 5); /* half conserve exactly */
    }
    a[0] = 0, b[0] = 0, c[0] = 0;                 /* nothing */
    a[1] = 1000000, b[1] = 750000, c[1] = 250000; /* pressing: conserved */
    a[2] = 600000, b[2] = 598500, c[2] = 1500;    /* bottling: conserved */
    a[3] = 100, b[3] = 90, c[3] = 5;              /* 5 ml unaccounted */
    a[4] = 0, b[4] = 0, c[4] = 1;                 /* more lost than came: -1 */
    a[5] = INT64_MAX, b[5] = 0, c[5] = 0;         /* the max */
    a[6] = -5, b[6] = 0, c[6] = 0;                /* negative */
    a[7] = INT64_MIN + 1, b[7] = 0, c[7] = 0;     /* the least */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = a[i] - b[i] - c[i];
    zxapp_arg_t fa[3] = {{"before", a, WAVE_ROWS}, {"after", b, WAVE_ROWS}, {"loss", c, WAVE_ROWS}};
    CHECK(wave_same(app, "flow_gap", fa, 3, want, WAVE_ROWS, false),
          "Vintner: flow_gap, Trebago = C");
    {
        /* max + 1: past 64 bits, refused, never wrapped */
        static const int64_t x[2] = {0, INT64_MAX}, y[2] = {0, -1}, z[2] = {0, 0};
        zxapp_arg_t xa[3] = {{"before", x, 2}, {"after", y, 2}, {"loss", z, 2}};
        CHECK(vn_rc(app, "flow_gap", xa, 3) != ZXAPP_OK,
              "Vintner: a gap past 64 bits is refused (Trebago)");
        static const int64_t ok[2] = {0, INT64_MAX - 1};
        zxapp_arg_t oa[3] = {{"before", ok, 2}, {"after", y, 2}, {"loss", z, 2}};
        CHECK(vn_rc(app, "flow_gap", oa, 3) == ZXAPP_OK, "Vintner: a gap of INT64_MAX answers");
    }

    /* probe_tenths: one reading per call (Forth reads the stack), Forth = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t raw = i == 0   ? -880
                      : i == 1 ? 2000
                      : i == 2 ? 0
                      : i == 3 ? -1
                      : i == 4 ? -879
                      : i == 5 ? 1999
                      : i == 6 ? 200
                               : wrand(-880, 2000);
        int64_t t = vn_tenths(raw);
        zxapp_arg_t pa = {"raw", &raw, 1};
        CHECK(wave_same(app, "probe_tenths", &pa, 1, &t, 1, false),
              "Vintner: probe_tenths %lld, Forth = KRON = C", (long long) raw);
    }
    {
        /* max + 1 and least - 1: Forth refuses the count, KRON converts it */
        static const int64_t hi[1] = {2001}, lo[1] = {-881}, far[1] = {INT64_C(999999999)};
        zxapp_arg_t ha = {"raw", hi, 1}, la = {"raw", lo, 1}, fa2 = {"raw", far, 1};
        CHECK(wave_lp(app, "probe_tenths", &ha, 1) == ZXAPP_BOTH,
              "Vintner: a count past 2000: Forth refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "probe_tenths", &la, 1) == ZXAPP_BOTH,
              "Vintner: a count below -880: Forth refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "probe_tenths", &fa2, 1) == ZXAPP_BOTH,
              "Vintner: a count far past the range: BOTH");
    }

    /* lot_ok: a lot's record fits ^ZXVINTNER, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, i < 32 ? 999 : VN_NINE + 2);
        b[i] = wrand(1900, i < 32 ? 2100 : 10001);
        c[i] = wrand(1, i < 32 ? 4 : 10);
        d[i] = wrand(0, i < 32 ? 12000 : VN_NINE + 2);
        e[i] = wrand(0, i < 32 ? 9000000 : INT64_C(1000000000001));
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0;      /* every least */
    a[1] = VN_NINE, b[1] = 9999, c[1] = 9, d[1] = VN_NINE; /* every max */
    e[1] = INT64_C(999999999999);
    a[2] = VN_NINE + 1, b[2] = 2024, c[2] = 1, d[2] = 0, e[2] = 0; /* lot max + 1 */
    a[3] = 7, b[3] = 10000, c[3] = 1, d[3] = 0, e[3] = 0;          /* vintage max + 1 */
    a[4] = 7, b[4] = 2024, c[4] = 10, d[4] = 0, e[4] = 0;          /* stage max + 1 */
    a[5] = 7, b[5] = 2024, c[5] = 4, d[5] = VN_NINE + 1, e[5] = 0; /* bottles max + 1 */
    a[6] = 7, b[6] = 2024, c[6] = 4, d[6] = 0;                     /* volume max + 1 */
    e[6] = INT64_C(1000000000000);
    a[7] = -7, b[7] = 2024, c[7] = 1, d[7] = 0, e[7] = 0; /* negative: sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(vn_lot_fits(a[i], b[i], c[i], d[i], e[i]));
    zxapp_arg_t la2[5] = {{"lot", a, WAVE_ROWS},
                          {"vintage", b, WAVE_ROWS},
                          {"stage", c, WAVE_ROWS},
                          {"bottles", d, WAVE_ROWS},
                          {"volume", e, WAVE_ROWS}};
    CHECK(wave_same(app, "lot_ok", la2, 5, want, WAVE_ROWS, true), "Vintner: lot_ok, MYRA = C");

    /* coffer.balance, used for the revenue after a sale: Sutra = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, INT64_C(999999999999));
        b[i] = wrand(1, VN_NINE) * wrand(1, 1000); /* a sale's value */
        want[i] = a[i] + b[i];
    }
    zxapp_arg_t ca[2] = {{"before", a, WAVE_ROWS}, {"moved", b, WAVE_ROWS}};
    CHECK(wave_same(app, "coffer.balance", ca, 2, want, WAVE_ROWS, false),
          "Vintner: the revenue after a sale, through coffer.balance");
}
