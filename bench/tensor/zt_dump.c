/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zt_gguf.h"
#include "zt_model.h"
#include "zt_simd.h"

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9;
}

static uint8_t *read_file(const char *path, uint64_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = malloc((size_t) n);
    if (buf && fread(buf, 1, (size_t) n, f) != (size_t) n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *size = (uint64_t) n;
    return buf;
}

static void *alloc64(uint64_t bytes)
{
    void *p = NULL;
    if (posix_memalign(&p, 64, bytes ? (size_t) bytes : 64) != 0) return NULL;
    return p;
}

/* zt_dump MODEL.gguf IDS.bin LOGITS.f32 [--kern NAME] [--threads T] [--batch B]
 * Runs the int32 token ids in IDS.bin through the forward pass as one
 * sequence and writes every position's logits as little-endian float32
 * (Q16 / 65536) to LOGITS.f32, so they can be compared with another runtime
 * fed the same ids. Prints zt.n=<tokens> zt.tps=<tokens/s>. Hosted tool. */
int main(int argc, char **argv)
{
    const char *kern_name = NULL;
    uint32_t n_threads = 1, n_batch = 32;
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s MODEL.gguf IDS.bin LOGITS.f32 [--kern NAME] [--threads T] [--batch B]\n",
                argv[0]);
        return 2;
    }
    for (int i = 4; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--kern"))
            kern_name = argv[i + 1];
        else if (!strcmp(argv[i], "--threads"))
            n_threads = (uint32_t) strtoul(argv[i + 1], NULL, 10);
        else if (!strcmp(argv[i], "--batch"))
            n_batch = (uint32_t) strtoul(argv[i + 1], NULL, 10);
        else
            return 2;
    }
    uint64_t size = 0, isz = 0;
    uint8_t *file = read_file(argv[1], &size);
    int32_t *ids = (int32_t *) read_file(argv[2], &isz);
    if (!file || !ids || isz % 4 || n_batch == 0) return 3;
    uint32_t n = (uint32_t) (isz / 4);
    zt_gguf_t g;
    if (zt_gguf_open(&g, file, size) != 0) return 4;
    zt_model_err_t err;
    memset(&err, 0, sizeof err);
    uint64_t mbytes = zt_model_arena_bytes(&g);
    void *marena = mbytes ? alloc64(mbytes) : NULL;
    zt_model_t m;
    if (!marena || zt_model_load(&m, &g, marena, mbytes, &err) != 0) {
        fprintf(stderr, "zt_dump: model load failed: %s %s\n", err.what ? err.what : "", err.name);
        return 4;
    }
    const zt_kern_t *kb = kern_name ? zt_simd_find(kern_name) : zt_simd_best();
    if (!kb) return 2;
    zt_pool_t *pool = n_threads > 1 ? zt_pool_new(n_threads) : NULL;
    zt_kern_t kern;
    zt_kern_threaded(&kern, kb, pool);
    m.kern = &kern;
    uint64_t sbytes = zt_model_state_bytes(&m, n, n_batch, ZT_KV_Q16);
    void *smem = alloc64(sbytes);
    zt_model_state_t st;
    if (!smem || zt_model_state_init(&st, &m, n, n_batch, ZT_KV_Q16, smem, sbytes) != 0) return 5;
    uint32_t nv = m.cfg.n_vocab;
    zt_fx *logits = malloc((size_t) n * nv * sizeof(zt_fx));
    float *row = malloc((size_t) nv * sizeof(float));
    if (!logits || !row) return 5;
    double t0 = now_s();
    int32_t r = zt_model_eval(&m, &st, ids, n, logits, ZT_EVAL_ALL_LOGITS);
    double t1 = now_s();
    if (r != 0) {
        fprintf(stderr, "zt_dump: eval failed (%d)\n", r);
        return 5;
    }
    FILE *out = fopen(argv[3], "wb");
    if (!out) return 3;
    for (uint32_t p = 0; p < n; p++) {
        for (uint32_t j = 0; j < nv; j++) row[j] = (float) logits[(uint64_t) p * nv + j] / 65536.0f;
        if (fwrite(row, sizeof(float), nv, out) != nv) return 3;
    }
    fclose(out);
    printf("zt.n=%u\nzt.tps=%.3f\n", n, n / (t1 - t0));
    zt_pool_free(pool);
    return 0;
}
