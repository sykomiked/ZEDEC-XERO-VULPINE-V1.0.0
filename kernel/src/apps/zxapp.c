/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp.c — app manifests and the language-runtime registry (zxapp.h). */
#include "zxapp.h"

#include "../robin_debanks/sha256.h"

/* The native, SDK and wasm runtimes; the Lingua languages follow ZLIR. */
static const char *const LANG[ZXAPP_LANG_ZL_FORTRAN] = {
    "c",      "sutra", "eve",    "kron",   "trebago",    "myra", "cpp",  "rust", "go",   "swift",
    "kotlin", "java",  "dotnet", "python", "typescript", "dart", "ruby", "php",  "wasm", "wire", "zlir"};

/* Each Lingua language: the name an app writes (zl_langs.def's runtime
 * field), and its catalogue name and its twin's name, accepted too. */
static const struct {
    const char *rt, *name, *twin;
} ZL[] = {
#define ZL_LANG_X(id, n, name, twin, fe, ext, rt) {rt, name, twin},
#include "../lingua/zl_langs.def"
#undef ZL_LANG_X
};
_Static_assert(sizeof ZL / sizeof ZL[0] == ZXAPP_LANG_COUNT - ZXAPP_LANG_ZL_FORTRAN,
               "one Lingua name per Lingua runtime");

/* Other names people write for the same languages. */
static const struct {
    const char *name;
    zxapp_lang_t lang;
} ALIAS[] = {
    {"cobol", ZXAPP_LANG_SUTRA},      {"ada", ZXAPP_LANG_EVE},
    {"apl", ZXAPP_LANG_KRON},         {"k", ZXAPP_LANG_KRON},
    {"fortran", ZXAPP_LANG_TREBAGO},  {"mumps", ZXAPP_LANG_MYRA},
    {"m", ZXAPP_LANG_MYRA},           {"c++", ZXAPP_LANG_CPP},
    {"cxx", ZXAPP_LANG_CPP},          {"golang", ZXAPP_LANG_GO},
    {"kt", ZXAPP_LANG_KOTLIN},        {"csharp", ZXAPP_LANG_DOTNET},
    {"c#", ZXAPP_LANG_DOTNET},        {"cs", ZXAPP_LANG_DOTNET},
    {"fsharp", ZXAPP_LANG_DOTNET},    {"f#", ZXAPP_LANG_DOTNET},
    {"vb", ZXAPP_LANG_DOTNET},        {"py", ZXAPP_LANG_PYTHON},
    {"ts", ZXAPP_LANG_TYPESCRIPT},    {"javascript", ZXAPP_LANG_TYPESCRIPT},
    {"js", ZXAPP_LANG_TYPESCRIPT},    {"rb", ZXAPP_LANG_RUBY},
    {"webassembly", ZXAPP_LANG_WASM}, {"f77", ZXAPP_LANG_FORTRAN77},
    {"circuit", ZXAPP_LANG_WIRE},
    {"lingua", ZXAPP_LANG_ZLIR},      {"algol", ZXAPP_LANG_ZL_ALGOL60},
};

static bool seq(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; a[i] && a[i] == b[i]; i++) {
    }
    return a[i] == b[i];
}

/* Equal, ignoring ASCII case (language names). */
static bool seq_ci(const char *a, const char *b)
{
    uint32_t i = 0;
    for (;; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char) (x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char) (y - 'A' + 'a');
        if (x != y) return false;
        if (!x) return true;
    }
}

static void put_err(zxapp_t *a, uint32_t line, const char *m1, const char *m2)
{
    uint32_t n = 0;
    for (const char *p = m1; p && *p && n + 1u < ZXAPP_ERR_MAX; p++) a->err[n++] = *p;
    for (const char *p = m2; p && *p && n + 1u < ZXAPP_ERR_MAX; p++) a->err[n++] = *p;
    a->err[n] = 0;
    a->err_line = line;
}

const char *zxapp_lang_name(zxapp_lang_t l)
{
    if (l < 0 || l >= ZXAPP_LANG_COUNT) return "?";
    return l < ZXAPP_LANG_ZL_FORTRAN ? LANG[l] : ZL[l - ZXAPP_LANG_ZL_FORTRAN].rt;
}

/* Runtime names first, then the aliases (so "fortran" stays Trebago), then
 * each Lingua language's catalogue name and its twin's name. */
zxapp_lang_t zxapp_lang_of(const char *name)
{
    for (int i = 0; i < ZXAPP_LANG_COUNT; i++)
        if (seq_ci(name, zxapp_lang_name((zxapp_lang_t) i))) return (zxapp_lang_t) i;
    for (uint32_t i = 0; i < sizeof ALIAS / sizeof ALIAS[0]; i++)
        if (seq_ci(name, ALIAS[i].name)) return ALIAS[i].lang;
    for (uint32_t i = 0; i < sizeof ZL / sizeof ZL[0]; i++)
        if (seq_ci(name, ZL[i].name) || seq_ci(name, ZL[i].twin))
            return (zxapp_lang_t) (ZXAPP_LANG_ZL_FORTRAN + (int) i);
    return ZXAPP_LANG_UNKNOWN;
}

/* ---- parsing --------------------------------------------------------------- */

typedef struct {
    const char *s;
    uint32_t n, at, line;
} rd_t;

/* The next line, without its newline (and a trailing CR). */
static bool next_line(rd_t *r, const char **p, uint32_t *len)
{
    if (r->at >= r->n) return false;
    uint32_t b = r->at;
    while (r->at < r->n && r->s[r->at] != '\n') r->at++;
    uint32_t e = r->at;
    if (r->at < r->n) r->at++;
    if (e > b && r->s[e - 1] == '\r') e--;
    *p = r->s + b;
    *len = e - b;
    r->line++;
    return true;
}

static bool space(char c)
{
    return c == ' ' || c == '\t';
}

/* Split a line into up to 4 words (the 4th takes the rest of the line,
 * trimmed). '#' outside a word starts a comment. Returns the count. */
static uint32_t words(const char *p, uint32_t n, const char *w[4], uint32_t wl[4])
{
    uint32_t k = 0, i = 0;
    while (k < 4) {
        while (i < n && space(p[i])) i++;
        if (i >= n || p[i] == '#') break;
        uint32_t b = i;
        if (k == 3) {
            uint32_t e = n;
            for (uint32_t j = i; j < n; j++)
                if (p[j] == '#' && space(p[j - 1])) {
                    e = j;
                    break;
                }
            while (e > b && space(p[e - 1])) e--;
            w[k] = p + b;
            wl[k++] = e - b;
            break;
        }
        while (i < n && !space(p[i])) i++;
        w[k] = p + b;
        wl[k++] = i - b;
    }
    return k;
}

static bool word_is(const char *w, uint32_t n, const char *lit)
{
    uint32_t i = 0;
    for (; i < n && lit[i] && w[i] == lit[i]; i++) {
    }
    return i == n && !lit[i];
}

static bool word_copy(char *d, uint32_t cap, const char *w, uint32_t n)
{
    if (n + 1u > cap) return false;
    for (uint32_t i = 0; i < n; i++) d[i] = w[i];
    d[n] = 0;
    return true;
}

static bool ident(const char *w, uint32_t n)
{
    if (!n) return false;
    for (uint32_t i = 0; i < n; i++) {
        char c = w[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
                  (i && c >= '0' && c <= '9');
        if (!ok) return false;
    }
    return true;
}

/* Identifiers joined by dots: an entry, or a composed one (C1). */
static bool dotted(const char *w, uint32_t n)
{
    uint32_t b = 0;
    for (uint32_t i = 0; i <= n; i++)
        if (i == n || w[i] == '.') {
            if (!ident(w + b, i - b) || i - b + 1u > ZXAPP_ID_MAX) return false;
            b = i + 1u;
        }
    return true;
}

static bool cid_ok(const char *w, uint32_t n)
{
    if (n != 71u || !word_is(w, 7, "sha256:")) return false;
    for (uint32_t i = 7; i < n; i++)
        if (!((w[i] >= '0' && w[i] <= '9') || (w[i] >= 'a' && w[i] <= 'f'))) return false;
    return true;
}

/* "1", "1.2" or "1.2.3" (missing parts are 0); "=" first when allowed
 * marks an exact version. */
static bool parse_ver(const char *w, uint32_t n, uint16_t ver[3], bool *exact, bool allow_eq)
{
    uint32_t i = 0, part = 0;
    *exact = false;
    ver[0] = ver[1] = ver[2] = 0;
    if (allow_eq && n && w[0] == '=') {
        *exact = true;
        i++;
    }
    for (;;) {
        uint32_t v = 0, b = i;
        for (; i < n && w[i] >= '0' && w[i] <= '9'; i++) {
            v = v * 10u + (uint32_t) (w[i] - '0');
            if (v > 65535u) return false;
        }
        if (i == b) return false;
        ver[part++] = (uint16_t) v;
        if (i == n) return true;
        if (w[i] != '.' || part == 3u) return false;
        i++;
    }
}

/* B2: provides <entry> <num | truth> [input ...] */
static int parse_provides(zxapp_t *a, uint32_t line, const char *const w[4], const uint32_t wl[4],
                          uint32_t k)
{
    if (k < 3) {
        put_err(a, line, "provides needs: provides <entry> <num | truth> [input ...]", 0);
        return ZXAPP_E_SYNTAX;
    }
    if (!dotted(w[1], wl[1]) || wl[1] + 1u > ZXAPP_ENTRY_MAX) {
        put_err(a, line, "an entry name must be a short identifier (block.entry to re-export)", 0);
        return ZXAPP_E_SYNTAX;
    }
    if (a->nprov >= ZXAPP_LOGIC_MAX) {
        put_err(a, line, "too many provides lines", 0);
        return ZXAPP_E_FULL;
    }
    zxapp_provide_t *P = &a->prov[a->nprov];
    word_copy(P->entry, sizeof P->entry, w[1], wl[1]);
    for (uint32_t i = 0; i < a->nprov; i++)
        if (seq(a->prov[i].entry, P->entry)) {
            put_err(a, line, "a second provides for ", P->entry);
            return ZXAPP_E_SYNTAX;
        }
    if (word_is(w[2], wl[2], "num"))
        P->kind = ZXAPP_KIND_NUM;
    else if (word_is(w[2], wl[2], "truth"))
        P->kind = ZXAPP_KIND_TRUTH;
    else {
        put_err(a, line, "an entry answers num or truth", 0);
        return ZXAPP_E_SYNTAX;
    }
    P->ninputs = 0;
    if (k == 4) { /* the rest of the line: the inputs, in order */
        const char *p = w[3];
        uint32_t n = wl[3], i = 0;
        while (i < n) {
            while (i < n && space(p[i])) i++;
            uint32_t b = i;
            while (i < n && !space(p[i])) i++;
            if (i == b) break;
            if (P->ninputs >= ZXAPP_INPUTS || !ident(p + b, i - b) ||
                !word_copy(P->input[P->ninputs], ZXAPP_ID_MAX, p + b, i - b)) {
                put_err(a, line, "inputs are up to 8 short identifiers", 0);
                return ZXAPP_E_SYNTAX;
            }
            for (uint32_t j = 0; j < P->ninputs; j++)
                if (seq(P->input[j], P->input[P->ninputs])) {
                    put_err(a, line, "an input named twice: ", P->input[j]);
                    return ZXAPP_E_SYNTAX;
                }
            P->ninputs++;
        }
    }
    P->line = line;
    a->nprov++;
    return ZXAPP_OK;
}

/* B1: use <block> [=]<version> */
static int parse_use(zxapp_t *a, uint32_t line, const char *const w[4], const uint32_t wl[4],
                     uint32_t k)
{
    if (a->block) {
        put_err(a, line, "a block cannot use other blocks; the app composes them", 0);
        return ZXAPP_E_SYNTAX;
    }
    if (k != 3 && k != 4) {
        put_err(a, line, "use needs: use <block> [=]<version> [sha256:...]", 0);
        return ZXAPP_E_SYNTAX;
    }
    if (a->nuse >= ZXAPP_USES) {
        put_err(a, line, "too many use lines", 0);
        return ZXAPP_E_FULL;
    }
    zxapp_use_t *U = &a->use[a->nuse];
    if (!ident(w[1], wl[1]) || !word_copy(U->id, sizeof U->id, w[1], wl[1])) {
        put_err(a, line, "a block id must be a short identifier", 0);
        return ZXAPP_E_SYNTAX;
    }
    if (!parse_ver(w[2], wl[2], U->ver, &U->exact, true)) {
        put_err(a, line, "a bad version (1, 1.2, 1.2.3, or =1.2.3 for exactly that)", 0);
        return ZXAPP_E_SYNTAX;
    }
    if (k == 4 && (!cid_ok(w[3], wl[3]) || !word_copy(U->cid, sizeof U->cid, w[3], wl[3]))) {
        put_err(a, line, "a content id is \"sha256:\" and 64 lowercase hex digits", 0);
        return ZXAPP_E_SYNTAX;
    }
    for (uint32_t i = 0; i < a->nuse; i++)
        if (seq(a->use[i].id, U->id)) {
            put_err(a, line, "a block used twice: ", U->id);
            return ZXAPP_E_SYNTAX;
        }
    U->line = line;
    a->nuse++;
    return ZXAPP_OK;
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

/* A MIME token: letters, digits and ! # $ & ^ _ . + - (RFC 6838's set). */
static bool mime_token(const char *w, uint32_t n)
{
    if (!n || n > 63u) return false;
    for (uint32_t i = 0; i < n; i++) {
        char c = w[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '!' || c == '#' || c == '$' || c == '&' || c == '^' || c == '_' ||
                  c == '.' || c == '+' || c == '-';
        if (!ok) return false;
    }
    return true;
}

/* I1: "major/minor", "major/<star>" or "<star>/<star>". *slash gets the '/'. */
static bool mime_ok(const char *w, uint32_t n, uint32_t *slash)
{
    uint32_t s = 0;
    while (s < n && w[s] != '/') s++;
    if (s == n || s == 0 || s + 1u == n) return false;
    *slash = s;
    const char *mj = w, *mn = w + s + 1u;
    uint32_t nj = s, nn = n - s - 1u;
    bool star_j = nj == 1u && mj[0] == '*', star_n = nn == 1u && mn[0] == '*';
    if (star_j) return star_n;
    return mime_token(mj, nj) && (star_n || mime_token(mn, nn));
}

/* I1: intent <verb> <type> <entry> */
static int parse_intent(zxapp_t *a, uint32_t line, const char *const w[4], const uint32_t wl[4],
                        uint32_t k)
{
    uint32_t slash;
    if (k != 4) {
        put_err(a, line, "intent needs: intent <verb> <type> <entry>", 0);
        return ZXAPP_E_SYNTAX;
    }
    if (a->block) {
        put_err(a, line, "a block has no window to open; intents belong to apps", 0);
        return ZXAPP_E_SYNTAX;
    }
    if (!ident(w[1], wl[1]) || wl[1] + 1u > ZXAPP_VERB_MAX) {
        put_err(a, line, "a verb is one short word (share, open, view, edit, pick)", 0);
        return ZXAPP_E_SYNTAX;
    }
    if (wl[2] + 1u > ZXAPP_MIME_MAX || !mime_ok(w[2], wl[2], &slash)) {
        put_err(a, line, "a type is a MIME type or pattern: text/plain, image/*, */*", 0);
        return ZXAPP_E_SYNTAX;
    }
    if (!dotted(w[3], wl[3]) || wl[3] + 1u > ZXAPP_ENTRY_MAX) {
        put_err(a, line,
                "an entry name must be a short identifier (block.entry for a composed one)", 0);
        return ZXAPP_E_SYNTAX;
    }
    if (a->nintent >= ZXAPP_INTENTS) {
        put_err(a, line, "too many intent lines", 0);
        return ZXAPP_E_FULL;
    }
    zxapp_intent_t *I = &a->intent[a->nintent];
    for (uint32_t i = 0; i < wl[1]; i++) I->verb[i] = lower(w[1][i]);
    I->verb[wl[1]] = 0;
    for (uint32_t i = 0; i < wl[2]; i++) I->type[i] = lower(w[2][i]);
    I->type[wl[2]] = 0;
    word_copy(I->entry, sizeof I->entry, w[3], wl[3]);
    for (uint32_t i = 0; i < a->nintent; i++)
        if (seq(a->intent[i].verb, I->verb) && seq(a->intent[i].type, I->type)) {
            put_err(a, line, "a second intent for the same verb and type: ", I->verb);
            return ZXAPP_E_SYNTAX;
        }
    I->line = line;
    a->nintent++;
    return ZXAPP_OK;
}

/* ---- R: registry fields ------------------------------------------------------ */

/* R3: the one table of capability names. */
static const char *const CAPS[] = {
    "net",                         /* the network (beyond peers) */
    "peers",                       /* other ZXV nodes */
    "location",                    /* where the device is (Waymark's place.*) */
    "fs.read",                     /* the Hoard's files, read */
    "fs.write",                    /* the Hoard's files, write */
    "ledger.read",                 /* the owner's books, read */
    "ledger.post",                 /* the owner's books, post */
    "camera",      "mic", "print", /* printers on the local network (print.*) */
    "scan",                        /* scanners on the local network (scan.*) */
    "backup",                      /* backups and versions (backup.*) */
    "journal",                     /* zxvd's event history (journal.*, log.tail) */
    "screen.time",                 /* screen time and its limits (screen.*) */
    "sys.read",                    /* the machine's facts (sys.*) */
    "search",                      /* the Hoard's index (search.*) */
    "swarm",                       /* the local swarm (swarm.*) */
    "ai.ask",                      /* the swarm's answers (swarm.ask) */
    "notify",                      /* notices to the person */
};

const char *const *zxapp_caps(uint32_t *n)
{
    *n = (uint32_t) (sizeof CAPS / sizeof CAPS[0]);
    return CAPS;
}

bool zxapp_cap_known(const char *cap)
{
    if (!cap) return false;
    for (uint32_t i = 0; i < sizeof CAPS / sizeof CAPS[0]; i++)
        if (seq(CAPS[i], cap)) return true;
    return false;
}

int zxapp_slot_layer(uint32_t slot)
{
    if (slot < 1u || slot > ZXAPP_SLOTS) return ZXAPP_LAYER_NONE;
    return slot <= 144u ? ZXAPP_LAYER_L1 : slot <= 271u ? ZXAPP_LAYER_L2 : ZXAPP_LAYER_L3;
}

bool zxapp_needs(const zxapp_t *a, const char *cap, const char *scope)
{
    for (uint32_t i = 0; i < a->nneeds; i++)
        if (seq(a->needs[i].cap, cap) && (!scope || seq(a->needs[i].scope, scope))) return true;
    return false;
}

/* A scope word: letters, digits and . _ : / - */
static bool scope_ok(const char *w, uint32_t n)
{
    if (!n || n + 1u > ZXAPP_SCOPE_MAX) return false;
    for (uint32_t i = 0; i < n; i++) {
        char c = w[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == ':' || c == '/' || c == '-'))
            return false;
    }
    return true;
}

/* A whole number of 1 to 19 digits; false when it is not, or it is 0 or
 * past max. */
static bool whole(const char *w, uint32_t n, uint64_t max, uint64_t *out)
{
    if (!n || n > 19u) return false;
    uint64_t v = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (w[i] < '0' || w[i] > '9') return false;
        v = v * 10u + (uint64_t) (w[i] - '0');
    }
    if (!v || v > max) return false;
    *out = v;
    return true;
}

/* P1: "pending <language> <entry>". The language is a fit-table name with
 * spaces as _ (letters, digits and + # - . _), checked against the table by
 * tools/apps/lang_coverage.py; the entry is the one a runnable language
 * stands in for. */
static int parse_pending(zxapp_t *a, uint32_t line, const char *const w[4], const uint32_t wl[4],
                         uint32_t k)
{
    if (k != 3) {
        put_err(a, line, "pending needs: pending <language> <entry>", 0);
        return ZXAPP_E_SYNTAX;
    }
    bool ok = wl[1] && wl[1] < 32u;
    for (uint32_t i = 0; ok && i < wl[1]; i++) {
        char c = w[1][i];
        ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '+' || c == '#' || c == '-' || c == '.' || c == '_';
    }
    if (!ok) {
        put_err(a, line, "a pending language is its fit-table name, spaces as _", 0);
        return ZXAPP_E_SYNTAX;
    }
    if (!dotted(w[2], wl[2]) || wl[2] + 1u > ZXAPP_ID_MAX) {
        put_err(a, line,
                "an entry name must be a short identifier (block.entry for a composed one)", 0);
        return ZXAPP_E_SYNTAX;
    }
    a->npending++;
    return ZXAPP_OK;
}

/* R1-R4: slot, layer, icon, budget and needs. */
static int parse_registry(zxapp_t *a, uint32_t line, const char *const w[4], const uint32_t wl[4],
                          uint32_t k)
{
    if (a->block) {
        put_err(a, line, "a block may not carry slot, layer, icon, budget or needs", 0);
        return ZXAPP_E_SYNTAX;
    }
    if (word_is(w[0], wl[0], "needs")) {
        if (k < 2 || k > 3 || wl[1] + 1u > ZXAPP_CAP_MAX) {
            put_err(a, line, "needs needs: needs <capability> [<scope>]", 0);
            return ZXAPP_E_SYNTAX;
        }
        char cap[ZXAPP_CAP_MAX];
        word_copy(cap, sizeof cap, w[1], wl[1]);
        if (!zxapp_cap_known(cap)) {
            put_err(a, line, "an unknown capability: ", cap);
            return ZXAPP_E_SYNTAX;
        }
        if (k == 3 && !scope_ok(w[2], wl[2])) {
            put_err(a, line, "a scope is one short word (space:home)", 0);
            return ZXAPP_E_SYNTAX;
        }
        if (a->nneeds >= ZXAPP_NEEDS) {
            put_err(a, line, "too many needs lines", 0);
            return ZXAPP_E_FULL;
        }
        zxapp_need_t *N = &a->needs[a->nneeds];
        word_copy(N->cap, sizeof N->cap, cap, wl[1]);
        if (k == 3) word_copy(N->scope, sizeof N->scope, w[2], wl[2]);
        for (uint32_t i = 0; i < a->nneeds; i++)
            if (seq(a->needs[i].cap, N->cap) && seq(a->needs[i].scope, N->scope)) {
                put_err(a, line, "the same need twice: ", N->cap);
                N->cap[0] = N->scope[0] = 0;
                return ZXAPP_E_SYNTAX;
            }
        N->line = line;
        a->nneeds++;
        return ZXAPP_OK;
    }
    if (k != 2) {
        put_err(a, line, "one value is expected", 0);
        return ZXAPP_E_SYNTAX;
    }
    uint64_t v;
    if (word_is(w[0], wl[0], "slot")) {
        if (a->slot) {
            put_err(a, line, "a second slot line", 0);
            return ZXAPP_E_SYNTAX;
        }
        if (!whole(w[1], wl[1], ZXAPP_SLOTS, &v)) {
            put_err(a, line, "a slot is a number from 1 to 407", 0);
            return ZXAPP_E_SYNTAX;
        }
        a->slot = (uint16_t) v;
    } else if (word_is(w[0], wl[0], "layer")) {
        if (a->layer) {
            put_err(a, line, "a second layer line", 0);
            return ZXAPP_E_SYNTAX;
        }
        if (word_is(w[1], wl[1], "L1"))
            a->layer = ZXAPP_LAYER_L1;
        else if (word_is(w[1], wl[1], "L2"))
            a->layer = ZXAPP_LAYER_L2;
        else if (word_is(w[1], wl[1], "L3"))
            a->layer = ZXAPP_LAYER_L3;
        else {
            put_err(a, line, "a layer is L1, L2 or L3", 0);
            return ZXAPP_E_SYNTAX;
        }
    } else if (word_is(w[0], wl[0], "icon")) {
        if (a->icon[0]) {
            put_err(a, line, "a second icon line", 0);
            return ZXAPP_E_SYNTAX;
        }
        if (!word_copy(a->icon, sizeof a->icon, w[1], wl[1])) {
            put_err(a, line, "the icon path is too long", 0);
            return ZXAPP_E_SYNTAX;
        }
    } else {
        if (a->budget) {
            put_err(a, line, "a second budget line", 0);
            return ZXAPP_E_SYNTAX;
        }
        if (!whole(w[1], wl[1], UINT64_C(9223372036854775807), &v)) {
            put_err(a, line, "a budget is a whole number of tokens per cycle, from 1", 0);
            return ZXAPP_E_SYNTAX;
        }
        a->budget = v;
    }
    if (a->slot && a->layer && zxapp_slot_layer(a->slot) != a->layer) {
        put_err(a, line, "the slot is outside its layer (L1 1-144, L2 145-271, L3 272-407)", 0);
        return ZXAPP_E_SYNTAX;
    }
    return ZXAPP_OK;
}

int zxapp_parse(zxapp_t *a, const char *text, uint32_t len)
{
    uint8_t *z = (uint8_t *) a;
    for (uint32_t i = 0; i < sizeof *a; i++) z[i] = 0;
    if (!text) {
        put_err(a, 0, "no manifest text", 0);
        return ZXAPP_E_ARG;
    }
    zxapp_cid(text, len, a->cid);
    rd_t r = {text, len, 0, 0};
    const char *p;
    uint32_t n;
    bool header = false;
    while (next_line(&r, &p, &n)) {
        const char *w[4];
        uint32_t wl[4];
        uint32_t k = words(p, n, w, wl);
        if (!k) continue;
        if (!header) {
            bool app = word_is(w[0], wl[0], "zxapp"), blk = word_is(w[0], wl[0], "zxblock");
            if (k != 2 || !(app || blk) || !word_is(w[1], wl[1], "1")) {
                put_err(a, r.line, "the first line must be \"zxapp 1\" or \"zxblock 1\"", 0);
                return ZXAPP_E_SYNTAX;
            }
            a->block = blk;
            header = true;
            continue;
        }
        if (word_is(w[0], wl[0], "id") || word_is(w[0], wl[0], "name") ||
            word_is(w[0], wl[0], "view") || word_is(w[0], wl[0], "model") ||
            word_is(w[0], wl[0], "launch") || word_is(w[0], wl[0], "author")) {
            if (k < 2) {
                put_err(a, r.line, "a value is missing", 0);
                return ZXAPP_E_SYNTAX;
            }
            /* name may have spaces: everything after the directive */
            const char *v = w[1];
            uint32_t vl = (k >= 3) ? (uint32_t) ((w[k - 1] + wl[k - 1]) - w[1]) : wl[1];
            bool ok;
            if (word_is(w[0], wl[0], "id"))
                ok = k == 2 && ident(v, vl) && word_copy(a->id, sizeof a->id, v, vl);
            else if (word_is(w[0], wl[0], "name"))
                ok = word_copy(a->name, sizeof a->name, v, vl);
            else if (word_is(w[0], wl[0], "author"))
                ok = word_copy(a->author, sizeof a->author, v, vl);
            else if (word_is(w[0], wl[0], "launch"))
                ok = k == 2 && ident(v, vl) && word_copy(a->launch, sizeof a->launch, v, vl);
            else if (word_is(w[0], wl[0], "view"))
                ok = k == 2 && word_copy(a->view, sizeof a->view, v, vl);
            else
                ok = k == 2 && word_copy(a->model, sizeof a->model, v, vl);
            if (!ok) {
                put_err(a, r.line, "a bad or too long value", 0);
                return ZXAPP_E_SYNTAX;
            }
            continue;
        }
        if (word_is(w[0], wl[0], "logic")) {
            if (k != 4) {
                put_err(a, r.line, "logic needs: logic <language> <entry> <path | <<>", 0);
                return ZXAPP_E_SYNTAX;
            }
            char lang[16];
            if (!word_copy(lang, sizeof lang, w[1], wl[1]) ||
                zxapp_lang_of(lang) == ZXAPP_LANG_UNKNOWN) {
                put_err(a, r.line,
                        "an unknown language (a pillar, c, a libzxv SDK language, wasm or wire)",
                        0);
                return ZXAPP_E_SYNTAX;
            }
            if (!ident(w[2], wl[2]) || wl[2] + 1u > ZXAPP_ID_MAX) {
                put_err(a, r.line, "an entry name must be a short identifier", 0);
                return ZXAPP_E_SYNTAX;
            }
            if (a->n >= ZXAPP_LOGIC_MAX) {
                put_err(a, r.line, "too many logic entries", 0);
                return ZXAPP_E_FULL;
            }
            zxapp_logic_t *L = &a->logic[a->n];
            L->lang = zxapp_lang_of(lang);
            word_copy(L->entry, sizeof L->entry, w[2], wl[2]);
            for (uint32_t i = 0; i < a->n; i++) /* A5: one per language */
                if (seq(a->logic[i].entry, L->entry) && a->logic[i].lang == L->lang) {
                    put_err(a, r.line, "a second entry in the same language named ", L->entry);
                    return ZXAPP_E_SYNTAX;
                }
            L->line = r.line;
            if (word_is(w[3], wl[3], "<<")) {
                uint32_t start = r.at, end = r.at;
                bool closed = false;
                while (next_line(&r, &p, &n)) {
                    if (n == 1 && p[0] == '.') {
                        closed = true;
                        break;
                    }
                    end = r.at;
                }
                if (!closed) {
                    put_err(a, L->line, "an inline source has no closing \".\" line", 0);
                    return ZXAPP_E_SYNTAX;
                }
                L->src = text + start;
                L->src_len = end - start;
            } else if (!word_copy(L->path, sizeof L->path, w[3], wl[3])) {
                put_err(a, r.line, "the path is too long", 0);
                return ZXAPP_E_SYNTAX;
            }
            a->n++;
            continue;
        }
        if (word_is(w[0], wl[0], "version")) {
            bool exact;
            if (k != 2 || !parse_ver(w[1], wl[1], a->ver, &exact, false) ||
                !(a->ver[0] | a->ver[1] | a->ver[2])) {
                put_err(a, r.line, "version needs: version <major.minor.patch>, not 0.0.0", 0);
                return ZXAPP_E_SYNTAX;
            }
            continue;
        }
        if (word_is(w[0], wl[0], "provides")) {
            int rc = parse_provides(a, r.line, w, wl, k);
            if (rc != ZXAPP_OK) return rc;
            continue;
        }
        if (word_is(w[0], wl[0], "use")) {
            int rc = parse_use(a, r.line, w, wl, k);
            if (rc != ZXAPP_OK) return rc;
            continue;
        }
        if (word_is(w[0], wl[0], "pending")) {
            int rc = parse_pending(a, r.line, w, wl, k);
            if (rc != ZXAPP_OK) return rc;
            continue;
        }
        if (word_is(w[0], wl[0], "intent")) {
            int rc = parse_intent(a, r.line, w, wl, k);
            if (rc != ZXAPP_OK) return rc;
            continue;
        }
        if (word_is(w[0], wl[0], "slot") || word_is(w[0], wl[0], "layer") ||
            word_is(w[0], wl[0], "icon") || word_is(w[0], wl[0], "budget") ||
            word_is(w[0], wl[0], "needs")) {
            int rc = parse_registry(a, r.line, w, wl, k);
            if (rc != ZXAPP_OK) return rc;
            continue;
        }
        put_err(a, r.line,
                "an unknown directive (id, name, author, view, model, launch, logic, version, "
                "provides, use, intent, pending, slot, layer, icon, budget, needs)",
                0);
        return ZXAPP_E_SYNTAX;
    }
    if (!header) {
        put_err(a, 0, "an empty manifest", 0);
        return ZXAPP_E_SYNTAX;
    }
    if (!a->id[0]) {
        put_err(a, 0, "the manifest has no id", 0);
        return ZXAPP_E_SYNTAX;
    }
    return ZXAPP_OK;
}

const zxapp_logic_t *zxapp_find(const zxapp_t *a, const char *entry)
{
    for (uint32_t i = 0; i < a->n; i++)
        if (seq(a->logic[i].entry, entry)) return &a->logic[i];
    return 0;
}

/* ---- runtimes ---------------------------------------------------------------- */

void zxapp_host_init(zxapp_host_t *h)
{
    h->n = 0;
    h->read_file = 0;
    h->read_ctx = 0;
    h->progress = 0;
    h->progress_ctx = 0;
}

static void report(zxapp_host_t *h, uint32_t done, uint32_t total)
{
    if (h->progress) h->progress(h->progress_ctx, total ? done * 256u / total : 256u);
}

/* W1: a circuit goes to the wiring hook, not to a runtime. */
static bool wired(const zxapp_host_t *h, const zxapp_logic_t *L)
{
    return L->lang == ZXAPP_LANG_WIRE && h->wiring && h->wiring->check && h->wiring->run;
}

static const zxapp_runtime_t *rt_for(const zxapp_host_t *h, zxapp_lang_t lang)
{
    for (uint32_t i = 0; i < h->n; i++)
        if (h->rt[i].lang == lang) return &h->rt[i];
    return 0;
}

bool zxapp_host_has(const zxapp_host_t *h, zxapp_lang_t lang)
{
    return rt_for(h, lang) != 0;
}

int zxapp_host_register(zxapp_host_t *h, const zxapp_runtime_t *rt)
{
    if (!rt || !rt->load || !rt->call || rt->lang < 0 || rt->lang >= ZXAPP_LANG_COUNT)
        return ZXAPP_E_ARG;
    for (uint32_t i = 0; i < h->n; i++)
        if (h->rt[i].lang == rt->lang) {
            h->rt[i] = *rt; /* a newer runtime for the same language replaces it */
            return ZXAPP_OK;
        }
    if (h->n >= ZXAPP_RUNTIMES) return ZXAPP_E_FULL;
    h->rt[h->n++] = *rt;
    return ZXAPP_OK;
}

int zxapp_load(zxapp_t *a, zxapp_host_t *h)
{
    int loaded = 0;
    report(h, 0, a->n);
    for (uint32_t i = 0; i < a->n; i++) {
        zxapp_logic_t *L = &a->logic[i];
        if (i) report(h, i, a->n);
        if (L->loaded) {
            loaded++;
            continue;
        }
        if (wired(h, L)) {
            if (!L->src) { /* W1: the circuit is the manifest's own text */
                put_err(a, L->line, L->entry, ": a circuit is written inline (<<)");
                return ZXAPP_E_RUNTIME;
            }
            int rc = h->wiring->check(h->wiring->ctx, a, L);
            if (rc != ZXAPP_OK) return rc; /* a->err says why */
            L->loaded = true;
            loaded++;
            continue;
        }
        const zxapp_runtime_t *rt = rt_for(h, L->lang);
        if (!rt) continue; /* A2: the C model covers it */
        const char *src = L->src;
        uint32_t n = L->src_len;
        if (!src) {
            if (!h->read_file) {
                put_err(a, L->line, "no file reader for ", L->path);
                return ZXAPP_E_RUNTIME;
            }
            int32_t got = h->read_file(h->read_ctx, L->path, h->file_buf, sizeof h->file_buf);
            if (got < 0) {
                put_err(a, L->line, "cannot read ", L->path);
                return ZXAPP_E_RUNTIME;
            }
            src = h->file_buf;
            n = (uint32_t) got;
        }
        char err[ZXAPP_ERR_MAX];
        err[0] = 0;
        if (rt->load(rt->ctx, src, n, &L->compiled, err, sizeof err) != ZXAPP_OK) {
            put_err(a, L->line, L->entry, ": ");
            uint32_t k = 0;
            while (a->err[k]) k++;
            for (uint32_t j = 0; err[j] && k + 1u < ZXAPP_ERR_MAX; j++) a->err[k++] = err[j];
            a->err[k] = 0;
            return ZXAPP_E_RUNTIME;
        }
        L->loaded = true;
        loaded++;
    }
    report(h, a->n, a->n);
    return loaded;
}

void zxapp_unload(zxapp_t *a, zxapp_host_t *h)
{
    for (uint32_t i = 0; i < a->n; i++) {
        zxapp_logic_t *L = &a->logic[i];
        if (!L->loaded) continue;
        const zxapp_runtime_t *rt = wired(h, L) ? 0 : rt_for(h, L->lang);
        if (rt && rt->unload) rt->unload(rt->ctx, L->compiled);
        L->compiled = 0;
        L->loaded = false;
    }
}

/* B3: an entry's contract: set by compose for a block's entries, else the
 * app's own provides line of that name. */
static const zxapp_provide_t *contract_of(const zxapp_t *a, const zxapp_logic_t *L)
{
    if (L->contract) return L->contract;
    for (uint32_t i = 0; i < a->nprov; i++)
        if (seq(a->prov[i].entry, L->entry)) return &a->prov[i];
    return 0;
}

const zxapp_provide_t *zxapp_contract(const zxapp_t *a, const char *entry)
{
    const zxapp_logic_t *L = a && entry ? zxapp_find(a, entry) : 0;
    return L ? contract_of(a, L) : 0;
}

/* *inner is a circuit's own join (W1), or -1 for any other language. */
static int run_one(zxapp_t *a, zxapp_host_t *h, const zxapp_logic_t *L, const zxapp_arg_t *args,
                   uint32_t nargs, zxapp_num_t *out, uint32_t cap, uint32_t *n_out, int32_t *inner)
{
    uint32_t got = 0;
    *inner = -1;
    if (wired(h, L)) {
        int32_t lp = ZXAPP_NEITHER;
        int rc = h->wiring->run(h->wiring->ctx, a, h, L, args, nargs, out, cap, &got, &lp);
        *n_out = got;
        if (rc != ZXAPP_OK) return rc; /* a->err says which part or wire */
        *inner = lp;
    } else {
        const zxapp_runtime_t *rt = rt_for(h, L->lang);
        char err[ZXAPP_ERR_MAX];
        err[0] = 0;
        int rc = rt->call(rt->ctx, L->compiled, args, nargs, out, cap, &got, err, sizeof err);
        *n_out = got;
        if (rc == ZXAPP_E_BUFFER) return rc;
        if (rc != ZXAPP_OK) {
            put_err(a, L->line, L->entry, ": ");
            uint32_t k = 0;
            while (a->err[k]) k++;
            for (uint32_t j = 0; err[j] && k + 1u < ZXAPP_ERR_MAX; j++) a->err[k++] = err[j];
            a->err[k] = 0;
            return ZXAPP_E_RUNTIME;
        }
    }
    const zxapp_provide_t *P = contract_of(a, L);
    if (P) /* B3: the answer is the kind it promised */
        for (uint32_t i = 0; i < got && i < cap; i++) {
            bool truth = out[i].den == 0 && out[i].num >= ZXAPP_NEITHER && out[i].num <= ZXAPP_BOTH;
            if (P->kind == ZXAPP_KIND_NUM ? out[i].den <= 0 : !truth) {
                put_err(a, L->line, L->entry,
                        P->kind == ZXAPP_KIND_NUM ? ": answered a truth, it provides a number"
                                                  : ": answered a number, it provides a truth");
                return ZXAPP_E_CONTRACT;
            }
        }
    return ZXAPP_OK;
}

/* B3: the arguments are the inputs the entry provides, in order. */
static int check_args(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t nargs)
{
    const zxapp_logic_t *L = zxapp_find(a, entry);
    const zxapp_provide_t *P = L ? contract_of(a, L) : 0;
    if (!P) return ZXAPP_OK;
    if (nargs != P->ninputs) {
        put_err(a, L->line, L->entry, ": the wrong number of inputs for its contract");
        return ZXAPP_E_CONTRACT;
    }
    for (uint32_t i = 0; i < nargs; i++)
        if (!args[i].name || !seq(args[i].name, P->input[i])) {
            put_err(a, L->line, L->entry, ": an input out of order or misnamed, expected ");
            uint32_t k = 0;
            while (a->err[k]) k++;
            for (const char *q = P->input[i]; *q && k + 1u < ZXAPP_ERR_MAX; q++) a->err[k++] = *q;
            a->err[k] = 0;
            return ZXAPP_E_CONTRACT;
        }
    return ZXAPP_OK;
}

static bool runnable(const zxapp_logic_t *L, const zxapp_host_t *h, const char *entry)
{
    return seq(L->entry, entry) && L->loaded && (wired(h, L) || rt_for(h, L->lang));
}

int zxapp_call(zxapp_t *a, zxapp_host_t *h, const char *entry, const zxapp_arg_t *args,
               uint32_t nargs, zxapp_num_t *out, uint32_t cap, uint32_t *n_out)
{
    uint32_t got = 0;
    if (n_out) *n_out = 0;
    if (!entry || (!out && cap) || (!args && nargs)) return ZXAPP_E_ARG;
    if (!zxapp_find(a, entry)) return ZXAPP_E_NAME;
    int crc = check_args(a, entry, args, nargs);
    if (crc != ZXAPP_OK) return crc;
    for (uint32_t i = 0; i < a->n; i++) /* the first that can run (A5) */
        if (runnable(&a->logic[i], h, entry)) {
            int32_t inner;
            int rc = run_one(a, h, &a->logic[i], args, nargs, out, cap, &got, &inner);
            if (n_out) *n_out = got;
            return rc;
        }
    return ZXAPP_E_NORUNTIME;
}

int32_t zxapp_witness(const zxapp_num_t *x, uint32_t nx, const zxapp_num_t *y, uint32_t ny)
{
    if (nx != ny) return ZXAPP_BOTH;
    for (uint32_t i = 0; i < nx; i++)
        if (x[i].num != y[i].num || x[i].den != y[i].den) return ZXAPP_BOTH;
    return ZXAPP_TRUE;
}

int zxapp_call_witnessed(zxapp_t *a, zxapp_host_t *h, const char *entry, const zxapp_arg_t *args,
                         uint32_t nargs, zxapp_num_t *out, uint32_t cap, uint32_t *n_out,
                         int32_t *lp)
{
    zxapp_num_t other[ZXAPP_WITNESS_MAX];
    uint32_t n = 0, got = 0, tried = 0, answered = 0;
    bool differ = false, held = false;
    int32_t inner = -1, first_inner = -1;
    if (n_out) *n_out = 0;
    if (lp) *lp = ZXAPP_NEITHER;
    if (!entry || (!out && cap) || (!args && nargs) || !lp) return ZXAPP_E_ARG;
    if (!zxapp_find(a, entry)) return ZXAPP_E_NAME;
    int crc = check_args(a, entry, args, nargs);
    if (crc != ZXAPP_OK) return crc;
    for (uint32_t i = 0; i < a->n; i++) {
        if (!runnable(&a->logic[i], h, entry)) continue;
        tried++;
        if (!answered) { /* the first answer speaks for the entry */
            int rc = run_one(a, h, &a->logic[i], args, nargs, out, cap, &n, &inner);
            if (rc == ZXAPP_E_BUFFER) {
                if (n_out) *n_out = n;
                return rc;
            }
            held |= rc == ZXAPP_E_CONFLICT; /* a circuit held inside (W1) */
            if (rc == ZXAPP_OK) {
                answered++;
                first_inner = inner;
                differ |= tried > 1u; /* an earlier one refused what this answers */
            }
            continue;
        }
        int rc = run_one(a, h, &a->logic[i], args, nargs, other, ZXAPP_WITNESS_MAX, &got, &inner);
        held |= rc == ZXAPP_E_CONFLICT;
        if (rc == ZXAPP_OK) answered++;
        differ |= rc != ZXAPP_OK || zxapp_witness(out, n, other, got) != ZXAPP_TRUE;
    }
    if (!tried) return ZXAPP_E_NORUNTIME;
    if (!answered && held) { /* W1: held, not refused: nothing to show */
        *lp = ZXAPP_BOTH;
        return ZXAPP_E_CONFLICT;
    }
    if (!answered) return ZXAPP_E_RUNTIME; /* every witness refused */
    if (n_out) *n_out = n;
    differ |= held;
    /* One implementation: a circuit speaks for its parts' join (W1). */
    *lp = differ        ? ZXAPP_BOTH
          : tried == 1u ? (first_inner == ZXAPP_TRUE ? ZXAPP_TRUE : ZXAPP_NEITHER)
                        : ZXAPP_TRUE;
    return differ ? ZXAPP_E_CONFLICT : ZXAPP_OK;
}

/* ---- B: components (blocks) -------------------------------------------------- */

static void reg_err(zxapp_registry_t *r, const char *m1, const char *m2)
{
    uint32_t n = 0;
    for (const char *p = m1; p && *p && n + 1u < ZXAPP_ERR_MAX; p++) r->err[n++] = *p;
    for (const char *p = m2; p && *p && n + 1u < ZXAPP_ERR_MAX; p++) r->err[n++] = *p;
    r->err[n] = 0;
}

void zxapp_registry_init(zxapp_registry_t *r)
{
    r->n = 0;
    r->err[0] = 0;
}

/* -1, 0 or 1 as x is older, the same or newer than y. */
static int ver_cmp(const uint16_t x[3], const uint16_t y[3])
{
    for (uint32_t i = 0; i < 3u; i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

static bool same_contract(const zxapp_provide_t *x, const zxapp_provide_t *y)
{
    if (x->kind != y->kind || x->ninputs != y->ninputs) return false;
    for (uint32_t i = 0; i < x->ninputs; i++)
        if (!seq(x->input[i], y->input[i])) return false;
    return true;
}

int zxapp_registry_add(zxapp_registry_t *r, const zxapp_t *b)
{
    if (!b || (!b->block && !b->nprov)) {
        reg_err(r, "not a block, and exports nothing: ", b ? b->id : 0);
        return ZXAPP_E_CONTRACT;
    }
    if (!(b->ver[0] | b->ver[1] | b->ver[2])) {
        reg_err(r, "a block needs a version: ", b->id);
        return ZXAPP_E_CONTRACT;
    }
    if (b->nuse && !b->resolved) { /* C1: whole before it is used */
        reg_err(r, "compose it before registering it: ", b->id);
        return ZXAPP_E_CONTRACT;
    }
    for (uint32_t i = 0; i < b->nprov; i++) { /* B2: every promise is kept */
        const zxapp_logic_t *L = zxapp_find(b, b->prov[i].entry);
        if (!L) {
            reg_err(r, "provides an entry no logic implements: ", b->prov[i].entry);
            return ZXAPP_E_CONTRACT;
        }
        for (uint32_t j = 0; j < b->n; j++) /* a re-export keeps its block's promise */
            if (seq(b->logic[j].entry, L->entry) && b->logic[j].contract &&
                !same_contract(b->logic[j].contract, &b->prov[i])) {
                reg_err(r, "re-exports with a different contract: ", L->entry);
                return ZXAPP_E_CONTRACT;
            }
    }
    for (uint32_t i = 0; i < b->n && b->block; i++) { /* a block offers nothing unpromised */
        bool found = false;
        for (uint32_t j = 0; j < b->nprov && !found; j++)
            found = seq(b->prov[j].entry, b->logic[i].entry);
        if (!found) {
            reg_err(r, "logic with no provides line: ", b->logic[i].entry);
            return ZXAPP_E_CONTRACT;
        }
    }
    for (uint32_t i = 0; i < r->n; i++) {
        if (r->b[i] == b || (seq(r->b[i]->id, b->id) && seq(r->b[i]->cid, b->cid))) {
            reg_err(r, "already registered: ", b->id);
            return ZXAPP_E_NAME;
        }
        if (seq(r->b[i]->id, b->id) && !ver_cmp(r->b[i]->ver, b->ver)) {
            reg_err(r, "that version is already registered: ", b->id);
            return ZXAPP_E_NAME;
        }
    }
    if (r->n >= ZXAPP_BLOCKS) {
        reg_err(r, "the registry is full", 0);
        return ZXAPP_E_FULL;
    }
    r->b[r->n++] = b;
    return ZXAPP_OK;
}

const zxapp_t *zxapp_registry_find(const zxapp_registry_t *r, const char *id, const uint16_t ver[3],
                                   bool exact)
{
    const zxapp_t *best = 0;
    if (!r || !id || !ver) return 0;
    for (uint32_t i = 0; i < r->n; i++) {
        const zxapp_t *b = r->b[i];
        if (!seq(b->id, id)) continue;
        int c = ver_cmp(b->ver, ver);
        bool ok = exact ? c == 0 : (b->ver[0] == ver[0] && c >= 0);
        if (ok && (!best || ver_cmp(b->ver, best->ver) > 0)) best = b;
    }
    return best;
}

static void ver_text(char *d, const zxapp_use_t *u)
{
    uint32_t n = 0;
    if (u->exact) d[n++] = '=';
    for (uint32_t i = 0; i < 3u; i++) {
        char t[6];
        uint32_t k = 0, v = u->ver[i];
        do {
            t[k++] = (char) ('0' + v % 10u);
            v /= 10u;
        } while (v);
        while (k) d[n++] = t[--k];
        if (i < 2u) d[n++] = '.';
    }
    d[n] = 0;
}

/* C3: a pin covers the code only when the code is in the manifest. */
static bool sealed(const zxapp_t *b)
{
    for (uint32_t i = 0; i < b->n; i++)
        if (!b->logic[i].origin && !b->logic[i].src) return false;
    for (uint32_t i = 0; i < b->nuse; i++)
        if (!b->use[i].cid[0]) return false;
    return true;
}

static const zxapp_t *find_use(const zxapp_registry_t *r, const zxapp_use_t *U)
{
    if (!U->cid[0]) return zxapp_registry_find(r, U->id, U->ver, U->exact);
    for (uint32_t i = 0; i < r->n; i++) {
        const zxapp_t *b = r->b[i];
        int c = ver_cmp(b->ver, U->ver);
        if (seq(b->id, U->id) && seq(b->cid, U->cid) &&
            (U->exact ? c == 0 : (b->ver[0] == U->ver[0] && c >= 0)))
            return b;
    }
    return 0;
}

static void err_add(zxapp_t *a, const char *m)
{
    uint32_t k = 0;
    while (a->err[k]) k++;
    for (const char *q = m; q && *q && k + 1u < ZXAPP_ERR_MAX; q++) a->err[k++] = *q;
    a->err[k] = 0;
}

int zxapp_compose(zxapp_t *a, const zxapp_registry_t *r)
{
    if (!a || !r || a->block) return ZXAPP_E_ARG;
    uint32_t own = a->n; /* B4: an app's own entries come first; composed ones have a '.' */
    for (uint32_t i = 0; i < a->n; i++) {
        bool dot = false;
        for (const char *q = a->logic[i].entry; *q && !dot; q++) dot = *q == '.';
        if (dot) {
            own = i;
            break;
        }
    }
    for (uint32_t i = own; i < a->n; i++)
        if (a->logic[i].loaded) {
            put_err(a, a->logic[i].line, "unload before composing again: ", a->logic[i].entry);
            return ZXAPP_E_ARG;
        }
    a->n = own;
    a->composed = own;
    a->resolved = false;
    for (uint32_t u = 0; u < a->nuse; u++) {
        const zxapp_use_t *U = &a->use[u];
        const zxapp_t *b = find_use(r, U);
        if (!b) {
            char v[24];
            ver_text(v, U);
            put_err(a, U->line, "no registered block ", U->id);
            err_add(a, " at version ");
            err_add(a, v);
            if (U->cid[0]) err_add(a, " with that content id");
            a->n = own;
            return ZXAPP_E_NAME;
        }
        if (b == a) {
            put_err(a, U->line, "an app cannot use itself: ", U->id);
            a->n = own;
            return ZXAPP_E_NAME;
        }
        if (U->cid[0] && !sealed(b)) {
            put_err(a, U->line, U->id, ": pinned, but its code is not all in its manifest");
            a->n = own;
            return ZXAPP_E_CONTRACT;
        }
        for (uint32_t i = 0; i < b->nprov; i++) { /* C1: only what it exports */
            const zxapp_provide_t *P = &b->prov[i];
            const zxapp_logic_t *S = 0;
            for (uint32_t j = 0; j < b->n; j++) { /* every language of that entry (A5) */
                if (!seq(b->logic[j].entry, P->entry)) continue;
                S = &b->logic[j];
                uint32_t idl = 0, enl = 0;
                while (b->id[idl]) idl++;
                while (S->entry[enl]) enl++;
                if (idl + 1u + enl + 1u > ZXAPP_ENTRY_MAX) {
                    put_err(a, U->line, "a name is too deeply nested: ", S->entry);
                    a->n = own;
                    return ZXAPP_E_FULL;
                }
                if (a->n >= ZXAPP_LOGIC_MAX) {
                    put_err(a, U->line, "too many logic entries after using ", U->id);
                    a->n = own;
                    return ZXAPP_E_FULL;
                }
                zxapp_logic_t *L = &a->logic[a->n];
                *L = *S;
                uint32_t k = 0;
                for (uint32_t q = 0; q < idl; q++) L->entry[k++] = b->id[q];
                L->entry[k++] = '.';
                for (uint32_t q = 0; q < enl; q++) L->entry[k++] = S->entry[q];
                L->entry[k] = 0;
                L->compiled = 0;
                L->loaded = false;
                L->contract = P;
                L->origin = S->origin ? S->origin : b;
                a->n++;
            }
        }
    }
    a->resolved = true;
    return ZXAPP_OK;
}

void zxapp_cid(const char *text, uint32_t len, char out[ZXAPP_CID_MAX])
{
    static const char HEX[] = "0123456789abcdef";
    uint8_t d[SHA256_DIGEST_LEN];
    sha256((const uint8_t *) (text ? text : ""), text ? len : 0u, d);
    const char *pre = "sha256:";
    uint32_t k = 0;
    for (; pre[k]; k++) out[k] = pre[k];
    for (uint32_t i = 0; i < SHA256_DIGEST_LEN; i++) {
        out[k++] = HEX[d[i] >> 4];
        out[k++] = HEX[d[i] & 15u];
    }
    out[k] = 0;
}

uint32_t zxapp_provenance(const zxapp_t *a, zxapp_origin_t *out, uint32_t cap)
{
    const zxapp_t *seen[ZXAPP_LOGIC_MAX + 1u];
    uint32_t cnt[ZXAPP_LOGIC_MAX + 1u], m = 0;
    if (!a) return 0;
    seen[m] = 0; /* the app's own part comes first, even when empty */
    cnt[m++] = 0;
    for (uint32_t i = 0; i < a->n; i++) {
        const zxapp_t *o = a->logic[i].origin;
        uint32_t j = 0;
        while (j < m && seen[j] != o) j++;
        if (j == m) {
            seen[m] = o;
            cnt[m++] = 0;
        }
        cnt[j]++;
    }
    for (uint32_t j = 0; j < m && j < cap && out; j++) {
        const zxapp_t *o = seen[j] ? seen[j] : a;
        zxapp_origin_t *x = &out[j];
        x->from = seen[j];
        x->id = o->id;
        x->author = o->author;
        x->cid = o->cid;
        x->ver[0] = o->ver[0];
        x->ver[1] = o->ver[1];
        x->ver[2] = o->ver[2];
        x->entries = cnt[j];
        x->permille = a->n ? cnt[j] * 1000u / a->n : 0u;
    }
    return m;
}

/* ---- I: intents ---------------------------------------------------------------- */

static uint32_t slen(const char *p)
{
    uint32_t n = 0;
    while (p[n]) n++;
    return n;
}

/* Equal for n bytes, ignoring ASCII case. */
static bool neq_ci(const char *x, const char *y, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (lower(x[i]) != lower(y[i])) return false;
    return true;
}

int zxapp_mime_rank(const char *pattern, const char *type)
{
    uint32_t ps, ts;
    if (!pattern || !type) return -1;
    uint32_t pn = slen(pattern), tn = slen(type);
    if (!mime_ok(pattern, pn, &ps) || !mime_ok(type, tn, &ts)) return -1;
    if (pattern[0] == '*') return 0; /* takes anything */
    if (type[0] == '*') return -1;   /* anything is more than this pattern takes */
    if (ps != ts || !neq_ci(pattern, type, ps)) return -1;
    bool p_star = pattern[ps + 1u] == '*', t_star = type[ts + 1u] == '*';
    if (p_star) return 1;
    if (t_star) return -1;
    return (pn == tn && neq_ci(pattern, type, pn)) ? 2 : -1;
}

uint32_t zxapp_handlers(const zxapp_t *const *apps, uint32_t n, const char *verb, const char *type,
                        zxapp_handler_t *out, uint32_t cap)
{
    uint32_t total = 0;
    if (!apps || !verb || !type || (!out && cap)) return 0;
    uint32_t vn = slen(verb);
    for (int32_t want = 2; want >= 0; want--) /* most specific first (I2) */
        for (uint32_t i = 0; i < n; i++) {
            const zxapp_t *a = apps[i];
            if (!a) continue;
            const zxapp_intent_t *best = 0;
            int32_t rank = -1;
            for (uint32_t j = 0; j < a->nintent; j++) {
                const zxapp_intent_t *I = &a->intent[j];
                if (slen(I->verb) != vn || !neq_ci(I->verb, verb, vn)) continue;
                int32_t r = zxapp_mime_rank(I->type, type);
                if (r > rank) {
                    rank = r;
                    best = I;
                }
            }
            if (!best || rank != want) continue;
            if (total < cap) out[total] = (zxapp_handler_t){i, a, best, rank};
            total++;
        }
    return total;
}

uint32_t zxapp_registry_handlers(const zxapp_registry_t *r, const char *verb, const char *type,
                                 zxapp_handler_t *out, uint32_t cap)
{
    if (!r) return 0;
    return zxapp_handlers(r->b, r->n, verb, type, out, cap);
}
