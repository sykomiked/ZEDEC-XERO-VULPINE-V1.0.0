/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_mux.c — sub-terminals (zxsh_mux.h): the VT emulator, the layout
 * tree, rule C, and "together" without a terminal (with /bin/sh standing in
 * for zxsh, since commands run as `self -c CMD`). */
#define _DEFAULT_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "zxsh_core.h"
#include "zxsh_mux.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);                                \
        }                                                                                          \
    } while (0)

static void feed(zvt_t *v, const char *s)
{
    zvt_feed(v, s, strlen(s));
}

static char ch(const zvt_t *v, int r, int c)
{
    const zvt_cell_t *x = zvt_cell(v, r, c);
    return x ? (char) x->ch[0] : 0;
}

/* Row r as text (single-byte cells), trailing blanks trimmed. */
static const char *row(const zvt_t *v, int r)
{
    static char b[512];
    int n = 0;
    for (int c = 0; c < v->cols && n < 511; c++) b[n++] = ch(v, r, c);
    while (n && b[n - 1] == ' ') n--;
    b[n] = 0;
    return b;
}

static void test_vt_text(void)
{
    zvt_t v;
    CHECK(zvt_init(&v, 4, 10) == 0, "vt init");
    feed(&v, "hello");
    CHECK(!strcmp(row(&v, 0), "hello") && v.cx == 5 && v.cy == 0, "plain text");
    feed(&v, "\r\nab\tc");
    CHECK(!strcmp(row(&v, 1), "ab      c") && v.cx == 9, "CR LF TAB");
    feed(&v, "\b\bX\a");
    CHECK(!strcmp(row(&v, 1), "ab     Xc") && v.cx == 8, "BS BEL");
    zvt_free(&v);

    zvt_init(&v, 3, 5);
    feed(&v, "abcdefg");
    CHECK(!strcmp(row(&v, 0), "abcde") && !strcmp(row(&v, 1), "fg"), "autowrap");
    feed(&v, "\x1b[3;1Hxyzuv");
    CHECK(v.wrapnext && v.cy == 2 && v.cx == 4, "pending wrap at last column");
    feed(&v, "\r\n");
    CHECK(!strcmp(row(&v, 0), "fg") && !strcmp(row(&v, 1), "xyzuv") && !strcmp(row(&v, 2), ""),
          "LF at bottom scrolls, no extra line after a full row");
    feed(&v, "\x1b[?7l\x1b[1;1H1234567");
    CHECK(!strcmp(row(&v, 0), "12347"), "no autowrap overwrites the last column");
    feed(&v, "\x1b[?7h");
    zvt_free(&v);
}

static void test_vt_moves(void)
{
    zvt_t v;
    zvt_init(&v, 10, 20);
    feed(&v, "\x1b[5;7H");
    CHECK(v.cy == 4 && v.cx == 6, "CUP");
    feed(&v, "\x1b[2A\x1b[3C");
    CHECK(v.cy == 2 && v.cx == 9, "CUU CUF");
    feed(&v, "\x1b[B\x1b[4D");
    CHECK(v.cy == 3 && v.cx == 5, "CUD CUB default 1");
    feed(&v, "\x1b[2E");
    CHECK(v.cy == 5 && v.cx == 0, "CNL");
    feed(&v, "\x1b[3;9H\x1b[F");
    CHECK(v.cy == 1 && v.cx == 0, "CPL");
    feed(&v, "\x1b[12G\x1b[7d");
    CHECK(v.cx == 11 && v.cy == 6, "CHA VPA");
    feed(&v, "\x1b[999;999f");
    CHECK(v.cy == 9 && v.cx == 19, "HVP clamps");
    feed(&v, "\x1b[H\x1b[99A\x1b[99D");
    CHECK(v.cy == 0 && v.cx == 0, "moves clamp at home");
    feed(&v, "\x1b[4;4H\x1b"
             "7\x1b[1;1H\x1b"
             "8");
    CHECK(v.cy == 3 && v.cx == 3, "ESC 7 / ESC 8");
    feed(&v, "\x1b[6;6H\x1b[s\x1b[H\x1b[u");
    CHECK(v.cy == 5 && v.cx == 5, "CSI s / u");
    feed(&v, "\x1b[H\x1bM");
    CHECK(v.cy == 0, "RI at top");
    feed(&v, "\x1b[2;3H\x1b"
             "E");
    CHECK(v.cy == 2 && v.cx == 0, "NEL");
    feed(&v, "\x1b[5;5H\x1b"
             "D");
    CHECK(v.cy == 5 && v.cx == 4, "IND");
    zvt_free(&v);
}

static void test_vt_erase_scroll(void)
{
    zvt_t v;
    zvt_init(&v, 5, 6);
    feed(&v, "aaaaaa\r\nbbbbbb\r\ncccccc\r\ndddddd\r\neeeeee");
    feed(&v, "\x1b[3;3H\x1b[K");
    CHECK(!strcmp(row(&v, 2), "cc"), "EL 0");
    feed(&v, "\x1b[2;3H\x1b[1K");
    CHECK(!strcmp(row(&v, 1), "   bbb"), "EL 1");
    feed(&v, "\x1b[4;1H\x1b[2K");
    CHECK(!strcmp(row(&v, 3), ""), "EL 2");
    feed(&v, "\x1b[1;4H\x1b[1J");
    CHECK(!strcmp(row(&v, 0), "    aa") && !strcmp(row(&v, 1), "   bbb"), "ED 1");
    feed(&v, "\x1b[5;2H\x1b[J");
    CHECK(!strcmp(row(&v, 4), "e"), "ED 0");
    feed(&v, "\x1b[2J");
    int empty = 1;
    for (int r = 0; r < 5; r++) empty &= !strcmp(row(&v, r), "");
    CHECK(empty, "ED 2");

    feed(&v, "\x1b[H111111\r\n222222\r\n333333\r\n444444\r\n555555");
    feed(&v, "\x1b[2;4r");
    CHECK(v.top == 1 && v.bot == 3 && v.cy == 0 && v.cx == 0, "DECSTBM homes");
    feed(&v, "\x1b[4;1H\n");
    CHECK(!strcmp(row(&v, 0), "111111") && !strcmp(row(&v, 1), "333333") &&
              !strcmp(row(&v, 2), "444444") && !strcmp(row(&v, 3), "") &&
              !strcmp(row(&v, 4), "555555"),
          "LF scrolls only the region");
    feed(&v, "\x1b[2;1H\x1bM");
    CHECK(!strcmp(row(&v, 1), "") && !strcmp(row(&v, 2), "333333") &&
              !strcmp(row(&v, 3), "444444") && !strcmp(row(&v, 4), "555555"),
          "RI scrolls the region down");
    feed(&v, "\x1b[2S");
    CHECK(!strcmp(row(&v, 1), "444444") && !strcmp(row(&v, 2), "") && !strcmp(row(&v, 3), ""),
          "SU in region");
    feed(&v, "\x1b[T");
    CHECK(!strcmp(row(&v, 1), "") && !strcmp(row(&v, 2), "444444"), "SD in region");
    feed(&v, "\x1b[r\x1b[H\x1b[2J0123\r\nabcd\r\nwxyz");
    feed(&v, "\x1b[2;1H\x1b[L");
    CHECK(!strcmp(row(&v, 1), "") && !strcmp(row(&v, 2), "abcd") && !strcmp(row(&v, 3), "wxyz"),
          "IL");
    feed(&v, "\x1b[2M");
    CHECK(!strcmp(row(&v, 1), "wxyz") && !strcmp(row(&v, 2), ""), "DL");
    feed(&v, "\x1b[1;2H\x1b[2P");
    CHECK(!strcmp(row(&v, 0), "03"), "DCH");
    feed(&v, "\x1b[1;2H\x1b[3@");
    CHECK(!strcmp(row(&v, 0), "0   3"), "ICH");
    feed(&v, "\x1b[1;1H\x1b[99X");
    CHECK(!strcmp(row(&v, 0), ""), "ECH clamps");
    feed(&v, "\x1b[1;6H\x1b[99@\x1b[99P\x1b[99L\x1b[99M");
    CHECK(v.cx == 0 && v.cy == 0 && !strcmp(row(&v, 0), ""), "huge counts stay in bounds");
    zvt_free(&v);
}

static void test_vt_sgr(void)
{
    zvt_t v;
    zvt_init(&v, 3, 20);
    feed(&v, "\x1b[1;3;4;7;31;42mA\x1b[22;23;24;27;39;49mB\x1b[2;91;103mC");
    const zvt_cell_t *a = zvt_cell(&v, 0, 0), *b = zvt_cell(&v, 0, 1), *c = zvt_cell(&v, 0, 2);
    CHECK(a->attr == (ZVT_BOLD | ZVT_ITALIC | ZVT_UNDERLINE | ZVT_REVERSE) &&
              a->fg == (ZVT_IDX | 1) && a->bg == (ZVT_IDX | 2),
          "SGR attributes and 8 colours");
    CHECK(b->attr == 0 && b->fg == 0 && b->bg == 0, "SGR resets 22 23 24 27 39 49");
    CHECK(c->attr == ZVT_DIM && c->fg == (ZVT_IDX | 9) && c->bg == (ZVT_IDX | 11),
          "SGR 2 90s 100s");
    feed(&v, "\x1b[0;38;5;208;48;5;17mD\x1b[38;2;242;194;48;48;2;1;2;3mE\x1b[mF");
    const zvt_cell_t *d = zvt_cell(&v, 0, 3), *e = zvt_cell(&v, 0, 4), *f = zvt_cell(&v, 0, 5);
    CHECK(d->fg == (ZVT_IDX | 208) && d->bg == (ZVT_IDX | 17), "SGR 256 colours");
    CHECK(e->fg == (ZVT_RGB | 0xF2C230) && e->bg == (ZVT_RGB | 0x010203), "SGR truecolor");
    CHECK(f->fg == 0 && f->bg == 0 && f->attr == 0, "SGR empty resets");
    feed(&v, "\x1b[38;5mG\x1b[38;2;1mH\x1b[1mI");
    CHECK(zvt_cell(&v, 0, 6)->fg == 0 && zvt_cell(&v, 0, 8)->attr == ZVT_BOLD,
          "malformed extended colour ignored");
    zvt_free(&v);
}

static void test_vt_modes(void)
{
    zvt_t v;
    zvt_init(&v, 4, 10);
    feed(&v, "main\x1b[2;3H");
    feed(&v, "\x1b[?1049h");
    CHECK(v.scr == 1 && !strcmp(row(&v, 0), ""), "alt screen is clear");
    feed(&v, "\x1b[Halt text\x1b[4;1Hmore");
    feed(&v, "\x1b[?1049l");
    CHECK(v.scr == 0 && !strcmp(row(&v, 0), "main") && !strcmp(row(&v, 3), "") && v.cy == 1 &&
              v.cx == 2,
          "alt screen exit restores main screen and cursor");
    feed(&v, "\x1b[?47hX\x1b[?47l\x1b[?1047hY\x1b[?1047l");
    CHECK(v.scr == 0 && !strcmp(row(&v, 0), "main"), "?47 ?1047");
    feed(&v, "\x1b[?25l");
    CHECK(!v.cursor_on, "cursor hidden");
    feed(&v, "\x1b[?25h\x1b[?1000h\x1b[?2004h\x1b[>c\x1b=\x1b>\x1b(B\x1b)0");
    CHECK(v.cursor_on && !strcmp(row(&v, 0), "main"), "other modes ignored safely");
    feed(&v, "\x1b]0;first\a");
    CHECK(!strcmp(v.title, "first"), "OSC 0 title (BEL)");
    feed(&v, "\x1b]2;second\x1b\\");
    CHECK(!strcmp(v.title, "second"), "OSC 2 title (ST)");
    CHECK(v.tag == -1, "no tag yet");
    feed(&v, "\x1b]7770;55\a");
    CHECK(v.tag == 55, "OSC 7770 sets the tag");
    feed(&v, "\x1b]7770;64\a\x1b]7770;x\a\x1b]7770;\a");
    CHECK(v.tag == 55, "bad OSC 7770 ignored");
    feed(&v, "\x1bPq#0;1;2\x1b\\\x1b]8;;http://x\a");
    CHECK(!strcmp(row(&v, 0), "main"), "DCS and unknown OSC consumed");
    feed(&v, "\x1b[3;5H\x1b[6n");
    CHECK(v.replen == 6 && !memcmp(v.reply, "\x1b[3;5R", 6), "DSR reply");
    v.replen = 0;
    feed(&v, "\x1b[c");
    CHECK(v.replen == 7 && !memcmp(v.reply, "\x1b[?1;2c", 7), "DA reply");
    v.replen = 0;
    feed(&v, "\x1b"
             "c");
    CHECK(!strcmp(row(&v, 0), "") && v.cx == 0 && v.cy == 0, "ESC c resets");
    zvt_free(&v);
}

static void test_vt_utf8_resize(void)
{
    zvt_t v;
    zvt_init(&v, 3, 6);
    feed(&v, "a\xc3\xa9\xe2\x94\x82\xf0\x9f\x99\x82z");
    const zvt_cell_t *c1 = zvt_cell(&v, 0, 1), *c2 = zvt_cell(&v, 0, 2), *c3 = zvt_cell(&v, 0, 3);
    CHECK(c1->len == 2 && !memcmp(c1->ch, "\xc3\xa9", 2), "UTF-8 two bytes");
    CHECK(c2->len == 3 && !memcmp(c2->ch, "\xe2\x94\x82", 3), "UTF-8 three bytes");
    CHECK(c3->len == 4 && !memcmp(c3->ch, "\xf0\x9f\x99\x82", 4), "UTF-8 four bytes");
    CHECK(ch(&v, 0, 4) == 'z' && v.cx == 5, "one column per character");
    feed(&v, "\r\n\xc3(\xff");
    CHECK(!strcmp(row(&v, 1), "?(?"), "invalid UTF-8 becomes ?");
    CHECK(zvt_cell(&v, 3, 0) == NULL && zvt_cell(&v, 0, 6) == NULL && zvt_cell(&v, -1, 0) == NULL,
          "cell access out of range is NULL");
    zvt_free(&v);

    zvt_init(&v, 4, 8);
    feed(&v, "l1\r\nl2\r\nl3\r\nl4");
    zvt_resize(&v, 2, 4);
    CHECK(v.rows == 2 && v.cols == 4 && !strcmp(row(&v, 0), "l3") && !strcmp(row(&v, 1), "l4") &&
              v.cy == 1,
          "shrink keeps the cursor's rows");
    zvt_resize(&v, 5, 10);
    CHECK(!strcmp(row(&v, 0), "l3") && !strcmp(row(&v, 4), "") && v.bot == 4, "grow");
    feed(&v, "\x1b[5;10Hx");
    CHECK(ch(&v, 4, 9) == 'x', "write after resize");
    zvt_resize(&v, 0, -3);
    CHECK(v.rows == 1 && v.cols == 1, "resize clamps to 1x1");
    feed(&v, "abc\r\n\x1b[5;5Hq\x1b[?1049hzz\x1b[2Jww");
    CHECK(v.cx == 0 && v.cy == 0, "1x1 terminal stays in bounds");
    zvt_free(&v);
}

static void test_vt_fuzz(void)
{
    static const int sizes[][2] = {{1, 1}, {1, 7}, {3, 2}, {24, 80}, {5, 40}, {60, 3}};
    static const char *const seeds[] = {"\x1b[",
                                        "\x1b]",
                                        "\x1b[?1049h",
                                        "\x1b[?1049l",
                                        "\x1b[2;3r",
                                        "\x1b[99;99H",
                                        "\x1b[38;2;",
                                        "\x1bP",
                                        "\r\n",
                                        "\xe2\x94",
                                        "\x1b"
                                        "7",
                                        "\x1b"
                                        "8",
                                        "\x1b[6n",
                                        "\x1b]0;",
                                        "\a",
                                        "\x1b\\"};
    uint32_t s = 12345;
    int ok = 1;
    for (int z = 0; z < 6; z++) {
        zvt_t v;
        zvt_init(&v, sizes[z][0], sizes[z][1]);
        char buf[512];
        for (int round = 0; round < 400; round++) {
            size_t n = 0;
            while (n < sizeof buf - 16) {
                s = s * 1103515245u + 12345u;
                uint32_t r = s >> 8;
                if (r % 5 == 0) {
                    const char *x = seeds[(r >> 4) % 16];
                    size_t L = strlen(x);
                    memcpy(buf + n, x, L);
                    n += L;
                } else if (r % 5 == 1)
                    buf[n++] = (char) ('0' + (r >> 5) % 10);
                else
                    buf[n++] = (char) (r >> 12);
            }
            zvt_feed(&v, buf, n);
            v.replen = 0;
            if (round % 50 == 49) zvt_resize(&v, 1 + (int) (s % 30), 1 + (int) ((s >> 9) % 90));
            ok &= v.cx >= 0 && v.cx < v.cols && v.cy >= 0 && v.cy < v.rows && v.top >= 0 &&
                  v.bot < v.rows && v.top <= v.bot;
        }
        zvt_free(&v);
    }
    CHECK(ok, "fuzz: 6 sizes x ~200k bytes each, cursor and region stay in bounds");
}

static void test_layout(void)
{
    zmux_tree_t t;
    zmux_tree_init(&t);
    int ids[ZMUX_MAXP], nb = 0;
    zmux_rect_t r[ZMUX_MAXP], b[ZMUX_MAXP], area = {0, 0, 80, 23};
    CHECK(zmux_tree_rects(&t, area, ids, r, ZMUX_MAXP, b, &nb) == 0, "empty tree");
    CHECK(zmux_tree_add(&t, -1, 'h', 7) == 0, "first pane is the root");
    CHECK(zmux_tree_add(&t, 7, 'h', 8) == 0 && zmux_tree_add(&t, 8, 'v', 9) == 0, "splits");
    CHECK(zmux_tree_add(&t, 42, 'h', 10) == -1, "split of a missing pane refused");
    int n = zmux_tree_rects(&t, area, ids, r, ZMUX_MAXP, b, &nb);
    CHECK(n == 3 && nb == 2 && ids[0] == 7 && ids[1] == 8 && ids[2] == 9, "leaf order");
    CHECK(r[0].x == 0 && r[0].w == 39 && r[0].h == 23 && b[0].x == 39 && b[0].w == 1 &&
              r[1].x == 40 && r[1].w == 40 && r[1].h == 11 && b[1].y == 11 && r[2].y == 12 &&
              r[2].h == 11,
          "split rectangles");
    CHECK(zmux_tree_remove(&t, 8) == 2, "remove a pane");
    n = zmux_tree_rects(&t, area, ids, r, ZMUX_MAXP, b, &nb);
    CHECK(n == 2 && ids[1] == 9 && r[1].h == 23 && r[1].x == 40, "sibling takes the space");
    CHECK(zmux_tree_remove(&t, 8) == -1, "remove twice refused");
    CHECK(zmux_tree_remove(&t, 7) == 1 && zmux_tree_remove(&t, 9) == 0 && t.root == -1,
          "remove to empty");

    zmux_tree_add(&t, -1, 'h', 0);
    int full = 1;
    for (int i = 1; i < ZMUX_MAXP; i++) full &= zmux_tree_add(&t, i - 1, i % 2 ? 'h' : 'v', i) == 0;
    CHECK(full && zmux_tree_add(&t, 3, 'h', 99) == -1, "16 panes fit, the 17th is refused");

    /* coverage: panes and borders cover every cell exactly once */
    int ok = 1;
    static unsigned char cov[256 * 256];
    for (int tiles = 1; tiles <= ZMUX_MAXP; tiles++) {
        int p[ZMUX_MAXP];
        for (int i = 0; i < tiles; i++) p[i] = 100 + i;
        zmux_tree_t tt;
        zmux_tree_tile(&tt, p, tiles);
        for (int h = 0; h < 60; h += 3)
            for (int w = 0; w < 200; w += 7) {
                zmux_rect_t a = {5, 2, w, h};
                memset(cov, 0, sizeof cov);
                n = zmux_tree_rects(&tt, a, ids, r, ZMUX_MAXP, b, &nb);
                ok &= n == tiles && nb == tiles - 1;
                for (int i = 0; i < n + nb; i++) {
                    zmux_rect_t q = i < n ? r[i] : b[i - n];
                    ok &= q.w >= 0 && q.h >= 0;
                    for (int y = q.y; y < q.y + q.h; y++)
                        for (int x = q.x; x < q.x + q.w; x++) {
                            ok &= x >= a.x && x < a.x + w && y >= a.y && y < a.y + h;
                            if (x >= 0 && x < 256 && y >= 0 && y < 256) cov[y * 256 + x]++;
                        }
                }
                for (int y = a.y; y < a.y + h; y++)
                    for (int x = a.x; x < a.x + w; x++) ok &= cov[y * 256 + x] == 1;
                if (tiles == 4 && w == 161 && h == 51)
                    ok &= r[0].w == 80 && r[2].w == 80 && r[0].h == 25 && r[1].h == 25;
            }
    }
    CHECK(ok, "tiled 1..16 panes cover many sizes exactly once (2x2 grid for 4)");

    /* random splits and removes keep exact coverage */
    zmux_tree_init(&t);
    int live[ZMUX_MAXP], nl = 0, next = 0;
    uint32_t s = 99;
    ok = 1;
    for (int step = 0; step < 3000; step++) {
        s = s * 1103515245u + 12345u;
        if (nl == 0 || (nl < ZMUX_MAXP && (s >> 16) % 3)) {
            int at = nl ? live[(s >> 8) % (uint32_t) nl] : -1;
            if (zmux_tree_add(&t, at, (s >> 4) & 1 ? 'h' : 'v', next) == 0) live[nl++] = next++;
        } else {
            int k = (int) ((s >> 8) % (uint32_t) nl);
            ok &= zmux_tree_remove(&t, live[k]) == nl - 1;
            live[k] = live[--nl];
        }
        zmux_rect_t a = {0, 0, 1 + (int) (s % 150), 1 + (int) ((s >> 3) % 50)};
        memset(cov, 0, sizeof cov);
        n = zmux_tree_rects(&t, a, ids, r, ZMUX_MAXP, b, &nb);
        ok &= n == nl;
        for (int i = 0; i < n + nb; i++) {
            zmux_rect_t q = i < n ? r[i] : b[i - n];
            for (int y = q.y; y < q.y + q.h; y++)
                for (int x = q.x; x < q.x + q.w; x++) cov[y * 256 + x]++;
        }
        for (int y = 0; y < a.h && nl; y++)
            for (int x = 0; x < a.w; x++) ok &= cov[y * 256 + x] == 1;
    }
    CHECK(ok, "3000 random splits/removes keep exact coverage");
}

static void test_rule_c(void)
{
    lpx_t T = zxsh_tag(LPX_B_TRUE, LPX_P_NEUTRAL), F = zxsh_tag(LPX_B_FALSE, LPX_P_NEUTRAL),
          B = zxsh_tag(LPX_B_BOTH, LPX_P_NEUTRAL), N = zxsh_tag(LPX_B_NEITHER, LPX_P_NEUTRAL),
          X = zxsh_tag(LPX_B_TRUE, LPX_P_FORBIDDEN);
    CHECK(zmux_tag_from_status(0 << 8) == T, "exit 0 TRUE");
    CHECK(zmux_tag_from_status(1 << 8) == F, "exit 1 FALSE");
    CHECK(zmux_tag_from_status(2 << 8) == N, "exit 2 NEITHER");
    CHECK(zmux_tag_from_status(3 << 8) == zxsh_tag(LPX_B_FALSE, LPX_P_FORBIDDEN),
          "exit 3 FALSE/FORBIDDEN");
    CHECK(zmux_tag_from_status(4 << 8) == B, "exit 4 BOTH");
    CHECK(zmux_tag_from_status(5 << 8) == N, "exit 5 NEITHER");
    CHECK(zmux_tag_from_status(42 << 8) == F && zmux_tag_from_status(127 << 8) == F,
          "other codes FALSE");
    int crash[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGSYS, SIGTRAP}, ok = 1;
    for (int i = 0; i < 7; i++)
        ok &= zmux_tag_from_status(crash[i]) == zxsh_tag(LPX_B_FALSE, LPX_P_FORBIDDEN);
    CHECK(ok, "crash signals FALSE/FORBIDDEN");
    CHECK(zmux_tag_from_status(SIGTERM) == zxsh_tag(LPX_B_NEITHER, LPX_P_ENTROPIC) &&
              zmux_tag_from_status(SIGHUP) == zxsh_tag(LPX_B_NEITHER, LPX_P_ENTROPIC),
          "other signals NEITHER/entropic");

    lpx_t a[4] = {T, T, T, T};
    CHECK(zmux_combine(a, 0) == N, "combine of none");
    CHECK(zmux_combine(a, 1) == T, "one witness: neutral");
    CHECK(zmux_combine(a, 3) == zxsh_tag(LPX_B_TRUE, LPX_P_NEGENTROPIC), "agreeing witnesses");
    lpx_t m[2] = {T, F}, nn[2] = {N, zxsh_tag(LPX_B_NEITHER, LPX_P_ENTROPIC)}, x[3] = {T, X, B};
    CHECK(zmux_combine(m, 2) == zxsh_tag(LPX_B_BOTH, LPX_P_ENTROPIC), "disagreement is BOTH");
    CHECK(zmux_combine(nn, 2) == zxsh_tag(LPX_B_NEITHER, LPX_P_NEGENTROPIC),
          "bases agree whatever their polarity");
    CHECK(zmux_combine(x, 3) == zxsh_tag(LPX_B_FALSE, LPX_P_FORBIDDEN), "forbidden absorbs");
    lpx_t bb[2] = {B, B};
    CHECK(zmux_combine(bb, 2) == zxsh_tag(LPX_B_BOTH, LPX_P_NEGENTROPIC), "two BOTH agree");
    /* full table over the four bases, pairs */
    ok = 1;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            lpx_t p[2] = {zxsh_tag((uint8_t) i, LPX_P_NEUTRAL),
                          zxsh_tag((uint8_t) j, LPX_P_NEUTRAL)};
            lpx_t c = zmux_combine(p, 2);
            ok &= i == j ? c == zxsh_tag((uint8_t) i, LPX_P_NEGENTROPIC)
                         : c == zxsh_tag(LPX_B_BOTH, LPX_P_ENTROPIC);
        }
    CHECK(ok, "pair table");
}

/* Run together with stdout captured into a file. */
static lpx_t together(const char *const *cmds, int n, char *out, size_t max)
{
    char path[] = "/tmp/zxsh_mux_testXXXXXX";
    int fd = mkstemp(path), saved = dup(1);
    unlink(path);
    fflush(stdout);
    dup2(fd, 1);
    zmux_cfg_t cfg = {"/bin/sh", "/tmp"};
    char why[128];
    lpx_t t = zmux_request(&cfg, "together", 0, cmds, n, why, sizeof why);
    dup2(saved, 1);
    close(saved);
    lseek(fd, 0, SEEK_SET);
    ssize_t r = read(fd, out, max - 1);
    out[r > 0 ? r : 0] = 0;
    close(fd);
    return t;
}

static void test_together(void)
{
    char out[8192], why[128];
    unsetenv("ZXSH_MUX_SOCK");
    if (isatty(0) && isatty(1)) {
        printf("(stdin/stdout are a terminal: running the no-terminal tests with </dev/null)\n");
        int z = open("/dev/null", O_RDONLY);
        dup2(z, 0);
        close(z);
    }
    const char *c1[] = {"exit 0", "exit 1"};
    CHECK(together(c1, 2, out, sizeof out) == zxsh_tag(LPX_B_BOTH, LPX_P_ENTROPIC),
          "together exit 0 + exit 1 is BOTH/entropic");
    CHECK(strstr(out, "[1] T  exit 0\n") && strstr(out, "[2] F  exit 1\n"), "summary lines");
    const char *c2[] = {"exit 0", "exit 0"};
    CHECK(together(c2, 2, out, sizeof out) == zxsh_tag(LPX_B_TRUE, LPX_P_NEGENTROPIC),
          "together of two TRUE is TRUE/negentropic");
    const char *c3[] = {"echo fine", "kill -SEGV $$"};
    CHECK(together(c3, 2, out, sizeof out) == zxsh_tag(LPX_B_FALSE, LPX_P_FORBIDDEN),
          "a SIGSEGV makes it forbidden");
    CHECK(strstr(out, "[2] !  kill -SEGV $$\n") != NULL, "forbidden letter in summary");
    const char *c4[] = {"echo one; echo two", "echo err >&2; printf 'no newline'",
                        "sleep 0.2; echo late"};
    CHECK(together(c4, 3, out, sizeof out) == zxsh_tag(LPX_B_TRUE, LPX_P_NEGENTROPIC),
          "three TRUE");
    CHECK(strstr(out, "[1] one\n[1] two\n") ||
              (strstr(out, "[1] one\n") && strstr(out, "[1] two\n")),
          "stdout lines prefixed");
    CHECK(strstr(out, "[2] err\n") && strstr(out, "[2] no newline\n") && strstr(out, "[3] late\n"),
          "stderr and an unterminated line prefixed");
    CHECK(strstr(out, "[3] late") < strstr(out, "[3] T  sleep"), "summary comes last");
    const char *c5[] = {"exit 4"};
    CHECK(together(c5, 1, out, sizeof out) == zxsh_tag(LPX_B_BOTH, LPX_P_NEUTRAL),
          "one command: its own tag, neutral");
    zmux_cfg_t cfg = {"/bin/sh", "/tmp"};
    CHECK(zmux_request(&cfg, "split", 'h', NULL, 0, why, sizeof why) ==
                  zxsh_tag(LPX_B_NEITHER, LPX_P_NEUTRAL) &&
              !strcmp(why, "sub-terminals need a terminal"),
          "split without a terminal is NEITHER");
    CHECK(zmux_request(&cfg, "together", 0, NULL, 0, why, sizeof why) ==
              zxsh_tag(LPX_B_NEITHER, LPX_P_NEUTRAL),
          "together of nothing is NEITHER");
    CHECK(zmux_request(&cfg, "frobnicate", 0, NULL, 0, why, sizeof why) ==
              zxsh_tag(LPX_B_NEITHER, LPX_P_NEUTRAL),
          "unknown kind is NEITHER");
    setenv("ZXSH_MUX_SOCK", "/nonexistent/mux.sock", 1);
    CHECK(zmux_request(&cfg, "split", 'h', NULL, 0, why, sizeof why) ==
                  zxsh_tag(LPX_B_NEITHER, LPX_P_NEUTRAL) &&
              strstr(why, "not reachable"),
          "client with no multiplexer is NEITHER");
    unsetenv("ZXSH_MUX_SOCK");
}

int main(void)
{
    test_vt_text();
    test_vt_moves();
    test_vt_erase_scroll();
    test_vt_sgr();
    test_vt_modes();
    test_vt_utf8_resize();
    test_vt_fuzz();
    test_layout();
    test_rule_c();
    test_together();
    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
