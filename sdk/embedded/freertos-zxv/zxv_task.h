/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_task.h — one FreeRTOS task that owns every ZXV object.
 *
 * FreeRTOS builds have no thread-local storage, so libzxv is built with
 * ZXP_NO_TLS and only one task may call it (zxp.h, "single-owner mode").
 * This module is that task: other tasks send it requests on a queue and
 * block on a task notification for the answer. It also runs the periodic
 * "budget tick": every period it refills the posting budget (an exact
 * rational rmag budget) and advances the phase clock from the RTOS tick.
 *
 * The ledger is zxe_ledger in static memory (libzxv's own ledger needs a
 * ~24 MB kernel book; see ../README.md). ADVISORY: nothing here actuates. */
#ifndef ZXV_TASK_H
#define ZXV_TASK_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "zxe.h"

typedef enum {
    ZXV_REQ_POST = 1,    /* from -> to, amount, ref; replies status, entry_id */
    ZXV_REQ_BALANCE = 2, /* account; replies status, equity */
    ZXV_REQ_STATS = 3    /* replies budget remaining (num/den), phase tick, heap */
} zxv_req_kind;

typedef struct {
    zxv_req_kind kind;
    uint32_t from, to, account;
    int64_t amount_minor;
    char reference[ZXE_LEDGER_REF_MAX + 1];
    /* answers */
    int32_t status;
    uint64_t entry_id;
    int64_t equity;
    int64_t budget_rem_num, budget_rem_den;
    uint64_t phase_tick;
    size_t heap_live, heap_peak;
    TaskHandle_t reply_to; /* filled by zxv_task_call */
} zxv_req;

typedef struct {
    uint32_t tick_period_ms;       /* budget tick period */
    int64_t postings_per_period;   /* the budget limit, whole postings */
    uint32_t phase_ticks_per_second;
    const char *currency;          /* the ledger's one currency, e.g. "EUR" */
    uint8_t minor_units;
} zxv_task_config;

/* Accounts the task opens at start: 0 = issuer, 1 = grid, 2 = meter. */
#define ZXV_ACCT_ISSUER_ID 0u
#define ZXV_ACCT_GRID_ID   1u
#define ZXV_ACCT_METER_ID  2u

/* Create the owner task and its queue. zxp_init() must have run first (with
 * the board's entropy source); if it did not, the task still starts but
 * answers every request with ZXV_E_INTERNAL: fail closed. */
BaseType_t zxv_task_start(const zxv_task_config *cfg, UBaseType_t priority, uint32_t stack_words);

/* Send a request and wait up to `timeout` for the answer (req->status).
 * Returns pdFALSE on a queue or answer timeout. Must not be called from
 * the owner task itself, nor from an interrupt. */
BaseType_t zxv_task_call(zxv_req *req, TickType_t timeout);

/* The status of the task's start-up (ZXV_OK, or why ZXV stayed closed). */
int32_t zxv_task_init_status(void);

#endif /* ZXV_TASK_H */
