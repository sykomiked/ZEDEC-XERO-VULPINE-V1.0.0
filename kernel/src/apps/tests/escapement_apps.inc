/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Escapement's model against zxvd's real dispatcher: this clock is
 * sys.info's time_ms, read for real; the peers come from net.peers through
 * view ops that list one (and none set: networking off); timesheets and
 * their register are written with files.write into the test's scratch Hoard
 * and read back. This in-process zxvd links no device mesh, so the roster
 * (devmesh.devices) is answered on a link of its own (ES_LINK) in its
 * documented shape, and every other request goes to the real dispatcher.
 * The vote is checked at a fixed instant set after the real read:
 * 2026-10-22 12:00:00.000 UTC, a Thursday. */
#include "escapement_model.h"

#define ES_T0    INT64_C(1792670400000) /* 2026-10-22 12:00:00.000 UTC, ms */
#define ES_DESK  "00112233445566778899aabbccddeeff"
#define ES_PHONE "fedcba9876543210fedcba9876543210"
#define ES_OLD   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define ES_NODE  "ab00000000000000000000000000000000000000000000000000000000000007"
#define ES_DEV(id, status, self)                                                                   \
    "{\"id\":\"" id "\",\"name\":\"d\",\"role\":\"thin\",\"admin\":false,\"held\":false,"          \
    "\"status\":\"" status "\",\"self\":" self ",\"up\":true,\"since\":\"2\"}"

static zxm_link_t ES_LINK;
static const char *ES_ROSTER;

static int32_t es_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    if (!strstr(req, "\"devmesh.devices\"")) return inproc(NULL, req, len, resp, cap);
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, ES_ROSTER);
}

static void es_peers(zxvd_buf_t *o)
{
    static const char P[] =
        "{\"peers\":[{\"id\":\"" ES_NODE "\",\"addr\":\"192.168.1.9:8723\",\"bucket\":2,"
        "\"ubh\":true,\"last_seen_ms\":\"1792670399000\",\"in_book\":false,\"member\":true,"
        "\"held\":\"0\",\"receivable\":\"0\",\"social\":\"0\",\"pending\":0}],\"count\":1}";
    zxvd_buf_raw(o, P, strlen(P));
}

/* the text of a Hoard file, through the real dispatcher */
static bool es_read(const char *path, char *out, uint32_t cap)
{
    zxm_begin(&ES_LINK, "files.read");
    zxm_p_str(&ES_LINK, "path", path);
    return zxm_call(&ES_LINK) == ZXM_OK &&
           zxm_get_str(&ES_LINK, ES_LINK.result, "text", out, cap) == ZXM_OK;
}

APPS_TEST(escapement)
{
    static escapement_t E, E2;
    static const zxvd_view_ops_t VIEW = {es_peers, NULL, NULL};
    char text[256];
    int64_t wd = 0, minute = 0, total = -1, number = 0, next = -1;
    uint32_t idx = 99, job = 99;

    zxm_link_init(&ES_LINK, es_send, &ES_LINK);
    escapement_init(&E, &ES_LINK);
    CHECK(E.tol_ms == ESCAPEMENT_TOL_START && E.clock == LPRES_STATE_NEITHER && !E.have,
          "escapement: a tolerance of 500 ms and no time to begin with");
    CHECK(escapement_reading(&E, ES_PHONE, ES_T0) == ZXM_E_STATE &&
              escapement_vote(&E) == LPRES_STATE_NEITHER && escapement_shared_ms(&E) == -1 &&
              escapement_week_minute(&E, 0, &wd, &minute) == ZXM_E_STATE &&
              escapement_accept_vote(&E) == ZXM_E_STATE,
          "escapement: nothing timed before a read");

    /* EM1: zxvd's own clock, after this test was written */
    CHECK(escapement_refresh(&E) == ZXM_OK && E.have && E.now_ms >= ES_T0 - INT64_C(31536000000),
          "escapement: sys.info's time_ms read: %s", ES_LINK.err);
    ES_ROSTER =
        "{\"roster_version\":\"3\",\"devices\":[" ES_DEV(ES_DESK, "active", "true") "," ES_DEV(
            ES_PHONE, "active", "false") "," ES_DEV(ES_OLD, "revoked", "false") "]}";
    CHECK(escapement_read_roster(&E) == ZXM_OK && E.ndevices == 1 && !strcmp(E.device[0], ES_PHONE),
          "escapement: the roster, not this device and not a revoked one: %s", ES_LINK.err);
    zxvd_rpc_set_view_ops(NULL);
    CHECK(escapement_read_peers(&E) == ZXM_E_REMOTE && ES_LINK.err_code == -32601 && E.npeers == 0,
          "escapement: no networking: zxvd says so: %s", ES_LINK.err);
    zxvd_rpc_set_view_ops(&VIEW);
    CHECK(escapement_read_peers(&E) == ZXM_OK && E.npeers == 1 && !strcmp(E.peer[0], ES_NODE),
          "escapement: one verified peer: %s", ES_LINK.err);
    zxvd_rpc_set_view_ops(NULL);
    ES_ROSTER = "{\"roster_version\":\"3\",\"devices\":[{\"id\":\"xyz\"}]}";
    CHECK(escapement_read_roster(&E) == ZXM_E_PROTOCOL && E.ndevices == 1,
          "escapement: a roster not in its shape is refused, the last one kept");

    /* EM2: readings of one instant, A 12:00:00.000 (this one), B 12:00:02.500 */
    E.now_ms = ES_T0;
    CHECK(escapement_reading(&E, ES_PHONE, ES_T0 + 2500) == ZXM_OK && E.nread == 1,
          "escapement: the phone read 12:00:02.500");
    CHECK(escapement_reading(&E, ES_PHONE, ES_T0) == ZXM_E_STATE &&
              escapement_reading(&E, ES_OLD, ES_T0) == ZXM_E_STATE &&
              escapement_reading(&E, ES_DESK, ES_T0) == ZXM_E_STATE &&
              escapement_reading(&E, "0123", ES_T0) == ZXM_E_ARG &&
              escapement_reading(&E, ES_NODE, ES_T0 + INT64_C(86400001)) == ZXM_E_ARG &&
              escapement_reading(&E, ES_NODE, -1) == ZXM_E_ARG &&
              escapement_reading(&E, NULL, ES_T0) == ZXM_E_ARG && E.nread == 1,
          "escapement: once a device, never a revoked one or this one, within a day");
    CHECK(escapement_set_tolerance(&E, -1) == ZXM_E_ARG &&
              escapement_set_tolerance(&E, ESCAPEMENT_TOL_MAX + 1) == ZXM_E_ARG &&
              escapement_set_tolerance(&E, 500) == ZXM_OK,
          "escapement: a tolerance of 0..60000 ms");

    /* EM3, EM4, the BOTH test: two clocks 2.5 s apart, 500 ms allowed */
    CHECK(escapement_vote(&E) == LPRES_STATE_BOTH && !E.voted && strstr(E.notice, "too few") &&
              escapement_accept_vote(&E) == ZXM_E_STATE,
          "escapement: two clocks 2.5 s apart: BOTH, too few to vote (%s)", E.notice);
    CHECK(escapement_reading(&E, ES_NODE, ES_T0 + 100) == ZXM_OK &&
              escapement_vote(&E) == LPRES_STATE_BOTH && E.voted && E.vote_ms == 100 &&
              E.reading[0].read_ms == ES_T0 + 2500 && E.reading[1].read_ms == ES_T0 + 100 &&
              E.now_ms == ES_T0 && escapement_shared_ms(&E) == ES_T0 && E.offset_ms == 0,
          "escapement: three devices vote the median, 100 ms; no clock overwritten (%s)", E.notice);

    /* EM7 on a BOTH clock: an approved job inside its window is held */
    CHECK(escapement_job_add(&E, 7, 480, 60, 500, ES_T0 - 3600000, ES_T0 + 3600000, ES_T0 + 1800000,
                             &job) == ZXM_OK &&
              job == 0 && !E.job[0].approved,
          "escapement: a job added, waiting for approval");
    CHECK(escapement_job_add(&E, 7, 480, 60, 500, 0, 1, 1, &idx) == ZXM_E_STATE &&
              escapement_job_add(&E, 8, 1440, 60, 500, 0, 1, 1, &idx) == ZXM_E_ARG &&
              escapement_job_add(&E, 8, 480, 0, 500, 0, 1, 1, &idx) == ZXM_E_ARG &&
              escapement_job_add(&E, 8, 480, 60, -1, 0, 1, 1, &idx) == ZXM_E_ARG &&
              escapement_job_add(&E, 8, 480, 60, 5, 10, 10, 10, &idx) == ZXM_E_ARG &&
              escapement_job_add(&E, 1000, 480, 60, 5, 0, 1, 1, &idx) == ZXM_E_ARG && E.njob == 1,
          "escapement: a job twice, past the day, no period, a negative cost, no window: refused");
    CHECK(escapement_job_state(&E, 0, 0, 1000, &next) ==
                  lpres_conjoin(LPRES_STATE_BOTH, LPRES_STATE_FALSE) &&
              escapement_job_approve(&E, 0) == ZXM_OK && escapement_job_approve(&E, 5) == ZXM_E_ARG,
          "escapement: not approved yet");
    lpres_state_t held = escapement_job_state(&E, 0, 0, 1000, &next);
    CHECK(held == lpres_conjoin(LPRES_STATE_BOTH, LPRES_STATE_TRUE) && held != LPRES_STATE_TRUE &&
              strstr(E.notice, "held"),
          "escapement: on a BOTH clock the job does not run (%d, %s)", (int) held, E.notice);

    /* EM4: the owner accepts the vote; the shared time moves, no clock does */
    CHECK(escapement_accept_vote(&E) == ZXM_OK && E.offset_ms == 100 &&
              E.clock == LPRES_STATE_TRUE && escapement_shared_ms(&E) == ES_T0 + 100 &&
              E.now_ms == ES_T0,
          "escapement: the vote accepted: shared time 12:00:00.100");
    CHECK(escapement_week_minute(&E, 0, &wd, &minute) == ZXM_OK && wd == 4 &&
              minute == 3 * 1440 + 720 && escapement_week_minute(&E, 330, &wd, &minute) == ZXM_OK &&
              wd == 4 && minute == 3 * 1440 + 1050 &&
              escapement_week_minute(&E, 840, &wd, &minute) == ZXM_OK && wd == 5 &&
              minute == 4 * 1440 + 120 &&
              escapement_week_minute(&E, -720, &wd, &minute) == ZXM_OK && wd == 4 &&
              minute == 3 * 1440 && escapement_week_minute(&E, 841, &wd, &minute) == ZXM_E_ARG,
          "escapement: Thursday 12:00 UTC, 17:30 at +05:30, Friday 02:00 at +14:00");
    CHECK(escapement_job_state(&E, 0, 0, 1000, &next) == LPRES_STATE_TRUE && next == 720 &&
              strstr(E.notice, "may run"),
          "escapement: approved, in its window, within budget: runs; next at 12:00 (%lld)",
          (long long) next);
    CHECK(escapement_job_state(&E, 0, 0, -1, &next) == LPRES_STATE_FALSE &&
              escapement_job_state(&E, 0, 60, 1000, &next) == LPRES_STATE_TRUE && next == 780 &&
              escapement_job_state(&E, 3, 0, 1000, &next) == LPRES_STATE_NEITHER,
          "escapement: over budget it does not; at +01:00 next at 13:00");
    E.offset_ms = 1800001; /* past the deadline by the shared time */
    CHECK(escapement_job_state(&E, 0, 0, 1000, &next) == LPRES_STATE_FALSE,
          "escapement: the deadline passed: it does not run");
    E.offset_ms = 100;

    /* EM5: shifts, week 43; nobody booked twice at once */
    CHECK(escapement_shift_add(&E, 1, 43, 480, 1020, 30, &idx) == ZXM_OK && idx == 0 &&
              E.shift[0].shift == 1,
          "escapement: Ada's Monday 08:00-17:00 with a 30 minute break");
    CHECK(escapement_shift_add(&E, 1, 43, 600, 660, 0, &idx) == ZXM_E_STATE &&
              strstr(E.notice, "twice") && E.nshift == 1,
          "escapement: Ada again at 10:00: booked twice, refused");
    CHECK(escapement_shift_add(&E, 2, 43, 600, 660, 0, &idx) == ZXM_OK &&
              escapement_shift_add(&E, 1, 43, 1020, 1080, 0, &idx) == ZXM_OK && idx == 2 &&
              E.shift[2].shift == 2 && escapement_shift_add(&E, 1, 44, 600, 660, 0, &idx) == ZXM_OK,
          "escapement: another person then, Ada back to back, and Ada next week");
    CHECK(escapement_week_total(&E, 1, 43, &total) == ZXM_OK && total == 570 &&
              escapement_week_total(&E, 2, 43, &total) == ZXM_OK && total == 60 &&
              escapement_week_total(&E, 9, 43, &total) == ZXM_OK && total == 0,
          "escapement: Ada's week 43 is 570 minutes");
    CHECK(escapement_shift_add(&E, 5, 43, 0, 10080, 0, &idx) == ZXM_OK &&
              escapement_shift_add(&E, 5, 43, 0, 60, 0, &idx) == ZXM_E_ARG &&
              escapement_shift_add(&E, 6, 43, 600, 500, 0, &idx) == ZXM_E_ARG &&
              escapement_shift_add(&E, 6, 43, 0, 10081, 0, &idx) == ZXM_E_ARG &&
              escapement_shift_add(&E, 6, 43, 0, 60, 1000, &idx) == ZXM_E_ARG &&
              escapement_shift_add(&E, 6, 10000, 0, 60, 0, &idx) == ZXM_E_ARG &&
              escapement_shift_add(&E, 1000000, 43, 0, 60, 0, &idx) == ZXM_E_ARG &&
              escapement_shift_add(&E, 0, 43, 0, 60, 0, &idx) == ZXM_E_ARG && E.nshift == 5,
          "escapement: a week past 10080 minutes, a shift backwards or past the week: refused");

    /* EM6: timesheets numbered from the register, written as new files */
    CHECK(escapement_issue_timesheet(&E, 1, 43, &number) == ZXM_E_STATE,
          "escapement: no timesheet before the register is read");
    CHECK(escapement_load_register(&E) == ZXM_OK && E.register_loaded && E.last_sheet == 0,
          "escapement: no register yet: numbering starts at 1: %s", ES_LINK.err);
    CHECK(escapement_issue_timesheet(&E, 1, 43, &number) == ZXM_OK && number == 1 &&
              es_read("escapement-timesheet-1.txt", text, sizeof text) &&
              !strcmp(text, "Escapement timesheet 1\nperson 1\nweek 43\nshifts 2\nminutes 570\n"),
          "escapement: Ada's timesheet 1: %s", ES_LINK.err);
    CHECK(escapement_issue_timesheet(&E, 2, 43, &number) == ZXM_OK && number == 2 &&
              escapement_issue_timesheet(&E, 9, 43, &number) == ZXM_E_STATE && number == 2,
          "escapement: timesheet 2; none for a week without shifts");
    escapement_init(&E2, &ES_LINK);
    CHECK(escapement_load_register(&E2) == ZXM_OK && E2.last_sheet == 2 &&
              es_read(ESCAPEMENT_REGISTER, text, sizeof text) &&
              !strcmp(text, "escapement timesheets\nlast 2\n"),
          "escapement: the register read back: numbering goes on from 2");
    E2.shift[0] = E.shift[0];
    E2.nshift = 1;
    E2.last_sheet = 0; /* a register restarted would write over timesheet 1: never */
    CHECK(escapement_issue_timesheet(&E2, 1, 43, &number) == ZXM_E_REMOTE && number == 2 &&
              es_read("escapement-timesheet-1.txt", text, sizeof text) &&
              strstr(text, "minutes 570"),
          "escapement: an earlier timesheet is never written over: %s", ES_LINK.err);
    E2.last_sheet = TSTK_NUMBER_MAX;
    CHECK(escapement_issue_timesheet(&E2, 1, 43, &number) == ZXM_E_STATE,
          "escapement: the register at its last number: refused");

    /* EM1 again: a new read clears the readings, never the accepted offset */
    CHECK(escapement_refresh(&E) == ZXM_OK && E.nread == 0 && E.clock == LPRES_STATE_NEITHER &&
              E.offset_ms == 100 && escapement_vote(&E) == LPRES_STATE_TRUE && !E.voted,
          "escapement: one clock alone agrees with itself, no vote");
}
