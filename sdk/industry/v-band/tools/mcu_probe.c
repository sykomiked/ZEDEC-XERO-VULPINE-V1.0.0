/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mcu_probe.c — what a sensor node holds in RAM when it uses v-band: a
 * 64-reading log and one uplink buffer at the largest EU868 payload. Only
 * compiled (make size-arm), never linked: the board's ADC, radio and MAC
 * are not part of this kit. The two hooks below stand in for them. */
#include "zvband.h"

#define LOG_CAP 64u

static zvb_record g_storage[LOG_CAP];
static zvb_log g_log;
static uint8_t g_uplink[222];

/* Provided by the board support package. */
extern int32_t board_read_pair(int32_t *a, int32_t *b);
extern int32_t board_uplink(const uint8_t *buf, uint32_t len, uint8_t dr, uint32_t *next_expected);

int32_t probe_init(uint32_t device);
int32_t probe_tick(uint32_t now_s, uint8_t dr);

int32_t probe_init(uint32_t device) { return zvb_log_init(&g_log, g_storage, LOG_CAP, device); }

int32_t probe_tick(uint32_t now_s, uint8_t dr) {
    int32_t a = 0, b = 0, st;
    uint32_t max = 0, count = 0, next = 0;
    size_t len = 0;
    if (board_read_pair(&a, &b) == 0) st = zvb_log_append_pair(&g_log, 1, now_s, a, b, 2);
    else st = zvb_log_append(&g_log, 1, now_s, ZXV_LP_FALSE, 1);
    if (st != ZXV_OK && st != ZVB_E_FULL) return st;
    st = zvb_payload_max(ZVB_REGION_EU868, dr, &max);
    if (st != ZXV_OK) return st;
    st = zvb_batch_make(&g_log, max, g_uplink, sizeof g_uplink, &len, &count);
    if (st == ZVB_E_EMPTY) return ZXV_OK;
    if (st != ZXV_OK) return st;
    if (board_uplink(g_uplink, (uint32_t) len, dr, &next) != 0) return zvb_log_ack(&g_log, g_log.acked + 1);
    return zvb_log_ack(&g_log, next);
}
