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
 *   fortran77, basic, forth   legacy programs, as written  kernel/src/lingua
 *   zlir      any of those, precompiled to Lingua IR    kernel/src/lingua
 *   c         the view and the model                 kernel/src/zui/views, kernel/src/apps
 *
 * Modern languages are first class too, each through its libzxv SDK (the
 * same ABI, so the same exact values): cpp, rust, go, swift, kotlin, java,
 * dotnet (C#, F#, VB), python, typescript (and JavaScript), dart, ruby and
 * php. Whatever else there is compiles to WebAssembly and runs as "wasm"
 * in the Z-Wasm sandbox (sdk/kits/z-wasm, zxapp_wasm.c): fuel-metered,
 * integer-only, nothing imported but what the host grants. Rust, Go, C++,
 * Swift, Kotlin, Dart, Zig and AssemblyScript all target it, so an app can
 * ship logic in any of them as a .wasm even where no native runtime for
 * that language is installed. Names are case-insensitive and take the
 * usual aliases (c++, csharp, c#, js, ts, golang, ...).
 *
 *   A1 ONE FILE. A manifest is plain text, one directive per line, '#'
 *      starts a comment:
 *
 *        zxapp 1
 *        id     murmuration
 *        name   Murmuration
 *        view   zui/views/murmuration.c
 *        model  apps/murmuration_model.c
 *        logic  kron cycle_share apps/blocks/swarm_shares/cycle_share.kron
 *        logic  kron top_three <<
 *        3#re@>re
 *        .
 *        logic  eve  within_limits apps/blocks/swarm_shares/limits.eve
 *
 *      "launch <theme>" names the app's launch screen theme (optional).
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
 *   A5 A BASKET, NOT ONE LANGUAGE. Every language has strengths and
 *      weaknesses, so each feature is bound to the one that suits it, and
 *      a feature that matters (money, safety) can be written twice, in two
 *      languages, under one entry name:
 *
 *        logic  sutra fee apps/wallet/fee.sutra
 *        logic  kron  fee apps/wallet/fee.kron
 *
 *      zxapp_call runs the first that can run. zxapp_call_witnessed runs
 *      them all and joins the answers with the paraconsistent logic: all
 *      agree TRUE, any differ (or one answers and one refuses) BOTH, only
 *      one could run NEITHER (unwitnessed). BOTH is ZXAPP_E_CONFLICT: the
 *      caller holds the result and says so, it never picks one silently.
 *      A view can witness a language with its C model the same way
 *      (zxapp_witness).
 *
 * Apps are built from components ("blocks"), like a site from plugins: a
 * block is a manifest of its own, a few entries in whatever languages suit
 * them, made once and used by every app that needs it.
 *
 *   B1 A BLOCK starts "zxblock 1", has an id and a "version X.Y.Z", and
 *      may not use other blocks (the app composes them, so there is no
 *      tree to resolve). An app takes one with "use <block> <version>":
 *      the newest registered block with that id and major version, at
 *      least that minor.patch; "=X.Y.Z" asks for exactly that version.
 *   B2 A CONTRACT for every entry: "provides <entry> <num | truth>
 *      [input ...]" names its inputs, in order, and the kind of answer.
 *      A block is registered only when every provides line has logic and
 *      every logic line has a provides line. An app may promise its own
 *      entries the same way.
 *   B3 CHECKED ON EVERY CALL. Arguments that are not the promised inputs
 *      are ZXAPP_E_CONTRACT before any language runs; an answer of the
 *      wrong kind is ZXAPP_E_CONTRACT too, and a witnessed call counts it
 *      as a refusal, so it joins as BOTH (A5), never as an answer.
 *   B4 COMPOSING copies a used block's entries into the app as
 *      "<block>.<entry>" (each still in its own language, with its
 *      contract), after the app's own entries; zxapp_load then loads them
 *      like any other. Compose again after zxapp_unload to pick up a newer
 *      block.
 *
 * Apps are blocks too, so people build apps out of apps without writing C.
 *
 *   C1 AN APP EXPORTS what it provides. An app with a version and
 *      provides lines can be registered and used like a block; only the
 *      entries it provides are exported (its other entries stay private),
 *      and it may export entries it composed from its own blocks
 *      ("provides swarm_shares.cycle_share num t"), which then nest as
 *      "<app>.swarm_shares.cycle_share". An app that uses blocks is
 *      composed before it is registered, so every use points at something
 *      already whole: there are no cycles to find.
 *   C2 PROVENANCE. Every composed entry remembers the manifest that wrote
 *      it ("author <name>", its id, version and content id), through any
 *      depth of nesting. zxapp_provenance says who made which part of an
 *      app and what share of its entries each part is, the basis for
 *      crediting authors later.
 *   C3 SHARED BY CONTENT. A manifest's content id is "sha256:" and the
 *      SHA-256 of its text. "use <block> <version> <cid>" pins that exact
 *      content: a different manifest under the same id is refused. A pin
 *      covers code only when it is all in the manifest, so a pinned block
 *      must carry its own logic inline ("<<") and pin its own uses too.
 *
 * Apps are wired together like a circuit, too: a "logic wire" entry names
 * other entries as parts and wires their inputs (W1 to W6 in zxwire.h; the
 * hook is zxapp_wiring_t below). The Circuit board (zxboard.h) edits them.
 *
 * Apps hand work to each other by what they can do, not by name.
 *
 *   I1 AN INTENT says an app can do something with a kind of content:
 *      "intent <verb> <type> <entry>". The verb is a short word (share,
 *      open, view, edit, pick, or any other the apps agree on); the type
 *      is a MIME type ("text/plain") or a pattern, written here with <star>
 *      for the asterisk ("image/<star>", or "<star>/<star>" for anything);
 *      the entry is the logic that does it, the app's own or a composed one
 *      ("notes.append"). Verbs and types ignore case. Up to
 *      ZXAPP_INTENTS per app, one per verb and type. A block has no window
 *      to open, so intents belong to apps.
 *   I2 WHO CAN. zxapp_handlers lists the apps that handle a verb for a
 *      type, most specific first: an exact type, then "major/<star>", then
 *      anything; apps in the order given within each. Each app is listed
 *      once, through its most specific intent. Asking for a pattern finds
 *      only handlers that take all of it ("image/<star>" is not handled by
 *      an app that takes only "image/png").
 *   I3 NOTHING RUNS here. The query only names entries; the share sheet
 *      (zui_share.h) shows them to the person, and the one they pick is
 *      run through zxapp_call, under its contract (B3).
 */
#ifndef ZXAPP_H
#define ZXAPP_H

#include <stdbool.h>
#include <stdint.h>

#define ZXAPP_LOGIC_MAX   32u
#define ZXAPP_ID_MAX      32u
#define ZXAPP_PATH_MAX    128u
#define ZXAPP_ERR_MAX     160u
#define ZXAPP_ENTRY_MAX   128u /* "block.entry", deeper for nested apps (B4, C1) */
#define ZXAPP_USES        8u
#define ZXAPP_INPUTS      8u
#define ZXAPP_BLOCKS      64u
#define ZXAPP_CID_MAX     72u /* "sha256:" + 64 hex digits + NUL (C3) */
#define ZXAPP_AUTHOR_MAX  48u
#define ZXAPP_WITNESS_MAX 256u /* elements a second witness's answer may have */
#define ZXAPP_INTENTS     16u
#define ZXAPP_VERB_MAX    16u
#define ZXAPP_MIME_MAX    64u
#define ZXAPP_RUNTIMES    ((uint32_t) ZXAPP_LANG_COUNT)

typedef enum {
    ZXAPP_LANG_C = 0,
    ZXAPP_LANG_SUTRA,
    ZXAPP_LANG_EVE,
    ZXAPP_LANG_KRON,
    ZXAPP_LANG_TREBAGO,
    ZXAPP_LANG_MYRA,
    ZXAPP_LANG_CPP, /* the modern languages, through their libzxv SDKs */
    ZXAPP_LANG_RUST,
    ZXAPP_LANG_GO,
    ZXAPP_LANG_SWIFT,
    ZXAPP_LANG_KOTLIN,
    ZXAPP_LANG_JAVA,
    ZXAPP_LANG_DOTNET,
    ZXAPP_LANG_PYTHON,
    ZXAPP_LANG_TYPESCRIPT,
    ZXAPP_LANG_DART,
    ZXAPP_LANG_RUBY,
    ZXAPP_LANG_PHP,
    ZXAPP_LANG_WASM,      /* anything that compiles to WebAssembly (Z-Wasm) */
    ZXAPP_LANG_WIRE,      /* W: a circuit of other entries, wired (zxwire.h) */
    ZXAPP_LANG_FORTRAN77, /* the legacy languages, through Lingua (zxapp_lingua.c) */
    ZXAPP_LANG_BASIC,
    ZXAPP_LANG_FORTH,
    ZXAPP_LANG_ZLIR, /* a Lingua program precompiled to IR text (zl_pack) */
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
    ZXAPP_E_BUFFER = -7,   /* the result has more elements than fit           */
    ZXAPP_E_CONFLICT = -8, /* A5: the witnesses disagree; hold, do not act */
    ZXAPP_E_CONTRACT = -9  /* B3: arguments or answer break the entry's contract */
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

/* B2: what an entry promises: its inputs, in order, and what it answers. */
enum { ZXAPP_KIND_NUM = 1, ZXAPP_KIND_TRUTH = 2 };
typedef struct {
    char entry[ZXAPP_ENTRY_MAX]; /* an app may export a composed entry (C1) */
    uint8_t kind;                /* ZXAPP_KIND_* */
    char input[ZXAPP_INPUTS][ZXAPP_ID_MAX];
    uint32_t ninputs;
    uint32_t line;
} zxapp_provide_t;

/* B1: "use <block> <version>": the newest registered block with that id
 * and the same major version, at least this minor.patch ("=" first: that
 * version exactly). */
typedef struct {
    char id[ZXAPP_ID_MAX];
    uint16_t ver[3];
    bool exact;
    char cid[ZXAPP_CID_MAX]; /* C3: pinned to this exact content; "" = any */
    uint32_t line;
} zxapp_use_t;

/* I1: "intent <verb> <type> <entry>", stored in lower case. */
typedef struct {
    char verb[ZXAPP_VERB_MAX];
    char type[ZXAPP_MIME_MAX]; /* "text/plain", "image/<star>", "<star>/<star>" */
    char entry[ZXAPP_ENTRY_MAX];
    uint32_t line;
} zxapp_intent_t;

typedef struct {
    zxapp_lang_t lang;
    char entry[ZXAPP_ENTRY_MAX];
    char path[ZXAPP_PATH_MAX]; /* "" when inline                 */
    const char *src;           /* inline source, or NULL          */
    uint32_t src_len;
    uint32_t line;  /* where it is declared in the manifest */
    void *compiled; /* the runtime's handle after zxapp_load */
    bool loaded;
    const zxapp_provide_t *contract; /* B3: checked on every call; NULL = none */
    const struct zxapp *origin;      /* C2: the manifest that wrote it; NULL = this one */
} zxapp_logic_t;

typedef struct zxapp {
    char id[ZXAPP_ID_MAX];
    char name[48];
    char author[ZXAPP_AUTHOR_MAX]; /* C2: "author <name>", for provenance */
    char cid[ZXAPP_CID_MAX];       /* C3: the manifest text's content id */
    char view[ZXAPP_PATH_MAX];
    char model[ZXAPP_PATH_MAX];
    char launch[ZXAPP_ID_MAX]; /* "launch <theme>": the launch screen's theme id ("" = default) */
    zxapp_logic_t logic[ZXAPP_LOGIC_MAX];
    uint32_t n;
    bool block;      /* B1: "zxblock 1", a component, not an app */
    uint16_t ver[3]; /* a block's version (major.minor.patch) */
    zxapp_provide_t prov[ZXAPP_LOGIC_MAX];
    uint32_t nprov;
    zxapp_use_t use[ZXAPP_USES];
    uint32_t nuse;
    zxapp_intent_t intent[ZXAPP_INTENTS]; /* I1 */
    uint32_t nintent;
    uint32_t composed;       /* logic[composed..n) came from blocks (B4) */
    bool resolved;           /* zxapp_compose succeeded (C1) */
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

struct zxapp_host;

/* W1: circuits ("logic wire", zxwire.h). A circuit calls other entries of
 * its app, so it is not a runtime: zxwire_register sets this hook, and
 * zxapp_load and the calls hand "wire" entries to it. NULL: circuits
 * stay unloaded, like any language with no runtime. */
typedef struct {
    void *ctx;
    /* Check the circuit at load: its parts exist and have contracts. */
    int (*check)(void *ctx, zxapp_t *a, const zxapp_logic_t *L);
    /* Run it. *lp is its parts' join: TRUE when every part was witnessed,
     * NEITHER when any ran unwitnessed; ZXAPP_E_CONFLICT (a part held, or
     * a wire that carries the wrong kind) when it is BOTH. */
    int (*run)(void *ctx, zxapp_t *a, struct zxapp_host *h, const zxapp_logic_t *L,
               const zxapp_arg_t *args, uint32_t nargs, zxapp_num_t *out, uint32_t cap,
               uint32_t *n_out, int32_t *lp);
} zxapp_wiring_t;

typedef struct zxapp_host {
    zxapp_runtime_t rt[ZXAPP_RUNTIMES];
    uint32_t n;
    /* Reads a path into buf; returns its length or < 0. */
    int32_t (*read_file)(void *ctx, const char *path, char *buf, uint32_t cap);
    void *read_ctx;
    /* Called by zxapp_load as each entry is handled: real progress,
     * 0..256 (zxm_link_progress's scale), for the launch screen. */
    void (*progress)(void *ctx, uint32_t done256);
    void *progress_ctx;
    const zxapp_wiring_t *wiring; /* W1: NULL = no circuits */
    char file_buf[64u * 1024u];   /* one source at a time, for load() */
} zxapp_host_t;

const char *zxapp_lang_name(zxapp_lang_t l); /* "kron"; "?" when unknown */
/* A language by name or alias, any case; ZXAPP_LANG_UNKNOWN when unknown. */
zxapp_lang_t zxapp_lang_of(const char *name);

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

/* ---- B: components (blocks) ------------------------------------------------ */

/* The blocks an app can be composed from. Holds pointers: each block (and
 * its manifest text) must outlive the registry and every app composed
 * from it. */
typedef struct {
    const zxapp_t *b[ZXAPP_BLOCKS];
    uint32_t n;
    char err[ZXAPP_ERR_MAX];
} zxapp_registry_t;

void zxapp_registry_init(zxapp_registry_t *r);
/* B1/B2/C1: add a parsed block, or an app that exports entries. Refused (ZXAPP_E_CONTRACT, r->err
 * says why) when it is not a block, has no version, provides an entry no logic implements or has
 * logic it does not provide; ZXAPP_E_NAME when that id and version is already there; ZXAPP_E_FULL
 * past ZXAPP_BLOCKS. */
int zxapp_registry_add(zxapp_registry_t *r, const zxapp_t *block);
/* B1: the newest block `id` compatible with ver (see zxapp_use_t). */
const zxapp_t *zxapp_registry_find(const zxapp_registry_t *r, const char *id, const uint16_t ver[3],
                                   bool exact);
/* B4: resolve every "use" of app `a`: each used block's logic is added to
 * the app as "<block>.<entry>", with its contract. ZXAPP_E_NAME when a
 * block is missing (a->err names it and the version asked for). */
int zxapp_compose(zxapp_t *a, const zxapp_registry_t *r);

/* B2: the contract `entry` is called under (its block's, or the app's own
 * provides line); NULL when it has none. */
const zxapp_provide_t *zxapp_contract(const zxapp_t *a, const char *entry);

/* C3: "sha256:" and 64 lowercase hex digits for `len` bytes of text. */
void zxapp_cid(const char *text, uint32_t len, char out[ZXAPP_CID_MAX]);

/* C2: who made which part of an app. from is NULL for the app's own
 * entries. permille is that part's share of the app's entries, rounded
 * down (so the parts can sum to a little under 1000). */
typedef struct {
    const zxapp_t *from;
    const char *id, *author, *cid;
    uint16_t ver[3];
    uint32_t entries;
    uint32_t permille;
} zxapp_origin_t;
/* Fills up to cap parts, the app's own first; returns how many there are. */
uint32_t zxapp_provenance(const zxapp_t *a, zxapp_origin_t *out, uint32_t cap);

/* A5: run every implementation of `entry` that can run; out holds the
 * first's answer and *lp the join (ZXAPP_TRUE, ZXAPP_BOTH, ZXAPP_NEITHER).
 * ZXAPP_E_CONFLICT when *lp is BOTH; ZXAPP_E_RUNTIME when every one refused. */
int zxapp_call_witnessed(zxapp_t *a, zxapp_host_t *h, const char *entry, const zxapp_arg_t *args,
                         uint32_t nargs, zxapp_num_t *out, uint32_t cap, uint32_t *n_out,
                         int32_t *lp);
/* A5: two answers to the same question: ZXAPP_TRUE when they are the same
 * values (numbers in lowest terms, as every runtime returns them), else
 * ZXAPP_BOTH. */
int32_t zxapp_witness(const zxapp_num_t *x, uint32_t nx, const zxapp_num_t *y, uint32_t ny);

/* ---- I: intents ------------------------------------------------------------- */

/* I1: how well a handler's type pattern covers a content type: 2 the same
 * type, 1 "major/<star>" over that major, 0 "<star>/<star>"; -1 when it does not
 * cover it, or either is not a MIME type or pattern. */
int zxapp_mime_rank(const char *pattern, const char *type);

/* I2: one app that handles a verb for a type. */
typedef struct {
    uint32_t app; /* its index in the list asked */
    const zxapp_t *a;
    const zxapp_intent_t *in;
    int32_t rank; /* zxapp_mime_rank of its intent */
} zxapp_handler_t;

/* I2: the apps among apps[0..n) that handle `verb` for `type`, most
 * specific first, each once. Writes up to cap and returns how many there
 * are (more than cap: only the first cap are written). */
uint32_t zxapp_handlers(const zxapp_t *const *apps, uint32_t n, const char *verb, const char *type,
                        zxapp_handler_t *out, uint32_t cap);
/* I2 over a registry's apps (those that export entries, C1). */
uint32_t zxapp_registry_handlers(const zxapp_registry_t *r, const char *verb, const char *type,
                                 zxapp_handler_t *out, uint32_t cap);

#endif /* ZXAPP_H */
