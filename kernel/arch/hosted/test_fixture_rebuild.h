/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_fixture_rebuild.h — rebuild a tiny test model file from
 * kernel/src/tensor/test_model_fixture.h (shared by the hosted tests). Include
 * after test_model_fixture.h, <stdlib.h> and <string.h>. */
#ifndef TEST_FIXTURE_REBUILD_H
#define TEST_FIXTURE_REBUILD_H

/* the fixture generator, as in kernel/src/tensor/test_zt_model.c */
static uint64_t mix(uint64_t seed, uint64_t i)
{
    uint64_t z = seed + (i + 1) * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static uint8_t *rebuild(const mf_variant_t *v)
{
    uint8_t *b = calloc(1, v->file_len);
    if (!b) return NULL;
    memcpy(b, v->hdr, v->hdr_len);
    for (uint32_t t = 0; t < v->n_gen; t++) {
        const mf_gen_t *g = &v->gen[t];
        uint8_t *p = b + v->hdr_len + g->off;
        if (g->kind == 1) {
            for (uint64_t k = 0; k < g->n / 32; k++, p += 34) {
                uint16_t d = (uint16_t) ((g->param << 10) | (mix(g->seed, k * 33) & 0x3FF));
                p[0] = (uint8_t) d;
                p[1] = (uint8_t) (d >> 8);
                for (int i = 0; i < 32; i++)
                    p[2 + i] =
                        (uint8_t) (int8_t) ((int) (mix(g->seed, k * 33 + 1 + i) % 255) - 127);
            }
        } else if (g->kind == 2) {
            for (uint64_t i = 0; i < g->n; i++) {
                uint64_t z = mix(g->seed, i);
                uint16_t h = (uint16_t) ((((z >> 20) & 1) << 15) |
                                         ((g->param - ((z >> 16) & 1)) << 10) | (z & 0x3FF));
                p[2 * i] = (uint8_t) h;
                p[2 * i + 1] = (uint8_t) (h >> 8);
            }
        } else {
            for (uint64_t i = 0; i < g->n; i++) {
                uint64_t z = mix(g->seed, i);
                float f = g->kind == 3   ? 1.0f + (float) ((int) (z % 1025) - 512) / 2048.0f
                          : g->kind == 4 ? (float) ((int) (z % 2049) - 1024) / 4096.0f
                                         : 1.0f + (float) i / 8.0f;
                memcpy(p + 4 * i, &f, 4);
            }
        }
    }
    return b;
}

#endif /* TEST_FIXTURE_REBUILD_H */
