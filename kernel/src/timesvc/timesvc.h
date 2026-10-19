/* timesvc.h — the system's idea of the wall-clock time
 *
 * TLS refuses to verify a certificate without the current time (expiry
 * cannot be checked otherwise), so a device must learn the time at boot. It
 * has two sources:
 *
 *  - the battery-backed RTC, read by the RTC driver and handed in with
 *    timesvc_set_rtc(). A fresh or flat-battery board often reads
 *    2000-01-01; any reading below the build floor is ignored.
 *  - SNTP from several servers (sntp.h). Samples are collected with
 *    timesvc_add_sample() and combined with timesvc_commit().
 *
 * What one lying or broken server can do is limited:
 *  - The time never goes below the build floor (the time this image was
 *    built), so certificates that expired before the build stay expired.
 *  - timesvc_commit() needs at least `min_agree` servers (2 by default)
 *    agreeing within a tolerance, and they must be a strict majority of the
 *    samples. The agreeing sample with the smallest round trip wins.
 *  - A time more than 100 years past the floor is refused.
 *
 * An attacker who controls every server's path can still pick the time (SNTP
 * is unauthenticated). Network Time Security (RFC 8915) is the fix; not done.
 *
 * The local clock is any monotonic microsecond counter (the timer driver's);
 * timesvc keeps an offset from it, so reading the time costs one addition.
 * Nothing here reads hardware.
 *
 * Freestanding: integer only, no libc, no allocation, no 64-bit division.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_TIMESVC_H
#define ZXV_TIMESVC_H

#include <stdint.h>
#include <stdbool.h>
#include "sntp.h"

#define TIMESVC_MAX_SAMPLES 8u
/* Two samples agree when their offsets differ by at most this much plus half
 * of each round trip plus each server's root distance. */
#define TIMESVC_AGREE_US    1000000
#define TIMESVC_MAX_AHEAD_S ((int64_t) 100 * 366 * 86400)

/* The build floor when the build system supplies none: 2026-10-12T00:00:00Z.
 * build_system passes -DZXV_BUILD_UNIX=<seconds> to move it forward. */
#ifndef ZXV_BUILD_UNIX
#    define ZXV_BUILD_UNIX 1791763200
#endif

typedef enum { TIMESVC_SRC_NONE = 0, TIMESVC_SRC_RTC, TIMESVC_SRC_NETWORK } timesvc_source_t;

typedef enum {
    TIMESVC_SYNCED = 0,
    TIMESVC_TOO_FEW,     /* fewer than min_agree usable samples */
    TIMESVC_DISAGREE,    /* no group of min_agree samples that is a majority */
    TIMESVC_BELOW_FLOOR, /* the agreed time is before the build floor */
    TIMESVC_TOO_FAR,     /* the agreed time is implausibly far in the future */
} timesvc_status_t;

typedef struct {
    int64_t floor_s;
    uint32_t min_agree;
    timesvc_source_t source;
    int64_t offset_us; /* Unix microseconds = local microseconds + offset_us */
    sntp_sample_t samples[TIMESVC_MAX_SAMPLES];
    uint32_t n_samples;
    uint32_t agreed; /* how many samples agreed at the last commit */
} timesvc_t;

/* floor_s <= 0 selects ZXV_BUILD_UNIX. */
void timesvc_init(timesvc_t *t, int64_t floor_s);
/* 1 allows a single trusted server (a private network's own NTP). */
void timesvc_set_min_agree(timesvc_t *t, uint32_t n);

/* An RTC reading taken at local time `local_us`. Ignored (returns false)
 * when below the floor, too far ahead, or when the network has already set
 * the time. */
bool timesvc_set_rtc(timesvc_t *t, int64_t rtc_unix_s, int64_t local_us);

/* Keep one SNTP_OK sample for the next commit. Returns false when full. */
bool timesvc_add_sample(timesvc_t *t, const sntp_sample_t *s);
/* Combine the kept samples, set the time on success, and clear them. */
timesvc_status_t timesvc_commit(timesvc_t *t, int64_t local_us);

/* Unix microseconds now, or 0 when the time is unknown. */
int64_t timesvc_now_us(const timesvc_t *t, int64_t local_us);
/* Unix seconds now for tls_client_set_trust(): 0 (TLS then fails closed)
 * when the time is unknown. */
int64_t timesvc_now_s(const timesvc_t *t, int64_t local_us);

const char *timesvc_status_str(timesvc_status_t s);

#endif /* ZXV_TIMESVC_H */
