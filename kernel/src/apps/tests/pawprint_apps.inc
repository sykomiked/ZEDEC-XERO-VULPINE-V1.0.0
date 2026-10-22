/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pawprint's model against zxvd's real dispatcher (LINK): log.tail and
 * journal.query as zxvd answers them, without a journal and then with one
 * on disk. play.run is not linked in this in-process zxvd, so it is
 * answered on a link of its own (PW_LINK) with PW_PLAY, in the shape the
 * Playground gives (its steps, and a refused step's error with its line
 * and column); every other request goes to the real dispatcher. */
#include "pawprint_model.h"

static pawprint_t PW;
static zxm_link_t PW_LINK;
static const char *PW_PLAY; /* play.run's result, when set */
static char pw_req[ZXM_REQ_MAX + 1];

static int32_t pw_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    if (strstr(req, "\"play.run\"")) {
        uint32_t n = len < ZXM_REQ_MAX ? len : ZXM_REQ_MAX;
        memcpy(pw_req, req, n);
        pw_req[n] = 0;
        if (!PW_PLAY)
            return snprintf(resp, cap,
                            "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32601,"
                            "\"message\":\"not available\"}}",
                            l->seq);
        return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq,
                        PW_PLAY);
    }
    return inproc(NULL, req, len, resp, cap);
}

#define PW_STEP(name, lang, file, ran, verdict, error)                                             \
    "{\"name\":\"" name "\",\"lang\":\"" lang "\",\"file\":\"" file "\",\"gate\":false,"           \
    "\"advisory\":false,\"ran\":" ran ",\"verdict\":\"" verdict "\",\"outputs\":[],"               \
    "\"truths\":[],\"held\":[],\"pending\":[],\"notes\":[]" error "}"
#define PW_ERR(line, col)                                                                          \
    ",\"error\":{\"status\":-3,\"name\":\"ZXV_E_RANGE\",\"line\":" line ",\"col\":" col            \
    ",\"message\":\"does not fit\"}"

/* load passes main.sutra (5 lines), share is refused at aux.kron line 2
 * column 4, settle never runs */
#define PW_LOAD   PW_STEP("load", "sutra", "main.sutra", "true", "TRUE", "")
#define PW_SHARE  PW_STEP("share", "kron", "aux.kron", "true", "FALSE", PW_ERR("2", "4"))
#define PW_SETTLE PW_STEP("settle", "treaty", "settle.tr", "false", "NEITHER", "")
#define PW_HEAD   "{\"template\":\"shares\",\"headline\":\"Refused at step 'share'\","
static const char *const PW_RUN3 =
    PW_HEAD "\"verdict\":\"FALSE\",\"steps\":[" PW_LOAD "," PW_SHARE "," PW_SETTLE "]}";
/* a passed step, four of them, and thirteen: one more than the Playground runs */
#define PW_A  PW_STEP("a", "kron", "f", "true", "TRUE", "")
#define PW_A4 PW_A "," PW_A "," PW_A "," PW_A
static const char *const PW_RUN13 =
    PW_HEAD "\"verdict\":\"TRUE\",\"steps\":[" PW_A4 "," PW_A4 "," PW_A4 "," PW_A "]}";

APPS_TEST(pawprint)
{
    static pawprint_frame_t fr[PAWPRINT_STEPS];
    static char many[PAWPRINT_TEXT + 2];
    uint32_t n = 0, added = 0;
    int64_t hit = -1, total = -1;

    /* PR1-PR5 through the model's own C */
    {
        int64_t stops[3] = {3, 10, 42}, steps[3] = {3, 1, 4};
        CHECK(pawprint_may_attach(1, 1, 0) && !pawprint_may_attach(1, 0, 0) &&
                  !pawprint_may_attach(0, 1, 0) && !pawprint_may_attach(1, 1, 1),
              "pawprint: may attach only to the owner's own, approved, not a system process");
        CHECK(pawprint_breakpoint_hit(stops, 3, 10, &hit) && hit == 1 &&
                  pawprint_breakpoint_hit(stops, 3, 11, &hit) && hit == 0,
              "pawprint: a breakpoint line is hit, the next line is not");
        CHECK(pawprint_stepped_insns(steps, 3, &total) && total == 8,
              "pawprint: instructions stepped, summed");
        CHECK(pawprint_limits_ok(256, 64) && !pawprint_limits_ok(257, 0) &&
                  !pawprint_limits_ok(0, 65),
              "pawprint: 256 breakpoints and 64 frames at most");
        CHECK(pawprint_record_ok(99, 9999999, 999999999, 999999999) &&
                  !pawprint_record_ok(100, 1, 0, 0),
              "pawprint: the breakpoint record's digit widths");
    }

    /* PM1: the program */
    zxm_link_init(&PW_LINK, pw_send, &PW_LINK);
    pawprint_init(&PW, &PW_LINK);
    CHECK(pawprint_run(&PW) == ZXM_E_STATE && pawprint_file(&PW, "a", "b") == ZXM_E_STATE &&
              pawprint_attach(&PW, true, false) == ZXM_E_STATE,
          "pawprint: nothing before a template is opened");
    CHECK(pawprint_open(&PW, "") == ZXM_E_ARG && pawprint_open(&PW, NULL) == ZXM_E_ARG &&
              pawprint_open(&PW, "shares") == ZXM_OK && !strcmp(PW.template_id, "shares"),
          "pawprint: a template opened by its id");
    CHECK(pawprint_file(&PW, "main.sutra", "a\nb\nc\nd\ne\n") == ZXM_OK &&
              pawprint_file(&PW, "aux.kron", "x:1\ny") == ZXM_OK && PW.nfiles == 2 &&
              PW.file[0].lines == 5 && PW.file[1].lines == 2,
          "pawprint: two files opened, their lines counted");
    memset(many, 'x', sizeof many - 1u);
    many[sizeof many - 1u] = 0;
    CHECK(pawprint_file(&PW, "../up", "x") == ZXM_E_ARG &&
              pawprint_file(&PW, "a\\b", "x") == ZXM_E_ARG &&
              pawprint_file(&PW, "", "x") == ZXM_E_ARG &&
              pawprint_file(&PW, "big", many) == ZXM_E_TOOBIG && PW.nfiles == 2,
          "pawprint: a path, an empty name or a file past 4096 bytes is refused");

    /* PM2: attach, owner-confirmed */
    CHECK(pawprint_run(&PW) == ZXM_E_STATE && strstr(PW.notice, "Not attached"),
          "pawprint: no run before an attach");
    CHECK(pawprint_attach(&PW, false, false) == ZXM_E_STATE && !PW.asked &&
              pawprint_attach(&PW, true, true) == ZXM_E_STATE && strstr(PW.notice, "system"),
          "pawprint: another's program or a system process is refused before asking");
    CHECK(pawprint_approve(&PW, true) == ZXM_E_STATE, "pawprint: nothing to approve");
    CHECK(pawprint_attach(&PW, true, false) == ZXM_OK && PW.asked && !PW.attached &&
              pawprint_run(&PW) == ZXM_E_STATE && pawprint_approve(&PW, false) == ZXM_E_STATE &&
              !PW.attached,
          "pawprint: asked, then the owner says no: not attached");
    CHECK(pawprint_attach(&PW, true, false) == ZXM_OK && pawprint_approve(&PW, true) == ZXM_OK &&
              PW.attached,
          "pawprint: the owner approves: attached");

    /* PM5: breakpoints */
    CHECK(pawprint_break(&PW, 0, 3, 0) == ZXM_OK && pawprint_break(&PW, 1, 2, 1) == ZXM_OK &&
              pawprint_break(&PW, 0, 1, 0) == ZXM_OK && PW.nbreaks == 3 && PW.brk[0].line == 1 &&
              PW.brk[1].line == 3 && PW.brk[2].file == 1,
          "pawprint: breakpoints kept by file, then line");
    CHECK(pawprint_break(&PW, 0, 3, 0) == ZXM_E_STATE &&
              pawprint_break(&PW, 0, 6, 0) == ZXM_E_ARG &&
              pawprint_break(&PW, 0, 0, 0) == ZXM_E_ARG &&
              pawprint_break(&PW, 2, 1, 0) == ZXM_E_ARG &&
              pawprint_break(&PW, 0, 2, -1) == ZXM_E_ARG &&
              pawprint_break(&PW, 0, 2, PAW_COUNT_MAX + 1) == ZXM_E_ARG && PW.nbreaks == 3,
          "pawprint: a breakpoint twice, past the file, on line 0, in no file, or with a bad "
          "condition is refused");
    CHECK(pawprint_unbreak(&PW, 0, 1) == ZXM_OK && pawprint_unbreak(&PW, 0, 1) == ZXM_E_ARG &&
              PW.nbreaks == 2,
          "pawprint: a breakpoint removed once");

    /* PM3: run */
    CHECK(pawprint_step(&PW) == ZXM_E_STATE && pawprint_stack(&PW, fr, 12, &n) == ZXM_E_STATE,
          "pawprint: nothing to replay before a run");
    PW_PLAY = NULL;
    CHECK(pawprint_run(&PW) == ZXM_E_REMOTE && !PW.have_run, "pawprint: play.run refused");
    PW_PLAY = PW_RUN3;
    CHECK(pawprint_run(&PW) == ZXM_OK && PW.have_run && PW.verdict == LPRES_STATE_FALSE &&
              PW.nsteps == 3 && PW.step[0].reach == 5 && PW.step[1].reach == 2 &&
              PW.step[1].line == 2 && PW.step[1].col == 4 && PW.step[2].reach == 0 &&
              PW.step[2].opened == -1 && !PW.step[2].ran,
          "pawprint: the run kept, each step's lines: %s", PW_LINK.err);
    CHECK(strstr(pw_req, "\"template\":\"shares\"") &&
              strstr(pw_req, "\"files\":{\"main.sutra\":\"a\\nb\\nc\\nd\\ne\\n\","
                             "\"aux.kron\":\"x:1\\ny\"}"),
          "pawprint: play.run carries the template and the opened files: %s", pw_req);

    /* PM4: continue to a breakpoint, step, continue to the end */
    CHECK(pawprint_continue(&PW) == ZXM_OK && PW.stopped && PW.at_step == 0 && PW.at_line == 3 &&
              PW.brk[PW.stopped_at].line == 3 && PW.instructions == 3 &&
              strstr(PW.notice, "Breakpoint at step load, main.sutra line 3"),
          "pawprint: stopped at main.sutra line 3: %s", PW.notice);
    CHECK(pawprint_stack(&PW, fr, 12, &n) == ZXM_OK && n == 1 && !strcmp(fr[0].step, "load") &&
              fr[0].line == 3,
          "pawprint: one frame, at line 3");
    CHECK(pawprint_step(&PW) == ZXM_OK && !PW.stopped && PW.at_line == 4 && PW.instructions == 4,
          "pawprint: one line on");
    CHECK(pawprint_step(&PW) == ZXM_OK && pawprint_step(&PW) == ZXM_OK && PW.at_step == 1 &&
              PW.at_line == 1,
          "pawprint: into the next step's file");
    CHECK(pawprint_step(&PW) == ZXM_OK && PW.at_line == 2 && PW.brk[1].hits == 1 && !PW.stopped &&
              strstr(PW.notice, "Step share refused at aux.kron line 2 column 4"),
          "pawprint: at the refusal, its breakpoint hit once but its condition not passed: %s",
          PW.notice);
    CHECK(pawprint_stack(&PW, fr, 12, &n) == ZXM_OK && n == 2 && !strcmp(fr[0].step, "share") &&
              fr[0].line == 2 && !strcmp(fr[1].step, "load") && fr[1].line == 5 &&
              pawprint_stack(&PW, fr, 1, &n) == ZXM_E_TOOBIG,
          "pawprint: two frames, newest first");
    CHECK(pawprint_continue(&PW) == ZXM_OK && PW.ended && PW.instructions == 7 && PW.nmoves == 6 &&
              strstr(PW.notice, "Replay ended: the run refused"),
          "pawprint: the replay ends with the run's verdict: %s", PW.notice);
    CHECK(pawprint_step(&PW) == ZXM_E_STATE && pawprint_continue(&PW) == ZXM_E_STATE &&
              pawprint_stack(&PW, fr, 12, &n) == ZXM_OK && n == 3 &&
              !strcmp(fr[0].step, "settle") && fr[0].line == 0,
          "pawprint: nothing past the end; every step a frame");
    CHECK(pawprint_stepped_insns(PW.moves, PW.nmoves, &total) && total == PW.instructions,
          "pawprint: the instructions are the moves' exact sum");
    for (uint32_t i = 0; i < PW.nbreaks; i++)
        CHECK(
            pawprint_record_ok(PW.brk[i].file, PW.brk[i].line, PW.brk[i].condition, PW.brk[i].hits),
            "pawprint: breakpoint %u fits its record", i);

    /* a run again replays from the start, the hits counted again */
    CHECK(pawprint_run(&PW) == ZXM_OK && PW.brk[1].hits == 0 && PW.instructions == 0 && !PW.ended &&
              pawprint_continue(&PW) == ZXM_OK && PW.at_line == 3 &&
              pawprint_continue(&PW) == ZXM_OK && PW.ended && PW.instructions == 7,
          "pawprint: a second run, continued twice to its end");

    /* replies out of bounds leave the last run as it was */
    PW_PLAY = "{\"template\":\"shares\",\"verdict\":\"MAYBE\",\"headline\":\"x\",\"steps\":[]}";
    CHECK(pawprint_run(&PW) == ZXM_E_PROTOCOL && PW.nsteps == 3 && PW.ended,
          "pawprint: an unknown verdict");
    PW_PLAY =
        "{\"template\":\"shares\",\"verdict\":\"FALSE\",\"headline\":\"x\",\"steps\":[" PW_STEP(
            "share", "kron", "aux.kron", "true", "FALSE", PW_ERR("3", "1")) "]}";
    CHECK(pawprint_run(&PW) == ZXM_E_PROTOCOL && PW.nsteps == 3,
          "pawprint: a refusal past the opened file's last line");
    PW_PLAY = PW_RUN13;
    CHECK(pawprint_run(&PW) == ZXM_E_PROTOCOL && PW.nsteps == 3,
          "pawprint: more steps than the Playground has");

    /* changing a file ends the run and the attach; breakpoints past its end go */
    CHECK(pawprint_file(&PW, "aux.kron", "x:1\n") == ZXM_OK && !PW.attached && !PW.have_run &&
              PW.nbreaks == 1 && pawprint_run(&PW) == ZXM_E_STATE,
          "pawprint: a changed file must be approved again");

    /* PR4 through the model: 256 breakpoints, not 257 */
    {
        static char lines[2 * 300 + 1];
        for (uint32_t i = 0; i < 300; i++) lines[2 * i] = 'x', lines[2 * i + 1] = '\n';
        CHECK(pawprint_file(&PW, "long.sutra", lines) == ZXM_OK && PW.file[2].lines == 300,
              "pawprint: a file of 300 lines");
        int rc = ZXM_OK;
        for (int64_t line = 1; PW.nbreaks < (uint32_t) PAW_BREAK_MAX && rc == ZXM_OK; line++)
            rc = pawprint_break(&PW, 2, line, 0);
        CHECK(rc == ZXM_OK && PW.nbreaks == (uint32_t) PAW_BREAK_MAX &&
                  pawprint_break(&PW, 2, 299, 0) == ZXM_E_TOOBIG,
              "pawprint: the 257th breakpoint is past the limit");
    }

    /* PM6: events, live from the real zxvd */
    zxvd_log(ZXVD_LOG_WARN, "play", "pawprint a");
    zxvd_log(ZXVD_LOG_INFO, "play", "pawprint b");
    CHECK(pawprint_follow(&PW, &added) == ZXM_OK && added >= 2 && PW.nev >= 2 &&
              !strcmp(pawprint_event(&PW, PW.nev - 1u)->text, "pawprint b") &&
              pawprint_event(&PW, PW.nev - 2u)->level == 1 &&
              pawprint_event(&PW, PAWPRINT_EVENTS) == NULL,
          "pawprint: the live events, the newest last: %s", PW_LINK.err);
    zxvd_log(ZXVD_LOG_ERROR, "play", "pawprint c");
    CHECK(pawprint_follow(&PW, &added) == ZXM_OK && added == 1 &&
              !strcmp(pawprint_event(&PW, PW.nev - 1u)->text, "pawprint c") &&
              pawprint_event(&PW, PW.nev - 1u)->level == 2,
          "pawprint: read on from the last event");
    CHECK(pawprint_history(&PW, "pawprint") == ZXM_OK && !PW.journal_on && PW.nhist == 0 &&
              strstr(PW.notice, "no journal"),
          "pawprint: no journal: nothing kept, and it says so: %s", PW_LINK.err);
    {
        char jdir[256];
        snprintf(jdir, sizeof jdir, "%s/zxv-paw-XXXXXX",
                 getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
        CHECK(mkdtemp(jdir) != NULL && zjr_open(jdir) == 0, "pawprint: a journal");
        for (int i = 0; i < 40; i++) zxvd_log(ZXVD_LOG_INFO, "play", "pawprint run %d", i);
        CHECK(pawprint_history(&PW, "pawprint run") == ZXM_OK && PW.journal_on &&
                  PW.nhist == PAWPRINT_PAGE && PW.more &&
                  !strcmp(PW.hist[0].text, "pawprint run 39") && PW.hist[0].seq > PW.hist[1].seq,
              "pawprint: the journal, newest first: %s", PW_LINK.err);
        zjr_close();
        CHECK(rm_tree(jdir) == 0, "pawprint: journal cleanup");
    }
}
