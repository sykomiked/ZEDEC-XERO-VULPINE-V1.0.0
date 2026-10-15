/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_locale.h — the person's language and region: which way text runs,
 * how numbers, dates and times are written, which day starts the week, how
 * plurals work, and what the screen reader says, for 30 languages.
 *
 * The data follows the Unicode CLDR (version 48: number symbols, grouping,
 * default and native numbering systems, short date and time patterns,
 * day periods, first day of the week, cardinal plural rules). The screen
 * reader's words are this project's translations.
 *
 *   L1 LOCALES. 183 of them (tools/zui/locales/LANGUAGES.tsv): 104 living
 *      languages and 79 ancient and historical ones (L7). The living: en es fr de
 *      pt it ru uk pl ar he fa ur hi bn ta zh-Hans zh-Hant ja ko th vi tr id
 *      sw am yo ha el nl; South Asia mr te gu kn ml pa or as ne si sd ps sat
 *      dv; Southeast and East Asia ms fil jv su my km lo mn bo yue; Central
 *      Asia and the Caucasus kk uz ky tg tk az ka hy ckb ug; Europe ro cs hu
 *      sv bg sr hr sk da fi nb lt be ca cy ga; Africa zu xh ig so om ti rw mg
 *      sn wo ln af ff nqo zgh; Indigenous languages of the Americas, the
 *      Pacific and the Arctic qu gn chr nv mi haw iu se; and ht. They are
 *      written in 37 scripts. zui_locale_find takes a BCP 47 tag (any case,
 *      "-" or "_") and falls back step by step: pt-BR -> pt -> en, zh-TW /
 *      zh-HK / zh-MO -> zh-Hant, zh / zh-CN / zh-SG -> zh-Hans, the legacy
 *      iw -> he, in -> id, tl -> fil, no -> nb, mo -> ro, sh -> sr; anything
 *      unknown is en. It never returns NULL. zui_locale_script gives the
 *      ISO 15924 script the locale is written in ("Latn", "Deva", "Hant"),
 *      which picks its ASCW Golden face (ascw_face_chain).
 *   L2 DIRECTION. ar he fa ur sd ps dv ckb ug nqo are right-to-left (CLDR
 *      layout); the rest left-to-right. Use it as zui_bidi's paragraph
 *      direction for the interface.
 *   L3 PLURALS. zui_locale_plural gives the CLDR cardinal category (zero,
 *      one, two, few, many, other) of an integer (the rules with v = 0 and
 *      e = 0; a negative number takes its absolute value's category).
 *   L4 NUMBERS. zui_locale_number writes value / 10^scale with the locale's
 *      decimal and grouping symbols, its grouping (3, or 3;2 for the Indian
 *      system of hi, bn, ta), its minimum grouping digits (es, it, pl write
 *      1234 ungrouped) and its minus sign (with the bidi marks CLDR puts in
 *      it). Digits: ZUI_DIGITS_DEFAULT is CLDR's default numbering system
 *      (native for fa and bn; Latin for ar, hi, th and the others since CLDR
 *      46), ZUI_DIGITS_NATIVE the locale's native system (Arabic-Indic for ar,
 *      Persian for fa and ur, Devanagari for hi, Bengali for bn, Thai for th,
 *      Tamil for ta, CJK ideographs for zh), with that system's symbols;
 *      ZUI_DIGITS_LATIN always 0-9.
 *   L5 DATES AND TIMES. zui_locale_date / _time / _datetime write the CLDR
 *      short pattern for integer epoch seconds plus a UTC offset in seconds,
 *      in the locale's default digits: fa in the Solar Hijri calendar, th in
 *      the Buddhist era, every other locale Gregorian. Fields: y yy M MM d dd
 *      H HH h hh m mm a (AM/PM in the locale's words) and B (zh-Hant's
 *      periods of the day). zui_locale_civil gives the Gregorian date.
 *   L6 WEEK. zui_locale_first_day: 0 Sunday .. 6 Saturday, from the locale's
 *      most likely region (en -> US, ar -> EG, fa -> IR, zh-Hans -> CN, ...).
 *   L7 THE READER'S WORDS. Every locale but en has machine-made words
 *      (zui_locale_review gives ZUI_REVIEW_MACHINE) until a native speaker
 *      reviews its file in tools/zui/locales/words and marks it reviewed;
 *      settings should say so next to the language. A historical language
 *      with no vocabulary for a user interface (Akkadian, Egyptian, Linear
 *      B, ...) is a locale for its script and numbers only: its words are
 *      en's and zui_locale_review gives ZUI_REVIEW_UNTRANSLATED. A tag with
 *      a script subtag (arc-Nbat, cu-Glag) names its own locale.  zui_locale_word gives one of the
 * screen reader's words (zui_reader.c's English: "button", "selected", "no next heading", ...) in
 * the locale; zui_locale_count says a count with the right plural form ("3 элемента", "5
 * элементов"); zui_locale_position says "k of n"; zui_locale_heading "heading level n". A word a
 * locale lacks comes from its parent, then en. L8 PHRASING AN ANNOUNCEMENT. zui_locale_phrase turns
 * an utterance the reader built in English into the locale: it is read as parts separated by ", "
 * (the reader's own joiner); each part that is one of the reader's words or patterns ("3 of 12", "5
 * items", "heading level 2", "about 40 tokens", "from X", "shortcut ...", "Error: ...", "verbosity
 * high") is translated with L7, numbers are written with L4, and everything else (names, labels,
 * the text people typed) is kept as it is. Parts are joined with the locale's list separator ("، "
 * in Arabic script, "、" in Japanese, "，" in Chinese, a space in Thai). L9 A SPEECH SHIM.
 * zui_locale_voice_t sits between zui_reader and its speech: pass zui_locale_speak as the reader's
 * zui_speak_fn and the shim as its ctx; it phrases each utterance (L8) and hands it on to the next
 * zui_speak_fn (zui_reader_voice_speak, with the voice's lang set to zui_locale_tag). Stop requests
 * (empty text) pass through. L10 Integer only, no allocation, deterministic; every output is cut at
 * a whole UTF-8 character and NUL-terminated when cap > 0.
 */
#ifndef ZXV_ZUI_LOCALE_H
#define ZXV_ZUI_LOCALE_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_reader.h"

typedef struct zui_locale zui_locale_t;

/* L3 */
typedef enum {
    ZUI_PLURAL_ZERO = 0,
    ZUI_PLURAL_ONE,
    ZUI_PLURAL_TWO,
    ZUI_PLURAL_FEW,
    ZUI_PLURAL_MANY,
    ZUI_PLURAL_OTHER,
    ZUI_PLURAL_COUNT
} zui_plural_t;

/* L7: who wrote a locale's reader words */
typedef enum {
    ZUI_REVIEW_SOURCE = 0,  /* the source language (en)                        */
    ZUI_REVIEW_MACHINE,     /* machine-made, not yet checked by a native speaker */
    ZUI_REVIEW_NATIVE,      /* reviewed by a native speaker                    */
    ZUI_REVIEW_UNTRANSLATED /* none: a historical language whose script is
                               offered for display and input; the words are en */
} zui_review_t;

/* L4 */
typedef enum { ZUI_DIGITS_DEFAULT = 0, ZUI_DIGITS_NATIVE, ZUI_DIGITS_LATIN } zui_digits_t;

/* L7: the reader's words. The comment is the English the reader says. */
typedef enum {
    ZUI_WORD_GROUP = 0,           /* group                                  */
    ZUI_WORD_BUTTON,              /* button                                 */
    ZUI_WORD_LIST,                /* list                                   */
    ZUI_WORD_READONLY,            /* read-only text                         */
    ZUI_WORD_TEXTAREA,            /* text area                              */
    ZUI_WORD_TEXTFIELD,           /* text field                             */
    ZUI_WORD_SCROLL,              /* scroll area                            */
    ZUI_WORD_AI,                  /* AI suggestion                          */
    ZUI_WORD_PROGRESS,            /* progress bar                           */
    ZUI_WORD_IMAGE,               /* image                                  */
    ZUI_WORD_SEPARATOR,           /* separator                              */
    ZUI_WORD_HEADING,             /* heading                                */
    ZUI_WORD_CHECKBOX,            /* check box                              */
    ZUI_WORD_REGION,              /* region                                 */
    ZUI_WORD_ERROR,               /* error                                  */
    ZUI_WORD_MENU,                /* menu                                   */
    ZUI_WORD_MENUITEM,            /* menu item                              */
    ZUI_WORD_DIALOG,              /* dialog                                 */
    ZUI_WORD_OPTION,              /* option                                 */
    ZUI_WORD_SECURE,              /* secure                                 */
    ZUI_WORD_EMPTY,               /* empty                                  */
    ZUI_WORD_BUSY,                /* busy                                   */
    ZUI_WORD_DISABLED,            /* disabled                               */
    ZUI_WORD_CHECKED,             /* checked                                */
    ZUI_WORD_NOT_CHECKED,         /* not checked                            */
    ZUI_WORD_PARTLY_CHECKED,      /* partly checked                         */
    ZUI_WORD_SELECTED,            /* selected                               */
    ZUI_WORD_DIMMED,              /* dimmed                                 */
    ZUI_WORD_BLANK,               /* blank                                  */
    ZUI_WORD_PENDING,             /* pending                                */
    ZUI_WORD_ACCEPTED,            /* accepted                               */
    ZUI_WORD_REJECTED,            /* rejected                               */
    ZUI_WORD_AI_ACCEPTED,         /* AI suggestion accepted                 */
    ZUI_WORD_AI_REJECTED,         /* AI suggestion rejected                 */
    ZUI_WORD_READER_ON,           /* screen reader on                       */
    ZUI_WORD_READER_OFF,          /* screen reader off                      */
    ZUI_WORD_NOTHING,             /* nothing on screen                      */
    ZUI_WORD_END,                 /* end                                    */
    ZUI_WORD_TOP,                 /* top                                    */
    ZUI_WORD_NO_NEXT_HEADING,     /* no next heading                        */
    ZUI_WORD_NO_PREV_HEADING,     /* no previous heading                    */
    ZUI_WORD_NO_NEXT_LANDMARK,    /* no next landmark                       */
    ZUI_WORD_NO_PREV_LANDMARK,    /* no previous landmark                   */
    ZUI_WORD_NO_NEXT_AI,          /* no next AI suggestion                  */
    ZUI_WORD_NO_PREV_AI,          /* no previous AI suggestion              */
    ZUI_WORD_NO_FOCUS,            /* no focus                               */
    ZUI_WORD_NOTHING_TO_ACTIVATE, /* nothing to activate                   */
    ZUI_WORD_NOT_A_CONTROL,       /* not a control                          */
    ZUI_WORD_NO_ACTIONS,          /* no actions match                       */
    ZUI_WORD_ACCEPT_HINT,         /* accept with Command Return, or reject  */
    ZUI_WORD_LM_BANNER,           /* banner landmark                        */
    ZUI_WORD_LM_NAVIGATION,       /* navigation landmark                    */
    ZUI_WORD_LM_MAIN,             /* main landmark                          */
    ZUI_WORD_LM_SEARCH,           /* search landmark                        */
    ZUI_WORD_LM_COMPLEMENTARY,    /* complementary landmark                 */
    ZUI_WORD_LM_STATUS,           /* status landmark                        */
    ZUI_WORD_VERB_LOW,            /* verbosity low                          */
    ZUI_WORD_VERB_MEDIUM,         /* verbosity medium                       */
    ZUI_WORD_VERB_HIGH,           /* verbosity high                         */
    ZUI_WORD_ERROR_PREFIX,        /* "Error: " (with its space)             */
    ZUI_WORD_SHORTCUT,            /* shortcut                               */
    ZUI_WORD_FROM,                /* from {x}  (the agent of a suggestion)  */
    ZUI_WORD_BY,                  /* by {x}                                 */
    ZUI_WORD_ABOUT,               /* about {x} (an estimate)                */
    ZUI_WORD_HEADING_LEVEL,       /* heading level {n}                      */
    ZUI_WORD_POSITION,            /* {k} of {n}                             */
    ZUI_WORD_COUNT
} zui_word_t;

/* L7: counted nouns. */
typedef enum {
    ZUI_COUNT_ITEM = 0,  /* {n} items      */
    ZUI_COUNT_CHARACTER, /* {n} characters */
    ZUI_COUNT_TOKEN,     /* {n} tokens     */
    ZUI_COUNT_PERCENT,   /* {n} percent    */
    ZUI_COUNT_NOUNS
} zui_count_t;

/* L1 */
const zui_locale_t *zui_locale_find(const char *tag);
const zui_locale_t *zui_locale_at(uint32_t i); /* 0 .. zui_locale_count_all()-1, else NULL */
uint32_t zui_locale_count_all(void);
const char *zui_locale_tag(const zui_locale_t *loc);  /* "pt", "zh-Hant", ... */
const char *zui_locale_name(const zui_locale_t *loc); /* its own name: "Deutsch" */
const char *zui_locale_script(const zui_locale_t *loc); /* ISO 15924: "Latn" */
zui_review_t zui_locale_review(const zui_locale_t *loc);
/* The translated locale that stands in for a script-only one (hbo -> he,
 * arc -> syr, akk -> en); a translated locale is its own bridge. */
const zui_locale_t *zui_locale_bridge(const zui_locale_t *loc);
/* L2: true when the locale is written right to left. */
bool zui_locale_rtl(const zui_locale_t *loc);

/* L3 */
zui_plural_t zui_locale_plural(const zui_locale_t *loc, int64_t n);
const char *zui_plural_name(zui_plural_t p); /* "one", "few", ... */

/* L4: returns the bytes written. */
uint32_t zui_locale_number(const zui_locale_t *loc, int64_t value, uint32_t scale,
                           zui_digits_t digits, char *out, uint32_t cap);
uint32_t zui_locale_integer(const zui_locale_t *loc, int64_t value, char *out, uint32_t cap);
/* The zero digit of a numbering choice ('0', U+0660, U+06F0, ...). */
uint32_t zui_locale_zero(const zui_locale_t *loc, zui_digits_t digits);

/* L5 */
typedef struct {
    int32_t year;  /* in the calendar asked for            */
    uint8_t month; /* 1..12                                */
    uint8_t day;   /* 1..31                                */
    uint8_t hour, minute, second;
    uint8_t weekday; /* 0 Sunday .. 6 Saturday              */
} zui_civil_t;

zui_civil_t zui_locale_civil(int64_t epoch, int32_t offset);   /* Gregorian */
zui_civil_t zui_locale_persian(int64_t epoch, int32_t offset); /* Solar Hijri */
uint32_t zui_locale_date(const zui_locale_t *loc, int64_t epoch, int32_t offset, char *out,
                         uint32_t cap);
uint32_t zui_locale_time(const zui_locale_t *loc, int64_t epoch, int32_t offset, char *out,
                         uint32_t cap);
uint32_t zui_locale_datetime(const zui_locale_t *loc, int64_t epoch, int32_t offset, char *out,
                             uint32_t cap); /* date, a space, time */
/* L6 */
uint8_t zui_locale_first_day(const zui_locale_t *loc);

/* L7 */
const char *zui_locale_word(const zui_locale_t *loc, zui_word_t w);
const char *zui_locale_english(zui_word_t w); /* what zui_reader says */
uint32_t zui_locale_count(const zui_locale_t *loc, zui_count_t noun, int64_t n, char *out,
                          uint32_t cap);
uint32_t zui_locale_position(const zui_locale_t *loc, uint64_t k, uint64_t n, char *out,
                             uint32_t cap);
uint32_t zui_locale_heading(const zui_locale_t *loc, uint32_t level, char *out, uint32_t cap);
const char *zui_locale_list_separator(const zui_locale_t *loc);

/* L8 */
uint32_t zui_locale_phrase(const zui_locale_t *loc, const char *english, char *out, uint32_t cap);

/* L9 */
typedef struct {
    const zui_locale_t *loc;
    zui_speak_fn next; /* e.g. zui_reader_voice_speak */
    void *next_ctx;
    uint32_t phrased;
    char buf[ZUI_READER_UTTER * 2u];
} zui_locale_voice_t;

void zui_locale_voice_init(zui_locale_voice_t *v, const zui_locale_t *loc, zui_speak_fn next,
                           void *next_ctx);
void zui_locale_speak(const char *text, zui_say_t how, void *voice);

#endif /* ZXV_ZUI_LOCALE_H */
