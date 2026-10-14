/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_place.c — location (see zxv_place.h P1-P5). */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <unistd.h>
#else
#    include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sha256.h"
#include "zxv_place.h"

#define MAGIC    "zxv-places 1\n"
#define FILE_CAP (128u * 1024u)

static char PATH_[ZPL_PATH_MAX + 1];
static zpl_here_t HERE;
static zpl_place_t PL[ZPL_PLACES];
static uint32_t NPL;
static zpl_app_t AP[ZPL_APPS];
static uint32_t NAP;
static bool RESET, LOADING;
static int64_t (*CLOCK)(void);

void zpl_set_clock(int64_t (*now)(void))
{
    CLOCK = now;
}

static int64_t now_s(void)
{
    return CLOCK ? CLOCK() : (int64_t) time(NULL);
}

/* ===================== points and distance (P1, P4) ===================== */

bool zpl_valid(zpl_point_t p)
{
    return p.lat >= -ZPL_LAT_MAX && p.lat <= ZPL_LAT_MAX && p.lon >= -ZPL_LON_MAX &&
           p.lon <= ZPL_LON_MAX;
}

/* cos(d degrees) in millionths, d = 0..90 */
static const int32_t COS[91] = {
    1000000, 999848, 999391, 998630, 997564, 996195, 994522, 992546, 990268, 987688, 984808, 981627,
    978148,  974370, 970296, 965926, 961262, 956305, 951057, 945519, 939693, 933580, 927184, 920505,
    913545,  906308, 898794, 891007, 882948, 874620, 866025, 857167, 848048, 838671, 829038, 819152,
    809017,  798636, 788011, 777146, 766044, 754710, 743145, 731354, 719340, 707107, 694658, 681998,
    669131,  656059, 642788, 629320, 615661, 601815, 587785, 573576, 559193, 544639, 529919, 515038,
    500000,  484810, 469472, 453990, 438371, 422618, 406737, 390731, 374607, 358368, 342020, 325568,
    309017,  292372, 275637, 258819, 241922, 224951, 207912, 190809, 173648, 156434, 139173, 121869,
    104528,  87156,  69756,  52336,  34899,  17452,  0};

static int64_t cos_e6(int32_t lat)
{
    int64_t a = lat < 0 ? -(int64_t) lat : lat;
    int64_t d = a / 1000000, f = a % 1000000;
    if (d >= 90) return 0;
    return COS[d] + (COS[d + 1] - COS[d]) * f / 1000000;
}

static uint64_t isqrt(uint64_t v)
{
    uint64_t r = 0, bit = (uint64_t) 1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

uint64_t zpl_distance_m(zpl_point_t a, zpl_point_t b)
{
    int64_t dlon = (int64_t) b.lon - a.lon;
    if (dlon > ZPL_LON_MAX) dlon -= 2 * (int64_t) ZPL_LON_MAX;
    if (dlon < -ZPL_LON_MAX) dlon += 2 * (int64_t) ZPL_LON_MAX;
    int64_t dlat = (int64_t) b.lat - a.lat;
    int64_t c = cos_e6((int32_t) (((int64_t) a.lat + b.lat) / 2));
    int64_t dx = dlon * c / 1000000 * 111320 / 1000000; /* metres per degree at the equator */
    int64_t dy = dlat * 111132 / 1000000;               /* metres per degree of latitude */
    return isqrt((uint64_t) (dx * dx) + (uint64_t) (dy * dy));
}

/* ===================== names ===================== */

static bool plain(const char *s, size_t min, size_t max)
{
    size_t n = s ? strlen(s) : 0;
    if (n < min || n > max) return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < ' ' || s[i] == 0x7f) return false; /* no tabs or newlines */
    return true;
}

static bool good_app(const char *a)
{
    size_t n = a ? strlen(a) : 0;
    if (n < 1 || n > ZPL_APP_MAX) return false;
    for (size_t i = 0; i < n; i++)
        if (a[i] <= ' ' || a[i] > '~') return false;
    return true;
}

static bool same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char) (*a | 0x20) : *a;
        char y = *b >= 'A' && *b <= 'Z' ? (char) (*b | 0x20) : *b;
        if (x != y) return false;
    }
    return *a == *b;
}

static const char *const MODE[] = {"ask", "once", "always", "never"};

const char *zpl_mode_name(zpl_mode_t m)
{
    return (unsigned) m < 4 ? MODE[m] : "ask";
}

int zpl_mode_parse(const char *s)
{
    for (int i = 0; s && i < 4; i++)
        if (!strcmp(s, MODE[i])) return i;
    return -1;
}

/* ===================== the file (P5) ===================== */

static const char HEX[] = "0123456789abcdef";

static void hexify(const uint8_t *b, char *out)
{
    for (size_t i = 0; i < 32; i++) {
        out[2 * i] = HEX[b[i] >> 4];
        out[2 * i + 1] = HEX[b[i] & 15];
    }
    out[64] = 0;
}

static bool replace_file(const char *from, const char *to)
{
#if defined(_WIN32)
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(from, to) == 0;
#endif
}

static int save(void)
{
    if (!PATH_[0]) return 0;
    static char buf[FILE_CAP];
    size_t n = 0;
    int k = snprintf(buf, sizeof buf, "%s", MAGIC);
    n = (size_t) k;
    for (uint32_t i = 0; i < NPL && n < sizeof buf; i++) {
        k = snprintf(buf + n, sizeof buf - n, "place\t%ld\t%ld\t%s\t%s\n", (long) PL[i].at.lat,
                     (long) PL[i].at.lon, PL[i].name, PL[i].note);
        n += k > 0 ? (size_t) k : 0;
    }
    for (uint32_t i = 0; i < NAP && n < sizeof buf; i++) {
        if (AP[i].mode == ZPL_ASK || AP[i].mode == ZPL_ONCE) continue; /* only lasting answers */
        k = snprintf(buf + n, sizeof buf - n, "app\t%s\t%s\n", AP[i].app, MODE[AP[i].mode]);
        n += k > 0 ? (size_t) k : 0;
    }
    if (n + 70 >= sizeof buf) return ZPL_E_IO;
    uint8_t h[32];
    char hex[65];
    sha256((const uint8_t *) buf, n, h);
    hexify(h, hex);
    n += (size_t) snprintf(buf + n, sizeof buf - n, "end %s\n", hex);
    char tmp[ZPL_PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", PATH_);
#if defined(_WIN32)
    FILE *f = fopen(tmp, "wb");
#else
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    FILE *f = fd < 0 ? NULL : fdopen(fd, "wb");
    if (fd >= 0 && !f) close(fd);
#endif
    if (!f) return ZPL_E_IO;
    bool ok = fwrite(buf, 1, n, f) == n;
    ok = fclose(f) == 0 && ok;
    if (!ok || !replace_file(tmp, PATH_)) {
        remove(tmp);
        return ZPL_E_IO;
    }
    return 0;
}

static zpl_app_t *app_for(const char *app, bool make)
{
    for (uint32_t i = 0; i < NAP; i++)
        if (!strcmp(AP[i].app, app)) return &AP[i];
    if (!make || NAP == ZPL_APPS) return NULL;
    zpl_app_t *a = &AP[NAP++];
    memset(a, 0, sizeof *a);
    memcpy(a->app, app, strlen(app) + 1);
    return a;
}

/* split a line at tabs into at most n fields; the count */
static int fields(char *l, char **f, int n)
{
    int k = 0;
    f[k++] = l;
    for (char *p = l; *p && k <= n; p++)
        if (*p == '\t') {
            *p = 0;
            if (k == n) return n + 1; /* too many */
            f[k++] = p + 1;
        }
    return k;
}

static bool num(const char *s, long lo, long hi, int32_t *v)
{
    char *end;
    long x = strtol(s, &end, 10);
    if (!*s || *end || x < lo || x > hi) return false;
    *v = (int32_t) x;
    return true;
}

static bool load_line(char *l)
{
    char *f[5];
    int k = fields(l, f, 5);
    if (k == 5 && !strcmp(f[0], "place")) {
        zpl_point_t at;
        if (!num(f[1], -ZPL_LAT_MAX, ZPL_LAT_MAX, &at.lat) ||
            !num(f[2], -ZPL_LON_MAX, ZPL_LON_MAX, &at.lon))
            return false;
        return zpl_add(f[3], at, f[4]) == 0;
    }
    if (k == 3 && !strcmp(f[0], "app")) {
        int m = zpl_mode_parse(f[2]);
        if ((m != ZPL_ALWAYS && m != ZPL_NEVER) || !good_app(f[1]) || app_for(f[1], false))
            return false;
        zpl_app_t *a = app_for(f[1], true);
        if (!a) return false;
        a->mode = (uint8_t) m;
        return true;
    }
    return false;
}

static void forget(void)
{
    memset(&HERE, 0, sizeof HERE);
    memset(PL, 0, sizeof PL);
    memset(AP, 0, sizeof AP);
    NPL = NAP = 0;
}

/* 1 loaded, 0 no file, -1 damaged */
static int load(void)
{
    static char buf[FILE_CAP + 2];
    FILE *f = fopen(PATH_, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, FILE_CAP + 1, f);
    fclose(f);
    buf[n] = 0;
    char *end = n > 70 ? strstr(buf, "\nend ") : NULL;
    if (n > FILE_CAP || strncmp(buf, MAGIC, strlen(MAGIC)) || !end || end + 70 != buf + n)
        return -1;
    uint8_t got[32];
    char hex[65];
    sha256((const uint8_t *) buf, (size_t) (end + 1 - buf), got);
    hexify(got, hex);
    if (memcmp(hex, end + 5, 64) || end[69] != '\n') return -1;
    end[1] = 0;
    for (char *l = buf + strlen(MAGIC); *l;) {
        char *nl = strchr(l, '\n');
        *nl = 0; /* the text ends in "\n" (end[0]) */
        if (!load_line(l)) return -1;
        l = nl + 1;
    }
    return 1;
}

int zpl_open(const char *path)
{
    zpl_close();
    if (!path || !path[0]) return 0;
    if (strlen(path) > ZPL_PATH_MAX) return ZPL_E_ARG;
    memcpy(PATH_, path, strlen(path) + 1);
    LOADING = true; /* zpl_add does not write while the file is read */
    int rc = load();
    LOADING = false;
    if (rc < 0) {
        forget();
        char aside[ZPL_PATH_MAX + 16];
        snprintf(aside, sizeof aside, "%s.damaged", PATH_);
        if (!replace_file(PATH_, aside)) {
            PATH_[0] = 0;
            return ZPL_E_IO;
        }
        RESET = true;
    }
    if (rc <= 0 && save() != 0) {
        PATH_[0] = 0;
        RESET = false;
        return ZPL_E_IO;
    }
    return 0;
}

void zpl_close(void)
{
    PATH_[0] = 0;
    RESET = false;
    forget();
}

bool zpl_was_reset(void)
{
    return RESET;
}

/* ===================== where (P1) ===================== */

int zpl_set_here(zpl_point_t at, uint32_t accuracy_m, const char *source)
{
    if (!zpl_valid(at) || !accuracy_m || accuracy_m > 1000000 || !good_app(source) ||
        strlen(source) > ZPL_SRC_MAX)
        return ZPL_E_ARG;
    HERE.known = true;
    HERE.at = at;
    HERE.accuracy_m = accuracy_m;
    memcpy(HERE.source, source, strlen(source) + 1);
    HERE.time = now_s();
    return 0;
}

void zpl_forget_here(void)
{
    memset(&HERE, 0, sizeof HERE);
}

const zpl_here_t *zpl_here(void)
{
    return &HERE;
}

bool zpl_stale(void)
{
    return HERE.known && now_s() - HERE.time > ZPL_STALE_S;
}

/* ===================== permission (P2) ===================== */

int zpl_ask(const char *app, zpl_here_t *out)
{
    memset(out, 0, sizeof *out);
    if (!good_app(app)) return ZPL_E_ARG;
    zpl_app_t *a = app_for(app, true);
    if (!a) return ZPL_E_FULL;
    a->asked = now_s();
    if (a->mode == ZPL_NEVER) return ZPL_E_DENIED;
    if (a->mode == ZPL_ASK) {
        a->pending = true;
        return ZPL_E_PENDING;
    }
    if (!HERE.known) return ZPL_E_NONE;
    if (a->mode == ZPL_ONCE) a->mode = ZPL_ASK; /* used up */
    *out = HERE;
    return 0;
}

int zpl_permit(const char *app, zpl_mode_t mode)
{
    if (!good_app(app) || (unsigned) mode > ZPL_NEVER) return ZPL_E_ARG;
    zpl_app_t *a = app_for(app, true);
    if (!a) return ZPL_E_FULL;
    bool lasting =
        a->mode == ZPL_ALWAYS || a->mode == ZPL_NEVER || mode == ZPL_ALWAYS || mode == ZPL_NEVER;
    a->mode = (uint8_t) mode;
    a->pending = false;
    return lasting ? save() : 0;
}

uint32_t zpl_app_count(void)
{
    return NAP;
}

const zpl_app_t *zpl_app(uint32_t i)
{
    return i < NAP ? &AP[i] : NULL;
}

/* ===================== places (P3) ===================== */

const zpl_place_t *zpl_find(const char *name)
{
    for (uint32_t i = 0; name && i < NPL; i++)
        if (same_name(PL[i].name, name)) return &PL[i];
    return NULL;
}

int zpl_add(const char *name, zpl_point_t at, const char *note)
{
    if (!note) note = "";
    if (!plain(name, 1, ZPL_NAME_MAX) || !plain(note, 0, ZPL_NOTE_MAX) || !zpl_valid(at) ||
        name[0] == ' ')
        return ZPL_E_ARG;
    if (zpl_find(name)) return ZPL_E_EXISTS;
    if (NPL == ZPL_PLACES) return ZPL_E_FULL;
    zpl_place_t *p = &PL[NPL++];
    memset(p, 0, sizeof *p);
    memcpy(p->name, name, strlen(name) + 1);
    memcpy(p->note, note, strlen(note) + 1);
    p->at = at;
    return LOADING ? 0 : save();
}

int zpl_remove(const char *name)
{
    const zpl_place_t *p = zpl_find(name);
    if (!p) return name ? ZPL_E_NOTFOUND : ZPL_E_ARG;
    uint32_t i = (uint32_t) (p - PL);
    memmove(&PL[i], &PL[i + 1], (NPL - i - 1) * sizeof PL[0]);
    memset(&PL[--NPL], 0, sizeof PL[0]);
    return save();
}

uint32_t zpl_count(void)
{
    return NPL;
}

const zpl_place_t *zpl_place(uint32_t i)
{
    return i < NPL ? &PL[i] : NULL;
}

const char *zpl_strerror(int code)
{
    switch (code) {
    case ZPL_E_ARG:
        return "a bad name, app or coordinate";
    case ZPL_E_NOTFOUND:
        return "no such place";
    case ZPL_E_EXISTS:
        return "a place has that name";
    case ZPL_E_FULL:
        return "no room for another place or app";
    case ZPL_E_IO:
        return "the places file could not be written";
    case ZPL_E_NONE:
        return "the location is not known";
    case ZPL_E_DENIED:
        return "the owner does not let this app know the location";
    case ZPL_E_PENDING:
        return "waiting for the owner to answer";
    default:
        return "location failed";
    }
}
