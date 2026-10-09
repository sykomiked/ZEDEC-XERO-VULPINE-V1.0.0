/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_residual_carrier.c — see zt_residual_carrier.h. */
#include "zt_residual_carrier.h"
#include "zt_harmonic_tables.h"

void zt_residual_carrier_init(zt_residual_generator_t *gen)
{
    for (uint32_t s = 0; s < ZT_NUM_TRUNKS; s++) {
        zt_shell_residual_t *sh = &gen->shells[s];
        sh->delta_accumulator = 0;
        sh->step_counter = 0;
        sh->ring_head = 0;
        for (uint32_t k = 0; k < ZT_CARRIER_RING_SIZE; k++) sh->sample_history[k] = 0;
        gen->tap.acc[s] = 0;
    }
    gen->master_tick = 0;
    gen->fundamentals = 0;
    gen->composite_hum = 0;
}

void zt_residual_consume_tap(zt_residual_generator_t *gen)
{
    for (uint32_t s = 0; s < ZT_NUM_TRUNKS; s++) {
        int32_t *a = &gen->shells[s].delta_accumulator;
        int32_t r;
        if (__builtin_add_overflow(*a, gen->tap.acc[s], &r))
            r = gen->tap.acc[s] < 0 ? INT32_MIN : INT32_MAX;
        *a = r;
        gen->tap.acc[s] = 0;
    }
}

/* R2: Q15 sine of a 16-bit phase, 16-step quarter wave, interpolated. */
static int32_t sin16(uint32_t phase)
{
    uint32_t quad = (phase >> 14) & 3u;
    uint32_t p = phase & 0x3FFFu;
    if (quad & 1u) p = 0x4000u - p; /* 0 .. 2^14 */
    uint32_t idx = p >> 10;         /* 0 .. 16 */
    int32_t v;
    if (idx >= 16u) {
        v = ZT_SINE_QW16_Q15[16];
    } else {
        int32_t a = ZT_SINE_QW16_Q15[idx], b = ZT_SINE_QW16_Q15[idx + 1u];
        v = a + (((b - a) * (int32_t) (p & 0x3FFu)) >> 10);
    }
    return quad & 2u ? -v : v;
}

int16_t zt_residual_step_sample(zt_residual_generator_t *gen)
{
    int32_t sum = 0;
    for (uint32_t s = 0; s < ZT_NUM_TRUNKS; s++) {
        zt_shell_residual_t *sh = &gen->shells[s];
        sh->step_counter = (sh->step_counter + ZT_RESIDUAL_STEP16[s]) & 0xFFFFu; /* R1 */
        int32_t acc = sh->delta_accumulator;
        uint32_t mag = acc < 0 ? 0u - (uint32_t) acc : (uint32_t) acc;
        int32_t amp = mag < (uint32_t) ZT_CARRIER_BASELINE  ? ZT_CARRIER_BASELINE
                      : mag > (uint32_t) ZT_CARRIER_AMP_MAX ? ZT_CARRIER_AMP_MAX
                                                            : (int32_t) mag; /* R3 */
        int32_t v = (sin16(sh->step_counter) * amp) >> 15;
        if (acc < 0) v = -v;
        sh->sample_history[sh->ring_head++] = (int16_t) v;
        sh->delta_accumulator = acc - (acc >> 10); /* R4 */
        sum += v;
    }
    if (sum > 32767) sum = 32767; /* R5 */
    if (sum < -32768) sum = -32768;
    gen->composite_hum = (int16_t) sum;
    if (++gen->master_tick == ZT_CARRIER_CYCLE) {
        gen->master_tick = 0;
        gen->fundamentals++;
    }
    return gen->composite_hum;
}

void zt_residual_render_stream(zt_residual_generator_t *gen, int16_t *dst_pcm, size_t sample_count)
{
    zt_residual_consume_tap(gen);
    for (size_t n = 0; n < sample_count; n++) dst_pcm[n] = zt_residual_step_sample(gen);
}
