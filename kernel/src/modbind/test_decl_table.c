/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_decl_table.c — the declaration walk and the boot gate at the two table
 * sizes test_modbind.c cannot reach with its six declarations.
 *
 * The table is fixed at link time, so this file is built twice:
 *   -DDECL_N=12  more declarations than the gate names (ZXV_DECL_MAX_FAILED = 8):
 *                every name slot is used, the ninth and later are counted and
 *                not named, and nothing is written past the name tables (the
 *                san build would stop on it).
 *   -DDECL_N=0   an empty section: the gate must fail, never certify an empty
 *                graph as sound (zxv_decl.h, "+1 the linker collected NO
 *                declarations").
 * Expected values are worked from zxv_decl.h, not read back from the code. */
#include <stdio.h>
#include <string.h>
#include "modbind.h"
#include "zxv_decl.h"

#ifndef DECL_N
#    error "build with -DDECL_N=12 or -DDECL_N=0"
#endif

static int failures = 0;
static unsigned checks = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            printf("  FAIL: %s (line %d)\n", msg, __LINE__);                                       \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static char out_buf[65536];
static size_t out_len;
static void cap_puts(const char *s)
{
    size_t n = strlen(s);
    if (out_len + n < sizeof out_buf) {
        memcpy(out_buf + out_len, s, n);
        out_len += n;
        out_buf[out_len] = 0;
    }
}
static int said(const char *s)
{
    return strstr(out_buf, s) != 0;
}
static int count_said(const char *s)
{
    int n = 0;
    for (const char *p = out_buf; (p = strstr(p, s)) != 0; p += strlen(s)) n++;
    return n;
}
static void fresh(void)
{
    modbind_reset();
    mb_carrier_up();
    out_len = 0;
    out_buf[0] = 0;
}

_Static_assert(sizeof(void *) == 8, "the table end below counts 8-byte pointers");
#define STR_(x) #x
#define STR(x)  STR_(x)

#if DECL_N > 0
static mb_module_t g_dm[DECL_N];
static zxv_decl_t g_decl[DECL_N];
const zxv_decl_t *zxv_test_decls[DECL_N];
__asm__(".globl __zxv_decl_start\n\t.set __zxv_decl_start, zxv_test_decls\n\t"
        ".globl __zxv_decl_end\n\t.set __zxv_decl_end, zxv_test_decls + 8 * " STR(DECL_N) "\n");

static int rc_all;
static int calls;
static int bring(void)
{
    calls++;
    return rc_all;
}

/* DECL_N independent modules d00, d01, ...: no requirement, so each is ready
 * and each bring-up runs and returns rc */
static void table(int rc)
{
    for (int i = 0; i < DECL_N; i++) {
        memset(&g_dm[i], 0, sizeof g_dm[i]);
        g_dm[i].name[0] = 'd';
        g_dm[i].name[1] = (char) ('0' + i / 10);
        g_dm[i].name[2] = (char) ('0' + i % 10);
        g_dm[i].ready = MB_HELD;
        g_decl[i].mod = &g_dm[i];
        g_decl[i].bringup = bring;
        zxv_test_decls[i] = &g_decl[i];
    }
    rc_all = rc;
    calls = 0;
    fresh();
}

/* "BRING-UP FAILED: 'dNN'" for NN = 0..7, in table order, and nothing else */
static int eight_failures_named(void)
{
    char want[64];
    for (int i = 0; i < 8; i++) {
        snprintf(want, sizeof want, "         BRING-UP FAILED: 'd%02d' -- its own check", i);
        if (!said(want)) return 0;
    }
    return count_said("BRING-UP FAILED: '") == 8 &&
           said("         BRING-UP FAILED: 'd07' -- its own check returned non-zero\n"
                "         (further failures counted but not named; raise ZXV_DECL_MAX_FAILED)\n");
}

static void test_many(void)
{
    char line[96];
    CHECK(zxv_decl_count() == DECL_N, "the linked table");

    /* every bring-up fails: all counted, eight named, the rest said to exist */
    table(-1);
    CHECK(zxv_decl_boot_gate(cap_puts) == DECL_N, "one problem per failed bring-up");
    CHECK(calls == DECL_N, "every ready module's bring-up ran once");
    snprintf(line, sizeof line, "         bring-up: ran=0 failed=%d held=0 declared-only=0\n",
             DECL_N);
    CHECK(said(line), "the counts include the unnamed failures");
    CHECK(eight_failures_named(), "eight named in order, then the overflow line");
    CHECK(zxv_decl_failed_name(7) && strcmp(zxv_decl_failed_name(7), "d07") == 0 &&
              zxv_decl_failed_name(8) == 0 && zxv_decl_failed_name(DECL_N) == 0,
          "the readout has eight slots");

    /* every declaration refused: counted twice (zxv_decl.h, gate return), eight named */
    table(0);
    for (int i = 0; i < DECL_N; i++) g_dm[i].n_emits = MB_MAX_PORTS + 1;
    CHECK(zxv_decl_boot_gate(cap_puts) == 2 * DECL_N, "refused + not registered, for each");
    snprintf(line, sizeof line, "registered=0 refused=%d\n", DECL_N);
    CHECK(said(line), "all refused");
    CHECK(calls == 0, "a refused declaration's bring-up does not run");
    snprintf(line, sizeof line, "         bring-up: ran=0 failed=%d held=0 declared-only=0\n",
             DECL_N);
    CHECK(said(line), "every refused declaration counted as failed");
    CHECK(eight_failures_named(), "the first eight refused named, then the overflow line");
    CHECK(zxv_decl_failed_name(8) == 0, "eight slots, not nine");

    /* every bring-up holds: eight held names, the count says all of them */
    table(MB_BRINGUP_HELD);
    CHECK(zxv_decl_boot_gate(cap_puts) == 0, "held bring-ups are not problems");
    snprintf(line, sizeof line, "         bring-up: ran=0 failed=0 held=%d declared-only=0\n",
             DECL_N);
    CHECK(said(line), "every held bring-up counted");
    CHECK(count_said("S0 HELD (bring-up): '") == 8 && said("S0 HELD (bring-up): 'd00'") &&
              said("S0 HELD (bring-up): 'd07'") && !said("S0 HELD (bring-up): 'd08'"),
          "the first eight named");
    CHECK(zxv_decl_held_name(7) && strcmp(zxv_decl_held_name(7), "d07") == 0 &&
              zxv_decl_held_name(8) == 0,
          "eight held slots, not nine");
    CHECK(zxv_decl_failed_name(0) == 0, "no failure from the run before survives");

    /* a null slot and a declaration with no module: refused, never run */
    table(0);
    zxv_test_decls[3] = 0;
    g_decl[5].mod = 0;
    CHECK(zxv_decl_boot_gate(cap_puts) == 2, "two refused slots");
    snprintf(line, sizeof line, "  declarations collected=%d registered=%d refused=2\n", DECL_N,
             DECL_N - 2);
    CHECK(said(line), "counted as refused");
    snprintf(line, sizeof line, "         bring-up: ran=%d failed=0 held=0 declared-only=0\n",
             DECL_N - 2);
    CHECK(said(line) && calls == DECL_N - 2, "and skipped by the bring-up walk");

    /* the bring-up walk with nowhere to put its counts */
    table(0);
    zxv_decl_register_all(0);
    modbind_resolve();
    CHECK(zxv_decl_bringup_ready(0, 0, 0) == DECL_N && calls == DECL_N,
          "null count pointers are skipped, not written");

    /* and healthy: every bring-up succeeds */
    table(0);
    CHECK(zxv_decl_boot_gate(cap_puts) == 0, "all up");
    snprintf(line, sizeof line, "         bring-up: ran=%d failed=0 held=0 declared-only=0\n",
             DECL_N);
    CHECK(said(line), "all ran");
    CHECK(zxv_decl_held_name(0) == 0 && zxv_decl_failed_name(0) == 0, "no name survives");
}
#else
/* an empty section: start and end at the same address */
const zxv_decl_t *zxv_test_decls[1];
__asm__(".globl __zxv_decl_start\n\t.set __zxv_decl_start, zxv_test_decls\n\t"
        ".globl __zxv_decl_end\n\t.set __zxv_decl_end, zxv_test_decls\n");

static void test_none(void)
{
    uint32_t refused = 7;
    CHECK(zxv_decl_count() == 0, "an empty section counts zero");
    fresh();
    CHECK(zxv_decl_register_all(&refused) == 0 && refused == 0, "nothing registered or refused");
    CHECK(modbind_count() == 0, "the registry stays empty");
    fresh();
    CHECK(zxv_decl_boot_gate(cap_puts) == 1, "an empty graph is one problem, not a pass");
    CHECK(said("  declarations collected=0 registered=0 refused=0\n"), "the counts");
    CHECK(said("  [FAIL] no declarations collected -- .rodata.zxv_decl was not placed\n"),
          "the empty section is named");
    CHECK(count_said("[FAIL]") == 1, "and it is the only failure");
}
#endif

int main(void)
{
#if DECL_N > 0
    test_many();
#else
    test_none();
#endif
    printf("test_decl_table (DECL_N=%d): %u checks, %d failure(s)\n", DECL_N, checks, failures);
    return failures ? 1 : 0;
}
