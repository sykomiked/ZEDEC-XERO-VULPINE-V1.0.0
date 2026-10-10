/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* swarm_boot.c — the swarm's boot self-check and the combined [AI_OK]
 * check. See swarm_boot.h. */
#include "swarm_boot.h"
#include "swarm_budget.h"
#include "../tensor/zt_boot.h" /* zt_boot_fnv, zt_boot_selfcheck */

static const uint32_t k_level_of[SWARM_BOOT_MODELS] = {0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 3};
static const uint32_t k_allot[SWARM_BOOT_MODELS] = {454, 137, 136, 61, 61, 60, 19, 18, 18, 18, 18};
static const uint32_t k_level_budget[SWARM_BOOT_LEVELS] = {454, 273, 182, 91};

static swarm_budget_t g_boot_budget; /* static, not on the boot stack */

int swarm_boot_selfcheck(uint32_t *hash)
{
    swarm_budget_t *b = &g_boot_budget;
    uint32_t h = ZT_BOOT_FNV_INIT;
    int fail = 0;
    uint64_t g1 = 0, g7 = 0, sum = 0;

    if (hash) *hash = 0;
    if (swarm_budget_init(b, SWARM_BOOT_LEVELS, SWARM_BOOT_TOKENS) != SWARM_OK) return 1;
    for (uint32_t i = 0; i < SWARM_BOOT_MODELS; i++)
        if (swarm_budget_register(b, i + 1u, k_level_of[i]) != SWARM_OK) return 2;
    /* R1: a sixth model on level 3 must be refused. */
    if (swarm_budget_register(b, 99u, 3u) != SWARM_ERR_FULL) return 3;
    if (swarm_budget_begin_cycle(b) != SWARM_OK) return 4;

    for (uint32_t i = 0; i < SWARM_BOOT_MODELS; i++) {
        uint64_t a = b->slots[i].allotted;
        if (b->slots[i].model_id != i + 1u && !fail) fail = 5;
        if (a != k_allot[i] && !fail) fail = 6;
        sum += a;
        h = zt_boot_fnv(h, (uint32_t) a);
    }
    if (sum != SWARM_BOOT_TOKENS && !fail) fail = 7; /* R5: exactly T */
    for (uint32_t d = 0; d < SWARM_BOOT_LEVELS; d++) {
        if (b->level_budget[d] != k_level_budget[d] && !fail) fail = 8;
        h = zt_boot_fnv(h, (uint32_t) b->level_budget[d]);
    }
    if ((swarm_budget_consume(b, 1u, 500u, &g1) != SWARM_OK || g1 != 454u) && !fail) fail = 9;
    if ((swarm_budget_consume(b, 7u, 10u, &g7) != SWARM_OK || g7 != 10u) && !fail) fail = 10;
    h = zt_boot_fnv(h, (uint32_t) g1);
    h = zt_boot_fnv(h, (uint32_t) g7);
    if (swarm_budget_end_cycle(b) != SWARM_OK && !fail) fail = 11;
    if (b->last_unused != SWARM_BOOT_UNUSED && !fail) fail = 12; /* R6 */
    h = zt_boot_fnv(h, (uint32_t) b->last_unused);

    if (hash) *hash = h;
    if (!fail && h != SWARM_BOOT_HASH) fail = 13;
    return fail;
}

static char *put_str(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    return p;
}

static char *put_hex(char *p, uint32_t v)
{
    static const char hx[] = "0123456789abcdef";
    *p++ = '0';
    *p++ = 'x';
    for (int k = 7; k >= 0; k--) *p++ = hx[(v >> (4 * k)) & 0xFu];
    return p;
}

static char *put_dec(char *p, uint32_t v)
{
    char d[10];
    uint32_t n = 0;
    do {
        d[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v && n < 10u);
    while (n) *p++ = d[--n];
    return p;
}

bool ai_boot_selfcheck(void (*out)(const char *))
{
    static char line[128];
    uint32_t hs = 0, ht = 0;
    int fs = swarm_boot_selfcheck(&hs);
    int ft = zt_boot_selfcheck(&ht);
    uint32_t ha = zt_boot_fnv(zt_boot_fnv(ZT_BOOT_FNV_INIT, hs), ht);
    bool ok = fs == 0 && ft == 0 && ha == AI_BOOT_HASH;
    char *p = line;

    if (ok) {
        p = put_str(p, "[AI_OK] swarm+tensor selfcheck ");
        p = put_hex(p, ha);
    } else {
        p = put_str(p, "[FAULT] AI selfcheck mismatch: swarm step ");
        p = put_dec(p, (uint32_t) fs);
        p = put_str(p, " ");
        p = put_hex(p, hs);
        p = put_str(p, ", tensor step ");
        p = put_dec(p, (uint32_t) ft);
        p = put_str(p, " ");
        p = put_hex(p, ht);
    }
    *p++ = '\n';
    *p = 0;
    if (out) out(line);
    return ok;
}
