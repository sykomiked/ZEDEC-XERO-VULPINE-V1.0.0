/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_econ.c — ABI 1.1: rational resource budgets (kernel/src/rmag, K2),
 * phase-coordinator ticks (kernel/src/phase_coord, K6) and the swarm token
 * economy (kernel/src/swarm: swarm_budget + swarm_emotion).
 *
 * Exactness: rmag's own arithmetic saturates on overflow. This adapter
 * checks every sum first with rational.h (128-bit intermediates) and
 * refuses an amount that would overflow with ZXV_E_RMAG_INVALID, so the
 * kernel never sees a value it would have to saturate. */
#define _POSIX_C_SOURCE 200809L
#include "zxv_internal.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "phase_coord.h"
#include "rational.h"
#include "rmag.h"
#include "swarm_budget.h"
#include "swarm_emotion.h"

/* ===================================================================== */
/* rmag                                                                  */
/* ===================================================================== */

struct zxv_rmag {
    zxv_ctx *ctx;
    pthread_mutex_t mu;
    pthread_cond_t idle;  /* signalled when inflight drops to zero        */
    uint32_t inflight;    /* listener calls started and not yet returned   */
    rmag_registry_t reg;
    uint64_t epoch[RMAG_MAX_BUDGET_ENTRIES];
    zxv_rmag_listener listener;
    void *user;
};

/* Listener delivery (ABI 1.2).
 *
 * A call decides under the budget lock whether an event fires, copies the
 * listener and its user pointer, and counts the delivery as in flight
 * (inflight++) BEFORE it drops the lock. zxv_rmag_listen replaces the
 * listener and then waits until no delivery is in flight, so once it
 * returns the old listener is not running on any thread and never will be:
 * the host may free the old `user` immediately. (Without this a removal
 * could return while another thread was still inside, or about to enter,
 * the old callback: a use-after-free of `user`.)
 *
 * A listener may call back into the library, including zxv_rmag_listen on
 * its own zxv_rmag. That call cannot wait for itself, so from inside a
 * callback it swaps the listener without waiting (the frames list below
 * tells it so). The calling thread's error detail (R7) is saved around the
 * callback and restored, so the status detail the outer call returns is the
 * outer call's, whatever the listener did. */
typedef struct cb_frame {
    const zxv_rmag *m;
    struct cb_frame *next;
} cb_frame;
static _Thread_local cb_frame *tl_frames;

static bool in_callback_of(const zxv_rmag *m)
{
    for (const cb_frame *f = tl_frames; f; f = f->next)
        if (f->m == m) return true;
    return false;
}

/* Call with m->mu held: claim a delivery if there is a listener. */
static zxv_rmag_listener claim(zxv_rmag *m, void **user)
{
    zxv_rmag_listener fn = m->listener;
    *user = m->user;
    if (fn) m->inflight++;
    return fn;
}

/* Call without the lock, once per successful claim. */
static void deliver(zxv_rmag *m, uint32_t budget, int32_t ev, zxv_rmag_listener fn, void *user)
{
    if (!fn) return;
    char saved[ZXVI_ERR_MAX];
    zxvi_err_save(saved);
    cb_frame f = {m, tl_frames};
    tl_frames = &f;
    fn(user, budget, ev);
    tl_frames = f.next;
    zxvi_err_restore(saved);
    pthread_mutex_lock(&m->mu);
    if (--m->inflight == 0) pthread_cond_broadcast(&m->idle);
    pthread_mutex_unlock(&m->mu);
}

static int32_t to_rmag(int64_t num, int64_t den, bool allow_zero, rmag_rational_t *out,
                       rat_t *exact)
{
    if (den == 0 || num == INT64_MIN || den == INT64_MIN)
        return zxvi_fail(ZXV_E_RMAG_INVALID, "amount %lld/%lld is not a valid rational",
                         (long long) num, (long long) den);
    rat_t r = rat_make(num, den);
    if (!r.valid) return zxvi_fail(ZXV_E_RMAG_INVALID, "amount does not normalize");
    if (r.num < 0) return zxvi_fail(ZXV_E_RMAG_INVALID, "amount is negative");
    if (r.num == 0 && !allow_zero) return zxvi_fail(ZXV_E_RMAG_INVALID, "amount is zero");
    *out = rmag_rational_from_frac((uint64_t) r.num, (uint64_t) r.den, false);
    if (exact) *exact = r;
    return ZXV_OK;
}

static rat_t from_rmag(rmag_rational_t r)
{
    if (r.numerator > (uint64_t) INT64_MAX || r.denominator > (uint64_t) INT64_MAX ||
        r.denominator == 0)
        return (rat_t){0, 1, false};
    rat_t x = rat_make((int64_t) r.numerator, (int64_t) r.denominator);
    return r.negative ? rat_neg(x) : x;
}

int32_t zxv_rmag_create(zxv_ctx *ctx, zxv_rmag **out_rmag)
{
    if (!ctx || !out_rmag) return zxvi_fail(ZXV_E_ARG, "ctx or out_rmag is NULL");
    int32_t rc = zxvi_ctx_check(ctx, "rmag_create");
    if (rc) return rc;
    zxv_rmag *m = calloc(1, sizeof *m);
    if (!m) return zxvi_fail(ZXV_E_NOMEM, "rmag allocation failed");
    m->ctx = ctx;
    pthread_mutex_init(&m->mu, NULL);
    pthread_cond_init(&m->idle, NULL);
    rmag_registry_init(&m->reg);
    *out_rmag = m;
    return zxvi_ok();
}

int32_t zxv_rmag_create_cap(const zxv_cap *cap, zxv_rmag **out_rmag)
{
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_BUDGET, "rmag_create", &ctx);
    return rc ? rc : zxv_rmag_create(ctx, out_rmag);
}

void zxv_rmag_destroy(zxv_rmag *rmag)
{
    if (!rmag) return;
    pthread_cond_destroy(&rmag->idle);
    pthread_mutex_destroy(&rmag->mu);
    free(rmag);
}

zxv_ctx *zxvi_rmag_ctx(const zxv_rmag *rmag);
zxv_ctx *zxvi_rmag_ctx(const zxv_rmag *rmag)
{
    return rmag->ctx;
}

int32_t zxv_rmag_budget(const zxv_cap *cap, zxv_rmag *rmag, const char *name, int32_t resource,
                        int64_t limit_num, int64_t limit_den, int32_t enforce, uint32_t *out_budget)
{
    if (!rmag || !out_budget) return zxvi_fail(ZXV_E_ARG, "rmag or out_budget is NULL");
    int32_t rc = zxvi_cap_gate_ctx(cap, ZXV_CAP_BUDGET, "rmag_budget", rmag->ctx);
    if (rc) return rc;
    if ((rc = zxvi_check_str(name, RMAG_MAX_NAME_LEN - 1, "name", false))) return rc;
    if (resource < 0 || resource >= RMAG_RES_COUNT)
        return zxvi_fail(ZXV_E_ARG, "resource %d is not 0..%d", resource, RMAG_RES_COUNT - 1);
    rmag_rational_t lim;
    if ((rc = to_rmag(limit_num, limit_den, true, &lim, NULL))) return rc;
    pthread_mutex_lock(&rmag->mu);
    int32_t idx = rmag_create_budget(&rmag->reg, name, (rmag_resource_t) resource, lim, enforce != 0);
    pthread_mutex_unlock(&rmag->mu);
    if (idx < 0) return zxvi_fail(ZXV_E_CAPACITY, "at most %d budgets", RMAG_MAX_BUDGET_ENTRIES);
    *out_budget = (uint32_t) idx;
    return zxvi_ok();
}

int32_t zxv_rmag_consume(const zxv_cap *cap, zxv_rmag *rmag, uint32_t budget, int64_t num,
                         int64_t den)
{
    if (!rmag) return zxvi_fail(ZXV_E_ARG, "rmag is NULL");
    int32_t rc = zxvi_cap_gate_ctx(cap, ZXV_CAP_BUDGET, "rmag_consume", rmag->ctx);
    if (rc) return rc;
    rmag_rational_t amt;
    rat_t exact;
    pthread_mutex_lock(&rmag->mu);
    if (budget >= rmag->reg.budget_count) {
        pthread_mutex_unlock(&rmag->mu);
        return zxvi_fail(ZXV_E_NOT_FOUND, "budget %u does not exist", budget);
    }
    rc = to_rmag(num, den, false, &amt, &exact);
    if (rc == ZXV_OK) {
        rat_t sum = rat_add(from_rmag(rmag->reg.budgets[budget].consumed), exact);
        if (!sum.valid) rc = zxvi_fail(ZXV_E_RMAG_INVALID, "consumed total would overflow");
    }
    if (rc == ZXV_OK) {
        rmag_result_t r = rmag_consume(&rmag->reg, budget, amt);
        rc = r == RMAG_RESULT_OK       ? zxvi_ok()
             : r == RMAG_RESULT_DENIED ? zxvi_fail(ZXV_E_BUDGET, "budget %u (\"%s\") is exhausted",
                                                   budget, rmag->reg.budgets[budget].name)
                                       : zxvi_fail(ZXV_E_RMAG_INVALID, "rmag refused the amount (%s)",
                                                   rmag_result_name(r));
    }
    int32_t ev = rc == ZXV_E_BUDGET ? ZXV_RMAG_EV_DENIED : rc == ZXV_E_RMAG_INVALID ? ZXV_RMAG_EV_INVALID : 0;
    void *user = NULL;
    zxv_rmag_listener fn = ev ? claim(rmag, &user) : NULL;
    pthread_mutex_unlock(&rmag->mu);
    deliver(rmag, budget, ev, fn, user);
    return rc;
}

int32_t zxv_rmag_release(const zxv_cap *cap, zxv_rmag *rmag, uint32_t budget, int64_t num,
                         int64_t den)
{
    if (!rmag) return zxvi_fail(ZXV_E_ARG, "rmag is NULL");
    int32_t rc = zxvi_cap_gate_ctx(cap, ZXV_CAP_BUDGET, "rmag_release", rmag->ctx);
    if (rc) return rc;
    rmag_rational_t amt;
    pthread_mutex_lock(&rmag->mu);
    if (budget >= rmag->reg.budget_count) {
        pthread_mutex_unlock(&rmag->mu);
        return zxvi_fail(ZXV_E_NOT_FOUND, "budget %u does not exist", budget);
    }
    rc = to_rmag(num, den, false, &amt, NULL);
    if (rc == ZXV_OK) {
        rmag_result_t r = rmag_release(&rmag->reg, budget, amt);
        rc = r == RMAG_RESULT_OK ? zxvi_ok()
                                 : zxvi_fail(ZXV_E_RMAG_INVALID, "rmag refused the release (%s)",
                                             rmag_result_name(r));
        if (rc == ZXV_OK) rmag->epoch[budget]++;
    }
    void *user = NULL;
    zxv_rmag_listener fn = claim(rmag, &user);
    pthread_mutex_unlock(&rmag->mu);
    deliver(rmag, budget, rc == ZXV_OK ? ZXV_RMAG_EV_REPLENISHED : ZXV_RMAG_EV_INVALID, fn, user);
    return rc;
}

int32_t zxv_rmag_set_limit(const zxv_cap *cap, zxv_rmag *rmag, uint32_t budget, int64_t num,
                           int64_t den)
{
    if (!rmag) return zxvi_fail(ZXV_E_ARG, "rmag is NULL");
    int32_t rc = zxvi_cap_gate_ctx(cap, ZXV_CAP_BUDGET, "rmag_set_limit", rmag->ctx);
    if (rc) return rc;
    rmag_rational_t lim;
    rat_t exact;
    if ((rc = to_rmag(num, den, true, &lim, &exact))) return rc;
    pthread_mutex_lock(&rmag->mu);
    if (budget >= rmag->reg.budget_count) {
        pthread_mutex_unlock(&rmag->mu);
        return zxvi_fail(ZXV_E_NOT_FOUND, "budget %u does not exist", budget);
    }
    rmag_budget_entry_t *b = &rmag->reg.budgets[budget];
    bool grew = rat_cmp(exact, from_rmag(b->limit)) > 0;
    b->limit = rmag_rational_reduce(lim);
    if (grew) rmag->epoch[budget]++;
    void *user = NULL;
    zxv_rmag_listener fn = grew ? claim(rmag, &user) : NULL;
    pthread_mutex_unlock(&rmag->mu);
    zxvi_ok();
    deliver(rmag, budget, ZXV_RMAG_EV_REPLENISHED, fn, user);
    return ZXV_OK;
}

int32_t zxv_rmag_status(zxv_rmag *rmag, uint32_t budget, int64_t *out_rem_num,
                        int64_t *out_rem_den, int64_t *out_used_num, int64_t *out_used_den,
                        uint64_t *out_epoch)
{
    if (!rmag) return zxvi_fail(ZXV_E_ARG, "rmag is NULL");
    int32_t rc = zxvi_ctx_check(rmag->ctx, "rmag_status");
    if (rc) return rc;
    pthread_mutex_lock(&rmag->mu);
    if (budget >= rmag->reg.budget_count) {
        pthread_mutex_unlock(&rmag->mu);
        return zxvi_fail(ZXV_E_NOT_FOUND, "budget %u does not exist", budget);
    }
    rmag_budget_entry_t *b = &rmag->reg.budgets[budget];
    rat_t used = from_rmag(b->consumed);
    rat_t lim = from_rmag(b->limit);
    rat_t rem = rat_sub(lim, used);
    if (rem.valid && rem.num < 0) rem = rat_zero();
    uint64_t ep = rmag->epoch[budget];
    pthread_mutex_unlock(&rmag->mu);
    if (!used.valid || !rem.valid) return zxvi_fail(ZXV_E_RANGE, "budget values exceed int64");
    if (out_rem_num) *out_rem_num = rem.num;
    if (out_rem_den) *out_rem_den = rem.den;
    if (out_used_num) *out_used_num = used.num;
    if (out_used_den) *out_used_den = used.den;
    if (out_epoch) *out_epoch = ep;
    return zxvi_ok();
}

int32_t zxv_rmag_listen(zxv_rmag *rmag, zxv_rmag_listener fn, void *user)
{
    if (!rmag) return zxvi_fail(ZXV_E_ARG, "rmag is NULL");
    int32_t rc = zxvi_ctx_check(rmag->ctx, "rmag_listen");
    if (rc) return rc;
    bool nested = in_callback_of(rmag);
    pthread_mutex_lock(&rmag->mu);
    rmag->listener = fn;
    rmag->user = fn ? user : NULL;
    /* Wait out every delivery already claimed (see "Listener delivery"). */
    while (!nested && rmag->inflight > 0) pthread_cond_wait(&rmag->idle, &rmag->mu);
    pthread_mutex_unlock(&rmag->mu);
    return zxvi_ok();
}

/* Used by the tensor adapter: charge `n` whole units, no cap re-check. */
int32_t zxvi_rmag_charge(zxv_rmag *rmag, uint32_t budget, uint64_t n);
int32_t zxvi_rmag_charge(zxv_rmag *rmag, uint32_t budget, uint64_t n)
{
    if (n == 0) return ZXV_OK;
    if (n > (uint64_t) INT64_MAX) return zxvi_fail(ZXV_E_RANGE, "charge too large");
    int32_t rc = zxvi_ctx_check(rmag->ctx, "rmag_charge");
    if (rc) return rc;
    pthread_mutex_lock(&rmag->mu);
    if (budget >= rmag->reg.budget_count) {
        rc = zxvi_fail(ZXV_E_NOT_FOUND, "bound budget %u does not exist", budget);
    } else {
        rat_t sum = rat_add(from_rmag(rmag->reg.budgets[budget].consumed), rat_from_int((int64_t) n));
        if (!sum.valid) {
            rc = zxvi_fail(ZXV_E_RMAG_INVALID, "consumed total would overflow");
        } else {
            rmag_result_t r = rmag_consume(&rmag->reg, budget, rmag_rational_from_uint(n));
            rc = r == RMAG_RESULT_OK       ? ZXV_OK
                 : r == RMAG_RESULT_DENIED ? zxvi_fail(ZXV_E_BUDGET, "token budget %u exhausted", budget)
                                           : zxvi_fail(ZXV_E_RMAG_INVALID, "%s", rmag_result_name(r));
        }
    }
    void *user = NULL;
    zxv_rmag_listener fn = rc == ZXV_E_BUDGET ? claim(rmag, &user) : NULL;
    pthread_mutex_unlock(&rmag->mu);
    deliver(rmag, budget, ZXV_RMAG_EV_DENIED, fn, user);
    return rc;
}

/* ===================================================================== */
/* Phase coordinator                                                     */
/* ===================================================================== */

typedef struct {
    uint64_t id, due;
    int32_t state;
} phase_ev;

struct zxv_phase {
    zxv_ctx *ctx;
    pthread_mutex_t mu;
    pc_registry_t reg;
    uint64_t now;
    phase_ev *ev;
    size_t n_ev, cap_ev;
    uint64_t next_ev;
};

int32_t zxv_phase_create(zxv_ctx *ctx, zxv_phase **out_phase)
{
    if (!ctx || !out_phase) return zxvi_fail(ZXV_E_ARG, "ctx or out_phase is NULL");
    int32_t rc = zxvi_ctx_check(ctx, "phase_create");
    if (rc) return rc;
    zxv_phase *p = calloc(1, sizeof *p);
    if (!p) return zxvi_fail(ZXV_E_NOMEM, "phase allocation failed");
    p->ctx = ctx;
    pthread_mutex_init(&p->mu, NULL);
    pc_registry_init(&p->reg);
    p->next_ev = 1;
    *out_phase = p;
    return zxvi_ok();
}

int32_t zxv_phase_create_cap(const zxv_cap *cap, zxv_phase **out_phase)
{
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_PHASE, "phase_create", &ctx);
    return rc ? rc : zxv_phase_create(ctx, out_phase);
}

void zxv_phase_destroy(zxv_phase *phase)
{
    if (!phase) return;
    free(phase->ev);
    pthread_mutex_destroy(&phase->mu);
    free(phase);
}

int32_t zxv_phase_now(zxv_phase *phase, uint64_t *out_tick)
{
    if (!phase || !out_tick) return zxvi_fail(ZXV_E_ARG, "phase or out_tick is NULL");
    int32_t rc = zxvi_ctx_check(phase->ctx, "phase_now");
    if (rc) return rc;
    pthread_mutex_lock(&phase->mu);
    *out_tick = phase->now;
    pthread_mutex_unlock(&phase->mu);
    return zxvi_ok();
}

int32_t zxv_phase_advance(const zxv_cap *cap, zxv_phase *phase, uint64_t delta, uint64_t *out_now,
                          uint32_t *out_cleared)
{
    if (!phase) return zxvi_fail(ZXV_E_ARG, "phase is NULL");
    int32_t rc = zxvi_cap_gate_ctx(cap, ZXV_CAP_PHASE, "phase_advance", phase->ctx);
    if (rc) return rc;
    pthread_mutex_lock(&phase->mu);
    if (phase->now > UINT64_MAX - delta) {
        pthread_mutex_unlock(&phase->mu);
        return zxvi_fail(ZXV_E_RANGE, "tick counter would overflow");
    }
    phase->now += delta;
    pc_advance_time(&phase->reg, delta);
    uint32_t cleared = 0;
    for (size_t i = 0; i < phase->n_ev; i++)
        if (phase->ev[i].state == ZXV_EV_PENDING && phase->ev[i].due <= phase->now) {
            phase->ev[i].state = ZXV_EV_CLEARED;
            cleared++;
        }
    if (out_now) *out_now = phase->now;
    pthread_mutex_unlock(&phase->mu);
    if (out_cleared) *out_cleared = cleared;
    return zxvi_ok();
}

int32_t zxv_phase_admit(const zxv_cap *cap, zxv_phase *phase, uint32_t phase_id, uint32_t step_id,
                        uint32_t flags, int32_t *out_decision, uint64_t *out_token)
{
    if (!phase || !out_decision) return zxvi_fail(ZXV_E_ARG, "phase or out_decision is NULL");
    int32_t rc = zxvi_cap_gate_ctx(cap, ZXV_CAP_PHASE, "phase_admit", phase->ctx);
    if (rc) return rc;
    if (phase_id < 1 || phase_id > PC_MAX_PHASES)
        return zxvi_fail(ZXV_E_ARG, "phase_id %u is not 1..%u", phase_id, PC_MAX_PHASES);
    if (flags & ~(ZXV_STEP_REQUIRES_HEALTH | ZXV_STEP_HARDWARE | ZXV_STEP_S0_PENDING))
        return zxvi_fail(ZXV_E_ARG, "unknown step flags 0x%x", flags);
    pc_step_request_t req;
    memset(&req, 0, sizeof req);
    req.phase_id = phase_id;
    req.step_id = step_id;
    req.requires_health = (flags & ZXV_STEP_REQUIRES_HEALTH) != 0;
    req.is_hardware_action = (flags & ZXV_STEP_HARDWARE) != 0;
    req.is_s0_pending = (flags & ZXV_STEP_S0_PENDING) != 0;
    pthread_mutex_lock(&phase->mu);
    /* The kernel table holds PC_MAX_TOKENS; keep the newest half. */
    if (phase->reg.token_count >= PC_MAX_TOKENS) {
        uint32_t keep = PC_MAX_TOKENS / 2;
        memmove(phase->reg.tokens, phase->reg.tokens + (PC_MAX_TOKENS - keep),
                keep * sizeof phase->reg.tokens[0]);
        phase->reg.token_count = keep;
    }
    pc_token_t *t = pc_admit(&phase->reg, &req);
    int32_t decision = t ? (int32_t) t->decision : -1;
    uint64_t token = t && t->decision == PC_DECISION_ADMIT ? t->token_id : 0;
    pthread_mutex_unlock(&phase->mu);
    if (!t) return zxvi_fail(ZXV_E_INTERNAL, "phase coordinator refused the request");
    *out_decision = decision;
    if (out_token) *out_token = token;
    return zxvi_ok();
}

int32_t zxv_phase_set_health(const zxv_cap *cap, zxv_phase *phase, uint32_t phase_id,
                             int32_t health)
{
    if (!phase) return zxvi_fail(ZXV_E_ARG, "phase is NULL");
    int32_t rc = zxvi_cap_gate_ctx(cap, ZXV_CAP_PHASE, "phase_set_health", phase->ctx);
    if (rc) return rc;
    if (phase_id < 1 || phase_id > PC_MAX_PHASES || health < 0 || health > 3)
        return zxvi_fail(ZXV_E_ARG, "bad phase or health value");
    pthread_mutex_lock(&phase->mu);
    pc_set_phase_health(&phase->reg, (pc_phase_id_t) phase_id, (pc_health_t) health);
    pthread_mutex_unlock(&phase->mu);
    return zxvi_ok();
}

int32_t zxv_phase_defer(const zxv_cap *cap, zxv_phase *phase, uint64_t due_tick,
                        uint64_t *out_event)
{
    if (!phase || !out_event) return zxvi_fail(ZXV_E_ARG, "phase or out_event is NULL");
    int32_t rc = zxvi_cap_gate_ctx(cap, ZXV_CAP_PHASE, "phase_defer", phase->ctx);
    if (rc) return rc;
    pthread_mutex_lock(&phase->mu);
    if (due_tick <= phase->now) {
        uint64_t now = phase->now;
        pthread_mutex_unlock(&phase->mu);
        return zxvi_fail(ZXV_E_ARG, "due tick %llu is not after now (%llu)",
                         (unsigned long long) due_tick, (unsigned long long) now);
    }
    /* Event ids are a 64-bit counter that only grows: an id is never issued
     * twice, so a stale id can never name (or cancel) a newer event. Only
     * table SLOTS are recycled, once an event is finished and 4096 newer
     * ids exist; a recycled event's id then reports ZXV_E_NOT_FOUND. */
    if (phase->next_ev == UINT64_MAX) {
        pthread_mutex_unlock(&phase->mu);
        return zxvi_fail(ZXV_E_CAPACITY, "event ids exhausted");
    }
    size_t slot = phase->n_ev;
    for (size_t i = 0; i < phase->n_ev; i++)
        if (phase->ev[i].state != ZXV_EV_PENDING && phase->ev[i].id + 4096 < phase->next_ev) {
            slot = i;
            break;
        }
    if (slot == phase->n_ev) {
        if (phase->n_ev == phase->cap_ev) {
            size_t nc = phase->cap_ev ? phase->cap_ev * 2 : 64;
            phase_ev *ne = realloc(phase->ev, nc * sizeof *ne);
            if (!ne) {
                pthread_mutex_unlock(&phase->mu);
                return zxvi_fail(ZXV_E_NOMEM, "event table allocation failed");
            }
            phase->ev = ne;
            phase->cap_ev = nc;
        }
        phase->n_ev++;
    }
    phase->ev[slot] = (phase_ev){phase->next_ev++, due_tick, ZXV_EV_PENDING};
    *out_event = phase->ev[slot].id;
    pthread_mutex_unlock(&phase->mu);
    return zxvi_ok();
}

static phase_ev *ev_find(zxv_phase *p, uint64_t id)
{
    for (size_t i = 0; i < p->n_ev; i++)
        if (p->ev[i].id == id) return &p->ev[i];
    return NULL;
}

int32_t zxv_phase_event(zxv_phase *phase, uint64_t event, int32_t *out_state,
                        uint64_t *out_due_tick)
{
    if (!phase) return zxvi_fail(ZXV_E_ARG, "phase is NULL");
    int32_t rc = zxvi_ctx_check(phase->ctx, "phase_event");
    if (rc) return rc;
    pthread_mutex_lock(&phase->mu);
    phase_ev *e = ev_find(phase, event);
    if (e) {
        if (out_state) *out_state = e->state;
        if (out_due_tick) *out_due_tick = e->due;
    }
    pthread_mutex_unlock(&phase->mu);
    return e ? zxvi_ok() : zxvi_fail(ZXV_E_NOT_FOUND, "event %llu is unknown or recycled",
                                      (unsigned long long) event);
}

int32_t zxv_phase_cancel(const zxv_cap *cap, zxv_phase *phase, uint64_t event)
{
    if (!phase) return zxvi_fail(ZXV_E_ARG, "phase is NULL");
    int32_t rc = zxvi_cap_gate_ctx(cap, ZXV_CAP_PHASE, "phase_cancel", phase->ctx);
    if (rc) return rc;
    pthread_mutex_lock(&phase->mu);
    phase_ev *e = ev_find(phase, event);
    int32_t st = e ? e->state : -1;
    if (e && e->state == ZXV_EV_PENDING) e->state = ZXV_EV_CANCELLED;
    pthread_mutex_unlock(&phase->mu);
    if (!e) return zxvi_fail(ZXV_E_NOT_FOUND, "event %llu is unknown", (unsigned long long) event);
    if (st != ZXV_EV_PENDING) return zxvi_fail(ZXV_E_STATE, "event is no longer pending");
    return zxvi_ok();
}

int32_t zxv_phase_next_due(zxv_phase *phase, uint64_t *out_tick)
{
    if (!phase || !out_tick) return zxvi_fail(ZXV_E_ARG, "phase or out_tick is NULL");
    int32_t rc = zxvi_ctx_check(phase->ctx, "phase_next_due");
    if (rc) return rc;
    pthread_mutex_lock(&phase->mu);
    bool any = false;
    uint64_t best = 0;
    for (size_t i = 0; i < phase->n_ev; i++)
        if (phase->ev[i].state == ZXV_EV_PENDING && (!any || phase->ev[i].due < best)) {
            best = phase->ev[i].due;
            any = true;
        }
    pthread_mutex_unlock(&phase->mu);
    if (!any) return zxvi_fail(ZXV_E_NOT_FOUND, "no pending event");
    *out_tick = best;
    return zxvi_ok();
}

/* ===================================================================== */
/* Swarm token economy                                                   */
/* ===================================================================== */

struct zxv_swarm {
    zxv_ctx *ctx;
    pthread_mutex_t mu;
    swarm_budget_t b;
    swarm_emotion_state_t e;
};

static int32_t swarm_status(swarm_status_t s, const char *what)
{
    switch (s) {
    case SWARM_OK: return zxvi_ok();
    case SWARM_ERR_FULL: return zxvi_fail(ZXV_E_CAPACITY, "%s: level is at its Fibonacci capacity", what);
    case SWARM_ERR_NO_MODEL: return zxvi_fail(ZXV_E_NOT_FOUND, "%s: model is not registered", what);
    case SWARM_ERR_DUPLICATE: return zxvi_fail(ZXV_E_DUPLICATE, "%s: model already registered", what);
    case SWARM_ERR_NO_CYCLE: return zxvi_fail(ZXV_E_STATE, "%s: no cycle is open", what);
    default: return zxvi_fail(ZXV_E_ARG, "%s: argument out of range", what);
    }
}

int32_t zxv_swarm_create(zxv_ctx *ctx, uint32_t num_levels, uint64_t tokens_per_cycle,
                         zxv_swarm **out_swarm)
{
    if (!ctx || !out_swarm) return zxvi_fail(ZXV_E_ARG, "ctx or out_swarm is NULL");
    int32_t rc = zxvi_ctx_check(ctx, "swarm_create");
    if (rc) return rc;
    zxv_swarm *s = calloc(1, sizeof *s);
    if (!s) return zxvi_fail(ZXV_E_NOMEM, "swarm allocation failed");
    rc = swarm_status(swarm_budget_init(&s->b, num_levels, tokens_per_cycle), "swarm_create");
    if (rc) {
        free(s);
        return rc;
    }
    swarm_emotion_init(&s->e);
    s->ctx = ctx;
    pthread_mutex_init(&s->mu, NULL);
    *out_swarm = s;
    return zxvi_ok();
}

int32_t zxv_swarm_create_cap(const zxv_cap *cap, uint32_t num_levels, uint64_t tokens_per_cycle,
                             zxv_swarm **out_swarm)
{
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_SWARM, "swarm_create", &ctx);
    return rc ? rc : zxv_swarm_create(ctx, num_levels, tokens_per_cycle, out_swarm);
}

void zxv_swarm_destroy(zxv_swarm *swarm)
{
    if (!swarm) return;
    pthread_mutex_destroy(&swarm->mu);
    free(swarm);
}

#define SWARM_ENTER(name)                                                    \
    if (!swarm) return zxvi_fail(ZXV_E_ARG, "%s: swarm is NULL", name);       \
    int32_t rc = zxvi_cap_gate_ctx(cap, ZXV_CAP_SWARM, name, swarm->ctx);     \
    if (rc) return rc;                                                        \
    pthread_mutex_lock(&swarm->mu)

int32_t zxv_swarm_register(const zxv_cap *cap, zxv_swarm *swarm, uint32_t model_id, uint32_t level)
{
    SWARM_ENTER("swarm_register");
    rc = swarm_status(swarm_budget_register(&swarm->b, model_id, level), "swarm_register");
    pthread_mutex_unlock(&swarm->mu);
    return rc;
}

int32_t zxv_swarm_set_active(const zxv_cap *cap, zxv_swarm *swarm, uint32_t model_id,
                             int32_t active)
{
    SWARM_ENTER("swarm_set_active");
    rc = swarm_status(swarm_budget_set_active(&swarm->b, model_id, active != 0), "swarm_set_active");
    pthread_mutex_unlock(&swarm->mu);
    return rc;
}

int32_t zxv_swarm_set_rate(const zxv_cap *cap, zxv_swarm *swarm, uint64_t tokens_per_cycle)
{
    SWARM_ENTER("swarm_set_rate");
    rc = swarm_status(swarm_budget_set_rate(&swarm->b, tokens_per_cycle), "swarm_set_rate");
    pthread_mutex_unlock(&swarm->mu);
    return rc;
}

static int32_t feeling(uint32_t codepoint, uint32_t intensity, swarm_feeling_t *f)
{
    swarm_emotion_t e;
    if (swarm_emotion_from_codepoint(codepoint, &e) != SWARM_OK)
        return zxvi_fail(ZXV_E_NOT_FOUND, "U+%04X is not in the emotion palette", codepoint);
    if (intensity > SWARM_EMO_MAX_INTENSITY)
        return zxvi_fail(ZXV_E_ARG, "intensity %u is not 0..%u", intensity, SWARM_EMO_MAX_INTENSITY);
    f->emotion = e;
    f->intensity = (uint8_t) intensity;
    return ZXV_OK;
}

int32_t zxv_swarm_set_mood(const zxv_cap *cap, zxv_swarm *swarm, uint32_t codepoint,
                           uint32_t intensity)
{
    swarm_feeling_t f;
    int32_t frc = feeling(codepoint, intensity, &f);
    if (frc) return frc;
    SWARM_ENTER("swarm_set_mood");
    rc = swarm_status(swarm_emotion_set_mood(&swarm->e, f), "swarm_set_mood");
    pthread_mutex_unlock(&swarm->mu);
    return rc;
}

int32_t zxv_swarm_set_feeling(const zxv_cap *cap, zxv_swarm *swarm, uint32_t model_id,
                              uint32_t codepoint, uint32_t intensity)
{
    swarm_feeling_t f;
    int32_t frc = feeling(codepoint, intensity, &f);
    if (frc) return frc;
    SWARM_ENTER("swarm_set_feeling");
    rc = swarm_status(swarm_emotion_set_feeling(&swarm->e, model_id, f), "swarm_set_feeling");
    pthread_mutex_unlock(&swarm->mu);
    return rc;
}

int32_t zxv_swarm_begin_cycle(const zxv_cap *cap, zxv_swarm *swarm, uint64_t *out_cycle)
{
    SWARM_ENTER("swarm_begin_cycle");
    rc = swarm_status(swarm_emotion_begin_cycle(&swarm->b, &swarm->e), "swarm_begin_cycle");
    if (rc == ZXV_OK && out_cycle) *out_cycle = swarm->b.cycle;
    pthread_mutex_unlock(&swarm->mu);
    return rc;
}

int32_t zxv_swarm_consume(const zxv_cap *cap, zxv_swarm *swarm, uint32_t model_id,
                          uint64_t requested, uint64_t *out_granted)
{
    if (!out_granted) return zxvi_fail(ZXV_E_ARG, "out_granted is NULL");
    SWARM_ENTER("swarm_consume");
    uint64_t granted = 0;
    rc = swarm_status(swarm_budget_consume(&swarm->b, model_id, requested, &granted),
                      "swarm_consume");
    pthread_mutex_unlock(&swarm->mu);
    if (rc) return rc;
    *out_granted = granted;
    if (granted < requested)
        return zxvi_fail(ZXV_E_BUDGET, "model %u: granted %llu of %llu tokens", model_id,
                         (unsigned long long) granted, (unsigned long long) requested);
    return zxvi_ok();
}

int32_t zxv_swarm_allotment(zxv_swarm *swarm, uint32_t model_id, uint64_t *out_allotted,
                            uint64_t *out_imaginary, uint64_t *out_used)
{
    if (!swarm) return zxvi_fail(ZXV_E_ARG, "swarm is NULL");
    int32_t rc = zxvi_ctx_check(swarm->ctx, "swarm_allotment");
    if (rc) return rc;
    pthread_mutex_lock(&swarm->mu);
    const swarm_slot_t *s = NULL;
    for (uint32_t i = 0; i < swarm->b.num_slots; i++)
        if (swarm->b.slots[i].model_id == model_id) s = &swarm->b.slots[i];
    if (s) {
        if (out_allotted) *out_allotted = s->allotted;
        if (out_imaginary) *out_imaginary = s->allotted_im;
        if (out_used) *out_used = s->used;
    }
    pthread_mutex_unlock(&swarm->mu);
    return s ? zxvi_ok() : zxvi_fail(ZXV_E_NOT_FOUND, "model %u is not registered", model_id);
}

int32_t zxv_swarm_end_cycle(const zxv_cap *cap, zxv_swarm *swarm, uint64_t *out_expired)
{
    SWARM_ENTER("swarm_end_cycle");
    rc = swarm_status(swarm_budget_end_cycle(&swarm->b), "swarm_end_cycle");
    if (rc == ZXV_OK && out_expired) *out_expired = swarm->b.last_unused;
    pthread_mutex_unlock(&swarm->mu);
    return rc;
}

int32_t zxv_swarm_level_capacity(uint32_t level, uint32_t *out_capacity)
{
    if (!out_capacity || level >= SWARM_MAX_LEVELS)
        return zxvi_fail(ZXV_E_ARG, "level must be 0..%u", SWARM_MAX_LEVELS - 1);
    *out_capacity = swarm_level_capacity(level);
    return zxvi_ok();
}

int32_t zxv_swarm_emotion_profile(uint32_t codepoint, int32_t *out_valence,
                                  uint32_t *out_temperature_milli, uint32_t *out_explore_permille,
                                  uint32_t *out_verify_passes, uint32_t *out_priority)
{
    swarm_emotion_t e;
    if (swarm_emotion_from_codepoint(codepoint, &e) != SWARM_OK)
        return zxvi_fail(ZXV_E_NOT_FOUND, "U+%04X is not in the emotion palette", codepoint);
    const swarm_emotion_profile_t *p = swarm_emotion_profile(e);
    if (out_valence) *out_valence = p->valence;
    if (out_temperature_milli) *out_temperature_milli = p->temperature_milli;
    if (out_explore_permille) *out_explore_permille = p->explore_permille;
    if (out_verify_passes) *out_verify_passes = p->verify_passes;
    if (out_priority) *out_priority = p->priority;
    return zxvi_ok();
}
