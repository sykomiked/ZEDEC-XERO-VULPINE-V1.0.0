/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* main_posix.c — the freertos-zxv sample on the FreeRTOS POSIX simulator.
 *
 * A metering gateway in miniature: the ZXV owner task keeps an exact ledger
 * (issuer, grid, meter) and a posting budget refilled every period; a
 * client task posts through it. It is also the integration test:
 *
 *   zxv_freertos_sim               the scenario; exit 0 if every check holds
 *   zxv_freertos_sim --no-entropy  no entropy source: ZXV must stay closed
 *   zxv_freertos_sim --violate     a second task calls libzxv directly:
 *                                  the single-owner check must stop it
 *
 * On a board, replace host_entropy with the TRNG driver and keep the rest.
 * ADVISORY: a real meter or grid controller acts only through its own
 * certified protection and with human approval; this posts numbers. */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

#include "FreeRTOS.h"
#include "task.h"

#include "zxe.h"
#include "zxp.h"
#include "zxv_task.h"

#define PERIOD_MS 1000u
#define BUDGET    5

static int g_checks, g_fail;
#define CHECK(c, ...)                                         \
    do {                                                      \
        g_checks++;                                           \
        if (!(c)) {                                           \
            g_fail++;                                         \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);       \
            printf(__VA_ARGS__);                              \
            printf("\n");                                     \
        }                                                     \
    } while (0)

/* The host stands in for the board TRNG. */
static int host_entropy(void *user, uint8_t *buf, size_t len)
{
    (void) user;
    size_t got = 0;
    while (got < len) {
        ssize_t r = getrandom(buf + got, len - got, 0);
        if (r <= 0) return -1;
        got += (size_t) r;
    }
    return 0;
}

void vAssertCalled(const char *file, int line)
{
    printf("configASSERT failed at %s:%d\n", file, line);
    fflush(stdout);
    exit(70);
}

static void on_fatal(const char *why)
{
    printf("zxp fatal: %s\n", why);
    fflush(stdout);
    exit(70);
}

static int32_t post(uint32_t from, uint32_t to, int64_t amount, const char *ref, uint64_t *id)
{
    zxv_req r;
    memset(&r, 0, sizeof r);
    r.kind = ZXV_REQ_POST;
    r.from = from;
    r.to = to;
    r.amount_minor = amount;
    snprintf(r.reference, sizeof r.reference, "%s", ref);
    if (zxv_task_call(&r, pdMS_TO_TICKS(2000)) != pdTRUE) return ZXV_E_STATE;
    if (id) *id = r.entry_id;
    return r.status;
}

static int64_t equity(uint32_t account)
{
    zxv_req r;
    memset(&r, 0, sizeof r);
    r.kind = ZXV_REQ_BALANCE;
    r.account = account;
    if (zxv_task_call(&r, pdMS_TO_TICKS(2000)) != pdTRUE || r.status != ZXV_OK) return INT64_MIN;
    return r.equity;
}

static void client_task(void *arg)
{
    (void) arg;
    uint64_t id = 0;
    vTaskDelay(pdMS_TO_TICKS(50)); /* let the owner open its objects */
    CHECK(zxv_task_init_status() == ZXV_OK, "owner start-up: %s", zxv_status_name(zxv_task_init_status()));

    CHECK(post(ZXV_ACCT_ISSUER_ID, ZXV_ACCT_METER_ID, 100000, "mint-1", &id) == ZXV_OK, "mint");
    CHECK(id == 1, "entry 1");
    CHECK(post(ZXV_ACCT_METER_ID, ZXV_ACCT_GRID_ID, 1234, "kwh-0001", &id) == ZXV_OK, "settle");
    CHECK(post(ZXV_ACCT_METER_ID, ZXV_ACCT_GRID_ID, 1234, "kwh-0001", &id) == ZXV_E_DUPLICATE,
          "a replayed reference is refused");
    CHECK(post(ZXV_ACCT_GRID_ID, ZXV_ACCT_METER_ID, 5000, "kwh-0002", &id) == ZXV_E_FUNDS,
          "a holder cannot go below zero");
    CHECK(equity(ZXV_ACCT_METER_ID) == 98766, "meter equity %lld", (long long) equity(ZXV_ACCT_METER_ID));
    CHECK(equity(ZXV_ACCT_GRID_ID) == 1234, "grid equity");
    CHECK(equity(ZXV_ACCT_ISSUER_ID) == -100000, "issuer carries the issuance");

    /* the posting budget: BUDGET per period, exact, refilled by the tick */
    vTaskDelay(pdMS_TO_TICKS(PERIOD_MS + 100));
    int ok = 0, denied = 0;
    for (int i = 0; i < BUDGET + 3; i++) {
        char ref[24];
        snprintf(ref, sizeof ref, "burst-%d", i);
        int32_t rc = post(ZXV_ACCT_METER_ID, ZXV_ACCT_GRID_ID, 1, ref, NULL);
        ok += rc == ZXV_OK;
        denied += rc == ZXV_E_BUDGET;
    }
    printf("burst of %d postings: %d accepted, %d refused by the budget\n", BUDGET + 3, ok, denied);
    CHECK(ok >= BUDGET && ok + denied == BUDGET + 3 && denied >= 1, "rate limited by the budget");

    zxv_req st;
    memset(&st, 0, sizeof st);
    st.kind = ZXV_REQ_STATS;
    CHECK(zxv_task_call(&st, pdMS_TO_TICKS(2000)) == pdTRUE && st.status == ZXV_OK, "stats");
    vTaskDelay(pdMS_TO_TICKS(PERIOD_MS + 100));
    CHECK(post(ZXV_ACCT_METER_ID, ZXV_ACCT_GRID_ID, 1, "after-tick", NULL) == ZXV_OK,
          "the budget tick refilled the budget");
    zxv_req st2;
    memset(&st2, 0, sizeof st2);
    st2.kind = ZXV_REQ_STATS;
    CHECK(zxv_task_call(&st2, pdMS_TO_TICKS(2000)) == pdTRUE && st2.status == ZXV_OK, "stats");
    CHECK(st2.phase_tick > st.phase_tick, "the phase clock advanced (%llu -> %llu)",
          (unsigned long long) st.phase_tick, (unsigned long long) st2.phase_tick);
    printf("phase tick %llu, budget remaining %lld/%lld, ZXV heap live %zu bytes (peak %zu)\n",
           (unsigned long long) st2.phase_tick, (long long) st2.budget_rem_num,
           (long long) st2.budget_rem_den, st2.heap_live, st2.heap_peak);
    printf("freertos-zxv: %d checks, %d failures\n", g_checks, g_fail);
    fflush(stdout);
    exit(g_fail ? 1 : 0);
}

static void closed_task(void *arg)
{
    (void) arg;
    vTaskDelay(pdMS_TO_TICKS(50));
    CHECK(zxv_task_init_status() == ZXV_E_INTERNAL, "start-up refused: %s",
          zxv_status_name(zxv_task_init_status()));
    CHECK(post(ZXV_ACCT_ISSUER_ID, ZXV_ACCT_METER_ID, 1, "x", NULL) == ZXV_E_INTERNAL,
          "every request refused");
    printf("freertos-zxv --no-entropy: %d checks, %d failures (ZXV stayed closed)\n", g_checks, g_fail);
    fflush(stdout);
    exit(g_fail ? 1 : 0);
}

static void rogue_task(void *arg)
{
    (void) arg;
    vTaskDelay(pdMS_TO_TICKS(50));
    zxv_ctx *ctx = NULL;
    printf("rogue task calls libzxv directly...\n");
    fflush(stdout);
    (void) zxv_ctx_create(&ctx); /* takes a lock from a non-owner task: fatal */
    printf("NOT STOPPED\n");
    exit(1);
}

int main(int argc, char **argv)
{
    int no_entropy = argc > 1 && strcmp(argv[1], "--no-entropy") == 0;
    int violate = argc > 1 && strcmp(argv[1], "--violate") == 0;
    zxp_set_fatal_handler(on_fatal);
    if (!no_entropy) {
        zxp_set_entropy(host_entropy, NULL); /* board: the TRNG driver */
        int rc = zxp_init();
        if (rc != ZXP_OK) {
            printf("zxp_init: %s\n", zxp_status_name(rc));
            return 1;
        }
    }
    zxv_task_config cfg = {PERIOD_MS, BUDGET, ZXV_PHASE_CYCLE, "EUR", 2};
    if (zxv_task_start(&cfg, tskIDLE_PRIORITY + 2, configMINIMAL_STACK_SIZE * 2) != pdPASS) return 1;
    TaskFunction_t client = no_entropy ? closed_task : violate ? rogue_task : client_task;
    xTaskCreate(client, "client", configMINIMAL_STACK_SIZE * 2, NULL, tskIDLE_PRIORITY + 1, NULL);
    vTaskStartScheduler();
    return 1;
}
