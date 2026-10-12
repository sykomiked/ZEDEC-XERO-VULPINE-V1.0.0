/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_log.c — see zxvd_log.h */
#if !defined(_WIN32)
#    define _POSIX_C_SOURCE 200809L
#endif

#include "zxvd_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#    include <windows.h>
#endif

static zxvd_log_entry_t RING[ZXVD_LOG_RING];
static uint64_t LAST;
static zxvd_traffic_t TRAFFIC;

uint64_t zxvd_now_ms(void)
{
#if defined(_WIN32)
    FILETIME f;
    GetSystemTimeAsFileTime(&f);
    uint64_t t = ((uint64_t) f.dwHighDateTime << 32) | f.dwLowDateTime; /* 100 ns since 1601 */
    return t / 10000u - 11644473600000ull;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
    return (uint64_t) ts.tv_sec * 1000u + (uint64_t) ts.tv_nsec / 1000000u;
#endif
}

uint64_t zxvd_mono_us(void)
{
#if defined(_WIN32)
    LARGE_INTEGER c, f;
    if (!QueryPerformanceCounter(&c) || !QueryPerformanceFrequency(&f) || !f.QuadPart) return 0;
    return (uint64_t) (c.QuadPart / f.QuadPart) * 1000000u +
           (uint64_t) (c.QuadPart % f.QuadPart) * 1000000u / (uint64_t) f.QuadPart;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t) ts.tv_sec * 1000000u + (uint64_t) ts.tv_nsec / 1000u;
#endif
}

void zxvd_log(zxvd_log_level_t level, const char *source, const char *fmt, ...)
{
    zxvd_log_entry_t *e = &RING[LAST % ZXVD_LOG_RING];
    char tmp[ZXVD_LOG_TEXT * 2];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    size_t len = (size_t) n < sizeof tmp ? (size_t) n : sizeof tmp - 1;
    if (len > ZXVD_LOG_TEXT) {
        len = ZXVD_LOG_TEXT;
        while (len && ((unsigned char) tmp[len] & 0xC0) == 0x80) len--; /* not mid-character */
    }
    memset(e, 0, sizeof *e);
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char) tmp[i];
        e->text[i] = ch < 0x20 || ch == 0x7F ? ' ' : (char) ch;
    }
    size_t sl = source ? strlen(source) : 0;
    if (sl >= sizeof e->source) sl = sizeof e->source - 1;
    if (sl) memcpy(e->source, source, sl);
    e->level = (uint8_t) (level <= ZXVD_LOG_ERROR ? level : ZXVD_LOG_ERROR);
    e->unix_ms = zxvd_now_ms();
    e->seq = ++LAST;
}

uint32_t zxvd_log_tail(uint64_t since, uint32_t max, zxvd_log_level_t min_level,
                       zxvd_log_entry_t *out, uint64_t *dropped)
{
    uint64_t first = LAST > ZXVD_LOG_RING ? LAST - ZXVD_LOG_RING + 1 : 1; /* oldest held */
    uint64_t from = since + 1;
    *dropped = from < first ? first - from : 0;
    if (from < first) from = first;
    uint32_t k = 0;
    for (uint64_t s = from; s <= LAST && k < max; s++) {
        const zxvd_log_entry_t *e = &RING[(s - 1) % ZXVD_LOG_RING];
        if (e->level >= (uint8_t) min_level) out[k++] = *e;
    }
    return k;
}

uint64_t zxvd_log_last(void)
{
    return LAST;
}

const char *zxvd_log_level_name(zxvd_log_level_t l)
{
    return l >= ZXVD_LOG_ERROR ? "error" : l == ZXVD_LOG_WARN ? "warn" : "info";
}

void zxvd_traffic_add(uint64_t bytes_in, uint64_t bytes_out, bool error, uint64_t us)
{
    TRAFFIC.requests++;
    TRAFFIC.errors += error;
    TRAFFIC.bytes_in += bytes_in;
    TRAFFIC.bytes_out += bytes_out;
    TRAFFIC.busy_us += us;
}

void zxvd_traffic_get(zxvd_traffic_t *t)
{
    *t = TRAFFIC;
}

void zxvd_log_reset(void)
{
    memset(RING, 0, sizeof RING);
    LAST = 0;
    memset(&TRAFFIC, 0, sizeof TRAFFIC);
}
