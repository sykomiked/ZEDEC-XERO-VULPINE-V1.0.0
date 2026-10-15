/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_ime.h — input methods: typing what the keyboard has no key for.
 *
 * An input method sits between the keyboard (zui_keymap's translator, the
 * on-screen keyboard, a present layer) and the app. It watches KEY and TEXT
 * events, and while a composition is open it keeps them, shows what is
 * being composed as PREEDIT and, when done, sends the result as TEXT. The
 * app sees the same events it already handles (zui_input.h I4).
 *
 *   M1 ENGINES. An engine is a small table of functions (zui_ime_engine_t):
 *      feed a key, feed a character, commit, cancel, choose a candidate. It
 *      keeps its state in zui_ime_t and reports through it: the preedit
 *      string, the candidate list, and text to commit. Built in: Unicode
 *      hex entry, compose sequences, and a table-driven phonetic engine
 *      (with a tiny sample Pinyin table to prove the candidate path).
 *   M2 LIFE CYCLE. A composition opens when the preedit becomes non-empty
 *      and closes when it is empty again. Each change of the preedit is one
 *      PREEDIT event; closing is an empty PREEDIT. Committed text is TEXT
 *      (split on character boundaries to fit events). When a composition
 *      closes with a commit, the empty PREEDIT comes first, then the TEXT;
 *      when a commit leaves a composition open, the TEXT comes first, then
 *      the new PREEDIT.
 *   M3 WHAT IS KEPT. While composing, every key press without Ctrl, Alt or
 *      Meta is kept from the app (its key up too); keys that are not text
 *      go to the engine, which may use them (Backspace, Enter, arrows) or
 *      decline. A declined key, a shortcut, or losing focus commits the
 *      composition first and then passes on. When idle, only keys and
 *      characters the engine claims (to open a composition) are kept.
 *   M4 CANDIDATES. The candidate list, the highlighted one and the page
 *      (ZUI_IME_PAGE per page) are state here, with the caret position the
 *      app reports, so any UI can draw the window (zui_ime_window). Arrows
 *      move the highlight, Page Up / Page Down turn pages, 1..9 choose on
 *      the page, space chooses the highlighted one.
 *   M5 No allocation, integer only, deterministic: the same events give the
 *      same output.
 *   M6 LANGUAGES. Pinyin, Traditional Pinyin and Jyutping (single Han
 *      characters from Unihan), Japanese kana with Joyo kanji, two-set
 *      Hangul, and Ge'ez syllables; zui_ime_for_locale picks one for a
 *      locale (zui_ime_lang.c).
 */
#ifndef ZXV_ZUI_IME_H
#define ZXV_ZUI_IME_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_input.h"
#include "zui_keymap.h"

#define ZUI_IME_PRE_MAX    64u  /* bytes of preedit, with the NUL          */
#define ZUI_IME_CAND_MAX   64u  /* candidates kept                         */
#define ZUI_IME_CAND_BYTES 24u  /* bytes of one candidate, with the NUL    */
#define ZUI_IME_COMMIT_MAX 128u /* bytes of text waiting to be sent        */
#define ZUI_IME_PAGE       9u   /* candidates per page (keys 1..9)         */
#define ZUI_IME_EV_MAX     16u  /* a sensible out[] size for zui_ime_feed  */

typedef struct zui_ime zui_ime_t;

/* M1: an engine. Every function is optional except key and text. */
typedef struct {
    const char *id;
    const char *name;
    const void *data;                                        /* engine tables          */
    void (*reset)(zui_ime_t *ime);                           /* fresh state            */
    bool (*key)(zui_ime_t *ime, uint16_t key, uint8_t mods); /* true: used          */
    bool (*text)(zui_ime_t *ime, uint32_t cp);               /* true: used             */
    void (*commit)(zui_ime_t *ime);                          /* finish: send what is composed */
    void (*cancel)(zui_ime_t *ime);                          /* drop the composition   */
    void (*select)(zui_ime_t *ime, uint32_t index);          /* a candidate was chosen */
} zui_ime_engine_t;

typedef struct {
    char text[ZUI_IME_CAND_BYTES];
    uint8_t used; /* preedit bytes this candidate replaces (phonetic engine) */
} zui_ime_cand_t;

/* Built-in engines' state. */
typedef struct {
    uint32_t value, digits;
} zui_ime_hex_t;

typedef struct {
    char seq[16];
    uint32_t n;
} zui_ime_compose_t;

typedef struct {
    char in[25]; /* letters typed (ASCII) */
    uint32_t n;
} zui_ime_table_t;

struct zui_ime {
    const zui_ime_engine_t *eng;
    union {
        zui_ime_hex_t hex;
        zui_ime_compose_t compose;
        zui_ime_table_t table;
        uint8_t raw[64];
    } st;
    char pre[ZUI_IME_PRE_MAX];  /* the preedit now                        */
    char sent[ZUI_IME_PRE_MAX]; /* the preedit the app was last told       */
    uint32_t caret;             /* caret inside the preedit, bytes         */
    zui_ime_cand_t cand[ZUI_IME_CAND_MAX];
    uint32_t ncand, sel;
    char commit[ZUI_IME_COMMIT_MAX];
    uint32_t ncommit;
    uint32_t kept[32];                 /* M3: key codes whose down was kept (bitmap, 0..1023) */
    int32_t caret_x, caret_y, caret_h; /* M4: where the app's caret is     */
    uint32_t dropped;                  /* events lost to a small out[]      */
};

/* ---- the framework ------------------------------------------------------------ */
void zui_ime_init(zui_ime_t *ime, const zui_ime_engine_t *eng); /* eng may be 0 */
/* Switch engines; an open composition is committed (events in out). */
uint32_t zui_ime_use(zui_ime_t *ime, const zui_ime_engine_t *eng, zui_event_t *out, uint32_t cap);
/* M2, M3: one event in, the events for the app out. Returns the count. */
uint32_t zui_ime_feed(zui_ime_t *ime, const zui_event_t *e, zui_event_t *out, uint32_t cap);
/* Commit or drop the open composition now (focus moved, the app asked). */
uint32_t zui_ime_commit(zui_ime_t *ime, zui_event_t *out, uint32_t cap);
uint32_t zui_ime_cancel(zui_ime_t *ime, zui_event_t *out, uint32_t cap);
/* Choose candidate i (a click on the candidate window). */
uint32_t zui_ime_choose(zui_ime_t *ime, uint32_t i, zui_event_t *out, uint32_t cap);

bool zui_ime_composing(const zui_ime_t *ime);
const char *zui_ime_preedit(const zui_ime_t *ime);
uint32_t zui_ime_cand_count(const zui_ime_t *ime);
const char *zui_ime_cand(const zui_ime_t *ime, uint32_t i); /* 0 past the end */

/* ---- M4: the candidate window ---------------------------------------------------- */
typedef struct {
    bool visible;
    uint32_t first, count; /* the page shown: candidates first .. first+count-1 */
    uint32_t sel;          /* highlighted, an absolute index                    */
    uint32_t page, pages;
    int32_t x, y; /* top-left: just under the caret                        */
} zui_ime_window_t;

void zui_ime_set_caret(zui_ime_t *ime, int32_t x, int32_t y, int32_t h);
void zui_ime_window(const zui_ime_t *ime, zui_ime_window_t *w);

/* ---- for engine writers ------------------------------------------------------------ */
void zui_ime_set_preedit(zui_ime_t *ime, const char *utf8);
void zui_ime_send(zui_ime_t *ime, const char *utf8); /* queue text to commit */
void zui_ime_send_cp(zui_ime_t *ime, uint32_t cp);
void zui_ime_cands_clear(zui_ime_t *ime);
bool zui_ime_cand_add(zui_ime_t *ime, const char *utf8, uint32_t len, uint8_t used);
bool zui_ime_cand_nav(zui_ime_t *ime, uint16_t key);           /* arrows, pages; true: used */
int32_t zui_ime_cand_digit(const zui_ime_t *ime, uint32_t cp); /* 1..9 -> index, else -1 */

/* ---- built-in engines ------------------------------------------------------------- */
/* Hex entry: Ctrl+Shift+U, up to six hex digits, then space or Enter. The
 * preedit reads "u" and the digits; the one candidate is the character. */
extern const zui_ime_engine_t zui_ime_hex;
/* Compose: ZUI_KEY_COMPOSE, then a sequence from the table (' e = é,
 * o c = ©, - - - = —). A sequence that matches nothing is sent as typed. */
typedef struct {
    const char *seq; /* what is typed after compose */
    const char *out; /* what it makes                */
} zui_compose_seq_t;
typedef struct {
    const zui_compose_seq_t *seq;
    uint32_t n;
} zui_compose_table_t;
extern const zui_compose_table_t zui_compose_default;
extern const zui_ime_engine_t zui_ime_compose;

/* Table-driven phonetic engine: lowercase ASCII letters are spelled into the
 * preedit; the longest table key that starts the preedit offers its
 * candidates (and, when the whole preedit is that key or no key, the
 * completions that start with it). Choosing one sends it and leaves what
 * remains of the preedit composing. Enter sends the letters as typed.
 * Make an engine of your own table by copying zui_ime_pinyin_sample with
 * another .data. */
typedef struct {
    const char *keys;  /* the spelling, ASCII lowercase   */
    const char *cands; /* candidates separated by spaces */
} zui_ime_entry_t;
typedef struct {
    const zui_ime_entry_t *e;
    uint32_t n;
} zui_ime_dict_t;
extern const zui_ime_dict_t zui_pinyin_sample_dict; /* SAMPLE DATA: a handful of words */
extern const zui_ime_engine_t zui_ime_pinyin_sample;
/* The shared table engine functions, for engines over other tables. */
void zui_ime_table_reset(zui_ime_t *ime);
bool zui_ime_table_key(zui_ime_t *ime, uint16_t key, uint8_t mods);
bool zui_ime_table_text(zui_ime_t *ime, uint32_t cp);
void zui_ime_table_commit(zui_ime_t *ime);
void zui_ime_table_cancel(zui_ime_t *ime);
void zui_ime_table_select(zui_ime_t *ime, uint32_t i);

/* ---- M6: the input methods of languages (zui_ime_lang.c) ------------------------ */
/* Pinyin and Jyutping: the table engine over single Han characters from the
 * Unihan database (tools/zui/gen_ime_han.py), typed without tones (u-umlaut
 * is v), most used first. No word dictionary yet: words are typed one
 * character at a time. */
extern const zui_ime_dict_t zui_pinyin_dict, zui_pinyin_hant_dict, zui_jyutping_dict;
extern const zui_ime_engine_t zui_ime_pinyin;      /* Simplified Chinese  */
extern const zui_ime_engine_t zui_ime_pinyin_hant; /* Traditional Chinese */
extern const zui_ime_engine_t zui_ime_jyutping;    /* Cantonese           */
/* Japanese: romaji (Hepburn or Kunrei, nn or n' for ん, a doubled consonant
 * for っ, - . , [ ] for ー 。 、 「 」) becomes hiragana as it is typed; the
 * candidates are the hiragana, the katakana, then the Joyo kanji read that
 * way (zui_kana_dict, keyed by hiragana). Enter sends the hiragana. */
extern const zui_ime_dict_t zui_kana_dict;
extern const zui_ime_engine_t zui_ime_kana;
/* Korean: the two-set (Dubeolsik) layout on Latin keys; jamo join into
 * syllables as they are typed, a final consonant moves on to the next
 * syllable when a vowel follows, and Backspace takes back one jamo. */
extern const zui_ime_engine_t zui_ime_hangul;
/* Amharic and Tigrinya: SERA transliteration. A consonant letter waits in
 * its sixth form; a vowel letter (e u i a E I o) picks the syllable, and W a
 * the wa form (lWa is ሏ); a vowel alone gives the አ row, ` then a vowel the
 * ዐ row, and `s `h `S give ሠ ኀ ፀ. : is ፡, :: is ።, , is ፣ and ; is ፤. */
extern const zui_ime_engine_t zui_ime_geez;
/* The input method a locale types with (whole tag, then its language), or 0
 * when its keyboard layout types it directly. */
const zui_ime_engine_t *zui_ime_for_locale(const char *tag);

#endif /* ZXV_ZUI_IME_H */
