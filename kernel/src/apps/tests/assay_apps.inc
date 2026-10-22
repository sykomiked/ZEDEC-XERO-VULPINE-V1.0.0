/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Assay's model against zxvd's real dispatcher (LINK): swarm.state and
 * swarm.ask as zxvd answers them. play.run is not linked in this
 * in-process zxvd, and the budget gate never refuses its stub, so both are
 * answered on a link of its own (AYA_LINK), which passes every other
 * request to the real dispatcher. */
#include "assay_model.h"

static assay_t AYA;
static assay_score_t AYS[ASSAY_SUBJECTS];
static zxm_link_t AYA_LINK;
static const char *AYA_PLAY; /* play.run's result; NULL: zxvd refuses the template */
static bool AYA_GATE;        /* swarm.ask refused by the budget gate */

static int32_t aya_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    if (strstr(req, "\"play.run\"")) {
        if (AYA_PLAY)
            return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq,
                            AYA_PLAY);
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32602,"
                        "\"message\":\"no such template\"}}",
                        l->seq);
    }
    if (AYA_GATE && strstr(req, "\"swarm.ask\""))
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32000,"
                        "\"message\":\"over the tokens-per-cycle budget\"}}",
                        l->seq);
    return inproc(NULL, req, len, resp, cap);
}

/* the four counts of subject s's latest run */
static bool aya_cur(uint32_t s, int64_t t, int64_t f, int64_t b, int64_t n)
{
    const int64_t *c = AYA.subject[s].cur.c;
    return c[0] == t && c[1] == f && c[2] == b && c[3] == n;
}

APPS_TEST(assay)
{
    /* AY1-AY4 through the model's own C */
    {
        static const int64_t s[4] = {6666, 3000, 0, 0};
        CHECK(assay_pass_share_x100(2, 3) == 6666 && assay_pass_share_x100(0, 7) == 0 &&
                  assay_pass_share_x100(4, 3) == ASSAY_REFUSED && assay_meets_bar(0, 0, 0) &&
                  !assay_meets_bar(-1, 0, 0) && assay_score_rank(s, 4, 0) == 1 &&
                  assay_score_rank(s, 4, 3) == 3 && assay_fraction_ok(1, 2) &&
                  !assay_fraction_ok(3, 2) && !assay_fraction_ok(0, 2),
              "assay: the rules (AY1-AY4)");
    }
    /* AM4: grading an answer, four-valued */
    CHECK(assay_grade("It is 42.", "42", "") == LPRES_STATE_TRUE &&
              assay_grade("It is 41.", "42", "") == LPRES_STATE_FALSE &&
              assay_grade("It is 42.", "", "sorry") == LPRES_STATE_TRUE &&
              assay_grade("sorry", "", "sorry") == LPRES_STATE_FALSE &&
              assay_grade("42, sorry", "42", "sorry") == LPRES_STATE_BOTH &&
              assay_grade("41", "42", "sorry") == LPRES_STATE_NEITHER &&
              assay_grade("sorry", "42", "sorry") == LPRES_STATE_FALSE &&
              assay_grade("", "42", "") == LPRES_STATE_NEITHER &&
              assay_grade(NULL, "42", "") == LPRES_STATE_NEITHER,
          "assay: an answer graded TRUE, FALSE, BOTH or NEITHER");

    /* AM3: the bar and the thresholds */
    assay_init(&AYA, &AYA_LINK);
    zxm_link_init(&AYA_LINK, aya_send, &AYA_LINK);
    CHECK(AYA.bar_x100 == ASSAY_DEFAULT_BAR && AYA.max_refusals == 0 && AYA.min_runs == 1 &&
              AYA.decide_num == 1 && AYA.decide_den == 2 && AYA.conflict_num == 1 &&
              AYA.conflict_den == 4 && AYA.nsubjects == 1 && AYA.subject[0].id == -1,
          "assay: the defaults, and the companion as subject 0");
    CHECK(assay_set_bar(&AYA, 10001, 0, 1) == ZXM_E_ARG &&
              assay_set_bar(&AYA, -1, 0, 1) == ZXM_E_ARG &&
              assay_set_bar(&AYA, 5000, -1, 1) == ZXM_E_ARG &&
              assay_set_bar(&AYA, 5000, 0, 0) == ZXM_E_ARG &&
              assay_set_bar(&AYA, 5000, ASSAY_COUNT_MAX + 1, 1) == ZXM_E_ARG &&
              AYA.bar_x100 == ASSAY_DEFAULT_BAR,
          "assay: a bar outside AM3 is refused, the old one stays");
    CHECK(assay_set_thresholds(&AYA, 0, 2, 1, 4) == ZXM_E_ARG &&
              assay_set_thresholds(&AYA, 3, 2, 1, 4) == ZXM_E_ARG &&
              assay_set_thresholds(&AYA, 1, 2, 1, 0) == ZXM_E_ARG && AYA.decide_den == 2,
          "assay: thresholds outside (0, 1] are refused");
    CHECK(assay_set_thresholds(&AYA, 1, 2, 1, 4) == ZXM_OK && strstr(AYA.notice, "1/2"),
          "assay: thresholds: %s", AYA.notice);

    /* AM2: the suite */
    CHECK(assay_run_suite(&AYA) == ZXM_E_STATE && AYA.subject[0].runs == 0,
          "assay: no suite, no run");
    CHECK(assay_add_ask(&AYA, "", "42", "") == ZXM_E_ARG &&
              assay_add_ask(&AYA, "What is six times seven?", "", "") == ZXM_E_ARG &&
              assay_add_ask(&AYA, "q",
                            "0123456789012345678901234567890123456789012345678901234567890123",
                            "") == ZXM_E_ARG &&
              assay_add_play(&AYA, "") == ZXM_E_ARG && assay_add_play(&AYA, NULL) == ZXM_E_ARG &&
              AYA.ntests == 0,
          "assay: tests that are none");
    CHECK(assay_add_ask(&AYA, "What is six times seven?", "42", "") == 0 &&
              assay_add_ask(&AYA, "And six times eight?", "48", "") == 1 &&
              assay_add_ask(&AYA, "Say it plainly.", "42", "answer") == 2 &&
              assay_add_ask(&AYA, "Be polite.", "", "sorry") == 3 &&
              assay_add_play(&AYA, "shares") == 4 && AYA.ntests == 5,
          "assay: four prompts and a template");

    /* AM4: a run, every test once, through the real swarm.ask */
    ANSWER = "The answer is 42.";
    AYA_PLAY = "{\"template\":\"shares\",\"verdict\":\"TRUE\",\"steps\":[]}";
    AYA_GATE = false;
    CHECK(assay_run_suite(&AYA) == ZXM_OK && aya_cur(0, 3, 1, 1, 0) && AYA.subject[0].runs == 1 &&
              AYA.subject[0].refusals == 0 && !strcmp(AYA.answer, "The answer is 42.") &&
              assay_log_count(&AYA) == 5,
          "assay: the first run: 3 passed, 1 failed, 1 contested: %s (%s)", AYA.notice,
          AYA_LINK.err);
    /* the budget gate refuses every prompt; the template now fails */
    AYA_GATE = true;
    AYA_PLAY = "{\"template\":\"shares\",\"verdict\":\"FALSE\",\"steps\":[]}";
    CHECK(assay_run_suite(&AYA) == ZXM_OK && aya_cur(0, 0, 1, 0, 4) &&
              AYA.subject[0].prev.c[0] == 3 && AYA.subject[0].refusals == 4 &&
              AYA.subject[0].all.c[0] == 3 && AYA.subject[0].all.c[3] == 4 &&
              assay_log_count(&AYA) == 10 && assay_log_at(&AYA, 5)->refused &&
              !assay_log_at(&AYA, 9)->refused && assay_log_at(&AYA, 9)->from == ASSAY_FROM_PLAY,
          "assay: refused by the budget gate, counted apart: %s", AYA.notice);
    /* a broken answer keeps nothing of the run */
    AYA_GATE = false;
    AYA_PLAY = "{\"template\":\"shares\",\"verdict\":\"MAYBE\"}";
    CHECK(assay_run_suite(&AYA) == ZXM_E_PROTOCOL && AYA.subject[0].runs == 2 &&
              aya_cur(0, 0, 1, 0, 4) && assay_log_count(&AYA) == 10,
          "assay: a verdict that is none keeps nothing");
    AYA_PLAY = NULL; /* zxvd refuses the template: a refusal, not a failure */
    {
        assay_subject_t keep = AYA.subject[0];
        uint64_t head = AYA.head;
        AYA.subject[0].all.c[0] = ASSAY_COUNT_MAX - 2; /* a total that would pass its range */
        CHECK(assay_run_suite(&AYA) == ZXM_E_STATE && AYA.subject[0].runs == 2 && AYA.head == head,
              "assay: a total past ASSAY_COUNT_MAX keeps nothing");
        AYA.subject[0] = keep;
    }

    /* AM4: rounds over the swarm's agents and its audit (the real swarm.state) */
    STATE = "{\"audit\":\"active\",\"agents\":[{\"id\":1,\"name\":\"Critic-1\",\"witness\":1},"
            "{\"id\":2,\"name\":\"Maker-2\",\"witness\":0},{\"id\":3,\"name\":\"Scout-3\","
            "\"witness\":2}]}";
    CHECK(assay_round(&AYA) == ZXM_OK && AYA.nsubjects == 4 && AYA.subject[1].id == 1 &&
              !strcmp(AYA.subject[2].name, "Maker-2") && aya_cur(1, 1, 0, 0, 0) &&
              aya_cur(2, 0, 1, 0, 0) && aya_cur(3, 0, 0, 1, 0) && AYA.unscored == 0 &&
              !strcmp(AYA.audit, "active") && assay_log_at(&AYA, 12)->from == ASSAY_FROM_AUDIT,
          "assay: a round: agree, does not add up, contested: %s (%s)", AYA.notice, LINK.err);
    STATE = "{\"audit\":\"off\",\"agents\":[{\"id\":3,\"name\":\"Scout-3\",\"witness\":2},"
            "{\"id\":1,\"name\":\"Critic-1\",\"witness\":1},{\"id\":2,\"name\":\"Maker-2\","
            "\"witness\":0}]}";
    CHECK(assay_round(&AYA) == ZXM_OK && AYA.nsubjects == 4 && aya_cur(1, 0, 0, 0, 1) &&
              aya_cur(2, 0, 0, 0, 1) && aya_cur(3, 0, 0, 0, 1) && strstr(AYA.notice, "no evidence"),
          "assay: the audit off is no evidence, whatever the order: %s", AYA.notice);
    STATE = "{\"audit\":\"active\",\"agents\":[{\"id\":1,\"name\":\"Critic-1\",\"witness\":1},"
            "{\"id\":3,\"name\":\"Scout-3\",\"witness\":2}]}";
    CHECK(assay_round(&AYA) == ZXM_OK && AYA.nsubjects == 4 && !AYA.subject[2].present &&
              AYA.subject[1].present && aya_cur(2, 0, 0, 0, 1) && aya_cur(1, 1, 0, 0, 0) &&
              AYA.subject[2].runs == 3,
          "assay: an agent that left keeps its record and is undecided");
    {
        static const char *BAD[] = {
            "{\"audit\":\"active\"}",
            "{\"agents\":[]}",
            "{\"audit\":\"active\",\"agents\":[{\"id\":1,\"name\":\"x\",\"witness\":3}]}",
            "{\"audit\":\"active\",\"agents\":[{\"id\":-1,\"name\":\"x\",\"witness\":1}]}",
            "{\"audit\":\"active\",\"agents\":[{\"id\":9,\"witness\":1}]}",
        };
        uint32_t bad = 0;
        for (uint32_t i = 0; i < 5u; i++) {
            STATE = BAD[i];
            bad += assay_round(&AYA) == ZXM_E_PROTOCOL && AYA.nsubjects == 4 &&
                   AYA.subject[1].runs == 3;
        }
        CHECK(bad == 5, "assay: a state that is not zxvd's keeps nothing (%u of 5)", bad);
    }

    /* AM6: the scores, with the real pce_prob */
    CHECK(assay_score(&AYA, AYS, 2) == ZXM_E_ARG, "assay: too little room for the scores");
    CHECK(assay_score(&AYA, AYS, ASSAY_SUBJECTS) == ZXM_OK && AYS[0].results == 10 &&
              AYS[0].refusals == 4 && AYS[0].share_x100 == 3000 && AYS[1].share_x100 == 6666 &&
              AYS[2].share_x100 == 0 && AYS[3].share_x100 == 0 && AYS[1].rank == 1 &&
              AYS[0].rank == 2 && AYS[2].rank == 3 && AYS[3].rank == 3 && !AYS[0].meets &&
              !AYS[1].meets && strstr(AYA.notice, "Critic-1 leads at 66.66%") &&
              strstr(AYA.notice, "Advice only"),
          "assay: shares, ranks and the default bar: %s", AYA.notice);
    /* the companion's latest run is undecided; joined with the run before, conflict */
    CHECK(AYS[0].verdict == LPRES_STATE_NEITHER && AYS[0].repeat == LPRES_STATE_BOTH &&
              AYS[1].verdict == LPRES_STATE_TRUE && AYS[1].repeat == LPRES_STATE_TRUE &&
              AYS[3].verdict == LPRES_STATE_BOTH && AYS[3].repeat == LPRES_STATE_BOTH &&
              AYS[2].verdict == LPRES_STATE_NEITHER && AYS[2].repeat == LPRES_STATE_NEITHER,
          "assay: verdicts %d %d %d %d, repeated %d %d %d %d", AYS[0].verdict, AYS[1].verdict,
          AYS[2].verdict, AYS[3].verdict, AYS[0].repeat, AYS[1].repeat, AYS[2].repeat,
          AYS[3].repeat);
    CHECK(assay_set_bar(&AYA, 6000, 0, 3) == ZXM_OK &&
              assay_score(&AYA, AYS, ASSAY_SUBJECTS) == ZXM_OK && AYS[1].meets && !AYS[0].meets &&
              !AYS[2].meets && strstr(AYA.notice, "1 of 4"),
          "assay: a bar of 60%%, no refusals, 3 results: %s", AYA.notice);
    CHECK(assay_set_bar(&AYA, 6000, 0, 4) == ZXM_OK &&
              assay_score(&AYA, AYS, ASSAY_SUBJECTS) == ZXM_OK && !AYS[1].meets,
          "assay: one result short of the bar");

    /* AM5: the log is a bounded ring; the oldest are dropped and counted */
    AYA_PLAY = "{\"template\":\"shares\",\"verdict\":\"TRUE\"}";
    for (uint32_t i = 0; i < 3u; i++) CHECK(assay_run_suite(&AYA) == ZXM_OK, "assay: run %u", i);
    CHECK(assay_log_count(&AYA) == ASSAY_LOG && AYA.head == 34 && AYA.dropped == 2 &&
              assay_log_at(&AYA, 0)->seq == 2 && assay_log_at(&AYA, ASSAY_LOG - 1u)->seq == 33 &&
              assay_log_at(&AYA, ASSAY_LOG) == NULL,
          "assay: %u entries kept, %llu dropped", assay_log_count(&AYA),
          (unsigned long long) AYA.dropped);

    /* AM1: a swarm larger than the table: the rest counted, never dropped silently */
    {
        static char big[4096];
        uint32_t k = (uint32_t) snprintf(big, sizeof big, "{\"audit\":\"active\",\"agents\":[");
        for (uint32_t i = 0; i < ASSAY_SUBJECTS + 2u; i++)
            k += (uint32_t) snprintf(big + k, sizeof big - k,
                                     "%s{\"id\":%u,\"name\":\"Agent-%u\",\"witness\":1}",
                                     i ? "," : "", 100u + i, i);
        snprintf(big + k, sizeof big - k, "]}");
        STATE = big;
        assay_init(&AYA, &AYA_LINK);
        CHECK(assay_round(&AYA) == ZXM_OK && AYA.nsubjects == ASSAY_SUBJECTS && AYA.unscored == 3 &&
                  AYA.subject[ASSAY_SUBJECTS - 1u].id == 100 + ASSAY_SUBJECTS - 2u &&
                  strstr(AYA.notice, "3 not scored"),
              "assay: %s", AYA.notice);
    }
    STATE = "{}";
    ANSWER = "ok";
}
