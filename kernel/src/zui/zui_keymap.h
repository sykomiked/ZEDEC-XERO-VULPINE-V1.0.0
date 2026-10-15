/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_keymap.h — keyboard layouts: where a key sits, and what it types.
 *
 * A keyboard reports which physical key moved; the layout decides what that
 * key means. This follows the CLDR keyboard model, so a layout is data that
 * can be read from text, and the built-in set is that same text.
 *
 *   K1 POSITIONS, NOT LETTERS. A key is named by where it sits on the ISO
 *      grid: row E is the digit row, D the top letter row, C the home row,
 *      B the bottom row; columns count from the left (E00 is the key left of
 *      1, B00 the extra key left of Z on ISO boards, C12 the key left of
 *      Enter on ISO boards, D13 the key above Enter on ANSI boards; both
 *      report the same scan code, so a layout that defines only one of them
 *      gets it either way). Scan codes (evdev key codes, USB HID usages)
 *      turn into positions; layouts turn positions into characters.
 *   K2 FOUR LEVELS per key: base, shift, AltGr, AltGr+shift. Right Alt is
 *      AltGr on a layout that defines any AltGr character, plain Alt
 *      otherwise.
 *   K3 CAPS LOCK. "caps letters" (the default) flips shift only on keys
 *      whose shift character is the capital of the base character, so Caps
 *      gives É on a French é key only when the layout says so and never
 *      turns 1 into !. "caps shift" is a shift lock for every key that has a
 *      shift character. "caps none" ignores it. Caps with shift gives back
 *      the small letter.
 *   K4 DEAD KEYS type nothing at once; the next character is looked up in
 *      the dead key's combination table. dead + space = the accent itself;
 *      dead + the same dead key = the accent itself; dead + a character with
 *      no entry = the accent, then the character; dead + another dead key =
 *      the first accent, and the second waits. Backspace and Escape drop a
 *      waiting dead key (and are used up doing so); any other key that is
 *      not text drops it and then does its own work. While a dead key waits
 *      its accent is shown as PREEDIT (zui_input.h I4), cleared by an empty
 *      PREEDIT when it resolves.
 *   K5 LAYOUTS ARE DATA. zui_keymap_parse reads the text format below; the
 *      built-in layouts are text in that format, so a new layout needs no
 *      code. Errors report the line.
 *   K6 KEY THEN TEXT. Every press is a ZUI_EV_KEY_DOWN whose key is the
 *      shortcut value (the layout's own letter when it is ASCII, the digit
 *      on a key whose shift level is a digit, else the US letter at that
 *      place, so Cmd-C works on any layout); the character it types follows
 *      as ZUI_EV_TEXT. Ctrl, Alt (not AltGr) and Meta type no text. Release
 *      is ZUI_EV_KEY_UP. Modifier keys and Caps Lock change state only.
 *   K7 Integer only, freestanding, no allocation: the caller owns every
 *      zui_keymap_t and zui_kbd_t.
 *
 * Text format, one directive per line; '#' at the start of a line is a
 * comment; tokens are separated by spaces:
 *
 *   keyboard <id> <name words...>
 *   caps letters | shift | none
 *   <level> <POS> <tok> <tok> ...   level: base, shift, altgr, altgr+shift
 *                                   (altR and altR+shift are accepted too);
 *                                   the tokens fill POS and the columns to
 *                                   its right (A03 is the space bar)
 *   deadkey <name> <accent> <pair> ...
 *                                   define or replace a dead key: <accent> is
 *                                   what dead + space types; each <pair> is
 *                                   two characters, base then result ("eé"),
 *                                   or =<name> to include a standard table;
 *                                   a later pair for the same base wins
 *
 * A <tok> is one character, several characters (typed together, at most 15
 * bytes), "--" for nothing, dead:<name> for a dead key (a deadkey line of
 * that name, else a standard table: grave acute circumflex tilde diaeresis
 * ring cedilla caron macron tonos), \uXXXX or \u{X..} for a code point.
 */
#ifndef ZXV_ZUI_KEYMAP_H
#define ZXV_ZUI_KEYMAP_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_input.h"

/* ---- K1: positions --------------------------------------------------------- */
enum { ZUI_KP_ROW_E = 0, ZUI_KP_ROW_D = 1, ZUI_KP_ROW_C = 2, ZUI_KP_ROW_B = 3 };
#define ZUI_KP(row, col) ((row) * 16 + (col))
#define ZUI_KP_SPACE     64 /* A03 */
#define ZUI_KP_COUNT     65
#define ZUI_KP_D13       ZUI_KP(ZUI_KP_ROW_D, 13)
#define ZUI_KP_C12       ZUI_KP(ZUI_KP_ROW_C, 12)

/* Keys zui_input.h has no code for. High values, clear of ZUI_KEY_*. */
enum {
    ZUI_KEY_COMPOSE = 0x1C0 /* the compose key (zui_kbd_t.compose_code) */
};

/* ---- K2: levels and values ------------------------------------------------- */
enum { ZUI_KM_BASE = 0, ZUI_KM_SHIFT, ZUI_KM_ALTGR, ZUI_KM_ALTGR_SHIFT, ZUI_KM_LEVELS };

/* A key value is 0 (nothing), a code point, or one of these tagged forms. */
#define ZUI_KM_DEAD       0x80000000u /* dead key; low byte = dead index        */
#define ZUI_KM_STR        0x40000000u /* several characters; low byte = string  */
#define ZUI_KM_IS_DEAD(v) (((v) & ZUI_KM_DEAD) != 0u)
#define ZUI_KM_IS_STR(v)  (((v) & ZUI_KM_STR) != 0u)

#define ZUI_KM_DEAD_MAX  8u
#define ZUI_KM_COMBO_MAX 48u
#define ZUI_KM_STR_MAX   8u
#define ZUI_KM_STR_BYTES 16u

typedef enum { ZUI_CAPS_LETTERS = 0, ZUI_CAPS_SHIFT, ZUI_CAPS_NONE } zui_caps_mode_t;

typedef struct {
    char name[16];
    uint32_t accent; /* what dead + space types              */
    uint32_t n;
    uint32_t from[ZUI_KM_COMBO_MAX];
    uint32_t to[ZUI_KM_COMBO_MAX];
} zui_km_dead_t;

typedef struct {
    char id[16];
    char name[48];
    uint32_t key[ZUI_KP_COUNT][ZUI_KM_LEVELS];
    uint8_t caps[ZUI_KP_COUNT]; /* K3: bit 0 base/shift flips, bit 1 AltGr pair */
    uint8_t caps_mode;          /* zui_caps_mode_t                           */
    bool has_altgr;
    uint32_t ndead;
    zui_km_dead_t dead[ZUI_KM_DEAD_MAX];
    uint32_t nstr;
    char str[ZUI_KM_STR_MAX][ZUI_KM_STR_BYTES];
} zui_keymap_t;

/* ---- K5: loading ------------------------------------------------------------- */
/* Parse layout text (len bytes, need not be NUL-terminated). On failure the
 * keymap is left empty and *err_line (if given) is the 1-based bad line. */
bool zui_keymap_parse(zui_keymap_t *km, const char *text, uint32_t len, uint32_t *err_line);

/* The built-in set: us uk us-intl de fr es it pt-br se dvorak colemak ru gr
 * he ar, then 70 layouts generated from xkeyboard-config by
 * tools/zui/gen_keymaps.py (zui_keymap_xkb.inc): a layout for every script
 * and language of zui_locale's 104 locales (InScript for the Indic scripts,
 * Thai, Lao, Khmer, Burmese, Tibetan, Ge'ez, Georgian, Armenian, Thaana,
 * Ol Chiki, N'Ko, Tifinagh, Cherokee, Inuktitut syllabics, the Cyrillic and
 * Arabic-script languages, and the Latin ones with their own letters). */
uint32_t zui_keymap_builtin_count(void);
const char *zui_keymap_builtin_id(uint32_t i);   /* 0 past the end */
const char *zui_keymap_builtin_text(uint32_t i); /* the layout as text */
bool zui_keymap_load(zui_keymap_t *km, const char *id);
/* The built-in layout a locale types with (BCP 47 tag, any case; the whole
 * tag, then its language); "us" when none is listed. Chinese, Cantonese,
 * Japanese and Korean give a Latin layout for zui_ime to compose from. */
const char *zui_keymap_for_locale(const char *tag);

/* ---- lookups ------------------------------------------------------------------ */
/* Scan codes to positions: -1 when the key is not a character key. */
int32_t zui_kpos_from_evdev(uint16_t code);
uint16_t zui_evdev_from_hid(uint16_t usage); /* 0 when unknown */

/* K2, K3: the value at pos for these modifiers (D13 and C12 stand in for
 * each other). */
uint32_t zui_keymap_get(const zui_keymap_t *km, int32_t pos, bool shift, bool altgr, bool caps);
/* UTF-8 of a value into dst (cap bytes, NUL-terminated); a dead key gives
 * its accent. Returns the byte count. */
uint32_t zui_keymap_utf8(const zui_keymap_t *km, uint32_t v, char *dst, uint32_t cap);
/* K6: the shortcut value of a position. */
uint16_t zui_keymap_shortcut(const zui_keymap_t *km, int32_t pos);
/* K4: base cp under dead key index d; 0 when the table has no entry. */
uint32_t zui_keymap_combine(const zui_keymap_t *km, uint32_t d, uint32_t cp);

/* Character helpers (simple case pairs for Latin, Greek and Cyrillic). */
uint32_t zui_km_upper(uint32_t cp);
uint32_t zui_km_lower(uint32_t cp);
bool zui_km_is_letter(uint32_t cp);
uint32_t zui_km_utf8_put(char *dst, uint32_t cp);                  /* 0..4 bytes  */
uint32_t zui_km_utf8_get(const char *s, uint32_t n, uint32_t *cp); /* bytes used */

/* ---- K4, K6: the translator ------------------------------------------------- */
#define ZUI_KBD_EV_MAX 4u /* events one zui_kbd_feed can write */

typedef struct {
    const zui_keymap_t *km;
    uint16_t held;         /* modifier keys down (ZUI_KBD_H_* in the .c) */
    bool caps;             /* Caps Lock on                              */
    int16_t dead;          /* waiting dead key index, -1 none            */
    bool cmd_is_meta;      /* the shortcut key (ZUI_MOD_CMD) is Meta     */
    bool dead_preedit;     /* show a waiting dead key as PREEDIT         */
    uint16_t compose_code; /* evdev code that sends ZUI_KEY_COMPOSE, 0 none */
} zui_kbd_t;

void zui_kbd_init(zui_kbd_t *k, const zui_keymap_t *km);
/* Swap layouts; a waiting dead key is dropped (no event). */
void zui_kbd_set_keymap(zui_kbd_t *k, const zui_keymap_t *km);
/* One physical key change (evdev code). Writes up to ZUI_KBD_EV_MAX events
 * (never more than cap) and returns how many. */
uint32_t zui_kbd_feed(zui_kbd_t *k, uint16_t code, bool down, bool repeat, uint32_t ms,
                      zui_event_t *out, uint32_t cap);
/* The same, pushed onto a queue. */
uint32_t zui_kbd_feed_queue(zui_kbd_t *k, zui_queue_t *q, uint16_t code, bool down, bool repeat,
                            uint32_t ms);
uint8_t zui_kbd_mods(const zui_kbd_t *k); /* ZUI_MOD_* now held */

#endif /* ZXV_ZUI_KEYMAP_H */
