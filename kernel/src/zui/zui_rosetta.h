/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_rosetta.h — Rosetta, the human-side language bridge: it carries what
 * the system says from any of zui_locale's 183 languages into any other.
 *
 * Rosetta does not speak each language natively. It bridges them: what it
 * recognises it carries through a shared pivot, and it says exactly which
 * parts it could not carry, so nothing machine-made passes as reviewed.
 * Its machine-side counterpart is Babel, which moves values and programs
 * between runtimes (docs/BABEL.md). An explainer of code, built on a
 * language's AI twin, can hand Rosetta a plain English explanation through
 * zui_babel_explain_fn, and Rosetta brings it into the person's language.
 *
 *   R1 THE PIVOT. A sentence is read as parts, split on the source locale's
 *      list separator and ", ", exactly as zui_locale_phrase writes them.
 *      Each part that is one of the screen reader's words or patterns in the
 *      source locale ("кнопка", "3 из 12", "5 элементов", "заголовок
 *      уровня 2", "от X") is recognised by writing the candidate back with
 *      zui_locale itself and comparing, so a part is only ever carried when
 *      the source locale would have said exactly that. Numbers are read in
 *      the locale's default and native digits and in Latin digits. A
 *      recognised part becomes its English reader form (the pivot) and is
 *      then said in the target with zui_locale_phrase, numbers included.
 *   R2 BRIDGES. A script-only locale (Akkadian, Egyptian, Linear B, ...)
 *      has no words of its own, so Rosetta speaks its bridge instead
 *      (zui_locale_bridge: hbo -> he, arc -> syr, peo -> fa, akk -> en) and
 *      names it in the report. Reading from a script-only locale reads its
 *      English words.
 *   R3 GAPS. A part Rosetta does not recognise (a name, a label, text a
 *      person typed) goes to the provider, a translator the caller plugs in
 *      (a local model of the swarm), when there is one; its answer counts as
 *      machine-made. Without a provider, or when the provider declines, the
 *      part is kept as written. The report counts each kind.
 *   R4 TRUST. The report's trust is the weakest of the target's own review
 *      state (zui_locale_review) and the provider's (always
 *      ZUI_REVIEW_MACHINE): English is ZUI_REVIEW_SOURCE, every other
 *      translation ZUI_REVIEW_MACHINE until a native speaker reviews it.
 *      Show it next to the text.
 *   R5 EXPLAINING CODE. zui_rosetta_explain asks the explainer for a plain English
 *      explanation of some code, then carries it into the target language
 *      as in R1 to R4. The explainer's English is machine-made too.
 *   R6 Integer only, no allocation, deterministic; every output is cut at a
 *      whole UTF-8 character and NUL-terminated when cap > 0.
 */
#ifndef ZXV_ZUI_ROSETTA_H
#define ZXV_ZUI_ROSETTA_H

#include <stdint.h>

#include "zui_locale.h"

/* R3: translate text[0..n) from one locale to another into out; return
 * the bytes written, or 0 to decline (the part is then kept). */
typedef uint32_t (*zui_rosetta_provider_fn)(const zui_locale_t *from, const zui_locale_t *to,
                                            const char *text, uint32_t n, char *out, uint32_t cap,
                                            void *ctx);
/* R5: a plain English explanation of code[0..n) into out; bytes written. */
typedef uint32_t (*zui_babel_explain_fn)(const char *code, uint32_t n, char *english, uint32_t cap,
                                         void *ctx);

typedef struct {
    zui_rosetta_provider_fn provider; /* may be NULL */
    void *provider_ctx;
    zui_babel_explain_fn babel; /* may be NULL */
    void *babel_ctx;
} zui_rosetta_t;

typedef struct {
    const zui_locale_t *spoken; /* the locale the text came out in (R2) */
    uint32_t parts;             /* parts read                            */
    uint32_t bridged;           /* carried through the pivot (R1)        */
    uint32_t provided;          /* translated by the provider (R3)       */
    uint32_t kept;              /* kept as written (R3)                  */
    zui_review_t trust;         /* R4                                    */
} zui_rosetta_report_t;

/* R2: the locale Rosetta speaks for to. */
const zui_locale_t *zui_rosetta_target(const zui_locale_t *to);

/* R1-R4. r and report may be NULL. */
uint32_t zui_rosetta_translate(const zui_rosetta_t *r, const zui_locale_t *from,
                               const zui_locale_t *to, const char *text, char *out, uint32_t cap,
                               zui_rosetta_report_t *report);

/* R5. Returns 0 (and an empty out) when there is no explainer or it
 * gives nothing. */
uint32_t zui_rosetta_explain(const zui_rosetta_t *r, const zui_locale_t *to, const char *code,
                             uint32_t n, char *out, uint32_t cap, zui_rosetta_report_t *report);

#endif
