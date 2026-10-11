/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_task.c — the ZXV owner task (see zxv_task.h). */
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "zxe.h"
#include "zxp.h"
#include "zxv_task.h"

#define N_ACCOUNTS 8u
#define N_REFS     256u /* power of two: 128 postings per reference period */

static zxe_account s_accounts[N_ACCOUNTS];
static zxe_refkey s_refs[N_REFS];

static QueueHandle_t s_q;
static zxv_task_config s_cfg;
static volatile int32_t s_init_status = ZXV_E_STATE;

typedef struct {
    zxv_ctx *ctx;
    zxv_cap *root;
    zxv_cap *poster; /* LEDGER_POST | LEDGER_READ | BUDGET only */
    zxv_rmag *rmag;
    uint32_t budget;
    zxv_phase *phase;
    zxe_clock clock;
    zxe_ledger ledger;
} zxv_state;

static int32_t open_all(zxv_state *s)
{
    int32_t rc;
    uint32_t id;
    if ((rc = zxv_ctx_create(&s->ctx))) return rc; /* ZXV_E_INTERNAL if zxp_init never ran */
    if ((rc = zxv_cap_create(s->ctx, 1, NULL, ZXV_CAP_ALL, &s->root))) return rc;
    if ((rc = zxv_cap_attenuate(s->root, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ | ZXV_CAP_BUDGET,
                                &s->poster)))
        return rc;
    if ((rc = zxv_rmag_create_cap(s->root, &s->rmag))) return rc;
    if ((rc = zxv_rmag_budget(s->root, s->rmag, "postings", ZXV_RES_COMPUTE_CYCLES,
                              s_cfg.postings_per_period, 1, 1, &s->budget)))
        return rc;
    if ((rc = zxv_phase_create_cap(s->root, &s->phase))) return rc;
    if ((rc = zxe_clock_init(&s->clock, s_cfg.phase_ticks_per_second))) return rc;
    if ((rc = zxe_ledger_init(&s->ledger, s->ctx, s_accounts, N_ACCOUNTS, s_refs, N_REFS))) return rc;
    if ((rc = zxe_ledger_ccy_enable(&s->ledger, s->root, s_cfg.currency, s_cfg.minor_units))) return rc;
    if ((rc = zxe_ledger_open_account(&s->ledger, s->root, "issuer", s_cfg.currency, ZXV_ACCT_ISSUER,
                                      &id)))
        return rc;
    if ((rc = zxe_ledger_open_account(&s->ledger, s->root, "grid", s_cfg.currency, ZXV_ACCT_HOLDER,
                                      &id)))
        return rc;
    if ((rc = zxe_ledger_open_account(&s->ledger, s->root, "meter", s_cfg.currency, ZXV_ACCT_HOLDER,
                                      &id)))
        return rc;
    return zxe_clock_pump(&s->clock, s->root, s->phase, NULL, NULL, NULL);
}

/* The budget tick: refill what the last period used, advance the clock. */
static void budget_tick(zxv_state *s)
{
    int64_t un = 0, ud = 1;
    if (zxv_rmag_status(s->rmag, s->budget, NULL, NULL, &un, &ud, NULL) == ZXV_OK && un > 0)
        (void) zxv_rmag_release(s->root, s->rmag, s->budget, un, ud);
    (void) zxe_clock_pump(&s->clock, s->root, s->phase, NULL, NULL, NULL);
}

static void handle(zxv_state *s, zxv_req *r)
{
    switch (r->kind) {
    case ZXV_REQ_POST:
        /* one posting = one unit of the period's budget; refused, not queued */
        r->status = zxv_rmag_consume(s->poster, s->rmag, s->budget, 1, 1);
        if (r->status == ZXV_OK) {
            r->status = zxe_ledger_post(&s->ledger, s->poster, r->from, r->to, r->amount_minor,
                                        s_cfg.currency, r->reference, &r->entry_id);
            if (r->status != ZXV_OK) /* give the unit back: nothing was posted */
                (void) zxv_rmag_release(s->poster, s->rmag, s->budget, 1, 1);
        }
        break;
    case ZXV_REQ_BALANCE:
        r->status = zxe_ledger_balance(&s->ledger, s->poster, r->account, NULL, NULL, &r->equity);
        break;
    case ZXV_REQ_STATS: {
        zxp_heap_stats_t hs;
        r->status = zxv_rmag_status(s->rmag, s->budget, &r->budget_rem_num, &r->budget_rem_den, NULL,
                                    NULL, NULL);
        if (r->status == ZXV_OK) r->status = zxv_phase_now(s->phase, &r->phase_tick);
        zxp_heap_stats(&hs);
        r->heap_live = hs.live_bytes;
        r->heap_peak = hs.peak_bytes;
        break;
    }
    default:
        r->status = ZXV_E_ARG;
    }
}

static void zxv_owner_task(void *arg)
{
    (void) arg;
    static zxv_state s;
    zxp_set_owner_current(); /* from now on any other task calling libzxv is fatal */
    int32_t rc = open_all(&s);
    s_init_status = rc;
    TickType_t period = pdMS_TO_TICKS(s_cfg.tick_period_ms);
    if (period == 0) period = 1;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        zxv_req *r = NULL;
        TickType_t elapsed = xTaskGetTickCount() - last; /* unsigned: wrap-safe */
        if (elapsed >= period) {
            if (rc == ZXV_OK) budget_tick(&s);
            last += period * (elapsed / period);
            elapsed %= period;
        }
        if (xQueueReceive(s_q, &r, period - elapsed) == pdTRUE && r) {
            if (rc != ZXV_OK) r->status = ZXV_E_INTERNAL; /* closed: no entropy, no ZXV */
            else handle(&s, r);
            xTaskNotifyGive(r->reply_to);
        }
    }
}

BaseType_t zxv_task_start(const zxv_task_config *cfg, UBaseType_t priority, uint32_t stack_words)
{
    if (!cfg || !cfg->currency || cfg->tick_period_ms == 0 || cfg->postings_per_period <= 0)
        return pdFALSE;
    s_cfg = *cfg;
    s_q = xQueueCreate(8, sizeof(zxv_req *));
    if (!s_q) return pdFALSE;
    return xTaskCreate(zxv_owner_task, "zxv", stack_words, NULL, priority, NULL);
}

BaseType_t zxv_task_call(zxv_req *req, TickType_t timeout)
{
    if (!req || !s_q) return pdFALSE;
    req->reply_to = xTaskGetCurrentTaskHandle();
    req->status = ZXV_E_STATE;
    (void) ulTaskNotifyTake(pdTRUE, 0); /* clear a stale notification */
    if (xQueueSend(s_q, &req, timeout) != pdTRUE) return pdFALSE;
    return ulTaskNotifyTake(pdTRUE, timeout) ? pdTRUE : pdFALSE;
}

int32_t zxv_task_init_status(void)
{
    return s_init_status;
}
