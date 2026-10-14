/* tz.c — time zones: UTC to local time. See tz.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tz.h"
#include "tz_zones.h"

#define DAY_S        86400
#define BASE_DAYS    (-25203) /* 1900-12-31, a day before the supported range */
#define MAX_RULE_S   (167 * 3600)
#define MAX_OFFSET_S (25 * 3600)

/* ---- calendar arithmetic (Hinnant's algorithms), 32-bit and division by
 * constants only, so 32-bit targets call no helper */

int32_t tz_days_from_civil(int32_t y, uint32_t m, uint32_t d)
{
    y -= m <= 2;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    int32_t yoe = y - era * 400;
    int32_t doy = (153 * (int32_t) (m > 2 ? m - 3 : m + 9) + 2) / 5 + (int32_t) d - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int32_t z, int32_t *y, uint32_t *m, uint32_t *d)
{
    z += 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    int32_t doe = z - era * 146097;
    int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int32_t mp = (5 * doy + 2) / 153;
    *d = (uint32_t) (doy - (153 * mp + 2) / 5 + 1);
    *m = (uint32_t) (mp < 10 ? mp + 3 : mp - 9);
    *y = yoe + era * 400 + (*m <= 2);
}

static uint32_t weekday(int32_t days)
{
    return (uint32_t) (((days % 7) + 11) % 7); /* 1970-01-01 was a Thursday */
}

static bool is_leap(int32_t y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static uint32_t month_len(int32_t y, uint32_t m)
{
    static const uint8_t len[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return len[m - 1] + (m == 2 && is_leap(y));
}

/* floor(t / 86400) and the remainder, for t within the supported range */
static bool split_days(int64_t t, int32_t *days, int32_t *sod)
{
    if (t < TZ_MIN_UNIX - 2 * DAY_S || t > TZ_MAX_UNIX + 2 * DAY_S) return false;
    uint64_t u = (uint64_t) (t - (int64_t) BASE_DAYS * DAY_S);
    uint32_t rel = (uint32_t) (u >> 7) / 675u; /* 86400 = 128 * 675 */
    *days = BASE_DAYS + (int32_t) rel;
    *sod = (int32_t) (u - (uint64_t) rel * DAY_S);
    return true;
}

bool tz_civil(int64_t t, int32_t utoff, tz_civil_t *out)
{
    if (t < TZ_MIN_UNIX || t > TZ_MAX_UNIX) return false;
    int32_t days, sod;
    if (!split_days(t + utoff, &days, &sod)) return false;
    int32_t y;
    uint32_t m, d;
    civil_from_days(days, &y, &m, &d);
    out->year = y;
    out->month = (uint8_t) m;
    out->day = (uint8_t) d;
    out->hour = (uint8_t) (sod / 3600);
    out->min = (uint8_t) ((sod / 60) % 60);
    out->sec = (uint8_t) (sod % 60);
    out->wday = (uint8_t) weekday(days);
    out->yday = (uint16_t) (days - tz_days_from_civil(y, 1, 1));
    return true;
}

/* ---- POSIX TZ strings */

typedef struct {
    const char *s;
    uint32_t n, i;
} cur_t;

static int peek(const cur_t *c)
{
    return c->i < c->n ? (unsigned char) c->s[c->i] : -1;
}

static bool is_digit(int ch)
{
    return ch >= '0' && ch <= '9';
}

static bool is_alpha(int ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

static bool parse_num(cur_t *c, uint32_t max_digits, uint32_t max, uint32_t *out)
{
    uint32_t v = 0, k = 0;
    while (k < max_digits && is_digit(peek(c))) {
        v = v * 10 + (uint32_t) (c->s[c->i++] - '0');
        k++;
    }
    if (k == 0 || v > max || is_digit(peek(c))) return false;
    *out = v;
    return true;
}

static bool parse_name(cur_t *c, char out[TZ_ABBR_MAX])
{
    uint32_t k = 0;
    if (peek(c) == '<') {
        c->i++;
        for (;;) {
            int ch = peek(c);
            if (ch == '>') break;
            if (!(is_alpha(ch) || is_digit(ch) || ch == '+' || ch == '-')) return false;
            if (k + 1 >= TZ_ABBR_MAX) return false;
            out[k++] = (char) ch;
            c->i++;
        }
        c->i++;
    } else {
        while (is_alpha(peek(c))) {
            if (k + 1 >= TZ_ABBR_MAX) return false;
            out[k++] = c->s[c->i++];
        }
    }
    out[k] = 0;
    return k >= 3;
}

/* [+-]h[h][h][:mm[:ss]] in seconds */
static bool parse_hms(cur_t *c, uint32_t max_h, int32_t *out)
{
    int32_t sign = 1;
    if (peek(c) == '+' || peek(c) == '-') sign = c->s[c->i++] == '-' ? -1 : 1;
    uint32_t h, m = 0, s = 0;
    if (!parse_num(c, 3, max_h, &h)) return false;
    if (peek(c) == ':') {
        c->i++;
        if (!parse_num(c, 2, 59, &m)) return false;
        if (peek(c) == ':') {
            c->i++;
            if (!parse_num(c, 2, 59, &s)) return false;
        }
    }
    *out = sign * (int32_t) (h * 3600 + m * 60 + s);
    return true;
}

static bool parse_rule(cur_t *c, tz_rule_t *r)
{
    uint32_t a, b, d;
    r->time = 7200;
    r->mon = r->week = r->wday = 0;
    r->day = 0;
    if (peek(c) == 'J') {
        c->i++;
        if (!parse_num(c, 3, 365, &a) || a < 1) return false;
        r->kind = TZ_RULE_JULIAN1;
        r->day = (uint16_t) a;
    } else if (peek(c) == 'M') {
        c->i++;
        if (!parse_num(c, 2, 12, &a) || a < 1 || peek(c) != '.') return false;
        c->i++;
        if (!parse_num(c, 1, 5, &b) || b < 1 || peek(c) != '.') return false;
        c->i++;
        if (!parse_num(c, 1, 6, &d)) return false;
        r->kind = TZ_RULE_MONTH;
        r->mon = (uint8_t) a;
        r->week = (uint8_t) b;
        r->wday = (uint8_t) d;
    } else {
        if (!parse_num(c, 3, 365, &a)) return false;
        r->kind = TZ_RULE_JULIAN0;
        r->day = (uint16_t) a;
    }
    if (peek(c) == '/') {
        c->i++;
        if (!parse_hms(c, 167, &r->time)) return false;
    }
    return true;
}

/* Every accepted zone keeps both offsets within 25 hours, whichever way
 * the string ends: a DST zone with no rules takes std + 1 h by default,
 * which can pass the limit even when std alone does not. */
static bool offsets_ok(const tz_posix_t *z)
{
    return z->std.utoff >= -MAX_OFFSET_S && z->std.utoff <= MAX_OFFSET_S &&
           z->dst.utoff >= -MAX_OFFSET_S && z->dst.utoff <= MAX_OFFSET_S;
}

bool tz_posix_parse(tz_posix_t *z, const char *s, uint32_t len)
{
    cur_t c = {s, len, 0};
    int32_t off;
    if (!s || !parse_name(&c, z->std.abbr) || !parse_hms(&c, 24, &off)) return false;
    z->std.utoff = -off;
    z->std.is_dst = false;
    z->has_dst = false;
    z->dst.abbr[0] = 0;
    z->dst.utoff = z->std.utoff;
    z->dst.is_dst = false;
    if (c.i == c.n) return offsets_ok(z);

    if (!parse_name(&c, z->dst.abbr)) return false;
    z->has_dst = true;
    z->dst.is_dst = true;
    z->dst.utoff = z->std.utoff + 3600;
    if (peek(&c) != ',' && c.i < c.n) {
        if (!parse_hms(&c, 24, &off)) return false;
        z->dst.utoff = -off;
    }
    if (c.i == c.n) {
        /* no rules: the POSIX default, as tzcode applies it */
        cur_t d = {"M3.2.0,M11.1.0", 14, 0};
        parse_rule(&d, &z->start);
        d.i++;
        parse_rule(&d, &z->end);
        return offsets_ok(z);
    }
    if (peek(&c) != ',') return false;
    c.i++;
    if (!parse_rule(&c, &z->start) || peek(&c) != ',') return false;
    c.i++;
    if (!parse_rule(&c, &z->end)) return false;
    return c.i == c.n && offsets_ok(z);
}

/* The rule's moment in year y, as seconds since the epoch on the local
 * clock it is written against. */
static int64_t rule_local(int32_t y, const tz_rule_t *r)
{
    int32_t days = tz_days_from_civil(y, 1, 1);
    if (r->kind == TZ_RULE_JULIAN1) {
        days += r->day - 1 + (is_leap(y) && r->day >= 60);
    } else if (r->kind == TZ_RULE_JULIAN0) {
        days += r->day;
    } else {
        int32_t first = tz_days_from_civil(y, r->mon, 1);
        uint32_t day = 1 + (r->wday + 7 - weekday(first)) % 7 + (uint32_t) (r->week - 1) * 7;
        while (day > month_len(y, r->mon)) day -= 7;
        days = first + (int32_t) day - 1;
    }
    return (int64_t) days * DAY_S + r->time;
}

static void copy_info(tz_info_t *dst, const tz_info_t *src)
{
    dst->utoff = src->utoff;
    dst->is_dst = src->is_dst;
    for (uint32_t i = 0; i < TZ_ABBR_MAX; i++) dst->abbr[i] = src->abbr[i];
}

void tz_posix_lookup(const tz_posix_t *z, int64_t t, tz_info_t *out)
{
    bool dst = false;
    int32_t days, sod;
    if (z->has_dst && split_days(t + z->std.utoff, &days, &sod)) {
        int32_t y;
        uint32_t m, d;
        civil_from_days(days, &y, &m, &d);
        int64_t s = rule_local(y, &z->start) - z->std.utoff;
        int64_t e = rule_local(y, &z->end) - z->dst.utoff;
        if (s < e)
            dst = t >= s && t < e;
        else
            dst = !(t >= e && t < s); /* southern hemisphere: DST spans new year */
    }
    copy_info(out, dst ? &z->dst : &z->std);
}

/* ---- TZif (RFC 8536) */

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static int64_t be64(const uint8_t *p)
{
    return (int64_t) (((uint64_t) be32(p) << 32) | be32(p + 4));
}

bool tz_tzif_parse(tz_tzif_t *z, const uint8_t *data, uint32_t len)
{
    if (!data || len < 44 || data[0] != 'T' || data[1] != 'Z' || data[2] != 'i' || data[3] != 'f')
        return false;
    if (data[4] < '2' || data[4] > '4') return false; /* version 1 has no 64-bit data */

    /* skip the version 1 block */
    uint64_t c[6];
    for (uint32_t i = 0; i < 6; i++) c[i] = be32(data + 20 + 4 * i);
    uint64_t v1 = 44 + c[3] * 5 + c[4] * 6 + c[5] + c[2] * 8 + c[1] + c[0];
    if (v1 + 44 > len) return false;
    const uint8_t *h = data + v1;
    if (h[0] != 'T' || h[1] != 'Z' || h[2] != 'i' || h[3] != 'f' || h[4] != data[4]) return false;

    uint32_t isut = be32(h + 20), isstd = be32(h + 24), leap = be32(h + 28);
    uint32_t timecnt = be32(h + 32), typecnt = be32(h + 36), charcnt = be32(h + 40);
    if (leap != 0) return false; /* "right/" zones count leap seconds: not Unix time */
    if (typecnt == 0 || typecnt > 256 || charcnt == 0 || (isut && isut != typecnt) ||
        (isstd && isstd != typecnt) || timecnt > 100000 || charcnt > 65536)
        return false;
    uint64_t body = (uint64_t) timecnt * 9 + (uint64_t) typecnt * 6 + charcnt + isstd + isut;
    if (v1 + 44 + body > len) return false;

    const uint8_t *p = h + 44;
    z->times = p;
    z->idx = p + timecnt * 8;
    z->types = z->idx + timecnt;
    z->chars = z->types + typecnt * 6;
    z->timecnt = timecnt;
    z->typecnt = typecnt;
    z->charcnt = charcnt;

    for (uint32_t i = 0; i < timecnt; i++) {
        if (z->idx[i] >= typecnt) return false;
        if (i && be64(z->times + 8 * i) <= be64(z->times + 8 * (i - 1))) return false;
    }
    for (uint32_t i = 0; i < typecnt; i++) {
        const uint8_t *ty = z->types + 6 * i;
        int32_t off = (int32_t) be32(ty);
        if (off < -MAX_OFFSET_S || off > MAX_OFFSET_S || ty[4] > 1 || ty[5] >= charcnt)
            return false;
        uint32_t k = ty[5];
        while (k < charcnt && z->chars[k]) k++;
        if (k == charcnt) return false; /* designation not terminated */
    }

    /* footer: "\n" TZ-string "\n" */
    uint64_t at = v1 + 44 + body;
    z->has_footer = false;
    if (at >= len || data[at] != '\n') return false; /* the footer is mandatory */
    uint64_t end = at + 1;
    while (end < len && data[end] != '\n') end++;
    if (end >= len || end + 1 != len) return false;
    if (end == at + 1) return true; /* empty footer */
    if (!tz_posix_parse(&z->footer, (const char *) data + at + 1, (uint32_t) (end - at - 1)))
        return false;
    z->has_footer = true;
    return true;
}

static void tzif_type(const tz_tzif_t *z, uint32_t i, tz_info_t *out)
{
    const uint8_t *ty = z->types + 6 * i;
    out->utoff = (int32_t) be32(ty);
    out->is_dst = ty[4] != 0;
    uint32_t k = 0;
    for (uint32_t j = ty[5]; j < z->charcnt && z->chars[j] && k + 1 < TZ_ABBR_MAX; j++)
        out->abbr[k++] = (char) z->chars[j];
    out->abbr[k] = 0;
}

void tz_tzif_lookup(const tz_tzif_t *z, int64_t t, tz_info_t *out)
{
    uint32_t n = z->timecnt;
    if (n == 0 || t >= be64(z->times + 8 * (n - 1))) {
        if (z->has_footer) {
            tz_posix_lookup(&z->footer, t, out);
            return;
        }
        tzif_type(z, n ? z->idx[n - 1] : 0, out);
        return;
    }
    if (t < be64(z->times)) {
        tzif_type(z, 0, out); /* RFC 8536 3.2: before the first transition */
        return;
    }
    uint32_t lo = 0, hi = n - 1; /* times[lo] <= t < times[hi] */
    while (hi - lo > 1) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (be64(z->times + 8 * mid) <= t)
            lo = mid;
        else
            hi = mid;
    }
    tzif_type(z, z->idx[lo], out);
}

/* ---- the built-in table */

static int name_cmp(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return (unsigned char) *a - (unsigned char) *b;
}

const char *tz_builtin_find(const char *name)
{
    if (!name) return 0;
    uint32_t lo = 0, hi = TZ_ZONE_COUNT;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int c = name_cmp(name, tz_zones[mid].name);
        if (c == 0) return tz_zones[mid].tz;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return 0;
}

uint32_t tz_builtin_count(void)
{
    return TZ_ZONE_COUNT;
}

const char *tz_builtin_name(uint32_t i)
{
    return i < TZ_ZONE_COUNT ? tz_zones[i].name : 0;
}

const char *tz_builtin_version(void)
{
    return TZ_ZONES_VERSION;
}
