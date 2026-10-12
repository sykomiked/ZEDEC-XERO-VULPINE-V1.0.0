/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxapp.c — app manifests (zxapp.h) and the KRON runtime over the
 * apl-kron pillar. Run by test_zxapp.sh. */
#include <stdio.h>
#include <string.h>

#include "zxapp.h"
#include "zxapp_eve.h"
#include "zxapp_kron.h"
#include "zxapp_wasm.h"
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
                 {"php", ZXAPP_LANG_PHP}};
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
    CHECK(parse("zxapp 1\nid x\nlogic brainfork a b\n") == ZXAPP_E_SYNTAX &&
              strstr(A.err, "unknown language"),
          "unknown language");
    CHECK(parse("zxapp 1\nid x\nlogic kron a\n") == ZXAPP_E_SYNTAX, "logic needs a source");
    CHECK(parse("zxapp 1\nid x\nlogic kron 1a p\n") == ZXAPP_E_SYNTAX, "entry identifier");
    CHECK(parse("zxapp 1\nid x\nlogic kron a p\nlogic eve a q\n") == ZXAPP_E_SYNTAX &&
              A.err_line == 4,
          "duplicate entry");
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

int main(void)
{
    test_names();
    test_parse();
    test_host();
    test_kron();
    test_eve();
    test_wasm();
    printf("%s test_zxapp: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails, checks);
    return fails ? 1 : 0;
}
