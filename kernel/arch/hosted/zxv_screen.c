/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_screen.c — screen time (see zxv_screen.h SC1-SC7). */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <unistd.h>
#else
#    include <windows.h>
#    include <bcrypt.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sha256.h"
#include "zxv_screen.h"

#define MAGIC  "zxv-screen 1\n"
#define ROUNDS 200000u /* SHA-256 rounds over the passphrase (SC3) */

static char PATH_[ZSC_PATH_MAX + 1];
static zsc_day_t DAY[ZSC_DAYS]; /* newest first */
static uint32_t NDAYS;
static zsc_limit_t LIM[ZSC_APPS];
static uint32_t NLIM;
static bool LOCKED, RESET;
static uint8_t SALT[16], HASH[32];
static uint32_t FAILS;
static int64_t WAIT_UNTIL, LAST_TICK;
static int64_t (*CLOCK)(void);

/* ===================== time ===================== */

void zsc_set_clock(int64_t (*now)(void))
{
    CLOCK = now;
}

static int64_t now_s(void)
{
    return CLOCK ? CLOCK() : (int64_t) time(NULL);
}

static void local(int64_t t, struct tm *tm)
{
    time_t tt = (time_t) t;
#if defined(_WIN32)
    localtime_s(tm, &tt);
#else
    localtime_r(&tt, tm);
#endif
}

uint32_t zsc_today(void)
{
    struct tm tm;
    local(now_s(), &tm);
    return (uint32_t) (tm.tm_year + 1900) * 10000u + (uint32_t) (tm.tm_mon + 1) * 100u +
           (uint32_t) tm.tm_mday;
}

static uint16_t minute_of_day(void)
{
    struct tm tm;
    local(now_s(), &tm);
    return (uint16_t) (tm.tm_hour * 60 + tm.tm_min);
}

/* ===================== the passphrase (SC3) ===================== */

static int random_bytes(uint8_t *p, size_t n)
{
#if defined(_WIN32)
    return BCryptGenRandom(NULL, p, (ULONG) n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? 0 : -1;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return -1;
    size_t got = fread(p, 1, n, f);
    fclose(f);
    return got == n ? 0 : -1;
#endif
}

static void stretch(const uint8_t salt[16], const char *pass, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, salt, 16);
    sha256_update(&c, (const uint8_t *) pass, strlen(pass));
    sha256_final(&c, out);
    for (uint32_t i = 1; i < ROUNDS; i++) {
        sha256_init(&c);
        sha256_update(&c, out, 32);
        sha256_update(&c, salt, 16);
        sha256_final(&c, out);
    }
}

/* 0 when unlocked or the passphrase is right */
static int guardian(const char *pass)
{
    if (!LOCKED) return 0;
    if (zsc_wait() > 0) return ZSC_E_WAIT;
    uint8_t h[32], diff = 0;
    stretch(SALT, pass ? pass : "", h);
    for (int i = 0; i < 32; i++) diff |= (uint8_t) (h[i] ^ HASH[i]);
    if (!diff) {
        FAILS = 0;
        return 0;
    }
    if (++FAILS >= ZSC_TRIES) {
        FAILS = 0;
        WAIT_UNTIL = now_s() + ZSC_WAIT_S;
    }
    return ZSC_E_PASS;
}

int64_t zsc_wait(void)
{
    int64_t w = WAIT_UNTIL - now_s();
    return w > 0 ? w : 0;
}

bool zsc_locked(void)
{
    return LOCKED;
}

/* ===================== the file (SC6) ===================== */

static const char HEX[] = "0123456789abcdef";

static void hexify(const uint8_t *b, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = HEX[b[i] >> 4];
        out[2 * i + 1] = HEX[b[i] & 15];
    }
    out[2 * n] = 0;
}

static bool unhex(const char *s, uint8_t *b, size_t n)
{
    if (strlen(s) != 2 * n) return false;
    for (size_t i = 0; i < 2 * n; i++) {
        const char *p = strchr(HEX, s[i]);
        if (!p || !*p) return false;
        b[i / 2] = (uint8_t) ((i % 2 ? b[i / 2] << 4 : 0) | (p - HEX));
    }
    return true;
}

static bool good_app(const char *a)
{
    size_t n = a ? strlen(a) : 0;
    if (n < 1 || n > ZSC_APP_MAX) return false;
    if (!strcmp(a, ZSC_ALL)) return true;
    for (size_t i = 0; i < n; i++)
        if (a[i] <= ' ' || a[i] > '~' || a[i] == '*') return false;
    return true;
}

typedef struct {
    char *p;
    size_t n, cap;
    bool over;
} out_t;

static void put(out_t *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void put(out_t *o, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(o->p + o->n, o->cap - o->n, fmt, ap);
    va_end(ap);
    if (k < 0 || (size_t) k >= o->cap - o->n)
        o->over = true;
    else
        o->n += (size_t) k;
}

#define FILE_CAP (64u * 1024u)

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
    out_t o = {buf, 0, sizeof buf, false};
    char hs[33], hh[65];
    put(&o, "%s", MAGIC);
    if (LOCKED) {
        hexify(SALT, 16, hs);
        hexify(HASH, 32, hh);
        put(&o, "lock %s %s\n", hs, hh);
    }
    for (uint32_t i = 0; i < NLIM; i++)
        put(&o, "limit %s %ld %u %u %lu %lu\n", LIM[i].app, (long) LIM[i].minutes,
            (unsigned) LIM[i].from, (unsigned) LIM[i].to, (unsigned long) LIM[i].extra_date,
            (unsigned long) LIM[i].extra);
    for (uint32_t d = NDAYS; d-- > 0;)
        for (uint32_t i = 0; i < DAY[d].n; i++)
            put(&o, "day %lu %s %lu\n", (unsigned long) DAY[d].date, DAY[d].a[i].app,
                (unsigned long) DAY[d].a[i].minutes);
    if (o.over) return ZSC_E_IO;
    uint8_t h[32];
    sha256((const uint8_t *) buf, o.n, h);
    hexify(h, 32, hh);
    put(&o, "end %s\n", hh);
    if (o.over) return ZSC_E_IO;
    char tmp[ZSC_PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", PATH_);
#if defined(_WIN32)
    FILE *f = fopen(tmp, "wb");
#else
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    FILE *f = fd < 0 ? NULL : fdopen(fd, "wb");
    if (fd >= 0 && !f) close(fd);
#endif
    if (!f) return ZSC_E_IO;
    bool ok = fwrite(buf, 1, o.n, f) == o.n;
    ok = fclose(f) == 0 && ok;
    if (!ok || !replace_file(tmp, PATH_)) {
        remove(tmp);
        return ZSC_E_IO;
    }
    return 0;
}

static zsc_day_t *day_for(uint32_t date, bool make)
{
    for (uint32_t i = 0; i < NDAYS; i++)
        if (DAY[i].date == date) return &DAY[i];
    if (!make) return NULL;
    /* newest first; a new day goes in its place, the oldest drops */
    uint32_t at = 0;
    while (at < NDAYS && DAY[at].date > date) at++;
    if (at == ZSC_DAYS) return NULL;
    uint32_t keep = NDAYS < ZSC_DAYS ? NDAYS : ZSC_DAYS - 1;
    memmove(&DAY[at + 1], &DAY[at], (keep - at) * sizeof DAY[0]);
    memset(&DAY[at], 0, sizeof DAY[0]);
    DAY[at].date = date;
    NDAYS = keep + 1;
    return &DAY[at];
}

static zsc_use_t *use_for(zsc_day_t *d, const char *app, bool make)
{
    for (uint32_t i = 0; i < d->n; i++)
        if (!strcmp(d->a[i].app, app)) return &d->a[i];
    if (!make || d->n == ZSC_APPS) return NULL;
    zsc_use_t *u = &d->a[d->n++];
    memset(u, 0, sizeof *u);
    memcpy(u->app, app, strlen(app) + 1);
    return u;
}

static zsc_limit_t *limit_for(const char *app)
{
    for (uint32_t i = 0; i < NLIM; i++)
        if (!strcmp(LIM[i].app, app)) return &LIM[i];
    return NULL;
}

static bool load_line(char *l)
{
    char a[34], hh[66], b[ZSC_APP_MAX + 2];
    unsigned long x, y, z, w;
    long m;
    char tail;
    if (sscanf(l, "lock %33s %65s %c", a, hh, &tail) == 2) {
        if (LOCKED || !unhex(a, SALT, 16) || !unhex(hh, HASH, 32)) return false;
        LOCKED = true;
        return true;
    }
    if (sscanf(l, "limit %32s %ld %lu %lu %lu %lu %c", b, &m, &x, &y, &z, &w, &tail) == 6) {
        if (!good_app(b) || m < -1 || m > 1440 || x >= 1440 || y >= 1440 || w > 100000 ||
            z > 99991231 || limit_for(b) || NLIM == ZSC_APPS)
            return false;
        zsc_limit_t *t = &LIM[NLIM++];
        memset(t, 0, sizeof *t);
        memcpy(t->app, b, strlen(b) + 1);
        t->minutes = (int32_t) m;
        t->from = (uint16_t) x;
        t->to = (uint16_t) y;
        t->extra_date = (uint32_t) z;
        t->extra = (uint32_t) w;
        return true;
    }
    if (sscanf(l, "day %lu %32s %lu %c", &x, b, &y, &tail) == 3) {
        if (!good_app(b) || !strcmp(b, ZSC_ALL) || x < 19700101 || x > 99991231 || y > 1440)
            return false;
        zsc_day_t *d = day_for((uint32_t) x, true);
        if (!d) return true; /* older than the days kept */
        zsc_use_t *u = use_for(d, b, true);
        if (!u || u->minutes) return false;
        u->minutes = (uint32_t) y;
        d->total += (uint32_t) y;
        return true;
    }
    return false;
}

static void forget(void)
{
    memset(DAY, 0, sizeof DAY);
    memset(LIM, 0, sizeof LIM);
    NDAYS = NLIM = FAILS = 0;
    LOCKED = false;
    memset(SALT, 0, sizeof SALT);
    memset(HASH, 0, sizeof HASH);
    WAIT_UNTIL = LAST_TICK = 0;
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
    if (n > FILE_CAP || strncmp(buf, MAGIC, strlen(MAGIC)) || !end || end + 70 != buf + n ||
        buf[n - 1] != '\n')
        return -1;
    uint8_t want[32], got[32];
    end[69] = 0;
    if (!unhex(end + 5, want, 32)) return -1;
    sha256((const uint8_t *) buf, (size_t) (end + 1 - buf), got);
    if (memcmp(want, got, 32)) return -1;
    end[1] = 0;
    for (char *l = buf + strlen(MAGIC); *l;) {
        char *nl = strchr(l, '\n');
        if (!nl) return -1;
        *nl = 0;
        if (!load_line(l)) return -1;
        l = nl + 1;
    }
    return 1;
}

int zsc_open(const char *path)
{
    zsc_close();
    if (!path || !path[0]) return 0;
    if (strlen(path) > ZSC_PATH_MAX) return ZSC_E_ARG;
    memcpy(PATH_, path, strlen(path) + 1);
    int rc = load();
    if (rc < 0) {
        forget();
        char aside[ZSC_PATH_MAX + 16];
        snprintf(aside, sizeof aside, "%s.damaged", PATH_);
        if (!replace_file(PATH_, aside)) {
            PATH_[0] = 0;
            return ZSC_E_IO;
        }
        RESET = true;
    }
    if (rc <= 0 && save() != 0) {
        PATH_[0] = 0;
        RESET = false;
        return ZSC_E_IO;
    }
    return 0;
}

void zsc_close(void)
{
    PATH_[0] = 0;
    RESET = false;
    forget();
}

bool zsc_was_reset(void)
{
    return RESET;
}

/* ===================== checks (SC2, SC5) ===================== */

static bool in_hours(const zsc_limit_t *l, uint16_t now)
{
    if (l->from == l->to) return true;
    if (l->from < l->to) return now >= l->from && now < l->to;
    return now >= l->from || now < l->to;
}

/* apply one limit to `used` minutes; false when it says no */
static bool apply(const zsc_limit_t *l, uint32_t used, uint32_t today, uint16_t now,
                  zsc_answer_t *a)
{
    if (!in_hours(l, now)) {
        snprintf(a->why, sizeof a->why, "%s can be used only from %02u:%02u to %02u:%02u",
                 strcmp(l->app, ZSC_ALL) ? l->app : "the machine", l->from / 60u, l->from % 60u,
                 l->to / 60u, l->to % 60u);
        a->left = 0;
        return false;
    }
    if (l->minutes < 0) return true;
    uint32_t cap = (uint32_t) l->minutes + (l->extra_date == today ? l->extra : 0);
    int32_t left = used >= cap ? 0 : (int32_t) (cap - used);
    if (a->left < 0 || left < a->left) a->left = left;
    if (left == 0) {
        if (strcmp(l->app, ZSC_ALL))
            snprintf(a->why, sizeof a->why, "today's %u minute%s of %s %s used up", cap,
                     cap == 1 ? "" : "s", l->app, cap == 1 ? "is" : "are");
        else
            snprintf(a->why, sizeof a->why, "today's %u minute%s %s used up", cap,
                     cap == 1 ? "" : "s", cap == 1 ? "is" : "are");
        return false;
    }
    return true;
}

static void answer(const char *app, zsc_answer_t *a)
{
    uint32_t today = zsc_today();
    uint16_t now = minute_of_day();
    const zsc_day_t *d = day_for(today, false);
    const zsc_use_t *u = d ? use_for((zsc_day_t *) d, app, false) : NULL;
    a->used = u ? u->minutes : 0;
    a->total = d ? d->total : 0;
    a->left = -1;
    a->why[0] = 0;
    a->allowed = true;
    const zsc_limit_t *own = limit_for(app), *all = limit_for(ZSC_ALL);
    if (own && !apply(own, a->used, today, now, a)) a->allowed = false;
    if (a->allowed && all && !apply(all, a->total, today, now, a)) a->allowed = false;
}

int zsc_check(const char *app, zsc_answer_t *a)
{
    memset(a, 0, sizeof *a);
    if (!good_app(app) || !strcmp(app, ZSC_ALL)) return ZSC_E_ARG;
    answer(app, a);
    return 0;
}

int zsc_tick(const char *app, zsc_answer_t *a)
{
    memset(a, 0, sizeof *a);
    if (!good_app(app) || !strcmp(app, ZSC_ALL)) return ZSC_E_ARG;
    int64_t t = now_s();
    int rc = 0;
    if (!LAST_TICK || t - LAST_TICK >= ZSC_TICK_GAP || t < LAST_TICK) {
        zsc_day_t *d = day_for(zsc_today(), true);
        zsc_use_t *u = d ? use_for(d, app, true) : NULL;
        if (!u) return ZSC_E_FULL;
        if (u->minutes < 1440 && d->total < 1440) {
            u->minutes++;
            d->total++;
        }
        LAST_TICK = t;
        a->counted = true;
        rc = save();
    }
    answer(app, a);
    return rc;
}

/* ===================== the guardian's changes ===================== */

int zsc_lock(const char *old, const char *pass)
{
    size_t n = pass ? strlen(pass) : 0;
    if (n && (n < ZSC_PASS_MIN || n > ZSC_PASS_MAX)) return ZSC_E_ARG;
    int rc = guardian(old);
    if (rc) return rc;
    if (!n) {
        LOCKED = false;
        memset(SALT, 0, sizeof SALT);
        memset(HASH, 0, sizeof HASH);
        return save();
    }
    uint8_t salt[16];
    if (random_bytes(salt, sizeof salt) != 0) return ZSC_E_IO;
    memcpy(SALT, salt, sizeof salt);
    stretch(SALT, pass, HASH);
    LOCKED = true;
    return save();
}

int zsc_set_limit(const char *pass, const char *app, int32_t minutes, uint16_t from, uint16_t to)
{
    if (!good_app(app) || minutes < -1 || minutes > 1440 || from >= 1440 || to >= 1440)
        return ZSC_E_ARG;
    int rc = guardian(pass);
    if (rc) return rc;
    zsc_limit_t *l = limit_for(app);
    if (!l) {
        if (NLIM == ZSC_APPS) return ZSC_E_FULL;
        l = &LIM[NLIM++];
        memset(l, 0, sizeof *l);
        memcpy(l->app, app, strlen(app) + 1);
    }
    l->minutes = minutes;
    l->from = from;
    l->to = to;
    return save();
}

int zsc_clear_limit(const char *pass, const char *app)
{
    if (!good_app(app)) return ZSC_E_ARG;
    int rc = guardian(pass);
    if (rc) return rc;
    zsc_limit_t *l = limit_for(app);
    if (!l) return ZSC_E_NOTFOUND;
    *l = LIM[--NLIM];
    memset(&LIM[NLIM], 0, sizeof LIM[0]);
    return save();
}

int zsc_grant(const char *pass, const char *app, uint32_t minutes)
{
    if (!good_app(app) || minutes < 1 || minutes > 1440) return ZSC_E_ARG;
    int rc = guardian(pass);
    if (rc) return rc;
    zsc_limit_t *l = limit_for(app);
    if (!l) return ZSC_E_NOTFOUND;
    uint32_t today = zsc_today();
    if (l->extra_date != today) l->extra = 0;
    l->extra_date = today;
    l->extra = l->extra + minutes > 1440 ? 1440 : l->extra + minutes;
    return save();
}

/* ===================== reading ===================== */

uint32_t zsc_limit_count(void)
{
    return NLIM;
}

const zsc_limit_t *zsc_limit(uint32_t i)
{
    return i < NLIM ? &LIM[i] : NULL;
}

uint32_t zsc_day_count(void)
{
    return NDAYS;
}

const zsc_day_t *zsc_day(uint32_t i)
{
    return i < NDAYS ? &DAY[i] : NULL;
}

const char *zsc_strerror(int code)
{
    switch (code) {
    case ZSC_E_ARG:
        return "a bad app name, minutes or hours";
    case ZSC_E_PASS:
        return "the guardian's passphrase is wrong";
    case ZSC_E_WAIT:
        return "too many wrong passphrases; try again in a minute";
    case ZSC_E_FULL:
        return "no room for another app";
    case ZSC_E_IO:
        return "the screen-time file could not be written";
    case ZSC_E_NOTFOUND:
        return "no limit for that app";
    default:
        return "screen time failed";
    }
}
