/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Bowline's model against zxvd's real dispatcher (LINK): each task is a
 * file in the Hoard's Bowline folder, listed with files.list, read with
 * files.read and written with files.write; the notices are read with
 * notes.list through host ops that hold the notes below. */
#include "bowline_model.h"

static bowline_t BOW, BOW2;
static const char *bow_notes_json = "{\"unread\":0,\"notes\":[]}";

static void bow_notes(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, bow_notes_json, strlen(bow_notes_json));
}

static bool bow_file_is(const char *path, const char *want)
{
    static uint8_t buf[BOWLINE_TEXT_MAX + 1];
    uint32_t got = 0;
    uint64_t size = 0;
    if (zxf_read(path, 0, buf, BOWLINE_TEXT_MAX, true, &got, &size) != 0) return false;
    buf[got] = 0;
    return size == got && !strcmp((const char *) buf, want);
}

static bool bow_put(const char *path, const char *text)
{
    uint64_t size;
    return zxf_write(path, (const uint8_t *) text, (uint32_t) strlen(text), ZXF_W_REPLACE, false,
                     &size) == 0;
}

/* the same tasks, field by field (a struct's padding is not compared) */
static bool bow_same_tasks(const bowline_t *a, const bowline_t *b)
{
    if (a->n != b->n) return false;
    for (uint32_t i = 0; i < a->n; i++) {
        const bowline_task_t *x = &a->t[i], *y = &b->t[i];
        if (x->id != y->id || x->list != y->list || x->start != y->start || x->due != y->due ||
            x->every != y->every || x->unit != y->unit || x->skip != y->skip ||
            x->done != y->done || strcmp(x->title, y->title))
            return false;
    }
    return true;
}

#define BOW_NOTE(seq, read, src)                                                                   \
    "{\"seq\":" #seq ",\"kind\":\"note\",\"pri\":1,\"read\":" #read                                \
    ",\"count\":1,\"source\":\"" src "\",\"title\":\"t\",\"body\":\"\",\"action\":\"\"}"

/* 2026-10-19, a Monday, is day 20745 */
#define BOW_MON 20745

APPS_TEST(bowline)
{
    /* the rules the model keeps (BM3) */
    CHECK(
        bowline_task_valid(1, 0, 0, 0, 1, 0, 0) && bowline_task_valid(1, 0, 0, 0, 1, 0, 1) &&
            bowline_task_valid(BOWLINE_LIST_MAX, SOLSTICE_DAY_MAX, SOLSTICE_DAY_MAX,
                               BOWLINE_EVERY_MAX, 7, BOWLINE_SKIP_MAX, 0) &&
            !bowline_task_valid(0, 0, 0, 0, 1, 0, 0) &&
            !bowline_task_valid(BOWLINE_LIST_MAX + 1, 0, 0, 0, 1, 0, 0) &&
            !bowline_task_valid(1, -1, 0, 0, 1, 0, 0) && !bowline_task_valid(1, 5, 4, 0, 1, 0, 0) &&
            !bowline_task_valid(1, 0, SOLSTICE_DAY_MAX + 1, 0, 1, 0, 0) &&
            !bowline_task_valid(1, 0, 0, -1, 1, 0, 0) &&
            !bowline_task_valid(1, 0, 0, BOWLINE_EVERY_MAX + 1, 1, 0, 0) &&
            !bowline_task_valid(1, 0, 0, 1, 2, 0, 0) && !bowline_task_valid(1, 0, 0, 1, 0, 0, 0) &&
            !bowline_task_valid(1, 0, 0, 0, 1, 127, 0) &&
            !bowline_task_valid(1, 0, 0, 0, 1, -1, 0) && !bowline_task_valid(1, 0, 0, 0, 1, 0, 2) &&
            !bowline_task_valid(1, 0, 0, 1, 1, 0, 1),
        "task: valid at its edges, refused past them; a repeating task is never done");
    static char long_title[BOWLINE_TITLE_MAX + 2];
    memset(long_title, 'a', BOWLINE_TITLE_MAX);
    CHECK(bowline_title_ok("Water plants") && bowline_title_ok(long_title) &&
              bowline_title_ok("Caf\xc3\xa9") && !bowline_title_ok("") && !bowline_title_ok(NULL) &&
              !bowline_title_ok("two\nlines"),
          "title: 1 to 120 bytes, no control byte");
    long_title[BOWLINE_TITLE_MAX] = 'a';
    CHECK(!bowline_title_ok(long_title), "title: 121 bytes refused");

    /* when a task is next due (BM4) and whether it is overdue (BW2) */
    bowline_task_t wk = {1, 1, BOW_MON + 5, BOW_MON + 7, 1, 7, 96, 0, "Water plants"};
    bowline_task_t day = {2, 1, BOW_MON + 4, BOW_MON + 4, 1, 1, 96, 0, "Stand-up notes"};
    bowline_task_t once = {3, 2, BOW_MON, BOW_MON, 0, 1, 0, 0, "Pay rent"};
    int64_t at = 0;
    CHECK(bowline_next_due(&wk, BOW_MON + 7, &at) == ZXM_OK && at == BOW_MON + 14,
          "weekly from a Saturday, weekends skipped: Monday, then the Monday after: %lld",
          (long long) at);
    CHECK(bowline_next_due(&wk, BOW_MON + 2, &at) == ZXM_OK && at == BOW_MON + 14 &&
              bowline_next_due(&wk, BOW_MON + 20, &at) == ZXM_OK && at == BOW_MON + 28,
          "done early: the repeat after its due day; done late: the repeat after today");
    CHECK(bowline_next_due(&day, BOW_MON + 4, &at) == ZXM_OK && at == BOW_MON + 7 &&
              bowline_next_due(&day, BOW_MON + 7, &at) == ZXM_OK && at == BOW_MON + 8,
          "daily on weekdays: Friday to Monday, Monday to Tuesday");
    {
        bowline_task_t last = {4, 1, SOLSTICE_DAY_MAX - 1, SOLSTICE_DAY_MAX - 1, 1, 7, 0, 0, "x"};
        CHECK(bowline_next_due(&once, BOW_MON, &at) == ZXM_E_STATE &&
                  bowline_next_due(&last, SOLSTICE_DAY_MAX - 1, &at) == ZXM_E_STATE &&
                  bowline_next_due(&day, -1, &at) == ZXM_E_ARG &&
                  bowline_next_due(&day, BOW_MON, NULL) == ZXM_E_ARG,
              "once has no next day; none past 9999-12-31");
    }
    CHECK(bowline_is_overdue(&once, BOW_MON + 1) && !bowline_is_overdue(&once, BOW_MON) &&
              !bowline_is_overdue(&once, BOW_MON - 1) && !bowline_is_overdue(&once, -1),
          "overdue from the day after it was due");
    once.done = 1;
    CHECK(!bowline_is_overdue(&once, BOW_MON + 30), "a task done is never overdue");

    /* nothing is written before the tasks are read */
    CHECK(zxf_use_space(NULL) == 0, "home");
    bowline_init(&BOW, &LINK);
    int64_t id = 0;
    int before = sent;
    CHECK(bowline_add(&BOW, 1, BOW_MON + 5, 1, 7, 96, "Water plants", &id) == ZXM_E_STATE &&
              sent == before && !BOW.loaded,
          "not loaded: nothing written");
    CHECK(bowline_add(&BOW, 0, BOW_MON, 0, 1, 0, "x", &id) == ZXM_E_ARG &&
              bowline_add(&BOW, 1, BOW_MON, 0, 1, 127, "x", &id) == ZXM_E_ARG &&
              bowline_add(&BOW, 1, SOLSTICE_DAY_MAX, 0, 1, 16, "x", &id) == ZXM_E_ARG &&
              bowline_add(&BOW, 1, BOW_MON, 0, 1, 0, "", &id) == ZXM_E_ARG &&
              bowline_add(&BOW, 1, BOW_MON, 0, 1, 0, "x", NULL) == ZXM_E_ARG &&
              bowline_complete(&BOW, 0, BOW_MON) == ZXM_E_STATE && sent == before,
          "bad tasks are refused before anything is asked");

    /* no folder yet is no tasks; the first task makes it */
    CHECK(bowline_load(&BOW) == ZXM_OK && BOW.loaded && !BOW.folder && BOW.n == 0,
          "no folder yet: %s", LINK.err);
    CHECK(bowline_add(&BOW, 1, BOW_MON + 5, 1, 7, 96, "Water plants", &id) == ZXM_OK && id == 1 &&
              BOW.folder && BOW.t[0].due == BOW_MON + 7,
          "add a weekly task from a Saturday, weekends skipped: due Monday: %s", LINK.err);
    CHECK(bowline_add(&BOW, 2, BOW_MON, 0, 1, 0, "Pay rent", &id) == ZXM_OK && id == 2 &&
              bowline_add(&BOW, 1, BOW_MON + 4, 1, 1, 96, "Stand-up notes", &id) == ZXM_OK &&
              id == 3 && BOW.n == 3,
          "add a single task and a daily one: %s", LINK.err);
    CHECK(bow_file_is("Bowline/1.task", "bowline 1\n1 20750 20752 1 7 96 0\nWater plants\n") &&
              bow_file_is("Bowline/2.task", "bowline 1\n2 20745 20745 0 1 0 0\nPay rent\n") &&
              bow_file_is("Bowline/3.task", "bowline 1\n1 20749 20749 1 1 96 0\nStand-up notes\n"),
          "each task is its own file in the Hoard (BM1, BM2)");

    /* what the list says */
    uint32_t n = 9;
    CHECK(bowline_list_count(&BOW, 1, &n) == ZXM_OK && n == 2 &&
              bowline_list_count(&BOW, 2, &n) == ZXM_OK && n == 1 &&
              bowline_list_count(&BOW, 3, &n) == ZXM_OK && n == 0 &&
              bowline_list_count(&BOW, 0, &n) == ZXM_E_ARG,
          "two tasks in list 1, one in list 2, none in list 3");
    CHECK(bowline_overdue_count(&BOW, BOW_MON + 5, &n) == ZXM_OK && n == 2 &&
              bowline_overdue_count(&BOW, BOW_MON, &n) == ZXM_OK && n == 0 &&
              bowline_overdue_count(&BOW, -1, &n) == ZXM_E_ARG,
          "on Saturday the rent and the notes are overdue");
    CHECK(bowline_reminders(&BOW, BOW_MON + 4, &n) == ZXM_OK && n == 2 &&
              !strcmp(BOW.notice, "Pay rent: overdue since Monday 2026-10-19\n"
                                  "Stand-up notes: due Friday 2026-10-23\n"),
          "reminders on Friday: %s", BOW.notice);
    CHECK(bowline_reminders(&BOW, BOW_MON - 1, &n) == ZXM_OK && n == 0 && !BOW.notice[0] &&
              bowline_reminders(&BOW, SOLSTICE_DAY_MAX + 1, &n) == ZXM_E_ARG,
          "nothing due the day before");

    /* completing: a single task is done, a repeating one moves on (BM4) */
    CHECK(bowline_complete(&BOW, 1, BOW_MON + 5) == ZXM_OK && BOW.t[1].done == 1 &&
              bow_file_is("Bowline/2.task", "bowline 1\n2 20745 20745 0 1 0 1\nPay rent\n") &&
              bowline_complete(&BOW, 1, BOW_MON + 5) == ZXM_E_STATE &&
              bowline_list_count(&BOW, 2, &n) == ZXM_OK && n == 0,
          "the rent paid: done, and out of its list's count: %s", LINK.err);
    CHECK(
        bowline_complete(&BOW, 2, BOW_MON + 5) == ZXM_OK && BOW.t[2].due == BOW_MON + 7 &&
            BOW.t[2].done == 0 &&
            bow_file_is("Bowline/3.task", "bowline 1\n1 20749 20752 1 1 96 0\nStand-up notes\n") &&
            bowline_complete(&BOW, 3, BOW_MON) == ZXM_E_ARG,
        "the notes done on Saturday: due again on Monday: %s", LINK.err);

    /* a second model reads them all back; files not Bowline's are left alone */
    CHECK(bow_put("Bowline/7.task", "bowline 1\n1 9 8 0 1 0 0\nBackwards\n") &&
              bow_put("Bowline/readme.txt", "not a task\n") &&
              bow_put("Bowline/012.task", "bowline 1\n1 0 0 0 1 0 0\nLeading zero\n"),
          "files that are not tasks");
    bowline_init(&BOW2, &LINK);
    CHECK(bowline_load(&BOW2) == ZXM_OK && BOW2.folder && BOW2.n == 3 && BOW2.skipped == 1 &&
              BOW2.last_id == 7 && bow_same_tasks(&BOW2, &BOW),
          "read back: %u tasks, %u skipped, last %lld %s", BOW2.n, BOW2.skipped,
          (long long) BOW2.last_id, LINK.err);
    CHECK(bowline_add(&BOW2, 3, BOW_MON, 0, 1, 0, "Call home", &id) == ZXM_OK && id == 8 &&
              bow_file_is("Bowline/7.task", "bowline 1\n1 9 8 0 1 0 0\nBackwards\n") &&
              bow_file_is("Bowline/readme.txt", "not a task\n"),
          "a new number past every file's; the others untouched (BM5)");

    /* a number taken meanwhile is never written over (BM5, BM6) */
    CHECK(bow_put("Bowline/9.task", "bowline 1\n1 0 0 0 1 0 0\nSomeone else's\n"), "taken");
    n = BOW2.n;
    CHECK(bowline_add(&BOW2, 1, 0, 0, 1, 0, "Mine", &id) == ZXM_E_REMOTE &&
              LINK.err_code == BOWLINE_E_FILES && BOW2.n == n && BOW2.last_id == 8 &&
              bow_file_is("Bowline/9.task", "bowline 1\n1 0 0 0 1 0 0\nSomeone else's\n"),
          "exclusive: refused, nothing kept: %s", LINK.err);

    /* the notices: how many are unread, and how many of them are Bowline's */
    static zxvd_host_ops_t ops;
    ops = OPS;
    ops.notes = bow_notes;
    zxvd_rpc_set_ops(&ops);
    bow_notes_json = "{\"unread\":2,\"notes\":[" BOW_NOTE(3, false, "bowline") "," BOW_NOTE(
        2, true, "bowline") "," BOW_NOTE(1, false, "solstice") "]}";
    CHECK(bowline_notices(&BOW) == ZXM_OK && BOW.unread == 2 && BOW.ours == 1,
          "two unread, one of them Bowline's: %u %u %s", BOW.unread, BOW.ours, LINK.err);
    bow_notes_json = "{\"unread\":1,\"notes\":{}}";
    CHECK(bowline_notices(&BOW) == ZXM_E_PROTOCOL && BOW.unread == 2 && BOW.ours == 1,
          "notes that are not a list: refused, nothing changes");
    bow_notes_json = "{\"notes\":[]}";
    CHECK(bowline_notices(&BOW) == ZXM_E_PROTOCOL && BOW.unread == 2,
          "no unread count: refused, nothing changes");

    /* without the host's notes, zxvd answers an error and nothing changes */
    zxvd_rpc_set_ops(&OPS);
    CHECK(bowline_notices(&BOW) == ZXM_E_REMOTE && LINK.err_code == ZXVD_E_METHOD &&
              BOW.unread == 2 && BOW.ours == 1,
          "notes.list not available: %s", LINK.err);
}
