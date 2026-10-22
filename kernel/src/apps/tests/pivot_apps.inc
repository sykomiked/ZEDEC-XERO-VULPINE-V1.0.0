/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pivot's model: zxvd's clock (sys.info) through the real dispatcher
 * (LINK), then a clock of its own (PIV_LINK) that answers sys.info at the
 * time set below, so a scan can be timed exactly. Gaze and switches come
 * from the test, as the view would hand them (zxvd has no method for
 * either). */
#include "pivot_model.h"

static zxm_link_t PIV_LINK;
static int64_t piv_time = 1700000000000; /* ms since 1970 */
static uint32_t piv_calls;
static bool piv_bad; /* answer a clock that is no clock */

static int32_t piv_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    (void) len;
    piv_calls++;
    if (!strstr(req, "\"sys.info\""))
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32601,"
                        "\"message\":\"not here\"}}",
                        l->seq);
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":%lld}}",
                    l->seq, piv_bad ? -5LL : (long long) piv_time);
}

/* hand the model a run of samples at (x, y) from t0 to t1 every 20 ms */
static int piv_look(pivot_t *p, int32_t x, int32_t y, int64_t t0, int64_t t1)
{
    int rc = ZXM_OK;
    for (int64_t t = t0; t <= t1 && rc == ZXM_OK; t += 20) rc = pivot_gaze(p, x, y, t);
    return rc;
}

APPS_TEST(pivot)
{
    static pivot_t P, Q;
    static const pivot_rect_t pay = {100, 100, 120, 60}, ok = {400, 100, 80, 60},
                              small = {0, 0, 43, 60};
    int out = -1;
    int32_t item = -9, tgt = -9;
    int64_t now = 0;

    pivot_init(&P, &LINK);
    CHECK(P.prof.dwell_ms == 1000 && P.prof.switches == 1 && P.armed == -1 && !P.camera,
          "pivot: a default profile, nothing armed, no camera");

    /* PM6: zxvd's own clock through the real dispatcher */
    CHECK(pivot_clock(&P, &now) == ZXM_OK && now > 1600000000000 && P.clock_ms == now,
          "pivot: sys.info time_ms is zxvd's clock: %s", LINK.err);
    CHECK(pivot_scan_start(&P, 4, 0) == ZXM_OK && P.scanning && P.start_ms >= now,
          "pivot: a scan starts on zxvd's clock");
    CHECK(pivot_switch(&P, PIVOT_SWITCH_1, &out, &item) == ZXM_OK && out == PIVOT_FIRE &&
              item >= 0 && item < 4 && P.fired == 1,
          "pivot: one switch selects what is lit, timed by zxvd's clock");

    /* PM1: the profile on the owner's word only */
    pivot_profile_t pr = {.number = 2,
                          .switches = 2,
                          .order = PIVOT_ORDER_LINEAR,
                          .dwell_ms = 800,
                          .step_ms = 600,
                          .side_px = 44};
    CHECK(pivot_set_profile(&P, &pr, false) == ZXM_E_STATE && P.prof.number == 1 &&
              P.prof.dwell_ms == 1000 && P.scanning,
          "pivot: without the owner's word nothing changes");
    pivot_profile_t bad = pr;
    bad.dwell_ms = 199;
    CHECK(pivot_set_profile(&P, &bad, true) == ZXM_E_ARG && P.prof.number == 1,
          "pivot: a dwell below 200 ms is refused before the owner is asked");
    bad = pr;
    bad.step_ms = 10001;
    pivot_profile_t bad2 = pr;
    bad2.side_px = 43;
    pivot_profile_t bad3 = pr;
    bad3.switches = 3;
    pivot_profile_t bad4 = pr;
    bad4.number = 100;
    CHECK(pivot_set_profile(&P, &bad, true) == ZXM_E_ARG &&
              pivot_set_profile(&P, &bad2, true) == ZXM_E_ARG &&
              pivot_set_profile(&P, &bad3, true) == ZXM_E_ARG &&
              pivot_set_profile(&P, &bad4, true) == ZXM_E_ARG &&
              pivot_set_profile(&P, NULL, true) == ZXM_E_ARG && P.prof.number == 1,
          "pivot: a step, side, switch count or number past its limit is refused");
    CHECK(pivot_set_profile(&P, &pr, true) == ZXM_OK && P.prof.number == 2 &&
              P.prof.dwell_ms == 800 && !P.scanning,
          "pivot: the owner sets a profile; the scan ends");

    /* PM2: gaze only while the camera is ready, never back in time, bounded */
    CHECK(pivot_gaze(&P, 150, 120, 0) == ZXM_E_STATE && pivot_samples(&P) == 0,
          "pivot: no camera, no gaze");
    CHECK(pivot_camera(&P, 1, 1, 2) == ZXM_OK && !P.camera && pivot_camera(&P, 1, 1, 1) == ZXM_OK &&
              P.camera,
          "pivot: the camera is ready only on pinhole.camera_ready");
    CHECK(pivot_gaze(&P, 150, 120, 1000) == ZXM_OK && pivot_gaze(&P, 150, 120, 999) == ZXM_E_ARG &&
              pivot_gaze(&P, 150, 120, -1) == ZXM_E_ARG && pivot_samples(&P) == 1,
          "pivot: a sample going back in time is refused");
    {
        int rc = ZXM_OK;
        uint32_t n = 1;
        while (rc == ZXM_OK && n < PIVOT_SAMPLES + 2u) {
            rc = pivot_gaze(&P, 150, 120, 1000 + n);
            n += rc == ZXM_OK;
        }
        CHECK(rc == ZXM_E_STATE && pivot_samples(&P) == PIVOT_SAMPLES && n == PIVOT_SAMPLES,
              "pivot: a full ring is backpressure, never overwritten");
        CHECK(pivot_camera(&P, 0, 1, 1) == ZXM_OK && pivot_samples(&P) == 0,
              "pivot: a camera that stops drops its samples");
        pivot_camera(&P, 1, 1, 1);
    }

    /* PM3: dwell to select a plain target */
    CHECK(piv_look(&P, 300, 300, 0, 400) == ZXM_OK &&        /* elsewhere first */
              piv_look(&P, 150, 120, 420, 1200) == ZXM_OK && /* 780 ms on it */
              pivot_dwell(&P, 7, &pay, false, &out) == ZXM_OK && out == PIVOT_WAIT &&
              pivot_samples(&P) == 40,
          "pivot: 780 ms of an 800 ms dwell is not yet a select; the look away is dropped");
    CHECK(piv_look(&P, 150, 120, 1220, 1220) == ZXM_OK &&
              pivot_dwell(&P, 7, &pay, false, &out) == ZXM_OK && out == PIVOT_FIRE &&
              pivot_samples(&P) == 0 && P.fired == 2,
          "pivot: 800 ms inside fires a plain action, and spends the run");
    CHECK(piv_look(&P, 150, 120, 2000, 2500) == ZXM_OK &&
              piv_look(&P, 100, 159, 2520, 2520) == ZXM_OK && /* the corner: still inside */
              piv_look(&P, 150, 120, 2540, 2800) == ZXM_OK &&
              pivot_dwell(&P, 7, &pay, false, &out) == ZXM_OK && out == PIVOT_FIRE,
          "pivot: a sample on the target's last pixel is inside");
    CHECK(piv_look(&P, 150, 120, 3000, 3500) == ZXM_OK &&
              piv_look(&P, 99, 120, 3520, 3520) == ZXM_OK && /* one pixel outside */
              piv_look(&P, 150, 120, 3540, 4000) == ZXM_OK &&
              pivot_dwell(&P, 7, &pay, false, &out) == ZXM_OK && out == PIVOT_WAIT,
          "pivot: one sample outside starts the dwell again");
    CHECK(pivot_dwell(&P, 7, &small, false, &out) == ZXM_E_ARG &&
              pivot_dwell(&P, -1, &pay, false, &out) == ZXM_E_ARG &&
              pivot_dwell(&P, 7, NULL, false, &out) == ZXM_E_ARG,
          "pivot: a target under 44 px is refused");

    /* PM4: a held action is armed by a dwell, fired only by a second one on confirm */
    P.tail = P.head;
    CHECK(pivot_confirm(&P, &ok, &out, &tgt) == ZXM_E_STATE, "pivot: nothing armed to confirm");
    CHECK(piv_look(&P, 150, 120, 5000, 5800) == ZXM_OK &&
              pivot_dwell(&P, 3, &pay, true, &out) == ZXM_OK && out == PIVOT_ARMED &&
              P.armed == 3 && P.fired == 3,
          "pivot: a dwell never confirms a held action (pay): it arms it");
    CHECK(piv_look(&P, 150, 120, 6000, 6800) == ZXM_OK &&
              pivot_confirm(&P, &ok, &out, &tgt) == ZXM_OK && out == PIVOT_WAIT && tgt == -1 &&
              P.armed == 3 && P.fired == 3,
          "pivot: a dwell on the action again is no confirm");
    CHECK(piv_look(&P, 440, 130, 7000, 7600) == ZXM_OK &&
              pivot_confirm(&P, &ok, &out, &tgt) == ZXM_OK && out == PIVOT_WAIT && P.armed == 3,
          "pivot: a short look at confirm is no confirm");
    CHECK(piv_look(&P, 440, 130, 7620, 7800) == ZXM_OK &&
              pivot_confirm(&P, &ok, &out, &tgt) == ZXM_OK && out == PIVOT_FIRE && tgt == 3 &&
              P.armed == -1 && P.fired == 4,
          "pivot: a second dwell on the confirm target fires the held action");
    CHECK(piv_look(&P, 150, 120, 8000, 8800) == ZXM_OK &&
              pivot_dwell(&P, 3, &pay, true, &out) == ZXM_OK && out == PIVOT_ARMED &&
              piv_look(&P, 150, 120, 9000, 9800) == ZXM_OK &&
              pivot_dwell(&P, 5, &pay, false, &out) == ZXM_OK && out == PIVOT_FIRE &&
              P.armed == -1 && pivot_confirm(&P, &ok, &out, &tgt) == ZXM_E_STATE,
          "pivot: another select disarms a held action");

    /* PM5: two switches, linear: switch 1 steps, switch 2 selects */
    piv_time = 1700000000000;
    piv_calls = 0;
    zxm_link_init(&PIV_LINK, piv_send, &PIV_LINK);
    pivot_init(&Q, &PIV_LINK);
    CHECK(pivot_set_profile(&Q, &pr, true) == ZXM_OK, "pivot: two switches");
    int32_t lit = -1;
    uint32_t count = 0;
    CHECK(pivot_switch(&Q, PIVOT_SWITCH_1, &out, &item) == ZXM_E_STATE && piv_calls == 0,
          "pivot: no scan, no press, nothing sent");
    CHECK(pivot_scan_start(&Q, 5, 1u << 3) == ZXM_OK && piv_calls == 1 && Q.start_ms == piv_time &&
              pivot_scan_start(&Q, 0, 0) == ZXM_E_ARG && pivot_scan_start(&Q, 65, 0) == ZXM_E_ARG &&
              pivot_scan_start_at(&Q, 3, 1u << 3, piv_time) == ZXM_E_ARG && piv_calls == 1 &&
              Q.items == 5,
          "pivot: five items, the fourth held; a bad scan changes nothing and sends nothing");
    CHECK(pivot_switch(&Q, 3, &out, &item) == ZXM_E_ARG, "pivot: there is no switch 3");
    piv_time += 999999; /* hand stepping ignores the time */
    CHECK(pivot_scan_lit(&Q, piv_time, &lit, &count) == ZXM_OK && lit == 0 && count == 5,
          "pivot: the first item is lit until switch 1 steps");
    for (int i = 0; i < 3; i++) pivot_switch(&Q, PIVOT_SWITCH_1, &out, &item);
    CHECK(pivot_scan_lit(&Q, piv_time, &lit, &count) == ZXM_OK && lit == 3,
          "pivot: three presses light item 3");
    CHECK(pivot_switch(&Q, PIVOT_SWITCH_2, &out, &item) == ZXM_OK && out == PIVOT_ARMED &&
              item == 3 && Q.armed == 3 && Q.confirming && Q.fired == 0,
          "pivot: selecting the held item arms it");
    CHECK(pivot_scan_lit(&Q, piv_time, &lit, &count) == ZXM_OK && lit == 0 && count == 2,
          "pivot: cancel is lit first");
    CHECK(pivot_switch(&Q, PIVOT_SWITCH_2, &out, &item) == ZXM_OK && out == PIVOT_CANCEL &&
              item == 3 && Q.armed == -1 && !Q.confirming && Q.fired == 0,
          "pivot: selecting cancel fires nothing");
    for (int i = 0; i < 3; i++) pivot_switch(&Q, PIVOT_SWITCH_1, &out, &item);
    pivot_switch(&Q, PIVOT_SWITCH_2, &out, &item);
    pivot_switch(&Q, PIVOT_SWITCH_1, &out, &item);
    CHECK(pivot_switch(&Q, PIVOT_SWITCH_2, &out, &item) == ZXM_OK && out == PIVOT_FIRE &&
              item == 3 && Q.fired == 1 && Q.armed == -1,
          "pivot: the held item fires only on its confirm");
    for (int i = 0; i < 6; i++) pivot_switch(&Q, PIVOT_SWITCH_1, &out, &item);
    CHECK(pivot_switch(&Q, PIVOT_SWITCH_2, &out, &item) == ZXM_OK && out == PIVOT_FIRE &&
              item == 1 && Q.fired == 2,
          "pivot: six steps over five items wrap to item 1, a plain action");

    /* PM5: one switch, row then column over Hollow's grid, timed */
    pr.switches = 1;
    pr.order = PIVOT_ORDER_ROWCOL;
    pr.step_ms = 1000;
    CHECK(pivot_set_profile(&Q, &pr, true) == ZXM_OK, "pivot: one switch, rows then columns");
    int64_t t0 = 1700000100000;
    CHECK(pivot_scan_start_at(&Q, 10, 0, t0) == ZXM_OK &&
              pivot_scan_lit(&Q, t0 + 2500, &lit, &count) == ZXM_OK && lit == 2 && count == 3 &&
              pivot_scan_lit(&Q, t0 + 3000, &lit, &count) == ZXM_OK && lit == 0 &&
              pivot_scan_lit(&Q, t0 - 1, &lit, &count) == ZXM_E_ARG,
          "pivot: ten items make three rows of four (hollow), lit in turn");
    CHECK(pivot_switch_at(&Q, PIVOT_SWITCH_2, t0 + 2500, &out, &item) == ZXM_E_ARG &&
              pivot_switch_at(&Q, PIVOT_SWITCH_1, t0 + 2500, &out, &item) == ZXM_OK &&
              out == PIVOT_WAIT && Q.phase == 1 && Q.row == 2,
          "pivot: one switch: switch 1 chooses the last row");
    CHECK(pivot_scan_lit(&Q, t0 + 2500, &lit, &count) == ZXM_OK && lit == 0 && count == 2 &&
              pivot_scan_lit(&Q, t0 + 3500, &lit, &count) == ZXM_OK && lit == 1,
          "pivot: the last row holds two items");
    CHECK(pivot_switch_at(&Q, PIVOT_SWITCH_1, t0 + 3500, &out, &item) == ZXM_OK &&
              out == PIVOT_FIRE && item == 9 && Q.phase == 0 && Q.start_ms == t0 + 3500,
          "pivot: row 2, column 1 is item 9; the scan starts over");
    CHECK(pivot_scan_lit(&Q, t0 + 3500 + PIVOT_ELAPSED_MAX + 1, &lit, &count) == ZXM_E_STATE,
          "pivot: a scan left running past its limit is refused");

    /* PM6: a clock that is no clock is refused, nothing changed */
    piv_bad = true;
    CHECK(pivot_clock(&Q, &now) == ZXM_E_PROTOCOL && pivot_scan_start(&Q, 4, 0) == ZXM_E_PROTOCOL &&
              Q.items == 10,
          "pivot: a negative time_ms is no clock");
    piv_bad = false;
}
