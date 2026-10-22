/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Treadle's model against zxvd's real dispatcher: swarm.state gives the
 * swarm's tokens per cycle and swarm.ask runs each task (the stub host's
 * set answer), every answer held for the owner. */
#include "treadle_model.h"

APPS_TEST(treadle)
{
    static treadle_t T;
    const char *was_state = STATE, *was_answer = ANSWER;
    STATE = "{}";
    ANSWER = "Three notes are unread.";
    treadle_init(&T, &LINK);
    uint32_t i0 = 99, i1 = 99;

    /* TR1, TR2: the C rules the model runs */
    CHECK(treadle_next_run(480, 240, 0) == 480 && treadle_next_run(480, 240, 481) == 720 &&
              treadle_next_run(480, 240, 1201) == 1920 && treadle_next_run(0, 1, 1440) == -1 &&
              treadle_budget_ok(0, 0) && !treadle_budget_ok(1, -1) &&
              !treadle_budget_ok(TREADLE_TOKENS_MAX + 1, 0),
          "treadle: next run and budget (TR1, TR2)");

    /* TM1: a task is added held, and only when it fits */
    CHECK(treadle_add_timed(&T, "Morning digest", "Summarize my unread notes", 480, 240, 50, &i0) ==
                  ZXM_OK &&
              i0 == 0 && T.ntask == 1 && !T.task[0].approved && T.task[0].last_day == -1,
          "treadle: a digest every four hours from 08:00, held");
    CHECK(treadle_add_timed(&T, "", "x", 0, 1, 1, NULL) == ZXM_E_ARG &&
              treadle_add_timed(&T, "a", "", 0, 1, 1, NULL) == ZXM_E_ARG &&
              treadle_add_timed(&T, "a", "x", 1440, 1, 1, NULL) == ZXM_E_ARG &&
              treadle_add_timed(&T, "a", "x", 0, 0, 1, NULL) == ZXM_E_ARG &&
              treadle_add_timed(&T, "a", "x", 0, 1441, 1, NULL) == ZXM_E_ARG &&
              treadle_add_timed(&T, "a", "x", 0, 1, 0, NULL) == ZXM_E_ARG &&
              treadle_add_timed(&T, "a", "x", 0, 1, TREADLE_TOKENS_MAX + 1, NULL) == ZXM_E_ARG &&
              treadle_add_event(&T, "a", "x", "", 1, NULL) == ZXM_E_ARG &&
              treadle_add_event(&T, "a", "x", "a-kind-of-sixteen", 1, NULL) == ZXM_E_ARG &&
              T.ntask == 1,
          "treadle: a task that does not fit changes nothing");
    CHECK(treadle_add_event(&T, "Missed call", "Draft a short callback note", "call-missed", 40,
                            &i1) == ZXM_OK &&
              i1 == 1 && T.task[1].kind == TREADLE_ON_EVENT && treadle_next(&T, 1, 600) == -1 &&
              treadle_next(&T, 0, 600) == 720 && treadle_next(&T, 2, 600) == -1,
          "treadle: a task on an event");

    /* TM2: nothing runs before the swarm's limit is read */
    int was = asked;
    CHECK(treadle_tick(&T, 100, 480) == ZXM_E_STATE && asked == was, "treadle: no swarm limit yet");
    CHECK(treadle_refresh(&T) == ZXM_E_PROTOCOL && !T.swarm_seen,
          "treadle: a state without tokens per cycle is refused");
    STATE = "{\"tokens_per_cycle\":1000,\"cycle\":7}";
    CHECK(treadle_refresh(&T) == ZXM_OK && T.swarm_seen && T.per_cycle == 1000,
          "treadle: swarm.state read: %s", LINK.err);

    /* TM1, TM2: held, or a cap of 0: nothing runs */
    CHECK(treadle_tick(&T, 100, 480) == ZXM_OK && T.ran == 0 && T.over == 0 && asked == was,
          "treadle: a held task never runs");
    CHECK(treadle_approve(&T, 0) == ZXM_OK && treadle_approve(&T, 2) == ZXM_E_ARG &&
              treadle_tick(&T, 100, 480) == ZXM_OK && T.ran == 0 && T.over == 1 && asked == was,
          "treadle: approved, but the owner's cap is 0");
    CHECK(treadle_set_cap(&T, TREADLE_TOKENS_MAX + 1) == ZXM_E_ARG && T.cap == 0 &&
              treadle_set_cap(&T, 1000) == ZXM_OK,
          "treadle: the owner's cap");

    /* TM3, TM4, TM5: a run, its answer held, its tokens spent */
    CHECK(treadle_tick(&T, 100, 480) == ZXM_OK && T.ran == 1 && asked == was + 1 &&
              strstr(last_prompt, "Summarize my unread notes") && treadle_pending(&T) == 1 &&
              T.held[0].task == 0 && !strcmp(T.held[0].answer.text, "Three notes are unread.") &&
              T.held[0].answer.cost.tokens > 0 && T.spent == T.held[0].answer.cost.tokens &&
              T.task[0].runs == 1 && T.task[0].last_day == 100 && T.task[0].last_min == 480,
          "treadle: the digest ran at 08:00, its answer held: %s", LINK.err);
    uint64_t one = T.held[0].answer.cost.tokens;
    CHECK(treadle_tick(&T, 100, 480) == ZXM_OK && T.ran == 0 && asked == was + 1 &&
              treadle_tick(&T, 100, 481) == ZXM_OK && T.ran == 0 &&
              treadle_tick(&T, 100, 720) == ZXM_OK && T.ran == 1 && T.task[0].runs == 2 &&
              T.spent == 2 * one,
          "treadle: once a minute, then at 12:00");
    CHECK(treadle_event(&T, 100, 721, "call-missed") == ZXM_OK && T.ran == 0 &&
              treadle_approve(&T, 1) == ZXM_OK &&
              treadle_event(&T, 100, 721, "message") == ZXM_OK && T.ran == 0 &&
              treadle_event(&T, 100, 721, "call-missed") == ZXM_OK && T.ran == 1 &&
              strstr(last_prompt, "callback") && T.held[2].task == 1 && treadle_pending(&T) == 3 &&
              treadle_event(&T, 100, 721, "") == ZXM_E_ARG,
          "treadle: a missed call runs the approved event task");

    /* TM4: a suggestion is held and adds nothing; with every slot waiting, a run waits */
    CHECK(treadle_suggest(&T, "") == ZXM_E_ARG &&
              treadle_suggest(&T, "Remind me to stretch every hour") == ZXM_OK &&
              T.held[3].task == -1 && treadle_pending(&T) == 4 && T.ntask == 2 &&
              strstr(last_prompt, "stretch"),
          "treadle: the swarm's suggestion is held");
    int before = asked;
    CHECK(treadle_tick(&T, 100, 960) == ZXM_OK && T.ran == 0 && T.waiting == 1 && asked == before &&
              treadle_suggest(&T, "more") == ZXM_E_STATE,
          "treadle: every slot waits for the owner: backpressure");
    CHECK(treadle_accept(&T, 3) == ZXM_OK && T.ntask == 2 && treadle_accept(&T, 3) == ZXM_E_STATE &&
              treadle_accept(&T, TREADLE_HELD) == ZXM_E_ARG &&
              treadle_reject(&T, TREADLE_HELD) == ZXM_E_ARG,
          "treadle: accepting a suggestion adds no task (M3)");
    CHECK(treadle_tick(&T, 100, 960) == ZXM_OK && T.ran == 1 && T.held[3].task == 0 &&
              T.task[0].runs == 3,
          "treadle: the slot freed, the 16:00 run goes");

    /* TM2: over the cap, over a cycle */
    CHECK(treadle_reject(&T, 0) == ZXM_OK && treadle_reject(&T, 1) == ZXM_OK &&
              !T.held[0].answer.text[0] && treadle_pending(&T) == 2,
          "treadle: two answers rejected");
    CHECK(treadle_set_cap(&T, (uint32_t) T.spent + 49) == ZXM_OK &&
              treadle_tick(&T, 100, 1200) == ZXM_OK && T.ran == 0 && T.over == 1 &&
              treadle_set_cap(&T, (uint32_t) T.spent + 50) == ZXM_OK &&
              treadle_tick(&T, 100, 1200) == ZXM_OK && T.ran == 1,
          "treadle: a run one token over the cap waits; exactly the cap runs");
    CHECK(treadle_tick(&T, 99, 480) == ZXM_E_ARG && treadle_tick(&T, 100, 1440) == ZXM_E_ARG &&
              treadle_tick(&T, -1, 0) == ZXM_E_ARG && T.day == 100,
          "treadle: a day going back, a minute past the day");
    CHECK(treadle_reject(&T, 0) == ZXM_OK && treadle_tick(&T, 101, 480) == ZXM_OK && T.day == 101 &&
              T.ran == 1 && T.spent == one,
          "treadle: a new day starts the spend from 0");
    STATE = "{\"tokens_per_cycle\":49}";
    CHECK(treadle_refresh(&T) == ZXM_OK && T.per_cycle == 49 && treadle_reject(&T, 0) == ZXM_OK &&
              treadle_tick(&T, 101, 720) == ZXM_OK && T.ran == 0 && T.over == 1,
          "treadle: a run past the swarm's tokens per cycle waits");
    STATE = "{\"tokens_per_cycle\":1000}";

    /* TM1: held again, removed */
    CHECK(treadle_refresh(&T) == ZXM_OK && treadle_hold(&T, 0) == ZXM_OK &&
              treadle_tick(&T, 101, 960) == ZXM_OK && T.ran == 0 && T.over == 0 &&
              treadle_hold(&T, 5) == ZXM_E_ARG,
          "treadle: a task held again does not run");
    CHECK(treadle_remove(&T, 0) == ZXM_OK && T.ntask == 1 && T.held[2].task == 0 &&
              T.held[3].task == -2 && !strcmp(T.task[0].name, "Missed call") &&
              treadle_remove(&T, 1) == ZXM_E_ARG,
          "treadle: removed; held answers keep their text and follow their task");
    {
        int rc = ZXM_OK;
        for (uint32_t i = 1; i < TREADLE_TASKS && rc == ZXM_OK; i++)
            rc = treadle_add_timed(&T, "t", "x", 0, 60, 1, NULL);
        CHECK(rc == ZXM_OK && T.ntask == TREADLE_TASKS &&
                  treadle_add_timed(&T, "t", "x", 0, 60, 1, NULL) == ZXM_E_STATE,
              "treadle: the tasks are bounded");
    }

    /* without swarm.state, zxvd answers an error and the limit read before stays */
    static zxvd_host_ops_t ops;
    ops = OPS;
    ops.state = NULL;
    zxvd_rpc_set_ops(&ops);
    CHECK(treadle_refresh(&T) == ZXM_E_REMOTE && T.per_cycle == 1000,
          "treadle: swarm.state not available: %s", LINK.err);
    zxvd_rpc_set_ops(&OPS);
    STATE = was_state;
    ANSWER = was_answer;
}
