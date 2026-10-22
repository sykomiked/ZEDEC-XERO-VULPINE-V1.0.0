/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_sched.c — timers apps register (see zxv_sched.h T1-T5). */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <sys/time.h>
#    include <unistd.h>
#else
#    include <windows.h>
#endif
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sha256.h"
#include "zxv_sched.h"

#define MAGIC    "zxv-sched 1\n"
#define FILE_CAP (192u * 1024u)

static char PATH_[ZSD_PATH_MAX + 1];
static zsd_entry_t S[ZSD_ENTRIES];
static uint32_t NS;
static zsd_fired_t F[ZSD_FIRED]; /* a ring: F[(seq - 1) % ZSD_FIRED] */
static uint64_t SEQ = 1;         /* the next firing's number */
static uint32_t NEXT_ID = 1;
static bool RESET, LOADING;
static int64_t (*CLOCK)(void);
static void (*HOOK)(void *, const zsd_fired_t *, const zsd_entry_t *);
static void *HOOK_CTX;

void zsd_set_clock(int64_t (*now_ms)(void))
{
    CLOCK = now_ms;
}

void zsd_set_hook(void (*on_fire)(void *ctx, const zsd_fired_t *f, const zsd_entry_t *e), void *ctx)
{
    HOOK = on_fire;
    HOOK_CTX = ctx;
}

static int64_t now_ms(void)
{
    if (CLOCK) return CLOCK();
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t) ft.dwHighDateTime << 32) | ft.dwLowDateTime; /* 100 ns since 1601 */
    return (int64_t) (t / 10000u) - INT64_C(11644473600000);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t) tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

/* ===================== names and times ===================== */

static bool good_app(const char *a)
{
    size_t n = a ? strlen(a) : 0;
    if (n < 1 || n > ZSD_APP_MAX) return false;
    for (size_t i = 0; i < n; i++)
        if (a[i] <= ' ' || a[i] > '~') return false;
    return true;
}

/* printable: no byte below a space and no DEL (UTF-8 above is kept) */
static bool good_text(const char *s, size_t lo, size_t hi)
{
    size_t n = strlen(s);
    if (n < lo || n > hi) return false;
    for (size_t i = 0; i < n; i++)
        if ((uint8_t) s[i] < 0x20 || s[i] == 0x7f) return false;
    return true;
}

int zsd_minute_parse(const char *s)
{
    if (!s || strlen(s) != 5 || s[2] != ':') return -1;
    for (int i = 0; i < 5; i++)
        if (i != 2 && (s[i] < '0' || s[i] > '9')) return -1;
    int h = (s[0] - '0') * 10 + (s[1] - '0'), m = (s[3] - '0') * 10 + (s[4] - '0');
    return h < 24 && m < 60 ? h * 60 + m : -1;
}

static const char *const KIND[] = {"at", "every", "daily"};

const char *zsd_kind_name(zsd_kind_t k)
{
    return (unsigned) k < 3 ? KIND[k] : "at";
}

const char *zsd_strerror(int code)
{
    switch (code) {
    case ZSD_OK:
        return "done";
    case ZSD_E_ARG:
        return "a bad app, name, note or time";
    case ZSD_E_FULL:
        return "no room for another schedule";
    case ZSD_E_IO:
        return "the schedules file could not be written";
    case ZSD_E_NOTFOUND:
        return "no such schedule";
    default:
        return "unknown";
    }
}

static bool local_tm(int64_t ms, struct tm *out)
{
    time_t t = (time_t) (ms / 1000);
#if defined(_WIN32)
    return localtime_s(out, &t) == 0;
#else
    return localtime_r(&t, out) != NULL;
#endif
}

/* T2: the first time after `now` that `e` is due; 0 when there is none
 * (a daily schedule with no days, or past the year 9999). */
static int64_t next_after(const zsd_entry_t *e, int64_t now)
{
    if (e->kind == ZSD_AT) return e->at_ms > now ? e->at_ms : 0;
    if (e->kind == ZSD_EVERY) {
        int64_t p = (int64_t) e->every_s * 1000;
        if (e->at_ms > now) return e->at_ms;
        int64_t k = (now - e->at_ms) / p + 1;
        int64_t t = e->at_ms + k * p;
        return t <= ZSD_TIME_MAX ? t : 0;
    }
    struct tm today, last;
    bool have_last = e->last_ms && local_tm(e->last_ms, &last);
    if (!(e->days & 0x7f) || !local_tm(now, &today)) return 0;
    for (int d = 0; d <= 8; d++) {
        struct tm t = today;
        t.tm_mday += d;
        t.tm_hour = e->minute / 60;
        t.tm_min = e->minute % 60;
        t.tm_sec = 0;
        t.tm_isdst = -1;
        time_t at = mktime(&t); /* t now normalized: its own date and weekday */
        if (at == (time_t) -1 || !(e->days & (1u << t.tm_wday))) continue;
        int64_t ms = (int64_t) at * 1000;
        if (ms <= now) continue;
        if (have_last && t.tm_year == last.tm_year && t.tm_yday == last.tm_yday) continue;
        return ms <= ZSD_TIME_MAX ? ms : 0;
    }
    return 0;
}

/* ===================== the file (T5) ===================== */

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

/* app, name and note are written as they are: none holds a tab or a line
 * end (good_app, good_text) */
static int save(void)
{
    if (!PATH_[0] || LOADING) return 0;
    static char buf[FILE_CAP];
    size_t n = strlen(MAGIC);
    memcpy(buf, MAGIC, n);
    int k = snprintf(buf + n, sizeof buf - n, "next\t%" PRIu64 "\t%" PRIu32 "\n", SEQ, NEXT_ID);
    n += (size_t) k;
    for (uint32_t i = 0; i < NS; i++) {
        const zsd_entry_t *e = &S[i];
        k = snprintf(buf + n, sizeof buf - n,
                     "sched\t%" PRIu32 "\t%s\t%s\t%s\t%" PRId64 "\t%" PRIu32
                     "\t%u\t%u\t%d\t%" PRId64 "\t%" PRId64 "\t%" PRIu32 "\t%s\n",
                     e->id, e->app, e->name, KIND[e->kind], e->at_ms, e->every_s,
                     (unsigned) e->minute, (unsigned) e->days, e->notify ? 1 : 0, e->next_ms,
                     e->last_ms, e->fired, e->note);
        if (k < 0 || (size_t) k >= sizeof buf - n) return ZSD_E_IO;
        n += (size_t) k;
    }
    for (uint64_t q = SEQ > ZSD_FIRED ? SEQ - ZSD_FIRED : 1; q < SEQ; q++) {
        const zsd_fired_t *f = &F[(q - 1) % ZSD_FIRED];
        k = snprintf(buf + n, sizeof buf - n,
                     "fired\t%" PRIu64 "\t%" PRIu32 "\t%s\t%s\t%" PRId64 "\t%" PRId64 "\t%" PRIu32
                     "\t%d\t%d\n",
                     f->seq, f->id, f->app, f->name, f->due_ms, f->fired_ms, f->missed,
                     f->late ? 1 : 0, f->ended ? 1 : 0);
        if (k < 0 || (size_t) k >= sizeof buf - n) return ZSD_E_IO;
        n += (size_t) k;
    }
    if (n + 70 >= sizeof buf) return ZSD_E_IO;
    uint8_t h[32];
    char hex[65];
    sha256((const uint8_t *) buf, n, h);
    hexify(h, hex);
    n += (size_t) snprintf(buf + n, sizeof buf - n, "end %s\n", hex);
    char tmp[ZSD_PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", PATH_);
#if defined(_WIN32)
    FILE *f = fopen(tmp, "wb");
#else
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    FILE *f = fd < 0 ? NULL : fdopen(fd, "wb");
    if (fd >= 0 && !f) close(fd);
#endif
    if (!f) return ZSD_E_IO;
    bool ok = fwrite(buf, 1, n, f) == n;
    ok = fclose(f) == 0 && ok;
    if (!ok || !replace_file(tmp, PATH_)) {
        remove(tmp);
        return ZSD_E_IO;
    }
    return 0;
}

/* A decimal integer field, all of it, within [lo, hi]. */
static bool num(const char *s, int64_t lo, int64_t hi, int64_t *v)
{
    if (!*s || strlen(s) > 20) return false;
    char *end;
    long long x = strtoll(s, &end, 10);
    if (*end || x < lo || x > hi) return false;
    *v = (int64_t) x;
    return true;
}

static bool copy(char *dst, const char *src, size_t max)
{
    size_t n = strlen(src);
    if (n > max) return false;
    memcpy(dst, src, n + 1);
    return true;
}

static zsd_entry_t *find(uint32_t id)
{
    for (uint32_t i = 0; i < NS; i++)
        if (S[i].id == id) return &S[i];
    return NULL;
}

static bool load_line(char *l, bool *had_next)
{
    char *f[14];
    int k = 0;
    f[k++] = l;
    for (char *p = l; *p; p++)
        if (*p == '\t') {
            if (k == 14) return false;
            *p = 0;
            f[k++] = p + 1;
        }
    int64_t v[12];
    if (!strcmp(f[0], "next") && k == 3 && !*had_next) {
        if (!num(f[1], 1, INT64_MAX, &v[0]) || !num(f[2], 1, UINT32_MAX, &v[1])) return false;
        SEQ = (uint64_t) v[0];
        NEXT_ID = (uint32_t) v[1];
        *had_next = true;
        return true;
    }
    if (!*had_next) return false;
    if (!strcmp(f[0], "sched") && k == 14) {
        if (NS == ZSD_ENTRIES) return false;
        zsd_entry_t e;
        memset(&e, 0, sizeof e);
        int kind = -1;
        for (int i = 0; i < 3; i++)
            if (!strcmp(f[4], KIND[i])) kind = i;
        if (!num(f[1], 1, UINT32_MAX, &v[0]) || !num(f[5], 0, ZSD_TIME_MAX, &v[1]) ||
            !num(f[6], 0, ZSD_EVERY_MAX, &v[2]) || !num(f[7], 0, 1439, &v[3]) ||
            !num(f[8], 0, 127, &v[4]) || !num(f[9], 0, 1, &v[5]) ||
            !num(f[10], 0, ZSD_TIME_MAX, &v[6]) || !num(f[11], 0, ZSD_TIME_MAX, &v[7]) ||
            !num(f[12], 0, UINT32_MAX, &v[8]) || kind < 0 || !good_app(f[2]) ||
            !good_text(f[3], 1, ZSD_NAME_MAX) || !good_text(f[13], 0, ZSD_NOTE_MAX) ||
            find((uint32_t) v[0]))
            return false;
        e.id = (uint32_t) v[0];
        copy(e.app, f[2], ZSD_APP_MAX);
        copy(e.name, f[3], ZSD_NAME_MAX);
        copy(e.note, f[13], ZSD_NOTE_MAX);
        e.kind = (uint8_t) kind;
        e.at_ms = v[1];
        e.every_s = (uint32_t) v[2];
        e.minute = (uint16_t) v[3];
        e.days = (uint8_t) v[4];
        e.notify = v[5] != 0;
        e.next_ms = v[6];
        e.last_ms = v[7];
        e.fired = (uint32_t) v[8];
        if ((kind == ZSD_EVERY && e.every_s < ZSD_EVERY_MIN) ||
            (kind == ZSD_DAILY && !(e.days & 0x7f)) || !e.next_ms)
            return false;
        S[NS++] = e;
        return true;
    }
    if (!strcmp(f[0], "fired") && k == 10) {
        if (!num(f[1], 1, INT64_MAX, &v[0]) || !num(f[2], 1, UINT32_MAX, &v[1]) ||
            !num(f[5], 0, ZSD_TIME_MAX, &v[2]) || !num(f[6], 0, ZSD_TIME_MAX, &v[3]) ||
            !num(f[7], 0, UINT32_MAX, &v[4]) || !num(f[8], 0, 1, &v[5]) ||
            !num(f[9], 0, 1, &v[6]) || !good_app(f[3]) || !good_text(f[4], 1, ZSD_NAME_MAX) ||
            (uint64_t) v[0] >= SEQ || (uint64_t) v[0] + ZSD_FIRED < SEQ)
            return false;
        zsd_fired_t *r = &F[((uint64_t) v[0] - 1) % ZSD_FIRED];
        if (r->seq) return false; /* twice */
        r->seq = (uint64_t) v[0];
        r->id = (uint32_t) v[1];
        copy(r->app, f[3], ZSD_APP_MAX);
        copy(r->name, f[4], ZSD_NAME_MAX);
        r->due_ms = v[2];
        r->fired_ms = v[3];
        r->missed = (uint32_t) v[4];
        r->late = v[5] != 0;
        r->ended = v[6] != 0;
        return true;
    }
    return false;
}

static void clear(void)
{
    memset(S, 0, sizeof S);
    memset(F, 0, sizeof F);
    NS = 0;
    SEQ = 1;
    NEXT_ID = 1;
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
    if (n > FILE_CAP || n < strlen(MAGIC) + 69 || strncmp(buf, MAGIC, strlen(MAGIC)) ||
        strlen(buf) != n)
        return -1;
    char *end = buf + n - 69; /* "end <64 hex>\n" */
    if (strncmp(end, "end ", 4) || end[68] != '\n' || end[-1] != '\n') return -1;
    uint8_t got[32];
    char hex[65];
    sha256((const uint8_t *) buf, (size_t) (end - buf), got);
    hexify(got, hex);
    if (memcmp(hex, end + 4, 64)) return -1;
    *end = 0;
    bool had_next = false;
    for (char *l = buf + strlen(MAGIC); *l;) {
        char *nl = strchr(l, '\n'); /* the text above "end" ends in "\n" */
        *nl = 0;
        if (!load_line(l, &had_next)) return -1;
        l = nl + 1;
    }
    return had_next ? 1 : -1;
}

int zsd_open(const char *path)
{
    zsd_close();
    if (!path || !path[0]) return 0;
    if (strlen(path) > ZSD_PATH_MAX) return ZSD_E_ARG;
    memcpy(PATH_, path, strlen(path) + 1);
    LOADING = true;
    int rc = load();
    LOADING = false;
    if (rc < 0) {
        clear();
        char aside[ZSD_PATH_MAX + 16];
        snprintf(aside, sizeof aside, "%s.damaged", PATH_);
        if (!replace_file(PATH_, aside)) {
            PATH_[0] = 0;
            return ZSD_E_IO;
        }
        RESET = true;
    }
    if (rc <= 0 && save() != 0) {
        PATH_[0] = 0;
        RESET = false;
        return ZSD_E_IO;
    }
    return 0;
}

void zsd_close(void)
{
    PATH_[0] = 0;
    RESET = false;
    clear();
}

bool zsd_was_reset(void)
{
    return RESET;
}

/* ===================== schedules (T2-T4) ===================== */

int zsd_add(zsd_entry_t *e, uint32_t *id)
{
    if (!e || !good_app(e->app) || !good_text(e->name, 1, ZSD_NAME_MAX) ||
        !good_text(e->note, 0, ZSD_NOTE_MAX) || e->kind > ZSD_DAILY)
        return ZSD_E_ARG;
    int64_t now = now_ms();
    if (e->kind == ZSD_AT && (e->at_ms <= now || e->at_ms > ZSD_TIME_MAX)) return ZSD_E_ARG;
    if (e->kind == ZSD_EVERY) {
        if (e->every_s < ZSD_EVERY_MIN || e->every_s > ZSD_EVERY_MAX || e->at_ms < 0 ||
            e->at_ms > ZSD_TIME_MAX)
            return ZSD_E_ARG;
        if (!e->at_ms) e->at_ms = now + (int64_t) e->every_s * 1000;
    }
    if (e->kind == ZSD_DAILY && (e->minute > 1439 || !(e->days & 0x7f))) return ZSD_E_ARG;
    if (e->kind != ZSD_AT && e->kind != ZSD_EVERY) e->at_ms = 0;
    if (e->kind != ZSD_EVERY) e->every_s = 0;
    if (e->kind != ZSD_DAILY) e->minute = e->days = 0;
    e->days &= 0x7f;
    e->last_ms = 0;
    e->fired = 0;
    e->next_ms = next_after(e, now);
    if (!e->next_ms) return ZSD_E_ARG;
    uint32_t mine = 0;
    for (uint32_t i = 0; i < NS; i++) mine += !strcmp(S[i].app, e->app);
    if (NS == ZSD_ENTRIES || mine >= ZSD_PER_APP) return ZSD_E_FULL;
    while (find(NEXT_ID)) NEXT_ID = NEXT_ID == UINT32_MAX ? 1 : NEXT_ID + 1; /* after a wrap */
    e->id = NEXT_ID;
    S[NS] = *e;
    NS++;
    NEXT_ID = NEXT_ID == UINT32_MAX ? 1 : NEXT_ID + 1;
    if (save() != 0) {
        NS--;
        NEXT_ID = e->id;
        return ZSD_E_IO;
    }
    if (id) *id = e->id;
    return ZSD_OK;
}

static void drop(zsd_entry_t *e)
{
    uint32_t i = (uint32_t) (e - S);
    memmove(&S[i], &S[i + 1], (NS - i - 1) * sizeof S[0]);
    NS--;
}

int zsd_cancel(uint32_t id)
{
    zsd_entry_t *e = find(id);
    if (!e) return ZSD_E_NOTFOUND;
    zsd_entry_t keep = *e;
    drop(e);
    if (save() != 0) { /* put it back where it was: the list keeps its order */
        uint32_t i = 0;
        while (i < NS && S[i].id < keep.id) i++;
        memmove(&S[i + 1], &S[i], (NS - i) * sizeof S[0]);
        S[i] = keep;
        NS++;
        return ZSD_E_IO;
    }
    return ZSD_OK;
}

uint32_t zsd_list(const char *app, zsd_entry_t *out, uint32_t cap)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < NS; i++) {
        if (app && app[0] && strcmp(S[i].app, app)) continue;
        if (n < cap) out[n] = S[i];
        n++;
    }
    return n;
}

uint32_t zsd_fired(const char *app, uint64_t after, zsd_fired_t *out, uint32_t cap, uint64_t *next)
{
    uint64_t q = SEQ > ZSD_FIRED ? SEQ - ZSD_FIRED : 1;
    if (q <= after) q = after + 1;
    uint32_t n = 0;
    uint64_t last = after;
    for (; q < SEQ && n < cap; q++) {
        const zsd_fired_t *f = &F[(q - 1) % ZSD_FIRED];
        last = q;
        if (app && app[0] && strcmp(f->app, app)) continue;
        out[n++] = *f;
    }
    if (next) *next = last > after ? last : (after < SEQ ? after : SEQ - 1);
    return n;
}

uint32_t zsd_tick(void)
{
    int64_t now = now_ms();
    uint32_t n = 0;
    for (uint32_t i = 0; i < NS;) {
        zsd_entry_t *e = &S[i];
        if (e->next_ms > now) {
            i++;
            continue;
        }
        zsd_fired_t *f = &F[(SEQ - 1) % ZSD_FIRED];
        memset(f, 0, sizeof *f);
        f->seq = SEQ++;
        f->id = e->id;
        memcpy(f->app, e->app, sizeof f->app);
        memcpy(f->name, e->name, sizeof f->name);
        f->due_ms = e->next_ms;
        f->fired_ms = now;
        f->late = now - e->next_ms > ZSD_LATE_MS;
        if (e->kind == ZSD_EVERY) {
            int64_t p = (int64_t) e->every_s * 1000, gap = (now - e->next_ms) / p;
            f->missed = gap > UINT32_MAX ? UINT32_MAX : (uint32_t) gap;
        }
        e->last_ms = now;
        e->fired++;
        zsd_entry_t as = *e;
        int64_t nx = next_after(e, now);
        if (nx) {
            e->next_ms = nx;
            i++;
        } else { /* an `at` schedule, or one with no time left */
            f->ended = true;
            drop(e);
        }
        n++;
        if (HOOK) HOOK(HOOK_CTX, f, &as);
    }
    if (n) save(); /* a write that fails is tried again at the next change */
    return n;
}
