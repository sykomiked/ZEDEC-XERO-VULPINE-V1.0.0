/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_log.h — zxvd's recent events, for the Wake console (log.tail) and
 * the Sounding network monitor (the RPC traffic counts).
 *
 *   L1 A RING. The last ZXVD_LOG_RING events stay in memory; older ones
 *      are dropped, never written to disk here. Each has a sequence number
 *      that only grows, so a reader asks for what came after the last one
 *      it saw and learns how many it missed.
 *   L2 NO SECRETS. Events say what happened (a method, an outcome, how long
 *      it took), never a request's parameters or a reply's content.
 *   L3 ONE THREAD. Like the dispatcher, the ring is used from zxvd's one
 *      event loop. */
#ifndef ZXVD_LOG_H
#define ZXVD_LOG_H

#include <stdbool.h>
#include <stdint.h>

#define ZXVD_LOG_RING 512u
#define ZXVD_LOG_TEXT 160u

typedef enum { ZXVD_LOG_INFO, ZXVD_LOG_WARN, ZXVD_LOG_ERROR } zxvd_log_level_t;

typedef struct {
    uint64_t seq;     /* 1, 2, 3, ... */
    uint64_t unix_ms; /* when */
    uint8_t level;
    char source[16]; /* "rpc", "zxvd", "net", ... */
    char text[ZXVD_LOG_TEXT + 1];
} zxvd_log_entry_t;

/* Record an event (printf-style; cut to ZXVD_LOG_TEXT bytes on a UTF-8
 * boundary, control characters become spaces). */
void zxvd_log(zxvd_log_level_t level, const char *source, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;
/* Up to max events after `since`, oldest first, at least `min_level`.
 * *dropped = how many after `since` the ring no longer holds. Returns the
 * count written. */
uint32_t zxvd_log_tail(uint64_t since, uint32_t max, zxvd_log_level_t min_level,
                       zxvd_log_entry_t *out, uint64_t *dropped);
uint64_t zxvd_log_last(void);                        /* the newest sequence number, 0 = none yet */
const char *zxvd_log_level_name(zxvd_log_level_t l); /* "info", "warn", "error" */

/* RPC traffic since zxvd started. */
typedef struct {
    uint64_t requests, errors, bytes_in, bytes_out;
    uint64_t busy_us; /* time spent answering */
} zxvd_traffic_t;
void zxvd_traffic_add(uint64_t bytes_in, uint64_t bytes_out, bool error, uint64_t us);
void zxvd_traffic_get(zxvd_traffic_t *t);

uint64_t zxvd_now_ms(void);  /* wall clock, ms since 1970 */
uint64_t zxvd_mono_us(void); /* a clock that only goes forward */

void zxvd_log_reset(void); /* tests */

#endif
