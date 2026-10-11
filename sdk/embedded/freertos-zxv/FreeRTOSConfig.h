/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* FreeRTOSConfig.h — for the FreeRTOS POSIX simulator build of the
 * freertos-zxv sample (host tests and CI). On a board, start from your
 * vendor's config and keep the ZXV lines marked below. */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configTICK_RATE_HZ                      ((TickType_t) 1000)
#define configMAX_PRIORITIES                    5
#define configMINIMAL_STACK_SIZE                ((unsigned short) 4096)
#define configTOTAL_HEAP_SIZE                   ((size_t) (512 * 1024))
#define configMAX_TASK_NAME_LEN                 16
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TIMERS                        0
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TRACE_FACILITY                0
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configQUEUE_REGISTRY_SIZE               0

/* ---- needed by the ZXV port (port/src/zxp_freertos.c) ---- */
#define configUSE_MUTEXES                       1
#define configSUPPORT_STATIC_ALLOCATION         1 /* mutexes live inside libzxv objects */
#define configSUPPORT_DYNAMIC_ALLOCATION        1 /* pvPortMalloc for libzxv's heap use */
#define configKERNEL_PROVIDED_STATIC_MEMORY     1 /* idle task memory (V11+) */
#define INCLUDE_xTaskGetCurrentTaskHandle       1 /* single-owner check */
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskSuspend                    1 /* portMAX_DELAY waits forever */
#define INCLUDE_vTaskDelete                     1

#define configASSERT(x)                                 \
    do {                                                \
        if (!(x)) vAssertCalled(__FILE__, __LINE__);    \
    } while (0)
void vAssertCalled(const char *file, int line);

#endif /* FREERTOS_CONFIG_H */
