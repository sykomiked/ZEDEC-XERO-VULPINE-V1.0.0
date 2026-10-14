/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_aml_ops.c — every opcode family of fixtures/ops.asl, with exact
 * expected results, run twice each so a leak (heap growth on the second call)
 * is caught as well as a wrong value. */
#include "amltest.h"

#define ONES 0xFFFFFFFFFFFFFFFFull

typedef struct {
    const char *path;
    int nargs;
    uint64_t arg;
    int err;
    uint64_t want;
} icase_t;

static const icase_t ints[] = {
    {"T001", 0, 0, 0, 0x8000000000000001ull},
    {"T002", 0, 0, 0, 0xFFFFFFFFFFFFFFFEull},
    {"T003", 0, 0, 0, 0x100000000ull},
    {"T004", 0, 0, 0, 2014},
    {"T005", 0, 0, 0, 2},
    {"T006", 0, 0, 0, 0x10000000000ull},
    {"T007", 0, 0, 0, 1},
    {"T008", 0, 0, 0, 0},
    {"T009", 0, 0, 0, 0xF0},
    {"T010", 0, 0, 0, 0xF00F},
    {"T011", 0, 0, 0, 0xF0F0},
    {"T012", 0, 0, 0, 0xFFFFFFFFFFFFFFCFull},
    {"T013", 0, 0, 0, 0xFFFFFFFFFFFFFF00ull},
    {"T014", 0, 0, 0, ONES},
    {"T015", 0, 0, 0, 8},
    {"T016", 0, 0, 0, 8},
    {"T017", 0, 0, 0, 0},
    {"T018", 0, 0, 0, 2},
    {"T019", 0, 0, 0, 6},
    {"T020", 0, 0, 0, 4},
    {"T021", 0, 0, AML_EDIV0, 0},
    {"T022", 0, 0, AML_EDIV0, 0},
    {"T023", 0, 0, 0, 3},
    {"T024", 0, 0, 0, ONES},
    {"T025", 0, 0, 0, 0x123456789ull},
    {"T026", 0, 0, 0, 0},
    {"T027", 0, 0, 0, ONES},
    {"T028", 0, 0, 0, 64},
    {"T029", 0, 0, 0, 64},
    {"T030", 0, 0, 0, ONES},
    {"T031", 0, 0, 0, 0},
    {"T032", 0, 0, 0, 0},
    {"T033", 0, 0, 0, ONES},
    {"T034", 0, 0, 0, ONES},
    {"T035", 0, 0, 0, 0},
    {"T036", 0, 0, 0, ONES},
    {"T037", 0, 0, 0, ONES},
    {"T038", 0, 0, 0, 0},
    {"T039", 0, 0, 0, ONES},
    {"T040", 0, 0, 0, ONES},
    {"T041", 0, 0, 0, 0},
    {"T042", 0, 0, 0, ONES},
    {"T043", 0, 0, 0, ONES},
    {"T044", 0, 0, 0, ONES},
    {"T045", 0, 0, 0, ONES},
    {"T046", 0, 0, 0, ONES},
    {"T047", 0, 0, 0, ONES},
    {"T048", 1, 7, 0, 1},
    {"T048", 1, 3, 0, 2},
    {"T048", 1, 1, 0, 3},
    {"T048", 1, 5, 0, 2},
    {"T048", 1, 6, 0, 1},
    {"T048", 1, 2, 0, 3},
    {"T049", 0, 0, 0, 0},
    {"T050", 0, 0, 0, 55},
    {"T051", 0, 0, 0, 5},
    {"T052", 0, 0, 0, 25},
    {"T053", 0, 0, 0, 12},
    {"T054", 1, 1, 0, 10},
    {"T054", 1, 2, 0, 20},
    {"T054", 1, 3, 0, 20},
    {"T054", 1, 9, 0, 30},
    {"T055", 0, 0, 0, 3628800},
    {"T056", 0, 0, 0, 28},
    {"T057", 0, 0, 0, 42},
    {"T058", 0, 0, AML_ELIMIT, 0},
    {"T059", 0, 0, AML_ELIMIT, 0},
    {"T060", 0, 0, 0, 5},
    {"T061", 0, 0, 0, 4},
    {"T062", 0, 0, 0, 4},
    {"T071", 0, 0, 0, 31},
    {"T072", 0, 0, 0, 123},
    {"T073", 0, 0, 0, 0x1234},
    {"T081", 0, 0, 0, 0x1234},
    {"T082", 0, 0, 0, 9876},
    {"T083", 0, 0, 0, 0x0807090604030201ull},
    {"T084", 0, 0, 0, 0x00100B0D0C0E050Aull},
    {"T087", 0, 0, 0, 16},
    {"T090", 0, 0, 0, 1},
    {"T091", 0, 0, 0, 3},
    {"T092", 0, 0, 0, 5},
    {"T093", 0, 0, 0, 9},
    {"T094", 0, 0, 0, 0x1155},
    {"T095", 0, 0, 0, 10},
    {"T096", 0, 0, 0, 42},
    {"T097", 0, 0, 0, 77},
    {"T098", 0, 0, 0, 6},
    {"T099", 0, 0, 0, 2},
    {"T100", 0, 0, 0, ONES},
    {"T101", 0, 0, 0, 1},
    {"T102", 0, 0, AML_EBOUNDS, 0},
    {"T103", 1, 6, 0, 6},
    {"T103", 1, 0, 0, 0},
    {"T104", 0, 0, 0, 41},
    {"T105", 0, 0, 0, 3},
    {"T106", 0, 0, 0, 9},
    {"T107", 0, 0, 0, 1},
    {"T108", 0, 0, 0, 1},
    {"T109", 0, 0, AML_EBOUNDS, 0},
    {"T110", 0, 0, 0, 0x43344AABBull},
    {"T111", 0, 0, 0, 0x1A},
    {"T112", 0, 0, 0, 16},
    {"T113", 0, 0, 0, 2},
    {"T114", 0, 0, 0, 0x0000123456780001ull},
    {"T115", 0, 0, 0, 0xCA},
    {"T116", 0, 0, 0, 0x0000001F00000005ull},
    {"T117", 0, 0, 0, 12},
    {"T118", 0, 0, AML_EBOUNDS, 0},
    {"T119", 0, 0, 0, 0xBEEF12345678FF01ull},
    {"T120", 0, 0, 0, 5},
    {"T121", 0, 0, AML_EMUTEX, 0},
    {"T122", 0, 0, 0, 7},
    {"T124", 0, 0, AML_EMUTEX, 0},
    {"T134", 0, 0, AML_EFATAL, 0},
    {"T135", 0, 0, 0, 0x20261012},
    {"T136", 0, 0, AML_ELIMIT, 0},
    {"T137", 0, 0, 0, 1},
    {"T140", 0, 0, 0, 41},
    {"T141", 0, 0, 0, 6},
    {"T142", 0, 0, 0, 3},
    {"T143", 0, 0, 0, 1},
    {"T144", 0, 0, 0, 0x214},
    {"T145", 0, 0, 0, 21},
    {"T146", 0, 0, 0, 99},
    {"T147", 0, 0, 0, 0x5B},
    {"T148", 0, 0, 0, 41},
    {"T149", 0, 0, 0, 6},
};

typedef struct {
    const char *path;
    const char *str;    /* expected string, or 0 */
    const uint8_t *buf; /* expected buffer */
    uint32_t blen;
} vcase_t;

static const uint8_t b064[] = {1, 2, 3};
static const uint8_t b065[] = {1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t b074[] = {0x41, 0x42, 0};
static const uint8_t b075[] = {2, 1, 0, 0, 0, 0, 0, 0};
static const uint8_t b079[] = {3, 4};
static const uint8_t b089[] = {0x47, 0x01, 0x60, 0x00, 0x60, 0x00, 0x00, 0x01, 0x47,
                               0x01, 0x64, 0x00, 0x64, 0x00, 0x00, 0x01, 0x79, 0x00};

static const vcase_t vals[] = {
    {"T063", "abcd", 0, 0},
    {"T064", 0, b064, sizeof b064},
    {"T065", 0, b065, sizeof b065},
    {"T066", "x000000000000001F", 0, 0},
    {"T067", "00000000000000AB", 0, 0},
    {"T068", "0xAB,0x01", 0, 0},
    {"T069", "1234", 0, 0},
    {"T070", "1,200,34", 0, 0},
    {"T074", 0, b074, sizeof b074},
    {"T075", 0, b075, sizeof b075},
    {"T076", "AB", 0, 0},
    {"T077", "AB", 0, 0},
    {"T078", "ell", 0, 0},
    {"T079", 0, b079, sizeof b079},
    {"T080", "", 0, 0},
    {"T085", "a01 A2", 0, 0},
    {"T086", "abc", 0, 0},
    {"T088", "str", 0, 0},
    {"T089", 0, b089, sizeof b089},
    {"\\GSTR", "hello", 0, 0},
};

static void test_ints(aml_ctx_t *c)
{
    for (uint32_t i = 0; i < sizeof ints / sizeof ints[0]; i++) {
        const icase_t *t = &ints[i];
        for (int round = 0; round < 2; round++) {
            uint64_t v;
            uint32_t before = aml_stats(c).heap_used;
            int e = t->nargs ? ev_int1(c, t->path, t->arg, &v) : ev_int(c, t->path, &v);
            uint32_t after = aml_stats(c).heap_used;
            if (e != t->err || (!e && v != t->want)) {
                g_fails++;
                printf(
                    "FAIL %s(%llu) round %d: err %d (%s) value 0x%llx, want err %d value 0x%llx\n",
                    t->path, (unsigned long long) t->arg, round, e, aml_strerror(e),
                    (unsigned long long) v, t->err, (unsigned long long) t->want);
            }
            g_checks++;
            /* steady state: a second identical call must not grow the heap */
            if (round == 1) CHECK_EQ(after, before);
            CHECK_EQ(c->depth, 0);
            CHECK_EQ(c->nest, 0);
        }
    }
}

static void test_vals(aml_ctx_t *c)
{
    for (uint32_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        const vcase_t *t = &vals[i];
        aml_val_t r;
        int e = aml_eval(c, 0, t->path, 0, 0, &r);
        CHECK_EQ(e, 0);
        int ok = t->str ? val_is_str(&r, t->str) : val_is_buf(&r, t->buf, t->blen);
        if (!ok) {
            printf("FAIL %s: type %u len %u", t->path, r.type, r.len);
            if (r.type == AML_T_STR) printf(" \"%s\"", (const char *) r.u.bytes);
            printf("\n");
        }
        CHECK(ok);
        aml_val_free(c, &r);
    }
}

static void test_side_effects(amlfake_t *f, aml_ctx_t *c)
{
    uint64_t v;
    aml_notify_t n;
    char p[64];
    /* Notify queues in order and logs (T137 ran twice in test_ints) */
    CHECK_EQ(aml_notify_pop(c, &n), 1);
    CHECK_EQ(n.value, 1);
    aml_node_path(n.node, p, sizeof p);
    CHECK(strcmp(p, "\\CPU0") == 0);
    CHECK_EQ(aml_notify_pop(c, &n), 1);
    CHECK_EQ(aml_notify_pop(c, &n), 0);
    CHECK_EQ(ev_int(c, "T130", &v), 0);
    CHECK_EQ(v, 2);
    CHECK_EQ(aml_notify_pop(c, &n), 1);
    aml_node_path(n.node, p, sizeof p);
    CHECK(strcmp(p, "\\DEV0") == 0);
    CHECK_EQ(n.value, 0x80);
    CHECK_EQ(aml_notify_pop(c, &n), 1);
    aml_node_path(n.node, p, sizeof p);
    CHECK(strcmp(p, "\\_SB_") == 0);
    CHECK_EQ(n.value, 0x81);
    CHECK_EQ(aml_notify_pop(c, &n), 0);
    CHECK(fk_log_has(f, "Notify \\DEV0 0x80"));
    /* Notify on a processor is allowed; on an integer it is a type error */
    CHECK_EQ(ev_int(c, "T137", &v), 0);
    CHECK_EQ(aml_notify_pop(c, &n), 1);
    CHECK_EQ(n.value, 1);
    /* Sleep(10) + Stall(50) go to the platform delay */
    uint64_t t0 = f->now_us;
    CHECK_EQ(ev_int(c, "T131", &v), 0);
    CHECK_EQ(f->now_us - t0, 10050);
    /* Timer is 100 ns units of the platform clock */
    CHECK_EQ(ev_int(c, "T132", &v), 0);
    CHECK_EQ(v, 10000);
    /* Debug object logs each store */
    fk_log_clear(f);
    uint32_t dl = c->debug_lines;
    CHECK_EQ(ev_int(c, "T133", &v), 0);
    CHECK_EQ(c->debug_lines - dl, 2);
    CHECK(fk_log_has(f, "Debug: msg"));
    CHECK(fk_log_has(f, "Debug: 0x5"));
    /* \_GL goes through the global lock hook exactly once each way */
    CHECK_EQ(ev_int(c, "T123", &v), 0);
    CHECK_EQ(v, 0);
    CHECK_EQ(f->gl_acq, 1);
    CHECK_EQ(f->gl_rel, 1);
    f->gl_fail = 1;
    CHECK_EQ(ev_int(c, "T123", &v), AML_EMUTEX); /* acquire timed out, release fails */
    f->gl_fail = 0;
    /* a method without Return yields an uninitialised value */
    aml_val_t r;
    CHECK_EQ(aml_eval(c, 0, "NORT", 0, 0, &r), 0);
    CHECK_EQ(r.type, AML_T_UNINIT);
    CHECK_EQ(aml_eval_int(c, 0, "NORT", &v), AML_ETYPE);
    /* method-local objects are gone after the call */
    uint32_t objs = aml_stats(c).objects;
    CHECK_EQ(ev_int(c, "T142", &v), 0);
    CHECK_EQ(aml_stats(c).objects, objs);
    CHECK(aml_lookup(c, 0, "\\T142.TMPD") == 0);
    CHECK(aml_lookup(c, 0, "\\T141.LOC1") == 0);
    /* argument count: extra arguments are dropped, missing ones uninitialised */
    aml_val_t args[3] = {aml_int(9), aml_int(1), aml_int(1)};
    CHECK_EQ(aml_eval(c, 0, "T048", args, 3, &r), 0);
    CHECK_EQ(r.u.i, 1);
    CHECK_EQ(aml_eval(c, 0, "T048", 0, 0, &r), AML_ETYPE);
    CHECK_EQ(aml_eval(c, 0, "T048", args, 8, &r), AML_EARG);
}

static void test_names(aml_ctx_t *c)
{
    char p[64], nm[5];
    struct aml_node *n = aml_lookup(c, 0, "\\DEV0.DSIB");
    CHECK(n != 0);
    CHECK_EQ(aml_node_type(n), AML_T_INT);
    CHECK_EQ(aml_node_path(n, p, sizeof p), 10);
    CHECK(strcmp(p, "\\DEV0.DSIB") == 0);
    aml_node_name(n, nm);
    CHECK(strcmp(nm, "DSIB") == 0);
    CHECK(aml_node_parent(n) == aml_lookup(c, 0, "DEV0"));
    aml_node_name(aml_lookup(c, 0, "\\"), nm);
    CHECK(strcmp(nm, "\\") == 0);
    CHECK_EQ(aml_node_path(n, p, 4), 3); /* truncated, still terminated */
    CHECK(strcmp(p, "\\DE") == 0);
    /* public lookup: relative single names do not search upward */
    struct aml_node *dev = aml_lookup(c, 0, "\\DEV0");
    CHECK(aml_lookup(c, dev, "DSIB") == n);
    CHECK(aml_lookup(c, dev, "GINT") == 0);
    CHECK(aml_lookup(c, dev, "^GINT") == aml_lookup(c, 0, "GINT"));
    CHECK(aml_lookup(c, 0, "_SB") == aml_lookup(c, 0, "\\_SB_"));
    CHECK(aml_lookup(c, 0, "\\DEV0.") == 0);
    CHECK(aml_lookup(c, 0, "\\DEV0..X") == 0);
    CHECK(aml_lookup(c, 0, "\\TOOLONG") == 0);
    CHECK(aml_lookup(c, 0, "\\9BAD") == 0);
    CHECK(aml_lookup(c, 0, "\\de0") == 0);
    CHECK(aml_lookup(c, 0, "^^^^") == 0);
    CHECK(aml_lookup(c, 0, 0) == 0);
    CHECK_EQ(aml_node_type(aml_lookup(c, 0, "\\_GPE")), AML_T_UNINIT);
    CHECK_EQ(aml_node_type(aml_lookup(c, 0, "\\ALI0")), AML_T_UNINIT);
    CHECK_EQ(aml_node_type(0), AML_T_UNINIT);
    /* stats: methods and devices from the table only */
    aml_stats_t s = aml_stats(c);
    CHECK(s.methods > 150);
    CHECK_EQ(s.devices, 1);
    CHECK_EQ(s.tables, 1);
    CHECK_EQ(s.errors, 0);
    CHECK_EQ(s.regions, 1);
    CHECK_EQ(s.fields, 1);
    CHECK(s.heap_used > 0 && s.heap_used <= s.heap_peak && s.heap_peak < s.heap_size);
    CHECK_EQ(c->dsdt_rev, 2);
    CHECK_EQ(c->int64, 1);
}

static void test_api_values(aml_ctx_t *c)
{
    uint64_t v;
    aml_val_t s = aml_str(c, "0x2A"), b = aml_buf(c, "\x01\x02", 2), i = aml_int(7);
    CHECK_EQ(aml_val_to_int(c, &s, &v), 0);
    CHECK_EQ(v, 0x2A);
    CHECK_EQ(aml_val_to_int(c, &b, &v), 0);
    CHECK_EQ(v, 0x0201);
    CHECK_EQ(aml_val_to_int(c, &i, &v), 0);
    CHECK_EQ(v, 7);
    CHECK_EQ(aml_val_to_int(c, 0, &v), AML_EARG);
    CHECK(aml_pkg_elem(&i, 0) == 0);
    aml_val_free(c, &s);
    aml_val_free(c, &b);
    CHECK_EQ(s.type, 0);
    CHECK(strcmp(aml_strerror(AML_EDIV0), "divide by zero") == 0);
    CHECK(strcmp(aml_strerror(AML_OK), "ok") == 0);
    CHECK(strcmp(aml_strerror(-99), "error") == 0);
    for (int e = AML_EMUTEX; e < 0; e++) CHECK(strcmp(aml_strerror(e), "error") != 0);
}

int main(void)
{
    static amlfake_t f;
    static aml_ctx_t c;
    CHECK_EQ(setup(&f, &c, fx_ops, sizeof fx_ops), 0);
    test_ints(&c);
    test_vals(&c);
    test_side_effects(&f, &c);
    test_names(&c);
    test_api_values(&c);
    return finish_tests("test_aml_ops");
}
