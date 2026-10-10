/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_boot.c — the tensor engine's boot self-check. See zt_boot.h. */
#include "zt_boot.h"
#include "zt.h"
#include "zt_rope.h"

#define NB (ZT_BOOT_DIM / ZT_BLOCK)

uint32_t zt_boot_fnv(uint32_t h, uint32_t v)
{
    for (uint32_t i = 0; i < 4u; i++) {
        h ^= (v >> (8u * i)) & 0xFFu;
        h *= 0x01000193u;
    }
    return h;
}

/* Every buffer is static: nothing large on the boot stack. */
static int8_t g_qx[ZT_BOOT_DIM];
static int8_t g_qw[ZT_BOOT_ROWS][ZT_BOOT_DIM];
static zt_fx g_x[ZT_BOOT_DIM];
static zt_fx g_w[ZT_BOOT_ROWS * ZT_BOOT_DIM];
static zt_q8_t g_xq[NB];
static zt_q8_t g_wq[ZT_BOOT_ROWS * NB];
static zt_fx g_y[ZT_BOOT_ROWS], g_h[ZT_BOOT_ROWS], g_q[ZT_BOOT_ROWS];
static zt_fx g_p[ZT_BOOT_ROWS], g_s[ZT_BOOT_ROWS], g_gain[ZT_BOOT_ROWS];
static zt_rope_t g_rope;

/* The fixed inputs. Values span -127..127; one element per block is pinned
 * to +-127 so every block's scale is exact. */
static void boot_inputs(void)
{
    for (uint32_t i = 0; i < ZT_BOOT_DIM; i++)
        g_qx[i] = (int8_t) ((int32_t) ((i * 37u + 11u) % 255u) - 127);
    g_qx[0] = 127;
    g_qx[ZT_BLOCK] = -127;
    for (uint32_t r = 0; r < ZT_BOOT_ROWS; r++) {
        for (uint32_t i = 0; i < ZT_BOOT_DIM; i++)
            g_qw[r][i] = (int8_t) ((int32_t) ((i * 13u + r * 29u + 7u) % 255u) - 127);
        g_qw[r][1] = 127;
        g_qw[r][ZT_BLOCK + 1u] = -127;
    }
    for (uint32_t i = 0; i < ZT_BOOT_DIM; i++) g_x[i] = (zt_fx) g_qx[i] * 512;
    for (uint32_t r = 0; r < ZT_BOOT_ROWS; r++)
        for (uint32_t i = 0; i < ZT_BOOT_DIM; i++)
            g_w[r * ZT_BOOT_DIM + i] = (zt_fx) g_qw[r][i] * 256;
    for (uint32_t i = 0; i < ZT_BOOT_ROWS; i++) g_gain[i] = ZT_ONE + (zt_fx) i * 4096;
}

static uint32_t fnv_vec(uint32_t h, const zt_fx *v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) h = zt_boot_fnv(h, (uint32_t) v[i]);
    return h;
}

int zt_boot_selfcheck(uint32_t *hash)
{
    uint32_t h = ZT_BOOT_FNV_INIT;
    int fail = 0;

    if (hash) *hash = 0;
    boot_inputs();
    zt_quantize(g_x, ZT_BOOT_DIM, g_xq, false);
    zt_quantize(g_w, ZT_BOOT_ROWS * ZT_BOOT_DIM, g_wq, false);
    for (uint32_t b = 0; b < NB; b++)
        if (g_xq[b].scale != 512) return 1;
    for (uint32_t b = 0; b < ZT_BOOT_ROWS * NB; b++)
        if (g_wq[b].scale != 256) return 2;

    /* y = W x, and the exact integer answer beside it. */
    zt_matvec(g_wq, ZT_BOOT_ROWS, g_xq, NB, g_y);
    for (uint32_t r = 0; r < ZT_BOOT_ROWS; r++) {
        int32_t s = 0;
        for (uint32_t i = 0; i < ZT_BOOT_DIM; i++) s += (int32_t) g_qw[r][i] * g_qx[i];
        if (g_y[r] != 2 * s && !fail) fail = 3;
    }

    zt_rmsnorm(g_y, g_gain, ZT_BOOT_ROWS, g_h);
    for (uint32_t i = 0; i < ZT_BOOT_ROWS; i++) g_q[i] = g_h[i];
    if (!zt_rope_init(&g_rope, ZT_BOOT_ROWS, 0x461C4000u /* 10000.0f */, false)) return 4;
    zt_rope_apply(&g_rope, g_q, ZT_BOOT_ROWS, ZT_BOOT_POS);

    int64_t psum = 0;
    for (uint32_t i = 0; i < ZT_BOOT_ROWS; i++) g_p[i] = g_q[i];
    zt_softmax(g_p, ZT_BOOT_ROWS);
    for (uint32_t i = 0; i < ZT_BOOT_ROWS; i++) {
        if (g_p[i] < 0 && !fail) fail = 5;
        psum += g_p[i];
    }
    if ((psum < ZT_ONE - 8 || psum > ZT_ONE + 8) && !fail) fail = 6;
    for (uint32_t i = 0; i < ZT_BOOT_ROWS; i++) g_s[i] = zt_silu(g_q[i]);
    if (zt_exp(0) != ZT_ONE && !fail) fail = 7;
    zt_fx e = zt_exp(-ZT_ONE);

    h = fnv_vec(h, g_y, ZT_BOOT_ROWS);
    h = fnv_vec(h, g_h, ZT_BOOT_ROWS);
    h = fnv_vec(h, g_q, ZT_BOOT_ROWS);
    h = fnv_vec(h, g_p, ZT_BOOT_ROWS);
    h = fnv_vec(h, g_s, ZT_BOOT_ROWS);
    h = zt_boot_fnv(h, (uint32_t) e);

    if (hash) *hash = h;
    if (!fail && h != ZT_BOOT_HASH) fail = 8;
    return fail;
}
