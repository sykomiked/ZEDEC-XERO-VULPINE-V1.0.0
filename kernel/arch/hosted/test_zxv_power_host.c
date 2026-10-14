/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_power_host.c — the Linux readers of zxv_power_host.c against a
 * fixture /sys and /proc tree: RAPL zones (root-only zones skipped with a
 * note, mmio duplicates and unknown zones ignored), two batteries in both
 * sysfs styles plus a peripheral battery that must be ignored, the battery
 * drain as a meter, per-app CPU time with names holding spaces and ')', a
 * new process and a reused pid, an agent under this process's app, and the
 * switch to mains. Runs on any POSIX host. */
#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "zxv_power_host.h"

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

static char R[256];
static zxv_power_host_t H;
static pw_ledger_t L;

static void mk(const char *rel)
{
    char p[512], *s;
    snprintf(p, sizeof(p), "%s/%s", R, rel);
    for (s = p + strlen(R) + 1; (s = strchr(s, '/')) != NULL; s++) {
        *s = 0;
        mkdir(p, 0700);
        *s = '/';
    }
    mkdir(p, 0700);
}

static void put(const char *rel, const char *text)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", R, rel);
    char d[512];
    snprintf(d, sizeof(d), "%s", rel);
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

static void stat_file(int pid, const char *name, unsigned utime, unsigned stime, unsigned start)
{
    char rel[64], body[512];
    snprintf(rel, sizeof(rel), "proc/%d/stat", pid);
    snprintf(body, sizeof(body),
             "%d (%s) S 1 1 1 0 -1 4194560 100 0 0 0 %u %u 0 0 20 0 1 0 %u 1000 100", pid, name,
             utime, stime, start);
    put(rel, body);
}

static int app_named(const char *name)
{
    return pw_consumer_find(&L, PW_KIND_APP, zph_name_id(name));
}

int main(void)
{
    snprintf(R, sizeof(R), "%s/zxv_power_fixture_XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(R)) return 2;

    put("proc/cpuinfo", "processor\t: 0\nmodel\t: x\n\nprocessor\t: 1\n\nprocessor\t: 2\n\n"
                        "processor\t: 3\nflags\t: notprocessor");
    put("proc/self/comm", "zxvd");
    /* RAPL */
    put("sys/class/powercap/intel-rapl:0/name", "package-0");
    put("sys/class/powercap/intel-rapl:0/energy_uj", "1000000");
    put("sys/class/powercap/intel-rapl:0/max_energy_range_uj", "262143328850");
    put("sys/class/powercap/intel-rapl:0:0/name", "core");
    put("sys/class/powercap/intel-rapl:0:0/energy_uj", "0");
    put("sys/class/powercap/intel-rapl:1/name", "psys");
    put("sys/class/powercap/intel-rapl:1/energy_uj", "5000000");
    put("sys/class/powercap/intel-rapl:2/name", "dram"); /* no energy_uj: root-only */
    put("sys/class/powercap/intel-rapl:3/name", "mystery");
    put("sys/class/powercap/intel-rapl:3/energy_uj", "1");
    put("sys/class/powercap/intel-rapl-mmio:0/name", "package-0"); /* duplicate, ignored */
    put("sys/class/powercap/intel-rapl-mmio:0/energy_uj", "1");
    /* power supplies */
    put("sys/class/power_supply/AC/type", "Mains");
    put("sys/class/power_supply/AC/online", "0");
    put("sys/class/power_supply/BAT0/type", "Battery");
    put("sys/class/power_supply/BAT0/status", "Discharging");
    put("sys/class/power_supply/BAT0/energy_now", "30000000");
    put("sys/class/power_supply/BAT0/energy_full", "45000000");
    put("sys/class/power_supply/BAT0/energy_full_design", "50000000");
    put("sys/class/power_supply/BAT0/power_now", "15000000");
    put("sys/class/power_supply/BAT0/voltage_now", "12000000");
    put("sys/class/power_supply/BAT0/cycle_count", "300");
    put("sys/class/power_supply/BAT0/temp", "312");
    put("sys/class/power_supply/BAT1/type", "Battery");
    put("sys/class/power_supply/BAT1/status", "Discharging");
    put("sys/class/power_supply/BAT1/charge_now", "1000000");
    put("sys/class/power_supply/BAT1/charge_full", "2000000");
    put("sys/class/power_supply/BAT1/charge_full_design", "2000000");
    put("sys/class/power_supply/BAT1/current_now", "-500000");
    put("sys/class/power_supply/BAT1/voltage_now", "10000000");
    put("sys/class/power_supply/BAT1/cycle_count", "120");
    put("sys/class/power_supply/hid-mouse/type", "Battery");
    put("sys/class/power_supply/hid-mouse/scope", "Device");
    put("sys/class/power_supply/hid-mouse/energy_now", "999999999");
    /* processes: hz is 100 under a fixture root */
    stat_file(1, "init", 100, 0, 5);
    stat_file(42, "my app) (x", 50, 50, 900);
    stat_file(99, "zxvd", 0, 0, 1000);
    put("proc/7/stat", "7 (broken");
    put("proc/8/stat", "8 (short) S 1 2");
    mk("proc/not-a-pid");

    CHECK(zph_open(&H, &L, R, 0) == PW_OK, "open");
    CHECK(H.linux_paths, "linux readers");
    CHECK(L.cpu_units == 4, "four cpus, 'notprocessor' not counted");
    CHECK(L.device == PW_DEV_LAPTOP, "battery makes a laptop");
    CHECK(L.host == PW_HOST_LINUX, "host");
    CHECK(strcmp(H.self_name, "zxvd") == 0, "self name");
    CHECK(H.nrapl == 3, "package, core, psys");
    CHECK(strstr(H.note, "root") != NULL, "dram needs root: noted");
    int pkg = -1, core = -1, psys = -1;
    for (uint32_t i = 0; i < L.nmeters; i++) {
        if (!strcmp(L.meters[i].name, "rapl-package-0")) pkg = (int) i;
        if (!strcmp(L.meters[i].name, "rapl-core")) core = (int) i;
        if (!strcmp(L.meters[i].name, "rapl-psys")) psys = (int) i;
    }
    CHECK(pkg >= 0 && core >= 0 && psys >= 0, "meters named");
    CHECK(L.meters[pkg].scope == PW_SCOPE_PART && L.meters[pkg].wrap_uj == 262143328851ull,
          "package part, wrap = range + 1");
    CHECK(L.meters[core].scope == PW_SCOPE_SUB, "core sub");
    CHECK(L.meters[psys].scope == PW_SCOPE_WHOLE && L.meters[psys].wrap_uj == 0, "psys whole");
    CHECK(H.batt_meter >= 0 && L.meters[H.batt_meter].scope == PW_SCOPE_WHOLE, "battery meter");
    CHECK(L.battery.present && !L.battery.on_mains && !L.battery.charging, "on battery");
    CHECK(L.battery.now_mwh == 30000 + 10000, "energy + charge styles add");
    CHECK(L.battery.full_mwh == 45000 + 20000 && L.battery.design_mwh == 50000 + 20000, "full");
    CHECK(L.battery.rate_mw == 15000 + 5000, "power_now + current x voltage");
    CHECK(L.battery.cycles == 300, "highest cycle count");
    CHECK(L.battery.temp_dc == 312, "temp");
    CHECK(L.battery.permille == 615, "40000 / 65000");
    CHECK(H.ncur == 3 && H.procs_primed, "three good processes");

    /* one second later */
    put("sys/class/powercap/intel-rapl:0/energy_uj", "15000000");
    put("sys/class/powercap/intel-rapl:0:0/energy_uj", "9000000");
    put("sys/class/powercap/intel-rapl:1/energy_uj", "25000000");
    stat_file(1, "init", 110, 0, 5);            /* 100 ms */
    stat_file(42, "my app) (x", 100, 100, 900); /* 1 s */
    stat_file(99, "zxvd", 150, 50, 1000);       /* 2 s */
    stat_file(77, "newcomer", 30, 20, 2000);    /* new: all 500 ms */
    CHECK(zph_sample(&H, 1000) == PW_OK, "sample");
    CHECK(zph_note_agent(&H, 7, "qwen", 500000000u, 0) == PW_OK, "agent noted");
    CHECK(zph_note_agent(&H, 7, "qwen", 0, 0) == PW_OK, "same agent again");
    CHECK(pw_settle(&L, 1000) == PW_OK, "settle");
    CHECK(L.last_src == PW_SRC_METERED, "psys and battery agree");
    CHECK(L.last_total_uj == 20000000, "20 J");
    CHECK(L.last_total_mw == 20000, "20 W");
    int my = app_named("my app) (x"), zx = app_named("zxvd"), in = app_named("init"),
        nw = app_named("newcomer");
    CHECK(my > 0 && zx > 0 && in > 0 && nw > 0, "apps by name");
    CHECK(L.consumers[my].last_uj == 5000000, "1 s of 4 cpus = quarter");
    CHECK(L.consumers[in].last_uj == 500000, "100 ms");
    CHECK(L.consumers[nw].last_uj == 2500000, "new process counts in full");
    int ag = pw_consumer_find(&L, PW_KIND_AGENT, 7);
    CHECK(ag > 0 && L.consumers[ag].parent == zx, "agent under zxvd");
    CHECK(L.consumers[ag].last_uj == 2500000, "agent quarter of zxvd's 10 J");
    CHECK(L.consumers[zx].last_uj == 7500000, "zxvd keeps the rest");
    CHECK(L.consumers[0].last_uj == 2000000, "idle to system");
    CHECK(L.meters[core].total_uj == 9000000, "core sub kept");

    /* pid 42 reused by another program; the charger goes in */
    stat_file(42, "other", 10, 0, 5000);
    put("sys/class/power_supply/AC/online", "1");
    put("sys/class/power_supply/BAT0/status", "Charging");
    put("sys/class/power_supply/BAT1/status", "Charging");
    put("sys/class/powercap/intel-rapl:1/energy_uj", "35000000");
    put("sys/class/powercap/intel-rapl:0/energy_uj", "20000000");
    zph_sample(&H, 2000);
    pw_settle(&L, 2000);
    CHECK(L.battery.on_mains && L.battery.charging, "charging");
    CHECK(L.battery.rate_mw == -20000, "charge rate negative");
    CHECK(pw_posture(&L) == PW_POSTURE_PERFORM, "mains posture");
    CHECK(L.last_total_uj == 10000000, "psys only, battery rests");
    CHECK(L.consumers[app_named("other")].last_uj == 250000, "reused pid counted afresh");
    CHECK(L.consumers[my].last_uj == 0, "old program gone");

    /* unplugged again: the battery meter restarts from a fresh base */
    put("sys/class/power_supply/AC/online", "0");
    put("sys/class/power_supply/BAT0/status", "Discharging");
    put("sys/class/power_supply/BAT1/status", "Discharging");
    zph_sample(&H, 3000);
    put("sys/class/powercap/intel-rapl:1/energy_uj", "55000000");
    zph_sample(&H, 4000);
    pw_settle(&L, 4000);
    CHECK(L.meters[H.batt_meter].total_uj == 40000000, "two discharging seconds in all");
    CHECK(L.last_total_uj == 20000000, "psys and battery for the last second");
    CHECK(L.last_src == PW_SRC_METERED, "still agree");

    /* the plausibility of names and ids */
    CHECK(zph_name_id("a") != zph_name_id("b"), "ids differ");
    CHECK(zph_name_id("") == 1469598103934665603ull, "fnv basis");
    CHECK(zph_app(NULL, "x") == PW_E_ARG, "null");
    CHECK(zph_note_agent(NULL, 1, "x", 1, 1) == PW_E_ARG, "null agent");
    CHECK(zph_sample(NULL, 0) == PW_E_ARG, "null sample");
    CHECK(zph_open(NULL, &L, R, 0) == PW_E_ARG, "null open");
    CHECK(zph_now_ms() > 0 || zph_now_ms() == 0, "clock reads");

    /* a desktop: no battery at all */
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s/sys/class/power_supply'", R);
    if (system(cmd) != 0) return 2;
    zph_open(&H, &L, R, 0);
    CHECK(L.device == PW_DEV_DESKTOP && !L.battery.present, "desktop");
    CHECK(H.batt_meter == -1, "no battery meter");

    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", R);
    if (system(cmd) != 0) return 2;
    printf("test_zxv_power_host: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
