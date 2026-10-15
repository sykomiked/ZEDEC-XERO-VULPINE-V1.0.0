/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Flint's model against zxvd's real dispatcher (LINK): its rules, its
 * word and path rules, then a session that walks a folder in the Hoard,
 * shows files, reads the machine's facts, and keeps its scrollback and
 * history inside their limits. */
#include "flint_model.h"

/* a file of n bytes written in parts (files.write begin, add, end) */
static int fl_write(const char *path, const char *text, uint32_t n)
{
    uint32_t parts = (n + 7999u) / 8000u, done = 0;
    if (!parts) parts = 1;
    for (uint32_t k = 0; k < parts; k++) {
        uint32_t m = n - done < 8000u ? n - done : 8000u;
        zxm_begin(&LINK, "files.write");
        zxm_p_str(&LINK, "path", path);
        zxm_p_strn(&LINK, "text", text + done, m);
        if (parts > 1) zxm_p_str(&LINK, "mode", k == 0 ? "begin" : k == parts - 1 ? "end" : "add");
        int rc = zxm_call(&LINK);
        if (rc) return rc;
        done += m;
    }
    return ZXM_OK;
}

static int32_t fl_parse(const char *s, flint_words_t *w)
{
    return flint_parse(s, (uint32_t) strlen(s), w);
}

/* line i of the scrollback counted from the newest (0 = the last) */
static const char *fl_back(const flint_t *f, uint32_t i)
{
    static char s[FLINT_LINE_MAX + 1];
    s[0] = 0;
    if (i >= flint_lines(f) || flint_line(f, flint_lines(f) - 1u - i, s, sizeof s) < 0) return "";
    return s;
}

APPS_TEST(flint)
{
    static flint_t F;
    static flint_words_t W;
    static char big[70000];
    char p[FLINT_PATH_MAX + 1];

    /* the rules, as the blocks run them */
    CHECK(flint_cells(1920, 8) == 240 && flint_cells(7, 8) == 0 && flint_cells(0, 1) == 0 &&
              flint_cells(65535, 1) == 65535 && flint_cells(65535, 256) == 255,
          "flint: whole cells, a part cell never counted");
    CHECK(flint_cells(65536, 8) == FLINT_REFUSED && flint_cells(-1, 8) == FLINT_REFUSED &&
              flint_cells(800, 0) == FLINT_REFUSED && flint_cells(800, 257) == FLINT_REFUSED,
          "flint: cells refuses outside its domain, as the C++ block does");
    CHECK(flint_scroll_ok(24, 4096) && flint_scroll_ok(10000, 1048576) &&
              !flint_scroll_ok(23, 4096) && !flint_scroll_ok(10001, 4096) &&
              !flint_scroll_ok(24, 4095) && !flint_scroll_ok(24, 1048577),
          "flint: the scrollback limits (Eve's ranges)");
    CHECK(flint_history_ok(999999999, INT64_C(999999999999), 999, 9999) &&
              flint_history_ok(-999999999, 0, -999, 0) && !flint_history_ok(1000000000, 0, 0, 0) &&
              !flint_history_ok(1, INT64_C(1000000000000), 0, 0) &&
              !flint_history_ok(1, 0, 1000, 0) && !flint_history_ok(1, 0, 0, 10000),
          "flint: the history record's digit widths (MYRA's)");
    CHECK(flint_used_x100(3, 2) == 3333 && flint_used_x100(1, 0) == 10000 &&
              flint_used_x100(0, 0) == FLINT_REFUSED && flint_used_x100(1, 2) == FLINT_REFUSED,
          "flint: the share in use (used_share's rule)");

    /* words: zxsh's quoting, comments, and what is zxsh's alone */
    CHECK(fl_parse("  echo 'a  b' \"c\\\"d\" e\\ f '' # gone", &W) == 5 &&
              !strcmp(W.argv[0], "echo") && !strcmp(W.argv[1], "a  b") &&
              !strcmp(W.argv[2], "c\"d") && !strcmp(W.argv[3], "e f") && !strcmp(W.argv[4], ""),
          "flint: words, quotes, an escape, an empty word and a comment");
    CHECK(fl_parse("a#b \"x\\ny\"", &W) == 2 && !strcmp(W.argv[0], "a#b") &&
              !strcmp(W.argv[1], "x\\ny"),
          "flint: # inside a word, a backslash kept inside double quotes");
    CHECK(fl_parse("ls | wc", &W) == FLINT_P_OP && fl_parse("a>b", &W) == FLINT_P_OP &&
              fl_parse("'|'", &W) == 1 && !strcmp(W.argv[0], "|"),
          "flint: an unquoted pipe or redirection is zxsh's; a quoted one is a word");
    CHECK(fl_parse("'open", &W) == FLINT_P_QUOTE && fl_parse("a\\", &W) == FLINT_P_QUOTE &&
              fl_parse("a\x01", &W) == FLINT_P_CTRL &&
              fl_parse("a b c d e f g h i j k l m n o p q", &W) == FLINT_P_MANY &&
              fl_parse("a b c d e f g h i j k l m n o p", &W) == 16 && fl_parse("", &W) == 0 &&
              flint_parse(big, FLINT_CMD_MAX + 1, &W) == FLINT_P_LONG,
          "flint: an open quote, a control character, 17 words, a line too long");

    /* paths: joined to the working folder, . and .. resolved, / the top */
    CHECK(flint_resolve("a/b", "c", p, sizeof p) == ZXM_OK && !strcmp(p, "a/b/c") &&
              flint_resolve("a/b", "../x/./y/", p, sizeof p) == ZXM_OK && !strcmp(p, "a/x/y") &&
              flint_resolve("a/b", "/z", p, sizeof p) == ZXM_OK && !strcmp(p, "z") &&
              flint_resolve("a", "../../..", p, sizeof p) == ZXM_OK && !strcmp(p, "") &&
              flint_resolve("", ".", p, sizeof p) == ZXM_OK && !strcmp(p, ""),
          "flint: paths resolve inside the Hoard");
    CHECK(flint_resolve("abc", "defg", p, 8) == ZXM_E_ARG &&
              flint_resolve("abc", "def", p, 8) == ZXM_OK && !strcmp(p, "abc/def"),
          "flint: a path one byte too long is refused");

    /* the grid and the limits: a refusal changes nothing */
    flint_init(&F, &LINK);
    CHECK(flint_resize(&F, 1920, 1080, 8, 16) == ZXM_OK && F.cols == 240 && F.rows == 67,
          "flint: 1920x1080 at 8x16 is 240 columns, 67 rows");
    CHECK(flint_resize(&F, 4, 1080, 8, 16) == ZXM_E_ARG &&
              flint_resize(&F, 70000, 1080, 8, 16) == ZXM_E_ARG &&
              flint_resize(&F, 1920, 1080, 0, 16) == ZXM_E_ARG && F.cols == 240 && F.rows == 67,
          "flint: no whole column, a span past 65535, a cell of 0: the grid stays");
    CHECK(flint_set_limits(&F, 23, 4096) == ZXM_E_ARG &&
              flint_set_limits(&F, 24, 1048577) == ZXM_E_ARG && F.lines_limit == FLINT_LINES_DEF &&
              F.bytes_limit == FLINT_BYTES_DEF,
          "flint: limits outside Eve's ranges are refused");

    /* a session in the Hoard */
    zxm_begin(&LINK, "files.mkdir");
    zxm_p_str(&LINK, "path", "flint-home");
    CHECK(zxm_call(&LINK) == ZXM_OK, "flint: a folder made: %s", LINK.err);
    zxm_begin(&LINK, "files.mkdir");
    zxm_p_str(&LINK, "path", "flint-home/logs");
    CHECK(zxm_call(&LINK) == ZXM_OK, "flint: a folder inside it: %s", LINK.err);
    CHECK(fl_write("flint-home/notes.txt", "first line\r\nsecond line\n", 24) == ZXM_OK,
          "flint: a note written: %s", LINK.err);
    for (uint32_t i = 0; i < sizeof big; i++)
        big[i] = (i % 100u) == 99u ? '\n' : (char) ('a' + i % 26u);
    CHECK(fl_write("flint-home/logs/big.txt", big, sizeof big) == ZXM_OK,
          "flint: 70000 bytes written in parts: %s", LINK.err);

    CHECK(flint_run(&F, "pwd", 1700000000) == FLINT_ST_OK && !strcmp(fl_back(&F, 0), "/") &&
              !strcmp(fl_back(&F, 1), "/ $ pwd"),
          "flint: the prompt, then /");
    CHECK(flint_run(&F, "cd flint-home", 1700000001) == FLINT_ST_OK &&
              !strcmp(F.cwd, "flint-home") && flint_run(&F, "pwd", 1700000002) == FLINT_ST_OK &&
              !strcmp(fl_back(&F, 0), "/flint-home") &&
              !strcmp(fl_back(&F, 1), "/flint-home $ pwd"),
          "flint: cd into a folder: %s", fl_back(&F, 0));
    CHECK(flint_run(&F, "ls", 1700000003) == FLINT_ST_OK &&
              !strcmp(fl_back(&F, 0), "notes.txt  24") && !strcmp(fl_back(&F, 1), "logs/"),
          "flint: ls, folders first: %s", fl_back(&F, 0));
    CHECK(flint_run(&F, "cat notes.txt", 1700000004) == FLINT_ST_OK &&
              !strcmp(fl_back(&F, 0), "second line") && !strcmp(fl_back(&F, 1), "first line"),
          "flint: cat, one line per line, the CR dropped");
    CHECK(flint_run(&F, "stat /flint-home/notes.txt", 1700000005) == FLINT_ST_OK &&
              !strncmp(fl_back(&F, 0), "/flint-home/notes.txt: 24 bytes, modified ", 42),
          "flint: stat: %s", fl_back(&F, 0));
    CHECK(flint_run(&F, "stat logs", 1700000005) == FLINT_ST_OK &&
              !strncmp(fl_back(&F, 0), "/flint-home/logs: folder, modified ", 35),
          "flint: stat of a folder: %s", fl_back(&F, 0));
    uint32_t before = flint_lines(&F);
    CHECK(flint_run(&F, "cat logs/big.txt", 1700000006) == FLINT_ST_OK &&
              flint_lines(&F) == before + 1u + 656u + 1u &&
              !strcmp(fl_back(&F, 0),
                      "flint: cat: /flint-home/logs/big.txt: showing its first 65536 bytes") &&
              strlen(fl_back(&F, 1)) == 36,
          "flint: a large file is shown in parts up to 65536 bytes (%u lines)",
          flint_lines(&F) - before);
    CHECK(flint_run(&F, "cd notes.txt", 1700000007) == FLINT_ST_REFUSED &&
              !strcmp(fl_back(&F, 0), "flint: cd: not a folder") && !strcmp(F.cwd, "flint-home"),
          "flint: cd into a file is refused");
    CHECK(flint_run(&F, "cat no-such.txt", 1700000008) == FLINT_ST_REFUSED &&
              !strncmp(fl_back(&F, 0), "flint: cat: ", 12) && strlen(fl_back(&F, 0)) > 12,
          "flint: a missing file is zxvd's refusal: %s", fl_back(&F, 0));
    CHECK(flint_run(&F, "cd ..", 1700000009) == FLINT_ST_OK && !strcmp(F.cwd, "") &&
              flint_run(&F, "cd no-such-folder", 1700000009) == FLINT_ST_REFUSED &&
              !strcmp(F.cwd, ""),
          "flint: cd .. to the top, and a folder that is not there");
    CHECK(flint_run(&F, "cd /flint-home/logs", 1700000010) == FLINT_ST_OK &&
              !strcmp(F.cwd, "flint-home/logs") && flint_run(&F, "cd", 1700000010) == FLINT_ST_OK &&
              !strcmp(F.cwd, ""),
          "flint: cd to a whole path, and cd alone back to /");

    /* the machine's facts */
    CHECK(flint_run(&F, "df", 1700000011) == FLINT_ST_OK &&
              !strncmp(fl_back(&F, 0), "the Hoard's volume: ", 20) &&
              strchr(fl_back(&F, 0), '%') != NULL,
          "flint: df: %s", fl_back(&F, 0));
    CHECK(flint_run(&F, "free", 1700000012) == FLINT_ST_OK &&
              !strncmp(fl_back(&F, 0), "memory: ", 8),
          "flint: free: %s", fl_back(&F, 0));
    CHECK(flint_run(&F, "uname", 1700000013) == FLINT_ST_OK && fl_back(&F, 0)[0] != 0 &&
              strncmp(fl_back(&F, 0), "flint:", 6),
          "flint: uname: %s", fl_back(&F, 0));
    int ns = flint_run(&F, "net", 1700000014);
    CHECK(ns == FLINT_ST_OK ||
              (ns == FLINT_ST_REFUSED && !strcmp(fl_back(&F, 0), "flint: net: this platform's "
                                                                 "interfaces are not listed")),
          "flint: net: %s", fl_back(&F, 0));

    /* misuse and unknown commands, as sh counts them */
    CHECK(flint_run(&F, "echo 'a  b'   c", 1700000015) == FLINT_ST_OK &&
              !strcmp(fl_back(&F, 0), "a  b c"),
          "flint: echo");
    CHECK(flint_run(&F, "ls | wc", 1700000016) == FLINT_ST_MISUSE &&
              !strcmp(fl_back(&F, 0),
                      "flint: pipes, redirection, lists and jobs run in zxsh, not here"),
          "flint: a pipe is refused as misuse");
    CHECK(flint_run(&F, "vi notes.txt", 1700000017) == FLINT_ST_NOCMD &&
              !strcmp(fl_back(&F, 0), "flint: vi: no such command (programs run in zxsh)"),
          "flint: an unknown command is 127");
    CHECK(flint_run(&F, "cat", 1700000018) == FLINT_ST_MISUSE &&
              flint_run(&F, "echo \"open", 1700000019) == FLINT_ST_MISUSE &&
              flint_run(&F, "pwd x", 1700000020) == FLINT_ST_MISUSE,
          "flint: wrong arguments and an open quote are misuse");

    /* the history: every command, with its status; a blank line is not one */
    uint32_t nh = flint_history_count(&F);
    CHECK(flint_run(&F, "   ", 1700000021) == FLINT_ST_OK && flint_history_count(&F) == nh,
          "flint: a blank line is not recorded");
    const flint_hist_t *h0 = flint_history_at(&F, 0), *hl = flint_history_at(&F, nh - 1u);
    CHECK(nh == 24 && h0 && h0->seq == 1 && !strcmp(h0->text, "pwd") && h0->status == 0 &&
              h0->stamp == 1700000000 && hl && hl->seq == 24 && !strcmp(hl->text, "pwd x") &&
              hl->status == FLINT_ST_MISUSE && hl->len == 5 && hl->stamp == 1700000020 &&
              flint_history_at(&F, 20)->status == FLINT_ST_NOCMD && !flint_history_at(&F, nh),
          "flint: %u commands recorded", nh);
    CHECK(flint_run(&F, "history", 1700000022) == FLINT_ST_OK &&
              !strcmp(fl_back(&F, 0), "  24  pwd x") &&
              !strcmp(fl_back(&F, 1), "  23  echo \"open"),
          "flint: history lists them: %s", fl_back(&F, 0));
    CHECK(flint_run(&F, "pwd", -1) == ZXM_E_ARG && flint_history_count(&F) == 25,
          "flint: a time before 1970 is refused, nothing recorded");
    F.next_seq = FLINT_SEQ_MAX + 1;
    uint32_t nl = flint_lines(&F);
    CHECK(flint_run(&F, "pwd", 1700000023) == ZXM_E_STATE && flint_lines(&F) == nl &&
              flint_history_count(&F) == 25,
          "flint: a command whose number does not fit i9 runs nothing");
    F.next_seq = FLINT_SEQ_MAX;
    CHECK(flint_run(&F, "pwd", 1700000024) == FLINT_ST_OK &&
              flint_history_at(&F, 25)->seq == FLINT_SEQ_MAX,
          "flint: the last number that fits");
    F.next_seq = 26;
    for (uint32_t i = 0; i < FLINT_HISTORY + 10u; i++) (void) flint_run(&F, "echo x", 1700000100);
    CHECK(flint_history_count(&F) == FLINT_HISTORY &&
              flint_history_at(&F, FLINT_HISTORY - 1u)->seq == 26 + FLINT_HISTORY + 9u,
          "flint: the history keeps the newest %u", FLINT_HISTORY);

    /* the scrollback: the newest lines kept inside both limits */
    CHECK(flint_set_limits(&F, 24, 1048576) == ZXM_OK && flint_lines(&F) == 24 &&
              !strcmp(fl_back(&F, 0), "x"),
          "flint: down to 24 lines, the newest kept");
    flint_clear(&F);
    CHECK(flint_lines(&F) == 0 && flint_bytes(&F) == 0, "flint: cleared");
    static char wide[2000];
    memset(wide, 'w', sizeof wide);
    for (uint32_t i = 0; i < 10; i++) flint_print(&F, wide, 1000);
    CHECK(flint_set_limits(&F, 24, 4096) == ZXM_OK && flint_lines(&F) == 4 &&
              flint_bytes(&F) == 4000,
          "flint: 4096 bytes hold four 1000-byte lines");
    flint_print(&F, wide, 99);
    CHECK(flint_lines(&F) == 4 && flint_bytes(&F) == 3099 && strlen(fl_back(&F, 0)) == 99,
          "flint: a new line drops the oldest until it fits");
    wide[FLINT_LINE_MAX - 1u] = (char) 0xc3; /* a two-byte character across the cut */
    wide[FLINT_LINE_MAX] = (char) 0xa9;
    flint_clear(&F);
    flint_print(&F, wide, sizeof wide);
    CHECK(flint_lines(&F) == 1 && strlen(fl_back(&F, 0)) == FLINT_LINE_MAX - 1u,
          "flint: a long line is cut on a UTF-8 boundary");
    CHECK(flint_set_limits(&F, 10000, 1048576) == ZXM_OK, "flint: the widest limits");
    for (uint32_t i = 0; i < 1100; i++) flint_print(&F, wide, FLINT_LINE_MAX - 1u);
    CHECK(flint_lines(&F) == 1048576u / (FLINT_LINE_MAX - 1u) && flint_bytes(&F) <= 1048576u,
          "flint: a full byte ring wraps (%u lines)", flint_lines(&F));
    F.rows = 67;
    CHECK(flint_top(&F) == flint_lines(&F) - 67u, "flint: the screen shows the newest 67");
    flint_clear(&F);
    flint_print(&F, "a\nb", 3);
    CHECK(flint_top(&F) == 0 && flint_lines(&F) == 2 && !strcmp(fl_back(&F, 0), "b"),
          "flint: fewer lines than rows start at the top");
    char tiny[2];
    CHECK(flint_line(&F, 0, tiny, sizeof tiny) == 1 && flint_line(&F, 0, tiny, 1) == -1 &&
              flint_line(&F, 2, tiny, sizeof tiny) == -1,
          "flint: a line that does not fit, or is not there");
    CHECK(flint_run(&F, "clear", 1700000200) == FLINT_ST_OK && flint_lines(&F) == 0,
          "flint: clear empties the scrollback");
}
