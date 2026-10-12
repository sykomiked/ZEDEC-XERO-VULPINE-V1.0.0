/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tidewatch_model.h — Tidewatch, the activity monitor, as a model (zxm.h):
 * samples sys.info over time and keeps the history a view graphs: zxvd's
 * CPU share and memory, the machine's free memory and load. Read only.
 * No drawing, no allocation, no floats (shares are x100: 1234 = 12.34%). */
#ifndef TIDEWATCH_MODEL_H
#define TIDEWATCH_MODEL_H

#include "zxm.h"

#define TIDEWATCH_SAMPLES 120u /* two minutes at one a second */

typedef struct {
    uint64_t time_ms;
    int32_t zxvd_cpu_x100; /* of one CPU since the previous sample; -1 = first */
    uint64_t zxvd_rss;
    int32_t mem_used_x100; /* the machine's, -1 = unknown */
    int32_t load1_x100;    /* -1 = unknown */
} tidewatch_sample_t;

typedef struct {
    zxm_link_t *link;
    tidewatch_sample_t s[TIDEWATCH_SAMPLES]; /* a ring */
    uint32_t first, n;
    uint64_t last_cpu_us, last_ms; /* the previous raw reading */
    bool have_last;
    uint32_t cpus;
    uint64_t mem_total;
} tidewatch_t;

void tidewatch_init(tidewatch_t *t, zxm_link_t *link);
/* Take one sample (call on a timer, e.g. once a second). */
int tidewatch_sample(tidewatch_t *t);
uint32_t tidewatch_count(const tidewatch_t *t);
const tidewatch_sample_t *tidewatch_at(const tidewatch_t *t, uint32_t i); /* 0 = oldest */
const tidewatch_sample_t *tidewatch_latest(const tidewatch_t *t);
/* The highest zxvd CPU share in the history (for a graph's scale), 0 if none. */
int32_t tidewatch_peak_cpu_x100(const tidewatch_t *t);

#endif
