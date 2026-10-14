/* timesvc.c — the system's idea of the wall-clock time. See timesvc.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "timesvc.h"

#define US_PER_S 1000000

/* Unsigned 64-by-32 division by shifting, so 32-bit targets need no compiler
 * helper (rv32 links no libgcc). */
static uint64_t udiv64(uint64_t n, uint32_t d)
{
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= (uint64_t) 1 << i;
        }
    }
    return q;
}

static int64_t abs64(int64_t v)
{
    return v < 0 ? -v : v;
}

static void copy_sample(sntp_sample_t *dst, const sntp_sample_t *src)
{
    const uint8_t *s = (const uint8_t *) src;
    uint8_t *d = (uint8_t *) dst;
    for (uint32_t i = 0; i < sizeof *dst; i++) d[i] = s[i];
}

void timesvc_init(timesvc_t *t, int64_t floor_s)
{
    uint8_t *p = (uint8_t *) t;
    for (uint32_t i = 0; i < sizeof *t; i++) p[i] = 0;
    t->floor_s = floor_s > 0 ? floor_s : (int64_t) ZXV_BUILD_UNIX;
    t->min_agree = 2;
    t->source = TIMESVC_SRC_NONE;
}

void timesvc_set_min_agree(timesvc_t *t, uint32_t n)
{
    t->min_agree = n == 0 ? 1 : n;
}

static bool plausible(const timesvc_t *t, int64_t unix_s)
{
    return unix_s >= t->floor_s && unix_s - t->floor_s <= TIMESVC_MAX_AHEAD_S;
}

bool timesvc_set_rtc(timesvc_t *t, int64_t rtc_unix_s, int64_t local_us)
{
    if (t->source == TIMESVC_SRC_NETWORK || !plausible(t, rtc_unix_s)) return false;
    t->offset_us = rtc_unix_s * US_PER_S - local_us;
    t->source = TIMESVC_SRC_RTC;
    return true;
}

bool timesvc_add_sample(timesvc_t *t, const sntp_sample_t *s)
{
    if (!s || t->n_samples >= TIMESVC_MAX_SAMPLES) return false;
    copy_sample(&t->samples[t->n_samples++], s);
    return true;
}

static bool agree(const sntp_sample_t *a, const sntp_sample_t *b)
{
    int64_t tol =
        TIMESVC_AGREE_US + a->delay_us / 2 + b->delay_us / 2 + a->root_dist_us + b->root_dist_us;
    return abs64(a->offset_us - b->offset_us) <= tol;
}

timesvc_status_t timesvc_commit(timesvc_t *t, int64_t local_us)
{
    uint32_t n = t->n_samples;
    t->n_samples = 0;
    t->agreed = 0;
    if (n < t->min_agree) return TIMESVC_TOO_FEW;

    /* For each sample, the size of the group agreeing with it; keep the
     * largest group, and within it the smallest round trip. */
    uint32_t best = 0, best_count = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t count = 0;
        for (uint32_t j = 0; j < n; j++) count += agree(&t->samples[i], &t->samples[j]);
        if (count > best_count ||
            (count == best_count && t->samples[i].delay_us < t->samples[best].delay_us)) {
            best = i;
            best_count = count;
        }
    }
    if (best_count < t->min_agree || best_count * 2 <= n) return TIMESVC_DISAGREE;

    uint32_t pick = best;
    for (uint32_t j = 0; j < n; j++)
        if (agree(&t->samples[best], &t->samples[j]) &&
            t->samples[j].delay_us < t->samples[pick].delay_us)
            pick = j;

    int64_t offset = t->samples[pick].offset_us;
    int64_t now_us = local_us + offset;
    if (now_us < t->floor_s * US_PER_S) return TIMESVC_BELOW_FLOOR;
    if (now_us - t->floor_s * US_PER_S > TIMESVC_MAX_AHEAD_S * US_PER_S) return TIMESVC_TOO_FAR;

    t->offset_us = offset;
    t->source = TIMESVC_SRC_NETWORK;
    t->agreed = best_count;
    return TIMESVC_SYNCED;
}

int64_t timesvc_now_us(const timesvc_t *t, int64_t local_us)
{
    if (t->source == TIMESVC_SRC_NONE) return 0;
    int64_t now = local_us + t->offset_us;
    return now > 0 ? now : 0;
}

int64_t timesvc_now_s(const timesvc_t *t, int64_t local_us)
{
    int64_t us = timesvc_now_us(t, local_us);
    return us > 0 ? (int64_t) udiv64((uint64_t) us, US_PER_S) : 0;
}

const char *timesvc_status_str(timesvc_status_t s)
{
    switch (s) {
    case TIMESVC_SYNCED:
        return "synced";
    case TIMESVC_TOO_FEW:
        return "too few servers answered";
    case TIMESVC_DISAGREE:
        return "servers disagree";
    case TIMESVC_BELOW_FLOOR:
        return "time is before the build";
    case TIMESVC_TOO_FAR:
        return "time is implausibly far ahead";
    }
    return "unknown";
}
