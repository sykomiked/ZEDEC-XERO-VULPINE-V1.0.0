/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvplay.h - the ZXV Playground: templates and one runner for every
 * ZXV language.
 *
 * A TEMPLATE is a small project: a manifest (template.zxv), one or more
 * source files in ZXV's languages, an inputs file and a README. Running it
 * runs its STEPS in order. Each step is one file in one language:
 *
 *   treaty   zxt Sutra treaties (kernel/src/treaty), run on a scratch engine
 *   sutra    COBOL/Sutra record layouts (sdk/pillars/cobol-sutra)
 *   trebago  exact numeric kernels (sdk/pillars/fortran-trebago)
 *   eve      range-checked Ada/Eve types (sdk/pillars/ada-eve)
 *   kron     exact APL/K array expressions (sdk/pillars/apl-kron)
 *   myra     MUMPS/MYRA tree records (sdk/pillars/mumps-myra)
 *
 * Every step ends in a four-valued VERDICT (libzxv's paraconsistent
 * logic, ZXV_LP_*), never a bare yes/no:
 *
 *   TRUE     the step ran and every check passed
 *   FALSE    refused: the input broke a rule (with line, column, reason)
 *   BOTH     contradictory evidence: HELD for a person to review. A
 *            contradiction is kept and shown; it never "explodes" into
 *            an arbitrary answer and it is never silently resolved.
 *   NEITHER  not enough to decide: a missing input, or a step that did
 *            not run because an earlier one was held or refused
 *
 * The run's verdict folds the steps: any FALSE gives FALSE, else any BOTH
 * gives BOTH, else any NEITHER gives NEITHER, else TRUE.
 *
 * SAFETY. A step marked `gate` runs only when every earlier step is TRUE.
 * A step marked `advisory` produces a proposal only: the result says a
 * person must approve it, and the Playground never actuates anything.
 * Treaty settlement runs on a fresh scratch engine, never on the owner's
 * books. AI drafts (template.zxv `draft` lines) are untrusted text until
 * they pass the same checks as anything a person typed.
 *
 * Results are exact: signed integers, int64/int64 rationals or four-valued
 * truth, as text. No floats anywhere (R11).
 *
 * The result of a run is one JSON object (see README.md for the schema).
 * PLAYGROUND API STABILITY: 0.x, may change before 1.0.
 */
#ifndef ZXVPLAY_H
#define ZXVPLAY_H

#include <stddef.h>
#include <stdint.h>

#define ZP_VERSION "0.1.0"

#define ZP_OK        0
#define ZP_E_ARG     (-2501) /* a NULL pointer or an unknown language          */
#define ZP_E_NOMEM   (-2502) /* out of memory                                  */
#define ZP_E_MANIFEST (-2503) /* template.zxv does not parse                    */
#define ZP_E_FILE    (-2504) /* a step names a file the template does not have */
#define ZP_E_NOTFOUND (-2505) /* no template with that id                       */
#define ZP_E_LIMIT   (-2506) /* a size limit (files, steps, bytes)             */
#define ZP_E_IO      (-2507) /* a file could not be read or written            */

#define ZP_MAX_FILES  16u
#define ZP_MAX_STEPS  12u
#define ZP_MAX_FILE   (256u * 1024u) /* bytes in one file          */
#define ZP_MAX_NAME   63u            /* bytes in an id or a name   */

/* One file of a template: a name (no directories) and its bytes. */
typedef struct {
    const char *name;
    const char *text;
    size_t len;
} zp_file;

/* A template: its files. The manifest is the file named "template.zxv". */
typedef struct {
    const char *id;
    const zp_file *files;
    uint32_t n_files;
} zp_template;

/* The templates compiled into the library (generated from templates/). */
uint32_t zp_template_count(void);
const zp_template *zp_template_at(uint32_t index);
const zp_template *zp_template_find(const char *id);
/* Find one file of a template, or NULL. */
const zp_file *zp_template_file(const zp_template *t, const char *name);

/* The catalogue as JSON: [{"id","title","lang","audience","safety",
 * "summary","files":[...],"draft":{...}}...]. *out is malloc'd. */
int32_t zp_catalogue_json(char **out, size_t *out_len);

/* Run a template. `files` overrides or adds files by name (the user's
 * edits); n_files may be 0. *out is the result JSON, malloc'd, also on a
 * refusal (it then says why). Returns ZP_OK when a result was written,
 * even if its verdict is FALSE; a negative code only when no result could
 * be written at all. */
int32_t zp_run(const zp_template *t, const zp_file *files, uint32_t n_files, char **out,
               size_t *out_len);

/* Run one source text in one language with NAME = VALUE input lines
 * (may be NULL). Same result JSON, with one step. */
int32_t zp_run_one(const char *lang, const char *src, size_t len, const char *inputs,
                   size_t inputs_len, char **out, size_t *out_len);

/* The prompt the local AI swarm gets to draft a template's file: the
 * template's `draft` instruction, the language's rules and the current
 * text. *out is malloc'd. ZP_E_NOTFOUND if the template has no draft. */
int32_t zp_draft_prompt(const zp_template *t, const zp_file *files, uint32_t n_files,
                        const char *request, char **out, size_t *out_len);

/* The Playground tab's request body (POST /api/play/run and /draft):
 *
 *   template ID\n                      the built-in template to start from
 *   request N\n<N bytes>\n             optional: what the user asks the AI
 *   file NAME N\n<N bytes>\n           each edited file, any number
 *
 * Lengths are byte counts, so file text needs no escaping. Files point
 * into `body`, which must outlive the request. */
typedef struct {
    char template_id[ZP_MAX_NAME + 1];
    char names[ZP_MAX_FILES][ZP_MAX_NAME + 1];
    zp_file files[ZP_MAX_FILES];
    uint32_t n_files;
    const char *request;
    size_t request_len;
} zp_request;

int32_t zp_request_parse(const char *body, size_t len, zp_request *out);

/* Languages known to this build ("treaty", "kron", ...). */
uint32_t zp_lang_count(void);
const char *zp_lang_name(uint32_t index);

const char *zp_status_name(int32_t status);

#endif /* ZXVPLAY_H */
