/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ticker_model.h — Ticker, live captions, as a model (zxm.h): words heard
 * in a call, in media or in the room become captions in the person's
 * language, each staying on screen long enough to be read. The rules are
 * ticker_rules.h (TK1-TK4), the same ones the app's Sutra, KRON, Eve and
 * MYRA blocks run. No drawing, no allocation, integers only.
 *
 *   TM1 HEARD: zxvd has no speech-to-text method, so the host hands the
 *       model the words it heard (UTF-8, 1..4000 bytes, xlate's limit),
 *       when (ms since 1970, never going back) and who spoke (0..999).
 *       Text that is not UTF-8, or holds a control byte other than tab
 *       and line feed (read as spaces), is refused before anything is
 *       sent.
 *   TM2 LANGUAGE: xlate.detect names the words' language; "und" when it
 *       does not know or refuses. Its place in xlate.languages (1-based,
 *       0 not known) is what a record keeps.
 *   TM3 TRANSLATION: when the language is known and is not the person's,
 *       xlate.translate turns the words into it. A machine translation is
 *       marked machine, for the view to say so. When no model can
 *       generate (-32031), the words are shown as heard, untranslated:
 *       nothing is invented. Any other refusal changes nothing.
 *   TM4 LINES: the words are broken at spaces into lines of at most
 *       TICKER_LINE_CHARS characters (a longer word is cut), two lines to
 *       a caption; longer text makes several captions.
 *   TM5 QUEUE: captions wait in a bounded ring of TICKER_QUEUE. When the
 *       new captions do not all fit, none is added (backpressure,
 *       ZXM_E_STATE): every check is made before anything changes.
 *   TM6 ON SCREEN: a caption leaves the screen for the next one only when
 *       it has stayed long enough (TK2, TK3): ticker_tick moves on, never
 *       before. The last one stays until ticker_clear.
 *   TM7 NOT KEPT: a caption that leaves the screen is gone. The person
 *       may save the one on screen (ticker_save), with its ^ZXTICKER
 *       record (TK4); saved captions stay in the model, bounded, until the
 *       person clears them. */
#ifndef TICKER_MODEL_H
#define TICKER_MODEL_H

#include "ticker_rules.h"
#include "zxm.h"

#define TICKER_TEXT_MAX    4000u /* xlate.detect, xlate.translate maxLength */
#define TICKER_OUT_MAX     8192u /* a translation as zxvd answers it */
#define TICKER_TAG_MAX     15u   /* xlate.translate's `to` maxLength */
#define TICKER_LANGS       64u
#define TICKER_QUEUE       16u /* captions waiting (a power of two) */
#define TICKER_SAVED       32u
#define TICKER_SPEAKER_MAX 999
#define TICKER_LINE_BYTES  (TICKER_LINE_CHARS * 4) /* UTF-8: 4 bytes a character at most */

typedef struct {
    char line[TICKER_LINES][TICKER_LINE_BYTES + 1];
    uint32_t nlines;
    uint32_t chars;   /* characters on its lines, the spaces inside them counted */
    uint32_t longest; /* characters on its longest line */
    int64_t at_ms;    /* when its words were heard */
    uint32_t speaker;
    uint32_t lang; /* the source language's place in xlate.languages; 0 not known */
    char from[TICKER_TAG_MAX + 1];
    bool translated, machine;
} ticker_caption_t;

typedef struct {
    ticker_caption_t cap;
    int64_t least_ms; /* TK1 */
} ticker_item_t;

typedef struct {
    zxm_link_t *link;
    char to[TICKER_TAG_MAX + 1]; /* the person's language */
    char langs[TICKER_LANGS][TICKER_TAG_MAX + 1];
    uint32_t nlangs;
    ticker_item_t ring[TICKER_QUEUE]; /* TM5: head - tail >= TICKER_QUEUE is full */
    uint32_t head, tail;
    ticker_item_t screen; /* TM6 */
    bool showing;
    int64_t shown_at, now_ms;
    ticker_caption_t saved[TICKER_SAVED]; /* TM7 */
    uint32_t nsaved;
    bool no_model; /* the last words could not be translated: no model */
    ticker_caption_t scratch[TICKER_QUEUE];
    char text[TICKER_OUT_MAX + 1];
} ticker_t;

/* `to`: the person's language, a BCP 47 tag such as "en" or "pt-BR". */
int ticker_init(ticker_t *t, zxm_link_t *link, const char *to);
int ticker_set_language(ticker_t *t, const char *to);
/* TM2: xlate.languages, the places a record keeps */
int ticker_languages(ticker_t *t);
uint32_t ticker_lang_place(const ticker_t *t, const char *tag);
/* TM1: characters in n bytes of UTF-8, or -1 when they are not UTF-8 or
 * hold a control byte other than tab and line feed */
int64_t ticker_chars(const char *s, uint32_t n);
/* TM4: break n bytes into captions (at most cap); how many, or -1 when
 * they do not fit or are not text (TM1) */
int32_t ticker_break(const char *s, uint32_t n, ticker_caption_t *out, uint32_t cap);
/* TM1-TM5: words heard, captioned and queued */
int ticker_heard(ticker_t *t, int64_t at_ms, uint32_t speaker, const char *text, uint32_t n);
uint32_t ticker_waiting(const ticker_t *t);
/* TM6: the clock moves to now_ms (never back); *moved is true when the
 * screen changed */
int ticker_tick(ticker_t *t, int64_t now_ms, bool *moved);
/* TM7 */
int ticker_save(ticker_t *t);
void ticker_clear(ticker_t *t);
void ticker_clear_saved(ticker_t *t);

#endif
