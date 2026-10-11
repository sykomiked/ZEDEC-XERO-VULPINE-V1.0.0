/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mkfixture.c — write the tiny test model every SDK's tests run:
 *   <dir>/tiny-qwen2.gguf      a 2-layer random qwen2 model (vocab 1200),
 *                              rebuilt bit for bit from kernel/src/tensor/
 *                              test_model_fixture.h (FNV-1a checked)
 *   <dir>/tiny-tokenizer.gguf  the matching BPE tokenizer
 *   <dir>/prompt.txt           the chat prompt whose greedy continuation
 *                              is pinned in ../conformance/vectors.json
 * The weights are random: the model's output is meaningless text, which is
 * the point: it tests determinism, not language. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_model_fixture.h"
#include "test_tok_fixture.h"

static uint64_t mix(uint64_t seed, uint64_t i)
{
    uint64_t z = seed + (i + 1) * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static void put_f32(uint8_t *p, float v)
{
    memcpy(p, &v, 4);
}

/* Same recipe as test_zt_model.c rebuild(). */
static uint8_t *rebuild(const mf_variant_t *v)
{
    uint8_t *b = calloc(1, v->file_len);
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
                    p[2 + i] = (uint8_t) (int8_t) ((int) (mix(g->seed, k * 33 + 1 + i) % 255) - 127);
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
                put_f32(p + 4 * i, f);
            }
        }
    }
    return b;
}

static uint64_t fnv(const uint8_t *b, uint64_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint64_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}

static int write_file(const char *dir, const char *name, const void *p, size_t n)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(p, 1, n, f) != n || fclose(f) != 0) {
        fprintf(stderr, "mkfixture: cannot write %s\n", path);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: mkfixture <dir>\n");
        return 2;
    }
    const mf_variant_t *v = &MF_VARIANTS[0];
    uint8_t *b = rebuild(v);
    if (fnv(b, v->file_len) != v->fnv) {
        fprintf(stderr, "mkfixture: rebuilt model does not match its FNV-1a hash\n");
        return 1;
    }
    int rc = write_file(argv[1], "tiny-qwen2.gguf", b, v->file_len);
    rc |= write_file(argv[1], "tiny-tokenizer.gguf", TOK_GGUF, TOK_GGUF_LEN);
    rc |= write_file(argv[1], "prompt.txt", TOK_TEXTS[12].s, TOK_TEXTS[12].len);
    free(b);
    return rc;
}
