/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_power.c — power.status, power.top and power.budget, and the agents'
 * energy around each model call (zxvd_power.h W1-W8), on the power ledger
 * (kernel/src/power) fed by zxv_power_host.c. Integers only.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L /* clock_gettime */
#endif
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#    include <windows.h>
#endif

#include "zxvd_groups.h"
#include "zxvd_power.h"
#include "zxv_power_host.h"

/* W2: the host reader holds two 4096-entry process tables (~400 KB) */
static zxv_power_host_t H;
static pw_ledger_t L;
static bool OPEN;
static uint16_t SELF; /* zxvd's own app */
static uint64_t LAST_MS;
static uint64_t (*CLOCK)(void);

void zxvd_power_set_clock(uint64_t (*fn)(void))
{
    CLOCK = fn;
}

static uint64_t now_ms(void)
{
    return CLOCK ? CLOCK() : zph_now_ms();
}

int zxvd_power_open(const char *root)
{
    OPEN = false;
    uint64_t now = now_ms();
    int self = zph_open(&H, &L, root, now) == PW_OK ? zph_app(&H, H.self_name) : -1;
    if (self < 0) return -1; /* W2: zxvd is an app */
    SELF = (uint16_t) self;
    LAST_MS = now;
    OPEN = true;
    return 0;
}

void zxvd_power_close(void)
{
    OPEN = false;
}

bool zxvd_power_is_open(void)
{
    return OPEN;
}

void zxvd_power_tick(bool force)
{
    if (!OPEN) return;
    uint64_t now = now_ms();
    if (!force && now - LAST_MS < 1000u) return;
    zph_sample(&H, now);
    pw_settle(&L, now);
    LAST_MS = now;
}

uint64_t zxvd_power_cpu_ns(void)
{
#if defined(_WIN32)
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
    uint64_t kt = ((uint64_t) k.dwHighDateTime << 32) | k.dwLowDateTime;
    uint64_t ut = ((uint64_t) u.dwHighDateTime << 32) | u.dwLowDateTime;
    return (kt + ut) * 100u; /* 100 ns ticks */
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) return 0;
    return (uint64_t) ts.tv_sec * 1000000000u + (uint64_t) ts.tv_nsec;
#endif
}

void zxvd_power_note_agent(uint64_t agent_id, const char *name, uint64_t cpu_ns, uint64_t gpu_ns)
{
    if (OPEN) zph_note_agent(&H, agent_id, name && name[0] ? name : "agent", cpu_ns, gpu_ns);
}

bool zxvd_power_admit(uint64_t agent_id)
{
    if (!OPEN) return true;
    /* W4: an agent never noted is judged as zxvd itself */
    int id = pw_consumer_find(&L, PW_KIND_AGENT, agent_id);
    return pw_admit(&L, id < 0 ? SELF : (uint16_t) id, 0, now_ms()) == PW_ADMIT_OK;
}

uint32_t zxvd_power_token_scale(void)
{
    return OPEN ? pw_posture_token_scale(pw_posture(&L)) : 1000u;
}

static bool need_open(zxvd_call_t *c)
{
    if (OPEN) return true;
    zxvd_fail(c, ZXVD_E_POWER, "power tracking is not running in this zxvd");
    return false;
}

/* W6 */
static void m_status(zxvd_call_t *c)
{
    static char js[256u * 1024u]; /* 512 consumers fit */
    if (!need_open(c)) return;
    zxvd_power_tick(false);
    int n = pw_render_json(&L, js, sizeof js);
    if (n < 0) {
        zxvd_fail(c, ZXVD_E_POWER, "the power report does not fit");
        return;
    }
    zxvd_buf_raw(zxvd_res(c), js, (size_t) n);
}

static bool p_u64(zxvd_call_t *c, const char *key, uint64_t *v)
{
    int64_t x;
    if (!zxvd_p_i64(c, key, &x)) return false;
    if (x < 0) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must not be negative", key);
        return false;
    }
    *v = (uint64_t) x;
    return true;
}

/* W6 */
static void m_top(zxvd_call_t *c)
{
    char kind[8];
    bool lifetime;
    uint32_t n = 10;
    if (!need_open(c) || !zxvd_p_str(c, "kind", kind, sizeof kind, false) ||
        !zxvd_p_bool(c, "lifetime", false, &lifetime) ||
        (zxvd_p_has(c, "n") && !zxvd_p_u32(c, "n", &n)))
        return;
    int k = -1;
    if (!strcmp(kind, "app"))
        k = PW_KIND_APP;
    else if (!strcmp(kind, "agent"))
        k = PW_KIND_AGENT;
    else if (kind[0] && strcmp(kind, "all")) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"kind\" must be \"app\", \"agent\" or \"all\"");
        return;
    }
    if (n < 1 || n > 64) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"n\" must be 1..64");
        return;
    }
    zxvd_power_tick(false);
    uint16_t ids[64];
    uint32_t got = pw_top(&L, k, lifetime, ids, n);
    zxvd_buf_t *b = zxvd_res(c);
    zxvd_buf_raw(b, "[", 1);
    for (uint32_t i = 0; i < got; i++) {
        const pw_consumer_t *p = &L.consumers[ids[i]];
        zxvd_buf_printf(b, "%s{\"id\":%u,\"kind\":\"%s\",\"name\":", i ? "," : "",
                        (unsigned) ids[i], pw_kind_name(p->kind));
        zxvd_buf_json_str(b, p->name);
        if (p->parent == PW_NONE)
            zxvd_buf_printf(b, ",\"parent\":null");
        else
            zxvd_buf_printf(b, ",\"parent\":%u", (unsigned) p->parent);
        zxvd_buf_printf(b,
                        ",\"uj\":%llu,\"mw\":%llu,\"source\":\"%s\",\"lifetime_uj\":%llu,"
                        "\"cpu_ns\":%llu}",
                        (unsigned long long) p->last_uj, (unsigned long long) p->last_mw,
                        pw_source_name(p->last_src), (unsigned long long) p->total_uj,
                        (unsigned long long) p->total_cpu_ns);
    }
    zxvd_buf_raw(b, "]", 1);
}

/* W7 */
static void m_budget(zxvd_call_t *c)
{
    char target[8], app[PW_NAME_LEN];
    uint64_t window_ms, max_uj, agent = 0;
    bool strict;
    if (!need_open(c) || !zxvd_p_str(c, "target", target, sizeof target, true) ||
        !p_u64(c, "window_ms", &window_ms) || !p_u64(c, "max_uj", &max_uj) ||
        !zxvd_p_bool(c, "strict", false, &strict))
        return;
    int id;
    if (!strcmp(target, "device")) {
        id = PW_NONE;
    } else if (!strcmp(target, "app")) {
        if (!zxvd_p_str(c, "app", app, sizeof app, true)) return;
        if (!app[0]) {
            zxvd_fail(c, ZXVD_E_PARAMS, "\"app\" must name an executable");
            return;
        }
        id = zph_app(&H, app);
        if (id < 0) {
            zxvd_fail(c, ZXVD_E_POWER, "no room for another app in the power ledger");
            return;
        }
    } else if (!strcmp(target, "agent")) {
        if (!p_u64(c, "agent", &agent)) return;
        id = pw_consumer_find(&L, PW_KIND_AGENT, agent);
        if (id < 0) {
            zxvd_fail(c, ZXVD_E_POWER, "unknown agent %llu", (unsigned long long) agent);
            return;
        }
    } else {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"target\" must be \"device\", \"app\" or \"agent\"");
        return;
    }
    if (pw_budget_set(&L, (uint16_t) id, window_ms, max_uj, strict, now_ms()) != PW_OK) {
        zxvd_fail(c, ZXVD_E_POWER, "the budget was refused");
        return;
    }
    zxvd_buf_printf(zxvd_res(c), "{\"ok\":true}");
}

static const zxvd_group_method_t METHODS[] = {
    {"power.status", m_status, true},
    {"power.top", m_top, true},
    {"power.budget", m_budget, false},
};

static const zxvd_group_t GROUP = {"power",
                                   false,
                                   "energy per app and agent, battery posture and energy budgets",
                                   METHODS,
                                   sizeof METHODS / sizeof METHODS[0],
                                   zxvd_power_close};

const zxvd_group_t *zxvd_power_group(void)
{
    return &GROUP;
}
