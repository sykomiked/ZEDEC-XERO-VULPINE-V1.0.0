/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Ramble's languages against the C its model runs (ramble_rules.h).
 * Slots: @logic.ramble_consent_covers, @logic.ramble_envelope_record_ok,
 * and, used as they are, @num.seedbank_cid_byte and
 * @num.reckoner_statement_close. */
#include "ramble_rules.h"
#include "reckoner_rules.h"
#include "seedbank_rules.h"

APP_TEST(ramble, 15, 25)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* the C rules' own edges */
    CHECK(ramble_need(false, 0) == 0 && ramble_need(true, 0) == RAMBLE_SCOPE_PII &&
              ramble_need(false, 1) == RAMBLE_SCOPE_MONEY &&
              ramble_need(true, 5) == RAMBLE_SCOPE_ALL && ramble_need(false, -3) == 0,
          "Ramble: the scopes an action needs (RR3)");
    CHECK(ramble_scope_gap(3, 3) == 0 && ramble_scope_gap(3, 0) == 2 &&
              ramble_scope_gap(2, 1) == 1 && ramble_scope_gap(0, 0) == 0 &&
              ramble_scope_gap(1, 2) == 1 && ramble_scope_gap(4, 0) == RAMBLE_REFUSED &&
              ramble_scope_gap(0, -1) == RAMBLE_REFUSED,
          "Ramble: the scopes a grant is missing (RR1)");
    CHECK(ramble_margin(10, 3) == 7 && ramble_margin(INT64_MIN, 1) == INT64_MIN &&
              ramble_margin(INT64_MAX, -1) == INT64_MIN && ramble_margin(0, INT64_MIN) == INT64_MIN,
          "Ramble: a margin past int64 is past every range");

    /* consent_covers (RR1): Eve = Sutra = C, swept near every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t k = wrand(0, 3);
        a[i] = k == 0 ? wrand(-9, 9) : 0;
        b[i] = k == 1 ? wrand(-1000, 1000) : wrand(0, RAMBLE_MINOR_MAX);
        c[i] = k == 2 ? wrand(-99999, 99999) : wrand(-3, RAMBLE_GRANT_MAX + 3);
    }
    a[0] = 0, b[0] = 0, c[0] = 1;                               /* the least that covers */
    a[1] = 0, b[1] = RAMBLE_MINOR_MAX, c[1] = RAMBLE_GRANT_MAX; /* the most */
    a[2] = 1, b[2] = 0, c[2] = 600;                             /* a scope missing */
    a[3] = 0, b[3] = -1, c[3] = 600;                            /* one minor unit over */
    a[4] = 0, b[4] = 0, c[4] = 0;                               /* expiring now */
    a[5] = 0, b[5] = 0, c[5] = RAMBLE_GRANT_MAX + 1;            /* longer than zxvd grants */
    a[6] = -1, b[6] = 0, c[6] = 600;                            /* a gap that is no count */
    a[7] = 0, b[7] = -RAMBLE_MINOR_MAX, c[7] = -99999;          /* long expired, far over */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(ramble_consent_covers(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE,
          "Ramble: the C consent rule's own edges (RR1)");
    zxapp_arg_t ca[3] = {
        {"scopegap", a, WAVE_ROWS}, {"ceilingleft", b, WAVE_ROWS}, {"secondsleft", c, WAVE_ROWS}};
    CHECK(wave_same(app, "consent_covers", ca, 3, want, WAVE_ROWS, true),
          "Ramble: consent_covers, Eve = Sutra = C");
    {
        /* past Sutra's pictures: Sutra refuses, Eve answers no */
        static const int64_t g[3][2] = {{0, 0}, {0, 0}, {0, 10}};
        static const int64_t m[3][2] = {{0, RAMBLE_MINOR_MAX + 1}, {0, 0}, {0, 0}};
        static const int64_t s[3][2] = {{600, 600}, {600, 100000}, {600, 600}};
        for (uint32_t k = 0; k < 3; k++) {
            zxapp_arg_t xa[3] = {
                {"scopegap", g[k], 2}, {"ceilingleft", m[k], 2}, {"secondsleft", s[k], 2}};
            CHECK(wave_lp(app, "consent_covers", xa, 3) == ZXAPP_BOTH &&
                      !ramble_consent_covers(g[k][1], m[k][1], s[k][1]),
                  "Ramble: consent_covers case %u: Sutra refuses, Eve answers no: BOTH", k);
        }
    }

    /* envelope_record_ok (RR2): MYRA = C, each piece at its edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? wrand(1, UINT32_MAX) : wrand(-11000000000, 11000000000);
        b[i] = wrand(-11, 11);
        c[i] = wrand(-11, 11);
        d[i] = i % 3 ? wrand(0, 999999999) : wrand(-RAMBLE_MINOR_MAX, RAMBLE_MINOR_MAX);
        e[i] = i % 4 ? wrand(1600000000, 2000000000) : wrand(-1100000000000, 1100000000000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0; /* every least */
    a[1] = RAMBLE_ID_FIELD, b[1] = 9, c[1] = 9, d[1] = RAMBLE_AMOUNT_FIELD;
    e[1] = RAMBLE_STAMP_FIELD;                                               /* every most */
    a[2] = RAMBLE_ID_FIELD + 1, b[2] = 0, c[2] = 0, d[2] = 0, e[2] = 0;      /* id past i10 */
    a[3] = 1, b[3] = 10, c[3] = 0, d[3] = 0, e[3] = 0;                       /* kind past i1 */
    a[4] = 1, b[4] = 0, c[4] = 10, d[4] = 0, e[4] = 0;                       /* status past i1 */
    a[5] = 1, b[5] = 0, c[5] = 0, d[5] = RAMBLE_AMOUNT_FIELD + 1, e[5] = 0;  /* amount past i18 */
    a[6] = 1, b[6] = 0, c[6] = 0, d[6] = 0, e[6] = RAMBLE_STAMP_FIELD + 1;   /* stamp past i12 */
    a[7] = -1, b[7] = -9, c[7] = -9, d[7] = -RAMBLE_AMOUNT_FIELD, e[7] = -1; /* sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(ramble_envelope_record_ok(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Ramble: the C record's own edges (RR2)");
    zxapp_arg_t ea[5] = {{"id", a, WAVE_ROWS},
                         {"kind", b, WAVE_ROWS},
                         {"status", c, WAVE_ROWS},
                         {"amount", d, WAVE_ROWS},
                         {"stamp", e, WAVE_ROWS}};
    CHECK(wave_same(app, "envelope_record_ok", ea, 5, want, WAVE_ROWS, true),
          "Ramble: envelope_record_ok, MYRA = C");

    /* seedbank.cid_byte over the CID texts Ramble fetches: Rust = KRON = C */
    static const char HELLO[] = "bafkreibm6jg3ux5qumhcn2b3flc3tyu6dmlb4xa7u5bf44yegnrjhc4yeq";
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        c[i] = i % 8 ? SEEDBANK_CID_LEN : wrand(1, SEEDBANK_TEXT_MAX);
        a[i] = wrand(0, c[i] - 1);
        b[i] = i % 3 ? (unsigned char) HELLO[a[i] % SEEDBANK_CID_LEN] : wrand(0, 255);
        want[i] = seedbank_cid_byte(a[i], b[i], c[i]);
    }
    zxapp_arg_t sa[3] = {{"pos", a, WAVE_ROWS}, {"code", b, WAVE_ROWS}, {"len", c, WAVE_ROWS}};
    CHECK(wave_same(app, "seedbank.cid_byte", sa, 3, want, WAVE_ROWS, false),
          "Ramble: the CID check through seedbank.cid_byte, Rust = KRON = C");
    {
        static const int64_t p[2] = {1, 1}, q[2] = {97, 256}, n[2] = {59, 59};
        zxapp_arg_t xa[3] = {{"pos", p, 2}, {"code", q, 2}, {"len", n, 2}};
        CHECK(wave_lp(app, "seedbank.cid_byte", xa, 3) == ZXAPP_BOTH,
              "Ramble: a code that is no byte: Rust refuses, KRON answers: BOTH");
    }

    /* reckoner.statement_close: a balance after a held envelope's amount */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-999999999999, 999999999999);
        b[i] = 0;
        c[i] = wrand(1, 999999999999);
        want[i] = reckoner_statement_close(a[i], b[i], c[i]);
    }
    zxapp_arg_t ra[3] = {
        {"opening", a, WAVE_ROWS}, {"credits", b, WAVE_ROWS}, {"debits", c, WAVE_ROWS}};
    CHECK(wave_same(app, "reckoner.statement_close", ra, 3, want, WAVE_ROWS, false),
          "Ramble: the balance after the money left, Sutra = Trebago = C");
}
