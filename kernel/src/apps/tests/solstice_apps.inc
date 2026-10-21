/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Solstice's model against zxvd's real dispatcher (LINK): each event is a
 * file in the Hoard's Solstice folder, listed with files.list, read with
 * files.read, written with files.write and put in the Trash with
 * files.trash. */
#include "solstice_model.h"

static solstice_t SOL, SOL2;

static bool sol_file_is(const char *path, const char *want)
{
    static uint8_t buf[SOLSTICE_TEXT_MAX + 1];
    uint32_t got = 0;
    uint64_t size = 0;
    if (zxf_read(path, 0, buf, SOLSTICE_TEXT_MAX, true, &got, &size) != 0) return false;
    buf[got] = 0;
    return size == got && !strcmp((const char *) buf, want);
}

static bool sol_put(const char *path, const char *text)
{
    uint64_t size;
    return zxf_write(path, (const uint8_t *) text, (uint32_t) strlen(text), ZXF_W_REPLACE, false,
                     &size) == 0;
}

/* the same events, field by field (a struct's padding is not compared) */
static bool sol_same(const solstice_t *a, const solstice_t *b)
{
    if (a->n != b->n) return false;
    for (uint32_t i = 0; i < a->n; i++) {
        const solstice_event_t *x = &a->e[i], *y = &b->e[i];
        if (x->id != y->id || x->start != y->start || x->finish != y->finish ||
            x->every != y->every || x->alarm != y->alarm || strcmp(x->title, y->title))
            return false;
    }
    return true;
}

/* 2026-10-19, a Monday, is day 20745; 2026-10-26 is day 20752 */
#define SOL_MON  INT64_C(1792368000) /* 2026-10-19 00:00 UTC */
#define SOL_WEEK INT64_C(604800)

APPS_TEST(solstice)
{
    /* the rules the model keeps (SM3) */
    CHECK(solstice_event_valid(0, 1, 0, 0) && solstice_event_valid(0, 86400, 1, 9999) &&
              solstice_event_valid(SOLSTICE_SECONDS_MAX - 1, SOLSTICE_SECONDS_MAX, SOLSTICE_DAY_MAX,
                                   0) &&
              !solstice_event_valid(5, 5, 0, 0) && !solstice_event_valid(6, 5, 0, 0) &&
              !solstice_event_valid(-1, 5, 0, 0) &&
              !solstice_event_valid(0, SOLSTICE_SECONDS_MAX + 1, 0, 0) &&
              !solstice_event_valid(0, 86401, 1, 0) && !solstice_event_valid(0, 1, -1, 0) &&
              !solstice_event_valid(0, 1, SOLSTICE_DAY_MAX + 1, 0) &&
              !solstice_event_valid(0, 1, 0, SOLSTICE_ALARM_MAX + 1) &&
              !solstice_event_valid(0, 1, 0, -1),
          "event: valid at its edges, refused past them");
    static char long_title[SOLSTICE_TITLE_MAX + 2];
    memset(long_title, 'a', SOLSTICE_TITLE_MAX);
    CHECK(solstice_title_ok("Standup") && solstice_title_ok(long_title) &&
              solstice_title_ok("Caf\xc3\xa9") && !solstice_title_ok("") &&
              !solstice_title_ok(NULL) && !solstice_title_ok("two\nlines") &&
              !solstice_title_ok("tab\there"),
          "title: 1 to 120 bytes, no control byte");
    long_title[SOLSTICE_TITLE_MAX] = 'a';
    CHECK(!solstice_title_ok(long_title), "title: 121 bytes refused");

    /* a month's page (SO1, SO2) */
    solstice_month_t mo;
    CHECK(solstice_month(2026, 10, &mo) == ZXM_OK && mo.first_weekday == 4 && mo.days == 31 &&
              mo.first_day == 20727,
          "October 2026: a Thursday first, 31 days");
    CHECK(solstice_month(2024, 2, &mo) == ZXM_OK && mo.days == 29 && mo.first_weekday == 4 &&
              solstice_month(1970, 1, &mo) == ZXM_OK && mo.first_day == 0 &&
              mo.first_weekday == 4 && solstice_month(9999, 12, &mo) == ZXM_OK &&
              mo.first_day == SOLSTICE_DAY_MAX - 30 && solstice_month(1969, 12, &mo) == ZXM_E_ARG &&
              solstice_month(10000, 1, &mo) == ZXM_E_ARG &&
              solstice_month(2026, 13, &mo) == ZXM_E_ARG &&
              solstice_month(2026, 1, NULL) == ZXM_E_ARG,
          "months 1970..9999 only");

    /* when an event next falls (SO3) and what overlaps (SO4) */
    solstice_event_t up = {1, SOL_MON + 32400, SOL_MON + 34200, 7, 15, "Standup"};
    solstice_event_t dent = {
        2, SOL_MON + SOL_WEEK + 33300, SOL_MON + SOL_WEEK + 36000, 0, 60, "Dentist"};
    solstice_event_t lunch = {3, SOL_MON + 43200, SOL_MON + 46800, 1, 0, "Lunch"};
    int64_t wed10 = SOL_MON + 2 * 86400 + 36000, at = 0;
    CHECK(solstice_next_start(&up, wed10, &at) == ZXM_OK && at == SOL_MON + SOL_WEEK + 32400,
          "the weekly standup next falls on Monday the 26th: %lld", (long long) at);
    CHECK(solstice_next_start(&up, SOL_MON + SOL_WEEK + 33000, &at) == ZXM_OK &&
              at == SOL_MON + SOL_WEEK + 32400 &&
              solstice_next_start(&up, SOL_MON + SOL_WEEK + 34200, &at) == ZXM_OK &&
              at == SOL_MON + 2 * SOL_WEEK + 32400 && solstice_next_start(&up, 0, &at) == ZXM_OK &&
              at == up.start,
          "a running occurrence counts until its finish; before the first, the first");
    CHECK(solstice_next_start(&lunch, wed10, &at) == ZXM_OK && at == SOL_MON + 2 * 86400 + 43200 &&
              solstice_next_start(&dent, wed10, &at) == ZXM_OK && at == dent.start &&
              solstice_next_start(&dent, dent.finish, &at) == ZXM_E_STATE &&
              solstice_next_start(&lunch, SOLSTICE_SECONDS_MAX, &at) == ZXM_E_STATE &&
              solstice_next_start(&lunch, -1, &at) == ZXM_E_ARG,
          "daily lunch today, the dentist once, nothing past year 9999");
    bool clash = false;
    CHECK(solstice_clash(&up, &dent, wed10, &clash) == ZXM_OK && clash &&
              solstice_clash(&dent, &up, wed10, &clash) == ZXM_OK && clash &&
              solstice_clash(&up, &lunch, wed10, &clash) == ZXM_OK && !clash &&
              solstice_clash(&dent, &lunch, SOL_MON + SOL_WEEK, &clash) == ZXM_OK && !clash &&
              solstice_clash(&dent, &dent, dent.finish, &clash) == ZXM_OK && !clash,
          "the standup and the dentist overlap on the 26th; lunch does not");

    /* nothing is written before the events are read */
    CHECK(zxf_use_space(NULL) == 0, "home");
    solstice_init(&SOL, &LINK);
    int64_t id = 0;
    int before = sent;
    CHECK(solstice_add(&SOL, up.start, up.finish, 7, 15, "Standup", &id) == ZXM_E_STATE &&
              sent == before && !SOL.loaded,
          "not loaded: nothing written");
    CHECK(solstice_add(&SOL, 5, 5, 0, 0, "x", &id) == ZXM_E_ARG &&
              solstice_add(&SOL, 0, 1, 0, 0, "", &id) == ZXM_E_ARG &&
              solstice_add(&SOL, 0, 1, 0, 0, "x", NULL) == ZXM_E_ARG &&
              solstice_remove(&SOL, 0) == ZXM_E_STATE && sent == before,
          "bad events are refused before anything is asked");

    /* no folder yet is no events; the first event makes it */
    CHECK(solstice_load(&SOL) == ZXM_OK && SOL.loaded && !SOL.folder && SOL.n == 0,
          "no folder yet: %s", LINK.err);
    CHECK(solstice_add(&SOL, up.start, up.finish, 7, 15, "Standup", &id) == ZXM_OK && id == 1 &&
              SOL.folder,
          "add the standup: %s", LINK.err);
    CHECK(solstice_add(&SOL, dent.start, dent.finish, 0, 60, "Dentist", &id) == ZXM_OK && id == 2 &&
              solstice_add(&SOL, lunch.start, lunch.finish, 1, 0, "Lunch", &id) == ZXM_OK &&
              id == 3 && SOL.n == 3,
          "add the dentist and lunch: %s", LINK.err);
    CHECK(
        sol_file_is("Solstice/1.event", "solstice 1\n1792400400 1792402200 7 15\nStandup\n") &&
            sol_file_is("Solstice/2.event", "solstice 1\n1793006100 1793008800 0 60\nDentist\n") &&
            sol_file_is("Solstice/3.event", "solstice 1\n1792411200 1792414800 1 0\nLunch\n"),
        "each event is its own file in the Hoard (SM1, SM2)");

    /* what the calendar says */
    uint32_t n = 0, idx[4];
    CHECK(solstice_clashes(&SOL, 0, wed10, &n) == ZXM_OK && n == 1 &&
              solstice_clashes(&SOL, 2, wed10, &n) == ZXM_OK && n == 0 &&
              solstice_clashes(&SOL, 3, wed10, &n) == ZXM_E_ARG,
          "the standup clashes with one event, lunch with none");
    CHECK(solstice_on_day(&SOL, 20752, idx, 4, &n) == ZXM_OK && n == 3 && idx[0] == 0 &&
              idx[1] == 1 && idx[2] == 2,
          "Monday the 26th: all three");
    CHECK(solstice_on_day(&SOL, 20747, idx, 4, &n) == ZXM_OK && n == 1 && idx[0] == 2 &&
              solstice_on_day(&SOL, 20744, idx, 4, &n) == ZXM_OK && n == 0 &&
              solstice_on_day(&SOL, 20759, idx, 1, &n) == ZXM_OK && n == 2 && idx[0] == 0 &&
              solstice_on_day(&SOL, -1, idx, 4, &n) == ZXM_E_ARG,
          "a Wednesday: lunch; before them: none; a later Monday counted past cap");
    int64_t before9 = SOL_MON + SOL_WEEK + 32400 - 600; /* 08:50 on the 26th */
    CHECK(solstice_due(&SOL, before9, &n) == ZXM_OK && n == 2 &&
              !strcmp(SOL.notice, "Standup: Monday 2026-10-26 09:00 UTC\n"
                                  "Dentist: Monday 2026-10-26 09:15 UTC\n"),
          "notices due at 08:50: %s", SOL.notice);
    CHECK(solstice_due(&SOL, wed10, &n) == ZXM_OK && n == 0 && !SOL.notice[0] &&
              solstice_due(&SOL, SOL_MON + 2 * 86400 + 43199, &n) == ZXM_OK && n == 0 &&
              solstice_due(&SOL, SOL_MON + 2 * 86400 + 43201, &n) == ZXM_OK && n == 0,
          "nothing due mid-morning, nor just before or after lunch begins");
    CHECK(solstice_due(&SOL, SOL_MON + 2 * 86400 + 43200, &n) == ZXM_OK && n == 1 &&
              !strcmp(SOL.notice, "Lunch: Wednesday 2026-10-21 12:00 UTC\n"),
          "an alarm of 0 is due at its start: %s", SOL.notice);

    /* a second model reads them all back; files not Solstice's are left alone */
    CHECK(sol_put("Solstice/7.event", "solstice 1\n5 4 0 0\nBackwards\n") &&
              sol_put("Solstice/notes.txt", "not an event\n") &&
              sol_put("Solstice/012.event", "solstice 1\n0 1 0 0\nLeading zero\n"),
          "files that are not events");
    solstice_init(&SOL2, &LINK);
    CHECK(solstice_load(&SOL2) == ZXM_OK && SOL2.folder && SOL2.n == 3 && SOL2.skipped == 1 &&
              SOL2.last_id == 7 && sol_same(&SOL2, &SOL),
          "read back: %u events, %u skipped, last %lld %s", SOL2.n, SOL2.skipped,
          (long long) SOL2.last_id, LINK.err);
    CHECK(solstice_add(&SOL2, 0, 3600, 0, 5, "Epoch", &id) == ZXM_OK && id == 8 &&
              sol_file_is("Solstice/7.event", "solstice 1\n5 4 0 0\nBackwards\n") &&
              sol_file_is("Solstice/notes.txt", "not an event\n"),
          "a new number past every file's; the others untouched (SM4)");

    /* a number taken meanwhile is never written over (SM4, SM5) */
    CHECK(sol_put("Solstice/9.event", "solstice 1\n0 60 0 0\nSomeone else's\n"), "taken");
    n = SOL2.n;
    CHECK(solstice_add(&SOL2, 0, 60, 0, 0, "Mine", &id) == ZXM_E_REMOTE &&
              LINK.err_code == SOLSTICE_E_FILES && SOL2.n == n && SOL2.last_id == 8 &&
              sol_file_is("Solstice/9.event", "solstice 1\n0 60 0 0\nSomeone else's\n"),
          "exclusive: refused, nothing kept: %s", LINK.err);

    /* removal puts the file in the Trash */
    zxf_entry_t st;
    CHECK(solstice_remove(&SOL2, 1) == ZXM_OK && SOL2.n == 3 && SOL2.e[1].id == 3 &&
              zxf_stat("Solstice/2.event", &st) != 0 && solstice_remove(&SOL2, 9) == ZXM_E_ARG,
          "the dentist in the Trash: %s", LINK.err);
    solstice_init(&SOL, &LINK);
    CHECK(solstice_load(&SOL) == ZXM_OK && SOL.n == 4 && SOL.skipped == 1 && SOL.last_id == 9 &&
              SOL.e[1].id == 3 && SOL.e[2].id == 8 && SOL.e[3].id == 9 &&
              !strcmp(SOL.e[3].title, "Someone else's"),
          "read back after: %u %s", SOL.n, LINK.err);
}
