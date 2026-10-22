/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* keystone_model.c — Keystone's model (keystone_model.h KM1-KM4). */
#include <stdio.h>
#include <string.h>

#include "keystone_model.h"

#define RPC_NOT_FOUND (-32601) /* zxvd serves no such method */

static void say(keystone_t *k, const char *s)
{
    size_t n = strlen(s);
    if (n > KEYSTONE_NOTICE_MAX) n = KEYSTONE_NOTICE_MAX;
    memcpy(k->notice, s, n);
    k->notice[n] = 0;
}

/* an executable name power.budget takes: 1..31 printable bytes, no space */
static bool app_name_ok(const char *s)
{
    if (!s) return false;
    size_t n = 0;
    while (n <= KEYSTONE_APP_NAME && s[n]) {
        if (s[n] <= ' ' || s[n] > '~') return false;
        n++;
    }
    return n > 0 && n <= KEYSTONE_APP_NAME;
}

void keystone_init(keystone_t *k, zxm_link_t *link)
{
    if (!k) return;
    memset(k, 0, sizeof *k);
    k->link = link;
}

int keystone_refresh(keystone_t *k)
{
    if (!k || !k->link) return ZXM_E_ARG;
    zxm_link_t *l = k->link;
    uint64_t cpus = 0, mem = 0, file_max = 0;
    char root[KEYSTONE_ROOT_MAX + 1], name[KEYSTONE_MODEL_NAME + 1] = "";
    bool loaded = false, simple = false, simple_known = false;

    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "cpus", &cpus) || cpus > UINT32_MAX ||
        zxm_get_u64(l, l->result, "mem_total", &mem))
        return ZXM_E_PROTOCOL;

    zxm_begin(l, "files.root");
    rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "root", root, sizeof root) || root[0] != '/' ||
        zxm_get_u64(l, l->result, "file_max", &file_max))
        return ZXM_E_PROTOCOL;

    zxm_begin(l, "model.info");
    rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "loaded", &loaded)) return ZXM_E_PROTOCOL;
    if (loaded && zxm_get_str(l, l->result, "name", name, sizeof name)) return ZXM_E_PROTOCOL;

    zxm_begin(l, "ui.get");
    rc = zxm_call(l);
    if (rc == ZXM_OK) {
        if (zxm_get_bool(l, l->result, "simple_mode", &simple)) return ZXM_E_PROTOCOL;
        simple_known = true;
    } else if (rc != ZXM_E_REMOTE || l->err_code != RPC_NOT_FOUND) {
        return rc;
    }

    /* the whole answer is read: only now is the copy changed */
    k->cpus = (uint32_t) cpus;
    k->mem_total = mem;
    memcpy(k->root, root, sizeof root);
    k->file_max = file_max;
    k->model_loaded = loaded;
    memcpy(k->model_name, name, sizeof name);
    k->simple_known = simple_known;
    k->simple_mode = simple_known && simple;
    k->have = true;
    return ZXM_OK;
}

int keystone_set_simple(keystone_t *k, bool on, bool owner_confirmed)
{
    if (!k || !k->link) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE; /* a setting changes only on the owner's word */
    zxm_link_t *l = k->link;
    zxm_begin(l, "ui.set");
    zxm_p_bool(l, "simple_mode", on);
    int rc = zxm_call(l);
    if (rc) return rc;
    bool got = !on;
    if (zxm_get_bool(l, l->result, "simple_mode", &got) || got != on) return ZXM_E_PROTOCOL;
    k->simple_known = true;
    k->simple_mode = on;
    say(k, on ? "Simple mode is on: advanced panels are hidden."
              : "Simple mode is off: every panel is shown.");
    return ZXM_OK;
}

int keystone_propose(keystone_t *k, int64_t window, int64_t whole, const char *const *apps,
                     const int64_t *weight, uint32_t n)
{
    if (!k || !apps || !weight || n == 0 || n > KEYSTONE_APPS) return ZXM_E_ARG;
    if (k->held.pending) return ZXM_E_STATE; /* one proposal at a time */
    for (uint32_t i = 0; i < n; i++) {
        if (!app_name_ok(apps[i])) return ZXM_E_ARG;
        for (uint32_t j = 0; j < i; j++)
            if (!strcmp(apps[i], apps[j])) return ZXM_E_ARG;
    }
    int64_t share[KEYSTONE_APPS], extra[KEYSTONE_APPS], total = 0;
    if (crown_allot(whole, weight, n, share, extra, &total) != 0) return ZXM_E_ARG;
    for (uint32_t i = 0; i < n; i++)
        if (!keystone_energy_budget_ok(window, share[i], whole)) return ZXM_E_ARG;
    keystone_proposal_t p;
    memset(&p, 0, sizeof p);
    p.window = window;
    p.whole = whole;
    p.n = n;
    for (uint32_t i = 0; i < n; i++) {
        memcpy(p.app[i].app, apps[i], strlen(apps[i]) + 1u);
        p.app[i].weight = weight[i];
        p.app[i].share = share[i];
    }
    p.pending = true;
    k->held = p;
    k->applied = 0;
    char s[KEYSTONE_NOTICE_MAX + 1];
    snprintf(s, sizeof s,
             "Energy budgets for %u apps are proposed: %lld mJ every %lld ms in all. Nothing is "
             "set until you confirm.",
             (unsigned) n, (long long) whole, (long long) window);
    say(k, s);
    return ZXM_OK;
}

static int send_budget(zxm_link_t *l, const char *app, int64_t window, int64_t max_uj)
{
    zxm_begin(l, "power.budget");
    zxm_p_str(l, "target", app ? "app" : "device");
    if (app) zxm_p_str(l, "app", app);
    zxm_p_i64(l, "window_ms", window);
    zxm_p_i64(l, "max_uj", max_uj);
    int rc = zxm_call(l);
    if (rc) return rc;
    bool ok = false;
    if (zxm_get_bool(l, l->result, "ok", &ok) || !ok) return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

int keystone_apply(keystone_t *k, bool owner_confirmed)
{
    if (!k || !k->link) return ZXM_E_ARG;
    if (!k->held.pending) return ZXM_E_STATE;
    if (!owner_confirmed) return ZXM_E_STATE; /* advice until the owner says yes */
    const keystone_proposal_t *p = &k->held;
    k->applied = 0;
    for (uint32_t i = 0; i < p->n; i++) {
        int rc =
            send_budget(k->link, p->app[i].app, p->window, p->app[i].share * KEYSTONE_UJ_PER_MJ);
        if (rc) {
            say(k, "An energy budget was refused; the rest were not sent.");
            return rc;
        }
        k->applied++;
    }
    int rc = send_budget(k->link, NULL, p->window, p->whole * KEYSTONE_UJ_PER_MJ);
    if (rc) {
        say(k, "The device's energy budget was refused; the apps' budgets are set.");
        return rc;
    }
    k->applied++;
    k->held.pending = false;
    say(k, "Energy budgets are set as you confirmed.");
    return ZXM_OK;
}

int keystone_clear(keystone_t *k, const char *app, bool owner_confirmed)
{
    if (!k || !k->link || !app_name_ok(app)) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    int rc = send_budget(k->link, app, 0, 0);
    if (rc) return rc;
    say(k, "The app's energy budget is cleared.");
    return ZXM_OK;
}

void keystone_cancel(keystone_t *k)
{
    if (!k) return;
    memset(&k->held, 0, sizeof k->held);
    k->applied = 0;
    say(k, "The proposed energy budgets are dropped.");
}
