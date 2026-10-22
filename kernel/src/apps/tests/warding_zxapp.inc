/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Warding's languages, witnessed, row by row against warding_rules.h, the
 * C the model runs; the parts it composes (threshold, gatehouse, lockbox,
 * homestead) resolved and called against their own rules; and the BOTH
 * of key 19, revoked on device A and still active on device B.
 * Slots: @logic.warding_key_active, @logic.warding_key_expiry_ok,
 * @logic.warding_key_serial_ok, @num.warding_key_entropy_bits,
 * @num.warding_access_grid_cell, @logic.warding_holder_node_ok; and, used,
 * @logic.threshold_rights_ok, @logic.threshold_seat_ok,
 * @logic.gatehouse_use_ok, @num.lockbox_days_left,
 * @logic.homestead_device_cmd_ok. */
#include "gatehouse_rules.h"
#include "homestead_rules.h"
#include "lockbox_rules.h"
#include "threshold_rules.h"
#include "warding_rules.h"

APP_TEST(warding, 34, 173)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* compose: four parts, every part entry it calls resolves to its part */
    static const char *const used[5][2] = {{"threshold.rights_ok", "threshold"},
                                           {"threshold.seat_ok", "threshold"},
                                           {"gatehouse.use_ok", "gatehouse"},
                                           {"lockbox.days_left", "lockbox"},
                                           {"homestead.device_cmd_ok", "homestead"}};
    CHECK(app->resolved && app->nuse == 4 && app->composed == 8 && app->n == 34,
          "Warding: composed of 4 parts, 8 own lines and 26 of theirs (%u, %u)", app->composed,
          app->n);
    for (uint32_t i = 0; i < 5u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Warding: %s resolves to %s and loads", used[i][0], used[i][1]);
    }
    zxapp_origin_t org[8];
    CHECK(zxapp_provenance(app, org, 8) >= 5 && !org[0].from, "Warding: itself and its parts");

    /* WR1: key_active, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 4u ? 1 : wrand(0, 1);
        b[i] = i % 3u ? 0 : wrand(0, 8);
        c[i] = wrand(-10, WARDING_EXPIRY_MAX);
        if (i == 0) a[i] = 1, b[i] = 0, c[i] = 1;                  /* the last day */
        if (i == 1) a[i] = 1, b[i] = 0, c[i] = WARDING_EXPIRY_MAX; /* the longest */
        if (i == 2) a[i] = 1, b[i] = 0, c[i] = 0;                  /* expired */
        if (i == 3) a[i] = 1, b[i] = 1, c[i] = 5;                  /* revoked on one device */
        if (i == 4) a[i] = 0, b[i] = 0, c[i] = 5;                  /* never issued */
        want[i] = T_(warding_key_active(a[i], b[i], c[i]));
    }
    zxapp_arg_t ka[3] = {
        {"issued", a, WAVE_ROWS}, {"revoked", b, WAVE_ROWS}, {"left", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE &&
              wave_same(app, "key_active", ka, 3, want, WAVE_ROWS, true),
          "Warding: key_active, Eve = KRON = C");
    {
        /* a flag that is no flag, a negative count, days past the limits:
         * Eve says no, KRON yes: held */
        static const int64_t edge[3][3] = {{2, 0, 5}, {1, -1, 5}, {1, 0, WARDING_EXPIRY_MAX + 1}};
        for (uint32_t k = 0; k < 3u; k++) {
            int64_t is[2] = {1, edge[k][0]}, rv[2] = {0, edge[k][1]}, lf[2] = {5, edge[k][2]};
            zxapp_arg_t xa[3] = {{"issued", is, 2}, {"revoked", rv, 2}, {"left", lf, 2}};
            CHECK(!warding_key_active(edge[k][0], edge[k][1], edge[k][2]) &&
                      wave_lp(app, "key_active", xa, 3) == ZXAPP_BOTH,
                  "Warding: key_active edge %u: Eve says no, KRON yes: BOTH", k);
        }
    }

    /* WR2: key_expiry_ok, Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-10, WARDING_EXPIRY_MAX + 10);
        if (i == 0) a[i] = 0;
        if (i == 1) a[i] = 1;
        if (i == 2) a[i] = WARDING_EXPIRY_MAX;
        if (i == 3) a[i] = WARDING_EXPIRY_MAX + 1;
        if (i == 4) a[i] = -1;
        want[i] = T_(warding_expiry_ok(a[i]));
    }
    zxapp_arg_t ea[1] = {{"days", a, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_FALSE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_TRUE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE &&
              wave_same(app, "key_expiry_ok", ea, 1, want, WAVE_ROWS, true),
          "Warding: key_expiry_ok, Eve = C");

    /* WR3: key_serial_ok, Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = i % 2u ? wrand(0, 1000) : wrand(0, WARDING_SERIAL_MAX);
        a[i] = i % 3u ? wrand(b[i] < WARDING_SERIAL_MAX ? b[i] + 1 : b[i], WARDING_SERIAL_MAX)
                      : wrand(0, b[i]);
        if (i == 0) a[i] = 1, b[i] = 0;                                       /* the first */
        if (i == 1) a[i] = 19, b[i] = 19;                                     /* reused */
        if (i == 2) a[i] = 18, b[i] = 19;                                     /* falling */
        if (i == 3) a[i] = WARDING_SERIAL_MAX, b[i] = WARDING_SERIAL_MAX - 1; /* the last */
        want[i] = T_(warding_serial_ok(a[i], b[i]) == 1);
    }
    zxapp_arg_t sa[2] = {{"serial", a, WAVE_ROWS}, {"last", b, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_TRUE &&
              wave_same(app, "key_serial_ok", sa, 2, want, WAVE_ROWS, true),
          "Warding: key_serial_ok, Sutra = C");
    {
        /* past 9(9) (the register has overflowed) or negative: refused */
        static const int64_t bad[3][2] = {{WARDING_SERIAL_MAX + 1, 5}, {-1, 0}, {5, -1}};
        for (uint32_t k = 0; k < 3u; k++) {
            int64_t se[2] = {2, bad[k][0]}, la[2] = {1, bad[k][1]};
            zxapp_arg_t xa[2] = {{"serial", se, 2}, {"last", la, 2}};
            CHECK(warding_serial_ok(bad[k][0], bad[k][1]) == WARDING_REFUSED &&
                      wave_lp(app, "key_serial_ok", xa, 2) != ZXAPP_TRUE,
                  "Warding: key_serial_ok edge %u: refused, never wrapped", k);
        }
    }

    /* WR4: key_entropy_bits, Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, WARDING_CHARS_MAX);
        b[i] = wrand(1, WARDING_ALPHABET_MAX);
        if (i == 0) a[i] = 32, b[i] = 256;                                 /* 32 bytes */
        if (i == 1) a[i] = 0, b[i] = 62;                                   /* empty */
        if (i == 2) a[i] = WARDING_CHARS_MAX, b[i] = WARDING_ALPHABET_MAX; /* max */
        if (i == 3) a[i] = 4000, b[i] = 1;                                 /* one symbol */
        if (i == 4) a[i] = 16, b[i] = 62;
        want[i] = warding_entropy_bits(a[i], b[i]);
    }
    zxapp_arg_t na[2] = {{"chars", a, WAVE_ROWS}, {"alphabet", b, WAVE_ROWS}};
    CHECK(want[0] == 256 && want[1] == 0 && want[2] == 32768 && want[3] == 0 && want[4] == 95 &&
              wave_same(app, "key_entropy_bits", na, 2, want, WAVE_ROWS, false),
          "Warding: key_entropy_bits, Trebago = KRON = C");
    {
        /* the table itself, every alphabet once: 1000 symbols each */
        int64_t ch[WAVE_ROWS], al[WAVE_ROWS], w2[WAVE_ROWS];
        bool all = true;
        for (uint32_t base = 1; base <= WARDING_ALPHABET_MAX; base += WAVE_ROWS) {
            for (uint32_t i = 0; i < WAVE_ROWS; i++) {
                ch[i] = 1000;
                al[i] = base + i;
                w2[i] = WARDING_LOG2_MILLI[al[i]];
            }
            zxapp_arg_t ta[2] = {{"chars", ch, WAVE_ROWS}, {"alphabet", al, WAVE_ROWS}};
            all = all && wave_same(app, "key_entropy_bits", ta, 2, w2, WAVE_ROWS, false);
        }
        CHECK(all, "Warding: the log2 table, all 256 alphabets, Trebago = KRON = C");
        /* past 4096 symbols, or an alphabet of 0: Trebago refuses, KRON answers */
        static const int64_t edge[2][2] = {{WARDING_CHARS_MAX + 1, 256}, {32, 0}};
        for (uint32_t k = 0; k < 2u; k++) {
            int64_t c1[2] = {32, edge[k][0]}, a1[2] = {256, edge[k][1]};
            zxapp_arg_t xa[2] = {{"chars", c1, 2}, {"alphabet", a1, 2}};
            CHECK(warding_entropy_bits(edge[k][0], edge[k][1]) == WARDING_REFUSED &&
                      wave_lp(app, "key_entropy_bits", xa, 2) == ZXAPP_BOTH,
                  "Warding: key_entropy_bits edge %u: Trebago refuses, KRON answers: BOTH", k);
        }
    }

    /* WR5: access_grid_cell, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        a[i] = wrand(1, 4), b[i] = wrand(1, 3) * 10000 + wrand(1, 3), c[i] = wrand(0, 1);
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = warding_grid_cell(a, b, c, WAVE_ROWS, i);
    zxapp_arg_t ga[3] = {{"holder", a, WAVE_ROWS}, {"item", b, WAVE_ROWS}, {"live", c, WAVE_ROWS}};
    CHECK(wave_same(app, "access_grid_cell", ga, 3, want, WAVE_ROWS, false),
          "Warding: access_grid_cell, KRON = C");

    /* WR6: holder_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-12000, 12000);
        b[i] = wrand(-12, 12);
        c[i] = wrand(-12000, 12000);
        d[i] = wrand(-1200000000, 1200000000);
        e[i] = wrand(-INT64_C(1200000000000), INT64_C(1200000000000));
        if (i == 0) a[i] = 9999, b[i] = 3, c[i] = 9999, d[i] = 999999999, e[i] = 999999999999;
        if (i == 1)
            a[i] = 10000, b[i] = 1, c[i] = 1, d[i] = 1, e[i] = 1; /* a holder past 4 digits */
        want[i] = T_(warding_node_ok(a[i], b[i], c[i], d[i], e[i]));
    }
    zxapp_arg_t ha[5] = {{"holder", a, WAVE_ROWS},
                         {"kind", b, WAVE_ROWS},
                         {"item", c, WAVE_ROWS},
                         {"serial", d, WAVE_ROWS},
                         {"expires", e, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "holder_node_ok", ha, 5, want, WAVE_ROWS, true),
          "Warding: holder_node_ok, MYRA = C");

    /* the parts, called through Warding as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 3), b[i] = wrand(0, 1), c[i] = wrand(0, 4);
        want[i] = T_(threshold_rights(a[i], b[i], c[i]));
    }
    zxapp_arg_t ra[3] = {{"role", a, WAVE_ROWS}, {"own", b, WAVE_ROWS}, {"target", c, WAVE_ROWS}};
    CHECK(wave_same(app, "threshold.rights_ok", ra, 3, want, WAVE_ROWS, true),
          "Warding: threshold.rights_ok, who may issue or revoke a holder's key");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-10000, 10000), b[i] = wrand(-10, 10), c[i] = wrand(-10, 10);
        d[i] = wrand(0, 4294967295), e[i] = wrand(-10000000, 10000000);
        want[i] = T_(threshold_seat_fits(a[i], b[i], c[i], d[i], e[i]));
    }
    zxapp_arg_t ta[5] = {{"seat", a, WAVE_ROWS},
                         {"role", b, WAVE_ROWS},
                         {"lock", c, WAVE_ROWS},
                         {"color", d, WAVE_ROWS},
                         {"wait", e, WAVE_ROWS}};
    CHECK(wave_same(app, "threshold.seat_ok", ta, 5, want, WAVE_ROWS, true),
          "Warding: threshold.seat_ok, a holder's seat record");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1), b[i] = wrand(0, 1), c[i] = wrand(0, 1);
        want[i] = T_(gatehouse_use_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t ua[3] = {
        {"asked", a, WAVE_ROWS}, {"granted", b, WAVE_ROWS}, {"revoked", c, WAVE_ROWS}};
    CHECK(wave_same(app, "gatehouse.use_ok", ua, 3, want, WAVE_ROWS, true),
          "Warding: gatehouse.use_ok, is this use allowed");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(0, 2000000000);
        a[i] =
            i % 3u ? b[i] + wrand(0, (int64_t) WARDING_EXPIRY_MAX * LOCKBOX_DAY) : wrand(0, b[i]);
        want[i] = lockbox_days_left(a[i], b[i]);
    }
    zxapp_arg_t la[2] = {{"expires", a, WAVE_ROWS}, {"now", b, WAVE_ROWS}};
    CHECK(wave_same(app, "lockbox.days_left", la, 2, want, WAVE_ROWS, false),
          "Warding: lockbox.days_left, a key's days before it expires");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1);
        b[i] = 1 - a[i];
        c[i] = homestead_owed(HOMESTEAD_LOCK, wrand(0, 1), wrand(0, 1));
        d[i] = wrand(0, 1);
        want[i] = T_(homestead_command_ok(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t da[4] = {{"above", a, WAVE_ROWS},
                         {"below", b, WAVE_ROWS},
                         {"owed", c, WAVE_ROWS},
                         {"ai", d, WAVE_ROWS}};
    CHECK(wave_same(app, "homestead.device_cmd_ok", da, 4, want, WAVE_ROWS, true),
          "Warding: homestead.device_cmd_ok, a door command");

    /* WR7, the BOTH test: key 19 is revoked on device A and still active on
     * device B. Each device's word is what key_active answers for it there
     * (A counts its revocation, B none); joined, the key is BOTH, never
     * TRUE, so it opens nothing on any device until A's revocation
     * reaches B. */
    {
        static const int64_t is[2] = {1, 1}, rv[2] = {1, 0}, lf[2] = {30, 30};
        int64_t wa[2] = {ZXAPP_FALSE, ZXAPP_TRUE};
        zxapp_arg_t xa[3] = {{"issued", is, 2}, {"revoked", rv, 2}, {"left", lf, 2}};
        CHECK(wave_same(app, "key_active", xa, 3, wa, 2, true),
              "Warding: key 19 on device A (revoked) and B (active)");
        lpres_state_t word[2] = {warding_device_word(true, true), warding_device_word(true, false)};
        lpres_state_t s = warding_key_state(word, 2);
        CHECK(s == LPRES_STATE_BOTH && (int) s == 3 && s != LPRES_STATE_TRUE &&
                  lpres_conjoin(s, LPRES_STATE_TRUE) == LPRES_STATE_BOTH,
              "Warding: key 19 revoked on A, active on B: BOTH, locked out everywhere");
        word[1] = warding_device_word(true, true); /* the revocation reaches B */
        lpres_state_t none[2] = {LPRES_STATE_NEITHER, LPRES_STATE_TRUE};
        CHECK(
            warding_key_state(word, 2) == LPRES_STATE_FALSE &&
                warding_key_state(none, 2) == LPRES_STATE_TRUE &&
                warding_key_state(none, 0) == LPRES_STATE_NEITHER,
            "Warding: once every device has it, revoked (FALSE); a device not heard adds nothing");
    }
}
