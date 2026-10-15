/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxapp.c — app manifests (zxapp.h) and the language runtimes over the
 * pillars: KRON, Eve, Sutra, Trebago, MYRA and WebAssembly. Run by
 * test_zxapp.sh, which passes kernel/src for apps/fee_check.zxapp. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "zxapp.h"
#include "zxapp_eve.h"
#include "zxapp_kron.h"
#include "zxapp_lingua.h"
#include "zxapp_myra.h"
#include "zxapp_sutra.h"
#include "zxapp_trebago.h"
#include "zxapp_wasm.h"
#include "zxboard.h"
#include "zxwire.h"
#include "../lingua/zl.h"
#include "wasm_build.h" /* sdk/kits/z-wasm/test: modules without a toolchain */

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static zxapp_t A;
static zxapp_host_t H;

static int parse(const char *t)
{
    return zxapp_parse(&A, t, (uint32_t) strlen(t));
}

static void test_names(void)
{
    CHECK(strcmp(zxapp_lang_name(ZXAPP_LANG_KRON), "kron") == 0, "kron name");
    CHECK(strcmp(zxapp_lang_name(ZXAPP_LANG_SUTRA), "sutra") == 0, "sutra name");
    CHECK(strcmp(zxapp_lang_name(ZXAPP_LANG_MYRA), "myra") == 0, "myra name");
    CHECK(strcmp(zxapp_lang_name(ZXAPP_LANG_C), "c") == 0, "c name");
    CHECK(strcmp(zxapp_lang_name(ZXAPP_LANG_COUNT), "?") == 0, "past the end");
    CHECK(strcmp(zxapp_lang_name(ZXAPP_LANG_UNKNOWN), "?") == 0, "unknown");
    for (int i = 0; i < ZXAPP_LANG_COUNT; i++)
        CHECK(zxapp_lang_of(zxapp_lang_name((zxapp_lang_t) i)) == (zxapp_lang_t) i, "round trip");
    CHECK(zxapp_lang_of("KRON") == ZXAPP_LANG_KRON, "names ignore case");
    static const struct {
        const char *n;
        zxapp_lang_t l;
    } alias[] = {{"c++", ZXAPP_LANG_CPP},
                 {"C#", ZXAPP_LANG_DOTNET},
                 {"fsharp", ZXAPP_LANG_DOTNET},
                 {"js", ZXAPP_LANG_TYPESCRIPT},
                 {"JavaScript", ZXAPP_LANG_TYPESCRIPT},
                 {"golang", ZXAPP_LANG_GO},
                 {"py", ZXAPP_LANG_PYTHON},
                 {"kt", ZXAPP_LANG_KOTLIN},
                 {"WebAssembly", ZXAPP_LANG_WASM},
                 {"apl", ZXAPP_LANG_KRON},
                 {"cobol", ZXAPP_LANG_SUTRA},
                 {"ada", ZXAPP_LANG_EVE},
                 {"fortran", ZXAPP_LANG_TREBAGO},
                 {"mumps", ZXAPP_LANG_MYRA},
                 {"rust", ZXAPP_LANG_RUST},
                 {"swift", ZXAPP_LANG_SWIFT},
                 {"java", ZXAPP_LANG_JAVA},
                 {"dart", ZXAPP_LANG_DART},
                 {"ruby", ZXAPP_LANG_RUBY},
                 {"php", ZXAPP_LANG_PHP},
                 {"fortran77", ZXAPP_LANG_FORTRAN77},
                 {"F77", ZXAPP_LANG_FORTRAN77},
                 {"Reckonwyrm", ZXAPP_LANG_FORTRAN77},
                 {"BASIC", ZXAPP_LANG_BASIC},
                 {"Hatchling", ZXAPP_LANG_BASIC},
                 {"forth", ZXAPP_LANG_FORTH},
                 {"Cairndrake", ZXAPP_LANG_FORTH},
                 {"zlir", ZXAPP_LANG_ZLIR},
                 {"lingua", ZXAPP_LANG_ZLIR}};
    for (unsigned i = 0; i < sizeof alias / sizeof alias[0]; i++)
        CHECK(zxapp_lang_of(alias[i].n) == alias[i].l, "alias %s", alias[i].n);
    CHECK(zxapp_lang_of("cobolx") == ZXAPP_LANG_UNKNOWN && zxapp_lang_of("") == ZXAPP_LANG_UNKNOWN,
          "near misses are unknown");
    CHECK(ZXAPP_RUNTIMES >= (uint32_t) ZXAPP_LANG_COUNT, "a runtime for every language fits");
    CHECK(zxapp_lang_of("kro") == ZXAPP_LANG_UNKNOWN, "no prefix match");
    CHECK(zxapp_lang_of("") == ZXAPP_LANG_UNKNOWN, "empty");
}

static void test_parse(void)
{
    const char *m = "# a comment\n"
                    "\n"
                    "zxapp 1\n"
                    "id     murmuration   # trailing comment\n"
                    "name   Murmuration Swarm\n"
                    "view   zui/views/murmuration.c\r\n"
                    "model  apps/murmuration_model.c\n"
                    "logic  kron share apps/m/share.kron\n"
                    "logic  eve  limits <<\n"
                    "type Tokens is range 0 .. 1000;\n"
                    "  second line\n"
                    ".\n"
                    "logic sutra fee apps/m/fee file.sutra # a path with a space\n";
    CHECK(parse(m) == ZXAPP_OK, "a full manifest parses");
    CHECK(strcmp(A.id, "murmuration") == 0, "id");
    CHECK(strcmp(A.name, "Murmuration Swarm") == 0, "name keeps its spaces");
    CHECK(strcmp(A.view, "zui/views/murmuration.c") == 0, "view, CR dropped");
    CHECK(strcmp(A.model, "apps/murmuration_model.c") == 0, "model");
    CHECK(A.n == 3, "three logic entries");
    CHECK(A.logic[0].lang == ZXAPP_LANG_KRON && strcmp(A.logic[0].entry, "share") == 0 &&
              strcmp(A.logic[0].path, "apps/m/share.kron") == 0 && !A.logic[0].src,
          "a path entry");
    CHECK(A.logic[0].line == 8, "its line");
    CHECK(A.logic[1].lang == ZXAPP_LANG_EVE && A.logic[1].path[0] == 0 && A.logic[1].src,
          "an inline entry");
    CHECK(A.logic[1].src_len == strlen("type Tokens is range 0 .. 1000;\n  second line\n") &&
              memcmp(A.logic[1].src, "type Tokens", 11) == 0,
          "inline source is the lines between << and .");
    CHECK(strcmp(A.logic[2].path, "apps/m/fee file.sutra") == 0, "rest of line is the path");
    CHECK(A.logic[2].line == 13, "line counting continues after an inline block");
    CHECK(zxapp_find(&A, "limits") == &A.logic[1], "find");
    CHECK(zxapp_find(&A, "limit") == 0 && zxapp_find(&A, "") == 0, "find is exact");

    CHECK(parse("zxapp 1\nid x\nlogic kron a <<\n.\n") == ZXAPP_OK && A.logic[0].src_len == 0,
          "an empty inline block");
    CHECK(parse("zxapp 1\nid x\nlogic kron a <<\n1+1\n") == ZXAPP_E_SYNTAX && A.err_line == 3,
          "an unclosed block names its logic line");
    CHECK(parse("zxapp 2\nid x\n") == ZXAPP_E_SYNTAX && A.err_line == 1, "version 2 refused");
    CHECK(parse("id x\n") == ZXAPP_E_SYNTAX && A.err_line == 1, "no header");
    CHECK(parse("zxapp 1 extra\nid x\n") == ZXAPP_E_SYNTAX, "header with extra words");
    CHECK(parse("") == ZXAPP_E_SYNTAX && A.err_line == 0, "empty");
    CHECK(parse("# only a comment\n") == ZXAPP_E_SYNTAX, "only a comment");
    CHECK(parse("zxapp 1\n") == ZXAPP_E_SYNTAX && strstr(A.err, "no id"), "id required");
    CHECK(parse("zxapp 1\nid two words\n") == ZXAPP_E_SYNTAX && A.err_line == 2, "id one word");
    CHECK(parse("zxapp 1\nid 9lives\n") == ZXAPP_E_SYNTAX, "id is an identifier");
    CHECK(parse("zxapp 1\nid a_b9\n") == ZXAPP_OK, "underscore and digits");
    CHECK(parse("zxapp 1\nid x\nview\n") == ZXAPP_E_SYNTAX && A.err_line == 3, "missing value");
    CHECK(parse("zxapp 1\nid x\ncolour gold\n") == ZXAPP_E_SYNTAX && A.err_line == 3,
          "unknown directive");
    CHECK(parse("zxapp 1\nid x\npending Common_Lisp risk\npending C# share\npending Objective-C "
                "a.b\n") == ZXAPP_OK &&
              A.npending == 3,
          "pending lines name a waiting language");
    CHECK(parse("zxapp 1\nid x\npending Lisp\n") == ZXAPP_E_SYNTAX && A.err_line == 3,
          "pending needs a language and an entry");
    CHECK(parse("zxapp 1\nid x\npending Lisp a b\n") == ZXAPP_E_SYNTAX, "pending one entry");
    CHECK(parse("zxapp 1\nid x\npending Lisp/2 a\n") == ZXAPP_E_SYNTAX &&
              strstr(A.err, "fit-table name"),
          "pending language characters");
    CHECK(parse("zxapp 1\nid x\npending ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef a\n") == ZXAPP_E_SYNTAX,
          "pending language at most 31 characters");
    CHECK(parse("zxapp 1\nid x\npending ABCDEFGHIJKLMNOPQRSTUVWXYZabcde a\n") == ZXAPP_OK,
          "pending language of 31 characters");
    CHECK(parse("zxapp 1\nid x\npending Lisp 1a\n") == ZXAPP_E_SYNTAX &&
              strstr(A.err, "entry name"),
          "pending entry identifier");
    CHECK(parse("zxapp 1\nid x\nlogic brainfork a b\n") == ZXAPP_E_SYNTAX &&
              strstr(A.err, "unknown language"),
          "unknown language");
    CHECK(parse("zxapp 1\nid x\nlogic kron a\n") == ZXAPP_E_SYNTAX, "logic needs a source");
    CHECK(parse("zxapp 1\nid x\nlogic kron 1a p\n") == ZXAPP_E_SYNTAX, "entry identifier");
    CHECK(parse("zxapp 1\nid x\nlogic kron a p\nlogic kron a q\n") == ZXAPP_E_SYNTAX &&
              A.err_line == 4,
          "a second entry in the same language");
    CHECK(parse("zxapp 1\nid x\nlogic kron a p\nlogic eve a q\nlogic wasm a r\n") == ZXAPP_OK &&
              A.n == 3,
          "A5: one entry, three languages");
    char big[4096];
    int n = sprintf(big, "zxapp 1\nid x\n");
    for (unsigned i = 0; i < ZXAPP_LOGIC_MAX; i++) n += sprintf(big + n, "logic kron e%u p\n", i);
    CHECK(parse(big) == ZXAPP_OK && A.n == ZXAPP_LOGIC_MAX, "the most entries");
    sprintf(big + n, "logic kron last p\n");
    CHECK(parse(big) == ZXAPP_E_FULL, "one too many");
    char longid[64];
    memset(longid, 'a', sizeof longid);
    longid[ZXAPP_ID_MAX] = 0;
    sprintf(big, "zxapp 1\nid %s\n", longid);
    CHECK(parse(big) == ZXAPP_E_SYNTAX, "id too long");
    longid[ZXAPP_ID_MAX - 1] = 0;
    sprintf(big, "zxapp 1\nid %s\n", longid);
    CHECK(parse(big) == ZXAPP_OK, "longest id");
    CHECK(zxapp_parse(&A, 0, 3) == ZXAPP_E_ARG, "NULL text");
    /* the length is honoured, not the NUL */
    const char *cut = "zxapp 1\nid x\nbogus\n";
    CHECK(zxapp_parse(&A, cut, 13) == ZXAPP_OK, "length stops before a bad line");
}

/* ---- a fake runtime: records what it is asked ---------------------------- */

static int loads, calls, unloads;
static char last_src[256];

static int f_load(void *ctx, const char *src, uint32_t n, void **c, char *err, uint32_t cap)
{
    (void) ctx;
    loads++;
    memcpy(last_src, src, n < sizeof last_src - 1 ? n : sizeof last_src - 1);
    last_src[n < sizeof last_src - 1 ? n : sizeof last_src - 1] = 0;
    if (n >= 3 && memcmp(src, "bad", 3) == 0) {
        snprintf(err, cap, "line 1: no");
        return ZXAPP_E_RUNTIME;
    }
    *c = (void *) (uintptr_t) (0x100 + loads);
    return ZXAPP_OK;
}

static int f_call(void *ctx, void *c, const zxapp_arg_t *args, uint32_t nargs, zxapp_num_t *out,
                  uint32_t cap, uint32_t *n_out, char *err, uint32_t errcap)
{
    (void) ctx;
    (void) c;
    calls++;
    if (nargs && args[0].n == 0) {
        snprintf(err, errcap, "empty");
        return ZXAPP_E_RUNTIME;
    }
    uint32_t need = nargs ? args[0].n : 1;
    *n_out = need;
    if (need > cap) return ZXAPP_E_BUFFER;
    for (uint32_t i = 0; i < need; i++) out[i] = (zxapp_num_t){nargs ? args[0].v[i] * 2 : 7, 1};
    return ZXAPP_OK;
}

static void f_unload(void *ctx, void *c)
{
    (void) ctx;
    (void) c;
    unloads++;
}

static int32_t f_read(void *ctx, const char *path, char *buf, uint32_t cap)
{
    (void) ctx;
    if (strcmp(path, "missing") == 0) return -1;
    int n = snprintf(buf, cap, "src of %s", path);
    return n;
}

static void test_host(void)
{
    zxapp_host_init(&H);
    zxapp_runtime_t bad = {ZXAPP_LANG_EVE, 0, 0, f_call, 0};
    CHECK(zxapp_host_register(&H, &bad) == ZXAPP_E_ARG, "a runtime needs load");
    bad.load = f_load;
    bad.call = 0;
    CHECK(zxapp_host_register(&H, &bad) == ZXAPP_E_ARG, "and call");
    bad.call = f_call;
    bad.lang = ZXAPP_LANG_COUNT;
    CHECK(zxapp_host_register(&H, &bad) == ZXAPP_E_ARG, "a real language");
    CHECK(zxapp_host_register(&H, 0) == ZXAPP_E_ARG, "NULL");
    zxapp_runtime_t eve = {ZXAPP_LANG_EVE, 0, f_load, f_call, f_unload};
    CHECK(zxapp_host_register(&H, &eve) == ZXAPP_OK && H.n == 1, "register eve");
    CHECK(zxapp_host_register(&H, &eve) == ZXAPP_OK && H.n == 1, "same language replaces");
    CHECK(zxapp_host_has(&H, ZXAPP_LANG_EVE) && !zxapp_host_has(&H, ZXAPP_LANG_SUTRA), "has");

    CHECK(parse("zxapp 1\nid t\nlogic eve inl <<\nrule\n.\nlogic eve file rules.eve\n"
                "logic sutra money fee.sutra\n") == ZXAPP_OK,
          "manifest");
    loads = 0;
    CHECK(zxapp_load(&A, &H) == ZXAPP_E_RUNTIME && strstr(A.err, "no file reader") &&
              A.err_line == 6,
          "a path needs a reader");
    CHECK(A.logic[0].loaded && !A.logic[1].loaded, "the inline one loaded first");
    CHECK(strcmp(last_src, "rule\n") == 0, "inline source handed over as is");
    H.read_file = f_read;
    CHECK(zxapp_load(&A, &H) == 2 && loads == 2, "now both eve entries load");
    CHECK(strcmp(last_src, "src of rules.eve") == 0, "file source handed over");
    CHECK(!A.logic[2].loaded, "sutra has no runtime: skipped, not an error (A2)");
    CHECK(zxapp_load(&A, &H) == 2 && loads == 2, "loading again loads nothing new");

    int64_t v[3] = {1, 2, 3};
    zxapp_arg_t arg = {"x", v, 3};
    zxapp_num_t out[4];
    uint32_t n = 99;
    CHECK(zxapp_call(&A, &H, "money", &arg, 1, out, 4, &n) == ZXAPP_E_NORUNTIME && n == 0,
          "no runtime: the C model computes it");
    CHECK(zxapp_call(&A, &H, "nope", &arg, 1, out, 4, &n) == ZXAPP_E_NAME, "unknown entry");
    CHECK(zxapp_call(&A, &H, "file", &arg, 1, out, 4, &n) == ZXAPP_OK && n == 3 &&
              out[0].num == 2 && out[2].num == 6 && out[1].den == 1,
          "a call");
    CHECK(zxapp_call(&A, &H, "file", &arg, 1, out, 2, &n) == ZXAPP_E_BUFFER && n == 3,
          "too small: how many are needed");
    zxapp_arg_t empty = {"x", v, 0};
    CHECK(zxapp_call(&A, &H, "file", &empty, 1, out, 4, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "file: empty") && A.err_line == 6,
          "a runtime refusal names the entry and line");
    CHECK(zxapp_call(&A, &H, "file", 0, 1, out, 4, &n) == ZXAPP_E_ARG, "NULL args");
    CHECK(zxapp_call(&A, &H, "file", &arg, 1, 0, 4, &n) == ZXAPP_E_ARG, "NULL out");
    CHECK(zxapp_call(&A, &H, 0, &arg, 1, out, 4, &n) == ZXAPP_E_ARG, "NULL entry");
    CHECK(zxapp_call(&A, &H, "inl", 0, 0, out, 4, &n) == ZXAPP_OK && n == 1 && out[0].num == 7,
          "no args");
    unloads = 0;
    zxapp_unload(&A, &H);
    CHECK(unloads == 2 && !A.logic[0].loaded && !A.logic[1].loaded, "unload both");
    zxapp_unload(&A, &H);
    CHECK(unloads == 2, "unload is idempotent");
    CHECK(zxapp_call(&A, &H, "file", &arg, 1, out, 4, &n) == ZXAPP_E_NORUNTIME, "unloaded");

    CHECK(parse("zxapp 1\nid t\nlogic eve x missing\n") == ZXAPP_OK, "missing file");
    CHECK(zxapp_load(&A, &H) == ZXAPP_E_RUNTIME && strstr(A.err, "cannot read missing"),
          "unreadable path");
    CHECK(parse("zxapp 1\nid t\nlogic eve x <<\nbad\n.\n") == ZXAPP_OK, "bad source");
    CHECK(zxapp_load(&A, &H) == ZXAPP_E_RUNTIME && strstr(A.err, "x: line 1: no") &&
              A.err_line == 3,
          "a load refusal names the entry");

    zxapp_host_init(&H);
    for (int i = 0; i < ZXAPP_LANG_COUNT; i++) {
        zxapp_runtime_t r = {(zxapp_lang_t) i, 0, f_load, f_call, 0};
        CHECK(zxapp_host_register(&H, &r) == ZXAPP_OK, "every language");
    }
    CHECK(H.n == ZXAPP_LANG_COUNT && H.n <= ZXAPP_RUNTIMES, "one per language fits");
}

/* ---- the real KRON runtime -------------------------------------------------- */

static void test_kron(void)
{
    zxapp_host_init(&H);
    CHECK(zxapp_kron_register(&H) == ZXAPP_OK && zxapp_host_has(&H, ZXAPP_LANG_KRON),
          "kron registers");
    CHECK(parse("zxapp 1\nid m\n"
                "logic kron share <<\n_1000*re%1|+/re\n.\n"
                "logic kron busiest <<\n3#>re\n.\n"
                "logic kron half <<\nre%2\n.\n"
                "logic kron pos <<\nre>0\n.\n"
                "logic kron apl <<\n+/re\xC3\x97"
                "2\n.\n"
                "logic kron oops <<\n1 2 +\n.\n") == ZXAPP_OK,
          "kron manifest");
    CHECK(zxapp_load(&A, &H) == ZXAPP_E_RUNTIME && strstr(A.err, "oops: ") && A.err_line == 18,
          "a KRON syntax error names the entry and line");
    A.n--; /* drop the bad one */
    CHECK(zxapp_load(&A, &H) == 5, "five KRON programs load");
    int64_t re[4] = {3, 1, 4, 0};
    zxapp_arg_t a = {"re", re, 4};
    zxapp_num_t out[8];
    uint32_t n = 0;
    CHECK(zxapp_call(&A, &H, "share", &a, 1, out, 8, &n) == ZXAPP_OK && n == 4, "share");
    CHECK(out[0].num == 375 && out[1].num == 125 && out[2].num == 500 && out[3].num == 0 &&
              out[0].den == 1,
          "per mille, floored, exact");
    int64_t zero[2] = {0, 0};
    zxapp_arg_t z = {"re", zero, 2};
    CHECK(zxapp_call(&A, &H, "share", &z, 1, out, 8, &n) == ZXAPP_OK && n == 2 && out[0].num == 0 &&
              out[1].num == 0,
          "an idle cycle is all zero, not a division by zero");
    int64_t one[1] = {9};
    zxapp_arg_t o = {"re", one, 1};
    CHECK(zxapp_call(&A, &H, "share", &o, 1, out, 8, &n) == ZXAPP_OK && n == 1 &&
              out[0].num == 1000,
          "one agent has it all (a one-element vector, not a scalar)");
    CHECK(zxapp_call(&A, &H, "busiest", &a, 1, out, 8, &n) == ZXAPP_OK && n == 3 &&
              out[0].num == 2 && out[1].num == 0 && out[2].num == 1,
          "busiest: indexes, most first");
    CHECK(zxapp_call(&A, &H, "busiest", &o, 1, out, 8, &n) == ZXAPP_OK && n == 3 &&
              out[0].num == 0 && out[2].num == 0,
          "take repeats on a short vector (K semantics)");
    CHECK(zxapp_call(&A, &H, "half", &a, 1, out, 8, &n) == ZXAPP_OK && n == 4 && out[0].num == 3 &&
              out[0].den == 2 && out[2].num == 2 && out[2].den == 1,
          "exact rationals, reduced");
    CHECK(zxapp_call(&A, &H, "pos", &a, 1, out, 8, &n) == ZXAPP_OK && n == 4 && out[0].den == 0 &&
              out[3].den == 0 && out[0].num != out[3].num,
          "truth comes back as codes with den 0");
    CHECK(zxapp_call(&A, &H, "apl", &a, 1, out, 8, &n) == ZXAPP_OK && n == 1 && out[0].num == 16,
          "APL glyphs work too");
    CHECK(zxapp_call(&A, &H, "share", &a, 1, out, 3, &n) == ZXAPP_E_BUFFER && n == 4,
          "a short buffer");
    zxapp_arg_t wrong = {"rr", re, 4};
    CHECK(zxapp_call(&A, &H, "share", &wrong, 1, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "share: "),
          "an unbound name is refused, not ignored");
    int64_t big[2] = {INT64_MAX, INT64_MAX};
    zxapp_arg_t bg = {"re", big, 2};
    CHECK(zxapp_call(&A, &H, "share", &bg, 1, out, 8, &n) == ZXAPP_E_RUNTIME,
          "an overflow is refused, never rounded");
    zxapp_unload(&A, &H);
}

/* ---- the real Eve runtime, and one app in three languages ------------------- */

static void test_eve(void)
{
    zxapp_host_init(&H);
    CHECK(zxapp_eve_register(&H) == ZXAPP_OK && zxapp_kron_register(&H) == ZXAPP_OK && H.n == 2,
          "eve and kron register side by side");
    CHECK(parse("zxapp 1\nid wallet\nname Wallet\nview zui/views/wallet.c\n"
                "logic eve  fits <<\n"
                "eve wallet\nmoney = -1000 .. 1000\npay { amount: money 1 .. 500\n fee: money 0 .. "
                "9 }\n.\n"
                "logic kron total <<\n+/amount\n.\n"
                "logic sutra post apps/wallet/post.sutra\n"
                "logic eve  bad <<\neve x\nn = 0 .. 9\n.\n") == ZXAPP_OK &&
              A.n == 4,
          "a manifest mixing eve, kron and sutra");
    CHECK(zxapp_load(&A, &H) == ZXAPP_E_RUNTIME && strstr(A.err, "bad: ") &&
              strstr(A.err, "record"),
          "an Eve entry with no record to check is refused by name");
    A.n--;
    CHECK(zxapp_load(&A, &H) == 2 && A.logic[0].loaded && A.logic[1].loaded && !A.logic[2].loaded,
          "eve and kron load; sutra, with no runtime here, stays for the C model");
    int64_t amt[4] = {1, 500, 501, 0}, fee[1] = {9};
    zxapp_arg_t args[2] = {{"amount", amt, 4}, {"fee", fee, 1}};
    zxapp_num_t out[8];
    uint32_t n = 0;
    CHECK(zxapp_call(&A, &H, "fits", args, 2, out, 8, &n) == ZXAPP_OK && n == 4,
          "fits: one per row");
    CHECK(out[0].num == ZXAPP_TRUE && out[1].num == ZXAPP_TRUE && out[2].num == ZXAPP_FALSE &&
              out[3].num == ZXAPP_FALSE && out[0].den == 0,
          "EV2: inside the record's limits TRUE, outside FALSE (both ends)");
    int64_t fee2[1] = {10};
    args[1].v = fee2;
    CHECK(zxapp_call(&A, &H, "fits", args, 2, out, 8, &n) == ZXAPP_OK &&
              out[0].num == ZXAPP_FALSE && out[1].num == ZXAPP_FALSE,
          "a shared value outside its limits fails every row");
    args[1].v = fee;
    CHECK(zxapp_call(&A, &H, "fits", args, 1, out, 8, &n) == ZXAPP_OK && out[1].num == ZXAPP_TRUE,
          "EV3: a component no argument names keeps its initial value");
    CHECK(zxapp_call(&A, &H, "total", args, 1, out, 8, &n) == ZXAPP_OK && n == 1 &&
              out[0].num == 1002,
          "the same values through KRON");
    int64_t two[2] = {1, 2};
    zxapp_arg_t mism[2] = {{"amount", amt, 4}, {"fee", two, 2}};
    CHECK(zxapp_call(&A, &H, "fits", mism, 2, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "two lengths"),
          "vectors of two lengths are refused");
    zxapp_arg_t nope = {"tip", fee, 1};
    CHECK(zxapp_call(&A, &H, "fits", &nope, 1, out, 8, &n) == ZXAPP_E_RUNTIME,
          "a name the record does not have is refused");
    CHECK(zxapp_call(&A, &H, "fits", args, 2, out, 3, &n) == ZXAPP_E_BUFFER && n == 4,
          "a short buffer");
    CHECK(zxapp_call(&A, &H, "post", args, 2, out, 8, &n) == ZXAPP_E_NORUNTIME,
          "the sutra entry asks for the C model");
    zxapp_unload(&A, &H);
}

/* ---- WebAssembly: logic in any language that compiles to it ---------------- */

static wb MOD[4];

static const uint8_t *module(int k, const char *sig, const uint8_t *code, size_t len, uint32_t *n)
{
    wt t;
    wt_one(&t, code, len);
    t.types[0] = sig;
    t.exports[0] = "run";
    t.nexports = 1;
    wb_reset(&MOD[k]);
    wt_build(&MOD[k], &t);
    *n = (uint32_t) MOD[k].n;
    return MOD[k].b;
}

static int32_t read_mod(void *ctx, const char *path, char *buf, uint32_t cap)
{
    (void) ctx;
    int k = path[0] - '0';
    if (k < 0 || k > 3 || MOD[k].n > cap) return -1;
    memcpy(buf, MOD[k].b, MOD[k].n);
    return (int32_t) MOD[k].n;
}

static void test_wasm(void)
{
    /* run(a, b) = a * b + 1, as rustc, tinygo or clang would emit it */
    static const uint8_t mul1[] = {0x20, 0x00, 0x20, 0x01, 0x7E, 0x42, 0x01, 0x7C};
    static const uint8_t trap[] = {0x20, 0x00, 0x50, 0x04, 0x40, 0x00, 0x0B, 0x20, 0x00};
    static const uint8_t fl[] = {0x20, 0x00, 0xB9, 0xB0}; /* via f64: refused */
    static const uint8_t spin[] = {0x03, 0x40, 0x0C, 0x00, 0x0B, 0x42, 0x00};
    uint32_t n0, n1, n2, n3;
    module(0, "II:I", mul1, sizeof mul1, &n0);
    module(1, "I:I", trap, sizeof trap, &n1); /* traps on 0 (eqz, if, unreachable) */
    module(2, "I:I", fl, sizeof fl, &n2);
    module(3, ":I", spin, sizeof spin, &n3);
    static zxapp_wasm_t W;
    zxapp_host_init(&H);
    H.read_file = read_mod;
    CHECK(zxapp_wasm_register(&H, &W, 200000) == ZXAPP_OK && zxapp_host_has(&H, ZXAPP_LANG_WASM),
          "wasm registers");
    CHECK(zxapp_kron_register(&H) == ZXAPP_OK && zxapp_eve_register(&H) == ZXAPP_OK && H.n == 3,
          "beside kron and eve");
    CHECK(parse("zxapp 1\nid every\n"
                "logic wasm  price 0\n" /* from Rust, say */
                "logic wasm  guard 1\n" /* from Go */
                "logic kron  sum <<\n+/a\n.\n"
                "logic python report apps/every/report.py\n"
                "logic c#    ledger apps/every/Ledger.cs\n"
                "logic typescript chart apps/every/chart.ts\n"
                "logic rust  native apps/every/lib.rs\n") == ZXAPP_OK &&
              A.n == 7 && A.logic[4].lang == ZXAPP_LANG_DOTNET,
          "one manifest, seven entries, five languages: %s", A.err);
    CHECK(zxapp_load(&A, &H) == 3 && A.logic[0].loaded && A.logic[1].loaded && A.logic[2].loaded &&
              !A.logic[3].loaded && !A.logic[6].loaded,
          "wasm and kron load; languages with no runtime in this host wait for theirs");
    int64_t a[3] = {2, 3, -4}, b[1] = {10};
    zxapp_arg_t args[2] = {{"a", a, 3}, {"b", b, 1}};
    zxapp_num_t out[8];
    uint32_t n = 0;
    CHECK(zxapp_call(&A, &H, "price", args, 2, out, 8, &n) == ZXAPP_OK && n == 3 &&
              out[0].num == 21 && out[1].num == 31 && out[2].num == -39 && out[2].den == 1,
          "W2: run per row, a shared b");
    CHECK(zxapp_call(&A, &H, "sum", args, 1, out, 8, &n) == ZXAPP_OK && out[0].num == 1,
          "the same values through KRON");
    CHECK(zxapp_call(&A, &H, "guard", args, 1, out, 8, &n) == ZXAPP_OK && n == 3, "guard runs");
    int64_t z[2] = {0, 5};
    zxapp_arg_t za = {"a", z, 2};
    CHECK(zxapp_call(&A, &H, "guard", &za, 1, out, 8, &n) == ZXAPP_E_RUNTIME && A.err[0],
          "W3: a trap is a refusal: %s", A.err);
    za.n = 1;
    za.v = z + 1;
    CHECK(zxapp_call(&A, &H, "guard", &za, 1, out, 8, &n) == ZXAPP_OK && out[0].num == 5,
          "W3: and the next call reloads it");
    CHECK(zxapp_call(&A, &H, "report", args, 1, out, 8, &n) == ZXAPP_E_NORUNTIME,
          "python without its runtime asks for the C model");
    int64_t c[2] = {1, 2};
    zxapp_arg_t mism[2] = {{"a", a, 3}, {"b", c, 2}};
    CHECK(zxapp_call(&A, &H, "price", mism, 2, out, 8, &n) == ZXAPP_E_RUNTIME, "two lengths");
    CHECK(zxapp_call(&A, &H, "price", args, 2, out, 2, &n) == ZXAPP_E_BUFFER && n == 3,
          "a short buffer");
    zxapp_unload(&A, &H);

    CHECK(parse("zxapp 1\nid f\nlogic wasm f 2\n") == ZXAPP_OK &&
              zxapp_load(&A, &H) == ZXAPP_E_RUNTIME && strstr(A.err, "f: "),
          "R11: floating point is refused at load: %s", A.err);
    CHECK(parse("zxapp 1\nid s\nlogic wasm s 3\n") == ZXAPP_OK && zxapp_load(&A, &H) == 1,
          "a spinning module loads");
    CHECK(zxapp_call(&A, &H, "s", 0, 0, out, 8, &n) == ZXAPP_E_RUNTIME,
          "and runs out of fuel instead of hanging: %s", A.err);
    zxapp_unload(&A, &H);
    zxapp_wasm_close(&W);
}

/* ---- A5: one feature in two languages, joined by the paraconsistent logic -- */

static void test_witness(void)
{
    zxapp_num_t x[2] = {{1, 2}, {3, 1}}, y[2] = {{1, 2}, {3, 1}}, z[2] = {{1, 2}, {4, 1}};
    CHECK(zxapp_witness(x, 2, y, 2) == ZXAPP_TRUE, "same answers agree");
    CHECK(zxapp_witness(x, 2, z, 2) == ZXAPP_BOTH, "a different value is BOTH");
    CHECK(zxapp_witness(x, 2, y, 1) == ZXAPP_BOTH, "a different length is BOTH");
    zxapp_num_t t = {ZXAPP_TRUE, 0}, one = {1, 1};
    CHECK(zxapp_witness(&t, 1, &one, 1) == ZXAPP_BOTH, "a truth is not the number 1");

    /* run(a, b) = a * b + 1 in Wasm, the same in KRON, and a wrong one */
    static const uint8_t mul1[] = {0x20, 0x00, 0x20, 0x01, 0x7E, 0x42, 0x01, 0x7C};
    static const uint8_t mul2[] = {0x20, 0x00, 0x20, 0x01, 0x7E, 0x42, 0x02, 0x7C};
    uint32_t n0, n1;
    module(0, "II:I", mul1, sizeof mul1, &n0);
    module(1, "II:I", mul2, sizeof mul2, &n1);
    static zxapp_wasm_t W;
    zxapp_host_init(&H);
    H.read_file = read_mod;
    CHECK(zxapp_wasm_register(&H, &W, 0) == ZXAPP_OK && zxapp_kron_register(&H) == ZXAPP_OK,
          "runtimes");
    CHECK(parse("zxapp 1\nid w\n"
                "logic wasm  fee 0\n"
                "logic kron  fee <<\n1+a*b\n.\n"
                "logic wasm  bad 1\n"
                "logic kron  bad <<\n1+a*b\n.\n"
                "logic kron  solo <<\n1+a*b\n.\n"
                "logic python solo apps/w/solo.py\n"
                "logic kron  half <<\na%0\n.\n"
                "logic wasm  half 0\n") == ZXAPP_OK &&
              zxapp_load(&A, &H) == 7,
          "a witnessed manifest loads: %s", A.err);
    int64_t a[3] = {2, 3, -4}, b[3] = {10, 10, 10}; /* KRON does not stretch a 1-vector */
    zxapp_arg_t args[2] = {{"a", a, 3}, {"b", b, 3}};
    zxapp_num_t out[8];
    uint32_t n = 0;
    int32_t lp = -1;
    CHECK(zxapp_call(&A, &H, "fee", args, 2, out, 8, &n) == ZXAPP_OK && n == 3 && out[0].num == 21,
          "zxapp_call runs the first (Wasm)");
    CHECK(zxapp_call_witnessed(&A, &H, "fee", args, 2, out, 8, &n, &lp) == ZXAPP_OK &&
              lp == ZXAPP_TRUE && n == 3 && out[2].num == -39,
          "Wasm and KRON agree: TRUE");
    CHECK(zxapp_call_witnessed(&A, &H, "bad", args, 2, out, 8, &n, &lp) == ZXAPP_E_CONFLICT &&
              lp == ZXAPP_BOTH && n == 3 && out[0].num == 22,
          "they differ: BOTH, held, never picked silently");
    CHECK(zxapp_call_witnessed(&A, &H, "solo", args, 2, out, 8, &n, &lp) == ZXAPP_OK &&
              lp == ZXAPP_NEITHER && out[0].num == 21,
          "one language could run: NEITHER, unwitnessed");
    CHECK(zxapp_call_witnessed(&A, &H, "half", args, 2, out, 8, &n, &lp) == ZXAPP_E_CONFLICT &&
              lp == ZXAPP_BOTH && out[0].num == 21,
          "one refuses (KRON divides by zero), one answers: BOTH");
    CHECK(zxapp_call_witnessed(&A, &H, "nope", args, 2, out, 8, &n, &lp) == ZXAPP_E_NAME,
          "no such entry");
    CHECK(zxapp_call_witnessed(&A, &H, "fee", args, 2, out, 8, &n, 0) == ZXAPP_E_ARG,
          "the join must be read");
    CHECK(zxapp_call_witnessed(&A, &H, "fee", args, 2, out, 2, &n, &lp) == ZXAPP_E_BUFFER && n == 3,
          "a short buffer");
    zxapp_unload(&A, &H);
    CHECK(parse("zxapp 1\nid r\nlogic kron z <<\na%0\n.\nlogic kron y <<\n1\n.\n") == ZXAPP_OK &&
              zxapp_load(&A, &H) == 2 &&
              zxapp_call_witnessed(&A, &H, "z", args, 1, out, 8, &n, &lp) == ZXAPP_E_RUNTIME,
          "every witness refused: a refusal, not a conflict");
    zxapp_unload(&A, &H);
    zxapp_wasm_close(&W);
}

/* ---- B: blocks, the registry, composing and contracts ---------------------- */

static zxapp_t B1, B2, B3, APPB;
static zxapp_registry_t R;

static int parse_into(zxapp_t *a, const char *t)
{
    return zxapp_parse(a, t, (uint32_t) strlen(t));
}

static const char SHARES_100[] = "zxblock 1\nid shares\nversion 1.0.0\n"
                                 "provides share num re\n"
                                 "logic kron share <<\n_1000*re%1|+/re\n.\n"
                                 "provides pos truth re\n"
                                 "logic kron pos <<\nre>0\n.\n";
static const char SHARES_120[] = "zxblock 1\nid shares\nversion 1.2.0\n"
                                 "provides share num re\n"
                                 "logic kron share <<\n_1000*re%1|+/re\n.\n"
                                 "provides pos truth re\n"
                                 "logic kron pos <<\nre>0\n.\n";
static const char SHARES_200[] = "zxblock 1\nid shares\nversion 2.0.0\n"
                                 "provides share num re\nlogic kron share <<\n+/re\n.\n";

static void test_block_parse(void)
{
    CHECK(parse(SHARES_100) == ZXAPP_OK && A.block, "a block parses");
    CHECK(A.ver[0] == 1 && A.ver[1] == 0 && A.ver[2] == 0, "its version");
    CHECK(A.nprov == 2 && strcmp(A.prov[0].entry, "share") == 0 &&
              A.prov[0].kind == ZXAPP_KIND_NUM && A.prov[0].ninputs == 1 &&
              strcmp(A.prov[0].input[0], "re") == 0 && A.prov[0].line == 4,
          "provides share num re");
    CHECK(A.prov[1].kind == ZXAPP_KIND_TRUTH, "provides a truth");
    CHECK(parse("zxapp 1\nid x\n") == ZXAPP_OK && !A.block, "an app is not a block");
    CHECK(parse("zxblock 2\nid x\n") == ZXAPP_E_SYNTAX && strstr(A.err, "zxblock 1"),
          "block version 2 refused: %s", A.err);
    CHECK(parse("zxapp 1\nid x\nversion 1.2.3\n") == ZXAPP_OK && A.ver[0] == 1 && A.ver[1] == 2 &&
              A.ver[2] == 3,
          "an app may carry a version");
    CHECK(parse("zxblock 1\nid x\nversion 2\n") == ZXAPP_OK && A.ver[0] == 2 && A.ver[1] == 0,
          "missing parts are 0");
    CHECK(parse("zxblock 1\nid x\nversion 65535.0.1\n") == ZXAPP_OK && A.ver[0] == 65535,
          "largest part");
    const char *badv[] = {"0.0.0", "1.",  ".1", "1..2", "1.2.3.4",
                          "65536", "1.x", "=1", "-1",   "1 2"};
    for (unsigned i = 0; i < sizeof badv / sizeof badv[0]; i++) {
        char m[96];
        snprintf(m, sizeof m, "zxblock 1\nid x\nversion %s\n", badv[i]);
        CHECK(parse(m) == ZXAPP_E_SYNTAX && A.err_line == 3, "bad version %s", badv[i]);
    }
    CHECK(parse("zxblock 1\nid x\nprovides f num a b c d e f g h\n") == ZXAPP_OK &&
              A.prov[0].ninputs == 8 && strcmp(A.prov[0].input[7], "h") == 0,
          "eight inputs");
    CHECK(parse("zxblock 1\nid x\nprovides f num a b c d e f g h i\n") == ZXAPP_E_SYNTAX,
          "nine inputs");
    CHECK(parse("zxblock 1\nid x\nprovides f num  a\t b  # the rest\n") == ZXAPP_OK &&
              A.prov[0].ninputs == 2 && strcmp(A.prov[0].input[1], "b") == 0,
          "inputs split on spaces and tabs, comment dropped");
    CHECK(parse("zxblock 1\nid x\nprovides f truth\n") == ZXAPP_OK && A.prov[0].ninputs == 0,
          "no inputs");
    CHECK(parse("zxblock 1\nid x\nprovides f\n") == ZXAPP_E_SYNTAX, "provides needs a kind");
    CHECK(parse("zxblock 1\nid x\nprovides f real a\n") == ZXAPP_E_SYNTAX &&
              strstr(A.err, "num or truth"),
          "num or truth only: %s", A.err);
    CHECK(parse("zxblock 1\nid x\nprovides 1f num\n") == ZXAPP_E_SYNTAX, "entry identifier");
    CHECK(parse("zxblock 1\nid x\nprovides f num a 2b\n") == ZXAPP_E_SYNTAX, "input identifier");
    CHECK(parse("zxblock 1\nid x\nprovides f num a a\n") == ZXAPP_E_SYNTAX &&
              strstr(A.err, "twice"),
          "an input twice");
    CHECK(parse("zxblock 1\nid x\nprovides f num\nprovides f truth\n") == ZXAPP_E_SYNTAX &&
              A.err_line == 4,
          "provides twice");
    CHECK(parse("zxapp 1\nid x\nuse shares 1\nuse other =2.1.3\n") == ZXAPP_OK && A.nuse == 2 &&
              strcmp(A.use[0].id, "shares") == 0 && A.use[0].ver[0] == 1 && !A.use[0].exact &&
              A.use[1].exact && A.use[1].ver[1] == 1 && A.use[1].ver[2] == 3 && A.use[1].line == 4,
          "use");
    CHECK(parse("zxapp 1\nid x\nuse shares\n") == ZXAPP_E_SYNTAX, "use needs a version");
    CHECK(parse("zxapp 1\nid x\nuse shares 1 2\n") == ZXAPP_E_SYNTAX, "one version");
    CHECK(parse("zxapp 1\nid x\nuse shares ==1\n") == ZXAPP_E_SYNTAX, "one =");
    CHECK(parse("zxapp 1\nid x\nuse 9s 1\n") == ZXAPP_E_SYNTAX, "block identifier");
    CHECK(parse("zxapp 1\nid x\nuse s 1\nuse s 2\n") == ZXAPP_E_SYNTAX && strstr(A.err, "twice"),
          "a block used twice");
    CHECK(parse("zxblock 1\nid x\nuse s 1\n") == ZXAPP_E_SYNTAX && strstr(A.err, "cannot use"),
          "blocks do not use blocks");
    char big[1024];
    int n = sprintf(big, "zxapp 1\nid x\n");
    for (unsigned i = 0; i < ZXAPP_USES; i++) n += sprintf(big + n, "use b%u 1\n", i);
    CHECK(parse(big) == ZXAPP_OK && A.nuse == ZXAPP_USES, "the most uses");
    sprintf(big + n, "use last 1\n");
    CHECK(parse(big) == ZXAPP_E_FULL, "one too many uses");
    CHECK(parse("zxapp 1\nid x\nflavour mint\n") == ZXAPP_E_SYNTAX &&
              strstr(A.err, "provides, use"),
          "the error lists the directives: %s", A.err);
}

static void test_registry(void)
{
    zxapp_registry_init(&R);
    CHECK(parse_into(&B1, SHARES_100) == ZXAPP_OK && parse_into(&B2, SHARES_120) == ZXAPP_OK &&
              parse_into(&B3, SHARES_200) == ZXAPP_OK,
          "three versions parse");
    CHECK(zxapp_registry_add(&R, &B1) == ZXAPP_OK && zxapp_registry_add(&R, &B3) == ZXAPP_OK &&
              zxapp_registry_add(&R, &B2) == ZXAPP_OK && R.n == 3,
          "registered in any order");
    CHECK(zxapp_registry_add(&R, &B2) == ZXAPP_E_NAME && strstr(R.err, "already"),
          "the same version twice");
    static zxapp_t X;
    CHECK(parse_into(&X, "zxapp 1\nid app\nversion 1\n") == ZXAPP_OK &&
              zxapp_registry_add(&R, &X) == ZXAPP_E_CONTRACT && strstr(R.err, "not a block"),
          "an app is not a block");
    CHECK(zxapp_registry_add(&R, 0) == ZXAPP_E_CONTRACT, "NULL");
    CHECK(parse_into(&X, "zxblock 1\nid nov\nprovides f num\nlogic kron f <<\n1\n.\n") ==
                  ZXAPP_OK &&
              zxapp_registry_add(&R, &X) == ZXAPP_E_CONTRACT && strstr(R.err, "version"),
          "a block needs a version");
    CHECK(parse_into(&X, "zxblock 1\nid b\nversion 1\nprovides f num\nprovides g num\n"
                         "logic kron f <<\n1\n.\n") == ZXAPP_OK &&
              zxapp_registry_add(&R, &X) == ZXAPP_E_CONTRACT && strstr(R.err, ": g"),
          "a promise with no logic: %s", R.err);
    CHECK(parse_into(&X, "zxblock 1\nid b\nversion 1\nprovides f num\n"
                         "logic kron f <<\n1\n.\nlogic kron g <<\n2\n.\n") == ZXAPP_OK &&
              zxapp_registry_add(&R, &X) == ZXAPP_E_CONTRACT && strstr(R.err, ": g"),
          "logic with no promise: %s", R.err);
    CHECK(R.n == 3, "refusals add nothing");
    uint16_t v1[3] = {1, 0, 0}, v11[3] = {1, 1, 0}, v12[3] = {1, 2, 0}, v13[3] = {1, 3, 0},
             v2[3] = {2, 0, 0}, v3[3] = {3, 0, 0}, v101[3] = {1, 0, 1};
    CHECK(zxapp_registry_find(&R, "shares", v1, false) == &B2, "the newest 1.x");
    CHECK(zxapp_registry_find(&R, "shares", v11, false) == &B2, "1.1 takes 1.2");
    CHECK(zxapp_registry_find(&R, "shares", v12, false) == &B2, "1.2 itself");
    CHECK(zxapp_registry_find(&R, "shares", v13, false) == 0, "nothing as new as 1.3");
    CHECK(zxapp_registry_find(&R, "shares", v101, false) == &B2, "1.0.1 takes 1.2.0");
    CHECK(zxapp_registry_find(&R, "shares", v2, false) == &B3, "major 2 is its own line");
    CHECK(zxapp_registry_find(&R, "shares", v3, false) == 0, "no major 3");
    CHECK(zxapp_registry_find(&R, "shares", v1, true) == &B1, "=1.0.0 exactly");
    CHECK(zxapp_registry_find(&R, "shares", v11, true) == 0, "=1.1.0 is not there");
    CHECK(zxapp_registry_find(&R, "share", v1, false) == 0, "ids are exact");
    CHECK(zxapp_registry_find(&R, 0, v1, false) == 0 &&
              zxapp_registry_find(0, "shares", v1, false) == 0,
          "NULLs");
    zxapp_registry_t full;
    zxapp_registry_init(&full);
    static zxapp_t many[ZXAPP_BLOCKS + 1];
    static char src[ZXAPP_BLOCKS + 1][64];
    int rc = ZXAPP_OK;
    for (unsigned i = 0; i <= ZXAPP_BLOCKS; i++) {
        snprintf(src[i], sizeof src[i], "zxblock 1\nid b\nversion 1.%u\n", i);
        parse_into(&many[i], src[i]);
        rc = zxapp_registry_add(&full, &many[i]);
        if (i < ZXAPP_BLOCKS && rc != ZXAPP_OK) break;
    }
    CHECK(rc == ZXAPP_E_FULL && full.n == ZXAPP_BLOCKS, "the registry fills (%d)", rc);
}

static void test_compose(void)
{
    zxapp_host_init(&H);
    CHECK(zxapp_kron_register(&H) == ZXAPP_OK, "kron");
    CHECK(parse_into(&APPB, "zxapp 1\nid app\nlogic kron own <<\n+/re\n.\nuse shares 1.1\n") ==
              ZXAPP_OK,
          "an app that uses a block");
    CHECK(zxapp_compose(&APPB, &R) == ZXAPP_OK && APPB.n == 3 && APPB.composed == 1, "composed: %s",
          APPB.err);
    CHECK(strcmp(APPB.logic[1].entry, "shares.share") == 0 &&
              strcmp(APPB.logic[2].entry, "shares.pos") == 0,
          "entries are block.entry");
    CHECK(APPB.logic[1].contract == &B2.prov[0] && APPB.logic[2].contract == &B2.prov[1],
          "the newest compatible block's contracts (1.2)");
    CHECK(APPB.logic[0].contract == 0, "the app's own entry has none");
    CHECK(zxapp_load(&APPB, &H) == 3, "all three load: %s", APPB.err);
    CHECK(B2.logic[0].compiled == 0 && !B2.logic[0].loaded, "the block itself stays unloaded");
    int64_t re[3] = {1, 1, 2}, neg[3] = {1, 0, 2};
    zxapp_arg_t a = {"re", re, 3}, an = {"re", neg, 3};
    zxapp_num_t out[4];
    uint32_t n = 0;
    CHECK(zxapp_call(&APPB, &H, "shares.share", &a, 1, out, 4, &n) == ZXAPP_OK && n == 3 &&
              out[0].num == 250 && out[2].num == 500 && out[2].den == 1,
          "a block's entry runs in the app");
    CHECK(zxapp_call(&APPB, &H, "shares.pos", &an, 1, out, 4, &n) == ZXAPP_OK && n == 3 &&
              out[1].num == ZXAPP_FALSE && out[1].den == 0,
          "and answers truths");
    CHECK(zxapp_call(&APPB, &H, "share", &a, 1, out, 4, &n) == ZXAPP_E_NAME,
          "only by its composed name");
    zxapp_arg_t wrong = {"t", re, 3};
    CHECK(zxapp_call(&APPB, &H, "shares.share", &wrong, 1, out, 4, &n) == ZXAPP_E_CONTRACT &&
              strstr(APPB.err, "expected re"),
          "B3: a misnamed input: %s", APPB.err);
    zxapp_arg_t two[2] = {{"re", re, 3}, {"x", re, 3}};
    CHECK(zxapp_call(&APPB, &H, "shares.share", two, 2, out, 4, &n) == ZXAPP_E_CONTRACT &&
              strstr(APPB.err, "number of inputs"),
          "B3: too many inputs");
    zxapp_arg_t noname = {0, re, 3};
    CHECK(zxapp_call(&APPB, &H, "shares.share", &noname, 1, out, 4, &n) == ZXAPP_E_CONTRACT,
          "B3: an unnamed input");
    int32_t lp = -1;
    CHECK(zxapp_call_witnessed(&APPB, &H, "shares.share", &wrong, 1, out, 4, &n, &lp) ==
                  ZXAPP_E_CONTRACT &&
              lp == ZXAPP_NEITHER,
          "B3 before any language runs, witnessed too");
    CHECK(zxapp_call(&APPB, &H, "own", &a, 1, out, 4, &n) == ZXAPP_OK && out[0].num == 4,
          "no contract, no check");

    /* composing again replaces the composed entries, after an unload */
    CHECK(zxapp_compose(&APPB, &R) == ZXAPP_E_ARG && strstr(APPB.err, "unload"),
          "not while loaded");
    zxapp_unload(&APPB, &H);
    APPB.use[0].exact = true;
    APPB.use[0].ver[1] = 0;
    CHECK(zxapp_compose(&APPB, &R) == ZXAPP_OK && APPB.n == 3 &&
              APPB.logic[1].contract == &B1.prov[0],
          "recomposed against =1.0.0");
    APPB.use[0].ver[1] = 1;
    CHECK(zxapp_compose(&APPB, &R) == ZXAPP_E_NAME &&
              strstr(APPB.err, "shares at version =1.1.0") && APPB.n == 1 && APPB.err_line == 6,
          "a missing block is named with its version: %s", APPB.err);
    CHECK(zxapp_compose(&B1, &R) == ZXAPP_E_ARG, "a block is not composed");
    CHECK(zxapp_compose(0, &R) == ZXAPP_E_ARG && zxapp_compose(&APPB, 0) == ZXAPP_E_ARG, "NULLs");

    /* B3: an answer of the wrong kind is refused, and a witness counts it */
    static zxapp_t BADB, APP2;
    static zxapp_registry_t R2;
    zxapp_registry_init(&R2);
    CHECK(parse_into(&BADB, "zxblock 1\nid lim\nversion 1\n"
                            "provides ok truth re\nlogic kron ok <<\nre\n.\nlogic eve ok <<\n.\n"
                            "provides n num re\nlogic kron n <<\nre>0\n.\n") == ZXAPP_OK &&
              zxapp_registry_add(&R2, &BADB) == ZXAPP_OK,
          "a block whose answers break its contract: %s", R2.err);
    CHECK(parse_into(&APP2, "zxapp 1\nid a\nuse lim 1\n") == ZXAPP_OK &&
              zxapp_compose(&APP2, &R2) == ZXAPP_OK && zxapp_load(&APP2, &H) == 2,
          "composes; kron loads, eve has no runtime here: %s", APP2.err);
    CHECK(zxapp_call(&APP2, &H, "lim.ok", &a, 1, out, 4, &n) == ZXAPP_E_CONTRACT &&
              strstr(APP2.err, "provides a truth"),
          "a number where a truth was promised: %s", APP2.err);
    CHECK(zxapp_call(&APP2, &H, "lim.n", &a, 1, out, 4, &n) == ZXAPP_E_CONTRACT &&
              strstr(APP2.err, "provides a number"),
          "a truth where a number was promised: %s", APP2.err);
    CHECK(zxapp_call_witnessed(&APP2, &H, "lim.ok", &a, 1, out, 4, &n, &lp) == ZXAPP_E_RUNTIME,
          "the only witness broke the contract: a refusal");
    zxapp_unload(&APP2, &H);
    zxapp_unload(&APPB, &H);

    /* the longest names still fit once composed */
    static zxapp_t LB, LA;
    static zxapp_registry_t R3;
    char lb[512], la[256], id[ZXAPP_ID_MAX], en[ZXAPP_ID_MAX];
    memset(id, 'b', sizeof id - 1);
    id[sizeof id - 1] = 0;
    memset(en, 'e', sizeof en - 1);
    en[sizeof en - 1] = 0;
    snprintf(lb, sizeof lb,
             "zxblock 1\nid %s\nversion 1\nprovides %s num\nlogic kron %s <<\n7\n.\n", id, en, en);
    snprintf(la, sizeof la, "zxapp 1\nid a\nuse %s 1\n", id);
    zxapp_registry_init(&R3);
    CHECK(parse_into(&LB, lb) == ZXAPP_OK && zxapp_registry_add(&R3, &LB) == ZXAPP_OK &&
              parse_into(&LA, la) == ZXAPP_OK && zxapp_compose(&LA, &R3) == ZXAPP_OK &&
              strlen(LA.logic[0].entry) == 2u * (ZXAPP_ID_MAX - 1u) + 1u,
          "a 63-byte composed name: %s", LA.err);
    CHECK(zxapp_load(&LA, &H) == 1 &&
              zxapp_call(&LA, &H, LA.logic[0].entry, 0, 0, out, 4, &n) == ZXAPP_OK &&
              out[0].num == 7,
          "and runs");
    zxapp_unload(&LA, &H);
}

static uint32_t prog[ZXAPP_LOGIC_MAX + 2], nprog;
static void on_progress(void *ctx, uint32_t d)
{
    (void) ctx;
    if (nprog < ZXAPP_LOGIC_MAX + 2) prog[nprog++] = d;
}

static void test_launch(void)
{
    CHECK(parse("zxapp 1\nid x\nlaunch murmur_flock\n") == ZXAPP_OK &&
              strcmp(A.launch, "murmur_flock") == 0,
          "launch theme");
    CHECK(parse("zxapp 1\nid x\n") == ZXAPP_OK && A.launch[0] == 0, "no theme: default");
    CHECK(parse("zxapp 1\nid x\nlaunch two words\n") == ZXAPP_E_SYNTAX, "one theme id");
    CHECK(parse("zxapp 1\nid x\nlaunch 9a\n") == ZXAPP_E_SYNTAX, "an identifier");
    zxapp_host_init(&H);
    CHECK(H.progress == 0, "no progress hook by default");
    H.progress = on_progress;
    CHECK(zxapp_kron_register(&H) == ZXAPP_OK, "kron");
    CHECK(parse("zxapp 1\nid x\nlogic kron a <<\n1\n.\nlogic eve b <<\n.\n"
                "logic kron c <<\n2\n.\nlogic kron d <<\n3\n.\n") == ZXAPP_OK,
          "four entries");
    nprog = 0;
    CHECK(zxapp_load(&A, &H) == 3, "three load");
    CHECK(nprog == 5 && prog[0] == 0 && prog[1] == 64 && prog[2] == 128 && prog[3] == 192 &&
              prog[4] == 256,
          "progress 0, 64, 128, 192, 256 (%u reports)", nprog);
    zxapp_unload(&A, &H);
    CHECK(parse("zxapp 1\nid x\n") == ZXAPP_OK, "no entries");
    nprog = 0;
    CHECK(zxapp_load(&A, &H) == 0 && nprog == 2 && prog[0] == 256 && prog[1] == 256,
          "nothing to load is done");
}

/* ---- C: apps as blocks, provenance, content ids ----------------------------- */

static void test_apps_as_blocks(void)
{
    char cid[ZXAPP_CID_MAX];
    zxapp_cid("abc", 3, cid);
    CHECK(strcmp(cid, "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") ==
              0,
          "C3: the content id is SHA-256: %s", cid);
    zxapp_cid(SHARES_100, (uint32_t) strlen(SHARES_100), cid);
    CHECK(strcmp(B1.cid, cid) == 0 && strcmp(B1.cid, B2.cid) != 0, "parse records it");
    CHECK(parse("zxapp 1\nid x\nauthor Ana Lima  # a comment\n") == ZXAPP_OK &&
              strcmp(A.author, "Ana Lima") == 0,
          "author keeps its spaces: '%s'", A.author);
    CHECK(parse("zxapp 1\nid x\nprovides a.b.c num t\n") == ZXAPP_OK &&
              strcmp(A.prov[0].entry, "a.b.c") == 0,
          "a re-export names a composed entry");
    CHECK(parse("zxapp 1\nid x\nprovides a..b num\n") == ZXAPP_E_SYNTAX &&
              parse("zxapp 1\nid x\nprovides .a num\n") == ZXAPP_E_SYNTAX &&
              parse("zxapp 1\nid x\nprovides a. num\n") == ZXAPP_E_SYNTAX,
          "dots join identifiers");
    char m[256];
    snprintf(m, sizeof m, "zxapp 1\nid x\nuse shares 1 %.71s\n", B1.cid);
    CHECK(parse(m) == ZXAPP_OK && strcmp(A.use[0].cid, B1.cid) == 0, "use pins a content id");
    CHECK(parse("zxapp 1\nid x\nuse shares 1 sha256:abc\n") == ZXAPP_E_SYNTAX &&
              strstr(A.err, "64 lowercase hex"),
          "a short content id");
    snprintf(m, sizeof m, "zxapp 1\nid x\nuse shares 1 %.71s\n", B1.cid + 1);
    CHECK(parse(m) == ZXAPP_E_SYNTAX, "no sha256: prefix");

    /* C1: an app made of a block, then an app made of that app */
    static zxapp_t DASH, STUDIO, WRONG, PRIV;
    static const char DASH_SRC[] = "zxapp 1\nid dashboard\nauthor Ana\nversion 1.0.0\n"
                                   "use shares 1\n"
                                   "logic kron total <<\n+/re\n.\n"
                                   "logic kron secret <<\n0\n.\n"
                                   "provides total num re\n"
                                   "provides shares.share num re\n";
    zxapp_host_init(&H);
    CHECK(zxapp_kron_register(&H) == ZXAPP_OK, "kron");
    CHECK(parse_into(&DASH, DASH_SRC) == ZXAPP_OK && !DASH.block, "an app that exports");
    CHECK(zxapp_registry_add(&R, &DASH) == ZXAPP_E_CONTRACT && strstr(R.err, "compose it"),
          "not before it is composed: %s", R.err);
    CHECK(zxapp_compose(&DASH, &R) == ZXAPP_OK && DASH.resolved && DASH.n == 4, "composed: %s",
          DASH.err);
    CHECK(zxapp_registry_add(&R, &DASH) == ZXAPP_OK, "then it registers like a block: %s", R.err);
    CHECK(zxapp_registry_add(&R, &DASH) == ZXAPP_E_NAME, "once");
    CHECK(parse_into(&STUDIO, "zxapp 1\nid studio\nauthor Ben\nuse dashboard 1\n") == ZXAPP_OK &&
              zxapp_compose(&STUDIO, &R) == ZXAPP_OK && STUDIO.n == 2,
          "an app made of an app: %s", STUDIO.err);
    CHECK(zxapp_find(&STUDIO, "dashboard.total") && zxapp_find(&STUDIO, "dashboard.shares.share") &&
              !zxapp_find(&STUDIO, "dashboard.secret") &&
              !zxapp_find(&STUDIO, "dashboard.shares.pos"),
          "only what it exports, nested by name");
    CHECK(zxapp_load(&STUDIO, &H) == 2, "both load: %s", STUDIO.err);
    int64_t re[4] = {1, 1, 1, 1};
    zxapp_arg_t a = {"re", re, 4}, wrong = {"t", re, 4};
    zxapp_num_t out[4];
    uint32_t n = 0;
    CHECK(zxapp_call(&STUDIO, &H, "dashboard.shares.share", &a, 1, out, 4, &n) == ZXAPP_OK &&
              n == 4 && out[0].num == 250,
          "a block's entry, two apps deep, runs");
    CHECK(zxapp_call(&STUDIO, &H, "dashboard.total", &a, 1, out, 4, &n) == ZXAPP_OK &&
              out[0].num == 4,
          "the middle app's own entry runs");
    CHECK(zxapp_call(&STUDIO, &H, "dashboard.total", &wrong, 1, out, 4, &n) == ZXAPP_E_CONTRACT,
          "its contract is checked");

    /* C2: who made which part */
    zxapp_origin_t o[4];
    uint32_t k = zxapp_provenance(&STUDIO, o, 4);
    CHECK(k == 3 && !o[0].from && strcmp(o[0].id, "studio") == 0 && o[0].entries == 0 &&
              strcmp(o[0].author, "Ben") == 0,
          "the app's own part first, empty here (%u)", k);
    CHECK(o[1].from == &DASH && strcmp(o[1].author, "Ana") == 0 && o[1].entries == 1 &&
              o[1].permille == 500 && strcmp(o[1].cid, DASH.cid) == 0,
          "the dashboard wrote one of two entries");
    CHECK(o[2].from == &B2 && o[2].entries == 1 && o[2].permille == 500 && o[2].ver[1] == 2,
          "the shares block, through the dashboard, wrote the other");
    CHECK(zxapp_provenance(&STUDIO, o, 1) == 3 && !o[0].from, "cap limits what is filled");
    CHECK(zxapp_provenance(&DASH, o, 4) == 2 && o[0].entries == 2 && o[0].permille == 500 &&
              o[1].from == &B2 && o[1].entries == 2,
          "the dashboard: two of its own, two from shares");
    CHECK(zxapp_provenance(0, o, 4) == 0, "NULL");
    zxapp_unload(&STUDIO, &H);

    /* a re-export keeps the contract it was given */
    CHECK(parse_into(&WRONG, "zxapp 1\nid w\nversion 1\nuse shares 1\n"
                             "provides shares.share truth re\n") == ZXAPP_OK &&
              zxapp_compose(&WRONG, &R) == ZXAPP_OK &&
              zxapp_registry_add(&R, &WRONG) == ZXAPP_E_CONTRACT && strstr(R.err, "different"),
          "a re-export cannot change the promise: %s", R.err);
    CHECK(parse_into(&PRIV, "zxapp 1\nid p\nversion 1\nlogic kron a <<\n1\n.\n") == ZXAPP_OK &&
              zxapp_registry_add(&R, &PRIV) == ZXAPP_E_CONTRACT && strstr(R.err, "exports nothing"),
          "an app with nothing to export is not a block");

    /* C3: a pin picks that exact content */
    static zxapp_t PIN, FILEB;
    snprintf(m, sizeof m, "zxapp 1\nid pin\nuse shares 1 %.71s\n", B1.cid);
    CHECK(parse_into(&PIN, m) == ZXAPP_OK && zxapp_compose(&PIN, &R) == ZXAPP_OK &&
              PIN.logic[0].contract == &B1.prov[0],
          "the pinned 1.0.0, not the newer 1.2.0: %s", PIN.err);
    snprintf(m, sizeof m, "zxapp 1\nid pin\nuse shares 1 %.71s\n", B3.cid);
    CHECK(parse_into(&PIN, m) == ZXAPP_OK && zxapp_compose(&PIN, &R) == ZXAPP_E_NAME &&
              strstr(PIN.err, "with that content id"),
          "content that is not 1.x is refused: %s", PIN.err);
    snprintf(m, sizeof m, "zxapp 1\nid pin\nuse shares 2 %.71s\n", B3.cid);
    CHECK(parse_into(&PIN, m) == ZXAPP_OK && zxapp_compose(&PIN, &R) == ZXAPP_OK,
          "the same content at its own version");
    static const char FILE_SRC[] = "zxblock 1\nid onfile\nversion 1\nprovides f num\n"
                                   "logic kron f apps/f.kron\n";
    static zxapp_registry_t R4;
    zxapp_registry_init(&R4);
    CHECK(parse_into(&FILEB, FILE_SRC) == ZXAPP_OK && zxapp_registry_add(&R4, &FILEB) == ZXAPP_OK,
          "a block with its code in a file");
    snprintf(m, sizeof m, "zxapp 1\nid pin\nuse onfile 1 %.71s\n", FILEB.cid);
    CHECK(parse_into(&PIN, m) == ZXAPP_OK && zxapp_compose(&PIN, &R4) == ZXAPP_E_CONTRACT &&
              strstr(PIN.err, "not all in its manifest"),
          "cannot be pinned: the id would not cover the code: %s", PIN.err);
    snprintf(m, sizeof m, "zxapp 1\nid pin\nuse onfile 1\n");
    CHECK(parse_into(&PIN, m) == ZXAPP_OK && zxapp_compose(&PIN, &R4) == ZXAPP_OK,
          "but can be used unpinned");
    /* an app that uses something unpinned cannot be pinned either */
    snprintf(m, sizeof m, "zxapp 1\nid pin\nuse dashboard 1 %.71s\n", DASH.cid);
    CHECK(parse_into(&PIN, m) == ZXAPP_OK && zxapp_compose(&PIN, &R) == ZXAPP_E_CONTRACT,
          "the dashboard's own use is not pinned");
}

/* ---- I: intents ------------------------------------------------------------- */

static zxapp_t NOTES, PAINT, VIEWER, MAILER, ANY, EXP;

static void test_intents(void)
{
    /* I1: parsing */
    CHECK(parse("zxapp 1\nid notes\nlogic kron add <<\n1\n.\n"
                "intent share text/plain add\nintent Open TEXT/Markdown add  # a comment\n"
                "intent edit text/* add\n") == ZXAPP_OK,
          "three intents parse: %s", A.err);
    CHECK(A.nintent == 3, "three intents");
    CHECK(strcmp(A.intent[0].verb, "share") == 0 && strcmp(A.intent[0].type, "text/plain") == 0 &&
              strcmp(A.intent[0].entry, "add") == 0 && A.intent[0].line == 6,
          "the first, with its line (%u)", A.intent[0].line);
    CHECK(strcmp(A.intent[1].verb, "open") == 0 && strcmp(A.intent[1].type, "text/markdown") == 0,
          "verbs and types are kept in lower case");
    CHECK(strcmp(A.intent[2].type, "text/*") == 0, "a pattern");
    CHECK(parse("zxapp 1\nid x\nintent pick */* notes.pick\n") == ZXAPP_OK &&
              strcmp(A.intent[0].entry, "notes.pick") == 0,
          "anything, through a composed entry");
    CHECK(parse("zxapp 1\nid x\nintent share application/vnd.zxv+json-1.x f\n") == ZXAPP_OK,
          "a type with + . - in it");
    CHECK(parse("zxapp 1\nid x\n") == ZXAPP_OK && A.nintent == 0, "no intents");

    static const struct {
        const char *m, *why;
        int rc;
    } bad[] = {
        {"zxapp 1\nid x\nintent share text/plain\n", "intent needs", ZXAPP_E_SYNTAX},
        {"zxapp 1\nid x\nintent share\n", "intent needs", ZXAPP_E_SYNTAX},
        {"zxapp 1\nid x\nintent share text/plain f g\n", "an entry name", ZXAPP_E_SYNTAX},
        {"zxapp 1\nid x\nintent 9share text/plain f\n", "a verb", ZXAPP_E_SYNTAX},
        {"zxapp 1\nid x\nintent share-it text/plain f\n", "a verb", ZXAPP_E_SYNTAX},
        {"zxapp 1\nid x\nintent shareshareshareshare text/plain f\n", "a verb", ZXAPP_E_SYNTAX},
        {"zxapp 1\nid x\nintent share text f\n", "a type", ZXAPP_E_SYNTAX},
        {"zxapp 1\nid x\nintent share text/ f\n", "a type", ZXAPP_E_SYNTAX},
        {"zxapp 1\nid x\nintent share /plain f\n", "a type", ZXAPP_E_SYNTAX},
        {"zxapp 1\nid x\nintent share */plain f\n", "a type", ZXAPP_E_SYNTAX},
        {"zxapp 1\nid x\nintent share text/pl*in f\n", "a type", ZXAPP_E_SYNTAX},
        {"zxapp 1\nid x\nintent share text/plain;charset=utf-8 f\n", "a type", ZXAPP_E_SYNTAX},
        {"zxapp 1\nid x\nintent share te xt/plain f\n", "a type", ZXAPP_E_SYNTAX},
        {"zxapp 1\nid x\nintent share text/plain 1f\n", "an entry name", ZXAPP_E_SYNTAX},
        {"zxapp 1\nid x\nintent share text/plain a..b\n", "an entry name", ZXAPP_E_SYNTAX},
        {"zxapp 1\nid x\nintent share text/plain f\nintent SHARE Text/Plain g\n", "a second intent",
         ZXAPP_E_SYNTAX},
        {"zxblock 1\nid b\nversion 1\nintent share text/plain f\n", "a block has no window",
         ZXAPP_E_SYNTAX},
    };
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        int rc = parse(bad[i].m);
        CHECK(rc == bad[i].rc && strstr(A.err, bad[i].why) && A.err_line >= 3,
              "bad intent %u refused (%d, line %u): %s", i, rc, A.err_line, A.err);
    }
    {
        char m[2048];
        uint32_t n = (uint32_t) snprintf(m, sizeof m, "zxapp 1\nid x\n");
        for (uint32_t i = 0; i <= ZXAPP_INTENTS && n < sizeof m; i++) {
            int k = snprintf(m + n, sizeof m - n, "intent v%u text/plain f\n", i);
            n = k < 0 || (uint32_t) k >= sizeof m - n ? (uint32_t) sizeof m - 1 : n + (uint32_t) k;
        }
        CHECK(parse(m) == ZXAPP_E_FULL && A.err_line == 3u + ZXAPP_INTENTS &&
                  strstr(A.err, "too many intent"),
              "past ZXAPP_INTENTS: %s (line %u)", A.err, A.err_line);
    }
    CHECK(parse("zxapp 1\nid x\nfrobnicate a\n") == ZXAPP_E_SYNTAX && strstr(A.err, "intent"),
          "the unknown-directive message lists intent: %s", A.err);

    /* I1: ranks */
    CHECK(zxapp_mime_rank("text/plain", "text/plain") == 2, "exact");
    CHECK(zxapp_mime_rank("TEXT/Plain", "text/PLAIN") == 2, "exact, any case");
    CHECK(zxapp_mime_rank("text/*", "text/plain") == 1, "major");
    CHECK(zxapp_mime_rank("*/*", "text/plain") == 0, "anything");
    CHECK(zxapp_mime_rank("text/plain", "text/html") == -1, "another minor");
    CHECK(zxapp_mime_rank("text/plain", "text/plainer") == -1, "not a prefix match");
    CHECK(zxapp_mime_rank("text/plai", "text/plain") == -1, "nor the other way");
    CHECK(zxapp_mime_rank("image/*", "text/plain") == -1, "another major");
    CHECK(zxapp_mime_rank("image/*", "imagex/png") == -1, "major is whole");
    CHECK(zxapp_mime_rank("image/png", "image/*") == -1, "one image type does not take them all");
    CHECK(zxapp_mime_rank("image/*", "image/*") == 1, "a pattern over the same pattern");
    CHECK(zxapp_mime_rank("*/*", "*/*") == 0 && zxapp_mime_rank("image/*", "*/*") == -1,
          "anything is taken only by anything");
    CHECK(zxapp_mime_rank("text/plain", "text") == -1 &&
              zxapp_mime_rank("bogus", "text/plain") == -1,
          "not MIME types");
    CHECK(zxapp_mime_rank(0, "text/plain") == -1 && zxapp_mime_rank("text/plain", 0) == -1, "NULL");

    /* I2: who can */
    CHECK(parse_into(&NOTES, "zxapp 1\nid notes\nintent share text/plain append\n"
                             "intent share text/* append_any\nintent open text/markdown open\n") ==
              ZXAPP_OK,
          "notes");
    CHECK(parse_into(&PAINT, "zxapp 1\nid paint\nintent share image/* add_layer\n"
                             "intent edit image/png edit\n") == ZXAPP_OK,
          "paint");
    CHECK(parse_into(&VIEWER, "zxapp 1\nid viewer\nintent view */* show\n"
                              "intent share */* keep\n") == ZXAPP_OK,
          "viewer");
    CHECK(parse_into(&MAILER, "zxapp 1\nid mailer\nintent share text/* compose\n"
                              "intent share image/png attach\n") == ZXAPP_OK,
          "mailer");
    CHECK(parse_into(&ANY, "zxapp 1\nid other\nintent pick text/plain choose\n") == ZXAPP_OK,
          "other");
    const zxapp_t *apps[] = {&VIEWER, &MAILER, &NOTES, &PAINT, 0, &ANY};
    zxapp_handler_t h[8];
    uint32_t n = zxapp_handlers(apps, 6, "share", "text/plain", h, 8);
    CHECK(n == 3, "three apps share plain text (%u)", n);
    CHECK(n == 3 && h[0].a == &NOTES && h[0].rank == 2 && strcmp(h[0].in->entry, "append") == 0 &&
              h[0].app == 2,
          "notes first: exact, through its most specific intent");
    CHECK(n == 3 && h[1].a == &MAILER && h[1].rank == 1 && strcmp(h[1].in->entry, "compose") == 0,
          "then text/*");
    CHECK(n == 3 && h[2].a == &VIEWER && h[2].rank == 0 && h[2].app == 0, "then anything");
    n = zxapp_handlers(apps, 6, "SHARE", "Image/PNG", h, 8);
    CHECK(n == 3 && h[0].a == &MAILER && h[0].rank == 2 && h[1].a == &PAINT && h[1].rank == 1 &&
              h[2].a == &VIEWER,
          "images: mailer exact, paint image/*, viewer anything; verbs ignore case");
    n = zxapp_handlers(apps, 6, "share", "image/*", h, 8);
    CHECK(n == 2 && h[0].a == &PAINT && h[1].a == &VIEWER,
          "any image: mailer takes only png, so it is not listed");
    n = zxapp_handlers(apps, 6, "share", "text/markdown", h, 8);
    CHECK(n == 3 && h[0].a == &MAILER && h[0].rank == 1 && h[1].a == &NOTES &&
              strcmp(h[1].in->entry, "append_any") == 0 && h[2].a == &VIEWER,
          "ties keep the order given: mailer before notes");
    n = zxapp_handlers(apps, 6, "share", "text/plain", h, 1);
    CHECK(n == 3 && h[0].a == &NOTES, "cap: the count, the best first");
    CHECK(zxapp_handlers(apps, 6, "share", "text/plain", 0, 0) == 3, "count only");
    CHECK(zxapp_handlers(apps, 6, "delete", "text/plain", h, 8) == 0, "no one deletes");
    CHECK(zxapp_handlers(apps, 6, "share", "text", h, 8) == 0, "a bad type finds no one");
    CHECK(zxapp_handlers(apps, 6, "shar", "text/plain", h, 8) == 0, "verbs are whole words");
    CHECK(zxapp_handlers(apps, 6, "view", "application/pdf", h, 8) == 1 && h[0].a == &VIEWER,
          "a viewer for anything");
    CHECK(zxapp_handlers(apps, 6, "pick", "text/plain", h, 8) == 1 && h[0].a == &ANY &&
              h[0].app == 5,
          "a NULL app is skipped, indexes stay the list's");
    CHECK(zxapp_handlers(0, 6, "share", "text/plain", h, 8) == 0 &&
              zxapp_handlers(apps, 6, 0, "text/plain", h, 8) == 0 &&
              zxapp_handlers(apps, 6, "share", 0, h, 8) == 0 &&
              zxapp_handlers(apps, 6, "share", "text/plain", 0, 8) == 0,
          "bad arguments");

    /* I2 over a registry: only apps that export (C1) are there */
    static zxapp_registry_t RI;
    zxapp_registry_init(&RI);
    CHECK(parse_into(&EXP,
                     "zxapp 1\nid exporter\nversion 1.0.0\nprovides keep num x\n"
                     "logic kron keep <<\nx\n.\nintent share text/plain keep\n") == ZXAPP_OK &&
              zxapp_registry_add(&RI, &EXP) == ZXAPP_OK,
          "an exporting app with an intent registers: %s", RI.err);
    n = zxapp_registry_handlers(&RI, "share", "text/plain", h, 8);
    CHECK(n == 1 && h[0].a == &EXP && strcmp(h[0].in->entry, "keep") == 0, "found in the registry");
    CHECK(zxapp_registry_handlers(0, "share", "text/plain", h, 8) == 0, "NULL registry");
}

/* ---- the real Sutra runtime (cobol-sutra pillar + a small procedure) -------- */

static void test_sutra(void)
{
    zxapp_host_init(&H);
    CHECK(zxapp_sutra_register(&H) == ZXAPP_OK && zxapp_host_has(&H, ZXAPP_LANG_SUTRA),
          "sutra registers");
    CHECK(parse("zxapp 1\nid s\n"
                "logic sutra fee <<\n"
                "record fee-calc\n  amount: 9(9)\n  rate: 9(4)\n  fee: 9(7)\n"
                "procedure\n"
                "  compute fee = amount * rate / 10000\n"
                "  if fee < 50\n    compute fee = 50\n  end-if\n"
                "  answer fee\n.\n"
                "logic sutra price <<\n"
                "record p\n  base: s9(7)v99\n  qty: 9(3)\n  total: s9(9)v99\n"
                "procedure\n"
                "  COMPUTE TOTAL = BASE * QTY - 0.5.   # COBOL case and period\n"
                "  answer total\n.\n"
                "logic sutra band <<\n"
                "record b\n  x: s9(5)\n  y: s9(5)\n"
                "procedure\n"
                "  if x < 0\n    answer -1\n  else\n"
                "    if x > 100 then\n      compute y = 2\n    else\n      compute y = 1\n"
                "    end-if\n  end-if\n"
                "  answer y\n.\n"
                "logic sutra ok <<\n"
                "record c\n  a: s9(5)\n  b: s9(5)\n"
                "procedure\n  answer a > 0 and not b = 0 or a = -7\n.\n"
                "logic sutra half <<\nrecord h\n  a: s9(5)\nprocedure\n  answer a / 2\n.\n"
                "logic sutra none <<\nrecord n\n  a: s9(5)\nprocedure\n"
                "  if a > 0\n    answer a\n  end-if\n.\n"
                "logic sutra small <<\nrecord m\n  a: s9(9)\n  r: 9(3)\nprocedure\n"
                "  compute r = a * 2\n  answer r\n.\n"
                "logic sutra wide <<\nrecord w\n  a: s9(18)\nprocedure\n"
                "  answer a * a * a\n.\n"
                "logic sutra divz <<\nrecord z\n  a: s9(5)\n  b: s9(5)\nprocedure\n"
                "  answer a / b\n.\n") == ZXAPP_OK &&
              A.n == 9,
          "a sutra manifest: %s", A.err);
    CHECK(zxapp_load(&A, &H) == 9, "nine Sutra entries load: %s", A.err);
    int64_t amt[4] = {10000, 123456, 0, 99999999}, rate[1] = {30};
    zxapp_arg_t args[2] = {{"amount", amt, 4}, {"rate", rate, 1}};
    zxapp_num_t out[8];
    uint32_t n = 0;
    CHECK(zxapp_call(&A, &H, "fee", args, 2, out, 8, &n) == ZXAPP_OK && n == 4 &&
              out[0].num == 50 && out[1].num == 370 && out[2].num == 50 && out[3].num == 299999 &&
              out[1].den == 1,
          "SU3: COMPUTE truncates, IF applies the minimum, a shared rate");
    CHECK(zxapp_call(&A, &H, "fee", args, 2, out, 3, &n) == ZXAPP_E_BUFFER && n == 4,
          "a short buffer");
    int64_t base[2] = {1250, -199}, qty[2] = {3, 2};
    zxapp_arg_t pa[2] = {{"base", base, 2}, {"qty", qty, 2}};
    CHECK(zxapp_call(&A, &H, "price", pa, 2, out, 8, &n) == ZXAPP_OK && n == 2 &&
              out[0].num == 3700 && out[1].num == -448,
          "SU1/SU4: v99 fields take and answer hundredths (12.50 * 3 - 0.50 = 37.00)");
    int64_t xs[4] = {-5, 50, 101, 100};
    zxapp_arg_t xa = {"x", xs, 4};
    CHECK(zxapp_call(&A, &H, "band", &xa, 1, out, 8, &n) == ZXAPP_OK && n == 4 &&
              out[0].num == -1 && out[1].num == 1 && out[2].num == 2 && out[3].num == 1,
          "nested if/else, an early answer");
    int64_t av[4] = {1, 1, -7, 0}, bv[4] = {1, 0, 0, 5};
    zxapp_arg_t ca[2] = {{"a", av, 4}, {"b", bv, 4}};
    CHECK(zxapp_call(&A, &H, "ok", ca, 2, out, 8, &n) == ZXAPP_OK && n == 4 &&
              out[0].num == ZXAPP_TRUE && out[1].num == ZXAPP_FALSE && out[2].num == ZXAPP_TRUE &&
              out[3].num == ZXAPP_FALSE && out[0].den == 0,
          "a condition answers a truth (not, and, or)");
    int64_t even[2] = {8, -4}, odd[1] = {7};
    zxapp_arg_t ha = {"a", even, 2};
    CHECK(zxapp_call(&A, &H, "half", &ha, 1, out, 8, &n) == ZXAPP_OK && out[0].num == 4 &&
              out[1].num == -2,
          "a whole answer");
    ha.v = odd;
    ha.n = 1;
    CHECK(zxapp_call(&A, &H, "half", &ha, 1, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "whole number"),
          "SU4: 7/2 is not answered as a number: %s", A.err);
    int64_t neg[1] = {-1};
    zxapp_arg_t na = {"a", neg, 1};
    CHECK(zxapp_call(&A, &H, "none", &na, 1, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "without an answer"),
          "a row that never answers is refused");
    int64_t five[1] = {499}, six[1] = {500};
    zxapp_arg_t sa = {"a", five, 1};
    CHECK(zxapp_call(&A, &H, "small", &sa, 1, out, 8, &n) == ZXAPP_OK && out[0].num == 998,
          "998 fits 9(3)");
    sa.v = six;
    CHECK(zxapp_call(&A, &H, "small", &sa, 1, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "size error"),
          "SU3: 1000 in 9(3) is a size error, never stored short: %s", A.err);
    sa.v = neg;
    CHECK(zxapp_call(&A, &H, "small", &sa, 1, out, 8, &n) == ZXAPP_E_RUNTIME,
          "a negative value in an unsigned picture is refused");
    int64_t big[1] = {INT64_C(999999999999)};
    zxapp_arg_t wa = {"a", big, 1};
    CHECK(zxapp_call(&A, &H, "wide", &wa, 1, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "never rounded"),
          "an int64 overflow in an expression is refused: %s", A.err);
    int64_t huge[1] = {INT64_C(10000000000)};
    zxapp_arg_t ga = {"amount", huge, 1};
    zxapp_arg_t gargs[2] = {ga, {"rate", rate, 1}};
    CHECK(zxapp_call(&A, &H, "fee", gargs, 2, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "cannot hold"),
          "SU1: an argument wider than its picture is the pillar's refusal: %s", A.err);
    int64_t zero[1] = {0};
    zxapp_arg_t za[2] = {{"a", odd, 1}, {"b", zero, 1}};
    CHECK(zxapp_call(&A, &H, "divz", za, 2, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "division by zero"),
          "division by zero is refused");
    zxapp_arg_t wrong = {"tip", odd, 1};
    CHECK(zxapp_call(&A, &H, "half", &wrong, 1, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "not a numeric field"),
          "SU5: a name the record does not have");
    int64_t two[2] = {1, 2}, three[3] = {1, 2, 3};
    zxapp_arg_t mism[2] = {{"a", two, 2}, {"b", three, 3}};
    CHECK(zxapp_call(&A, &H, "ok", mism, 2, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "two lengths"),
          "vectors of two lengths");
    zxapp_unload(&A, &H);

    static const struct {
        const char *src, *why;
    } bad[] = {
        {"record r\n  a: 9(3)\n", "procedure"},
        {"record r\n  a: 9(3)v99 comp-1\nprocedure\n  answer a\n", "comp-1"},
        {"record r\n  a: 9(3)\nprocedure\n  answer b\n", "b is not a field"},
        {"record r\n  a: 9(3)\n  t: x(4)\nprocedure\n  answer t\n", "not a numeric"},
        {"record r\n  a: 9(3)\nprocedure\n  move a to a\n", "a statement is"},
        {"record r\n  a: 9(3)\nprocedure\n  if a > 1\n  answer a\n", "no end-if"},
        {"record r\n  a: 9(3)\nprocedure\n  end-if\n", "no open if"},
        {"record r\n  a: 9(3)\nprocedure\n  if a\n  end-if\n", "compares"},
        {"record r\n  a: 9(3)\nprocedure\n  answer a and a > 1\n", "and joins"},
        {"record r\n  a: 9(3)\nprocedure\n  answer (a + 1\n", "')'"},
        {"record r\n  a: 9(3)\nprocedure\n  compute a = 1 2\n", "unexpected"},
        {"record r\n  a: 9(3)\nprocedure\n  answer 1234567890123456789\n", "18 digits"},
        {"record r\n  a: 9(3)\nprocedure\n", "empty"},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        static char text[512];
        int k = snprintf(text, sizeof text, "zxapp 1\nid b\nlogic sutra e <<\n%s.\n", bad[i].src);
        CHECK(k > 0 && (size_t) k < sizeof text && parse(text) == ZXAPP_OK &&
                  zxapp_load(&A, &H) == ZXAPP_E_RUNTIME && strstr(A.err, "e: ") &&
                  strstr(A.err, bad[i].why),
              "bad Sutra %zu refused at load (%s): %s", i, bad[i].why, A.err);
        zxapp_unload(&A, &H);
    }
    CHECK(parse("zxapp 1\nid b\nlogic sutra e <<\nrecord r\n  a: 9(3)\nprocedure\n\n"
                "  answer zz\n.\n") == ZXAPP_OK &&
              zxapp_load(&A, &H) == ZXAPP_E_RUNTIME && strstr(A.err, "line 5: zz"),
          "a procedure refusal names the line in the source: %s", A.err);
    zxapp_unload(&A, &H);
}

/* ---- the real Trebago runtime (fortran-trebago pillar) ---------------------- */

static void test_trebago(void)
{
    zxapp_host_init(&H);
    CHECK(zxapp_trebago_register(&H) == ZXAPP_OK && zxapp_host_has(&H, ZXAPP_LANG_TREBAGO),
          "trebago registers");
    CHECK(parse("zxapp 1\nid t\n"
                "logic trebago total <<\ntrebago total\n  in amount(n), fee(n)\n  out total\n"
                "  total = sum(i, amount(i) + fee(i))\nend\n.\n"
                "logic trebago axpy <<\ntrebago axpy\n  in x(n), alpha\n  out y(n)\n"
                "  y(i) = alpha * x(i) / 3\nend\n.\n"
                "logic trebago acc <<\ntrebago acc\n  in x(n)\n  inout z(n)\n"
                "  z(i) = z(i) + x(i)\nend\n.\n"
                "logic trebago fixed <<\ntrebago fixed\n  in x(n)\n  out f(2)\n"
                "  f(i) = sum(j, x(j)) * i\nend\n.\n"
                "logic trebago inv <<\ntrebago inv\n  in x(n)\n  out y(n)\n  y(i) = 1 / x(i)\nend\n"
                ".\n") == ZXAPP_OK &&
              zxapp_load(&A, &H) == 5,
          "five Trebago programs load: %s", A.err);
    int64_t amt[3] = {10000, 250, -50}, fee[3] = {50, 50, 0};
    zxapp_arg_t args[2] = {{"amount", amt, 3}, {"fee", fee, 3}};
    zxapp_num_t out[8];
    uint32_t n = 0;
    CHECK(zxapp_call(&A, &H, "total", args, 2, out, 8, &n) == ZXAPP_OK && n == 1 &&
              out[0].num == 10300 && out[0].den == 1,
          "TB2: a scalar out is one answer");
    int64_t alpha[1] = {2};
    zxapp_arg_t xa[2] = {{"x", amt, 3}, {"alpha", alpha, 1}};
    CHECK(zxapp_call(&A, &H, "axpy", xa, 2, out, 8, &n) == ZXAPP_OK && n == 3 &&
              out[0].num == 20000 && out[0].den == 3 && out[1].num == 500 && out[1].den == 3 &&
              out[2].num == -100 && out[2].den == 3,
          "TB2: a vector out takes its input's extent; exact rationals");
    CHECK(zxapp_call(&A, &H, "axpy", xa, 2, out, 2, &n) == ZXAPP_E_BUFFER && n == 3,
          "a short buffer");
    zxapp_arg_t za[2] = {{"x", amt, 3}, {"z", fee, 3}};
    CHECK(zxapp_call(&A, &H, "acc", za, 2, out, 8, &n) == ZXAPP_OK && n == 3 &&
              out[0].num == 10050 && out[2].num == -50 && fee[0] == 50,
          "TB2: an inout answers when there is no out (the caller's values untouched)");
    CHECK(zxapp_call(&A, &H, "fixed", args, 1, out, 8, &n) == ZXAPP_E_RUNTIME, "x not given");
    zxapp_arg_t fx = {"x", fee, 3};
    CHECK(zxapp_call(&A, &H, "fixed", &fx, 1, out, 8, &n) == ZXAPP_OK && n == 2 &&
              out[0].num == 100 && out[1].num == 200,
          "a fixed extent");
    CHECK(zxapp_call(&A, &H, "total", args, 1, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "missing argument: fee"),
          "TB1: every input must be bound: %s", A.err);
    zxapp_arg_t extra[3] = {args[0], args[1], {"tip", fee, 3}};
    CHECK(zxapp_call(&A, &H, "total", extra, 3, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "does not take: tip"),
          "TB1: an argument the program does not take");
    zxapp_arg_t vec_alpha[2] = {{"x", amt, 3}, {"alpha", fee, 3}};
    CHECK(zxapp_call(&A, &H, "axpy", vec_alpha, 2, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "scalar"),
          "a scalar takes one value");
    int64_t short_fee[2] = {1, 2};
    zxapp_arg_t mism[2] = {{"amount", amt, 3}, {"fee", short_fee, 2}};
    CHECK(zxapp_call(&A, &H, "total", mism, 2, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "TB_E_SHAPE"),
          "extents that disagree are the pillar's refusal: %s", A.err);
    int64_t big[2] = {INT64_MAX, INT64_MAX}, zero[2] = {0, 0};
    zxapp_arg_t bg[2] = {{"amount", big, 2}, {"fee", zero, 2}};
    CHECK(zxapp_call(&A, &H, "total", bg, 2, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "ZXV_E_RANGE"),
          "TB4: an overflow is refused, never rounded: %s", A.err);
    int64_t withz[2] = {4, 0};
    zxapp_arg_t ia = {"x", withz, 2};
    CHECK(zxapp_call(&A, &H, "inv", &ia, 1, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "TB_E_DIVZERO"),
          "TB4: division by zero: %s", A.err);
    zxapp_unload(&A, &H);

    static const struct {
        const char *src, *why;
    } bad[] = {
        {"trebago b\n  in x(n)\n  out s\n  s = sum(i, x(i) * 1.0e-3)\nend\n", "line"},
        {"trebago b\n  in a(m, n)\n  out s\n  s = sum(i, a(i, 1))\nend\n", "matrix"},
        {"trebago b\n  in x(n)\n  var t(n)\n  out y(n)\n  y(i) = x(i)\nend\n", ""},
        {"trebago b\n  in x(n)\n  inout q\n  out y(m)\n  y(i) = q\nend\n", "y"},
        {"trebago b\n  in x(n)\nend\n", ""},
        {"trebago b\n  in x(n)\n  out s\n  s = sum(i, x(i)\nend\n", "line"},
    };
    int expect_ok[] = {0, 0, 1, 0, 0, 0};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        static char text[512];
        int k = snprintf(text, sizeof text, "zxapp 1\nid b\nlogic trebago e <<\n%s.\n", bad[i].src);
        int rc =
            k > 0 && (size_t) k < sizeof text && parse(text) == ZXAPP_OK ? zxapp_load(&A, &H) : -99;
        if (expect_ok[i])
            CHECK(rc == 1, "Trebago %zu loads: %s", i, A.err);
        else
            CHECK(rc == ZXAPP_E_RUNTIME && strstr(A.err, "e: ") && strstr(A.err, bad[i].why),
                  "bad Trebago %zu refused at load: %s", i, A.err);
        zxapp_unload(&A, &H);
    }
}

/* ---- the real MYRA runtime (mumps-myra pillar) ------------------------------ */

static void test_myra(void)
{
    zxapp_host_init(&H);
    CHECK(zxapp_myra_register(&H) == ZXAPP_OK && zxapp_host_has(&H, ZXAPP_LANG_MYRA),
          "myra registers");
    CHECK(parse("zxapp 1\nid m\n"
                "logic myra pay <<\n"
                "^ZXFEE(batch:i9, seq:i9) = amount:n15.2! ^ fee:n9.2! ^ day:d ^ memo:t20\n.\n"
                "logic myra gmap <<\nGLOBAL ^ZXQ\nNODE ^ZXQ(ID)\n  KEY ID INTEGER(3)\n"
                "  PIECE 1 QTY INTEGER(2) REQUIRED\n.\n") == ZXAPP_OK &&
              zxapp_load(&A, &H) == 2,
          "a MYRA schema and a global map load: %s", A.err);
    int64_t batch[1] = {7}, seq[4] = {1, 2, 3, 1000000000};
    int64_t amt[4] = {1250, INT64_C(999999999999999), INT64_C(1000000000000000), 5};
    int64_t fee[4] = {50, 50, 50, 50};
    zxapp_arg_t args[4] = {
        {"batch", batch, 1}, {"seq", seq, 4}, {"amount", amt, 4}, {"FEE", fee, 4}};
    zxapp_num_t out[8];
    uint32_t n = 0;
    CHECK(zxapp_call(&A, &H, "pay", args, 4, out, 8, &n) == ZXAPP_OK && n == 4 &&
              out[0].num == ZXAPP_TRUE && out[1].num == ZXAPP_TRUE && out[2].num == ZXAPP_FALSE &&
              out[3].num == ZXAPP_FALSE && out[0].den == 0,
          "MY3: 15 digits fit n15.2, 16 do not; a 10-digit key does not fit i9");
    int64_t days[3] = {67854, 2980013, 2980014};
    zxapp_arg_t da = {"day", days, 3};
    CHECK(zxapp_call(&A, &H, "pay", &da, 1, out, 8, &n) == ZXAPP_OK && n == 3 &&
              out[0].num == ZXAPP_TRUE && out[1].num == ZXAPP_TRUE && out[2].num == ZXAPP_FALSE,
          "MY2: a DATE is a $H day, inside 0..2980013");
    int64_t big[1] = {INT64_MAX};
    zxapp_arg_t ba = {"fee", big, 1};
    CHECK(zxapp_call(&A, &H, "pay", &ba, 1, out, 8, &n) == ZXAPP_OK && out[0].num == ZXAPP_FALSE,
          "an overflowing value does not fit: FALSE, never rounded");
    int64_t q[3] = {99, 100, -99};
    zxapp_arg_t qa = {"qty", q, 3};
    CHECK(zxapp_call(&A, &H, "gmap", &qa, 1, out, 8, &n) == ZXAPP_OK && n == 3 &&
              out[0].num == ZXAPP_TRUE && out[1].num == ZXAPP_FALSE && out[2].num == ZXAPP_TRUE,
          "the global map's INTEGER(2)");
    zxapp_arg_t memo = {"memo", fee, 1};
    CHECK(zxapp_call(&A, &H, "pay", &memo, 1, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "TEXT"),
          "MY4: a TEXT field takes no number");
    zxapp_arg_t nope = {"tip", fee, 1};
    CHECK(zxapp_call(&A, &H, "pay", &nope, 1, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "does not have: tip"),
          "MY4: a name the node does not have");
    int64_t two[2] = {1, 2};
    zxapp_arg_t mism[2] = {{"seq", seq, 4}, {"fee", two, 2}};
    CHECK(zxapp_call(&A, &H, "pay", mism, 2, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "two lengths"),
          "vectors of two lengths");
    CHECK(zxapp_call(&A, &H, "pay", args, 4, out, 3, &n) == ZXAPP_E_BUFFER && n == 4,
          "a short buffer");
    zxapp_unload(&A, &H);
    CHECK(parse("zxapp 1\nid b\nlogic myra e <<\n^ZXBAD(id:i9,seq:i9)=amount:n9.2\n"
                "^ZXBAD(id:i9,day:d)=note:t20\n.\n") == ZXAPP_OK &&
              zxapp_load(&A, &H) == ZXAPP_E_RUNTIME && strstr(A.err, "e: "),
          "an ambiguous schema is the pillar's refusal at load: %s", A.err);
    zxapp_unload(&A, &H);
    CHECK(parse("zxapp 1\nid b\nlogic myra e <<\n^ZXBAD(id:q9)=amount:n9.2\n.\n") == ZXAPP_OK &&
              zxapp_load(&A, &H) == ZXAPP_E_RUNTIME && strstr(A.err, "line 1"),
          "a type MYRA does not have is refused at load: %s", A.err);
    zxapp_unload(&A, &H);
}

/* ---- A5 across languages: Sutra, KRON, Eve, Trebago and MYRA witness -------- */

static const char LIMITS_EVE[] = "eve f\ncents = 0 .. 100000000\n"
                                 "fee_limits {\n  amount: cents 1 .. 1000000\n"
                                 "  fee: cents 0 .. 2500\n}\n";

static void test_witness_mixed(void)
{
    zxapp_host_init(&H);
    CHECK(zxapp_sutra_register(&H) == ZXAPP_OK && zxapp_kron_register(&H) == ZXAPP_OK &&
              zxapp_eve_register(&H) == ZXAPP_OK && zxapp_trebago_register(&H) == ZXAPP_OK &&
              zxapp_myra_register(&H) == ZXAPP_OK && H.n == 5,
          "five runtimes side by side");
    static char text[2048];
    int k = snprintf(text, sizeof text,
                     "zxapp 1\nid mix\n"
                     "logic sutra ok <<\nrecord r\n  amount: s9(18)\n  fee: s9(18)\nprocedure\n"
                     "  answer amount >= 1 and amount <= 1000000 and fee >= 0 and fee <= 2500\n.\n"
                     "logic kron  ok <<\n(amount>0)&(amount<1000001)&(fee>-1)&fee<2501\n.\n"
                     "logic eve   ok <<\n%s.\n"
                     "logic sutra off <<\nrecord r\n  amount: s9(18)\n  fee: s9(18)\nprocedure\n"
                     "  answer amount >= 1 and amount <= 1000000 and fee >= 0 and fee < 2500\n.\n"
                     "logic kron  off <<\n(amount>0)&(amount<1000001)&(fee>-1)&fee<2501\n.\n"
                     "logic eve   off <<\n%s.\n"
                     "logic sutra solo <<\nrecord r\n  amount: s9(18)\n  fee: s9(18)\nprocedure\n"
                     "  answer fee <= 2500\n.\n"
                     "logic python solo apps/mix/solo.py\n"
                     "logic trebago sum <<\ntrebago s\n  in amount(n), fee(n)\n  out s\n"
                     "  s = sum(i, amount(i) + fee(i))\nend\n.\n"
                     "logic kron  sum <<\n+/amount+fee\n.\n"
                     "logic myra  fits <<\n^ZXF(n:i9)=amount:i7!^fee:i4!\n.\n"
                     "logic eve   fits <<\neve g\nn = 0 .. 9999999\nr {\n  amount: n\n  fee: n 0 "
                     ".. 9999\n}\n.\n",
                     LIMITS_EVE, LIMITS_EVE);
    CHECK(k > 0 && (size_t) k < sizeof text && parse(text) == ZXAPP_OK && A.n == 12,
          "one manifest, five languages, witnessed entries: %s", A.err);
    CHECK(zxapp_load(&A, &H) == 11 && !A.logic[7].loaded,
          "eleven entries load; python waits for its runtime: %s", A.err);
    int64_t amt[4] = {10000, 0, 1000000, 500}, fee[4] = {30, 30, 2500, 2501};
    zxapp_arg_t args[2] = {{"amount", amt, 4}, {"fee", fee, 4}};
    zxapp_num_t out[8];
    uint32_t n = 0;
    int32_t lp = -1;
    CHECK(zxapp_call_witnessed(&A, &H, "ok", args, 2, out, 8, &n, &lp) == ZXAPP_OK &&
              lp == ZXAPP_TRUE && n == 4 && out[0].num == ZXAPP_TRUE && out[1].num == ZXAPP_FALSE &&
              out[2].num == ZXAPP_TRUE && out[3].num == ZXAPP_FALSE,
          "Sutra, KRON and Eve agree: TRUE");
    CHECK(zxapp_call_witnessed(&A, &H, "off", args, 2, out, 8, &n, &lp) == ZXAPP_E_CONFLICT &&
              lp == ZXAPP_BOTH && n == 4 && out[2].num == ZXAPP_FALSE,
          "an off-by-one Sutra rule disagrees at the limit: BOTH, held (Sutra's answer kept)");
    int64_t inside[1] = {100}, small[1] = {30};
    zxapp_arg_t in1[2] = {{"amount", inside, 1}, {"fee", small, 1}};
    CHECK(zxapp_call_witnessed(&A, &H, "off", in1, 2, out, 8, &n, &lp) == ZXAPP_OK &&
              lp == ZXAPP_TRUE,
          "away from the limit the same three agree");
    CHECK(zxapp_call_witnessed(&A, &H, "solo", args, 2, out, 8, &n, &lp) == ZXAPP_OK &&
              lp == ZXAPP_NEITHER && n == 4 && out[3].num == ZXAPP_FALSE,
          "the other witness is missing (no python runtime): NEITHER");
    CHECK(zxapp_call_witnessed(&A, &H, "sum", args, 2, out, 8, &n, &lp) == ZXAPP_OK &&
              lp == ZXAPP_TRUE && n == 1 && out[0].num == 1015561,
          "Trebago and KRON add up the same");
    int64_t big[2] = {INT64_MAX, 1}, zero[2] = {0, 0};
    zxapp_arg_t ov[2] = {{"amount", big, 2}, {"fee", zero, 2}};
    CHECK(zxapp_call_witnessed(&A, &H, "sum", ov, 2, out, 8, &n, &lp) == ZXAPP_E_RUNTIME,
          "both refuse the overflow: a refusal, not a conflict");
    CHECK(zxapp_call_witnessed(&A, &H, "fits", args, 2, out, 8, &n, &lp) == ZXAPP_OK &&
              lp == ZXAPP_TRUE && out[3].num == ZXAPP_TRUE,
          "MYRA's i7/i4 and Eve's ranges agree");
    int64_t wide[1] = {10000};
    zxapp_arg_t wf[2] = {{"amount", inside, 1}, {"fee", wide, 1}};
    CHECK(zxapp_call_witnessed(&A, &H, "fits", wf, 2, out, 8, &n, &lp) == ZXAPP_OK &&
              lp == ZXAPP_TRUE && out[0].num == ZXAPP_FALSE,
          "and agree on what does not fit");
    zxapp_unload(&A, &H);
}

/* ---- apps/fee_check.zxapp: one app, five languages, read from disk ---------- */

static const char *SRC_ROOT;

static int32_t read_src(void *ctx, const char *path, char *buf, uint32_t cap)
{
    (void) ctx;
    char full[512];
    int k = snprintf(full, sizeof full, "%s/%s", SRC_ROOT, path);
    if (k < 0 || (size_t) k >= sizeof full) return -1;
    FILE *f = fopen(full, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap, f);
    int more = fgetc(f) != EOF;
    fclose(f);
    return more ? -1 : (int32_t) n;
}

static void test_fee_check(void)
{
    static char manifest[8192];
    static zxapp_t FEE;
    zxapp_host_init(&H);
    H.read_file = read_src;
    zxapp_sutra_register(&H);
    zxapp_kron_register(&H);
    zxapp_eve_register(&H);
    zxapp_trebago_register(&H);
    zxapp_myra_register(&H);
    int32_t len = SRC_ROOT ? read_src(0, "apps/fee_check.zxapp", manifest, sizeof manifest) : -1;
    CHECK(len > 0 && zxapp_parse(&FEE, manifest, (uint32_t) len) == ZXAPP_OK && FEE.n == 7,
          "apps/fee_check.zxapp parses: %s", FEE.err);
    if (len <= 0) return;
    CHECK(zxapp_load(&FEE, &H) == 7, "all seven entries load: %s", FEE.err);
    bool lang[ZXAPP_LANG_COUNT] = {false};
    uint32_t langs = 0;
    for (uint32_t i = 0; i < FEE.n; i++)
        if (FEE.logic[i].loaded && !lang[FEE.logic[i].lang]) {
            lang[FEE.logic[i].lang] = true;
            langs++;
        }
    CHECK(langs == 5 && lang[ZXAPP_LANG_SUTRA] && lang[ZXAPP_LANG_KRON] && lang[ZXAPP_LANG_EVE] &&
              lang[ZXAPP_LANG_TREBAGO] && lang[ZXAPP_LANG_MYRA],
          "one app runs five languages natively");
    int64_t amt[4] = {10000, 123456, 1000000, 99999999}, rate[4] = {30, 30, 25, 30};
    zxapp_arg_t fa[2] = {{"amount", amt, 4}, {"rate", rate, 4}};
    zxapp_num_t out[8];
    uint32_t n = 0;
    int32_t lp = -1;
    CHECK(zxapp_call_witnessed(&FEE, &H, "fee", fa, 2, out, 8, &n, &lp) == ZXAPP_OK &&
              lp == ZXAPP_TRUE && n == 4 && out[0].num == 50 && out[1].num == 370 &&
              out[2].num == 2500 && out[3].num == 299999,
          "fee: Sutra and KRON agree (TRUE): 50 minimum, 370, 2500, 299999");
    int64_t fee[4] = {50, 370, 2500, 299999};
    zxapp_arg_t oa[2] = {{"amount", amt, 4}, {"fee", fee, 4}};
    CHECK(zxapp_call_witnessed(&FEE, &H, "fee_ok", oa, 2, out, 8, &n, &lp) == ZXAPP_OK &&
              lp == ZXAPP_TRUE && n == 4 && out[0].num == ZXAPP_TRUE && out[1].num == ZXAPP_TRUE &&
              out[2].num == ZXAPP_TRUE && out[3].num == ZXAPP_FALSE,
          "fee_ok: Sutra, KRON and Eve agree (TRUE); the last payment is over the limits");
    int64_t wide[1] = {INT64_C(100000000000)}, r1[1] = {30};
    zxapp_arg_t wa[2] = {{"amount", wide, 1}, {"rate", r1, 1}};
    CHECK(zxapp_call_witnessed(&FEE, &H, "fee", wa, 2, out, 8, &n, &lp) == ZXAPP_E_CONFLICT &&
              lp == ZXAPP_BOTH,
          "an amount too wide for Sutra's 9(9): Sutra refuses, KRON answers: BOTH, held");
    CHECK(zxapp_call(&FEE, &H, "total", oa, 2, out, 8, &n) == ZXAPP_OK && n == 1 &&
              out[0].num == 101436374 && out[0].den == 1,
          "total: Trebago's exact batch sum");
    int64_t batch[1] = {7}, seq[4] = {1, 2, 3, 4};
    zxapp_arg_t ra[4] = {{"batch", batch, 1}, {"seq", seq, 4}, {"amount", amt, 4}, {"fee", fee, 4}};
    CHECK(zxapp_call(&FEE, &H, "record_ok", ra, 4, out, 8, &n) == ZXAPP_OK && n == 4 &&
              out[0].num == ZXAPP_TRUE && out[3].num == ZXAPP_TRUE,
          "record_ok: MYRA files them in ^ZXFEE");
    zxapp_arg_t bad_order[2] = {{"rate", rate, 4}, {"amount", amt, 4}};
    CHECK(zxapp_call_witnessed(&FEE, &H, "fee", bad_order, 2, out, 8, &n, &lp) == ZXAPP_E_CONTRACT,
          "B3: the contract is checked before any of the languages runs");
    zxapp_unload(&FEE, &H);
}

/* R1-R4: slot, layer, icon, budget and needs */
static void test_registry_fields(void)
{
    CHECK(parse("zxapp 1\nid a\n") == ZXAPP_OK && A.slot == 0 && A.layer == ZXAPP_LAYER_NONE &&
              !A.icon[0] && A.budget == 0 && A.nneeds == 0,
          "none given: all empty");
    CHECK(parse("zxapp 1\nid murmuration\nslot 017   # catalogue number\nlayer L1\n"
                "icon icons/l1/017-murmuration.svg\nbudget 1500\nneeds net\n"
                "needs fs.read space:home\nneeds peers\nneeds location\n") == ZXAPP_OK,
          "full: %s", A.err);
    CHECK(A.slot == 17 && A.layer == ZXAPP_LAYER_L1 &&
              !strcmp(A.icon, "icons/l1/017-murmuration.svg") && A.budget == 1500,
          "values: %u %u %s %llu", A.slot, A.layer, A.icon, (unsigned long long) A.budget);
    CHECK(A.nneeds == 4 && !strcmp(A.needs[0].cap, "net") && !A.needs[0].scope[0] &&
              !strcmp(A.needs[1].cap, "fs.read") && !strcmp(A.needs[1].scope, "space:home") &&
              A.needs[1].line == 8 && !strcmp(A.needs[3].cap, "location"),
          "needs in order");
    CHECK(zxapp_needs(&A, "fs.read", NULL) && zxapp_needs(&A, "fs.read", "space:home") &&
              !zxapp_needs(&A, "fs.read", "") && zxapp_needs(&A, "net", "") &&
              !zxapp_needs(&A, "fs.write", NULL) && !zxapp_needs(&A, "fs.read", "space:office"),
          "zxapp_needs");
    /* the layers' ranges, at their edges */
    static const struct {
        uint32_t slot;
        int layer;
    } edge[] = {{0, ZXAPP_LAYER_NONE}, {1, ZXAPP_LAYER_L1},    {144, ZXAPP_LAYER_L1},
                {145, ZXAPP_LAYER_L2}, {271, ZXAPP_LAYER_L2},  {272, ZXAPP_LAYER_L3},
                {407, ZXAPP_LAYER_L3}, {408, ZXAPP_LAYER_NONE}};
    for (unsigned i = 0; i < sizeof edge / sizeof edge[0]; i++)
        CHECK(zxapp_slot_layer(edge[i].slot) == edge[i].layer, "slot %u", edge[i].slot);
    CHECK(parse("zxapp 1\nid a\nslot 144\nlayer L1\n") == ZXAPP_OK, "144 is L1");
    CHECK(parse("zxapp 1\nid a\nlayer L2\nslot 145\n") == ZXAPP_OK && A.slot == 145, "145 is L2");
    CHECK(parse("zxapp 1\nid a\nslot 271\nlayer L2\n") == ZXAPP_OK, "271 is L2");
    CHECK(parse("zxapp 1\nid a\nslot 272\nlayer L3\n") == ZXAPP_OK, "272 is L3");
    CHECK(parse("zxapp 1\nid a\nslot 407\nlayer L3\n") == ZXAPP_OK && A.slot == 407, "407 is L3");
    CHECK(parse("zxapp 1\nid a\nslot 145\nlayer L1\n") == ZXAPP_E_SYNTAX &&
              strstr(A.err, "outside its layer") && A.err_line == 4,
          "145 is not L1: %s", A.err);
    CHECK(parse("zxapp 1\nid a\nlayer L2\nslot 144\n") == ZXAPP_E_SYNTAX, "144 is not L2");
    CHECK(parse("zxapp 1\nid a\nslot 272\nlayer L2\n") == ZXAPP_E_SYNTAX, "272 is not L2");
    CHECK(parse("zxapp 1\nid a\nslot 271\nlayer L3\n") == ZXAPP_E_SYNTAX, "271 is not L3");
    static const char *const bad[] = {
        "slot 0",          "slot 408",   "slot -1",   "slot 1x",       "slot",
        "slot 1 2",        "layer L4",   "layer l1",  "layer",         "budget 0",
        "budget -5",       "budget 1.5", "budget",    "icon",          "icon a b",
        "needs",           "needs disk", "needs NET", "needs net a b", "needs net sp@ce",
        "needs fs.read a", /* fine alone: the next line repeats it */
    };
    for (unsigned i = 0; i + 1 < sizeof bad / sizeof bad[0]; i++) {
        char m[160];
        snprintf(m, sizeof m, "zxapp 1\nid a\n%s\n", bad[i]);
        CHECK(parse(m) == ZXAPP_E_SYNTAX && A.err_line == 3, "refused: %s (%s)", bad[i], A.err);
    }
    CHECK(parse("zxapp 1\nid a\nneeds disk\n") == ZXAPP_E_SYNTAX && strstr(A.err, "disk"),
          "an unknown capability is named: %s", A.err);
    CHECK(parse("zxapp 1\nid a\nbudget 9223372036854775807\n") == ZXAPP_OK &&
              A.budget == UINT64_C(9223372036854775807),
          "the biggest budget");
    CHECK(parse("zxapp 1\nid a\nbudget 9223372036854775808\n") == ZXAPP_E_SYNTAX, "one past it");
    CHECK(parse("zxapp 1\nid a\nbudget 1\n") == ZXAPP_OK && A.budget == 1, "the smallest budget");
    CHECK(parse("zxapp 1\nid a\nneeds fs.read a\nneeds fs.read a\n") == ZXAPP_E_SYNTAX &&
              A.err_line == 4 && A.nneeds == 1,
          "the same need twice");
    CHECK(parse("zxapp 1\nid a\nneeds fs.read a\nneeds fs.read b\nneeds fs.read\n") == ZXAPP_OK &&
              A.nneeds == 3,
          "other scopes are other needs");
    CHECK(parse("zxapp 1\nid a\nslot 1\nslot 1\n") == ZXAPP_E_SYNTAX && A.err_line == 4,
          "a second slot");
    CHECK(parse("zxapp 1\nid a\nlayer L1\nlayer L1\n") == ZXAPP_E_SYNTAX, "a second layer");
    CHECK(parse("zxapp 1\nid a\nicon x.svg\nicon y.svg\n") == ZXAPP_E_SYNTAX, "a second icon");
    CHECK(parse("zxapp 1\nid a\nbudget 5\nbudget 5\n") == ZXAPP_E_SYNTAX, "a second budget");
    char big[ZXAPP_PATH_MAX + 40];
    memset(big, 0, sizeof big);
    strcpy(big, "zxapp 1\nid a\nicon ");
    size_t at = strlen(big);
    memset(big + at, 'i', ZXAPP_PATH_MAX - 1);
    big[at + ZXAPP_PATH_MAX - 1] = '\n';
    CHECK(parse(big) == ZXAPP_OK && strlen(A.icon) == ZXAPP_PATH_MAX - 1, "the longest icon path");
    big[at + ZXAPP_PATH_MAX - 1] = 'i';
    big[at + ZXAPP_PATH_MAX] = '\n';
    CHECK(parse(big) == ZXAPP_E_SYNTAX, "one longer is refused");
    char many[1024] = "zxapp 1\nid a\n";
    for (unsigned i = 0; i < ZXAPP_NEEDS; i++) {
        char l[40];
        snprintf(l, sizeof l, "needs net s%u\n", i);
        strcat(many, l);
    }
    CHECK(parse(many) == ZXAPP_OK && A.nneeds == ZXAPP_NEEDS, "ZXAPP_NEEDS needs");
    strcat(many, "needs net over\n");
    CHECK(parse(many) == ZXAPP_E_FULL, "one more is too many");
    char scope[ZXAPP_SCOPE_MAX + 40];
    snprintf(scope, sizeof scope, "zxapp 1\nid a\nneeds net %.*s\n", (int) ZXAPP_SCOPE_MAX - 1,
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    CHECK(parse(scope) == ZXAPP_OK && strlen(A.needs[0].scope) == ZXAPP_SCOPE_MAX - 1,
          "the longest scope");
    snprintf(scope, sizeof scope, "zxapp 1\nid a\nneeds net %.*s\n", (int) ZXAPP_SCOPE_MAX,
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    CHECK(parse(scope) == ZXAPP_E_SYNTAX, "one longer is refused");
    CHECK(parse("zxapp 1\nid a\nneeds net Az09._:/-\n") == ZXAPP_OK, "every scope character");
    /* R4: blocks carry none of them */
    static const char *const in_block[] = {"slot 1", "layer L1", "icon a.svg", "budget 1",
                                           "needs net"};
    for (unsigned i = 0; i < sizeof in_block / sizeof in_block[0]; i++) {
        char m[160];
        snprintf(m, sizeof m, "zxblock 1\nid b\nversion 1.0.0\n%s\n", in_block[i]);
        CHECK(parse(m) == ZXAPP_E_SYNTAX && strstr(A.err, "block"), "a block refuses %s",
              in_block[i]);
    }
    /* R3: one table, every name known, none twice */
    uint32_t nc;
    const char *const *caps = zxapp_caps(&nc);
    CHECK(nc >= 10, "the table: %u", nc);
    for (uint32_t i = 0; i < nc; i++) {
        CHECK(zxapp_cap_known(caps[i]), "%s known", caps[i]);
        for (uint32_t j = i + 1; j < nc; j++) CHECK(strcmp(caps[i], caps[j]), "%s twice", caps[i]);
    }
    CHECK(!zxapp_cap_known(NULL) && !zxapp_cap_known("") && !zxapp_cap_known("fs") &&
              !zxapp_cap_known("fs.reads") && !zxapp_cap_known("Net"),
          "near misses");
}

/* ---- wave 0: the built-in apps' own manifests, read from disk ----------------
 * Each app's logic runs in the languages its manifest names, witnessed
 * (A5), and every row is checked against the C the app or zxvd already
 * runs (the reference is named beside each). Inputs are swept with a fixed
 * generator, so a run is the same everywhere. */

#define WAVE_ROWS 64u

static zxapp_registry_t WREG;
static zxapp_wasm_t WW;
static uint64_t WSEED = UINT64_C(0x9E3779B97F4A7C15);

static int64_t wrand(int64_t lo, int64_t hi) /* lo..hi inclusive */
{
    WSEED = WSEED * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    uint64_t span = (uint64_t) (hi - lo) + 1u;
    return lo + (int64_t) ((WSEED >> 11) % span);
}

/* Read, compose and load one manifest; the number of entries loaded, or <0. */
static int wave_app(zxapp_t *a, char *buf, uint32_t cap, const char *path)
{
    int32_t len = read_src(0, path, buf, cap);
    if (len <= 0) return -100;
    int rc = zxapp_parse(a, buf, (uint32_t) len);
    if (rc == ZXAPP_OK) rc = zxapp_compose(a, &WREG);
    if (rc != ZXAPP_OK) return rc;
    return zxapp_load(a, &H);
}

/* A witnessed call whose every row must equal want[]; lp must be TRUE. */
static bool wave_same(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t nargs,
                      const int64_t *want, uint32_t rows, bool truth)
{
    /* one language alone is unwitnessed (NEITHER); two or more must agree */
    uint32_t langs = 0;
    for (uint32_t i = 0; i < a->n; i++) langs += !strcmp(a->logic[i].entry, entry);
    int32_t want_lp = langs > 1 ? ZXAPP_TRUE : ZXAPP_NEITHER;
    zxapp_num_t out[WAVE_ROWS];
    uint32_t n = 0;
    int32_t lp = -1;
    int rc = zxapp_call_witnessed(a, &H, entry, args, nargs, out, WAVE_ROWS, &n, &lp);
    if (rc != ZXAPP_OK || lp != want_lp || n != rows) {
        printf("    %s: rc %d lp %d n %u (%s)\n", entry, rc, (int) lp, n, a->err);
        return false;
    }
    for (uint32_t i = 0; i < rows; i++)
        if (out[i].num != want[i] || out[i].den != (truth ? 0 : 1)) {
            printf("    %s row %u: %lld/%lld, want %lld\n", entry, i, (long long) out[i].num,
                   (long long) out[i].den, (long long) want[i]);
            return false;
        }
    return true;
}

static int32_t wave_lp(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t nargs)
{
    zxapp_num_t out[WAVE_ROWS];
    uint32_t n = 0;
    int32_t lp = -1;
    (void) zxapp_call_witnessed(a, &H, entry, args, nargs, out, WAVE_ROWS, &n, &lp);
    return lp;
}

static int64_t T_(bool b)
{
    return b ? ZXAPP_TRUE : ZXAPP_FALSE;
}

/* The C references. Each is the rule as the named C runs it. */
static int64_t c_used_x100(int64_t total, int64_t left) /* almanac_model.c used_x100 */
{
    return !total || left > total ? -1 : (total - left) * 10000 / total;
}
static int64_t c_rate(int64_t now, int64_t was, int64_t dt) /* sounding_model.c rate */
{
    return !dt || now < was ? -1 : (now - was) * 1000 / dt;
}
static int64_t c_left(int64_t minutes, int64_t extra, int64_t used) /* zxv_screen.c apply */
{
    int64_t cap = minutes + extra;
    return used >= cap ? 0 : cap - used;
}

/* ---- one test file per app: kernel/src/apps/tests/<id>_zxapp.inc --------------
 * An app's whole test sits in its own file, so it can be read and written
 * beside the manifest:
 *
 *     APP_TEST(<id>, <entries that load>, <slot>)
 *     {
 *         ... CHECK(wave_same(app, "<entry>", ...), ...) for each entry ...
 *     }
 *
 * test_zxapp.sh writes the index (APP_TESTS_INDEX) that includes every such
 * file. Each app is read from apps/<id>.zxapp and composed from the shared
 * blocks, the wave 0 apps and the apps tested before it (an app waits until
 * every app it uses has loaded), then loaded and checked like a wave 0 app,
 * and only then handed to its own test, which must check something. */
typedef struct {
    const char *id;
    int loaded;
    uint32_t slot;
    void (*run)(zxapp_t *app);
} app_test_t;
#define APP_TEST(id_, loaded_, slot_)                                                              \
    static void app_test_##id_(zxapp_t *app);                                                      \
    static const app_test_t APP_TEST_##id_ = {#id_, loaded_, slot_, app_test_##id_};               \
    static void app_test_##id_(zxapp_t *app)
#define APP_TEST_ROW(id_) &APP_TEST_##id_,
#ifdef APP_TESTS_INDEX
#    include APP_TESTS_INDEX
#endif
#ifndef APP_TESTS_ROWS
#    define APP_TESTS_ROWS
#endif
static const app_test_t *const APP_TESTS[] = {APP_TESTS_ROWS 0};

#define APP_TESTS_MAX 400u

/* The part `id` among the wave 0 apps and the per-app apps that loaded. */
static const zxapp_t *app_part(const zxapp_t *w0, int nw0, const zxapp_t *t, const bool *ok,
                               uint32_t n, const char *id)
{
    for (int i = 0; i < nw0; i++)
        if (!strcmp(w0[i].id, id)) return &w0[i];
    for (uint32_t i = 0; i < n; i++)
        if (ok[i] && !strcmp(t[i].id, id)) return &t[i];
    return 0;
}

static void app_tests_run(const zxapp_t *w0, int nw0)
{
    static zxapp_t TAPP[APP_TESTS_MAX];
    static char TBUF[APP_TESTS_MAX][16384];
    static bool done[APP_TESTS_MAX], ok[APP_TESTS_MAX];
    static zxapp_registry_t R, C;
    uint32_t n = 0;
    while (APP_TESTS[n]) n++;
    CHECK(n <= APP_TESTS_MAX, "per-app tests: %u, at most %u", n, APP_TESTS_MAX);
    if (n > APP_TESTS_MAX) return;
    if (!n) return;
    CHECK(zxapp_lingua_register(&H, 0) == ZXAPP_OK, "per-app tests: Lingua registers too");
    for (bool progress = true; progress;) {
        progress = false;
        for (uint32_t i = 0; i < n; i++) {
            if (done[i]) continue;
            const app_test_t *t = APP_TESTS[i];
            zxapp_t *a = &TAPP[i];
            char path[ZXAPP_PATH_MAX];
            snprintf(path, sizeof path, "apps/%s.zxapp", t->id);
            int32_t len = read_src(0, path, TBUF[i], sizeof TBUF[i]);
            int rc = len > 0 ? zxapp_parse(a, TBUF[i], (uint32_t) len) : ZXAPP_E_ARG;
            if (rc != ZXAPP_OK) {
                done[i] = progress = true;
                CHECK(0, "%s: read and parse %s: %d %s", t->id, path, rc, len > 0 ? a->err : "");
                continue;
            }
            R = WREG; /* the shared blocks, then the parts this app uses */
            bool wait = false;
            for (uint32_t u = 0; u < a->nuse && !wait; u++) {
                if (zxapp_registry_find(&R, a->use[u].id, a->use[u].ver, a->use[u].exact)) continue;
                const zxapp_t *p = app_part(w0, nw0, TAPP, ok, n, a->use[u].id);
                if (p)
                    CHECK(zxapp_registry_add(&R, p) == ZXAPP_OK, "%s: part %s registers: %s", t->id,
                          p->id, R.err);
                else
                    wait = true;
            }
            if (wait) continue; /* a part is not loaded yet: try again next pass */
            done[i] = progress = true;
            rc = zxapp_compose(a, &R);
            if (rc == ZXAPP_OK) rc = zxapp_load(a, &H);
            CHECK(rc == t->loaded, "%s: %d of %d entries load: %s", path, rc, t->loaded, a->err);
            int layer = zxapp_slot_layer(t->slot);
            char icon[ZXAPP_PATH_MAX];
            snprintf(icon, sizeof icon, "icons/l%d/%03u-%s.svg", layer, t->slot, t->id);
            CHECK(!strcmp(a->id, t->id) && a->slot == t->slot && a->layer == layer &&
                      a->budget > 0 && a->nneeds > 0 && !strcmp(a->icon, icon),
                  "%s: registry fields (slot %u, layer L%d, icon %s)", t->id, t->slot, layer, icon);
            zxapp_registry_init(&C); /* C1: every app is a block too */
            CHECK(zxapp_registry_add(&C, a) == ZXAPP_OK, "%s registers as a block: %s", t->id,
                  C.err);
            if (rc != t->loaded || rc < 0) continue;
            int before = checks;
            t->run(a);
            CHECK(checks > before + 1, "%s: its test checks something", t->id);
            zxapp_unload(a, &H);
            ok[i] = true;
        }
    }
    for (uint32_t i = 0; i < n; i++)
        CHECK(done[i], "%s: an app it uses has no test or never loads (%s)", APP_TESTS[i]->id,
              TAPP[i].nuse ? TAPP[i].use[0].id : "?");
}

static void test_wave0(void)
{
    static char buf[16][4096];
    static zxapp_t APP[16], USED;
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], want[WAVE_ROWS];
    if (!SRC_ROOT) return;
    zxapp_host_init(&H);
    H.read_file = read_src;
    CHECK(zxapp_sutra_register(&H) == ZXAPP_OK && zxapp_kron_register(&H) == ZXAPP_OK &&
              zxapp_eve_register(&H) == ZXAPP_OK && zxapp_trebago_register(&H) == ZXAPP_OK &&
              zxapp_myra_register(&H) == ZXAPP_OK && zxapp_wasm_register(&H, &WW, 0) == ZXAPP_OK,
          "wave 0: every runtime the apps name registers");
    zxapp_registry_init(&WREG);
    int32_t len = read_src(0, "apps/blocks/used_share.zxblock", buf[15], sizeof buf[15]);
    CHECK(len > 0 && zxapp_parse(&USED, buf[15], (uint32_t) len) == ZXAPP_OK &&
              zxapp_registry_add(&WREG, &USED) == ZXAPP_OK,
          "the used_share block registers: %s", USED.err);
    static const struct {
        const char *path, *id;
        int loaded;
        uint32_t slot;
    } apps[] = {
        {"apps/banker.zxapp", "banker", 4, 1},
        {"apps/scent.zxapp", "scent", 2, 5},
        {"apps/keepsake.zxapp", "keepsake", 3, 10},
        {"apps/wake.zxapp", "wake", 2, 14},
        {"apps/tidewatch.zxapp", "tidewatch", 5, 15},
        {"apps/sounding.zxapp", "sounding", 2, 16},
        {"apps/almanac.zxapp", "almanac", 6, 17},
        {"apps/spindle.zxapp", "spindle", 1, 22},
        {"apps/quillon.zxapp", "quillon", 1, 37},
        {"apps/glint.zxapp", "glint", 1, 49},
        {"apps/murmuration.zxapp", "murmuration", 3, 61},
        {"apps/waymark.zxapp", "waymark", 2, 85},
        {"apps/platen.zxapp", "platen", 4, 115},
        {"apps/shepherd.zxapp", "shepherd", 4, 142},
    };
    enum {
        BANKER,
        SCENT,
        KEEPSAKE,
        WAKE,
        TIDEWATCH,
        SOUNDING,
        ALMANAC,
        SPINDLE,
        QUILLON,
        GLINT,
        MURMURATION,
        WAYMARK,
        PLATEN,
        SHEPHERD,
        NAPPS
    };
    zxapp_t SHARES;
    static char sbuf[4096];
    len = read_src(0, "apps/blocks/swarm_shares.zxblock", sbuf, sizeof sbuf);
    CHECK(len > 0 && zxapp_parse(&SHARES, sbuf, (uint32_t) len) == ZXAPP_OK &&
              zxapp_registry_add(&WREG, &SHARES) == ZXAPP_OK,
          "the swarm_shares block registers");
    for (int i = 0; i < NAPPS; i++) {
        int got = wave_app(&APP[i], buf[i], sizeof buf[i], apps[i].path);
        CHECK(got == apps[i].loaded, "%s: %d of %d entries load: %s", apps[i].path, got,
              apps[i].loaded, APP[i].err);
        CHECK(!strcmp(APP[i].id, apps[i].id) && APP[i].slot == apps[i].slot &&
                  APP[i].layer == ZXAPP_LAYER_L1 && APP[i].budget > 0 && APP[i].nneeds > 0 &&
                  APP[i].icon[0],
              "%s: registry fields", apps[i].id);
        char want_icon[ZXAPP_PATH_MAX];
        snprintf(want_icon, sizeof want_icon, "icons/l1/%03u-%s.svg", apps[i].slot, apps[i].id);
        CHECK(!strcmp(APP[i].icon, want_icon), "%s: icon %s", apps[i].id, APP[i].icon);
        /* C1: every L1 app is a block too, so L2 apps can "use" it */
        static zxapp_registry_t L2REG;
        if (!i) zxapp_registry_init(&L2REG);
        CHECK(zxapp_registry_add(&L2REG, &APP[i]) == ZXAPP_OK, "%s registers as a block: %s",
              apps[i].id, L2REG.err);
    }

    /* used_share (Almanac and Tidewatch): Sutra and KRON against almanac_model.c */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, i < 8 ? 3 : INT64_C(1) << 40); /* small totals reach every edge */
        b[i] = wrand(0, a[i]);
        want[i] = c_used_x100(a[i], b[i]);
    }
    a[0] = 1, b[0] = 0, want[0] = 10000;
    a[1] = 1, b[1] = 1, want[1] = 0;
    a[2] = 3, b[2] = 2, want[2] = 3333;
    zxapp_arg_t ua[2] = {{"total", a, WAVE_ROWS}, {"left", b, WAVE_ROWS}};
    CHECK(wave_same(&APP[ALMANAC], "used_share.used_x100", ua, 2, want, WAVE_ROWS, false),
          "Almanac: memory and disk in use, Sutra = KRON = C");
    CHECK(wave_same(&APP[TIDEWATCH], "used_share.used_x100", ua, 2, want, WAVE_ROWS, false),
          "Tidewatch: the same block, the same answers");
    int64_t t1[1] = {100}, l1[1] = {101};
    zxapp_arg_t over[2] = {{"total", t1, 1}, {"left", l1, 1}};
    CHECK(wave_lp(&APP[ALMANAC], "used_share.used_x100", over, 2) == ZXAPP_BOTH,
          "more left than the total: Sutra refuses, KRON answers: BOTH, held (C says unknown)");

    /* Almanac uptime: C++ (wasm) and KRON against almanac_uptime_text's d and h */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, i < 16 ? 200000 : INT64_C(10000000000));
        want[i] = a[i] / 86400;
        c[i] = a[i] % 86400 / 3600;
    }
    a[0] = 0, want[0] = 0, c[0] = 0;
    a[1] = 86399, want[1] = 0, c[1] = 23;
    a[2] = 86400, want[2] = 1, c[2] = 0;
    a[3] = 3599, want[3] = 0, c[3] = 0;
    a[4] = 3600, want[4] = 0, c[4] = 1;
    zxapp_arg_t sa[1] = {{"seconds", a, WAVE_ROWS}};
    CHECK(wave_same(&APP[ALMANAC], "days", sa, 1, want, WAVE_ROWS, false),
          "Almanac: days of uptime, C++ = KRON = C");
    CHECK(wave_same(&APP[ALMANAC], "hours", sa, 1, c, WAVE_ROWS, false),
          "Almanac: hours past the day, C++ = KRON = C");

    /* Tidewatch: zxvd's CPU share (tidewatch_model.c), and the peak */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, INT64_C(1000000000));
        b[i] = wrand(1, 1000000);
        want[i] = a[i] * 10 / b[i];
    }
    a[0] = 999, b[0] = 1000, want[0] = 9;
    zxapp_arg_t ca[2] = {{"cpu", a, WAVE_ROWS}, {"ms", b, WAVE_ROWS}};
    CHECK(wave_same(&APP[TIDEWATCH], "cpu_x100", ca, 2, want, WAVE_ROWS, false),
          "Tidewatch: CPU share, Sutra = KRON = C");
    {
        int64_t pk = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++)
            if (want[i] > pk) pk = want[i];
        zxapp_arg_t pa = {"share", want, WAVE_ROWS};
        zxapp_num_t o[2];
        uint32_t n = 0;
        CHECK(zxapp_call(&APP[TIDEWATCH], &H, "peak", &pa, 1, o, 2, &n) == ZXAPP_OK && n == 1 &&
                  o[0].num == pk,
              "Tidewatch: the graph's peak (KRON) = C");
        int64_t none[1] = {-5};
        zxapp_arg_t na = {"share", none, 1};
        CHECK(zxapp_call(&APP[TIDEWATCH], &H, "peak", &na, 1, o, 2, &n) == ZXAPP_OK &&
                  o[0].num == 0,
              "Tidewatch: no share above 0 gives a peak of 0, as C");
    }

    /* Sounding: a counter's rate, Rust (wasm) and Sutra against sounding_model.c */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(0, INT64_C(1000000000000));
        a[i] = i % 7 == 3 ? wrand(0, b[i]) : b[i] + wrand(0, INT64_C(1000000000)); /* resets */
        c[i] = i % 11 == 5 ? 0 : wrand(1, 100000);
        want[i] = c_rate(a[i], b[i], c[i]);
    }
    a[0] = 5000, b[0] = 0, c[0] = 1000, want[0] = 5000;
    a[1] = 1, b[1] = 0, c[1] = 1001, want[1] = 0;
    a[2] = 4, b[2] = 5, c[2] = 10, want[2] = -1;
    a[3] = 5, b[3] = 5, c[3] = 0, want[3] = -1;
    zxapp_arg_t ra[3] = {{"now", a, WAVE_ROWS}, {"was", b, WAVE_ROWS}, {"dt", c, WAVE_ROWS}};
    CHECK(wave_same(&APP[SOUNDING], "rate", ra, 3, want, WAVE_ROWS, false),
          "Sounding: rates and resets, Rust = Sutra = C");

    /* Wake: events at or above the view's level, KRON and Trebago against wake_filter */
    for (int round = 0; round < 32; round++) {
        uint32_t n = (uint32_t) wrand(1, 40);
        int64_t bar[1] = {wrand(0, 5)}, cnt = 0;
        for (uint32_t i = 0; i < n; i++) {
            a[i] = wrand(0, 5);
            cnt += a[i] >= bar[0];
        }
        zxapp_arg_t wa[2] = {{"level", a, n}, {"bar", bar, 1}};
        CHECK(wave_same(&APP[WAKE], "count", wa, 2, &cnt, 1, false),
              "Wake: round %d, KRON = Trebago = C", round);
    }

    /* Keepsake: what a prune removes (zks_prune), and what it may keep */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 50);
        b[i] = wrand(1, 50);
        want[i] = a[i] > b[i] ? a[i] - b[i] : 0;
    }
    a[0] = 5, b[0] = 5, want[0] = 0;
    a[1] = 6, b[1] = 5, want[1] = 1;
    zxapp_arg_t ka[2] = {{"snapshots", a, WAVE_ROWS}, {"keep", b, WAVE_ROWS}};
    CHECK(wave_same(&APP[KEEPSAKE], "removed", ka, 2, want, WAVE_ROWS, false),
          "Keepsake: backups a prune removes, Sutra = KRON = C");
    {
        int64_t k[3] = {0, 1, 4294967295}, w[3] = {ZXAPP_FALSE, ZXAPP_TRUE, ZXAPP_TRUE};
        zxapp_arg_t kk = {"keep", k, 3};
        CHECK(wave_same(&APP[KEEPSAKE], "keep_ok", &kk, 1, w, 3, true),
              "Keepsake: a prune may never keep none (Eve)");
    }

    /* Platen: sheets for a job, and copies the printer takes (ipp.c copies-supported) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 10000);
        b[i] = wrand(1, 2);
        c[i] = wrand(1, 99);
        want[i] = (a[i] + b[i] - 1) / b[i] * c[i];
    }
    a[0] = 3, b[0] = 2, c[0] = 1, want[0] = 2;
    a[1] = 4, b[1] = 2, c[1] = 3, want[1] = 6;
    a[2] = 1, b[2] = 1, c[2] = 1, want[2] = 1;
    zxapp_arg_t pa3[3] = {
        {"pages", a, WAVE_ROWS}, {"sides", b, WAVE_ROWS}, {"copies", c, WAVE_ROWS}};
    CHECK(wave_same(&APP[PLATEN], "sheets", pa3, 3, want, WAVE_ROWS, false),
          "Platen: sheets of paper, Sutra = KRON = C");
    {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = (int64_t) i * 2 - 8; /* -8 .. 118 */
            b[i] = i % 3 ? 99 : wrand(1, 999);
            want[i] = T_(a[i] >= 1 && a[i] <= b[i]);
        }
        a[63] = 99, b[63] = 99, want[63] = ZXAPP_TRUE;
        a[62] = 100, b[62] = 99, want[62] = ZXAPP_FALSE;
        zxapp_arg_t co[2] = {{"copies", a, WAVE_ROWS}, {"most", b, WAVE_ROWS}};
        CHECK(wave_same(&APP[PLATEN], "copies_ok", co, 2, want, WAVE_ROWS, true),
              "Platen: copies the printer takes, Sutra = KRON = C");
    }

    /* Shepherd: minutes left (zxv_screen.c apply), the limit's rule, the week */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1440);
        b[i] = wrand(0, 1440);
        c[i] = wrand(0, 3000);
        want[i] = c_left(a[i], b[i], c[i]);
    }
    a[0] = 60, b[0] = 0, c[0] = 60, want[0] = 0;
    a[1] = 60, b[1] = 15, c[1] = 60, want[1] = 15;
    a[2] = 0, b[2] = 0, c[2] = 0, want[2] = 0;
    zxapp_arg_t la[3] = {
        {"minutes", a, WAVE_ROWS}, {"extra", b, WAVE_ROWS}, {"used", c, WAVE_ROWS}};
    CHECK(wave_same(&APP[SHEPHERD], "left", la, 3, want, WAVE_ROWS, false),
          "Shepherd: minutes left today, Sutra = KRON = zxvd's rule");
    {
        int64_t m[6] = {-2, -1, 0, 1440, 1441, 30}, f[6] = {0, 0, 1439, 0, 0, 1440},
                t[6] = {0, 1439, 0, 0, 0, 0},
                w[6] = {ZXAPP_FALSE, ZXAPP_TRUE, ZXAPP_TRUE, ZXAPP_TRUE, ZXAPP_FALSE, ZXAPP_FALSE};
        zxapp_arg_t ea[3] = {{"minutes", m, 6}, {"from", f, 6}, {"to", t, 6}};
        CHECK(wave_same(&APP[SHEPHERD], "limit_ok", ea, 3, w, 6, true),
              "Shepherd: a limit's rule at its edges (Eve, as zxv_screen.c)");
        int64_t days[7] = {30, 0, 1440, 45, 12, 0, 7}, wk = 1534;
        zxapp_arg_t da = {"minutes", days, 7};
        zxapp_num_t o[2];
        uint32_t n = 0;
        CHECK(zxapp_call(&APP[SHEPHERD], &H, "week", &da, 1, o, 2, &n) == ZXAPP_OK && n == 1 &&
                  o[0].num == wk && o[0].den == 1,
              "Shepherd: the week's minutes (Trebago)");
    }

    /* Waymark: a place on the globe (zxv_place.c), Eve and KRON */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-100000000, 100000000);
        b[i] = wrand(-200000000, 200000000);
    }
    a[0] = 90000000, b[0] = 180000000;
    a[1] = -90000000, b[1] = -180000000;
    a[2] = 90000001, b[2] = 0;
    a[3] = 0, b[3] = -180000001;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] =
            T_(a[i] >= -90000000 && a[i] <= 90000000 && b[i] >= -180000000 && b[i] <= 180000000);
    zxapp_arg_t ga[2] = {{"lat", a, WAVE_ROWS}, {"lon", b, WAVE_ROWS}};
    CHECK(wave_same(&APP[WAYMARK], "place_ok", ga, 2, want, WAVE_ROWS, true),
          "Waymark: places on the globe, Eve = KRON = C");

    /* Scent: a hit's score, KRON and Trebago against zxv_search.c */
    for (int round = 0; round < 32; round++) {
        uint32_t n = (uint32_t) wrand(1, 8);
        int64_t s = 0;
        for (uint32_t i = 0; i < n; i++) {
            a[i] = wrand(0, 1);
            b[i] = wrand(0, 1);
            c[i] = wrand(0, 300);
            s += a[i] ? 1000 : b[i] ? 100 : 0;
            s += c[i] > 99 ? 99 : c[i];
        }
        zxapp_arg_t sc[3] = {{"name", a, n}, {"folder", b, n}, {"count", c, n}};
        CHECK(wave_same(&APP[SCENT], "score", sc, 3, &s, 1, false),
              "Scent: round %d, KRON = Trebago = C", round);
    }

    /* The Banker: entry shares, the folder's total, the entry record */
    {
        uint32_t n = 20;
        int64_t tot = 0;
        for (uint32_t i = 0; i < n; i++) tot += a[i] = wrand(0, INT64_C(1) << 36);
        int64_t total[1] = {tot};
        for (uint32_t i = 0; i < n; i++) want[i] = a[i] * 10000 / tot;
        zxapp_arg_t sh[2] = {{"size", a, n}, {"total", total, 1}};
        CHECK(wave_same(&APP[BANKER], "share", sh, 2, want, n, false),
              "Banker: each entry's share, KRON = C++ = C");
        int64_t z[2] = {0, 0}, zt[1] = {0}, zw[2] = {0, 0};
        zxapp_arg_t ze[2] = {{"size", z, 2}, {"total", zt, 1}};
        CHECK(wave_same(&APP[BANKER], "share", ze, 2, zw, 2, false),
              "Banker: an empty folder's shares are 0, not a division by zero");
        int64_t big[1] = {11}, small[1] = {10};
        zxapp_arg_t bs[2] = {{"size", big, 1}, {"total", small, 1}};
        CHECK(wave_lp(&APP[BANKER], "share", bs, 2) == ZXAPP_BOTH,
              "Banker: an entry bigger than its folder: BOTH, held");
        zxapp_arg_t ta = {"size", a, n};
        zxapp_num_t o[2];
        uint32_t k = 0;
        CHECK(zxapp_call(&APP[BANKER], &H, "total", &ta, 1, o, 2, &k) == ZXAPP_OK && k == 1 &&
                  o[0].num == tot && o[0].den == 1,
              "Banker: the folder's total (Trebago)");
        int64_t f[1] = {3}, sq[2] = {1, 2}, sz[2] = {INT64_C(999999999999999999), 12},
                mt[2] = {1760000000, 0};
        zxapp_arg_t ea[4] = {{"folder", f, 1}, {"seq", sq, 2}, {"size", sz, 2}, {"mtime", mt, 2}};
        int64_t ok2[2] = {ZXAPP_TRUE, ZXAPP_TRUE};
        CHECK(wave_same(&APP[BANKER], "entry_ok", ea, 4, ok2, 2, true),
              "Banker: entries fit ^ZXHOARD (MYRA)");
        int64_t mbad[1] = {INT64_C(1000000000000)};
        zxapp_arg_t eb[4] = {{"folder", f, 1}, {"seq", sq, 1}, {"size", sz, 1}, {"mtime", mbad, 1}};
        int64_t no[1] = {ZXAPP_FALSE};
        CHECK(wave_same(&APP[BANKER], "entry_ok", eb, 4, no, 1, true),
              "Banker: a 13-digit time does not fit i12");
    }

    /* Spindle, Quillon, Glint: the limits their models hold to */
    {
        int64_t p[4] = {0, 4096, 4097, 10}, r[4] = {0, 8192, 0, 8193},
                w[4] = {ZXAPP_TRUE, ZXAPP_TRUE, ZXAPP_FALSE, ZXAPP_FALSE};
        zxapp_arg_t pa2[2] = {{"params", p, 4}, {"reply", r, 4}};
        CHECK(wave_same(&APP[SPINDLE], "call_ok", pa2, 2, w, 4, true),
              "Spindle: SPINDLE_PARAMS_MAX and SPINDLE_REPLY_MAX (Eve)");
        int64_t q[3] = {0, 1048576, 1048577}, qw[3] = {ZXAPP_TRUE, ZXAPP_TRUE, ZXAPP_FALSE};
        zxapp_arg_t qa = {"bytes", q, 3};
        CHECK(wave_same(&APP[QUILLON], "size_ok", &qa, 1, qw, 3, true),
              "Quillon: QUILLON_TEXT_MAX (Eve)");
        int64_t f[1] = {1}, sq[2] = {1, 2}, k[2] = {3, 1000}, sz[2] = {5, 5},
                gw[2] = {ZXAPP_TRUE, ZXAPP_FALSE};
        zxapp_arg_t ia[4] = {{"folder", f, 1}, {"seq", sq, 2}, {"kind", k, 2}, {"size", sz, 2}};
        CHECK(wave_same(&APP[GLINT], "item_ok", ia, 4, gw, 2, true),
              "Glint: a media item fits ^ZXGLINT (MYRA)");
    }
    for (int i = 0; i < NAPPS; i++) zxapp_unload(&APP[i], &H);
    app_tests_run(APP, NAPPS);
    zxapp_wasm_close(&WW);
}

/* ---- W: circuits ---------------------------------------------------------- */

static zxwire_t WIRE;

static const char PAY[] = "zxapp 1\nid pay\nname Pay\nversion 1.0.0\n"
                          "provides fee num amount rate\n"
                          "logic kron fee <<\n50|_amount*rate%10000\n.\n"
                          "provides fee_ok truth amount fee\n"
                          "logic kron fee_ok <<\n(amount>0)&fee<2501\n.\n"
                          "provides total num amount fee\n"
                          "logic kron total <<\n+/amount+fee\n.\n"
                          "provides half num x\n"
                          "logic kron half <<\nx%2\n.\n"
                          "provides dbl num x\n"
                          "logic kron dbl <<\nx*2\n.\n"
                          "logic python dbl <<\n.\n"
                          "provides odd num x\n"
                          "logic kron odd <<\nx*3\n.\n"
                          "logic python odd <<\n.\n"
                          "provides pay_ok truth amount rate\n"
                          "logic wire pay_ok <<\n"
                          "part f  fee    amount=amount rate=rate\n"
                          "part ok fee_ok amount=amount fee=f   # wired to the fee\n"
                          "out ok\n"
                          "at amount 20 40\nat f 160 80\nat ok 400 80\nat out 600 80\n.\n"
                          "provides due num amount\n"
                          "logic wire due <<\n"
                          "part f fee amount=amount rate=30\n"
                          "part t total fee=f amount=amount\n"
                          "out t\n.\n"
                          "provides bad_kind num amount\n"
                          "logic wire bad_kind <<\n"
                          "part ok fee_ok amount=amount fee=5\n"
                          "part t total amount=amount fee=ok\n"
                          "out t\n.\n"
                          "provides frac num x\n"
                          "logic wire frac <<\n"
                          "part h half x=x\n"
                          "part t total amount=x fee=h\n"
                          "out t\n.\n"
                          "provides wrong_out num amount\n"
                          "logic wire wrong_out <<\n"
                          "part ok fee_ok amount=amount fee=1\n"
                          "out ok\n.\n"
                          "provides witnessed num x\n"
                          "logic wire witnessed <<\npart d dbl x=x\nout d\n.\n"
                          "provides split num x\n"
                          "logic wire split <<\n"
                          "part o odd x=x\npart t total amount=x fee=o\nout t\n.\n"
                          "provides nest truth amount\n"
                          "logic wire nest <<\n"
                          "part p pay_ok amount=amount rate=1000\nout p\n.\n"
                          "provides zero num x\n"
                          "logic kron zero <<\nx%0\n.\n"
                          "provides refuse num x\n"
                          "logic wire refuse <<\npart z zero x=x\nout z\n.\n"
                          "provides setting num\n"
                          "logic wire setting <<\nout 4,5,-6\n.\n";

static void wire_parse_err(const char *src, const char *want, uint32_t line)
{
    zxwire_circuit_t c;
    zxapp_provide_t P = {"x", ZXAPP_KIND_NUM, {"a", "b"}, 2, 1};
    int rc = zxwire_parse(&c, &P, src, (uint32_t) strlen(src));
    CHECK(rc == ZXAPP_E_SYNTAX && strstr(c.err, want) && c.err_line == line,
          "circuit refused: \"%s\" -> %s (line %u)", src, c.err, c.err_line);
}

static void test_wire(void)
{
    /* W2/W3/W5: the text */
    zxwire_circuit_t c;
    zxapp_provide_t P = {"x", ZXAPP_KIND_TRUTH, {"a", "b"}, 2, 1};
    const char *ok = "part f blk.fee amount=a rate=25\n"
                     "\tpart g fee_ok amount=b fee=f   # comment\n"
                     "# a comment line\n\n"
                     "out g\r\nat f 10 20\nat a 0 0\nat out 300 40\n";
    CHECK(zxwire_parse(&c, &P, ok, (uint32_t) strlen(ok)) == ZXAPP_OK, "a circuit parses: %s",
          c.err);
    CHECK(c.ninputs == 2 && c.kind == ZXAPP_KIND_TRUTH && c.nparts == 2, "inputs, kind, parts");
    CHECK(strcmp(c.part[0].name, "f") == 0 && strcmp(c.part[0].entry, "blk.fee") == 0 &&
              c.part[0].nports == 2 && strcmp(c.part[0].port[1], "rate") == 0,
          "part f");
    CHECK(c.part[0].src[0].kind == ZXWIRE_SRC_INPUT && c.part[0].src[0].index == 0, "amount=a");
    CHECK(c.part[0].src[1].kind == ZXWIRE_SRC_CONST && c.part[0].src[1].nconst == 1 &&
              c.part[0].src[1].konst[0] == 25,
          "rate=25 is a setting");
    CHECK(c.part[1].src[0].kind == ZXWIRE_SRC_INPUT && c.part[1].src[0].index == 1 &&
              c.part[1].src[1].kind == ZXWIRE_SRC_PART && c.part[1].src[1].index == 0,
          "part g takes b and f");
    CHECK(c.out.kind == ZXWIRE_SRC_PART && c.out.index == 1, "out g");
    CHECK(c.part[0].placed && c.part[0].x == 10 && c.part[0].y == 20 && !c.part[1].placed,
          "at f 10 20");
    CHECK(c.in_placed[0] && !c.in_placed[1] && c.out_placed && c.out_x == 300 && c.out_y == 40,
          "inputs and out are placed too");
    CHECK(zxwire_parse(&c, &P, "out 1,-2,3\n", 11) == ZXAPP_OK && c.out.kind == ZXWIRE_SRC_CONST &&
              c.out.nconst == 3 && c.out.konst[1] == -2,
          "a setting of three");
    CHECK(zxwire_parse(&c, 0, "out a\n", 6) == ZXAPP_E_SYNTAX && strstr(c.err, "provides"),
          "a circuit needs its contract");
    wire_parse_err("", "needs an out", 0);
    wire_parse_err("part f e x=a\n", "needs an out", 1);
    wire_parse_err("out a\nout b\n", "one out", 2);
    wire_parse_err("out\n", "out needs", 1);
    wire_parse_err("out a b\n", "out needs", 1);
    wire_parse_err("out z\n", "unknown source", 1);
    wire_parse_err("wire f\n", "part, out or at", 1);
    wire_parse_err("part f\n", "part needs", 1);
    wire_parse_err("part 1f e\nout a\n", "short identifier", 1);
    wire_parse_err("part out e\nout a\n", "not \"out\"", 1);
    wire_parse_err("part a e\nout a\n", "used twice", 1);
    wire_parse_err("part f e\npart f e\nout a\n", "used twice", 2);
    wire_parse_err("part f e..x\nout a\n", "block.entry", 1);
    wire_parse_err("part f e x\nout a\n", "<input>=<source>", 1);
    wire_parse_err("part f e x=a x=b\nout a\n", "wired twice", 1);
    wire_parse_err("part f e 2=a\nout a\n", "input's name", 1);
    wire_parse_err("part f e x=g\npart g e y=a\nout f\n", "unknown source", 1);
    wire_parse_err("part f e x=f\nout f\n", "earlier parts", 1);
    wire_parse_err("part f e x=1,2,3,4,5,6,7,8,9\nout f\n", "up to 8", 1);
    wire_parse_err("part f e x=1,,2\nout f\n", "whole numbers", 1);
    wire_parse_err("part f e x=1x\nout f\n", "whole numbers", 1);
    wire_parse_err("part f e x=1234567890123456789\nout f\n", "whole numbers", 1);
    wire_parse_err("part f e a=a b=a c=a d=a e=a f=a g=a h=a i=a\nout f\n", "at most 8", 1);
    wire_parse_err("out a\nat a 1\n", "at needs", 2);
    wire_parse_err("out a\nat a 1 2 3\n", "at needs", 2);
    wire_parse_err("out a\nat a -1 2\n", "at needs", 2);
    wire_parse_err("out a\nat a 1 100001\n", "at needs", 2);
    wire_parse_err("out a\nat z 1 2\n", "at names", 2);
    {
        char many[1024] = "";
        for (int i = 0; i < 17; i++) {
            char l[32];
            snprintf(l, sizeof l, "part p%d e x=a\n", i);
            strcat(many, l);
        }
        strcat(many, "out a\n");
        wire_parse_err(many, "at most 16", 17);
    }
    char full[ZXAPP_ENTRY_MAX];
    CHECK(zxwire_resolve("total", "swarm.share", full) && strcmp(full, "swarm.share") == 0,
          "an app's own circuit names parts as written");
    CHECK(zxwire_resolve("dash.total", "swarm.share", full) &&
              strcmp(full, "dash.swarm.share") == 0,
          "a composed circuit finds its parts under its app (W2)");
    char deep[ZXAPP_ENTRY_MAX];
    memset(deep, 'a', sizeof deep - 1);
    deep[sizeof deep - 1] = 0;
    CHECK(zxwire_resolve("b", deep, full) && !zxwire_resolve("a.b", deep, full),
          "too long to resolve");

    /* W1/W4: running */
    zxapp_host_init(&H);
    zxapp_kron_register(&H);
    zxapp_runtime_t py = {ZXAPP_LANG_PYTHON, 0, f_load, f_call, 0};
    zxapp_host_register(&H, &py);
    CHECK(zxapp_lang_of("wire") == ZXAPP_LANG_WIRE && zxapp_lang_of("Circuit") == ZXAPP_LANG_WIRE,
          "wire, or circuit");
    CHECK(parse(PAY) == ZXAPP_OK, "pay parses: %s", A.err);
    int loaded = zxapp_load(&A, &H);
    CHECK(loaded == 9, "without the hook circuits stay unloaded (%d)", loaded);
    int64_t amt[3] = {10000, 500000, 0}, rate[3] = {30, 30, 30};
    zxapp_arg_t pa[2] = {{"amount", amt, 3}, {"rate", rate, 3}};
    zxapp_num_t out[8];
    uint32_t n = 0;
    int32_t lp = -1;
    CHECK(zxapp_call(&A, &H, "pay_ok", pa, 2, out, 8, &n) == ZXAPP_E_NORUNTIME,
          "no hook: no runtime, like a missing language");
    zxapp_unload(&A, &H);
    zxwire_register(&H, &WIRE);
    loaded = zxapp_load(&A, &H);
    CHECK(loaded == 19, "with the hook every circuit loads (%d): %s", loaded, A.err);
    CHECK(zxapp_call_witnessed(&A, &H, "pay_ok", pa, 2, out, 8, &n, &lp) == ZXAPP_OK && n == 3 &&
              out[0].den == 0 && out[0].num == ZXAPP_TRUE && out[1].num == ZXAPP_TRUE &&
              out[2].num == ZXAPP_FALSE && lp == ZXAPP_NEITHER,
          "pay_ok: fee then fee_ok; one language each, so NEITHER (rc lp %d n %u: %s)", lp, n,
          A.err);
    int64_t one[1] = {10000};
    zxapp_arg_t da[1] = {{"amount", one, 1}};
    CHECK(zxapp_call(&A, &H, "due", da, 1, out, 8, &n) == ZXAPP_OK && n == 1 &&
              out[0].num == 10050 && out[0].den == 1,
          "due: a setting (rate=30) and two parts, wired in any order: %s", A.err);
    zxapp_arg_t da3[1] = {{"amount", amt, 3}};
    CHECK(zxapp_call(&A, &H, "due", da3, 1, out, 8, &n) == ZXAPP_OK && n == 1 &&
              out[0].num == 10000 + 500000 + 0 + 50 + 1500 + 50,
          "W2: one setting is the same for every row: %s", A.err);
    CHECK(zxapp_call_witnessed(&A, &H, "bad_kind", da, 1, out, 8, &n, &lp) == ZXAPP_E_CONFLICT &&
              lp == ZXAPP_BOTH && strstr(A.err, "a truth from ok is wired into t.fee"),
          "W4: a truth into a number input is held, never converted: %s", A.err);
    int64_t three[1] = {3};
    zxapp_arg_t xa[1] = {{"x", three, 1}};
    CHECK(zxapp_call(&A, &H, "frac", xa, 1, out, 8, &n) == ZXAPP_E_CONFLICT &&
              strstr(A.err, "h answered a fraction"),
          "W4: a fraction into an input is held: %s", A.err);
    int64_t four[1] = {4};
    zxapp_arg_t x4[1] = {{"x", four, 1}};
    CHECK(zxapp_call(&A, &H, "frac", x4, 1, out, 8, &n) == ZXAPP_OK && out[0].num == 6,
          "a whole half passes: 4 + 2");
    CHECK(zxapp_call_witnessed(&A, &H, "wrong_out", da, 1, out, 8, &n, &lp) == ZXAPP_E_CONFLICT &&
              lp == ZXAPP_BOTH && strstr(A.err, "out is wired to a truth"),
          "W4: out of the wrong kind is held: %s", A.err);
    CHECK(zxapp_call_witnessed(&A, &H, "witnessed", xa, 1, out, 8, &n, &lp) == ZXAPP_OK &&
              lp == ZXAPP_TRUE && out[0].num == 6,
          "every part witnessed (KRON = python): TRUE");
    CHECK(zxapp_call_witnessed(&A, &H, "split", xa, 1, out, 8, &n, &lp) == ZXAPP_E_CONFLICT &&
              lp == ZXAPP_BOTH && strstr(A.err, "held at part o"),
          "a part whose languages disagree holds the circuit there: %s", A.err);
    CHECK(zxapp_call(&A, &H, "split", xa, 1, out, 8, &n) == ZXAPP_E_CONFLICT,
          "held through zxapp_call too");
    int64_t big[1] = {500000};
    zxapp_arg_t na[1] = {{"amount", big, 1}};
    CHECK(zxapp_call_witnessed(&A, &H, "nest", da, 1, out, 8, &n, &lp) == ZXAPP_OK && n == 1 &&
              out[0].num == ZXAPP_TRUE && lp == ZXAPP_NEITHER &&
              zxapp_call_witnessed(&A, &H, "nest", na, 1, out, 8, &n, &lp) == ZXAPP_OK &&
              out[0].num == ZXAPP_FALSE,
          "a circuit uses a circuit: rate 1000 makes 500000's fee too high: %s", A.err);
    CHECK(zxapp_call(&A, &H, "setting", 0, 0, out, 8, &n) == ZXAPP_OK && n == 3 &&
              out[2].num == -6 && out[2].den == 1,
          "out can be a setting");
    CHECK(zxapp_call(&A, &H, "setting", 0, 0, out, 2, &n) == ZXAPP_E_BUFFER && n == 3,
          "more than fit");
    zxapp_arg_t wrong[1] = {{"rate", one, 1}};
    CHECK(zxapp_call(&A, &H, "due", wrong, 1, out, 8, &n) == ZXAPP_E_CONTRACT,
          "B3: a circuit's own contract is checked before it runs");
    CHECK(zxapp_call(&A, &H, "refuse", xa, 1, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "refuse: part z: "),
          "a part's refusal is named: %s", A.err);
    CHECK(WIRE.depth == 0, "the working space is released after every run");
    zxapp_unload(&A, &H);

    /* load refusals */
    static const struct {
        const char *m, *want;
    } refuse[] = {
        {"zxapp 1\nid r\nprovides c num\nlogic wire c apps/c.wire\n", "inline"},
        {"zxapp 1\nid r\nlogic wire c <<\nout 1\n.\n", "provides"},
        {"zxapp 1\nid r\nprovides c num\nlogic wire c <<\npart p nope\nout p\n.\n",
         "no entry for part p"},
        {"zxapp 1\nid r\nprovides c num\nlogic wire c <<\npart p c\nout p\n.\n", "its own part"},
        {"zxapp 1\nid r\nlogic kron k <<\n1\n.\nprovides c num\nlogic wire c <<\npart p k\nout "
         "p\n.\n",
         "no provides line"},
        {"zxapp 1\nid r\nprovides k num x\nlogic kron k <<\nx\n.\nprovides c num\nlogic wire c "
         "<<\npart p k y=1\nout p\n.\n",
         "no input y"},
        {"zxapp 1\nid r\nprovides k num x y\nlogic kron k <<\nx+y\n.\nprovides c num\nlogic wire "
         "c <<\npart p k x=1\nout p\n.\n",
         "not wired"},
        {"zxapp 1\nid r\nprovides c num\nlogic wire c <<\nout q\n.\n", "unknown source"},
    };
    for (unsigned i = 0; i < sizeof refuse / sizeof refuse[0]; i++) {
        CHECK(parse(refuse[i].m) == ZXAPP_OK && zxapp_load(&A, &H) < 0 &&
                  strstr(A.err, refuse[i].want),
              "load refuses %u: %s", i, A.err);
        zxapp_unload(&A, &H);
    }

    /* W2 through composing: a circuit exported by an app, used by another */
    static const char DASH[] = "zxapp 1\nid dash\nversion 1.0.0\n"
                               "use shares 1\n"
                               "provides shares.share num re\n"
                               "provides top num re\n"
                               "logic wire top <<\npart s shares.share re=re\nout s\n.\n";
    static const char HOME[] = "zxapp 1\nid home\nuse dash 1\n"
                               "provides mine num re\n"
                               "logic wire mine <<\npart t dash.top re=re\nout t\n.\n";
    static zxapp_t SH, DA, HO;
    zxapp_registry_t reg;
    zxapp_registry_init(&reg);
    CHECK(parse_into(&SH, SHARES_100) == ZXAPP_OK && zxapp_registry_add(&reg, &SH) == ZXAPP_OK,
          "shares");
    CHECK(parse_into(&DA, DASH) == ZXAPP_OK && zxapp_compose(&DA, &reg) == ZXAPP_OK &&
              zxapp_registry_add(&reg, &DA) == ZXAPP_OK,
          "dash exports a circuit: %s %s", DA.err, reg.err);
    CHECK(parse_into(&HO, HOME) == ZXAPP_OK && zxapp_compose(&HO, &reg) == ZXAPP_OK,
          "home uses dash: %s", HO.err);
    CHECK(zxapp_load(&HO, &H) >= 3, "home loads, its circuit and dash's: %s", HO.err);
    int64_t re[3] = {1, 1, 2};
    zxapp_arg_t ra[1] = {{"re", re, 3}};
    CHECK(zxapp_call(&HO, &H, "mine", ra, 1, out, 8, &n) == ZXAPP_OK && n == 3 &&
              out[0].num == 250 && out[2].num == 500,
          "home.mine -> dash.top -> dash.shares.share: %s", HO.err);
    zxapp_origin_t org[4];
    uint32_t parts = zxapp_provenance(&HO, org, 4);
    CHECK(parts == 3, "provenance names home, dash and shares (%u)", parts);
    zxapp_unload(&HO, &H);
}

/* ---- CB: the Circuit board -------------------------------------------------- */

static const char FEES[] = "zxblock 1\nid fees\nname Fees\nversion 1.2.0\n"
                           "provides fee num amount rate\n"
                           "logic kron fee <<\n50|_amount*rate%10000\n.\n"
                           "provides fee_ok truth amount fee\n"
                           "logic kron fee_ok <<\n(amount>0)&fee<2501\n.\n"
                           "provides total num amount fee\n"
                           "logic kron total <<\n+/amount+fee\n.\n";

static zxboard_t BOARD, BOARD2;

static void test_board(void)
{
    static zxapp_t FB, SB;
    zxapp_registry_t reg;
    zxapp_registry_init(&reg);
    CHECK(parse_into(&FB, FEES) == ZXAPP_OK && zxapp_registry_add(&reg, &FB) == ZXAPP_OK, "fees");
    CHECK(parse_into(&SB, SHARES_100) == ZXAPP_OK && zxapp_registry_add(&reg, &SB) == ZXAPP_OK,
          "shares");
    zxboard_t *b = &BOARD;
    CHECK(zxboard_init(b, &reg, "1bad", 0) == ZXAPP_E_ARG, "an id is an identifier");
    CHECK(zxboard_init(b, &reg, "checkout", "Checkout") == ZXAPP_OK, "a new board");

    /* CB1 */
    CHECK(zxboard_shelf_count(b) == 5, "the shelf: 3 fees + 2 shares entries");
    zxboard_item_t it;
    CHECK(zxboard_shelf(b, 0, &it) && strcmp(it.ref, "fees.fee") == 0 && it.from == &FB &&
              it.prov->ninputs == 2,
          "fees.fee");
    CHECK(zxboard_shelf(b, 4, &it) && strcmp(it.ref, "shares.pos") == 0 &&
              it.prov->kind == ZXAPP_KIND_TRUTH,
          "shares.pos is a truth");
    CHECK(!zxboard_shelf(b, 5, &it), "past the end");
    CHECK(zxboard_contract(b, "fees.nope", 0) == 0 && zxboard_contract(b, "fees", 0) == 0 &&
              zxboard_contract(b, "nope.fee", 0) == 0,
          "only what is on the shelf");

    /* CB2 */
    CHECK(zxboard_input_add(b, "amount") == 0 && zxboard_input_add(b, "rate") == 1, "inputs");
    CHECK(zxboard_input_add(b, "amount") == ZXAPP_E_NAME, "a name used twice");
    CHECK(zxboard_input_add(b, "out") == ZXAPP_E_NAME, "out is the answer's name");
    CHECK(zxboard_input_add(b, "2x") == ZXAPP_E_ARG, "an identifier");
    int f = zxboard_part_add(b, "fees.fee", 240, 80);
    int ok = zxboard_part_add(b, "fees.fee_ok", 480, 80);
    int f2 = zxboard_part_add(b, "fees.fee", 240, 260);
    CHECK(f == 0 && ok == 1 && f2 == 2, "three parts");
    CHECK(strcmp(b->c.part[2].name, "fee2") == 0, "named after the entry, made unique: %s",
          b->c.part[2].name);
    CHECK(zxboard_part_add(b, "fees.nope", 0, 0) == ZXAPP_E_NAME, "not on the shelf");
    CHECK(zxboard_part_add(b, "fees.fee", -1, 0) == ZXAPP_E_ARG, "off the board");
    CHECK(b->c.part[0].nports == 2 && strcmp(b->c.part[0].port[1], "rate") == 0,
          "ports from the contract");
    zxboard_check_t k;
    zxboard_check(b, &k);
    CHECK(k.lp == ZXAPP_NEITHER && k.open == 7 && k.held == 0 && strstr(k.why, "not wired"),
          "CB3: nothing wired yet: NEITHER (%u open): %s", k.open, k.why);
    char text[ZXBOARD_TEXT];
    CHECK(zxboard_save(b, text, sizeof text) == ZXAPP_E_CONTRACT && strstr(b->err, "not saved"),
          "CB4: an open wire is not saved");
    CHECK(zxboard_part_remove(b, (uint32_t) f2) == ZXAPP_OK && b->c.nparts == 2, "remove fee2");
    zxwire_src_t s;
    s = zxboard_src_input(0);
    CHECK(zxboard_wire(b, 0, 0, &s) == ZXAPP_OK, "fee.amount <- amount");
    s = zxboard_src_input(1);
    CHECK(zxboard_wire(b, 0, 1, &s) == ZXAPP_OK, "fee.rate <- rate");
    s = zxboard_src_input(0);
    CHECK(zxboard_wire(b, 1, 0, &s) == ZXAPP_OK, "fee_ok.amount <- amount");
    s = zxboard_src_part(1);
    CHECK(zxboard_wire(b, 1, 1, &s) == ZXAPP_E_ARG, "a part cannot take its own answer");
    s = zxboard_src_part(5);
    CHECK(zxboard_wire(b, 1, 1, &s) == ZXAPP_E_ARG, "no such part");
    CHECK(zxboard_wire(b, 1, 9, &s) == ZXAPP_E_ARG, "no such port");
    s = zxboard_src_part(0);
    CHECK(zxboard_wire(b, 1, 1, &s) == ZXAPP_OK, "fee_ok.fee <- fee");
    s = zxboard_src_part(1);
    CHECK(zxboard_out(b, &s) == ZXAPP_OK, "the answer <- fee_ok");
    zxboard_check(b, &k);
    CHECK(k.lp == ZXAPP_BOTH && k.held == 1 && k.out == ZXWIRE_HELD &&
              strstr(k.why, "a truth, and the app answers numbers"),
          "CB3: a truth answer on a numbers app is BOTH, shown, not converted: %s", k.why);
    CHECK(zxboard_answer_kind(b, 7) == ZXAPP_E_ARG &&
              zxboard_answer_kind(b, ZXAPP_KIND_TRUTH) == ZXAPP_OK,
          "the app answers a truth");
    zxboard_check(b, &k);
    CHECK(k.lp == ZXAPP_TRUE && k.ok == 5 && !k.why[0], "every wire TRUE (%u ok): %s", k.ok, k.why);

    /* CB4 */
    int32_t len = zxboard_save(b, text, sizeof text);
    static const char WANT[] = "zxapp 1\nid checkout\nname Checkout\nversion 1.0.0\nview auto\n"
                               "use fees 1.2\n"
                               "provides fees.fee num amount rate\n"
                               "provides fees.fee_ok truth amount fee\n"
                               "provides main truth amount rate\n"
                               "logic wire main <<\n"
                               "part fee fees.fee amount=amount rate=rate\n"
                               "part fee_ok fees.fee_ok amount=amount fee=fee\n"
                               "out fee_ok\n"
                               "at amount 40 80\nat rate 40 176\n"
                               "at fee 240 80\nat fee_ok 480 80\nat out 600 200\n.\n";
    CHECK(len > 0 && strcmp(text, WANT) == 0, "CB4: the manifest:\n%s", len > 0 ? text : b->err);
    CHECK(zxboard_save(b, text, 40) == ZXAPP_E_BUFFER, "too small");

    /* the saved app is an ordinary app: it parses, composes, loads, runs and nests */
    zxapp_host_init(&H);
    zxapp_kron_register(&H);
    zxwire_register(&H, &WIRE);
    static char app_text[ZXBOARD_TEXT]; /* A4: outlives APP */
    len = zxboard_save(b, app_text, sizeof app_text);
    static zxapp_t APP;
    CHECK(parse_into(&APP, app_text) == ZXAPP_OK && zxapp_compose(&APP, &reg) == ZXAPP_OK &&
              zxapp_load(&APP, &H) == 4,
          "the saved app loads: %s", APP.err);
    int64_t amt[2] = {10000, 9000000}, rate[2] = {30, 30};
    zxapp_arg_t args[2] = {{"amount", amt, 2}, {"rate", rate, 2}};
    zxapp_num_t out[8];
    uint32_t n = 0;
    int32_t lp = -1;
    CHECK(zxapp_call_witnessed(&APP, &H, "main", args, 2, out, 8, &n, &lp) == ZXAPP_OK && n == 2 &&
              out[0].num == ZXAPP_TRUE && out[1].num == ZXAPP_FALSE,
          "checkout.main: %s", APP.err);
    zxapp_unload(&APP, &H);
    CHECK(zxapp_registry_add(&reg, &APP) == ZXAPP_OK, "C1: the board's app is a block too: %s",
          reg.err);
    zxapp_origin_t org[4];
    CHECK(zxapp_provenance(&APP, org, 4) == 2 && org[1].from == &FB,
          "C2: its parts are credited to fees");

    /* CB6 */
    n = 0;
    CHECK(zxboard_try(b, &H, args, 2, out, 8, &n, &lp) == ZXAPP_OK && n == 2 &&
              out[1].num == ZXAPP_FALSE && lp == ZXAPP_NEITHER,
          "CB6: try it before keeping it: %s", b->err);

    /* CB5 */
    zxboard_t *b2 = &BOARD2;
    CHECK(zxboard_open(b2, &reg, &APP, "main") == ZXAPP_OK, "open it again: %s", b2->err);
    char again[ZXBOARD_TEXT];
    CHECK(zxboard_save(b2, again, sizeof again) == len && strcmp(again, app_text) == 0,
          "CB5: the same text, so the same content id:\n%s", again);
    CHECK(zxboard_open(b2, &reg, &APP, "fees.fee") == ZXAPP_E_NAME, "not a circuit");

    /* a board built on the first board's app */
    CHECK(zxboard_init(b2, &reg, "till", "Till") == ZXAPP_OK && zxboard_shelf_count(b2) == 8,
          "checkout is on the shelf now, with its 3 exports");
    CHECK(zxboard_input_add(b2, "amount") == 0, "input");
    int p = zxboard_part_add(b2, "checkout.main", 240, 80);
    s = zxboard_src_input(0);
    zxboard_wire(b2, (uint32_t) p, 0, &s);
    CHECK(zxboard_src_setting("25", &s) && s.nconst == 1 &&
              zxboard_wire(b2, (uint32_t) p, 1, &s) == ZXAPP_OK,
          "rate=25, a setting");
    int t = zxboard_part_add(b2, "fees.total", 480, 80);
    s = zxboard_src_input(0);
    zxboard_wire(b2, (uint32_t) t, 0, &s);
    s = zxboard_src_part((uint32_t) p);
    zxboard_wire(b2, (uint32_t) t, 1, &s);
    s = zxboard_src_part((uint32_t) t);
    zxboard_out(b2, &s);
    zxboard_check(b2, &k);
    CHECK(k.lp == ZXAPP_BOTH && k.port[t][1] == ZXWIRE_HELD &&
              strstr(k.why, "total.fee takes whole numbers, and gets a truth"),
          "CB3: a truth into a number input is drawn held: %s", k.why);
    len = zxboard_save(b2, text, sizeof text);
    CHECK(len > 0, "CB4: a held wire still saves: %s", b2->err);
    n = 0;
    CHECK(zxboard_try(b2, &H, args, 1, out, 8, &n, &lp) == ZXAPP_E_CONFLICT && lp == ZXAPP_BOTH,
          "and holds when run, as the board said: %s", b2->err);
    zxboard_src_setting("1,2", &s);
    CHECK(zxboard_wire(b2, (uint32_t) t, 1, &s) == ZXAPP_OK, "rewire total.fee to a setting");
    zxboard_check(b2, &k);
    CHECK(k.lp == ZXAPP_TRUE, "TRUE again: %s", k.why);

    /* loops, renames, removal */
    s = zxboard_src_part((uint32_t) t);
    CHECK(zxboard_wire(b2, (uint32_t) p, 1, &s) == ZXAPP_OK, "main.rate <- total");
    s = zxboard_src_part((uint32_t) p);
    zxboard_wire(b2, (uint32_t) t, 1, &s);
    zxboard_check(b2, &k);
    CHECK(k.loop[p] && k.loop[t] && k.held >= 2 && strstr(k.why, "on a loop"),
          "CB3: a loop is held: %s", k.why);
    CHECK(zxboard_save(b2, text, sizeof text) == ZXAPP_E_CONTRACT && strstr(b2->err, "loop"),
          "CB4: a loop is not saved");
    CHECK(zxboard_part_rename(b2, (uint32_t) t, "main") == ZXAPP_E_NAME &&
              zxboard_part_rename(b2, (uint32_t) t, "amount") == ZXAPP_E_NAME &&
              zxboard_part_rename(b2, (uint32_t) t, "sum") == ZXAPP_OK,
          "rename");
    CHECK(zxboard_part_remove(b2, (uint32_t) p) == ZXAPP_OK && b2->c.nparts == 1 &&
              b2->c.part[0].src[1].kind == ZXWIRE_SRC_NONE && b2->c.out.kind == ZXWIRE_SRC_PART &&
              b2->c.out.index == 0,
          "removing a part unwires what took it, and renumbers the rest");
    CHECK(zxboard_input_remove(b2, 0) == ZXAPP_OK && b2->c.ninputs == 0 &&
              b2->c.part[0].src[0].kind == ZXWIRE_SRC_NONE,
          "removing an input unwires it");
    CHECK(zxboard_move(b2, ZXBOARD_NODE_PART, 0, 10, 20) == ZXAPP_OK && b2->c.part[0].x == 10 &&
              zxboard_move(b2, ZXBOARD_NODE_OUT, 0, 5, 6) == ZXAPP_OK && b2->c.out_y == 6 &&
              zxboard_move(b2, ZXBOARD_NODE_INPUT, 3, 1, 1) == ZXAPP_E_ARG &&
              zxboard_move(b2, ZXBOARD_NODE_PART, 0, 1, 100001) == ZXAPP_E_ARG,
          "move");
    CHECK(!zxboard_src_setting("", &s) && !zxboard_src_setting("1x", &s) &&
              !zxboard_src_setting("1,2,3,4,5,6,7,8,9", &s) &&
              !zxboard_src_setting("1234567890123456789", &s) && zxboard_src_setting("-4, 5", &s) &&
              s.konst[0] == -4 && s.konst[1] == 5,
          "settings are up to 8 whole numbers");
}

/* ---- legacy languages through Lingua (zxapp_lingua.c) ----------------------- */

static void test_lingua(void)
{
    zxapp_num_t out[8];
    uint32_t n = 0;
    int32_t lp = -1;
    zxapp_host_init(&H);
    H.read_file = read_src;
    CHECK(zxapp_lingua_register(&H, 100000) == ZXAPP_OK && H.n == 4 &&
              zxapp_host_has(&H, ZXAPP_LANG_FORTRAN77) && zxapp_host_has(&H, ZXAPP_LANG_BASIC) &&
              zxapp_host_has(&H, ZXAPP_LANG_FORTH) && zxapp_host_has(&H, ZXAPP_LANG_ZLIR),
          "Lingua registers FORTRAN 77, BASIC, Forth and precompiled IR");
    CHECK(parse("zxapp 1\nid legacy\n"
                "provides span num x\n"
                "logic fortran77 span <<\n"
                "      INTEGER A, B, C\n"
                "      READ *, A, B, C\n"
                "      PRINT *, MAX(A, B, C) - MIN(A, B, C), A + B + C\n"
                "      END\n.\n"
                "logic basic span <<\n"
                "10 INPUT A, B, C\n20 LET H = A\n30 IF B <= H THEN 50\n40 LET H = B\n"
                "50 IF C <= H THEN 70\n60 LET H = C\n70 LET L = A\n80 IF B >= L THEN 100\n"
                "90 LET L = B\n100 IF C >= L THEN 120\n110 LET L = C\n"
                "120 PRINT H - L; A + B + C\n130 END\n.\n"
                "logic forth span <<\n"
                "VARIABLE A VARIABLE B VARIABLE C C ! B ! A !\n"
                "A @ B @ MAX C @ MAX A @ B @ MIN C @ MIN - . A @ B @ + C @ + .\n.\n"
                "logic forth words <<\n.\" six\"\n.\n"
                "logic basic halves <<\n10 INPUT A\n20 PRINT A / 2\n30 END\n.\n"
                "logic forth spin <<\n: S BEGIN 0 UNTIL ; S\n.\n"
                "logic forth many <<\n1 . 2 . 3 . 4 . 5 . 6 . 7 . 8 . 9 .\n.\n"
                "logic basic broken <<\n10 PRINT (1\n20 END\n.\n") == ZXAPP_OK &&
              A.n == 8,
          "one manifest, three legacy languages: %s", A.err);
    int loaded = zxapp_load(&A, &H);
    CHECK(loaded == ZXAPP_E_RUNTIME && strstr(A.err, "broken: line 1"),
          "a load refusal names the entry and the line: %s", A.err);
    zxapp_unload(&A, &H);
    A.n = 7; /* drop the broken one */
    loaded = zxapp_load(&A, &H);
    CHECK(loaded == 7, "all seven compile once, at load: %d %s", loaded, A.err);
    int64_t x[3] = {7, -2, 40};
    zxapp_arg_t xa = {"x", x, 3};
    CHECK(zxapp_call_witnessed(&A, &H, "span", &xa, 1, out, 8, &n, &lp) == ZXAPP_OK &&
              lp == ZXAPP_TRUE && n == 2 && out[0].num == 42 && out[0].den == 1 && out[1].num == 45,
          "L1/L2: FORTRAN, BASIC and Forth agree (TRUE): span 42, sum 45 (lp %d n %u %s)", lp, n,
          A.err);
    CHECK(zxapp_call(&A, &H, "words", 0, 0, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "not a number"),
          "L2: printed words are refused, not skipped: %s", A.err);
    int64_t odd[1] = {7}, even[1] = {8};
    zxapp_arg_t oa = {"a", odd, 1}, ea = {"a", even, 1};
    CHECK(zxapp_call(&A, &H, "halves", &ea, 1, out, 8, &n) == ZXAPP_OK && n == 1 && out[0].num == 4,
          "BASIC divides exactly");
    CHECK(zxapp_call(&A, &H, "halves", &oa, 1, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "line 2") && strstr(A.err, "stopped"),
          "L3: 7 / 2 is a trap naming the line, never 3 or 3.5: %s", A.err);
    CHECK(zxapp_call(&A, &H, "spin", 0, 0, out, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(A.err, "stopped"),
          "L3: a loop that never ends runs out of fuel: %s", A.err);
    CHECK(zxapp_call(&A, &H, "many", 0, 0, out, 8, &n) == ZXAPP_E_BUFFER && n == 9,
          "nine answers do not fit eight");
    CHECK(zxapp_call(&A, &H, "span", 0, 0, out, 8, &n) == ZXAPP_E_CONTRACT,
          "B3: the contract is checked before the program runs");
    zxapp_unload(&A, &H);

    /* the number_classics block, read from disk, composed into an app */
    if (!SRC_ROOT) return;
    static char btext[8192], src[16384], pre[65536];
    static zxapp_t BLK, APP;
    static zxapp_registry_t REG;
    int32_t len = read_src(0, "apps/blocks/number_classics.zxblock", btext, sizeof btext);
    CHECK(len > 0 && zxapp_parse(&BLK, btext, (uint32_t) len) == ZXAPP_OK && BLK.n == 4,
          "number_classics parses: %s", BLK.err);
    zxapp_registry_init(&REG);
    CHECK(zxapp_registry_add(&REG, &BLK) == ZXAPP_OK, "and registers: %s", REG.err);
    static const char app[] = "zxapp 1\nid calc\nname Calculator\nuse number_classics 1.0.0\n";
    CHECK(zxapp_parse(&APP, app, sizeof app - 1) == ZXAPP_OK &&
              zxapp_compose(&APP, &REG) == ZXAPP_OK && APP.n == 4,
          "an app takes the block with one line, no FORTRAN, BASIC or Forth in it: %s", APP.err);
    CHECK(zxapp_load(&APP, &H) == 4, "all four implementations load: %s", APP.err);
    int64_t a[1] = {84}, b[1] = {-36};
    zxapp_arg_t ga[2] = {{"a", a, 1}, {"b", b, 1}};
    CHECK(zxapp_call_witnessed(&APP, &H, "number_classics.gcd", ga, 2, out, 8, &n, &lp) ==
                  ZXAPP_OK &&
              lp == ZXAPP_TRUE && n == 1 && out[0].num == 12,
          "gcd(84, -36) = 12 in all four (TRUE): %s", APP.err);
    a[0] = INT64_C(-2147483648);
    b[0] = 1;
    CHECK(zxapp_call_witnessed(&APP, &H, "number_classics.gcd", ga, 2, out, 8, &n, &lp) ==
                  ZXAPP_E_CONFLICT &&
              lp == ZXAPP_BOTH,
          "|-2^31| overflows 32-bit FORTRAN and BASIC, Forth's 64-bit cell holds it: BOTH, held");
    zxapp_unload(&APP, &H);

    /* the shipped IR is exactly what gcd.f compiles to today */
    static zl_prog_t P1, P2;
    static zl_work_t W;
    zl_diag_t d;
    int32_t sl = read_src(0, "apps/blocks/number_classics/gcd.f", src, sizeof src);
    int32_t pl = read_src(0, "apps/blocks/number_classics/gcd.zlir", pre, sizeof pre);
    CHECK(sl > 0 && pl > 0 && zl_compile(ZL_LANG_FORTRAN, src, (size_t) sl, &P1, &W, &d) == ZL_OK &&
              zl_unpack(pre, (size_t) pl, &P2, &d) == ZL_OK && zl_prog_equal(&P1, &P2),
          "gcd.zlir is gcd.f compiled (rebuild with zxlang pack): %s", d.msg);
}

int main(int argc, char **argv)
{
    SRC_ROOT = argc > 1 ? argv[1] : 0;
    test_names();
    test_parse();
    test_host();
    test_kron();
    test_eve();
    test_wasm();
    test_witness();
    test_block_parse();
    test_registry();
    test_compose();
    test_launch();
    test_apps_as_blocks();
    test_intents();
    test_sutra();
    test_trebago();
    test_myra();
    test_witness_mixed();
    test_fee_check();
    test_registry_fields();
    test_wave0();
    test_wire();
    test_board();
    test_lingua();
    printf("%s test_zxapp: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails, checks);
    return fails ? 1 : 0;
}
