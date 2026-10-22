/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sundial_model.c — Sundial, the clock (see sundial_model.h). */
#include <string.h>

#include "sundial_model.h"

#define MS_PER_MIN 60000

/* ---- text ------------------------------------------------------------------ */

/* Append s to out at *at, bounded by cap (room for a NUL is kept). */
static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = (uint32_t) strlen(s);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

/* v >= 0 in decimal, at least `width` digits (zeros in front). */
static bool put_num(char *out, uint32_t cap, uint32_t *at, int64_t v, int width)
{
    char t[24];
    int k = 23;
    uint64_t u = v < 0 ? 0u : (uint64_t) v;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while ((u || 23 - k < width) && k > 0);
    return put(out, cap, at, t + k);
}

/* HH:MM of a minute of the day. */
static bool put_hhmm(char *out, uint32_t cap, uint32_t *at, int64_t minute)
{
    return put_num(out, cap, at, minute / 60, 2) && put(out, cap, at, ":") &&
           put_num(out, cap, at, minute % 60, 2);
}

static bool label_ok(const char *label)
{
    if (!label) return false;
    size_t n = strlen(label);
    if (n > SUNDIAL_LABEL_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char) label[i];
        if (c < 0x20u || c == 0x7fu) return false;
    }
    return true;
}

static const char *const WEEKDAY[8] = {"",         "Monday", "Tuesday",  "Wednesday",
                                       "Thursday", "Friday", "Saturday", "Sunday"};

/* ---- the time (SM1, SM2) ----------------------------------------------------- */

void sundial_init(sundial_t *s, zxm_link_t *link)
{
    memset(s, 0, sizeof *s);
    s->link = link;
    s->zone = SUNDIAL_ZONE_UTC;
    s->snooze = SUNDIAL_SNOOZE_START;
}

int sundial_refresh(sundial_t *s)
{
    zxm_link_t *l = s->link;
    int64_t ms;
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_i64(l, l->result, "time_ms", &ms)) return ZXM_E_PROTOCOL;
    if (ms < 0 || ms > SUNDIAL_MS_MAX) return ZXM_E_PROTOCOL; /* no time a clock shows */
    s->now_ms = ms;
    s->have = true;
    return ZXM_OK;
}

int sundial_local(const sundial_t *s, int64_t zone, sundial_local_t *out)
{
    if (!out) return ZXM_E_ARG;
    int64_t off = sundial_zone_offset(zone); /* SD4 */
    if (off == SUNDIAL_REFUSED) return ZXM_E_ARG;
    if (!s->have) return ZXM_E_STATE;
    int64_t utc_s = s->now_ms / 1000;
    int64_t local_s = utc_s + off * 60;
    if (local_s < 0 || local_s > SOLSTICE_SECONDS_MAX) return ZXM_E_STATE;  /* off the calendar */
    int64_t minute = sundial_zone_minute(utc_s % SOLSTICE_DAY_S / 60, off); /* SD1 */
    int64_t y, m, d;
    if (minute < 0 || !solstice_date_of(local_s / SOLSTICE_DAY_S, &y, &m, &d)) return ZXM_E_STATE;
    int64_t wd = solstice_weekday(y, m, d); /* SO2 */
    if (wd < 1) return ZXM_E_STATE;
    out->offset = off;
    out->minute = minute;
    out->weekday = wd;
    out->year = y;
    out->month = m;
    out->day = d;
    return ZXM_OK;
}

int sundial_set_zone(sundial_t *s, int64_t zone)
{
    if (sundial_zone_offset(zone) == SUNDIAL_REFUSED) return ZXM_E_ARG;
    s->zone = zone;
    return ZXM_OK;
}

int sundial_clock_text(const sundial_t *s, int64_t zone, char *out, uint32_t cap)
{
    sundial_local_t t;
    uint32_t at = 0;
    if (!out || cap < SUNDIAL_CLOCK_TEXT) return ZXM_E_ARG;
    out[0] = 0;
    int rc = sundial_local(s, zone, &t);
    if (rc) return rc;
    int64_t a = t.offset < 0 ? -t.offset : t.offset;
    bool ok = put(out, cap, &at, t.offset < 0 ? "UTC-" : "UTC+") && put_hhmm(out, cap, &at, a) &&
              put(out, cap, &at, " ") && put(out, cap, &at, WEEKDAY[t.weekday]) &&
              put(out, cap, &at, " ") && put_hhmm(out, cap, &at, t.minute);
    return ok ? ZXM_OK : ZXM_E_TOOBIG;
}

int sundial_world_add(sundial_t *s, int64_t zone)
{
    if (sundial_zone_offset(zone) == SUNDIAL_REFUSED) return ZXM_E_ARG;
    for (uint32_t i = 0; i < s->nworld; i++)
        if (s->world[i] == zone) return ZXM_E_STATE;
    if (s->nworld >= SUNDIAL_WORLD) return ZXM_E_TOOBIG;
    s->world[s->nworld++] = zone;
    return ZXM_OK;
}

int sundial_world_remove(sundial_t *s, uint32_t index)
{
    if (index >= s->nworld) return ZXM_E_ARG;
    memmove(&s->world[index], &s->world[index + 1u],
            (size_t) (s->nworld - index - 1u) * sizeof s->world[0]);
    s->nworld--;
    return ZXM_OK;
}

/* ---- alarms (SM3, SM4) ---------------------------------------------------------- */

int sundial_alarm_add(sundial_t *s, int64_t at, int64_t mask, const char *label, uint32_t *index)
{
    if (!index || at < 0 || at >= SUNDIAL_DAY_MIN || mask < 1 || mask > 127 || !label_ok(label))
        return ZXM_E_ARG;
    if (!sundial_limits_ok((int64_t) s->nalarm + 1, s->snooze, 0)) return ZXM_E_TOOBIG; /* SD3 */
    sundial_alarm_t *a = &s->alarm[s->nalarm];
    memset(a, 0, sizeof *a);
    a->at = at;
    a->mask = mask;
    a->on = true;
    memcpy(a->label, label, strlen(label) + 1u);
    *index = s->nalarm++;
    return ZXM_OK;
}

int sundial_alarm_remove(sundial_t *s, uint32_t index)
{
    if (index >= s->nalarm) return ZXM_E_ARG;
    memmove(&s->alarm[index], &s->alarm[index + 1u],
            (size_t) (s->nalarm - index - 1u) * sizeof s->alarm[0]);
    s->nalarm--;
    return ZXM_OK;
}

int sundial_alarm_set_on(sundial_t *s, uint32_t index, bool on)
{
    if (index >= s->nalarm) return ZXM_E_ARG;
    s->alarm[index].on = on;
    return ZXM_OK;
}

int sundial_next_alarm(const sundial_t *s, uint32_t *index, int64_t *wait)
{
    sundial_local_t t;
    if (!index || !wait) return ZXM_E_ARG;
    int rc = sundial_local(s, s->zone, &t);
    if (rc) return rc;
    bool found = false;
    for (uint32_t i = 0; i < s->nalarm; i++) {
        if (!s->alarm[i].on) continue;
        int64_t w = sundial_alarm_wait(t.minute, t.weekday, s->alarm[i].mask, s->alarm[i].at);
        if (w < 0) return ZXM_E_STATE; /* SM3 keeps every alarm in SD2's domain */
        if (found && w >= *wait) continue;
        found = true;
        *index = i;
        *wait = w;
    }
    return found ? ZXM_OK : ZXM_E_STATE;
}

int sundial_set_snooze(sundial_t *s, int64_t minutes)
{
    if (!sundial_limits_ok((int64_t) s->nalarm, minutes, 0)) return ZXM_E_ARG; /* SD3 */
    s->snooze = minutes;
    return ZXM_OK;
}

int sundial_snooze_now(sundial_t *s)
{
    if (!s->have) return ZXM_E_STATE;
    s->snoozed_ms = s->now_ms + s->snooze * MS_PER_MIN;
    return ZXM_OK;
}

/* ---- the timer (SM5) ---------------------------------------------------------- */

int sundial_timer_start(sundial_t *s, int64_t minutes)
{
    if (minutes < 1 || !sundial_limits_ok((int64_t) s->nalarm, s->snooze, minutes)) /* SD3 */
        return ZXM_E_ARG;
    if (!s->have) return ZXM_E_STATE;
    s->timer_min = minutes;
    s->timer_start_ms = s->now_ms;
    return ZXM_OK;
}

int sundial_timer_stop(sundial_t *s)
{
    if (!s->timer_min) return ZXM_E_STATE;
    s->timer_min = 0;
    s->timer_start_ms = 0;
    return ZXM_OK;
}

int64_t sundial_timer_left_ms(const sundial_t *s)
{
    if (!s->timer_min) return -1;
    int64_t left = s->timer_start_ms + s->timer_min * MS_PER_MIN - s->now_ms;
    return left < 0 ? 0 : left; /* never below zero */
}

int64_t sundial_timer_hms(const sundial_t *s)
{
    int64_t left = sundial_timer_left_ms(s);
    return left < 0 ? -1 : halloo_hms(left); /* HR4 */
}

/* ---- what is due -------------------------------------------------------------- */

/* Add one line to the notice, or count it only when it does not fit. */
static void tell(sundial_t *s, uint32_t *at, bool ok, uint32_t keep, uint32_t *n)
{
    (*n)++;
    if (!ok) {
        *at = keep;
        s->notice[keep] = 0;
    }
}

int sundial_due(sundial_t *s, uint32_t *n)
{
    sundial_local_t t;
    uint32_t at = 0, keep;
    const uint32_t cap = sizeof s->notice;
    if (!n) return ZXM_E_ARG;
    *n = 0;
    s->notice[0] = 0;
    int rc = sundial_local(s, s->zone, &t);
    if (rc) return rc;
    for (uint32_t i = 0; i < s->nalarm; i++) {
        const sundial_alarm_t *a = &s->alarm[i];
        if (!a->on || sundial_alarm_wait(t.minute, t.weekday, a->mask, a->at) != 0) continue;
        keep = at;
        bool ok = put(s->notice, cap, &at, "Alarm ") && put_hhmm(s->notice, cap, &at, a->at) &&
                  put(s->notice, cap, &at, a->label[0] ? " " : "") &&
                  put(s->notice, cap, &at, a->label) && put(s->notice, cap, &at, "\n");
        tell(s, &at, ok, keep, n);
    }
    if (s->snoozed_ms && s->now_ms >= s->snoozed_ms) {
        keep = at;
        tell(s, &at, put(s->notice, cap, &at, "Snoozed alarm\n"), keep, n);
        s->snoozed_ms = 0;
    }
    if (s->timer_min && sundial_timer_left_ms(s) == 0) {
        keep = at;
        bool ok =
            put(s->notice, cap, &at, "Timer of ") &&
            put_num(s->notice, cap, &at, s->timer_min, 1) &&
            put(s->notice, cap, &at, s->timer_min == 1 ? " minute done\n" : " minutes done\n");
        tell(s, &at, ok, keep, n);
        s->timer_min = 0;
        s->timer_start_ms = 0;
    }
    return ZXM_OK;
}
