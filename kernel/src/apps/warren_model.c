/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* warren_model.c — Warren (see warren_model.h). */
#include <string.h>

#include "warren_model.h"

/* ---- small helpers ---------------------------------------------------- */

/* a feature name as zxvd takes it: ^[a-z0-9_]{1,23}$ */
static bool name_ok(const char *s)
{
    if (!s || !*s) return false;
    size_t n = 0;
    for (; s[n]; n++) {
        char c = s[n];
        if (n >= WARREN_NAME_MAX) return false;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

static int32_t find(const warren_t *w, const char *name)
{
    for (uint32_t i = 0; name && i < w->nengines; i++)
        if (!strcmp(w->engine[i].name, name)) return (int32_t) i;
    return -1;
}

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

/* x100 as "12.34" */
static bool put_x100(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char f[4] = {'.', (char) ('0' + (int) (v % 100 / 10)), (char) ('0' + (int) (v % 10)), 0};
    return put_num(out, cap, at, v / 100) && put(out, cap, at, f);
}

/* ---- the notice (WM6) ------------------------------------------------- */

static void compose(warren_t *w)
{
    char *s = w->notice;
    uint32_t cap = sizeof w->notice, at = 0, on = 0, reg = 0;
    s[0] = 0;
    if (!w->have) return;
    for (uint32_t i = 0; i < w->nengines; i++) {
        on += w->engine[i].enabled;
        reg += w->engine[i].enabled && w->engine[i].regulated;
    }
    bool ok = put_num(s, cap, &at, w->nengines) && put(s, cap, &at, " engines, ") &&
              put_num(s, cap, &at, on) && put(s, cap, &at, " on");
    if (ok && reg) {
        ok = put(s, cap, &at, "; regulated and on, by the owner's approval:");
        for (uint32_t i = 0; ok && i < w->nengines; i++)
            if (w->engine[i].enabled && w->engine[i].regulated)
                ok = put(s, cap, &at, " ") && put(s, cap, &at, w->engine[i].name);
    }
    if (ok && w->retry.waiting)
        ok = put(s, cap, &at, "; ") && put(s, cap, &at, w->retry.engine) &&
             put(s, cap, &at, " did not come back on: attempt ") &&
             put_num(s, cap, &at, (int64_t) w->retry.attempt + 1) && put(s, cap, &at, " of ") &&
             put_num(s, cap, &at, w->tries) && put(s, cap, &at, " in ") &&
             put_num(s, cap, &at, w->retry.wait_s) && put(s, cap, &at, " s");
    else if (ok && w->retry.gave_up)
        ok = put(s, cap, &at, "; ") && put(s, cap, &at, w->retry.engine) &&
             put(s, cap, &at, " stayed off after ") && put_num(s, cap, &at, w->tries) &&
             put(s, cap, &at, " attempts; turn it on when ready");
    if (ok && w->nconsumers)
        ok = put(s, cap, &at, "; most energy: ") && put(s, cap, &at, w->consumer[0].name) &&
             put(s, cap, &at, " ") && put_x100(s, cap, &at, w->consumer[0].share_x100) &&
             put(s, cap, &at, "%");
    if (ok && w->dropped) ok = put(s, cap, &at, "; some older events were not kept");
    if (!ok) put(s, cap, &at, " ...");
}

/* ---- the model -------------------------------------------------------- */

void warren_init(warren_t *w, zxm_link_t *link)
{
    memset(w, 0, sizeof *w);
    w->link = link;
    w->tries = WARREN_TRIES;
    w->base_s = WARREN_BASE;
    w->most_s = WARREN_MOST;
}

void warren_set_owner(warren_t *w, bool owner)
{
    w->owner = owner;
}

int warren_set_backoff(warren_t *w, int64_t tries, int64_t base, int64_t most)
{
    if (w->retry.waiting) return ZXM_E_STATE;
    if (warren_backoff(1, tries, base, most) == WARREN_REFUSED) return ZXM_E_ARG;
    w->tries = (uint32_t) tries;
    w->base_s = base;
    w->most_s = most;
    return ZXM_OK;
}

int64_t warren_wait(const warren_t *w, int64_t k)
{
    return warren_backoff(k, w->tries, w->base_s, w->most_s);
}

/* sys.info's clock and the daemon's own facts, into the given fields */
static int read_sys(warren_t *w, int64_t *time_ms, uint64_t *uptime, uint64_t *pid, uint64_t *rss)
{
    zxm_link_t *l = w->link;
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t z = zxm_get(l, l->result, "zxvd");
    if (zxm_get_i64(l, l->result, "time_ms", time_ms) != ZXM_OK || *time_ms <= 0 ||
        zxm_get_u64(l, l->result, "uptime_s", uptime) != ZXM_OK || z < 0 ||
        zxm_get_u64(l, z, "pid", pid) != ZXM_OK || zxm_get_u64(l, z, "rss_bytes", rss) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

int warren_refresh(warren_t *w)
{
    zxm_link_t *l = w->link;
    char app[WARREN_TEXT_MAX + 1], proto[WARREN_WORD_MAX + 1], lib[WARREN_WORD_MAX + 1];
    uint64_t mask = 0, uptime = 0, pid = 0, rss = 0;
    int64_t now = 0;
    static warren_engine_t eng[WARREN_ENGINES];
    uint32_t n = 0;
    bool saved = false;

    zxm_begin(l, "zxvd.version");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "app", app, sizeof app) != ZXM_OK ||
        zxm_get_str(l, l->result, "protocol", proto, sizeof proto) != ZXM_OK ||
        zxm_get_str(l, l->result, "libzxv", lib, sizeof lib) != ZXM_OK ||
        zxm_get_u64(l, l->result, "libzxv_features", &mask) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    rc = read_sys(w, &now, &uptime, &pid, &rss);
    if (rc != ZXM_OK) return rc;

    zxm_begin(l, "features.get");
    rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(l, l->result, "features");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR ||
        zxm_get_bool(l, l->result, "saved", &saved) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    n = zxm_len(l, arr);
    if (n > WARREN_ENGINES) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < n; i++) {
        int32_t f = zxm_at(l, arr, i);
        warren_engine_t *e = &eng[i];
        memset(e, 0, sizeof *e);
        if (zxm_get_str(l, f, "name", e->name, sizeof e->name) != ZXM_OK || !name_ok(e->name) ||
            zxm_get_bool(l, f, "enabled", &e->enabled) != ZXM_OK ||
            zxm_get_bool(l, f, "regulated", &e->regulated) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        for (uint32_t k = 0; k < i; k++)
            if (!strcmp(eng[k].name, e->name)) return ZXM_E_PROTOCOL;
        int32_t g = zxm_get(l, f, "groups");
        if (g < 0 || l->tok[g].type != W4_J_ARR) return ZXM_E_PROTOCOL;
        e->groups = zxm_len(l, g);
        for (uint32_t k = 0; k < e->groups; k++) {
            uint64_t m = 0;
            if (zxm_get_u64(l, zxm_at(l, g, k), "methods", &m) != ZXM_OK || m < 1 || m > 65535)
                return ZXM_E_PROTOCOL;
            e->methods += (uint32_t) m;
        }
        int32_t old = find(w, e->name); /* the owner's history stays with its engine */
        if (old >= 0) {
            e->restarts = w->engine[old].restarts;
            e->changed_ms = w->engine[old].changed_ms;
        }
    }

    /* everything checked: commit */
    memcpy(w->app, app, sizeof app);
    memcpy(w->protocol, proto, sizeof proto);
    memcpy(w->libzxv, lib, sizeof lib);
    w->libzxv_features = mask;
    w->time_ms = now;
    w->uptime_s = uptime;
    w->pid = pid;
    w->rss_bytes = rss;
    memcpy(w->engine, eng, n * sizeof eng[0]);
    w->nengines = n;
    w->saved = saved;
    w->have = true;
    compose(w);
    return ZXM_OK;
}

int warren_energy(warren_t *w, uint32_t n)
{
    zxm_link_t *l = w->link;
    static warren_consumer_t c[WARREN_CONSUMERS];
    uint64_t whole = 0;
    if (n < 1 || n > WARREN_CONSUMERS) return ZXM_E_ARG;
    zxm_begin(l, "power.top");
    zxm_p_str(l, "kind", "all");
    zxm_p_u64(l, "n", n);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (l->tok[l->result].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t k = zxm_len(l, l->result);
    if (k > n) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < k; i++) {
        int32_t t = zxm_at(l, l->result, i);
        uint64_t id = 0;
        memset(&c[i], 0, sizeof c[i]);
        if (zxm_get_u64(l, t, "id", &id) != ZXM_OK || id > UINT32_MAX ||
            zxm_get_str(l, t, "kind", c[i].kind, sizeof c[i].kind) != ZXM_OK ||
            (strcmp(c[i].kind, "system") && strcmp(c[i].kind, "app") &&
             strcmp(c[i].kind, "agent")) ||
            zxm_get_str(l, t, "name", c[i].name, sizeof c[i].name) != ZXM_OK ||
            zxm_get_u64(l, t, "uj", &c[i].uj) != ZXM_OK ||
            zxm_get_u64(l, t, "mw", &c[i].mw) != ZXM_OK || c[i].uj > (uint64_t) INT64_MAX ||
            whole > (uint64_t) INT64_MAX - c[i].uj)
            return ZXM_E_PROTOCOL;
        if (i && c[i].uj > c[i - 1].uj) return ZXM_E_PROTOCOL; /* ranked, highest first */
        c[i].id = (uint32_t) id;
        whole += c[i].uj;
    }
    for (uint32_t i = 0; i < k; i++) {
        c[i].share_x100 = warren_share_x100((int64_t) c[i].uj, (int64_t) whole);
        if (c[i].share_x100 == WARREN_REFUSED) return ZXM_E_PROTOCOL;
    }
    memcpy(w->consumer, c, k * sizeof c[0]);
    w->nconsumers = k;
    w->whole_uj = whole;
    compose(w);
    return ZXM_OK;
}

int warren_events(warren_t *w, uint32_t max)
{
    zxm_link_t *l = w->link;
    static warren_event_t e[WARREN_EVENTS];
    uint64_t next = 0, dropped = 0, last = 0;
    if (max < 1 || max > WARREN_EVENTS) return ZXM_E_ARG;
    zxm_begin(l, "log.tail");
    zxm_p_u64(l, "since", w->next);
    zxm_p_u64(l, "max", max);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    uint32_t k = zxm_len(l, arr);
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || k > max ||
        zxm_get_u64(l, l->result, "next", &next) != ZXM_OK ||
        zxm_get_u64(l, l->result, "dropped", &dropped) != ZXM_OK || next < w->next)
        return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < k; i++) {
        int32_t t = zxm_at(l, arr, i);
        memset(&e[i], 0, sizeof e[i]);
        if (zxm_get_u64(l, t, "seq", &e[i].seq) != ZXM_OK ||
            zxm_get_i64(l, t, "time_ms", &e[i].time_ms) != ZXM_OK || e[i].time_ms < 0 ||
            zxm_get_str(l, t, "level", e[i].level, sizeof e[i].level) != ZXM_OK ||
            (strcmp(e[i].level, "info") && strcmp(e[i].level, "warn") &&
             strcmp(e[i].level, "error")) ||
            zxm_get_str(l, t, "source", e[i].source, sizeof e[i].source) != ZXM_OK ||
            zxm_get_str(l, t, "text", e[i].text, sizeof e[i].text) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        if (e[i].seq <= last || e[i].seq < w->next) return ZXM_E_PROTOCOL; /* oldest first */
        last = e[i].seq;
    }
    memcpy(w->event, e, k * sizeof e[0]);
    w->nevents = k;
    w->next = next;
    w->dropped += dropped;
    compose(w);
    return ZXM_OK;
}

/* WM2: the rules, before anything is sent */
static bool allowed(const warren_t *w, const warren_engine_t *e, bool enable, bool approved)
{
    int64_t own = w->owner ? 1 : 0, reg = e->regulated ? 1 : 0, ok = approved ? 1 : 0;
    if (!warren_change_ok(own, reg, ok, enable ? 1 : 0)) return false;
    if (e->regulated && enable && !warren_approval_ok(own, ok)) return false;
    return true;
}

/* features.set, taken only when zxvd says back the same engine and state */
static int set(warren_t *w, int32_t i, bool enable)
{
    zxm_link_t *l = w->link;
    char name[WARREN_NAME_MAX + 1];
    bool on = !enable, reg = false;
    zxm_begin(l, "features.set");
    zxm_p_str(l, "feature", w->engine[i].name);
    zxm_p_bool(l, "enabled", enable);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "feature", name, sizeof name) != ZXM_OK ||
        strcmp(name, w->engine[i].name) || zxm_get_bool(l, l->result, "enabled", &on) != ZXM_OK ||
        on != enable || zxm_get_bool(l, l->result, "regulated", &reg) != ZXM_OK ||
        reg != w->engine[i].regulated)
        return ZXM_E_PROTOCOL;
    w->engine[i].enabled = enable;
    return ZXM_OK;
}

/* zxvd's clock for the change; the last one read when it cannot be read */
static void stamp(warren_t *w, int32_t i)
{
    int64_t now = 0;
    uint64_t up = 0, pid = 0, rss = 0;
    if (read_sys(w, &now, &up, &pid, &rss) == ZXM_OK) {
        w->time_ms = now;
        w->uptime_s = up;
        w->pid = pid;
        w->rss_bytes = rss;
    }
    w->engine[i].changed_ms = w->time_ms;
}

int warren_switch(warren_t *w, const char *name, bool enable, bool approved)
{
    if (!name_ok(name)) return ZXM_E_ARG;
    int32_t i = find(w, name);
    if (i < 0) return ZXM_E_ARG;
    if (!allowed(w, &w->engine[i], enable, approved)) return ZXM_E_STATE;
    if (w->retry.waiting && !strcmp(w->retry.engine, name)) return ZXM_E_STATE; /* warren_retry */
    int rc = set(w, i, enable);
    if (rc != ZXM_OK) return rc;
    stamp(w, i);
    if (w->retry.gave_up && !strcmp(w->retry.engine, name)) memset(&w->retry, 0, sizeof w->retry);
    compose(w);
    return ZXM_OK;
}

/* the "on" of a restart failed: wait, or give up after the last attempt */
static void failed(warren_t *w, const char *name)
{
    w->retry.attempt++;
    if (w->retry.attempt >= w->tries) {
        w->retry.waiting = false;
        w->retry.gave_up = true;
        w->retry.wait_s = 0;
    } else {
        w->retry.waiting = true;
        w->retry.gave_up = false;
        w->retry.wait_s = warren_wait(w, w->retry.attempt);
    }
    memcpy(w->retry.engine, name, strlen(name) + 1u);
    compose(w);
}

int warren_restart(warren_t *w, const char *name, bool approved)
{
    if (!name_ok(name)) return ZXM_E_ARG;
    int32_t i = find(w, name);
    if (i < 0) return ZXM_E_ARG;
    const warren_engine_t *e = &w->engine[i];
    if (!e->enabled || w->retry.waiting || !allowed(w, e, false, approved) ||
        !allowed(w, e, true, approved))
        return ZXM_E_STATE;
    int rc = set(w, i, false);
    if (rc != ZXM_OK) return rc;
    stamp(w, i);
    memset(&w->retry, 0, sizeof w->retry);
    rc = set(w, i, true);
    if (rc != ZXM_OK) {
        failed(w, name);
        return rc;
    }
    stamp(w, i);
    w->engine[i].restarts++;
    compose(w);
    return ZXM_OK;
}

int warren_retry(warren_t *w, bool approved)
{
    if (!w->retry.waiting) return ZXM_E_STATE;
    int32_t i = find(w, w->retry.engine);
    if (i < 0) return ZXM_E_STATE;
    if (!allowed(w, &w->engine[i], true, approved)) return ZXM_E_STATE;
    int rc = set(w, i, true);
    if (rc != ZXM_OK) {
        char name[WARREN_NAME_MAX + 1];
        memcpy(name, w->retry.engine, sizeof name);
        failed(w, name);
        return rc;
    }
    stamp(w, i);
    w->engine[i].restarts++;
    memset(&w->retry, 0, sizeof w->retry);
    compose(w);
    return ZXM_OK;
}

bool warren_record(const warren_t *w, uint32_t i)
{
    if (i >= w->nengines) return false;
    const warren_engine_t *e = &w->engine[i];
    return warren_record_fits(i, e->enabled, e->regulated, e->restarts, e->changed_ms);
}

const warren_engine_t *warren_find(const warren_t *w, const char *name)
{
    int32_t i = find(w, name);
    return i < 0 ? NULL : &w->engine[i];
}
