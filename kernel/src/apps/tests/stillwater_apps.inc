/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Stillwater's model against zxvd's real dispatcher: notes.list and
 * notes.mark_read through host ops that hold the notes below, screen.* on
 * zxvd's screen time in memory. */
#include "stillwater_model.h"

static const char *SW_NOTES = "{\"unread\":0,\"notes\":[]}";
static uint32_t sw_marked;

static void sw_notes(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, SW_NOTES, strlen(SW_NOTES));
}

static uint32_t sw_read(void)
{
    return sw_marked;
}

#define SWN(seq, kind, pri, read, count, src)                                                      \
    "{\"seq\":" #seq ",\"kind\":\"" kind "\",\"pri\":" #pri ",\"read\":" #read                     \
    ",\"count\":" #count ",\"source\":\"" src "\",\"title\":\"t\",\"body\":\"\",\"action\":\"\"}"
#define SWL(unread, notes) "{\"unread\":" #unread ",\"notes\":[" notes "]}"

static const stillwater_note_t *sw_n(const stillwater_t *s, uint32_t seq)
{
    static const stillwater_note_t none;
    const stillwater_note_t *n = stillwater_note(s, seq);
    return n ? n : &none;
}

APPS_TEST(stillwater)
{
    static stillwater_t S;
    static zxvd_host_ops_t ops;
    ops = OPS;
    ops.notes = sw_notes;
    ops.notes_read = sw_read;
    zxvd_rpc_set_ops(&ops);
    CHECK(zsc_open(NULL) == 0, "stillwater: screen time in memory");
    stillwater_init(&S, &LINK);

    /* SW2: a Watchfire emergency goes through when every other input says hold */
    CHECK(stillwater_through(1, 0, -1, 1) && !stillwater_through(1, 0, -1, 0) &&
              stillwater_through(INT64_MAX, INT64_MIN, INT64_MIN, 1) &&
              stillwater_through(1, 0, 180, 0) && !stillwater_through(1, 0, 181, 0) &&
              stillwater_through(1, 1, -1, 0) && stillwater_through(0, 0, -1, 0),
          "stillwater: let through (SW2), the emergency first");

    /* ST1: a schedule is set only when it fits */
    CHECK(stillwater_set_schedule(&S, STILLWATER_QUIET, 0x1f, 1320, 420) == ZXM_OK &&
              S.sched.mode == STILLWATER_QUIET && S.sched.days == 0x1f && S.sched.from == 1320 &&
              S.sched.to == 420,
          "stillwater: weeknights 22:00 to 07:00");
    CHECK(stillwater_set_schedule(&S, 3, 0x1f, 0, 0) == ZXM_E_ARG &&
              stillwater_set_schedule(&S, 1, 128, 0, 0) == ZXM_E_ARG &&
              stillwater_set_schedule(&S, 1, 1, 1440, 0) == ZXM_E_ARG &&
              stillwater_set_schedule(&S, 1, 1, 0, 1440) == ZXM_E_ARG && S.sched.from == 1320 &&
              S.sched.days == 0x1f,
          "stillwater: a schedule that does not fit changes nothing");

    /* ST2: quiet now, the part after midnight belonging to the day it began */
    const stillwater_schedule_t *sc = &S.sched;
    CHECK(stillwater_quiet_at(sc, 0, 1380) == 1 && stillwater_quiet_at(sc, 1, 180) == 1 &&
              stillwater_quiet_at(sc, 5, 180) == 1 && stillwater_quiet_at(sc, 6, 180) == 0 &&
              stillwater_quiet_at(sc, 5, 1380) == 0 && stillwater_quiet_at(sc, 0, 720) == 0 &&
              stillwater_quiet_at(sc, 0, 420) == 0 && stillwater_quiet_at(sc, 0, 419) == 0 &&
              stillwater_quiet_at(sc, 1, 419) == 1 && stillwater_quiet_at(sc, 0, 1320) == 1 &&
              stillwater_quiet_at(sc, 7, 0) == -1 && stillwater_quiet_at(sc, 0, 1440) == -1,
          "stillwater: quiet hours by day and minute");
    {
        stillwater_schedule_t x = *sc;
        x.from = x.to = 600; /* the whole day, on the days chosen */
        bool all = stillwater_quiet_at(&x, 0, 0) == 1 && stillwater_quiet_at(&x, 4, 1439) == 1 &&
                   stillwater_quiet_at(&x, 5, 600) == 0;
        x.mode = STILLWATER_FOCUS;
        all = all && stillwater_quiet_at(&x, 6, 0) == 1;
        x.mode = STILLWATER_OFF;
        CHECK(all && stillwater_quiet_at(&x, 0, 600) == 0, "stillwater: whole day, focus, off");
    }

    /* the allow list */
    static char longsrc[STILLWATER_SOURCE + 2];
    memset(longsrc, 's', STILLWATER_SOURCE + 1);
    CHECK(stillwater_allow(&S, "ana") == ZXM_OK && stillwater_allow(&S, "ana") == ZXM_OK &&
              S.sched.nallowed == 1 && stillwater_allowed(&S, "ana") &&
              !stillwater_allowed(&S, "bob") && stillwater_allow(&S, "") == ZXM_E_ARG &&
              stillwater_allow(&S, NULL) == ZXM_E_ARG && stillwater_allow(&S, longsrc) == ZXM_E_ARG,
          "stillwater: allow ana");
    {
        char who[8];
        int rc = ZXM_OK;
        for (uint32_t i = 1; i < STILLWATER_ALLOWED && rc == ZXM_OK; i++) {
            snprintf(who, sizeof who, "p%u", i);
            rc = stillwater_allow(&S, who);
        }
        CHECK(rc == ZXM_OK && S.sched.nallowed == STILLWATER_ALLOWED &&
                  stillwater_allow(&S, "one-more") == ZXM_E_STATE,
              "stillwater: the allow list is bounded");
        for (uint32_t i = 1; i < STILLWATER_ALLOWED && rc == ZXM_OK; i++) {
            snprintf(who, sizeof who, "p%u", i);
            rc = stillwater_disallow(&S, who);
        }
        CHECK(rc == ZXM_OK && S.sched.nallowed == 1 && stillwater_allowed(&S, "ana") &&
                  stillwater_disallow(&S, "p1") == ZXM_E_ARG,
              "stillwater: disallowed again");
    }

    /* ST3, ST4: Monday 23:00, quiet; notes held or let through */
    SW_NOTES = SWL(
        5,
        SWN(6, "message", 1, true, 1, "dan") "," SWN(
            5, "message", 2, false, 1,
            "watchfire") "," SWN(4, "message", 3, false, 1,
                                 "watchfire") "," SWN(3, "call-incoming", 1, false, 1,
                                                      "carol") "," SWN(2, "message", 1, false, 1,
                                                                       "bob") "," SWN(1, "message",
                                                                                      1, false, 1,
                                                                                      "ana"));
    CHECK(stillwater_refresh(&S, 1000, 0, 1380) == ZXM_OK && S.quiet && S.nnote == 6 &&
              S.unread == 5 && S.held == 3 && sw_n(&S, 1)->through && sw_n(&S, 1)->allowed &&
              !sw_n(&S, 2)->through && !sw_n(&S, 3)->through && sw_n(&S, 3)->call &&
              sw_n(&S, 3)->gap_s == -1 && sw_n(&S, 4)->through && sw_n(&S, 4)->emergency &&
              !sw_n(&S, 5)->through && !sw_n(&S, 5)->emergency && sw_n(&S, 6)->read,
          "stillwater: quiet: ana allowed, the urgent Watchfire note through, three held: %s",
          LINK.err);
    SW_NOTES = SWL(
        5,
        SWN(6, "message", 1, true, 1, "dan") "," SWN(
            5, "message", 2, false, 1,
            "watchfire") "," SWN(4, "message", 3, false, 1,
                                 "watchfire") "," SWN(3, "call-incoming", 1, false, 2,
                                                      "carol") "," SWN(2, "message", 1, false, 3,
                                                                       "bob") "," SWN(1, "message",
                                                                                      1, false, 1,
                                                                                      "ana"));
    CHECK(stillwater_refresh(&S, 1100, 0, 1381) == ZXM_OK && S.held == 2 &&
              sw_n(&S, 3)->gap_s == 100 && sw_n(&S, 3)->through && sw_n(&S, 2)->gap_s == -1 &&
              !sw_n(&S, 2)->through,
          "stillwater: carol called again 100 s later: through; bob's messages are no call");
    SW_NOTES = SWL(1, SWN(7, "call-missed", 1, false, 2, "erin"));
    CHECK(stillwater_refresh(&S, 1400, 0, 1386) == ZXM_OK && S.nnote == 1 && S.held == 1 &&
              sw_n(&S, 7)->gap_s == 300 && !sw_n(&S, 7)->through,
          "stillwater: two calls first seen 300 s after the refresh before: held");
    SW_NOTES = SWL(1, SWN(7, "call-missed", 1, false, 4, "erin"));
    CHECK(stillwater_refresh(&S, 1450, 0, 1387) == ZXM_OK && S.held == 0 &&
              sw_n(&S, 7)->gap_s == 50 && sw_n(&S, 7)->through,
          "stillwater: two more within 50 s: through");
    SW_NOTES = SWL(1, SWN(8, "call-incoming", 1, false, 1, "erin"));
    CHECK(stillwater_refresh(&S, 1700, 0, 1391) == ZXM_OK && sw_n(&S, 8)->gap_s == 250 &&
              !sw_n(&S, 8)->through,
          "stillwater: a new note from erin is timed against her last call");

    /* ST4: in focus with nobody allowed, only the emergency goes through */
    CHECK(stillwater_disallow(&S, "ana") == ZXM_OK &&
              stillwater_set_schedule(&S, STILLWATER_FOCUS, 0, 0, 0) == ZXM_OK,
          "stillwater: focus");
    SW_NOTES = SWL(
        3, SWN(10, "message", 3, false, 1, "watchfire") "," SWN(
               9, "message", 3, false, 1, "ana") "," SWN(11, "message", 2, false, 1, "watchfire"));
    CHECK(stillwater_refresh(&S, 1800, 3, 720) == ZXM_OK && S.quiet && S.held == 2 &&
              sw_n(&S, 10)->through && sw_n(&S, 10)->emergency && !sw_n(&S, 9)->through &&
              !sw_n(&S, 11)->through,
          "stillwater: a Watchfire emergency is never held");
    CHECK(stillwater_set_schedule(&S, STILLWATER_OFF, 0, 0, 0) == ZXM_OK &&
              stillwater_refresh(&S, 1800, 3, 720) == ZXM_OK && !S.quiet && S.held == 0 &&
              sw_n(&S, 9)->through,
          "stillwater: off: everything through");

    /* ST5: refused before sending, or a bad reply changes nothing */
    CHECK(stillwater_refresh(&S, 1799, 3, 720) == ZXM_E_ARG &&
              stillwater_refresh(&S, 1900, 7, 720) == ZXM_E_ARG &&
              stillwater_refresh(&S, 1900, 3, 1440) == ZXM_E_ARG &&
              stillwater_refresh(&S, -1, 3, 1) == ZXM_E_ARG && S.at_s == 1800,
          "stillwater: time going back, a bad day or minute");
    static const char *const bad[] = {
        SWL(1, SWN(0, "message", 1, false, 1, "a")),
        SWL(1, SWN(1, "message", 4, false, 1, "a")),
        SWL(1, SWN(1, "message", 1, false, 0, "a")),
        SWL(2, SWN(1, "message", 1, false, 1, "a") "," SWN(1, "message", 1, false, 1, "b")),
        SWL(1, SWN(1, "message", 1, false, 1, "a-source-of-twenty-four!")),
        "{\"unread\":1,\"notes\":{}}",
        "{\"notes\":[]}",
    };
    bool kept = true;
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SW_NOTES = bad[i];
        kept = kept && stillwater_refresh(&S, 1900, 3, 720) == ZXM_E_PROTOCOL && S.nnote == 3 &&
               S.at_s == 1800 && stillwater_note(&S, 9);
    }
    CHECK(kept, "stillwater: a reply out of bounds leaves what was shown");
    {
        static char many[4096];
        size_t n = (size_t) snprintf(many, sizeof many, "{\"unread\":21,\"notes\":[");
        for (uint32_t i = 1; i <= STILLWATER_NOTES + 1; i++)
            n += (size_t) snprintf(many + n, sizeof many - n,
                                   "%s{\"seq\":%u,\"kind\":\"message\",\"pri\":1,\"read\":false,"
                                   "\"count\":1,\"source\":\"a\",\"title\":\"t\"}",
                                   i > 1 ? "," : "", i);
        snprintf(many + n, sizeof many - n, "]}");
        SW_NOTES = many;
        CHECK(stillwater_refresh(&S, 1900, 3, 720) == ZXM_E_PROTOCOL && S.nnote == 3,
              "stillwater: more notes than notes.list gives");
    }

    /* notes.mark_read */
    SW_NOTES =
        SWL(2, SWN(12, "message", 1, false, 1, "bob") "," SWN(13, "message", 1, false, 1, "cy"));
    CHECK(stillwater_set_schedule(&S, STILLWATER_FOCUS, 0, 0, 0) == ZXM_OK &&
              stillwater_refresh(&S, 1900, 3, 720) == ZXM_OK && S.held == 2 && S.unread == 2,
          "stillwater: two held");
    uint32_t marked = 0;
    sw_marked = 2;
    {
        int before = sent;
        CHECK(stillwater_mark_read(&S, &marked, false) == ZXM_E_STATE && sent == before &&
                  marked == 0 && S.unread == 2 && S.held == 2 && !sw_n(&S, 12)->read &&
                  !sw_n(&S, 13)->read,
              "stillwater: without the owner's word nothing is sent and nothing marked");
    }
    CHECK(stillwater_mark_read(&S, &marked, true) == ZXM_OK && marked == 2 && S.unread == 0 &&
              S.held == 0 && sw_n(&S, 12)->read && sw_n(&S, 13)->read,
          "stillwater: marked read through zxvd");

    /* screen.status and screen.check: Shepherd's limits, read here */
    CHECK(raw_call("screen.set_limit", "{\"app\":\"quillon\",\"minutes\":30}") == ZXM_OK &&
              raw_call("screen.set_limit", "{\"app\":\"glint\",\"minutes\":0}") == ZXM_OK &&
              raw_call("screen.set_limit", "{\"app\":\"wake\",\"from\":480,\"to\":1260}") == ZXM_OK,
          "stillwater: three limits set: %s", LINK.err);
    CHECK(stillwater_refresh(&S, 2000, 3, 721) == ZXM_OK && S.nlim == 3 && !S.locked,
          "stillwater: limits read: %s", LINK.err);
    {
        bool q = false, g = false, w = false;
        for (uint32_t i = 0; i < S.nlim; i++) {
            q = q || (!strcmp(S.lim[i].app, "quillon") && S.lim[i].minutes == 30 &&
                      S.lim[i].from == 0 && S.lim[i].to == 0);
            g = g || (!strcmp(S.lim[i].app, "glint") && S.lim[i].minutes == 0);
            w = w || (!strcmp(S.lim[i].app, "wake") && S.lim[i].minutes == -1 &&
                      S.lim[i].from == 480 && S.lim[i].to == 1260);
        }
        CHECK(q && g && w, "stillwater: each limit as zxvd keeps it");
    }
    CHECK(stillwater_check_app(&S, "quillon") == ZXM_OK && S.check.allowed && S.check.left == 30 &&
              S.check.used == 0 && !S.check.why[0] && !strcmp(S.check.app, "quillon"),
          "stillwater: quillon may be used, 30 minutes left: %s", LINK.err);
    CHECK(stillwater_check_app(&S, "glint") == ZXM_OK && !S.check.allowed && S.check.left == 0 &&
              S.check.why[0],
          "stillwater: glint has no minutes today: %s", S.check.why);
    CHECK(stillwater_check_app(&S, "") == ZXM_E_ARG && stillwater_check_app(&S, NULL) == ZXM_E_ARG,
          "stillwater: no app, refused before sending");
    zsc_close();

    /* without the host's notes, zxvd answers an error and nothing changes */
    zxvd_rpc_set_ops(&OPS);
    CHECK(stillwater_refresh(&S, 2100, 3, 722) == ZXM_E_REMOTE && LINK.err_code == ZXVD_E_METHOD &&
              S.at_s == 2000 && S.nnote == 2,
          "stillwater: notes.list not available: %s", LINK.err);
}
