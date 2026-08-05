/* mlkem_sample.c — ML-KEM-768 SampleNTT (FIPS 203 Algorithm 7)
 * See mlkem_sample.h for design notes.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "mlkem_sample.h"
#include "keccak.h"

void sample_ntt(const uint8_t rho[32], uint8_t i, uint8_t j, poly_t *out) {
    uint8_t seed[34];
    for (int b = 0; b < 32; b++) seed[b] = rho[b];
    seed[32] = i;
    seed[33] = j;

    shake128_ctx_t ctx;
    shake128_init(&ctx);
    shake128_absorb(&ctx, seed, 34);

    int count = 0;
    uint8_t c[3];
    while (count < MLKEM_N) {
        shake128_squeeze(&ctx, c, 3);
        uint16_t d1 = (uint16_t)(c[0] | ((uint16_t)(c[1] & 0x0F) << 8));
        uint16_t d2 = (uint16_t)((c[1] >> 4) | ((uint16_t)c[2] << 4));

        if (d1 < MLKEM_Q) {
            out->coeffs[count++] = (int16_t)d1;
        }
        if (d2 < MLKEM_Q && count < MLKEM_N) {
            out->coeffs[count++] = (int16_t)d2;
        }
    }
}
