/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_thermal_host.c — see zxv_thermal_host.h (T1-T7). */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L /* opendir, readdir */
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#    define _DARWIN_C_SOURCE 1
#endif
#include "zxv_thermal_host.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#    include <dirent.h>
#endif
#if defined(__APPLE__)
#    include <CoreFoundation/CoreFoundation.h>
#    include <IOKit/IOKitLib.h>
#    include <mach/mach.h>
#endif

/* ---- T2: classes ----------------------------------------------------------- */

static const char *const CLASS_NAMES[ZTH_NCLASS] = {"cpu", "gpu", "case", "other"};

const char *zth_class_name(zth_class_t c)
{
    return (unsigned) c < ZTH_NCLASS ? CLASS_NAMES[c] : "other";
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
}

/* needle (lower case) anywhere in hay, ignoring case */
static bool has_word(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (const char *h = hay; *h; h++) {
        size_t i = 0;
        while (i < n && h[i] && lower(h[i]) == needle[i]) i++;
        if (i == n) return true;
    }
    return false;
}

static bool starts(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

typedef struct {
    const char *text;
    bool prefix; /* false: the whole name */
    zth_class_t cls;
} zth_rule_t;

static const zth_rule_t DRIVERS[] = {
    {"coretemp", false, ZTH_CPU}, {"k10temp", false, ZTH_CPU},      {"k8temp", false, ZTH_CPU},
    {"zenpower", false, ZTH_CPU}, {"x86_pkg_temp", false, ZTH_CPU}, {"via_cputemp", false, ZTH_CPU},
    {"cpu", true, ZTH_CPU},       {"soc", true, ZTH_CPU},           {"tcpu", false, ZTH_CPU},
    {"amdgpu", false, ZTH_GPU},   {"nouveau", false, ZTH_GPU},      {"radeon", false, ZTH_GPU},
    {"i915", false, ZTH_GPU},     {"xe", false, ZTH_GPU},           {"gpu", true, ZTH_GPU},
    {"acpitz", false, ZTH_CASE},  {"nct", true, ZTH_CASE},          {"it8", true, ZTH_CASE},
    {"w83", true, ZTH_CASE},      {"f71", true, ZTH_CASE},          {"pch_", true, ZTH_CASE},
    {"board", true, ZTH_CASE},    {"dell_smm", false, ZTH_CASE},    {"thinkpad", false, ZTH_CASE},
    {"asus", true, ZTH_CASE},
};

/* in order: a CPU word first, then the GPU, then the board */
static const zth_rule_t LABELS[] = {
    {"cpu", true, ZTH_CPU},    {"package", true, ZTH_CPU},  {"core", true, ZTH_CPU},
    {"tctl", true, ZTH_CPU},   {"tdie", true, ZTH_CPU},     {"tccd", true, ZTH_CPU},
    {"pacc", true, ZTH_CPU},   {"eacc", true, ZTH_CPU},     {"soc", true, ZTH_CPU},
    {"gpu", true, ZTH_GPU},    {"systin", true, ZTH_CASE},  {"auxtin", true, ZTH_CASE},
    {"board", true, ZTH_CASE}, {"ambient", true, ZTH_CASE}, {"case", true, ZTH_CASE},
    {"pch", true, ZTH_CASE},   {"chipset", true, ZTH_CASE},
};

zth_class_t zth_classify(const char *driver, const char *label)
{
    if (label)
        for (size_t i = 0; i < sizeof LABELS / sizeof LABELS[0]; i++)
            if (has_word(label, LABELS[i].text)) return LABELS[i].cls;
    if (driver)
        for (size_t i = 0; i < sizeof DRIVERS / sizeof DRIVERS[0]; i++) {
            const zth_rule_t *r = &DRIVERS[i];
            if (r->prefix ? starts(driver, r->text) : !strcmp(driver, r->text)) return r->cls;
        }
    return ZTH_OTHER;
}

/* ---- T3: values ------------------------------------------------------------ */

bool zth_parse_mdeg(const char *buf, size_t n, int32_t *mdeg)
{
    if (n && buf[n - 1] == '\n') n--;
    size_t i = 0;
    bool neg = false;
    if (i < n && buf[i] == '-') {
        neg = true;
        i++;
    }
    size_t digits = n - i;
    if (digits < 1 || digits > 9) return false;
    int32_t v = 0;
    for (; i < n; i++) {
        if (buf[i] < '0' || buf[i] > '9') return false;
        v = v * 10 + (buf[i] - '0');
    }
    *mdeg = neg ? -v : v;
    return true;
}

bool zth_in_range(int32_t mdeg)
{
    return mdeg >= ZTH_MIN_MDEG && mdeg <= ZTH_MAX_MDEG;
}

static void copy_text(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1u;
    memcpy(dst, src, n);
    dst[n] = 0;
}

#if !defined(_WIN32) /* the readers; Windows has none (T1) */
/* T3: printable ASCII, cut at ZTH_NAME_MAX - 1; any other byte drops it all */
static void keep_name(char *dst, const char *src, size_t n)
{
    while (n && (src[n - 1] == '\n' || src[n - 1] == ' ')) n--;
    if (n > ZTH_NAME_MAX - 1u) n = ZTH_NAME_MAX - 1u;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char) src[i] < 0x20u || (unsigned char) src[i] > 0x7eu) n = 0;
    memcpy(dst, src, n);
    dst[n] = 0;
}

/* ---- the report ------------------------------------------------------------- */

static const char *const WHY_LONG = "too long";
static const char *const WHY_GARBLED = "not an integer";
static const char *const WHY_RANGE = "out of range";
static const char *const WHY_UNREADABLE = "unreadable";

/* T3/T4: one reading; why is NULL when the value is to be judged */
static void add_reading(zth_report_t *rep, zth_source_t src, const char *driver, const char *label,
                        const char *path, int32_t mdeg, const char *why)
{
    if (rep->n >= ZTH_MAX_READINGS) {
        rep->dropped++;
        return;
    }
    zth_reading_t *r = &rep->r[rep->n++]; /* every field is set below */
    r->src = src;
    copy_text(r->driver, sizeof r->driver, driver);
    copy_text(r->label, sizeof r->label, label);
    copy_text(r->path, sizeof r->path, path);
    r->cls = zth_classify(r->driver, r->label);
    if (!why && !zth_in_range(mdeg)) why = WHY_RANGE;
    r->reason = why;
    r->state = why ? LPRES_STATE_FALSE : LPRES_STATE_TRUE;
    r->mdeg = why ? 0 : mdeg;
}
#endif /* !_WIN32 */

/* T6: paths compare with digit runs as numbers (hwmon2 before hwmon10) */
static int natural_cmp(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9') {
            while (*a == '0') a++;
            while (*b == '0') b++;
            size_t na = 0, nb = 0;
            while (a[na] >= '0' && a[na] <= '9') na++;
            while (b[nb] >= '0' && b[nb] <= '9') nb++;
            if (na != nb) return (int) na - (int) nb;
            int c = strncmp(a, b, na);
            if (c) return c;
            a += na;
            b += nb;
            continue;
        }
        if (*a != *b) break;
        a++;
        b++;
    }
    return (unsigned char) *a - (unsigned char) *b;
}

static int reading_cmp(const void *x, const void *y)
{
    const char *a = ((const zth_reading_t *) x)->path, *b = ((const zth_reading_t *) y)->path;
    int c = natural_cmp(a, b);
    return c ? c : strcmp(a, b); /* hwmon01 and hwmon1: still one order */
}

/* T4/T6: sort, then the verdicts and the per-class figures, on a report
 * zth_read zeroed (NEITHER is 0) */
static void finish(zth_report_t *rep)
{
    qsort(rep->r, rep->n, sizeof rep->r[0], reading_cmp);
    for (uint32_t i = 0; i < rep->n; i++) {
        const zth_reading_t *r = &rep->r[i];
        zth_class_sum_t *s = &rep->cls[r->cls];
        rep->state = lpres_kjoin(rep->state, r->state);
        s->state = lpres_kjoin(s->state, r->state);
        if (r->state != LPRES_STATE_TRUE) {
            rep->rejected++;
            continue;
        }
        if (!s->n || r->mdeg > s->max_mdeg) s->max_mdeg = r->mdeg;
        s->n++;
    }
}

/* ---- T1: Linux sysfs (the live system or a fixture root) -------------------- */

#if !defined(_WIN32)
/* "<prefix><1..9 digits><suffix>" exactly */
static bool numbered(const char *name, const char *prefix, const char *suffix)
{
    size_t p = strlen(prefix), s = strlen(suffix), n = strlen(name);
    if (n <= p + s || n - p - s > 9u || strncmp(name, prefix, p) || strcmp(name + n - s, suffix))
        return false;
    for (size_t i = p; i < n - s; i++)
        if (name[i] < '0' || name[i] > '9') return false;
    return true;
}

/* a, b, c and d one after the other (NULL ends early); false when they do
 * not fit in cap with the NUL */
static bool join(char *out, size_t cap, const char *a, const char *b, const char *c, const char *d)
{
    const char *part[4] = {a, b, c, d};
    size_t n = 0;
    for (int i = 0; i < 4 && part[i]; i++)
        for (const char *q = part[i]; *q; q++) {
            if (n + 1u >= cap) return false;
            out[n++] = *q;
        }
    out[n] = 0;
    return true;
}

/* the root joined with a path under it; false when it does not fit */
static bool full_path(char *out, size_t cap, const char *root, const char *rel)
{
    return join(out, cap, root, rel, NULL, NULL);
}

/* up to cap - 1 bytes; -1 when the file cannot be opened (errno kept), -2 on
 * a read error */
static int read_file(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap - 1u, f);
    int bad = ferror(f);
    fclose(f);
    if (bad) return -2;
    buf[n] = 0;
    return (int) n;
}

static void read_name(const char *root, const char *rel, char *out)
{
    char path[ZTH_ROOT_MAX + ZTH_PATH_MAX], buf[2u * ZTH_NAME_MAX];
    out[0] = 0;
    if (!full_path(path, sizeof path, root, rel)) return;
    int n = read_file(path, buf, sizeof buf);
    if (n > 0) keep_name(out, buf, (size_t) n);
}

/* T3: one value file; a file that does not exist is no reading at all */
static void read_value(zth_report_t *rep, zth_source_t src, const char *root, const char *rel,
                       const char *driver, const char *label)
{
    char path[ZTH_ROOT_MAX + ZTH_PATH_MAX], buf[ZTH_VALUE_MAX + 2u];
    if (!full_path(path, sizeof path, root, rel)) return;
    int n = read_file(path, buf, sizeof buf); /* fopen sets errno when it fails */
    if (n == -1 && errno == ENOENT) return;
    int32_t v = 0;
    const char *why = NULL;
    if (n < 0)
        why = WHY_UNREADABLE;
    else if ((size_t) n > ZTH_VALUE_MAX)
        why = WHY_LONG;
    else if (!zth_parse_mdeg(buf, (size_t) n, &v))
        why = WHY_GARBLED;
    add_reading(rep, src, driver, label, rel, v, why);
}

/* T3/T6: the entries of dir named prefix<digits>suffix, at most cap of them
 * among the first ZTH_MAX_ENTRIES; returns how many */
static uint32_t list_numbered(const char *root, const char *rel, const char *prefix,
                              const char *suffix, char (*names)[32], uint32_t cap)
{
    char path[ZTH_ROOT_MAX + ZTH_PATH_MAX];
    if (!full_path(path, sizeof path, root, rel)) return 0;
    DIR *d = opendir(path);
    if (!d) return 0;
    uint32_t got = 0;
    struct dirent *e;
    for (uint32_t seen = 0; seen < ZTH_MAX_ENTRIES && got < cap && (e = readdir(d)) != NULL; seen++)
        if (numbered(e->d_name, prefix, suffix)) copy_text(names[got++], 32, e->d_name);
    closedir(d);
    return got;
}

static void linux_hwmon(zth_report_t *rep, const char *root)
{
    static char devs[ZTH_MAX_DEVS][32], ents[ZTH_MAX_ENTRIES][32];
    uint32_t nd = list_numbered(root, "/sys/class/hwmon", "hwmon", "", devs, ZTH_MAX_DEVS);
    for (uint32_t i = 0; i < nd; i++) {
        char dir[ZTH_PATH_MAX], rel[ZTH_PATH_MAX], driver[ZTH_NAME_MAX], label[ZTH_NAME_MAX];
        join(dir, sizeof dir, "/sys/class/hwmon/", devs[i], "/", NULL);
        join(rel, sizeof rel, dir, "name", NULL, NULL);
        read_name(root, rel, driver);
        uint32_t ne = list_numbered(root, dir, "temp", "_input", ents, ZTH_MAX_ENTRIES);
        for (uint32_t j = 0; j < ne; j++) {
            char stem[32]; /* temp<N> */
            copy_text(stem, strlen(ents[j]) - strlen("_input") + 1u, ents[j]);
            join(rel, sizeof rel, dir, stem, "_label", NULL);
            read_name(root, rel, label);
            join(rel, sizeof rel, dir, ents[j], NULL, NULL);
            read_value(rep, ZTH_SRC_HWMON, root, rel, driver, label);
        }
    }
}

static void linux_zones(zth_report_t *rep, const char *root)
{
    static char zones[ZTH_MAX_DEVS][32];
    uint32_t nz =
        list_numbered(root, "/sys/class/thermal", "thermal_zone", "", zones, ZTH_MAX_DEVS);
    for (uint32_t i = 0; i < nz; i++) {
        char rel[ZTH_PATH_MAX], type[ZTH_NAME_MAX];
        join(rel, sizeof rel, "/sys/class/thermal/", zones[i], "/type", NULL);
        read_name(root, rel, type);
        join(rel, sizeof rel, "/sys/class/thermal/", zones[i], "/temp", NULL);
        read_value(rep, ZTH_SRC_ZONE, root, rel, type, "");
    }
}
#endif /* !_WIN32 */

/* ---- T1: macOS (compiled only on macOS; not compiled or tested in CI) ------- */

#if defined(__APPLE__)
/* whole millidegrees from the OS's double, or false for NaN and the absurd */
static bool mdeg_of(double c, int32_t *mdeg)
{
    if (!(c > -1000.0 && c < 1000.0)) return false; /* also false for NaN */
    double m = c * 1000.0;
    *mdeg = (int32_t) (m < 0 ? m - 0.5 : m + 0.5);
    return true;
}

#    if defined(__arm64__) || defined(__aarch64__)
/* Apple silicon: the HID event system's temperature services. These calls
 * are exported by IOKit.framework but not in its public headers. */
typedef struct __IOHIDEvent *IOHIDEventRef;
typedef struct __IOHIDServiceClient *IOHIDServiceClientRef;
typedef struct __IOHIDEventSystemClient *IOHIDEventSystemClientRef;
IOHIDEventSystemClientRef IOHIDEventSystemClientCreate(CFAllocatorRef allocator);
int IOHIDEventSystemClientSetMatching(IOHIDEventSystemClientRef client, CFDictionaryRef match);
CFArrayRef IOHIDEventSystemClientCopyServices(IOHIDEventSystemClientRef client);
CFTypeRef IOHIDServiceClientCopyProperty(IOHIDServiceClientRef service, CFStringRef key);
IOHIDEventRef IOHIDServiceClientCopyEvent(IOHIDServiceClientRef service, int64_t type,
                                          int32_t options, int64_t timestamp);
double IOHIDEventGetFloatValue(IOHIDEventRef event, int32_t field);

#        define ZTH_HID_TEMPERATURE 15 /* kIOHIDEventTypeTemperature */

static CFNumberRef cf_int(int32_t v)
{
    return CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &v);
}

static void mac_read(zth_report_t *rep)
{
    IOHIDEventSystemClientRef sys = IOHIDEventSystemClientCreate(kCFAllocatorDefault);
    if (!sys) {
        copy_text(rep->reason, sizeof rep->reason, "the HID event system could not be opened");
        return;
    }
    CFNumberRef page = cf_int(0xff00), usage = cf_int(5);
    const void *keys[2] = {CFSTR("PrimaryUsagePage"), CFSTR("PrimaryUsage")};
    const void *vals[2] = {page, usage};
    CFDictionaryRef match = page && usage ? CFDictionaryCreate(kCFAllocatorDefault, keys, vals, 2,
                                                               &kCFTypeDictionaryKeyCallBacks,
                                                               &kCFTypeDictionaryValueCallBacks)
                                          : NULL;
    if (page) CFRelease(page);
    if (usage) CFRelease(usage);
    CFArrayRef svcs = NULL;
    if (match) {
        IOHIDEventSystemClientSetMatching(sys, match);
        CFRelease(match);
        svcs = IOHIDEventSystemClientCopyServices(sys);
    }
    CFIndex n = svcs ? CFArrayGetCount(svcs) : 0;
    for (CFIndex i = 0; i < n && i < (CFIndex) ZTH_MAX_ENTRIES; i++) {
        IOHIDServiceClientRef s = (IOHIDServiceClientRef) CFArrayGetValueAtIndex(svcs, i);
        char raw[2u * ZTH_NAME_MAX] = {0}, label[ZTH_NAME_MAX], path[ZTH_PATH_MAX];
        CFTypeRef name = IOHIDServiceClientCopyProperty(s, CFSTR("Product"));
        if (name && CFGetTypeID(name) == CFStringGetTypeID() &&
            !CFStringGetCString((CFStringRef) name, raw, sizeof raw, kCFStringEncodingASCII))
            raw[0] = 0;
        if (name) CFRelease(name);
        keep_name(label, raw, strlen(raw));
        snprintf(path, sizeof path, "/iohid/%ld", (long) i);
        IOHIDEventRef ev = IOHIDServiceClientCopyEvent(s, ZTH_HID_TEMPERATURE, 0, 0);
        if (!ev) {
            add_reading(rep, ZTH_SRC_IOHID, "iohid", label, path, 0, WHY_UNREADABLE);
            continue;
        }
        int32_t v = 0;
        bool ok = mdeg_of(IOHIDEventGetFloatValue(ev, ZTH_HID_TEMPERATURE << 16), &v);
        CFRelease(ev);
        add_reading(rep, ZTH_SRC_IOHID, "iohid", label, path, v, ok ? NULL : WHY_RANGE);
    }
    if (svcs) CFRelease(svcs);
    CFRelease(sys);
    if (!rep->n && !rep->dropped)
        copy_text(rep->reason, sizeof rep->reason,
                  "the HID event system lists no temperature service");
}
#    else
/* Intel Macs: the SMC's temperature keys (type sp78: signed 7.8 fixed point) */
typedef struct {
    char major, minor, build, reserved;
    uint16_t release;
} zth_smc_vers_t;
typedef struct {
    uint16_t version, length;
    uint32_t cpu_plimit, gpu_plimit, mem_plimit;
} zth_smc_plimit_t;
typedef struct {
    uint32_t size, type;
    uint8_t attributes;
} zth_smc_info_t;
typedef struct {
    uint32_t key;
    zth_smc_vers_t vers;
    zth_smc_plimit_t plimit;
    zth_smc_info_t info;
    uint8_t result, status, data8;
    uint32_t data32;
    uint8_t bytes[32];
} zth_smc_t;
_Static_assert(sizeof(zth_smc_t) == 80, "the SMC call's structure is 80 bytes");

#        define ZTH_SMC_CALL     2 /* kSMCHandleYPCEvent */
#        define ZTH_SMC_READ     5
#        define ZTH_SMC_KEY_INFO 9

static uint32_t fourcc(const char *k)
{
    return (uint32_t) (unsigned char) k[0] << 24 | (uint32_t) (unsigned char) k[1] << 16 |
           (uint32_t) (unsigned char) k[2] << 8 | (uint32_t) (unsigned char) k[3];
}

static bool smc_call(io_connect_t c, zth_smc_t *in, zth_smc_t *out)
{
    size_t n = sizeof *out;
    memset(out, 0, sizeof *out);
    return IOConnectCallStructMethod(c, ZTH_SMC_CALL, in, sizeof *in, out, &n) == KERN_SUCCESS &&
           out->result == 0;
}

static const struct {
    const char key[5];
    const char *label;
} SMC_KEYS[] = {
    {"TC0P", "TC0P CPU proximity"}, {"TC0D", "TC0D CPU die"},       {"TC0E", "TC0E CPU die"},
    {"TC0F", "TC0F CPU die"},       {"TG0P", "TG0P GPU proximity"}, {"TG0D", "TG0D GPU die"},
    {"TA0P", "TA0P ambient"},       {"Tm0P", "Tm0P mainboard"},     {"TB0T", "TB0T battery"},
};

static void mac_read(zth_report_t *rep)
{
    io_service_t svc = IOServiceGetMatchingService(MACH_PORT_NULL, IOServiceMatching("AppleSMC"));
    io_connect_t c = 0;
    if (!svc || IOServiceOpen(svc, mach_task_self(), 0, &c) != KERN_SUCCESS) {
        if (svc) IOObjectRelease(svc);
        copy_text(rep->reason, sizeof rep->reason, "the SMC could not be opened");
        return;
    }
    IOObjectRelease(svc);
    for (size_t i = 0; i < sizeof SMC_KEYS / sizeof SMC_KEYS[0]; i++) {
        zth_smc_t in, out;
        char path[ZTH_PATH_MAX];
        memset(&in, 0, sizeof in);
        in.key = fourcc(SMC_KEYS[i].key);
        in.data8 = ZTH_SMC_KEY_INFO;
        if (!smc_call(c, &in, &out)) continue; /* this Mac has no such key */
        snprintf(path, sizeof path, "/smc/%s", SMC_KEYS[i].key);
        if (out.info.type != fourcc("sp78") || out.info.size != 2u) {
            add_reading(rep, ZTH_SRC_SMC, "smc", SMC_KEYS[i].label, path, 0, WHY_GARBLED);
            continue;
        }
        in.info.size = 2u;
        in.data8 = ZTH_SMC_READ;
        if (!smc_call(c, &in, &out)) {
            add_reading(rep, ZTH_SRC_SMC, "smc", SMC_KEYS[i].label, path, 0, WHY_UNREADABLE);
            continue;
        }
        int32_t raw = (int16_t) (uint16_t) ((uint16_t) out.bytes[0] << 8 | out.bytes[1]);
        add_reading(rep, ZTH_SRC_SMC, "smc", SMC_KEYS[i].label, path, raw * 1000 / 256, NULL);
    }
    IOServiceClose(c);
    if (!rep->n)
        copy_text(rep->reason, sizeof rep->reason, "the SMC has none of the temperature keys");
}
#    endif
#endif /* __APPLE__ */

/* ---- T1/T5: the whole read --------------------------------------------------- */

lpres_state_t zth_read(const char *root, zth_report_t *out)
{
    memset(out, 0, sizeof *out);
    bool fixture = root && root[0];
#if defined(_WIN32)
    out->host = "windows";
    copy_text(out->reason, sizeof out->reason, "this build has no temperature reader for Windows");
    (void) fixture;
#else
#    if defined(__APPLE__)
    if (!fixture) {
        out->host = "macos";
        mac_read(out);
        finish(out);
        return out->state;
    }
#    elif !defined(__linux__)
    if (!fixture) {
        out->host = "other";
        copy_text(out->reason, sizeof out->reason,
                  "this build has no temperature reader for this OS");
        finish(out);
        return out->state;
    }
#    endif
    out->host = "linux";
    if (fixture && strlen(root) >= ZTH_ROOT_MAX) {
        copy_text(out->reason, sizeof out->reason, "the sensor root path is too long");
        finish(out);
        return out->state;
    }
    const char *r = fixture ? root : "";
    linux_hwmon(out, r);
    linux_zones(out, r);
    if (!out->n && !out->dropped)
        copy_text(out->reason, sizeof out->reason,
                  "no temperature sensor under /sys/class/hwmon or /sys/class/thermal");
#endif
    finish(out);
    return out->state;
}

/* ---- T7: JSON ------------------------------------------------------------------ */

typedef struct {
    char *p;
    size_t cap, n;
    bool full;
} zth_out_t;

static void put(zth_out_t *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void put(zth_out_t *o, const char *fmt, ...)
{
    if (o->full) return;
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(o->p + o->n, o->cap - o->n, fmt, ap);
    va_end(ap);
    if (k < 0 || (size_t) k >= o->cap - o->n) {
        o->full = true;
        return;
    }
    o->n += (size_t) k;
}

/* a JSON string: names are printable ASCII (T3), so only " and \ need escaping */
static void put_str(zth_out_t *o, const char *s)
{
    put(o, "\"");
    for (; *s; s++) put(o, *s == '"' || *s == '\\' ? "\\%c" : "%c", *s);
    put(o, "\"");
}

static const char *const SOURCE_NAMES[] = {"hwmon", "thermal_zone", "iohid", "smc"};

int zth_render_json(const zth_report_t *rep, char *out, size_t cap)
{
    if (!out || !cap) return -1;
    zth_out_t o = {out, cap, 0, false};
    put(&o, "{\"state\":\"%s\",\"host\":", lpres_state_name(rep->state));
    put_str(&o, rep->host ? rep->host : "other");
    put(&o, ",\"reason\":");
    if (rep->reason[0])
        put_str(&o, rep->reason);
    else
        put(&o, "null");
    put(&o, ",\"classes\":{");
    for (uint32_t c = 0; c < ZTH_NCLASS; c++) {
        const zth_class_sum_t *s = &rep->cls[c];
        put(&o, "%s\"%s\":{\"state\":\"%s\",\"n\":%u,\"max_mdeg\":", c ? "," : "", CLASS_NAMES[c],
            lpres_state_name(s->state), (unsigned) s->n);
        if (s->n)
            put(&o, "%ld}", (long) s->max_mdeg);
        else
            put(&o, "null}");
    }
    put(&o, "},\"readings\":[");
    for (uint32_t i = 0; i < rep->n; i++) {
        const zth_reading_t *r = &rep->r[i];
        put(&o, "%s{\"class\":\"%s\",\"state\":\"%s\",\"mdeg\":", i ? "," : "", CLASS_NAMES[r->cls],
            lpres_state_name(r->state));
        if (r->state == LPRES_STATE_TRUE)
            put(&o, "%ld", (long) r->mdeg);
        else
            put(&o, "null");
        put(&o, ",\"source\":\"%s\",\"driver\":", SOURCE_NAMES[r->src]);
        put_str(&o, r->driver);
        put(&o, ",\"label\":");
        put_str(&o, r->label);
        put(&o, ",\"path\":");
        put_str(&o, r->path);
        put(&o, ",\"reason\":");
        if (r->reason)
            put_str(&o, r->reason);
        else
            put(&o, "null");
        put(&o, "}");
    }
    put(&o, "],\"rejected\":%u,\"dropped\":%u}", (unsigned) rep->rejected, (unsigned) rep->dropped);
    return o.full ? -1 : (int) o.n;
}
