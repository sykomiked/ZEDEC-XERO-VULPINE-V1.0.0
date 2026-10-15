/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tidewatch_model.c — Tidewatch (see tidewatch_model.h). */
#include <string.h>

#include "tidewatch_model.h"

void tidewatch_init(tidewatch_t *t, zxm_link_t *link)
{
    memset(t, 0, sizeof *t);
    t->link = link;
}

int tidewatch_sample(tidewatch_t *t)
{
    zxm_link_t *l = t->link;
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc) return rc;
    uint64_t now, cpus, total, avail, user, sys, rss;
    int32_t z = zxm_get(l, l->result, "zxvd");
    if (zxm_get_u64(l, l->result, "time_ms", &now) || zxm_get_u64(l, l->result, "cpus", &cpus) ||
        cpus > UINT32_MAX || zxm_get_u64(l, l->result, "mem_total", &total) ||
        zxm_get_u64(l, l->result, "mem_avail", &avail) || z < 0 || l->tok[z].type != W4_J_OBJ ||
        zxm_get_u64(l, z, "cpu_user_us", &user) || zxm_get_u64(l, z, "cpu_sys_us", &sys) ||
        zxm_get_u64(l, z, "rss_bytes", &rss))
        return ZXM_E_PROTOCOL;
    tidewatch_sample_t s = {now, -1, rss, -1, -1};
    uint64_t cpu = user + sys;
    if (t->have_last && now > t->last_ms && cpu >= t->last_cpu_us)
        /* us of CPU per ms of wall time, as a share of one CPU, x100 */
        s.zxvd_cpu_x100 = (int32_t) ((cpu - t->last_cpu_us) * 10u / (now - t->last_ms));
    if (total && avail && avail <= total)
        s.mem_used_x100 = (int32_t) ((total - avail) * 10000u / total);
    int32_t ld = zxm_get(l, l->result, "load");
    if (ld >= 0 && l->tok[ld].type == W4_J_ARR && zxm_len(l, ld) == 3) {
        int64_t v;
        int32_t e = zxm_at(l, ld, 0);
        if (e >= 0 && w4_json_i64(l->resp, &l->tok[e], &v) == W4_OK && v >= 0 && v <= INT32_MAX)
            s.load1_x100 = (int32_t) v;
    }
    t->cpus = (uint32_t) cpus;
    t->mem_total = total;
    t->last_cpu_us = cpu;
    t->last_ms = now;
    t->have_last = true;
    if (t->n == TIDEWATCH_SAMPLES) {
        t->first = (t->first + 1) % TIDEWATCH_SAMPLES;
        t->n--;
    }
    t->s[(t->first + t->n) % TIDEWATCH_SAMPLES] = s;
    t->n++;
    return ZXM_OK;
}

uint32_t tidewatch_count(const tidewatch_t *t)
{
    return t->n;
}

const tidewatch_sample_t *tidewatch_at(const tidewatch_t *t, uint32_t i)
{
    return i < t->n ? &t->s[(t->first + i) % TIDEWATCH_SAMPLES] : NULL;
}

const tidewatch_sample_t *tidewatch_latest(const tidewatch_t *t)
{
    return t->n ? tidewatch_at(t, t->n - 1) : NULL;
}

int32_t tidewatch_peak_cpu_x100(const tidewatch_t *t)
{
    int32_t p = 0;
    for (uint32_t i = 0; i < t->n; i++) {
        int32_t v = tidewatch_at(t, i)->zxvd_cpu_x100;
        if (v > p) p = v;
    }
    return p;
}
