/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Colonnade's model against zxvd's real dispatcher (LINK): files.read and
 * files.write as zxvd answers them, and play.* refused there, since the
 * Playground is not linked in this in-process zxvd. play.catalogue,
 * play.run and play.draft are answered on a link of their own (CL_LINK),
 * which passes every other request to the real dispatcher and counts what
 * it is sent. The clock is the test's: each read moves it CL_STEP ms. */
#include "colonnade_model.h"

static colonnade_t CL;
static zxm_link_t CL_LINK;
static const char *CL_CAT, *CL_RUN, *CL_DRAFT; /* play.* results */
static uint32_t CL_SENT;                       /* play.* requests sent */
static char CL_REQ[ZXM_REQ_MAX + 1];           /* the last play.* request */
static int64_t CL_NOW, CL_STEP;

static int32_t cl_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *r = NULL;
    if (strstr(req, "\"method\":\"play.")) {
        CL_SENT++;
        memcpy(CL_REQ, req, len);
        CL_REQ[len] = 0;
    }
    if (strstr(req, "\"play.catalogue\"")) r = CL_CAT;
    if (strstr(req, "\"play.run\"")) r = CL_RUN;
    if (strstr(req, "\"play.draft\"")) r = CL_DRAFT;
    if (r) return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
    return inproc(NULL, req, len, resp, cap);
}

static int64_t cl_clock(void *ctx)
{
    (void) ctx;
    int64_t now = CL_NOW;
    CL_NOW += CL_STEP;
    return now;
}

static bool cl_write(const char *path, const char *text)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "text", text);
    return zxm_call(&LINK) == ZXM_OK;
}

#define CL_RESULT(v, s1, s2)                                                                       \
    "{\"template\":\"account-statement\",\"verdict\":\"" v "\",\"headline\":\"h\","                \
    "\"approval\":\"not required\",\"safety\":\"advisory\",\"steps\":["                            \
    "{\"name\":\"post\",\"lang\":\"sutra\",\"file\":\"a.sutra\",\"verdict\":\"" v "\","            \
    "\"ran\":" s1 "},{\"name\":\"sum\",\"lang\":\"kron\",\"file\":\"b.kron\",\"verdict\":"         \
    "\"TRUE\",\"ran\":" s2 "}]}"

APPS_TEST(colonnade)
{
    colonnade_witness_t w;
    int64_t runs = 0, ms = 0, place = 0;
    uint32_t before;

    /* CL1-CL6 and the index through the model's own C */
    CHECK(colonnade_fuel_left(10, 3) == 7 && colonnade_fuel_left(3, 10) == COLONNADE_REFUSED &&
              colonnade_play_limits_ok(49152, 60000) && !colonnade_play_limits_ok(49153, 1) &&
              colonnade_witness_split(1, 1) && !colonnade_witness_split(1, 2) &&
              colonnade_pillar_of("COBOL", &place) == COLONNADE_SUTRA && place == 9 &&
              colonnade_pillar_of("treaty", NULL) == COLONNADE_SUTRA &&
              colonnade_pillar_of("kron", &place) == COLONNADE_KRON && place == 14 &&
              colonnade_pillar_of("K", &place) == COLONNADE_KRON && place == 73 &&
              colonnade_pillar_of("Cairndrake", &place) == COLONNADE_LINGUA && place == 30 &&
              colonnade_pillar_of("myra", NULL) == COLONNADE_MYRA &&
              colonnade_pillar_of("Ada", NULL) == COLONNADE_EVE &&
              colonnade_pillar_of("FORTRAN", NULL) == COLONNADE_TREBAGO &&
              colonnade_pillar_of("OCaml", NULL) == COLONNADE_REFUSED &&
              colonnade_pillar_of(NULL, NULL) == COLONNADE_REFUSED,
          "colonnade: the rules and the language index (CL1-CL5)");

    /* CM1: the real dispatcher has no Playground linked: refused, nothing kept */
    colonnade_init(&CL, &LINK, cl_clock, NULL);
    CHECK(colonnade_catalogue(&CL) == ZXM_E_REMOTE && LINK.err_code == -32601 && !CL.known &&
              CL.ntemplates == 0,
          "colonnade: play.catalogue where zxvd has no Playground: %s", LINK.err);
    CHECK(colonnade_run(&CL, 0) == ZXM_E_STATE && colonnade_draft(&CL, 0, "x") == ZXM_E_STATE &&
              colonnade_edit(&CL, 0, "a.sutra", "x") == ZXM_E_STATE && CL.nruns == 0,
          "colonnade: nothing runs before the catalogue is read");

    /* CM1 through its own link */
    zxm_link_init(&CL_LINK, cl_send, &CL_LINK);
    colonnade_init(&CL, &CL_LINK, cl_clock, NULL);
    CL_CAT = "[{\"id\":\"account-statement\",\"title\":\"Account statement\",\"lang\":\"sutra\","
             "\"audience\":\"a\",\"safety\":\"advisory\",\"summary\":\"s\",\"steps\":["
             "{\"name\":\"post\",\"lang\":\"sutra\",\"file\":\"a.sutra\"},{\"name\":\"sum\","
             "\"lang\":\"kron\",\"file\":\"b.kron\"}],\"files\":[],\"draft\":{\"file\":"
             "\"statement.sutra\",\"instruction\":\"write it\"}},"
             "{\"id\":\"pump-controller\",\"title\":\"Pump\",\"lang\":\"eve\",\"safety\":\"none\","
             "\"steps\":[{\"name\":\"limits\",\"lang\":\"eve\",\"file\":\"p.eve\"}]}]";
    CHECK(colonnade_catalogue(&CL) == ZXM_OK && CL.known && CL.ntemplates == 2 &&
              !strcmp(CL.tmpl[0].id, "account-statement") && CL.tmpl[0].pillar == COLONNADE_SUTRA &&
              CL.tmpl[0].nsteps == 2 && !strcmp(CL.tmpl[0].draft_file, "statement.sutra") &&
              !strcmp(CL.tmpl[0].safety, "advisory") && CL.tmpl[1].pillar == COLONNADE_EVE &&
              !CL.tmpl[1].draft_file[0] && strstr(CL.notice, "2 templates"),
          "colonnade: the catalogue: %s (%s)", CL.notice, CL_LINK.err);
    CL_CAT = "[{\"title\":\"no id\",\"lang\":\"sutra\",\"steps\":[]}]";
    CHECK(colonnade_catalogue(&CL) == ZXM_E_PROTOCOL && CL.ntemplates == 2,
          "colonnade: a template without its id is not zxvd's; the catalogue stays");
    CL_CAT = "{\"id\":\"x\"}";
    CHECK(colonnade_catalogue(&CL) == ZXM_E_PROTOCOL && CL.ntemplates == 2,
          "colonnade: a catalogue that is no list");

    /* CM3: a run without the editor's file, then with it */
    CL_NOW = 1000, CL_STEP = 40;
    CL_RUN = CL_RESULT("TRUE", "true", "true");
    before = CL_SENT;
    CHECK(colonnade_run(&CL, 0) == ZXM_OK && CL_SENT == before + 1u && CL.nruns == 1 &&
              CL.run[0].verdict == LPRES_STATE_TRUE && CL.run[0].steps_ran == 2 &&
              CL.run[0].ms == 40 && !CL.run[0].over && !CL.run[0].edited && CL.spent == 2 &&
              colonnade_fuel(&CL) == COLONNADE_DEFAULT_FUEL - 2 &&
              strstr(CL_REQ, "\"template\":\"account-statement\"") && !strstr(CL_REQ, "files"),
          "colonnade: a run on scratch state: %s (%s)", CL.notice, CL_REQ);
    CHECK(colonnade_edit(&CL, 0, "a/b.sutra", "x") == ZXM_E_ARG &&
              colonnade_edit(&CL, 0, "", "x") == ZXM_E_ARG &&
              colonnade_edit(&CL, 9, "a.sutra", "x") == ZXM_E_STATE && CL.edit_tmpl == -1,
          "colonnade: a file name that play.run would not take");
    CHECK(colonnade_edit(&CL, 0, "a.sutra", "record \"q\"\n  x: 9(3)\n") == ZXM_OK &&
              CL.edit_tmpl == 0 && CL.len == 21,
          "colonnade: the person's edit: %s", CL.notice);
    CL_RUN = CL_RESULT("FALSE", "true", "false");
    CHECK(colonnade_run(&CL, 0) == ZXM_OK && CL.nruns == 2 &&
              CL.run[1].verdict == LPRES_STATE_FALSE && CL.run[1].steps_ran == 1 &&
              CL.run[1].edited && CL.spent == 3 &&
              strstr(CL_REQ, "\"files\":{\"a.sutra\":\"record \\\"q\\\"\\n  x: 9(3)\\n\"}"),
          "colonnade: the edit is sent, escaped: %s", CL_REQ);
    CL_RUN = CL_RESULT("TRUE", "true", "true");
    CHECK(colonnade_run(&CL, 1) == ZXM_OK && !CL.run[2].edited && !strstr(CL_REQ, "files") &&
              CL.run[2].pillar == COLONNADE_EVE,
          "colonnade: another template's run leaves the editor's file out");

    /* CM6: witnessed runs */
    {
        static const uint32_t agree[2] = {0, 2}, split[3] = {0, 1, 2}, bad[2] = {0, 9};
        CHECK(colonnade_witness(&CL, agree, 2, &w) == ZXM_OK && w.verdict == LPRES_STATE_TRUE &&
                  !w.held && w.ntrue == 2 && w.nfalse == 0,
              "colonnade: two runs agree: %s", CL.notice);
        CHECK(colonnade_witness(&CL, split, 3, &w) == ZXM_OK && w.verdict == LPRES_STATE_BOTH &&
                  w.held && w.ntrue == 2 && w.nfalse == 1 && w.true_side[0] == 0 &&
                  w.true_side[1] == 2 && w.false_side[0] == 1 && strstr(CL.notice, "Held"),
              "colonnade: a TRUE run and a FALSE run: BOTH, each side kept: %s", CL.notice);
        CHECK(colonnade_witness(&CL, bad, 2, &w) == ZXM_E_ARG &&
                  colonnade_witness(&CL, split, 0, &w) == ZXM_E_ARG &&
                  colonnade_witness(&CL, NULL, 2, &w) == ZXM_E_ARG,
              "colonnade: runs that are none");
    }

    /* CM3, CL4: a run past a minute is kept as over, its verdict not used */
    CL_STEP = COLONNADE_MS_MAX + 1;
    CL_RUN = CL_RESULT("FALSE", "true", "true");
    CHECK(colonnade_run(&CL, 0) == ZXM_OK && CL.nruns == 4 && CL.run[3].over &&
              CL.run[3].verdict == LPRES_STATE_NEITHER && CL.run[3].ms == COLONNADE_MS_MAX + 1 &&
              CL.spent == 7 && strstr(CL.notice, "past its limits"),
          "colonnade: over a minute: %s", CL.notice);
    {
        static const uint32_t with_over[2] = {0, 3};
        CHECK(colonnade_witness(&CL, with_over, 2, &w) == ZXM_OK && w.verdict == LPRES_STATE_TRUE &&
                  !w.held,
              "colonnade: an over run says nothing in the join");
    }
    CL_STEP = 40;

    /* a result that is not zxvd's: nothing kept, no fuel spent */
    CL_RUN = CL_RESULT("MAYBE", "true", "true");
    CHECK(colonnade_run(&CL, 0) == ZXM_E_PROTOCOL && CL.nruns == 4 && CL.spent == 7,
          "colonnade: a verdict that is none");
    CL_RUN = "{\"verdict\":\"TRUE\"}";
    CHECK(colonnade_run(&CL, 0) == ZXM_E_PROTOCOL && CL.nruns == 4 && CL.spent == 7,
          "colonnade: a result without its steps");

    /* CM7: runs and time per pillar, and the output share */
    CHECK(colonnade_tally(&CL, COLONNADE_SUTRA, &runs, &ms) == ZXM_OK && runs == 3 &&
              ms == 40 + 40 + COLONNADE_MS_MAX,
          "colonnade: Sutra's runs and time: %s", CL.notice);
    CHECK(colonnade_tally(&CL, COLONNADE_EVE, &runs, &ms) == ZXM_OK && runs == 1 && ms == 40 &&
              colonnade_tally(&CL, COLONNADE_MYRA, &runs, &ms) == ZXM_OK && runs == 0 && ms == 0 &&
              colonnade_tally(&CL, 6, &runs, &ms) == ZXM_E_ARG,
          "colonnade: Eve's, MYRA's, and a pillar that is none");
    CHECK(colonnade_output_x100(&CL, 0) ==
                  colonnade_used_x100(COLONNADE_OUT_MAX, COLONNADE_OUT_MAX - CL.run[0].outbytes) &&
              CL.run[0].outbytes > 0 && colonnade_output_x100(&CL, 0) > 0 &&
              colonnade_output_x100(&CL, 99) == COLONNADE_REFUSED,
          "colonnade: the share of the output limit a run used");

    /* CM4: fuel only on the owner's word, never below what is spent */
    CL_RUN = CL_RESULT("TRUE", "true", "true");
    before = CL_SENT;
    CHECK(colonnade_set_fuel(&CL, 100, false) == ZXM_E_STATE &&
              CL.fuel_max == COLONNADE_DEFAULT_FUEL,
          "colonnade: no fuel set without the owner's word");
    CHECK(colonnade_set_fuel(&CL, 5, true) == ZXM_E_ARG &&
              colonnade_set_fuel(&CL, 0, true) == ZXM_E_ARG &&
              colonnade_set_fuel(&CL, COLONNADE_FUEL_MAX + 1, true) == ZXM_E_ARG &&
              CL.fuel_max == COLONNADE_DEFAULT_FUEL,
          "colonnade: a budget below the 7 spent, 0 or past 18 digits is refused");
    CHECK(colonnade_set_fuel(&CL, 7 + COLONNADE_STEPS - 1, true) == ZXM_OK &&
              colonnade_fuel(&CL) == COLONNADE_STEPS - 1 && colonnade_run(&CL, 0) == ZXM_E_STATE &&
              CL_SENT == before && CL.nruns == 4 && strstr(CL.notice, "fuel left"),
          "colonnade: 11 fuel left: no run is sent: %s", CL.notice);
    CHECK(colonnade_set_fuel(&CL, 7 + COLONNADE_STEPS, true) == ZXM_OK &&
              colonnade_run(&CL, 0) == ZXM_OK && CL.spent == 9 && colonnade_fuel(&CL) == 10,
          "colonnade: 12 fuel left: the run goes, 2 steps spend 2");

    /* CM5: a draft is untrusted: kept apart, never in the editor, never run by itself */
    CHECK(colonnade_set_fuel(&CL, 200, true) == ZXM_OK, "colonnade: fuel for the rest");
    before = CL_SENT;
    CHECK(colonnade_draft(&CL, 1, "make it count") == ZXM_E_STATE && CL_SENT == before &&
              !CL.has_draft,
          "colonnade: a template with no draft is not asked");
    CL_DRAFT = "{\"model\":true,\"draft\":\"record d\\n  n: 9(2)\\n\"}";
    CHECK(colonnade_draft(&CL, 0, "count the lines") == ZXM_OK && CL.has_draft && CL.draft_model &&
              !strcmp(CL.draft, "record d\n  n: 9(2)\n") && !strcmp(CL.file, "a.sutra") &&
              !strcmp(CL.text, "record \"q\"\n  x: 9(3)\n") &&
              strstr(CL_REQ, "\"request\":\"count the lines\"") && strstr(CL.notice, "untrusted"),
          "colonnade: the draft is shown, the editor is as it was: %s", CL.notice);
    CL_RUN = CL_RESULT("TRUE", "true", "true");
    CHECK(colonnade_run(&CL, 0) == ZXM_OK && !strstr(CL_REQ, "record d") &&
              strstr(CL_REQ, "record \\\"q\\\""),
          "colonnade: a run sends the person's file, never the draft");
    CHECK(colonnade_take_draft(&CL, false) == ZXM_E_STATE && CL.has_draft &&
              !strcmp(CL.file, "a.sutra"),
          "colonnade: the draft is not taken without the owner's word");
    CHECK(colonnade_take_draft(&CL, true) == ZXM_OK && !CL.has_draft &&
              !strcmp(CL.file, "statement.sutra") && !strcmp(CL.text, "record d\n  n: 9(2)\n") &&
              colonnade_take_draft(&CL, true) == ZXM_E_STATE,
          "colonnade: taken on the owner's word, once: %s", CL.notice);
    CHECK(colonnade_run(&CL, 0) == ZXM_OK && strstr(CL_REQ, "\"statement.sutra\":\"record d\\n"),
          "colonnade: the taken draft runs as the person's file, checked by play.run");
    CHECK(colonnade_draft(&CL, 0, NULL) == ZXM_E_ARG, "colonnade: no request");

    /* CM2: a source file from the Hoard, one files.read */
    {
        static char big[COLONNADE_SOURCE_MAX + 2];
        memset(big, 'x', sizeof big - 1u);
        big[sizeof big - 1u] = 0;
        CHECK(cl_write("colonnade-sum.kron", "+/x\n") && cl_write("colonnade-big.kron", big),
              "colonnade: two files written: %s", LINK.err);
        CHECK(colonnade_open(&CL, 1, "", "colonnade-sum.kron") == ZXM_OK &&
                  !strcmp(CL.text, "+/x\n") && !strcmp(CL.file, "colonnade-sum.kron") &&
                  CL.edit_tmpl == 1 && CL.len == 4,
              "colonnade: opened from the Hoard: %s (%s)", CL.notice, CL_LINK.err);
        CHECK(colonnade_open(&CL, 0, "", "colonnade-big.kron") == ZXM_E_TOOBIG &&
                  CL.edit_tmpl == 1 && !strcmp(CL.text, "+/x\n"),
              "colonnade: a file past the editor is refused, the editor kept");
        CHECK(colonnade_open(&CL, 0, "", "no-such.kron") == ZXM_E_REMOTE && CL.edit_tmpl == 1,
              "colonnade: a missing file is zxvd's error: %s", CL_LINK.err);
        CHECK(colonnade_open(&CL, 0, "", "") == ZXM_E_ARG, "colonnade: no path");
    }

    /* CM3: the runs a session keeps */
    while (CL.nruns < COLONNADE_RUNS && colonnade_run(&CL, 1) == ZXM_OK) {
    }
    before = CL_SENT;
    CHECK(CL.nruns == COLONNADE_RUNS && colonnade_run(&CL, 1) == ZXM_E_STATE && CL_SENT == before,
          "colonnade: %u runs kept, then none sent", COLONNADE_RUNS);
}
