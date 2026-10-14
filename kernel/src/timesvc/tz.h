/* tz.h — time zones: UTC to local time
 *
 * Three pieces:
 *
 *  - POSIX TZ strings ("EST5EDT,M3.2.0,M11.1.0", "<+0530>-5:30"), including
 *    the RFC 8536 version 3 extensions (rule times from -167 to 167 hours,
 *    so all-year daylight time can be written).
 *  - TZif files (RFC 8536, versions 2-4): the full history of a zone, as
 *    shipped in /usr/share/zoneinfo. Files with leap-second records are
 *    refused, since their timestamps are not Unix time.
 *  - A built-in table mapping every IANA zone name to the POSIX string from
 *    the footer of its TZif file (tz_zones.h, generated from tzdata, which is
 *    public domain). The footer gives each zone's current rules, so the table
 *    is right for the present and the future but not for dates before a
 *    zone's last rule change; a TZif file is needed for those. Two zones
 *    (Asia/Gaza and Asia/Hebron, whose daylight time ends early in some
 *    years for Ramadan) are not expressible as a POSIX string at all.
 *
 * Offsets are seconds east of UTC (POSIX strings count west; the parser
 * flips the sign). Supported instants: years 1901 to 6000.
 *
 * Freestanding: integer only, no libc, no allocation, no 64-bit division.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_TZ_H
#define ZXV_TZ_H

#include <stdint.h>
#include <stdbool.h>

#define TZ_ABBR_MAX 8u                       /* 7 characters and a NUL; POSIX asks for 3 to 6 */
#define TZ_MIN_UNIX ((int64_t) - 2177452800) /* 1901-01-01T00:00:00Z */
#define TZ_MAX_UNIX ((int64_t) 127174492800) /* 6000-01-01T00:00:00Z */

typedef struct {
    int32_t utoff; /* seconds east of UTC */
    bool is_dst;
    char abbr[TZ_ABBR_MAX];
} tz_info_t;

typedef struct {
    int32_t year;  /* e.g. 2026 */
    uint8_t month; /* 1-12 */
    uint8_t day;   /* 1-31 */
    uint8_t hour, min, sec;
    uint8_t wday;  /* 0 = Sunday */
    uint16_t yday; /* 0-365 */
} tz_civil_t;

typedef enum { TZ_RULE_JULIAN1 = 0, TZ_RULE_JULIAN0, TZ_RULE_MONTH } tz_rule_kind_t;

typedef struct {
    tz_rule_kind_t kind;
    uint16_t day;            /* Jn: 1-365 (no Feb 29); n: 0-365 */
    uint8_t mon, week, wday; /* Mm.w.d */
    int32_t time;            /* seconds after local midnight, -167 h to 167 h */
} tz_rule_t;

typedef struct {
    tz_info_t std, dst;
    bool has_dst;
    tz_rule_t start, end; /* start in standard time, end in daylight time */
} tz_posix_t;

typedef struct {
    const uint8_t *times; /* timecnt big-endian int64 */
    const uint8_t *idx;   /* timecnt type indices */
    const uint8_t *types; /* typecnt 6-byte records */
    const uint8_t *chars; /* charcnt designation bytes */
    uint32_t timecnt, typecnt, charcnt;
    bool has_footer;
    tz_posix_t footer;
} tz_tzif_t;

/* Parse a POSIX TZ string of `len` bytes. False on anything malformed. */
bool tz_posix_parse(tz_posix_t *z, const char *s, uint32_t len);
void tz_posix_lookup(const tz_posix_t *z, int64_t t, tz_info_t *out);

/* Parse a TZif file. The view points into `data`, which must outlive it. */
bool tz_tzif_parse(tz_tzif_t *z, const uint8_t *data, uint32_t len);
void tz_tzif_lookup(const tz_tzif_t *z, int64_t t, tz_info_t *out);

/* The built-in table: the POSIX string for an IANA name such as
 * "Europe/Paris", or 0 when the name is unknown. */
const char *tz_builtin_find(const char *name);
uint32_t tz_builtin_count(void);
const char *tz_builtin_name(uint32_t i);
const char *tz_builtin_version(void); /* the tzdata release, e.g. "2026c" */

/* Calendar fields of `t` shifted by `utoff`. False outside the supported
 * range (TZ_MIN_UNIX to TZ_MAX_UNIX). */
bool tz_civil(int64_t t, int32_t utoff, tz_civil_t *out);
/* Days from 1970-01-01 to a calendar date; years 1901-5999. */
int32_t tz_days_from_civil(int32_t y, uint32_t m, uint32_t d);

#endif /* ZXV_TZ_H */
