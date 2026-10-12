/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_power.c — power_ledger.h P1-P6 and power_rapl.h R1-R3 against
 * hand-computed figures: meter wrap, faults and ceilings, the interval total
 * and its conflict boundaries, attribution per app and per agent with exact
 * conservation, the GPU pool, per-process metered energy, battery figures and
 * posture thresholds, budgets and their windows, ranking, the exact JSON and
 * RAPL decoding through a fake MSR file. Integers only. */
#include <stdio.h>
#include <string.h>
#include "power_ledger.h"
#include "power_rapl.h"
#include "lpres.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
        }                                                                                          \
    } while (0)

static pw_ledger_t L;

static uint64_t sum_last(const pw_ledger_t *l)
{
    uint64_t s = 0;
    for (int i = 0; i < PW_MAX_CONSUMERS; i++)
        if (l->consumers[i].used) s += l->consumers[i].last_uj;
    return s;
}

static void t_muldiv(void)
{
    CHECK(pw_muldiv(10, 20, 3) == 66, "small");
    CHECK(pw_muldiv(7, 3, 0) == 0, "c=0");
    CHECK(pw_muldiv(0, UINT64_MAX, 5) == 0, "zero");
    CHECK(pw_muldiv((uint64_t) 1 << 63, 4, 8) == (uint64_t) 1 << 62, "128-bit product");
    CHECK(pw_muldiv(UINT64_MAX, UINT64_MAX, UINT64_MAX) == UINT64_MAX, "max");
    CHECK(pw_muldiv(UINT64_MAX, 2, 1) == UINT64_MAX, "saturates");
    CHECK(pw_muldiv(UINT64_MAX, UINT64_MAX - 1, UINT64_MAX) == UINT64_MAX - 1, "exact high");
    CHECK(pw_muldiv((uint64_t) 1 << 32, (uint64_t) 1 << 32, 3) == 6148914691236517205ull, "2^64/3");
    /* cross-check against the compiler's 128-bit arithmetic */
    uint64_t x = 0x9E3779B97F4A7C15ull;
    int bad = 0;
    for (int i = 0; i < 20000; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        uint64_t a = x;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        uint64_t b = x >> (i % 40);
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        uint64_t c = (x >> (i % 50)) | 1u;
        unsigned __int128 q = ((unsigned __int128) a * b) / c;
        uint64_t want = q > UINT64_MAX ? UINT64_MAX : (uint64_t) q;
        if (pw_muldiv(a, b, c) != want) bad++;
    }
    CHECK(bad == 0, "muldiv matches __int128 on 20000 triples");
}

static void t_init_and_meters(void)
{
    pw_init(&L, PW_DEV_LAPTOP, PW_HOST_LINUX, 0, 0, 100);
    CHECK(L.cpu_units == 1, "cpu units default 1");
    CHECK(L.tol_permille == 100, "tolerance 10%");
    CHECK(L.consumers[0].used && L.consumers[0].kind == PW_KIND_SYSTEM, "system consumer");
    CHECK(strcmp(L.consumers[0].name, "system") == 0, "system name");
    CHECK(L.consumers[0].parent == PW_NONE, "system no parent");
    CHECK(L.battery.temp_dc == INT32_MIN, "temp unknown");
    CHECK(L.last_settle_ms == 100 && L.interval_start_ms == 100, "times");
    CHECK(L.device == PW_DEV_LAPTOP && L.host == PW_HOST_LINUX, "device host");

    CHECK(pw_meter_add(&L, "x", (pw_domain_t) 7, PW_SCOPE_PART, 0, 0) == PW_E_ARG, "bad domain");
    CHECK(pw_meter_add(&L, "x", PW_DOM_OTHER, (pw_scope_t) 3, 0, 0) == PW_E_ARG, "bad scope");
    CHECK(pw_meter_add(NULL, "x", PW_DOM_OTHER, PW_SCOPE_PART, 0, 0) == PW_E_ARG, "null");
    for (int i = 0; i < PW_MAX_METERS; i++)
        CHECK(pw_meter_add(&L, "m", PW_DOM_OTHER, PW_SCOPE_SUB, 0, 0) == i, "meter index");
    CHECK(pw_meter_add(&L, "m", PW_DOM_OTHER, PW_SCOPE_SUB, 0, 0) == PW_E_FULL, "meters full");

    pw_init(&L, PW_DEV_DESKTOP, PW_HOST_LINUX, 4, 0, 0);
    int m = pw_meter_add(&L, "a-very-long-meter-name-that-is-cut-off-here", PW_DOM_PACKAGE,
                         PW_SCOPE_PART, 1000, 1000);
    CHECK(m == 0, "first meter");
    CHECK(strlen(L.meters[0].name) == PW_NAME_LEN - 1, "name cut to fit");
    CHECK(pw_meter_sample(&L, 1, 5, 0) == PW_E_ARG, "no such meter");
    CHECK(pw_meter_sample(&L, 0, 100, 10) == PW_OK, "base");
    CHECK(!L.meters[0].fresh && L.meters[0].pending_uj == 0, "base gives no delta");
    CHECK(pw_meter_sample(&L, 0, 400, 20) == PW_OK, "delta");
    CHECK(L.meters[0].pending_uj == 300 && L.meters[0].fresh, "pending 300");
    CHECK(pw_meter_sample(&L, 0, 100, 30) == PW_OK, "wrap");
    CHECK(L.meters[0].pending_uj == 300 + 600 + 100, "wrap delta 700");
    CHECK(pw_meter_sample(&L, 0, 150, 29) == PW_E_ORDER, "time backwards");
    CHECK(L.meters[0].last_uj == 100, "order error keeps base");
    CHECK(pw_meter_sample(&L, 0, 1000, 40) == PW_E_FAULT, "counter at wrap is a fault");
    CHECK(L.meters[0].faults == 1, "fault counted");
    CHECK(L.meters[0].last_uj == 100 && L.meters[0].last_ms == 30, "out-of-range is no base");
    CHECK(pw_meter_sample(&L, 0, 990, 41) == PW_OK, "next reading measures from the old base");
    CHECK(L.meters[0].faults == 1 && L.meters[0].pending_uj == 1890, "890 more");
    CHECK(pw_meter_sample(&L, 0, 5, 42) == PW_OK, "wrap again");
    CHECK(L.meters[0].pending_uj == 1905, "15 across the wrap");
}

static void t_ceiling(void)
{
    pw_init(&L, PW_DEV_DESKTOP, PW_HOST_LINUX, 1, 0, 0);
    int m = pw_meter_add(&L, "pkg", PW_DOM_PACKAGE, PW_SCOPE_PART, 0, 1000);
    pw_meter_sample(&L, (uint32_t) m, 0, 0);
    CHECK(pw_meter_sample(&L, 0, 10000, 10) == PW_OK, "exactly at ceiling");
    CHECK(pw_meter_sample(&L, 0, 20001, 20) == PW_E_FAULT, "one over ceiling");
    CHECK(L.meters[0].pending_uj == 10000 && L.meters[0].total_uj == 10000, "fault dropped");
    CHECK(pw_meter_sample(&L, 0, 20001, 20) == PW_OK, "same ms, no energy");
    CHECK(pw_meter_sample(&L, 0, 20002, 20) == PW_E_FAULT, "energy in zero time");
    CHECK(pw_meter_sample(&L, 0, 5, 30) == PW_E_FAULT, "backwards without wrap");
    CHECK(L.meters[0].faults == 3, "three faults");
}

static void t_none(void)
{
    pw_init(&L, PW_DEV_DESKTOP, PW_HOST_WINDOWS, 2, 0, 0);
    int a = pw_consumer_add(&L, PW_KIND_APP, "app", PW_NONE, 7);
    pw_note_activity(&L, (uint16_t) a, 1000000000u, 0);
    CHECK(pw_settle(&L, 1000) == PW_OK, "settle");
    CHECK(L.last_src == PW_SRC_NONE && L.last_total_uj == 0, "no meter -> none");
    CHECK(L.consumers[a].last_uj == 0 && L.consumers[a].last_src == PW_SRC_NONE, "app none");
    CHECK(L.consumers[0].last_src == PW_SRC_NONE, "system none");
    CHECK(L.consumers[a].total_cpu_ns == 1000000000u, "activity still kept");
    CHECK(L.consumers[a].cpu_ns == 0, "interval activity reset");
    CHECK(L.consumers[a].last_cpu_ns == 1000000000u, "last interval activity kept");
    CHECK(pw_settle(&L, 999) == PW_E_ORDER, "settle backwards");
    CHECK(L.intervals == 1 && L.conflicts == 0, "counts");
}

static void t_attribution(void)
{
    pw_init(&L, PW_DEV_LAPTOP, PW_HOST_LINUX, 4, 0, 0);
    int pk = pw_meter_add(&L, "pkg", PW_DOM_PACKAGE, PW_SCOPE_PART, 0, 0);
    int co = pw_meter_add(&L, "cores", PW_DOM_CORES, PW_SCOPE_SUB, 0, 0);
    pw_meter_sample(&L, (uint32_t) pk, 5000, 0);
    pw_meter_sample(&L, (uint32_t) co, 0, 0);
    pw_meter_sample(&L, (uint32_t) pk, 1005000, 1000);
    pw_meter_sample(&L, (uint32_t) co, 800000, 1000);
    int a = pw_consumer_add(&L, PW_KIND_APP, "editor", PW_NONE, 101);
    int b = pw_consumer_add(&L, PW_KIND_APP, "browser", PW_NONE, 102);
    int z = pw_consumer_add(&L, PW_KIND_APP, "zxvd", PW_NONE, 103);
    int g1 = pw_consumer_add(&L, PW_KIND_AGENT, "qwen", (uint16_t) z, 1);
    int g2 = pw_consumer_add(&L, PW_KIND_AGENT, "coder", (uint16_t) z, 2);
    CHECK(a == 1 && b == 2 && z == 3 && g1 == 4 && g2 == 5, "ids in order");
    pw_note_activity(&L, (uint16_t) a, 1000000000u, 0);
    pw_note_activity(&L, (uint16_t) b, 500000000u, 0);
    pw_note_activity(&L, (uint16_t) b, 500000000u, 0); /* adds up */
    pw_note_activity(&L, (uint16_t) z, 1000000000u, 0);
    pw_note_activity(&L, (uint16_t) g1, 500000000u, 0);
    pw_note_activity(&L, (uint16_t) g2, 250000000u, 0);
    CHECK(pw_settle(&L, 1000) == PW_OK, "settle");
    CHECK(L.last_src == PW_SRC_METERED, "package metered");
    CHECK(L.last_total_uj == 1000000, "SUB not added");
    CHECK(L.last_total_mw == 1000, "1 W");
    CHECK(L.consumers[a].last_uj == 250000, "editor quarter");
    CHECK(L.consumers[b].last_uj == 250000, "browser quarter");
    CHECK(L.consumers[g1].last_uj == 125000, "agent half of zxvd");
    CHECK(L.consumers[g2].last_uj == 62500, "agent quarter of zxvd");
    CHECK(L.consumers[z].last_uj == 62500, "zxvd keeps the rest");
    CHECK(L.consumers[0].last_uj == 250000, "idle to system");
    CHECK(sum_last(&L) == L.last_total_uj, "conserves exactly");
    CHECK(L.consumers[a].last_src == PW_SRC_ATTRIBUTED, "attributed");
    CHECK(L.consumers[g1].last_src == PW_SRC_ATTRIBUTED, "agent attributed");
    CHECK(L.consumers[0].last_src == PW_SRC_ATTRIBUTED, "system attributed");
    CHECK(L.consumers[a].last_mw == 250, "editor 250 mW");
    CHECK(L.consumers[g2].last_mw == 62, "agent 62 mW (floor)");
    CHECK(L.meters[pk].pending_uj == 0 && !L.meters[pk].fresh, "meter reset");
    CHECK(L.meters[co].total_uj == 800000, "sub lifetime kept");
    CHECK(L.interval_start_ms == 0 && L.last_settle_ms == 1000, "interval bounds");

    /* second interval: lifetime adds; an interval with no samples is NONE */
    pw_note_activity(&L, (uint16_t) a, 4000000000u, 0);
    CHECK(pw_settle(&L, 2000) == PW_OK, "second settle");
    CHECK(L.last_src == PW_SRC_NONE, "no fresh meter");
    CHECK(L.consumers[a].total_uj == 250000, "lifetime unchanged");
    CHECK(L.total_uj == 1000000, "device lifetime");

    /* rounding: three equal users of 1000 uJ over 3 cpus, all busy */
    pw_init(&L, PW_DEV_DESKTOP, PW_HOST_LINUX, 3, 0, 0);
    pk = pw_meter_add(&L, "pkg", PW_DOM_PACKAGE, PW_SCOPE_PART, 0, 0);
    pw_meter_sample(&L, 0, 0, 0);
    pw_meter_sample(&L, 0, 1000, 1);
    for (int i = 0; i < 3; i++) {
        int c = pw_consumer_add(&L, PW_KIND_APP, "u", PW_NONE, (uint64_t) i);
        pw_note_activity(&L, (uint16_t) c, 1000000u, 0);
    }
    pw_settle(&L, 1);
    CHECK(L.consumers[1].last_uj == 333 && L.consumers[3].last_uj == 333, "floor shares");
    CHECK(L.consumers[0].last_uj == 1, "rounding to system");
    CHECK(sum_last(&L) == 1000, "rounding conserves");

    /* skew: more activity than capacity, the pool is handed out, never more */
    pw_init(&L, PW_DEV_DESKTOP, PW_HOST_LINUX, 1, 0, 0);
    pw_meter_add(&L, "pkg", PW_DOM_PACKAGE, PW_SCOPE_PART, 0, 0);
    pw_meter_sample(&L, 0, 0, 0);
    pw_meter_sample(&L, 0, 1000, 1000);
    a = pw_consumer_add(&L, PW_KIND_APP, "a", PW_NONE, 1);
    b = pw_consumer_add(&L, PW_KIND_APP, "b", PW_NONE, 2);
    pw_note_activity(&L, (uint16_t) a, 1500000000u, 0);
    pw_note_activity(&L, (uint16_t) b, 500000000u, 0);
    pw_settle(&L, 1000);
    CHECK(L.consumers[a].last_uj == 750 && L.consumers[b].last_uj == 250, "skew shares");
    CHECK(L.consumers[0].last_uj == 0, "skew leaves system nothing");

    /* zero-length interval */
    pw_settle(&L, 1000);
    CHECK(L.last_total_mw == 0 && L.consumers[a].last_mw == 0, "dt 0 gives 0 mW");
}

static void t_conflicts(void)
{
    /* whole + part that agree: whole wins */
    pw_init(&L, PW_DEV_LAPTOP, PW_HOST_LINUX, 1, 0, 0);
    pw_meter_add(&L, "psys", PW_DOM_SYSTEM, PW_SCOPE_WHOLE, 0, 0);
    pw_meter_add(&L, "pkg", PW_DOM_PACKAGE, PW_SCOPE_PART, 0, 0);
    pw_meter_sample(&L, 0, 0, 0);
    pw_meter_sample(&L, 1, 0, 0);
    pw_meter_sample(&L, 0, 1000000, 1000);
    pw_meter_sample(&L, 1, 900000, 1000);
    pw_settle(&L, 1000);
    CHECK(L.last_src == PW_SRC_METERED && L.last_total_uj == 1000000, "whole wins");
    /* parts exactly 10% over: still agree */
    pw_meter_sample(&L, 0, 2000000, 2000);
    pw_meter_sample(&L, 1, 2000000, 2000);
    pw_settle(&L, 2000);
    CHECK(L.last_src == PW_SRC_METERED && L.last_total_uj == 1000000, "10% over agrees");
    /* parts one uJ past 10%: conflict, larger kept, other recorded */
    pw_meter_sample(&L, 0, 3000000, 3000);
    pw_meter_sample(&L, 1, 3100001, 3000);
    pw_settle(&L, 3000);
    CHECK(L.last_src == PW_SRC_CONFLICT, "parts over whole");
    CHECK(L.last_total_uj == 1100001 && L.last_other_uj == 1000000, "larger used");
    CHECK(L.conflicts == 1, "conflict counted");
    CHECK(L.consumers[0].last_src == PW_SRC_CONFLICT, "system shares the conflict");
    CHECK(L.consumers[0].last_uj == 1100001, "system holds it all");

    /* two whole meters (psys + battery drain) */
    pw_init(&L, PW_DEV_LAPTOP, PW_HOST_LINUX, 1, 0, 0);
    pw_meter_add(&L, "psys", PW_DOM_SYSTEM, PW_SCOPE_WHOLE, 0, 0);
    pw_meter_add(&L, "battery", PW_DOM_BATTERY, PW_SCOPE_WHOLE, 0, 0);
    pw_meter_sample(&L, 0, 0, 0);
    pw_meter_sample(&L, 1, 0, 0);
    pw_meter_sample(&L, 0, 900000, 1000);
    pw_meter_sample(&L, 1, 1000000, 1000);
    pw_settle(&L, 1000);
    CHECK(L.last_src == PW_SRC_METERED && L.last_total_uj == 1000000, "exactly 10% apart agrees");
    pw_meter_sample(&L, 0, 1799999, 2000);
    pw_meter_sample(&L, 1, 2000000, 2000);
    pw_settle(&L, 2000);
    CHECK(L.last_src == PW_SRC_CONFLICT, "just past 10% conflicts");
    CHECK(L.last_total_uj == 1000000 && L.last_other_uj == 899999, "max used, min kept");
    pw_meter_sample(&L, 1, 2500000, 3000); /* only the battery reports */
    pw_settle(&L, 3000);
    CHECK(L.last_src == PW_SRC_METERED && L.last_total_uj == 500000, "single whole");
    CHECK(L.last_other_uj == 0, "other cleared");

    /* tolerance zero would make any difference a conflict */
    L.tol_permille = 0;
    pw_meter_sample(&L, 0, 2299998, 4000);
    pw_meter_sample(&L, 1, 3000000, 4000);
    pw_settle(&L, 4000);
    CHECK(L.last_src == PW_SRC_CONFLICT && L.last_other_uj == 499999, "tol 0");
    CHECK(L.last_total_uj == 500000, "tol 0 total");
}

static void t_gpu_and_direct(void)
{
    pw_init(&L, PW_DEV_DESKTOP, PW_HOST_LINUX, 2, 1, 0);
    pw_meter_add(&L, "pkg", PW_DOM_PACKAGE, PW_SCOPE_PART, 0, 0);
    pw_meter_add(&L, "gpu", PW_DOM_GPU, PW_SCOPE_PART, 0, 0);
    pw_meter_sample(&L, 0, 0, 0);
    pw_meter_sample(&L, 1, 0, 0);
    pw_meter_sample(&L, 0, 700000, 1000);
    pw_meter_sample(&L, 1, 300000, 1000);
    int a = pw_consumer_add(&L, PW_KIND_APP, "render", PW_NONE, 1);
    int b = pw_consumer_add(&L, PW_KIND_APP, "shell", PW_NONE, 2);
    pw_note_activity(&L, (uint16_t) a, 1000000000u, 500000000u);
    pw_note_activity(&L, (uint16_t) b, 1000000000u, 0);
    pw_settle(&L, 1000);
    CHECK(L.last_total_uj == 1000000, "parts add");
    CHECK(L.consumers[a].last_uj == 350000 + 150000, "cpu half + gpu half");
    CHECK(L.consumers[b].last_uj == 350000, "cpu half");
    CHECK(L.consumers[0].last_uj == 150000, "idle gpu to system");
    CHECK(L.consumers[a].total_gpu_ns == 500000000u, "gpu time kept");

    /* a GPU meter with no GPU units known: everything is one pool */
    pw_init(&L, PW_DEV_DESKTOP, PW_HOST_LINUX, 1, 0, 0);
    pw_meter_add(&L, "gpu", PW_DOM_GPU, PW_SCOPE_PART, 0, 0);
    pw_meter_sample(&L, 0, 0, 0);
    pw_meter_sample(&L, 0, 1000, 1000);
    a = pw_consumer_add(&L, PW_KIND_APP, "a", PW_NONE, 1);
    pw_note_activity(&L, (uint16_t) a, 1000000000u, 0);
    pw_settle(&L, 1000);
    CHECK(L.consumers[a].last_uj == 1000, "no gpu units: cpu pool");

    /* macOS shape: per-process energy only, no device meter */
    pw_init(&L, PW_DEV_DESKTOP, PW_HOST_MACOS, 8, 0, 0);
    a = pw_consumer_add(&L, PW_KIND_APP, "metered", PW_NONE, 1);
    b = pw_consumer_add(&L, PW_KIND_APP, "unmetered", PW_NONE, 2);
    int g = pw_consumer_add(&L, PW_KIND_AGENT, "agent", (uint16_t) a, 9);
    CHECK(pw_note_energy(&L, (uint16_t) g, 5) == PW_E_ARG, "agents are never metered directly");
    CHECK(pw_note_energy(&L, PW_SYSTEM, 5) == PW_E_ARG, "nor the system");
    pw_note_energy(&L, (uint16_t) a, 300000);
    pw_note_energy(&L, (uint16_t) a, 100000);
    pw_note_activity(&L, (uint16_t) a, 400000000u, 0);
    pw_note_activity(&L, (uint16_t) g, 100000000u, 0);
    pw_note_activity(&L, (uint16_t) b, 4000000000u, 0);
    pw_settle(&L, 1000);
    CHECK(L.last_src == PW_SRC_METERED && L.last_partial, "lower bound");
    CHECK(L.last_total_uj == 400000, "sum of metered");
    CHECK(L.consumers[a].last_src == PW_SRC_METERED, "metered app");
    CHECK(L.consumers[g].last_uj == 100000 && L.consumers[a].last_uj == 300000, "agent quarter");
    CHECK(L.consumers[g].last_src == PW_SRC_ATTRIBUTED, "agent of metered app is attributed");
    CHECK(L.consumers[b].last_uj == 0, "nothing left to attribute");
    CHECK(L.consumers[0].last_uj == 0, "system nothing");
    CHECK(sum_last(&L) == 400000, "conserves");

    /* a whole meter plus a metered app */
    pw_meter_add(&L, "battery", PW_DOM_BATTERY, PW_SCOPE_WHOLE, 0, 0);
    pw_meter_sample(&L, 0, 0, 1000);
    pw_meter_sample(&L, 0, 1000000, 2000);
    pw_note_energy(&L, (uint16_t) a, 400000);
    pw_note_activity(&L, (uint16_t) b, 4000000000u, 0); /* half of 8 cpus */
    pw_settle(&L, 2000);
    CHECK(!L.last_partial && L.last_src == PW_SRC_METERED, "full total");
    CHECK(L.consumers[b].last_uj == 300000, "half of the rest");
    CHECK(L.consumers[0].last_uj == 300000, "system the other half");
    CHECK(L.consumers[a].last_uj == 400000, "agent idle: app keeps all");
    CHECK(L.consumers[g].last_uj == 0, "idle agent");

    /* metered apps claiming more than the device used */
    pw_meter_sample(&L, 0, 1500000, 3000);
    pw_note_energy(&L, (uint16_t) a, 600000);
    pw_settle(&L, 3000);
    CHECK(L.last_src == PW_SRC_CONFLICT, "direct over total");
    CHECK(L.last_total_uj == 600000 && L.last_other_uj == 500000, "larger kept");
    CHECK(L.consumers[a].last_src == PW_SRC_CONFLICT, "app marked conflict");
    CHECK(L.consumers[g].last_src == PW_SRC_CONFLICT, "agent inherits conflict");
}

static void t_battery(void)
{
    pw_battery_t b;
    memset(&b, 0, sizeof(b));
    CHECK(pw_battery_health_permille(&b) == 0, "absent");
    CHECK(pw_battery_health_permille(NULL) == 0, "null");
    b.present = true;
    b.design_mwh = 50000;
    b.full_mwh = 45000;
    b.now_mwh = 30000;
    b.rate_mw = 15000;
    CHECK(pw_battery_health_permille(&b) == 900, "health 90%");
    CHECK(pw_battery_minutes_to_empty(&b) == 120, "2 h left");
    CHECK(pw_battery_minutes_to_full(&b) == 0, "not charging");
    b.full_mwh = 51000;
    CHECK(pw_battery_health_permille(&b) == 1020, "new battery over design is reported as is");
    b.full_mwh = 45000;
    b.on_mains = true;
    CHECK(pw_battery_minutes_to_empty(&b) == 0, "on mains");
    b.on_mains = false;
    b.rate_mw = 0;
    CHECK(pw_battery_minutes_to_empty(&b) == 0, "idle");
    b.charging = true;
    b.on_mains = true;
    b.rate_mw = -7500;
    CHECK(pw_battery_minutes_to_full(&b) == 120, "2 h to full");
    b.rate_mw = INT64_MIN;
    CHECK(pw_battery_minutes_to_full(&b) == 0, "huge rate, no overflow");
    b.now_mwh = 45000;
    b.rate_mw = -7500;
    CHECK(pw_battery_minutes_to_full(&b) == 0, "already full");
    b.design_mwh = 0;
    CHECK(pw_battery_health_permille(&b) == 0, "no design");

    pw_init(&L, PW_DEV_LAPTOP, PW_HOST_LINUX, 1, 0, 0);
    CHECK(pw_posture(&L) == PW_POSTURE_PERFORM, "no battery");
    CHECK(pw_posture(NULL) == PW_POSTURE_PERFORM, "null");
    memset(&b, 0, sizeof(b));
    b.present = true;
    b.permille = 1200;
    pw_battery_set(&L, &b);
    CHECK(L.battery.permille == 1000, "clamped");
    CHECK(pw_posture(&L) == PW_POSTURE_BALANCED, "on battery");
    int cases[][2] = {{201, PW_POSTURE_BALANCED},
                      {200, PW_POSTURE_CONSERVE},
                      {51, PW_POSTURE_CONSERVE},
                      {50, PW_POSTURE_CRITICAL},
                      {0, PW_POSTURE_CRITICAL}};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        b.permille = (uint16_t) cases[i][0];
        pw_battery_set(&L, &b);
        CHECK(pw_posture(&L) == (pw_posture_t) cases[i][1], "posture threshold");
    }
    b.on_mains = true;
    pw_battery_set(&L, &b);
    CHECK(pw_posture(&L) == PW_POSTURE_PERFORM, "plugged in at 0%");
    pw_battery_set(&L, NULL);
    CHECK(L.battery.on_mains, "null ignored");
    CHECK(pw_posture_token_scale(PW_POSTURE_PERFORM) == 1000, "scale perform");
    CHECK(pw_posture_token_scale(PW_POSTURE_BALANCED) == 618, "scale balanced");
    CHECK(pw_posture_token_scale(PW_POSTURE_CONSERVE) == 382, "scale conserve");
    CHECK(pw_posture_token_scale(PW_POSTURE_CRITICAL) == 0, "scale critical");
}

static void t_budgets(void)
{
    pw_init(&L, PW_DEV_DESKTOP, PW_HOST_LINUX, 1, 0, 0);
    pw_meter_add(&L, "pkg", PW_DOM_PACKAGE, PW_SCOPE_PART, 0, 0);
    int a = pw_consumer_add(&L, PW_KIND_APP, "a", PW_NONE, 1);
    int g = pw_consumer_add(&L, PW_KIND_AGENT, "g", (uint16_t) a, 2);
    CHECK(pw_admit(&L, (uint16_t) a, UINT64_MAX, 0) == PW_ADMIT_OK, "no budget");
    CHECK(pw_admit(&L, 99, 1, 0) == PW_ADMIT_BAD, "unknown");
    CHECK(pw_admit(NULL, 1, 1, 0) == PW_ADMIT_BAD, "null");
    CHECK(pw_budget_set(&L, 99, 1000, 1, false, 0) == PW_E_ARG, "unknown budget");
    CHECK(pw_budget_set(&L, (uint16_t) a, 1000, 600000, false, 0) == PW_OK, "set");
    CHECK(pw_budget_set(&L, (uint16_t) g, 1000, 100000, true, 0) == PW_OK, "strict agent");
    CHECK(pw_admit(&L, (uint16_t) g, 1, 0) == PW_ADMIT_HOLD, "strict, nothing measured");
    CHECK(pw_admit(&L, (uint16_t) a, 600000, 0) == PW_ADMIT_OK, "within");
    CHECK(pw_admit(&L, (uint16_t) a, 600001, 0) == PW_ADMIT_WAIT, "over");

    pw_meter_sample(&L, 0, 0, 0);
    pw_meter_sample(&L, 0, 1000000, 500);
    pw_note_activity(&L, (uint16_t) a, 250000000u, 0);
    pw_note_activity(&L, (uint16_t) g, 125000000u, 0);
    pw_settle(&L, 500);
    CHECK(L.consumers[a].last_uj == 250000 && L.consumers[g].last_uj == 250000, "split");
    CHECK(L.consumers[a].budget.used_uj == 250000, "app used");
    CHECK(L.consumers[g].budget.used_uj == 250000, "agent used");
    CHECK(pw_admit(&L, (uint16_t) a, 350000, 600) == PW_ADMIT_OK, "app has 350000 left");
    CHECK(pw_admit(&L, (uint16_t) a, 350001, 600) == PW_ADMIT_WAIT, "app one over");
    CHECK(pw_admit(&L, (uint16_t) g, 1, 600) == PW_ADMIT_WAIT, "agent spent");
    CHECK(pw_admit(&L, (uint16_t) g, 1, 999) == PW_ADMIT_WAIT, "still in window");
    CHECK(pw_admit(&L, (uint16_t) g, 100000, 1000) == PW_ADMIT_OK, "window rolled");
    CHECK(pw_admit(&L, (uint16_t) g, 100001, 1000) == PW_ADMIT_WAIT, "fresh window cap");

    /* the app's budget binds its agent too */
    pw_budget_set(&L, (uint16_t) g, 0, 0, false, 500);
    CHECK(L.consumers[g].budget.window_ms == 0, "cleared");
    CHECK(pw_admit(&L, (uint16_t) g, 350001, 600) == PW_ADMIT_WAIT, "parent budget binds");
    CHECK(pw_admit(&L, (uint16_t) g, 350000, 600) == PW_ADMIT_OK, "parent has room");

    /* device budget (the site's ecological limit) */
    CHECK(pw_budget_set(&L, PW_NONE, 1000, 1500000, false, 0) == PW_OK, "device budget");
    CHECK(pw_admit(&L, PW_NONE, 1500000, 0) == PW_ADMIT_OK, "device ok");
    pw_meter_sample(&L, 0, 2500000, 2500);
    pw_settle(&L, 2500);
    CHECK(L.device_budget.start_ms == 2000, "window aligned to its grid");
    CHECK(L.device_budget.used_uj == 1500000, "rolled, then this interval");
    CHECK(L.consumers[a].budget.start_ms == 2000 && L.consumers[a].budget.used_uj == 0,
          "app rolled, idle");
    CHECK(pw_admit(&L, (uint16_t) a, 1, 2600) == PW_ADMIT_WAIT, "device spent binds the app");
    CHECK(pw_admit(&L, PW_NONE, 1, 2999) == PW_ADMIT_WAIT, "device spent");
    CHECK(pw_admit(&L, PW_NONE, 1500000, 3000) == PW_ADMIT_OK, "device next window");
    CHECK(pw_admit(&L, PW_NONE, 1500001, 3000) == PW_ADMIT_WAIT, "device cap");
    pw_budget_set(&L, PW_NONE, 1000, 1, true, 3000);
    pw_settle(&L, 3500); /* nothing sampled: NONE */
    CHECK(pw_admit(&L, PW_NONE, 0, 3500) == PW_ADMIT_HOLD, "strict device, nothing measured");
    CHECK(pw_admit(&L, (uint16_t) a, 0, 3500) == PW_ADMIT_HOLD, "hold beats ok");
    CHECK(pw_budget_set(NULL, 1, 1, 1, false, 0) == PW_E_ARG, "null");
}

static void t_consumers(void)
{
    pw_init(&L, PW_DEV_MOBILE, PW_HOST_ANDROID, 1, 0, 0);
    CHECK(pw_consumer_add(&L, PW_KIND_SYSTEM, "s", PW_NONE, 0) == PW_E_ARG, "no second system");
    CHECK(pw_consumer_add(&L, PW_KIND_APP, "a", 0, 0) == PW_E_ARG, "app with parent");
    CHECK(pw_consumer_add(&L, PW_KIND_AGENT, "g", PW_NONE, 0) == PW_E_ARG, "agent no parent");
    CHECK(pw_consumer_add(&L, PW_KIND_AGENT, "g", PW_SYSTEM, 0) == PW_E_ARG, "agent under system");
    CHECK(pw_consumer_add(NULL, PW_KIND_APP, "a", PW_NONE, 0) == PW_E_ARG, "null");
    int a = pw_consumer_add(&L, PW_KIND_APP, "a", PW_NONE, 41);
    int g = pw_consumer_add(&L, PW_KIND_AGENT, "g", (uint16_t) a, 42);
    CHECK(pw_consumer_add(&L, PW_KIND_AGENT, "h", (uint16_t) g, 43) == PW_E_ARG, "agent of agent");
    CHECK(pw_consumer_find(&L, PW_KIND_APP, 41) == a, "find app");
    CHECK(pw_consumer_find(&L, PW_KIND_AGENT, 42) == g, "find agent");
    CHECK(pw_consumer_find(&L, PW_KIND_APP, 42) == PW_E_ARG, "kind matters");
    CHECK(pw_consumer_find(NULL, PW_KIND_APP, 41) == PW_E_ARG, "null find");
    CHECK(pw_consumer_remove(&L, PW_SYSTEM) == PW_E_ARG, "system stays");
    CHECK(pw_consumer_remove(&L, 77) == PW_E_ARG, "unknown");
    CHECK(pw_note_activity(&L, 77, 1, 1) == PW_E_ARG, "activity unknown");
    CHECK(pw_note_activity(&L, PW_MAX_CONSUMERS, 1, 1) == PW_E_ARG, "activity out of range");
    CHECK(pw_note_activity(&L, (uint16_t) a, UINT64_MAX, 0) == PW_OK, "big");
    CHECK(pw_note_activity(&L, (uint16_t) a, 5, 0) == PW_OK, "saturates");
    CHECK(L.consumers[a].cpu_ns == UINT64_MAX, "saturated");
    CHECK(pw_consumer_remove(&L, (uint16_t) a) == PW_OK, "remove app");
    CHECK(!L.consumers[g].used, "its agents go with it");
    CHECK(pw_consumer_find(&L, PW_KIND_APP, 41) == PW_E_ARG, "gone");
    int r = pw_consumer_add(&L, PW_KIND_APP, NULL, PW_NONE, 50);
    CHECK(r == 1 && L.consumers[1].name[0] == 0, "slot reused, null name");
    int b = pw_consumer_add(&L, PW_KIND_APP, "b", PW_NONE, 51);
    int k = pw_consumer_add(&L, PW_KIND_AGENT, "k", (uint16_t) b, 52);
    CHECK(pw_consumer_remove(&L, (uint16_t) k) == PW_OK && L.consumers[b].used, "agent alone");
    for (int i = 0; i < PW_MAX_CONSUMERS; i++) pw_consumer_add(&L, PW_KIND_APP, "f", PW_NONE, 0);
    CHECK(pw_consumer_add(&L, PW_KIND_APP, "f", PW_NONE, 0) == PW_E_FULL, "full");
}

static void t_top(void)
{
    pw_init(&L, PW_DEV_DESKTOP, PW_HOST_LINUX, 1, 0, 0);
    int ids[6];
    for (int i = 0; i < 5; i++) ids[i] = pw_consumer_add(&L, PW_KIND_APP, "x", PW_NONE, 0);
    int g = pw_consumer_add(&L, PW_KIND_AGENT, "g", (uint16_t) ids[0], 0);
    uint64_t vals[] = {10, 50, 30, 50, 0};
    for (int i = 0; i < 5; i++) {
        L.consumers[ids[i]].last_uj = vals[i];
        L.consumers[ids[i]].total_uj = 100 - vals[i];
    }
    L.consumers[g].last_uj = 40;
    L.consumers[0].last_uj = 20;
    uint16_t out[8];
    uint32_t n = pw_top(&L, PW_KIND_APP, false, out, 8);
    CHECK(n == 5, "five apps");
    CHECK(out[0] == ids[1] && out[1] == ids[3], "tie: lower id first");
    CHECK(out[2] == ids[2] && out[3] == ids[0] && out[4] == ids[4], "descending");
    n = pw_top(&L, -1, false, out, 3);
    CHECK(n == 3 && out[0] == ids[1] && out[1] == ids[3] && out[2] == g, "all kinds, cut");
    n = pw_top(&L, PW_KIND_AGENT, false, out, 8);
    CHECK(n == 1 && out[0] == g, "agents only");
    n = pw_top(&L, PW_KIND_APP, true, out, 2);
    CHECK(n == 2 && out[0] == ids[4] && out[1] == ids[0], "lifetime");
    CHECK(pw_top(&L, -1, false, out, 0) == 0, "max 0");
    CHECK(pw_top(NULL, -1, false, out, 3) == 0, "null");
    n = pw_top(&L, PW_KIND_SYSTEM, false, out, 8);
    CHECK(n == 1 && out[0] == 0, "system");
}

static void t_names(void)
{
    CHECK(pw_source_truth(PW_SRC_NONE) == LPRES_STATE_NEITHER, "none neither");
    CHECK(pw_source_truth(PW_SRC_METERED) == LPRES_STATE_TRUE, "metered true");
    CHECK(pw_source_truth(PW_SRC_ATTRIBUTED) == LPRES_STATE_TRUE, "attributed true");
    CHECK(pw_source_truth(PW_SRC_FAULT) == LPRES_STATE_FALSE, "fault false");
    CHECK(pw_source_truth(PW_SRC_CONFLICT) == LPRES_STATE_BOTH, "conflict both");
    CHECK(strcmp(pw_source_name(PW_SRC_NONE), "none") == 0, "n1");
    CHECK(strcmp(pw_source_name(PW_SRC_METERED), "metered") == 0, "n2");
    CHECK(strcmp(pw_source_name(PW_SRC_ATTRIBUTED), "attributed") == 0, "n3");
    CHECK(strcmp(pw_source_name(PW_SRC_CONFLICT), "conflict") == 0, "n4");
    CHECK(strcmp(pw_source_name(PW_SRC_FAULT), "fault") == 0, "n5");
    CHECK(strcmp(pw_posture_name(PW_POSTURE_PERFORM), "perform") == 0, "p1");
    CHECK(strcmp(pw_posture_name(PW_POSTURE_BALANCED), "balanced") == 0, "p2");
    CHECK(strcmp(pw_posture_name(PW_POSTURE_CONSERVE), "conserve") == 0, "p3");
    CHECK(strcmp(pw_posture_name(PW_POSTURE_CRITICAL), "critical") == 0, "p4");
    CHECK(strcmp(pw_kind_name(PW_KIND_SYSTEM), "system") == 0, "k1");
    CHECK(strcmp(pw_kind_name(PW_KIND_APP), "app") == 0, "k2");
    CHECK(strcmp(pw_kind_name(PW_KIND_AGENT), "agent") == 0, "k3");
    CHECK(strcmp(pw_device_name(PW_DEV_DESKTOP), "desktop") == 0, "d1");
    CHECK(strcmp(pw_device_name(PW_DEV_LAPTOP), "laptop") == 0, "d2");
    CHECK(strcmp(pw_device_name(PW_DEV_MOBILE), "mobile") == 0, "d3");
    CHECK(strcmp(pw_device_name(PW_DEV_SERVER), "server") == 0, "d4");
    CHECK(strcmp(pw_host_name(PW_HOST_BAREMETAL), "baremetal") == 0, "h1");
    CHECK(strcmp(pw_host_name(PW_HOST_LINUX), "linux") == 0, "h2");
    CHECK(strcmp(pw_host_name(PW_HOST_MACOS), "macos") == 0, "h3");
    CHECK(strcmp(pw_host_name(PW_HOST_WINDOWS), "windows") == 0, "h4");
    CHECK(strcmp(pw_host_name(PW_HOST_IOS), "ios") == 0, "h5");
    CHECK(strcmp(pw_host_name(PW_HOST_ANDROID), "android") == 0, "h6");
}

static void t_json(void)
{
    static char buf[8192];
    pw_init(&L, PW_DEV_LAPTOP, PW_HOST_LINUX, 2, 0, 0);
    pw_meter_add(&L, "pkg", PW_DOM_PACKAGE, PW_SCOPE_PART, 0, 0);
    pw_meter_sample(&L, 0, 0, 0);
    pw_meter_sample(&L, 0, 2000, 1000);
    int a = pw_consumer_add(&L, PW_KIND_APP, "ed\"it\\or\n", PW_NONE, 12);
    int g = pw_consumer_add(&L, PW_KIND_AGENT, "qwen", (uint16_t) a, 3);
    pw_note_activity(&L, (uint16_t) a, 1000000000u, 0);
    pw_note_activity(&L, (uint16_t) g, 500000000u, 0);
    pw_budget_set(&L, (uint16_t) a, 60000, 9000, true, 0);
    pw_battery_t b;
    memset(&b, 0, sizeof(b));
    b.present = true;
    b.permille = 150;
    b.now_mwh = 6000;
    b.full_mwh = 40000;
    b.design_mwh = 50000;
    b.rate_mw = 3000;
    b.cycles = 321;
    b.temp_dc = -15;
    pw_battery_set(&L, &b);
    pw_settle(&L, 1000);
    int n = pw_render_json(&L, buf, sizeof(buf));
    const char *want =
        "{\"device\":\"laptop\",\"host\":\"linux\",\"interval_ms\":1000,\"total_uj\":2000,"
        "\"total_mw\":2,\"source\":\"metered\",\"lower_bound\":false,\"lifetime_uj\":2000,"
        "\"posture\":\"conserve\",\"token_scale_permille\":382,\"battery\":{\"on_mains\":false,"
        "\"charging\":false,\"permille\":150,\"now_mwh\":6000,\"full_mwh\":40000,"
        "\"design_mwh\":50000,\"health_permille\":800,\"rate_mw\":3000,\"cycles\":321,"
        "\"minutes_to_empty\":120,\"minutes_to_full\":0,\"temp_dc\":-15},\"meters\":[{\"name\":"
        "\"pkg\",\"domain\":2,\"scope\":1,\"lifetime_uj\":2000,\"faults\":0}],\"consumers\":[{"
        "\"id\":0,\"kind\":\"system\",\"name\":\"system\",\"parent\":null,\"ext_id\":0,"
        "\"uj\":1000,\"mw\":1,\"source\":\"attributed\",\"lifetime_uj\":1000,\"cpu_ns\":0,"
        "\"gpu_ns\":0,\"lifetime_cpu_ns\":0,\"lifetime_gpu_ns\":0},{\"id\":1,\"kind\":\"app\","
        "\"name\":\"ed\\\"it\\\\or\\u000a\","
        "\"parent\":null,\"ext_id\":12,\"uj\":500,\"mw\":0,\"source\":\"attributed\","
        "\"lifetime_uj\":500,\"cpu_ns\":1000000000,\"gpu_ns\":0,\"lifetime_cpu_ns\":1000000000,"
        "\"lifetime_gpu_ns\":0,\"budget\":{\"window_ms\":60000,"
        "\"max_uj\":9000,\"used_uj\":500,\"strict\":true}},{\"id\":2,\"kind\":\"agent\","
        "\"name\":\"qwen\",\"parent\":1,\"ext_id\":3,\"uj\":500,\"mw\":0,\"source\":"
        "\"attributed\",\"lifetime_uj\":500,\"cpu_ns\":500000000,\"gpu_ns\":0,"
        "\"lifetime_cpu_ns\":500000000,\"lifetime_gpu_ns\":0}]}";
    CHECK(n == (int) strlen(want), "json length");
    CHECK(strcmp(buf, want) == 0, "exact json");
    if (strcmp(buf, want) != 0) printf("got  %s\nwant %s\n", buf, want);
    CHECK(pw_render_json(&L, buf, (size_t) n) == PW_E_NOSPC, "one byte short");
    CHECK(buf[0] == 0, "cleared on overflow");
    CHECK(pw_render_json(&L, buf, (size_t) n + 1) == n, "exact fit");
    CHECK(pw_render_json(&L, buf, 0) == PW_E_ARG, "cap 0");
    CHECK(pw_render_json(NULL, buf, 10) == PW_E_ARG, "null");

    /* no battery, a conflict, a charging battery with INT64_MIN-safe rate */
    pw_init(&L, PW_DEV_DESKTOP, PW_HOST_MACOS, 1, 0, 0);
    pw_meter_add(&L, "a", PW_DOM_SYSTEM, PW_SCOPE_WHOLE, 0, 0);
    pw_meter_add(&L, "b", PW_DOM_BATTERY, PW_SCOPE_WHOLE, 0, 0);
    pw_meter_sample(&L, 0, 0, 0);
    pw_meter_sample(&L, 1, 0, 0);
    pw_meter_sample(&L, 0, 100, 10);
    pw_meter_sample(&L, 1, 50, 10);
    pw_settle(&L, 10);
    n = pw_render_json(&L, buf, sizeof(buf));
    CHECK(n > 0 && strstr(buf, "\"source\":\"conflict\",\"lower_bound\":false,\"other_uj\":50,"),
          "conflict json");
    CHECK(strstr(buf, "\"battery\":null,") != NULL, "no battery");
    memset(&b, 0, sizeof(b));
    b.present = true;
    b.on_mains = true;
    b.charging = true;
    b.rate_mw = INT64_MIN;
    b.temp_dc = INT32_MIN;
    pw_battery_set(&L, &b);
    n = pw_render_json(&L, buf, sizeof(buf));
    CHECK(strstr(buf, "\"rate_mw\":-9223372036854775808,") != NULL, "INT64_MIN");
    CHECK(strstr(buf, "temp_dc") == NULL, "unknown temp left out");
    CHECK(strstr(buf, "\"posture\":\"perform\",\"token_scale_permille\":1000") != NULL,
          "mains posture");
}

/* ---- RAPL through a fake MSR file ------------------------------------- */

typedef struct {
    uint32_t msr[8];
    uint64_t val[8];
    int n;
} fake_msr_t;

static int fake_rd(void *ctx, uint32_t msr, uint64_t *val)
{
    fake_msr_t *f = (fake_msr_t *) ctx;
    for (int i = 0; i < f->n; i++)
        if (f->msr[i] == msr) {
            *val = f->val[i];
            return 0;
        }
    return -1;
}

static void fake_set(fake_msr_t *f, uint32_t msr, uint64_t v)
{
    for (int i = 0; i < f->n; i++)
        if (f->msr[i] == msr) {
            f->val[i] = v;
            return;
        }
    f->msr[f->n] = msr;
    f->val[f->n] = v;
    f->n++;
}

static void t_rapl(void)
{
    CHECK(pw_rapl_to_uj(16384, 14) == 1000000, "one joule at ESU 14");
    CHECK(pw_rapl_to_uj(1, 14) == 61, "one count floors to 61 uJ");
    CHECK(pw_rapl_to_uj(0x100000001ull, 0) == 1000000, "only 32 bits");
    CHECK(pw_rapl_to_uj(5, 64) == 0, "bad esu");
    CHECK(pw_rapl_wrap_uj(14) == 262144000000ull, "wrap at ESU 14");
    CHECK(pw_rapl_wrap_uj(16) == 65536000000ull, "wrap at ESU 16 (AMD)");
    CHECK(pw_rapl_wrap_uj(0) == 4294967296000000ull, "wrap at ESU 0");
    CHECK(pw_rapl_wrap_uj(64) == 0, "bad esu wrap");
    CHECK(pw_rapl_amd_cpuid_ok(1u << 14), "cpuid bit 14");
    CHECK(!pw_rapl_amd_cpuid_ok(~(1u << 14)), "cpuid other bits");

    fake_msr_t f;
    memset(&f, 0, sizeof(f));
    pw_rapl_t r;
    pw_init(&L, PW_DEV_LAPTOP, PW_HOST_BAREMETAL, 4, 0, 0);
    CHECK(pw_rapl_probe(&r, &L, PW_RAPL_INTEL, fake_rd, &f, 0) == PW_E_FAULT, "no unit MSR");
    CHECK(pw_rapl_probe(&r, &L, (pw_rapl_vendor_t) 2, fake_rd, &f, 0) == PW_E_ARG, "vendor");
    CHECK(pw_rapl_probe(&r, &L, PW_RAPL_INTEL, NULL, &f, 0) == PW_E_ARG, "no reader");
    fake_set(&f, PW_MSR_INTEL_UNIT, 0xA0E03u); /* ESU = 0xE = 14 */
    fake_set(&f, PW_MSR_INTEL_PKG, 0);
    fake_set(&f, PW_MSR_INTEL_PP0, 0);
    fake_set(&f, PW_MSR_INTEL_DRAM, 0);
    CHECK(pw_rapl_probe(&r, &L, PW_RAPL_INTEL, fake_rd, &f, 100000) == 3, "three domains");
    CHECK(r.esu == 14 && r.wrap_uj == 262144000000ull, "units");
    CHECK(L.nmeters == 3, "three meters");
    CHECK(strcmp(L.meters[0].name, "rapl-package") == 0 && L.meters[0].scope == PW_SCOPE_PART,
          "package part");
    CHECK(L.meters[1].domain == PW_DOM_DRAM && L.meters[1].scope == PW_SCOPE_PART, "dram part");
    CHECK(L.meters[2].domain == PW_DOM_CORES && L.meters[2].scope == PW_SCOPE_SUB, "cores sub");
    CHECK(L.meters[0].wrap_uj == 262144000000ull && L.meters[0].max_mw == 100000, "meter limits");
    fake_set(&f, PW_MSR_INTEL_PKG, 0xFFFFC000u); /* just below the wrap */
    CHECK(pw_rapl_sample(&r, &L, 0) == 3, "base samples");
    fake_set(&f, PW_MSR_INTEL_PKG, 16384u * 4u - 16384u); /* wrapped: 4 J later */
    fake_set(&f, PW_MSR_INTEL_DRAM, 16384u);
    fake_set(&f, PW_MSR_INTEL_PP0, 16384u * 2u);
    CHECK(pw_rapl_sample(&r, &L, 1000) == 3, "three samples");
    CHECK(L.meters[0].pending_uj == 4000000, "package 4 J across the wrap");
    pw_settle(&L, 1000);
    CHECK(L.last_total_uj == 5000000, "package + dram, cores not added");
    CHECK(L.last_total_mw == 5000, "5 W");
    f.n = 1; /* only the unit MSR reads now */
    CHECK(pw_rapl_sample(&r, &L, 2000) == 0, "unreadable domains skipped");
    CHECK(pw_rapl_sample(NULL, &L, 0) == PW_E_ARG, "null");

    memset(&f, 0, sizeof(f));
    pw_init(&L, PW_DEV_DESKTOP, PW_HOST_BAREMETAL, 16, 0, 0);
    fake_set(&f, PW_MSR_AMD_UNIT, 0x1003u); /* ESU 16 */
    fake_set(&f, PW_MSR_AMD_PKG, 100);
    fake_set(&f, PW_MSR_INTEL_PSYS, 5); /* never asked for on AMD */
    CHECK(pw_rapl_probe(&r, &L, PW_RAPL_AMD, fake_rd, &f, 0) == 1, "amd package");
    CHECK(r.esu == 16 && L.meters[0].domain == PW_DOM_PACKAGE, "amd units");
    memset(&f, 0, sizeof(f));
    pw_init(&L, PW_DEV_LAPTOP, PW_HOST_BAREMETAL, 4, 0, 0);
    fake_set(&f, PW_MSR_INTEL_UNIT, 0xE00u);
    fake_set(&f, PW_MSR_INTEL_PSYS, 0);
    fake_set(&f, PW_MSR_INTEL_PP1, 0);
    CHECK(pw_rapl_probe(&r, &L, PW_RAPL_INTEL, fake_rd, &f, 0) == 2, "psys + pp1");
    CHECK(L.meters[0].scope == PW_SCOPE_WHOLE && L.meters[0].domain == PW_DOM_SYSTEM, "psys whole");
    CHECK(L.meters[1].scope == PW_SCOPE_SUB && L.meters[1].domain == PW_DOM_GPU, "pp1 sub");
    for (int i = 0; i < PW_MAX_METERS - 1; i++)
        pw_meter_add(&L, "f", PW_DOM_OTHER, PW_SCOPE_SUB, 0, 0);
    CHECK(pw_rapl_probe(&r, &L, PW_RAPL_INTEL, fake_rd, &f, 0) == PW_E_FULL, "meters full");
}

int main(void)
{
    t_muldiv();
    t_init_and_meters();
    t_ceiling();
    t_none();
    t_attribution();
    t_conflicts();
    t_gpu_and_direct();
    t_battery();
    t_budgets();
    t_consumers();
    t_top();
    t_names();
    t_json();
    t_rapl();
    printf("test_power: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
