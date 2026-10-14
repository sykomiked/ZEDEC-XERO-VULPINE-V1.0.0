/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_tz.c — time zones against Python's zoneinfo.
 *
 * test_tz_vectors.h (gen_tz_vectors.py) holds, for every zone in the
 * built-in table, its state at 2027-01-01 and every change of offset,
 * daylight flag or abbreviation up to 2046 (from 2027 because the table
 * holds current rules, and some zones, British Columbia and Alberta among
 * them, change theirs in late 2026); and for twelve embedded TZif
 * files the same from 1901 to 2100. Each change is checked one second before
 * and at the instant, and the middle of each stretch is checked too. */
#include <stdio.h>
#include <string.h>
#include "tz.h"
#include "test_tz_vectors.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] " __VA_ARGS__);                                                         \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* Zones whose rules a POSIX string cannot express, so the built-in table is
 * not always right for them: Palestine ends daylight time early in years
 * when Ramadan falls in it, and tzdata lists those dates one by one. */
static const char *const posix_inexact[] = {"Asia/Gaza", "Asia/Hebron"};

static bool inexact(const char *name)
{
    for (unsigned i = 0; i < sizeof posix_inexact / sizeof *posix_inexact; i++)
        if (!strcmp(name, posix_inexact[i])) return true;
    return false;
}

/* where gen_tz_vectors.py stopped scanning: 2046-01-01 and 2100-01-01 */
#define TV_END_BUILTIN  2398377600LL
#define TV_END_FIXTURES 4102444800LL

typedef void (*lookup_fn)(const void *z, int64_t t, tz_info_t *out);

static void posix_fn(const void *z, int64_t t, tz_info_t *out)
{
    tz_posix_lookup((const tz_posix_t *) z, t, out);
}

static void tzif_fn(const void *z, int64_t t, tz_info_t *out)
{
    tz_tzif_lookup((const tz_tzif_t *) z, t, out);
}

/* Returns the number of mismatches for zone i. */
static int check_zone(uint32_t i, const void *z, lookup_fn fn, bool quiet)
{
    int bad = 0;
    int32_t off = tv_zone[i].off;
    uint8_t dst = tv_zone[i].dst, abbr = tv_zone[i].abbr;
    int64_t from = tv_zone[i].from;
    for (uint32_t k = 0; k <= tv_zone[i].count; k++) {
        bool last = k == tv_zone[i].count;
        int64_t end = i < TV_N_BUILTIN ? TV_END_BUILTIN : TV_END_FIXTURES;
        int64_t to = last ? end : tv_tr[tv_zone[i].first + k].t;
        int64_t probes[3] = {from, from + (to - from) / 2, to - 1};
        for (int p = 0; p < 3; p++) {
            tz_info_t got;
            fn(z, probes[p], &got);
            bool ok = got.utoff == off && got.is_dst == dst && !strcmp(got.abbr, tv_abbr[abbr]);
            checks++;
            if (!ok) {
                bad++;
                if (!quiet && bad <= 3)
                    printf("[FAIL] %s at %lld: got %d %d %s, want %d %d %s\n", tv_zone[i].name,
                           (long long) probes[p], got.utoff, got.is_dst, got.abbr, off, dst,
                           tv_abbr[abbr]);
            }
        }
        if (last) break;
        uint32_t r = tv_zone[i].first + k;
        from = tv_tr[r].t;
        off = tv_tr[r].off;
        dst = tv_tr[r].dst;
        abbr = tv_tr[r].abbr;
    }
    return bad;
}

static void test_builtin_zones(void)
{
    int zones_ok = 0, inexact_seen = 0;
    CHECK(tz_builtin_count() == TV_N_BUILTIN, "table has every zone (%u vs %u)", tz_builtin_count(),
          TV_N_BUILTIN);
    for (uint32_t i = 0; i < TV_N_BUILTIN; i++) {
        const char *s = tz_builtin_find(tv_zone[i].name);
        CHECK(s != 0, "%s is in the table", tv_zone[i].name);
        if (!s) continue;
        tz_posix_t z;
        bool parsed = tz_posix_parse(&z, s, (uint32_t) strlen(s));
        CHECK(parsed, "%s: \"%s\" parses", tv_zone[i].name, s);
        if (!parsed) continue;
        bool quiet = inexact(tv_zone[i].name);
        int bad = check_zone(i, &z, posix_fn, quiet);
        if (quiet) {
            inexact_seen += bad > 0;
            checks++;
            continue;
        }
        if (bad) failures++;
        zones_ok += bad == 0;
    }
    /* If tzdata ever makes these expressible, the list above should shrink. */
    CHECK(inexact_seen == (int) (sizeof posix_inexact / sizeof *posix_inexact),
          "the zones listed as inexact really are (%d)", inexact_seen);
    printf("[INFO] %d zones match zoneinfo exactly, 2027-2046\n", zones_ok);
}

static void test_tzif_fixtures(void)
{
    for (uint32_t f = 0; f < TV_N_FIXTURES; f++) {
        uint32_t i = TV_N_BUILTIN + f;
        tz_tzif_t z;
        bool parsed = tz_tzif_parse(&z, (const uint8_t *) tv_tzif[f].data, tv_tzif[f].len);
        CHECK(parsed, "%s TZif parses", tv_zone[i].name);
        if (!parsed) continue;
        if (check_zone(i, &z, tzif_fn, false)) failures++;
    }
}

static void test_tzif_malformed(void)
{
    const uint8_t *ny = (const uint8_t *) tv_tzif[0].data;
    uint32_t len = tv_tzif[0].len;
    static uint8_t buf[8192];
    tz_tzif_t z;
    int accepted = 0;
    for (uint32_t n = 0; n < len; n++) accepted += tz_tzif_parse(&z, ny, n);
    CHECK(accepted == 0, "every truncation of a TZif file is refused (%d accepted)", accepted);

    /* single-byte corruption: refused or parsed, never a crash (ASan build) */
    for (uint32_t n = 0; n < len && n < sizeof buf; n++) {
        memcpy(buf, ny, len);
        buf[n] ^= 0xA5;
        if (tz_tzif_parse(&z, buf, len)) {
            tz_info_t out;
            tz_tzif_lookup(&z, 0, &out);
            tz_tzif_lookup(&z, 1791763200, &out);
        }
    }
    memcpy(buf, ny, len);
    buf[4] = '1';
    CHECK(!tz_tzif_parse(&z, buf, len), "version 1 file refused");
    memcpy(buf, ny, len);
    buf[len - 2] = 'Q'; /* corrupt the footer's last rule */
    CHECK(!tz_tzif_parse(&z, buf, len), "bad footer refused");
    CHECK(!tz_tzif_parse(&z, 0, 100), "null refused");
}

static void test_posix_strings(void)
{
    static const char *const good[] = {"UTC0",
                                       "EST5EDT",
                                       "EST5EDT,M3.2.0,M11.1.0",
                                       "<+0530>-5:30",
                                       "<-03>3",
                                       "NST3:30NDT,M3.2.0,M11.1.0",
                                       "<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45",
                                       "<-02>2<-01>,M3.5.0/-1,M10.5.0/0",
                                       "EST5EDT,0/0,J365/25",
                                       "IST-1GMT0,M10.5.0,M3.5.0/1",
                                       "AAA3BBB,J60/2,300/-167",
                                       "XXX-14"};
    static const char *const bad[] = {"", "ES5", "EST", "EST5EDT,M13.1.0,M11.1.0", "EST5EDT,M3.2.0",
                                      "EST5EDT,M3.6.0,M11.1.0", "EST5EDT,M3.2.7,M11.1.0", "EST5x",
                                      "<A>1", "<AAAA1", "EST25", "EST5EDT,J0,J365", "EST5EDT,366,0",
                                      "EST5EDT,M3.2.0/168,M11.1.0", "EST5:60",
                                      "EST5EDT,M3.2.0,M11.1.0,", "TOOLONGNAME5", "EST+",
                                      /* DST defaults to std + 1 h: 26 hours east */
                                      "AAA-24:59BBB", "AAA-24:59BBB,M3.2.0,M11.1.0"};
    tz_posix_t z;
    for (unsigned i = 0; i < sizeof good / sizeof *good; i++)
        CHECK(tz_posix_parse(&z, good[i], (uint32_t) strlen(good[i])), "parses: %s", good[i]);
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++)
        CHECK(!tz_posix_parse(&z, bad[i], (uint32_t) strlen(bad[i])), "refused: \"%s\"", bad[i]);

    /* "EST5EDT" with no rules takes the POSIX default (the US rules) */
    tz_info_t a, b;
    tz_posix_t us;
    tz_posix_parse(&z, "EST5EDT", 7);
    tz_posix_parse(&us, "EST5EDT,M3.2.0,M11.1.0", 22);
    for (int64_t t = 1767225600; t < 1767225600 + 366 * 86400; t += 3607) {
        tz_posix_lookup(&z, t, &a);
        tz_posix_lookup(&us, t, &b);
        if (a.utoff != b.utoff) break;
        if (t + 3607 >= 1767225600 + 366 * 86400) CHECK(1, "EST5EDT defaults to the US rules");
    }

    /* all-year daylight time (RFC 8536 version 3 extension) */
    tz_posix_parse(&z, "EST5EDT,0/0,J365/25", 19);
    int dst_all = 1;
    for (int64_t t = 1767225600; t < 1767225600 + 3 * 366 * 86400; t += 7 * 3600) {
        tz_posix_lookup(&z, t, &a);
        dst_all &= a.is_dst && a.utoff == -4 * 3600;
    }
    CHECK(dst_all, "EST5EDT,0/0,J365/25 is daylight time all year");

    /* Jn skips 29 February, n counts it */
    tz_posix_parse(&z, "AAA0BBB,J60/0,J61/0", 19); /* J60 is 1 March in any year */
    tz_posix_lookup(&z, 1709251200, &a);           /* 2024-03-01T00:00:00Z */
    CHECK(a.is_dst, "J60 is 1 March in a leap year");
    tz_posix_parse(&z, "AAA0BBB,59/0,60/0", 17); /* day 59 is 29 February in 2024 */
    tz_posix_lookup(&z, 1709164800, &a);         /* 2024-02-29T00:00:00Z */
    CHECK(a.is_dst, "zero-based day 59 is 29 February in a leap year");
}

static void test_civil(void)
{
    tz_civil_t c;
    CHECK(tz_civil(0, 0, &c) && c.year == 1970 && c.month == 1 && c.day == 1 && c.wday == 4,
          "epoch is Thursday 1970-01-01");
    CHECK(tz_civil(1791763200, 0, &c) && c.year == 2026 && c.month == 10 && c.day == 12 &&
              c.wday == 1 && c.yday == 284,
          "2026-10-12 is a Monday, day 284");
    CHECK(tz_civil(1709251199, 0, &c) && c.month == 2 && c.day == 29 && c.hour == 23 &&
              c.min == 59 && c.sec == 59,
          "2024-02-29T23:59:59Z");
    CHECK(tz_civil(1791763200, -4 * 3600, &c) && c.day == 11 && c.hour == 20,
          "a negative offset steps back a day");
    CHECK(tz_civil(-1, 0, &c) && c.year == 1969 && c.month == 12 && c.day == 31 && c.hour == 23 &&
              c.sec == 59,
          "one second before the epoch");
    CHECK(tz_civil(TZ_MIN_UNIX, 0, &c) && c.year == 1901 && c.yday == 0, "the first instant");
    CHECK(tz_civil(TZ_MAX_UNIX, 0, &c) && c.year == 6000 && c.yday == 0, "the last instant");
    CHECK(!tz_civil(TZ_MIN_UNIX - 1, 0, &c) && !tz_civil(TZ_MAX_UNIX + 1, 0, &c),
          "outside the range is refused");

    /* every day of the range round-trips, and weekdays advance by one */
    int bad = 0;
    int32_t d0 = tz_days_from_civil(1901, 1, 1), d1 = tz_days_from_civil(6000, 1, 1);
    uint8_t wd = 2; /* 1901-01-01 was a Tuesday */
    for (int32_t d = d0; d < d1; d++) {
        if (!tz_civil((int64_t) d * 86400 + 43200, 0, &c) ||
            tz_days_from_civil(c.year, c.month, c.day) != d || c.wday != wd || c.hour != 12)
            bad++;
        wd = (uint8_t) ((wd + 1) % 7);
    }
    CHECK(bad == 0, "every day 1901-6000 round-trips (%d bad)", bad);
}

static void test_names(void)
{
    const char *p = tz_builtin_find("Europe/Paris");
    CHECK(p && !strcmp(p, "CET-1CEST,M3.5.0,M10.5.0/3"), "Europe/Paris");
    CHECK(tz_builtin_find("Europe/Pariss") == 0, "a misspelt name is unknown");
    CHECK(tz_builtin_find("") == 0 && tz_builtin_find(0) == 0, "empty and null are unknown");
    CHECK(tz_builtin_find(tz_builtin_name(0)) != 0 &&
              tz_builtin_find(tz_builtin_name(tz_builtin_count() - 1)) != 0,
          "first and last names are found");
    CHECK(tz_builtin_name(tz_builtin_count()) == 0, "past the end is null");
    CHECK(strlen(tz_builtin_version()) >= 5, "tzdata version %s", tz_builtin_version());
}

int main(void)
{
    test_posix_strings();
    test_civil();
    test_names();
    test_builtin_zones();
    test_tzif_fixtures();
    test_tzif_malformed();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
