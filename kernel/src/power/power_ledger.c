/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* power_ledger.c — see power_ledger.h. Integer only, no allocation, no libc. */
#include "power_ledger.h"

#define PW_DEFAULT_TOL 100u /* 10% */

/* ---- helpers ---------------------------------------------------------- */

static void pw_zero(void *p, size_t n)
{
    unsigned char *b = (unsigned char *) p;
    for (size_t i = 0; i < n; i++) b[i] = 0;
}

static void pw_name(char *dst, const char *src)
{
    size_t i = 0;
    if (src)
        for (; i + 1 < PW_NAME_LEN && src[i]; i++) dst[i] = src[i];
    for (; i < PW_NAME_LEN; i++) dst[i] = 0;
}

static uint64_t pw_add_sat(uint64_t a, uint64_t b)
{
    return (a > UINT64_MAX - b) ? UINT64_MAX : a + b;
}

uint64_t pw_muldiv(uint64_t a, uint64_t b, uint64_t c)
{
    if (c == 0) return 0;
    /* 128-bit product from 32-bit limbs. */
    uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32;
    uint64_t b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
    uint64_t lo = (p00 & 0xFFFFFFFFu) | (mid << 32);
    uint64_t hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    if (hi == 0) return lo / c;
    if (hi >= c) return UINT64_MAX; /* the quotient does not fit */
    uint64_t q = 0, r = hi;
    for (int i = 63; i >= 0; i--) {
        uint64_t carry = r >> 63;
        r = (r << 1) | ((lo >> i) & 1u);
        q <<= 1;
        if (carry || r >= c) {
            r -= c;
            q |= 1u;
        }
    }
    return q;
}

static bool pw_valid(const pw_ledger_t *l, uint16_t id)
{
    return l && id < PW_MAX_CONSUMERS && l->consumers[id].used;
}

/* ---- lifecycle -------------------------------------------------------- */

void pw_init(pw_ledger_t *l, pw_device_t device, pw_host_t host, uint32_t cpu_units,
             uint32_t gpu_units, uint64_t now_ms)
{
    if (!l) return;
    pw_zero(l, sizeof(*l));
    l->device = device;
    l->host = host;
    l->cpu_units = cpu_units ? cpu_units : 1u;
    l->gpu_units = gpu_units;
    l->tol_permille = PW_DEFAULT_TOL;
    l->battery.temp_dc = INT32_MIN;
    l->interval_start_ms = now_ms;
    l->last_settle_ms = now_ms;
    l->started = true;
    l->last_src = PW_SRC_NONE;
    pw_consumer_t *s = &l->consumers[PW_SYSTEM];
    s->used = true;
    s->kind = PW_KIND_SYSTEM;
    s->parent = PW_NONE;
    s->last_src = PW_SRC_NONE;
    pw_name(s->name, "system");
}

/* ---- meters ----------------------------------------------------------- */

int pw_meter_add(pw_ledger_t *l, const char *name, pw_domain_t domain, pw_scope_t scope,
                 uint64_t wrap_uj, uint64_t max_mw)
{
    if (!l || (unsigned) domain > PW_DOM_OTHER || (unsigned) scope > PW_SCOPE_SUB) return PW_E_ARG;
    if (l->nmeters >= PW_MAX_METERS) return PW_E_FULL;
    pw_meter_t *m = &l->meters[l->nmeters];
    pw_zero(m, sizeof(*m));
    pw_name(m->name, name);
    m->domain = domain;
    m->scope = scope;
    m->wrap_uj = wrap_uj;
    m->max_mw = max_mw;
    return (int) l->nmeters++;
}

int pw_meter_sample(pw_ledger_t *l, uint32_t meter, uint64_t counter_uj, uint64_t now_ms)
{
    if (!l || meter >= l->nmeters) return PW_E_ARG;
    pw_meter_t *m = &l->meters[meter];
    if (!m->have_last) {
        m->have_last = true;
        m->last_uj = counter_uj;
        m->last_ms = now_ms;
        return PW_OK;
    }
    if (now_ms < m->last_ms) return PW_E_ORDER;
    if (m->wrap_uj && counter_uj >= m->wrap_uj) {
        m->faults++; /* not a counter value at all: keep the old base */
        return PW_E_FAULT;
    }
    uint64_t d;
    bool bad = false;
    if (counter_uj >= m->last_uj) {
        d = counter_uj - m->last_uj;
    } else if (m->wrap_uj && m->last_uj < m->wrap_uj) {
        d = (m->wrap_uj - m->last_uj) + counter_uj;
    } else {
        d = 0;
        bad = true;
    }
    if (!bad && m->max_mw) {
        /* mW * ms = uJ */
        uint64_t ceiling = pw_muldiv(m->max_mw, now_ms - m->last_ms, 1u);
        if (d > ceiling) bad = true;
    }
    m->last_uj = counter_uj; /* a rejected reading becomes the new base */
    m->last_ms = now_ms;
    if (bad) {
        m->faults++;
        return PW_E_FAULT;
    }
    m->pending_uj = pw_add_sat(m->pending_uj, d);
    m->total_uj = pw_add_sat(m->total_uj, d);
    m->fresh = true;
    return PW_OK;
}

/* ---- consumers -------------------------------------------------------- */

int pw_consumer_add(pw_ledger_t *l, pw_kind_t kind, const char *name, uint16_t parent,
                    uint64_t ext_id)
{
    if (!l) return PW_E_ARG;
    if (kind == PW_KIND_AGENT) {
        if (!pw_valid(l, parent) || l->consumers[parent].kind != PW_KIND_APP) return PW_E_ARG;
    } else if (kind == PW_KIND_APP) {
        if (parent != PW_NONE) return PW_E_ARG;
    } else {
        return PW_E_ARG; /* the system consumer is made by pw_init */
    }
    for (uint16_t i = 1; i < PW_MAX_CONSUMERS; i++) {
        pw_consumer_t *c = &l->consumers[i];
        if (c->used) continue;
        pw_zero(c, sizeof(*c));
        c->used = true;
        c->kind = kind;
        c->parent = parent;
        c->ext_id = ext_id;
        c->last_src = PW_SRC_NONE;
        pw_name(c->name, name);
        return (int) i;
    }
    return PW_E_FULL;
}

int pw_consumer_find(const pw_ledger_t *l, pw_kind_t kind, uint64_t ext_id)
{
    if (!l) return PW_E_ARG;
    for (uint16_t i = 1; i < PW_MAX_CONSUMERS; i++) {
        const pw_consumer_t *c = &l->consumers[i];
        if (c->used && c->kind == kind && c->ext_id == ext_id) return (int) i;
    }
    return PW_E_ARG;
}

int pw_consumer_remove(pw_ledger_t *l, uint16_t id)
{
    if (id == PW_SYSTEM || !pw_valid(l, id)) return PW_E_ARG;
    if (l->consumers[id].kind == PW_KIND_APP)
        for (uint16_t i = 1; i < PW_MAX_CONSUMERS; i++)
            if (l->consumers[i].used && l->consumers[i].parent == id) l->consumers[i].used = false;
    l->consumers[id].used = false;
    return PW_OK;
}

int pw_note_activity(pw_ledger_t *l, uint16_t id, uint64_t cpu_ns, uint64_t gpu_ns)
{
    if (!pw_valid(l, id)) return PW_E_ARG;
    pw_consumer_t *c = &l->consumers[id];
    c->cpu_ns = pw_add_sat(c->cpu_ns, cpu_ns);
    c->gpu_ns = pw_add_sat(c->gpu_ns, gpu_ns);
    return PW_OK;
}

int pw_note_energy(pw_ledger_t *l, uint16_t id, uint64_t uj)
{
    if (!pw_valid(l, id) || l->consumers[id].kind != PW_KIND_APP) return PW_E_ARG;
    pw_consumer_t *c = &l->consumers[id];
    c->direct_uj = pw_add_sat(c->direct_uj, uj);
    c->has_direct = true;
    return PW_OK;
}

/* ---- settle ----------------------------------------------------------- */

static void pw_roll(pw_budget_t *b, uint64_t now_ms)
{
    if (!b->window_ms || now_ms < b->start_ms) return;
    uint64_t span = now_ms - b->start_ms;
    if (span >= b->window_ms) {
        b->start_ms += span - (span % b->window_ms);
        b->used_uj = 0;
    }
}

int pw_settle(pw_ledger_t *l, uint64_t now_ms)
{
    if (!l) return PW_E_ARG;
    if (now_ms < l->last_settle_ms) return PW_E_ORDER;
    uint64_t dt_ms = now_ms - l->last_settle_ms;

    /* P2: the device total for the interval. */
    bool have_whole = false, have_part = false, have_gpu = false;
    uint64_t whole_max = 0, whole_min = UINT64_MAX, part_sum = 0, gpu_sum = 0;
    for (uint32_t i = 0; i < l->nmeters; i++) {
        const pw_meter_t *m = &l->meters[i];
        if (!m->fresh) continue;
        if (m->scope == PW_SCOPE_WHOLE) {
            have_whole = true;
            if (m->pending_uj > whole_max) whole_max = m->pending_uj;
            if (m->pending_uj < whole_min) whole_min = m->pending_uj;
        } else if (m->scope == PW_SCOPE_PART) {
            have_part = true;
            part_sum = pw_add_sat(part_sum, m->pending_uj);
            if (m->domain == PW_DOM_GPU) {
                have_gpu = true;
                gpu_sum = pw_add_sat(gpu_sum, m->pending_uj);
            }
        }
    }
    uint64_t tol = l->tol_permille;
    uint64_t total = 0, other = 0;
    pw_source_t src = PW_SRC_NONE;
    bool partial = false;
    if (have_whole) {
        total = whole_max;
        src = PW_SRC_METERED;
        if (pw_muldiv(whole_max - whole_min, 1000u, 1u) > pw_muldiv(whole_max, tol, 1u)) {
            src = PW_SRC_CONFLICT;
            other = whole_min;
        }
        if (have_part && pw_muldiv(part_sum, 1000u, 1u) > pw_muldiv(whole_max, 1000u + tol, 1u)) {
            src = PW_SRC_CONFLICT;
            other = whole_max;
            total = part_sum;
        }
    } else if (have_part) {
        total = part_sum;
        src = PW_SRC_METERED;
    }

    /* P3: energy metered per consumer comes first. */
    uint64_t direct = 0;
    for (uint16_t i = 1; i < PW_MAX_CONSUMERS; i++) {
        const pw_consumer_t *c = &l->consumers[i];
        if (c->used && c->has_direct) direct = pw_add_sat(direct, c->direct_uj);
    }
    bool any_direct = false;
    for (uint16_t i = 1; i < PW_MAX_CONSUMERS; i++)
        if (l->consumers[i].used && l->consumers[i].has_direct) any_direct = true;
    if (src == PW_SRC_NONE && any_direct) {
        total = direct; /* a lower bound: only what the platform metered */
        src = PW_SRC_METERED;
        partial = true;
    } else if (src != PW_SRC_NONE && direct > total) {
        other = total;
        total = direct;
        src = PW_SRC_CONFLICT;
    }
    uint64_t pool = total - (direct < total ? direct : total);
    uint64_t gpu_pool = (have_gpu && l->gpu_units) ? (gpu_sum < pool ? gpu_sum : pool) : 0;
    uint64_t cpu_pool = pool - gpu_pool;

    uint64_t dt_ns = pw_muldiv(dt_ms, 1000000u, 1u);
    uint64_t cap_cpu = pw_muldiv(dt_ns, l->cpu_units, 1u);
    uint64_t cap_gpu = pw_muldiv(dt_ns, l->gpu_units ? l->gpu_units : 1u, 1u);
    uint64_t sum_cpu = 0, sum_gpu = 0;
    for (uint16_t i = 1; i < PW_MAX_CONSUMERS; i++) {
        const pw_consumer_t *c = &l->consumers[i];
        if (!c->used || c->parent != PW_NONE || c->has_direct) continue;
        sum_cpu = pw_add_sat(sum_cpu, c->cpu_ns);
        sum_gpu = pw_add_sat(sum_gpu, c->gpu_ns);
    }
    if (sum_cpu > cap_cpu) cap_cpu = sum_cpu; /* sampling skew: never hand out more than the pool */
    if (sum_gpu > cap_gpu) cap_gpu = sum_gpu;

    pw_source_t share_src = (src == PW_SRC_CONFLICT) ? PW_SRC_CONFLICT : PW_SRC_ATTRIBUTED;
    uint64_t given = 0;
    for (uint16_t i = 1; i < PW_MAX_CONSUMERS; i++) {
        pw_consumer_t *c = &l->consumers[i];
        if (!c->used || c->parent != PW_NONE) continue;
        if (c->has_direct) {
            c->last_uj = c->direct_uj;
            c->last_src = (src == PW_SRC_CONFLICT) ? PW_SRC_CONFLICT : PW_SRC_METERED;
        } else if (src == PW_SRC_NONE) {
            c->last_uj = 0;
            c->last_src = PW_SRC_NONE;
        } else {
            c->last_uj = pw_add_sat(pw_muldiv(cpu_pool, c->cpu_ns, cap_cpu),
                                    pw_muldiv(gpu_pool, c->gpu_ns, cap_gpu));
            c->last_src = share_src;
        }
        given = pw_add_sat(given, c->last_uj);
    }
    /* Agents split their app's energy by their share of its activity. */
    for (uint16_t p = 1; p < PW_MAX_CONSUMERS; p++) {
        pw_consumer_t *app = &l->consumers[p];
        if (!app->used || app->kind != PW_KIND_APP) continue;
        uint64_t act = pw_add_sat(app->cpu_ns, app->gpu_ns), kids = 0;
        for (uint16_t i = 1; i < PW_MAX_CONSUMERS; i++) {
            const pw_consumer_t *c = &l->consumers[i];
            if (c->used && c->parent == p)
                kids = pw_add_sat(kids, pw_add_sat(c->cpu_ns, c->gpu_ns));
        }
        uint64_t den = act > kids ? act : kids, app_e = app->last_uj, handed = 0;
        for (uint16_t i = 1; i < PW_MAX_CONSUMERS; i++) {
            pw_consumer_t *c = &l->consumers[i];
            if (!c->used || c->parent != p) continue;
            c->last_uj = pw_muldiv(app_e, pw_add_sat(c->cpu_ns, c->gpu_ns), den);
            c->last_src = (app->last_src == PW_SRC_METERED) ? PW_SRC_ATTRIBUTED : app->last_src;
            handed += c->last_uj;
        }
        app->last_uj = app_e - handed;
    }
    /* The system keeps idle, shared and rounding energy: exact conservation. */
    pw_consumer_t *sys = &l->consumers[PW_SYSTEM];
    sys->last_uj = (src == PW_SRC_NONE || given > total) ? 0 : total - given;
    sys->last_src = (src == PW_SRC_NONE) ? PW_SRC_NONE : share_src;

    for (uint16_t i = 0; i < PW_MAX_CONSUMERS; i++) {
        pw_consumer_t *c = &l->consumers[i];
        if (!c->used) continue;
        c->last_mw = dt_ms ? c->last_uj / dt_ms : 0;
        c->total_uj = pw_add_sat(c->total_uj, c->last_uj);
        c->last_cpu_ns = c->cpu_ns;
        c->last_gpu_ns = c->gpu_ns;
        c->total_cpu_ns = pw_add_sat(c->total_cpu_ns, c->cpu_ns);
        c->total_gpu_ns = pw_add_sat(c->total_gpu_ns, c->gpu_ns);
        pw_roll(&c->budget, now_ms);
        if (c->budget.window_ms) c->budget.used_uj = pw_add_sat(c->budget.used_uj, c->last_uj);
        c->cpu_ns = 0;
        c->gpu_ns = 0;
        c->direct_uj = 0;
        c->has_direct = false;
    }
    pw_roll(&l->device_budget, now_ms);
    if (l->device_budget.window_ms)
        l->device_budget.used_uj = pw_add_sat(l->device_budget.used_uj, total);
    for (uint32_t i = 0; i < l->nmeters; i++) {
        l->meters[i].pending_uj = 0;
        l->meters[i].fresh = false;
    }
    l->last_total_uj = total;
    l->last_total_mw = dt_ms ? total / dt_ms : 0;
    l->last_other_uj = other;
    l->last_src = src;
    l->last_partial = partial;
    l->total_uj = pw_add_sat(l->total_uj, total);
    l->interval_start_ms = l->last_settle_ms;
    l->last_settle_ms = now_ms;
    l->intervals++;
    if (src == PW_SRC_CONFLICT) l->conflicts++;
    return PW_OK;
}

/* ---- battery ---------------------------------------------------------- */

void pw_battery_set(pw_ledger_t *l, const pw_battery_t *b)
{
    if (!l || !b) return;
    l->battery = *b;
    if (l->battery.permille > 1000u) l->battery.permille = 1000u;
}

uint32_t pw_battery_health_permille(const pw_battery_t *b)
{
    if (!b || !b->present || !b->design_mwh) return 0;
    uint64_t h = pw_muldiv(b->full_mwh, 1000u, b->design_mwh);
    return h > UINT32_MAX ? UINT32_MAX : (uint32_t) h;
}

uint64_t pw_battery_minutes_to_empty(const pw_battery_t *b)
{
    if (!b || !b->present || b->on_mains || b->rate_mw <= 0) return 0;
    return pw_muldiv(b->now_mwh, 60u, (uint64_t) b->rate_mw);
}

uint64_t pw_battery_minutes_to_full(const pw_battery_t *b)
{
    if (!b || !b->present || !b->charging || b->rate_mw >= 0 || b->full_mwh <= b->now_mwh) return 0;
    return pw_muldiv(b->full_mwh - b->now_mwh, 60u, (uint64_t) (-(b->rate_mw + 1)) + 1u);
}

pw_posture_t pw_posture(const pw_ledger_t *l)
{
    if (!l || !l->battery.present || l->battery.on_mains) return PW_POSTURE_PERFORM;
    if (l->battery.permille <= 50u) return PW_POSTURE_CRITICAL;
    if (l->battery.permille <= 200u) return PW_POSTURE_CONSERVE;
    return PW_POSTURE_BALANCED;
}

uint32_t pw_posture_token_scale(pw_posture_t p)
{
    switch (p) {
    case PW_POSTURE_PERFORM:
        return 1000u;
    case PW_POSTURE_BALANCED:
        return 618u; /* 1/phi  */
    case PW_POSTURE_CONSERVE:
        return 382u; /* 1/phi^2 */
    default:
        return 0u;
    }
}

/* ---- budgets ---------------------------------------------------------- */

int pw_budget_set(pw_ledger_t *l, uint16_t id, uint64_t window_ms, uint64_t max_uj, bool strict,
                  uint64_t now_ms)
{
    if (!l) return PW_E_ARG;
    pw_budget_t *b;
    if (id == PW_NONE)
        b = &l->device_budget;
    else if (pw_valid(l, id))
        b = &l->consumers[id].budget;
    else
        return PW_E_ARG;
    pw_zero(b, sizeof(*b));
    if (!window_ms) return PW_OK;
    b->window_ms = window_ms;
    b->max_uj = max_uj;
    b->strict = strict;
    b->start_ms = now_ms;
    return PW_OK;
}

/* 0 ok, 1 wait, 2 hold */
static int pw_judge(const pw_budget_t *b, bool measured, uint64_t want, uint64_t now_ms)
{
    if (!b->window_ms) return 0;
    if (b->strict && !measured) return 2;
    uint64_t used = b->used_uj;
    if (now_ms >= b->start_ms && now_ms - b->start_ms >= b->window_ms) used = 0;
    return pw_add_sat(used, want) > b->max_uj ? 1 : 0;
}

static bool pw_measured(pw_source_t s)
{
    return s == PW_SRC_METERED || s == PW_SRC_ATTRIBUTED || s == PW_SRC_CONFLICT;
}

pw_admit_t pw_admit(const pw_ledger_t *l, uint16_t id, uint64_t want_uj, uint64_t now_ms)
{
    if (!l) return PW_ADMIT_BAD;
    int own = 0;
    if (id != PW_NONE) {
        if (!pw_valid(l, id)) return PW_ADMIT_BAD;
        const pw_consumer_t *c = &l->consumers[id];
        own = pw_judge(&c->budget, pw_measured(c->last_src), want_uj, now_ms);
        if (c->parent != PW_NONE && pw_valid(l, c->parent)) {
            const pw_consumer_t *p = &l->consumers[c->parent];
            int up = pw_judge(&p->budget, pw_measured(p->last_src), want_uj, now_ms);
            if (up > own) own = up;
        }
    }
    int dev = pw_judge(&l->device_budget, pw_measured(l->last_src), want_uj, now_ms);
    int v = own > dev ? own : dev;
    return v == 2 ? PW_ADMIT_HOLD : v == 1 ? PW_ADMIT_WAIT : PW_ADMIT_OK;
}

/* ---- reports ---------------------------------------------------------- */

uint32_t pw_top(const pw_ledger_t *l, int kind, bool lifetime, uint16_t *ids, uint32_t max)
{
    if (!l || !ids) return 0;
    uint32_t n = 0;
    for (uint16_t i = 0; i < PW_MAX_CONSUMERS; i++) {
        const pw_consumer_t *c = &l->consumers[i];
        if (!c->used || (kind >= 0 && (int) c->kind != kind)) continue;
        uint64_t v = lifetime ? c->total_uj : c->last_uj;
        /* insertion into the sorted prefix, keeping at most max */
        uint32_t pos = n;
        while (pos > 0) {
            const pw_consumer_t *o = &l->consumers[ids[pos - 1]];
            uint64_t ov = lifetime ? o->total_uj : o->last_uj;
            if (ov >= v) break;
            pos--;
        }
        if (pos >= max) continue;
        uint32_t end = n < max ? n : max - 1;
        for (uint32_t k = end; k > pos; k--) ids[k] = ids[k - 1];
        ids[pos] = i;
        if (n < max) n++;
    }
    return n;
}

int pw_source_truth(pw_source_t s)
{
    /* lpres_state_t: NEITHER 0, TRUE 1, FALSE 2, BOTH 3 */
    switch (s) {
    case PW_SRC_METERED:
    case PW_SRC_ATTRIBUTED:
        return 1;
    case PW_SRC_FAULT:
        return 2;
    case PW_SRC_CONFLICT:
        return 3;
    default:
        return 0;
    }
}

const char *pw_source_name(pw_source_t s)
{
    switch (s) {
    case PW_SRC_METERED:
        return "metered";
    case PW_SRC_ATTRIBUTED:
        return "attributed";
    case PW_SRC_CONFLICT:
        return "conflict";
    case PW_SRC_FAULT:
        return "fault";
    default:
        return "none";
    }
}

const char *pw_posture_name(pw_posture_t p)
{
    switch (p) {
    case PW_POSTURE_PERFORM:
        return "perform";
    case PW_POSTURE_BALANCED:
        return "balanced";
    case PW_POSTURE_CONSERVE:
        return "conserve";
    default:
        return "critical";
    }
}

const char *pw_kind_name(pw_kind_t k)
{
    switch (k) {
    case PW_KIND_APP:
        return "app";
    case PW_KIND_AGENT:
        return "agent";
    default:
        return "system";
    }
}

const char *pw_device_name(pw_device_t d)
{
    switch (d) {
    case PW_DEV_LAPTOP:
        return "laptop";
    case PW_DEV_MOBILE:
        return "mobile";
    case PW_DEV_SERVER:
        return "server";
    default:
        return "desktop";
    }
}

const char *pw_host_name(pw_host_t h)
{
    switch (h) {
    case PW_HOST_LINUX:
        return "linux";
    case PW_HOST_MACOS:
        return "macos";
    case PW_HOST_WINDOWS:
        return "windows";
    case PW_HOST_IOS:
        return "ios";
    case PW_HOST_ANDROID:
        return "android";
    default:
        return "baremetal";
    }
}

/* ---- JSON ------------------------------------------------------------- */

typedef struct {
    char *p;
    size_t cap, n;
    bool over;
} pw_out_t;

static void pw_putc(pw_out_t *o, char ch)
{
    if (o->n + 1 >= o->cap) {
        o->over = true;
        return;
    }
    o->p[o->n++] = ch;
}

static void pw_puts(pw_out_t *o, const char *s)
{
    while (*s) pw_putc(o, *s++);
}

static void pw_putu(pw_out_t *o, uint64_t v)
{
    char d[20];
    int k = 0;
    do {
        d[k++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (k) pw_putc(o, d[--k]);
}

static void pw_puti(pw_out_t *o, int64_t v)
{
    if (v < 0) {
        pw_putc(o, '-');
        pw_putu(o, (uint64_t) (-(v + 1)) + 1u);
    } else {
        pw_putu(o, (uint64_t) v);
    }
}

static void pw_putstr(pw_out_t *o, const char *s)
{
    static const char hex[] = "0123456789abcdef";
    pw_putc(o, '"');
    for (size_t i = 0; i < PW_NAME_LEN && s[i]; i++) {
        unsigned char ch = (unsigned char) s[i];
        if (ch == '"' || ch == '\\') {
            pw_putc(o, '\\');
            pw_putc(o, (char) ch);
        } else if (ch < 0x20u || ch == 0x7Fu) {
            pw_puts(o, "\\u00");
            pw_putc(o, hex[ch >> 4]);
            pw_putc(o, hex[ch & 15u]);
        } else {
            pw_putc(o, (char) ch);
        }
    }
    pw_putc(o, '"');
}

static void pw_key(pw_out_t *o, const char *k)
{
    pw_putc(o, '"');
    pw_puts(o, k);
    pw_puts(o, "\":");
}

int pw_render_json(const pw_ledger_t *l, char *out, size_t cap)
{
    if (!l || !out || !cap) return PW_E_ARG;
    pw_out_t o = {out, cap, 0, false};
    pw_puts(&o, "{");
    pw_key(&o, "device"), pw_putstr(&o, pw_device_name(l->device)), pw_putc(&o, ',');
    pw_key(&o, "host"), pw_putstr(&o, pw_host_name(l->host)), pw_putc(&o, ',');
    pw_key(&o, "interval_ms"), pw_putu(&o, l->last_settle_ms - l->interval_start_ms),
        pw_putc(&o, ',');
    pw_key(&o, "total_uj"), pw_putu(&o, l->last_total_uj), pw_putc(&o, ',');
    pw_key(&o, "total_mw"), pw_putu(&o, l->last_total_mw), pw_putc(&o, ',');
    pw_key(&o, "source"), pw_putstr(&o, pw_source_name(l->last_src)), pw_putc(&o, ',');
    pw_key(&o, "lower_bound"), pw_puts(&o, l->last_partial ? "true" : "false"), pw_putc(&o, ',');
    if (l->last_src == PW_SRC_CONFLICT) {
        pw_key(&o, "other_uj"), pw_putu(&o, l->last_other_uj), pw_putc(&o, ',');
    }
    pw_key(&o, "lifetime_uj"), pw_putu(&o, l->total_uj), pw_putc(&o, ',');
    pw_posture_t ps = pw_posture(l);
    pw_key(&o, "posture"), pw_putstr(&o, pw_posture_name(ps)), pw_putc(&o, ',');
    pw_key(&o, "token_scale_permille"), pw_putu(&o, pw_posture_token_scale(ps)), pw_putc(&o, ',');
    const pw_battery_t *b = &l->battery;
    pw_key(&o, "battery");
    if (!b->present) {
        pw_puts(&o, "null");
    } else {
        pw_puts(&o, "{");
        pw_key(&o, "on_mains"), pw_puts(&o, b->on_mains ? "true" : "false"), pw_putc(&o, ',');
        pw_key(&o, "charging"), pw_puts(&o, b->charging ? "true" : "false"), pw_putc(&o, ',');
        pw_key(&o, "permille"), pw_putu(&o, b->permille), pw_putc(&o, ',');
        pw_key(&o, "now_mwh"), pw_putu(&o, b->now_mwh), pw_putc(&o, ',');
        pw_key(&o, "full_mwh"), pw_putu(&o, b->full_mwh), pw_putc(&o, ',');
        pw_key(&o, "design_mwh"), pw_putu(&o, b->design_mwh), pw_putc(&o, ',');
        pw_key(&o, "health_permille"), pw_putu(&o, pw_battery_health_permille(b)), pw_putc(&o, ',');
        pw_key(&o, "rate_mw"), pw_puti(&o, b->rate_mw), pw_putc(&o, ',');
        pw_key(&o, "cycles"), pw_putu(&o, b->cycles), pw_putc(&o, ',');
        pw_key(&o, "minutes_to_empty"), pw_putu(&o, pw_battery_minutes_to_empty(b)),
            pw_putc(&o, ',');
        pw_key(&o, "minutes_to_full"), pw_putu(&o, pw_battery_minutes_to_full(b));
        if (b->temp_dc != INT32_MIN) {
            pw_putc(&o, ',');
            pw_key(&o, "temp_dc"), pw_puti(&o, b->temp_dc);
        }
        pw_puts(&o, "}");
    }
    pw_puts(&o, ",");
    pw_key(&o, "meters");
    pw_puts(&o, "[");
    for (uint32_t i = 0; i < l->nmeters; i++) {
        const pw_meter_t *m = &l->meters[i];
        if (i) pw_putc(&o, ',');
        pw_puts(&o, "{");
        pw_key(&o, "name"), pw_putstr(&o, m->name), pw_putc(&o, ',');
        pw_key(&o, "domain"), pw_putu(&o, (uint64_t) m->domain), pw_putc(&o, ',');
        pw_key(&o, "scope"), pw_putu(&o, (uint64_t) m->scope), pw_putc(&o, ',');
        pw_key(&o, "lifetime_uj"), pw_putu(&o, m->total_uj), pw_putc(&o, ',');
        pw_key(&o, "faults"), pw_putu(&o, m->faults);
        pw_puts(&o, "}");
    }
    pw_puts(&o, "],");
    pw_key(&o, "consumers");
    pw_puts(&o, "[");
    bool first = true;
    for (uint16_t i = 0; i < PW_MAX_CONSUMERS; i++) {
        const pw_consumer_t *c = &l->consumers[i];
        if (!c->used) continue;
        if (!first) pw_putc(&o, ',');
        first = false;
        pw_puts(&o, "{");
        pw_key(&o, "id"), pw_putu(&o, i), pw_putc(&o, ',');
        pw_key(&o, "kind"), pw_putstr(&o, pw_kind_name(c->kind)), pw_putc(&o, ',');
        pw_key(&o, "name"), pw_putstr(&o, c->name), pw_putc(&o, ',');
        pw_key(&o, "parent");
        if (c->parent == PW_NONE)
            pw_puts(&o, "null");
        else
            pw_putu(&o, c->parent);
        pw_putc(&o, ',');
        /* 64-bit ids exceed 2^53: a decimal string, as zxvd does (zxvd_rpc.h M3) */
        pw_key(&o, "ext_id"), pw_putc(&o, '"'), pw_putu(&o, c->ext_id), pw_puts(&o, "\",");
        pw_key(&o, "uj"), pw_putu(&o, c->last_uj), pw_putc(&o, ',');
        pw_key(&o, "mw"), pw_putu(&o, c->last_mw), pw_putc(&o, ',');
        pw_key(&o, "source"), pw_putstr(&o, pw_source_name(c->last_src)), pw_putc(&o, ',');
        pw_key(&o, "lifetime_uj"), pw_putu(&o, c->total_uj), pw_putc(&o, ',');
        pw_key(&o, "cpu_ns"), pw_putu(&o, c->last_cpu_ns), pw_putc(&o, ',');
        pw_key(&o, "gpu_ns"), pw_putu(&o, c->last_gpu_ns), pw_putc(&o, ',');
        pw_key(&o, "lifetime_cpu_ns"), pw_putu(&o, c->total_cpu_ns), pw_putc(&o, ',');
        pw_key(&o, "lifetime_gpu_ns"), pw_putu(&o, c->total_gpu_ns);
        if (c->budget.window_ms) {
            pw_putc(&o, ',');
            pw_key(&o, "budget");
            pw_puts(&o, "{");
            pw_key(&o, "window_ms"), pw_putu(&o, c->budget.window_ms), pw_putc(&o, ',');
            pw_key(&o, "max_uj"), pw_putu(&o, c->budget.max_uj), pw_putc(&o, ',');
            pw_key(&o, "used_uj"), pw_putu(&o, c->budget.used_uj), pw_putc(&o, ',');
            pw_key(&o, "strict"), pw_puts(&o, c->budget.strict ? "true" : "false");
            pw_puts(&o, "}");
        }
        pw_puts(&o, "}");
    }
    pw_puts(&o, "]}");
    if (o.over) {
        out[0] = 0;
        return PW_E_NOSPC;
    }
    out[o.n] = 0;
    return (int) o.n;
}
