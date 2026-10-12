/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp.h — an app's manifest: which C view and model it has, and which of
 * its rules are written in ZXV's own languages.
 *
 * The toolkit (zui), the renderer and the input layers are C, the kernel's
 * native language, because the same code runs on bare metal. An app's own
 * logic does not have to be, and no app is one language: each rule or
 * calculation is written in the pillar language that fits it and bound here
 * by name, so one app mixes as many as it needs (a wallet keeps its books in
 * Sutra, its limits in Eve and its charts in KRON, behind one C screen).
 *
 *   Language  For                                   Pillar
 *   sutra     money, ledgers, settlement rules      kernel/src/sutra, sdk/pillars/cobol-sutra
 *   eve       safety, limits and policy             sdk/pillars/ada-eve
 *   kron      numbers and array views (exact)       sdk/pillars/apl-kron
 *   trebago   heavier exact numeric kernels         sdk/pillars/fortran-trebago
 *   myra      records, indexes and search           sdk/pillars/mumps-myra
 *   c         the view and the model                 kernel/src/zui/views, kernel/src/apps
 *
 *   A1 ONE FILE. A manifest is plain text, one directive per line, '#'
 *      starts a comment:
 *
 *        zxapp 1
 *        id     murmuration
 *        name   Murmuration
 *        view   zui/views/murmuration.c
 *        model  apps/murmuration_model.c
 *        logic  kron cycle_share apps/murmuration/cycle_share.kron
 *        logic  kron top_three <<
 *        3#re@>re
 *        .
 *        logic  eve  within_limits apps/murmuration/limits.eve
 *
 *      "logic <language> <entry> <path>" names a source file; "<<" instead
 *      of a path takes the source inline, up to a line holding only ".",
 *      for targets with no file system (bare metal boots its apps from the
 *      image).
 *   A2 RUNTIMES REGISTER. This file knows no language. A runtime (the KRON
 *      evaluator, the Sutra interpreter, ...) registers a zxapp_runtime_t;
 *      zxapp_load compiles every logic entry whose language has one. An
 *      entry with no runtime stays listed and unloaded, and the app uses its
 *      C model for that rule instead, so a build without a pillar still
 *      runs.
 *   A3 EXACT VALUES. Arguments and results are integers or exact
 *      rationals (num/den), never floats, matching the pillars' rule R11.
 *   A4 NO ALLOCATION here. Parsed text stays in the caller's buffer (inline
 *      sources point into it, so it must outlive the app); a runtime may
 *      allocate inside its own load and free it in unload.
 */
#ifndef ZXAPP_H
#define ZXAPP_H

#include <stdbool.h>
#include <stdint.h>

#define ZXAPP_LOGIC_MAX 16u
#define ZXAPP_ID_MAX    32u
#define ZXAPP_PATH_MAX  128u
#define ZXAPP_ERR_MAX   160u
#define ZXAPP_RUNTIMES  8u

typedef enum {
    ZXAPP_LANG_C = 0,
    ZXAPP_LANG_SUTRA,
    ZXAPP_LANG_EVE,
    ZXAPP_LANG_KRON,
    ZXAPP_LANG_TREBAGO,
    ZXAPP_LANG_MYRA,
    ZXAPP_LANG_COUNT,
    ZXAPP_LANG_UNKNOWN = -1
} zxapp_lang_t;

enum {
    ZXAPP_OK = 0,
    ZXAPP_E_SYNTAX = -1,    /* a line of the manifest does not parse          */
    ZXAPP_E_FULL = -2,      /* more than ZXAPP_LOGIC_MAX entries, or runtimes  */
    ZXAPP_E_NAME = -3,      /* no such entry                                   */
    ZXAPP_E_NORUNTIME = -4, /* the entry's language has no runtime loaded   */
    ZXAPP_E_RUNTIME = -5,   /* the runtime refused (its message is in err)     */
    ZXAPP_E_ARG = -6,
    ZXAPP_E_BUFFER = -7 /* the result has more elements than fit           */
};

/* A number (den > 0), or a truth: den = 0 and num is one of these
 * (libzxv's ZXV_LP_* codes, which KRON and Eve return). */
typedef struct {
    int64_t num, den;
} zxapp_num_t;
enum { ZXAPP_NEITHER = 0, ZXAPP_TRUE = 1, ZXAPP_FALSE = 2, ZXAPP_BOTH = 3 };

/* One named input: an integer vector (a scalar is n = 1). */
typedef struct {
    const char *name;
    const int64_t *v;
    uint32_t n;
} zxapp_arg_t;

typedef struct {
    zxapp_lang_t lang;
    char entry[ZXAPP_ID_MAX];
    char path[ZXAPP_PATH_MAX]; /* "" when inline                 */
    const char *src;           /* inline source, or NULL          */
    uint32_t src_len;
    uint32_t line;  /* where it is declared in the manifest */
    void *compiled; /* the runtime's handle after zxapp_load */
    bool loaded;
} zxapp_logic_t;

typedef struct {
    char id[ZXAPP_ID_MAX];
    char name[48];
    char view[ZXAPP_PATH_MAX];
    char model[ZXAPP_PATH_MAX];
    zxapp_logic_t logic[ZXAPP_LOGIC_MAX];
    uint32_t n;
    char err[ZXAPP_ERR_MAX]; /* the last refusal, for the view to show */
    uint32_t err_line;       /* the manifest line it came from (0 = none) */
} zxapp_t;

/* A2: a language runtime. read_file is how it gets a path's source on
 * hosted builds (NULL: path entries cannot load, inline ones can). */
typedef struct {
    zxapp_lang_t lang;
    void *ctx;
    int (*load)(void *ctx, const char *src, uint32_t n, void **compiled, char *err, uint32_t cap);
    /* Run `compiled` with args; write up to cap results into out and their
     * count into *n_out (more than cap: ZXAPP_E_BUFFER, *n_out = needed). */
    int (*call)(void *ctx, void *compiled, const zxapp_arg_t *args, uint32_t nargs,
                zxapp_num_t *out, uint32_t cap, uint32_t *n_out, char *err, uint32_t errcap);
    void (*unload)(void *ctx, void *compiled);
} zxapp_runtime_t;

typedef struct {
    zxapp_runtime_t rt[ZXAPP_RUNTIMES];
    uint32_t n;
    /* Reads a path into buf; returns its length or < 0. */
    int32_t (*read_file)(void *ctx, const char *path, char *buf, uint32_t cap);
    void *read_ctx;
    char file_buf[64u * 1024u]; /* one source at a time, for load() */
} zxapp_host_t;

const char *zxapp_lang_name(zxapp_lang_t l);  /* "kron"; "?" when unknown */
zxapp_lang_t zxapp_lang_of(const char *name); /* ZXAPP_LANG_UNKNOWN when unknown */

/* A1: parse `len` bytes of manifest text. On ZXAPP_E_SYNTAX a->err and
 * a->err_line say where. The text must outlive `a` (inline sources). */
int zxapp_parse(zxapp_t *a, const char *text, uint32_t len);
const zxapp_logic_t *zxapp_find(const zxapp_t *a, const char *entry);

void zxapp_host_init(zxapp_host_t *h);
int zxapp_host_register(zxapp_host_t *h, const zxapp_runtime_t *rt);
bool zxapp_host_has(const zxapp_host_t *h, zxapp_lang_t lang);

/* A2: compile every entry that has a runtime. Returns the number loaded,
 * or the first runtime refusal (a->err names the entry). Entries whose
 * language has no runtime are skipped, not errors. */
int zxapp_load(zxapp_t *a, zxapp_host_t *h);
void zxapp_unload(zxapp_t *a, zxapp_host_t *h);

/* Run entry `entry`. ZXAPP_E_NORUNTIME when it is not loaded: the caller
 * then computes the same thing in C (A2). */
int zxapp_call(zxapp_t *a, zxapp_host_t *h, const char *entry, const zxapp_arg_t *args,
               uint32_t nargs, zxapp_num_t *out, uint32_t cap, uint32_t *n_out);

#endif /* ZXAPP_H */
