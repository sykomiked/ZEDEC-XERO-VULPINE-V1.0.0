/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_host.c — the Z-Wasm host: fuel charged to an rmag budget, traps,
 * grants, the S0 proposal queue and fail-closed behaviour.
 *
 *   test_host [rational_plugin.wasm float_plugin.wasm spin_plugin.wasm]
 *
 * The example-plugin tests run when the three paths are given (the
 * Makefile builds them with zig); everything else uses modules assembled
 * in C. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wasm_build.h"
#include "zwasm.h"

static int g_checks, g_fail;

#define CHECK(cond, ...)                                                                       \
    do {                                                                                       \
        g_checks++;                                                                            \
        if (!(cond)) {                                                                         \
            g_fail++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                               \
            fprintf(stderr, __VA_ARGS__);                                                      \
            fprintf(stderr, " [last error: %s]\n", last_err());                                \
        }                                                                                      \
    } while (0)

static const char *last_err(void) {
    static char b[512];
    size_t n = 0;
    if (zw_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = '\0';
    return b;
}

static zxv_ctx *ctx;
static zxv_cap *root, *budget_cap, *post_cap, *read_cap;
static zxv_rmag *rm;
static uint32_t bud;
static uint32_t acct_issuer, acct_holder;

static uint8_t *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (uint8_t *) malloc((size_t) n);
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    *len = (size_t) n;
    return b;
}

static int64_t budget_used(void) {
    int64_t un = 0, ud = 1;
    if (zxv_rmag_status(rm, bud, NULL, NULL, &un, &ud, NULL) != ZXV_OK) return -1;
    return ud == 1 ? un : -1;
}

static zw_host *new_host(uint32_t grants) {
    zw_host *h = NULL;
    if (zw_host_create(budget_cap, rm, bud, &h) != ZXV_OK) return NULL;
    if (grants && zw_host_grant(h, grants) != ZXV_OK) return NULL;
    return h;
}

static zw_plugin *load_wt(zw_host *h, const wt *t, int32_t *out_st) {
    wb m;
    zw_plugin *p = NULL;
    wt_build(&m, t);
    *out_st = zw_plugin_load(h, m.b, m.n, "test", &p);
    return p;
}

static void setup(void) {
    if (zxv_ctx_create(&ctx) != ZXV_OK) abort();
    if (zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root) != ZXV_OK) abort();
    if (zxv_cap_attenuate(root, ZXV_CAP_BUDGET, &budget_cap) != ZXV_OK) abort();
    if (zxv_cap_attenuate(root, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ, &post_cap) != ZXV_OK)
        abort();
    if (zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &read_cap) != ZXV_OK) abort();
    if (zxv_rmag_create_cap(root, &rm) != ZXV_OK) abort();
    if (zxv_rmag_budget(root, rm, "plugins", ZXV_RES_COMPUTE_CYCLES, 100000000, 1, 1, &bud) !=
        ZXV_OK)
        abort();
    if (zxv_ccy_enable_cap(post_cap, "NGN") != ZXV_OK) abort();
    if (zxv_ledger_open_account_cap(post_cap, "issuer", "NGN", ZXV_ACCT_ISSUER, &acct_issuer) !=
        ZXV_OK)
        abort();
    if (zxv_ledger_open_account_cap(post_cap, "holder", "NGN", ZXV_ACCT_HOLDER, &acct_holder) !=
        ZXV_OK)
        abort();
}

static void test_basics(void) {
    zw_host *h = new_host(0);
    wt t;
    int32_t st, state;
    int64_t res = 0;
    int32_t has = 0;
    uint64_t fuel = 0;
    static const uint8_t add[] = {0x20, 0x00, 0x20, 0x01, 0x7C}; /* local.get 0 1 i64.add */
    CHECK(h != NULL, "host");
    CHECK(zw_host_create(read_cap, rm, bud, &(zw_host *){NULL}) == ZXV_E_CAP, "host needs BUDGET");
    CHECK(zw_host_create(budget_cap, rm, 999, &(zw_host *){NULL}) != ZXV_OK, "unknown budget");
    CHECK(zw_host_grant(h, 0x80) == ZXV_E_ARG, "unknown grant");

    wt_one(&t, add, sizeof add);
    t.types[0] = "II:I";
    t.exports[0] = "add";
    t.nexports = 1;
    zw_plugin *p = load_wt(h, &t, &st);
    CHECK(st == ZXV_OK && p, "load add: %s", zw_status_name(st));
    int64_t before = budget_used();
    int64_t args[2] = {40, 2};
    st = zw_plugin_call(p, "add", args, 2, &res, &has, &fuel);
    CHECK(st == ZXV_OK && has && res == 42, "add = %lld (%s)", (long long) res, zw_status_name(st));
    /* 4 instructions: local.get, local.get, i64.add, end */
    CHECK(fuel == 4, "fuel used %llu, want 4", (unsigned long long) fuel);
    CHECK(budget_used() - before == 4, "budget charged %lld", (long long) (budget_used() - before));
    int64_t big[2] = {INT64_MAX, 1};
    st = zw_plugin_call(p, "add", big, 2, &res, &has, NULL);
    CHECK(st == ZXV_OK && res == INT64_MIN, "i64 wraps like Wasm");
    CHECK(zw_plugin_call(p, "nope", args, 2, &res, &has, NULL) == ZW_E_EXPORT, "no export");
    CHECK(zw_plugin_call(p, "add", args, 1, &res, &has, NULL) == ZW_E_EXPORT, "arg count");
    CHECK(zw_plugin_call(p, "add", args, 9, &res, &has, NULL) == ZXV_E_ARG, "9 args");
    CHECK(zw_plugin_state(p, &state) == ZXV_OK && state == ZW_PLUGIN_READY, "ready");
    zw_plugin_destroy(p);

    /* i32 parameter range */
    static const uint8_t id32[] = {0x20, 0x00};
    wt_one(&t, id32, sizeof id32);
    t.types[0] = "i:i";
    t.exports[0] = "id";
    t.nexports = 1;
    p = load_wt(h, &t, &st);
    CHECK(st == ZXV_OK, "load id");
    int64_t a = -5;
    CHECK(zw_plugin_call(p, "id", &a, 1, &res, &has, NULL) == ZXV_OK && res == -5, "i32 -5");
    a = (int64_t) UINT32_MAX;
    CHECK(zw_plugin_call(p, "id", &a, 1, &res, &has, NULL) == ZXV_OK && res == -1, "u32 max -> -1");
    a = (int64_t) UINT32_MAX + 1;
    CHECK(zw_plugin_call(p, "id", &a, 1, &res, &has, NULL) == ZXV_E_RANGE, "2^32 refused");
    a = (int64_t) INT32_MIN - 1;
    CHECK(zw_plugin_call(p, "id", &a, 1, &res, &has, NULL) == ZXV_E_RANGE, "below INT32_MIN");
    zw_plugin_destroy(p);

    /* memory.grow past the declared maximum fails inside Wasm (-1), no trap */
    static const uint8_t grow[] = {0x41, 0x05, 0x40, 0x00};
    wt_one(&t, grow, sizeof grow);
    t.types[0] = ":i";
    t.memory = 1;
    t.mem_min = 1;
    t.mem_max = 2;
    t.exports[0] = "grow";
    t.nexports = 1;
    p = load_wt(h, &t, &st);
    CHECK(st == ZXV_OK, "load grow");
    CHECK(zw_plugin_call(p, "grow", NULL, 0, &res, &has, NULL) == ZXV_OK && res == -1,
          "grow beyond max = -1, got %lld", (long long) res);
    zw_plugin_destroy(p);

    /* the host policy is applied */
    zw_policy *pol;
    CHECK(zw_host_policy(h, &pol) == ZXV_OK, "policy");
    CHECK(zw_policy_set_limit(pol, ZW_LIM_MEMORY_PAGES, 1) == ZXV_OK, "pages 1");
    p = load_wt(h, &t, &st);
    CHECK(st == ZW_E_REJECTED && !p && strstr(last_err(), "MEMORY_LIMIT"), "host policy pages");
    zw_host_destroy(h);
}

static void test_traps_and_fuel(void) {
    zw_host *h = new_host(ZW_GRANT_PROPOSE);
    wt t;
    int32_t st, state, poisoned = -1;
    uint64_t fuel = 0;
    int64_t res = 0;
    static const uint8_t spin[] = {0x03, 0x40, 0x0C, 0x00, 0x0B};
    static const uint8_t boom[] = {0x00};
    static const uint8_t div0[] = {0x41, 0x01, 0x41, 0x00, 0x6D};
    CHECK(zw_host_set_call_fuel(h, 1000) == ZXV_OK, "call fuel");
    CHECK(zw_host_set_call_fuel(h, 0) == ZXV_E_ARG, "call fuel 0");

    wt_one(&t, spin, sizeof spin);
    t.exports[0] = "spin";
    t.nexports = 1;
    zw_plugin *p = load_wt(h, &t, &st);
    CHECK(st == ZXV_OK, "load spin");
    int64_t before = budget_used();
    st = zw_plugin_call(p, "spin", NULL, 0, NULL, NULL, &fuel);
    CHECK(st == ZW_E_FUEL, "spin stops on fuel: %s", zw_status_name(st));
    CHECK(fuel == 1000 && budget_used() - before == 1000, "fuel 1000 charged exactly (%llu)",
          (unsigned long long) fuel);
    CHECK(zw_plugin_state(p, &state) == ZXV_OK && state == ZW_PLUGIN_DEAD, "dead after fuel");
    CHECK(zw_plugin_call(p, "spin", NULL, 0, NULL, NULL, NULL) == ZW_E_DEAD, "dead refuses");
    zw_plugin_destroy(p);

    wt_one(&t, boom, sizeof boom);
    t.exports[0] = "boom";
    t.nexports = 1;
    p = load_wt(h, &t, &st);
    st = zw_plugin_call(p, "boom", NULL, 0, NULL, NULL, &fuel);
    CHECK(st == ZW_E_TRAP && strstr(last_err(), "unreachable"), "unreachable traps");
    zw_plugin_destroy(p);

    wt_one(&t, div0, sizeof div0);
    t.types[0] = ":i";
    t.exports[0] = "div0";
    t.nexports = 1;
    p = load_wt(h, &t, &st);
    st = zw_plugin_call(p, "div0", NULL, 0, &res, NULL, NULL);
    CHECK(st == ZW_E_TRAP && strstr(last_err(), "divide"), "div by zero traps: %s", last_err());
    zw_plugin_destroy(p);

    /* the host's context is untouched by any of it */
    CHECK(zxv_ctx_is_poisoned(ctx, &poisoned) == ZXV_OK && poisoned == 0, "ctx not poisoned");
    CHECK(zxv_ledger_check_cap(read_cap) == ZXV_OK, "ledger intact");

    /* budget exhaustion: leave 3 units; `add` needs 4 */
    static const uint8_t add[] = {0x20, 0x00, 0x20, 0x01, 0x7C};
    wt_one(&t, add, sizeof add);
    t.types[0] = "II:I";
    t.exports[0] = "add";
    t.nexports = 1;
    p = load_wt(h, &t, &st);
    int64_t used = budget_used();
    CHECK(zxv_rmag_set_limit(root, rm, bud, used + 3, 1) == ZXV_OK, "limit");
    int64_t args[2] = {1, 2};
    st = zw_plugin_call(p, "add", args, 2, &res, NULL, &fuel);
    CHECK(st == ZW_E_FUEL && fuel == 3, "3 units are not enough: %s %llu", zw_status_name(st),
          (unsigned long long) fuel);
    zw_plugin_destroy(p);
    p = load_wt(h, &t, &st);
    st = zw_plugin_call(p, "add", args, 2, &res, NULL, &fuel);
    CHECK(st == ZXV_E_BUDGET, "empty budget: %s", zw_status_name(st));
    CHECK(zw_plugin_state(p, &state) == ZXV_OK && state == ZW_PLUGIN_READY,
          "a refused call does not kill the plugin");
    CHECK(zxv_rmag_set_limit(root, rm, bud, 100000000, 1) == ZXV_OK, "refill");
    st = zw_plugin_call(p, "add", args, 2, &res, NULL, &fuel);
    CHECK(st == ZXV_OK && res == 3, "after refill");
    /* revoking the budget capability stops plugin calls */
    zxv_cap *bc2 = NULL;
    zw_host *h2 = NULL;
    CHECK(zxv_cap_attenuate(root, ZXV_CAP_BUDGET, &bc2) == ZXV_OK, "cap");
    CHECK(zw_host_create(bc2, rm, bud, &h2) == ZXV_OK, "host2");
    zw_plugin *p2 = load_wt(h2, &t, &st);
    CHECK(zw_plugin_call(p2, "add", args, 2, &res, NULL, NULL) == ZXV_OK, "h2 call");
    CHECK(zxv_cap_revoke(bc2) == ZXV_OK, "revoke");
    CHECK(zw_plugin_call(p2, "add", args, 2, &res, NULL, NULL) == ZXV_E_CAP, "revoked cap");
    zw_plugin_destroy(p2);
    zw_host_destroy(h2);
    zxv_cap_destroy(bc2);
    zw_plugin_destroy(p);
    zw_host_destroy(h);
}

static void test_queue(void) {
    zw_host *h = new_host(ZW_GRANT_PROPOSE);
    zw_host *nogrant = new_host(ZW_GRANT_RATIONAL);
    wt t;
    int32_t st, state = -1;
    int64_t res = 0, num = 0, den = 0;
    uint32_t pending = 0, kind = 0, naccts0 = 0, naccts1 = 0;
    uint64_t id = 0;
    char who[32];
    /* zxv.propose(7, 3, 4, 0, 0) */
    static const uint8_t prop[] = {0x41, 0x07, 0x42, 0x03, 0x42, 0x04, 0x41, 0x00, 0x41, 0x00,
                                   0x10, 0x00};
    wt_one(&t, prop, sizeof prop);
    t.types[0] = ":i";
    t.types[1] = "iIIii:i";
    t.ntypes = 2;
    t.fimports[0].mod = "zxv";
    t.fimports[0].name = "propose";
    t.fimports[0].type = 1;
    t.nfimports = 1;
    t.exports[0] = "p";
    t.nexports = 1;

    zw_plugin *p = load_wt(nogrant, &t, &st);
    CHECK(st == ZW_E_REJECTED && !p && strstr(last_err(), "IMPORT_DENIED"),
          "propose without the grant is refused");
    p = load_wt(h, &t, &st);
    CHECK(st == ZXV_OK, "load proposer");
    CHECK(zxv_ledger_account_count_cap(read_cap, &naccts0) == ZXV_OK, "accounts");
    st = zw_plugin_call(p, "p", NULL, 0, &res, NULL, NULL);
    CHECK(st == ZXV_OK && res == 0, "propose ok (%lld)", (long long) res);
    CHECK(zw_queue_pending(h, &pending) == ZXV_OK && pending == 1, "1 pending");
    CHECK(zw_queue_next(h, &id) == ZXV_OK && id == 1, "id 1");
    CHECK(zw_proposal_get(h, id, &state, &kind, &num, &den, who, NULL, 0, NULL) == ZXV_OK, "get");
    CHECK(state == ZXV_LP_NEITHER && kind == 7 && num == 3 && den == 4 && strcmp(who, "test") == 0,
          "S0 proposal 3/4 kind 7");
    CHECK(zxv_ledger_account_count_cap(read_cap, &naccts1) == ZXV_OK && naccts1 == naccts0,
          "ledger untouched by a proposal");
    CHECK(zw_proposal_accept(h, read_cap, id) == ZXV_E_CAP, "read-only cap cannot accept");
    CHECK(zw_proposal_get(h, id, &state, NULL, NULL, NULL, NULL, NULL, 0, NULL) == ZXV_OK &&
              state == ZXV_LP_NEITHER, "still S0");
    CHECK(zw_proposal_accept(h, post_cap, id) == ZXV_OK, "accept");
    CHECK(zw_proposal_get(h, id, &state, NULL, NULL, NULL, NULL, NULL, 0, NULL) == ZXV_OK &&
              state == ZXV_LP_TRUE, "TRUE");
    CHECK(zw_proposal_accept(h, post_cap, id) == ZXV_E_STATE, "decided once");
    CHECK(zw_proposal_reject(h, post_cap, id) == ZXV_E_STATE, "decided once (reject)");
    CHECK(zw_queue_next(h, &id) == ZXV_E_NOT_FOUND, "queue empty");
    CHECK(zw_proposal_get(h, 999, &state, NULL, NULL, NULL, NULL, NULL, 0, NULL) ==
              ZXV_E_NOT_FOUND, "unknown id");

    st = zw_plugin_call(p, "p", NULL, 0, &res, NULL, NULL);
    CHECK(zw_queue_next(h, &id) == ZXV_OK && id == 2, "id 2");
    CHECK(zw_proposal_reject(h, post_cap, id) == ZXV_OK, "reject");
    CHECK(zw_proposal_get(h, id, &state, NULL, NULL, NULL, NULL, NULL, 0, NULL) == ZXV_OK &&
              state == ZXV_LP_FALSE, "FALSE");
    CHECK(zw_host_set_accept_caps(h, ZXV_CAP_ATTEST) == ZXV_OK, "accept caps ATTEST");
    st = zw_plugin_call(p, "p", NULL, 0, &res, NULL, NULL);
    CHECK(zw_queue_next(h, &id) == ZXV_OK, "id 3");
    CHECK(zw_proposal_accept(h, post_cap, id) == ZXV_E_CAP, "post cap lacks ATTEST");
    CHECK(zw_proposal_accept(h, root, id) == ZXV_OK, "root has ATTEST");
    CHECK(zw_host_set_accept_caps(h, ZXV_CAP_LEDGER_POST) == ZXV_OK, "accept caps back");

    /* the pending cap: 1024 */
    int full = 0;
    for (int i = 0; i < 1100; i++) {
        st = zw_plugin_call(p, "p", NULL, 0, &res, NULL, NULL);
        if (st == ZXV_OK && res == ZXV_E_CAPACITY) full++;
    }
    CHECK(zw_queue_pending(h, &pending) == ZXV_OK && pending == 1024 && full == 1100 - 1024,
          "pending capped at 1024 (%u, %d refused)", pending, full);
    while (zw_queue_next(h, &id) == ZXV_OK) (void) zw_proposal_reject(h, post_cap, id);
    CHECK(zw_queue_pending(h, &pending) == ZXV_OK && pending == 0, "drained");
    st = zw_plugin_call(p, "p", NULL, 0, &res, NULL, NULL);
    CHECK(st == ZXV_OK && res == 0, "proposing again after draining");
    zw_plugin_destroy(p);
    zw_host_destroy(h);
    zw_host_destroy(nogrant);
}

static void test_examples(const char *rational, const char *flt, const char *spin) {
    size_t len = 0;
    uint8_t *w;
    zw_plugin *p = NULL;
    int32_t st;
    int64_t res = 0, num = 0, den = 0;
    uint64_t id = 0, entry = 0, fuel = 0;
    uint8_t label[65];
    size_t llen = 0;
    zw_host *h = new_host(ZW_GRANT_RATIONAL | ZW_GRANT_LOGIC | ZW_GRANT_PROPOSE);

    /* the float plugin is refused, with a reason a developer can act on */
    w = slurp(flt, &len);
    CHECK(w != NULL, "read %s", flt);
    st = zw_plugin_load(h, w, len, "float", &p);
    CHECK(st == ZW_E_REJECTED && !p, "float plugin rejected");
    CHECK(strstr(last_err(), "FLOAT_OP") && strstr(last_err(), "f64") &&
              strstr(last_err(), "offset"), "float message: %s", last_err());
    printf("  float plugin: %s\n", last_err());
    free(w);

    w = slurp(rational, &len);
    CHECK(w != NULL, "read %s", rational);
    st = zw_plugin_load(h, w, len, "rational", &p);
    CHECK(st == ZXV_OK && p, "load rational plugin: %s", zw_status_name(st));
    free(w);
    int64_t n = 10;
    st = zw_plugin_call(p, "run", &n, 1, &res, NULL, &fuel);
    CHECK(st == ZXV_OK && res == ZXV_LP_TRUE, "run(10) verdict %lld (%s)", (long long) res,
          zw_status_name(st));
    CHECK(zw_queue_next(h, &id) == ZXV_OK, "proposal");
    CHECK(zw_proposal_get(h, id, NULL, NULL, &num, &den, NULL, label, sizeof label, &llen) ==
              ZXV_OK && num == 10 && den == 11, "sum = 10/11, got %lld/%lld", (long long) num,
          (long long) den);
    CHECK(llen == strlen("telescoping sum n/(n+1)") &&
              memcmp(label, "telescoping sum n/(n+1)", llen) == 0, "label");
    CHECK(zw_proposal_get(h, id, NULL, NULL, NULL, NULL, NULL, NULL, 0, &llen) == ZXV_E_BUFFER,
          "label two-call");
    CHECK(zw_proposal_accept_post(h, post_cap, id, acct_issuer, acct_holder, "NGN", "zw-1",
                                  &entry) == ZXV_E_RANGE, "10/11 cannot be posted");
    CHECK(zw_proposal_reject(h, post_cap, id) == ZXV_OK, "reject the fraction");

    /* fee(10000, 250) = 250 minor units: accept and post with the host's cap */
    int64_t fa[2] = {10000, 250};
    int64_t d0 = 0, c0 = 0, e0 = 0, e1 = 0;
    CHECK(zxv_ledger_balance_cap(read_cap, acct_holder, &d0, &c0, &e0) == ZXV_OK, "balance");
    st = zw_plugin_call(p, "fee", fa, 2, &res, NULL, NULL);
    CHECK(st == ZXV_OK && res == 0, "fee proposes");
    CHECK(zw_queue_next(h, &id) == ZXV_OK, "fee proposal");
    CHECK(zxv_ledger_balance_cap(read_cap, acct_holder, NULL, NULL, &e1) == ZXV_OK && e1 == e0,
          "nothing posted before acceptance");
    CHECK(zw_proposal_accept_post(h, read_cap, id, acct_issuer, acct_holder, "NGN", "zw-fee-1",
                                  &entry) == ZXV_E_CAP, "read cap cannot post");
    CHECK(zw_proposal_accept_post(h, post_cap, id, acct_holder, acct_issuer, "NGN", "zw-fee-0",
                                  &entry) == ZXV_E_FUNDS, "holder has no funds");
    CHECK(zw_proposal_accept_post(h, post_cap, id, acct_issuer, acct_holder, "NGN", "zw-fee-1",
                                  &entry) == ZXV_OK, "accept + post: %s", last_err());
    CHECK(zxv_ledger_balance_cap(read_cap, acct_holder, NULL, NULL, &e1) == ZXV_OK &&
              e1 == e0 + 250, "holder +250 (equity %lld)", (long long) e1);
    CHECK(zxv_ledger_check_cap(read_cap) == ZXV_OK, "ledger invariants hold");

    /* fuel grows exactly linearly with the work (n a multiple of any
     * unroll factor the compiler chose, so each step adds the same) */
    uint64_t f0 = 0, f1 = 0, f2 = 0;
    int64_t k0 = 1024, k1 = 2048, k2 = 3072;
    CHECK(zw_plugin_call(p, "checksum", &k0, 1, &res, NULL, &f0) == ZXV_OK, "checksum 1024");
    CHECK(zw_plugin_call(p, "checksum", &k1, 1, &res, NULL, &f1) == ZXV_OK, "checksum 2048");
    CHECK(zw_plugin_call(p, "checksum", &k2, 1, &res, NULL, &f2) == ZXV_OK, "checksum 3072");
    CHECK(f2 - f1 == f1 - f0 && f1 > f0, "fuel linear: %llu %llu %llu", (unsigned long long) f0,
          (unsigned long long) f1, (unsigned long long) f2);
    printf("  checksum fuel: n=1024 %llu, n=2048 %llu, n=3072 %llu\n", (unsigned long long) f0,
           (unsigned long long) f1, (unsigned long long) f2);
    zw_plugin_destroy(p);

    /* misbehaving plugins */
    w = slurp(spin, &len);
    CHECK(w != NULL, "read %s", spin);
    CHECK(zw_host_set_call_fuel(h, 200000) == ZXV_OK, "fuel");
    uint32_t pend0 = 0, pend1 = 0;
    struct {
        const char *fn;
        int64_t arg;
        int nargs;
        int32_t want;
    } bad[] = {
        {"spin", 1, 1, ZW_E_FUEL},
        {"propose_trap", 0, 0, ZW_E_TRAP},
        {"poke", 1 << 20, 1, ZW_E_TRAP},
        {"recurse", 0, 1, 0 /* trap or fuel */},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        int32_t state = -1, poisoned = -1;
        st = zw_plugin_load(h, w, len, "spin", &p);
        CHECK(st == ZXV_OK, "load spin plugin");
        (void) zw_queue_pending(h, &pend0);
        st = zw_plugin_call(p, bad[i].fn, &bad[i].arg, (uint32_t) bad[i].nargs, &res, NULL, &fuel);
        if (bad[i].want)
            CHECK(st == bad[i].want, "%s: %s", bad[i].fn, zw_status_name(st));
        else
            CHECK(st == ZW_E_TRAP || st == ZW_E_FUEL, "%s: %s", bad[i].fn, zw_status_name(st));
        printf("  %s -> %s: %s\n", bad[i].fn, zw_status_name(st), last_err());
        (void) zw_queue_pending(h, &pend1);
        CHECK(pend1 == pend0, "%s: no proposal reaches the queue", bad[i].fn);
        CHECK(zw_plugin_state(p, &state) == ZXV_OK && state == ZW_PLUGIN_DEAD, "%s dead", bad[i].fn);
        CHECK(zxv_ctx_is_poisoned(ctx, &poisoned) == ZXV_OK && poisoned == 0,
              "%s: host context not poisoned", bad[i].fn);
        zw_plugin_destroy(p);
    }
    /* poke inside memory works */
    st = zw_plugin_load(h, w, len, "spin", &p);
    int64_t addr = 1024;
    CHECK(zw_plugin_call(p, "poke", &addr, 1, &res, NULL, NULL) == ZXV_OK && res == 42, "poke 1024");
    zw_plugin_destroy(p);
    free(w);
    zw_host_destroy(h);
}

/* Call checks (kit API 0.2): the hook z-contract uses. */
typedef struct {
    int before, after, nested_state, staged_seen;
    int64_t staged_num, staged_den;
} chk_rec;

static int32_t chk_fn(void *user, zw_plugin *pl, int32_t stage, const char *export_name,
                      const int64_t *args, uint32_t nargs, int64_t result, int32_t has_result) {
    chk_rec *r = (chk_rec *) user;
    uint32_t n = 0, kind = 0;
    int64_t num = 0, den = 0;
    (void) export_name;
    r->nested_state = zw_plugin_call(pl, "p", args, nargs, NULL, NULL, NULL);
    if (stage == ZW_CHECK_BEFORE) {
        r->before++;
        return nargs == 1 && args[0] >= 0 ? 0 : 1;
    }
    r->after++;
    if (zw_plugin_staged_count(pl, &n) != ZXV_OK || n != 1) return 1;
    if (zw_plugin_staged_get(pl, 0, &kind, &num, &den, NULL, 0, NULL) != ZXV_OK) return 1;
    r->staged_seen = (int) n;
    r->staged_num = num;
    r->staged_den = den;
    return (has_result && result == 0 && kind == 7 && num <= 100) ? 0 : 1;
}

static void test_checks(void) {
    zw_host *h = new_host(ZW_GRANT_PROPOSE);
    wt t;
    int32_t st, state = -1;
    int64_t res = -5, arg;
    uint32_t pending = 9, n = 0;
    chk_rec rec;
    memset(&rec, 0, sizeof rec);
    /* p(x: i64) -> i32 = zxv.propose(7, x, 4, 0, 0) */
    static const uint8_t prop[] = {0x41, 0x07, 0x20, 0x00, 0x42, 0x04, 0x41, 0x00, 0x41, 0x00,
                                   0x10, 0x00};
    wt_one(&t, prop, sizeof prop);
    t.types[0] = "I:i";
    t.types[1] = "iIIii:i";
    t.ntypes = 2;
    t.fimports[0].mod = "zxv";
    t.fimports[0].name = "propose";
    t.fimports[0].type = 1;
    t.nfimports = 1;
    t.exports[0] = "p";
    t.nexports = 1;
    zw_plugin *p = load_wt(h, &t, &st);
    CHECK(st == ZXV_OK, "load checked proposer");
    CHECK(zw_plugin_set_check(p, "nope", chk_fn, &rec) == ZW_E_EXPORT, "check on a missing export");
    CHECK(zw_plugin_set_check(NULL, "p", chk_fn, &rec) == ZXV_E_ARG, "NULL plugin");
    CHECK(zw_plugin_set_check(p, "p", chk_fn, &rec) == ZXV_OK, "attach");
    CHECK(zw_plugin_set_check(p, "p", chk_fn, &rec) == ZXV_OK, "replace");
    CHECK(zw_plugin_staged_count(p, &n) == ZXV_E_STATE, "staged_count outside a check");
    CHECK(zw_plugin_staged_get(p, 0, NULL, NULL, NULL, NULL, 0, NULL) == ZXV_E_STATE,
          "staged_get outside a check");
    int64_t used0 = budget_used();
    arg = -1;
    st = zw_plugin_call(p, "p", &arg, 1, &res, NULL, NULL);
    CHECK(st == ZW_E_CHECK && res == -5, "BEFORE refuses (%s)", zw_status_name(st));
    CHECK(budget_used() == used0, "nothing ran, no fuel charged");
    CHECK(zw_plugin_state(p, &state) == ZXV_OK && state == ZW_PLUGIN_READY, "still READY");
    CHECK(rec.nested_state == ZXV_E_STATE, "a check cannot call the plugin");
    arg = 8;
    st = zw_plugin_call(p, "p", &arg, 1, &res, NULL, NULL);
    CHECK(st == ZXV_OK && res == 0, "passing call (%s)", zw_status_name(st));
    CHECK(rec.before == 2 && rec.after == 1 && rec.staged_seen == 1 && rec.staged_num == 2 &&
              rec.staged_den == 1, "AFTER saw the staged 8/4 = 2");
    CHECK(zw_queue_pending(h, &pending) == ZXV_OK && pending == 1, "it reached the queue");
    int64_t used1 = budget_used();
    arg = 1000;
    res = -5;
    st = zw_plugin_call(p, "p", &arg, 1, &res, NULL, NULL);
    CHECK(st == ZW_E_CHECK && res == -5, "AFTER refuses 1000/4");
    CHECK(budget_used() > used1, "the refused call's fuel is charged");
    CHECK(zw_queue_pending(h, &pending) == ZXV_OK && pending == 1, "its proposal was discarded");
    CHECK(zw_plugin_state(p, &state) == ZXV_OK && state == ZW_PLUGIN_DEAD, "plugin DEAD");
    CHECK(zw_plugin_call(p, "p", &arg, 1, &res, NULL, NULL) == ZW_E_DEAD, "dead stays dead");
    CHECK(strcmp(zw_status_name(ZW_E_CHECK), "ZW_E_CHECK") == 0, "status name");
    zw_plugin_destroy(p);

    p = load_wt(h, &t, &st);
    CHECK(zw_plugin_set_check(p, "p", chk_fn, &rec) == ZXV_OK, "attach again");
    CHECK(zw_plugin_set_check(p, "p", NULL, NULL) == ZXV_OK, "detach");
    arg = 1000;
    CHECK(zw_plugin_call(p, "p", &arg, 1, &res, NULL, NULL) == ZXV_OK, "no check after detach");
    zw_plugin_destroy(p);
    uint64_t id;
    while (zw_queue_next(h, &id) == ZXV_OK) (void) zw_proposal_reject(h, post_cap, id);
    zw_host_destroy(h);
}

static void test_fail_closed(void) {
    zw_host *h = new_host(ZW_GRANT_PROPOSE);
    wt t;
    int32_t st;
    int64_t res;
    uint64_t id;
    static const uint8_t prop[] = {0x41, 0x01, 0x42, 0x05, 0x42, 0x01, 0x41, 0x00, 0x41, 0x00,
                                   0x10, 0x00};
    wt_one(&t, prop, sizeof prop);
    t.types[0] = ":i";
    t.types[1] = "iIIii:i";
    t.ntypes = 2;
    t.fimports[0].mod = "zxv";
    t.fimports[0].name = "propose";
    t.fimports[0].type = 1;
    t.nfimports = 1;
    t.exports[0] = "p";
    t.nexports = 1;
    zw_plugin *p = load_wt(h, &t, &st);
    CHECK(zw_plugin_call(p, "p", NULL, 0, &res, NULL, NULL) == ZXV_OK, "propose");
    CHECK(zw_queue_next(h, &id) == ZXV_OK, "pending");
    /* the HOST decides to halt (an operator kill switch) */
    CHECK(zxv_ctx_halt(ctx, "test: operator halt") == ZXV_OK, "halt");
    CHECK(zw_plugin_call(p, "p", NULL, 0, &res, NULL, NULL) == ZXV_E_POISONED,
          "plugins cannot run on a poisoned context");
    CHECK(zw_proposal_accept_post(h, post_cap, id, acct_issuer, acct_holder, "NGN", "zw-x",
                                  NULL) == ZXV_E_POISONED, "nothing can be accepted");
    zw_plugin_destroy(p);
    zw_host_destroy(h);
}

int main(int argc, char **argv) {
    printf("test_host: Z-Wasm host on wasm3 + libzxv\n");
    setup();
    test_basics();
    test_traps_and_fuel();
    test_queue();
    if (argc >= 4)
        test_examples(argv[1], argv[2], argv[3]);
    else
        printf("  (example plugins not given: skipping their tests)\n");
    test_checks();
    test_fail_closed();
    zxv_rmag_destroy(rm);
    zxv_cap_destroy(read_cap);
    zxv_cap_destroy(post_cap);
    zxv_cap_destroy(budget_cap);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    printf("test_host: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
