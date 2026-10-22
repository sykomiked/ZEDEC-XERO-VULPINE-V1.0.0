/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Talewind's model against zxvd's real dispatcher (LINK): a story is a file
 * in the Banker (files.read, files.write), and a drafted passage comes from
 * swarm.ask and is held until the author accepts it. */
#include "talewind_model.h"

static talewind_t TALE, TALE2;

static bool tw_file_is(const char *path, const char *want)
{
    static uint8_t buf[TALEWIND_FILE_MAX];
    uint32_t got = 0;
    uint64_t size = 0;
    if (zxf_read(path, 0, buf, sizeof buf - 1u, true, &got, &size) != 0) return false;
    buf[got] = 0;
    return size == got && !strcmp((const char *) buf, want);
}

static bool tw_file_has(const char *path, const char *part)
{
    static uint8_t buf[TALEWIND_FILE_MAX];
    uint32_t got = 0;
    uint64_t size = 0;
    if (zxf_read(path, 0, buf, sizeof buf - 1u, true, &got, &size) != 0) return false;
    buf[got] = 0;
    return strstr((const char *) buf, part) != NULL;
}

static bool tw_shown_are(const talewind_t *t, uint32_t n, uint32_t first, uint32_t second)
{
    uint32_t out[TALEWIND_CHOICES];
    uint32_t got = talewind_shown_list(t, out);
    if (got != n) return false;
    return (n < 1 || out[0] == first) && (n < 2 || out[1] == second);
}

APPS_TEST(talewind)
{
    const int64_t NOW = 1760000000;
    static const char *const PATH = "lantern.tale";
    static const char *const STORY = "talewind 1 42\n"
                                     "p 1 0 You stand at the lighthouse door.\n"
                                     "p 2 0 The stair winds up into the dark.\n"
                                     "p 3 3 The keeper's log tells of the wreck.\n"
                                     "p 4 0 The lamp room glows.\n"
                                     "c 1 1 2 0 1 Climb the stair\n"
                                     "c 1 2 3 0 0 Read the keeper's log\n"
                                     "c 2 1 4 1 2 Light the lamp\n"
                                     "c 2 2 4 6 0 Use the brass key\n";

    /* the rules the model runs */
    CHECK(talewind_choice_shown(0, 0) && talewind_choice_shown(0, 4) &&
              !talewind_choice_shown(0, -1) && !talewind_choice_shown(1, 4) &&
              !talewind_choice_shown(0, 5),
          "talewind: choice shown (TR1)");
    CHECK(talewind_flags_lacking(6, 2) == 1 && talewind_flags_after(1, 2) == 3 &&
              talewind_flags_lacking(65536, 0) == TALEWIND_REFUSED,
          "talewind: flags (TR2, TR3)");

    /* nothing is written before the story is opened, or without the author's word */
    CHECK(zxf_use_space(NULL) == 0, "home");
    talewind_init(&TALE, &LINK);
    int before = sent;
    CHECK(talewind_set_passage(&TALE, 1, 0, "x", true) == ZXM_E_STATE && sent == before,
          "not opened: nothing written");
    CHECK(talewind_open(&TALE, "", 1) == ZXM_E_ARG && talewind_open(&TALE, PATH, 0) == ZXM_E_ARG &&
              talewind_open(&TALE, PATH, 1000000) == ZXM_E_ARG && sent == before,
          "no path or no story number: nothing asked");
    CHECK(talewind_open(&TALE, PATH, 42) == ZXM_OK && TALE.opened && TALE.story == 42 &&
              !TALE.passage[1].present && TALE.version == 0,
          "a story not made yet is empty: %s", LINK.err);
    before = sent;
    CHECK(talewind_set_passage(&TALE, 1, 0, "You stand at the lighthouse door.", false) ==
                  ZXM_E_STATE &&
              sent == before && !TALE.passage[1].present && TALE.version == 0,
          "TM2: without the author's word nothing is sent and nothing changes");
    CHECK(talewind_set_passage(&TALE, 0, 0, "x", true) == ZXM_E_ARG &&
              talewind_set_passage(&TALE, 65, 0, "x", true) == ZXM_E_ARG &&
              talewind_set_passage(&TALE, 1, 5, "x", true) == ZXM_E_ARG &&
              talewind_set_passage(&TALE, 1, -1, "x", true) == ZXM_E_ARG &&
              talewind_set_passage(&TALE, 1, 0, "", true) == ZXM_E_ARG &&
              talewind_set_passage(&TALE, 1, 0, "two\nlines", true) == ZXM_E_ARG &&
              talewind_set_passage(&TALE, 1, 0, NULL, true) == ZXM_E_ARG && sent == before,
          "bad passages are refused before anything is asked");

    /* the story graph, written whole each time (TM1, TM2) */
    CHECK(talewind_set_passage(&TALE, 1, 0, "You stand at the lighthouse door.", true) == ZXM_OK &&
              talewind_set_passage(&TALE, 2, 0, "The stair winds up into the dark.", true) ==
                  ZXM_OK &&
              talewind_set_passage(&TALE, 3, 3, "The keeper's log tells of the wreck.", true) ==
                  ZXM_OK &&
              talewind_set_passage(&TALE, 4, 0, "The lamp room glows.", true) == ZXM_OK &&
              TALE.version == 4,
          "passages: %s", LINK.err);
    before = sent;
    CHECK(talewind_add_choice(&TALE, 1, 5, 0, 0, "Nowhere", true) == ZXM_E_ARG &&
              talewind_add_choice(&TALE, 1, 2, 65536, 0, "x", true) == ZXM_E_ARG &&
              talewind_add_choice(&TALE, 1, 2, 0, -1, "x", true) == ZXM_E_ARG &&
              talewind_add_choice(&TALE, 1, 2, 0, 0, "", true) == ZXM_E_ARG &&
              talewind_add_choice(&TALE, 1, 2, 0, 1, "Climb the stair", false) == ZXM_E_STATE &&
              sent == before && TALE.passage[1].nchoices == 0,
          "bad choices, and a choice without the author's word: nothing sent");
    CHECK(talewind_add_choice(&TALE, 1, 2, 0, 1, "Climb the stair", true) == ZXM_OK &&
              talewind_add_choice(&TALE, 1, 3, 0, 0, "Read the keeper's log", true) == ZXM_OK &&
              talewind_add_choice(&TALE, 2, 4, 1, 2, "Light the lamp", true) == ZXM_OK &&
              talewind_add_choice(&TALE, 2, 4, 6, 0, "Use the brass key", true) == ZXM_OK &&
              TALE.version == 8,
          "choices: %s", LINK.err);
    CHECK(tw_file_is(PATH, STORY), "the story in the Banker (TM1)");
    for (int k = 0; k < 2; k++)
        CHECK(talewind_add_choice(&TALE, 4, 1, 0, 0, "Again", true) == ZXM_OK, "room: %s",
              LINK.err);
    CHECK(talewind_add_choice(&TALE, 4, 2, 0, 0, "Up", true) == ZXM_OK &&
              talewind_add_choice(&TALE, 4, 3, 0, 0, "Log", true) == ZXM_OK,
          "four: %s", LINK.err);
    before = sent;
    CHECK(talewind_add_choice(&TALE, 4, 1, 0, 0, "Fifth", true) == ZXM_E_TOOBIG && sent == before &&
              TALE.passage[4].nchoices == TALEWIND_CHOICES,
          "a fifth choice has no room: nothing sent");

    /* opened again, the same graph */
    talewind_init(&TALE2, &LINK);
    CHECK(talewind_open(&TALE2, PATH, 7) == ZXM_OK && TALE2.story == 42 &&
              TALE2.passage[3].rating == 3 && TALE2.passage[2].nchoices == 2 &&
              TALE2.passage[2].choice[1].need == 6 && TALE2.passage[4].nchoices == 4 &&
              !strcmp(TALE2.passage[1].choice[1].label, "Read the keeper's log") &&
              !strcmp(TALE2.passage[3].text, "The keeper's log tells of the wreck."),
          "reopened: %s", LINK.err);

    /* reading (TM4): flags and the age rating ceiling decide what is shown */
    CHECK(talewind_read_begin(&TALE2, 5) == ZXM_E_ARG && talewind_choose(&TALE2, 0) == ZXM_E_STATE,
          "no ceiling past adults only; nothing to take before reading");
    CHECK(talewind_read_begin(&TALE2, 1) == ZXM_OK && TALE2.at == 1 && TALE2.held == 0 &&
              tw_shown_are(&TALE2, 1, 0, 0) && !talewind_shown(&TALE2, 1),
          "a seven-and-up reader: the log rated sixteen and up is not shown");
    CHECK(talewind_choose(&TALE2, 1) == ZXM_E_STATE && TALE2.at == 1,
          "a hidden choice is not taken");
    CHECK(talewind_choose(&TALE2, 0) == ZXM_OK && TALE2.at == 2 && TALE2.held == 1 &&
              tw_shown_are(&TALE2, 1, 0, 0),
          "climbed: flag 1 held; the brass key needs flags 2 and 3");
    CHECK(talewind_choose(&TALE2, 0) == ZXM_OK && TALE2.at == 4 && TALE2.held == 3,
          "lit: flags 1, 2");
    CHECK(talewind_read_begin(&TALE2, 4) == ZXM_OK && tw_shown_are(&TALE2, 2, 0, 1),
          "an adult reader sees both choices");

    /* a story that is no story is never overwritten (TM1) */
    uint64_t wrote = 0;
    CHECK(zxf_write("broken.tale", (const uint8_t *) "talewind 1 42\nc 1 1 2 0 0 x\n", 28,
                    ZXF_W_REPLACE, false, &wrote) == 0 &&
              wrote == 28,
          "broken story");
    talewind_init(&TALE2, &LINK);
    CHECK(talewind_open(&TALE2, "broken.tale", 1) == ZXM_E_PROTOCOL && !TALE2.opened &&
              talewind_set_passage(&TALE2, 1, 0, "x", true) == ZXM_E_STATE &&
              tw_file_is("broken.tale", "talewind 1 42\nc 1 1 2 0 0 x\n"),
          "a choice to no passage: not a story, not overwritten");

    /* drafts (TM3): asked, held, never written until accepted */
    ANSWER = "The storm\nbreaks over\tthe rocks.\n";
    int was = asked;
    before = sent;
    CHECK(talewind_draft(&TALE, 5, 1, "the storm arrives", NOW, 600) == ZXM_OK &&
              TALE.draft.pending && !TALE.passage[5].present && asked == was + 1 &&
              !strcmp(TALE.draft.text, "The storm breaks over the rocks.") &&
              TALE.draft.version == TALE.version && sent == before + 1 &&
              strstr(last_prompt, "age rating 1 of 0") && strstr(last_prompt, "the storm arrives"),
          "a draft is held, not written: %s", LINK.err);
    CHECK(!tw_file_has(PATH, "\np 5 "), "the draft is not in the file");
    was = asked;
    CHECK(talewind_draft(&TALE, 6, 0, "more", NOW, 600) == ZXM_E_STATE &&
              talewind_draft(&TALE, 0, 0, "x", NOW, 600) == ZXM_E_ARG &&
              talewind_draft(&TALE, 6, 5, "x", NOW, 600) == ZXM_E_ARG &&
              talewind_draft(&TALE, 6, 0, "", NOW, 600) == ZXM_E_ARG &&
              talewind_draft(&TALE, 6, 0, "x", NOW, 0) == ZXM_E_ARG &&
              talewind_draft(&TALE, 6, 0, "x", NOW, SIEVE_HOLD_MAX + 1) == ZXM_E_ARG &&
              asked == was,
          "one draft at a time; bad drafts are not asked");
    before = sent;
    int64_t v = TALE.version;
    CHECK(talewind_draft_accept(&TALE, NOW + 10, false) == ZXM_E_STATE && sent == before &&
              TALE.draft.pending && !TALE.passage[5].present && TALE.version == v,
          "TM2: an accept without the author's word sends nothing");
    CHECK(talewind_draft_accept(&TALE, NOW + 600, true) == ZXM_E_STATE && sent == before &&
              TALE.draft.pending,
          "a lapsed hold is not written");
    CHECK(talewind_draft_accept(&TALE, NOW + 10, true) == ZXM_OK && !TALE.draft.pending &&
              TALE.passage[5].present && TALE.passage[5].rating == 1 &&
              !strcmp(TALE.passage[5].text, "The storm breaks over the rocks.") &&
              TALE.version == v + 1 &&
              tw_file_has(PATH, "\np 5 1 The storm breaks over the rocks.\n"),
          "accepted: written: %s", LINK.err);

    /* a draft made before the story changed is not written over the change */
    ANSWER = "The keeper returns.";
    CHECK(talewind_draft(&TALE, 6, 0, "the keeper", NOW, 600) == ZXM_OK &&
              talewind_set_passage(&TALE, 6, 0, "Silence.", true) == ZXM_OK,
          "drafted, then edited: %s", LINK.err);
    before = sent;
    CHECK(talewind_draft_accept(&TALE, NOW + 10, true) == ZXM_E_STATE && sent == before &&
              !strcmp(TALE.passage[6].text, "Silence."),
          "drift: the draft is not written");
    CHECK(talewind_draft_reject(&TALE, false) == ZXM_E_STATE && TALE.draft.pending &&
              talewind_draft_reject(&TALE, true) == ZXM_OK && !TALE.draft.pending &&
              talewind_draft_reject(&TALE, true) == ZXM_E_STATE && sent == before,
          "rejected only on the author's word, sending nothing");
    ANSWER = " \n ";
    CHECK(talewind_draft(&TALE, 6, 0, "x", NOW, 600) == ZXM_E_PROTOCOL && !TALE.draft.pending,
          "a blank answer is nothing to hold");
    ANSWER = "ok";
}
