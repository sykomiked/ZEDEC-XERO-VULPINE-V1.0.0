/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_power.c — zxvd_power.c (power.*) behind the real RPC layer on the
 * real power ledger, fed from a Linux-style /sys and /proc fixture on any
 * POSIX host, with a test clock: the agent under zxvd's own app, the budget
 * that makes the next call wait, the battery posture's token scale, and the
 * parameters. Replies are compared exactly with literal JSON. */
#define _XOPEN_SOURCE    700 /* nftw */
#define _DARWIN_C_SOURCE     /* mkdtemp and friends on macOS */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "zxvd_groups.h"
#include "zxvd_power.h"
#include "power_ledger.h" /* PW_MAX_CONSUMERS */
#include "zxv_thermal_host.h"

static int fails, checks;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL %s:%d ", __FILE__, __LINE__);                                           \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static zxvd_buf_t OUT;
static char REQB[1000], WANT[4000];

static const char *rpc(const char *line)
{
    CHECK(zxvd_rpc_handle_line(line, strlen(line), &OUT), "a reply for %.200s", line);
    return OUT.p ? OUT.p : "";
}

static void expect(const char *req, const char *want, int line)
{
    const char *got = rpc(req);
    checks++;
    if (strcmp(got, want)) {
        fails++;
        printf("  FAIL line %d\n    req  %.300s\n    want %.900s\n    got  %.900s\n", line, req,
               want, got);
    }
}
#define EXPECT(req, want) expect(req, want, __LINE__)

static const char *call(const char *method, const char *params)
{
    snprintf(REQB, sizeof REQB, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":%s}",
             method, params);
    return REQB;
}

static const char *ok(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static const char *ok(const char *fmt, ...)
{
    va_list ap;
    int k = snprintf(WANT, sizeof WANT, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":");
    va_start(ap, fmt);
    k += vsnprintf(WANT + k, sizeof WANT - (size_t) k, fmt, ap);
    va_end(ap);
    snprintf(WANT + k, sizeof WANT - (size_t) k, "}");
    return WANT;
}

static const char *err(int code, const char *msg)
{
    snprintf(WANT, sizeof WANT,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":%d,\"message\":\"%s\"}}", code,
             msg);
    return WANT;
}

/* ----- the fixture: a Linux-style root and a test clock ----- */

static char R[256];
static uint64_t NOW;

static uint64_t clock_ms(void)
{
    return NOW;
}

static void mk(const char *rel)
{
    char p[1024], *s;
    snprintf(p, sizeof p, "%s/%s", R, rel);
    for (s = p + strlen(R) + 1; (s = strchr(s, '/')) != NULL; s++) {
        *s = 0;
        mkdir(p, 0700);
        *s = '/';
    }
    mkdir(p, 0700);
}

static void put(const char *rel, const char *text)
{
    char p[1024], d[512];
    snprintf(p, sizeof p, "%s/%s", R, rel);
    snprintf(d, sizeof d, "%s", rel);
    char *slash = strrchr(d, '/');
    if (slash) {
        *slash = 0;
        mk(d);
    }
    FILE *f = fopen(p, "w");
    if (!f) {
        printf("cannot write %s\n", p);
        exit(2);
    }
    fprintf(f, "%s\n", text);
    fclose(f);
}

/* zxvd's own process: utime + stime in 1/100 s under a fixture root */
static void zxvd_cpu(unsigned ticks)
{
    char body[256];
    snprintf(body, sizeof body,
             "99 (zxvd) S 1 1 1 0 -1 4194560 100 0 0 0 %u 0 0 0 20 0 1 0 1000 "
             "1000 100",
             ticks);
    put("proc/99/stat", body);
}

static void psys(const char *uj)
{
    put("sys/class/powercap/intel-rapl:1/energy_uj", uj);
}

static void battery(const char *status, const char *now_uwh, bool mains)
{
    put("sys/class/power_supply/AC/online", mains ? "1" : "0");
    put("sys/class/power_supply/BAT0/status", status);
    put("sys/class/power_supply/BAT0/energy_now", now_uwh);
}

static void fixture(void)
{
    snprintf(R, sizeof R, "%s/zxvd_power_XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(R)) exit(2);
    put("proc/cpuinfo", "processor\t: 0\n\nprocessor\t: 1\n\nprocessor\t: 2\n\nprocessor\t: 3");
    put("proc/self/comm", "zxvd");
    put("sys/class/powercap/intel-rapl:1/name", "psys");
    psys("5000000");
    put("sys/class/power_supply/AC/type", "Mains");
    put("sys/class/power_supply/BAT0/type", "Battery");
    put("sys/class/power_supply/BAT0/energy_full", "45000000");
    put("sys/class/power_supply/BAT0/energy_full_design", "50000000");
    put("sys/class/power_supply/BAT0/power_now", "0");
    put("sys/class/power_supply/BAT0/voltage_now", "12000000");
    battery("Charging", "30000000", true);
    zxvd_cpu(0);
}

static int rm_one(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
    (void) st;
    (void) flag;
    (void) ftw;
    return remove(path);
}

/* the fixture tree, deepest first, without a shell */
static void cleanup(void)
{
    if (nftw(R, rm_one, 16, FTW_DEPTH | FTW_PHYS) != 0) printf("  (could not remove %s)\n", R);
}

static void fresh(void)
{
    zxvd_rpc_groups_reset();
    CHECK(zxvd_rpc_add_group(zxvd_power_group()) == 0, "add the power group");
}

/* ----- power is not running ----- */

static void test_closed(void)
{
    static const char *NOT = "power tracking is not running in this zxvd";
    fresh();
    EXPECT(call("features.get", "{}"),
           ok("{\"features\":[{\"name\":\"power\",\"enabled\":false,\"regulated\":false,"
              "\"groups\":[{\"note\":\"energy per app and agent, battery posture, energy "
              "budgets and temperatures\",\"methods\":4}]}],\"saved\":false}"));
    zxvd_power_close();
    CHECK(!zxvd_power_is_open(), "closed");
    EXPECT(call("power.status", "{}"), err(ZXVD_E_POWER, NOT));
    EXPECT(call("power.top", "{}"), err(ZXVD_E_POWER, NOT));
    CHECK(zxvd_rpc_enable("power") == 0, "power on");
    EXPECT(call("power.budget", "{\"target\":\"device\",\"window_ms\":1,\"max_uj\":1}"),
           err(ZXVD_E_POWER, NOT));
    CHECK(zxvd_power_admit(1), "W4: closed admits");
    CHECK(zxvd_power_token_scale() == 1000, "W5: closed scale is 1000");
    zxvd_power_note_agent(1, "x", 5, 0); /* a no-op */
    zxvd_power_note_agent(5, "ghost", 7, 0); /* noted while closed: never seen after open */
    zxvd_power_tick(true);               /* a no-op */
    CHECK(!zxvd_power_is_open(), "still closed");
    CHECK(zxvd_power_open("/nonexistent/zxvd-power-root") == 0, "a root with nothing to read");
    CHECK(zxvd_power_is_open(), "it opens: nothing measured is not a failure");
    /* W6: no meter at all: every consumer is NEITHER with no figure, even
     * after a settled interval with CPU time in it */
    NOW = 1000;
    zxvd_power_note_agent(5, "engine", 300000000u, 0);
    zxvd_power_tick(true);
    const char *top = rpc(call("power.top", "{\"lifetime\":true}"));
    CHECK(
        strstr(top, "\"kind\":\"system\",\"name\":\"system\",\"parent\":null,\"uj\":null,"
                    "\"mw\":null,\"source\":\"none\",\"state\":\"neither\",\"lifetime_uj\":0,") &&
            strstr(top, "\"name\":\"engine\",\"parent\":1,\"uj\":null,\"mw\":null,\"source\":"
                        "\"none\",\"state\":\"neither\",\"lifetime_uj\":0,\"cpu_ns\":300000000}") &&
            !strstr(top, "\"uj\":0") && !strstr(top, "\"state\":\"true\"") && !strstr(top, "ghost"),
        "W6: nothing measured is null, never 0: %.600s", top);
    NOW = 0;
    zxvd_rpc_groups_reset(); /* the group's close */
    CHECK(!zxvd_power_is_open(), "the group's close closes power");
}

/* ----- the agent, the budget and the posture ----- */

static void test_ledger(void)
{
    fresh();
    NOW = 0;
    CHECK(zxvd_power_open(R) == 0, "open on the fixture");
    CHECK(zxvd_power_token_scale() == 1000, "W5: on mains, x1");
    EXPECT(call("power.top", "{\"kind\":\"agent\"}"), ok("[]"));
    EXPECT(
        call("power.top", "{\"kind\":\"app\"}"),
        ok("[{\"id\":1,\"kind\":\"app\",\"name\":\"zxvd\",\"parent\":null,\"uj\":null,\"mw\":null,"
           "\"source\":\"none\",\"state\":\"neither\",\"lifetime_uj\":0,\"cpu_ns\":0}]"));
    CHECK(zxvd_power_admit(7), "W4: an agent never noted is admitted with no budget set");

    /* one second: 20 J on psys; zxvd ran 2 s of CPU, the agent 0.5 s of it */
    NOW = 1000;
    psys("25000000");
    zxvd_cpu(200);
    zxvd_power_note_agent(7, "companion", 500000000u, 0);
    zxvd_power_tick(false);
    EXPECT(call("power.top", "{\"kind\":\"agent\"}"),
           ok("[{\"id\":2,\"kind\":\"agent\",\"name\":\"companion\",\"parent\":1,\"uj\":2500000,"
              "\"mw\":2500,\"source\":\"attributed\",\"state\":\"true\",\"lifetime_uj\":2500000,"
              "\"cpu_ns\":500000000}]"));
    /* idle and shared energy stays with the system (power_ledger.h P3) */
    EXPECT(call("power.top", "{\"kind\":\"all\",\"n\":2}"),
           ok("[{\"id\":0,\"kind\":\"system\",\"name\":\"system\",\"parent\":null,"
              "\"uj\":10000000,\"mw\":10000,\"source\":\"attributed\",\"state\":\"true\","
              "\"lifetime_uj\":10000000,"
              "\"cpu_ns\":0},{\"id\":1,\"kind\":\"app\",\"name\":\"zxvd\",\"parent\":null,"
              "\"uj\":7500000,\"mw\":7500,\"source\":\"attributed\",\"state\":\"true\",\"lifetime_"
              "uj\":7500000,"
              "\"cpu_ns\":2000000000}]"));
    EXPECT(call("power.top", "{\"n\":1}"),
           ok("[{\"id\":0,\"kind\":\"system\",\"name\":\"system\",\"parent\":null,\"uj\":10000000,"
              "\"mw\":10000,\"source\":\"attributed\",\"state\":\"true\",\"lifetime_uj\":10000000,"
              "\"cpu_ns\":0}]"));
    const char *st = rpc(call("power.status", "{}"));
    CHECK(!strncmp(st,
                   "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"device\":\"laptop\",\"host\":"
                   "\"linux\",\"interval_ms\":1000,\"total_uj\":20000000,\"total_mw\":20000,",
                   112) &&
              strstr(st, "\"posture\":\"perform\",\"token_scale_permille\":1000") &&
              !strcmp(st + strlen(st) - 3, "]}}"),
          "W6: power.status is the ledger's JSON as-is: %.300s", st);

    /* W2: at most once a second */
    NOW = 1999;
    psys("45000000");
    zxvd_cpu(400);
    zxvd_power_note_agent(7, NULL, 500000000u, 0); /* a name is kept from the first note */
    zxvd_power_tick(false);
    EXPECT(call("power.top", "{\"kind\":\"agent\",\"lifetime\":true}"),
           ok("[{\"id\":2,\"kind\":\"agent\",\"name\":\"companion\",\"parent\":1,\"uj\":2500000,"
              "\"mw\":2500,\"source\":\"attributed\",\"state\":\"true\",\"lifetime_uj\":2500000,"
              "\"cpu_ns\":500000000}]"));

    /* W7: a 1 uJ budget for the agent; the next second spends it */
    EXPECT(
        call("power.budget", "{\"target\":\"agent\",\"agent\":7,\"window_ms\":60000,\"max_uj\":1}"),
        err(ZXVD_E_OFF, "power is off: start zxvd with --enable power, or call features.set"));
    CHECK(zxvd_rpc_enable("power") == 0, "power on");
    EXPECT(
        call("power.budget", "{\"target\":\"agent\",\"agent\":7,\"window_ms\":60000,\"max_uj\":1}"),
        ok("{\"ok\":true}"));
    CHECK(zxvd_power_admit(7), "nothing spent in the window yet");
    NOW = 2000;
    zxvd_power_tick(false);
    CHECK(!zxvd_power_admit(7), "W4: the spent window makes the agent wait");
    EXPECT(call("power.top", "{\"kind\":\"agent\",\"lifetime\":true}"),
           ok("[{\"id\":2,\"kind\":\"agent\",\"name\":\"companion\",\"parent\":1,\"uj\":2500000,"
              "\"mw\":2500,\"source\":\"attributed\",\"state\":\"true\",\"lifetime_uj\":5000000,"
              "\"cpu_ns\":1000000000}]"));
    EXPECT(call("power.budget", "{\"target\":\"agent\",\"agent\":7,\"window_ms\":0,\"max_uj\":0}"),
           ok("{\"ok\":true}"));
    CHECK(zxvd_power_admit(7), "a cleared budget admits again");
    /* the app's budget binds its agents; the device's binds everyone */
    EXPECT(call("power.budget",
                "{\"target\":\"app\",\"app\":\"zxvd\",\"window_ms\":60000,\"max_uj\":1}"),
           ok("{\"ok\":true}"));
    NOW = 3000;
    psys("65000000");
    zxvd_cpu(600);
    zxvd_power_note_agent(7, "companion", 1000000u, 0);
    zxvd_power_tick(false);
    CHECK(!zxvd_power_admit(7), "zxvd's spent budget makes its agent wait");
    CHECK(!zxvd_power_admit(99), "W4: an agent never noted is judged as zxvd");
    EXPECT(
        call("power.budget", "{\"target\":\"app\",\"app\":\"zxvd\",\"window_ms\":0,\"max_uj\":0}"),
        ok("{\"ok\":true}"));
    CHECK(zxvd_power_admit(7), "cleared");
    EXPECT(call("power.budget",
                "{\"target\":\"device\",\"window_ms\":60000,\"max_uj\":1,\"strict\":true}"),
           ok("{\"ok\":true}"));
    NOW = 4000;
    psys("85000000");
    zxvd_power_tick(false);
    CHECK(!zxvd_power_admit(7), "the device's spent budget makes every agent wait");
    CHECK(!zxvd_power_admit(99), "an agent never noted waits too");
    EXPECT(call("power.budget", "{\"target\":\"device\",\"window_ms\":0,\"max_uj\":0}"),
           ok("{\"ok\":true}"));
    CHECK(zxvd_power_admit(7) && zxvd_power_admit(99), "cleared");
    /* W4: admitting asks for nothing up front: a window spent to exactly its
     * limit, here 0 of 0 uJ, still admits */
    EXPECT(
        call("power.budget", "{\"target\":\"agent\",\"agent\":7,\"window_ms\":60000,\"max_uj\":0}"),
        ok("{\"ok\":true}"));
    CHECK(zxvd_power_admit(7), "W4: a window spent to exactly its limit admits");
    EXPECT(call("power.budget", "{\"target\":\"agent\",\"agent\":7,\"window_ms\":0,\"max_uj\":0}"),
           ok("{\"ok\":true}"));
    /* an app not seen yet is added */
    EXPECT(call("power.budget",
                "{\"target\":\"app\",\"app\":\"brand new\",\"window_ms\":1000,\"max_uj\":5}"),
           ok("{\"ok\":true}"));
    EXPECT(call("power.top", "{\"kind\":\"app\",\"n\":5}"),
           ok("[{\"id\":1,\"kind\":\"app\",\"name\":\"zxvd\",\"parent\":null,\"uj\":0,\"mw\":0,"
              "\"source\":\"attributed\",\"state\":\"true\",\"lifetime_uj\":24995000,\"cpu_ns\":"
              "6000000000},{\"id\":3,"
              "\"kind\":\"app\",\"name\":\"brand new\",\"parent\":null,\"uj\":null,\"mw\":null,"
              "\"source\":\"none\",\"state\":\"neither\",\"lifetime_uj\":0,\"cpu_ns\":0}]"));

    /* W5: the battery posture scales the tokens */
    NOW = 5000;
    battery("Discharging", "20000000", false);
    zxvd_power_tick(true);
    CHECK(zxvd_power_token_scale() == 618, "on battery above 20%%: x0.618 (%u)",
          (unsigned) zxvd_power_token_scale());
    battery("Discharging", "9000000", false); /* 20% of 45 Wh */
    zxvd_power_tick(true);
    CHECK(zxvd_power_token_scale() == 382, "20%% or below: x0.382 (%u)",
          (unsigned) zxvd_power_token_scale());
    battery("Discharging", "2250000", false); /* 5% */
    zxvd_power_tick(true);
    CHECK(zxvd_power_token_scale() == 0, "5%% or below: x0 (%u)",
          (unsigned) zxvd_power_token_scale());
    battery("Charging", "2250000", true);
    zxvd_power_tick(true);
    CHECK(zxvd_power_token_scale() == 1000, "on mains again: x1");
}

/* ----- parameters ----- */

static void test_params(void)
{
    static const char *TGT = "\\\"target\\\" must be \\\"device\\\", \\\"app\\\" or \\\"agent\\\"";
    static const char *KIND = "\\\"kind\\\" must be \\\"app\\\", \\\"agent\\\" or \\\"all\\\"";
    EXPECT(call("power.top", "{\"kind\":\"apps\"}"), err(ZXVD_E_PARAMS, KIND));
    /* lifetime: the figures add up to everything measured (85 J - 5 J) */
    EXPECT(call("power.top", "{\"kind\":\"\",\"lifetime\":true}"),
           ok("[{\"id\":0,\"kind\":\"system\",\"name\":\"system\",\"parent\":null,\"uj\":0,"
              "\"mw\":0,\"source\":\"attributed\",\"state\":\"true\",\"lifetime_uj\":50000000,"
              "\"cpu_ns\":0},"
              "{\"id\":1,\"kind\":\"app\",\"name\":\"zxvd\",\"parent\":null,\"uj\":0,\"mw\":0,"
              "\"source\":\"attributed\",\"state\":\"true\",\"lifetime_uj\":24995000,\"cpu_ns\":"
              "6000000000},"
              "{\"id\":2,\"kind\":\"agent\",\"name\":\"companion\",\"parent\":1,\"uj\":0,"
              "\"mw\":0,\"source\":\"attributed\",\"state\":\"true\",\"lifetime_uj\":5005000,"
              "\"cpu_ns\":1001000000},{\"id\":3,\"kind\":\"app\",\"name\":\"brand new\","
              "\"parent\":null,\"uj\":0,\"mw\":0,\"source\":\"attributed\",\"state\":\"true\","
              "\"lifetime_uj\":0,"
              "\"cpu_ns\":0}]"));
    EXPECT(call("power.top", "{\"kind\":\"agent\",\"lifetime\":false}"),
           ok("[{\"id\":2,\"kind\":\"agent\",\"name\":\"companion\",\"parent\":1,\"uj\":0,"
              "\"mw\":0,\"source\":\"attributed\",\"state\":\"true\",\"lifetime_uj\":5005000,"
              "\"cpu_ns\":1001000000}]"));
    /* n defaults to 10 */
    for (int i = 0; i < 10; i++) {
        char p[200];
        snprintf(p, sizeof p,
                 "{\"target\":\"app\",\"app\":\"extra %d\",\"window_ms\":0,\"max_uj\":0}", i);
        EXPECT(call("power.budget", p), ok("{\"ok\":true}"));
    }
    const char *ten = rpc(call("power.top", "{}"));
    int ids = 0;
    for (const char *q = ten; (q = strstr(q, "{\"id\":")) != NULL; q++) ids++;
    CHECK(ids == 10, "power.top: ten by default (%d)", ids);
    EXPECT(call("power.budget", "{\"target\":\"devicexx\",\"window_ms\":1,\"max_uj\":1}"),
           err(ZXVD_E_PARAMS, "\\\"target\\\" must be a string of at most 7 bytes"));
    EXPECT(call("power.top", "{\"kind\":7}"),
           err(ZXVD_E_PARAMS, "\\\"kind\\\" must be a string of at most 7 bytes"));
    EXPECT(call("power.top", "{\"n\":0}"), err(ZXVD_E_PARAMS, "\\\"n\\\" must be 1..64"));
    EXPECT(call("power.top", "{\"n\":65}"), err(ZXVD_E_PARAMS, "\\\"n\\\" must be 1..64"));
    EXPECT(call("power.top", "{\"n\":-1}"), err(ZXVD_E_PARAMS, "\\\"n\\\" is out of range"));
    EXPECT(call("power.top", "{\"lifetime\":1}"),
           err(ZXVD_E_PARAMS, "\\\"lifetime\\\" must be true or false"));
    const char *top = rpc(call("power.top", "{\"n\":64}"));
    CHECK(strstr(top, "\"brand new\"") != NULL, "n 64 is allowed: %.100s", top);
    EXPECT(call("power.budget", "{\"window_ms\":1,\"max_uj\":1}"),
           err(ZXVD_E_PARAMS, "missing string parameter \\\"target\\\""));
    EXPECT(call("power.budget", "{\"target\":\"room\",\"window_ms\":1,\"max_uj\":1}"),
           err(ZXVD_E_PARAMS, TGT));
    EXPECT(call("power.budget", "{\"target\":\"device\",\"max_uj\":1}"),
           err(ZXVD_E_PARAMS, "missing integer parameter \\\"window_ms\\\""));
    EXPECT(call("power.budget", "{\"target\":\"device\",\"window_ms\":1}"),
           err(ZXVD_E_PARAMS, "missing integer parameter \\\"max_uj\\\""));
    EXPECT(call("power.budget", "{\"target\":\"device\",\"window_ms\":-1,\"max_uj\":1}"),
           err(ZXVD_E_PARAMS, "\\\"window_ms\\\" must not be negative"));
    EXPECT(call("power.budget", "{\"target\":\"device\",\"window_ms\":1,\"max_uj\":-1}"),
           err(ZXVD_E_PARAMS, "\\\"max_uj\\\" must not be negative"));
    EXPECT(call("power.budget",
                "{\"target\":\"device\",\"window_ms\":1,\"max_uj\":1,\"strict\":\"yes\"}"),
           err(ZXVD_E_PARAMS, "\\\"strict\\\" must be true or false"));
    EXPECT(call("power.budget", "{\"target\":\"app\",\"window_ms\":1,\"max_uj\":1}"),
           err(ZXVD_E_PARAMS, "missing string parameter \\\"app\\\""));
    EXPECT(call("power.budget", "{\"target\":\"app\",\"app\":\"\",\"window_ms\":1,\"max_uj\":1}"),
           err(ZXVD_E_PARAMS, "\\\"app\\\" must name an executable"));
    EXPECT(call("power.budget", "{\"target\":\"agent\",\"window_ms\":1,\"max_uj\":1}"),
           err(ZXVD_E_PARAMS, "missing integer parameter \\\"agent\\\""));
    EXPECT(call("power.budget", "{\"target\":\"agent\",\"agent\":-7,\"window_ms\":1,"
                                "\"max_uj\":1}"),
           err(ZXVD_E_PARAMS, "\\\"agent\\\" must not be negative"));
    EXPECT(call("power.budget", "{\"target\":\"agent\",\"agent\":8,\"window_ms\":1,\"max_uj\":1}"),
           err(ZXVD_E_POWER, "unknown agent 8"));
    EXPECT(call("power.budget", "{\"target\":\"agent\",\"agent\":7,\"window_ms\":1,\"max_uj\":1}"),
           ok("{\"ok\":true}"));
    /* an agent noted without a name is "agent" */
    zxvd_power_note_agent(9, "", 1000u, 0);
    zxvd_power_note_agent(10, NULL, 1000u, 0);
    const char *ag = rpc(call("power.top", "{\"kind\":\"agent\"}"));
    CHECK(strstr(ag, "{\"id\":14,\"kind\":\"agent\",\"name\":\"agent\",\"parent\":1,") &&
              strstr(ag, "{\"id\":15,\"kind\":\"agent\",\"name\":\"agent\",\"parent\":1,"),
          "unnamed agents: %.400s", ag);
}

/* ----- thermal.read (W9) ----- */

static const char *NO_CLASSES =
    "\"classes\":{\"cpu\":{\"state\":\"neither\",\"n\":0,\"max_mdeg\":null},"
    "\"gpu\":{\"state\":\"neither\",\"n\":0,\"max_mdeg\":null},"
    "\"case\":{\"state\":\"neither\",\"n\":0,\"max_mdeg\":null},"
    "\"other\":{\"state\":\"neither\",\"n\":0,\"max_mdeg\":null}}";

static void test_thermal(void)
{
    char want[3000], empty[512];
    fresh(); /* the switch is off and power is not open: thermal.read still answers */
    CHECK(!zxvd_rpc_enabled("power") && !zxvd_power_is_open(), "off and closed");
    snprintf(empty, sizeof empty, "%s/thermal-empty", R);
    mk("thermal-empty");
    zxvd_power_set_thermal_root(empty);
    snprintf(want, sizeof want,
             "{\"state\":\"neither\",\"host\":\"linux\",\"reason\":\"no temperature sensor under "
             "/sys/class/hwmon or /sys/class/thermal\",%s,\"readings\":[],\"rejected\":0,"
             "\"dropped\":0}",
             NO_CLASSES);
    EXPECT(call("thermal.read", "{}"), ok("%s", want));
    /* the fixture: a CPU package, a GPU edge, an absurd board value */
    put("sys/class/hwmon/hwmon0/name", "k10temp");
    put("sys/class/hwmon/hwmon0/temp1_label", "Tctl");
    put("sys/class/hwmon/hwmon0/temp1_input", "47250");
    put("sys/class/hwmon/hwmon1/name", "amdgpu");
    put("sys/class/hwmon/hwmon1/temp1_label", "edge");
    put("sys/class/hwmon/hwmon1/temp1_input", "55000");
    put("sys/class/thermal/thermal_zone0/type", "acpitz");
    put("sys/class/thermal/thermal_zone0/temp", "999999");
    zxvd_power_set_thermal_root(R);
    snprintf(want, sizeof want,
             "{\"state\":\"both\",\"host\":\"linux\",\"reason\":null,\"classes\":{"
             "\"cpu\":{\"state\":\"true\",\"n\":1,\"max_mdeg\":47250},"
             "\"gpu\":{\"state\":\"true\",\"n\":1,\"max_mdeg\":55000},"
             "\"case\":{\"state\":\"false\",\"n\":0,\"max_mdeg\":null},"
             "\"other\":{\"state\":\"neither\",\"n\":0,\"max_mdeg\":null}},\"readings\":["
             "{\"class\":\"cpu\",\"state\":\"true\",\"mdeg\":47250,\"source\":\"hwmon\","
             "\"driver\":\"k10temp\",\"label\":\"Tctl\",\"path\":"
             "\"/sys/class/hwmon/hwmon0/temp1_input\",\"reason\":null},"
             "{\"class\":\"gpu\",\"state\":\"true\",\"mdeg\":55000,\"source\":\"hwmon\","
             "\"driver\":\"amdgpu\",\"label\":\"edge\",\"path\":"
             "\"/sys/class/hwmon/hwmon1/temp1_input\",\"reason\":null},"
             "{\"class\":\"case\",\"state\":\"false\",\"mdeg\":null,\"source\":\"thermal_zone\","
             "\"driver\":\"acpitz\",\"label\":\"\",\"path\":"
             "\"/sys/class/thermal/thermal_zone0/temp\",\"reason\":\"out of range\"}],"
             "\"rejected\":1,\"dropped\":0}");
    EXPECT(call("thermal.read", "{}"), ok("%s", want));
    /* each call reads afresh */
    put("sys/class/thermal/thermal_zone0/temp", "31000");
    const char *got = rpc(call("thermal.read", "{}"));
    CHECK(strstr(got, "\"state\":\"true\",\"host\"") &&
              strstr(got, "\"case\":{\"state\":\"true\",\"n\":1,\"max_mdeg\":31000}"),
          "W9: read on each call: %.300s", got);
    /* a root too long for a fixture is this machine: whatever it has, the
     * reply is a report (no error) */
    char lng[ZTH_ROOT_MAX + 2];
    memset(lng, 'a', sizeof lng - 1);
    lng[sizeof lng - 1] = 0;
    zxvd_power_set_thermal_root(lng);
    static zth_report_t live;
    static char live_js[96u * 1024u];
    zth_read(NULL, &live);
    int ln = zth_render_json(&live, live_js, sizeof live_js);
    const char *cls = ln > 0 ? strstr(live_js, ",\"classes\":") : NULL;
    got = rpc(call("thermal.read", "{}"));
    static const char *HEAD = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":";
    CHECK(cls && !strncmp(got, HEAD, strlen(HEAD)) &&
              !strncmp(got + strlen(HEAD), live_js, (size_t) (cls - live_js)),
          "W9: this machine's state, host and reason: %.200s", got);
    lng[ZTH_ROOT_MAX] = 0; /* exactly ZTH_ROOT_MAX: no room for its end, so this machine too */
    zxvd_power_set_thermal_root(lng);
    got = rpc(call("thermal.read", "{}"));
    CHECK(!strncmp(got, HEAD, strlen(HEAD)) &&
              !strncmp(got + strlen(HEAD), live_js, (size_t) (cls - live_js)),
          "W9: a root of ZTH_ROOT_MAX bytes is this machine: %.200s", got);
    lng[ZTH_ROOT_MAX - 1] = 0; /* the longest root kept: a fixture that is not there */
    zxvd_power_set_thermal_root(lng);
    snprintf(want, sizeof want,
             "{\"state\":\"neither\",\"host\":\"linux\",\"reason\":\"no temperature sensor under "
             "/sys/class/hwmon or /sys/class/thermal\",%s,\"readings\":[],\"rejected\":0,"
             "\"dropped\":0}",
             NO_CLASSES);
    EXPECT(call("thermal.read", "{}"), ok("%s", want));
    zxvd_power_set_thermal_root(NULL);
}

/* ----- the CPU clock (W3) ----- */

static uint64_t cpu_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return (uint64_t) ts.tv_sec * 1000000000u + (uint64_t) ts.tv_nsec;
}

static void test_cpu(void)
{
    volatile uint64_t x = 0;
    for (uint64_t i = 0; i < 30000000u; i++) x += i ^ (x >> 3);
    uint64_t lo = cpu_now(), v = zxvd_power_cpu_ns(), hi = cpu_now();
    CHECK(lo > 1000000u && lo <= v && v <= hi,
          "W3: the process's CPU time in ns (%llu <= %llu <= %llu)", (unsigned long long) lo,
          (unsigned long long) v, (unsigned long long) hi);
}

/* ----- a full ledger (W7): an app budget with no room left is refused ----- */

static void test_full(void)
{
    fresh();
    NOW = 0;
    CHECK(zxvd_power_open(R) == 0, "open on the fixture");
    CHECK(zxvd_rpc_enable("power") == 0, "power on");
    const char *got = NULL;
    char params[160];
    for (unsigned i = 0; i < PW_MAX_CONSUMERS + 1; i++) {
        snprintf(params, sizeof params,
                 "{\"target\":\"app\",\"app\":\"app%u\",\"window_ms\":1000,\"max_uj\":1}", i);
        got = rpc(call("power.budget", params));
        if (!strstr(got, "\"ok\":true")) break;
    }
    CHECK(got && strstr(got, "no room for another app in the power ledger"),
          "W7: no room for another app: %.200s", got ? got : "");
    zxvd_rpc_groups_reset();
}

int main(void)
{
    zxvd_power_set_clock(clock_ms);
    fixture();
    test_closed();
    test_ledger();
    test_params();
    test_thermal();
    test_cpu();
    test_full();
    zxvd_rpc_groups_reset();
    zxvd_power_set_clock(NULL);
    cleanup();
    free(OUT.p);
    printf("test_zxvd_power: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
