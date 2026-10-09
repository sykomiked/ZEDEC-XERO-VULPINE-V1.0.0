/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* xlate.h — auto-translation: everyone reads everything in their own language.
 *
 * THE PIPELINE (one call: xlate_view)
 *
 *   post / message / caption (UTF-8)
 *     -> which language is it?   given by the author, else xlate_langid()
 *     -> does this reader want it translated?   xlate_prefs_t:
 *          auto-translate off, a language they read, or "show original"
 *          toggled for this item  ->  the original, untouched
 *     -> cache lookup by (content id, source, target)  -> hit: done
 *     -> prompt template (data: xlate_prompts.c) -> model backend (a function
 *        pointer: zt_model when it lands, or any engine) -> clean the output
 *     -> cache it, return it with XLATE_F_MACHINE set
 *
 * Each post or message is translated ONCE per target language: the cache key
 * is the content id (an IPFS CID digest when the caller has one, else the
 * SHA-256 of the text) plus the source and target tags. Changing the backend
 * empties the cache, because a different model gives different text.
 *
 * HONEST LIMITS. Machine translation is wrong sometimes. Every translated
 * result carries XLATE_F_MACHINE so the interface can say so (see
 * xlate_marker_label) and offer the original one tap away. With no backend
 * bound, nothing is invented: the reader gets the original and
 * XLATE_ERR_NO_BACKEND. Text is content, never instructions: the prompt
 * templates say so and xlate_build_prompt() neutralises chat-template control
 * tokens inside it.
 *
 * ENOCHIAN CORE (swarm_enochian.h E8). The swarm stores and reasons in
 * Enochian and translates at the edge; this module is that edge for human
 * text shown to people.
 *
 * Freestanding: no libc, no allocation, no float, no 64-bit division. The
 * caller owns xlate_t (it holds the cache; about XLATE_CACHE_SLOTS *
 * (XLATE_CACHE_TEXT + 80) bytes plus two work buffers).
 */
#ifndef ZXV_XLATE_H
#define ZXV_XLATE_H

#include <stdint.h>
#include <stdbool.h>
#include "xlate_langid.h"

#define XLATE_TAG_MAX 16u
#ifndef XLATE_TEXT_MAX
#    define XLATE_TEXT_MAX 4096u /* largest source or translation, bytes */
#endif
#ifndef XLATE_CACHE_SLOTS
#    define XLATE_CACHE_SLOTS 64u
#endif
#ifndef XLATE_CACHE_TEXT
#    define XLATE_CACHE_TEXT 1024u /* longer translations are returned, not cached */
#endif
#define XLATE_PROMPT_MAX       (XLATE_TEXT_MAX + 2048u)
#define XLATE_KEY_LEN          32u
#define XLATE_KNOWN_MAX        8u
#define XLATE_ORIGINAL_TOGGLES 64u

typedef enum {
    XLATE_OK = 0,
    XLATE_ERR_ARG = -1,
    XLATE_ERR_NO_BACKEND = -2,  /* nothing bound: the original is shown       */
    XLATE_ERR_BACKEND = -3,     /* the model failed or returned nothing       */
    XLATE_ERR_TOO_LONG = -4,    /* text or prompt does not fit                */
    XLATE_ERR_UNKNOWN_LANG = -5 /* source language could not be identified    */
} xlate_result_t;

typedef enum {
    XLATE_KIND_POST = 0,    /* feed posts, comments                  */
    XLATE_KIND_CHAT = 1,    /* direct and group messages             */
    XLATE_KIND_CAPTION = 2, /* live captions from speech             */
    XLATE_KIND_UI = 3,      /* interface strings with placeholders   */
    XLATE_KIND_DOC = 4      /* documents, long form                  */
} xlate_kind_t;

/* ===== Prompt templates (data, in xlate_prompts.c) ===== */
typedef enum {
    XLATE_FMT_CHATML = 0, /* <|im_start|>role ... <|im_end|> (Qwen2.5)       */
    XLATE_FMT_MADLAD = 1, /* "<2xx> text" seq2seq (MADLAD-400 MT)            */
    XLATE_FMT_MISTRAL = 2 /* [INST] ... [/INST] (Mistral 7B Instruct v0.3)   */
} xlate_prompt_format_t;

typedef struct {
    const char *id; /* "qwen2.5-instruct", "madlad400", "mistral-instruct" */
    xlate_prompt_format_t format;
    const char *system; /* placeholders: {src} {tgt} {src_name} {tgt_name} {kind_note} */
    const char *user;   /* placeholders as above plus {text}                         */
    const char *stop;   /* where the backend must stop ("" = end of sequence)        */
    const char *models; /* models it is written for, with their licences             */
} xlate_template_t;

uint32_t xlate_template_count(void);
const xlate_template_t *xlate_template_at(uint32_t i);
const xlate_template_t *xlate_template_find(const char *id);
/* Kind-specific instruction substituted for {kind_note}. */
const char *xlate_kind_note(xlate_kind_t kind);

/* Fill a template. Control sequences inside text ("<|", "[INST]", "[/INST]",
 * a leading "<2xx>") are defused so content cannot steer the model. Returns
 * the prompt length, or -1 if it does not fit in cap (incl. NUL). */
int32_t xlate_build_prompt(const xlate_template_t *t, const char *src, const char *tgt,
                           xlate_kind_t kind, const char *text, uint32_t len, char *out,
                           uint32_t cap);

/* ===== The model backend ===== */
typedef struct {
    const char *prompt; /* formatted by the template ("" when none bound) */
    uint32_t prompt_len;
    const char *text; /* the raw source text, for non-LLM engines        */
    uint32_t text_len;
    const char *src; /* BCP 47 */
    const char *tgt;
    xlate_kind_t kind;
    const char *stop;
    uint32_t max_out; /* bytes the caller can take */
} xlate_backend_request_t;

/* 0 on success with *out_len bytes in out (no NUL needed), negative on
 * failure. The output is untrusted: it is bounded, cut at the stop sequence
 * and trimmed before anyone sees it. */
typedef int (*xlate_backend_fn)(const xlate_backend_request_t *req, char *out, uint32_t cap,
                                uint32_t *out_len, void *ctx);

typedef struct {
    xlate_backend_fn translate;
    void *ctx;
    const char *model_id;         /* shown in the marker, e.g. "Qwen2.5-7B-Instruct" */
    const xlate_template_t *tmpl; /* NULL: the backend formats its own input        */
} xlate_backend_t;

/* ===== Requests and responses ===== */
typedef struct {
    const char *text;
    uint32_t len;
    const uint8_t *content_id; /* CID digest or other stable id; NULL = hash the text */
    uint32_t content_id_len;
    const char *src; /* NULL / "" / "auto" = identify */
    const char *tgt;
    xlate_kind_t kind;
} xlate_request_t;

#define XLATE_F_MACHINE       1u  /* out is a machine translation              */
#define XLATE_F_CACHED        2u  /* served from the cache                     */
#define XLATE_F_ORIGINAL      4u  /* out is the original text                  */
#define XLATE_F_SRC_DETECTED  8u  /* source language came from xlate_langid    */
#define XLATE_F_SRC_UNSURE    16u /* ... and the guess was not reliable        */
#define XLATE_F_CAN_TRANSLATE 32u /* original shown, a translation is offered  */
#define XLATE_F_TRUNCATED     64u /* out did not fit the caller's buffer       */

typedef struct {
    int32_t status; /* xlate_result_t */
    uint32_t flags;
    uint32_t len; /* bytes in out (NUL-terminated) */
    char src[XLATE_TAG_MAX];
    char tgt[XLATE_TAG_MAX];
    uint32_t src_confidence;
    const char *model_id; /* "" for originals */
    uint8_t key[XLATE_KEY_LEN];
} xlate_response_t;

/* ===== Cache and context ===== */
typedef struct {
    bool used;
    uint8_t key[XLATE_KEY_LEN];
    char src[XLATE_TAG_MAX];
    char tgt[XLATE_TAG_MAX];
    uint32_t stamp; /* LRU */
    uint32_t len;
    char text[XLATE_CACHE_TEXT];
} xlate_cache_slot_t;

typedef struct {
    xlate_backend_t backend;
    xlate_cache_slot_t slot[XLATE_CACHE_SLOTS];
    uint32_t clock;
    uint32_t hits, misses, backend_calls, backend_failures;
    char prompt[XLATE_PROMPT_MAX];
    char work[XLATE_TEXT_MAX];
} xlate_t;

void xlate_init(xlate_t *x);
/* Bind (or with NULL, unbind) the model backend. Empties the cache. */
void xlate_set_backend(xlate_t *x, const xlate_backend_t *b);
void xlate_cache_clear(xlate_t *x);

/* Translate one item into req->tgt. Writes the translation, or the original
 * when no translation is needed or possible, into out (cap bytes incl. NUL).
 * Returns resp->status. */
int xlate_translate(xlate_t *x, const xlate_request_t *req, char *out, uint32_t cap,
                    xlate_response_t *resp);

/* ===== Per-reader settings: "show everything in my language" ===== */
typedef struct {
    char native[XLATE_TAG_MAX];                 /* the reader's language                 */
    bool auto_translate;                        /* show everything in my language        */
    bool translate_captions;                    /* live captions in my language too      */
    char known[XLATE_KNOWN_MAX][XLATE_TAG_MAX]; /* languages I read      */
    uint32_t known_count;
    uint8_t original[XLATE_ORIGINAL_TOGGLES][8]; /* "show original" items */
    uint32_t original_count;
    uint32_t original_next;
} xlate_prefs_t;

/* native language tag; auto-translate and captions on. */
void xlate_prefs_init(xlate_prefs_t *p, const char *native);
int xlate_prefs_add_known(xlate_prefs_t *p, const char *tag);
/* Flip "show original" for one item (by its response key). Returns the new
 * state: true = this item now shows its original. Oldest toggles are
 * forgotten after XLATE_ORIGINAL_TOGGLES. */
bool xlate_prefs_toggle_original(xlate_prefs_t *p, const uint8_t key[XLATE_KEY_LEN]);
bool xlate_prefs_shows_original(const xlate_prefs_t *p, const uint8_t key[XLATE_KEY_LEN]);
/* Does this reader want text in `lang` translated? (Primary subtags compare:
 * "en-GB" text is not translated for an "en" reader.) */
bool xlate_prefs_wants(const xlate_prefs_t *p, const char *lang);

/* What this reader should see for item (item->tgt is ignored; the reader's
 * native language is the target). Falls back to the original on any error,
 * with resp->status saying why. */
int xlate_view(xlate_t *x, const xlate_prefs_t *p, const xlate_request_t *item, char *out,
               uint32_t cap, xlate_response_t *resp);

/* ===== Machine-translation markers ===== */
/* Message key for the i18n catalog; xlate_marker_label renders the English
 * fallback "Translated from Spanish by machine (Qwen2.5-7B-Instruct)". */
#define XLATE_MARKER_KEY "xlate.marker.machine_translated"
uint32_t xlate_marker_label(const xlate_response_t *r, char *out, uint32_t cap);
/* Compact inline tag for plain-text channels and exports: "[MT es>en]".
 * Returns 0 (and writes "") when r is not a machine translation. */
uint32_t xlate_marker_tag(const xlate_response_t *r, char *out, uint32_t cap);
/* True when text begins with a "[MT xx>yy]" tag (so it is not re-translated
 * or mistaken for an original). */
bool xlate_has_marker_tag(const char *text, uint32_t len);

/* ===== Live captions: plugs into speech_session (speech_text_filter_fn) ===== */
typedef struct {
    xlate_t *x;
    const xlate_prefs_t *prefs;
} xlate_caption_ctx_t;
/* Returns 1 with a translation in out, 0 with the input copied to out, <0
 * on error (out then holds the input). */
int xlate_caption_filter(const char *in, const char *src_lang, char *out, uint32_t cap, void *ctx);

/* Same-language test on primary subtags ("pt-BR" ~ "pt", "zh-Hant" !~
 * "zh-Hans": script subtags are compared for zh). */
bool xlate_same_language(const char *a, const char *b);

#endif /* ZXV_XLATE_H */
