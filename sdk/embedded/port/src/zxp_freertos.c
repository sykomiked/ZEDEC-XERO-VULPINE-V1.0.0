/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxp_freertos.c — the port on FreeRTOS (V10.4+ / V11.x kernel API).
 *
 * FreeRTOSConfig.h needs:
 *   INCLUDE_xTaskGetCurrentTaskHandle 1   (owner checks)
 *   INCLUDE_vTaskDelay 1, INCLUDE_vTaskSuspend 1 (portMAX_DELAY waits)
 *   configUSE_MUTEXES 1
 *   configSUPPORT_STATIC_ALLOCATION 1 (mutexes live inside libzxv objects;
 *   with only dynamic allocation each mutex takes a heap block instead)
 *
 * Thread-local storage: the default FreeRTOS + newlib setup has none, so
 * build libzxv with -DZXP_NO_TLS and make every ZXV call from one owner
 * task (zxp_set_owner_current; other tasks send it requests on a queue).
 * Allocation: pvPortMalloc / vPortFree (any heap_N with vPortFree).
 * Entropy: none by default. Install the board TRNG with zxp_set_entropy. */
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "zxp.h"
#include "zxp_os.h"

#if configSUPPORT_STATIC_ALLOCATION
_Static_assert(sizeof(StaticSemaphore_t) <= sizeof(((zxp_mutex_t *) 0)->os),
               "raise ZXP_MUTEX_WORDS for this StaticSemaphore_t");
#endif

static SemaphoreHandle_t handle(zxp_mutex_t *m)
{
#if configSUPPORT_STATIC_ALLOCATION
    return (SemaphoreHandle_t) (void *) &m->os;
#else
    return (SemaphoreHandle_t) m->os.ptr_;
#endif
}

int zxp_os_mutex_create(zxp_mutex_t *m)
{
#if configSUPPORT_STATIC_ALLOCATION
    return xSemaphoreCreateMutexStatic((StaticSemaphore_t *) (void *) &m->os) ? 0 : -1;
#else
    m->os.ptr_ = xSemaphoreCreateMutex();
    return m->os.ptr_ ? 0 : -1;
#endif
}

void zxp_os_mutex_delete(zxp_mutex_t *m)
{
    vSemaphoreDelete(handle(m));
}

int zxp_os_mutex_lock(zxp_mutex_t *m)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) return 0; /* single thread yet */
    while (xSemaphoreTake(handle(m), portMAX_DELAY) != pdTRUE) {
    }
    return 0;
}

int zxp_os_mutex_unlock(zxp_mutex_t *m)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) return 0;
    return xSemaphoreGive(handle(m)) == pdTRUE ? 0 : -1;
}

void zxp_os_crit_enter(void)
{
    taskENTER_CRITICAL();
}

void zxp_os_crit_exit(void)
{
    taskEXIT_CRITICAL();
}

void zxp_os_sleep_tick(void)
{
    vTaskDelay(1);
}

/* TickType_t may be 16 or 32 bits; extend it to 64. Call at least once per
 * tick-counter wrap (49.7 days at 1 kHz with 32-bit ticks) for an exact
 * count: zxe_clock_pump in a periodic task does. */
uint64_t zxp_os_monotonic_us(void)
{
    static TickType_t last;
    static uint64_t high;
    uint64_t ticks;
    taskENTER_CRITICAL();
    TickType_t now = xTaskGetTickCount();
    if (now < last) high += (uint64_t) ((TickType_t) ~(TickType_t) 0) + 1u;
    last = now;
    ticks = high + now;
    taskEXIT_CRITICAL();
    /* configTICK_RATE_HZ usually carries a cast, so no #if: constant-folded. */
    const uint64_t hz = (uint64_t) configTICK_RATE_HZ;
    if (1000000u % hz == 0) return ticks * (1000000u / hz);
    return ticks / hz * 1000000u + ticks % hz * 1000000u / hz;
}

void *zxp_os_task_self(void)
{
    return (void *) xTaskGetCurrentTaskHandle();
}

void *zxp_os_alloc(void *user, size_t n)
{
    (void) user;
    return pvPortMalloc(n);
}

void zxp_os_free(void *user, void *p)
{
    (void) user;
    vPortFree(p);
}

void zxp_os_halt(const char *why)
{
    (void) why;
    configASSERT(0);
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}
