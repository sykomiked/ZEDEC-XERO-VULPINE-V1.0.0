/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Tributary's languages, witnessed, row by row against tributary_rules.h,
 * the C the model runs; the parts it composes (retort, assay,
 * twelvefold, swarm_shares) resolved and called against their own rules; and the BOTH
 * of three models answering 42, 42 and 41 under a quorum of 3.
 * Slots: @num.tributary_agreeing_answers, @logic.tributary_ask_within_caps,
 * @num.tributary_answer_conf_x100, @num.tributary_answer_models,
 * @num.tributary_query_ledger, @logic.tributary_answer_src_ok;
 * and, used, @num.retort_model_score_total, @num.retort_latency_fastest_ms,
 * @num.retort_latency_slowest_ms, @logic.retort_run_limits_ok,
 * @num.retort_energy_per_answer_uj, @swarm.cycle_share,
 * @num.assay_pass_share_x100, @logic.assay_meets_bar,
 * @logic.twelvefold_lp_entail_kept. */
#include "tributary_rules.h"

/* One call of an entry answering exact rationals: every row num[i]/den[i]
 * and the join `lp` (NEITHER for one language). */
static bool trib_rat_rows(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t nargs,
                          const int64_t *num, const int64_t *den, uint32_t rows, int32_t want_lp)
{
    zxapp_num_t o[WAVE_ROWS];
    uint32_t k = 0;
    int32_t lp = -1;
    int rc = zxapp_call_witnessed(a, &H, entry, args, nargs, o, WAVE_ROWS, &k, &lp);
    if (rc != ZXAPP_OK || lp != want_lp || k != rows) {
        printf("    %s: rc %d lp %d n %u (%s)\n", entry, rc, (int) lp, k, a->err);
        return false;
    }
    for (uint32_t i = 0; i < rows; i++)
        if (o[i].num != num[i] || o[i].den != den[i]) {
            printf("    %s row %u: %lld/%lld, want %lld/%lld\n", entry, i, (long long) o[i].num,
                   (long long) o[i].den, (long long) num[i], (long long) den[i]);
            return false;
        }
    return true;
}

/* An entry of one language refuses these rows: no answer comes back. */
static bool trib_refused(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t nargs)
{
    zxapp_num_t o[WAVE_ROWS];
    uint32_t k = 0;
    int32_t lp = -1;
    int rc = zxapp_call_witnessed(a, &H, entry, args, nargs, o, WAVE_ROWS, &k, &lp);
    return rc != ZXAPP_OK;
}

APP_TEST(tributary, 29, 225)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];
    static int64_t num[WAVE_ROWS], den[WAVE_ROWS];

    /* compose: four parts, every part entry it calls resolves to its part */
    static const char *const used[9][2] = {
        {"retort.model_score_total", "retort"},     {"retort.latency_fastest_ms", "retort"},
        {"retort.latency_slowest_ms", "retort"},    {"retort.run_limits_ok", "retort"},
        {"retort.energy_per_answer_uj", "retort"},  {"swarm_shares.cycle_share", "swarm_shares"},
        {"assay.pass_share_x100", "assay"},         {"assay.meets_bar", "assay"},
        {"twelvefold.lp_entail_kept", "twelvefold"}};
    CHECK(app->resolved && app->nuse == 4 && app->composed == 8 && app->n == 29,
          "Tributary: composed of 4 parts, 8 own lines and 21 of theirs (%u, %u)", app->composed,
          app->n);
    for (uint32_t i = 0; i < 9u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Tributary: %s resolves to %s and loads", used[i][0], used[i][1]);
    }

    /* TB1: agreeing_answers, KRON = Trebago = C (witness_kind_count) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = i % 3u ? wrand(1, 6) : wrand(1, TRIB_CODE_MAX);
    a[0] = TRIB_CODE_MAX, a[1] = 1, a[2] = 1; /* the edges of a code */
    CHECK(tributary_agree_counts(a, WAVE_ROWS, want) && want[0] >= 1 && want[1] >= 2,
          "Tributary: TB1 answers");
    zxapp_arg_t ga[1] = {{"code", a, WAVE_ROWS}};
    CHECK(wave_same(app, "agreeing_answers", ga, 1, want, WAVE_ROWS, false),
          "Tributary: agreeing_answers, KRON = Trebago = C");
    {
        static const int64_t bad[3] = {0, TRIB_CODE_MAX + 1, -5};
        for (uint32_t k = 0; k < 3u; k++) {
            int64_t cd[2] = {42, bad[k]}, o[2];
            zxapp_arg_t xa[1] = {{"code", cd, 2}};
            CHECK(!tributary_agree_counts(cd, 2, o) &&
                      wave_lp(app, "agreeing_answers", xa, 1) == ZXAPP_BOTH,
                  "Tributary: a code %lld is no code: Trebago refuses, KRON counts: BOTH",
                  (long long) bad[k]);
        }
    }

    /* TB3: ask_within_caps, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 4u ? wrand(0, TRIB_TOKEN_ROOM_MAX) : wrand(-3, 3);
        b[i] = i % 5u ? wrand(0, TRIB_ROOM_MAX) : wrand(-3, 3);
        c[i] = i % 6u ? wrand(0, TRIB_ROOM_MAX) : wrand(-3, 3);
        if (i == 0) a[i] = 0, b[i] = 0, c[i] = 0; /* to every cap */
        if (i == 1) a[i] = TRIB_TOKEN_ROOM_MAX, b[i] = TRIB_ROOM_MAX, c[i] = TRIB_ROOM_MAX;
        if (i == 2) a[i] = -1, b[i] = 5, c[i] = 5; /* one token over */
        if (i == 3) a[i] = 5, b[i] = 5, c[i] = -1; /* one minor unit over */
        want[i] = T_(tributary_ask_within_caps(a[i], b[i], c[i]));
    }
    zxapp_arg_t ca[3] = {
        {"tokenroom", a, WAVE_ROWS}, {"energyroom", b, WAVE_ROWS}, {"priceroom", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE &&
              wave_same(app, "ask_within_caps", ca, 3, want, WAVE_ROWS, true),
          "Tributary: ask_within_caps, Eve = KRON = C");
    {
        static const int64_t edge[3][3] = {
            {TRIB_TOKEN_ROOM_MAX + 1, 0, 0}, {0, TRIB_ROOM_MAX + 1, 0}, {0, 0, TRIB_ROOM_MAX + 1}};
        for (uint32_t k = 0; k < 3u; k++) {
            int64_t t1[2] = {0, edge[k][0]}, e1[2] = {0, edge[k][1]}, p1[2] = {0, edge[k][2]};
            zxapp_arg_t xa[3] = {{"tokenroom", t1, 2}, {"energyroom", e1, 2}, {"priceroom", p1, 2}};
            CHECK(!tributary_ask_within_caps(edge[k][0], edge[k][1], edge[k][2]) &&
                      wave_lp(app, "ask_within_caps", xa, 3) == ZXAPP_BOTH,
                  "Tributary: ask_within_caps margin %u past its range: Eve refuses, KRON reads "
                  "room: BOTH",
                  k);
        }
    }

    /* TB4: answer_conf_x100, Trebago = C, exact */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = i % 2u ? wrand(1, 8) : wrand(1, TRIB_ASKED_MAX);
        a[i] = wrand(0, b[i]);
        if (i == 0) a[i] = 2, b[i] = 3;                           /* 200/3 */
        if (i == 1) a[i] = 0, b[i] = 1;                           /* none */
        if (i == 2) a[i] = TRIB_ASKED_MAX, b[i] = TRIB_ASKED_MAX; /* all, at max */
        CHECK(tributary_confidence_x100(a[i], b[i], &num[i], &den[i]), "Tributary: TB4 row %u", i);
    }
    zxapp_arg_t fa[2] = {{"agree", a, WAVE_ROWS}, {"asked", b, WAVE_ROWS}};
    CHECK(num[0] == 200 && den[0] == 3 && num[1] == 0 && num[2] == 100 && den[2] == 1 &&
              trib_rat_rows(app, "answer_conf_x100", fa, 2, num, den, WAVE_ROWS, ZXAPP_NEITHER),
          "Tributary: answer_conf_x100, Trebago = C");
    {
        static const int64_t bad[4][2] = {{4, 3}, {-1, 3}, {1, 0}, {1, TRIB_ASKED_MAX + 1}};
        for (uint32_t k = 0; k < 4u; k++) {
            int64_t ag[2] = {1, bad[k][0]}, as[2] = {3, bad[k][1]}, n1 = 0, d1 = 0;
            zxapp_arg_t xa[2] = {{"agree", ag, 2}, {"asked", as, 2}};
            CHECK(!tributary_confidence_x100(bad[k][0], bad[k][1], &n1, &d1) &&
                      trib_refused(app, "answer_conf_x100", xa, 2),
                  "Tributary: answer_conf_x100 edge %u refused", k);
        }
    }

    /* TB5: answer_models, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(0, 4), b[i] = wrand(1, 5);
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = tributary_code_models(a, b, WAVE_ROWS, i);
    zxapp_arg_t ma[2] = {{"model", a, WAVE_ROWS}, {"code", b, WAVE_ROWS}};
    CHECK(wave_same(app, "answer_models", ma, 2, want, WAVE_ROWS, false),
          "Tributary: answer_models, KRON = C");
    {
        /* one model asked twice that said 42 both times counts once */
        static const int64_t mo[4] = {0, 0, 1, 2}, co[4] = {42, 42, 42, 41};
        int64_t w[4];
        for (uint32_t i = 0; i < 4u; i++) w[i] = tributary_code_models(mo, co, 4, i);
        zxapp_arg_t xa[2] = {{"model", mo, 4}, {"code", co, 4}};
        CHECK(w[0] == 2 && w[1] == 2 && w[2] == 2 && w[3] == 1 &&
                  wave_same(app, "answer_models", xa, 2, w, 4, false),
              "Tributary: a model asked twice is one model behind its code");
    }

    /* TB6: query_ledger, Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? wrand(0, 1000000) : wrand(0, INT64_C(999999999999));
        b[i] = wrand(0, TRIB_TIER_MAX);
        c[i] = i % 3u ? wrand(0, 5000) : wrand(0, INT64_C(999999999));
        if (i == 0) a[i] = 0, b[i] = 0, c[i] = 7;                   /* plain */
        if (i == 1) a[i] = 0, b[i] = TRIB_TIER_MAX, c[i] = 1;       /* one racing face */
        if (i == 2) a[i] = TRIB_LEDGER_MAX - 1, b[i] = 0, c[i] = 1; /* to the last token */
        if (i == 3) a[i] = 10, b[i] = 9, c[i] = 0;                  /* nothing entered */
        want[i] = tributary_ledger_after(a[i], b[i], c[i]);
    }
    zxapp_arg_t la[3] = {
        {"before", a, WAVE_ROWS}, {"tier", b, WAVE_ROWS}, {"pieces", c, WAVE_ROWS}};
    CHECK(want[0] == 7 && want[1] == 75025 && want[2] == TRIB_LEDGER_MAX && want[3] == 10 &&
              tributary_face(9) == 89 && tributary_face(14) == 987 &&
              wave_same(app, "query_ledger", la, 3, want, WAVE_ROWS, false),
          "Tributary: query_ledger, Sutra = C, faces 1 .. 75025");
    {
        static const int64_t bad[4][3] = {
            {0, 24, 1}, {TRIB_LEDGER_MAX, 0, 1}, {0, -1, 1}, {0, 23, INT64_C(99999999999999)}};
        for (uint32_t k = 0; k < 4u; k++) {
            int64_t be[2] = {0, bad[k][0]}, ti[2] = {0, bad[k][1]}, pi[2] = {1, bad[k][2]};
            zxapp_arg_t xa[3] = {{"before", be, 2}, {"tier", ti, 2}, {"pieces", pi, 2}};
            CHECK(tributary_ledger_after(bad[k][0], bad[k][1], bad[k][2]) == TRIB_REFUSED &&
                      trib_refused(app, "query_ledger", xa, 3),
                  "Tributary: query_ledger edge %u refused, never wrapped", k);
        }
    }

    /* TB7: answer_src_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1200000000, 1200000000);
        b[i] = wrand(-120, 120);
        c[i] = wrand(-1200000000, 1200000000);
        d[i] = wrand(0, INT64_C(1200000000000000));
        if (i == 0) a[i] = 999999999, b[i] = 99, c[i] = 999999999, d[i] = INT64_C(999999999999999);
        if (i == 1) a[i] = 1000000000, b[i] = 0, c[i] = 1, d[i] = 0; /* question past 9 digits */
        want[i] = T_(tributary_source_fits(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t sa[4] = {{"question", a, WAVE_ROWS},
                         {"model", b, WAVE_ROWS},
                         {"code", c, WAVE_ROWS},
                         {"stamp", d, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "answer_src_ok", sa, 4, want, WAVE_ROWS, true),
          "Tributary: answer_src_ok, MYRA = C");

    /* the parts, called through Tributary as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(0, 3), b[i] = wrand(0, RETORT_SCORE_MAX);
    {
        /* want is one value for the whole vector (KRON sums it to a scalar) */
        int64_t w1 = retort_model_score_total(a, 2, b, WAVE_ROWS), two[1] = {2};
        zxapp_arg_t xa[3] = {{"model", a, WAVE_ROWS}, {"want", two, 1}, {"score", b, WAVE_ROWS}};
        zxapp_num_t o[WAVE_ROWS];
        uint32_t k = 0;
        int32_t lp = -1;
        int rc =
            zxapp_call_witnessed(app, &H, "retort.model_score_total", xa, 3, o, WAVE_ROWS, &k, &lp);
        CHECK(rc == ZXAPP_OK && lp == ZXAPP_TRUE && k == 1 && o[0].num == w1 && o[0].den == 1,
              "Tributary: retort.model_score_total, a source's points (%lld)", (long long) w1);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, RETORT_LATENCY_MAX);
        b[i] = wrand(0, RETORT_LATENCY_MAX);
        want[i] = retort_latency_fastest(a[i], b[i]);
        c[i] = retort_latency_slowest(a[i], b[i]);
    }
    zxapp_arg_t fs[2] = {{"fastest", a, WAVE_ROWS}, {"ms", b, WAVE_ROWS}};
    zxapp_arg_t ss[2] = {{"slowest", a, WAVE_ROWS}, {"ms", b, WAVE_ROWS}};
    CHECK(wave_same(app, "retort.latency_fastest_ms", fs, 2, want, WAVE_ROWS, false) &&
              wave_same(app, "retort.latency_slowest_ms", ss, 2, c, WAVE_ROWS, false),
          "Tributary: retort's latency fold, the fastest and slowest answer");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1, RETORT_PROMPT_MAX + 1);
        b[i] = wrand(-1, 5);
        c[i] = wrand(-1, RETORT_SECONDS_MAX + 1);
        want[i] = T_(retort_run_limits_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t ra[3] = {
        {"prompt", a, WAVE_ROWS}, {"fuel", b, WAVE_ROWS}, {"seconds", c, WAVE_ROWS}};
    CHECK(wave_same(app, "retort.run_limits_ok", ra, 3, want, WAVE_ROWS, true),
          "Tributary: retort.run_limits_ok, one question's limits");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, RETORT_UJ_MAX);
        b[i] = wrand(1, 8); /* answers to one question */
        CHECK(retort_energy_per_answer(a[i], b[i], &num[i], &den[i]), "Tributary: RT4 row %u", i);
    }
    zxapp_arg_t ea[2] = {{"uj", a, WAVE_ROWS}, {"answers", b, WAVE_ROWS}};
    CHECK(trib_rat_rows(app, "retort.energy_per_answer_uj", ea, 2, num, den, WAVE_ROWS,
                        ZXAPP_NEITHER),
          "Tributary: retort.energy_per_answer_uj, the energy per answer");
    {
        /* three sources' tokens: 120, 80 and 0 */
        static const int64_t tk[3] = {120, 80, 0};
        int64_t w[3];
        for (uint32_t i = 0; i < 3u; i++) w[i] = tributary_token_permille(tk, 3, i);
        zxapp_arg_t xa[1] = {{"t", tk, 3}};
        CHECK(w[0] == 600 && w[1] == 400 && w[2] == 0 &&
                  wave_same(app, "swarm_shares.cycle_share", xa, 1, w, 3, false),
              "Tributary: swarm_shares.cycle_share, each source's tokens per mille");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(1, 8); /* answers to one question */
        a[i] = wrand(0, b[i]);
        want[i] = assay_pass_share_x100(a[i], b[i]);
    }
    zxapp_arg_t pa[2] = {{"passes", a, WAVE_ROWS}, {"runs", b, WAVE_ROWS}};
    CHECK(wave_same(app, "assay.pass_share_x100", pa, 2, want, WAVE_ROWS, false),
          "Tributary: assay.pass_share_x100, the share behind the lead");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1, ASSAY_SHARE_MAX);
        b[i] = wrand(-1, 9);
        c[i] = wrand(-1, 9);
        want[i] = T_(assay_meets_bar(a[i], b[i], c[i]));
    }
    zxapp_arg_t ba[3] = {
        {"passroom", a, WAVE_ROWS}, {"refusalroom", b, WAVE_ROWS}, {"runroom", c, WAVE_ROWS}};
    CHECK(wave_same(app, "assay.meets_bar", ba, 3, want, WAVE_ROWS, true),
          "Tributary: assay.meets_bar, the owner's bar");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 3);
        b[i] = wrand(0, 3);
        want[i] = T_(twelvefold_kept(a[i], b[i]) == 1);
    }
    zxapp_arg_t ka[2] = {{"premise", a, WAVE_ROWS}, {"conclusion", b, WAVE_ROWS}};
    CHECK(wave_same(app, "twelvefold.lp_entail_kept", ka, 2, want, WAVE_ROWS, true),
          "Tributary: twelvefold.lp_entail_kept, a chosen answer follows");

    /* TB2, the BOTH test: three models answer 42, 42 and 41 under a quorum
     * of 3. Their codes' agree counts come from the witnessed entry; the
     * verdict is BOTH, both answers shown with the models that gave them,
     * none picked at random. */
    {
        static const char *const who[3] = {"the slot tinyllama", "peer 10.0.0.7", "template sums"};
        static const char *const said[3] = {"42", "42", "41"};
        static const int64_t code[3] = {1, 1, 2}; /* numbered by first appearance */
        int64_t agree[3] = {2, 2, 1};
        zxapp_arg_t xa[1] = {{"code", code, 3}};
        CHECK(wave_same(app, "agreeing_answers", xa, 1, agree, 3, false),
              "Tributary: 42, 42, 41: agree counts 2, 2, 1, KRON = Trebago");
        lpres_state_t v = tributary_verdict(code, 3, 3);
        CHECK(v == LPRES_STATE_BOTH && (int) v == 3 && v != LPRES_STATE_TRUE &&
                  lpres_conjoin(v, LPRES_STATE_TRUE) == LPRES_STATE_BOTH,
              "Tributary: a split under a quorum of 3 is BOTH (%d)", (int) v);
        CHECK(tributary_lead_code(code, agree, 3) == 1, "Tributary: 42 leads, 2 of 3");
        char shown[256];
        uint32_t k = 0;
        for (uint32_t i = 0; i < 3u; i++) {
            bool first = true;
            for (uint32_t j = 0; j < i; j++) first = first && code[j] != code[i];
            if (!first) continue;
            k += (uint32_t) snprintf(shown + k, sizeof shown - k, "%s%s:", k ? "; " : "", said[i]);
            for (uint32_t j = i; j < 3u; j++)
                if (code[j] == code[i])
                    k += (uint32_t) snprintf(shown + k, sizeof shown - k, " %s", who[j]);
        }
        CHECK(!strcmp(shown, "42: the slot tinyllama peer 10.0.0.7; 41: template sums"),
              "Tributary: both answers shown with their models: %s", shown);
        /* the same answers below the quorum, all agreeing, and a tie */
        static const int64_t same[3] = {7, 7, 7}, tie_code[2] = {1, 2};
        int64_t tie[2] = {1, 1};
        CHECK(tributary_verdict(code, 3, 4) == LPRES_STATE_NEITHER &&
                  tributary_verdict(same, 3, 3) == LPRES_STATE_TRUE &&
                  tributary_verdict(same, 0, 1) == LPRES_STATE_NEITHER &&
                  tributary_lead_code(tie_code, tie, 2) == 0,
              "Tributary: too few NEITHER, all agree TRUE, none NEITHER, a tie picks none");
    }
}
