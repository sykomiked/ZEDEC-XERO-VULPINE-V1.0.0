/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mfg_budget.c — F.10 hard nature budgets and the minimal F.6 queue (see
 * mfg_budget.h).
 *
 * WHAT IT DOES
 *   Admits a job only if every resource it needs has a trusted meter
 *   reading and stays within the site's hard limit; orders admission by
 *   bid with a no-monopoly share cap. Bids never enter the budget check.
 *
 * HONEST LIMITS
 *   Trusts the caller's meter readings and limits; no payment happens here.
 */
#include "mfg_budget.h"

static int mfg_b_ok(int64_t v)
{
    return v >= 0 && v <= MFG_BUDGET_MAX;
}

mfg_status_t mfg_site_init(mfg_site_t *s, uint64_t epoch, const int64_t limits[MFG_RES_N])
{
    int r;
    if (!s || !limits) return MFG_ERR_NULL;
    for (r = 0; r < MFG_RES_N; r++)
        if (!mfg_b_ok(limits[r])) return MFG_ERR_RANGE;
    s->epoch = epoch;
    for (r = 0; r < MFG_RES_N; r++) {
        s->limit[r] = limits[r];
        s->used[r] = 0;
        s->reserved[r] = 0;
        s->meter[r] = MFG_NEITHER;
    }
    s->init = 1;
    return MFG_OK;
}

mfg_status_t mfg_site_new_epoch(mfg_site_t *s, uint64_t epoch)
{
    int r;
    if (!s) return MFG_ERR_NULL;
    if (!s->init) return MFG_ERR_STATE;
    if (epoch <= s->epoch) return MFG_ERR_RANGE;
    s->epoch = epoch;
    for (r = 0; r < MFG_RES_N; r++) {
        s->used[r] = 0;
        s->reserved[r] = 0;
        s->meter[r] = MFG_NEITHER;
    }
    return MFG_OK;
}

mfg_status_t mfg_site_meter(mfg_site_t *s, uint32_t r, int64_t reading)
{
    if (!s) return MFG_ERR_NULL;
    if (!s->init) return MFG_ERR_STATE;
    if (r >= MFG_RES_N || !mfg_b_ok(reading)) return MFG_ERR_RANGE;
    if (s->meter[r] == MFG_BOTH) return MFG_OK; /* stays contradictory until cleared */
    if (s->meter[r] == MFG_TRUE && reading < s->used[r]) {
        s->meter[r] = MFG_BOTH;
        return MFG_OK;
    }
    s->used[r] = reading;
    s->meter[r] = MFG_TRUE;
    return MFG_OK;
}

mfg_status_t mfg_site_meter_clear(mfg_site_t *s, uint32_t r)
{
    if (!s) return MFG_ERR_NULL;
    if (!s->init) return MFG_ERR_STATE;
    if (r >= MFG_RES_N) return MFG_ERR_RANGE;
    s->meter[r] = MFG_NEITHER;
    s->used[r] = 0;
    return MFG_OK;
}

mfg_status_t mfg_site_check(const mfg_site_t *s, const int64_t need[MFG_RES_N],
                            mfg_tv_t per_res[MFG_RES_N], mfg_admit_t *decision)
{
    int r, wait = 0;
    if (!s || !need || !decision) return MFG_ERR_NULL;
    if (!s->init) return MFG_ERR_STATE;
    for (r = 0; r < MFG_RES_N; r++)
        if (!mfg_b_ok(need[r])) return MFG_ERR_RANGE;
    for (r = 0; r < MFG_RES_N; r++) {
        mfg_tv_t t;
        if (need[r] == 0) {
            t = MFG_TRUE;
        } else if (s->meter[r] != MFG_TRUE) {
            t = (mfg_tv_t) s->meter[r]; /* NEITHER or BOTH */
        } else {
            /* each term <= 1e15: the sum cannot overflow int64 */
            int64_t total = s->used[r] + s->reserved[r] + need[r];
            t = total <= s->limit[r] ? MFG_TRUE : MFG_FALSE;
        }
        if (t != MFG_TRUE) wait = 1;
        if (per_res) per_res[r] = t;
    }
    *decision = wait ? MFG_WAIT : MFG_ADMIT;
    return MFG_OK;
}

mfg_status_t mfg_site_reserve(mfg_site_t *s, const int64_t need[MFG_RES_N],
                              mfg_tv_t per_res[MFG_RES_N], mfg_admit_t *decision)
{
    int r;
    mfg_status_t st = mfg_site_check(s, need, per_res, decision);
    if (st != MFG_OK) return st;
    if (*decision != MFG_ADMIT) return MFG_OK;
    for (r = 0; r < MFG_RES_N; r++) s->reserved[r] += need[r]; /* <= limit, fits */
    return MFG_OK;
}

mfg_status_t mfg_site_release(mfg_site_t *s, const int64_t need[MFG_RES_N])
{
    int r;
    if (!s || !need) return MFG_ERR_NULL;
    if (!s->init) return MFG_ERR_STATE;
    for (r = 0; r < MFG_RES_N; r++)
        if (!mfg_b_ok(need[r]) || need[r] > s->reserved[r]) return MFG_ERR_RANGE;
    for (r = 0; r < MFG_RES_N; r++) s->reserved[r] -= need[r];
    return MFG_OK;
}

/* ---- F.6 minimal queue ------------------------------------------------ */

mfg_status_t mfg_queue_init(mfg_queue_t *q)
{
    if (!q) return MFG_ERR_NULL;
    q->njob = 0;
    q->next_seq = 0;
    return MFG_OK;
}

static int mfg_q_find(const mfg_queue_t *q, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < q->njob; i++)
        if (q->job[i].id == id) return (int) i;
    return -1;
}

mfg_status_t mfg_queue_add(mfg_queue_t *q, uint32_t id, uint32_t owner, int64_t bid,
                           int64_t minutes, const int64_t need[MFG_RES_N])
{
    mfg_job_t *j;
    int r;
    if (!q || !need) return MFG_ERR_NULL;
    if (bid < 0 || bid > MFG_Q_BID_MAX || minutes < 1 || minutes > MFG_Q_TIME_MAX)
        return MFG_ERR_RANGE;
    for (r = 0; r < MFG_RES_N; r++)
        if (!mfg_b_ok(need[r])) return MFG_ERR_RANGE;
    if (q->njob > MFG_Q_MAX_JOBS) return MFG_ERR_STATE;
    if (mfg_q_find(q, id) >= 0) return MFG_ERR_STATE;
    if (q->njob == MFG_Q_MAX_JOBS) return MFG_ERR_CAPACITY;
    j = &q->job[q->njob++];
    j->id = id;
    j->owner = owner;
    j->bid = bid;
    j->minutes = minutes;
    for (r = 0; r < MFG_RES_N; r++) {
        j->need[r] = need[r];
        j->res_state[r] = MFG_NEITHER;
    }
    j->seq = q->next_seq++;
    j->state = MFG_JOB_PENDING;
    j->wait = MFG_WAIT_NONE;
    return MFG_OK;
}

static void mfg_job_copy(mfg_job_t *d, const mfg_job_t *s)
{
    int r;
    d->id = s->id;
    d->owner = s->owner;
    d->bid = s->bid;
    d->minutes = s->minutes;
    for (r = 0; r < MFG_RES_N; r++) {
        d->need[r] = s->need[r];
        d->res_state[r] = s->res_state[r];
    }
    d->seq = s->seq;
    d->state = s->state;
    d->wait = s->wait;
}

mfg_status_t mfg_queue_remove(mfg_queue_t *q, uint32_t id)
{
    int k;
    uint32_t i;
    if (!q) return MFG_ERR_NULL;
    if (q->njob > MFG_Q_MAX_JOBS) return MFG_ERR_STATE;
    k = mfg_q_find(q, id);
    if (k < 0) return MFG_ERR_NOT_FOUND;
    for (i = (uint32_t) k; i + 1 < q->njob; i++) mfg_job_copy(&q->job[i], &q->job[i + 1]);
    q->njob--;
    return MFG_OK;
}

/* j ranks before k: higher bid, then earlier submission */
static int mfg_q_before(const mfg_job_t *j, const mfg_job_t *k)
{
    if (j->bid != k->bid) return j->bid > k->bid;
    return j->seq < k->seq;
}

mfg_status_t mfg_queue_run(mfg_queue_t *q, mfg_site_t *s, int64_t capacity_min, int64_t share_num,
                           int64_t share_den, uint32_t *order, uint32_t max_order,
                           uint32_t *nadmitted)
{
    uint8_t now[MFG_Q_MAX_JOBS];
    int64_t used = 0, cap_lim;
    uint32_t i, n = 0;
    if (!q || !s || !nadmitted || (!order && max_order != 0)) return MFG_ERR_NULL;
    if (!s->init || q->njob > MFG_Q_MAX_JOBS) return MFG_ERR_STATE;
    if (capacity_min < 0 || capacity_min > MFG_Q_TIME_MAX) return MFG_ERR_RANGE;
    if (share_den < 1 || share_den > MFG_Q_SHARE_MAX || share_num < 1 || share_num > share_den)
        return MFG_ERR_RANGE;
    cap_lim = capacity_min * share_num; /* <= 1e9 * 1e6: fits */
    for (i = 0; i < q->njob; i++) now[i] = 0;
    *nadmitted = 0;

    for (;;) {
        int best_free = -1, best_any = -1;
        for (i = 0; i < q->njob; i++) {
            mfg_job_t *j = &q->job[i];
            mfg_admit_t d;
            mfg_tv_t rs[MFG_RES_N];
            int64_t own = 0;
            uint32_t k;
            int r;
            if (j->state != MFG_JOB_PENDING) continue;
            if (mfg_site_check(s, j->need, rs, &d) != MFG_OK) return MFG_ERR_STATE;
            for (r = 0; r < MFG_RES_N; r++) j->res_state[r] = (uint8_t) rs[r];
            if (d != MFG_ADMIT) {
                j->wait = MFG_WAIT_BUDGET;
                continue;
            }
            if (j->minutes > capacity_min - used) {
                j->wait = MFG_WAIT_CAPACITY;
                continue;
            }
            j->wait = MFG_WAIT_NONE;
            if (best_any < 0 || mfg_q_before(j, &q->job[best_any])) best_any = (int) i;
            for (k = 0; k < q->njob; k++)
                if (now[k] && q->job[k].owner == j->owner) own += q->job[k].minutes;
            /* (own + minutes) * den <= capacity * num; own + minutes <= capacity */
            if ((own + j->minutes) * share_den <= cap_lim &&
                (best_free < 0 || mfg_q_before(j, &q->job[best_free])))
                best_free = (int) i;
        }
        {
            int pick = best_free >= 0 ? best_free : best_any;
            mfg_job_t *j;
            mfg_admit_t d;
            if (pick < 0) break;
            j = &q->job[pick];
            if (mfg_site_reserve(s, j->need, 0, &d) != MFG_OK || d != MFG_ADMIT)
                return MFG_ERR_STATE;
            j->state = MFG_JOB_ADMITTED;
            j->wait = MFG_WAIT_NONE;
            now[pick] = 1;
            used += j->minutes;
            if (n < max_order) order[n] = j->id;
            n++;
        }
    }
    *nadmitted = n;
    return MFG_OK;
}
