/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Updraft's languages, witnessed, row by row against updraft_rules.h, the
 * C the model runs; the parts it composes (aerie, sounding, trellis,
 * commons) resolved and called; and the BOTH of frame 31, heard one way by
 * relay 1 and another by relay 2. Slots: @num.updraft_link_margin_db,
 * @logic.updraft_transmit_power_ok, @num.updraft_radio_link_state,
 * @logic.updraft_relay_node_ok; and, used, @num.aerie_frame_tag,
 * @logic.aerie_band_fits, @logic.aerie_radio_msg_ok,
 * @logic.aerie_ham_clear_ok, @num.sounding_rate,
 * @num.trellis_peers_in_state, @logic.trellis_peer_record_ok,
 * @logic.commons_room_post_ok. */
#include "aerie_rules.h"
#include "commons_rules.h"
#include "trellis_rules.h"
#include "updraft_rules.h"

/* one frame's parts through Updraft: Forth reads one frame per call */
static bool upd_tag_row(zxapp_t *app, int64_t sync, int64_t shell, int64_t trunk)
{
    int64_t sa[1] = {sync}, ea[1] = {shell}, ta[1] = {trunk};
    int64_t w[1] = {aerie_frame_tag(sync, shell, trunk)};
    zxapp_arg_t a[3] = {{"sync", sa, 1}, {"shell", ea, 1}, {"trunk", ta, 1}};
    return wave_same(app, "aerie.frame_tag", a, 3, w, 1, false);
}

APP_TEST(updraft, 30, 214)
{
    /* the shared row generator is put back as it was at the end, so the apps
     * tested after Updraft draw the rows they drew before it was built */
    const uint64_t seed_was = WSEED;
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], want[WAVE_ROWS];

    /* compose: four parts, every part entry it calls resolves to its part */
    static const char *const used[8][2] = {
        {"aerie.frame_tag", "aerie"},          {"aerie.band_fits", "aerie"},
        {"aerie.radio_msg_ok", "aerie"},       {"aerie.ham_clear_ok", "aerie"},
        {"sounding.rate", "sounding"},         {"trellis.peers_in_state", "trellis"},
        {"trellis.peer_record_ok", "trellis"}, {"commons.room_post_ok", "commons"}};
    CHECK(app->resolved && app->nuse == 4 && app->composed == 6 && app->n == 30 &&
              app->n <= ZXAPP_LOGIC_MAX && app->nuse <= ZXAPP_USES,
          "Updraft: composed of 4 parts, 6 own lines and %u of theirs (%u)", app->n - 6u, app->n);
    for (uint32_t i = 0; i < 8u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Updraft: %s resolves to %s and loads", used[i][0], used[i][1]);
    }
    {
        zxapp_origin_t org[12];
        uint32_t no = zxapp_provenance(app, org, 12);
        CHECK(no >= 5u && !org[0].from, "Updraft: itself and its parts (%u)", no);
    }

    /* the C rules' own edges (UR1-UR6) */
    {
        int64_t s = 0;
        static const int64_t X[2] = {1111, 2222}, XYX[3] = {1111, 2222, 1111};
        CHECK(updraft_link_margin(30, 0, 1, -120) == 74 &&
                  updraft_link_margin(0, 7, 2, -150) == 19 &&
                  updraft_link_margin(61, 0, 0, -60) == 55 &&
                  updraft_link_margin(62, 0, 1, -120) == UPDRAFT_REFUSED &&
                  updraft_link_margin(30, 8, 1, -120) == UPDRAFT_REFUSED &&
                  updraft_link_margin(30, 0, 3, -120) == UPDRAFT_REFUSED &&
                  updraft_link_margin(30, 0, 1, -59) == UPDRAFT_REFUSED &&
                  updraft_transmit_power_ok(61, 0) && !updraft_transmit_power_ok(30, -1) &&
                  !updraft_transmit_power_ok(30, 62) && updraft_relay_node_fits(1, 1, 0, 1, 0, 3) &&
                  !updraft_relay_node_fits(1, 1000, 0, 1, 0, 3) &&
                  updraft_frame_join(X, 0, false, 0, &s) == LPRES_STATE_NEITHER && s == -1 &&
                  updraft_frame_join(XYX, 3, false, 0, &s) == LPRES_STATE_TRUE && s == 1111 &&
                  updraft_frame_join(X, 2, true, 3333, &s) == LPRES_STATE_FALSE &&
                  updraft_counter_rate(5000, 0, 1000) == 5000 &&
                  updraft_counter_rate(4, 5, 10) == -1 && updraft_counter_rate(1, 0, 0) == -1 &&
                  updraft_counter_rate(INT64_C(1000000000000000000), 0, 1) == -1,
              "Updraft: the C rules' own edges (UR1-UR6)");
    }

    /* UR1: link_margin_db, Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, UPDRAFT_POWER_MAX);
        b[i] = wrand(0, UPDRAFT_STEPS - 1);
        c[i] = wrand(0, UPDRAFT_RF - 1);
        d[i] = wrand(UPDRAFT_FLOOR_MIN, UPDRAFT_FLOOR_MAX);
        if (i == 0)
            a[i] = 0, b[i] = 7, c[i] = 2,
            d[i] = UPDRAFT_FLOOR_MIN; /* the least power, the most loss */
        if (i == 1)
            a[i] = UPDRAFT_POWER_MAX, b[i] = 0, c[i] = 0, d[i] = UPDRAFT_FLOOR_MAX; /* the most */
        if (i == 2) a[i] = 30, b[i] = 3, c[i] = 1, d[i] = -60; /* below the floor */
        want[i] = updraft_link_margin(a[i], b[i], c[i], d[i]);
    }
    zxapp_arg_t ma[4] = {{"power", a, WAVE_ROWS},
                         {"step", b, WAVE_ROWS},
                         {"rf", c, WAVE_ROWS},
                         {"rxfloor", d, WAVE_ROWS}};
    CHECK(want[0] == 19 && want[1] == 55 && want[2] == -6 &&
              wave_same(app, "link_margin_db", ma, 4, want, WAVE_ROWS, false),
          "Updraft: link_margin_db, Trebago = KRON = C");
    {
        static const int64_t pw[2] = {30, UPDRAFT_POWER_MAX + 1}, st[2] = {0, 0}, rf[2] = {1, 1},
                             fl[2] = {-120, -120}, pw2[2] = {30, 30},
                             fl2[2] = {-120, UPDRAFT_FLOOR_MIN - 1}, st3[2] = {0, UPDRAFT_STEPS};
        zxapp_arg_t xa[4] = {{"power", pw, 2}, {"step", st, 2}, {"rf", rf, 2}, {"rxfloor", fl, 2}};
        zxapp_arg_t ya[4] = {
            {"power", pw2, 2}, {"step", st, 2}, {"rf", rf, 2}, {"rxfloor", fl2, 2}};
        zxapp_arg_t za[4] = {
            {"power", pw2, 2}, {"step", st3, 2}, {"rf", rf, 2}, {"rxfloor", fl, 2}};
        CHECK(wave_lp(app, "link_margin_db", xa, 4) == ZXAPP_BOTH,
              "Updraft: a power of 62 dBm: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "link_margin_db", ya, 4) == ZXAPP_BOTH,
              "Updraft: a floor below -150 dBm: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "link_margin_db", za, 4) != ZXAPP_TRUE,
              "Updraft: a step past the table is never an answer");
    }

    /* UR2: transmit_power_ok, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, UPDRAFT_POWER_MAX + 2);
        b[i] = wrand(-3, UPDRAFT_POWER_MAX);
        if (i == 0) a[i] = 0, b[i] = 0;
        if (i == 1) a[i] = UPDRAFT_POWER_MAX, b[i] = 0;
        if (i == 2) a[i] = 30, b[i] = -1; /* one dB past the licence */
        if (i == 3) a[i] = UPDRAFT_POWER_MAX + 1, b[i] = 0;
        want[i] = T_(updraft_transmit_power_ok(a[i], b[i]));
    }
    zxapp_arg_t ta[2] = {{"power", a, WAVE_ROWS}, {"headroom", b, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE &&
              wave_same(app, "transmit_power_ok", ta, 2, want, WAVE_ROWS, true),
          "Updraft: transmit_power_ok, Eve = KRON = C");
    {
        static const int64_t pw[2] = {30, 30}, hr[2] = {5, UPDRAFT_POWER_MAX + 1};
        zxapp_arg_t xa[2] = {{"power", pw, 2}, {"headroom", hr, 2}};
        CHECK(!updraft_transmit_power_ok(30, UPDRAFT_POWER_MAX + 1) &&
                  wave_lp(app, "transmit_power_ok", xa, 2) == ZXAPP_BOTH,
              "Updraft: a headroom of 62 dB is no licence: Eve refuses, KRON allows: BOTH");
    }

    /* UR3: radio_link_state, KRON = C, over a whole matrix of links */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 9);
        b[i] = wrand(0, 9);
        c[i] = wrand(-20, 20);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = updraft_link_state(a, b, c, WAVE_ROWS, i);
    zxapp_arg_t la[3] = {{"src", a, WAVE_ROWS}, {"dst", b, WAVE_ROWS}, {"margin", c, WAVE_ROWS}};
    CHECK(wave_same(app, "radio_link_state", la, 3, want, WAVE_ROWS, false),
          "Updraft: radio_link_state, KRON = C");
    {
        static const int64_t s[5] = {0, 1, 0, 2, 3}, t[5] = {1, 0, 2, 0, 4},
                             m[5] = {10, 5, 8, -3, 0}, st[5] = {2, 2, 1, 0, 1};
        zxapp_arg_t xa[3] = {{"src", s, 5}, {"dst", t, 5}, {"margin", m, 5}};
        CHECK(wave_same(app, "radio_link_state", xa, 3, st, 5, false) &&
                  updraft_link_state(s, t, m, 5, 2) == UPDRAFT_ONE_WAY,
              "Updraft: 0 and 1 hear each other, 2 does not hear 0 back: one-way");
    }

    /* UR4: relay_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1200000000, 1200000000);
        b[i] = wrand(-1200, 1200);
        c[i] = wrand(0, 1200);
        d[i] = wrand(0, 120);
        e[i] = wrand(0, 12000000000);
        f[i] = wrand(0, 12);
        if (i == 0)
            a[i] = UPDRAFT_FRAME_MAX, b[i] = 999, c[i] = 999, d[i] = 99, e[i] = 9999999999,
            f[i] = 3;                                                               /* max */
        if (i == 1) a[i] = 31, b[i] = 1000, c[i] = 0, d[i] = 1, e[i] = 0, f[i] = 1; /* past */
        want[i] = T_(updraft_relay_node_fits(a[i], b[i], c[i], d[i], e[i], f[i]));
    }
    zxapp_arg_t na[6] = {{"frame", a, WAVE_ROWS},   {"relay", b, WAVE_ROWS},
                         {"parent", c, WAVE_ROWS},  {"hops", d, WAVE_ROWS},
                         {"payload", e, WAVE_ROWS}, {"state", f, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "relay_node_ok", na, 6, want, WAVE_ROWS, true),
          "Updraft: relay_node_ok, MYRA = C");

    /* the parts, called through Updraft as it exports them */
    {
        bool all = true;
        for (uint32_t i = 0; i < 8u; i++)
            all = upd_tag_row(app, wrand(0, 1), wrand(0, 9), wrand(0, 9)) && all;
        int64_t sp = 0, sh = 0, tr = 0;
        CHECK(all && aerie_tag_split(123, &sp, &sh, &tr) && sp == 1 && sh == 2 && tr == 3,
              "Updraft: aerie.frame_tag, Forth = KRON = C, and the split back");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(AERIE_FRAME_MIN, AERIE_FRAME_MAX);
        a[i] = wrand(0, b[i] - AERIE_OVERHEAD);
        want[i] = T_(aerie_band_fits(a[i], b[i]));
    }
    zxapp_arg_t ba[2] = {{"bytes", a, WAVE_ROWS}, {"frame", b, WAVE_ROWS}};
    CHECK(wave_same(app, "aerie.band_fits", ba, 2, want, WAVE_ROWS, true),
          "Updraft: aerie.band_fits, a relayed message fits its band, Eve = KRON");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, AERIE_SEQ_MAX + 1);
        b[i] = wrand(0, 1);
        c[i] = wrand(0, 4);
        d[i] = wrand(0, 1);
        e[i] = wrand(0, 4294967295);
        f[i] = wrand(0, 3360);
        g[i] = wrand(0, AERIE_STAMP_MAX);
        want[i] = T_(aerie_msg_ok(a[i], b[i], c[i], d[i], e[i], f[i], g[i]));
    }
    zxapp_arg_t ra[7] = {{"seq", a, WAVE_ROWS},  {"bearer", b, WAVE_ROWS}, {"band", c, WAVE_ROWS},
                         {"dir", d, WAVE_ROWS},  {"sender", e, WAVE_ROWS}, {"bytes", f, WAVE_ROWS},
                         {"stamp", g, WAVE_ROWS}};
    CHECK(wave_same(app, "aerie.radio_msg_ok", ra, 7, want, WAVE_ROWS, true),
          "Updraft: aerie.radio_msg_ok, the message log record");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1);
        b[i] = wrand(0, 1);
        if (i == 0) a[i] = 1, b[i] = 1; /* sealed on amateur radio */
        want[i] = T_(aerie_ham_clear_ok(a[i], b[i]));
    }
    zxapp_arg_t ha[2] = {{"amateur", a, WAVE_ROWS}, {"sealed", b, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_FALSE &&
              wave_same(app, "aerie.ham_clear_ok", ha, 2, want, WAVE_ROWS, true),
          "Updraft: aerie.ham_clear_ok, no secrets on amateur radio, Sutra = KRON");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1000000000000);
        b[i] = i % 5u ? wrand(0, a[i]) : wrand(0, 1000000000000);
        c[i] = i % 7u ? wrand(1, 3600000) : 0;
        want[i] = updraft_counter_rate(a[i], b[i], c[i]);
    }
    zxapp_arg_t sa[3] = {{"now", a, WAVE_ROWS}, {"was", b, WAVE_ROWS}, {"dt", c, WAVE_ROWS}};
    CHECK(wave_same(app, "sounding.rate", sa, 3, want, WAVE_ROWS, false),
          "Updraft: sounding.rate, frames heard a second, wasm = Sutra");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(TRELLIS_ACTIVE, TRELLIS_WAITING);
        b[i] = TRELLIS_ACTIVE;
    }
    {
        int64_t w = trellis_peers_in_state(a, b, WAVE_ROWS);
        zxapp_arg_t pa[2] = {{"state", a, WAVE_ROWS}, {"want", b, WAVE_ROWS}};
        CHECK(w >= 0 && wave_same(app, "trellis.peers_in_state", pa, 2, &w, 1, false),
              "Updraft: trellis.peers_in_state, live peers, KRON = Trebago (%lld)", (long long) w);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 12);
        b[i] = wrand(1, 3);
        c[i] = wrand(1, 3);
        d[i] = 0;
        e[i] = 0;
        f[i] = wrand(0, 1000);
        want[i] = T_(trellis_peer_record_fits(a[i], b[i], c[i], d[i], e[i], f[i]));
    }
    zxapp_arg_t qa[6] = {{"peer", a, WAVE_ROWS},  {"state", b, WAVE_ROWS}, {"role", c, WAVE_ROWS},
                         {"admin", d, WAVE_ROWS}, {"held", e, WAVE_ROWS},  {"since", f, WAVE_ROWS}};
    CHECK(wave_same(app, "trellis.peer_record_ok", qa, 6, want, WAVE_ROWS, true),
          "Updraft: trellis.peer_record_ok, a relay station's roster record");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1), b[i] = wrand(0, 1), c[i] = wrand(0, 1);
        if (i == 0) a[i] = b[i] = c[i] = 1;
        want[i] = T_(commons_room_post_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t pa[3] = {
        {"member", a, WAVE_ROWS}, {"joined", b, WAVE_ROWS}, {"enabled", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE &&
              wave_same(app, "commons.room_post_ok", pa, 3, want, WAVE_ROWS, true),
          "Updraft: commons.room_post_ok, may a settled frame go to the bulletin room");

    /* the BOTH test: relay 1 hears frame 31 as payload X, relay 2 as payload
     * Y. The frame is BOTH and kept; a third relay hearing X, or the
     * frame's checksum naming Y, settles it. */
    {
        const int64_t X = 0x01230456, Y = 0x0789012a;
        int64_t heard[3] = {X, Y, X}, s = 0;
        CHECK(updraft_frame_join(heard, 2, false, 0, &s) == LPRES_STATE_BOTH && s == -1,
              "Updraft: frame 31 heard as X and as Y is BOTH, held");
        CHECK(updraft_frame_join(heard, 3, false, 0, &s) == LPRES_STATE_TRUE && s == X,
              "Updraft: a third relay hearing X settles frame 31 to X");
        CHECK(updraft_frame_join(heard, 2, true, Y, &s) == LPRES_STATE_TRUE && s == Y,
              "Updraft: the checksum naming Y settles frame 31 to Y");
        CHECK(lpres_kjoin(LPRES_STATE_TRUE, LPRES_STATE_FALSE) == LPRES_STATE_BOTH &&
                  lpres_conjoin(LPRES_STATE_BOTH, LPRES_STATE_TRUE) != LPRES_STATE_TRUE,
              "Updraft: BOTH is never TRUE, so a held frame is never posted");
        /* both reports file in the relay tree with the frame's state */
        static const int64_t fr[2] = {31, 31}, rl[2] = {1, 2}, pr[2] = {0, 0}, hp[2] = {1, 1},
                             st[2] = {LPRES_STATE_BOTH, LPRES_STATE_BOTH};
        int64_t py[2] = {X, Y}, w[2] = {ZXAPP_TRUE, ZXAPP_TRUE};
        zxapp_arg_t xa[6] = {{"frame", fr, 2}, {"relay", rl, 2},   {"parent", pr, 2},
                             {"hops", hp, 2},  {"payload", py, 2}, {"state", st, 2}};
        CHECK(wave_same(app, "relay_node_ok", xa, 6, w, 2, true),
              "Updraft: both reports of frame 31 file as BOTH in ^ZXUPDRAFT");
    }
    WSEED = seed_was;
}
