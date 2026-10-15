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
                 {"lingua", ZXAPP_LANG_ZLIR},
                 {"pascal", ZXAPP_LANG_ZL_PASCAL},
                 {"Tutordrake", ZXAPP_LANG_ZL_PASCAL},
                 {"algol60", ZXAPP_LANG_ZL_ALGOL60},
                 {"ALGOL 60", ZXAPP_LANG_ZL_ALGOL60},
                 {"algol", ZXAPP_LANG_ZL_ALGOL60},
                 {"scheme", ZXAPP_LANG_ZL_SCHEME},
                 {"Spiraldrake", ZXAPP_LANG_ZL_SCHEME},
                 {"lisp", ZXAPP_LANG_ZL_LISP},
                 {"Kiteling", ZXAPP_LANG_ZL_LOGO},
                 {"commonlisp", ZXAPP_LANG_ZL_COMMONLISP},
                 {"Common Lisp", ZXAPP_LANG_ZL_COMMONLISP},
                 {"Braidwyrm", ZXAPP_LANG_ZL_COMMONLISP},
                 {"iec_st", ZXAPP_LANG_ZL_IEC_ST},
                 {"Objective-C", ZXAPP_LANG_ZL_OBJC},
                 {"verilog", ZXAPP_LANG_ZL_VERILOG}};
    for (unsigned i = 0; i < sizeof alias / sizeof alias[0]; i++)
        CHECK(zxapp_lang_of(alias[i].n) == alias[i].l, "alias %s", alias[i].n);
    CHECK(strcmp(zxapp_lang_name(ZXAPP_LANG_FORTRAN77), "fortran77") == 0 &&
              strcmp(zxapp_lang_name(ZXAPP_LANG_ZL_ALGOL60), "algol60") == 0 &&
              strcmp(zxapp_lang_name(ZXAPP_LANG_ZL_IEC_ST), "iec_st") == 0 &&
              strcmp(zxapp_lang_name(ZXAPP_LANG_ZL_VERILOG), "verilog") == 0,
          "Lingua languages are named by zl_langs.def's runtime field");
    CHECK(zxapp_lang_of("fortran") == ZXAPP_LANG_TREBAGO &&
              zxapp_lang_of("FORTRAN") == ZXAPP_LANG_TREBAGO,
          "FORTRAN stays the Trebago pillar; Lingua's is fortran77");
    CHECK(zxapp_lang_of("Mojo") == ZXAPP_LANG_ZL_MOJO &&
              zxapp_lang_of("Forgewyrm") == ZXAPP_LANG_ZL_MOJO,
          "every Lingua language, by id and by twin");
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

/* ---- legacy languages through Lingua (zxapp_lingua.c) ----------------------- */

static void test_lingua(void)
{
    zxapp_num_t out[8];
    uint32_t n = 0;
    int32_t lp = -1;
    zxapp_host_init(&H);
    H.read_file = read_src;
    CHECK(zxapp_lingua_register(&H, 100000) == ZXAPP_OK &&
              H.n == (uint32_t) (ZXAPP_LANG_COUNT - ZXAPP_LANG_ZLIR) &&
              zxapp_host_has(&H, ZXAPP_LANG_FORTRAN77) && zxapp_host_has(&H, ZXAPP_LANG_BASIC) &&
              zxapp_host_has(&H, ZXAPP_LANG_FORTH) && zxapp_host_has(&H, ZXAPP_LANG_ZLIR) &&
              zxapp_host_has(&H, ZXAPP_LANG_ZL_PASCAL) && zxapp_host_has(&H, ZXAPP_LANG_ZL_VERILOG),
          "Lingua registers precompiled IR and every language in zl_langs.def");
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

    /* every newer front-end is reachable the same way; a placeholder says so */
    CHECK(parse("zxapp 1\nid legacy2\n"
                "provides span num x\n"
                "logic fortran77 span <<\n"
                "      INTEGER A, B, C\n"
                "      READ *, A, B, C\n"
                "      PRINT *, MAX(A, B, C) - MIN(A, B, C), A + B + C\n"
                "      END\n.\n"
                "logic Tutordrake span <<\n"
                "program span;\nvar a, b, c, h, l: integer;\nbegin\n"
                "  read(a, b, c);\n  h := a; if b > h then h := b; if c > h then h := c;\n"
                "  l := a; if b < l then l := b; if c < l then l := c;\n"
                "  writeln(h - l, ' ', a + b + c)\nend.\n.\n") == ZXAPP_OK &&
              A.n == 2,
          "FORTRAN and Pascal (by its twin's name) in one manifest: %s", A.err);
    CHECK(zxapp_load(&A, &H) == 2, "both compile: %s", A.err);
    CHECK(zxapp_call_witnessed(&A, &H, "span", &xa, 1, out, 8, &n, &lp) == ZXAPP_OK &&
              lp == ZXAPP_TRUE && n == 2 && out[0].num == 42 && out[1].num == 45,
          "FORTRAN and Pascal agree (TRUE): %s", A.err);
    zxapp_unload(&A, &H);
    CHECK(parse("zxapp 1\nid later\nprovides f num x\nlogic mojo f <<\nfn main(): pass\n.\n") ==
                  ZXAPP_OK &&
              zxapp_load(&A, &H) == ZXAPP_E_RUNTIME && strstr(A.err, "not built yet"),
          "a language whose front-end is not built refuses at load, by name: %s", A.err);
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
    test_lingua();
    printf("%s test_zxapp: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails, checks);
    return fails ? 1 : 0;
}
