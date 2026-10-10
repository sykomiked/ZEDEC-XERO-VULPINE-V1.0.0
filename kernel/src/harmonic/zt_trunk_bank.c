/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_trunk_bank.c — ten-trunk Goertzel bank and DQPSK frames. See
 * zt_trunk_bank.h. The integer Goertzel follows kernel/src/legacy/dtmf.c
 * (int64 state, Q14 coefficient, the same power formula). */
#include "zt_trunk_bank.h"
#include "zt_harmonic_tables.h"

_Static_assert(ZT_TRUNK_BLOCK == ZT_TRUNK_BLOCK_SAMPLES, "regenerate zt_harmonic_tables.h");
_Static_assert(ZT_TRUNK_FRAME_SYMBOLS * 2u == 8u * ZT_UBH168_FRAME_BYTES, "84 symbols = 168 bits");

const uint16_t ZT_TRUNK_FREQS[ZT_NUM_TRUNKS] = {111, 222, 333, 444, 555, 666, 777, 888, 999, 1111};

/* ===== sine ===== */

int32_t zt_trunk_sin_q15(uint32_t phase)
{
    uint32_t quad = phase >> 30;
    uint32_t p = phase & 0x3FFFFFFFu;
    if (quad & 1u) p = 0x40000000u - p; /* mirror: 0 .. 2^30 */
    uint32_t idx = p >> 22;             /* 0 .. 256 */
    int32_t v;
    if (idx >= 256u) {
        v = ZT_SINE_QW256_Q15[256];
    } else {
        int32_t a = ZT_SINE_QW256_Q15[idx], b = ZT_SINE_QW256_Q15[idx + 1u];
        int32_t frac = (int32_t) ((p >> 6) & 0xFFFFu);
        v = a + (int32_t) (((int64_t) (b - a) * frac) >> 16);
    }
    return quad & 2u ? -v : v;
}

static int16_t sat16(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t) v;
}

/* ===== receiver helpers ===== */

static int64_t abs64(int64_t v)
{
    return v < 0 ? -v : v;
}

/* Shift (re, im) so the larger magnitude lies in [2^19, 2^20). */
static void normalise(int64_t re, int64_t im, int32_t *nre, int32_t *nim)
{
    int64_t m = abs64(re) > abs64(im) ? abs64(re) : abs64(im);
    if (m == 0) {
        *nre = *nim = 0;
        return;
    }
    while (m >= (1LL << 20)) {
        re >>= 1;
        im >>= 1;
        m >>= 1;
    }
    while (m < (1LL << 19)) {
        re *= 2;
        im *= 2;
        m *= 2;
    }
    *nre = (int32_t) re;
    *nim = (int32_t) im;
}

/* Sector 0..7 of the angle of (x, y): sector s covers s*45 +- 22.5 degrees. */
static uint32_t sector8(int64_t x, int64_t y)
{
    int64_t ax = abs64(x), ay = abs64(y);
    const int64_t T = 6786; /* tan(22.5 deg) in Q14 */
    if (ay * 16384 <= ax * T) return x >= 0 ? 0u : 4u;
    if (ax * 16384 <= ay * T) return y >= 0 ? 2u : 6u;
    if (x >= 0) return y >= 0 ? 1u : 7u;
    return y >= 0 ? 3u : 5u;
}

/* Phase step from the previous block to this one, after removing the
 * carrier's own advance per block: returns its sector. */
static uint32_t phase_step_sector(uint32_t i, int32_t re, int32_t im, int32_t pre, int32_t pim)
{
    /* P = X * conj(Xprev): up to 2^41, scaled to 2^20 */
    int64_t pr = ((int64_t) re * pre + (int64_t) im * pim) >> 21;
    int64_t pi = ((int64_t) im * pre - (int64_t) re * pim) >> 21;
    /* Z = P * conj(R), R = e^{j w N} */
    int64_t rc = ZT_TRUNK_ROT_COS_Q14[i], rs = ZT_TRUNK_ROT_SIN_Q14[i];
    int64_t zr = pr * rc + pi * rs;
    int64_t zi = pi * rc - pr * rs;
    return sector8(zr, zi);
}

static void block_output(uint32_t i, int64_t q1, int64_t q2, int64_t *power, int64_t *re,
                         int64_t *im)
{
    int64_t c = ZT_TRUNK_COEF_Q14[i];
    int64_t p = q1 * q1 + q2 * q2 - c * ((q1 * q2) >> 14);
    *power = p < 0 ? 0 : p;
    *re = q1 - ((q2 * ZT_TRUNK_COS_Q14[i]) >> 14);
    *im = (q2 * ZT_TRUNK_SIN_Q14[i]) >> 14;
}

/* ===== receiver ===== */

void zt_trunk_bank_init(zt_trunk_demux_bank_t *bank)
{
    for (uint32_t i = 0; i < ZT_NUM_TRUNKS; i++) {
        bank->coef_q14[i] = ZT_TRUNK_COEF_Q14[i];
        bank->q1[i] = bank->q2[i] = 0;
        bank->line_state[i] = ZT_LINE_DEAD;
        bank->power[i] = bank->peak_power[i] = 0;
        bank->prev_re[i] = bank->prev_im[i] = 0;
        bank->prev_valid[i] = false;
        bank->offhook_blocks[i] = bank->onhook_blocks[i] = 0;
    }
    bank->sample_acc = 0;
    bank->blocks = 0;
}

static void end_block(zt_trunk_demux_bank_t *bank)
{
    for (uint32_t i = 0; i < ZT_NUM_TRUNKS; i++) {
        int64_t p, re, im;
        block_output(i, bank->q1[i], bank->q2[i], &p, &re, &im);
        bank->q1[i] = bank->q2[i] = 0;
        bank->power[i] = p;
        if (p > bank->peak_power[i]) bank->peak_power[i] = p;
        if (p < ZT_TRUNK_NOISE_FLOOR) {
            bank->line_state[i] = ZT_LINE_DEAD;
            bank->prev_valid[i] = false;
            continue;
        }
        int32_t nre, nim;
        normalise(re, im, &nre, &nim);
        zt_line_status_t st = ZT_LINE_ON_HOOK;
        if (bank->prev_valid[i] &&
            phase_step_sector(i, nre, nim, bank->prev_re[i], bank->prev_im[i]) != 0u)
            st = ZT_LINE_OFF_HOOK;
        bank->line_state[i] = st;
        if (st == ZT_LINE_OFF_HOOK)
            bank->offhook_blocks[i]++;
        else
            bank->onhook_blocks[i]++;
        bank->prev_re[i] = nre;
        bank->prev_im[i] = nim;
        bank->prev_valid[i] = true;
    }
    bank->blocks++;
    bank->sample_acc = 0;
}

void zt_trunk_bank_process_pcm(zt_trunk_demux_bank_t *bank, const int16_t *pcm_in, size_t count)
{
    for (size_t n = 0; n < count; n++) {
        int64_t xw = ((int64_t) pcm_in[n] * ZT_TRUNK_WINDOW_Q15[bank->sample_acc]) >> 15;
        for (uint32_t i = 0; i < ZT_NUM_TRUNKS; i++) {
            int64_t q0 = xw + ((bank->coef_q14[i] * bank->q1[i]) >> 14) - bank->q2[i];
            bank->q2[i] = bank->q1[i];
            bank->q1[i] = q0;
        }
        if (++bank->sample_acc == ZT_TRUNK_BLOCK) end_block(bank);
    }
}

zt_line_status_t zt_trunk_query_line(const zt_trunk_demux_bank_t *bank, zt_trunk_id_t trunk_id)
{
    if ((uint32_t) trunk_id >= ZT_NUM_TRUNKS) return ZT_LINE_DEAD;
    return bank->line_state[trunk_id];
}

/* One windowed Goertzel block on one trunk. */
static void goertzel_block(uint32_t i, const int16_t *pcm, int64_t *power, int64_t *re, int64_t *im)
{
    int64_t q1 = 0, q2 = 0, c = ZT_TRUNK_COEF_Q14[i];
    for (uint32_t n = 0; n < ZT_TRUNK_BLOCK; n++) {
        int64_t xw = ((int64_t) pcm[n] * ZT_TRUNK_WINDOW_Q15[n]) >> 15;
        int64_t q0 = xw + ((c * q1) >> 14) - q2;
        q2 = q1;
        q1 = q0;
    }
    block_output(i, q1, q2, power, re, im);
}

bool zt_trunk_read_frame(zt_trunk_demux_bank_t *bank, zt_trunk_id_t trunk_id, const int16_t *pcm_in,
                         size_t count, zt_ubh168_frame_t *dst_frame)
{
    uint32_t i = (uint32_t) trunk_id;
    if (i >= ZT_NUM_TRUNKS || !pcm_in || !dst_frame || count < ZT_TRUNK_FRAME_SAMPLES) return false;
    uint8_t oct[ZT_UBH168_FRAME_BYTES];
    for (uint32_t b = 0; b < ZT_UBH168_FRAME_BYTES; b++) oct[b] = 0;
    int32_t pre = 0, pim = 0;
    bool ok = true;
    for (uint32_t k = 0; k < ZT_TRUNK_FRAME_BLOCKS; k++) {
        int64_t p, re, im;
        int32_t nre, nim;
        goertzel_block(i, pcm_in + (size_t) k * ZT_TRUNK_BLOCK, &p, &re, &im);
        if (p < ZT_TRUNK_NOISE_FLOOR) ok = false;
        normalise(re, im, &nre, &nim);
        if (k > 0) {
            uint32_t s = phase_step_sector(i, nre, nim, pre, pim);
            uint32_t dibit;
            switch (s) {
            case 1:
                dibit = 0u;
                break; /* +45  */
            case 3:
                dibit = 1u;
                break; /* +135 */
            case 5:
                dibit = 3u;
                break; /* -135 */
            case 7:
                dibit = 2u;
                break; /* -45  */
            default:
                dibit = 0u;
                ok = false;
                break;
            }
            uint32_t bit = (k - 1u) * 2u; /* MSB first */
            oct[bit >> 3] |= (uint8_t) (dibit << (6u - (bit & 7u)));
        }
        pre = nre;
        pim = nim;
    }
    zt_wire_from_octets(oct, dst_frame);
    if (bank) bank->line_state[i] = ok ? ZT_LINE_OFF_HOOK : bank->line_state[i];
    return ok;
}

/* ===== transmitter ===== */

void zt_trunk_mux_init(zt_trunk_mux_bank_t *mux)
{
    for (uint32_t i = 0; i < ZT_NUM_TRUNKS; i++) {
        mux->phase_acc[i] = 0;
        mux->phase_step[i] = ZT_TRUNK_STEP32[i];
        mux->phase_offset[i] = 0;
        mux->amplitude[i] = ZT_TRUNK_DEFAULT_AMP;
        mux->line_active[i] = false;
    }
}

static void synth(zt_trunk_mux_bank_t *mux, uint32_t i, int16_t *pcm, size_t count)
{
    int32_t amp = mux->amplitude[i];
    uint32_t ph = mux->phase_acc[i], step = mux->phase_step[i], off = mux->phase_offset[i];
    for (size_t n = 0; n < count; n++) {
        int32_t s = (int32_t) (((int64_t) zt_trunk_sin_q15(ph + off) * amp) >> 15);
        pcm[n] = sat16(pcm[n] + s);
        ph += step;
    }
    mux->phase_acc[i] = ph;
}

void zt_trunk_mux_tone(zt_trunk_mux_bank_t *mux, zt_trunk_id_t trunk_id, int16_t *pcm_out,
                       size_t count)
{
    if ((uint32_t) trunk_id >= ZT_NUM_TRUNKS || !pcm_out) return;
    mux->line_active[trunk_id] = true;
    synth(mux, (uint32_t) trunk_id, pcm_out, count);
}

bool zt_trunk_mux_transmit_frame(zt_trunk_mux_bank_t *mux, zt_trunk_id_t trunk_id,
                                 const zt_ubh168_frame_t *frame, int16_t *pcm_out, size_t count)
{
    static const uint32_t STEP[4] = {0x20000000u,  /* 00: +45  */
                                     0x60000000u,  /* 01: +135 */
                                     0xE0000000u,  /* 10: -45  */
                                     0xA0000000u}; /* 11: -135 */
    uint32_t i = (uint32_t) trunk_id;
    if (i >= ZT_NUM_TRUNKS || !frame || !pcm_out || count < ZT_TRUNK_FRAME_SAMPLES) return false;
    uint8_t oct[ZT_UBH168_FRAME_BYTES];
    zt_wire_to_octets(frame, oct);
    mux->line_active[i] = true;
    synth(mux, i, pcm_out, ZT_TRUNK_BLOCK); /* reference block */
    for (uint32_t k = 0; k < ZT_TRUNK_FRAME_SYMBOLS; k++) {
        uint32_t bit = k * 2u;
        uint32_t dibit = (uint32_t) (oct[bit >> 3] >> (6u - (bit & 7u))) & 3u;
        mux->phase_offset[i] += STEP[dibit];
        synth(mux, i, pcm_out + (size_t) (k + 1u) * ZT_TRUNK_BLOCK, ZT_TRUNK_BLOCK);
    }
    return true;
}

/* ===== levels ===== */

int64_t zt_trunk_noise_floor(void)
{
    return ZT_TRUNK_NOISE_FLOOR;
}

/* log2(x) in Q8 for x > 0. */
static int32_t log2_q8(uint64_t x)
{
    int32_t ip = 63;
    while (!(x >> ip)) ip--;
    /* mantissa in Q30, [1, 2) */
    uint64_t m = ip >= 30 ? x >> (ip - 30) : x << (30 - ip);
    int32_t frac = 0;
    for (int b = 7; b >= 0; b--) {
        m = (m * m) >> 30; /* m < 2^31: the square fits */
        if (m >= (1ull << 31)) {
            m >>= 1;
            frac |= 1 << b;
        }
    }
    return ip * 256 + frac;
}

int32_t zt_trunk_power_db10(int64_t power)
{
    if (power <= 0) return -1000;
    int32_t d = log2_q8((uint64_t) power) - log2_q8((uint64_t) ZT_TRUNK_P_FULLSCALE);
    /* tenths of a dB = 100 * log10(2) * log2 = d * 30.103 / 256 */
    return (d * 30103) / 256000;
}
