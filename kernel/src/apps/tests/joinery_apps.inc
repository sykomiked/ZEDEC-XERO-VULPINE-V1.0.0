/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Joinery's model against zxvd's real dispatcher: source files written
 * with files.write into the test's scratch Hoard, opened, edited, placed by
 * line and column, undone, saved whole (in parts when large) and renamed
 * only on the owner's word, found with search.query and search.refresh, and
 * the swarm's suggestions (swarm.ask, the test's stub) held until accepted.
 * play.run is not linked in this in-process zxvd, so it is answered on a
 * link of Joinery's own (JNA_LINK), which passes every other request to the
 * real dispatcher; it also stands in for a file past 16 MiB, which would
 * take two thousand writes to make. */
#include "joinery_model.h"

static joinery_t JN, JN2;
static zxm_link_t JNA_LINK;
static const char *JNA_PLAY; /* play.run's result */
static bool JNA_HUGE;        /* files.read answers a file of 16 MiB + 1 */
static char jna_req[ZXM_REQ_MAX + 1];

static int32_t jna_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    if (strstr(req, "\"play.run\"")) {
        sent++;
        memcpy(jna_req, req, len);
        jna_req[len] = 0;
        return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq,
                        JNA_PLAY);
    }
    if (JNA_HUGE && strstr(req, "\"files.read\"")) {
        sent++;
        return snprintf(
            resp, cap,
            "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"path\":\"Code/huge.c\","
            "\"size\":16777217,\"offset\":0,\"next\":3,\"eof\":false,\"text\":\"abc\"}}",
            l->seq);
    }
    return inproc(NULL, req, len, resp, cap);
}

/* a file written straight through zxvd, as another app would, in parts */
static bool jna_write(const char *path, const char *text, uint32_t n)
{
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t k = n - at < 8000u ? n - at : 8000u;
        bool last = at + k >= n;
        zxm_begin(&LINK, "files.write");
        zxm_p_str(&LINK, "path", path);
        zxm_p_strn(&LINK, "text", text + at, k);
        if (!(first && last)) zxm_p_str(&LINK, "mode", first ? "begin" : last ? "end" : "add");
        if (zxm_call(&LINK) != ZXM_OK) return false;
        at += k;
        first = false;
    } while (at < n);
    return true;
}

/* what the Hoard holds at path, whole, into out (cap bytes); its length or -1 */
static int64_t jna_read(const char *path, char *out, uint32_t cap)
{
    static char chunk[64u * 1024u];
    uint64_t off = 0;
    uint32_t len = 0;
    for (;;) {
        zxm_begin(&LINK, "files.read");
        zxm_p_str(&LINK, "path", path);
        zxm_p_u64(&LINK, "offset", off);
        uint64_t next;
        bool eof;
        int32_t t;
        if (zxm_call(&LINK) != ZXM_OK || zxm_get_u64(&LINK, LINK.result, "next", &next) ||
            zxm_get_bool(&LINK, LINK.result, "eof", &eof) ||
            (t = zxm_get(&LINK, LINK.result, "text")) < 0)
            return -1;
        int32_t n = w4_json_str(LINK.resp, &LINK.tok[t], chunk, sizeof chunk);
        if (n < 0 || len + (uint32_t) n >= cap) return -1;
        memcpy(out + len, chunk, (size_t) n);
        len += (uint32_t) n;
        if (eof) break;
        off = next;
    }
    out[len] = 0;
    return len;
}

/* every boundary of the buffer goes to a place and back to itself */
static bool jna_round_trip(joinery_t *j)
{
    for (uint32_t off = 0; off <= j->len; off++) {
        int64_t line, col, back;
        bool mid = off < j->len && ((uint8_t) j->text[off] & 0xc0) == 0x80;
        int rc = joinery_offset_place(j, off, &line, &col);
        if (mid) {
            if (rc != ZXM_E_ARG) return false;
            continue;
        }
        if (rc || joinery_place_offset(j, line, col, &back) || back != off) return false;
    }
    return true;
}

APPS_TEST(joinery)
{
    static char big[40000], back[40000], longline[JOINERY_LINE_MAX + 2];
    static const char SRC[] =
        "int main(void)\n{\n    /* h\xc3\xa9llo \xe2\x82\xac \xf0\x9d\x84\x9e */\n"
        "    return 0;\n}";
    const uint32_t SRCN = (uint32_t) sizeof SRC - 1u;
    int64_t line = -1, col = -1, off = -1;
    int before;
    zxm_link_init(&JNA_LINK, jna_send, &JNA_LINK);

    /* JR1-JR7 through the model's own C */
    {
        static const int64_t lb[2] = {4, 2}, w[3] = {1, 3, 4};
        CHECK(joinery_line_start(lb, 2, 2) == 6 && joinery_offset_line(lb, 2, 4) == 1 &&
                  joinery_col_offset(w, 3, 2) == 4 && joinery_offset_col(w, 3, 8) == 3 &&
                  joinery_offset_col(w, 3, 3) == JOINERY_REFUSED &&
                  joinery_buffer_ok(JOINERY_FILE_MAX, JOINERY_LINE_MAX, JOINERY_UNDO_MAX) &&
                  !joinery_buffer_ok(10, 11, 0) && joinery_undo_fits(1024, 16777216, 0, 5) &&
                  !joinery_undo_fits(10000, 0, 0, 0) &&
                  joinery_fill_x100(JOINERY_FILE_MAX / 2) == 5000,
              "joinery: the rules (JR1-JR7)");
    }

    /* nothing open: every action refused before anything is sent */
    joinery_init(&JN, &JNA_LINK);
    before = sent;
    CHECK(joinery_edit(&JN, 0, 0, "x", 1) == ZXM_E_STATE && joinery_undo(&JN) == ZXM_E_STATE &&
              joinery_save(&JN, true) == ZXM_E_STATE &&
              joinery_rename(&JN, "Code/b.c", true) == ZXM_E_STATE &&
              joinery_place_offset(&JN, 0, 0, &off) == ZXM_E_STATE &&
              joinery_run(&JN, "hello", "main.c") == ZXM_E_STATE &&
              joinery_suggest(&JN, JOINERY_AI_FIX, 0, 0) == ZXM_E_STATE &&
              joinery_fill(&JN) == JOINERY_REFUSED && joinery_open(&JN, "") == ZXM_E_ARG &&
              joinery_open(&JN, "/abs.c") == ZXM_E_ARG && joinery_open(&JN, "a\nb") == ZXM_E_ARG &&
              sent == before,
          "joinery: nothing open, nothing sent");

    /* JM1: open a file of the Hoard */
    zxm_begin(&LINK, "files.mkdir");
    zxm_p_str(&LINK, "path", "Code");
    CHECK(zxm_call(&LINK) == ZXM_OK && jna_write("Code/main.c", SRC, SRCN), "joinery: written");
    CHECK(joinery_open(&JN, "Code/main.c") == ZXM_OK && JN.open && !JN.fresh && !JN.dirty &&
              JN.len == SRCN && !memcmp(JN.text, SRC, SRCN) && JN.longest == 25 &&
              joinery_fill(&JN) == (int64_t) SRCN * 10000 / JOINERY_FILE_MAX,
          "joinery: opened (%s)", JNA_LINK.err);
    joinery_init(&JN2, &JNA_LINK);
    CHECK(joinery_open(&JN2, "Code/none.c") == ZXM_E_REMOTE && !JN2.open,
          "joinery: a missing file is not opened");

    /* JM3: places, through JR1-JR4 */
    CHECK(joinery_offset_place(&JN, 0, &line, &col) == ZXM_OK && line == 0 && col == 0,
          "joinery: the first place");
    CHECK(joinery_place_offset(&JN, 2, 8, &off) == ZXM_OK && off == 25 &&
              joinery_place_offset(&JN, 2, 9, &off) == ZXM_OK && off == 27,
          "joinery: a column past a two-byte character (%lld)", (long long) off);
    CHECK(joinery_place_offset(&JN, 2, 16, &off) == ZXM_OK && off == 39 &&
              joinery_offset_place(&JN, 39, &line, &col) == ZXM_OK && line == 2 && col == 16,
          "joinery: past the four-byte character and back");
    CHECK(joinery_place_offset(&JN, 4, 1, &off) == ZXM_OK && off == (int64_t) SRCN &&
              joinery_offset_place(&JN, SRCN, &line, &col) == ZXM_OK && line == 4 && col == 1,
          "joinery: the end, on a last line with no newline");
    CHECK(joinery_offset_place(&JN, 26, &line, &col) == ZXM_E_ARG &&
              joinery_offset_place(&JN, SRCN + 1, &line, &col) == ZXM_E_ARG &&
              joinery_place_offset(&JN, 5, 0, &off) == ZXM_E_ARG &&
              joinery_place_offset(&JN, 1, 2, &off) == ZXM_E_ARG &&
              joinery_place_offset(&JN, -1, 0, &off) == ZXM_E_ARG,
          "joinery: inside a character, past the text, past a line: refused");
    CHECK(jna_round_trip(&JN), "joinery: every offset to a place and back");

    /* JM2: edits and their undo */
    CHECK(joinery_edit(&JN, 15, 0, "\t", 1) == ZXM_OK && JN.text[15] == '\t' && JN.dirty &&
              JN.nundo == 1 && JN.undo[0].step == 1 && JN.undo[0].offset == 15 &&
              JN.undo[0].removed == 0 && JN.undo[0].inserted == 1,
          "joinery: an insertion and its record");
    CHECK(joinery_edit(&JN, 0, 3, "long", 4) == ZXM_OK && !strncmp(JN.text, "long main", 9) &&
              JN.nundo == 2,
          "joinery: a replacement");
    CHECK(joinery_undo(&JN) == ZXM_OK && joinery_undo(&JN) == ZXM_OK && JN.len == SRCN &&
              !memcmp(JN.text, SRC, SRCN) && JN.nundo == 0 && joinery_undo(&JN) == ZXM_E_STATE,
          "joinery: both undone, back to the file");
    memset(longline, 'x', JOINERY_LINE_MAX + 1);
    CHECK(joinery_edit(&JN, 26, 0, "x", 1) == ZXM_E_ARG &&
              joinery_edit(&JN, 0, 26, "x", 1) == ZXM_E_ARG &&
              joinery_edit(&JN, SRCN + 1, 0, "x", 1) == ZXM_E_ARG &&
              joinery_edit(&JN, 0, SRCN + 1, "", 0) == ZXM_E_ARG &&
              joinery_edit(&JN, 0, 0, "\xc3", 1) == ZXM_E_ARG &&
              joinery_edit(&JN, 0, 0, "a\0b", 3) == ZXM_E_ARG &&
              joinery_edit(&JN, 0, 0, NULL, 1) == ZXM_E_ARG &&
              joinery_edit(&JN, 0, 0, "", 0) == ZXM_E_ARG &&
              joinery_edit(&JN, 14, 0, longline, (uint32_t) JOINERY_LINE_MAX - 13u) == ZXM_E_ARG &&
              JN.len == SRCN && !memcmp(JN.text, SRC, SRCN) && JN.nundo == 0,
          "joinery: inside a character, past the text, not UTF-8, a line past 65536: unchanged");
    CHECK(joinery_edit(&JN, 14, 0, longline, (uint32_t) JOINERY_LINE_MAX - 14u) == ZXM_OK &&
              JN.longest == JOINERY_LINE_MAX && joinery_undo(&JN) == ZXM_OK && JN.longest == 25,
          "joinery: a line of exactly 65536 bytes, then undone");
    for (uint32_t i = 0; i < JOINERY_UNDO_MAX + 6u; i++) joinery_edit(&JN, SRCN, 0, "a", 1);
    bool steps = JN.nundo == JOINERY_UNDO_MAX && JN.undo_dropped == 6;
    for (uint32_t i = 0; i < JN.nundo; i++)
        steps = steps && JN.undo[i].step == (int64_t) i + 1 && JN.undo[i].offset == SRCN &&
                joinery_undo_fits(JN.undo[i].step, JN.undo[i].offset, JN.undo[i].removed,
                                  JN.undo[i].inserted);
    CHECK(steps, "joinery: the history holds 1024 steps, the oldest given up, renumbered");
    for (uint32_t i = 0; i < JOINERY_UNDO_MAX; i++) joinery_undo(&JN);
    CHECK(JN.nundo == 0 && JN.len == SRCN + 6u && !memcmp(JN.text, SRC, SRCN) &&
              !strcmp(JN.text + SRCN, "aaaaaa"),
          "joinery: 1024 steps undone, the six given up stay");
    {
        static char huge[JOINERY_UNDO_BYTES + 2];
        for (uint32_t i = 0; i < JOINERY_UNDO_BYTES + 1u; i++)
            huge[i] = i % 100u == 99u ? '\n' : 'y';
        uint32_t was = JN.len;
        CHECK(joinery_edit(&JN, 0, 0, "z", 1) == ZXM_OK && JN.nundo == 1 &&
                  joinery_edit(&JN, 0, 0, huge, JOINERY_UNDO_BYTES + 1u) == ZXM_OK &&
                  JN.nundo == 0 && JN.len == was + 2u + JOINERY_UNDO_BYTES &&
                  joinery_undo(&JN) == ZXM_E_STATE,
              "joinery: an edit larger than the history clears it");
    }

    /* JM4: saving waits for the owner */
    CHECK(joinery_open(&JN, "Code/main.c") == ZXM_OK, "joinery: opened again");
    CHECK(joinery_edit(&JN, 0, 3, "long", 4) == ZXM_OK, "joinery: changed");
    before = sent;
    CHECK(joinery_save(&JN, false) == ZXM_E_STATE && sent == before && JN.dirty &&
              jna_read("Code/main.c", back, sizeof back) == SRCN && !memcmp(back, SRC, SRCN),
          "joinery: no save without the owner's word: nothing sent, the file unchanged");
    CHECK(joinery_save(&JN, true) == ZXM_OK && !JN.dirty &&
              jna_read("Code/main.c", back, sizeof back) == JN.len &&
              !memcmp(back, JN.text, JN.len),
          "joinery: saved on the owner's word (%s)", JNA_LINK.err);
    for (uint32_t i = 0; i + 4u <= sizeof big - 1u; i += 4u)
        memcpy(big + i, i % 400u == 396u ? "\xe2\x82\xac\n" : "ab\"\\", 4);
    big[sizeof big - 1u] = 0;
    uint32_t bign = (uint32_t) strlen(big);
    CHECK(joinery_edit(&JN, 0, JN.len, big, bign) == ZXM_OK && joinery_save(&JN, true) == ZXM_OK &&
              jna_read("Code/main.c", back, sizeof back) == bign && !memcmp(back, big, bign),
          "joinery: a large file saved in parts, escaped (%s)", JNA_LINK.err);
    CHECK(jna_round_trip(&JN), "joinery: every offset of the large file to a place and back");
    CHECK(joinery_new(&JN2, "Code/main.c") == ZXM_OK && JN2.fresh &&
              joinery_edit(&JN2, 0, 0, "x", 1) == ZXM_OK &&
              joinery_save(&JN2, true) == ZXM_E_REMOTE && JN2.fresh &&
              jna_read("Code/main.c", back, sizeof back) == bign,
          "joinery: a new file is never written over another");
    CHECK(joinery_new(&JN2, "Code/new.c") == ZXM_OK &&
              joinery_edit(&JN2, 0, 0, "int x;\n", 7) == ZXM_OK &&
              joinery_save(&JN2, true) == ZXM_OK && !JN2.fresh &&
              jna_read("Code/new.c", back, sizeof back) == 7,
          "joinery: a new file saved (%s)", JNA_LINK.err);
    before = sent;
    CHECK(joinery_rename(&JN2, "Code/old.c", false) == ZXM_E_STATE && sent == before &&
              !strcmp(JN2.path, "Code/new.c") &&
              joinery_rename(&JN2, "Code/new.c", true) == ZXM_E_ARG &&
              joinery_rename(&JN2, "", true) == ZXM_E_ARG && sent == before,
          "joinery: no rename without the owner's word: nothing sent");
    CHECK(joinery_rename(&JN2, "Code/main.c", true) == ZXM_E_REMOTE &&
              !strcmp(JN2.path, "Code/new.c"),
          "joinery: a rename never replaces a file");
    CHECK(joinery_rename(&JN2, "Code/old.c", true) == ZXM_OK && !strcmp(JN2.path, "Code/old.c") &&
              jna_read("Code/old.c", back, sizeof back) == 7 &&
              jna_read("Code/new.c", back, sizeof back) == -1,
          "joinery: renamed on the owner's word (%s)", JNA_LINK.err);

    /* JM1: limits on open */
    memset(longline, 'x', JOINERY_LINE_MAX + 1);
    longline[JOINERY_LINE_MAX + 1] = 0;
    CHECK(jna_write("Code/wide.c", longline, JOINERY_LINE_MAX + 1u), "joinery: a wide file");
    CHECK(joinery_open(&JN2, "Code/wide.c") == ZXM_E_STATE && !strcmp(JN2.path, "Code/old.c") &&
              JN2.len == 7,
          "joinery: a line past 65536 bytes is not opened, the buffer kept");
    JNA_HUGE = true;
    CHECK(joinery_open(&JN2, "Code/huge.c") == ZXM_E_TOOBIG && !strcmp(JN2.path, "Code/old.c"),
          "joinery: a file past 16 MiB is not opened");
    JNA_HUGE = false;

    /* JM5: finding in files */
    CHECK(jna_write("Code/finder.c", "int zebrawood = 1;\n", 19), "joinery: a file to find");
    before = sent;
    CHECK(joinery_find(&JN, "") == ZXM_E_ARG && joinery_find(&JN, "   ") == ZXM_E_ARG &&
              sent == before,
          "joinery: an empty query is not sent");
    CHECK(joinery_find(&JN, "zebrawood") == ZXM_OK && JN.nhits == 1 && JN.total_hits == 1 &&
              !strcmp(JN.hit[0].path, "Code/finder.c") && !JN.hit[0].in_name &&
              strstr(JN.hit[0].snippet, "zebrawood"),
          "joinery: found in a file (%s)", JNA_LINK.err);
    CHECK(joinery_find(&JN, "finder") == ZXM_OK && JN.nhits >= 1 && JN.hit[0].in_name,
          "joinery: found by its name");
    CHECK(joinery_reindex(&JN) == ZXM_OK && JN.indexed >= 4 && JN.text_indexed <= JN.indexed,
          "joinery: the index brought up to date (%s)", JNA_LINK.err);

    /* JM6: a Playground run, four-valued */
    CHECK(joinery_edit(&JN, 0, JN.len, "x = 1\n", 6) == ZXM_OK, "joinery: a short program");
    JNA_PLAY = "{\"template\":\"hello\",\"verdict\":\"TRUE\",\"headline\":\"passed\","
               "\"approval\":\"not required\",\"steps\":[]}";
    CHECK(joinery_run(&JN, "hello", "main.kron") == ZXM_OK && JN.verdict == LPRES_STATE_TRUE &&
              !JN.approval_required && !strcmp(JN.headline, "passed") &&
              !strcmp(JN.template_id, "hello") &&
              strstr(jna_req, "\"files\":{\"main.kron\":\"x = 1\\n\"}"),
          "joinery: a run that passed, the buffer sent as its file: %s", jna_req);
    JNA_PLAY = "{\"verdict\":\"FALSE\",\"headline\":\"line 1, column 3: refused\","
               "\"approval\":\"required\"}";
    CHECK(joinery_run(&JN, "hello", "main.kron") == ZXM_OK && JN.verdict == LPRES_STATE_FALSE &&
              JN.approval_required && strstr(JN.headline, "column 3"),
          "joinery: a run refused, with its line and column");
    JNA_PLAY = "{\"verdict\":\"BOTH\"}";
    CHECK(joinery_run(&JN, "hello", "main.kron") == ZXM_OK && JN.verdict == LPRES_STATE_BOTH,
          "joinery: a run held for a person");
    JNA_PLAY = "{\"verdict\":\"MAYBE\"}";
    CHECK(joinery_run(&JN, "hello", "main.kron") == ZXM_E_PROTOCOL &&
              JN.verdict == LPRES_STATE_BOTH,
          "joinery: a verdict that is none of the four");
    before = sent;
    CHECK(joinery_run(&JN, "", "main.kron") == ZXM_E_ARG &&
              joinery_run(&JN, "hello", "a/b") == ZXM_E_ARG && sent == before,
          "joinery: a bad template or file name is not sent");
    CHECK(joinery_edit(&JN, 0, JN.len, big, bign) == ZXM_OK &&
              joinery_run(&JN, "hello", "main.kron") == ZXM_E_TOOBIG && sent == before,
          "joinery: a buffer too large for one request is not sent");

    /* JM7: suggestions held until accepted */
    CHECK(joinery_edit(&JN, 0, JN.len, "int a = 1\nint b;\n", 17) == ZXM_OK, "joinery: code");
    ANSWER = "int a = 1;";
    CHECK(joinery_suggest(&JN, JOINERY_AI_COUNT, 0, 0) == ZXM_E_ARG &&
              joinery_suggest(&JN, JOINERY_AI_FIX, 3, 99) == ZXM_E_ARG,
          "joinery: a bad action or range");
    CHECK(joinery_suggest(&JN, JOINERY_AI_FIX, 0, 9) == ZXM_OK &&
              JN.held.state == ZXM_HELD_PENDING && !strcmp(JN.text, "int a = 1\nint b;\n") &&
              strstr(last_prompt, "\n\nint a = 1") && !strstr(last_prompt, "int b"),
          "joinery: a fix held, the buffer unchanged: %s", last_prompt);
    CHECK(joinery_suggest_accept(&JN) == ZXM_OK && !strcmp(JN.text, "int a = 1;\nint b;\n") &&
              JN.held.state == ZXM_HELD_ACCEPTED && joinery_undo(&JN) == ZXM_OK &&
              !strcmp(JN.text, "int a = 1\nint b;\n"),
          "joinery: accepted as an edit, and undone like one: %s", JN.text);
    ANSWER = "int c;\n";
    CHECK(joinery_suggest(&JN, JOINERY_AI_COMPLETE, 0, 0) == ZXM_OK &&
              joinery_suggest_accept(&JN) == ZXM_OK &&
              !strcmp(JN.text, "int a = 1\nint b;\nint c;\n"),
          "joinery: a completion after the whole buffer");
    ANSWER = "It declares three integers.";
    CHECK(joinery_suggest(&JN, JOINERY_AI_EXPLAIN, 0, 0) == ZXM_OK &&
              joinery_suggest_accept(&JN) == ZXM_OK &&
              !strcmp(JN.text, "int a = 1\nint b;\nint c;\n"),
          "joinery: an explanation changes nothing");
    CHECK(joinery_suggest(&JN, JOINERY_AI_FIX, 0, 0) == ZXM_OK &&
              joinery_edit(&JN, 0, 0, "x", 1) == ZXM_OK && JN.held.state == ZXM_HELD_REJECTED &&
              joinery_suggest_accept(&JN) == ZXM_E_STATE,
          "joinery: an edit drops a held suggestion");
    CHECK(joinery_suggest(&JN, JOINERY_AI_FIX, 0, 0) == ZXM_OK &&
              joinery_suggest_reject(&JN) == ZXM_OK && JN.held.state == ZXM_HELD_REJECTED &&
              JN.text[0] == 'x',
          "joinery: a rejected suggestion keeps the buffer");
    CHECK(joinery_suggest_accept(&JN) == ZXM_E_STATE && joinery_suggest_reject(&JN) == ZXM_E_STATE,
          "joinery: nothing held, nothing to accept or reject");
    ANSWER = "ok";
}
