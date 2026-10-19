/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sounding_model.h — Sounding, the network monitor, as a model (zxm.h):
 * the machine's interfaces with their addresses and live rates, and zxvd's
 * own request traffic, from sys.net read on a timer. Read only. No
 * drawing, no allocation, no floats. */
#ifndef SOUNDING_MODEL_H
#define SOUNDING_MODEL_H

#include "zxm.h"

#define SOUNDING_IFS     16u
#define SOUNDING_ADDRS   8u
#define SOUNDING_SAMPLES 120u

typedef struct {
    char name[40];
    bool up, loopback, counters;
    char addr[SOUNDING_ADDRS][48];
    uint32_t naddr;
    uint64_t rx_bytes, tx_bytes;
    int64_t rx_rate, tx_rate; /* bytes a second since the last read; -1 = not yet */
} sounding_if_t;

typedef struct {
    uint64_t time_ms;
    int64_t rx_rate, tx_rate; /* all interfaces but loopback; -1 = not yet */
} sounding_sample_t;

typedef struct {
    zxm_link_t *link;
    bool supported; /* false: this platform's interfaces are not listed yet */
    sounding_if_t ifs[SOUNDING_IFS];
    uint32_t n;
    uint64_t time_ms;
    uint64_t rpc_requests, rpc_errors, rpc_bytes_in, rpc_bytes_out;
    int64_t rpc_rate; /* requests a second since the last read; -1 = not yet */
    sounding_sample_t hist[SOUNDING_SAMPLES]; /* a ring */
    uint32_t first, nhist;
} sounding_t;

void sounding_init(sounding_t *s, zxm_link_t *link);
int sounding_refresh(sounding_t *s);
const sounding_if_t *sounding_find(const sounding_t *s, const char *name);
uint32_t sounding_history_len(const sounding_t *s);
const sounding_sample_t *sounding_history(const sounding_t *s, uint32_t i); /* 0 = oldest */

#endif
