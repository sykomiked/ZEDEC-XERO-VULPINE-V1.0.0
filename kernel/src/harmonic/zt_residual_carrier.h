/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_residual_carrier.h — ten integer tones whose amplitudes follow the
 * tensor engine's per-shell residuals.
 *
 * What it is: a ten-oscillator synthesiser. Shell s (0..9) drives the tone of
 * trunk s (111 .. 999, 1111 Hz). Each shell has a leaky accumulator of
 * residuals ("delta" = activation minus parent prediction, from the
 * holographic coding of zt.h T15). Every 8 kHz sample:
 *
 *   R1  phase_s += f_s * 65536 / 8000 (16 bits, truncated as the spec says:
 *       111 Hz becomes 110.96 Hz; table in zt_harmonic_tables.h).
 *   R2  wave = sine from a 16-step quarter-wave Q15 table (the spec's size),
 *       linearly interpolated (without interpolation the 64-point table's
 *       spurs land near other trunks).
 *   R3  amplitude = clamp(|acc_s|, ZT_CARRIER_BASELINE, ZT_CARRIER_AMP_MAX);
 *       a negative accumulator inverts the wave (a 180 degree phase step).
 *       The baseline 0x0400 (-30 dB re full scale) keeps an idle line
 *       ON_HOOK: the dial tone never drops.
 *   R4  acc_s -= acc_s >> 10 (leaky integrator, time constant 1024 samples =
 *       128 ms), so a burst of residuals decays back to the baseline.
 *   R5  the ten tones are summed and saturated to int16. Saturation is a
 *       nonlinearity: with several shells surging at once it puts harmonics
 *       of 111 Hz on other trunks (zt_trunk_bank.h B1).
 *
 * The spec says the tones are a "side effect of the matrix multiply" with
 * "zero extra FLOPs". In this code the harvest is one saturating add per call
 * (the engine's tap does it inside zt_holo_encode), and the synthesis costs
 * about ten table lookups and multiplies per output sample. It is ordinary
 * computation, not free.
 */
#ifndef ZT_RESIDUAL_CARRIER_H
#define ZT_RESIDUAL_CARRIER_H

#include "zt_trunk_bank.h"
#include "zt_residual_tap.h"

#define ZT_CARRIER_RING_SIZE 256u /* per-shell history of synthesised samples */
#define ZT_CARRIER_BASELINE  0x0400
#define ZT_CARRIER_AMP_MAX   0x5000
#define ZT_CARRIER_CYCLE     27720u /* swarm_harmonic fundamental, in samples */

_Static_assert(ZT_RESIDUAL_SHELLS == ZT_NUM_TRUNKS, "one shell per trunk");

typedef struct {
    int32_t delta_accumulator; /* leaky sum of residuals / 256 */
    uint32_t step_counter;     /* 16-bit phase in the low half */
    int16_t sample_history[ZT_CARRIER_RING_SIZE];
    uint8_t ring_head; /* wraps at 256 by itself */
} zt_shell_residual_t;

typedef struct {
    zt_shell_residual_t shells[ZT_NUM_TRUNKS]; /* shell s drives trunk s */
    uint32_t master_tick;                      /* sample count mod 27720 */
    uint32_t fundamentals;                     /* completed 27720-sample cycles */
    int16_t composite_hum;                     /* last mixed sample */
    zt_residual_tap_t tap;                     /* engine tap target (zt_residual_tap.h) */
} zt_residual_generator_t;

void zt_residual_carrier_init(zt_residual_generator_t *gen);

/* The siphon: add delta_q16 / 256 to a shell, saturating. Out-of-range
 * shells are ignored. */
static inline void zt_residual_harvest_delta(zt_residual_generator_t *gen, uint8_t shell_idx,
                                             int32_t delta_q16)
{
    if (shell_idx < ZT_NUM_TRUNKS) {
        int32_t *a = &gen->shells[shell_idx].delta_accumulator;
        int32_t r;
        if (__builtin_add_overflow(*a, delta_q16 >> 8, &r))
            r = delta_q16 < 0 ? INT32_MIN : INT32_MAX;
        *a = r;
    }
}

/* Move whatever the engine's tap collected into the shells and clear it. */
void zt_residual_consume_tap(zt_residual_generator_t *gen);

int16_t zt_residual_step_sample(zt_residual_generator_t *gen);
/* Consumes the tap once, then renders sample_count samples. */
void zt_residual_render_stream(zt_residual_generator_t *gen, int16_t *dst_pcm, size_t sample_count);

#endif /* ZT_RESIDUAL_CARRIER_H */
