/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Signpost's languages against the C its model runs (signpost_rules.h).
 * Slots: @num.signpost_name_fault, @logic.signpost_name_ok,
 * @num.signpost_addr_class, @logic.signpost_cache_ok, @num.signpost_ttl_left. */
#include "signpost_rules.h"

APP_TEST(signpost, 8, 34)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* name_fault (SP2): Rust (wasm) = KRON = C, swept, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 70);
        b[i] = i % 4 ? a[i] + wrand(0, 200) : wrand(0, 260);
        c[i] = i % 3 ? 0 : wrand(0, 3);
    }
    a[0] = 0, b[0] = 0, c[0] = 0;                            /* nothing: an empty label */
    a[1] = 1, b[1] = 1, c[1] = 0;                            /* the shortest name */
    a[2] = 63, b[2] = 253, c[2] = 0;                         /* every max */
    a[3] = 64, b[3] = 64, c[3] = 0;                          /* label max + 1 */
    a[4] = 63, b[4] = 254, c[4] = 0;                         /* name max + 1 */
    a[5] = 10, b[5] = 9, c[5] = 0;                           /* a label longer than its name */
    a[6] = 63, b[6] = 63, c[6] = 1;                          /* one byte not allowed */
    a[7] = 0, b[7] = 300, c[7] = 5;                          /* the label's fault comes first */
    a[8] = 64, b[8] = 300, c[8] = 0;                         /* and before the name's */
    a[9] = 10, b[9] = 300, c[9] = 5;                         /* the name's before a byte's */
    a[10] = INT64_MAX, b[10] = INT64_MAX, c[10] = INT64_MAX; /* int64 max */
    a[11] = 5, b[11] = INT64_MAX, c[11] = 0;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = signpost_name_fault(a[i], b[i], c[i]);
    CHECK(want[0] == SIGNPOST_BAD_LABEL && want[2] == SIGNPOST_FITS &&
              want[5] == SIGNPOST_BAD_TOTAL && want[6] == SIGNPOST_BAD_BYTE &&
              want[7] == SIGNPOST_BAD_LABEL && want[9] == SIGNPOST_BAD_TOTAL,
          "Signpost: the C rule's own edges (SP2)");
    zxapp_arg_t fa[3] = {{"label", a, WAVE_ROWS}, {"total", b, WAVE_ROWS}, {"bad", c, WAVE_ROWS}};
    CHECK(wave_same(app, "name_fault", fa, 3, want, WAVE_ROWS, false),
          "Signpost: name_fault, Rust = KRON = C");
    {
        /* a negative count is no measurement: Rust traps (refused), KRON answers */
        static const int64_t l[2] = {5, -1}, t[2] = {10, 10}, z[2] = {0, 0}, nb[2] = {0, -1};
        zxapp_arg_t na[3] = {{"label", l, 2}, {"total", t, 2}, {"bad", z, 2}};
        CHECK(wave_lp(app, "name_fault", na, 3) == ZXAPP_BOTH,
              "Signpost: a negative label: Rust refuses, KRON answers: BOTH");
        zxapp_arg_t nc[3] = {{"label", t, 2}, {"total", t, 2}, {"bad", nb, 2}};
        CHECK(wave_lp(app, "name_fault", nc, 3) == ZXAPP_BOTH,
              "Signpost: a negative count of bad bytes: Rust refuses, KRON answers: BOTH");
    }

    /* name_ok (SP3): Eve = KRON = C, every name no shorter than its label */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, 70);
        b[i] = (a[i] > 0 ? a[i] : 0) + wrand(0, 220);
        c[i] = i % 3 ? 0 : wrand(-1, 2);
    }
    a[0] = 1, b[0] = 1, c[0] = 0;    /* every least */
    a[1] = 63, b[1] = 253, c[1] = 0; /* every max */
    a[2] = 64, b[2] = 64, c[2] = 0;  /* label max + 1 */
    a[3] = 0, b[3] = 5, c[3] = 0;    /* an empty label */
    a[4] = 10, b[4] = 254, c[4] = 0; /* name max + 1 */
    a[5] = 10, b[5] = 253, c[5] = 1; /* a byte not allowed */
    a[6] = -1, b[6] = 10, c[6] = 0;  /* negative */
    a[7] = 10, b[7] = 10, c[7] = -1; /* a negative count */
    a[8] = 10, b[8] = INT64_MAX, c[8] = 0;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(signpost_name_ok(a[i], b[i], c[i]));
    zxapp_arg_t oa[3] = {{"label", a, WAVE_ROWS}, {"total", b, WAVE_ROWS}, {"bad", c, WAVE_ROWS}};
    CHECK(wave_same(app, "name_ok", oa, 3, want, WAVE_ROWS, true),
          "Signpost: name_ok, Eve = KRON = C");
    {
        /* a label longer than its name: each range holds (Eve TRUE), KRON FALSE */
        static const int64_t l[2] = {10, 10}, t[2] = {5, 20}, z[2] = {0, 0};
        zxapp_arg_t la[3] = {{"label", l, 2}, {"total", t, 2}, {"bad", z, 2}};
        CHECK(wave_lp(app, "name_ok", la, 3) == ZXAPP_BOTH,
              "Signpost: a label longer than its name: Eve TRUE, KRON FALSE: BOTH");
    }

    /* addr_class (SP4): Rust (wasm) = KRON = C */
    static const int64_t FIRST[] = {0, 8, 10, 100, 127, 169, 172, 192, 223, 224, 239, 240, 255};
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? FIRST[wrand(0, 12)] : wrand(0, 255);
        b[i] = i % 3 ? wrand(0, 255) : a[i] == 255 ? 255 : wrand(14, 34);
        c[i] = i % 5 ? wrand(0, 255) : 255;
        d[i] = i % 7 ? wrand(0, 255) : 255;
    }
    static const int64_t EDGE[][5] = {
        {0, 0, 0, 0, SIGNPOST_THIS_NET},         {127, 0, 0, 1, SIGNPOST_LOOPBACK},
        {10, 0, 0, 1, SIGNPOST_PRIVATE},         {172, 15, 0, 1, SIGNPOST_PUBLIC},
        {172, 16, 0, 1, SIGNPOST_PRIVATE},       {172, 31, 255, 255, SIGNPOST_PRIVATE},
        {172, 32, 0, 0, SIGNPOST_PUBLIC},        {192, 168, 1, 1, SIGNPOST_PRIVATE},
        {192, 167, 1, 1, SIGNPOST_PUBLIC},       {169, 254, 0, 1, SIGNPOST_LINK_LOCAL},
        {169, 253, 0, 1, SIGNPOST_PUBLIC},       {100, 63, 255, 255, SIGNPOST_PUBLIC},
        {100, 64, 0, 0, SIGNPOST_SHARED},        {100, 127, 255, 255, SIGNPOST_SHARED},
        {100, 128, 0, 0, SIGNPOST_PUBLIC},       {223, 255, 255, 255, SIGNPOST_PUBLIC},
        {224, 0, 0, 0, SIGNPOST_MULTICAST},      {239, 255, 255, 255, SIGNPOST_MULTICAST},
        {240, 0, 0, 0, SIGNPOST_RESERVED},       {255, 255, 255, 254, SIGNPOST_RESERVED},
        {255, 255, 254, 255, SIGNPOST_RESERVED}, {255, 255, 255, 255, SIGNPOST_BROADCAST},
        {8, 8, 8, 8, SIGNPOST_PUBLIC},           {1, 0, 0, 0, SIGNPOST_PUBLIC},
    };
    for (uint32_t i = 0; i < sizeof EDGE / sizeof EDGE[0]; i++) {
        a[i] = EDGE[i][0], b[i] = EDGE[i][1], c[i] = EDGE[i][2], d[i] = EDGE[i][3];
        CHECK(signpost_addr_class(a[i], b[i], c[i], d[i]) == EDGE[i][4],
              "Signpost: the C rule's edge %u (SP4)", i);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = signpost_addr_class(a[i], b[i], c[i], d[i]);
    zxapp_arg_t ca[4] = {
        {"a", a, WAVE_ROWS}, {"b", b, WAVE_ROWS}, {"c", c, WAVE_ROWS}, {"d", d, WAVE_ROWS}};
    CHECK(wave_same(app, "addr_class", ca, 4, want, WAVE_ROWS, false),
          "Signpost: addr_class, Rust = KRON = C");
    {
        /* an octet outside 0..255 is no address: Rust refuses, KRON answers */
        static const int64_t x[2] = {8, 256}, y[2] = {8, 0}, m[2] = {8, -1}, k[2] = {8, 8};
        zxapp_arg_t xa[4] = {{"a", x, 2}, {"b", y, 2}, {"c", y, 2}, {"d", y, 2}};
        CHECK(wave_lp(app, "addr_class", xa, 4) == ZXAPP_BOTH,
              "Signpost: octet 256: Rust refuses, KRON answers: BOTH");
        zxapp_arg_t ma[4] = {{"a", k, 2}, {"b", y, 2}, {"c", y, 2}, {"d", m, 2}};
        CHECK(wave_lp(app, "addr_class", ma, 4) == ZXAPP_BOTH,
              "Signpost: octet -1: Rust refuses, KRON answers: BOTH");
    }

    /* cache_ok (SP5): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1000000000, 1000000000);
        b[i] = wrand(-10, 10);
        c[i] = wrand(-10000000000, 10000000000);
        d[i] = wrand(-100000000000, 100000000000);
        e[i] = wrand(-10000000000, 10000000000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0;
    a[1] = 999999999, b[1] = 9, c[1] = 9999999999, d[1] = 99999999999, e[1] = 9999999999;
    a[2] = 1000000000, b[2] = 9, c[2] = 9999999999, d[2] = 99999999999, e[2] = 9999999999;
    a[3] = 999999999, b[3] = 10, c[3] = 9999999999, d[3] = 99999999999, e[3] = 9999999999;
    a[4] = 999999999, b[4] = 9, c[4] = 10000000000, d[4] = 99999999999, e[4] = 9999999999;
    a[5] = 999999999, b[5] = 9, c[5] = 9999999999, d[5] = 100000000000, e[5] = 9999999999;
    a[6] = 999999999, b[6] = 9, c[6] = 9999999999, d[6] = 99999999999, e[6] = 10000000000;
    a[7] = -999999999, b[7] = -9, c[7] = -9999999999, d[7] = -99999999999, e[7] = -9999999999;
    a[8] = 1, b[8] = SIGNPOST_PRIVATE, c[8] = 3232235777, d[8] = 1700000000, e[8] = 300;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(signpost_cache_fits(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Signpost: the C rule's own edges (SP5)");
    zxapp_arg_t ra[5] = {{"seq", a, WAVE_ROWS},
                         {"class", b, WAVE_ROWS},
                         {"addr", c, WAVE_ROWS},
                         {"stored", d, WAVE_ROWS},
                         {"ttl", e, WAVE_ROWS}};
    CHECK(wave_same(app, "cache_ok", ra, 5, want, WAVE_ROWS, true),
          "Signpost: an answer makes a ^ZXSIGNPOST record, MYRA = C");

    /* ttl_left (SP6): KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 99999999999);
        b[i] = wrand(0, 2147483647);
        c[i] = a[i] + wrand(-10, b[i] + 10);
    }
    a[0] = 1000, b[0] = 300, c[0] = 1000;   /* just stored: all of it */
    a[1] = 1000, b[1] = 300, c[1] = 1299;   /* one second left */
    a[2] = 1000, b[2] = 300, c[2] = 1300;   /* lapsed at its end */
    a[3] = 1000, b[3] = 300, c[3] = 999999; /* long lapsed: never negative */
    a[4] = 1000, b[4] = 0, c[4] = 1000;     /* no time to live */
    a[5] = 1000, b[5] = 300, c[5] = 900;    /* a clock behind the store */
    a[6] = 0, b[6] = 2147483647, c[6] = 0;  /* the longest time to live */
    a[7] = 1000, b[7] = -5, c[7] = 990;     /* a negative time to live */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = signpost_ttl_left(a[i], b[i], c[i]);
    CHECK(want[0] == 300 && want[1] == 1 && want[2] == 0 && want[3] == 0 && want[5] == 400 &&
              want[7] == 5,
          "Signpost: the C rule's own edges (SP6)");
    zxapp_arg_t ta[3] = {{"stored", a, WAVE_ROWS}, {"ttl", b, WAVE_ROWS}, {"now", c, WAVE_ROWS}};
    CHECK(wave_same(app, "ttl_left", ta, 3, want, WAVE_ROWS, false),
          "Signpost: ttl_left, KRON = C");
}
