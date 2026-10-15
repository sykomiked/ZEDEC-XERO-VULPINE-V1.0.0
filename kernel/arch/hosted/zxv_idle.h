/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_idle.h — the swarm's own work while no one is asking (R24, hosted app).
 *
 *   I1 QUEUE. A bounded ring of ZXV_IDLE_CAP tasks. A full ring refuses the
 *      task (backpressure); nothing is allocated.
 *   I2 SLICE. zxv_idle_slice does one slice of the oldest task and returns.
 *      A pin check hashes at most ZXV_IDLE_CHUNK bytes per slice; a
 *      self-test is one slice. The task's state is kept between slices, so
 *      the host can serve a person between any two slices (I3).
 *   I3 YIELD. The host calls zxv_idle_slice only when no request is waiting;
 *      a request is served at once, and the idle task resumes after it.
 *   I4 BUDGET. zxv_idle_cost says what the next slice costs (one token per
 *      started chunk, one per self-test); the host charges that to a
 *      stand-in agent's allotment before the slice runs, and runs nothing
 *      when the allotment cannot pay.
 *   I5 RESULT. A finished task records PASS or FAIL with a reason; a pin
 *      check fails when the file's SHA-256 differs from the expected one or
 *      the file cannot be read. Results depend only on the inputs (files,
 *      code), never on time, so a run can be replayed.
 *   I6 INWARD. Idle work reads files and runs checks; it never writes a
 *      file, sends a message, spends money or acts on a device.
 */
#ifndef ZXV_IDLE_H
#define ZXV_IDLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "sha256.h"

#define ZXV_IDLE_CAP   16u
#define ZXV_IDLE_CHUNK (1u << 20)
#define ZXV_IDLE_PATH  1024u

typedef enum { ZXV_IDLE_PIN = 1, ZXV_IDLE_SELFTEST = 2 } zxv_idle_kind_t;
typedef enum { ZXV_IDLE_PENDING = 0, ZXV_IDLE_PASS = 1, ZXV_IDLE_FAIL = 2 } zxv_idle_result_t;

typedef struct {
    zxv_idle_kind_t kind;
    char path[ZXV_IDLE_PATH]; /* PIN: the file */
    uint8_t want[32];         /* PIN: its expected SHA-256 */
    uint32_t tag;             /* the caller's label (e.g. the slot number) */
    /* progress */
    FILE *f;
    uint64_t done; /* bytes hashed */
    sha256_ctx_t h;
} zxv_idle_task_t;

typedef struct {
    zxv_idle_kind_t kind;
    uint32_t tag;
    zxv_idle_result_t result;
    char why[160];
} zxv_idle_outcome_t;

typedef struct {
    zxv_idle_task_t t[ZXV_IDLE_CAP];
    uint32_t head, tail; /* head - tail = tasks queued */
    uint64_t slices, finished, failed;
    zxv_idle_outcome_t last; /* the latest finished task */
} zxv_idle_t;

void zxv_idle_init(zxv_idle_t *q);
/* Close any open file; the queue is empty afterwards. */
void zxv_idle_clear(zxv_idle_t *q);
uint32_t zxv_idle_queued(const zxv_idle_t *q);

/* I1. 0, or -1 when the ring is full or an argument is bad. want_hex is 64
 * hex digits. */
int zxv_idle_add_pin(zxv_idle_t *q, const char *path, const char *want_hex, uint32_t tag);
int zxv_idle_add_selftest(zxv_idle_t *q, uint32_t tag);

/* I4. Tokens the next slice costs; 0 when the queue is empty. */
uint64_t zxv_idle_cost(const zxv_idle_t *q);

/* I2. Run one slice of the oldest task. Returns 1 when that slice finished
 * the task (its outcome is in *out when out is not NULL, and in q->last),
 * 0 when the task goes on, -1 when the queue is empty. */
int zxv_idle_slice(zxv_idle_t *q, zxv_idle_outcome_t *out);

/* A short description of the oldest task's progress, for a receipt
 * ("pin 0 at 3145728 bytes", "selftest 1", "none"). */
void zxv_idle_describe(const zxv_idle_t *q, char *out, size_t cap);

/* The self-test itself (one slice): the Tiamat knowledge join's laws over all
 * four states and the reckoner on fixed sums. 0 when all hold, else -1 with
 * the first failure in why. */
int zxv_idle_selftest(char *why, size_t cap);

#endif
