/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Twelvefold's model against zxvd's real dispatcher (LINK): formulas parsed
 * and refused, the kernel's tables, a formula's table, counts and tag,
 * consequences that follow and that do not, a set kept in the Hoard and
 * read back, and the Playground (play.catalogue, play.run) with a stand-in
 * engine set in zxvd for the test, then taken away again. */
#include "twelvefold_model.h"

static char tf_play_seen[256];

static const char *tf_play_cat(zxvd_buf_t *out)
{
    static const char C[] = "[{\"id\":\"lp-tables\",\"title\":\"Four values\",\"lang\":\"kron\"},"
                            "{\"id\":\"cobol-hello\",\"title\":\"Hello\",\"lang\":\"sutra\"}]";
    zxvd_buf_raw(out, C, sizeof C - 1u);
    return NULL;
}

static const char *tf_play_run(const char *tid, const zxvd_play_file_t *f, uint32_t n,
                               zxvd_buf_t *out)
{
    static const char R[] =
        "{\"template\":\"lp-tables\",\"verdict\":\"BOTH\","
        "\"headline\":\"held for a person\",\"approval\":\"required\","
        "\"steps\":[{\"name\":\"a\",\"ran\":true},{\"name\":\"b\",\"ran\":false}]}";
    snprintf(tf_play_seen, sizeof tf_play_seen, "%s|%u|%s|%s", tid, n, n ? f[0].name : "",
             n ? f[0].text : "");
    if (!strcmp(tid, "no-such")) return "no such template";
    zxvd_buf_raw(out, R, sizeof R - 1u);
    return NULL;
}

APPS_TEST(twelvefold)
{
    static twelvefold_t T, T2;
    static twelvefold_table_t TB;
    static twelvefold_formula_t F;
    twelvefold_verdict_t V;
    uint32_t at = 0;

    /* TF1, TF2: parsing and the limits */
    CHECK(twelvefold_parse(&F, "(a & ~b) | c -> T", &at) == ZXM_OK && F.vars == 7u &&
              F.depth == 4 && !strcmp(F.text, "(a & ~b) | c -> T"),
          "twelvefold: a formula parsed, 3 variables, 4 deep");
    CHECK(twelvefold_parse(&F, "a &", &at) == ZXM_E_ARG && at == 3 &&
              twelvefold_parse(&F, "a b", &at) == ZXM_E_ARG && at == 2 &&
              twelvefold_parse(&F, "(a", &at) == ZXM_E_ARG && at == 2 &&
              twelvefold_parse(&F, "", &at) == ZXM_E_ARG && at == 0 &&
              twelvefold_parse(&F, "a % b", &at) == ZXM_E_ARG && at == 2 &&
              twelvefold_parse(&F, "A", &at) == ZXM_E_ARG && at == 0,
          "twelvefold: a broken formula is refused at the byte where it broke");
    CHECK(twelvefold_parse(&F, "a|b|c|d|e|f", &at) == ZXM_OK &&
              twelvefold_parse(&F, "a|b|c|d|e|f|g", &at) == ZXM_E_ARG && at == 13,
          "twelvefold: 6 variables pass, a seventh is past the limit (Eve's range)");
    CHECK(twelvefold_parse(&F, "~~~~~~~~~~~~~~~~a", &at) == ZXM_OK && F.depth == 16 &&
              twelvefold_parse(&F, "~~~~~~~~~~~~~~~~~a", &at) == ZXM_E_ARG && at == 18 &&
              twelvefold_parse(&F, "((((((((((((((((((((a))))))))))))))))))))", &at) == ZXM_OK &&
              F.depth == 0,
          "twelvefold: 16 deep passes, 17 is past the limit; brackets add no depth");

    /* TF3: the kernel's tables, never re-derived */
    CHECK(twelvefold_connective(TWELVEFOLD_OP_AND, TWELVEFOLD_CODE_B, TWELVEFOLD_CODE_N) ==
                  TWELVEFOLD_CODE_N &&
              twelvefold_connective(TWELVEFOLD_OP_OR, TWELVEFOLD_CODE_B, TWELVEFOLD_CODE_N) ==
                  TWELVEFOLD_CODE_B &&
              twelvefold_connective(TWELVEFOLD_OP_NOT, TWELVEFOLD_CODE_B, 0) == TWELVEFOLD_CODE_B &&
              twelvefold_connective(TWELVEFOLD_OP_NOT, TWELVEFOLD_CODE_T, 0) == TWELVEFOLD_CODE_F &&
              twelvefold_connective(TWELVEFOLD_OP_IMP, TWELVEFOLD_CODE_T, TWELVEFOLD_CODE_F) ==
                  TWELVEFOLD_CODE_F &&
              twelvefold_connective(TWELVEFOLD_OP_AND, TWELVEFOLD_CODE_F, TWELVEFOLD_CODE_B) ==
                  TWELVEFOLD_CODE_F &&
              twelvefold_connective(TWELVEFOLD_OP_AND, 4, 1) == 255u &&
              twelvefold_connective(9, 1, 1) == 255u,
          "twelvefold: the connectives are lpres's chain F < N < B < T");
    CHECK(twelvefold_tag(TWELVEFOLD_CODE_T, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST, NULL) == 63u &&
              twelvefold_tag(TWELVEFOLD_CODE_F, LPX_P_ENTROPIC, LPX_PH_POTENTIAL, NULL) == 0u &&
              twelvefold_tag(4, 0, 0, NULL) == 255u,
          "twelvefold: the hexagram tags at the two ends, and a code that is none");

    /* the set */
    twelvefold_init(&T, &LINK);
    CHECK(twelvefold_add(&T, "a | ~a", &at) == ZXM_OK && twelvefold_add(&T, "a", &at) == ZXM_OK &&
              twelvefold_add(&T, "a -> b", &at) == ZXM_OK &&
              twelvefold_add(&T, "b", &at) == ZXM_OK && twelvefold_add(&T, "~a", &at) == ZXM_OK &&
              twelvefold_add(&T, "a & b", &at) == ZXM_OK &&
              twelvefold_add(&T, "a &&", &at) == ZXM_E_ARG && T.n == 6 && T.dirty,
          "twelvefold: six formulas added, a broken one refused");

    /* TF4: excluded middle's table, counts and tag */
    CHECK(twelvefold_table(&T, 0, &TB) == ZXM_OK && TB.nvars == 1 && TB.rows == 4 &&
              TB.value[TWELVEFOLD_CODE_N] == TWELVEFOLD_CODE_N &&
              TB.value[TWELVEFOLD_CODE_T] == TWELVEFOLD_CODE_T &&
              TB.value[TWELVEFOLD_CODE_F] == TWELVEFOLD_CODE_T &&
              TB.value[TWELVEFOLD_CODE_B] == TWELVEFOLD_CODE_B && TB.count[0] == 1 &&
              TB.count[1] == 2 && TB.count[2] == 0 && TB.count[3] == 1 &&
              TB.meet == TWELVEFOLD_CODE_N &&
              TB.tag == twelvefold_tag(TWELVEFOLD_CODE_N, LPX_P_NEUTRAL, LPX_PH_MANIFEST, NULL) &&
              TB.action == LPX_ACT_WAIT,
          "twelvefold: a | ~a gives N, T, T, B: one NEITHER, two TRUE, one BOTH; it waits");
    CHECK(twelvefold_table(&T, 5, &TB) == ZXM_OK && TB.rows == 16 &&
              TB.count[0] + TB.count[1] + TB.count[2] + TB.count[3] == 16 && TB.count[1] == 1 &&
              TB.count[2] == 7 && TB.meet == TWELVEFOLD_CODE_F && TB.action == LPX_ACT_REFUSE &&
              twelvefold_table(&T, 6, &TB) == ZXM_E_ARG,
          "twelvefold: a & b over 16 assignments: one TRUE, seven FALSE; refused");

    /* TF5: modus ponens fails, explosion fails, conjunction elimination holds */
    {
        static const uint32_t mp[2] = {1, 2}, ex[2] = {1, 4}, ce[1] = {5};
        CHECK(twelvefold_follows(&T, mp, 2, 3, &V) == ZXM_OK && !V.follows && V.breaks == 2 &&
                  V.rows == 16 && V.counter == 12 && V.premise == TWELVEFOLD_CODE_B &&
                  V.conclusion == TWELVEFOLD_CODE_N,
              "twelvefold: a, a -> b do not give b: a BOTH breaks it twice, first at b NEITHER");
        CHECK(twelvefold_follows(&T, ex, 2, 3, &V) == ZXM_OK && !V.follows && V.breaks > 0,
              "twelvefold: a, ~a do not give every b (no explosion)");
        CHECK(twelvefold_follows(&T, ce, 1, 1, &V) == ZXM_OK && V.follows && V.breaks == 0 &&
                  V.counter == -1,
              "twelvefold: a & b gives a");
        CHECK(twelvefold_follows(&T, NULL, 0, 0, &V) == ZXM_OK && !V.follows && V.breaks == 1 &&
                  V.counter == TWELVEFOLD_CODE_N,
              "twelvefold: a | ~a from nothing breaks only where a is NEITHER");
        CHECK(twelvefold_follows(&T, mp, 2, 9, &V) == ZXM_E_ARG, "twelvefold: no such conclusion");
    }
    {
        /* seven variables shared between two formulas of six are past the limit */
        twelvefold_t *W = &T2;
        static const uint32_t one[1] = {0};
        twelvefold_init(W, &LINK);
        CHECK(twelvefold_add(W, "a|b|c|d|e|f", &at) == ZXM_OK &&
                  twelvefold_add(W, "g", &at) == ZXM_OK &&
                  twelvefold_follows(W, one, 1, 1, &V) == ZXM_E_ARG,
              "twelvefold: a consequence over 7 variables is refused (TF2)");
    }

    /* TF7: into the Hoard and back */
    CHECK(twelvefold_save(&T) == ZXM_E_STATE && twelvefold_save_as(&T, "", "") == ZXM_E_ARG,
          "twelvefold: nowhere to save yet");
    CHECK(twelvefold_save_as(&T, "home", "logic-set.lp") == ZXM_OK && !T.dirty &&
              !strcmp(T.path, "logic-set.lp") && T.space[0] == 0,
          "twelvefold: the set saved: %s", LINK.err);
    twelvefold_init(&T2, &LINK);
    CHECK(twelvefold_open(&T2, "", "logic-set.lp") == ZXM_OK && T2.n == 6 &&
              !strcmp(T2.f[2].text, "a -> b") && !strcmp(T2.f[5].text, "a & b") && !T2.dirty,
          "twelvefold: read back formula for formula: %s", LINK.err);
    CHECK(twelvefold_remove(&T2, 0) == ZXM_OK && T2.n == 5 && !strcmp(T2.f[0].text, "a") &&
              twelvefold_remove(&T2, 5) == ZXM_E_ARG && twelvefold_save(&T2) == ZXM_OK &&
              twelvefold_open(&T, "", "logic-set.lp") == ZXM_OK && T.n == 5,
          "twelvefold: one removed, saved in place and read again: %s", LINK.err);
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", "not-a-set.lp");
    zxm_p_str(&LINK, "text", "# twelvefold 1\na &\n");
    CHECK(zxm_call(&LINK) == ZXM_OK, "twelvefold: a broken set written: %s", LINK.err);
    CHECK(twelvefold_open(&T, "", "not-a-set.lp") == ZXM_E_ARG && T.n == 5 &&
              !strcmp(T.path, "logic-set.lp"),
          "twelvefold: a set with a broken line leaves the open set as it was");
    CHECK(twelvefold_open(&T, "", "no-such.lp") == ZXM_E_REMOTE && T.n == 5,
          "twelvefold: a missing set is zxvd's error: %s", LINK.err);
    for (uint32_t i = T.n; i < TWELVEFOLD_FORMULAS; i++) (void) twelvefold_add(&T, "T", &at);
    CHECK(T.n == TWELVEFOLD_FORMULAS && twelvefold_add(&T, "F", &at) == ZXM_E_STATE,
          "twelvefold: a full set takes no more");

    /* TF6: the Playground, first absent, then a stand-in engine */
    CHECK(twelvefold_play(&T, "lp-tables", NULL, NULL) == ZXM_E_REMOTE && LINK.err_code == -32601,
          "twelvefold: no Playground in this build: %s", LINK.err);
    {
        static const zxvd_play_ops_t OPS2 = {tf_play_cat, tf_play_run, NULL};
        zxvd_rpc_set_play_ops(&OPS2);
        CHECK(twelvefold_catalogue(&T) == ZXM_OK && T.ntpl == 2 &&
                  !strcmp(T.tpl[0].id, "lp-tables") && !strcmp(T.tpl[1].lang, "sutra"),
              "twelvefold: the catalogue: %s", LINK.err);
        CHECK(twelvefold_play(&T, "lp-tables", "main.kron", "a \"quoted\"\nline") == ZXM_OK &&
                  T.play.verdict == TWELVEFOLD_CODE_B && T.play.approval && T.play.steps == 2 &&
                  T.play.ran == 1 && !strcmp(T.play.headline, "held for a person") &&
                  !strcmp(tf_play_seen, "lp-tables|1|main.kron|a \"quoted\"\nline"),
              "twelvefold: a template run with one edited file, its BOTH held: %s", LINK.err);
        CHECK(twelvefold_play(&T, "no-such", NULL, NULL) == ZXM_E_REMOTE &&
                  LINK.err_code == -32016 && T.play.verdict == TWELVEFOLD_CODE_B,
              "twelvefold: a failed run is zxvd's error and leaves the last result");
        CHECK(twelvefold_play(&T, "", NULL, NULL) == ZXM_E_ARG &&
                  twelvefold_play(&T, "t", "f.kron", NULL) == ZXM_E_ARG &&
                  twelvefold_play(&T, "t", "a/b", "x") == ZXM_E_ARG,
              "twelvefold: no template, a file without text, a name with a folder");
        zxvd_rpc_set_play_ops(NULL);
    }
}
