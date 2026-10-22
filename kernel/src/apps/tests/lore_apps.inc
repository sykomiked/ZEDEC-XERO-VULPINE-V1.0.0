/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Lore's model against zxvd's real dispatcher (LINK): files.list,
 * files.read, search.query and search.refresh in a space, spaces.get for
 * its kind, and swarm.ask (the stub) only with the owner's leave. */
#include "lore_model.h"

static lore_t LO;
static uint64_t lore_sz;

APPS_TEST(lore)
{
    /* the pure helpers */
    CHECK(lore_kind_of("home") == 0 && lore_kind_of("office") == 1 && lore_kind_of("team") == 2 &&
              lore_kind_of("cloudtop") == 3 && lore_kind_of("park") == 4 &&
              lore_kind_of("attic") == LORE_REFUSED && lore_kind_of(NULL) == LORE_REFUSED,
          "lore: the five kinds, most private first");
    CHECK(lore_depth("") == 0 && lore_depth("notes") == 1 && lore_depth("notes/2026/q3") == 3,
          "lore: a folder's depth");
    CHECK(lore_under("notes/a.md", "notes") && lore_under("notes", "notes") &&
              lore_under("x/y", "") && !lore_under("notesy/a.md", "notes") &&
              !lore_under("note", "notes") && !lore_under("other/notes/a.md", "notes"),
          "lore: a path under a folder, never a mere prefix of its name");

    /* home: two notes, one in a private folder */
    CHECK(zxf_use_space(NULL) == 0 && zxf_mkdir("lore") == 0 && zxf_mkdir("lore/notes") == 0 &&
              zxf_mkdir("lore/diary") == 0 &&
              zxf_write("lore/notes/orchard.md",
                        (const uint8_t *) "quokka orchard: prune the pears in March\n", 41,
                        ZXF_W_REPLACE, false, &lore_sz) == 0 &&
              zxf_write("lore/diary/secret.md", (const uint8_t *) "quokka diary\n", 13,
                        ZXF_W_REPLACE, false, &lore_sz) == 0,
          "lore: the home notes");

    lore_init(&LO, &LINK);
    int before = sent, why = -1;
    CHECK(LO.kind == LORE_KIND_HOME &&
              lore_may_read(&LO, "lore/notes/orchard.md", 1760000000) == LORE_NO_LEAVE,
          "lore: no leave yet");
    CHECK(lore_ask(&LO, "lore/notes/orchard.md", "When to prune?", 1760000000, &why) ==
                  ZXM_E_STATE &&
              why == LORE_NO_LEAVE && sent == before && LO.held.state == ZXM_HELD_NONE,
          "lore: no leave: nothing is sent to the companion (LO4)");
    CHECK(lore_ask(&LO, "lore/notes/orchard.md", "  ", 1760000000, NULL) == ZXM_E_ARG &&
              lore_ask(&LO, "../x", "q", 1760000000, NULL) == ZXM_E_ARG && sent == before,
          "lore: a blank question or a path out of the Hoard is refused unsent");

    /* LO2: leaves are the owner's, checked against the record first */
    uint32_t v = 99, w = 99;
    CHECK(lore_leave_give(&LO, "lore/notes", 0, 1760000000, 1762592000, &v) == ZXM_OK && v == 0 &&
              LO.leave[0].used && !strcmp(LO.leave[0].folder, "lore/notes"),
          "lore: a 30-day leave for lore/notes, reaching home");
    CHECK(lore_leave_give(&LO, "lore/notes", 5, 1760000000, 1762592000, NULL) == ZXM_E_ARG &&
              lore_leave_give(&LO, "lore/notes", 0, 1762592000, 1762592000, NULL) == ZXM_E_ARG &&
              lore_leave_give(&LO, "lore/notes", 0, -1, 1762592000, NULL) == ZXM_E_ARG &&
              lore_leave_give(&LO, "lore/../x", 0, 1760000000, 1762592000, NULL) == ZXM_E_ARG &&
              lore_leave_give(&LO, "lore//x", 0, 1760000000, 1762592000, NULL) == ZXM_E_ARG &&
              lore_leave_give(&LO,
                              "a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q/r/s/t/u/v/w/x/y/z/1/2/3/4/5/6/7",
                              0, 1760000000, 1762592000, NULL) == ZXM_E_ARG &&
              lore_leave_give(&LO, "lore/notes", 0, 1760000000, LOCKBOX_STAMP_MAX + 1, NULL) ==
                  ZXM_E_ARG &&
              !LO.leave[1].used && sent == before,
          "lore: leaves that do not fit are refused (LR4)");
    CHECK(lore_leave_days(&LO, 0, 1760000000) == 30 && lore_leave_days(&LO, 0, 1762591999) == 0 &&
              lore_leave_days(&LO, 0, 1762592001) == 0 &&
              lore_leave_days(&LO, 1, 0) == LORE_REFUSED &&
              lore_leave_days(&LO, 0, -1) == LORE_REFUSED,
          "lore: a leave's days are Lockbox's (LR2)");

    /* LO3 at home */
    CHECK(lore_may_read(&LO, "lore/notes/orchard.md", 1760000000) == LORE_READ_OK &&
              lore_may_read(&LO, "lore/diary/secret.md", 1760000000) == LORE_NOT_GRANTED &&
              lore_may_read(&LO, "lore/notes/orchard.md", 1759999999) == LORE_LAPSED &&
              lore_may_read(&LO, "lore/notes/orchard.md", 1762592000) == LORE_LAPSED &&
              lore_may_read(&LO, "", 1760000000) == LORE_NOT_GRANTED,
          "lore: granted folder, other folder, before and after the leave");

    /* the owner's own reads (LO5) */
    CHECK(lore_list(&LO, "lore") == ZXM_OK && LO.nentries == 2 && LO.entry[0].dir &&
              !strcmp(LO.entry[0].name, "diary") && !strcmp(LO.entry[1].name, "notes") &&
              !strcmp(LO.folder, "lore"),
          "lore: list lore: %s", LINK.err);
    CHECK(lore_list(&LO, "lore/notes") == ZXM_OK && LO.nentries == 1 && !LO.entry[0].dir &&
              LO.entry[0].size == 41,
          "lore: list lore/notes: %s", LINK.err);
    CHECK(lore_list(&LO, "lore/..") == ZXM_E_ARG, "lore: a listing out of the Hoard is refused");
    CHECK(lore_open(&LO, "lore/diary/secret.md") == ZXM_OK && LO.text_len == 13 &&
              !strcmp(LO.text, "quokka diary\n") && !LO.text_cut,
          "lore: the owner opens a file no leave covers: %s", LINK.err);
    CHECK(lore_open(&LO, "lore/none.md") == ZXM_E_REMOTE && LO.text_len == 13,
          "lore: a missing file keeps what was open");
    uint32_t files = 0;
    CHECK(lore_reindex(&LO, &files) == ZXM_OK && files >= 2, "lore: reindex: %s", LINK.err);
    CHECK(lore_search(&LO, "quokka", 1760000000) == ZXM_OK && LO.nhits == 2 && LO.total == 2,
          "lore: search: %s", LINK.err);
    {
        int got = 0;
        for (uint32_t i = 0; i < LO.nhits; i++) {
            if (!strcmp(LO.hit[i].path, "lore/notes/orchard.md") && LO.hit[i].ai_ok) got |= 1;
            if (!strcmp(LO.hit[i].path, "lore/diary/secret.md") && !LO.hit[i].ai_ok) got |= 2;
        }
        CHECK(got == 3, "lore: each hit says whether the companion may read it");
    }
    CHECK(lore_search(&LO, " ", 1760000000) == ZXM_OK && LO.nhits == 0, "lore: a blank search");

    /* LO4: asked with leave, the answer held */
    ANSWER = "Prune the pears in March.";
    int asked0 = asked;
    CHECK(lore_ask(&LO, "lore/notes/orchard.md", "When to prune?", 1760000000, &why) == ZXM_OK &&
              why == LORE_READ_OK && asked == asked0 + 1 &&
              strstr(last_prompt, "Question: When to prune?") &&
              strstr(last_prompt, "File: lore/notes/orchard.md") &&
              strstr(last_prompt, "prune the pears in March") &&
              LO.held.state == ZXM_HELD_PENDING && !strcmp(LO.held.text, ANSWER) &&
              !strcmp(LO.asked_path, "lore/notes/orchard.md"),
          "lore: the companion reads the granted file: %s", LINK.err);
    CHECK(lore_ai_accept(&LO) == ZXM_OK && LO.held.state == ZXM_HELD_ACCEPTED &&
              lore_ai_accept(&LO) == ZXM_E_STATE,
          "lore: the owner accepts the answer once");
    before = sent;
    CHECK(lore_ask(&LO, "lore/diary/secret.md", "What is here?", 1760000000, &why) == ZXM_E_STATE &&
              why == LORE_NOT_GRANTED && sent == before && asked == asked0 + 1 &&
              !strstr(last_prompt, "quokka diary"),
          "lore: the diary is never sent");

    /* an office, more open than home: the home leave reaches it; an
     * office leave does not reach home */
    CHECK(raw_call("spaces.create", "{\"kind\":\"office\",\"name\":\"Lore Desk\"}") == ZXM_OK &&
              raw_call("files.mkdir", "{\"space\":\"lore-desk\",\"path\":\"lore\"}") == 0 &&
              raw_call("files.mkdir", "{\"space\":\"lore-desk\",\"path\":\"lore/notes\"}") == 0 &&
              raw_call("files.write", "{\"space\":\"lore-desk\",\"path\":\"lore/notes/"
                                      "plan.md\",\"text\":\"quokka plan for the office\"}") == 0,
          "lore: the office and its note: %s", LINK.err);
    CHECK(lore_set_space(&LO, "lore-desk") == ZXM_OK && LO.kind == 1 &&
              !strcmp(LO.space, "lore-desk") && LO.nentries == 0 && LO.text_len == 0,
          "lore: in the office (LO1): %s", LINK.err);
    CHECK(lore_may_read(&LO, "lore/notes/plan.md", 1760000000) == LORE_READ_OK,
          "lore: a leave reaching home reaches the office too");
    CHECK(lore_open(&LO, "lore/notes/plan.md") == ZXM_OK &&
              !strcmp(LO.text, "quokka plan for the office"),
          "lore: the office note opens in its own space: %s", LINK.err);
    CHECK(lore_leave_revoke(&LO, 0) == ZXM_OK && lore_leave_revoke(&LO, 0) == ZXM_E_STATE &&
              lore_leave_revoke(&LO, 7) == ZXM_E_ARG && lore_leave_days(&LO, 0, 1760000000) == 0 &&
              lore_may_read(&LO, "lore/notes/plan.md", 1760000000) == LORE_NO_LEAVE,
          "lore: a revoked leave reads nothing");
    CHECK(lore_leave_give(&LO, "lore", 2, 1760000000, 1762592000, &w) == ZXM_OK && w == 1 &&
              lore_may_read(&LO, "lore/notes/plan.md", 1760000000) == LORE_TOO_PRIVATE,
          "lore: a leave reaching only team and beyond does not reach the office");
    before = sent;
    CHECK(lore_ask(&LO, "lore/notes/plan.md", "What is the plan?", 1760000000, &why) ==
                  ZXM_E_STATE &&
              why == LORE_TOO_PRIVATE && sent == before,
          "lore: from a more private space: nothing sent");
    CHECK(lore_leave_give(&LO, "lore/notes", 1, 1760000000, 1762592000, NULL) == ZXM_OK &&
              lore_may_read(&LO, "lore/notes/plan.md", 1760000000) == LORE_READ_OK,
          "lore: an office leave reaches the office");
    CHECK(lore_set_space(&LO, "home") == ZXM_OK && LO.kind == 0 && !LO.space[0] &&
              lore_may_read(&LO, "lore/notes/orchard.md", 1760000000) == LORE_TOO_PRIVATE,
          "lore: back home, the office leave does not reach it");
    CHECK(lore_set_space(&LO, "no-such-space") == ZXM_E_REMOTE && LO.kind == 0,
          "lore: an unknown space keeps the one in use");
    CHECK(
        !strcmp(lore_why(LORE_TOO_PRIVATE), "this space is more private than your leave reaches") &&
            !strcmp(lore_why(99), "unknown"),
        "lore: the reasons in words");
    ANSWER = "ok";
}
