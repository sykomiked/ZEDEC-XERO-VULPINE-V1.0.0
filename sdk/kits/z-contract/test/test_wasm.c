/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_wasm.c — z-contract on Z-Wasm exports: input refusals that leave
 * the plugin READY, output violations that kill it like a trap and drop
 * its proposals, the evidence records, mapping errors, fail-closed on a
 * full log, the quarantine sink (kit API 0.2), and (when its path is given)
 * the z-wasm rational example.
 *
 *   test_wasm [rational_plugin.wasm]
 *
 * Modules are assembled in C with z-wasm's test/wasm_build.h. */
#include "wasm_build.h"
#include "zc_test.h"
#include "zcontract_wasm.h"

static zxv_ctx *ctx;
static zxv_cap *root, *budget_cap;
static zxv_rmag *rm;
static uint32_t bud;

static const char *zw_err(void) {
    static char b[512];
    size_t n = 0;
    if (zw_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = '\0';
    return b;
}

static int64_t budget_used(void) {
    int64_t un = 0, ud = 1;
    if (zxv_rmag_status(rm, bud, NULL, NULL, &un, &ud, NULL) != ZXV_OK) return -1;
    return ud == 1 ? un : -1;
}

static void setup(void) {
    if (zxv_ctx_create(&ctx) || zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root) ||
        zxv_cap_attenuate(root, ZXV_CAP_BUDGET, &budget_cap) || zxv_rmag_create_cap(root, &rm) ||
        zxv_rmag_budget(root, rm, "plugins", ZXV_RES_COMPUTE_CYCLES, INT64_C(1) << 40, 1, 1, &bud)) {
        fprintf(stderr, "setup failed\n");
        exit(2);
    }
}

/* fee(amount i64, bps i64) -> i32 = zxv.propose(2, amount * bps, 10000, 0, 0) */
static zw_plugin *load_fee(zw_host *h, int with_result) {
    static const uint8_t code_r[] = {0x41, 0x02, 0x20, 0x00, 0x20, 0x01, 0x7E, 0x42, 0x90, 0xCE,
                                     0x00, 0x41, 0x00, 0x41, 0x00, 0x10, 0x00};
    static const uint8_t code_v[] = {0x41, 0x02, 0x20, 0x00, 0x20, 0x01, 0x7E, 0x42, 0x90, 0xCE,
                                     0x00, 0x41, 0x00, 0x41, 0x00, 0x10, 0x00, 0x1A};
    wt t;
    wb m;
    zw_plugin *p = NULL;
    wt_one(&t, with_result ? code_r : code_v, with_result ? sizeof code_r : sizeof code_v);
    t.types[0] = with_result ? "II:i" : "II";
    t.types[1] = "iIIii:i";
    t.ntypes = 2;
    t.fimports[0].mod = "zxv";
    t.fimports[0].name = "propose";
    t.fimports[0].type = 1;
    t.nfimports = 1;
    t.exports[0] = "fee";
    t.nexports = 1;
    wt_build(&m, &t);
    if (zw_plugin_load(h, m.b, m.n, "fee", &p) != ZXV_OK) {
        fprintf(stderr, "load_fee: %s\n", zw_err());
        return NULL;
    }
    return p;
}

/* half(n i64, d i64) -> i32 = `ret` (no host calls) */
static zw_plugin *load_const(zw_host *h, uint8_t ret) {
    uint8_t code[] = {0x41, ret};
    wt t;
    wb m;
    zw_plugin *p = NULL;
    wt_one(&t, code, sizeof code);
    t.types[0] = "II:i";
    t.ntypes = 1;
    t.exports[0] = "half";
    t.nexports = 1;
    wt_build(&m, &t);
    if (zw_plugin_load(h, m.b, m.n, "const", &p) != ZXV_OK) return NULL;
    return p;
}

static uint32_t pending(zw_host *h) {
    uint32_t n = 0;
    (void) zw_queue_pending(h, &n);
    return n;
}

static void drain(zw_host *h) {
    uint64_t id;
    while (zw_queue_next(h, &id) == ZXV_OK) (void) zw_proposal_reject(h, root, id);
}

static int sink_calls;
static int32_t count_sink(void *user, uint64_t seq, uint32_t origin, int32_t tag, int32_t reason,
                          const uint8_t subject[32], const uint8_t hash[32], const char *text) {
    (void) user;
    (void) seq;
    (void) tag;
    (void) reason;
    (void) subject;
    (void) hash;
    (void) text;
    if (origin == ZC_ORIGIN_WASM) sink_calls++;
    return 0;
}

static void test_fee(void) {
    zw_host *h = NULL;
    zc_evlog *log = NULL;
    zc_wasm_guard *g = NULL;
    zw_plugin *p;
    int32_t st, state = -1, part = 0, reason = 0, tag = 0;
    int64_t res = -9, args[2];
    uint32_t index = 9;
    uint64_t seq = 0, count = 0, calls = 0, viol = 0;
    char msg[400], txt[300];
    size_t ml = 0;
    zc_contract *c = parse_ok("fn fee(amount: int[0..10^12], bps: int[0..10000]) -> int[0..0]\n"
                              "    proposes rat[0..10^12] den|100 max 1;\n");
    CHECK(c != NULL, "contract");
    CHECK(zw_host_create(budget_cap, rm, bud, &h) == ZXV_OK && zw_host_grant(h, ZW_GRANT_PROPOSE) == ZXV_OK,
          "host");
    CHECK(zc_evlog_create(16, &log) == ZXV_OK && zc_evlog_set_sink(log, count_sink, NULL) == ZXV_OK, "log");
    p = load_fee(h, 1);
    CHECK(p != NULL, "load fee");
    CHECK(zc_wasm_attach(p, "fee", c, "fee", log, &g) == ZXV_OK, "attach");
    CHECK(zc_wasm_last_violation(g, &part, &index, &reason, &seq, NULL, 0, NULL) == ZXV_E_NOT_FOUND,
          "no violation yet");

    /* a passing call reaches the queue as usual */
    args[0] = 48000;
    args[1] = 125;
    st = zw_plugin_call(p, "fee", args, 2, &res, NULL, NULL);
    CHECK(st == ZXV_OK && res == 0 && pending(h) == 1, "fee(48000, 125) = 600 passes (%s)",
          zw_status_name(st));

    /* bad input: refused before anything runs; the plugin stays READY */
    int64_t used0 = budget_used();
    args[0] = -1;
    res = -9;
    st = zw_plugin_call(p, "fee", args, 2, &res, NULL, NULL);
    CHECK(st == ZW_E_CHECK && res == -9, "negative amount refused (%s)", zw_status_name(st));
    CHECK(budget_used() == used0, "no fuel used");
    CHECK(zw_plugin_state(p, &state) == ZXV_OK && state == ZW_PLUGIN_READY, "still READY");
    CHECK(zc_wasm_last_violation(g, &part, &index, &reason, &seq, msg, sizeof msg, &ml) == ZXV_OK &&
              part == ZC_PART_PARAMS && index == 0 && reason == ZC_V_BELOW && seq == 1 &&
              strstr(msg, "fee: parameter 0 'amount' = -1 is below the minimum 0"), "input violation: %s", msg);
    CHECK(zc_evlog_get(log, 1, NULL, &part, &index, &reason, &tag, NULL, NULL, NULL, txt, sizeof txt, &ml) ==
              ZXV_OK && strcmp(txt, msg) == 0, "evidence record 1");
    char tn[64];
    size_t tl = 0;
    CHECK(zc_tag_format(tag, tn, sizeof tn, &tl) == ZXV_OK && strcmp(tn, "false/neutral/potential") == 0,
          "input refusal tag %s", tn);
    args[0] = 1;
    args[1] = 10001;
    CHECK(zw_plugin_call(p, "fee", args, 2, &res, NULL, NULL) == ZW_E_CHECK, "bps above 10000 refused");

    /* bad output: 333 * 125 / 10000 = 333/80; 80 does not divide 100 */
    int64_t used1 = budget_used();
    args[0] = 333;
    args[1] = 125;
    res = -9;
    st = zw_plugin_call(p, "fee", args, 2, &res, NULL, NULL);
    CHECK(st == ZW_E_CHECK && res == -9, "fee(333, 125) violates (%s)", zw_status_name(st));
    CHECK(budget_used() > used1, "the call ran and its fuel was charged");
    CHECK(pending(h) == 1, "its proposal never reached the queue (%u pending)", pending(h));
    CHECK(zw_plugin_state(p, &state) == ZXV_OK && state == ZW_PLUGIN_DEAD, "plugin DEAD, like a trap");
    CHECK(zc_wasm_last_violation(g, &part, &index, &reason, &seq, msg, sizeof msg, &ml) == ZXV_OK &&
              part == ZC_PART_PROPOSALS && index == 0 && reason == ZC_V_DENOMINATOR && seq == 3 &&
              strstr(msg, "fee: proposal 0 = 4.1625 (333/80): denominator 80 does not divide 100"), "output violation: %s", msg);
    CHECK(zc_evlog_get(log, 3, NULL, NULL, NULL, NULL, &tag, NULL, NULL, NULL, txt, sizeof txt, &ml) == ZXV_OK &&
              zc_tag_format(tag, tn, sizeof tn, &tl) == ZXV_OK && strcmp(tn, "false/forbidden/manifest") == 0,
          "output violation tag %s", tn);
    args[0] = 48000;
    CHECK(zw_plugin_call(p, "fee", args, 2, &res, NULL, NULL) == ZW_E_DEAD, "dead stays dead");
    CHECK(zc_evlog_count(log, &count) == ZXV_OK && count == 3 && zc_evlog_verify(log) == ZXV_OK && sink_calls == 3,
          "3 records, chain verifies, sink saw all");
    CHECK(zc_wasm_stats(g, &calls, &viol, &st) == ZXV_OK && calls == 4 && viol == 3 && st == 0, "stats %llu %llu",
          (unsigned long long) calls, (unsigned long long) viol);
    /* the subject digest identifies the offending values */
    {
        zc_vals *v = NULL;
        uint8_t want[32], got[32];
        zc_vals_create(&v);
        zc_vals_push_rat(v, 333, 80);
        zc_vals_digest(v, ZC_PART_PROPOSALS, want);
        zc_evlog_get(log, 3, NULL, NULL, NULL, NULL, NULL, NULL, got, NULL, txt, sizeof txt, &ml);
        CHECK(memcmp(want, got, 32) == 0, "subject digest of record 3 is the 333/80 proposal");
        uint8_t cd[32], rd[32];
        zc_contract_digest(c, cd);
        zc_evlog_get(log, 3, NULL, NULL, NULL, NULL, NULL, rd, NULL, NULL, txt, sizeof txt, &ml);
        CHECK(memcmp(cd, rd, 32) == 0, "record cites the contract digest");
        zc_vals_destroy(v);
    }
    zc_wasm_detach(g);
    zw_plugin_destroy(p);

    /* a fresh plugin; after detach nothing is checked */
    p = load_fee(h, 1);
    CHECK(zc_wasm_attach(p, "fee", c, "fee", log, &g) == ZXV_OK, "attach again");
    zc_wasm_detach(g);
    args[0] = 333;
    args[1] = 125;
    CHECK(zw_plugin_call(p, "fee", args, 2, &res, NULL, NULL) == ZXV_OK && pending(h) == 2,
          "detached: 333/80 is queued unchecked");
    drain(h);
    zw_plugin_destroy(p);

    /* a contract that declares no proposals, and one with a different arity */
    zc_contract *c2 = parse_ok("fn fee(amount: int, bps: int) -> int;\nfn one(amount: int) -> int;\n"
                               "fn noresult(amount: int, bps: int) proposes rat max 1;\n");
    p = load_fee(h, 1);
    CHECK(zc_wasm_attach(p, "fee", c2, "fee", log, &g) == ZXV_OK, "attach (no proposes)");
    args[0] = 100;
    args[1] = 100;
    st = zw_plugin_call(p, "fee", args, 2, &res, NULL, NULL);
    CHECK(st == ZW_E_CHECK && pending(h) == 0, "undeclared proposal kills the plugin");
    CHECK(zc_wasm_last_violation(g, &part, &index, &reason, NULL, msg, sizeof msg, &ml) == ZXV_OK &&
              reason == ZC_V_COUNT && strstr(msg, "but the contract declares none"), "%s", msg);
    zc_wasm_detach(g);
    zw_plugin_destroy(p);
    p = load_fee(h, 1);
    CHECK(zc_wasm_attach(p, "fee", c2, "one", log, &g) == ZXV_OK, "attach (arity 1 on a 2-arg export)");
    CHECK(zw_plugin_call(p, "fee", args, 2, &res, NULL, NULL) == ZW_E_CHECK, "arity refused");
    CHECK(zc_wasm_last_violation(g, &part, &index, &reason, NULL, msg, sizeof msg, &ml) == ZXV_OK &&
              reason == ZC_V_ARITY && strstr(msg, "2 Wasm arguments, the contract maps to 1"), "%s", msg);
    zc_wasm_detach(g);
    CHECK(zc_wasm_attach(p, "fee", c2, "noresult", log, &g) == ZXV_OK, "attach (no result declared)");
    CHECK(zw_plugin_call(p, "fee", args, 2, &res, NULL, NULL) == ZW_E_CHECK, "unexpected result");
    CHECK(zc_wasm_last_violation(g, &part, &index, &reason, NULL, msg, sizeof msg, &ml) == ZXV_OK &&
              part == ZC_PART_RESULT && reason == ZC_V_ARITY && strstr(msg, "none is declared"), "%s", msg);
    zc_wasm_detach(g);
    zw_plugin_destroy(p);
    p = load_fee(h, 0);
    CHECK(p && zc_wasm_attach(p, "fee", c, "fee", log, &g) == ZXV_OK, "attach to a void export");
    args[0] = 48000;
    args[1] = 125;
    CHECK(zw_plugin_call(p, "fee", args, 2, &res, NULL, NULL) == ZW_E_CHECK, "missing result");
    CHECK(zc_wasm_last_violation(g, &part, &index, &reason, NULL, msg, sizeof msg, &ml) == ZXV_OK &&
              part == ZC_PART_RESULT && reason == ZC_V_ARITY && strstr(msg, "the declared result is missing"),
          "%s", msg);
    zc_wasm_detach(g);
    zw_plugin_destroy(p);
    drain(h);
    zc_contract_destroy(c2);
    zc_contract_destroy(c);
    zc_evlog_destroy(log);
    zw_host_destroy(h);
}

static void test_mapping_and_rats(void) {
    zw_host *h = NULL;
    zc_evlog *log = NULL;
    zc_wasm_guard *g = NULL;
    zw_plugin *p;
    int32_t part = 0, reason = 0, st;
    uint32_t index = 0;
    int64_t res = 0, args[10];
    char msg[400];
    size_t ml = 0;
    zc_contract *c = parse_ok(
        "fn half(x: rat[0..1] den<=10) -> truth{true};\n"
        "fn b(x: bytes) -> int;\n"
        "fn r(x: int, y: int) -> rat;\n"
        "fn many(a: rat, b: rat, c: rat, d: rat, e: rat);\n"
        "fn eight(a: rat, b: rat, c: rat, d: int, e: int) -> int;\n");
    zw_host_create(budget_cap, rm, bud, &h);
    zc_evlog_create(2, &log);
    p = load_const(h, 1);
    CHECK(p != NULL, "load const");
    CHECK(zc_wasm_attach(p, "half", c, "b", log, &g) == ZC_E_MAPPING && strstr(last_err(), "bytes"), "bytes param");
    CHECK(zc_wasm_attach(p, "half", c, "r", log, &g) == ZC_E_MAPPING && strstr(last_err(), "proposes"), "rat result");
    CHECK(zc_wasm_attach(p, "half", c, "many", log, &g) == ZC_E_MAPPING && strstr(last_err(), "10 Wasm arguments"),
          "10 arguments");
    CHECK(zc_wasm_attach(p, "half", c, "nope", log, &g) == ZXV_E_NOT_FOUND, "unknown fn");
    CHECK(zc_wasm_attach(p, "nope", c, "half", log, &g) == ZW_E_EXPORT, "unknown export");
    CHECK(zc_wasm_attach(p, "half", c, "half", NULL, &g) == ZXV_E_ARG, "a log is required");
    CHECK(zc_wasm_attach(p, "half", c, "eight", log, &g) == ZXV_OK, "8 arguments map");
    zc_wasm_detach(g);
    CHECK(zc_wasm_attach(p, "half", c, "half", log, &g) == ZXV_OK, "attach half");

    /* zc_wasm_args flattens rationals */
    zc_vals *v = NULL;
    uint32_t n = 0;
    zc_vals_create(&v);
    zc_vals_push_rat(v, 1, 2);
    CHECK(zc_wasm_args(v, args, 1, &n) == ZXV_E_BUFFER && n == 2, "flatten size");
    CHECK(zc_wasm_args(v, args, 10, &n) == ZXV_OK && n == 2 && args[0] == 1 && args[1] == 2, "flatten 1/2");
    st = zw_plugin_call(p, "half", args, n, &res, NULL, NULL);
    CHECK(st == ZXV_OK && res == 1, "half(1/2) passes");
    args[0] = 6;
    args[1] = 12; /* 1/2 again, unreduced */
    CHECK(zw_plugin_call(p, "half", args, 2, &res, NULL, NULL) == ZXV_OK, "6/12 reduces to 1/2");
    args[0] = 1;
    args[1] = 11;
    CHECK(zw_plugin_call(p, "half", args, 2, &res, NULL, NULL) == ZW_E_CHECK, "1/11: den above 10");
    args[0] = 1;
    args[1] = 0;
    CHECK(zw_plugin_call(p, "half", args, 2, &res, NULL, NULL) == ZW_E_CHECK, "1/0 refused");
    CHECK(zc_wasm_last_violation(g, &part, &index, &reason, NULL, msg, sizeof msg, &ml) == ZXV_OK &&
              reason == ZC_V_INVALID, "%s", msg);
    zc_vals_push_bytes(v, (const uint8_t *) "x", 1);
    CHECK(zc_wasm_args(v, args, 10, &n) == ZC_E_MAPPING, "bytes do not flatten");
    zc_vals_destroy(v);

    /* the log holds 2 records: the third violation cannot be recorded, so
     * the guard fails closed and refuses even a good call */
    args[0] = 3;
    args[1] = 2;
    CHECK(zw_plugin_call(p, "half", args, 2, &res, NULL, NULL) == ZW_E_CHECK, "3/2 above 1");
    int32_t lf = 0;
    CHECK(zc_wasm_stats(g, NULL, NULL, &lf) == ZXV_OK && lf == 1, "the log refused: guard closed");
    uint64_t seq = 9;
    CHECK(zc_wasm_last_violation(g, NULL, NULL, NULL, &seq, NULL, 0, NULL) == ZXV_OK && seq == 0,
          "unrecorded violation has seq 0");
    args[0] = 1;
    args[1] = 2;
    CHECK(zw_plugin_call(p, "half", args, 2, &res, NULL, NULL) == ZW_E_CHECK, "good call refused after");
    int32_t state = -1;
    CHECK(zw_plugin_state(p, &state) == ZXV_OK && state == ZW_PLUGIN_READY, "input refusals never kill");
    zc_wasm_detach(g);
    zw_plugin_destroy(p);

    /* a result outside the truth set kills the plugin */
    zc_evlog_destroy(log);
    zc_evlog_create(4, &log);
    p = load_const(h, 2); /* returns FALSE */
    CHECK(zc_wasm_attach(p, "half", c, "half", log, &g) == ZXV_OK, "attach");
    args[0] = 1;
    args[1] = 2;
    CHECK(zw_plugin_call(p, "half", args, 2, &res, NULL, NULL) == ZW_E_CHECK, "FALSE result refused");
    CHECK(zc_wasm_last_violation(g, &part, &index, &reason, NULL, msg, sizeof msg, &ml) == ZXV_OK &&
              part == ZC_PART_RESULT && reason == ZC_V_NOT_IN_SET && strstr(msg, "half: result = FALSE is not in truth{true}"),
          "%s", msg);
    CHECK(zw_plugin_state(p, &state) == ZXV_OK && state == ZW_PLUGIN_DEAD, "dead");
    zc_wasm_detach(g);
    zw_plugin_destroy(p);
    zc_evlog_destroy(log);
    zc_contract_destroy(c);
    zw_host_destroy(h);
}

static void test_example(const char *path) {
    FILE *f = fopen(path, "rb");
    uint8_t *w;
    long n;
    zw_host *h = NULL;
    zw_plugin *p = NULL;
    zc_evlog *log = NULL;
    zc_wasm_guard *gf = NULL, *gr = NULL, *gc = NULL;
    int64_t a[2], res = 0;
    char *text;
    size_t tl;
    CHECK(f != NULL, "open %s", path);
    if (!f) return;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    w = (uint8_t *) malloc((size_t) n);
    CHECK(fread(w, 1, (size_t) n, f) == (size_t) n, "read");
    fclose(f);
    f = fopen("examples/fees.zc", "rb");
    CHECK(f != NULL, "open examples/fees.zc");
    if (!f) return;
    text = (char *) calloc(1, 8192);
    tl = fread(text, 1, 8191, f);
    fclose(f);
    zc_contract *c = NULL;
    CHECK(zc_parse(text, tl, &c, NULL, NULL, NULL, NULL, 0, NULL) == ZXV_OK, "fees.zc parses");
    zw_host_create(budget_cap, rm, bud, &h);
    zw_host_grant(h, ZW_GRANT_RATIONAL | ZW_GRANT_LOGIC | ZW_GRANT_PROPOSE);
    CHECK(zw_plugin_load(h, w, (size_t) n, "rational", &p) == ZXV_OK, "load rational_plugin.wasm");
    zc_evlog_create(64, &log);
    CHECK(zc_wasm_attach(p, "fee", c, "fee", log, &gf) == ZXV_OK &&
              zc_wasm_attach(p, "run", c, "run", log, &gr) == ZXV_OK &&
              zc_wasm_attach(p, "checksum", c, "checksum", log, &gc) == ZXV_OK, "attach fee, run, checksum");
    a[0] = 48000;
    a[1] = 125;
    CHECK(zw_plugin_call(p, "fee", a, 2, &res, NULL, NULL) == ZXV_OK && res == 0, "fee(48000, 125) = 600");
    a[0] = 12;
    CHECK(zw_plugin_call(p, "run", a, 1, &res, NULL, NULL) == ZXV_OK && res == ZXV_LP_TRUE, "run(12) TRUE");
    a[0] = 1000;
    CHECK(zw_plugin_call(p, "checksum", a, 1, &res, NULL, NULL) == ZXV_OK, "checksum(1000)");
    a[0] = 2000000;
    CHECK(zw_plugin_call(p, "checksum", a, 1, &res, NULL, NULL) == ZW_E_CHECK, "checksum(2*10^6) refused");
    a[0] = 0;
    CHECK(zw_plugin_call(p, "run", a, 1, &res, NULL, NULL) == ZW_E_CHECK, "run(0) refused (the plugin "
          "would return ZXV_E_ARG; the contract says why first)");
    CHECK(pending(h) == 2, "two proposals queued");
    a[0] = 333;
    a[1] = 125;
    CHECK(zw_plugin_call(p, "fee", a, 2, &res, NULL, NULL) == ZW_E_CHECK, "fee(333, 125) = 333/80 kills");
    CHECK(pending(h) == 2, "333/80 never queued");
    a[0] = 5;
    CHECK(zw_plugin_call(p, "run", a, 1, &res, NULL, NULL) == ZW_E_DEAD, "the whole plugin is dead");
    CHECK(zc_evlog_verify(log) == ZXV_OK, "evidence verifies");
    zc_wasm_detach(gf);
    zc_wasm_detach(gr);
    zc_wasm_detach(gc);
    zw_plugin_destroy(p);
    drain(h);
    zw_host_destroy(h);
    zc_evlog_destroy(log);
    zc_contract_destroy(c);
    free(text);
    free(w);
}

/* kit API 0.2: the guard's evidence log feeds a libzxv quarantine log. */
static void test_hexsink(void) {
    zw_host *h = NULL;
    zc_evlog *log = NULL;
    zc_wasm_guard *g = NULL;
    zxv_hexlog *q = NULL;
    zc_hexsink *hs = NULL;
    zw_plugin *p;
    int64_t args[2], res = 0;
    uint32_t count = 9, submitter = 9;
    uint8_t htag = 0, hreason = 0, subj[32], eh[32];
    int32_t state = -1, poisoned = 0;
    char txt[300];
    zc_contract *c = parse_ok("fn fee(amount: int[0..10^12], bps: int[0..10000]) -> int[0..0]\n"
                              "    proposes rat[0..10^12] den|100 max 1;\n");
    zxv_cap *attest = NULL;
    CHECK(c != NULL && zxv_cap_attenuate(root, ZXV_CAP_ATTEST, &attest) == ZXV_OK &&
              zxv_hexlog_create(attest, 2, &q) == ZXV_OK && zc_hexsink_create(attest, q, 0, &hs) == ZXV_OK &&
              zc_hexsink_halt_on_full(hs, ctx) == ZXV_OK && zc_evlog_create(16, &log) == ZXV_OK &&
              zc_evlog_attach_hexsink(log, hs) == ZXV_OK,
          "evidence log with a quarantine sink");
    CHECK(zw_host_create(budget_cap, rm, bud, &h) == ZXV_OK && zw_host_grant(h, ZW_GRANT_PROPOSE) == ZXV_OK,
          "host");
    p = load_fee(h, 1);
    CHECK(p != NULL && zc_wasm_attach(p, "fee", c, "fee", log, &g) == ZXV_OK, "attach");
    if (!p || !g) return;

    /* bad input: quarantined as false/forbidden/potential, plugin READY */
    args[0] = -5;
    args[1] = 125;
    CHECK(zw_plugin_call(p, "fee", args, 2, &res, NULL, NULL) == ZW_E_CHECK &&
              zw_plugin_state(p, &state) == ZXV_OK && state == ZW_PLUGIN_READY,
          "input refused");
    CHECK(zxv_hexlog_count(q, &count) == ZXV_OK && count == 1 &&
              zxv_hexlog_at(q, 0, NULL, &htag, &hreason, &submitter, subj, NULL, NULL) == ZXV_OK &&
              strcmp(zxv_hex_name(htag), "false/forbidden/potential") == 0 && hreason == ZC_V_BELOW &&
              submitter == ZC_ORIGIN_WASM,
          "quarantine record 0: %s reason %u submitter %u", zxv_hex_name(htag), (unsigned) hreason,
          (unsigned) submitter);
    CHECK(zc_evlog_get(log, 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, eh, txt, sizeof txt, NULL) == ZXV_OK &&
              memcmp(eh, subj, 32) == 0,
          "its subject is the evidence record's hash");

    /* a bad output (a fraction with den 10000 against den|100): forbidden/manifest */
    args[0] = 333;
    args[1] = 125;
    CHECK(zw_plugin_call(p, "fee", args, 2, &res, NULL, NULL) == ZW_E_CHECK &&
              zw_plugin_state(p, &state) == ZXV_OK && state == ZW_PLUGIN_DEAD && pending(h) == 0,
          "output violation kills the plugin, nothing queued");
    CHECK(zxv_hexlog_count(q, &count) == ZXV_OK && count == 2 &&
              zxv_hexlog_at(q, 1, NULL, &htag, &hreason, &submitter, NULL, NULL, NULL) == ZXV_OK &&
              strcmp(zxv_hex_name(htag), "false/forbidden/manifest") == 0 && hreason == ZC_V_DENOMINATOR,
          "quarantine record 1: %s reason %u", zxv_hex_name(htag), (unsigned) hreason);
    CHECK(zxv_hexlog_verify(q) == ZXV_OK, "the quarantine chain verifies");
    zc_wasm_detach(g);
    zw_plugin_destroy(p);

    /* the quarantine log is now full: the next violation cannot be
     * recorded, so the guard fails closed and the context is halted */
    p = load_fee(h, 1);
    g = NULL;
    CHECK(p != NULL && zc_wasm_attach(p, "fee", c, "fee", log, &g) == ZXV_OK, "re-attach");
    if (p && g) {
        int32_t log_failed = 0;
        args[0] = -1;
        CHECK(zw_plugin_call(p, "fee", args, 2, &res, NULL, NULL) == ZW_E_CHECK &&
                  zc_wasm_stats(g, NULL, NULL, &log_failed) == ZXV_OK && log_failed == 1,
              "full quarantine log: the guard fails closed");
        args[0] = 48000;
        CHECK(zw_plugin_call(p, "fee", args, 2, &res, NULL, NULL) == ZW_E_CHECK && pending(h) == 0,
              "even a good call is refused afterwards");
        CHECK(zxv_ctx_is_poisoned(ctx, &poisoned) == ZXV_OK && poisoned == 1,
              "halt_on_full halted the context");
    }
    zc_wasm_detach(g);
    zw_plugin_destroy(p);
    drain(h);
    zw_host_destroy(h);
    zc_evlog_destroy(log);
    zc_hexsink_destroy(hs);
    zxv_hexlog_destroy(q);
    zxv_cap_destroy(attest);
    zc_contract_destroy(c);
}

int main(int argc, char **argv) {
    printf("test_wasm: z-contract on Z-Wasm exports\n");
    setup();
    test_fee();
    test_mapping_and_rats();
    if (argc >= 2)
        test_example(argv[1]);
    else
        printf("  (rational_plugin.wasm not given: skipping the example-plugin tests)\n");
    test_hexsink(); /* last: it halts the shared context on purpose */
    zxv_rmag_destroy(rm);
    zxv_cap_destroy(budget_cap);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    printf("test_wasm: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
