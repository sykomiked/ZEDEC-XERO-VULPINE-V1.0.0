/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* brazier_model.c — Brazier (see brazier_model.h). */
#include <string.h>

#include "brazier_model.h"

/* ---- the rules ------------------------------------------------------- */

int64_t brazier_level_x100(int64_t now, int64_t full)
{
    if (full <= 0 || now < 0 || now > full) return BRAZIER_UNKNOWN;
    if (now > INT64_MAX / 10000) return BRAZIER_UNKNOWN;
    return now * 10000 / full;
}

int64_t brazier_minutes_left(int64_t charge, int64_t draw)
{
    if (draw <= 0 || charge < 0) return BRAZIER_UNKNOWN;
    if (charge > BRAZIER_CHARGE_MAX || draw > BRAZIER_CHARGE_MAX) return BRAZIER_UNKNOWN;
    return charge * 60 / draw;
}

bool brazier_heat_ok(int64_t temp)
{
    return temp >= BRAZIER_WARN_LO && temp <= BRAZIER_WARN_HI;
}

bool brazier_heat_safe(int64_t temp)
{
    return temp >= BRAZIER_CRIT_LO && temp <= BRAZIER_CRIT_HI;
}

int32_t brazier_heat(int64_t temp)
{
    if (!brazier_heat_safe(temp)) return BRAZIER_HEAT_CRITICAL;
    if (!brazier_heat_ok(temp)) return BRAZIER_HEAT_WARN;
    return BRAZIER_HEAT_OK;
}

/* ---- the notice ------------------------------------------------------ */

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

static bool put_num(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char t[24];
    int k = 23;
    bool neg = v < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u && k > 1);
    if (neg) t[--k] = '-';
    return put(out, cap, at, t + k);
}

/* v / 10^digits with that many digits after the point: 7140, 2 -> "71.40";
 * -5, 1 -> "-0.5". */
static bool put_fixed(char *out, uint32_t cap, uint32_t *at, int64_t v, uint32_t digits)
{
    uint64_t scale = digits == 2u ? 100u : 10u;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    uint64_t frac = u % scale;
    char f[3] = {0, 0, 0};
    if (v < 0 && !put(out, cap, at, "-")) return false;
    if (!put_num(out, cap, at, (int64_t) (u / scale))) return false;
    if (digits == 2u) {
        f[0] = (char) ('0' + (int) (frac / 10u));
        f[1] = (char) ('0' + (int) (frac % 10u));
    } else {
        f[0] = (char) ('0' + (int) frac);
    }
    return put(out, cap, at, ".") && put(out, cap, at, f);
}

static bool put_temp(char *out, uint32_t cap, uint32_t *at, int64_t dc)
{
    return put_fixed(out, cap, at, dc, 1u) && put(out, cap, at, " C");
}

/* What the view says (BZ2): the draw, the battery, and its heat. */
static void compose(brazier_t *b)
{
    char *s = b->notice;
    uint32_t cap = sizeof b->notice, at = 0;
    const brazier_battery_t *t = &b->bat;
    bool ok = true;
    s[0] = 0;
    if (!strcmp(b->source, "none"))
        ok = put(s, cap, &at, "no power reading");
    else
        ok = put(s, cap, &at, "drawing ") && put_num(s, cap, &at, (int64_t) b->draw_mw) &&
             put(s, cap, &at, " mW");
    if (ok && !t->present) {
        ok = put(s, cap, &at, "; no battery");
    } else if (ok) {
        ok = put(s, cap, &at, "; battery ");
        if (ok && t->level_x100 >= 0)
            ok = put_fixed(s, cap, &at, t->level_x100, 2u) && put(s, cap, &at, "%");
        else if (ok)
            ok = put(s, cap, &at, "level unknown");
        if (ok)
            ok = put(s, cap, &at,
                     t->charging   ? ", charging"
                     : t->on_mains ? ", on mains"
                                   : ", on battery");
        if (ok && t->minutes_left >= 0)
            ok = put(s, cap, &at, ", ") && put_num(s, cap, &at, t->minutes_left) &&
                 put(s, cap, &at, " minutes left");
        if (ok && t->minutes_to_full >= 0)
            ok = put(s, cap, &at, ", full in ") && put_num(s, cap, &at, t->minutes_to_full) &&
                 put(s, cap, &at, " minutes");
        if (ok && !t->has_temp) {
            ok = put(s, cap, &at, "; no battery temperature reported");
        } else if (ok) {
            bool low = t->temp_dc < BRAZIER_WARN_LO;
            ok = put(s, cap, &at, "; battery temperature ") && put_temp(s, cap, &at, t->temp_dc);
            if (ok && t->heat == BRAZIER_HEAT_OK)
                ok = put(s, cap, &at, " is inside its usual range");
            else if (ok && t->heat == BRAZIER_HEAT_WARN)
                ok = put(s, cap, &at, low ? " is below the usual " : " is above the usual ") &&
                     put_temp(s, cap, &at, BRAZIER_WARN_LO) && put(s, cap, &at, " to ") &&
                     put_temp(s, cap, &at, BRAZIER_WARN_HI) &&
                     put(s, cap, &at,
                         low ? "; let it warm before charging (advice only)"
                             : "; let it cool (advice only)");
            else if (ok)
                ok =
                    put(s, cap, &at, low ? " is below the critical " : " is above the critical ") &&
                    put_temp(s, cap, &at, low ? BRAZIER_CRIT_LO : BRAZIER_CRIT_HI) &&
                    put(s, cap, &at,
                        low ? "; let it warm now (advice only)"
                            : "; let it cool now (advice only)");
        }
    }
    if (!ok) {
        /* every part above fits BRAZIER_NOTICE_MAX; a cut notice says so */
        at = 0;
        s[0] = 0;
        (void) put(s, cap, &at, "power readings too long to show");
    }
}

/* ---- the readings ---------------------------------------------------- */

void brazier_init(brazier_t *b, zxm_link_t *link)
{
    memset(b, 0, sizeof *b);
    b->link = link;
    b->bat.level_x100 = BRAZIER_UNKNOWN;
    b->bat.minutes_left = BRAZIER_UNKNOWN;
    b->bat.minutes_to_full = BRAZIER_UNKNOWN;
}

static bool is_word(const char *w, const char *const *set, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (!strcmp(w, set[i])) return true;
    return false;
}

/* The battery object of power.status (power_ledger.c pw_render_json). */
static int read_battery(const zxm_link_t *l, int32_t o, brazier_battery_t *t)
{
    uint64_t cycles, to_full;
    int64_t temp;
    if (zxm_get_bool(l, o, "on_mains", &t->on_mains) ||
        zxm_get_bool(l, o, "charging", &t->charging) || zxm_get_u64(l, o, "now_mwh", &t->now_mwh) ||
        zxm_get_u64(l, o, "full_mwh", &t->full_mwh) ||
        zxm_get_u64(l, o, "design_mwh", &t->design_mwh) ||
        zxm_get_i64(l, o, "rate_mw", &t->rate_mw) || zxm_get_u64(l, o, "cycles", &cycles) ||
        cycles > UINT32_MAX || zxm_get_u64(l, o, "minutes_to_full", &to_full))
        return ZXM_E_PROTOCOL;
    t->present = true;
    t->cycles = (uint32_t) cycles;
    t->has_temp = zxm_get(l, o, "temp_dc") >= 0;
    if (t->has_temp) {
        if (zxm_get_i64(l, o, "temp_dc", &temp) || temp < INT32_MIN || temp > INT32_MAX)
            return ZXM_E_PROTOCOL;
        t->temp_dc = (int32_t) temp;
        t->heat = brazier_heat(temp);
    }
    if (t->now_mwh <= INT64_MAX && t->full_mwh <= INT64_MAX)
        t->level_x100 = brazier_level_x100((int64_t) t->now_mwh, (int64_t) t->full_mwh);
    if (!t->on_mains && t->rate_mw > 0 && t->now_mwh <= INT64_MAX)
        t->minutes_left = brazier_minutes_left((int64_t) t->now_mwh, t->rate_mw);
    if (t->charging && to_full > 0 && to_full <= INT64_MAX) t->minutes_to_full = (int64_t) to_full;
    return ZXM_OK;
}

int brazier_refresh(brazier_t *b)
{
    static const char *const DEVICES[] = {"desktop", "laptop", "mobile", "server"};
    static const char *const SOURCES[] = {"none", "metered", "attributed", "conflict", "fault"};
    static const char *const POSTURES[] = {"perform", "balanced", "conserve", "critical"};
    zxm_link_t *l = b->link;
    brazier_t n;
    brazier_init(&n, l);

    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc) return rc;
    uint64_t cpus;
    if (zxm_get_u64(l, l->result, "time_ms", &n.time_ms) ||
        zxm_get_u64(l, l->result, "cpus", &cpus) || cpus > UINT32_MAX)
        return ZXM_E_PROTOCOL;
    n.cpus = (uint32_t) cpus;

    zxm_begin(l, "power.status");
    rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "total_mw", &n.draw_mw) ||
        zxm_get_str(l, l->result, "device", n.device, sizeof n.device) ||
        zxm_get_str(l, l->result, "source", n.source, sizeof n.source) ||
        zxm_get_str(l, l->result, "posture", n.posture, sizeof n.posture) ||
        !is_word(n.device, DEVICES, 4u) || !is_word(n.source, SOURCES, 5u) ||
        !is_word(n.posture, POSTURES, 4u))
        return ZXM_E_PROTOCOL;
    int32_t bt = zxm_get(l, l->result, "battery");
    if (bt < 0) return ZXM_E_PROTOCOL;
    if (l->tok[bt].type == W4_J_OBJ) {
        if (read_battery(l, bt, &n.bat)) return ZXM_E_PROTOCOL;
    } else if (l->tok[bt].type != W4_J_NULL) {
        return ZXM_E_PROTOCOL;
    }
    n.have = true;
    compose(&n);
    *b = n;
    return ZXM_OK;
}
