/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_ime.c — host tests for the input-method framework: the
 * composition life cycle, what is kept from the app, the candidate window,
 * and the hex, compose and sample phonetic engines, driven through the
 * keyboard translator as a real keyboard would drive them. */
#include <stdio.h>
#include <string.h>

#include "zui_ime.h"
#include "zui_keymap.h"

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

enum {
    K_ESC = 1,
    K_1 = 2,
    K_2 = 3,
    K_3 = 4,
    K_4 = 5,
    K_6 = 7,
    K_8 = 9,
    K_0 = 11,
    K_MINUS = 12,
    K_EQUAL = 13,
    K_BKSP = 14,
    K_TAB = 15,
    K_W = 17,
    K_E = 18,
    K_I = 23,
    K_O = 24,
    K_ENTER = 28,
    K_LCTRL = 29,
    K_A = 30,
    K_S = 31,
    K_D = 32,
    K_F = 33,
    K_G = 34,
    K_H = 35,
    K_J = 36,
    K_APOS = 40,
    K_LSHIFT = 42,
    K_Z = 44,
    K_X = 45,
    K_C = 46,
    K_B = 48,
    K_N = 49,
    K_M = 50,
    K_COMMA = 51,
    K_DOT = 52,
    K_SPACE = 57,
    K_PGUP = 104,
    K_LEFT = 105,
    K_RIGHT = 106,
    K_PGDN = 109,
    K_MENU = 127
};

static zui_keymap_t KM;
static zui_kbd_t KB;
static zui_ime_t IME;

/* What an app would see. */
static char DOC[512];  /* committed text */
static char PRE[128];  /* the preedit shown */
static uint32_t NKEY;  /* KEY_DOWN events that reached the app */
static uint32_t NUP;   /* KEY_UP events that reached the app   */
static uint32_t NPRE;  /* PREEDIT events */
static uint32_t NTEXT; /* TEXT events */
static zui_event_t LOG[256];
static uint32_t NLOG;

static void reset_app(void)
{
    DOC[0] = PRE[0] = 0;
    NKEY = NUP = NPRE = NTEXT = NLOG = 0;
}

static void app(const zui_event_t *e, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (NLOG < 256) LOG[NLOG++] = e[i];
        switch (e[i].type) {
        case ZUI_EV_TEXT:
            strcat(DOC, e[i].text);
            NTEXT++;
            break;
        case ZUI_EV_PREEDIT:
            strcpy(PRE, e[i].text);
            NPRE++;
            break;
        case ZUI_EV_KEY_DOWN:
            NKEY++;
            break;
        case ZUI_EV_KEY_UP:
            NUP++;
            break;
        default:
            break;
        }
    }
}

static void press(uint16_t code, bool down)
{
    zui_event_t ev[ZUI_KBD_EV_MAX], out[ZUI_IME_EV_MAX];
    uint32_t n = zui_kbd_feed(&KB, code, down, false, 10, ev, ZUI_KBD_EV_MAX);
    for (uint32_t i = 0; i < n; i++) app(out, zui_ime_feed(&IME, &ev[i], out, ZUI_IME_EV_MAX));
}

static void tap(uint16_t code)
{
    press(code, true);
    press(code, false);
}

static void tap_shift(uint16_t code)
{
    press(K_LSHIFT, true);
    tap(code);
    press(K_LSHIFT, false);
}

static void type_ascii(const char *s)
{
    static const uint16_t letter[26] = {30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
                                        49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44};
    for (; *s; s++) {
        char c = *s;
        if (c >= 'a' && c <= 'z')
            tap(letter[c - 'a']);
        else if (c >= 'A' && c <= 'Z')
            tap_shift(letter[c - 'A']);
        else if (c >= '1' && c <= '9')
            tap((uint16_t) (2 + (c - '1')));
        else if (c == '0')
            tap(K_0);
        else if (c == ' ')
            tap(K_SPACE);
        else if (c == '\'')
            tap(K_APOS);
        else if (c == '"')
            tap_shift(K_APOS);
        else if (c == '-')
            tap(K_MINUS);
        else if (c == '=')
            tap(K_EQUAL);
        else if (c == '.')
            tap(K_DOT);
        else if (c == ',')
            tap(K_COMMA);
        else if (c == '/')
            tap(53);
        else if (c == '<')
            tap_shift(K_COMMA);
        else if (c == '>')
            tap_shift(K_DOT);
    }
}

static void setup(const zui_ime_engine_t *eng)
{
    zui_keymap_load(&KM, "us");
    zui_kbd_init(&KB, &KM);
    KB.compose_code = K_MENU;
    zui_ime_init(&IME, eng);
    reset_app();
}

/* ---- framework ---------------------------------------------------------------------- */

static void test_passthrough(void)
{
    setup(0);
    type_ascii("hi");
    CHECK(strcmp(DOC, "hi") == 0 && NKEY == 2 && NUP == 2, "no engine: everything passes");
    setup(&zui_ime_pinyin_sample);
    tap(K_ENTER);
    tap(K_LEFT);
    CHECK(NKEY == 2 && NUP == 2 && DOC[0] == 0, "idle: keys the engine does not want pass");
    zui_event_t e, out[4];
    memset(&e, 0, sizeof e);
    e.type = ZUI_EV_PTR_DOWN;
    e.x = 5;
    CHECK(zui_ime_feed(&IME, &e, out, 4) == 1 && out[0].type == ZUI_EV_PTR_DOWN && out[0].x == 5,
          "other events pass untouched");
    e.type = ZUI_EV_TEXT;
    strcpy(e.text, "12,");
    CHECK(zui_ime_feed(&IME, &e, out, 4) == 1 && strcmp(out[0].text, "12,") == 0,
          "idle text the engine does not want passes as one event");
    CHECK(zui_ime_feed(&IME, &e, out, 0) == 0 && IME.dropped == 1, "a full out[] counts drops");
}

static void test_hex(void)
{
    setup(&zui_ime_hex);
    press(K_LCTRL, true);
    press(K_LSHIFT, true);
    tap(22); /* u */
    press(K_LSHIFT, false);
    press(K_LCTRL, false);
    CHECK(zui_ime_composing(&IME) && strcmp(PRE, "u") == 0 && NKEY == 0 && NUP == 0,
          "Ctrl+Shift+U opens hex entry; the shortcut is kept from the app");
    type_ascii("e9");
    CHECK(strcmp(PRE, "ue9") == 0 && strcmp(zui_ime_preedit(&IME), "ue9") == 0 && NKEY == 0,
          "digits go to the preedit, keys are kept");
    CHECK(zui_ime_cand_count(&IME) == 1 && strcmp(zui_ime_cand(&IME, 0), "é") == 0,
          "the one candidate previews the character");
    tap(K_SPACE);
    CHECK(strcmp(DOC, "é") == 0 && PRE[0] == 0 && !zui_ime_composing(&IME) && NKEY == 0,
          "space commits é");
    CHECK(LOG[NLOG - 2].type == ZUI_EV_PREEDIT && LOG[NLOG - 2].text[0] == 0 &&
              LOG[NLOG - 1].type == ZUI_EV_TEXT,
          "M2: closing: empty PREEDIT, then TEXT");

    reset_app();
    press(K_LCTRL, true);
    press(K_LSHIFT, true);
    tap(22);
    press(K_LSHIFT, false);
    press(K_LCTRL, false);
    type_ascii("1f98a");
    tap(K_ENTER);
    CHECK(strcmp(DOC, "🦊") == 0 && NKEY == 0, "Enter commits a five-digit code (%s)", DOC);

    reset_app();
    press(K_LCTRL, true);
    press(K_LSHIFT, true);
    tap(22);
    press(K_LSHIFT, false);
    press(K_LCTRL, false);
    type_ascii("20acx");
    CHECK(strcmp(PRE, "u20ac") == 0, "non-hex characters are ignored");
    tap(K_BKSP);
    CHECK(strcmp(PRE, "u20a") == 0, "backspace removes a digit");
    type_ascii("c");
    type_ascii("1234");
    CHECK(strcmp(PRE, "u20ac12") == 0, "at most six digits");
    tap(K_BKSP);
    tap(K_BKSP);
    tap(K_ENTER);
    CHECK(strcmp(DOC, "€") == 0, "€");

    reset_app();
    press(K_LCTRL, true);
    press(K_LSHIFT, true);
    tap(22);
    press(K_LSHIFT, false);
    press(K_LCTRL, false);
    tap(K_BKSP);
    CHECK(!zui_ime_composing(&IME) && PRE[0] == 0 && DOC[0] == 0 && NKEY == 0,
          "backspace with no digits closes it, nothing sent");

    reset_app();
    press(K_LCTRL, true);
    press(K_LSHIFT, true);
    tap(22);
    press(K_LSHIFT, false);
    press(K_LCTRL, false);
    type_ascii("d800");
    CHECK(zui_ime_cand_count(&IME) == 0, "a surrogate has no preview");
    tap(K_ENTER);
    CHECK(DOC[0] == 0 && !zui_ime_composing(&IME), "and commits nothing");

    reset_app();
    press(K_LCTRL, true);
    press(K_LSHIFT, true);
    tap(22);
    press(K_LSHIFT, false);
    press(K_LCTRL, false);
    type_ascii("41");
    tap(K_ESC);
    CHECK(DOC[0] == 0 && PRE[0] == 0 && NKEY == 0, "escape cancels");

    reset_app();
    press(K_LCTRL, true);
    press(K_LSHIFT, true);
    tap(22);
    press(K_LSHIFT, false);
    press(K_LCTRL, false);
    type_ascii("41");
    tap(K_TAB);
    CHECK(strcmp(DOC, "A") == 0 && NKEY == 1 && LOG[NLOG - 2].key == ZUI_KEY_TAB,
          "M3: a declined key commits, then passes (%s)", DOC);
    CHECK(NUP == 1, "its key up passes too");

    reset_app();
    press(K_LCTRL, true);
    press(K_LSHIFT, true);
    tap(22);
    press(K_LSHIFT, false);
    press(K_LCTRL, false);
    type_ascii("42");
    zui_event_t out[8];
    app(out, zui_ime_choose(&IME, 0, out, 8));
    CHECK(strcmp(DOC, "B") == 0, "choosing the preview commits it");

    reset_app();
    press(K_LCTRL, true);
    tap(22); /* ctrl+u only */
    press(K_LCTRL, false);
    CHECK(!zui_ime_composing(&IME) && NKEY == 1, "Ctrl+U alone is just a shortcut");
}

static void test_compose(void)
{
    setup(&zui_ime_compose);
    tap(K_MENU);
    CHECK(zui_ime_composing(&IME) && strcmp(PRE, "·") == 0 && NKEY == 0,
          "compose opens a sequence");
    type_ascii("'");
    CHECK(strcmp(PRE, "·'") == 0, "the sequence shows");
    type_ascii("e");
    CHECK(strcmp(DOC, "é") == 0 && PRE[0] == 0 && NKEY == 0 && NUP == 0,
          "compose ' e = é, every key kept");
    reset_app();
    tap(K_MENU);
    type_ascii("oc");
    tap(K_MENU);
    type_ascii("<<");
    tap(K_MENU);
    type_ascii("---");
    tap(K_MENU);
    type_ascii("--.");
    tap(K_MENU);
    type_ascii("e=");
    tap(K_MENU);
    type_ascii("\"u");
    CHECK(strcmp(DOC, "©«—–€ü") == 0, "sequences (%s)", DOC);
    reset_app();
    tap(K_MENU);
    type_ascii("qz");
    CHECK(strcmp(DOC, "qz") == 0 && !zui_ime_composing(&IME),
          "a sequence that matches nothing is sent as typed; the next key types (%s)", DOC);
    reset_app();
    tap(K_MENU);
    type_ascii("o");
    tap(K_BKSP);
    CHECK(strcmp(PRE, "·") == 0 && zui_ime_composing(&IME), "backspace steps back");
    tap(K_BKSP);
    CHECK(!zui_ime_composing(&IME) && DOC[0] == 0, "and closes when empty");
    tap(K_MENU);
    type_ascii("-");
    tap(K_ESC);
    CHECK(!zui_ime_composing(&IME) && DOC[0] == 0, "escape cancels");
    tap(K_MENU);
    tap(K_MENU);
    CHECK(!zui_ime_composing(&IME) && DOC[0] == 0 && NKEY == 0, "compose twice cancels");
    reset_app();
    tap(K_MENU);
    type_ascii("-");
    tap(K_LEFT);
    CHECK(strcmp(DOC, "-") == 0 && NKEY == 1, "a declined key sends what was typed, then passes");
    reset_app();
    tap(K_MENU);
    type_ascii("o");
    zui_event_t out[8];
    app(out, zui_ime_commit(&IME, out, 8));
    CHECK(strcmp(DOC, "o") == 0 && PRE[0] == 0, "commit on demand");
    reset_app();
    tap(K_MENU);
    type_ascii("o");
    zui_event_t fo;
    memset(&fo, 0, sizeof fo);
    fo.type = ZUI_EV_FOCUS_OUT;
    uint32_t n = zui_ime_feed(&IME, &fo, out, 8);
    app(out, n);
    CHECK(strcmp(DOC, "o") == 0 && out[n - 1].type == ZUI_EV_FOCUS_OUT,
          "losing focus commits, then the focus event passes");
    reset_app();
    tap(K_MENU);
    type_ascii("o");
    app(out, zui_ime_cancel(&IME, out, 8));
    CHECK(DOC[0] == 0 && PRE[0] == 0 && NPRE == 3, "cancel on demand");
    reset_app();
    press(K_LCTRL, true);
    tap(K_MENU);
    press(K_LCTRL, false);
    CHECK(zui_ime_composing(&IME), "ctrl does not stop compose from opening");
    tap(K_ESC);
    /* the compose engine over another table */
    static const zui_compose_seq_t mine[] = {{"ab", "X"}, {"abc", "Y"}};
    static const zui_compose_table_t mt = {mine, 2};
    zui_ime_engine_t eng = zui_ime_compose;
    eng.data = &mt;
    zui_ime_init(&IME, &eng);
    reset_app();
    tap(K_MENU);
    type_ascii("ab");
    CHECK(zui_ime_composing(&IME) && DOC[0] == 0, "an exact match that could go on waits");
    type_ascii("c");
    CHECK(strcmp(DOC, "Y") == 0, "longer sequence");
    tap(K_MENU);
    type_ascii("ab");
    tap(K_ENTER);
    CHECK(strcmp(DOC, "YX") == 0 && NKEY == 1, "a declined key takes the exact match");
}

static void test_pinyin(void)
{
    setup(&zui_ime_pinyin_sample);
    type_ascii("ni");
    CHECK(strcmp(PRE, "ni") == 0 && zui_ime_composing(&IME), "letters spell into the preedit");
    CHECK(NKEY == 1, "the first key reached the app before the composition opened");
    CHECK(zui_ime_cand_count(&IME) == 6 && strcmp(zui_ime_cand(&IME, 0), "你") == 0 &&
              strcmp(zui_ime_cand(&IME, 5), "你好") == 0 && zui_ime_cand(&IME, 6) == 0,
          "candidates: the syllable, then completions (%u)", zui_ime_cand_count(&IME));
    tap(K_SPACE);
    CHECK(strcmp(DOC, "你") == 0 && PRE[0] == 0, "space takes the first");
    reset_app();
    type_ascii("hao");
    type_ascii("2");
    CHECK(strcmp(DOC, "号") == 0, "a digit chooses on the page");
    reset_app();
    type_ascii("ma");
    tap(K_RIGHT);
    tap(K_RIGHT);
    tap(K_LEFT);
    CHECK(IME.sel == 1, "arrows move the highlight");
    tap(K_SPACE);
    CHECK(strcmp(DOC, "妈") == 0, "space takes the highlighted one");
    reset_app();
    type_ascii("woshi");
    CHECK(strcmp(zui_ime_cand(&IME, 0), "我") == 0 && IME.cand[0].used == 2,
          "the longest key that starts the input offers its candidates");
    tap(K_SPACE);
    CHECK(strcmp(DOC, "我") == 0 && strcmp(PRE, "shi") == 0 && zui_ime_composing(&IME),
          "choosing leaves the rest composing");
    CHECK(LOG[NLOG - 2].type == ZUI_EV_TEXT && LOG[NLOG - 1].type == ZUI_EV_PREEDIT,
          "M2: a commit that leaves it open: TEXT, then the new PREEDIT");
    zui_ime_window_t w;
    zui_ime_set_caret(&IME, 100, 40, 18);
    zui_ime_window(&IME, &w);
    CHECK(w.visible && w.x == 100 && w.y == 58 && w.first == 0 && w.count == 9 && w.pages == 2 &&
              w.page == 0 && w.sel == 0,
          "candidate window: ten candidates on two pages");
    tap(K_PGDN);
    zui_ime_window(&IME, &w);
    CHECK(w.page == 1 && w.first == 9 && w.count == 1 && w.sel == 9, "page down");
    tap(K_PGDN);
    zui_ime_window(&IME, &w);
    CHECK(w.page == 1, "no page past the last");
    type_ascii("1");
    CHECK(strcmp(DOC, "我师") == 0 && !zui_ime_composing(&IME), "1 on page two is the tenth");
    reset_app();
    type_ascii("shi=");
    CHECK(IME.sel == 9, "= turns the page");
    type_ascii("-");
    CHECK(IME.sel == 0, "- turns it back");
    tap(K_PGUP);
    CHECK(IME.sel == 0, "page up on the first page stays");
    type_ascii("9");
    CHECK(strcmp(DOC, "室") == 0, "9 is the ninth (%s)", DOC);
    reset_app();
    type_ascii("zh");
    CHECK(zui_ime_cand_count(&IME) == 6 && strcmp(zui_ime_cand(&IME, 5), "中国") == 0,
          "no whole key yet: completions");
    type_ascii("ongguo");
    CHECK(zui_ime_cand_count(&IME) == 1 && strcmp(zui_ime_cand(&IME, 0), "中国") == 0,
          "the phrase");
    tap(K_BKSP);
    tap(K_BKSP);
    tap(K_BKSP);
    CHECK(strcmp(PRE, "zhong") == 0 && zui_ime_cand_count(&IME) == 6, "backspace re-reads");
    tap(K_ENTER);
    CHECK(strcmp(DOC, "zhong") == 0 && PRE[0] == 0, "Enter sends the letters");
    reset_app();
    type_ascii("xq");
    CHECK(zui_ime_cand_count(&IME) == 0 && strcmp(PRE, "xq") == 0, "no candidates");
    tap(K_SPACE);
    CHECK(strcmp(DOC, "xq") == 0, "space with none sends the letters");
    reset_app();
    type_ascii("ni,");
    CHECK(strcmp(DOC, "ni,") == 0, "punctuation commits the letters and types");
    reset_app();
    type_ascii("de7");
    CHECK(zui_ime_composing(&IME) && DOC[0] == 0, "a digit with no candidate is ignored");
    tap(K_ESC);
    CHECK(DOC[0] == 0 && PRE[0] == 0, "escape drops it");
    reset_app();
    type_ascii("ni");
    type_ascii("A");
    CHECK(strcmp(DOC, "niA") == 0, "a capital letter commits, then types");
    reset_app();
    type_ascii("wo");
    press(K_LCTRL, true);
    tap(K_S);
    press(K_LCTRL, false);
    CHECK(strcmp(DOC, "wo") == 0 && NKEY == 2, "a shortcut commits, then reaches the app");
    reset_app();
    type_ascii("abcdefghijklmnopqrstuvwxyzab");
    CHECK(strlen(PRE) == 24, "the preedit is bounded");
    tap(K_ESC);
    reset_app();
    type_ascii("ni");
    zui_event_t out[16];
    app(out, zui_ime_choose(&IME, 5, out, 16));
    CHECK(strcmp(DOC, "你好") == 0 && PRE[0] == 0, "a click on a candidate");
    app(out, zui_ime_choose(&IME, 0, out, 16));
    CHECK(strcmp(DOC, "你好") == 0, "choosing with none open does nothing");
    zui_ime_window(&IME, &w);
    CHECK(!w.visible, "the window hides");

    /* switching engines commits */
    reset_app();
    type_ascii("ma");
    uint32_t n = zui_ime_use(&IME, &zui_ime_compose, out, 16);
    app(out, n);
    CHECK(strcmp(DOC, "ma") == 0 && PRE[0] == 0 && IME.eng == &zui_ime_compose,
          "switching engines commits what was composed");
    n = zui_ime_use(&IME, 0, out, 16);
    CHECK(n == 0 && IME.eng == 0, "and to none");
}

static void test_split_and_dead(void)
{
    /* long commits are split on character boundaries */
    setup(&zui_ime_pinyin_sample);
    zui_ime_send(&IME, "中中中中中中中中中中中中"); /* 36 bytes */
    zui_event_t out[8];
    uint32_t n = zui_ime_commit(&IME, out, 8);
    CHECK(n == 2 && strlen(out[0].text) == 30 && strlen(out[1].text) == 6,
          "36 bytes go as 30 + 6 (%u)", n);
    /* the keyboard's own dead-key PREEDIT reaches the app when idle */
    setup(&zui_ime_pinyin_sample);
    zui_keymap_load(&KM, "us-intl");
    zui_kbd_init(&KB, &KM);
    tap(K_APOS);
    CHECK(strcmp(PRE, "'") == 0, "a dead key's preedit passes when idle");
    type_ascii("e");
    CHECK(strcmp(DOC, "é") == 0 && PRE[0] == 0, "dead keys still work with an engine on");
    /* candidate helpers */
    zui_ime_init(&IME, &zui_ime_pinyin_sample);
    CHECK(!zui_ime_cand_nav(&IME, ZUI_KEY_LEFT) && zui_ime_cand_digit(&IME, '1') == -1,
          "no candidates: nothing to move or choose");
    CHECK(zui_ime_cand_add(&IME, "x", 1, 1) && !zui_ime_cand_add(&IME, "", 0, 0) &&
              !zui_ime_cand_add(&IME, "0123456789012345678901234", 25, 0),
          "empty and oversize candidates are refused");
    for (uint32_t i = 0; i < ZUI_IME_CAND_MAX + 8u; i++) zui_ime_cand_add(&IME, "y", 1, 0);
    CHECK(zui_ime_cand_count(&IME) == ZUI_IME_CAND_MAX, "at most ZUI_IME_CAND_MAX");
    CHECK(zui_ime_cand_nav(&IME, ZUI_KEY_END) && IME.sel == ZUI_IME_CAND_MAX - 1 &&
              zui_ime_cand_nav(&IME, ZUI_KEY_DOWN) && IME.sel == ZUI_IME_CAND_MAX - 1 &&
              zui_ime_cand_nav(&IME, ZUI_KEY_HOME) && IME.sel == 0 &&
              zui_ime_cand_nav(&IME, ZUI_KEY_UP) && IME.sel == 0 &&
              !zui_ime_cand_nav(&IME, ZUI_KEY_TAB),
          "end, home and the edges");
    zui_ime_set_preedit(&IME, "a very long preedit that runs past the sixty four byte limit ok");
    CHECK(strlen(zui_ime_preedit(&IME)) == ZUI_IME_PRE_MAX - 1, "the preedit is bounded");
    zui_ime_set_preedit(&IME, 0);
    CHECK(!zui_ime_composing(&IME) && zui_ime_cand_count(&IME) == 0,
          "an empty preedit clears the candidates");
    for (int i = 0; i < 50; i++) zui_ime_send(&IME, "abc");
    CHECK(IME.ncommit == ZUI_IME_COMMIT_MAX - 1, "the commit buffer is bounded");
}

/* ---- M6: the languages' input methods --------------------------------------------- */

/* text from elsewhere (an on-screen keyboard), one character per event */
static void feed_chars(const char *s)
{
    zui_event_t e, out[ZUI_IME_EV_MAX];
    while (*s) {
        memset(&e, 0, sizeof e);
        e.type = ZUI_EV_TEXT;
        uint32_t cp, n = zui_km_utf8_get(s, (uint32_t) strlen(s), &cp);
        memcpy(e.text, s, n);
        s += n;
        app(out, zui_ime_feed(&IME, &e, out, ZUI_IME_EV_MAX));
    }
}

static bool has_cand(const char *c)
{
    for (uint32_t i = 0; i < zui_ime_cand_count(&IME); i++)
        if (strcmp(zui_ime_cand(&IME, i), c) == 0) return true;
    return false;
}

static void test_han(void)
{
    setup(&zui_ime_pinyin);
    type_ascii("zhong");
    CHECK(strcmp(PRE, "zhong") == 0 && strcmp(zui_ime_cand(&IME, 0), "中") == 0,
          "pinyin: zhong offers 中 first");
    type_ascii(" ");
    type_ascii("guo1");
    CHECK(strcmp(DOC, "中国") == 0 && PRE[0] == 0, "pinyin: 中国 (%s)", DOC);
    reset_app();
    type_ascii("nv");
    CHECK(has_cand("女"), "pinyin: v is u-umlaut");
    type_ascii(" ");
    CHECK(strcmp(DOC, "女") == 0, "pinyin: nv -> 女");
    reset_app();
    type_ascii("shi");
    CHECK(zui_ime_cand_count(&IME) > 32 && strcmp(zui_ime_cand(&IME, 0), "是") == 0,
          "pinyin: shi has many candidates, 是 first (%u)", zui_ime_cand_count(&IME));
    type_ascii("xue");
    CHECK(strcmp(PRE, "shixue") == 0 && has_cand("是"), "pinyin: the longest syllable first");
    type_ascii("1");
    CHECK(strcmp(DOC, "是") == 0 && strcmp(PRE, "xue") == 0 && has_cand("学"),
          "pinyin: the rest keeps composing");
    tap(K_ENTER);
    CHECK(strcmp(DOC, "是xue") == 0 && PRE[0] == 0, "pinyin: Enter sends the letters");

    setup(&zui_ime_pinyin_hant);
    type_ascii("zhong guo ");
    CHECK(strcmp(DOC, "中國") == 0, "traditional pinyin: 中國 (%s)", DOC);
    reset_app();
    type_ascii("xue");
    CHECK(has_cand("學") && !has_cand("学"), "traditional pinyin: 學, not 学");

    setup(&zui_ime_jyutping);
    type_ascii("gwong2dung2");
    CHECK(strcmp(DOC, "廣東") == 0, "jyutping: 廣東 (%s)", DOC);
    reset_app();
    type_ascii("nei");
    CHECK(strcmp(zui_ime_cand(&IME, 0), "你") == 0, "jyutping: nei offers 你 first");
    type_ascii(" hou ");
    CHECK(strcmp(DOC, "你好") == 0, "jyutping: 你好 (%s)", DOC);
}

static void test_kana(void)
{
    setup(&zui_ime_kana);
    type_ascii("ni");
    CHECK(strcmp(PRE, "に") == 0, "kana: ni");
    type_ascii("h");
    CHECK(strcmp(PRE, "にh") == 0, "kana: a consonant waits");
    type_ascii("onn");
    CHECK(strcmp(PRE, "にほん") == 0, "kana: nihonn (%s)", PRE);
    CHECK(strcmp(zui_ime_cand(&IME, 0), "にほん") == 0 &&
              strcmp(zui_ime_cand(&IME, 1), "ニホン") == 0,
          "kana: hiragana then katakana");
    tap(K_ENTER);
    CHECK(strcmp(DOC, "にほん") == 0 && PRE[0] == 0, "kana: Enter sends the hiragana");
    reset_app();
    type_ascii("gakkou");
    CHECK(strcmp(PRE, "がっこう") == 0, "kana: a doubled consonant is っ (%s)", PRE);
    tap(K_ESC);
    CHECK(PRE[0] == 0 && DOC[0] == 0, "kana: Escape drops it");
    type_ascii("kyou");
    CHECK(strcmp(PRE, "きょう") == 0, "kana: kyo");
    tap(K_BKSP);
    CHECK(strcmp(PRE, "きょ") == 0, "kana: Backspace takes back a letter");
    tap(K_ESC);
    type_ascii("kanji");
    CHECK(strcmp(PRE, "かんじ") == 0, "kana: n before a consonant is ん (%s)", PRE);
    tap(K_ESC);
    type_ascii("yama");
    CHECK(has_cand("山"), "kana: yama offers 山");
    type_ascii("3");
    CHECK(strcmp(DOC, "山") == 0 && PRE[0] == 0, "kana: choose the kanji (%s)", DOC);
    reset_app();
    type_ascii("gaku");
    CHECK(has_cand("学") && has_cand("ガク"), "kana: gaku offers 学");
    tap(K_ESC);
    type_ascii("sushi-.");
    CHECK(strcmp(PRE, "すしー。") == 0, "kana: - and . (%s)", PRE);
    tap(K_ESC);
    type_ascii("pan");
    type_ascii(" ");
    CHECK(strcmp(DOC, "ぱん") == 0, "kana: a final n is ん on commit (%s)", DOC);
    reset_app();
    type_ascii("tya");
    CHECK(strcmp(PRE, "ちゃ") == 0, "kana: Kunrei tya");
}

static void test_hangul(void)
{
    setup(&zui_ime_hangul);
    type_ascii("g");
    CHECK(strcmp(PRE, "ㅎ") == 0, "hangul: a lone consonant");
    type_ascii("k");
    CHECK(strcmp(PRE, "하") == 0, "hangul: 하");
    type_ascii("s");
    CHECK(strcmp(PRE, "한") == 0 && DOC[0] == 0, "hangul: 한");
    type_ascii("r");
    CHECK(strcmp(DOC, "한") == 0 && strcmp(PRE, "ㄱ") == 0, "hangul: ㄴㄱ do not join: 한 is sent");
    type_ascii("ml");
    CHECK(strcmp(DOC, "한") == 0 && strcmp(PRE, "긔") == 0, "hangul: 한 then 긔 (%s|%s)", DOC, PRE);
    type_ascii("f");
    CHECK(strcmp(PRE, "긜") == 0, "hangul: 긜");
    tap(K_BKSP);
    CHECK(strcmp(PRE, "긔") == 0, "hangul: Backspace takes back a jamo");
    tap(K_BKSP);
    CHECK(strcmp(PRE, "그") == 0, "hangul: and the second vowel part");
    type_ascii("f");
    type_ascii(" ");
    CHECK(strcmp(DOC, "한글 ") == 0 && PRE[0] == 0, "hangul: space sends (%s)", DOC);
    reset_app();
    type_ascii("dkssudgktpdy");
    CHECK(strcmp(DOC, "안녕하세") == 0 && strcmp(PRE, "요") == 0, "hangul: 안녕하세요 (%s|%s)", DOC,
          PRE);
    tap(K_ENTER);
    CHECK(strcmp(DOC, "안녕하세요") == 0 && NKEY >= 1, "hangul: Enter sends and passes on");
    reset_app();
    type_ascii("dlfr");
    CHECK(strcmp(PRE, "읽") == 0, "hangul: a double final 읽 (%s)", PRE);
    type_ascii("j");
    CHECK(strcmp(DOC, "일") == 0 && strcmp(PRE, "거") == 0, "hangul: its second part moves on");
    tap(K_ESC);
    reset_app();
    type_ascii("Rk");
    CHECK(strcmp(PRE, "까") == 0, "hangul: shift gives the double consonant");
}

static void test_geez(void)
{
    setup(&zui_ime_geez);
    type_ascii("s");
    CHECK(strcmp(PRE, "ስ") == 0, "geez: a consonant waits in its sixth form");
    type_ascii("e");
    CHECK(strcmp(DOC, "ሰ") == 0 && PRE[0] == 0, "geez: se");
    type_ascii("lam");
    CHECK(strcmp(DOC, "ሰላ") == 0 && strcmp(PRE, "ም") == 0, "geez: selam (%s|%s)", DOC, PRE);
    type_ascii(" ");
    CHECK(strcmp(DOC, "ሰላም ") == 0, "geez: space sends the sixth form");
    reset_app();
    type_ascii("amarNa");
    CHECK(strcmp(DOC, "ኣማርኛ") == 0, "geez: amarNa (%s)", DOC);
    reset_app();
    type_ascii("ityoPya");
    CHECK(strcmp(DOC, "ኢትዮጵያ") == 0, "geez: P (%s)", DOC);
    reset_app();
    type_ascii("lWa");
    CHECK(strcmp(DOC, "ሏ") == 0, "geez: the wa form");
    reset_app();
    feed_chars("`a`se");
    CHECK(strcmp(DOC, "ዓሠ") == 0, "geez: the backtick rows (%s)", DOC);
    reset_app();
    feed_chars("ba::");
    CHECK(strcmp(DOC, "ባ።") == 0, "geez: :: is a full stop (%s)", DOC);
    reset_app();
    feed_chars("t:b,");
    CHECK(strcmp(DOC, "ት፡ብ፣") == 0, "geez: word space and comma (%s)", DOC);
    reset_app();
    type_ascii("k");
    tap(K_BKSP);
    CHECK(PRE[0] == 0 && DOC[0] == 0 && !zui_ime_composing(&IME), "geez: Backspace drops it");
    type_ascii("1");
    CHECK(strcmp(DOC, "1") == 0, "geez: digits pass");
}

static void test_for_locale(void)
{
    CHECK(zui_ime_for_locale("zh-Hans") == &zui_ime_pinyin &&
              zui_ime_for_locale("zh-Hant") == &zui_ime_pinyin_hant &&
              zui_ime_for_locale("zh_TW") == &zui_ime_pinyin_hant &&
              zui_ime_for_locale("zh-CN") == &zui_ime_pinyin &&
              zui_ime_for_locale("yue") == &zui_ime_jyutping &&
              zui_ime_for_locale("ja-JP") == &zui_ime_kana &&
              zui_ime_for_locale("KO") == &zui_ime_hangul &&
              zui_ime_for_locale("am") == &zui_ime_geez &&
              zui_ime_for_locale("ti-ER") == &zui_ime_geez,
          "for locale: the languages that need one");
    CHECK(!zui_ime_for_locale("en") && !zui_ime_for_locale("hi") && !zui_ime_for_locale("") &&
              !zui_ime_for_locale(0) && !zui_ime_for_locale("zhx"),
          "for locale: the rest type directly");
    CHECK(zui_pinyin_dict.n > 300 && zui_pinyin_hant_dict.n > 300 && zui_jyutping_dict.n > 400 &&
              zui_kana_dict.n > 1000,
          "the Unihan tables are there");
}

int main(void)
{
    test_passthrough();
    test_hex();
    test_compose();
    test_pinyin();
    test_split_and_dead();
    test_han();
    test_kana();
    test_hangul();
    test_geez();
    test_for_locale();
    printf("%s test_zui_ime: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails, checks);
    return fails ? 1 : 0;
}
