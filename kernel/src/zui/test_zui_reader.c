/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_reader.c — host tests for the screen reader: what it says for
 * each kind of widget at each verbosity, focus announcements, the reader
 * cursor, held answers, error lines, live regions, menus and the palette,
 * the keys, and the speech engine adapter. */
#include <stdio.h>
#include <string.h>

#include "zui_progress.h"
#include "zui_reader.h"
#include "zui_tokens.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

/* ---- a speaker that remembers ------------------------------------------- */

#define HEARD_MAX 64
static struct {
    char text[ZUI_READER_UTTER];
    zui_say_t how;
} HEARD[HEARD_MAX];
static int n_heard;

static void hear(const char *text, zui_say_t how, void *ctx)
{
    (void) ctx;
    if (n_heard < HEARD_MAX) {
        snprintf(HEARD[n_heard].text, sizeof HEARD[n_heard].text, "%s", text);
        HEARD[n_heard].how = how;
    }
    n_heard++;
}

static void quiet(void)
{
    n_heard = 0;
}

static const char *heard(int i)
{
    return i >= 0 && i < n_heard && i < HEARD_MAX ? HEARD[i].text : "<nothing>";
}

static const char *last(void)
{
    return heard(n_heard - 1);
}

/* ---- the screen ------------------------------------------------------------ */

static zui_ui_t UI;
static zui_reader_t RD;

static const char *FILES[] = {"alpha.txt", "beta.png", "gamma", "delta.md", "epsilon",
                              "zeta",      "eta",      "theta", "iota",     "kappa",
                              "lambda",    "mu",       "nu",    "xi",       "omicron"};

static void row_fn(const zui_ui_t *ui, zui_id_t list, int32_t row, zui_row_t *out, void *ctx)
{
    (void) ui;
    (void) list;
    (void) ctx;
    snprintf(out->text, sizeof out->text, "%s", FILES[row % 15]);
    snprintf(out->detail, sizeof out->detail, "%d KB", row);
    out->muted = row == 6;
}

static const zui_palette_item_t ACTIONS[] = {
    {"Summarize", "companion", 300},
    {"Rewrite", "editor", 600},
    {"Suggest folders", "librarian", 120},
    {"Explain this reply", "companion", 200},
};

typedef struct {
    zui_id_t root, top, title, search, body, nav, list, side, heading, notes, err, held, save,
        cancel, del, check, status, sep, bar, pw;
    char search_buf[64], notes_buf[256], pw_buf[32];
} screen_t;
static screen_t S;
static zui_screen_t laptop = {1280, 800, 256, ZUI_INPUT_POINTER};
static uint32_t fb[1280 * 800];

static void build(void)
{
    zui_theme_t th;
    memset(&th, 0, sizeof th);
    th.body.size_px = 16;
    th.label.size_px = 12;
    th.title.size_px = 20;
    th.mono.size_px = 16;
    zui_ui_init(&UI, &laptop, &th);
    memset(&S, 0, sizeof S);
    S.root = zui_box(&UI, ZUI_NONE, ZUI_COL);
    S.top = zui_box(&UI, S.root, ZUI_ROW);
    S.title = zui_label(&UI, S.top, "Burrow");
    zui_set_font(&UI, S.title, ZUI_TEXT_TITLE, 0);
    S.search = zui_field(&UI, S.top, S.search_buf, sizeof S.search_buf, false);
    zui_set_name(&UI, S.search, "Search");
    S.body = zui_box(&UI, S.root, ZUI_ROW);
    zui_set_size(&UI, S.body, ZUI_SIZE_FLEX, 1);
    S.nav = zui_panel(&UI, S.body, ZUI_COL);
    zui_set_name(&UI, S.nav, "Places");
    S.list = zui_list(&UI, S.nav, row_fn, 0);
    zui_set_name(&UI, S.list, "Files");
    zui_list_set_rows(&UI, S.list, 15);
    S.side = zui_panel(&UI, S.body, ZUI_COL);
    zui_set_name(&UI, S.side, "Details");
    zui_set_size(&UI, S.side, ZUI_SIZE_FIXED, 360);
    S.heading = zui_label(&UI, S.side, "alpha.txt");
    S.notes = zui_field(&UI, S.side, S.notes_buf, sizeof S.notes_buf, true);
    zui_set_name(&UI, S.notes, "Notes");
    S.err = zui_label(&UI, S.side, "");
    zui_set_font(&UI, S.err, ZUI_TEXT_BODY, ZUI_ALERT);
    S.held = zui_held(&UI, S.side);
    zui_id_t row = zui_box(&UI, S.side, ZUI_ROW);
    S.save = zui_button(&UI, row, "Save", ZUI_BTN_PRIMARY);
    zui_set_shortcut(&UI, S.save, "\xE2\x8C\x98S"); /* ⌘S */
    S.cancel = zui_button(&UI, row, "Cancel", ZUI_BTN_SECONDARY);
    S.del = zui_button(&UI, row, "Delete", ZUI_BTN_DANGER);
    S.check = zui_button(&UI, S.side, "Show hidden files", ZUI_BTN_GHOST);
    S.sep = zui_separator(&UI, S.side);
    S.status = zui_label(&UI, S.side, "Ready");
    S.pw = zui_field(&UI, S.side, S.pw_buf, sizeof S.pw_buf, false);
    zui_set_name(&UI, S.pw, "Password");
    zui_w(&UI, S.pw)->u.field.secret = true;
    S.bar = zui_progress(&UI, S.root);
    zui_surface_t s;
    zui_surface_init(&s, fb, 1280, 800, 1280);
    zui_ui_paint(&UI, &s);

    zui_reader_init(&RD, &UI, hear, 0);
    zui_reader_set_landmark(&RD, S.top, ZUI_LANDMARK_BANNER);
    zui_reader_set_landmark(&RD, S.nav, ZUI_LANDMARK_NAVIGATION);
    zui_reader_set_heading(&RD, S.heading, 2);
    zui_reader_set_check(&RD, S.check, ZUI_CHECK_OFF);
    zui_reader_set_live(&RD, S.status, ZUI_LIVE_POLITE);
    quiet();
}

static const char *desc(zui_id_t id, int32_t row)
{
    static char out[ZUI_READER_UTTER];
    zui_reader_describe(&RD, id, row, out, sizeof out);
    return out;
}

static zui_event_t key(uint8_t type, uint16_t k, uint8_t mods)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.key = k;
    e.mods = mods;
    return e;
}

static bool rkey(uint16_t k, uint8_t extra)
{
    zui_event_t e = key(ZUI_EV_KEY_DOWN, k, ZUI_MOD_CTRL | ZUI_MOD_ALT | extra);
    return zui_reader_event(&RD, &e);
}

/* ---- R1, R4: what is said for each widget ------------------------------------- */

static void test_describe(void)
{
    build();
    CHECK(RD.on && RD.verbosity == ZUI_VERB_MEDIUM && RD.cursor == ZUI_NONE, "init");
    CHECK(n_heard == 0, "nothing said before the first update");
    CHECK(strcmp(desc(S.title, -1), "Burrow, heading level 1") == 0, "title is a heading: %s",
          desc(S.title, -1));
    CHECK(strcmp(desc(S.heading, -1), "alpha.txt, heading level 2") == 0, "marked heading: %s",
          desc(S.heading, -1));
    CHECK(strcmp(desc(S.search, -1), "Search, text field, empty") == 0, "empty field: %s",
          desc(S.search, -1));
    strcpy(S.search_buf, "foo");
    zui_field_set(&UI, S.search, 3);
    CHECK(strcmp(desc(S.search, -1), "Search, text field, foo") == 0, "field value: %s",
          desc(S.search, -1));
    zui_set_name(&UI, S.search, "");
    snprintf(zui_w(&UI, S.search)->u.field.placeholder, ZUI_LABEL_MAX, "Search files");
    CHECK(strcmp(desc(S.search, -1), "Search files, text field, foo") == 0,
          "the placeholder names a nameless field: %s", desc(S.search, -1));
    CHECK(strcmp(desc(S.notes, -1), "Notes, text area, empty") == 0, "multi-line: %s",
          desc(S.notes, -1));
    CHECK(strcmp(desc(S.list, -1), "Files, list, 15 items") == 0, "list: %s", desc(S.list, -1));
    zui_list_select(&UI, S.list, 2);
    CHECK(strcmp(desc(S.list, -1), "Files, list, 15 items, gamma, 2 KB, selected, 3 of 15") == 0,
          "list with selection: %s", desc(S.list, -1));
    CHECK(strcmp(desc(S.list, 0), "alpha.txt, 0 KB, 1 of 15") == 0, "a row: %s", desc(S.list, 0));
    CHECK(strcmp(desc(S.list, 6), "eta, 6 KB, dimmed, 7 of 15") == 0, "a muted row: %s",
          desc(S.list, 6));
    CHECK(desc(S.list, 15)[0] == 0 && desc(S.list, -5)[0] != 0, "a row past the end is nothing");
    CHECK(strcmp(desc(S.save, -1), "Save, button") == 0, "button: %s", desc(S.save, -1));
    zui_set_disabled(&UI, S.del, true);
    CHECK(strcmp(desc(S.del, -1), "Delete, button, disabled") == 0, "disabled: %s",
          desc(S.del, -1));
    CHECK(strcmp(desc(S.check, -1), "Show hidden files, check box, not checked") == 0,
          "check box: %s", desc(S.check, -1));
    zui_reader_set_check(&RD, S.check, ZUI_CHECK_ON);
    CHECK(strstr(desc(S.check, -1), ", checked"), "checked");
    zui_reader_set_check(&RD, S.check, ZUI_CHECK_MIXED);
    CHECK(strstr(desc(S.check, -1), "partly checked"), "mixed");
    CHECK(strcmp(desc(S.bar, -1), "progress bar, busy") == 0, "progress, not started: %s",
          desc(S.bar, -1));
    zui_progress_set(&UI, S.bar, 128, 1);
    CHECK(strcmp(desc(S.bar, -1), "progress bar, 50 percent") == 0, "half");
    zui_progress_set(&UI, S.bar, ZUI_BAR_UNKNOWN, 1);
    CHECK(strcmp(desc(S.bar, -1), "progress bar, busy") == 0, "busy, never a number");
    CHECK(strcmp(desc(S.nav, -1), "Places, navigation landmark") == 0, "landmark: %s",
          desc(S.nav, -1));
    CHECK(strcmp(desc(S.side, -1), "Details, region") == 0, "a named panel is a region: %s",
          desc(S.side, -1));
    CHECK(strcmp(desc(S.top, -1), "banner landmark") == 0, "unnamed landmark: %s", desc(S.top, -1));
    CHECK(strcmp(desc(S.status, -1), "Ready") == 0, "plain text is just read");
    CHECK(strcmp(desc(S.pw, -1), "Password, text field, secure") == 0, "empty secret: %s",
          desc(S.pw, -1));
    strcpy(S.pw_buf, "h\xC3\xA9llo!");
    zui_field_set(&UI, S.pw, (uint32_t) strlen(S.pw_buf));
    CHECK(strcmp(desc(S.pw, -1), "Password, text field, secure, 6 characters") == 0,
          "a secret is counted in characters, never read: %s", desc(S.pw, -1));
    CHECK(!strstr(desc(S.pw, -1), "llo"), "the secret text is never spoken");
    zui_set_text(&UI, S.err, "Disk full");
    CHECK(strcmp(desc(S.err, -1), "Disk full, error") == 0, "an error line: %s", desc(S.err, -1));
    zui_held_set(&UI, S.held, "companion", 120, true, "Rename to notes.md", 1);
    CHECK(strcmp(desc(S.held, -1),
                 "AI suggestion, from companion, pending, 120 tokens, Rename to notes.md") == 0,
          "held: %s", desc(S.held, -1));
    zui_held_set(&UI, S.held, "companion", 1, false, "x", 1);
    CHECK(strstr(desc(S.held, -1), "about 1 token, x"), "an estimate, one token: %s",
          desc(S.held, -1));
    CHECK(strcmp(desc(ZUI_NONE, -1), "") == 0 && strcmp(desc(9999, -1), "") == 0,
          "no widget, nothing");

    /* LOW */
    zui_reader_set_verbosity(&RD, ZUI_VERB_LOW);
    CHECK(strcmp(desc(S.title, -1), "Burrow, heading") == 0, "low heading: %s", desc(S.title, -1));
    CHECK(strcmp(desc(S.list, -1), "Files, list, gamma, selected") == 0, "low list: %s",
          desc(S.list, -1));
    CHECK(strcmp(desc(S.list, 0), "alpha.txt") == 0, "low row");
    CHECK(strcmp(desc(S.pw, -1), "Password, text field, secure") == 0, "low secret");
    CHECK(strcmp(desc(S.held, -1), "AI suggestion, from companion, pending, x") == 0,
          "low held: %s", desc(S.held, -1));
    CHECK(strcmp(desc(S.del, -1), "Delete, button, disabled") == 0, "low keeps disabled");
    /* HIGH */
    zui_reader_set_verbosity(&RD, ZUI_VERB_HIGH);
    CHECK(strcmp(desc(S.save, -1), "Save, button, shortcut Command S") == 0,
          "high: the shortcut in words: %s", desc(S.save, -1));
    zui_reader_set_hint(&RD, S.save, "saves the notes");
    CHECK(strcmp(desc(S.save, -1), "Save, button, shortcut Command S, saves the notes") == 0,
          "high: the hint: %s", desc(S.save, -1));
    zui_set_shortcut(&UI, S.cancel, "\xE2\x8C\x98\xE2\x87\xA7Z");
    CHECK(strcmp(desc(S.cancel, -1), "Cancel, button, shortcut Command Shift Z") == 0,
          "two symbols: %s", desc(S.cancel, -1));
    zui_set_shortcut(&UI, S.cancel, "Ctrl-Q");
    CHECK(strcmp(desc(S.cancel, -1), "Cancel, button, shortcut Ctrl-Q") == 0, "plain text kept");
    CHECK(strstr(desc(S.held, -1), "accept with Command Return, or reject"), "high: held hint");
    zui_reader_set_verbosity(&RD, ZUI_VERB_MEDIUM);
    CHECK(!strstr(desc(S.save, -1), "shortcut") && !strstr(desc(S.save, -1), "saves"),
          "medium: no shortcut, no hint");
    zui_reader_set_verbosity(&RD, (zui_verbosity_t) 9);
    CHECK(RD.verbosity == ZUI_VERB_MEDIUM, "a bad level is ignored");

    /* cut to fit, never inside a character */
    char tiny[8];
    uint32_t n = zui_reader_describe(&RD, S.title, -1, tiny, sizeof tiny);
    CHECK(n == 7 && tiny[7] == 0 && strcmp(tiny, "Burrow,") == 0, "cut to fit: %s", tiny);
    zui_set_text(&UI, S.status, "ab\xE2\x82\xAC");
    n = zui_reader_describe(&RD, S.status, -1, tiny, 5);
    CHECK(n == 2 && strcmp(tiny, "ab") == 0, "a cut never splits a character");
    CHECK(zui_reader_describe(&RD, S.title, -1, tiny, 0) == 0, "cap 0");
    CHECK(zui_reader_describe(0, S.title, -1, tiny, sizeof tiny) == 0 && tiny[0] == 0,
          "NULL reader");
    CHECK(n_heard == 0, "describing says nothing");
}

/* ---- R1: focus ---------------------------------------------------------------- */

static void test_focus(void)
{
    build();
    zui_reader_update(&RD);
    CHECK(n_heard == 0, "no focus, no errors: silent (%s)", last());
    zui_focus(&UI, S.search, true);
    zui_reader_update(&RD);
    CHECK(n_heard == 1 && strcmp(last(), "banner landmark, Search, text field, empty") == 0,
          "focus with its landmark: %s", last());
    CHECK(HEARD[0].how == ZUI_SAY_INTERRUPT, "focus interrupts");
    CHECK(RD.cursor == S.search, "the reader cursor follows focus");
    quiet();
    zui_reader_update(&RD);
    CHECK(n_heard == 0, "no change, nothing said");
    zui_focus(&UI, S.save, true);
    zui_reader_update(&RD);
    CHECK(strcmp(last(), "Details, region, Save, button") == 0, "into a region: %s", last());
    quiet();
    zui_focus(&UI, S.cancel, true);
    zui_reader_update(&RD);
    CHECK(strcmp(last(), "Cancel, button") == 0, "same group: just the control: %s", last());
    /* Tab through the real event path */
    quiet();
    zui_event_t tab = key(ZUI_EV_KEY_DOWN, ZUI_KEY_TAB, 0);
    zui_ui_event(&UI, &tab);
    zui_reader_update(&RD);
    CHECK(strcmp(last(), "Delete, button") == 0, "Tab moves on and it is said: %s", last());
    /* a list, then its selection */
    quiet();
    zui_focus(&UI, S.list, true);
    zui_reader_update(&RD);
    CHECK(strcmp(last(), "Places, navigation landmark, Files, list, 15 items") == 0, "list: %s",
          last());
    quiet();
    zui_list_select(&UI, S.list, 3);
    zui_reader_update(&RD);
    CHECK(n_heard == 1 && strcmp(last(), "delta.md, 3 KB, selected, 4 of 15") == 0,
          "the selection moving is said: %s", last());
    CHECK(RD.cursor == S.list && RD.cursor_row == 3, "the cursor is on that row");
    quiet();
    zui_event_t down = key(ZUI_EV_KEY_DOWN, ZUI_KEY_DOWN, 0);
    zui_ui_event(&UI, &down);
    zui_reader_update(&RD);
    CHECK(strcmp(last(), "epsilon, 4 KB, selected, 5 of 15") == 0, "arrow key in the list: %s",
          last());
    /* LOW: no group, no position */
    zui_reader_set_verbosity(&RD, ZUI_VERB_LOW);
    quiet();
    zui_focus(&UI, S.search, true);
    zui_reader_update(&RD);
    CHECK(strcmp(last(), "Search, text field, empty") == 0, "low: no landmark: %s", last());
    /* focus lost: nothing */
    quiet();
    zui_focus(&UI, ZUI_NONE, false);
    zui_reader_update(&RD);
    CHECK(n_heard == 0, "losing focus is silent");
    /* where am I */
    zui_reader_set_verbosity(&RD, ZUI_VERB_MEDIUM);
    zui_reader_where(&RD);
    CHECK(strcmp(last(), "no focus") == 0, "where, with no focus");
    zui_focus(&UI, S.cancel, true);
    zui_reader_update(&RD);
    quiet();
    zui_reader_where(&RD);
    CHECK(strcmp(last(), "Details, region, Cancel, button") == 0, "where says the group again: %s",
          last());
}

/* ---- R2: the reader cursor ---------------------------------------------------- */

static void test_cursor(void)
{
    build();
    zui_focus(&UI, S.search, true);
    zui_reader_update(&RD);
    RD.cursor = ZUI_NONE;
    quiet();
    CHECK(zui_reader_move(&RD, ZUI_READ_NEXT) && strcmp(last(), "banner landmark") == 0,
          "first item: %s", last());
    CHECK(zui_reader_move(&RD, ZUI_READ_NEXT) && strcmp(last(), "Burrow, heading level 1") == 0,
          "then the title: %s", last());
    zui_reader_move(&RD, ZUI_READ_NEXT);
    CHECK(strcmp(last(), "Search, text field, empty") == 0, "search: %s", last());
    zui_reader_move(&RD, ZUI_READ_NEXT);
    CHECK(strcmp(last(), "Places, navigation landmark") == 0, "landmark: %s", last());
    zui_reader_move(&RD, ZUI_READ_NEXT);
    CHECK(strcmp(last(), "Files, list, 15 items") == 0, "list: %s", last());
    zui_reader_move(&RD, ZUI_READ_NEXT);
    CHECK(strcmp(last(), "alpha.txt, 0 KB, 1 of 15") == 0, "into the rows: %s", last());
    for (int i = 0; i < 14; i++) zui_reader_move(&RD, ZUI_READ_NEXT);
    CHECK(strcmp(last(), "omicron, 14 KB, 15 of 15") == 0, "the last row: %s", last());
    zui_reader_move(&RD, ZUI_READ_NEXT);
    CHECK(strcmp(last(), "Details, region") == 0, "out of the list: %s", last());
    CHECK(UI.focus == S.search, "keyboard focus never moved");
    zui_reader_move(&RD, ZUI_READ_PREV);
    CHECK(strcmp(last(), "omicron, 14 KB, 15 of 15") == 0, "back into the list's last row: %s",
          last());
    zui_reader_move(&RD, ZUI_READ_PREV);
    CHECK(RD.cursor_row == 13, "previous row");
    for (int i = 0; i < 14; i++) zui_reader_move(&RD, ZUI_READ_PREV);
    CHECK(RD.cursor == S.list && RD.cursor_row == -1 &&
              strcmp(last(), "Files, list, 15 items") == 0,
          "back to the list itself: %s", last());
    /* the rest of the side panel, in order: the empty error line, the
     * hidden held answer and the separator are skipped */
    zui_reader_move(&RD, ZUI_READ_FIRST);
    CHECK(strcmp(last(), "banner landmark") == 0 && RD.cursor == S.top, "first");
    quiet();
    CHECK(!zui_reader_move(&RD, ZUI_READ_PREV) && strcmp(last(), "top") == 0 && RD.cursor == S.top,
          "nothing before the first");
    zui_reader_move(&RD, ZUI_READ_LAST);
    CHECK(RD.cursor == S.bar && strcmp(last(), "progress bar, busy") == 0, "last: %s", last());
    CHECK(!zui_reader_move(&RD, ZUI_READ_NEXT) && strcmp(last(), "end") == 0 && RD.cursor == S.bar,
          "nothing after the last");
    zui_reader_move(&RD, ZUI_READ_PREV);
    CHECK(RD.cursor == S.pw, "before the bar: the password field");
    zui_reader_move(&RD, ZUI_READ_PREV);
    CHECK(RD.cursor == S.status, "then the status line");
    zui_reader_move(&RD, ZUI_READ_PREV);
    CHECK(RD.cursor == S.check, "the separator is skipped");
    zui_reader_move(&RD, ZUI_READ_PREV);
    zui_reader_move(&RD, ZUI_READ_PREV);
    zui_reader_move(&RD, ZUI_READ_PREV);
    CHECK(RD.cursor == S.save, "the buttons");
    zui_reader_move(&RD, ZUI_READ_PREV);
    CHECK(RD.cursor == S.notes, "the empty error line and hidden held are skipped");
    /* no cursor: PREV starts at the end */
    RD.cursor = ZUI_NONE;
    zui_reader_move(&RD, ZUI_READ_PREV);
    CHECK(RD.cursor == S.bar, "no cursor: previous is the last item");

    /* headings */
    RD.cursor = ZUI_NONE;
    CHECK(zui_reader_move(&RD, ZUI_READ_NEXT_HEADING) && RD.cursor == S.title, "first heading");
    CHECK(zui_reader_move(&RD, ZUI_READ_NEXT_HEADING) && RD.cursor == S.heading &&
              strcmp(last(), "alpha.txt, heading level 2") == 0,
          "next heading: %s", last());
    CHECK(!zui_reader_move(&RD, ZUI_READ_NEXT_HEADING) && strcmp(last(), "no next heading") == 0 &&
              RD.cursor == S.heading,
          "no more headings; the cursor stays");
    CHECK(zui_reader_move(&RD, ZUI_READ_PREV_HEADING) && RD.cursor == S.title, "previous heading");
    CHECK(!zui_reader_move(&RD, ZUI_READ_PREV_HEADING) &&
              strcmp(last(), "no previous heading") == 0,
          "none before");
    RD.cursor = ZUI_NONE;
    CHECK(zui_reader_move(&RD, ZUI_READ_PREV_HEADING) && RD.cursor == S.heading,
          "no cursor: the last heading");
    /* from inside a list's rows */
    RD.cursor = S.list;
    RD.cursor_row = 5;
    CHECK(zui_reader_move(&RD, ZUI_READ_NEXT_HEADING) && RD.cursor == S.heading &&
              RD.cursor_row == -1,
          "heading from a row");
    /* landmarks */
    RD.cursor = ZUI_NONE;
    CHECK(zui_reader_move(&RD, ZUI_READ_NEXT_LANDMARK) && RD.cursor == S.top, "banner");
    CHECK(zui_reader_move(&RD, ZUI_READ_NEXT_LANDMARK) && RD.cursor == S.nav, "navigation");
    CHECK(zui_reader_move(&RD, ZUI_READ_NEXT_LANDMARK) && RD.cursor == S.side, "region");
    CHECK(!zui_reader_move(&RD, ZUI_READ_NEXT_LANDMARK) && strcmp(last(), "no next landmark") == 0,
          "no more");
    CHECK(zui_reader_move(&RD, ZUI_READ_PREV_LANDMARK) && RD.cursor == S.nav, "back one");
    /* held answers */
    CHECK(!zui_reader_move(&RD, ZUI_READ_NEXT_HELD) && strcmp(last(), "no next AI suggestion") == 0,
          "a hidden held answer is not found");
    zui_held_set(&UI, S.held, "companion", 40, true, "Tag it 'draft'", 1);
    RD.cursor = ZUI_NONE;
    CHECK(zui_reader_move(&RD, ZUI_READ_NEXT_HELD) && RD.cursor == S.held &&
              strstr(last(), "Tag it 'draft'"),
          "found once shown: %s", last());
    CHECK(!zui_reader_move(&RD, ZUI_READ_PREV_HELD), "only one");
    zui_reader_move(&RD, ZUI_READ_NEXT);
    CHECK(strcmp(last(), "Accept, button") == 0, "after the held answer: its Accept button (%s)",
          last());
    /* a hidden cursor starts over */
    RD.cursor = S.cancel;
    zui_set_hidden(&UI, S.side, true);
    zui_reader_move(&RD, ZUI_READ_NEXT);
    CHECK(RD.cursor == S.top, "the cursor's widget went away: start at the top");
    zui_reader_move(&RD, ZUI_READ_LAST);
    CHECK(RD.cursor == S.bar, "the hidden panel's items are skipped");
    zui_reader_move(&RD, ZUI_READ_PREV);
    CHECK(RD.cursor == S.list && RD.cursor_row == 14, "straight back to the list");
    /* an empty tree */
    zui_ui_clear(&UI);
    quiet();
    CHECK(!zui_reader_move(&RD, ZUI_READ_NEXT) && strcmp(last(), "nothing on screen") == 0,
          "empty tree");
    zui_reader_read_all(&RD, false);
    CHECK(strcmp(last(), "nothing on screen") == 0, "read all on an empty tree");
}

static void test_read_all(void)
{
    build();
    zui_focus(&UI, S.cancel, true);
    zui_reader_update(&RD);
    quiet();
    zui_reader_read_all(&RD, false);
    CHECK(n_heard == 15, "every item once (%d)", n_heard);
    CHECK(strcmp(heard(0), "banner landmark") == 0 && HEARD[0].how == ZUI_SAY_INTERRUPT,
          "the first interrupts");
    bool queued = true;
    for (int i = 1; i < n_heard; i++)
        if (HEARD[i].how != ZUI_SAY_QUEUE) queued = false;
    CHECK(queued, "the rest queue behind it");
    CHECK(strcmp(heard(1), "Burrow, heading level 1") == 0 &&
              strcmp(heard(4), "Files, list, 15 items") == 0 &&
              strcmp(heard(5), "Details, region") == 0 &&
              strcmp(heard(14), "progress bar, busy") == 0,
          "in reading order; a list is read as one item");
    CHECK(RD.cursor == S.bar && UI.focus == S.cancel, "the cursor ends at the end; focus stays");
    /* from the cursor */
    RD.cursor = S.check;
    quiet();
    zui_reader_read_all(&RD, true);
    CHECK(n_heard == 4 && strcmp(heard(0), "Show hidden files, check box, not checked") == 0,
          "from the cursor (%d): %s", n_heard, heard(0));
    RD.cursor = ZUI_NONE;
    quiet();
    zui_reader_read_all(&RD, true);
    CHECK(n_heard == 15, "no cursor: from the top");
}

static void test_activate(void)
{
    build();
    zui_focus(&UI, S.search, true);
    zui_reader_update(&RD);
    quiet();
    RD.cursor = ZUI_NONE;
    CHECK(!zui_reader_activate(&RD) && strcmp(last(), "nothing to activate") == 0, "no cursor");
    RD.cursor = S.cancel;
    RD.cursor_row = -1;
    CHECK(zui_reader_activate(&RD) && UI.focus == S.cancel && UI.focus_visible,
          "focus moves to the cursor");
    quiet();
    zui_reader_update(&RD);
    CHECK(strcmp(last(), "Details, region, Cancel, button") == 0, "and it is said: %s", last());
    RD.cursor = S.list;
    RD.cursor_row = 4;
    CHECK(zui_reader_activate(&RD) && UI.focus == S.list && zui_w(&UI, S.list)->u.list.sel == 4,
          "a row: focus the list and select it");
    RD.cursor = S.title;
    RD.cursor_row = -1;
    quiet();
    CHECK(!zui_reader_activate(&RD) && strcmp(last(), "not a control") == 0 && UI.focus == S.list,
          "a label cannot take focus");
    zui_set_disabled(&UI, S.del, true);
    RD.cursor = S.del;
    CHECK(!zui_reader_activate(&RD), "nor a disabled button");
}

/* ---- R3: held answers, errors, live regions ----------------------------------- */

static void test_held(void)
{
    build();
    zui_reader_update(&RD);
    quiet();
    zui_held_set(&UI, S.held, "companion", 120, true, "Rename to notes.md", 1);
    zui_reader_update(&RD);
    CHECK(n_heard == 1 &&
              strcmp(last(),
                     "AI suggestion, from companion, pending, 120 tokens, Rename to notes.md") == 0,
          "a held answer is announced: %s", last());
    CHECK(HEARD[0].how == ZUI_SAY_QUEUE, "it waits its turn");
    quiet();
    zui_reader_update(&RD);
    CHECK(n_heard == 0, "once");
    zui_held_set(&UI, S.held, "companion", 130, true, "Rename to ideas.md", 1);
    zui_reader_update(&RD);
    CHECK(n_heard == 1 && strstr(last(), "ideas.md"), "a new answer is announced again");
    quiet();
    zui_held_set(&UI, S.held, "companion", 130, true, "Rename to ideas.md", 2);
    zui_reader_update(&RD);
    CHECK(n_heard == 1 && strcmp(last(), "AI suggestion accepted") == 0, "accepted: %s", last());
    quiet();
    zui_held_set(&UI, S.held, "companion", 130, true, "Rename to ideas.md", 3);
    zui_reader_update(&RD);
    CHECK(strcmp(last(), "AI suggestion rejected") == 0, "rejected");
    quiet();
    zui_held_set(&UI, S.held, "companion", 0, true, 0, 0);
    zui_reader_update(&RD);
    CHECK(n_heard == 0, "cleared: silent");
    /* first seen already accepted: not announced as a change */
    build();
    zui_held_set(&UI, S.held, "companion", 5, true, "done", 2);
    zui_reader_update(&RD);
    CHECK(n_heard == 0, "an old accepted answer is not news");
    /* focus and a held answer in one frame: focus first, the answer queued */
    build();
    zui_reader_update(&RD);
    zui_held_set(&UI, S.held, "companion", 5, true, "hi", 1);
    zui_focus(&UI, S.held + 3 + 1, true); /* the Accept button (meta, body, row, accept) */
    zui_reader_update(&RD);
    CHECK(n_heard == 2 && strstr(heard(0), "Accept, button") && HEARD[0].how == ZUI_SAY_INTERRUPT &&
              strstr(heard(1), "AI suggestion") && HEARD[1].how == ZUI_SAY_QUEUE,
          "focus interrupts, then the answer queues (%s | %s)", heard(0), heard(1));
}

static void test_errors_and_live(void)
{
    build();
    zui_reader_update(&RD);
    CHECK(n_heard == 0, "an empty error line and the polite status's first text are silent");
    zui_set_text(&UI, S.err, "Disk full");
    zui_reader_update(&RD);
    CHECK(n_heard == 1 && strcmp(last(), "Error: Disk full") == 0 &&
              HEARD[0].how == ZUI_SAY_INTERRUPT,
          "an error line interrupts: %s", last());
    quiet();
    zui_reader_update(&RD);
    CHECK(n_heard == 0, "said once");
    zui_set_hidden(&UI, S.err, true);
    zui_reader_update(&RD);
    zui_set_hidden(&UI, S.err, false);
    zui_reader_update(&RD);
    CHECK(n_heard == 1 && strcmp(last(), "Error: Disk full") == 0, "said again when it reappears");
    quiet();
    zui_set_text(&UI, S.err, "");
    zui_reader_update(&RD);
    CHECK(n_heard == 0, "cleared: silent");
    /* polite live region */
    zui_set_text(&UI, S.status, "Saved");
    zui_reader_update(&RD);
    CHECK(n_heard == 1 && strcmp(last(), "Saved") == 0 && HEARD[0].how == ZUI_SAY_QUEUE,
          "a polite change queues: %s", last());
    quiet();
    zui_reader_set_live(&RD, S.status, ZUI_LIVE_ASSERTIVE);
    zui_set_text(&UI, S.status, "Connection lost");
    zui_reader_update(&RD);
    CHECK(n_heard == 1 && HEARD[0].how == ZUI_SAY_INTERRUPT, "assertive interrupts");
    quiet();
    zui_reader_set_live(&RD, S.status, ZUI_LIVE_OFF);
    zui_set_text(&UI, S.status, "Quiet now");
    zui_reader_update(&RD);
    CHECK(n_heard == 0, "live off: silent");
    /* a live field (a transcript) */
    zui_reader_set_live(&RD, S.notes, ZUI_LIVE_POLITE);
    zui_reader_update(&RD);
    strcpy(S.notes_buf, "hello there");
    zui_field_set(&UI, S.notes, 11);
    zui_reader_update(&RD);
    CHECK(strcmp(last(), "hello there") == 0, "a live field's text: %s", last());
    /* an error and a focus change in one frame */
    quiet();
    zui_set_text(&UI, S.err, "Name taken");
    zui_focus(&UI, S.save, true);
    zui_reader_update(&RD);
    CHECK(n_heard == 2 && strcmp(heard(0), "Error: Name taken") == 0 &&
              HEARD[0].how == ZUI_SAY_INTERRUPT && HEARD[1].how == ZUI_SAY_QUEUE,
          "the error first, focus queued after it");
    /* the app's own */
    quiet();
    zui_reader_error(&RD, "Upload failed");
    CHECK(strcmp(last(), "Error: Upload failed") == 0 && HEARD[0].how == ZUI_SAY_INTERRUPT,
          "zui_reader_error");
    zui_reader_announce(&RD, "3 new messages", ZUI_SAY_QUEUE);
    CHECK(strcmp(last(), "3 new messages") == 0 && HEARD[1].how == ZUI_SAY_QUEUE, "announce");
    zui_reader_announce(&RD, "now", ZUI_SAY_INTERRUPT);
    CHECK(HEARD[2].how == ZUI_SAY_INTERRUPT, "each call may interrupt again");
}

/* ---- menus and the palette ------------------------------------------------------ */

static void test_menus(void)
{
    build();
    zui_focus(&UI, S.list, true);
    zui_reader_update(&RD);
    quiet();
    const char *items[] = {"Open", "-", "Save as", "Delete"};
    const char *keys[] = {"\xE2\x8C\x98O", 0, "", "\xE2\x8C\xAB"};
    zui_menu_open(&UI, S.list, 10, 10, items, keys, 4);
    zui_menu_disable(&UI, 3, true);
    zui_reader_update(&RD);
    CHECK(n_heard == 1 && strcmp(last(), "menu, 3 items, Open, menu item, 1 of 3") == 0,
          "a menu opens: %s", last());
    quiet();
    zui_event_t down = key(ZUI_EV_KEY_DOWN, ZUI_KEY_DOWN, 0);
    zui_ui_event(&UI, &down);
    zui_reader_update(&RD);
    CHECK(strcmp(last(), "Save as, menu item, 2 of 3") == 0, "the separator is not counted: %s",
          last());
    quiet();
    UI.menu.hot = 3;
    zui_reader_set_verbosity(&RD, ZUI_VERB_HIGH);
    zui_reader_update(&RD);
    CHECK(strcmp(last(), "Delete, menu item, disabled, 3 of 3, shortcut Delete") == 0,
          "disabled, with its key: %s", last());
    quiet();
    zui_menu_close(&UI);
    zui_reader_update(&RD);
    CHECK(n_heard == 0, "closing is silent (focus did not move)");
    zui_reader_set_verbosity(&RD, ZUI_VERB_MEDIUM);
    /* the palette */
    zui_palette_set(&UI, S.root, ACTIONS, 4);
    zui_palette_open(&UI, "3 files selected");
    zui_reader_update(&RD);
    CHECK(strcmp(last(), "Ask, dialog, 3 files selected, Summarize, option, 1 of 4") == 0,
          "the palette opens: %s", last());
    quiet();
    zui_ui_event(&UI, &down);
    zui_reader_update(&RD);
    CHECK(strcmp(last(), "Rewrite, option, 2 of 4") == 0, "down: %s", last());
    quiet();
    zui_event_t t;
    memset(&t, 0, sizeof t);
    t.type = ZUI_EV_TEXT;
    strcpy(t.text, "fold");
    zui_ui_event(&UI, &t);
    zui_reader_update(&RD);
    CHECK(strcmp(last(), "Suggest folders, option, 1 of 1") == 0, "filtered: %s", last());
    quiet();
    strcpy(t.text, "qqq");
    zui_ui_event(&UI, &t);
    zui_reader_update(&RD);
    CHECK(strcmp(last(), "no actions match") == 0, "no match: %s", last());
    zui_palette_close(&UI);
    zui_reader_update(&RD);
    zui_reader_set_verbosity(&RD, ZUI_VERB_HIGH);
    zui_palette_open(&UI, "");
    quiet();
    zui_reader_update(&RD);
    CHECK(strcmp(last(),
                 "Ask, dialog, Summarize, option, 1 of 4, by companion, about 300 tokens") == 0,
          "high: who and what it costs: %s", last());
}

/* ---- R6: keys -------------------------------------------------------------------- */

static void test_keys(void)
{
    build();
    zui_focus(&UI, S.search, true);
    zui_reader_update(&RD);
    RD.cursor = ZUI_NONE;
    quiet();
    CHECK(rkey(ZUI_KEY_RIGHT, 0) && RD.cursor == S.top, "Ctrl+Alt+Right: next item");
    CHECK(rkey(ZUI_KEY_RIGHT, 0) && RD.cursor == S.title, "again");
    CHECK(rkey(ZUI_KEY_LEFT, 0) && RD.cursor == S.top, "Ctrl+Alt+Left: previous");
    CHECK(rkey(ZUI_KEY_DOWN, 0) && RD.cursor == S.title, "Ctrl+Alt+Down: next heading");
    CHECK(rkey(ZUI_KEY_DOWN, 0) && RD.cursor == S.heading, "next heading again");
    CHECK(rkey(ZUI_KEY_UP, 0) && RD.cursor == S.title, "Ctrl+Alt+Up: previous heading");
    CHECK(rkey(ZUI_KEY_PAGE_DOWN, 0) && RD.cursor == S.nav, "Page Down: next landmark");
    CHECK(rkey(ZUI_KEY_PAGE_UP, 0) && RD.cursor == S.top, "Page Up: previous landmark");
    CHECK(rkey(ZUI_KEY_END, 0) && RD.cursor == S.bar, "End: last");
    CHECK(rkey(ZUI_KEY_HOME, 0) && RD.cursor == S.top, "Home: first");
    CHECK(UI.focus == S.search, "none of it moved keyboard focus");
    /* the shortcut bit set alongside Ctrl, as on Linux and Windows */
    CHECK(rkey(ZUI_KEY_RIGHT, ZUI_MOD_CMD) && RD.cursor == S.title, "with the CMD bit too");
    /* repeat, where, stop */
    quiet();
    CHECK(rkey('r', 0) && strcmp(last(), "Burrow, heading level 1") == 0, "R repeats");
    CHECK(rkey('R', 0) && n_heard == 2, "upper case too");
    CHECK(rkey('f', 0) && strcmp(last(), "banner landmark, Search, text field, empty") == 0,
          "F: where focus is: %s", last());
    quiet();
    CHECK(rkey('s', 0) && n_heard == 1 && last()[0] == 0 && HEARD[0].how == ZUI_SAY_INTERRUPT,
          "S: stop is an empty interrupt");
    /* read all */
    quiet();
    CHECK(rkey('a', 0) && n_heard == 15, "A: read everything (%d)", n_heard);
    RD.cursor = S.check;
    quiet();
    CHECK(rkey('a', ZUI_MOD_SHIFT) && n_heard == 4, "Shift+A: from the cursor (%d)", n_heard);
    /* activate */
    RD.cursor = S.save;
    CHECK(rkey(ZUI_KEY_SPACE, 0) && UI.focus == S.save, "Space: focus the cursor's control");
    RD.cursor = S.cancel;
    CHECK(rkey(ZUI_KEY_ENTER, 0) && UI.focus == S.cancel, "Enter too");
    /* held */
    zui_held_set(&UI, S.held, "companion", 9, true, "ok", 1);
    RD.cursor = ZUI_NONE;
    CHECK(rkey('n', 0) && RD.cursor == S.held, "N: next held answer");
    CHECK(rkey('n', ZUI_MOD_SHIFT) && strcmp(last(), "no previous AI suggestion") == 0,
          "Shift+N: previous");
    /* verbosity */
    quiet();
    CHECK(rkey('v', 0) && RD.verbosity == ZUI_VERB_HIGH && strcmp(last(), "verbosity high") == 0,
          "V: high");
    CHECK(rkey('v', 0) && RD.verbosity == ZUI_VERB_LOW && strcmp(last(), "verbosity low") == 0,
          "V: low");
    CHECK(rkey('v', 0) && RD.verbosity == ZUI_VERB_MEDIUM, "V: back to medium");
    /* key up is used too; other keys are not */
    zui_event_t up = key(ZUI_EV_KEY_UP, ZUI_KEY_RIGHT, ZUI_MOD_CTRL | ZUI_MOD_ALT);
    zui_id_t c = RD.cursor;
    quiet();
    CHECK(zui_reader_event(&RD, &up) && n_heard == 0 && RD.cursor == c,
          "key up: used, does nothing");
    CHECK(!rkey('q', 0), "an unbound letter passes");
    zui_event_t e = key(ZUI_EV_KEY_DOWN, ZUI_KEY_RIGHT, ZUI_MOD_CTRL);
    CHECK(!zui_reader_event(&RD, &e), "Ctrl alone passes");
    e.mods = ZUI_MOD_ALT;
    CHECK(!zui_reader_event(&RD, &e), "Alt alone passes");
    e.mods = ZUI_MOD_CTRL | ZUI_MOD_ALT | ZUI_MOD_META;
    CHECK(!zui_reader_event(&RD, &e), "with Meta it is the magnifier's");
    e.mods = 0;
    CHECK(!zui_reader_event(&RD, &e), "a plain arrow is the app's");
    e.type = ZUI_EV_PTR_DOWN;
    e.mods = ZUI_MOD_CTRL | ZUI_MOD_ALT;
    CHECK(!zui_reader_event(&RD, &e), "not a key");
    CHECK(!zui_reader_event(&RD, 0) && !zui_reader_event(0, &e), "NULLs");
    /* the reader off and on */
    quiet();
    CHECK(rkey('z', 0) && !RD.on && strcmp(last(), "screen reader off") == 0, "Z: off, said first");
    quiet();
    CHECK(!rkey(ZUI_KEY_RIGHT, 0), "off: the keys pass to the app");
    zui_reader_error(&RD, "x");
    zui_focus(&UI, S.save, true);
    zui_reader_update(&RD);
    CHECK(n_heard == 0, "off: silent");
    CHECK(rkey('z', 0) && RD.on && strcmp(last(), "screen reader on") == 0, "Z: on");
    quiet();
    zui_reader_update(&RD);
    CHECK(n_heard == 0, "what changed while off is not dumped on return");
    zui_reader_set_on(&RD, true);
    CHECK(n_heard == 0, "already on: nothing");
}

/* ---- R7: marks, rebuilding ------------------------------------------------------ */

static void test_marks(void)
{
    build();
    CHECK(!zui_reader_set_heading(&RD, ZUI_NONE, 1) && !zui_reader_set_heading(&RD, 9999, 1),
          "a bad id is refused");
    CHECK(!zui_reader_set_landmark(&RD, S.top, (zui_landmark_t) 99), "a bad landmark");
    CHECK(!zui_reader_set_check(&RD, S.check, (zui_check_t) 9), "a bad check state");
    CHECK(!zui_reader_set_live(&RD, S.status, (zui_live_t) 7), "a bad live level");
    CHECK(zui_reader_set_heading(&RD, S.status, 9) && strstr(desc(S.status, -1), "level 6"),
          "levels stop at 6");
    uint32_t before = RD.n_mark;
    zui_reader_set_heading(&RD, S.status, 3);
    CHECK(RD.n_mark == before, "marking again reuses the entry");
    char longhint[200];
    memset(longhint, 'h', sizeof longhint);
    longhint[199] = 0;
    CHECK(zui_reader_set_hint(&RD, S.save, longhint) &&
              strlen(RD.mark[RD.n_mark - 1].hint) == ZUI_READER_HINT - 1,
          "a hint is cut to fit");
    CHECK(zui_reader_set_hint(&RD, S.save, 0), "a NULL hint is empty");
    /* fill the table */
    zui_reader_clear_marks(&RD);
    CHECK(RD.n_mark == 0 && strcmp(desc(S.nav, -1), "Places, region") == 0,
          "cleared: the panel is a region again");
    uint32_t ok = 0;
    for (uint32_t i = 0; i < UI.n && i < ZUI_READER_MARKS + 5; i++)
        ok += zui_reader_set_hint(&RD, (zui_id_t) i, "h");
    CHECK(UI.n > ZUI_READER_MARKS || ok == UI.n, "marks up to the table");
    CHECK(RD.n_mark <= ZUI_READER_MARKS, "never past the table");
    /* rebuilding: marks on gone widgets drop, the cursor resets */
    build();
    zui_focus(&UI, S.pw, true);
    zui_reader_update(&RD);
    RD.cursor = S.bar;
    zui_reader_set_hint(&RD, S.bar, "x");
    zui_ui_clear(&UI);
    zui_id_t root = zui_box(&UI, ZUI_NONE, ZUI_COL);
    zui_id_t b = zui_button(&UI, root, "Only", ZUI_BTN_PRIMARY);
    zui_focus(&UI, b, true);
    quiet();
    zui_reader_update(&RD);
    CHECK(RD.cursor == b && strcmp(last(), "Only, button") == 0,
          "a new screen: its focus is said (%s)", last());
    bool gone = true;
    for (uint32_t i = 0; i < RD.n_mark; i++)
        if (RD.mark[i].id >= (zui_id_t) UI.n) gone = false;
    CHECK(gone, "marks past the new tree are dropped");
    /* names */
    CHECK(strcmp(zui_landmark_name(ZUI_LANDMARK_MAIN), "main") == 0 &&
              strcmp(zui_landmark_name(ZUI_LANDMARK_CONTENTINFO), "status") == 0 &&
              strcmp(zui_landmark_name(ZUI_LANDMARK_NONE), "") == 0,
          "landmark names");
    CHECK(strcmp(zui_verbosity_name(ZUI_VERB_LOW), "low") == 0 &&
              strcmp(zui_verbosity_name(ZUI_VERB_MEDIUM), "medium") == 0 &&
              strcmp(zui_verbosity_name(ZUI_VERB_HIGH), "high") == 0,
          "verbosity names");
    zui_reader_update(0);
    zui_reader_where(0);
    zui_reader_repeat(0);
    zui_reader_stop(0);
    zui_reader_announce(0, "x", ZUI_SAY_QUEUE);
    zui_reader_error(0, "x");
    CHECK(!zui_reader_move(0, ZUI_READ_NEXT) && !zui_reader_activate(0), "NULL reader is safe");
    /* no speak function: counted, nothing called */
    zui_reader_t r2;
    zui_reader_init(&r2, &UI, 0, 0);
    zui_reader_update(&r2);
    CHECK(r2.utterances == 1 && strcmp(r2.last, "Only, button") == 0, "no speaker: still tracked");
    zui_reader_repeat(&r2);
    CHECK(r2.utterances == 2, "repeat with no speaker");
}

/* ---- R5: the speech engine adapter ---------------------------------------------- */

typedef struct {
    char said[8][128];
    int n_said, n_stop, n_cap;
    int fail_next;
    const char *lang;
} engine_t;

static int eng_say(void *e, const char *text, const char *lang)
{
    engine_t *g = (engine_t *) e;
    g->lang = lang;
    if (g->fail_next) {
        g->fail_next = 0;
        return -3;
    }
    if (g->n_said < 8) snprintf(g->said[g->n_said], 128, "%s", text);
    g->n_said++;
    return 0;
}

static void eng_stop(void *e)
{
    ((engine_t *) e)->n_stop++;
}

static void eng_caption(void *e, const char *text)
{
    (void) text;
    ((engine_t *) e)->n_cap++;
}

static void test_voice(void)
{
    static engine_t g;
    memset(&g, 0, sizeof g);
    zui_reader_voice_t v = {eng_say, eng_stop, eng_caption, &g, "en", 0, 0, 0};
    zui_reader_voice_speak("hello", ZUI_SAY_QUEUE, &v);
    CHECK(g.n_said == 1 && g.n_stop == 0 && strcmp(g.said[0], "hello") == 0 && v.said == 1,
          "queue: just say");
    CHECK(g.lang && strcmp(g.lang, "en") == 0 && g.n_cap == 1, "language passed, caption shown");
    zui_reader_voice_speak("now", ZUI_SAY_INTERRUPT, &v);
    CHECK(g.n_stop == 1 && g.n_said == 2 && v.stopped == 1, "interrupt: stop, then say");
    zui_reader_voice_speak("", ZUI_SAY_INTERRUPT, &v);
    CHECK(g.n_stop == 2 && g.n_said == 2 && g.n_cap == 2, "empty interrupt: stop only");
    zui_reader_voice_speak("", ZUI_SAY_QUEUE, &v);
    zui_reader_voice_speak(0, ZUI_SAY_QUEUE, &v);
    CHECK(g.n_said == 2 && g.n_stop == 2, "empty queue: nothing");
    g.fail_next = 1;
    zui_reader_voice_speak("x", ZUI_SAY_QUEUE, &v);
    CHECK(v.failed == 1 && v.said == 2, "an engine failure is counted");
    zui_reader_voice_t none = {0, 0, 0, 0, 0, 0, 0, 0};
    zui_reader_voice_speak("x", ZUI_SAY_INTERRUPT, &none);
    CHECK(none.failed == 1 && none.stopped == 0, "no engine: counted as failed, no crash");
    zui_reader_voice_speak("x", ZUI_SAY_QUEUE, 0);
    CHECK(true, "NULL voice is safe");
    /* the reader through the adapter */
    build();
    memset(&g, 0, sizeof g);
    zui_reader_voice_t v2 = {eng_say, eng_stop, 0, &g, 0, 0, 0, 0};
    zui_reader_init(&RD, &UI, zui_reader_voice_speak, &v2);
    zui_focus(&UI, S.save, true);
    zui_reader_update(&RD);
    CHECK(g.n_said == 1 && strcmp(g.said[0], "Details, region, Save, button") == 0 && g.n_stop == 1,
          "focus reaches the engine, cutting off what played: %s", g.said[0]);
    zui_reader_read_all(&RD, false);
    /* init cleared the marks, so the unnamed banner box is no longer an item */
    CHECK(g.n_stop == 2 && g.n_said == 1 + 14, "read all: one stop, then every item (%d)",
          g.n_said);
}

int main(void)
{
    test_describe();
    test_focus();
    test_cursor();
    test_read_all();
    test_activate();
    test_held();
    test_errors_and_live();
    test_menus();
    test_keys();
    test_marks();
    test_voice();
    printf("%s test_zui_reader: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
