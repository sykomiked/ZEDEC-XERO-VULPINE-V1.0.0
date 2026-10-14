/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_power_host.c — see zxv_power_host.h. */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#    define _DEFAULT_SOURCE 1
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#    define _DARWIN_C_SOURCE 1
#endif
#include "zxv_power_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#    include <windows.h>
#    include <tlhelp32.h>
#    include <powrprof.h>
#else
#    include <dirent.h>
#    include <time.h>
#    include <unistd.h>
#endif
#if defined(__APPLE__)
#    include <libproc.h>
#    include <sys/resource.h>
#    include <mach/mach_time.h>
#    include <CoreFoundation/CoreFoundation.h>
#    include <IOKit/IOKitLib.h>
#endif

uint64_t zph_name_id(const char *name)
{
    uint64_t h = 1469598103934665603ull;
    for (; name && *name; name++) {
        h ^= (unsigned char) *name;
        h *= 1099511628211ull;
    }
    return h;
}

uint64_t zph_now_ms(void)
{
#if defined(_WIN32)
    return (uint64_t) GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000u + (uint64_t) ts.tv_nsec / 1000000u;
#endif
}

static void zph_copy(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static int zph_cmp_pid(const void *a, const void *b)
{
    int64_t x = ((const zph_proc_t *) a)->pid, y = ((const zph_proc_t *) b)->pid;
    return (x > y) - (x < y);
}

int zph_app(zxv_power_host_t *h, const char *name)
{
    if (!h || !h->l || !name) return PW_E_ARG;
    uint64_t id = zph_name_id(name);
    int c = pw_consumer_find(h->l, PW_KIND_APP, id);
    if (c >= 0) return c;
    c = pw_consumer_add(h->l, PW_KIND_APP, name, PW_NONE, id);
    if (c == PW_E_FULL) h->apps_dropped++;
    return c;
}

int zph_note_agent(zxv_power_host_t *h, uint64_t agent_id, const char *name, uint64_t cpu_ns,
                   uint64_t gpu_ns)
{
    if (!h || !h->l) return PW_E_ARG;
    int app = zph_app(h, h->self_name);
    if (app < 0) return app;
    int g = pw_consumer_find(h->l, PW_KIND_AGENT, agent_id);
    if (g >= 0 && h->l->consumers[g].parent != (uint16_t) app) g = PW_E_ARG;
    if (g < 0) g = pw_consumer_add(h->l, PW_KIND_AGENT, name, (uint16_t) app, agent_id);
    if (g < 0) return g;
    return pw_note_activity(h->l, (uint16_t) g, cpu_ns, gpu_ns);
}

/* ---- Linux-style readers (live /sys and /proc, or a fixture root) ------- */

#if !defined(_WIN32)
static int zph_read(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == ' ')) buf[--n] = 0;
    return (int) n;
}

static bool zph_read_u64(const char *path, uint64_t *v)
{
    char b[64];
    if (zph_read(path, b, sizeof(b)) <= 0) return false;
    char *end;
    unsigned long long x = strtoull(b, &end, 10);
    if (end == b || b[0] == '-') return false;
    *v = (uint64_t) x;
    return true;
}

static bool zph_read_i64(const char *path, int64_t *v)
{
    char b[64];
    if (zph_read(path, b, sizeof(b)) <= 0) return false;
    char *end;
    long long x = strtoll(b, &end, 10);
    if (end == b) return false;
    *v = (int64_t) x;
    return true;
}

static void zph_path(char *out, const zxv_power_host_t *h, const char *a, const char *b,
                     const char *c)
{
    const char *part[6] = {h->root, a, b ? "/" : "", b ? b : "", c ? "/" : "", c ? c : ""};
    size_t n = 0;
    for (int i = 0; i < 6; i++)
        for (const char *q = part[i]; *q && n + 1 < ZPH_PATH; q++) out[n++] = *q;
    out[n] = 0;
}

static uint32_t zph_linux_cpus(const zxv_power_host_t *h)
{
    char path[ZPH_PATH], buf[8192];
    zph_path(path, h, "/proc/cpuinfo", NULL, NULL);
    uint32_t n = 0;
    if (zph_read(path, buf, sizeof(buf)) > 0)
        for (char *p = buf; (p = strstr(p, "processor")) != NULL; p += 9)
            if (p == buf || p[-1] == '\n') n++;
    if (!n && !h->root[0]) {
        long c = sysconf(_SC_NPROCESSORS_ONLN);
        n = c > 0 ? (uint32_t) c : 1u;
    }
    return n ? n : 1u;
}

static void zph_linux_rapl(zxv_power_host_t *h)
{
    char dir[ZPH_PATH];
    zph_path(dir, h, "/sys/class/powercap", NULL, NULL);
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    bool denied = false;
    while ((e = readdir(d)) != NULL && h->nrapl < ZPH_MAX_RAPL) {
        if (strncmp(e->d_name, "intel-rapl:", 11) != 0) continue;
        char path[ZPH_PATH], name[PW_NAME_LEN - 5];
        zph_path(path, h, "/sys/class/powercap", e->d_name, "name");
        if (zph_read(path, name, sizeof(name)) <= 0) continue;
        pw_domain_t dom;
        pw_scope_t scope;
        if (strncmp(name, "package", 7) == 0)
            dom = PW_DOM_PACKAGE, scope = PW_SCOPE_PART;
        else if (strcmp(name, "psys") == 0)
            dom = PW_DOM_SYSTEM, scope = PW_SCOPE_WHOLE;
        else if (strcmp(name, "dram") == 0)
            dom = PW_DOM_DRAM, scope = PW_SCOPE_PART;
        else if (strcmp(name, "core") == 0)
            dom = PW_DOM_CORES, scope = PW_SCOPE_SUB;
        else if (strcmp(name, "uncore") == 0)
            dom = PW_DOM_GPU, scope = PW_SCOPE_SUB;
        else
            continue;
        uint64_t range = 0, v;
        zph_path(path, h, "/sys/class/powercap", e->d_name, "max_energy_range_uj");
        zph_read_u64(path, &range);
        zph_path(path, h, "/sys/class/powercap", e->d_name, "energy_uj");
        if (!zph_read_u64(path, &v)) {
            denied = true;
            continue;
        }
        char mname[PW_NAME_LEN];
        snprintf(mname, sizeof(mname), "rapl-%s", name);
        int m = pw_meter_add(h->l, mname, dom, scope, range ? range + 1u : 0u, 0);
        if (m < 0) break;
        zph_copy(h->rapl[h->nrapl].energy, ZPH_PATH, path);
        h->rapl[h->nrapl].meter = m;
        h->nrapl++;
    }
    closedir(d);
    if (denied)
        zph_copy(h->note, sizeof(h->note),
                 "RAPL energy counters are root-only on this kernel; run as root to meter them");
}

/* Sums every system battery; returns false when there is none. */
static bool zph_linux_battery(zxv_power_host_t *h, pw_battery_t *b, int64_t *uw)
{
    char dir[ZPH_PATH];
    zph_path(dir, h, "/sys/class/power_supply", NULL, NULL);
    DIR *d = opendir(dir);
    if (!d) return false;
    memset(b, 0, sizeof(*b));
    b->temp_dc = INT32_MIN;
    *uw = 0;
    bool mains = false, any = false, charging = false, discharging = false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[ZPH_PATH], type[32], buf[64];
        zph_path(path, h, "/sys/class/power_supply", e->d_name, "type");
        if (zph_read(path, type, sizeof(type)) <= 0) continue;
        if (strcmp(type, "Mains") == 0 || strcmp(type, "USB") == 0) {
            uint64_t on = 0;
            zph_path(path, h, "/sys/class/power_supply", e->d_name, "online");
            if (zph_read_u64(path, &on) && on) mains = true;
            continue;
        }
        if (strcmp(type, "Battery") != 0) continue;
        zph_path(path, h, "/sys/class/power_supply", e->d_name, "scope");
        if (zph_read(path, buf, sizeof(buf)) > 0 && strcmp(buf, "Device") == 0) continue;
        any = true;
        zph_path(path, h, "/sys/class/power_supply", e->d_name, "status");
        if (zph_read(path, buf, sizeof(buf)) > 0) {
            if (strcmp(buf, "Charging") == 0) charging = true;
            if (strcmp(buf, "Discharging") == 0) discharging = true;
        }
        uint64_t volt = 0, now = 0, full = 0, design = 0, v;
        zph_path(path, h, "/sys/class/power_supply", e->d_name, "voltage_now");
        zph_read_u64(path, &volt);
        zph_path(path, h, "/sys/class/power_supply", e->d_name, "energy_now");
        if (zph_read_u64(path, &now)) { /* uWh */
            zph_path(path, h, "/sys/class/power_supply", e->d_name, "energy_full");
            zph_read_u64(path, &full);
            zph_path(path, h, "/sys/class/power_supply", e->d_name, "energy_full_design");
            zph_read_u64(path, &design);
            now /= 1000u, full /= 1000u, design /= 1000u;
        } else { /* uAh x uV */
            zph_path(path, h, "/sys/class/power_supply", e->d_name, "charge_now");
            if (zph_read_u64(path, &v)) now = pw_muldiv(v, volt, 1000000000u);
            zph_path(path, h, "/sys/class/power_supply", e->d_name, "charge_full");
            if (zph_read_u64(path, &v)) full = pw_muldiv(v, volt, 1000000000u);
            zph_path(path, h, "/sys/class/power_supply", e->d_name, "charge_full_design");
            if (zph_read_u64(path, &v)) design = pw_muldiv(v, volt, 1000000000u);
        }
        b->now_mwh += now;
        b->full_mwh += full;
        b->design_mwh += design;
        int64_t p = 0, cur;
        zph_path(path, h, "/sys/class/power_supply", e->d_name, "power_now");
        if (!zph_read_i64(path, &p)) {
            zph_path(path, h, "/sys/class/power_supply", e->d_name, "current_now");
            if (zph_read_i64(path, &cur))
                p = (int64_t) pw_muldiv((uint64_t) (cur < 0 ? -cur : cur), volt, 1000000u);
        }
        if (p < 0) p = -p; /* some drivers sign it; the status says which way */
        *uw += p;
        zph_path(path, h, "/sys/class/power_supply", e->d_name, "cycle_count");
        if (zph_read_u64(path, &v) && v > b->cycles) b->cycles = (uint32_t) v;
        int64_t t;
        zph_path(path, h, "/sys/class/power_supply", e->d_name, "temp");
        if (zph_read_i64(path, &t)) b->temp_dc = (int32_t) t;
    }
    closedir(d);
    if (!any) return false;
    b->present = true;
    b->charging = charging;
    b->on_mains = mains || (charging && !discharging);
    if (b->on_mains) discharging = false;
    b->permille = b->full_mwh ? (uint16_t) pw_muldiv(b->now_mwh, 1000u, b->full_mwh) : 0;
    b->rate_mw = discharging ? *uw / 1000 : charging ? -(*uw / 1000) : 0;
    if (!discharging) *uw = -1; /* not draining: no battery meter reading */
    return true;
}

static void zph_linux_procs(zxv_power_host_t *h)
{
    char dir[ZPH_PATH];
    zph_path(dir, h, "/proc", NULL, NULL);
    DIR *d = opendir(dir);
    h->ncur = 0;
    if (!d) return;
    long hz = h->root[0] ? 100 : sysconf(_SC_CLK_TCK);
    if (hz <= 0) hz = 100;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && h->ncur < ZPH_MAX_PROCS) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char path[ZPH_PATH], buf[1024];
        zph_path(path, h, "/proc", e->d_name, "stat");
        if (zph_read(path, buf, sizeof(buf)) <= 0) continue;
        char *lp = strchr(buf, '('), *rp = strrchr(buf, ')');
        if (!lp || !rp || rp < lp) continue;
        zph_proc_t *p = &h->cur[h->ncur];
        memset(p, 0, sizeof(*p));
        p->pid = strtoll(buf, NULL, 10);
        size_t n = (size_t) (rp - lp - 1);
        if (n >= PW_NAME_LEN) n = PW_NAME_LEN - 1;
        memcpy(p->name, lp + 1, n);
        /* fields after ')' start at 3 (state); utime 14, stime 15, start 22 */
        uint64_t f[23] = {0};
        int k = 3;
        for (char *s = rp + 1; *s && k <= 22;) {
            while (*s == ' ') s++;
            if (!*s) break;
            f[k++] = strtoull(s, &s, 10);
            while (*s && *s != ' ') s++;
        }
        if (k <= 22) continue;
        p->cpu_ns = pw_muldiv(f[14] + f[15], 1000000000u, (uint64_t) hz);
        p->start = f[22];
        h->ncur++;
    }
    closedir(d);
}
#endif /* !_WIN32 */

/* ---- macOS -------------------------------------------------------------- */

#if defined(__APPLE__)
static int64_t zph_cf_i64(CFDictionaryRef d, const char *key, bool *ok)
{
    CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    CFTypeRef v = CFDictionaryGetValue(d, k);
    CFRelease(k);
    int64_t x = 0;
    *ok = false;
    if (v && CFGetTypeID(v) == CFNumberGetTypeID())
        *ok = CFNumberGetValue((CFNumberRef) v, kCFNumberSInt64Type, &x);
    else if (v && CFGetTypeID(v) == CFBooleanGetTypeID()) {
        x = CFBooleanGetValue((CFBooleanRef) v) ? 1 : 0;
        *ok = true;
    }
    return x;
}

static bool zph_mac_battery(pw_battery_t *b, int64_t *uw)
{
    memset(b, 0, sizeof(*b));
    b->temp_dc = INT32_MIN;
    *uw = -1;
    io_service_t svc =
        IOServiceGetMatchingService(MACH_PORT_NULL, IOServiceMatching("AppleSmartBattery"));
    if (!svc) return false;
    CFMutableDictionaryRef d = NULL;
    if (IORegistryEntryCreateCFProperties(svc, &d, kCFAllocatorDefault, 0) != KERN_SUCCESS || !d) {
        IOObjectRelease(svc);
        return false;
    }
    bool ok, ok2;
    int64_t mv = zph_cf_i64(d, "Voltage", &ok), ma = zph_cf_i64(d, "Amperage", &ok2);
    if (!ok) mv = 0;
    if (!ok2) ma = 0;
    int64_t now = zph_cf_i64(d, "AppleRawCurrentCapacity", &ok);
    if (!ok) now = zph_cf_i64(d, "CurrentCapacity", &ok);
    int64_t full = zph_cf_i64(d, "AppleRawMaxCapacity", &ok);
    if (!ok) full = zph_cf_i64(d, "MaxCapacity", &ok);
    int64_t design = zph_cf_i64(d, "DesignCapacity", &ok);
    if (!ok) design = 0;
    int64_t cycles = zph_cf_i64(d, "CycleCount", &ok);
    if (!ok) cycles = 0;
    int64_t temp = zph_cf_i64(d, "Temperature", &ok); /* hundredths of a degree */
    if (ok) b->temp_dc = (int32_t) (temp / 10);
    int64_t ext = zph_cf_i64(d, "ExternalConnected", &ok);
    int64_t chg = zph_cf_i64(d, "IsCharging", &ok2);
    CFRelease(d);
    IOObjectRelease(svc);
    if (mv <= 0) return false;
    b->present = true;
    b->on_mains = ext != 0;
    b->charging = chg != 0;
    b->now_mwh = (uint64_t) (now > 0 ? now : 0) * (uint64_t) mv / 1000u;
    b->full_mwh = (uint64_t) (full > 0 ? full : 0) * (uint64_t) mv / 1000u;
    b->design_mwh = (uint64_t) (design > 0 ? design : 0) * (uint64_t) mv / 1000u;
    b->cycles = (uint32_t) (cycles > 0 ? cycles : 0);
    b->permille = b->full_mwh ? (uint16_t) pw_muldiv(b->now_mwh, 1000u, b->full_mwh) : 0;
    b->rate_mw = -(ma * mv) / 1000; /* amperage is negative while discharging */
    if (!b->on_mains && ma < 0) *uw = (-ma) * mv;
    return true;
}

static void zph_mac_procs(zxv_power_host_t *h)
{
    static pid_t pids[ZPH_MAX_PROCS];
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    h->ncur = 0;
    int n = proc_listallpids(pids, (int) sizeof(pids));
    for (int i = 0; i < n && h->ncur < ZPH_MAX_PROCS; i++) {
        zph_proc_t *p = &h->cur[h->ncur];
        memset(p, 0, sizeof(*p));
#    if defined(RUSAGE_INFO_V6)
        struct rusage_info_v6 ri;
        if (proc_pid_rusage(pids[i], RUSAGE_INFO_V6, (rusage_info_t *) &ri) != 0) continue;
        p->nj = ri.ri_energy_nj;
        p->has_nj = true;
#    else
        struct rusage_info_v2 ri;
        if (proc_pid_rusage(pids[i], RUSAGE_INFO_V2, (rusage_info_t *) &ri) != 0) continue;
#    endif
        p->pid = pids[i];
        p->start = ri.ri_proc_start_abstime;
        p->cpu_ns = pw_muldiv(ri.ri_user_time + ri.ri_system_time, tb.numer, tb.denom);
        if (proc_name(pids[i], p->name, sizeof(p->name)) <= 0) zph_copy(p->name, PW_NAME_LEN, "?");
        h->ncur++;
    }
}
#endif /* __APPLE__ */

/* ---- Windows ------------------------------------------------------------ */

#if defined(_WIN32)
static bool zph_win_battery(pw_battery_t *b, int64_t *uw)
{
    memset(b, 0, sizeof(*b));
    b->temp_dc = INT32_MIN;
    *uw = -1;
    SYSTEM_BATTERY_STATE s;
    memset(&s, 0, sizeof(s));
    if (CallNtPowerInformation(SystemBatteryState, NULL, 0, &s, sizeof(s)) != 0) return false;
    if (!s.BatteryPresent) return false;
    b->present = true;
    b->on_mains = s.AcOnLine != 0;
    b->charging = s.Charging != 0;
    b->now_mwh = s.RemainingCapacity;
    b->full_mwh = s.MaxCapacity; /* design capacity needs the battery IOCTL: left 0 */
    b->permille =
        s.MaxCapacity ? (uint16_t) pw_muldiv(s.RemainingCapacity, 1000u, s.MaxCapacity) : 0;
    int32_t rate = (int32_t) s.Rate; /* mW, negative while discharging */
    b->rate_mw = -(int64_t) rate;
    if (s.Discharging && !s.AcOnLine && rate < 0) *uw = (int64_t) (-rate) * 1000;
    return true;
}

static void zph_win_procs(zxv_power_host_t *h)
{
    h->ncur = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32First(snap, &pe); ok && h->ncur < ZPH_MAX_PROCS;
         ok = Process32Next(snap, &pe)) {
        HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!ph) continue;
        FILETIME c, x, k, u;
        if (GetProcessTimes(ph, &c, &x, &k, &u)) {
            zph_proc_t *p = &h->cur[h->ncur];
            memset(p, 0, sizeof(*p));
            p->pid = pe.th32ProcessID;
            p->start = ((uint64_t) c.dwHighDateTime << 32) | c.dwLowDateTime;
            uint64_t kk = ((uint64_t) k.dwHighDateTime << 32) | k.dwLowDateTime;
            uint64_t uu = ((uint64_t) u.dwHighDateTime << 32) | u.dwLowDateTime;
            p->cpu_ns = (kk + uu) * 100u;
            zph_copy(p->name, PW_NAME_LEN, pe.szExeFile);
            h->ncur++;
        }
        CloseHandle(ph);
    }
    CloseHandle(snap);
}
#endif /* _WIN32 */

/* ---- common ------------------------------------------------------------- */

static void zph_procs_settle(zxv_power_host_t *h)
{
    qsort(h->cur, h->ncur, sizeof(h->cur[0]), zph_cmp_pid);
    if (h->procs_primed) {
        for (uint32_t i = 0; i < h->ncur; i++) {
            const zph_proc_t *c = &h->cur[i];
            const zph_proc_t *p = bsearch(c, h->prev, h->nprev, sizeof(h->prev[0]), zph_cmp_pid);
            uint64_t dc = c->cpu_ns, dn = c->nj; /* a new process: all of it is new */
            if (p && p->start == c->start) {
                dc = c->cpu_ns >= p->cpu_ns ? c->cpu_ns - p->cpu_ns : 0;
                dn = c->nj >= p->nj ? c->nj - p->nj : 0;
            }
            if (!dc && !dn) continue;
            int app = zph_app(h, c->name);
            if (app < 0) continue;
            pw_note_activity(h->l, (uint16_t) app, dc, 0);
            if (c->has_nj) pw_note_energy(h->l, (uint16_t) app, dn / 1000u);
        }
    }
    memcpy(h->prev, h->cur, sizeof(h->cur[0]) * h->ncur);
    h->nprev = h->ncur;
    h->procs_primed = true;
}

static void zph_battery_meter(zxv_power_host_t *h, const pw_battery_t *b, int64_t uw,
                              uint64_t now_ms)
{
    pw_battery_set(h->l, b);
    if (!b->present) return;
    if (h->batt_meter < 0) {
        h->batt_meter = pw_meter_add(h->l, "battery-drain", PW_DOM_BATTERY, PW_SCOPE_WHOLE, 0, 0);
        if (h->batt_meter < 0) return;
    }
    if (uw < 0) { /* not draining: the meter rests, the next drain starts fresh */
        h->batt_have = false;
        return;
    }
    if (h->batt_have && now_ms > h->batt_last_ms) {
        /* trapezoid over the interval: uW * ms / 1000 = uJ */
        uint64_t avg = ((uint64_t) h->batt_last_uw + (uint64_t) uw) / 2u;
        h->batt_acc_uj += pw_muldiv(avg, now_ms - h->batt_last_ms, 1000u);
    } else {
        /* the meter's first reading after a rest is its new base */
        h->l->meters[h->batt_meter].have_last = false;
    }
    pw_meter_sample(h->l, (uint32_t) h->batt_meter, h->batt_acc_uj, now_ms);
    h->batt_have = true;
    h->batt_last_ms = now_ms;
    h->batt_last_uw = uw;
}

int zph_open(zxv_power_host_t *h, pw_ledger_t *l, const char *root, uint64_t now_ms)
{
    if (!h || !l) return PW_E_ARG;
    memset(h, 0, sizeof(*h));
    h->l = l;
    h->batt_meter = -1;
    if (root && root[0]) zph_copy(h->root, sizeof(h->root), root);
    pw_host_t host = PW_HOST_LINUX;
    uint32_t cpus = 1;
#if defined(_WIN32)
    host = PW_HOST_WINDOWS;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    cpus = si.dwNumberOfProcessors;
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
    const char *base = exe;
    for (DWORD i = 0; i < n; i++)
        if (exe[i] == '\\' || exe[i] == '/') base = exe + i + 1;
    zph_copy(h->self_name, PW_NAME_LEN, n ? base : "zxvd");
#else
    h->linux_paths = h->root[0] != 0;
#    if defined(__linux__)
    h->linux_paths = true;
#    elif defined(__APPLE__)
    if (!h->root[0]) host = PW_HOST_MACOS;
#    endif
    if (h->linux_paths) {
        cpus = zph_linux_cpus(h);
    } else {
        long c = sysconf(_SC_NPROCESSORS_ONLN);
        cpus = c > 0 ? (uint32_t) c : 1u;
    }
    char self[ZPH_PATH], name[64];
    zph_path(self, h, "/proc/self/comm", NULL, NULL);
    if (h->linux_paths && zph_read(self, name, sizeof(name)) > 0)
        zph_copy(h->self_name, PW_NAME_LEN, name);
#    if defined(__APPLE__)
    else
        zph_copy(h->self_name, PW_NAME_LEN, getprogname());
#    endif
    if (!h->self_name[0]) zph_copy(h->self_name, PW_NAME_LEN, "zxvd");
#endif
    /* a battery makes it a laptop; the phone build sets MOBILE itself */
    pw_battery_t b;
    int64_t uw = -1;
    bool has_batt;
#if defined(_WIN32)
    has_batt = zph_win_battery(&b, &uw);
#elif defined(__APPLE__)
    has_batt = h->linux_paths ? zph_linux_battery(h, &b, &uw) : zph_mac_battery(&b, &uw);
#else
    has_batt = zph_linux_battery(h, &b, &uw);
#endif
    pw_init(l, has_batt ? PW_DEV_LAPTOP : PW_DEV_DESKTOP, host, cpus, 0, now_ms);
#if !defined(_WIN32)
    if (h->linux_paths) zph_linux_rapl(h);
#endif
    return zph_sample(h, now_ms);
}

int zph_sample(zxv_power_host_t *h, uint64_t now_ms)
{
    if (!h || !h->l) return PW_E_ARG;
    pw_battery_t b;
    int64_t uw = -1;
    bool has_batt;
#if defined(_WIN32)
    has_batt = zph_win_battery(&b, &uw);
    zph_win_procs(h);
#else
    if (h->linux_paths) {
        has_batt = zph_linux_battery(h, &b, &uw);
        for (uint32_t i = 0; i < h->nrapl; i++) {
            uint64_t v;
            if (zph_read_u64(h->rapl[i].energy, &v))
                pw_meter_sample(h->l, (uint32_t) h->rapl[i].meter, v, now_ms);
        }
        zph_linux_procs(h);
    } else {
#    if defined(__APPLE__)
        has_batt = zph_mac_battery(&b, &uw);
        zph_mac_procs(h);
#    else
        has_batt = false;
        h->ncur = 0;
#    endif
    }
#endif
    if (!has_batt) {
        memset(&b, 0, sizeof(b));
        b.temp_dc = INT32_MIN;
        uw = -1;
    }
    zph_battery_meter(h, &b, uw, now_ms);
    zph_procs_settle(h);
    return PW_OK;
}
