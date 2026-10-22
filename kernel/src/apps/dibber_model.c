/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dibber_model.c — Dibber (see dibber_model.h). */
#include <string.h>

#include "dibber_model.h"

#define DIBBER_LINE_MAX 80u /* one line of the file */

int dibber_init(dibber_t *d, zxm_link_t *link, int64_t plot)
{
    if (!d) return ZXM_E_ARG;
    memset(d, 0, sizeof *d);
    d->link = link;
    if (plot < 1 || plot > DIBBER_PLOT_MAX) return ZXM_E_ARG;
    d->plot = plot;
    return ZXM_OK;
}

/* ---- text, bounded ---- */

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (n < cap && s[n]) n++;
    return n;
}

static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = len_of(s, cap);
    if (*at + n >= cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

static bool put_i64(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char t[24];
    uint32_t k = 0;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    do {
        t[k++] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u);
    if (v < 0) t[k++] = '-';
    if (*at + k >= cap) return false;
    while (k) out[(*at)++] = t[--k];
    out[*at] = 0;
    return true;
}

/* tenths as "d.d", floored tenths in */
static bool put_tenths(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    char frac[2] = {(char) ('0' + (int) (u % 10u)), 0};
    return (v >= 0 || put(out, cap, at, "-")) && put_i64(out, cap, at, (int64_t) (u / 10u)) &&
           put(out, cap, at, ".") && put(out, cap, at, frac);
}

/* the next whole number of s[*i..n), skipping spaces; false at none */
static bool next_num(const char *s, uint32_t n, uint32_t *i, int64_t *v)
{
    uint32_t k = *i, digits = 0;
    bool neg = false;
    int64_t x = 0;
    while (k < n && s[k] == ' ') k++;
    if (k < n && s[k] == '-') neg = true, k++;
    while (k < n && s[k] >= '0' && s[k] <= '9') {
        if (++digits > 18u) return false;
        x = x * 10 + (s[k] - '0');
        k++;
    }
    if (!digits || (k < n && s[k] != ' ')) return false;
    *v = neg ? -x : x;
    *i = k;
    return true;
}

/* ---- DM1: the plot and its beds ---- */

const dibber_bed_t *dibber_find_bed(const dibber_t *d, int64_t bed)
{
    for (uint32_t i = 0; d && i < d->nbeds; i++)
        if (d->bed[i].bed == bed) return &d->bed[i];
    return NULL;
}

int dibber_add_bed(dibber_t *d, int64_t bed, int64_t area)
{
    if (!d || !d->plot || !dibber_plot_record_ok(d->plot, bed, area) || bed < 1 || area < 1 ||
        area > DIBBER_AREA_MAX)
        return ZXM_E_ARG;
    if (dibber_find_bed(d, bed) || d->nbeds >= DIBBER_BEDS) return ZXM_E_STATE;
    d->bed[d->nbeds].bed = bed;
    d->bed[d->nbeds].area = area;
    d->nbeds++;
    d->dirty = true;
    return ZXM_OK;
}

/* ---- DM2: plantings ---- */

int dibber_plant(dibber_t *d, int64_t bed, int64_t year, int64_t family)
{
    static int64_t b[DIBBER_CROPS + 1u], y[DIBBER_CROPS + 1u], f[DIBBER_CROPS + 1u];
    if (!d || year < 1 || year > DIBBER_YEAR_MAX || family < 1 || family > DIBBER_FAMILY_MAX)
        return ZXM_E_ARG;
    if (!dibber_find_bed(d, bed) || d->ncrops >= DIBBER_CROPS) return ZXM_E_STATE;
    for (uint32_t i = 0; i < d->ncrops; i++) {
        if (d->crop[i].bed == bed && d->crop[i].year == year) return ZXM_E_STATE; /* one a year */
        b[i] = d->crop[i].bed;
        y[i] = d->crop[i].year;
        f[i] = d->crop[i].family;
    }
    b[d->ncrops] = bed;
    y[d->ncrops] = year;
    f[d->ncrops] = family;
    if (dibber_family_repeats(b, y, f, d->ncrops + 1u, d->ncrops) != 0) return ZXM_E_STATE;
    d->crop[d->ncrops].bed = bed;
    d->crop[d->ncrops].year = year;
    d->crop[d->ncrops].family = family;
    d->ncrops++;
    d->dirty = true;
    return ZXM_OK;
}

/* ---- DM3: soil ---- */

static bool day_ok(int64_t day)
{
    return day >= 0 && day <= SOLSTICE_DAY_MAX;
}

int dibber_soil(dibber_t *d, int64_t bed, int64_t day, int64_t pct)
{
    if (!d || !day_ok(day) || pct < 0 || pct > 100) return ZXM_E_ARG;
    if (!dibber_find_bed(d, bed) || d->nsoil >= DIBBER_SOILS) return ZXM_E_STATE;
    d->soil[d->nsoil].bed = bed;
    d->soil[d->nsoil].day = day;
    d->soil[d->nsoil].pct = pct;
    d->nsoil++;
    d->dirty = true;
    return ZXM_OK;
}

/* the readings of a bed on a day, in the order taken */
static uint32_t soil_of(const dibber_t *d, int64_t bed, int64_t day, int64_t *pct)
{
    uint32_t k = 0;
    for (uint32_t i = 0; d && i < d->nsoil; i++)
        if (d->soil[i].bed == bed && d->soil[i].day == day) pct[k++] = d->soil[i].pct;
    return k;
}

lpres_state_t dibber_soil_on(const dibber_t *d, int64_t bed, int64_t day)
{
    int64_t pct[DIBBER_SOILS];
    uint32_t n = soil_of(d, bed, day, pct);
    return dibber_soil_state(pct, n);
}

/* ---- DM4: water ---- */

int dibber_set_budget(dibber_t *d, int64_t centilitres)
{
    if (!d || centilitres < 0 || centilitres > DIBBER_BUDGET_MAX) return ZXM_E_ARG;
    d->budget = centilitres;
    d->dirty = true;
    return ZXM_OK;
}

int dibber_log_water(dibber_t *d, int64_t bed, int64_t day, int64_t centilitres)
{
    if (!d || !day_ok(day) || centilitres < 1 || centilitres > DIBBER_BUDGET_MAX) return ZXM_E_ARG;
    if (!dibber_find_bed(d, bed) || d->nwater >= DIBBER_WATERINGS) return ZXM_E_STATE;
    d->water[d->nwater].bed = bed;
    d->water[d->nwater].day = day;
    d->water[d->nwater].cl = centilitres;
    d->nwater++;
    d->dirty = true;
    return ZXM_OK;
}

int64_t dibber_week_used(const dibber_t *d, int64_t day)
{
    int64_t amount[DIBBER_WEEK] = {0}, total[DIBBER_WEEK];
    if (!d || !day_ok(day)) return DIBBER_REFUSED;
    for (uint32_t i = 0; i < d->nwater; i++) { /* each day at most 256 * 10^12: no overflow */
        int64_t back = day - d->water[i].day;
        if (back >= 0 && back < DIBBER_WEEK) amount[DIBBER_WEEK - 1 - back] += d->water[i].cl;
    }
    if (understory_rolling(amount, DIBBER_WEEK, DIBBER_WEEK, total)) return DIBBER_REFUSED;
    return total[DIBBER_WEEK - 1];
}

/* the reading's flags (weatherglass.forecast_ok, then warn_flags), or -1 */
static int32_t reading_flags(const dibber_reading_t *r)
{
    if (!r || r->stamp < 1 || !weatherglass_reading_ok(r->stamp, r->temp, r->wind, r->rain) ||
        r->rain < 0 || r->rain > WEATHERGLASS_RAIN_MAX)
        return -1;
    int32_t f = weatherglass_warnings(r->temp, r->wind);
    return f == WEATHERGLASS_REFUSED ? -1 : f;
}

int64_t dibber_bed_week_cl(const dibber_t *d, int64_t bed, int64_t depth)
{
    const dibber_bed_t *b = dibber_find_bed(d, bed);
    int64_t num = 0, den = 1;
    if (!b || !dibber_bed_litres(b->area, depth, &num, &den)) return DIBBER_REFUSED;
    return num * (100 / den); /* den divides 100: exact */
}

int dibber_may_water(const dibber_t *d, int64_t bed, int64_t day, int64_t depth, int64_t times,
                     const dibber_reading_t *r)
{
    if (!d || !r || !day_ok(day)) return ZXM_E_ARG;
    int32_t flags = reading_flags(r);
    if (flags < 0) return ZXM_E_ARG;
    if (!dibber_find_bed(d, bed)) return ZXM_E_STATE;
    if (!dibber_watering_limit_ok(depth, times)) return DIBBER_PAST_LIMITS;
    int64_t now = dibber_bed_week_cl(d, bed, depth), used = dibber_week_used(d, day);
    if (now == DIBBER_REFUSED || used == DIBBER_REFUSED) return ZXM_E_STATE;
    int64_t excess = used + now - d->budget; /* each at most 10^12-scale: no overflow */
    int64_t frost = (flags & WEATHERGLASS_FROST) ? 1 : 0;
    if (dibber_water_now_ok(excess, frost)) return DIBBER_MAY_WATER;
    if (frost) return DIBBER_FROST;
    return understory_over_budget(excess) ? DIBBER_OVER_BUDGET : ZXM_E_STATE;
}

/* ---- DM5: the bed sheet ---- */

int dibber_sheet_total(const dibber_t *d, int64_t lo, int64_t hi, int64_t depth, int64_t *total)
{
    int64_t pos[DIBBER_BEDS], l[DIBBER_BEDS], h[DIBBER_BEDS], cell[DIBBER_BEDS];
    if (!d || !total || lo > hi) return ZXM_E_ARG;
    for (uint32_t i = 0; i < d->nbeds; i++) {
        pos[i] = d->bed[i].bed;
        l[i] = lo;
        h[i] = hi;
        cell[i] = dibber_bed_week_cl(d, d->bed[i].bed, depth);
        if (cell[i] == DIBBER_REFUSED) return ZXM_E_ARG;
    }
    return furrow_range_total(pos, l, h, cell, d->nbeds, total) ? ZXM_OK : ZXM_E_STATE;
}

int dibber_supply_total(const int64_t *count, const int64_t *price, uint32_t n, int64_t *total)
{
    int64_t pos[DIBBER_SUPPLIES], l[DIBBER_SUPPLIES], h[DIBBER_SUPPLIES], cell[DIBBER_SUPPLIES];
    if (!count || !price || !total || n > DIBBER_SUPPLIES) return ZXM_E_ARG;
    for (uint32_t i = 0; i < n; i++) {
        pos[i] = (int64_t) i + 1;
        l[i] = 1;
        h[i] = (int64_t) n;
        cell[i] = dibber_supply_cost(count[i], price[i]);
        if (cell[i] == DIBBER_REFUSED) return ZXM_E_ARG;
    }
    return furrow_range_total(pos, l, h, cell, n, total) ? ZXM_OK : ZXM_E_STATE;
}

/* ---- DM6: the suggestion ---- */

int dibber_suggest(const dibber_t *d, int64_t bed, int64_t day, const dibber_reading_t *r,
                   char *notice, uint32_t cap)
{
    int64_t pct[DIBBER_SOILS];
    uint32_t at = 0;
    if (!d || !notice || cap < DIBBER_NOTICE_MAX || !day_ok(day)) return ZXM_E_ARG;
    notice[0] = 0;
    int32_t flags = reading_flags(r);
    if (flags < 0) return ZXM_E_ARG;
    if (!dibber_find_bed(d, bed)) return ZXM_E_STATE;
    uint32_t n = soil_of(d, bed, day, pct);
    lpres_state_t s = dibber_soil_state(pct, n);
    int rc;
    bool ok = put(notice, cap, &at, "Bed ") && put_i64(notice, cap, &at, bed) &&
              put(notice, cap, &at, ": ");
    if (s == LPRES_STATE_BOTH) {
        rc = DIBBER_HELD;
        ok = ok && put(notice, cap, &at, "held: the soil reads");
        for (uint32_t i = 0; ok && i < n; i++)
            ok = put(notice, cap, &at, i ? (i + 1u == n ? " and " : ", ") : " ") &&
                 put_i64(notice, cap, &at, pct[i]) && put(notice, cap, &at, "%");
        ok = ok && put(notice, cap, &at, " on the same day; re-test the soil before planting.");
    } else if (s == LPRES_STATE_NEITHER) {
        rc = DIBBER_TEST_SOIL;
        ok = ok && put(notice, cap, &at, "no soil reading today; test the soil.");
    } else if (s == LPRES_STATE_FALSE) {
        rc = DIBBER_WATER_FIRST;
        ok = ok && put(notice, cap, &at, "too dry to sow; water first.");
    } else if (flags & WEATHERGLASS_FROST) {
        int64_t num = 0, den = 1;
        rc = DIBBER_WAIT_FROST;
        ok = ok && !weatherglass_fahrenheit(r->temp, &num, &den) &&
             put(notice, cap, &at, "frost warned (") && put_tenths(notice, cap, &at, r->temp) &&
             put(notice, cap, &at, " C, ") &&
             put_tenths(notice, cap, &at, weatherglass_floor(num, den)) &&
             put(notice, cap, &at, " F); wait to sow.");
    } else {
        rc = DIBBER_SOW;
        ok = ok && put(notice, cap, &at, "moist enough and no frost; it may be sown.");
    }
    ok = ok && put(notice, cap, &at, " Advice only.");
    if (!ok) {
        notice[0] = 0;
        return ZXM_E_TOOBIG;
    }
    return rc;
}

/* ---- DM7: dates ---- */

int64_t dibber_next_task(int64_t start, int64_t every, int64_t today)
{
    return solstice_next(start, every, today);
}

int64_t dibber_harvest(const int64_t *mean, uint32_t n, int64_t base, int64_t need)
{
    return dibber_harvest_days(mean, n, base, need);
}

/* ---- DM8: the file ---- */

static bool put_line(char *out, uint32_t cap, uint32_t *at, const char *tag, const int64_t *x,
                     uint32_t n)
{
    if (!put(out, cap, at, tag)) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!put(out, cap, at, " ") || !put_i64(out, cap, at, x[i])) return false;
    return put(out, cap, at, "\n");
}

int dibber_save(dibber_t *d, bool owner_confirmed)
{
    static char text[DIBBER_TEXT_MAX];
    uint32_t at = 0;
    if (!d || !d->link || !d->plot) return ZXM_E_ARG;
    text[0] = 0;
    int64_t p[2] = {d->plot, d->budget};
    bool ok =
        put(text, sizeof text, &at, "dibber 1\n") && put_line(text, sizeof text, &at, "p", p, 2);
    for (uint32_t i = 0; ok && i < d->nbeds; i++) {
        int64_t x[2] = {d->bed[i].bed, d->bed[i].area};
        ok = put_line(text, sizeof text, &at, "b", x, 2);
    }
    for (uint32_t i = 0; ok && i < d->ncrops; i++) {
        int64_t x[3] = {d->crop[i].bed, d->crop[i].year, d->crop[i].family};
        ok = put_line(text, sizeof text, &at, "c", x, 3);
    }
    for (uint32_t i = 0; ok && i < d->nsoil; i++) {
        int64_t x[3] = {d->soil[i].bed, d->soil[i].day, d->soil[i].pct};
        ok = put_line(text, sizeof text, &at, "s", x, 3);
    }
    for (uint32_t i = 0; ok && i < d->nwater; i++) {
        int64_t x[3] = {d->water[i].bed, d->water[i].day, d->water[i].cl};
        ok = put_line(text, sizeof text, &at, "w", x, 3);
    }
    if (!ok) return ZXM_E_TOOBIG;
    if (!owner_confirmed) return ZXM_E_STATE;
    zxm_begin(d->link, "files.write");
    zxm_p_str(d->link, "path", DIBBER_FILE);
    zxm_p_strn(d->link, "text", text, at);
    int rc = zxm_call(d->link);
    if (rc) return rc;
    d->dirty = false;
    return ZXM_OK;
}

/* one line of the file into what is being read; false when it does not fit */
static bool parse_line(dibber_t *into, char tag, const int64_t *x)
{
    switch (tag) {
    case 'b':
        return dibber_add_bed(into, x[0], x[1]) == ZXM_OK;
    case 'c':
        return dibber_plant(into, x[0], x[1], x[2]) == ZXM_OK;
    case 's':
        return dibber_soil(into, x[0], x[1], x[2]) == ZXM_OK;
    case 'w':
        return dibber_log_water(into, x[0], x[1], x[2]) == ZXM_OK;
    default:
        return false;
    }
}

static int parse(dibber_t *into, const char *text, uint32_t n)
{
    uint32_t i = 0, line = 0;
    while (i < n) {
        uint32_t e = i;
        while (e < n && text[e] != '\n') e++;
        const char *l = text + i;
        uint32_t ln = e - i, k = 1;
        i = e + 1u;
        if (ln > DIBBER_LINE_MAX) return ZXM_E_STATE;
        if (line++ == 0) {
            if (ln != 8u || memcmp(l, "dibber 1", 8)) return ZXM_E_STATE;
            continue;
        }
        if (!ln) continue;
        if (ln < 2u || l[1] != ' ') return ZXM_E_STATE;
        int64_t x[3];
        uint32_t want = l[0] == 'p' || l[0] == 'b' ? 2u : 3u;
        for (uint32_t f = 0; f < want; f++)
            if (!next_num(l, ln, &k, &x[f])) return ZXM_E_STATE;
        if (k != ln) return ZXM_E_STATE;
        if (l[0] == 'p') { /* the plot comes once, first */
            if (line != 2u || dibber_init(into, into->link, x[0]) || dibber_set_budget(into, x[1]))
                return ZXM_E_STATE;
            continue;
        }
        if (line == 2u || !parse_line(into, l[0], x)) return ZXM_E_STATE;
    }
    return into->plot ? ZXM_OK : ZXM_E_STATE;
}

int dibber_load(dibber_t *d)
{
    static char text[DIBBER_TEXT_MAX + 1u];
    static dibber_t got;
    bool eof = false;
    if (!d || !d->link) return ZXM_E_ARG;
    zxm_link_t *l = d->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", DIBBER_FILE);
    zxm_p_u64(l, "max", DIBBER_TEXT_MAX);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "eof", &eof) ||
        zxm_get_str(l, l->result, "text", text, sizeof text))
        return ZXM_E_PROTOCOL;
    if (!eof) return ZXM_E_TOOBIG;
    memset(&got, 0, sizeof got);
    got.link = d->link;
    rc = parse(&got, text, len_of(text, DIBBER_TEXT_MAX));
    if (rc) return rc;              /* what is open stays as it was */
    got.here_known = d->here_known; /* where the plot is stays */
    got.here_lat = d->here_lat;
    got.here_lon = d->here_lon;
    got.dirty = false;
    *d = got;
    return ZXM_OK;
}

int dibber_here(dibber_t *d)
{
    int64_t lat = 0, lon = 0;
    bool known = false;
    if (!d || !d->link) return ZXM_E_ARG;
    zxm_link_t *l = d->link;
    zxm_begin(l, "place.here");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "known", &known)) return ZXM_E_PROTOCOL;
    if (known && (zxm_get_i64(l, l->result, "lat", &lat) ||
                  zxm_get_i64(l, l->result, "lon", &lon) || !weatherglass_place_ok(lat, lon)))
        return ZXM_E_PROTOCOL;
    d->here_known = known;
    d->here_lat = known ? lat : 0;
    d->here_lon = known ? lon : 0;
    return ZXM_OK;
}
