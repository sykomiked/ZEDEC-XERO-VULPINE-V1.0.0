/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_trunk_bank.c — the trunk-bank spec's three tests plus line states, the
 * spec's 160-sample rectangular block for comparison, the harmonic cross-talk
 * that nonlinearity causes, and all ten trunks carrying frames at once. */
#include "zt_htest.h"
#include "zt_trunk_bank.h"
#include "zt_harmonic_tables.h"

#define BLOCKS 25u
static int16_t pcm[ZT_TRUNK_FRAME_SAMPLES];

static void clear(int16_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) p[i] = 0;
}

static uint32_t lcg(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

static void report_db(const char *what, int32_t db10)
{
    ht_puts("  ");
    ht_puts(what);
    ht_puts(": ");
    if (db10 < 0) ht_puts("-");
    int32_t a = db10 < 0 ? -db10 : db10;
    ht_putd(a / 10);
    ht_puts(".");
    ht_putd(a % 10);
    ht_puts(" dB re full scale\n");
}

/* Test 1: full-scale 555 + 666 Hz; 444 and 777 Hz below the noise floor. */
static void t1_crosstalk(void)
{
    zt_trunk_mux_bank_t mux;
    zt_trunk_demux_bank_t bank;
    zt_trunk_mux_init(&mux);
    zt_trunk_bank_init(&bank);
    mux.amplitude[4] = 16383; /* the pair peaks at full scale */
    mux.amplitude[5] = 16383;
    mux.phase_acc[5] = 0x12345678u;
    size_t n = BLOCKS * ZT_TRUNK_BLOCK;
    clear(pcm, n);
    zt_trunk_mux_tone(&mux, ZT_TRK_PULSE, pcm, n);
    zt_trunk_mux_tone(&mux, ZT_TRK_BRIG, pcm, n);
    int32_t peak = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t a = pcm[i] < 0 ? -pcm[i] : pcm[i];
        if (a > peak) peak = a;
    }
    HT_CHECK(peak > 30000, "T1 composite reaches full scale");
    zt_trunk_bank_process_pcm(&bank, pcm, n);
    HT_CHECK(bank.blocks == BLOCKS, "T1 block count");
    HT_CHECK(bank.peak_power[3] < zt_trunk_noise_floor(), "T1 444 Hz below the noise floor");
    HT_CHECK(bank.peak_power[6] < zt_trunk_noise_floor(), "T1 777 Hz below the noise floor");
    HT_CHECK(bank.line_state[3] == ZT_LINE_DEAD && bank.line_state[6] == ZT_LINE_DEAD,
             "T1 444 and 777 report DEAD");
    HT_CHECK(bank.line_state[4] == ZT_LINE_ON_HOOK && bank.line_state[5] == ZT_LINE_ON_HOOK,
             "T1 555 and 666 steady: ON_HOOK");
    bool others = true;
    for (uint32_t i = 0; i < ZT_NUM_TRUNKS; i++)
        if (i != 4 && i != 5) others = others && bank.peak_power[i] < zt_trunk_noise_floor();
    HT_CHECK(others, "T1 all eight other trunks below the floor");
    report_db("555 Hz", zt_trunk_power_db10(bank.peak_power[4]));
    report_db("444 Hz leakage (worst block)", zt_trunk_power_db10(bank.peak_power[3]));
    report_db("777 Hz leakage (worst block)", zt_trunk_power_db10(bank.peak_power[6]));
    report_db("noise floor", zt_trunk_power_db10(zt_trunk_noise_floor()));

    /* The spec's block for comparison: 160 samples, rectangular window. */
    int64_t worst = 0;
    for (uint32_t b = 0; b + 160u <= n; b += 160u) {
        int64_t q1 = 0, q2 = 0, c = ZT_TRUNK_COEF_Q14[3];
        for (uint32_t k = 0; k < 160u; k++) {
            int64_t q0 = pcm[b + k] + ((c * q1) >> 14) - q2;
            q2 = q1;
            q1 = q0;
        }
        int64_t p = q1 * q1 + q2 * q2 - c * ((q1 * q2) >> 14);
        /* scale to the same full-scale reference: a 32767 tone over 160
         * rectangular samples has |X| = 32767 * 80 */
        int64_t ref = (int64_t) 32767 * 80;
        ref *= ref;
        /* compare p / ref against 10^-4.5 = 1 / 31623 */
        if (p * 31623 > ref && p > worst) worst = p;
    }
    HT_CHECK(worst > 0, "spec's 160-sample rectangular block leaks 555+666 into 444 above -45 dB");
}

/* Test 2: distinct frames on trunks 2 and 7 in one buffer, zero bit errors. */
static void t2_concurrent(void)
{
    zt_trunk_mux_bank_t mux;
    zt_trunk_demux_bank_t bank;
    zt_trunk_mux_init(&mux);
    zt_trunk_bank_init(&bank);
    mux.amplitude[2] = mux.amplitude[7] = 15000;
    const uint32_t a3[3] = {0x11223344u, 0x55667788u, 0x99AABBCCu},
                   a2[2] = {0xDDEEFF00u, 0x12345678u};
    const uint32_t b3[3] = {0xCAFEBABEu, 0x00000001u, 0x80000000u}, b2[2] = {0xFFFFFFFFu, 0u};
    zt_ubh168_frame_t fa, fb, ra, rb;
    zt_wire_pack_ubh168(2, a3, a2, &fa);
    zt_wire_pack_ubh168(7, b3, b2, &fb);
    clear(pcm, ZT_TRUNK_FRAME_SAMPLES);
    HT_CHECK(zt_trunk_mux_transmit_frame(&mux, ZT_TRK_LEDGER, &fa, pcm, ZT_TRUNK_FRAME_SAMPLES),
             "T2 transmit trunk 2");
    HT_CHECK(zt_trunk_mux_transmit_frame(&mux, ZT_TRK_PARADOX, &fb, pcm, ZT_TRUNK_FRAME_SAMPLES),
             "T2 transmit trunk 7");
    HT_CHECK(zt_trunk_read_frame(&bank, ZT_TRK_LEDGER, pcm, ZT_TRUNK_FRAME_SAMPLES, &ra),
             "T2 trunk 2 frame valid");
    HT_CHECK(zt_trunk_read_frame(&bank, ZT_TRK_PARADOX, pcm, ZT_TRUNK_FRAME_SAMPLES, &rb),
             "T2 trunk 7 frame valid");
    uint8_t x[21], y[21];
    uint32_t errs = 0;
    zt_wire_to_octets(&fa, x);
    zt_wire_to_octets(&ra, y);
    for (int i = 0; i < 21; i++) errs += (uint32_t) __builtin_popcount((unsigned) (x[i] ^ y[i]));
    zt_wire_to_octets(&fb, x);
    zt_wire_to_octets(&rb, y);
    for (int i = 0; i < 21; i++) errs += (uint32_t) __builtin_popcount((unsigned) (x[i] ^ y[i]));
    HT_CHECK(errs == 0, "T2 zero bit errors on both trunks");
    /* the streaming bank sees both lines OFF_HOOK during the burst, others DEAD */
    zt_trunk_bank_process_pcm(&bank, pcm, ZT_TRUNK_FRAME_SAMPLES);
    HT_CHECK(bank.offhook_blocks[2] == ZT_TRUNK_FRAME_SYMBOLS &&
                 bank.offhook_blocks[7] == ZT_TRUNK_FRAME_SYMBOLS,
             "T2 every symbol block reads OFF_HOOK on 2 and 7");
    bool dead = true;
    for (uint32_t i = 0; i < ZT_NUM_TRUNKS; i++)
        if (i != 2 && i != 7)
            dead =
                dead && bank.offhook_blocks[i] == 0 && bank.peak_power[i] < zt_trunk_noise_floor();
    HT_CHECK(dead, "T2 no other trunk sees the bursts");
    zt_unpacked_rails_t r;
    zt_wire_rails_init(&r);
    zt_wire_unpack_ubh168(&rb, &r);
    HT_CHECK(r.trunk_id == 7 && r.s_plus[0] == 0xCAFEBABEu, "T2 decoded frame unpacks");
    ht_puts("  rate: 2 bits per 320-sample symbol = 50 bit/s per trunk; one 21-octet frame = 85 "
            "blocks = 27200 samples = 3.4 s\n");
}

/* All ten trunks carry different frames at once. */
static void ten_at_once(void)
{
    zt_trunk_mux_bank_t mux;
    zt_trunk_demux_bank_t bank;
    zt_trunk_mux_init(&mux);
    zt_trunk_bank_init(&bank);
    zt_ubh168_frame_t tx[10], rx;
    uint32_t seed = 7u;
    clear(pcm, ZT_TRUNK_FRAME_SAMPLES);
    for (uint32_t t = 0; t < 10; t++) {
        uint32_t s3[3] = {lcg(&seed), lcg(&seed), lcg(&seed)}, s2[2] = {lcg(&seed), lcg(&seed)};
        mux.amplitude[t] = 3200;
        zt_wire_pack_ubh168((uint8_t) t, s3, s2, &tx[t]);
        zt_trunk_mux_transmit_frame(&mux, (zt_trunk_id_t) t, &tx[t], pcm, ZT_TRUNK_FRAME_SAMPLES);
    }
    uint32_t errs = 0, valid = 0;
    for (uint32_t t = 0; t < 10; t++) {
        uint8_t x[21], y[21];
        valid += zt_trunk_read_frame(&bank, (zt_trunk_id_t) t, pcm, ZT_TRUNK_FRAME_SAMPLES, &rx);
        zt_wire_to_octets(&tx[t], x);
        zt_wire_to_octets(&rx, y);
        for (int i = 0; i < 21; i++)
            errs += (uint32_t) __builtin_popcount((unsigned) (x[i] ^ y[i]));
    }
    HT_CHECK(valid == 10 && errs == 0, "ten simultaneous frames, zero bit errors (500 bit/s)");
}

/* Test 3: one second of ten-carrier audio. */
static void t3_workload(void)
{
    static int16_t sec[8000];
    zt_trunk_mux_bank_t mux;
    zt_trunk_demux_bank_t bank;
    zt_trunk_mux_init(&mux);
    clear(sec, 8000);
    for (uint32_t t = 0; t < 10; t++) {
        mux.amplitude[t] = 3000;
        zt_trunk_mux_tone(&mux, (zt_trunk_id_t) t, sec, 8000);
    }
    uint64_t best = ~0ull;
    for (int rep = 0; rep < 20; rep++) {
        zt_trunk_bank_init(&bank);
        uint64_t t0 = ht_now_ns();
        zt_trunk_bank_process_pcm(&bank, sec, 8000);
        uint64_t dt = ht_now_ns() - t0;
        if (dt < best) best = dt;
    }
    bool all_on = true;
    for (uint32_t t = 0; t < 10; t++) all_on = all_on && bank.line_state[t] == ZT_LINE_ON_HOOK;
    HT_CHECK(all_on, "T3 ten steady carriers: all ON_HOOK");
    HT_CHECK(bank.blocks == 25u, "T3 25 blocks per second");
    ht_puts("  1 s of 10-carrier audio (8000 samples): best of 20 = ");
    ht_putd((int64_t) (best / 1000u));
    ht_puts(" us\n");
    if (HT_TIMED) HT_CHECK(best < 50000000ull, "T3 under a generous 50 ms bound");
}

static void line_states(void)
{
    zt_trunk_mux_bank_t mux;
    zt_trunk_demux_bank_t bank;
    zt_trunk_mux_init(&mux);
    zt_trunk_bank_init(&bank);
    size_t n = 4u * ZT_TRUNK_BLOCK;
    clear(pcm, n);
    zt_trunk_bank_process_pcm(&bank, pcm, n);
    bool dead = true;
    for (uint32_t t = 0; t < 10; t++) dead = dead && zt_trunk_query_line(&bank, t) == ZT_LINE_DEAD;
    HT_CHECK(dead, "silence: all DEAD");
    /* a steady burst is ON_HOOK, not OFF_HOOK */
    zt_trunk_bank_init(&bank);
    zt_trunk_mux_tone(&mux, ZT_TRK_FLEET, pcm, n);
    zt_trunk_bank_process_pcm(&bank, pcm, n);
    HT_CHECK(bank.line_state[9] == ZT_LINE_ON_HOOK && bank.offhook_blocks[9] == 0,
             "steady 1111 Hz: ON_HOOK, never OFF_HOOK");
    /* full-scale tone reads about 0 dB */
    zt_trunk_mux_init(&mux);
    zt_trunk_bank_init(&bank);
    clear(pcm, n);
    mux.amplitude[4] = 32767;
    zt_trunk_mux_tone(&mux, ZT_TRK_PULSE, pcm, n);
    zt_trunk_bank_process_pcm(&bank, pcm, n);
    int32_t db = zt_trunk_power_db10(bank.power[4]);
    HT_CHECK(db > -5 && db < 5, "full-scale tone reads 0 +- 0.5 dB");
}

/* B1: nonlinearity on one trunk lands on another. A 333 Hz tone clipped at a
 * third of its peak puts its 3rd harmonic on trunk 8 (999 Hz). */
static void harmonic_crosstalk(void)
{
    zt_trunk_mux_bank_t mux;
    zt_trunk_demux_bank_t bank;
    zt_trunk_mux_init(&mux);
    zt_trunk_bank_init(&bank);
    size_t n = 4u * ZT_TRUNK_BLOCK;
    clear(pcm, n);
    mux.amplitude[2] = 30000;
    zt_trunk_mux_tone(&mux, ZT_TRK_LEDGER, pcm, n);
    zt_trunk_bank_process_pcm(&bank, pcm, n);
    HT_CHECK(bank.peak_power[8] < zt_trunk_noise_floor(), "linear 333 Hz: 999 Hz clean");
    for (size_t i = 0; i < n; i++) {
        if (pcm[i] > 10000) pcm[i] = 10000;
        if (pcm[i] < -10000) pcm[i] = -10000;
    }
    zt_trunk_bank_init(&bank);
    zt_trunk_bank_process_pcm(&bank, pcm, n);
    HT_CHECK(bank.peak_power[8] > zt_trunk_noise_floor(),
             "clipped 333 Hz: 3rd harmonic seizes trunk 8 (999 Hz) - keep the bus linear");
    report_db("999 Hz from clipped 333 Hz", zt_trunk_power_db10(bank.peak_power[8]));
}

int main(void)
{
    t1_crosstalk();
    t2_concurrent();
    ten_at_once();
    t3_workload();
    line_states();
    harmonic_crosstalk();
    return ht_finish("test_trunk_bank");
}
