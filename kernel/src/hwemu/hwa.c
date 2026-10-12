/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwa.c — the hardware audit log. See hwa.h. */
#include "hwa.h"
#include "../robin_debanks/sha256.h"

#define HWA_DOMAIN     "zxv/hw-audit/v1"
#define HWA_DOMAIN_LEN 15u

static void link_hash(const hwa_rec_t *r, uint8_t out[HWA_HASH])
{
    uint8_t m[HWA_DOMAIN_LEN + 8 + 3 + 4 + 4 + 32 + HWA_HASH];
    uint32_t k = 0;
    for (uint32_t i = 0; i < HWA_DOMAIN_LEN; i++) m[k++] = (uint8_t) HWA_DOMAIN[i];
    for (int i = 0; i < 8; i++) m[k++] = (uint8_t) (r->seq >> (8 * i));
    m[k++] = r->tag;
    m[k++] = r->event;
    m[k++] = r->reason;
    for (int i = 0; i < 4; i++) m[k++] = (uint8_t) (r->region >> (8 * i));
    for (int i = 0; i < 4; i++) m[k++] = (uint8_t) (r->version >> (8 * i));
    for (int i = 0; i < 32; i++) m[k++] = r->subject[i];
    for (uint32_t i = 0; i < HWA_HASH; i++) m[k++] = r->prev[i];
    sha256(m, k, out);
}

void hwa_init(hwa_log_t *log, hwa_rec_t *storage, uint32_t cap)
{
    if (!log) return;
    log->rec = storage;
    log->cap = storage ? cap : 0;
    log->n = 0;
    for (uint32_t i = 0; i < HWA_HASH; i++) log->head[i] = 0;
}

bool hwa_append(hwa_log_t *log, lpx_t tag, uint8_t event, uint8_t reason, uint32_t region,
                uint32_t version, const uint8_t subject[32])
{
    if (!log || !log->rec || log->n >= log->cap || !subject) return false;
    hwa_rec_t *r = &log->rec[log->n];
    r->seq = log->n;
    r->tag = tag;
    r->event = event;
    r->reason = reason;
    r->region = region;
    r->version = version;
    for (int i = 0; i < 32; i++) r->subject[i] = subject[i];
    for (uint32_t i = 0; i < HWA_HASH; i++) r->prev[i] = log->head[i];
    link_hash(r, r->hash);
    for (uint32_t i = 0; i < HWA_HASH; i++) log->head[i] = r->hash[i];
    log->n++;
    return true;
}

bool hwa_verify(const hwa_log_t *log)
{
    if (!log) return false;
    uint8_t prev[HWA_HASH] = {0}, h[HWA_HASH];
    for (uint32_t n = 0; n < log->n; n++) {
        const hwa_rec_t *r = &log->rec[n];
        if (r->seq != n) return false;
        uint8_t d = 0;
        for (uint32_t i = 0; i < HWA_HASH; i++) d |= (uint8_t) (r->prev[i] ^ prev[i]);
        link_hash(r, h);
        for (uint32_t i = 0; i < HWA_HASH; i++) d |= (uint8_t) (r->hash[i] ^ h[i]);
        if (d) return false;
        for (uint32_t i = 0; i < HWA_HASH; i++) prev[i] = h[i];
    }
    uint8_t d = 0;
    for (uint32_t i = 0; i < HWA_HASH; i++) d |= (uint8_t) (log->head[i] ^ prev[i]);
    return d == 0;
}

const hwa_rec_t *hwa_get(const hwa_log_t *log, uint32_t index)
{
    return log && index < log->n ? &log->rec[index] : 0;
}

uint32_t hwa_forbidden(const hwa_log_t *log)
{
    uint32_t c = 0;
    for (uint32_t i = 0; log && i < log->n; i++)
        if (lpx_forbidden(log->rec[i].tag)) c++;
    return c;
}
