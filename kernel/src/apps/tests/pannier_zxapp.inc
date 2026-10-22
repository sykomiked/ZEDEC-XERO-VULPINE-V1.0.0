/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pannier's languages, witnessed, row by row against pannier_rules.h, the
 * C the model runs; the parts it composes (rookery, trellis, pollen,
 * ferry, caravan, cove, warren) resolved and called through Pannier; and
 * the BOTH of parcel 9, a signed receipt (sequence 5) and a hop report of
 * loss. Slots: @logic.pannier_parcel_send_now, @logic.pannier_parcel_retry_ok,
 * @num.pannier_route_hours, @num.pannier_parcel_queue_room,
 * @logic.pannier_parcel_node_ok; and, used, @logic.rookery_letter_send_ok,
 * @logic.rookery_letter_size_ok, @logic.rookery_letter_record_ok,
 * @num.trellis_peers_in_state, @logic.trellis_peer_record_ok,
 * @num.pollen_freight_count, @num.pollen_freight_packets,
 * @logic.ferry_target_fits, @logic.caravan_ship_ok,
 * @num.cove_tunnel_seq_gap, @num.warren_backoff. */
#include "pannier_rules.h"

/* route_hours over one route of n hops: Trebago alone (NEITHER), one exact
 * reduced fraction equal to the C's */
static bool pannier_route_row(zxapp_t *a, const int64_t *hop, uint32_t n)
{
    zxapp_arg_t ha[1] = {{"hop", hop, n}};
    zxapp_num_t o[WAVE_ROWS];
    uint32_t k = 0;
    int32_t lp = -1;
    int64_t num = 0, den = 0;
    int rc = zxapp_call_witnessed(a, &H, "route_hours", ha, 1, o, WAVE_ROWS, &k, &lp);
    if (rc != ZXAPP_OK || lp != ZXAPP_NEITHER || k != 1 ||
        !pannier_route_hours(hop, n, &num, &den)) {
        printf("    route_hours: rc %d lp %d n %u (%s)\n", rc, (int) lp, k, a->err);
        return false;
    }
    if (o[0].num != num || o[0].den != den) {
        printf("    route_hours: %lld/%lld, want %lld/%lld\n", (long long) o[0].num,
               (long long) o[0].den, (long long) num, (long long) den);
        return false;
    }
    return true;
}

APP_TEST(pannier, 53, 150)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], want[WAVE_ROWS];

    /* compose: seven parts, every part entry it calls resolves to its part */
    static const char *const used[11][2] = {{"rookery.letter_send_ok", "rookery"},
                                            {"rookery.letter_size_ok", "rookery"},
                                            {"rookery.letter_record_ok", "rookery"},
                                            {"trellis.peers_in_state", "trellis"},
                                            {"trellis.peer_record_ok", "trellis"},
                                            {"pollen.freight_count", "pollen"},
                                            {"pollen.freight_packets", "pollen"},
                                            {"ferry.target_fits", "ferry"},
                                            {"caravan.ship_ok", "caravan"},
                                            {"cove.tunnel_seq_gap", "cove"},
                                            {"warren.backoff", "warren"}};
    CHECK(app->resolved && app->nuse == 7 && app->composed == 6 && app->n == 53u &&
              app->n <= ZXAPP_LOGIC_MAX && app->nuse <= ZXAPP_USES,
          "Pannier: composed of 7 parts, 6 own lines and %u of theirs, within %u", app->n - 6u,
          (unsigned) ZXAPP_LOGIC_MAX);
    for (uint32_t i = 0; i < 11u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Pannier: %s resolves to %s and loads", used[i][0], used[i][1]);
    }

    /* PR1: parcel_send_now, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3u ? TRELLIS_ACTIVE : wrand(0, 4);
        b[i] = i % 2u ? wrand(-5, 5000) : wrand(0, PANNIER_PARCEL_MAX);
        c[i] = wrand(-1, PANNIER_TRIES + 2);
        if (i == 0) a[i] = TRELLIS_ACTIVE, b[i] = 0, c[i] = 1;
        if (i == 1) a[i] = TRELLIS_ACTIVE, b[i] = PANNIER_PARCEL_MAX, c[i] = PANNIER_TRIES;
        if (i == 2) a[i] = TRELLIS_ACTIVE, b[i] = 100, c[i] = PANNIER_TRIES + 1;
        if (i == 3) a[i] = TRELLIS_WAITING, b[i] = 100, c[i] = 1;
        if (i == 4) a[i] = TRELLIS_ACTIVE, b[i] = -1, c[i] = 1;
        want[i] = T_(pannier_send_now(a[i], b[i], c[i]));
    }
    zxapp_arg_t sa[3] = {
        {"peerstate", a, WAVE_ROWS}, {"room", b, WAVE_ROWS}, {"attempt", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE &&
              wave_same(app, "parcel_send_now", sa, 3, want, WAVE_ROWS, true),
          "Pannier: parcel_send_now, Eve = KRON = C");
    {
        static const int64_t act[2] = {1, 1}, room[2] = {100, PANNIER_PARCEL_MAX + 1},
                             one[2] = {1, 1};
        zxapp_arg_t s1[3] = {{"peerstate", act, 2}, {"room", room, 2}, {"attempt", one, 2}};
        CHECK(!pannier_send_now(1, PANNIER_PARCEL_MAX + 1, 1) &&
                  wave_lp(app, "parcel_send_now", s1, 3) == ZXAPP_BOTH,
              "Pannier: a room no parcel gives: Eve no, KRON yes: BOTH");
    }

    /* PR2: parcel_retry_ok, Eve = C (one language: unwitnessed) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1, PANNIER_TRIES + 2);
        b[i] = i % 2u ? pannier_wait(wrand(1, PANNIER_TRIES)) : wrand(-5, PANNIER_WAIT_MAX + 5);
        if (i == 0) a[i] = 1, b[i] = 0;
        if (i == 1) a[i] = PANNIER_TRIES, b[i] = PANNIER_WAIT_MAX;
        if (i == 2) a[i] = PANNIER_TRIES + 1, b[i] = 0;
        if (i == 3) a[i] = 2, b[i] = PANNIER_WAIT_MAX + 1;
        want[i] = T_(pannier_retry_ok(a[i], b[i]));
    }
    zxapp_arg_t ra[2] = {{"attempt", a, WAVE_ROWS}, {"backoff", b, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE &&
              wave_same(app, "parcel_retry_ok", ra, 2, want, WAVE_ROWS, true),
          "Pannier: parcel_retry_ok, Eve = C");
    {
        bool ok = pannier_wait(1) == 0 && pannier_wait(2) == PANNIER_WAIT_BASE &&
                  pannier_wait(3) == 2 * PANNIER_WAIT_BASE && pannier_wait(8) == 1920 &&
                  pannier_wait(9) == PANNIER_WAIT_MAX &&
                  pannier_wait(PANNIER_TRIES) == PANNIER_WAIT_MAX &&
                  pannier_wait(0) == PANNIER_REFUSED &&
                  pannier_wait(PANNIER_TRIES + 1) == PANNIER_REFUSED;
        for (int64_t k = 1; k <= PANNIER_TRIES; k++)
            ok = ok && pannier_retry_ok(k, pannier_wait(k));
        CHECK(ok, "Pannier: every wait warren.backoff gives is inside the retry limits");
    }

    /* PR4: route_hours, Trebago = C, exact */
    {
        bool all = true;
        for (uint32_t r = 0; r < WAVE_ROWS; r++) {
            uint32_t n = (uint32_t) wrand(1, (int64_t) PANNIER_HOPS);
            for (uint32_t i = 0; i < n; i++) a[i] = wrand(0, r % 2u ? 600 : PANNIER_HOP_MAX);
            if (r == 0) n = 2, a[0] = 90, a[1] = 60;
            if (r == 1) n = 1, a[0] = 0;
            if (r == 2) {
                n = PANNIER_HOPS;
                for (uint32_t i = 0; i < n; i++) a[i] = PANNIER_HOP_MAX;
            }
            all = all && pannier_route_row(app, a, n);
        }
        int64_t num = 0, den = 0, h2[2] = {90, 60};
        CHECK(all && pannier_route_hours(h2, 2, &num, &den) && num == 5 && den == 2,
              "Pannier: route_hours, Trebago = C, 64 routes, 150 minutes is 5/2 hours");
        static const int64_t neg[2] = {60, -1}, big[2] = {60, PANNIER_HOP_MAX + 1};
        zxapp_arg_t n1[1] = {{"hop", neg, 2}}, n2[1] = {{"hop", big, 2}};
        zxapp_num_t o[WAVE_ROWS];
        uint32_t k = 0;
        int32_t lp = -1;
        CHECK(!pannier_route_hours(neg, 2, &num, &den) &&
                  !pannier_route_hours(big, 2, &num, &den) &&
                  zxapp_call_witnessed(app, &H, "route_hours", n1, 1, o, WAVE_ROWS, &k, &lp) !=
                      ZXAPP_OK &&
                  zxapp_call_witnessed(app, &H, "route_hours", n2, 1, o, WAVE_ROWS, &k, &lp) !=
                      ZXAPP_OK,
              "Pannier: a negative hop or one past 99999 minutes: refused by Trebago and C");
    }

    /* PR5: parcel_queue_room, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        c[i] = i % 2u ? (int64_t) 16 : wrand(1, PANNIER_QUEUE_MAX);
        b[i] = wrand(0, INT64_C(999999999999));
        a[i] = b[i] + wrand(0, c[i]);
        if (i == 0) a[i] = 0, b[i] = 0, c[i] = 16;
        if (i == 1) a[i] = 16, b[i] = 0, c[i] = 16;
        if (i == 2) a[i] = 40, b[i] = 25, c[i] = 16;
        want[i] = pannier_queue_room(a[i], b[i], c[i]);
    }
    zxapp_arg_t qa[3] = {{"head", a, WAVE_ROWS}, {"tail", b, WAVE_ROWS}, {"cap", c, WAVE_ROWS}};
    CHECK(want[0] == 16 && want[1] == 0 && want[2] == 1 &&
              pannier_queue_room(17, 0, 16) == PANNIER_REFUSED &&
              pannier_queue_room(0, 1, 16) == PANNIER_REFUSED &&
              pannier_queue_room(0, 0, 0) == PANNIER_REFUSED &&
              wave_same(app, "parcel_queue_room", qa, 3, want, WAVE_ROWS, false),
          "Pannier: parcel_queue_room, KRON = C; 0 is backpressure");

    /* PR6: parcel_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-12, 12);
        b[i] = i % 4u ? wrand(1, 999999999) : wrand(0, INT64_C(1200000000));
        c[i] = i % 5u ? wrand(0, 999999) : wrand(0, 1200000);
        d[i] = i % 3u ? wrand(1, PANNIER_PARCEL_MAX) : wrand(0, INT64_C(120000000));
        e[i] = wrand(-1, 120);
        f[i] = wrand(-3, 12);
        g[i] = i % 6u ? wrand(0, INT64_C(999999999999)) : wrand(0, INT64_C(1200000000000));
        if (i == 0)
            a[i] = 1, b[i] = 9, c[i] = 0, d[i] = 4096, e[i] = 1, f[i] = 3, g[i] = 1792627200;
        if (i == 1) a[i] = 10, b[i] = 9, c[i] = 0, d[i] = 4096, e[i] = 1, f[i] = 3, g[i] = 0;
        want[i] = T_(pannier_node_ok(a[i], b[i], c[i], d[i], e[i], f[i], g[i]));
    }
    zxapp_arg_t na[7] = {{"peer", a, WAVE_ROWS},    {"parcel", b, WAVE_ROWS},
                         {"freight", c, WAVE_ROWS}, {"size", d, WAVE_ROWS},
                         {"attempt", e, WAVE_ROWS}, {"state", f, WAVE_ROWS},
                         {"stamp", g, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "parcel_node_ok", na, 7, want, WAVE_ROWS, true),
          "Pannier: parcel_node_ok, MYRA = C");

    /* the parts, called through Pannier as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1);
        b[i] = wrand(-5, ROOKERY_PAYLOAD_MAX);
        c[i] = i % 3u ? 0 : wrand(0, 2);
        want[i] = T_(rookery_letter_send_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t la[3] = {
        {"verified", a, WAVE_ROWS}, {"room", b, WAVE_ROWS}, {"consentgap", c, WAVE_ROWS}};
    CHECK(wave_same(app, "rookery.letter_send_ok", la, 3, want, WAVE_ROWS, true),
          "Pannier: rookery.letter_send_ok, may this be sent");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, ROOKERY_SUBJECT_MAX + 2);
        b[i] = wrand(-2, ROOKERY_BODY_MAX + 2);
        want[i] = T_(rookery_letter_size_ok(a[i], b[i]));
    }
    zxapp_arg_t za[2] = {{"subject", a, WAVE_ROWS}, {"content", b, WAVE_ROWS}};
    CHECK(wave_same(app, "rookery.letter_size_ok", za, 2, want, WAVE_ROWS, true),
          "Pannier: rookery.letter_size_ok, a message's size limits");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 12);
        b[i] = wrand(1, INT64_C(12000000000));
        c[i] = wrand(1, INT64_C(12000000000));
        d[i] = wrand(0, 12000);
        e[i] = wrand(0, INT64_C(1200000000000));
        want[i] = T_(rookery_letter_record_ok(a[i], b[i], c[i], d[i], e[i]));
    }
    zxapp_arg_t xa[5] = {{"folder", a, WAVE_ROWS},
                         {"thread", b, WAVE_ROWS},
                         {"letter", c, WAVE_ROWS},
                         {"size", d, WAVE_ROWS},
                         {"stamp", e, WAVE_ROWS}};
    CHECK(wave_same(app, "rookery.letter_record_ok", xa, 5, want, WAVE_ROWS, true),
          "Pannier: rookery.letter_record_ok, a message's index line");
    {
        bool all = true;
        for (uint32_t r = 0; r < 16u; r++) {
            uint32_t n = (uint32_t) wrand(2, TRELLIS_DEVICES);
            for (uint32_t i = 0; i < n; i++) a[i] = wrand(1, 3), b[i] = TRELLIS_ACTIVE;
            int64_t w = trellis_peers_in_state(a, b, n);
            zxapp_arg_t pa[2] = {{"state", a, n}, {"want", b, n}};
            all = all && w != TRELLIS_REFUSED &&
                  wave_same(app, "trellis.peers_in_state", pa, 2, &w, 1, false);
        }
        CHECK(all, "Pannier: trellis.peers_in_state, the active carrier peers");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-12, 12);
        b[i] = wrand(1, 3);
        c[i] = 1;
        d[i] = 0;
        e[i] = 0;
        f[i] = i % 4u ? wrand(0, INT64_C(1900000000000)) : 1;
        want[i] = T_(trellis_peer_record_fits(a[i], b[i], c[i], d[i], e[i], f[i]));
    }
    zxapp_arg_t pr[6] = {{"peer", a, WAVE_ROWS},  {"state", b, WAVE_ROWS}, {"role", c, WAVE_ROWS},
                         {"admin", d, WAVE_ROWS}, {"held", e, WAVE_ROWS},  {"since", f, WAVE_ROWS}};
    CHECK(wave_same(app, "trellis.peer_record_ok", pr, 6, want, WAVE_ROWS, true),
          "Pannier: trellis.peer_record_ok, a carrier peer's record");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? wrand(0, PANNIER_PARCEL_MAX) : wrand(0, 5000);
        b[i] = wrand(1, POLLEN_K_MAX);
        want[i] = pollen_freights(a[i], b[i]);
        c[i] = pollen_packets(a[i], b[i]);
    }
    zxapp_arg_t fa[2] = {{"size", a, WAVE_ROWS}, {"k", b, WAVE_ROWS}};
    CHECK(wave_same(app, "pollen.freight_count", fa, 2, want, WAVE_ROWS, false) &&
              wave_same(app, "pollen.freight_packets", fa, 2, c, WAVE_ROWS, false),
          "Pannier: pollen.freight_count and freight_packets, the chunk plan");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, INT64_C(1000000000));
        b[i] = wrand(1, PANNIER_PARCEL_MAX);
        c[i] = wrand(0, INT64_C(100000000));
        want[i] = T_(ferry_fits(a[i], b[i], c[i]));
    }
    zxapp_arg_t ta[3] = {{"avail", a, WAVE_ROWS}, {"need", b, WAVE_ROWS}, {"spare", c, WAVE_ROWS}};
    CHECK(wave_same(app, "ferry.target_fits", ta, 3, want, WAVE_ROWS, true),
          "Pannier: ferry.target_fits, fits on the receiver");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, PANNIER_PARCEL_NO);
        b[i] = wrand(1, TRELLIS_DEVICES);
        c[i] = wrand(1, PANNIER_PARCEL_MAX);
        d[i] = wrand(1, 1000000);
        e[i] = wrand(CARAVAN_DRAFTED, CARAVAN_RETURNED);
        f[i] = wrand(0, CARAVAN_STAMP_MAX);
        want[i] = T_(caravan_ship_ok(a[i], b[i], c[i], d[i], e[i], f[i]));
    }
    zxapp_arg_t ca[6] = {{"ship", a, WAVE_ROWS},   {"carrier", b, WAVE_ROWS},
                         {"mass", c, WAVE_ROWS},   {"charged", d, WAVE_ROWS},
                         {"status", e, WAVE_ROWS}, {"stamp", f, WAVE_ROWS}};
    CHECK(wave_same(app, "caravan.ship_ok", ca, 6, want, WAVE_ROWS, true),
          "Pannier: caravan.ship_ok, a parcel's shipment record");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, INT64_C(999999999));
        b[i] = a[i] + wrand(1, 1000);
        if (i == 0) a[i] = 0, b[i] = 1;
        if (i == 1) a[i] = 4, b[i] = 5;
        want[i] = cove_tunnel_seq_gap(a[i], b[i]);
    }
    zxapp_arg_t ga[2] = {{"last", a, WAVE_ROWS}, {"seq", b, WAVE_ROWS}};
    CHECK(want[0] == 1 && want[1] == 1 && cove_tunnel_seq_gap(5, 5) == COVE_REFUSED &&
              wave_same(app, "cove.tunnel_seq_gap", ga, 2, want, WAVE_ROWS, false),
          "Pannier: cove.tunnel_seq_gap, a receipt's sequence rises; reuse refused");
    {
        int64_t t[1] = {PANNIER_TRIES - 1}, s[1] = {PANNIER_WAIT_BASE}, m[1] = {PANNIER_WAIT_MAX};
        for (int64_t k = 1; k <= t[0]; k++) want[k - 1] = pannier_wait(k + 1);
        zxapp_arg_t ba[3] = {{"tries", t, 1}, {"base", s, 1}, {"most", m, 1}};
        CHECK(wave_same(app, "warren.backoff", ba, 3, want, (uint32_t) t[0], false),
              "Pannier: warren.backoff, the waits before attempts 2..16");
    }

    /* PR7, the BOTH test: parcel 9 has a signed receipt (sequence 5) and a
     * hop report saying lost: BOTH, neither sent again nor closed until a
     * witness settles it */
    {
        lpres_state_t st = pannier_delivery(1, 1, PANNIER_SAID_NONE);
        CHECK(cove_tunnel_seq_gap(4, 5) == 1 && st == LPRES_STATE_BOTH && (int) st == 3 &&
                  lpres_conjoin(st, LPRES_STATE_TRUE) != LPRES_STATE_TRUE,
              "Pannier: parcel 9, receipt 5 and a hop report of loss: BOTH");
        CHECK(pannier_delivery(2, 1, PANNIER_SAID_NONE) == LPRES_STATE_TRUE &&
                  pannier_delivery(1, 1, PANNIER_SAID_DELIVERED) == LPRES_STATE_TRUE &&
                  pannier_delivery(1, 1, PANNIER_SAID_LOST) == LPRES_STATE_FALSE &&
                  pannier_delivery(1, 0, PANNIER_SAID_NONE) == LPRES_STATE_TRUE &&
                  pannier_delivery(0, 1, PANNIER_SAID_NONE) == LPRES_STATE_FALSE &&
                  pannier_delivery(0, 0, PANNIER_SAID_NONE) == LPRES_STATE_NEITHER,
              "Pannier: a second receipt or the recipient settles it; one source alone stands");
    }
}
