/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
#include "quest_pay.h"
#include "pay_util.h"

qst_status_t qst_payout_plan(const qst_world_t *w, uint32_t goal,
                             const uint32_t member_acct[QST_GOAL_MEMBERS], pay_rat_t cap,
                             qst_payout_plan_t *out)
{
    if (!w || !member_acct || !out || cap.den == 0) return QST_ERR_ARG;
    qst__zero(out, (uint32_t) sizeof *out);
    const qst_goal_t *g = qst_goal_c(w, goal);
    if (!g) return QST_ERR_NOT_FOUND;
    if (g->state != QST_GOAL_COMPLETE && g->state != QST_GOAL_SETTLED) return QST_ERR_STATE;
    if (g->n_members == 0 || g->n_members > PAY_COMMONS_MAX_RECIPIENTS) return QST_ERR_STATE;

    uint64_t gross[QST_GOAL_MEMBERS];
    out->pool = g->pool_amount;
    out->unallocated = pay_commons_split(g->pool_amount, g->share_units, g->n_members, cap, gross);
    out->n = g->n_members;
    for (uint32_t i = 0; i < g->n_members; i++) {
        qst_payout_line_t *l = &out->line[i];
        const qst_profile_t *p = qst_profile_c(w, g->member[i]);
        l->member = g->member[i];
        l->gross = gross[i];
        l->tithe = pay_tithe_phi(gross[i]);
        l->net = gross[i] - l->tithe;
        l->to_acct = member_acct[i];
        if (p && !p->policy.allow_money) l->to_acct = p->policy.custodian_acct; /* P4 */
        if (l->to_acct == 0 && l->gross > 0) {
            l->held = true;
            out->held_total += l->gross;
        } else {
            out->tithe_total += l->tithe;
        }
    }
    return QST_OK;
}

static void idem_key(uint32_t goal, uint32_t member, uint32_t pool, uint8_t out[32])
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "ZXV-QUEST-PAYOUT-v1");
    pay_hbuf_u64(&h, goal);
    pay_hbuf_u64(&h, member);
    pay_hbuf_u64(&h, pool);
    pay_hbuf_final(&h, out);
}

static uint32_t put_dec(char *o, uint32_t at, uint32_t v)
{
    char t[10];
    uint32_t n = 0;
    do {
        t[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n) o[at++] = t[--n];
    o[at] = 0;
    return at;
}

static bool vfv_acct(const pay_ledger_t *L, uint32_t acct)
{
    const pay_account_t *a = pay_ledger_account(L, acct);
    return a && a->active && a->asset == L->vfv_asset;
}

qst_status_t qst_goal_settle(qst_world_t *w, uint32_t goal, pay_ledger_t *L,
                             const uint32_t member_acct[QST_GOAL_MEMBERS], uint32_t commons_acct,
                             pay_rat_t cap, uint64_t tick, uint32_t initiator,
                             qst_payout_plan_t *plan)
{
    qst_payout_plan_t local;
    if (!plan) plan = &local;
    if (!w || !L) return QST_ERR_ARG;
    qst_goal_t *g = qst_goal(w, goal);
    if (!g) return QST_ERR_NOT_FOUND;
    if (g->state != QST_GOAL_COMPLETE) return QST_ERR_STATE;
    if (g->pool_amount == 0) { /* standing-only goal: nothing to pay */
        qst__zero(plan, (uint32_t) sizeof *plan);
        g->state = QST_GOAL_SETTLED;
        return QST_OK;
    }
    /* P6 */
    const pay_account_t *ca = pay_ledger_account(L, commons_acct);
    if (!vfv_acct(L, g->pool_acct) || !vfv_acct(L, commons_acct) || !(ca->flags & PAY_ACCT_COMMONS))
        return QST_ERR_PAY;

    qst_status_t st = qst_payout_plan(w, goal, member_acct, cap, plan);
    if (st != QST_OK) return st;
    if (!g->settle_started) {
        g->settle_started = true;
        g->settle_tick = tick;
        g->settle_initiator = initiator;
    }
    tick = g->settle_tick;
    initiator = g->settle_initiator;
    for (uint32_t i = 0; i < plan->n; i++) {
        const qst_payout_line_t *l = &plan->line[i];
        if (l->held || l->gross == 0) continue;
        if (!vfv_acct(L, l->to_acct)) return QST_ERR_PAY;
        if (l->gross > (uint64_t) INT64_MAX) return QST_ERR_PAY;

        pay_posting_req_t req;
        pay_memset(&req, 0, sizeof req);
        idem_key(g->id, l->member, g->pool_acct, req.idem_key);
        uint8_t u[32];
        pay_sha3_256(req.idem_key, 32, u);
        pay_uetr_from_random(u, req.uetr);
        uint32_t n = 0;
        const char *pre = "QST-G";
        while (pre[n]) {
            req.e2e[n] = pre[n];
            n++;
        }
        n = put_dec(req.e2e, n, g->id);
        req.e2e[n++] = '-';
        req.e2e[n++] = 'M';
        put_dec(req.e2e, n, l->member);
        pay_strlcpy(req.memo, "quest co-op share", sizeof req.memo);
        req.initiator = initiator;
        req.tick = tick;
        req.attestor = initiator;
        req.kind = PAY_KIND_TRANSFER;
        req.lines[0].account = g->pool_acct;
        req.lines[0].d_debit = -(int64_t) l->gross;
        req.lines[1].account = l->to_acct;
        req.lines[1].d_debit = (int64_t) l->net;
        req.n_lines = 2;
        if (l->tithe) {
            req.lines[2].account = commons_acct;
            req.lines[2].d_debit = (int64_t) l->tithe;
            req.n_lines = 3;
        }
        pay_receipt_t rc;
        pay_status_t ps = pay_ledger_post(L, &req, &rc);
        if (ps != PAY_OK && ps != PAY_DUPLICATE) return QST_ERR_PAY;
    }
    g->state = QST_GOAL_SETTLED;
    return QST_OK;
}
