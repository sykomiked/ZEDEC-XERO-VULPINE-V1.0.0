/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* almanac_model.h — Almanac, "about this machine", as a model (zxm.h): the
 * system, the hardware zxvd sees, zxvd itself and the volume holding the
 * ZXV folder (sys.info, sys.disk). Read only. No drawing, no allocation. */
#ifndef ALMANAC_MODEL_H
#define ALMANAC_MODEL_H

#include "zxm.h"

typedef struct {
    zxm_link_t *link;
    bool have; /* a refresh has succeeded */
    char os[16], os_version[64], arch[16], host[64];
    uint32_t cpus;
    uint64_t mem_total, mem_avail, uptime_s;
    bool load_known;
    int32_t load_x100[3]; /* 1, 5, 15 minutes, x100 */
    uint64_t pid, cpu_user_us, cpu_sys_us, rss_bytes, max_rss_bytes;
    bool disk_known;
    char disk_path[512];
    uint64_t disk_total, disk_free, disk_avail;
    zxm_held_t held;
} almanac_t;

enum { ALMANAC_AI_EXPLAIN, ALMANAC_AI_COUNT };
extern const zxm_ai_action_t ALMANAC_AI[ALMANAC_AI_COUNT];

void almanac_init(almanac_t *a, zxm_link_t *link);
/* Read sys.info, then sys.disk. A disk the daemon cannot read leaves
 * disk_known false and still succeeds; nothing is kept from a failed read. */
int almanac_refresh(almanac_t *a);
/* Share of memory in use and of the volume used, in hundredths of a percent
 * (0..10000), or -1 when unknown. */
int32_t almanac_mem_used_x100(const almanac_t *a);
int32_t almanac_disk_used_x100(const almanac_t *a);
/* "3 days, 4 hours" style; out is always terminated. */
void almanac_uptime_text(uint64_t seconds, char *out, uint32_t cap);
/* "1.5 GB" style (1000s, as storage is sold); out is always terminated. */
void almanac_bytes_text(uint64_t bytes, char *out, uint32_t cap);

/* AI: explain these facts in plain words (held, M3). */
int almanac_ai(almanac_t *a, int32_t action);
int almanac_ai_accept(almanac_t *a);
int almanac_ai_reject(almanac_t *a);

#endif
