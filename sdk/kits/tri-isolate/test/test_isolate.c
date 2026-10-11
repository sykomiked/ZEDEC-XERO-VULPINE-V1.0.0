/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_isolate.c — Tri-Isolate end to end: real workers running Z-Wasm's
 * example plugins, and hostile workers that crash, hang, lie or break the
 * protocol.
 *
 *   test_isolate WORKER HOSTILE_DIR [rational.wasm float.wasm spin.wasm]
 *
 * Without the three plugins (no zig) the real-plugin tests are skipped.
 * After every test, no child process may be left unreaped. */
#if !defined(__APPLE__)
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "ti_test.h"
#include "triisolate.h"

static const char *g_worker, *g_hw;
static uint8_t *g_rational, *g_float, *g_spin;
static size_t g_rational_len, g_float_len, g_spin_len;

/* An empty module: valid, exports nothing (enough for the hostile workers). */
static const uint8_t k_empty[8] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};

static uint8_t *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    long n;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    b = (uint8_t *) malloc((size_t) n + 1u);
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    *len = (size_t) n;
    return b;
}

static int64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t) t.tv_sec * 1000 + (int64_t) (t.tv_nsec / 1000000);
}

/* No child left unreaped (every test must clean up after itself). */
static int no_children(void) {
    int ws;
    pid_t r = waitpid(-1, &ws, WNOHANG);
    return r < 0 && errno == ECHILD;
}

static const char *err(void) {
    static char b[600];
    size_t n;
    if (ti_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = 0;
    return b;
}

/* ----- a host's authority ----- */
typedef struct {
    zxv_ctx *ctx;
    zxv_cap *root, *budget_cap, *post_cap, *read_cap;
    zxv_rmag *rm;
    uint32_t bud;
    ti_host *h;
} env;

static void env_open(env *e, const char *worker, int64_t budget) {
    memset(e, 0, sizeof *e);
    if (zxv_ctx_create(&e->ctx) != ZXV_OK || zxv_cap_create(e->ctx, 1, NULL, ZXV_CAP_ALL, &e->root) != ZXV_OK ||
        zxv_cap_attenuate(e->root, ZXV_CAP_BUDGET, &e->budget_cap) != ZXV_OK ||
        zxv_cap_attenuate(e->root, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ, &e->post_cap) != ZXV_OK ||
        zxv_cap_attenuate(e->root, ZXV_CAP_LEDGER_READ, &e->read_cap) != ZXV_OK ||
        zxv_rmag_create_cap(e->root, &e->rm) != ZXV_OK ||
        zxv_rmag_budget(e->root, e->rm, "fuel", ZXV_RES_COMPUTE_CYCLES, budget, 1, 1, &e->bud) != ZXV_OK ||
        ti_host_create(e->budget_cap, e->rm, e->bud, worker, &e->h) != ZXV_OK ||
        ti_host_grant(e->h, ZW_GRANT_RATIONAL | ZW_GRANT_LOGIC | ZW_GRANT_PROPOSE) != ZXV_OK) {
        fprintf(stderr, "env_open failed: %s\n", err());
        exit(2);
    }
}

static void env_close(env *e) {
    ti_host_destroy(e->h);
    zxv_rmag_destroy(e->rm);
    zxv_cap_destroy(e->read_cap);
    zxv_cap_destroy(e->post_cap);
    zxv_cap_destroy(e->budget_cap);
    zxv_cap_destroy(e->root);
    zxv_ctx_destroy(e->ctx);
}

static int64_t used(const env *e) {
    int64_t n = 0, d = 1;
    (void) zxv_rmag_status(e->rm, e->bud, NULL, NULL, &n, &d, NULL);
    return d ? n / d : -1;
}

static char *hw(const char *mode) {
    static char p[1024];
    snprintf(p, sizeof p, "%s/%s", g_hw, mode);
    return p;
}

/* ----- API arguments ----- */
static void test_api(void) {
    env e;
    ti_host *h = NULL;
    env_open(&e, g_worker, 1000);
    CHECK(ti_host_create(NULL, e.rm, e.bud, g_worker, &h) == ZXV_E_ARG && !h, "NULL cap refused");
    CHECK(ti_host_create(e.read_cap, e.rm, e.bud, g_worker, &h) == ZXV_E_CAP, "a cap without BUDGET refused");
    CHECK(ti_host_create(e.budget_cap, e.rm, 999, g_worker, &h) != ZXV_OK, "unknown budget refused");
    CHECK(ti_host_create(e.budget_cap, e.rm, e.bud, "", &h) == ZXV_E_ARG, "empty worker path refused");
    CHECK(ti_host_grant(e.h, 0x8u) == ZXV_E_ARG, "unknown grant refused");
    CHECK(ti_host_set_call_fuel(e.h, 0) == ZXV_E_ARG, "zero call fuel refused");
    CHECK(ti_host_set_timeouts(e.h, 0, 10) == ZXV_E_ARG && ti_host_set_timeouts(e.h, 10, 3600001u) == ZXV_E_ARG,
          "timeouts out of range refused");
    CHECK(ti_host_set_accept_caps(e.h, 0) == ZXV_E_ARG, "zero accept caps refused");
    CHECK(ti_host_set_memory_limit(e.h, 4096) == ZXV_E_RANGE, "a memory limit under 1 MiB refused");
    CHECK(ti_host_set_flags(e.h, 0x80u) == ZXV_E_ARG, "unknown flag refused");
    CHECK(ti_host_set_limit(e.h, 99, 1) == ZXV_E_ARG, "unknown Z-Wasm limit refused");
    if (!ti_seccomp_available())
        CHECK(ti_host_set_flags(e.h, TI_F_SECCOMP) == ZXV_E_NOTIMPL, "TI_F_SECCOMP not available here");
    ti_plugin *p = NULL;
    CHECK(ti_plugin_load(e.h, k_empty, sizeof k_empty, "a name that is longer than 31 bytes", &p) ==
              ZXV_E_TOO_LONG, "long name refused");
    CHECK(ti_plugin_load(e.h, k_empty, sizeof k_empty, "tab\there", &p) == ZXV_E_ARG, "non-printable name refused");
    CHECK(ti_plugin_load(e.h, k_empty, 4, "x", &p) == ZW_E_REJECTED, "the host validates before spawning");
    uint64_t spawned = 9;
    CHECK(ti_host_stats(e.h, &spawned, NULL, NULL) == ZXV_OK && spawned == 0, "nothing spawned so far");
    CHECK(strcmp(ti_status_name(TI_E_TIMEOUT), "TI_E_TIMEOUT") == 0 &&
              strcmp(ti_status_name(ZW_E_FUEL), "ZW_E_FUEL") == 0, "status names");
    env_close(&e);
}

/* ----- start-up failures ----- */
static int32_t load_with(const char *worker, uint32_t flags, uint32_t load_ms, ti_plugin **out, env *e,
                         const char *name) {
    env_open(e, worker, 1000000);
    (void) ti_host_set_flags(e->h, flags);
    (void) ti_host_set_timeouts(e->h, 300, load_ms);
    return ti_plugin_load(e->h, k_empty, sizeof k_empty, name, out);
}

static void test_startup(void) {
    env e;
    ti_plugin *p = NULL;
    int64_t t0;
    int32_t st;

    st = load_with("/nonexistent/ti-worker", 0, 2000, &p, &e, "x");
    CHECK(st == TI_E_SPAWN && !p, "a missing worker program: TI_E_SPAWN (%s)", err());
    CHECK(strstr(err(), "exit status 127") != NULL, "the message says exec failed: %s", err());
    env_close(&e);
    CHECK(no_children(), "no child left (missing worker)");

    st = load_with(hw("exit-early"), 0, 2000, &p, &e, "x");
    CHECK(st == TI_E_SPAWN, "a worker that exits before HELLO: TI_E_SPAWN (%s)", ti_status_name(st));
    env_close(&e);

    st = load_with(hw("bad-version"), 0, 2000, &p, &e, "x");
    CHECK(st == TI_E_PROTOCOL, "unknown protocol version: TI_E_PROTOCOL (%s)", ti_status_name(st));
    uint64_t killed = 0;
    (void) ti_host_stats(e.h, NULL, &killed, NULL);
    CHECK(killed == 1, "the worker was killed");
    env_close(&e);

    if (ti_seccomp_available()) {
        st = load_with(hw("no-seccomp"), TI_F_SECCOMP, 2000, &p, &e, "x");
        CHECK(st == TI_E_PROTOCOL, "a worker that did not install seccomp is refused (%s)", ti_status_name(st));
        CHECK(strstr(err(), "seccomp") != NULL, "the message names seccomp: %s", err());
        env_close(&e);
    }

    st = load_with(hw("crash-load"), 0, 2000, &p, &e, "x");
    CHECK(st == TI_E_CRASHED, "abort() during LOAD: TI_E_CRASHED (%s)", ti_status_name(st));
    CHECK(strstr(err(), "signal 6") != NULL, "the message names SIGABRT: %s", err());
    env_close(&e);

    t0 = now_ms();
    st = load_with(hw("hang-load"), 0, 300, &p, &e, "x");
    int64_t dt = now_ms() - t0;
    CHECK(st == TI_E_TIMEOUT, "a worker that never answers LOAD: TI_E_TIMEOUT (%s)", ti_status_name(st));
    CHECK(dt >= 290 && dt < 3000, "the load timeout held (%lld ms)", (long long) dt);
    env_close(&e);

    st = load_with(hw("exit-after-hello"), 0, 2000, &p, &e, "x");
    CHECK(st == TI_E_CRASHED, "a worker gone after HELLO: TI_E_CRASHED (%s)", ti_status_name(st));
    env_close(&e);

    /* a module much larger than the socket buffer, to a worker that is
     * gone: the host gets EPIPE, not SIGPIPE, and carries on */
    {
        size_t n = 900000u, k = 0;
        uint8_t *m = (uint8_t *) malloc(n + 64u);
        const uint8_t head[] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00, 0x05, 0x04, 0x01, 0x01, 0x10, 0x10};
        if (!m) abort();
        memcpy(m, head, sizeof head);
        k = sizeof head;
        size_t dsz = n, content = 1 + 1 + 3 + 3 + dsz; /* count, flag, i32.const 0 end, uleb(dsz) 3 bytes */
        m[k++] = 0x0B;
        m[k++] = (uint8_t) (0x80u | (content & 0x7Fu));
        m[k++] = (uint8_t) (0x80u | ((content >> 7) & 0x7Fu));
        m[k++] = (uint8_t) ((content >> 14) & 0x7Fu);
        m[k++] = 0x01;
        m[k++] = 0x00;
        m[k++] = 0x41;
        m[k++] = 0x00;
        m[k++] = 0x0B;
        m[k++] = (uint8_t) (0x80u | (dsz & 0x7Fu));
        m[k++] = (uint8_t) (0x80u | ((dsz >> 7) & 0x7Fu));
        m[k++] = (uint8_t) ((dsz >> 14) & 0x7Fu);
        memset(m + k, 0x5A, dsz);
        k += dsz;
        env_open(&e, hw("exit-after-hello"), 1000000);
        st = ti_plugin_load(e.h, m, k, "big", &p);
        CHECK(st == TI_E_CRASHED, "900 KB to a vanished worker: TI_E_CRASHED, no SIGPIPE (%s: %s)",
              ti_status_name(st), err());
        env_close(&e);
        free(m);
    }

    st = load_with(hw("hygiene"), 0, 2000, &p, &e, "x");
    CHECK(st == ZXV_OK, "the worker inherits only fd 3, /dev/null, an empty environment and no core dumps (%s)",
          err());
    ti_plugin_destroy(p);
    p = NULL;
    env_close(&e);
    CHECK(no_children(), "no child left (start-up tests)");
}

/* ----- misbehaving calls ----- */
typedef struct {
    const char *mode;
    int32_t want;
} bad_call;

static void test_bad_calls(void) {
    static const bad_call cases[] = {
        {"hang-call", TI_E_TIMEOUT},     {"slow-drip", TI_E_TIMEOUT},    {"garbage", TI_E_PROTOCOL},
        {"oversize", TI_E_PROTOCOL},     {"zero-length", TI_E_PROTOCOL}, {"lie-fuel", TI_E_PROTOCOL},
        {"bad-prop", TI_E_PROTOCOL},     {"unnormal", TI_E_PROTOCOL},    {"long-label", TI_E_PROTOCOL},
        {"trailing", TI_E_PROTOCOL},     {"wrong-type", TI_E_PROTOCOL},  {"bad-status", TI_E_PROTOCOL},
        {"too-many", TI_E_PROTOCOL},     {"half-frame", TI_E_CRASHED},   {"close-and-hang", TI_E_CRASHED},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        env e;
        ti_plugin *p = NULL;
        int64_t r = 0, pid = 0;
        uint64_t fuel = 0;
        int32_t state = -1, how = -1, val = -1;
        uint32_t pending = 9;
        int32_t st = load_with(hw(cases[i].mode), 0, 2000, &p, &e, "bad");
        (void) ti_host_set_call_fuel(e.h, 5000);
        CHECK(st == ZXV_OK, "%s: loads (%s)", cases[i].mode, err());
        if (st != ZXV_OK) {
            env_close(&e);
            continue;
        }
        (void) ti_plugin_pid(p, &pid);
        int64_t t0 = now_ms();
        st = ti_plugin_call(p, "f", NULL, 0, &r, NULL, &fuel);
        int64_t dt = now_ms() - t0;
        CHECK(st == cases[i].want, "%s: %s, got %s (%s)", cases[i].mode, ti_status_name(cases[i].want),
              ti_status_name(st), err());
        CHECK(dt < 3000, "%s: answered in %lld ms", cases[i].mode, (long long) dt);
        CHECK(ti_plugin_state(p, &state) == ZXV_OK && state == TI_PLUGIN_DEAD, "%s: the plugin is DEAD",
              cases[i].mode);
        CHECK(fuel == 5000 && used(&e) == 5000, "%s: the whole allotment is charged (%llu, %lld)", cases[i].mode,
              (unsigned long long) fuel, (long long) used(&e));
        CHECK(ti_queue_pending(e.h, &pending) == ZXV_OK && pending == 0, "%s: nothing reached the queue",
              cases[i].mode);
        CHECK(ti_plugin_exit(p, &how, &val) == ZXV_OK && how != TI_EXIT_RUNNING, "%s: the worker is gone",
              cases[i].mode);
        CHECK(pid > 0 && kill((pid_t) pid, 0) != 0 && errno == ESRCH, "%s: no such process any more",
              cases[i].mode);
        if (strcmp(cases[i].mode, "close-and-hang") == 0)
            CHECK(how == TI_EXIT_SIGNAL && val == SIGKILL, "close-and-hang: killed, not left running");
        CHECK(ti_plugin_call(p, "f", NULL, 0, NULL, NULL, NULL) == TI_E_DEAD, "%s: then TI_E_DEAD", cases[i].mode);
        ti_plugin_destroy(p);
        env_close(&e);
        CHECK(no_children(), "%s: no child left", cases[i].mode);
    }
}

/* ----- the host-side queue (hostile 'props' worker: well-behaved) ----- */
static void test_queue(void) {
    env e;
    ti_plugin *p = NULL;
    int64_t r = 0, n3 = 3, n64 = 64, n0 = 0, num = 0, den = 0;
    int32_t has = 0, state = -1;
    uint64_t fuel = 0, id = 0, entry = 0;
    uint32_t pending = 0, kind = 0, issuer = 0, holder = 0;
    uint8_t label[65];
    size_t ll = 0;
    char who[32];

    CHECK(load_with(hw("props"), 0, 2000, &p, &e, "q") == ZXV_OK, "props worker loads (%s)", err());
    CHECK(ti_plugin_call(p, "f", &n3, 1, &r, &has, &fuel) == ZXV_OK && r == 7 && has == 1 && fuel == 10,
          "a call returns its result and fuel (%s)", err());
    CHECK(used(&e) == 10, "the host charged the reported fuel");
    CHECK(ti_queue_pending(e.h, &pending) == ZXV_OK && pending == 3, "3 proposals pending");
    CHECK(ti_queue_next(e.h, &id) == ZXV_OK && id == 1, "the oldest is id 1");
    CHECK(ti_proposal_get(e.h, 2, NULL, NULL, NULL, NULL, NULL, NULL, 0, &ll) == ZXV_E_BUFFER && ll == 2,
          "label length by the two-call rule");
    CHECK(ti_proposal_get(e.h, 2, &state, &kind, &num, &den, who, label, sizeof label, &ll) == ZXV_OK &&
              state == ZXV_LP_NEITHER && kind == 2 && num == 2 && den == 1 && strcmp((char *) label, "p1") == 0 &&
              strcmp(who, "q") == 0, "proposal 2 is 2/1 'p1' from 'q'");
    CHECK(ti_proposal_get(e.h, 99, NULL, NULL, NULL, NULL, NULL, label, sizeof label, &ll) == ZXV_E_NOT_FOUND,
          "unknown id");
    CHECK(ti_proposal_accept(e.h, e.read_cap, 1) == ZXV_E_CAP, "a cap without LEDGER_POST cannot accept");
    CHECK(ti_proposal_accept(e.h, e.budget_cap, 1) == ZXV_E_CAP, "nor can the budget cap");
    CHECK(ti_proposal_get(e.h, 1, &state, NULL, NULL, NULL, NULL, label, sizeof label, &ll) == ZXV_OK &&
              state == ZXV_LP_NEITHER, "a refused accept leaves it pending");
    CHECK(ti_proposal_accept(e.h, e.post_cap, 1) == ZXV_OK, "the host accepts with its own cap");
    CHECK(ti_proposal_accept(e.h, e.post_cap, 1) == ZXV_E_STATE, "decided once only");
    CHECK(ti_proposal_reject(e.h, e.post_cap, 2) == ZXV_OK, "reject");
    CHECK(ti_proposal_get(e.h, 2, &state, NULL, NULL, NULL, NULL, label, sizeof label, &ll) == ZXV_OK &&
              state == ZXV_LP_FALSE, "rejected is FALSE");
    /* accept and post proposal 3 (3/1) */
    CHECK(zxv_ccy_enable_cap(e.post_cap, "NGN") == ZXV_OK &&
              zxv_ledger_open_account_cap(e.post_cap, "treasury", "NGN", ZXV_ACCT_ISSUER, &issuer) == ZXV_OK &&
              zxv_ledger_open_account_cap(e.post_cap, "merchant", "NGN", ZXV_ACCT_HOLDER, &holder) == ZXV_OK,
          "ledger set up");
    CHECK(ti_proposal_accept_post(e.h, e.read_cap, 3, issuer, holder, "NGN", "ti-3", &entry) == ZXV_E_CAP,
          "accept_post needs the accept caps too");
    CHECK(ti_proposal_accept_post(e.h, e.post_cap, 3, issuer, holder, "NGN", "ti-3", &entry) == ZXV_OK,
          "accept_post posts with the host's cap (%s)", err());
    int64_t eq = 0;
    CHECK(zxv_ledger_balance_cap(e.post_cap, holder, NULL, NULL, &eq) == ZXV_OK && eq == 3, "merchant has 3");
    CHECK(ti_queue_pending(e.h, &pending) == ZXV_OK && pending == 0 && ti_queue_next(e.h, &id) == ZXV_E_NOT_FOUND,
          "nothing pending");

    /* capacity: 1024 pending at most; a call that does not fit queues none */
    int ok = 1;
    for (int i = 0; i < 16; i++) ok &= ti_plugin_call(p, "f", &n64, 1, &r, NULL, NULL) == ZXV_OK;
    CHECK(ok && ti_queue_pending(e.h, &pending) == ZXV_OK && pending == 1024, "1024 pending");
    r = 0;
    CHECK(ti_plugin_call(p, "f", &n3, 1, &r, &has, NULL) == ZXV_E_CAPACITY && r == 7,
          "the next call's proposals do not fit: ZXV_E_CAPACITY, result still written");
    CHECK(ti_queue_pending(e.h, &pending) == ZXV_OK && pending == 1024, "none of them was queued");
    CHECK(ti_plugin_state(p, &state) == ZXV_OK && state == TI_PLUGIN_READY, "the plugin is still READY");

    /* TI_F_CHARGE_ALLOTTED */
    int64_t before = used(&e);
    (void) ti_host_set_call_fuel(e.h, 777);
    (void) ti_host_set_flags(e.h, TI_F_CHARGE_ALLOTTED);
    CHECK(ti_plugin_call(p, "f", &n0, 1, &r, NULL, &fuel) == ZXV_OK && fuel == 777 && used(&e) - before == 777,
          "TI_F_CHARGE_ALLOTTED charges the whole allotment");

    /* a clean stop */
    int64_t pid = 0;
    int32_t how = -1, val = -1;
    (void) ti_plugin_pid(p, &pid);
    CHECK(ti_plugin_stop(p) == ZXV_OK && ti_plugin_exit(p, &how, &val) == ZXV_OK && how == TI_EXIT_CODE &&
              val == 0, "QUIT: the worker exits 0 (%d %d)", (int) how, (int) val);
    CHECK(kill((pid_t) pid, 0) != 0 && errno == ESRCH, "and is reaped");
    CHECK(ti_plugin_call(p, "f", &n0, 1, &r, NULL, NULL) == TI_E_DEAD, "a stopped plugin is DEAD");
    ti_plugin_destroy(p);
    env_close(&e);

    /* the budget: fuel = min(call fuel, floor(remaining)); none left, nothing sent */
    env_open(&e, hw("props"), 25);
    (void) ti_host_set_call_fuel(e.h, 10);
    CHECK(ti_plugin_load(e.h, k_empty, sizeof k_empty, "b", &p) == ZXV_OK, "load");
    uint64_t f1 = 0, f2 = 0, f3 = 0, calls = 0;
    CHECK(ti_plugin_call(p, "f", &n0, 1, NULL, NULL, &f1) == ZXV_OK &&
              ti_plugin_call(p, "f", &n0, 1, NULL, NULL, &f2) == ZXV_OK &&
              ti_plugin_call(p, "f", &n0, 1, NULL, NULL, &f3) == ZXV_OK && f1 == 10 && f2 == 10 && f3 == 5,
          "fuel 10, 10, then the last 5");
    CHECK(ti_plugin_call(p, "f", &n0, 1, NULL, NULL, NULL) == ZXV_E_BUDGET, "then ZXV_E_BUDGET");
    CHECK(ti_host_stats(e.h, NULL, NULL, &calls) == ZXV_OK && calls == 3, "the refused call was never sent");
    ti_plugin_destroy(p);
    env_close(&e);

    /* ignore-quit: destroy still ends it, by SIGKILL, promptly */
    CHECK(load_with(hw("ignore-quit"), 0, 2000, &p, &e, "iq") == ZXV_OK, "ignore-quit loads");
    int64_t t0 = now_ms();
    CHECK(ti_plugin_stop(p) == ZXV_OK && ti_plugin_exit(p, &how, &val) == ZXV_OK && how == TI_EXIT_SIGNAL &&
              val == SIGKILL, "a worker that ignores QUIT is killed");
    CHECK(now_ms() - t0 < 2000, "within the grace period");
    ti_plugin_destroy(p);
    env_close(&e);
    CHECK(no_children(), "no child left (queue tests)");
}

/* ----- seccomp (Linux x86-64 / arm64) ----- */
static void test_seccomp(void) {
    static const char *forbidden[] = {"open", "write1", "mmap-exec"};
    if (!ti_seccomp_available()) {
        printf("  seccomp: not available in this build (not Linux x86-64/arm64): skipped\n");
        return;
    }
    for (size_t i = 0; i < 3; i++) {
        env e;
        ti_plugin *p = NULL;
        int32_t st = load_with(hw("syscall"), 0, 2000, &p, &e, forbidden[i]);
        CHECK(st == ZXV_OK, "without seccomp, '%s' is allowed (%s)", forbidden[i], err());
        ti_plugin_destroy(p);
        p = NULL;
        env_close(&e);
        st = load_with(hw("syscall"), TI_F_SECCOMP, 2000, &p, &e, forbidden[i]);
        CHECK(st == TI_E_CRASHED && !p, "with seccomp, '%s' kills the worker (%s: %s)", forbidden[i],
              ti_status_name(st), err());
        CHECK(strstr(err(), "signal 31") != NULL || strstr(err(), "signal 12") != NULL,
              "%s: killed by SIGSYS: %s", forbidden[i], err());
        env_close(&e);
    }
    {
        env e;
        ti_plugin *p = NULL;
        int32_t st = load_with(hw("syscall"), TI_F_SECCOMP, 2000, &p, &e, "none");
        CHECK(st == ZXV_OK, "with seccomp, reading, writing fd 3 and malloc still work (%s)", err());
        ti_plugin_destroy(p);
        env_close(&e);
    }
    CHECK(no_children(), "no child left (seccomp tests)");
}

/* ----- real plugins: the same answers as Z-Wasm in-process ----- */
typedef struct {
    int32_t st;
    int64_t r;
    uint64_t fuel;
} outcome;

static void test_real(uint32_t flags) {
    const char *tag = (flags & TI_F_SECCOMP) ? "seccomp" : "plain";
    env e;
    ti_plugin *p = NULL;
    zw_host *zh = NULL;
    zw_plugin *zp = NULL;
    uint64_t spawned = 0, killed = 0;

    env_open(&e, g_worker, INT64_C(1) << 40);
    CHECK(ti_host_set_flags(e.h, flags) == ZXV_OK, "%s: flags", tag);
    CHECK(ti_host_set_call_fuel(e.h, 100000) == ZXV_OK, "%s: call fuel", tag);

    /* float: refused by the host's validator, no worker started */
    CHECK(ti_plugin_load(e.h, g_float, g_float_len, "float", &p) == ZW_E_REJECTED && !p,
          "%s: the float plugin is refused before any worker starts", tag);
    CHECK(strstr(err(), "FLOAT") != NULL, "%s: the message names the float op: %s", tag, err());
    CHECK(ti_host_stats(e.h, &spawned, NULL, NULL) == ZXV_OK && spawned == 0, "%s: nothing spawned", tag);

    /* rational, isolated and in-process */
    CHECK(ti_plugin_load(e.h, g_rational, g_rational_len, "rational", &p) == ZXV_OK, "%s: load rational (%s)",
          tag, err());
    if (zw_host_create(e.budget_cap, e.rm, e.bud, &zh) != ZXV_OK ||
        zw_host_grant(zh, ZW_GRANT_RATIONAL | ZW_GRANT_LOGIC | ZW_GRANT_PROPOSE) != ZXV_OK ||
        zw_host_set_call_fuel(zh, 100000) != ZXV_OK ||
        zw_plugin_load(zh, g_rational, g_rational_len, "rational", &zp) != ZXV_OK) {
        CHECK(0, "%s: in-process Z-Wasm host for comparison", tag);
        return;
    }
    static const struct {
        const char *fn;
        int64_t a[2];
        uint32_t n;
    } calls[] = {
        {"run", {12, 0}, 1}, {"fee", {48000, 125}, 2}, {"fee", {333, 125}, 2}, {"checksum", {1000, 0}, 1},
        {"run", {999, 0}, 1}, {"nosuch", {0, 0}, 0}, {"run", {INT64_C(1) << 40, 0}, 1},
    };
    int same = 1;
    for (size_t i = 0; i < sizeof calls / sizeof calls[0]; i++) {
        outcome a = {0, 0, 0}, b = {0, 0, 0};
        a.st = ti_plugin_call(p, calls[i].fn, calls[i].a, calls[i].n, &a.r, NULL, &a.fuel);
        b.st = zw_plugin_call(zp, calls[i].fn, calls[i].a, calls[i].n, &b.r, NULL, &b.fuel);
        if (b.st != ZXV_OK) b.fuel = 0, b.r = 0;
        if (a.st != ZXV_OK) a.r = 0;
        if (a.st != b.st || a.r != b.r || a.fuel != b.fuel) {
            same = 0;
            fprintf(stderr, "  %s %s: isolated %s r=%lld fuel=%llu, in-process %s r=%lld fuel=%llu\n", tag,
                    calls[i].fn, ti_status_name(a.st), (long long) a.r, (unsigned long long) a.fuel,
                    zw_status_name(b.st), (long long) b.r, (unsigned long long) b.fuel);
        }
    }
    CHECK(same, "%s: every call gives the same status, result and fuel as in-process Z-Wasm", tag);
    int32_t state = -1;
    CHECK(ti_plugin_state(p, &state) == ZXV_OK && state == TI_PLUGIN_READY,
          "%s: ZW_E_EXPORT and ZXV_E_RANGE leave the plugin READY", tag);

    /* the same proposals, in the same order */
    uint32_t np_ti = 0, np_zw = 0;
    (void) ti_queue_pending(e.h, &np_ti);
    (void) zw_queue_pending(zh, &np_zw);
    int psame = np_ti == np_zw && np_ti > 0;
    for (uint64_t id = 1; psame && id <= np_ti; id++) {
        uint32_t k1 = 0, k2 = 0;
        int64_t n1 = 0, d1 = 0, n2 = 0, d2 = 0;
        uint8_t l1[65], l2[65];
        size_t ll1 = 0, ll2 = 0;
        char w1[32], w2[32];
        psame = ti_proposal_get(e.h, id, NULL, &k1, &n1, &d1, w1, l1, sizeof l1, &ll1) == ZXV_OK &&
                zw_proposal_get(zh, id, NULL, &k2, &n2, &d2, w2, l2, sizeof l2, &ll2) == ZXV_OK && k1 == k2 &&
                n1 == n2 && d1 == d2 && ll1 == ll2 && memcmp(l1, l2, ll1) == 0 && strcmp(w1, w2) == 0;
    }
    CHECK(psame, "%s: the host's queue holds the same %u proposals as in-process Z-Wasm", tag, (unsigned) np_ti);
    zw_plugin_destroy(zp);
    zw_host_destroy(zh);

    /* two plugins at once, separate processes */
    ti_plugin *p2 = NULL;
    int64_t pid1 = 0, pid2 = 0, r = 0, twelve = 12;
    CHECK(ti_plugin_load(e.h, g_rational, g_rational_len, "second", &p2) == ZXV_OK, "%s: a second plugin", tag);
    (void) ti_plugin_pid(p, &pid1);
    (void) ti_plugin_pid(p2, &pid2);
    CHECK(pid1 > 0 && pid2 > 0 && pid1 != pid2 && pid1 != (int64_t) getpid(), "%s: two worker processes", tag);
    ti_plugin_destroy(p);
    CHECK(ti_plugin_call(p2, "run", &twelve, 1, &r, NULL, NULL) == ZXV_OK, "%s: the second still works", tag);
    int32_t how = -1, val = -1;
    CHECK(ti_plugin_stop(p2) == ZXV_OK && ti_plugin_exit(p2, &how, &val) == ZXV_OK && how == TI_EXIT_CODE &&
              val == 0, "%s: the real worker exits 0 on QUIT (%d %d)", tag, (int) how, (int) val);
    ti_plugin_destroy(p2);

    /* spin: out of fuel -> killed */
    int64_t one = 1, before;
    uint64_t fuel = 0;
    CHECK(ti_plugin_load(e.h, g_spin, g_spin_len, "spin", &p) == ZXV_OK, "%s: load spin (%s)", tag, err());
    (void) ti_plugin_pid(p, &pid1);
    (void) ti_host_stats(e.h, NULL, &killed, NULL);
    before = used(&e);
    int32_t st = ti_plugin_call(p, "spin", &one, 1, NULL, NULL, &fuel);
    CHECK(st == ZW_E_FUEL && fuel == 100000 && used(&e) - before == 100000,
          "%s: spin runs out of fuel; the allotment is charged (%s, %llu)", tag, ti_status_name(st),
          (unsigned long long) fuel);
    uint64_t killed2 = 0;
    (void) ti_host_stats(e.h, NULL, &killed2, NULL);
    CHECK(killed2 == killed + 1u && kill((pid_t) pid1, 0) != 0, "%s: and its worker is killed", tag);
    CHECK(ti_plugin_call(p, "spin", &one, 1, NULL, NULL, NULL) == TI_E_DEAD, "%s: then DEAD", tag);
    ti_plugin_destroy(p);

    /* spin past the wall-clock timeout (fuel would last much longer) */
    (void) ti_host_set_call_fuel(e.h, UINT64_C(1000000000000));
    (void) ti_host_set_timeouts(e.h, 300, 10000);
    CHECK(ti_plugin_load(e.h, g_spin, g_spin_len, "spin", &p) == ZXV_OK, "%s: load spin again", tag);
    int64_t t0 = now_ms();
    st = ti_plugin_call(p, "spin", &one, 1, NULL, NULL, &fuel);
    int64_t dt = now_ms() - t0;
    CHECK(st == TI_E_TIMEOUT && fuel == UINT64_C(1000000000000), "%s: TI_E_TIMEOUT, allotment charged (%s)", tag,
          ti_status_name(st));
    CHECK(dt >= 290 && dt < 3000, "%s: after about 300 ms (%lld)", tag, (long long) dt);
    ti_plugin_destroy(p);
    (void) ti_host_set_call_fuel(e.h, 100000);
    (void) ti_host_set_timeouts(e.h, 5000, 10000);

    /* propose then trap: the proposal is discarded, the worker killed */
    uint32_t pending_before = 0, pending_after = 0;
    (void) ti_queue_pending(e.h, &pending_before);
    CHECK(ti_plugin_load(e.h, g_spin, g_spin_len, "spin", &p) == ZXV_OK, "%s: load spin a third time", tag);
    st = ti_plugin_call(p, "propose_trap", NULL, 0, NULL, NULL, NULL);
    (void) ti_queue_pending(e.h, &pending_after);
    CHECK(st == ZW_E_TRAP && pending_after == pending_before, "%s: propose_trap traps, nothing queued (%s)", tag,
          ti_status_name(st));
    ti_plugin_destroy(p);
    CHECK(ti_plugin_load(e.h, g_spin, g_spin_len, "spin", &p) == ZXV_OK, "%s: load spin a fourth time", tag);
    int64_t addr = INT64_C(1) << 30;
    st = ti_plugin_call(p, "poke", &addr, 1, NULL, NULL, NULL);
    CHECK(st == ZW_E_TRAP, "%s: an out-of-bounds store traps (%s)", tag, ti_status_name(st));
    ti_plugin_destroy(p);

    env_close(&e);
    CHECK(no_children(), "%s: no child left (real plugins)", tag);
}

static void test_memory_limit(void) {
#if defined(TI_TEST_SANITIZE)
    printf("  memory limit: skipped under sanitizers (they reserve terabytes of address space)\n");
#elif !defined(__linux__)
    env e;
    env_open(&e, g_worker, 1000000);
    CHECK(ti_host_set_memory_limit(e.h, UINT64_C(1) << 31) == ZXV_E_NOTIMPL, "no memory limit off Linux");
    env_close(&e);
#else
    env e;
    ti_plugin *p = NULL;
    int64_t twelve = 12, r = 0;
    env_open(&e, g_worker, 1000000);
    CHECK(ti_host_set_memory_limit(e.h, UINT64_C(1) << 20) == ZXV_OK, "1 MiB limit");
    int32_t st = ti_plugin_load(e.h, g_rational, g_rational_len, "tiny", &p);
    CHECK(st != ZXV_OK && !p, "a worker under a 1 MiB address-space limit cannot start (%s)", ti_status_name(st));
    CHECK(ti_host_set_memory_limit(e.h, UINT64_C(1) << 31) == ZXV_OK, "2 GiB limit");
    CHECK(ti_plugin_load(e.h, g_rational, g_rational_len, "roomy", &p) == ZXV_OK &&
              ti_plugin_call(p, "run", &twelve, 1, &r, NULL, NULL) == ZXV_OK,
          "under a 2 GiB limit it runs (%s)", err());
    ti_plugin_destroy(p);
    env_close(&e);
    CHECK(no_children(), "no child left (memory limit)");
#endif
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: test_isolate WORKER HOSTILE_DIR [rational.wasm float.wasm spin.wasm]\n");
        return 2;
    }
    g_worker = argv[1];
    g_hw = argv[2];
    printf("test_isolate: processes, deadlines, the protocol and the host-side queue\n");
    test_api();
    test_startup();
    test_bad_calls();
    test_queue();
    test_seccomp();
    if (argc >= 6) {
        g_rational = slurp(argv[3], &g_rational_len);
        g_float = slurp(argv[4], &g_float_len);
        g_spin = slurp(argv[5], &g_spin_len);
        if (!g_rational || !g_float || !g_spin) {
            fprintf(stderr, "cannot read the example plugins\n");
            return 2;
        }
        test_real(0);
        if (ti_seccomp_available()) test_real(TI_F_SECCOMP);
        test_memory_limit();
        free(g_rational);
        free(g_float);
        free(g_spin);
    } else {
        printf("  real plugins: skipped (no example plugins given; needs zig)\n");
    }
    printf("test_isolate: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
