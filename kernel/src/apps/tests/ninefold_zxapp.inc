/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Ninefold's languages, witnessed, row by row against the rules of
 * ninefold_model.c (written out here, since this file cannot include the
 * model's header: it reaches zxm.h). Slots: @num.ninefold_form_holding,
 * @num.ninefold_form_share_x100, @logic.ninefold_state_reserved,
 * @num.ninefold_state_reserved_n, and Coffer's @num.coffer_holdings. */

#define NF_MINOR_MAX INT64_C(999999999999999999) /* NINEFOLD_MINOR_MAX */
#define NF_WHOLE_MAX INT64_C(99999999999999)     /* NINEFOLD_WHOLE_MAX */

static bool nf_reserved(int64_t form)
{
    return form >= 4 && form <= 7; /* ninefold_state_reserved */
}

static int64_t nf_holding(int64_t form, int64_t minor)
{
    return nf_reserved(form) ? 0 : minor; /* ninefold_form_holding, in its domain */
}

static int64_t nf_share(int64_t form, int64_t held, int64_t whole)
{
    return nf_reserved(form) ? 0 : held * 10000 / whole; /* ninefold_form_share, in its domain */
}

APP_TEST(ninefold, 13, 80)
{
    static int64_t f[WAVE_ROWS], a[WAVE_ROWS], b[WAVE_ROWS], want[WAVE_ROWS];

    /* form_holding: every form, 0 .. 18 digits, Sutra = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        f[i] = (int64_t) (i % 9u);
        a[i] = wrand(0, i < 32 ? 100000000 : NF_MINOR_MAX);
    }
    a[0] = 0;            /* nothing held */
    a[1] = NF_MINOR_MAX; /* the max, priced */
    a[4] = NF_MINOR_MAX; /* the max, state-reserved: 0 */
    a[8] = NF_MINOR_MAX; /* system, priced */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = nf_holding(f[i], a[i]);
    zxapp_arg_t ha[2] = {{"form", f, WAVE_ROWS}, {"minor", a, WAVE_ROWS}};
    CHECK(wave_same(app, "form_holding", ha, 2, want, WAVE_ROWS, false),
          "Ninefold: form_holding, Sutra = Trebago = C");
    {
        /* max + 1: past Sutra's 9(18), refused there; Trebago answers */
        static const int64_t x[2] = {0, 0}, y[2] = {1, NF_MINOR_MAX + 1};
        zxapp_arg_t xa[2] = {{"form", x, 2}, {"minor", y, 2}};
        CHECK(wave_lp(app, "form_holding", xa, 2) == ZXAPP_BOTH,
              "Ninefold: a holding past 18 digits: Sutra refuses, Trebago answers: BOTH");
        /* a negative holding */
        static const int64_t n[2] = {0, -1};
        zxapp_arg_t na[2] = {{"form", x, 2}, {"minor", n, 2}};
        CHECK(wave_lp(app, "form_holding", na, 2) == ZXAPP_BOTH,
              "Ninefold: a negative holding: Sutra refuses, Trebago answers: BOTH");
        /* form 9 is not one of the nine */
        static const int64_t g[2] = {0, 9}, o[2] = {1, 1};
        zxapp_arg_t ga[2] = {{"form", g, 2}, {"minor", o, 2}};
        CHECK(wave_lp(app, "form_holding", ga, 2) == ZXAPP_BOTH,
              "Ninefold: form 9: Sutra refuses, Trebago answers: BOTH");
    }

    /* form_share_x100: held 0 .. whole, whole 1 .. 14 digits, Trebago = Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        f[i] = (int64_t) ((i * 5u) % 9u);
        b[i] = wrand(1, i < 32 ? 1000000 : NF_WHOLE_MAX);
        a[i] = wrand(0, b[i]);
    }
    a[0] = 0, b[0] = 1;                       /* nothing of the least whole */
    a[1] = NF_WHOLE_MAX, b[1] = NF_WHOLE_MAX; /* all of the largest whole */
    a[2] = 1, b[2] = NF_WHOLE_MAX;            /* the least part of the largest: 0 */
    a[3] = 1, b[3] = 3;                       /* floored, never rounded up */
    f[3] = 0;
    a[4] = 2, b[4] = 3, f[4] = 8;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = nf_share(f[i], a[i], b[i]);
    zxapp_arg_t sa[3] = {{"form", f, WAVE_ROWS}, {"held", a, WAVE_ROWS}, {"whole", b, WAVE_ROWS}};
    CHECK(wave_same(app, "form_share_x100", sa, 3, want, WAVE_ROWS, false),
          "Ninefold: form_share_x100, Trebago = Sutra = C");
    {
        /* a whole of 0: Sutra's division refused, Trebago counts every step */
        static const int64_t x[2] = {0, 0}, h[2] = {1, 1}, w[2] = {1, 0};
        zxapp_arg_t xa[3] = {{"form", x, 2}, {"held", h, 2}, {"whole", w, 2}};
        CHECK(wave_lp(app, "form_share_x100", xa, 3) == ZXAPP_BOTH,
              "Ninefold: a whole of 0: Sutra refuses, Trebago answers: BOTH");
        /* held past the whole (max + 1 of held) */
        static const int64_t h2[2] = {1, 4}, w2[2] = {1, 3};
        zxapp_arg_t ya[3] = {{"form", x, 2}, {"held", h2, 2}, {"whole", w2, 2}};
        CHECK(wave_lp(app, "form_share_x100", ya, 3) == ZXAPP_BOTH,
              "Ninefold: held past the whole: Sutra refuses, Trebago answers: BOTH");
        /* a negative held */
        static const int64_t h3[2] = {1, -1}, w3[2] = {3, 3};
        zxapp_arg_t za[3] = {{"form", x, 2}, {"held", h3, 2}, {"whole", w3, 2}};
        CHECK(wave_lp(app, "form_share_x100", za, 3) == ZXAPP_BOTH,
              "Ninefold: a negative held: Sutra refuses, Trebago answers: BOTH");
    }

    /* state_reserved: Eve = Sutra = C over every form */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        f[i] = (int64_t) ((i * 7u) % 9u);
        want[i] = T_(nf_reserved(f[i]));
    }
    zxapp_arg_t ra = {"form", f, WAVE_ROWS};
    CHECK(wave_same(app, "state_reserved", &ra, 1, want, WAVE_ROWS, true),
          "Ninefold: state_reserved, Eve = Sutra = C");
    {
        /* 9, past the nine forms: Eve FALSE, Sutra refuses */
        static const int64_t x[2] = {0, 9};
        zxapp_arg_t xa = {"form", x, 2};
        CHECK(wave_lp(app, "state_reserved", &xa, 1) == ZXAPP_BOTH,
              "Ninefold: form 9: Eve FALSE, Sutra refuses: BOTH");
        static const int64_t n[2] = {4, -1};
        zxapp_arg_t na = {"form", n, 2};
        CHECK(wave_lp(app, "state_reserved", &na, 1) == ZXAPP_BOTH,
              "Ninefold: a negative form: Eve FALSE, Sutra refuses: BOTH");
    }

    /* state_reserved_n: Trebago = Sutra = C over every form */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = nf_reserved(f[i]) ? 1 : 0;
    CHECK(wave_same(app, "state_reserved_n", &ra, 1, want, WAVE_ROWS, false),
          "Ninefold: state_reserved_n, Trebago = Sutra = C");
    {
        static const int64_t x[2] = {0, -1};
        zxapp_arg_t xa = {"form", x, 2};
        CHECK(wave_lp(app, "state_reserved_n", &xa, 1) == ZXAPP_BOTH,
              "Ninefold: a negative form: Sutra refuses, Trebago answers 0: BOTH");
        static const int64_t y[2] = {8, 9};
        zxapp_arg_t ya = {"form", y, 2};
        CHECK(wave_lp(app, "state_reserved_n", &ya, 1) == ZXAPP_BOTH,
              "Ninefold: form 9 (max + 1): Sutra refuses, Trebago answers 0: BOTH");
    }

    /* coffer.holdings, used for each form's sum: Trebago = C */
    {
        static const int64_t bal[4] = {250000, 0, 100, NF_MINOR_MAX - 250100},
                             sum[1] = {NF_MINOR_MAX};
        zxapp_arg_t ca = {"balance", bal, 4};
        CHECK(wave_same(app, "coffer.holdings", &ca, 1, sum, 1, false),
              "Ninefold: a form's sum, through coffer.holdings");
    }
}
