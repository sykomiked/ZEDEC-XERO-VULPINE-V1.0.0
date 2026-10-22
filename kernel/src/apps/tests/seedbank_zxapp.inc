/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Seedbank's languages against the C its model runs (seedbank_rules.h).
 * Slots: @num.seedbank_cid_byte, @logic.seedbank_block_put_ok,
 * @logic.seedbank_block_get_ok, @logic.seedbank_pin_record_ok, and,
 * used as it is, @num.used_x100. */
#include "seedbank_rules.h"

APP_TEST(seedbank, 9, 33)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];
    /* "hello" as every IPFS implementation names it (test_zxvd_rpc_modules.c) */
    static const char HELLO[] = "bafkreibm6jg3ux5qumhcn2b3flc3tyu6dmlb4xa7u5bf44yegnrjhc4yeq";

    /* cid_byte (SR1): a real CID byte by byte, every one fits */
    CHECK(sizeof HELLO - 1u == SEEDBANK_CID_LEN && seedbank_cid_ok(HELLO, SEEDBANK_CID_LEN),
          "Seedbank: the CID of hello fits SR2 in C");
    for (uint32_t i = 0; i < SEEDBANK_CID_LEN; i++) {
        a[i] = i;
        b[i] = (unsigned char) HELLO[i];
        c[i] = SEEDBANK_CID_LEN;
        want[i] = 1;
    }
    zxapp_arg_t ha[3] = {
        {"pos", a, SEEDBANK_CID_LEN}, {"code", b, SEEDBANK_CID_LEN}, {"len", c, SEEDBANK_CID_LEN}};
    CHECK(wave_same(app, "cid_byte", ha, 3, want, SEEDBANK_CID_LEN, false),
          "Seedbank: every byte of hello's CID fits, Rust = KRON");

    /* then swept: mostly 59-byte texts, letters, digits and the rest */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        c[i] = i % 4 ? SEEDBANK_CID_LEN : wrand(1, SEEDBANK_TEXT_MAX);
        a[i] = i % 5 ? wrand(0, c[i] - 1) : 0;
        int64_t k = wrand(0, 3);
        b[i] = k == 0 ? wrand('a', 'z') : k == 1 ? wrand('0', '9') : k == 2 ? 'b' : wrand(0, 255);
    }
    a[0] = 0, b[0] = 'b', c[0] = 59;       /* the prefix */
    a[1] = 0, b[1] = 'a', c[1] = 59;       /* a wrong prefix */
    a[2] = 58, b[2] = '7', c[2] = 59;      /* the last byte, the last digit */
    a[3] = 7, b[3] = '8', c[3] = 59;       /* past the digits */
    a[4] = 7, b[4] = '1', c[4] = 59;       /* before them */
    a[5] = 7, b[5] = 'B', c[5] = 59;       /* upper case */
    a[6] = 7, b[6] = '{', c[6] = 59;       /* past 'z' */
    a[7] = 7, b[7] = '`', c[7] = 59;       /* before 'a' */
    a[8] = 0, b[8] = 'b', c[8] = 58;       /* one byte short */
    a[9] = 0, b[9] = 'b', c[9] = 60;       /* one byte long */
    a[10] = 254, b[10] = 255, c[10] = 255; /* the widest domain */
    a[11] = 0, b[11] = 0, c[11] = 1;       /* the narrowest */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = seedbank_cid_byte(a[i], b[i], c[i]);
    CHECK(want[0] == 1 && want[1] == 0 && want[2] == 1 && want[3] == 0 && want[4] == 0 &&
              want[5] == 0 && want[6] == 0 && want[7] == 0 && want[8] == 0 && want[9] == 0 &&
              want[10] == 0 && want[11] == 0,
          "Seedbank: the C byte rule's own edges (SR1)");
    CHECK(seedbank_cid_byte(-1, 'a', 59) == SEEDBANK_REFUSED &&
              seedbank_cid_byte(59, 'a', 59) == SEEDBANK_REFUSED &&
              seedbank_cid_byte(1, 256, 59) == SEEDBANK_REFUSED &&
              seedbank_cid_byte(1, -1, 59) == SEEDBANK_REFUSED &&
              seedbank_cid_byte(0, 'b', 256) == SEEDBANK_REFUSED &&
              seedbank_cid_byte(0, 'b', 0) == SEEDBANK_REFUSED,
          "Seedbank: the C refuses what is no byte of a text");
    CHECK(!seedbank_cid_ok(HELLO, SEEDBANK_CID_LEN - 1u) && !seedbank_cid_ok(NULL, 0) &&
              !seedbank_cid_ok("Bafkreibm6jg3ux5qumhcn2b3flc3tyu6dmlb4xa7u5bf44yegnrjhc4yeq", 59) &&
              !seedbank_cid_ok("bafkreibm6jg3ux5qumhcn2b3flc3tyu6dmlb4xa7u5bf44yegnrjhc4ye1", 59),
          "Seedbank: a text short, upper case, or with a '1' does not fit (SR2)");
    zxapp_arg_t ca[3] = {{"pos", a, WAVE_ROWS}, {"code", b, WAVE_ROWS}, {"len", c, WAVE_ROWS}};
    CHECK(wave_same(app, "cid_byte", ca, 3, want, WAVE_ROWS, false),
          "Seedbank: cid_byte, Rust (wasm) = KRON = C");
    {
        /* no byte of a text: Rust traps, KRON answers 0 */
        static const int64_t ps[4][2] = {{1, -1}, {1, 1}, {1, 59}, {1, 1}};
        static const int64_t cs[4][2] = {{97, 97}, {97, 256}, {97, 97}, {97, -1}};
        static const int64_t ls[4][2] = {{59, 59}, {59, 59}, {59, 59}, {59, 59}};
        for (uint32_t k = 0; k < 4; k++) {
            zxapp_arg_t xa[3] = {{"pos", ps[k], 2}, {"code", cs[k], 2}, {"len", ls[k], 2}};
            CHECK(wave_lp(app, "cid_byte", xa, 3) == ZXAPP_BOTH &&
                      seedbank_cid_byte(ps[k][1], cs[k][1], ls[k][1]) == SEEDBANK_REFUSED,
                  "Seedbank: cid_byte case %u: Rust refuses, KRON answers: BOTH", k);
        }
        static const int64_t lp[2] = {59, 256}, pp[2] = {1, 1}, cp[2] = {97, 97};
        zxapp_arg_t la[3] = {{"pos", pp, 2}, {"code", cp, 2}, {"len", lp, 2}};
        CHECK(wave_lp(app, "cid_byte", la, 3) == ZXAPP_BOTH,
              "Seedbank: a text of 256 bytes: Rust refuses, KRON answers: BOTH");
    }

    /* block_put_ok (SR3) and block_get_ok (SR4): Eve = Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t k = wrand(0, 2);
        a[i] = k == 0   ? wrand(0, SEEDBANK_GET_MAX + 2)
               : k == 1 ? wrand(SEEDBANK_PUT_MAX - 2, SEEDBANK_PUT_MAX + 2)
                        : wrand(0, SEEDBANK_BYTES_FIELD);
    }
    a[0] = 0, a[1] = SEEDBANK_GET_MAX, a[2] = SEEDBANK_GET_MAX + 1, a[3] = SEEDBANK_PUT_MAX;
    a[4] = SEEDBANK_PUT_MAX + 1, a[5] = SEEDBANK_BYTES_FIELD;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(seedbank_put_ok(a[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[2] == ZXAPP_TRUE && want[3] == ZXAPP_TRUE &&
              want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE && !seedbank_put_ok(-1),
          "Seedbank: the C put limit's own edges (SR3)");
    zxapp_arg_t sa[1] = {{"bytes", a, WAVE_ROWS}};
    CHECK(wave_same(app, "block_put_ok", sa, 1, want, WAVE_ROWS, true),
          "Seedbank: block_put_ok, Eve = Sutra = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(seedbank_get_ok(a[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && !seedbank_get_ok(-1),
          "Seedbank: the C get limit's own edges (SR4)");
    CHECK(wave_same(app, "block_get_ok", sa, 1, want, WAVE_ROWS, true),
          "Seedbank: block_get_ok, Eve = Sutra = C");
    {
        /* past Sutra's 9(7), or negative: Sutra refuses, Eve answers no */
        static const int64_t big[2] = {1, SEEDBANK_BYTES_FIELD + 1}, neg[2] = {1, -1},
                             wide[2] = {1, INT64_MAX};
        const int64_t *rows[3] = {big, neg, wide};
        for (uint32_t k = 0; k < 3; k++) {
            zxapp_arg_t xa[1] = {{"bytes", rows[k], 2}};
            CHECK(wave_lp(app, "block_put_ok", xa, 1) == ZXAPP_BOTH &&
                      wave_lp(app, "block_get_ok", xa, 1) == ZXAPP_BOTH,
                  "Seedbank: a count of %lld bytes: Sutra refuses, Eve answers: BOTH",
                  (long long) rows[k][1]);
        }
    }

    /* pin_record_ok (SR5): MYRA = C, each piece at its edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-120, 120);
        b[i] = i % 2 ? wrand(0, SEEDBANK_PUT_MAX) : wrand(-11000000, 11000000);
        c[i] = wrand(-11, 11);
        d[i] = i % 3 ? wrand(1600000000, 2000000000) : wrand(-120000000000, 120000000000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0;                                        /* every least */
    a[1] = 99, b[1] = SEEDBANK_BYTES_FIELD, c[1] = 9, d[1] = SEEDBANK_STAMP_FIELD; /* most */
    a[2] = 100, b[2] = 1, c[2] = 1, d[2] = 1;                                      /* pin past i2 */
    a[3] = 1, b[3] = SEEDBANK_BYTES_FIELD + 1, c[3] = 1, d[3] = 1; /* bytes past i7 */
    a[4] = 1, b[4] = 1, c[4] = 10, d[4] = 1;                       /* sealed past i1 */
    a[5] = 1, b[5] = 1, c[5] = 1, d[5] = SEEDBANK_STAMP_FIELD + 1; /* stamp past i11 */
    a[6] = -99, b[6] = -1, c[6] = -9, d[6] = -1;                   /* sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(seedbank_pin_record_ok(a[i], b[i], c[i], d[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_TRUE,
          "Seedbank: the C record's own edges (SR5)");
    zxapp_arg_t pa[4] = {{"pin", a, WAVE_ROWS},
                         {"bytes", b, WAVE_ROWS},
                         {"sealed", c, WAVE_ROWS},
                         {"stamp", d, WAVE_ROWS}};
    CHECK(wave_same(app, "pin_record_ok", pa, 4, want, WAVE_ROWS, true),
          "Seedbank: pin_record_ok, MYRA = C");

    /* used_share.used_x100 over blockstores: the share the model shows */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? (INT64_C(32) << 20) : wrand(1, INT64_C(1) << 40);
        b[i] = wrand(0, a[i]);
        want[i] = seedbank_used_x100(a[i], b[i]);
    }
    a[0] = 32 << 20, b[0] = 32 << 20; /* an empty store */
    a[1] = 32 << 20, b[1] = 0;        /* a full one */
    want[0] = seedbank_used_x100(a[0], b[0]);
    want[1] = seedbank_used_x100(a[1], b[1]);
    CHECK(want[0] == 0 && want[1] == 10000 && seedbank_used_x100(3, 2) == 3333 &&
              seedbank_used_x100(0, 0) == SEEDBANK_REFUSED &&
              seedbank_used_x100(10, 11) == SEEDBANK_REFUSED &&
              seedbank_used_x100(10, -1) == SEEDBANK_REFUSED,
          "Seedbank: the C share's own edges");
    zxapp_arg_t ua[2] = {{"total", a, WAVE_ROWS}, {"left", b, WAVE_ROWS}};
    CHECK(wave_same(app, "used_share.used_x100", ua, 2, want, WAVE_ROWS, false),
          "Seedbank: blockstore use through used_share.used_x100, Sutra = KRON = C");
    {
        static const int64_t tt[2] = {1000, 1000}, lf[2] = {0, 1001};
        zxapp_arg_t xa[2] = {{"total", tt, 2}, {"left", lf, 2}};
        CHECK(wave_lp(app, "used_share.used_x100", xa, 2) == ZXAPP_BOTH,
              "Seedbank: more left than the store: Sutra refuses, KRON answers: BOTH");
    }
}
