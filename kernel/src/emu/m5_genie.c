/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* m5_genie.c — the Game Genie interception & substitution vector table
 * (m5_genie.h). Freestanding: no libc, no allocation, no floats. The table
 * is bounded and ordered; the bus hook is a straight walk of the enabled
 * entries, so a hit or miss is deterministic and cheap. */
#include "m5_genie.h"

static zxv_genie_patch_t g_table[ZXV_GENIE_MAX];
static uint32_t g_count;

static uint64_t fnv1a(const char *s)
{
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (uint32_t i = 0; s[i] && i < 64u; i++) {
        h ^= (uint8_t) s[i];
        h *= UINT64_C(0x100000001b3);
    }
    return h;
}

int genie_apply_intercept(const char *code, uint64_t addr, uint64_t cmp,
                          uint64_t rep, uint32_t width)
{
    if (g_count >= ZXV_GENIE_MAX) return -1;
    if (width != 1u && width != 2u && width != 4u) return -1;
    zxv_genie_patch_t *p = &g_table[g_count];
    p->target_addr = addr;
    p->compare_val = cmp;
    p->replace_val = rep;
    p->width_bytes = width;
    p->flags = ZXV_GENIE_FLAG_ENABLED;
    p->hits = 0;
    p->code_str[0] = 0;
    if (code)
        for (uint32_t i = 0; i + 1u < ZXV_GENIE_CODE_LEN && code[i]; i++)
            p->code_str[i] = code[i];
    return (int) g_count++;
}

int genie_decode_code(const char *code, zxv_genie_patch_t *out)
{
    const char *w[3];
    uint32_t i, wi = 0, len = 0;
    if (!code || !out) return -1;
    w[0] = code;
    for (i = 0; code[i]; i++) {
        if (code[i] == '-') {
            if (wi >= 2u) return -1; /* more than three words */
            w[++wi] = code + i + 1;
            len = 0;
            continue;
        }
        len++;
        if (len > 4u) return -1; /* a word longer than four characters */
    }
    if (wi != 2u || len == 0u) return -1;
    out->target_addr = fnv1a(w[0]) & UINT64_C(0xffffffff);
    out->compare_val = fnv1a(w[1]) & UINT64_C(0xffff);
    out->replace_val = fnv1a(w[2]) & UINT64_C(0xffffffff);
    out->width_bytes = 1u;
    out->flags = ZXV_GENIE_FLAG_ENABLED;
    out->hits = 0;
    for (i = 0; i + 1u < ZXV_GENIE_CODE_LEN && code[i]; i++) out->code_str[i] = code[i];
    return 0;
}

uint32_t genie_eval_read(uint32_t addr, uint32_t val, uint32_t width)
{
    uint32_t mask;
    uint32_t i;
    switch (width) {
    case 1u: mask = 0xFFu; break;
    case 2u: mask = 0xFFFFu; break;
    default: mask = 0xFFFFFFFFu; break;
    }
    for (i = 0; i < g_count; i++) {
        const zxv_genie_patch_t *p = &g_table[i];
        uint32_t pm = p->width_bytes == 1u ? 0xFFu
                     : p->width_bytes == 2u ? 0xFFFFu
                     : 0xFFFFFFFFu;
        if (!(p->flags & ZXV_GENIE_FLAG_ENABLED)) continue;
        if (addr != (uint32_t) p->target_addr) continue; /* the address matches exactly */
        if (p->compare_val != 0 && (val & pm) != (uint32_t) (p->compare_val & pm)) continue;
        ((zxv_genie_patch_t *) p)->hits++;
        return (val & ~mask) | (uint32_t) (p->replace_val & mask);
    }
    return val;
}

int genie_active(void)
{
    uint32_t i;
    for (i = 0; i < g_count; i++)
        if (g_table[i].flags & ZXV_GENIE_FLAG_ENABLED) return 1;
    return 0;
}

uint32_t genie_count(void)
{
    return g_count;
}

const zxv_genie_patch_t *genie_get(uint32_t i)
{
    return i < g_count ? &g_table[i] : (const zxv_genie_patch_t *) 0;
}

void genie_clear(void)
{
    for (uint32_t i = 0; i < ZXV_GENIE_MAX; i++)
        g_table[i].flags = 0;
    g_count = 0;
}