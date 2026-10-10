/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_residual_carrier.c — the residual-carrier spec's three tests, through
 * the real trunk bank. */
#include "zt_htest.h"
#include "zt_residual_carrier.h"

static int16_t pcm[8000];

/* Test 1: idle generator, 8000 samples, all ten lines ON_HOOK. */
static void t1_idle(void)
{
    zt_residual_generator_t gen;
    zt_trunk_demux_bank_t bank;
    zt_residual_carrier_init(&gen);
    zt_trunk_bank_init(&bank);
    zt_residual_render_stream(&gen, pcm, 8000);
    zt_trunk_bank_process_pcm(&bank, pcm, 8000);
    bool on = true, never_off = true, never_dead = true;
    for (uint32_t t = 0; t < ZT_NUM_TRUNKS; t++) {
        on = on && zt_trunk_query_line(&bank, (zt_trunk_id_t) t) == ZT_LINE_ON_HOOK;
        never_off = never_off && bank.offhook_blocks[t] == 0;
        never_dead = never_dead && bank.onhook_blocks[t] == bank.blocks;
    }
    HT_CHECK(on, "T1 idle: all ten lines ON_HOOK");
    HT_CHECK(never_off && never_dead, "T1 idle: every block ON_HOOK (dial tone never drops)");
    int32_t db = zt_trunk_power_db10(bank.power[0]);
    HT_CHECK(db > -320 && db < -290, "T1 baseline tone at -30 dB re full scale");
    HT_CHECK(gen.master_tick == 8000u, "T1 master tick advanced");
}

/* Test 2: large positive deltas on shell 4 raise trunk 4 (555 Hz); 444 and
 * 666 Hz keep their baseline. A negative burst flips the phase: OFF_HOOK. */
static void t2_modulation(void)
{
    zt_residual_generator_t gen;
    zt_trunk_demux_bank_t base, bank;
    zt_residual_carrier_init(&gen);
    zt_trunk_bank_init(&base);
    zt_residual_render_stream(&gen, pcm, 8000);
    zt_trunk_bank_process_pcm(&base, pcm, 8000);

    zt_residual_carrier_init(&gen);
    zt_trunk_bank_init(&bank);
    for (uint32_t n = 0; n < 8000; n++) {
        zt_residual_harvest_delta(&gen, 4, 0x00400000); /* +64.0 per sample */
        pcm[n] = zt_residual_step_sample(&gen);
    }
    zt_trunk_bank_process_pcm(&bank, pcm, 8000);
    int32_t up = zt_trunk_power_db10(bank.power[4]) - zt_trunk_power_db10(base.power[4]);
    int32_t d3 = zt_trunk_power_db10(bank.power[3]) - zt_trunk_power_db10(base.power[3]);
    int32_t d5 = zt_trunk_power_db10(bank.power[5]) - zt_trunk_power_db10(base.power[5]);
    HT_CHECK(up > 200, "T2 555 Hz surges by more than 20 dB");
    HT_CHECK(d3 > -10 && d3 < 10 && d5 > -10 && d5 < 10, "T2 444 and 666 Hz stay within 1 dB");
    HT_CHECK(bank.line_state[3] == ZT_LINE_ON_HOOK && bank.line_state[5] == ZT_LINE_ON_HOOK,
             "T2 neighbours remain ON_HOOK");
    ht_puts("  555 Hz surge: +");
    ht_putd(up / 10);
    ht_puts(" dB; 444 Hz change ");
    ht_putd(d3);
    ht_puts(", 666 Hz change ");
    ht_putd(d5);
    ht_puts(" (tenths of a dB)\n");

    /* phase step: drive shell 4 negative in one burst after a steady block */
    zt_residual_carrier_init(&gen);
    zt_trunk_bank_init(&bank);
    zt_residual_render_stream(&gen, pcm, 640);
    zt_trunk_bank_process_pcm(&bank, pcm, 640);
    gen.tap.acc[4] = -0x4000; /* what the tensor tap would have collected */
    zt_residual_render_stream(&gen, pcm, 320);
    zt_trunk_bank_process_pcm(&bank, pcm, 320);
    HT_CHECK(bank.line_state[4] == ZT_LINE_OFF_HOOK,
             "T2 negative residual: 180 deg step, OFF_HOOK");
    HT_CHECK(bank.line_state[3] != ZT_LINE_OFF_HOOK && bank.line_state[5] != ZT_LINE_OFF_HOOK,
             "T2 phase step stays on its own trunk");
    HT_CHECK(gen.tap.acc[4] == 0, "T2 tap consumed");
}

/* Test 3: cost of 1,000,000 harvests. */
static void t3_cost(void)
{
    static zt_residual_generator_t gen;
    zt_residual_carrier_init(&gen);
    volatile int32_t d = 0x100;
    uint64_t best = ~0ull;
    for (int rep = 0; rep < 5; rep++) {
        uint64_t t0 = ht_now_ns();
        for (uint32_t i = 0; i < 1000000u; i++)
            zt_residual_harvest_delta(&gen, (uint8_t) (i % 10u), d);
        uint64_t dt = ht_now_ns() - t0;
        if (dt < best) best = dt;
    }
    HT_CHECK(gen.shells[3].delta_accumulator == 5 * 100000, "T3 harvest sums exactly");
    ht_puts("  1,000,000 harvests: best of 5 = ");
    ht_putd((int64_t) (best / 1000u));
    ht_puts(" us\n");
    if (HT_TIMED) HT_CHECK(best < 100000000ull, "T3 under a generous 100 ms bound");
    /* saturation, not wrap */
    zt_residual_carrier_init(&gen);
    for (int i = 0; i < 1000; i++) zt_residual_harvest_delta(&gen, 2, INT32_MAX);
    HT_CHECK(gen.shells[2].delta_accumulator == INT32_MAX, "harvest saturates");
    zt_residual_harvest_delta(&gen, 10, 5);
    zt_residual_harvest_delta(&gen, 255, 5);
    HT_CHECK(gen.shells[9].delta_accumulator == 0, "out-of-range shell ignored");
}

int main(void)
{
    t1_idle();
    t2_modulation();
    t3_cost();
    return ht_finish("test_residual_carrier");
}
