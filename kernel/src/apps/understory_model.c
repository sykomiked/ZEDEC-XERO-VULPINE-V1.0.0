/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* understory_model.c — Understory's model (understory_model.h UM1-UM4). */
#include <stdio.h>
#include <string.h>

#include "understory_model.h"

static void say(understory_t *u, const char *s)
{
    size_t n = strlen(s);
    if (n > UNDERSTORY_NOTICE_MAX) n = UNDERSTORY_NOTICE_MAX;
    memcpy(u->notice, s, n);
    u->notice[n] = 0;
}

/* an executable name power.budget takes: 1..31 printable bytes, no space */
static bool app_name_ok(const char *s)
{
    if (!s) return false;
    size_t n = 0;
    while (n <= UNDERSTORY_NAME && s[n]) {
        if (s[n] <= ' ' || s[n] > '~') return false;
        n++;
    }
    return n > 0 && n <= UNDERSTORY_NAME;
}

void understory_init(understory_t *u, zxm_link_t *link)
{
    if (!u) return;
    memset(u, 0, sizeof *u);
    u->link = link;
}

int understory_refresh(understory_t *u)
{
    if (!u || !u->link) return ZXM_E_ARG;
    zxm_link_t *l = u->link;
    int64_t total_uj = 0, total_mw = 0;
    char source[UNDERSTORY_WORD + 1], posture[UNDERSTORY_WORD + 1];

    zxm_begin(l, "power.status");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_i64(l, l->result, "total_uj", &total_uj) || total_uj < 0 ||
        zxm_get_i64(l, l->result, "total_mw", &total_mw) || total_mw < 0 ||
        zxm_get_str(l, l->result, "source", source, sizeof source) ||
        zxm_get_str(l, l->result, "posture", posture, sizeof posture))
        return ZXM_E_PROTOCOL;

    zxm_begin(l, "power.top");
    zxm_p_str(l, "kind", "app");
    zxm_p_u64(l, "n", UNDERSTORY_TOP);
    rc = zxm_call(l);
    if (rc) return rc;
    uint32_t n = zxm_len(l, l->result);
    if (n > UNDERSTORY_TOP) return ZXM_E_PROTOCOL;
    understory_app_t app[UNDERSTORY_TOP];
    memset(app, 0, sizeof app);
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, l->result, i);
        if (o < 0 || zxm_get_str(l, o, "name", app[i].name, sizeof app[i].name) ||
            zxm_get_i64(l, o, "uj", &app[i].uj) || app[i].uj < 0 ||
            zxm_get_i64(l, o, "mw", &app[i].mw) || app[i].mw < 0)
            return ZXM_E_PROTOCOL;
    }

    /* the whole answer is read: only now is the copy changed */
    u->total_uj = total_uj;
    u->total_mw = total_mw;
    memcpy(u->source, source, sizeof source);
    memcpy(u->posture, posture, sizeof posture);
    u->napps = n;
    memcpy(u->app, app, sizeof app);
    if (u->nsamples == UNDERSTORY_SAMPLES) { /* full: the oldest reading goes */
        memmove(u->sample_mj, u->sample_mj + 1, (UNDERSTORY_SAMPLES - 1u) * sizeof(int64_t));
        u->nsamples--;
    }
    u->sample_mj[u->nsamples++] = total_uj / UNDERSTORY_UJ_PER_MJ;
    u->have = true;
    return ZXM_OK;
}

int understory_energy(understory_t *u, int64_t width, int64_t tariff, int64_t budget_mj,
                      understory_report_t *r)
{
    if (!u || !r || width < 1 || width > (int64_t) u->nsamples || budget_mj < 1) return ZXM_E_ARG;
    int64_t rolled[UNDERSTORY_SAMPLES];
    if (understory_rolling(u->sample_mj, u->nsamples, width, rolled) != 0) return ZXM_E_ARG;
    int64_t total = rolled[u->nsamples - 1u];
    int64_t cost = understory_energy_cost(total, tariff);
    int64_t used = wellspring_progress_x100(total, budget_mj);
    if (cost < 0 || used < 0) return ZXM_E_ARG;
    understory_report_t out = {total, cost, understory_over_budget(total - budget_mj), used};
    *r = out;
    char s[UNDERSTORY_NOTICE_MAX + 1];
    snprintf(s, sizeof s,
             "Energy over the last %lld readings: %lld mJ, %lld.%02lld%% of the budget, costing "
             "%lld at your tariff.%s Advice only.",
             (long long) width, (long long) total, (long long) (used / 100),
             (long long) (used % 100), (long long) cost, out.over ? " The budget is passed." : "");
    say(u, s);
    return ZXM_OK;
}

int understory_propose(understory_t *u, const char *app, int64_t window, int64_t mj, int64_t whole)
{
    if (!u) return ZXM_E_ARG;
    if (u->held.pending) return ZXM_E_STATE; /* one budget at a time */
    if (app && !app_name_ok(app)) return ZXM_E_ARG;
    if (!app && whole != mj) return ZXM_E_ARG; /* the device's budget is its whole */
    if (!keystone_energy_budget_ok(window, mj, whole)) return ZXM_E_ARG;
    understory_budget_t b;
    memset(&b, 0, sizeof b);
    if (app) memcpy(b.app, app, strlen(app) + 1u);
    b.window = window;
    b.mj = mj;
    b.pending = true;
    u->held = b;
    char s[UNDERSTORY_NOTICE_MAX + 1];
    snprintf(s, sizeof s,
             "An energy budget of %lld mJ every %lld ms for %s is proposed. Nothing is set until "
             "you confirm.",
             (long long) mj, (long long) window, app ? app : "the device");
    say(u, s);
    return ZXM_OK;
}

int understory_apply(understory_t *u, bool owner_confirmed)
{
    if (!u || !u->link) return ZXM_E_ARG;
    if (!u->held.pending) return ZXM_E_STATE;
    if (!owner_confirmed) return ZXM_E_STATE; /* advice until the owner says yes */
    const understory_budget_t *b = &u->held;
    zxm_link_t *l = u->link;
    zxm_begin(l, "power.budget");
    zxm_p_str(l, "target", b->app[0] ? "app" : "device");
    if (b->app[0]) zxm_p_str(l, "app", b->app);
    zxm_p_i64(l, "window_ms", b->window);
    zxm_p_i64(l, "max_uj", b->mj * UNDERSTORY_UJ_PER_MJ);
    zxm_p_bool(l, "strict", false);
    int rc = zxm_call(l);
    if (rc) {
        say(u, "The energy budget was refused; it stays proposed.");
        return rc;
    }
    bool ok = false;
    if (zxm_get_bool(l, l->result, "ok", &ok) || !ok) return ZXM_E_PROTOCOL;
    u->held.pending = false;
    say(u, "The energy budget is set as you confirmed.");
    return ZXM_OK;
}

void understory_cancel(understory_t *u)
{
    if (!u) return;
    memset(&u->held, 0, sizeof u->held);
    say(u, "The proposed energy budget is dropped.");
}

int understory_add_water(understory_t *u, int64_t day, int64_t source, int64_t litres)
{
    if (!u || !understory_water_ok(day, source, litres)) return ZXM_E_ARG;
    if (u->nwater >= UNDERSTORY_WATERS) return ZXM_E_STATE; /* full */
    understory_water_t w = {day, source, litres};
    u->water[u->nwater++] = w;
    return ZXM_OK;
}

int understory_add_parcel(understory_t *u, int64_t parcel, int64_t kind, int64_t area)
{
    if (!u || !understory_land_ok(parcel, kind, area)) return ZXM_E_ARG;
    for (uint32_t i = 0; i < u->nland; i++)
        if (u->land[i].parcel == parcel) return ZXM_E_ARG;  /* one record per parcel */
    if (u->nland >= UNDERSTORY_PARCELS) return ZXM_E_STATE; /* full */
    understory_parcel_t p = {parcel, kind, area};
    u->land[u->nland++] = p;
    return ZXM_OK;
}

int understory_water(understory_t *u, int64_t from, int64_t to, int64_t budget, int64_t *litres,
                     bool *over)
{
    if (!u || !litres || !over || from < 0 || from > to || to > UNDERSTORY_DAY_MAX || budget < 0)
        return ZXM_E_ARG;
    int64_t s = 0; /* 256 readings of nine digits stay far inside 64 bits */
    for (uint32_t i = 0; i < u->nwater; i++)
        if (u->water[i].day >= from && u->water[i].day <= to) s += u->water[i].litres;
    *litres = s;
    *over = understory_over_budget(s - budget);
    char t[UNDERSTORY_NOTICE_MAX + 1];
    snprintf(t, sizeof t, "Water from day %lld to %lld: %lld litres against %lld.%s Advice only.",
             (long long) from, (long long) to, (long long) s, (long long) budget,
             *over ? " The budget is passed." : "");
    say(u, t);
    return ZXM_OK;
}

int understory_land(understory_t *u, int64_t kind, int64_t budget, int64_t *area, bool *over)
{
    if (!u || !area || !over || kind < 0 || kind > UNDERSTORY_WILD || budget < 0) return ZXM_E_ARG;
    int64_t s = 0; /* 64 parcels of twelve digits stay far inside 64 bits */
    for (uint32_t i = 0; i < u->nland; i++)
        if (kind == 0 || u->land[i].kind == kind) s += u->land[i].area;
    *area = s;
    *over = understory_over_budget(s - budget);
    char t[UNDERSTORY_NOTICE_MAX + 1];
    snprintf(t, sizeof t, "Land of %s: %lld square metres against %lld.%s Advice only.",
             kind == 0 ? "every kind" : "this kind", (long long) s, (long long) budget,
             *over ? " The budget is passed." : "");
    say(u, t);
    return ZXM_OK;
}
