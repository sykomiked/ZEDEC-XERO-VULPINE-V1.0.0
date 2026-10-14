/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zp_internal.h - what a language adapter sees.
 *
 * An adapter is one function: it gets the step's source text (with
 * ${name} already replaced) and the step context, runs the source with
 * its pillar library, and reports through the zp_* calls below:
 *
 *   zp_output(s, name, text)        an exact result; later steps can use
 *                                   it as ${name} and as an input
 *   zp_truths(s, name, lp, n)       a four-valued result; it also sets the
 *                                   verdict (any BOTH holds the step)
 *   zp_refuse(s, status, statusname, line, col, why)   verdict FALSE
 *   zp_hold(s, why)                 verdict BOTH: held for review
 *   zp_pending(s, why)              verdict NEITHER: cannot decide yet
 *   zp_note(s, text)                a line of explanation
 *
 * A step that reports nothing of the four is TRUE. Inputs (from the
 * inputs file and earlier steps' outputs) are read with zp_input; an
 * adapter that needs every input walks zp_input_count / zp_input_at.
 */
#ifndef ZP_INTERNAL_H
#define ZP_INTERNAL_H

#include "zxvplay.h"

#include <stdarg.h>

/* A growable text buffer. On out-of-memory it sets `oom` and stops
 * growing; the run then fails with ZP_E_NOMEM. */
typedef struct {
    char *p;
    size_t len, cap;
    int oom;
} zp_buf;

void zp_buf_put(zp_buf *b, const char *s, size_t n);
void zp_buf_puts(zp_buf *b, const char *s);
void zp_buf_printf(zp_buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* s as a JSON string, quotes included. */
void zp_buf_json(zp_buf *b, const char *s, size_t n);
void zp_buf_free(zp_buf *b);

typedef struct zp_step zp_step;

typedef void (*zp_lang_fn)(zp_step *s, const char *src, size_t len);

/* The step being run. Opaque to adapters except through the calls. */
const char *zp_step_name(const zp_step *s);
/* An option from the manifest step line (`key=value`), or NULL. */
const char *zp_step_opt(const zp_step *s, const char *key);
/* Another file of the template (for a step that needs two, e.g. a MYRA
 * schema and its data), or NULL. */
const zp_file *zp_step_file(const zp_step *s, const char *name);

const char *zp_input(const zp_step *s, const char *name);
uint32_t zp_input_count(const zp_step *s);
void zp_input_at(const zp_step *s, uint32_t i, const char **name, const char **value);

void zp_output(zp_step *s, const char *name, const char *text);
void zp_truths(zp_step *s, const char *name, const int32_t *lp, uint32_t n);
void zp_refuse(zp_step *s, int32_t status, const char *status_name, uint32_t line, uint32_t col,
               const char *why);
void zp_hold(zp_step *s, const char *why);
void zp_pending(zp_step *s, const char *why);
void zp_note(zp_step *s, const char *text);
void zp_notef(zp_step *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Line and column (1-based) of byte offset `off` in text. */
void zp_line_col(const char *text, size_t len, size_t off, uint32_t *line, uint32_t *col);

/* The adapters. */
void zp_lang_treaty(zp_step *s, const char *src, size_t len);
void zp_lang_sutra(zp_step *s, const char *src, size_t len);
void zp_lang_trebago(zp_step *s, const char *src, size_t len);
void zp_lang_eve(zp_step *s, const char *src, size_t len);
void zp_lang_kron(zp_step *s, const char *src, size_t len);
void zp_lang_myra(zp_step *s, const char *src, size_t len);

/* What the AI drafting prompt tells a model about each language. */
const char *zp_lang_rules(const char *lang);

/* Generated from templates/ by tools/gen_templates.py. */
extern const zp_template zp_builtin_templates[];
extern const uint32_t zp_builtin_template_count;

#endif /* ZP_INTERNAL_H */
