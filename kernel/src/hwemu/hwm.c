/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwm.c — the hardware emulation matrix. See hwm.h (M1-M9). */
#include "hwm.h"

/* Largest conformance output compared on the stack (M2). */
#define HWM_VEC_MAX 512u

void hwm_init(hwm_t *m)
{
    if (!m) return;
    uint8_t *p = (uint8_t *) m;
    for (uint32_t i = 0; i < (uint32_t) sizeof *m; i++) p[i] = 0;
    for (uint32_t i = 0; i < HWM_MAX_FUNCS; i++) m->fn[i].bound = HWM_NONE;
}

static void name_copy(char *dst, const char *src)
{
    uint32_t i = 0;
    if (src)
        for (; i + 1 < HWM_NAME_LEN && src[i]; i++) dst[i] = src[i];
    for (; i < HWM_NAME_LEN; i++) dst[i] = 0;
}

int hwm_func_find(const hwm_t *m, uint32_t id, uint32_t *out_index)
{
    if (!m) return HWM_E_ARG;
    for (uint32_t i = 0; i < m->nfn; i++)
        if (m->fn[i].id == id) {
            if (out_index) *out_index = i;
            return HWM_OK;
        }
    return HWM_E_NOT_FOUND;
}

int hwm_func_add(hwm_t *m, uint32_t id, const char *name, uint32_t *out_index)
{
    if (!m) return HWM_E_ARG;
    if (hwm_func_find(m, id, 0) == HWM_OK) return HWM_E_DUPLICATE;
    if (m->nfn >= HWM_MAX_FUNCS) return HWM_E_FULL;
    hwm_func_t *f = &m->fn[m->nfn];
    f->id = id;
    name_copy(f->name, name);
    f->nvec = 0;
    f->bound = HWM_NONE;
    f->failovers = 0;
    f->calls = 0;
    f->memo = 0;
    f->memo_cap = 0;
    f->memo_hits = f->memo_misses = 0;
    f->contract = 0;
    f->flags = 0;
    f->max_latency_ns = 0;
    f->power_budget_mw = 0;
    if (out_index) *out_index = m->nfn;
    m->nfn++;
    return HWM_OK;
}

int hwm_vector_add(hwm_t *m, uint32_t fn_index, uint32_t op, const uint8_t *in, uint32_t in_len,
                   const uint8_t *expect, uint32_t expect_len)
{
    if (!m || fn_index >= m->nfn || (in_len && !in) || (expect_len && !expect) ||
        expect_len > HWM_VEC_MAX)
        return HWM_E_ARG;
    hwm_func_t *f = &m->fn[fn_index];
    if (f->nvec >= HWM_MAX_VECTORS) return HWM_E_FULL;
    hwm_vector_t *v = &f->vec[f->nvec++];
    v->op = op;
    v->in = in;
    v->in_len = in_len;
    v->expect = expect;
    v->expect_len = expect_len;
    /* The contract changed: earlier proofs no longer cover it. */
    for (uint32_t i = 0; i < m->npv; i++)
        if (m->pv[i].fn == fn_index) m->pv[i].attest = LPRES_STATE_NEITHER;
    f->bound = HWM_NONE;
    hwm_memo_flush(m, fn_index);
    return HWM_OK;
}

int hwm_contract_begin(hwm_t *m, uint32_t fn_index, uint32_t version)
{
    if (!m || fn_index >= m->nfn || m->depth != 0) return HWM_E_ARG;
    hwm_func_t *f = &m->fn[fn_index];
    if (version <= f->contract) return HWM_E_ARG; /* M9: forward only */
    f->contract = version;
    f->nvec = 0;
    for (uint32_t i = 0; i < m->npv; i++)
        if (m->pv[i].fn == fn_index) m->pv[i].attest = LPRES_STATE_NEITHER;
    f->bound = HWM_NONE;
    hwm_memo_flush(m, fn_index);
    return HWM_OK;
}

static void memo_wipe(hwm_func_t *f)
{
    if (!f->memo) return;
    volatile uint8_t *b = (volatile uint8_t *) f->memo;
    uint32_t n = f->memo_cap * (uint32_t) sizeof *f->memo;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

int hwm_policy_set(hwm_t *m, uint32_t fn_index, uint32_t flags, uint32_t max_latency_ns,
                   uint32_t power_budget_mw)
{
    if (!m || fn_index >= m->nfn || (flags & ~HWM_F_MASK) || m->depth != 0) return HWM_E_ARG;
    hwm_func_t *f = &m->fn[fn_index];
    if ((f->flags & HWM_F_KEY_BEARING) && !(flags & HWM_F_KEY_BEARING)) return HWM_E_POLICY;
    if ((flags & HWM_F_KEY_BEARING) && f->memo) {
        /* M8: a key-bearing function keeps no stored inputs or outputs. */
        memo_wipe(f);
        f->memo = 0;
        f->memo_cap = 0;
    }
    f->flags = flags;
    f->max_latency_ns = max_latency_ns;
    f->power_budget_mw = power_budget_mw;
    f->bound = HWM_NONE;
    hwm_memo_flush(m, fn_index);
    return HWM_OK;
}

int hwm_resource_find(const hwm_t *m, uint32_t id, uint32_t *out_index)
{
    if (!m) return HWM_E_ARG;
    for (uint32_t i = 0; i < m->nres; i++)
        if (m->res[i].id == id) {
            if (out_index) *out_index = i;
            return HWM_OK;
        }
    return HWM_E_NOT_FOUND;
}

int hwm_resource_add(hwm_t *m, uint32_t id, uint32_t *out_index)
{
    if (!m) return HWM_E_ARG;
    if (hwm_resource_find(m, id, 0) == HWM_OK) return HWM_E_DUPLICATE;
    if (m->nres >= HWM_MAX_RESOURCES) return HWM_E_FULL;
    m->res[m->nres].id = id;
    m->res[m->nres].state = HWM_RES_OK;
    if (out_index) *out_index = m->nres;
    m->nres++;
    return HWM_OK;
}

int hwm_resource_set(hwm_t *m, uint32_t res_index, hwm_res_state_t st)
{
    if (!m || res_index >= m->nres || (uint32_t) st > (uint32_t) HWM_RES_OFFLINE) return HWM_E_ARG;
    m->res[res_index].state = st;
    return HWM_OK; /* bindings are re-checked on every call (M4) */
}

int hwm_provider_add(hwm_t *m, uint32_t fn_index, uint32_t res_index, hwm_kind_t kind,
                     uint32_t cost, hwm_call_fn call, void *inst, uint32_t *out_index)
{
    if (!m || fn_index >= m->nfn || res_index >= m->nres || !call ||
        (uint32_t) kind >= (uint32_t) HWM_KIND_COUNT)
        return HWM_E_ARG;
    if (m->npv >= HWM_MAX_PROVIDERS) return HWM_E_FULL;
    hwm_provider_t *p = &m->pv[m->npv];
    p->fn = fn_index;
    p->resource = res_index;
    p->kind = kind;
    p->cost = cost;
    p->call = call;
    p->inst = inst;
    p->ndeps = 0;
    p->attest = LPRES_STATE_NEITHER;
    p->enabled = true;
    p->calls = 0;
    p->cost_spent = 0;
    p->latency_ns = 0;
    p->power_mw = 0;
    p->throughput_class = 0;
    p->proven_contract = 0;
    if (out_index) *out_index = m->npv;
    m->npv++;
    return HWM_OK;
}

int hwm_provider_dep(hwm_t *m, uint32_t pv_index, uint32_t dep_fn_index)
{
    if (!m || pv_index >= m->npv || dep_fn_index >= m->nfn) return HWM_E_ARG;
    hwm_provider_t *p = &m->pv[pv_index];
    if (dep_fn_index == p->fn) return HWM_E_DEP; /* a function cannot lean on itself */
    for (uint32_t i = 0; i < p->ndeps; i++)
        if (p->deps[i] == dep_fn_index) return HWM_E_DUPLICATE;
    if (p->ndeps >= HWM_MAX_DEPS) return HWM_E_FULL;
    p->deps[p->ndeps++] = dep_fn_index;
    return HWM_OK;
}

int hwm_provider_traits(hwm_t *m, uint32_t pv_index, uint32_t latency_ns, uint32_t power_mw,
                        uint32_t throughput_class)
{
    if (!m || pv_index >= m->npv || throughput_class > 15u) return HWM_E_ARG;
    hwm_provider_t *p = &m->pv[pv_index];
    p->latency_ns = latency_ns;
    p->power_mw = power_mw;
    p->throughput_class = (uint8_t) throughput_class;
    m->fn[p->fn].bound = HWM_NONE; /* placement may change (M8) */
    return HWM_OK;
}

uint32_t hwm_power_class(uint32_t mw)
{
    if (mw == 0) return 0;
    uint32_t c = 1;
    for (uint32_t lim = 10; c < 6 && mw > lim; lim *= 10) c++;
    return c;
}

int hwm_provider_enable(hwm_t *m, uint32_t pv_index, bool on)
{
    if (!m || pv_index >= m->npv) return HWM_E_ARG;
    m->pv[pv_index].enabled = on;
    return HWM_OK;
}

int hwm_provider_retarget(hwm_t *m, uint32_t pv_index, uint32_t fn_index)
{
    if (!m || pv_index >= m->npv || fn_index >= m->nfn) return HWM_E_ARG;
    hwm_provider_t *p = &m->pv[pv_index];
    p->fn = fn_index;
    p->ndeps = 0;
    p->attest = LPRES_STATE_NEITHER;
    return HWM_OK;
}

int hwm_provider_set_attest(hwm_t *m, uint32_t pv_index, lpres_state_t st)
{
    if (!m || pv_index >= m->npv || (uint32_t) st > (uint32_t) LPRES_STATE_BOTH) return HWM_E_ARG;
    m->pv[pv_index].attest = st;
    return HWM_OK;
}

/* ---- selection (M3, M4, M5) ---- */

static uint32_t select_inner(const hwm_t *m, uint32_t fn, uint32_t mask, uint32_t depth,
                             uint32_t exclude, bool local);

static bool fn_local(const hwm_func_t *f)
{
    return (f->flags & (HWM_F_KEY_BEARING | HWM_F_LOCAL_ONLY)) != 0;
}

/* M8: may this provider serve its function under the function's policy?
 * `local` carries locality down from a key-bearing or local-only caller.
 * A declared ceiling is met only by a declared value (unknown never acts). */
static bool policy_ok(const hwm_t *m, uint32_t pv, bool local)
{
    const hwm_provider_t *p = &m->pv[pv];
    const hwm_func_t *f = &m->fn[p->fn];
    if (p->kind == HWM_KIND_REMOTE && (local || fn_local(f))) return false;
    if (f->max_latency_ns && (p->latency_ns == 0 || p->latency_ns > f->max_latency_ns))
        return false;
    if (f->power_budget_mw && (p->power_mw == 0 || p->power_mw > f->power_budget_mw)) return false;
    return true;
}

static bool can_serve(const hwm_t *m, uint32_t pv, uint32_t mask, uint32_t depth, bool local)
{
    const hwm_provider_t *p = &m->pv[pv];
    if (!p->enabled || p->attest != LPRES_STATE_TRUE || !p->call) return false;
    if (m->res[p->resource].state != HWM_RES_OK) return false;
    if (!policy_ok(m, pv, local)) return false;
    local = local || fn_local(&m->fn[p->fn]);
    for (uint32_t i = 0; i < p->ndeps; i++) {
        uint32_t d = p->deps[i];
        if (mask & (1u << d)) return false; /* would revisit a function on the path */
        if (depth + 1u > HWM_MAX_DEPTH) return false;
        if (select_inner(m, d, mask | (1u << d), depth + 1u, HWM_NONE, local) == HWM_NONE)
            return false;
    }
    return true;
}

static bool better(const hwm_t *m, uint32_t a, uint32_t b)
{
    const hwm_provider_t *pa = &m->pv[a], *pb = &m->pv[b];
    if (pa->cost != pb->cost) return pa->cost < pb->cost;
    if (pa->kind != pb->kind) return pa->kind < pb->kind;
    return a < b;
}

static uint32_t select_inner(const hwm_t *m, uint32_t fn, uint32_t mask, uint32_t depth,
                             uint32_t exclude, bool local)
{
    uint32_t best = HWM_NONE;
    for (uint32_t i = 0; i < m->npv; i++) {
        if (i == exclude || m->pv[i].fn != fn) continue;
        if (!can_serve(m, i, mask, depth, local)) continue;
        if (best == HWM_NONE || better(m, i, best)) best = i;
    }
    return best;
}

/* Functions on the active call path, as a mask. */
static uint32_t stack_mask(const hwm_t *m)
{
    uint32_t mask = 0;
    for (uint32_t i = 0; i < m->depth; i++) mask |= 1u << m->pv[m->stack[i]].fn;
    return mask;
}

/* M8: is any function on the active call path key-bearing or local-only? */
static bool stack_local(const hwm_t *m)
{
    for (uint32_t i = 0; i < m->depth; i++)
        if (fn_local(&m->fn[m->pv[m->stack[i]].fn])) return true;
    return false;
}

uint32_t hwm_select(const hwm_t *m, uint32_t fn_index)
{
    if (!m || fn_index >= m->nfn) return HWM_NONE;
    return select_inner(m, fn_index, 1u << fn_index, 0, HWM_NONE, false);
}

int hwm_bind(hwm_t *m, uint32_t fn_index, uint32_t *out_pv)
{
    if (!m || fn_index >= m->nfn) return HWM_E_ARG;
    hwm_func_t *f = &m->fn[fn_index];
    uint32_t prev = f->bound;
    uint32_t pv = hwm_select(m, fn_index);
    if (prev != HWM_NONE && pv != prev &&
        !(prev < m->npv && m->pv[prev].fn == fn_index &&
          can_serve(m, prev, 1u << fn_index, 0, false)))
        f->failovers++;
    if (pv != prev) hwm_memo_flush(m, fn_index); /* M7: answers come from who serves now */
    f->bound = pv;
    if (out_pv) *out_pv = pv;
    return pv == HWM_NONE ? HWM_E_NOPROVIDER : HWM_OK;
}

/* ---- memo (M7) ---- */

static uint64_t memo_key(uint32_t op, const uint8_t *in, uint32_t n)
{
    uint64_t h = 1469598103934665603ull ^ op; /* FNV-1a: picks a slot only */
    for (uint32_t i = 0; i < n; i++) h = (h ^ in[i]) * 1099511628211ull;
    return h ^ (h >> 29);
}

static bool bytes_eq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i]) return false;
    return true;
}

void hwm_memo_flush(hwm_t *m, uint32_t fn_index)
{
    if (!m || fn_index >= m->nfn) return;
    hwm_func_t *f = &m->fn[fn_index];
    for (uint32_t i = 0; f->memo && i < f->memo_cap; i++) f->memo[i].used = false;
}

int hwm_memo_attach(hwm_t *m, uint32_t fn_index, hwm_memo_entry_t *entries, uint32_t cap)
{
    if (!m || fn_index >= m->nfn) return HWM_E_ARG;
    if (entries && (cap == 0 || (cap & (cap - 1u)) != 0)) return HWM_E_ARG;
    hwm_func_t *f = &m->fn[fn_index];
    if (entries && (f->flags & HWM_F_KEY_BEARING)) return HWM_E_POLICY; /* M8 */
    f->memo = entries;
    f->memo_cap = entries ? cap : 0;
    f->memo_hits = f->memo_misses = 0;
    hwm_memo_flush(m, fn_index);
    return HWM_OK;
}

/* ---- calling ---- */

static int run(hwm_t *m, uint32_t pv, uint32_t op, const uint8_t *in, uint32_t in_len, uint8_t *out,
               uint32_t out_cap, uint32_t *out_len)
{
    if (m->depth > HWM_MAX_DEPTH) return HWM_E_DEPTH;
    hwm_provider_t *p = &m->pv[pv];
    uint32_t got = 0;
    m->stack[m->depth++] = pv;
    int rc = p->call(p->inst, m, op, in, in_len, out, out_cap, &got);
    m->depth--;
    p->calls++;
    p->cost_spent += p->cost;
    /* A nested M5 refusal and an output buffer too small for the result
     * are reported as themselves; anything else the provider returns is
     * its own error. */
    if (rc < 0)
        return rc == HWM_E_DEP || rc == HWM_E_DEPTH || rc == HWM_E_BUFFER ? rc : HWM_E_PROVIDER;
    if (got > out_cap) return HWM_E_BUFFER;
    if (out_len) *out_len = got;
    return HWM_OK;
}

int hwm_call(hwm_t *m, uint32_t fn_index, uint32_t op, const uint8_t *in, uint32_t in_len,
             uint8_t *out, uint32_t out_cap, uint32_t *out_len)
{
    if (!m || fn_index >= m->nfn || (in_len && !in) || (out_cap && !out)) return HWM_E_ARG;
    uint32_t pv;
    hwm_memo_entry_t *slot = 0;
    if (m->depth == 0) {
        int rc = hwm_bind(m, fn_index, &pv);
        if (rc != HWM_OK) return rc;
        hwm_func_t *f = &m->fn[fn_index];
        if (f->memo && in_len <= HWM_MEMO_BYTES) {
            slot = &f->memo[memo_key(op, in, in_len) & (f->memo_cap - 1u)];
            if (slot->used && slot->op == op && slot->in_len == in_len &&
                bytes_eq(slot->in, in, in_len)) {
                if (slot->out_len > out_cap) return HWM_E_BUFFER;
                for (uint32_t i = 0; i < slot->out_len; i++) out[i] = slot->out[i];
                if (out_len) *out_len = slot->out_len;
                f->calls++;
                f->memo_hits++;
                return HWM_OK;
            }
            f->memo_misses++;
        }
    } else {
        /* M5: only a declared dependency, never a function already on the path. */
        const hwm_provider_t *caller = &m->pv[m->stack[m->depth - 1u]];
        bool declared = false;
        for (uint32_t i = 0; i < caller->ndeps; i++)
            if (caller->deps[i] == fn_index) declared = true;
        if (!declared) return HWM_E_DEP;
        uint32_t mask = stack_mask(m);
        if (mask & (1u << fn_index)) return HWM_E_DEP;
        if (m->depth >= HWM_MAX_DEPTH) return HWM_E_DEPTH;
        pv = select_inner(m, fn_index, mask | (1u << fn_index), m->depth, HWM_NONE, stack_local(m));
        if (pv == HWM_NONE) return HWM_E_NOPROVIDER;
    }
    m->fn[fn_index].calls++;
    uint32_t got = 0;
    int rc = run(m, pv, op, in, in_len, out, out_cap, &got);
    if (rc != HWM_OK) return rc;
    if (out_len) *out_len = got;
    if (slot && got <= HWM_MEMO_BYTES) {
        slot->used = true;
        slot->op = op;
        slot->in_len = in_len;
        slot->out_len = got;
        for (uint32_t i = 0; i < in_len; i++) slot->in[i] = in[i];
        for (uint32_t i = 0; i < got; i++) slot->out[i] = out[i];
    }
    return HWM_OK;
}

int hwm_shadow(hwm_t *m, uint32_t fn_index, uint32_t op, const uint8_t *in, uint32_t in_len,
               uint8_t *out, uint8_t *scratch, uint32_t out_cap, uint32_t *out_len,
               bool *out_checked)
{
    if (out_checked) *out_checked = false;
    if (!m || fn_index >= m->nfn || (out_cap && (!out || !scratch)) || m->depth != 0)
        return HWM_E_ARG;
    uint32_t a;
    int rc = hwm_bind(m, fn_index, &a);
    if (rc != HWM_OK) return rc;
    uint32_t b = select_inner(m, fn_index, 1u << fn_index, 0, a, false);
    uint32_t la = 0, lb = 0;
    m->fn[fn_index].calls++;
    rc = run(m, a, op, in, in_len, out, out_cap, &la);
    if (rc != HWM_OK) return rc;
    if (out_len) *out_len = la;
    if (b == HWM_NONE) return HWM_OK;
    rc = run(m, b, op, in, in_len, scratch, out_cap, &lb);
    bool same = rc == HWM_OK && la == lb;
    for (uint32_t i = 0; same && i < la; i++)
        if (out[i] != scratch[i]) same = false;
    if (out_checked) *out_checked = true;
    if (!same) {
        /* M6: no winner is picked; both stop serving until re-attested. */
        m->pv[a].attest = LPRES_STATE_BOTH;
        m->pv[b].attest = LPRES_STATE_BOTH;
        m->fn[fn_index].bound = HWM_NONE;
        hwm_memo_flush(m, fn_index);
        return HWM_E_DISPUTE;
    }
    return HWM_OK;
}

/* ---- conformance (M2) ---- */

lpres_state_t hwm_attest(hwm_t *m, uint32_t pv_index, uint32_t *out_failed)
{
    if (out_failed) *out_failed = HWM_NONE;
    if (!m || pv_index >= m->npv || m->depth != 0) return LPRES_STATE_NEITHER;
    hwm_provider_t *p = &m->pv[pv_index];
    const hwm_func_t *f = &m->fn[p->fn];
    if (f->nvec == 0) {
        p->attest = LPRES_STATE_NEITHER;
        return p->attest;
    }
    uint8_t buf[HWM_VEC_MAX];
    for (uint32_t k = 0; k < f->nvec; k++) {
        const hwm_vector_t *v = &f->vec[k];
        uint32_t got = 0;
        int rc = run(m, pv_index, v->op, v->in, v->in_len, buf, HWM_VEC_MAX, &got);
        bool same = rc == HWM_OK && got == v->expect_len;
        for (uint32_t i = 0; same && i < got; i++)
            if (buf[i] != v->expect[i]) same = false;
        if (!same) {
            p->attest = LPRES_STATE_FALSE;
            if (out_failed) *out_failed = k;
            return p->attest;
        }
    }
    p->attest = LPRES_STATE_TRUE;
    p->proven_contract = f->contract;
    return p->attest;
}

uint32_t hwm_provider_flags(const hwm_t *m, uint32_t pv)
{
    if (!m || pv >= m->npv) return 0;
    const hwm_provider_t *p = &m->pv[pv];
    uint32_t fl = 0;
    hwm_res_state_t rs = m->res[p->resource].state;
    if (rs == HWM_RES_FAULTED || rs == HWM_RES_OFFLINE) fl |= HWM_PF_FAULT;
    if (p->attest == LPRES_STATE_BOTH) fl |= HWM_PF_DISPUTED;
    if (p->attest == LPRES_STATE_FALSE) fl |= HWM_PF_FAILED;
    if (p->attest == LPRES_STATE_NEITHER) fl |= HWM_PF_UNPROVEN;
    if (!p->enabled) fl |= HWM_PF_DISABLED;
    if (!policy_ok(m, pv, false)) fl |= HWM_PF_POLICY;
    if (m->fn[p->fn].bound == pv) fl |= HWM_PF_SERVING;
    return fl;
}

const char *hwm_kind_name(hwm_kind_t k)
{
    switch (k) {
    case HWM_KIND_HARD:
        return "hard";
    case HWM_KIND_FABRIC:
        return "fabric";
    case HWM_KIND_MICRO:
        return "micro";
    case HWM_KIND_PEER:
        return "peer";
    case HWM_KIND_SOFT:
        return "soft";
    case HWM_KIND_REMOTE:
        return "remote";
    default:
        return "invalid";
    }
}

bool hwm_kind_reconfigurable(hwm_kind_t k)
{
    return k == HWM_KIND_FABRIC || k == HWM_KIND_MICRO;
}
