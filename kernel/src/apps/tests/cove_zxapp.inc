/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Cove's languages against the C its model runs (cove_rules.h).
 * Slots: @logic.cove_tunnel_open_ok, @num.cove_tunnel_seq_gap,
 * @logic.cove_tunnel_frame_ok, @logic.cove_tunnel_record_ok, and
 * Trellis's @num.trellis_peers_in_state, the active devices. */
#include "cove_rules.h"
#include "trellis_rules.h"

APP_TEST(cove, 12, 32)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], want[WAVE_ROWS];

    /* tunnel_open_ok (CR1): Eve = KRON = C over flags 0 and 1 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1);
        b[i] = wrand(0, 1);
        c[i] = wrand(0, 1);
        d[i] = wrand(0, 1);
    }
    a[0] = 1, b[0] = 0, c[0] = 1, d[0] = 0; /* both active, neither revoked: open */
    a[1] = 0, b[1] = 0, c[1] = 1, d[1] = 0; /* this device not active */
    a[2] = 1, b[2] = 1, c[2] = 1, d[2] = 0; /* this device revoked */
    a[3] = 1, b[3] = 0, c[3] = 0, d[3] = 0; /* the far device not on the roster */
    a[4] = 1, b[4] = 0, c[4] = 1, d[4] = 1; /* the far device revoked */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(cove_tunnel_open_ok(a[i], b[i], c[i], d[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE &&
              !cove_tunnel_open_ok(2, 0, 1, 0) && !cove_tunnel_open_ok(1, -1, 1, 0) &&
              !cove_tunnel_open_ok(1, 0, 1, 2),
          "Cove: the C rule's own edges (CR1)");
    zxapp_arg_t oa[4] = {{"selfactive", a, WAVE_ROWS},
                         {"selfrevoked", b, WAVE_ROWS},
                         {"peeractive", c, WAVE_ROWS},
                         {"peerrevoked", d, WAVE_ROWS}};
    CHECK(wave_same(app, "tunnel_open_ok", oa, 4, want, WAVE_ROWS, true),
          "Cove: tunnel_open_ok, Eve = KRON = C");
    {
        /* max + 1 and negatives: Eve holds each flag to its range, KRON reads past them */
        static const int64_t one[2] = {1, 1}, zero[2] = {0, 0}, two[2] = {1, 2}, neg[2] = {0, -1};
        zxapp_arg_t xa[4] = {{"selfactive", two, 2},
                             {"selfrevoked", zero, 2},
                             {"peeractive", one, 2},
                             {"peerrevoked", zero, 2}};
        CHECK(wave_lp(app, "tunnel_open_ok", xa, 4) == ZXAPP_BOTH,
              "Cove: an active flag of 2: Eve false, KRON true: BOTH");
        zxapp_arg_t na[4] = {{"selfactive", one, 2},
                             {"selfrevoked", zero, 2},
                             {"peeractive", one, 2},
                             {"peerrevoked", neg, 2}};
        CHECK(wave_lp(app, "tunnel_open_ok", na, 4) == ZXAPP_BOTH,
              "Cove: a revoked flag of -1: Eve false, KRON true: BOTH");
    }

    /* tunnel_seq_gap (CR2): Sutra = Rust = C over rising seqs inside 9(18) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, COVE_SEQ_MAX - 1000000);
        b[i] = a[i] + wrand(1, 1000000);
    }
    a[0] = 0, b[0] = 1;                           /* the first frame */
    a[1] = 41, b[1] = 42;                         /* the next frame */
    a[2] = 0, b[2] = COVE_SEQ_MAX;                /* the widest gap */
    a[3] = COVE_SEQ_MAX - 1, b[3] = COVE_SEQ_MAX; /* the last seq of a channel */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = cove_tunnel_seq_gap(a[i], b[i]);
    CHECK(want[0] == 1 && want[1] == 1 && want[2] == COVE_SEQ_MAX && want[3] == 1 &&
              cove_tunnel_seq_gap(5, 5) == COVE_REFUSED &&
              cove_tunnel_seq_gap(5, 4) == COVE_REFUSED &&
              cove_tunnel_seq_gap(-1, 1) == COVE_REFUSED &&
              cove_tunnel_seq_gap(0, COVE_SEQ_MAX + 1) == COVE_REFUSED,
          "Cove: the C rule's own edges (CR2): reuse, replay, negative, overflow refused");
    zxapp_arg_t sa[2] = {{"last", a, WAVE_ROWS}, {"seq", b, WAVE_ROWS}};
    CHECK(wave_same(app, "tunnel_seq_gap", sa, 2, want, WAVE_ROWS, false),
          "Cove: tunnel_seq_gap, Sutra = Rust = C");
    {
        /* max + 1: Sutra's 9(18) refuses a seq of nineteen digits, Rust answers it */
        static const int64_t last[2] = {0, 1}, past[2] = {1, INT64_C(1000000000000000000)};
        zxapp_arg_t pa[2] = {{"last", last, 2}, {"seq", past, 2}};
        CHECK(wave_lp(app, "tunnel_seq_gap", pa, 2) == ZXAPP_BOTH,
              "Cove: a seq past 9(18): Sutra refuses, Rust answers: BOTH");
        /* a reuse, a replay and a negative last: both refuse, so nothing is TRUE */
        static const int64_t rl[3] = {5, 5, -1}, rs[3] = {5, 4, 1};
        zxapp_arg_t ra[2] = {{"last", rl, 3}, {"seq", rs, 3}};
        int32_t lp = wave_lp(app, "tunnel_seq_gap", ra, 2);
        CHECK(lp != ZXAPP_TRUE && lp != ZXAPP_BOTH,
              "Cove: a reused, replayed or negative seq is never answered (lp %d)", (int) lp);
    }

    /* tunnel_frame_ok (CR3): Eve = C over frames and the room their band leaves */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(COVE_FRAME_MIN - 2, COVE_FRAME_MAX + 2);
        b[i] = wrand(-2, COVE_PAYLOAD_MAX + 2);
    }
    a[0] = COVE_FRAME_MIN, b[0] = 0;        /* no data, a band exactly that wide */
    a[1] = COVE_FRAME_MAX, b[1] = 0;        /* the widest frame fills the chat band */
    a[2] = 560, b[2] = 0;                   /* 512 bytes fill the control band */
    a[3] = COVE_FRAME_MIN - 1, b[3] = 0;    /* shorter than the header and tags */
    a[4] = COVE_FRAME_MAX + 1, b[4] = 0;    /* past EHOP_MAX_FRAME */
    a[5] = 561, b[5] = -1;                  /* one byte wider than its band */
    a[6] = 48, b[6] = COVE_PAYLOAD_MAX;     /* the most room */
    a[7] = 48, b[7] = COVE_PAYLOAD_MAX + 1; /* room past any band */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(cove_tunnel_frame_ok(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_TRUE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_TRUE && want[7] == ZXAPP_FALSE,
          "Cove: the C rule's own edges (CR3)");
    zxapp_arg_t fa[2] = {{"framelen", a, WAVE_ROWS}, {"room", b, WAVE_ROWS}};
    CHECK(wave_same(app, "tunnel_frame_ok", fa, 2, want, WAVE_ROWS, true),
          "Cove: tunnel_frame_ok, Eve = C");

    /* tunnel_record_ok (CR4): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 99), b[i] = wrand(0, 9), c[i] = wrand(0, 9);
        d[i] = wrand(0, UINT32_MAX), e[i] = wrand(0, UINT32_MAX);
        f[i] = wrand(0, COVE_SEQ_MAX), g[i] = wrand(0, COVE_STAMP_MAX);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0, f[0] = 0, g[0] = 0; /* every least */
    a[1] = 99, b[1] = 9, c[1] = 9, d[1] = INT64_C(9999999999), e[1] = INT64_C(9999999999);
    f[1] = COVE_SEQ_MAX, g[1] = COVE_STAMP_MAX; /* every most */
    a[2] = 100;                                 /* tunnel max + 1 */
    b[3] = 10;                                  /* peer max + 1 */
    c[4] = 10;                                  /* band max + 1 */
    d[5] = INT64_C(10000000000);                /* chan max + 1 */
    e[6] = INT64_C(10000000000);                /* epoch max + 1 */
    f[7] = COVE_SEQ_MAX + 1;                    /* seq max + 1 */
    g[8] = COVE_STAMP_MAX + 1;                  /* stamp max + 1 */
    a[9] = -5, f[9] = -1;                       /* negative: sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(cove_tunnel_record_ok(a[i], b[i], c[i], d[i], e[i], f[i], g[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE &&
              want[9] == ZXAPP_TRUE,
          "Cove: the record's own edges (CR4)");
    zxapp_arg_t ka[7] = {{"tunnel", a, WAVE_ROWS}, {"peer", b, WAVE_ROWS},  {"band", c, WAVE_ROWS},
                         {"chan", d, WAVE_ROWS},   {"epoch", e, WAVE_ROWS}, {"seq", f, WAVE_ROWS},
                         {"stamp", g, WAVE_ROWS}};
    CHECK(wave_same(app, "tunnel_record_ok", ka, 7, want, WAVE_ROWS, true),
          "Cove: tunnel_record_ok, MYRA = C");

    /* trellis.peers_in_state (CM6): the active devices, KRON = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(TRELLIS_ACTIVE, TRELLIS_REVOKED);
        b[i] = TRELLIS_ACTIVE;
    }
    want[0] = trellis_peers_in_state(a, b, WAVE_ROWS);
    CHECK(want[0] >= 0 && want[0] <= WAVE_ROWS, "Cove: the C count of active devices");
    zxapp_arg_t pa[2] = {{"state", a, WAVE_ROWS}, {"want", b, WAVE_ROWS}};
    CHECK(wave_same(app, "trellis.peers_in_state", pa, 2, want, 1, false),
          "Cove: trellis.peers_in_state, KRON = Trebago = C");
}
