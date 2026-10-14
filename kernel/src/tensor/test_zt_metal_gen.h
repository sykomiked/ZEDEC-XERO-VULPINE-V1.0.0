/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_metal_gen.h — random weight rows and activations for the Metal
 * tests (test_zt_metal_math.c, test_zt_metal.c): every f16 class, all-ones
 * and all-zero quants, extreme activations, or model-like values (model = 1:
 * scales 2^-23..2^-7, no saturation). Test-only; include once. */
#ifndef TEST_ZT_METAL_GEN_H
#define TEST_ZT_METAL_GEN_H

static uint64_t rng_state = 0x6D6574616C2026ull;
static uint64_t rnd(void)
{
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static uint32_t below(uint32_t n)
{
    return (uint32_t) (rnd() % n);
}

static uint32_t model; /* model-like scales (2^-23..2^-7) and shifts: no saturation */

static uint16_t f16val(void)
{
    if (model)
        return (uint16_t) ((rnd() & 0x8000) | ((uint32_t) (2 + below(7)) << 10) | below(0x400));
    switch (below(8)) {
    case 0:
        return (uint16_t) rnd();
    case 1:
        return (uint16_t) ((rnd() & 0x8000) | below(0x400)); /* zero / subnormal */
    case 2:
        return (uint16_t) ((rnd() & 0x8000) | 0x7C00 | below(0x400)); /* inf / NaN */
    case 3:
        return (uint16_t) ((rnd() & 0x8000) | 0x7BFF); /* largest finite */
    default:
        return (uint16_t) ((rnd() & 0x8000) | ((uint32_t) (2 + below(12)) << 10) | below(0x400));
    }
}

static int16_t xval(uint32_t mode)
{
    switch (mode) {
    case 0:
        return (int16_t) (rnd() & 0xFFFF);
    case 1:
        return (int16_t) -32768;
    case 2:
        return (int16_t) (rnd() & 1 ? 32767 : -32767);
    default:
        return (int16_t) ((int) below(65535) - 32767);
    }
}

static uint8_t qbyte(uint32_t mode)
{
    return mode == 1 ? 0xFF : mode == 2 ? 0x00 : (uint8_t) rnd();
}

/* One row of nkb K-quant blocks (Q4_K or Q6_K) or nkb * 8 Q8_0 blocks. */
static void wfill(uint32_t qt, uint8_t *w, uint32_t nkb, uint32_t qmode)
{
    for (uint32_t k = 0; k < nkb; k++) {
        if (qt == ZT_GGML_Q4_K) {
            uint8_t *b = w + (size_t) k * ZT_Q4K_BYTES;
            uint16_t d = f16val(), dm = f16val();
            b[0] = (uint8_t) d, b[1] = (uint8_t) (d >> 8), b[2] = (uint8_t) dm,
            b[3] = (uint8_t) (dm >> 8);
            for (uint32_t i = 4; i < ZT_Q4K_BYTES; i++) b[i] = qbyte(i < 16 ? qmode % 2 : qmode);
        } else if (qt == ZT_GGML_Q6_K) {
            uint8_t *b = w + (size_t) k * ZT_Q6K_BYTES;
            for (uint32_t i = 0; i < 192; i++) b[i] = qbyte(qmode);
            for (uint32_t i = 192; i < 208; i++)
                b[i] = qmode == 1 ? (uint8_t) (rnd() & 1 ? 0x7F : 0x80) : (uint8_t) rnd();
            uint16_t d = f16val();
            b[208] = (uint8_t) d, b[209] = (uint8_t) (d >> 8);
        } else {
            for (uint32_t s = 0; s < 8; s++) {
                uint8_t *b = w + ((size_t) k * 8 + s) * 34;
                uint16_t d = f16val();
                b[0] = (uint8_t) d, b[1] = (uint8_t) (d >> 8);
                for (uint32_t i = 2; i < 34; i++)
                    b[i] = qmode == 1 ? (uint8_t) (rnd() & 1 ? 0x7F : 0x80) : qbyte(qmode);
            }
        }
    }
}

/* nb activation blocks of 32 (xmode as xval, or model-like), their exact
 * sums, and shifts: below(6) for model rows, else mostly act_quant's 0..17 */
static void afill(int16_t *a, uint8_t *ash, int32_t *asum, uint32_t nb, uint32_t xmode)
{
    for (uint32_t b = 0; b < nb; b++) {
        int32_t s = 0;
        for (uint32_t i = 0; i < 32; i++)
            s += a[b * 32 + i] = model ? (int16_t) ((int) below(65535) - 32767) : xval(xmode);
        asum[b] = s;
        ash[b] = (uint8_t) (model ? below(6) : below(4) ? below(18) : below(256));
    }
}

#endif /* TEST_ZT_METAL_GEN_H */
