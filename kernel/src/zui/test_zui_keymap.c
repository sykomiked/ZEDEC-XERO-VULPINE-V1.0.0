/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_keymap.c — host tests for keyboard layouts: positions, levels,
 * Caps Lock, dead keys, the layout text loader, every built-in layout, and
 * the translator from physical keys to KEY / TEXT / PREEDIT events. */
#include <stdio.h>
#include <string.h>

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

/* evdev codes used below */
enum {
    K_ESC = 1,
    K_1 = 2,
    K_2 = 3,
    K_6 = 7,
    K_MINUS = 12,
    K_BKSP = 14,
    K_TAB = 15,
    K_Q = 16,
    K_W = 17,
    K_E = 18,
    K_R = 19,
    K_Y = 21,
    K_U = 22,
    K_I = 23,
    K_O = 24,
    K_P = 25,
    K_LBRACE = 26,
    K_RBRACE = 27,
    K_ENTER = 28,
    K_LCTRL = 29,
    K_A = 30,
    K_S = 31,
    K_D = 32,
    K_H = 35,
    K_J = 36,
    K_L = 38,
    K_SEMI = 39,
    K_APOS = 40,
    K_GRAVE = 41,
    K_LSHIFT = 42,
    K_BSLASH = 43,
    K_Z = 44,
    K_X = 45,
    K_C = 46,
    K_V = 47,
    K_B = 48,
    K_N = 49,
    K_M = 50,
    K_COMMA = 51,
    K_SLASH = 53,
    K_RSHIFT = 54,
    K_LALT = 56,
    K_SPACE = 57,
    K_CAPS = 58,
    K_F1 = 59,
    K_KP7 = 71,
    K_KPDOT = 83,
    K_102ND = 86,
    K_RO = 89,
    K_KPENTER = 96,
    K_RALT = 100,
    K_LEFT = 105,
    K_LMETA = 125,
    K_MENU = 127
};

static zui_keymap_t KM;
static zui_kbd_t KB;
static zui_event_t EV[64];
static uint32_t NEV;
static char TYPED[256];
static char PRE[64];

static void reset_log(void)
{
    NEV = 0;
    TYPED[0] = 0;
    PRE[0] = 0;
}

static void press(uint16_t code, bool down)
{
    zui_event_t ev[ZUI_KBD_EV_MAX];
    uint32_t n = zui_kbd_feed(&KB, code, down, false, 100, ev, ZUI_KBD_EV_MAX);
    for (uint32_t i = 0; i < n; i++) {
        if (NEV < 64) EV[NEV++] = ev[i];
        if (ev[i].type == ZUI_EV_TEXT) strcat(TYPED, ev[i].text);
        if (ev[i].type == ZUI_EV_PREEDIT) strcpy(PRE, ev[i].text);
    }
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

static void tap_altgr(uint16_t code)
{
    press(K_RALT, true);
    tap(code);
    press(K_RALT, false);
}

static const char *typed_after(const char *layout, const uint16_t *codes, uint32_t n)
{
    zui_keymap_load(&KM, layout);
    zui_kbd_init(&KB, &KM);
    reset_log();
    for (uint32_t i = 0; i < n; i++) tap(codes[i]);
    return TYPED;
}

static uint32_t cp_at(const char *layout, int32_t pos, bool shift, bool altgr)
{
    static zui_keymap_t k;
    if (!zui_keymap_load(&k, layout)) return 0xFFFFFFFFu;
    return zui_keymap_get(&k, pos, shift, altgr, false);
}

static uint32_t utf8_cp(const char *s)
{
    uint32_t cp;
    zui_km_utf8_get(s, (uint32_t) strlen(s), &cp);
    return cp;
}

/* ---- helpers ------------------------------------------------------------------- */

static void test_utf8_case(void)
{
    char b[8];
    CHECK(zui_km_utf8_put(b, 'A') == 1 && b[0] == 'A', "ascii encodes in one byte");
    CHECK(zui_km_utf8_put(b, 0xE9) == 2 && memcmp(b, "é", 2) == 0, "é is two bytes");
    CHECK(zui_km_utf8_put(b, 0x20AC) == 3 && memcmp(b, "€", 3) == 0, "€ is three bytes");
    CHECK(zui_km_utf8_put(b, 0x1F98A) == 4 && memcmp(b, "🦊", 4) == 0, "a fox is four bytes");
    CHECK(zui_km_utf8_put(b, 0xD800) == 0 && zui_km_utf8_put(b, 0x110000) == 0 &&
              zui_km_utf8_put(b, 0) == 0,
          "surrogates, out of range and NUL are refused");
    uint32_t cp;
    CHECK(zui_km_utf8_get("€x", 4, &cp) == 3 && cp == 0x20AC, "decode €");
    CHECK(zui_km_utf8_get("\xC0\xAF", 2, &cp) == 1 && cp == 0xFFFD, "overlong is rejected");
    CHECK(zui_km_utf8_get("\xE2\x82", 2, &cp) == 1 && cp == 0xFFFD, "truncated is rejected");
    CHECK(zui_km_utf8_get("", 0, &cp) == 0, "empty");

    CHECK(zui_km_upper('q') == 'Q' && zui_km_lower('Q') == 'q', "ascii case");
    CHECK(zui_km_upper(0xE9) == 0xC9 && zui_km_lower(0xC9) == 0xE9, "é / É");
    CHECK(zui_km_upper(0xF7) == 0xF7 && zui_km_lower(0xD7) == 0xD7, "÷ and × have no case");
    CHECK(zui_km_upper(0xFF) == 0x178 && zui_km_lower(0x178) == 0xFF, "ÿ / Ÿ");
    CHECK(zui_km_upper(0x105) == 0x104 && zui_km_lower(0x104) == 0x105, "ą / Ą");
    CHECK(zui_km_upper(0x17E) == 0x17D && zui_km_lower(0x17D) == 0x17E, "ž / Ž");
    CHECK(zui_km_upper(0x3B1) == 0x391 && zui_km_lower(0x3A9) == 0x3C9, "α / Ω");
    CHECK(zui_km_upper(0x3C2) == 0x3C2, "final sigma has no capital of its own");
    CHECK(zui_km_upper(0x3AC) == 0x386 && zui_km_upper(0x3CE) == 0x38F &&
              zui_km_upper(0x3AF) == 0x38A && zui_km_lower(0x38C) == 0x3CC,
          "Greek with tonos");
    CHECK(zui_km_upper(0x439) == 0x419 && zui_km_upper(0x451) == 0x401 &&
              zui_km_lower(0x401) == 0x451,
          "й / Й and ё / Ё");
    CHECK(zui_km_is_letter('a') && zui_km_is_letter(0xE9) && zui_km_is_letter(0x5D0) &&
              zui_km_is_letter(0x634) && zui_km_is_letter(0x4F60) && zui_km_is_letter(0x3B1),
          "letters across scripts");
    CHECK(!zui_km_is_letter('1') && !zui_km_is_letter(0xD7) && !zui_km_is_letter(0x20AC) &&
              !zui_km_is_letter(0x60C),
          "digits, × , € and the Arabic comma are not letters");
}

static void test_scancodes(void)
{
    CHECK(zui_kpos_from_evdev(K_GRAVE) == ZUI_KP(ZUI_KP_ROW_E, 0), "grave is E00");
    CHECK(zui_kpos_from_evdev(K_1) == ZUI_KP(ZUI_KP_ROW_E, 1), "1 is E01");
    CHECK(zui_kpos_from_evdev(K_Q) == ZUI_KP(ZUI_KP_ROW_D, 1), "Q is D01");
    CHECK(zui_kpos_from_evdev(K_RBRACE) == ZUI_KP(ZUI_KP_ROW_D, 12), "] is D12");
    CHECK(zui_kpos_from_evdev(K_BSLASH) == ZUI_KP_D13, "backslash is D13");
    CHECK(zui_kpos_from_evdev(K_A) == ZUI_KP(ZUI_KP_ROW_C, 1), "A is C01");
    CHECK(zui_kpos_from_evdev(K_APOS) == ZUI_KP(ZUI_KP_ROW_C, 11), "' is C11");
    CHECK(zui_kpos_from_evdev(K_102ND) == ZUI_KP(ZUI_KP_ROW_B, 0), "ISO extra key is B00");
    CHECK(zui_kpos_from_evdev(K_Z) == ZUI_KP(ZUI_KP_ROW_B, 1), "Z is B01");
    CHECK(zui_kpos_from_evdev(K_SLASH) == ZUI_KP(ZUI_KP_ROW_B, 10), "/ is B10");
    CHECK(zui_kpos_from_evdev(K_RO) == ZUI_KP(ZUI_KP_ROW_B, 11), "ABNT2 key is B11");
    CHECK(zui_kpos_from_evdev(K_SPACE) == ZUI_KP_SPACE, "space");
    CHECK(zui_kpos_from_evdev(K_LSHIFT) == -1 && zui_kpos_from_evdev(K_ENTER) == -1 &&
              zui_kpos_from_evdev(500) == -1,
          "non-character keys have no position");

    CHECK(zui_evdev_from_hid(0x04) == K_A && zui_evdev_from_hid(0x1D) == K_Z, "HID a..z");
    CHECK(zui_evdev_from_hid(0x1E) == K_1 && zui_evdev_from_hid(0x27) == 11, "HID 1..0");
    CHECK(zui_evdev_from_hid(0x28) == K_ENTER && zui_evdev_from_hid(0x2C) == K_SPACE,
          "HID enter, space");
    CHECK(zui_evdev_from_hid(0x31) == K_BSLASH && zui_evdev_from_hid(0x32) == K_BSLASH,
          "HID backslash and non-US # are the same key code");
    CHECK(zui_evdev_from_hid(0x64) == K_102ND && zui_evdev_from_hid(0x87) == K_RO,
          "HID ISO and ABNT2 keys");
    CHECK(zui_evdev_from_hid(0x50) == K_LEFT && zui_evdev_from_hid(0x3A) == K_F1, "HID left, F1");
    CHECK(zui_evdev_from_hid(0xE1) == K_LSHIFT && zui_evdev_from_hid(0xE6) == K_RALT,
          "HID modifiers");
    CHECK(zui_evdev_from_hid(0x5F) == K_KP7 && zui_evdev_from_hid(0x58) == K_KPENTER, "HID keypad");
    CHECK(zui_evdev_from_hid(0x03) == 0 && zui_evdev_from_hid(0x200) == 0, "unknown HID");
}

/* ---- K5: loader ----------------------------------------------------------------- */

static void test_loader(void)
{
    static zui_keymap_t k;
    uint32_t line = 99;
    const char *ok = "# a test layout\n"
                     "keyboard tst Test layout\r\n"
                     "caps shift\n"
                     "\n"
                     "base E01 a b\n"
                     "shift E01 A --\n"
                     "altgr E01 \\u00e9 \\u{1F98A}\n"
                     "altR+shift E01 x\n"
                     "base D01 ab \\ \\u0644\\u0627\n"
                     "deadkey hat ^ =tilde aâ\n"
                     "base C01 dead:hat dead:acute dead:hat\n"
                     "base A03 _\n";
    CHECK(zui_keymap_parse(&k, ok, (uint32_t) strlen(ok), &line) && line == 0,
          "valid text parses (line %u)", line);
    CHECK(strcmp(k.id, "tst") == 0 && strcmp(k.name, "Test layout") == 0, "id and name");
    CHECK(k.caps_mode == ZUI_CAPS_SHIFT, "caps shift");
    int32_t e1 = ZUI_KP(ZUI_KP_ROW_E, 1), e2 = e1 + 1;
    CHECK(k.key[e1][0] == 'a' && k.key[e2][0] == 'b' && k.key[e1][1] == 'A' && k.key[e2][1] == 0,
          "tokens fill to the right; -- is nothing");
    CHECK(k.key[e1][2] == 0xE9 && k.key[e2][2] == 0x1F98A, "\\u escapes");
    CHECK(k.key[e1][3] == 'x' && k.has_altgr, "altR+shift alias; AltGr seen");
    int32_t d1 = ZUI_KP(ZUI_KP_ROW_D, 1);
    CHECK(ZUI_KM_IS_STR(k.key[d1][0]) && k.nstr == 2, "several characters are a string");
    char b[32];
    zui_keymap_utf8(&k, k.key[d1][0], b, sizeof b);
    CHECK(strcmp(b, "ab") == 0, "string text");
    CHECK(k.key[d1 + 1][0] == '\\', "a lone backslash is itself");
    zui_keymap_utf8(&k, k.key[d1 + 2][0], b, sizeof b);
    CHECK(strcmp(b, "لا") == 0, "escapes join into a string");
    int32_t c1 = ZUI_KP(ZUI_KP_ROW_C, 1);
    CHECK(ZUI_KM_IS_DEAD(k.key[c1][0]) && k.key[c1][0] == k.key[c1 + 2][0], "same dead key reused");
    CHECK(k.ndead == 2 && strcmp(k.dead[0].name, "hat") == 0 && k.dead[0].accent == '^',
          "deadkey line");
    CHECK(zui_keymap_combine(&k, 0, 'a') == 0xE2 && zui_keymap_combine(&k, 0, 'n') == 0xF1,
          "an included standard table, and an own pair overriding it");
    CHECK(zui_keymap_combine(&k, 1, 'e') == 0xE9 && k.dead[1].accent == 0xB4,
          "standard acute loaded by reference");
    CHECK(zui_keymap_combine(&k, 1, 'q') == 0 && zui_keymap_combine(&k, 7, 'a') == 0,
          "no entry / no table");
    CHECK(k.key[ZUI_KP_SPACE][0] == '_' && k.key[ZUI_KP_SPACE][1] == ' ', "A03 is the space bar");
    CHECK(k.caps[e1] == 3 && k.caps[e2] == 0, "caps shift flips every key with a shift level");

    /* errors report the line and leave the map empty */
    struct {
        const char *t;
        uint32_t line;
    } bad[] = {
        {"keyboard x\nbase Q01 a\n", 2},
        {"base E01\n", 1},
        {"frob E01 a\n", 1},
        {"base E14 a b c\n", 1},
        {"base A04 a\n", 1},
        {"base A03 a b\n", 1},
        {"base E01 dead:nosuch\n", 1},
        {"base E01 \\u{110000}\n", 1},
        {"base E01 \\u{}\n", 1},
        {"base E01 \\u12\n", 1},
        {"base E01 0123456789abcdefg\n", 1},
        {"caps sometimes\n", 1},
        {"keyboard\n", 1},
        {"deadkey d ab\n", 1},
        {"deadkey d ^ abc\n", 1},
        {"deadkey d ^ =nosuch\n", 1},
        {"base E01 \xC3\n", 1},
        {"\n\nbase E01 a b c d e f g h i j k l m n o p q r s t u v w x y z 1 2 3 4 5 6 7 8 9 0 "
         "a b c d e\n",
         3},
    };
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        line = 0;
        bool r = zui_keymap_parse(&k, bad[i].t, (uint32_t) strlen(bad[i].t), &line);
        CHECK(!r && line == bad[i].line && k.key[e1][0] == 0,
              "bad text %u fails at line %u (got %u)", i, bad[i].line, line);
    }
    /* too many dead keys and strings */
    char big[1024];
    strcpy(big, "deadkey a 1\ndeadkey b 2\ndeadkey c 3\ndeadkey d 4\ndeadkey e 5\n"
                "deadkey f 6\ndeadkey g 7\ndeadkey h 8\ndeadkey i 9\n");
    CHECK(!zui_keymap_parse(&k, big, (uint32_t) strlen(big), &line) && line == 9,
          "a ninth dead key does not fit");
    strcpy(big, "base E01 aa ab ac ad ae af ag ah ai\n");
    CHECK(!zui_keymap_parse(&k, big, (uint32_t) strlen(big), &line) && line == 1,
          "a ninth string does not fit");
    CHECK(!zui_keymap_load(&k, "klingon") && k.id[0] == 0, "unknown built-in");
    /* length bound is honoured: the second line is outside len */
    const char *cut = "base E01 a\nbase E02 b\n";
    CHECK(zui_keymap_parse(&k, cut, 11, &line) && k.key[e1][0] == 'a' && k.key[e2][0] == 0,
          "parse stops at len");
}

static void test_builtins(void)
{
    static zui_keymap_t k;
    uint32_t n = zui_keymap_builtin_count();
    CHECK(n == 85, "85 built-in layouts (%u)", n);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t line = 0;
        const char *t = zui_keymap_builtin_text(i);
        bool ok = zui_keymap_parse(&k, t, (uint32_t) strlen(t), &line);
        CHECK(ok, "built-in %s parses (line %u)", zui_keymap_builtin_id(i), line);
        CHECK(strcmp(k.id, zui_keymap_builtin_id(i)) == 0, "built-in %s id matches",
              zui_keymap_builtin_id(i));
        CHECK(k.name[0] != 0, "built-in %s has a name", k.id);
        uint32_t home = 0; /* Amharic's home row starts at C02: C01 is a vowel key */
        for (int32_t c = 1; c <= 11; c++) home += k.key[ZUI_KP(ZUI_KP_ROW_C, c)][0] != 0;
        CHECK(home >= 9 && k.key[ZUI_KP_SPACE][0] == ' ', "built-in %s has a home row and a space",
              k.id);
        CHECK(zui_keymap_load(&k, zui_keymap_builtin_id(i)), "load %s by id",
              zui_keymap_builtin_id(i));
    }
    CHECK(zui_keymap_builtin_id(n) == 0 && zui_keymap_builtin_text(n) == 0, "past the end");
    /* ids are unique */
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = i + 1; j < n; j++)
            CHECK(strcmp(zui_keymap_builtin_id(i), zui_keymap_builtin_id(j)) != 0, "id %s twice",
                  zui_keymap_builtin_id(i));
    /* every locale of zui_locale types with a built-in layout */
    static const char *const LOCALES[] = {
        "en", "es",  "fr",  "de", "pt",      "it",      "ru",  "uk", "pl", "ar",  "he",  "fa",
        "ur", "hi",  "bn",  "ta", "zh-Hans", "zh-Hant", "ja",  "ko", "th", "vi",  "tr",  "id",
        "sw", "am",  "yo",  "ha", "el",      "nl",      "mr",  "te", "gu", "kn",  "ml",  "pa",
        "or", "as",  "ne",  "si", "sd",      "ps",      "sat", "dv", "ms", "fil", "jv",  "su",
        "my", "km",  "lo",  "mn", "bo",      "yue",     "kk",  "uz", "ky", "tg",  "tk",  "az",
        "ka", "hy",  "ckb", "ug", "ro",      "cs",      "hu",  "sv", "bg", "sr",  "hr",  "sk",
        "da", "fi",  "nb",  "lt", "be",      "ca",      "cy",  "ga", "zu", "xh",  "ig",  "so",
        "om", "ti",  "rw",  "mg", "sn",      "wo",      "ln",  "af", "ff", "nqo", "zgh", "qu",
        "gn", "chr", "nv",  "mi", "haw",     "iu",      "se",  "ht"};
    for (uint32_t i = 0; i < sizeof LOCALES / sizeof LOCALES[0]; i++)
        CHECK(zui_keymap_load(&k, zui_keymap_for_locale(LOCALES[i])), "%s types with %s",
              LOCALES[i], zui_keymap_for_locale(LOCALES[i]));
    CHECK(!strcmp(zui_keymap_for_locale("pt-BR"), "pt-br") &&
              !strcmp(zui_keymap_for_locale("UK_ua"), "ua") &&
              !strcmp(zui_keymap_for_locale("zh-Hant-TW"), "us") &&
              !strcmp(zui_keymap_for_locale("xx"), "us") &&
              !strcmp(zui_keymap_for_locale(""), "us"),
          "locale fallbacks");
    {
        int32_t D_ = ZUI_KP_ROW_D, C_ = ZUI_KP_ROW_C;
        /* a key of each generated script types its letter */
        CHECK(cp_at("ua", ZUI_KP(D_, 1), false, false) == 0x0439 &&              /* й */
                  cp_at("ua", ZUI_KP(D_, 13), false, false) == 0x0491 &&         /* ґ */
                  cp_at("in-hi", ZUI_KP(C_, 8), false, false) == 0x0915 &&       /* क */
                  cp_at("th", ZUI_KP(D_, 1), false, false) == 0x0E46 &&          /* ๆ */
                  cp_at("ge", ZUI_KP(C_, 1), false, false) == 0x10D0 &&          /* ა */
                  cp_at("cn-tib", ZUI_KP(C_, 2), false, false) == 0x0F66 &&      /* ས */
                  cp_at("mv", ZUI_KP(C_, 2), false, false) == 0x0790 &&          /* ސ */
                  cp_at("gn", ZUI_KP(D_, 1), false, false) == 0x07CA &&          /* ߊ */
                  cp_at("us-chr", ZUI_KP(C_, 1), false, false) == 0x13A0 &&      /* Ꭰ */
                  cp_at("ca-ike", ZUI_KP(D_, 2), false, false) == 0x1403 &&      /* ᐃ */
                  cp_at("ma-tifinagh", ZUI_KP(D_, 1), false, false) == 0x2D30 && /* ⴰ */
                  cp_at("ng-yoruba", ZUI_KP(D_, 1), false, false) == 0x1EB9,     /* ẹ */
              "letters of the generated layouts");
        /* a generated dead key composes: Vietnamese dot below + a */
        static zui_keymap_t v;
        CHECK(zui_keymap_load(&v, "vn"), "load vn");
        int32_t d = -1;
        for (uint32_t i = 0; i < v.ndead; i++)
            if (!strcmp(v.dead[i].name, "belowdot")) d = (int32_t) i;
        CHECK(d >= 0 && zui_keymap_combine(&v, (uint32_t) d, 'a') == 0x1EA1, "vn: dot below + a");
    }

    int32_t E = ZUI_KP_ROW_E, D = ZUI_KP_ROW_D, C = ZUI_KP_ROW_C, B = ZUI_KP_ROW_B;
    CHECK(cp_at("us", ZUI_KP(D, 1), false, false) == 'q' &&
              cp_at("us", ZUI_KP(E, 2), true, false) == '@',
          "US q and shift 2");
    CHECK(cp_at("uk", ZUI_KP(E, 2), true, false) == '"' &&
              cp_at("uk", ZUI_KP(E, 3), true, false) == 0xA3 &&
              cp_at("uk", ZUI_KP(C, 12), false, false) == '#' &&
              cp_at("uk", ZUI_KP(B, 0), false, false) == '\\' &&
              cp_at("uk", ZUI_KP(E, 4), false, true) == 0x20AC,
          "UK \" £ # \\ and AltGr 4 = €");
    CHECK(cp_at("uk", ZUI_KP_D13, false, false) == '#', "UK: the backslash code reaches C12");
    CHECK(cp_at("de", ZUI_KP(D, 6), false, false) == 'z' &&
              cp_at("de", ZUI_KP(B, 1), false, false) == 'y' &&
              cp_at("de", ZUI_KP(C, 10), false, false) == 0xF6 &&
              cp_at("de", ZUI_KP(E, 11), false, false) == 0xDF &&
              cp_at("de", ZUI_KP(D, 1), false, true) == '@',
          "German z/y swap, ö, ß, AltGr q = @");
    CHECK(cp_at("fr", ZUI_KP(D, 1), false, false) == 'a' &&
              cp_at("fr", ZUI_KP(E, 2), false, false) == 0xE9 &&
              cp_at("fr", ZUI_KP(E, 2), true, false) == '2' &&
              cp_at("fr", ZUI_KP(C, 10), false, false) == 'm' &&
              cp_at("fr", ZUI_KP(E, 10), false, true) == '@',
          "AZERTY a, é/2, m, AltGr à = @");
    CHECK(cp_at("es", ZUI_KP(C, 10), false, false) == 0xF1 &&
              cp_at("es", ZUI_KP(E, 12), false, false) == 0xA1,
          "Spanish ñ and ¡");
    CHECK(cp_at("it", ZUI_KP(D, 11), false, false) == 0xE8 &&
              cp_at("it", ZUI_KP(D, 11), true, false) == 0xE9,
          "Italian è / é");
    CHECK(cp_at("pt-br", ZUI_KP(C, 10), false, false) == 0xE7 &&
              cp_at("pt-br", ZUI_KP(B, 11), false, false) == '/' &&
              cp_at("pt-br", ZUI_KP(B, 11), true, false) == '?',
          "ABNT2 ç and the extra / ? key");
    CHECK(cp_at("se", ZUI_KP(D, 11), false, false) == 0xE5 &&
              cp_at("se", ZUI_KP(C, 11), false, false) == 0xE4,
          "Swedish å ä");
    CHECK(cp_at("dvorak", ZUI_KP(C, 2), false, false) == 'o' &&
              cp_at("dvorak", ZUI_KP(D, 1), false, false) == '\'',
          "Dvorak o and '");
    CHECK(cp_at("colemak", ZUI_KP(C, 2), false, false) == 'r' &&
              cp_at("colemak", ZUI_KP(C, 10), false, false) == 'o',
          "Colemak r and o");
    CHECK(cp_at("ru", ZUI_KP(D, 1), false, false) == 0x439 &&
              cp_at("ru", ZUI_KP(E, 3), true, false) == 0x2116,
          "Russian й and №");
    CHECK(cp_at("gr", ZUI_KP(C, 1), false, false) == 0x3B1 &&
              cp_at("gr", ZUI_KP(D, 2), false, false) == 0x3C2,
          "Greek α and ς");
    CHECK(cp_at("he", ZUI_KP(C, 1), false, false) == 0x5E9 &&
              cp_at("he", ZUI_KP(D, 3), false, false) == 0x5E7 &&
              cp_at("he", ZUI_KP(E, 4), false, true) == 0x20AA,
          "Hebrew ש ק and ₪");
    CHECK(cp_at("ar", ZUI_KP(C, 1), false, false) == 0x634 &&
              cp_at("ar", ZUI_KP(E, 0), false, false) == 0x630 &&
              cp_at("ar", ZUI_KP(D, 1), true, false) == 0x64E,
          "Arabic ش ذ and fatha");
}

/* ---- K3 ---------------------------------------------------------------------------- */

static void test_caps(void)
{
    static zui_keymap_t k;
    zui_keymap_load(&k, "fr");
    int32_t e2 = ZUI_KP(ZUI_KP_ROW_E, 2), d1 = ZUI_KP(ZUI_KP_ROW_D, 1);
    CHECK(zui_keymap_get(&k, d1, false, false, true) == 'A', "caps gives A");
    CHECK(zui_keymap_get(&k, d1, true, false, true) == 'a', "caps + shift gives a");
    CHECK(zui_keymap_get(&k, e2, false, false, true) == 0xE9, "caps leaves é (shift is 2)");
    zui_keymap_load(&k, "de");
    CHECK(zui_keymap_get(&k, ZUI_KP(ZUI_KP_ROW_C, 10), false, false, true) == 0xD6,
          "German caps ö = Ö");
    CHECK(zui_keymap_get(&k, ZUI_KP(ZUI_KP_ROW_E, 1), false, false, true) == '1',
          "caps never makes 1 into !");
    CHECK(zui_keymap_get(&k, ZUI_KP(ZUI_KP_ROW_E, 11), false, false, true) == 0xDF,
          "ß has no single capital on the key: unchanged");
    zui_keymap_load(&k, "uk");
    CHECK(zui_keymap_get(&k, ZUI_KP(ZUI_KP_ROW_D, 3), false, true, true) == 0xC9,
          "caps flips the AltGr pair too (é -> É)");
    zui_keymap_load(&k, "ru");
    CHECK(zui_keymap_get(&k, ZUI_KP(ZUI_KP_ROW_E, 0), false, false, true) == 0x401,
          "Russian caps ё = Ё");
    zui_keymap_load(&k, "gr");
    CHECK(zui_keymap_get(&k, ZUI_KP(ZUI_KP_ROW_D, 2), false, false, true) == 0x3C2,
          "Greek ς is not flipped (shift is ΅)");
    zui_keymap_load(&k, "he");
    CHECK(zui_keymap_get(&k, ZUI_KP(ZUI_KP_ROW_C, 1), false, false, true) == 0x5E9,
          "Hebrew letters have no case");
    const char *none = "caps none\nbase D01 q\nshift D01 Q\n";
    zui_keymap_parse(&k, none, (uint32_t) strlen(none), 0);
    CHECK(zui_keymap_get(&k, ZUI_KP(ZUI_KP_ROW_D, 1), false, false, true) == 'q', "caps none");
    CHECK(zui_keymap_get(&k, -1, false, false, false) == 0 &&
              zui_keymap_get(&k, ZUI_KP_COUNT, false, false, false) == 0 &&
              zui_keymap_get(0, 0, false, false, false) == 0,
          "out of range");
}

static void test_shortcut(void)
{
    static zui_keymap_t k;
    zui_keymap_load(&k, "de");
    CHECK(zui_keymap_shortcut(&k, ZUI_KP(ZUI_KP_ROW_D, 6)) == 'z', "German Z key is 'z'");
    CHECK(zui_keymap_shortcut(&k, ZUI_KP(ZUI_KP_ROW_E, 0)) == '`', "dead key falls back to US");
    CHECK(zui_keymap_shortcut(&k, ZUI_KP(ZUI_KP_ROW_C, 10)) == ';', "ö falls back to US ;");
    zui_keymap_load(&k, "fr");
    CHECK(zui_keymap_shortcut(&k, ZUI_KP(ZUI_KP_ROW_E, 1)) == '1', "AZERTY & key is '1'");
    CHECK(zui_keymap_shortcut(&k, ZUI_KP(ZUI_KP_ROW_D, 1)) == 'a', "AZERTY a");
    zui_keymap_load(&k, "ru");
    CHECK(zui_keymap_shortcut(&k, ZUI_KP(ZUI_KP_ROW_B, 3)) == 'c', "Russian с key copies as c");
    zui_keymap_load(&k, "dvorak");
    CHECK(zui_keymap_shortcut(&k, ZUI_KP(ZUI_KP_ROW_D, 8)) == 'c', "Dvorak c where US has i");
    CHECK(zui_keymap_shortcut(&k, ZUI_KP_SPACE) == ' ' && zui_keymap_shortcut(&k, -3) == 0,
          "space; nothing");
    CHECK(zui_keymap_shortcut(0, ZUI_KP(ZUI_KP_ROW_B, 1)) == 'z', "no layout: US");
}

/* ---- K4, K6: translator ------------------------------------------------------------ */

static void test_translate(void)
{
    zui_keymap_load(&KM, "us");
    zui_kbd_init(&KB, &KM);
    reset_log();
    press(K_A, true);
    CHECK(NEV == 2 && EV[0].type == ZUI_EV_KEY_DOWN && EV[0].key == 'a' && EV[0].mods == 0 &&
              EV[1].type == ZUI_EV_TEXT && strcmp(EV[1].text, "a") == 0 && EV[1].ms == 100,
          "K6: key down, then text");
    press(K_A, false);
    CHECK(NEV == 3 && EV[2].type == ZUI_EV_KEY_UP && EV[2].key == 'a', "key up");
    reset_log();
    tap_shift(K_A);
    CHECK(strcmp(TYPED, "A") == 0 && EV[0].mods == ZUI_MOD_SHIFT && EV[0].key == 'a',
          "shift: text A, key still 'a'");
    reset_log();
    press(K_RSHIFT, true);
    tap(K_2);
    press(K_RSHIFT, false);
    CHECK(strcmp(TYPED, "@") == 0, "right shift");
    reset_log();
    press(K_LCTRL, true);
    tap(K_C);
    press(K_LCTRL, false);
    CHECK(NEV == 2 && TYPED[0] == 0 && EV[0].key == 'c' &&
              EV[0].mods == (ZUI_MOD_CTRL | ZUI_MOD_CMD),
          "ctrl: no text; CMD rides with Ctrl");
    reset_log();
    KB.cmd_is_meta = true;
    press(K_LMETA, true);
    tap(K_C);
    press(K_LMETA, false);
    CHECK(TYPED[0] == 0 && EV[0].mods == (ZUI_MOD_META | ZUI_MOD_CMD), "meta is CMD when asked");
    press(K_LCTRL, true);
    CHECK(zui_kbd_mods(&KB) == ZUI_MOD_CTRL, "then ctrl alone is not CMD");
    press(K_LCTRL, false);
    KB.cmd_is_meta = false;
    reset_log();
    press(K_LALT, true);
    tap(K_A);
    press(K_LALT, false);
    CHECK(TYPED[0] == 0 && EV[0].mods == ZUI_MOD_ALT, "alt: shortcut, no text");
    reset_log();
    press(K_RALT, true);
    tap(K_A);
    press(K_RALT, false);
    CHECK(TYPED[0] == 0 && EV[0].mods == ZUI_MOD_ALT, "US has no AltGr: right alt is alt");

    /* caps */
    reset_log();
    tap(K_CAPS);
    CHECK(NEV == 0 && KB.caps, "caps lock is state only");
    tap(K_A);
    tap(K_1);
    tap_shift(K_A);
    tap(K_CAPS);
    tap(K_A);
    CHECK(strcmp(TYPED, "A1aa") == 0, "caps letters: A 1, shift gives a, off again (%s)", TYPED);
    reset_log();
    zui_event_t ev[4];
    uint32_t n = zui_kbd_feed(&KB, K_CAPS, true, true, 1, ev, 4);
    CHECK(n == 0 && !KB.caps, "a repeating caps key does not toggle");

    /* specials */
    reset_log();
    tap(K_ENTER);
    tap(K_BKSP);
    tap(K_TAB);
    tap(K_ESC);
    tap(K_LEFT);
    tap(K_F1);
    tap(K_MENU);
    CHECK(NEV == 14 && TYPED[0] == 0 && EV[0].key == ZUI_KEY_ENTER &&
              EV[2].key == ZUI_KEY_BACKSPACE && EV[4].key == ZUI_KEY_TAB &&
              EV[6].key == ZUI_KEY_ESCAPE && EV[8].key == ZUI_KEY_LEFT &&
              EV[10].key == ZUI_KEY_F1 && EV[12].key == ZUI_KEY_MENU,
          "non-text keys are keys only");
    reset_log();
    zui_event_t e2[4];
    CHECK(zui_kbd_feed(&KB, 88, true, false, 0, e2, 4) == 1 && e2[0].key == ZUI_KEY_F12 &&
              zui_kbd_feed(&KB, 87, true, false, 0, e2, 4) == 1 && e2[0].key == ZUI_KEY_F1 + 10 &&
              zui_kbd_feed(&KB, 111, true, false, 0, e2, 4) == 1 && e2[0].key == ZUI_KEY_DELETE &&
              zui_kbd_feed(&KB, 158, true, false, 0, e2, 4) == 1 && e2[0].key == ZUI_KEY_BACK,
          "F11 F12 delete back");
    CHECK(zui_kbd_feed(&KB, 240, true, false, 0, e2, 4) == 0, "unknown codes are dropped");
    tap(K_KP7);
    tap(K_KPDOT);
    tap(K_KPENTER);
    CHECK(strcmp(TYPED, "7.") == 0 && EV[NEV - 2].key == ZUI_KEY_ENTER, "keypad");
    reset_log();
    tap(K_SPACE);
    CHECK(strcmp(TYPED, " ") == 0 && EV[0].key == ZUI_KEY_SPACE, "space");

    /* repeat */
    n = zui_kbd_feed(&KB, K_A, true, true, 7, ev, 4);
    CHECK(n == 2 && ev[0].repeat == 1 && strcmp(ev[1].text, "a") == 0, "a held key repeats text");

    /* cap on the output */
    n = zui_kbd_feed(&KB, K_A, true, false, 7, ev, 1);
    CHECK(n == 1 && ev[0].type == ZUI_EV_KEY_DOWN, "never more than cap");
    CHECK(zui_kbd_feed(&KB, K_A, false, false, 7, ev, 0) == 0, "cap 0");

    /* compose key */
    KB.compose_code = K_MENU;
    reset_log();
    tap(K_MENU);
    CHECK(NEV == 2 && EV[0].key == ZUI_KEY_COMPOSE && EV[1].type == ZUI_EV_KEY_UP,
          "the chosen key sends compose");
    KB.compose_code = 0;

    /* queue */
    static zui_queue_t q;
    zui_queue_init(&q);
    CHECK(zui_kbd_feed_queue(&KB, &q, K_B, true, false, 3) == 2 && zui_queue_len(&q) == 2,
          "pushed onto a queue");
    zui_event_t pe;
    zui_pop(&q, &pe);
    CHECK(pe.type == ZUI_EV_KEY_DOWN && pe.key == 'b', "queued key");
    zui_pop(&q, &pe);
    CHECK(pe.type == ZUI_EV_TEXT && strcmp(pe.text, "b") == 0, "queued text");

    /* no layout */
    zui_kbd_t bare;
    zui_kbd_init(&bare, 0);
    n = zui_kbd_feed(&bare, K_A, true, false, 0, ev, 4);
    CHECK(n == 1 && ev[0].key == 'a', "without a layout: keys, no text");
    n = zui_kbd_feed(&bare, K_KP7, true, false, 0, ev, 4);
    CHECK(n == 2 && strcmp(ev[1].text, "7") == 0, "keypad still types");
    n = zui_kbd_feed(&bare, K_RALT, true, false, 0, ev, 4);
    CHECK(n == 0 && zui_kbd_mods(&bare) == ZUI_MOD_ALT, "right alt is alt");
}

static void test_altgr(void)
{
    zui_keymap_load(&KM, "de");
    zui_kbd_init(&KB, &KM);
    reset_log();
    tap_altgr(K_Q);
    CHECK(strcmp(TYPED, "@") == 0, "German AltGr q = @");
    CHECK(EV[0].mods == 0, "AltGr is not Alt: no ALT in mods");
    tap_altgr(K_E);
    tap_altgr(8); /* 7 */
    CHECK(strcmp(TYPED, "@€{") == 0, "AltGr e = €, AltGr 7 = {");
    reset_log();
    tap(K_Y);
    tap(K_Z);
    tap(K_SEMI);
    tap(K_102ND);
    tap_shift(K_102ND);
    CHECK(strcmp(TYPED, "zyö<>") == 0, "German letters and the ISO key (%s)", TYPED);
    reset_log();
    press(K_RALT, true);
    press(K_LSHIFT, true);
    tap(K_Q);
    press(K_LSHIFT, false);
    press(K_RALT, false);
    CHECK(TYPED[0] == 0 && NEV == 2, "AltGr+shift with nothing there types nothing");
    zui_keymap_load(&KM, "uk");
    zui_kbd_set_keymap(&KB, &KM);
    reset_log();
    tap(K_BSLASH);
    tap_shift(K_BSLASH);
    tap(K_102ND);
    CHECK(strcmp(TYPED, "#~\\") == 0, "UK: the backslash code types # (C12)");
}

static void test_dead(void)
{
    zui_keymap_load(&KM, "de");
    zui_kbd_init(&KB, &KM);
    uint16_t acute = 13; /* German E12: dead acute */
    reset_log();
    tap(acute);
    CHECK(TYPED[0] == 0 && strcmp(PRE, "´") == 0 && KB.dead >= 0,
          "a dead key types nothing and shows its accent");
    CHECK(NEV == 3 && EV[0].type == ZUI_EV_KEY_DOWN && EV[1].type == ZUI_EV_PREEDIT &&
              EV[2].type == ZUI_EV_KEY_UP,
          "key, preedit, key up");
    tap(K_E);
    CHECK(strcmp(TYPED, "é") == 0 && PRE[0] == 0 && KB.dead < 0, "dead + e = é, preedit cleared");
    reset_log();
    tap(acute);
    tap(K_SPACE);
    CHECK(strcmp(TYPED, "´") == 0, "K4: dead + space = the accent");
    reset_log();
    tap(acute);
    tap(acute);
    CHECK(strcmp(TYPED, "´") == 0 && KB.dead < 0, "dead twice = the accent");
    reset_log();
    tap(acute);
    tap(K_X);
    CHECK(strcmp(TYPED, "´x") == 0, "dead + invalid = both characters");
    reset_log();
    tap(K_GRAVE); /* ^ */
    tap(acute);
    CHECK(strcmp(TYPED, "^") == 0 && strcmp(PRE, "´") == 0, "dead + other dead: first out");
    tap(K_A);
    CHECK(strcmp(TYPED, "^á") == 0, "second still combines");
    reset_log();
    tap(K_GRAVE);
    tap(K_BKSP);
    CHECK(TYPED[0] == 0 && KB.dead < 0 && PRE[0] == 0, "backspace drops a waiting dead key");
    CHECK(NEV == 5 && EV[3].type == ZUI_EV_PREEDIT && EV[4].type == ZUI_EV_KEY_UP,
          "...and is used up doing so");
    reset_log();
    tap(K_GRAVE);
    tap(K_ESC);
    CHECK(KB.dead < 0 && NEV == 5, "escape drops it the same way");
    reset_log();
    tap(K_GRAVE);
    tap(K_LEFT);
    CHECK(KB.dead < 0 && NEV == 6 && EV[4].type == ZUI_EV_KEY_DOWN && EV[4].key == ZUI_KEY_LEFT,
          "other keys drop it and still work");
    reset_log();
    tap(K_GRAVE);
    tap_shift(K_I);
    CHECK(strcmp(TYPED, "Î") == 0, "dead + shifted letter = Î");
    reset_log();
    tap(K_GRAVE);
    tap(K_LSHIFT);
    tap(K_O);
    CHECK(strcmp(TYPED, "ô") == 0, "modifier keys do not disturb a dead key");
    reset_log();
    tap_shift(acute); /* shift E12 = dead grave */
    tap(K_A);
    CHECK(strcmp(TYPED, "à") == 0, "shifted dead key: grave");
    reset_log();
    tap(K_GRAVE);
    zui_event_t ev[4];
    uint32_t n = zui_kbd_feed(&KB, K_GRAVE, true, true, 1, ev, 4);
    CHECK(n == 1 && KB.dead >= 0, "a repeating dead key does nothing more");
    press(K_LCTRL, true);
    tap(K_A);
    press(K_LCTRL, false);
    CHECK(KB.dead >= 0 && TYPED[0] == 0, "a shortcut leaves the dead key waiting");
    tap(K_U);
    CHECK(strcmp(TYPED, "û") == 0, "and it still combines");
    reset_log();
    tap(K_GRAVE);
    zui_kbd_set_keymap(&KB, &KM);
    CHECK(KB.dead < 0, "changing layouts drops it");

    /* without preedit */
    reset_log();
    KB.dead_preedit = false;
    tap(acute);
    tap(K_E);
    CHECK(strcmp(TYPED, "é") == 0 && NEV == 5, "no PREEDIT events when switched off");
    KB.dead_preedit = true;

    /* compose key cancels a dead key */
    KB.compose_code = K_MENU;
    reset_log();
    tap(acute);
    tap(K_MENU);
    CHECK(KB.dead < 0 && PRE[0] == 0, "compose drops the dead key");
    KB.compose_code = 0;

    /* US-International */
    static const uint16_t intl1[] = {K_APOS, K_E, K_APOS, K_C, K_APOS, K_SPACE, K_APOS, K_S};
    CHECK(strcmp(typed_after("us-intl", intl1, 8), "éç''s") == 0,
          "US-Intl: 'e = é, 'c = ç, ' space = ', ' s = 's (%s)", TYPED);
    reset_log();
    tap_shift(K_APOS);
    tap(K_U);
    tap_shift(K_6);
    tap(K_O);
    tap_shift(K_GRAVE);
    tap(K_N);
    tap(K_GRAVE);
    tap(K_A);
    CHECK(strcmp(TYPED, "üôñà") == 0, "US-Intl \" ^ ~ ` (%s)", TYPED);
    reset_log();
    tap_shift(K_APOS);
    tap(K_SPACE);
    CHECK(strcmp(TYPED, "\"") == 0, "US-Intl \" + space = \"");
    reset_log();
    tap_altgr(K_S);
    CHECK(strcmp(TYPED, "ß") == 0, "US-Intl AltGr s = ß");

    /* French ^ and ¨ */
    static const uint16_t fr1[] = {K_LBRACE, K_E, K_LBRACE, K_SPACE};
    CHECK(strcmp(typed_after("fr", fr1, 4), "ê^") == 0, "French ^e and ^ space");
    reset_log();
    tap_shift(K_LBRACE);
    tap(K_I);
    CHECK(strcmp(TYPED, "ï") == 0, "French ¨i");
    reset_log();
    tap_altgr(K_2);
    tap(K_N);
    CHECK(strcmp(TYPED, "ñ") == 0, "French AltGr dead tilde");

    /* Greek tonos */
    static const uint16_t gr1[] = {K_SEMI, K_A, K_SEMI, K_O, K_SEMI, K_SPACE};
    CHECK(strcmp(typed_after("gr", gr1, 6), "άό΄") == 0, "Greek tonos (%s)", TYPED);
    reset_log();
    tap_shift(K_SEMI);
    tap(K_I);
    CHECK(strcmp(TYPED, "ϊ") == 0, "Greek dialytika");

    /* Spanish */
    static const uint16_t es1[] = {K_APOS, K_A, K_LBRACE, K_E};
    CHECK(strcmp(typed_after("es", es1, 4), "áè") == 0, "Spanish ´ and `");
    /* Portuguese */
    static const uint16_t br1[] = {K_APOS, K_A, K_APOS, K_O, K_LBRACE, K_E};
    CHECK(strcmp(typed_after("pt-br", br1, 6), "ãõé") == 0, "ABNT2 ~a ~o ´e (%s)", TYPED);
    /* Swedish */
    static const uint16_t se1[] = {K_RBRACE, K_O, K_LBRACE, K_SEMI, K_APOS};
    CHECK(strcmp(typed_after("se", se1, 5), "öåöä") == 0, "Swedish ¨o å ö ä (%s)", TYPED);
    /* Arabic multi-character key */
    static const uint16_t ar1[] = {K_B, K_A};
    CHECK(strcmp(typed_after("ar", ar1, 2), "لاش") == 0, "Arabic lam-alef key types two");
    reset_log();
    tap_shift(K_Q);
    CHECK(strcmp(TYPED, "\xD9\x8E") == 0, "Arabic shift q = fatha");
    /* a dead key then a string: both */
    static zui_keymap_t mix;
    const char *mt = "base D01 dead:acute\nbase D02 xy\n";
    zui_keymap_parse(&mix, mt, (uint32_t) strlen(mt), 0);
    zui_kbd_init(&KB, &mix);
    reset_log();
    tap(K_Q);
    tap(K_W);
    CHECK(strcmp(TYPED, "´xy") == 0, "dead + string = both");
    reset_log();
    tap(K_Q);
    tap(K_E); /* D03: nothing on this layout */
    CHECK(TYPED[0] == 0 && KB.dead >= 0, "a key with nothing leaves the dead key waiting");
}

static void test_layouts_type(void)
{
    static const uint16_t word[] = {K_Q, K_W, K_E, K_R, K_Y, K_A, K_S, K_D, K_Z, K_M};
    CHECK(strcmp(typed_after("us", word, 10), "qweryasdzm") == 0, "US");
    CHECK(strcmp(typed_after("de", word, 10), "qwerzasdym") == 0, "German");
    CHECK(strcmp(typed_after("fr", word, 10), "azeryqsdw,") == 0, "French");
    CHECK(strcmp(typed_after("dvorak", word, 10), "',.pfaoe;m") == 0, "Dvorak (%s)", TYPED);
    CHECK(strcmp(typed_after("colemak", word, 10), "qwfpjarszm") == 0, "Colemak (%s)", TYPED);
    CHECK(strcmp(typed_after("ru", word, 10), "йцукнфывяь") == 0, "Russian (%s)", TYPED);
    CHECK(strcmp(typed_after("gr", word, 10), ";ςερυασδζμ") == 0, "Greek (%s)", TYPED);
    CHECK(strcmp(typed_after("he", word, 10), "/'קרטשדגזצ") == 0, "Hebrew (%s)", TYPED);
    static const uint16_t h1[] = {K_H, K_J};
    CHECK(utf8_cp(typed_after("ar", h1, 2)) == 0x627, "Arabic h = alef");
    CHECK(strcmp(typed_after("it", word, 10), "qweryasdzm") == 0, "Italian");
    zui_keymap_load(&KM, "ru");
    zui_kbd_init(&KB, &KM);
    reset_log();
    tap(K_CAPS);
    tap(K_GRAVE);
    tap(K_Q);
    tap(K_1);
    CHECK(strcmp(TYPED, "ЁЙ1") == 0, "Russian caps (%s)", TYPED);
}

int main(void)
{
    test_utf8_case();
    test_scancodes();
    test_loader();
    test_builtins();
    test_caps();
    test_shortcut();
    test_translate();
    test_altgr();
    test_dead();
    test_layouts_type();
    printf("%s test_zui_keymap: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
