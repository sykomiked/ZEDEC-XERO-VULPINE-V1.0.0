/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxpower.c — the command-line view of the power ledger on a hosted machine:
 * device power and where it came from, the battery, the swarm posture and
 * the apps using the most energy.
 *
 *   zxpower [--interval MS] [--count N] [--top N] [--json] [--root DIR]
 *
 * --root reads a Linux-style /sys and /proc tree under DIR (tests). */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#    define _DEFAULT_SOURCE 1
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#    include <windows.h>
#else
#    include <time.h>
#endif
#include "zxv_power_host.h"

static zxv_power_host_t H;
static pw_ledger_t L;
static char J[1u << 18];

static void zp_sleep(uint64_t ms)
{
#if defined(_WIN32)
    Sleep((DWORD) ms);
#else
    struct timespec ts = {(time_t) (ms / 1000u), (long) (ms % 1000u) * 1000000L};
    nanosleep(&ts, NULL);
#endif
}

static void zp_watts(char *out, size_t cap, uint64_t mw)
{
    snprintf(out, cap, "%llu.%03llu W", (unsigned long long) (mw / 1000u),
             (unsigned long long) (mw % 1000u));
}

static void zp_print(uint32_t top)
{
    char w[32];
    zp_watts(w, sizeof(w), L.last_total_mw);
    printf("ZXV power  %s / %s  %u CPUs  interval %llu ms\n", pw_device_name(L.device),
           pw_host_name(L.host), L.cpu_units,
           (unsigned long long) (L.last_settle_ms - L.interval_start_ms));
    if (L.last_src == PW_SRC_NONE)
        printf("device     not measured on this machine (CPU time below)\n");
    else
        printf("device     %s  %s%s\n", w, pw_source_name(L.last_src),
               L.last_partial ? " (lower bound: metered apps only)" : "");
    if (L.last_src == PW_SRC_CONFLICT) {
        zp_watts(w, sizeof(w),
                 (L.last_settle_ms > L.interval_start_ms)
                     ? L.last_other_uj / (L.last_settle_ms - L.interval_start_ms)
                     : 0);
        printf("           meters disagree; the other reading was %s\n", w);
    }
    const pw_battery_t *b = &L.battery;
    if (b->present) {
        printf("battery    %u.%u%%  %s", b->permille / 10u, b->permille % 10u,
               b->on_mains ? (b->charging ? "charging" : "on mains") : "on battery");
        if (b->rate_mw) {
            zp_watts(w, sizeof(w), (uint64_t) (b->rate_mw < 0 ? -b->rate_mw : b->rate_mw));
            printf("  %s %s", b->rate_mw > 0 ? "draining" : "charging at", w);
        }
        uint64_t m = pw_battery_minutes_to_empty(b);
        if (m)
            printf("  %llu h %llu min left", (unsigned long long) (m / 60u),
                   (unsigned long long) (m % 60u));
        m = pw_battery_minutes_to_full(b);
        if (m)
            printf("  full in %llu h %llu min", (unsigned long long) (m / 60u),
                   (unsigned long long) (m % 60u));
        uint32_t hp = pw_battery_health_permille(b);
        if (hp) printf("  health %u.%u%%", hp / 10u, hp % 10u);
        if (b->cycles) printf("  %u cycles", b->cycles);
        printf("\n");
    } else {
        printf("battery    none\n");
    }
    pw_posture_t p = pw_posture(&L);
    printf("posture    %s (swarm tokens per cycle x %u/1000)\n", pw_posture_name(p),
           pw_posture_token_scale(p));
    if (H.note[0]) printf("note       %s\n", H.note);
    if (H.apps_dropped) printf("note       %u apps left to system: ledger full\n", H.apps_dropped);
    static uint16_t ids[PW_MAX_CONSUMERS];
    uint32_t n = pw_top(&L, -1, false, ids, top);
    printf("\n  %-24s %-6s %12s %8s  %s\n", "NAME", "KIND", "POWER", "CPU %", "SOURCE");
    uint64_t span_ns = (L.last_settle_ms - L.interval_start_ms) * 1000000u;
    for (uint32_t i = 0; i < n; i++) {
        const pw_consumer_t *c = &L.consumers[ids[i]];
        uint64_t cpu = span_ns ? (c->last_cpu_ns * 1000u) / span_ns : 0;
        zp_watts(w, sizeof(w), c->last_mw);
        printf("  %-24s %-6s %12s %4llu.%llu  %s\n", c->name, pw_kind_name(c->kind),
               L.last_src == PW_SRC_NONE ? "-" : w, (unsigned long long) (cpu / 10u),
               (unsigned long long) (cpu % 10u), pw_source_name(c->last_src));
    }
}

int main(int argc, char **argv)
{
    uint64_t interval = 1000;
    unsigned count = 1, top = 15;
    int json = 0;
    const char *root = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--interval") && i + 1 < argc)
            interval = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--count") && i + 1 < argc)
            count = (unsigned) strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--top") && i + 1 < argc)
            top = (unsigned) strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--json"))
            json = 1;
        else if (!strcmp(argv[i], "--root") && i + 1 < argc)
            root = argv[++i];
        else {
            fprintf(stderr, "usage: zxpower [--interval MS] [--count N] [--top N] [--json] "
                            "[--root DIR]\n");
            return 2;
        }
    }
    if (!interval) interval = 1;
    if (!count) count = 1;
    if (top > PW_MAX_CONSUMERS) top = PW_MAX_CONSUMERS;
    if (zph_open(&H, &L, root, zph_now_ms()) != PW_OK) {
        fprintf(stderr, "zxpower: cannot read this machine\n");
        return 1;
    }
    for (unsigned k = 0; k < count; k++) {
        zp_sleep(interval);
        uint64_t now = zph_now_ms();
        zph_sample(&H, now);
        pw_settle(&L, now);
        if (json) {
            if (pw_render_json(&L, J, sizeof(J)) < 0) {
                fprintf(stderr, "zxpower: report too large\n");
                return 1;
            }
            printf("%s\n", J);
        } else {
            if (k) printf("\n");
            zp_print(top);
        }
        fflush(stdout);
    }
    return 0;
}
