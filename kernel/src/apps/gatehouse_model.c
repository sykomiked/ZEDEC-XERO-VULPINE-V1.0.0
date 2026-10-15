/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gatehouse_model.c — Gatehouse (see gatehouse_model.h). */
#include <string.h>

#include "gatehouse_model.h"

static const char *const MODE[] = {"ask", "once", "always", "never"};
enum { MODE_ASK, MODE_ONCE, MODE_ALWAYS, MODE_NEVER, MODES };

void gatehouse_init(gatehouse_t *g, zxm_link_t *link)
{
    memset(g, 0, sizeof *g);
    g->link = link;
}

/* an app's name as zxvd takes it (place.*): 1 to 31 printable ASCII bytes,
 * no spaces */
static bool name_ok(const char *s)
{
    if (!s || !s[0]) return false;
    uint32_t n = 0;
    for (; s[n]; n++)
        if (n >= GATEHOUSE_APP_MAX || s[n] <= ' ' || s[n] > '~') return false;
    return true;
}

static int32_t find_index(const gatehouse_t *g, const char *app)
{
    for (uint32_t i = 0; i < g->napps; i++)
        if (!strcmp(g->app[i].app, app)) return (int32_t) i;
    return -1;
}

/* the app's row, added when new; NULL when no row fits */
static gatehouse_app_t *row_of(gatehouse_t *g, const char *app)
{
    int32_t i = find_index(g, app);
    if (i >= 0) return &g->app[i];
    if (g->napps >= GATEHOUSE_APPS) return NULL;
    gatehouse_app_t *a = &g->app[g->napps++];
    memset(a, 0, sizeof *a);
    memcpy(a->app, app, strlen(app) + 1u);
    return a;
}

static int64_t bit(uint32_t mask, int32_t cap)
{
    return (int64_t) ((mask >> (uint32_t) cap) & 1u);
}

static void put(char *out, uint32_t cap, uint32_t *k, const char *s)
{
    for (; *s && *k + 1u < cap; s++) out[(*k)++] = *s;
    out[*k] = 0;
}

static void put_u(char *out, uint32_t cap, uint32_t *k, uint32_t v)
{
    char d[12];
    uint32_t n = 0;
    do {
        d[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n && *k + 1u < cap) out[(*k)++] = d[--n];
    out[*k] = 0;
}

/* GR4 for every capability, the asks that wait, and their notice (GH4) */
static void recount(gatehouse_t *g)
{
    static int64_t held[GATEHOUSE_APPS];
    uint32_t waiting = 0;
    for (int32_t c = 0; c < (int32_t) GATEHOUSE_CAPS; c++) {
        for (uint32_t i = 0; i < g->napps; i++) {
            const gatehouse_app_t *a = &g->app[i];
            held[i] =
                gatehouse_use_allowed(bit(a->asked, c), bit(a->granted, c), bit(a->revoked, c));
            waiting += bit(a->asked, c) && !bit(a->granted, c) && !bit(a->revoked, c);
        }
        g->holders[c] = gatehouse_holders(held, g->napps);
    }
    g->waiting = waiting;
    g->notice[0] = 0;
    if (!waiting) return;
    uint32_t k = 0;
    const uint32_t cap = GATEHOUSE_NOTICE_MAX;
    put(g->notice, cap, &k, "Gatehouse: ");
    put_u(g->notice, cap, &k, waiting);
    put(g->notice, cap, &k, waiting == 1u ? " ask waits" : " asks wait");
    put(g->notice, cap, &k, " for your answer:");
    for (uint32_t i = 0; i < g->napps; i++)
        for (int32_t c = 0; c < (int32_t) GATEHOUSE_CAPS; c++) {
            const gatehouse_app_t *a = &g->app[i];
            if (!bit(a->asked, c) || bit(a->granted, c) || bit(a->revoked, c)) continue;
            /* room for this ask and the closing words, or the list is cut */
            if (k + strlen(a->app) + strlen(GATEHOUSE_CAP_NAME[c]) + 24u > cap) {
                put(g->notice, cap, &k, " ...");
                goto close;
            }
            put(g->notice, cap, &k, " ");
            put(g->notice, cap, &k, a->app);
            put(g->notice, cap, &k, " (");
            put(g->notice, cap, &k, GATEHOUSE_CAP_NAME[c]);
            put(g->notice, cap, &k, ")");
        }
close:
    put(g->notice, cap, &k, ". Nothing changes until you decide.");
}

int gatehouse_ask(gatehouse_t *g, const char *app, const char *cap)
{
    int32_t c = gatehouse_cap_index(cap);
    if (!g || !name_ok(app) || c < 0) return ZXM_E_ARG;
    gatehouse_app_t *a = row_of(g, app);
    if (!a) return ZXM_E_TOOBIG;
    a->asked |= 1u << (uint32_t) c;
    recount(g);
    return ZXM_OK;
}

/* zxvd's clock, sys.info time_ms */
static int read_clock(zxm_link_t *l, int64_t *ms)
{
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_i64(l, l->result, "time_ms", ms) || *ms < 0) return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

static int permit(zxm_link_t *l, const char *app, int mode)
{
    zxm_begin(l, "place.permit");
    zxm_p_str(l, "app", app);
    zxm_p_str(l, "mode", MODE[mode]);
    int rc = zxm_call(l);
    char back[8];
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "mode", back, sizeof back) || strcmp(back, MODE[mode]))
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

/* GH2/GH3: checked, then zxvd's clock, then zxvd's own answer for location,
 * and only then the change */
static int decide(gatehouse_t *g, const char *app, const char *cap, bool confirmed, bool grant)
{
    int32_t c = gatehouse_cap_index(cap);
    if (!g || !name_ok(app) || c < 0) return ZXM_E_ARG;
    if (!confirmed) return ZXM_E_STATE;
    int32_t i = find_index(g, app);
    if (i < 0 || (grant && !bit(g->app[i].asked, c))) return ZXM_E_STATE;
    int64_t now = 0;
    int rc = read_clock(g->link, &now);
    if (rc == ZXM_OK && c == GATEHOUSE_LOCATION)
        rc = permit(g->link, app, grant ? MODE_ALWAYS : MODE_NEVER);
    if (rc != ZXM_OK) return rc;
    gatehouse_app_t *a = &g->app[i];
    uint32_t m = 1u << (uint32_t) c;
    if (grant) {
        a->granted |= m;
        a->revoked &= ~m;
    } else {
        a->revoked |= m;
    }
    a->changed_ms = now;
    g->now_ms = now;
    recount(g);
    return ZXM_OK;
}

int gatehouse_grant(gatehouse_t *g, const char *app, const char *cap, bool confirmed)
{
    return decide(g, app, cap, confirmed, true);
}

int gatehouse_revoke(gatehouse_t *g, const char *app, const char *cap, bool confirmed)
{
    return decide(g, app, cap, confirmed, false);
}

static int mode_of(const char *s)
{
    for (int i = 0; i < MODES; i++)
        if (!strcmp(s, MODE[i])) return i;
    return -1;
}

int gatehouse_refresh(gatehouse_t *g)
{
    if (!g) return ZXM_E_ARG;
    zxm_link_t *l = g->link;
    static gatehouse_t keep;
    keep = *g;
    int64_t now = 0;
    int rc = read_clock(l, &now);
    if (rc != ZXM_OK) return rc;
    zxm_begin(l, "place.apps");
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    int32_t arr = zxm_get(l, l->result, "apps");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > GATEHOUSE_APPS)
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    const uint32_t m = 1u << (uint32_t) GATEHOUSE_LOCATION;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        char app[GATEHOUSE_APP_MAX + 2], mode[8];
        bool pending;
        int64_t asked;
        int md = -1;
        if (o < 0 || zxm_get_str(l, o, "app", app, sizeof app) || !name_ok(app) ||
            zxm_get_str(l, o, "mode", mode, sizeof mode) || (md = mode_of(mode)) < 0 ||
            zxm_get_bool(l, o, "pending", &pending) || zxm_get_i64(l, o, "asked", &asked) ||
            asked < 0 || (pending && md != MODE_ASK)) {
            rc = ZXM_E_PROTOCOL;
            goto failed;
        }
        gatehouse_app_t *a = row_of(g, app);
        if (!a) {
            rc = ZXM_E_TOOBIG;
            goto failed;
        }
        if (pending || asked > 0) a->asked |= m;
        if (md == MODE_ONCE || md == MODE_ALWAYS) {
            a->granted |= m;
            a->revoked &= ~m;
        } else if (md == MODE_NEVER) {
            a->revoked |= m;
        } else {
            a->granted &= ~m;
            a->revoked &= ~m;
        }
    }
    g->now_ms = now;
    recount(g);
    return ZXM_OK;
failed:
    *g = keep; /* what was shown stays */
    return rc;
}

const gatehouse_app_t *gatehouse_find(const gatehouse_t *g, const char *app)
{
    int32_t i = g && app ? find_index(g, app) : -1;
    return i < 0 ? NULL : &g->app[i];
}

bool gatehouse_allowed(const gatehouse_t *g, const char *app, const char *cap)
{
    const gatehouse_app_t *a = gatehouse_find(g, app);
    int32_t c = gatehouse_cap_index(cap);
    if (!a || c < 0) return false;
    return gatehouse_use_ok(bit(a->asked, c), bit(a->granted, c), bit(a->revoked, c));
}

int64_t gatehouse_count(const gatehouse_t *g, const char *cap)
{
    int32_t c = gatehouse_cap_index(cap);
    if (!g || c < 0) return GATEHOUSE_REFUSED;
    return g->holders[c];
}
