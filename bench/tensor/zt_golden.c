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
#include "zt.h"
#include "zt_lattice.h"
#include <math.h>

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

/* zt_golden — the golden-ratio parts of the tensor engine (zt.h T3, T7-T15,
 * zt_lattice.h T17) applied to a REAL model's weights, so their effect on the
 * output can be measured against the plain engine and another runtime.
 *
 *   zt_golden MODEL.gguf IDS.bin MODE [--out LOGITS.f32] [--gen N --eos a,b]
 *             [--threads T] [--batch B]
 *
 * The model is loaded as usual, then MODE rewrites every converted weight
 * matrix (zt_q8_t blocks in the arena; the token embedding rows read from the
 * file are left alone) before anything runs:
 *   none      nothing (the baseline engine)
 *   requant   each block re-quantised to 8 bits with an exact amax/127 scale
 *             (the control for phi: re-rounding alone)
 *   phi       T3 at full precision: each block's scale snapped UP to the next
 *             power of the golden ratio, then the block re-quantised
 *   phi16     T3 exactly as the engine ships it: zt_quantize(golden = true)
 *             on the Q16 values (its phi ladder is held in Q16)
 *   e8 / e8down    T17: every 8 weights rounded to the E8 codebook (1.875
 *             bits a weight plus a scale per 32), all matrices / ffn_down only
 *   int2 / int2down   the control for e8: a uniform 4-level scalar quantiser
 *             (2 bits a weight, scale per 32)
 *   holo10 / holo9    T7 + T15: each row's 8-bit codes laid out on the
 *             smallest golden coil that holds it, holographically encoded
 *             (each place minus its parent), and decoded with all 10 shells
 *             (must be lossless) or with 9 (the innermost shell dropped)
 * It prints golden.* lines: what was changed, the entropy of the codes before
 * and after holographic coding (bits a weight, an estimate of how far an
 * entropy coder could compress them), and timings. Then it either writes every
 * position's logits (--out, float32) like zt_dump, or greedy-decodes N tokens
 * after the prompt (--gen) and prints golden.ids. Hosted experiment tool. */

typedef struct {
    double h_q, h_res; /* entropy sums (bits) */
    uint64_t n_w, n_blocks, n_mats, holo_mismatch;
    int max_shift, min_shift;
} gstats_t;

static double blk_val(const zt_q8_t *b, uint32_t i)
{
    return (double) b->q[i] * (double) b->scale / ldexp(1.0, 16 + b->shift);
}

/* Store 32 doubles as one block: scale = s / 2^shift with s in [2^24, 2^25). */
static void store_block(const double *v, double scale_real, zt_q8_t *o)
{
    o->phi_k = 0;
    if (scale_real <= 0) {
        o->scale = 0;
        o->shift = 0;
        memset(o->q, 0, sizeof o->q);
        return;
    }
    int sh = 0;
    double s = scale_real * 65536.0;
    while (s < 16777216.0 && sh < 60) s *= 2, sh++;
    while (s >= 33554432.0 && sh > 0) s /= 2, sh--;
    int32_t si = (int32_t) llround(s);
    o->scale = si;
    o->shift = (uint8_t) sh;
    double real = (double) si / ldexp(1.0, 16 + sh);
    for (int i = 0; i < 32; i++) {
        long q = lround(v[i] / real);
        o->q[i] = (int8_t) (q > 127 ? 127 : q < -127 ? -127 : q);
    }
}

static double amax32(const double *v)
{
    double a = 0;
    for (int i = 0; i < 32; i++) a = fabs(v[i]) > a ? fabs(v[i]) : a;
    return a;
}

static const double PHI = 1.6180339887498949;

static void tf_block(zt_q8_t *b, const char *mode, const int8_t *e8tab)
{
    double v[32];
    for (int i = 0; i < 32; i++) v[i] = blk_val(b, (uint32_t) i);
    double a = amax32(v);
    if (!strcmp(mode, "requant")) {
        store_block(v, a / 127.0, b);
    } else if (!strcmp(mode, "phi")) {
        double s = a / 127.0;
        if (s > 0) s = pow(PHI, ceil(log(s) / log(PHI) - 1e-12));
        store_block(v, s, b);
    } else if (!strcmp(mode, "phi16")) {
        zt_fx x[32];
        zt_dequantize(b, 1, x);
        zt_quantize(x, 32, b, true);
    } else if (!strncmp(mode, "e8", 2)) {
        zt_fx x[32], y[32];
        zt_dequantize(b, 1, x);
        zt_fx sc = zt_e8_scale(x, 32);
        for (int g = 0; g < 4; g++) {
            int8_t v2[8];
            uint16_t idx = zt_e8_quantize(e8tab, x + 8 * g, sc);
            zt_e8_decode(e8tab, idx, v2);
            zt_e8_dequantize(v2, sc, y + 8 * g);
        }
        for (int i = 0; i < 32; i++) v[i] = y[i] / 65536.0;
        store_block(v, amax32(v) / 127.0, b);
    } else if (!strncmp(mode, "int2", 4)) {
        /* levels (+-0.5, +-1.5) * d; d chosen by a small search for least error */
        double best = 1e300, bd = 0;
        for (int k = 1; k <= 64; k++) {
            double d = a / 1.5 * k / 64.0, e = 0;
            for (int i = 0; i < 32; i++) {
                double l = floor(v[i] / d) + 0.5;
                l = l > 1.5 ? 1.5 : l < -1.5 ? -1.5 : l;
                e += (v[i] - l * d) * (v[i] - l * d);
            }
            if (e < best) best = e, bd = d;
        }
        for (int i = 0; i < 32; i++) {
            if (bd == 0) break;
            double l = floor(v[i] / bd) + 0.5;
            v[i] = (l > 1.5 ? 1.5 : l < -1.5 ? -1.5 : l) * bd;
        }
        store_block(v, amax32(v) / 127.0, b);
    }
}

static double entropy_bits(const uint64_t *h, uint32_t nb, uint64_t n)
{
    double e = 0;
    for (uint32_t i = 0; i < nb; i++)
        if (h[i]) e -= (double) h[i] * log2((double) h[i] / (double) n);
    return e;
}

/* T7 + T15 on one row of codes. */
static void tf_holo_row(zt_q8_t *row, uint32_t nb, uint32_t shells, gstats_t *st)
{
    uint32_t cols = nb * 32, base;
    zt_coil_t c;
    for (base = 5; base <= 36; base++)
        if (zt_coil_init(&c, base, ZT_WIND_GOLDEN) && c.total >= cols) break;
    int64_t *val = calloc(c.total, sizeof *val), *res = calloc(c.total, sizeof *res),
            *dec = calloc(c.total, sizeof *dec);
    uint32_t *pos = malloc(cols * sizeof *pos);
    for (uint32_t i = 0; i < cols; i++) {
        zt_place_t p = zt_coil_place(&c, i);
        pos[i] = c.offset[p.shell] + p.slot;
        val[pos[i]] = row[i / 32].q[i % 32];
    }
    zt_holo_encode(&c, val, res);
    zt_holo_decode(&c, res, shells, dec);
    uint64_t hq[256] = {0}, hr[1026] = {0};
    for (uint32_t i = 0; i < cols; i++) {
        hq[val[pos[i]] + 128]++;
        int64_t r = res[pos[i]];
        hr[r < -512 ? 0 : r > 512 ? 1025 : r + 513]++;
        int64_t d = dec[pos[i]];
        if (d != val[pos[i]]) st->holo_mismatch++;
        row[i / 32].q[i % 32] = (int8_t) (d > 127 ? 127 : d < -127 ? -127 : d);
    }
    st->h_q += entropy_bits(hq, 256, cols);
    st->h_res += entropy_bits(hr, 1026, cols);
    free(val), free(res), free(dec), free(pos);
}

static void tf_mat(const zt_mat_t *mm, const char *mode, const int8_t *e8tab, gstats_t *st)
{
    if (!mm->q8) return;
    zt_q8_t *w = (zt_q8_t *) mm->q8; /* the arena is ours */
    uint32_t nb = mm->cols / 32;
    st->n_mats++;
    for (uint64_t r = 0; r < mm->rows; r++) {
        for (uint32_t b = 0; b < nb; b++) {
            zt_q8_t *blk = &w[r * nb + b];
            if (blk->shift > st->max_shift) st->max_shift = blk->shift;
            if (blk->shift < st->min_shift) st->min_shift = blk->shift;
        }
        if (!strncmp(mode, "holo", 4))
            tf_holo_row(&w[r * nb], nb, (uint32_t) atoi(mode + 4), st);
        else
            for (uint32_t b = 0; b < nb; b++) tf_block(&w[r * nb + b], mode, e8tab);
        st->n_blocks += nb;
        st->n_w += mm->cols;
    }
}

int main(int argc, char **argv)
{
    const char *out_path = NULL, *eos_list = "";
    uint32_t n_threads = 1, n_batch = 32, n_gen = 0;
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s MODEL.gguf IDS.bin MODE [--out F] [--gen N --eos a,b] "
                "[--threads T] [--batch B]\n",
                argv[0]);
        return 2;
    }
    const char *mode = argv[3];
    for (int i = 4; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--out"))
            out_path = argv[i + 1];
        else if (!strcmp(argv[i], "--gen"))
            n_gen = (uint32_t) strtoul(argv[i + 1], NULL, 10);
        else if (!strcmp(argv[i], "--eos"))
            eos_list = argv[i + 1];
        else if (!strcmp(argv[i], "--threads"))
            n_threads = (uint32_t) strtoul(argv[i + 1], NULL, 10);
        else if (!strcmp(argv[i], "--batch"))
            n_batch = (uint32_t) strtoul(argv[i + 1], NULL, 10);
        else
            return 2;
    }
    uint64_t size = 0, isz = 0;
    uint8_t *file = read_file(argv[1], &size);
    int32_t *ids0 = (int32_t *) read_file(argv[2], &isz);
    if (!file || !ids0 || isz % 4 || n_batch == 0) return 3;
    uint32_t n = (uint32_t) (isz / 4);
    zt_gguf_t g;
    if (zt_gguf_open(&g, file, size) != 0) return 4;
    zt_model_err_t err;
    memset(&err, 0, sizeof err);
    uint64_t mbytes = zt_model_arena_bytes(&g);
    void *marena = mbytes ? alloc64(mbytes) : NULL;
    zt_model_t m;
    if (!marena || zt_model_load(&m, &g, marena, mbytes, &err) != 0) {
        fprintf(stderr, "zt_golden: model load failed: %s %s\n", err.what ? err.what : "",
                err.name);
        return 4;
    }
    int8_t *e8tab = NULL;
    if (!strncmp(mode, "e8", 2)) {
        e8tab = malloc(26641 * 8);
        if (!e8tab || zt_e8_codebook_build(e8tab) != 26641) return 5;
    }
    gstats_t st = {0};
    st.min_shift = 255;
    double t0 = now_s();
    if (strcmp(mode, "none") != 0) {
        bool down_only = strstr(mode, "down") != NULL;
        for (uint32_t l = 0; l < m.cfg.n_layer; l++) {
            zt_layer_t *L = &m.layers[l];
            if (down_only) {
                tf_mat(&L->wd, mode, e8tab, &st);
                continue;
            }
            const zt_mat_t *all[] = {&L->wq, &L->wk, &L->wv, &L->wo, &L->wg, &L->wu, &L->wd};
            for (int k = 0; k < 7; k++) tf_mat(all[k], mode, e8tab, &st);
        }
        if (!down_only) tf_mat(&m.out, mode, e8tab, &st);
    }
    double t1 = now_s();
    printf("golden.mode=%s\ngolden.mats=%llu\ngolden.weights=%llu\ngolden.transform_s=%.2f\n", mode,
           (unsigned long long) st.n_mats, (unsigned long long) st.n_w, t1 - t0);
    printf("golden.out_matrix=%s\n", m.out.q8 ? "converted (transformed)" : "raw Q8_0 (untouched)");
    if (st.n_w) printf("golden.shift_range=%d..%d\n", st.min_shift, st.max_shift);
    if (!strncmp(mode, "holo", 4) && st.n_w)
        printf("golden.entropy_codes_bits=%.4f\ngolden.entropy_residuals_bits=%.4f\n"
               "golden.holo_changed_weights=%llu\n",
               st.h_q / st.n_w, st.h_res / st.n_w, (unsigned long long) st.holo_mismatch);

    zt_pool_t *pool = n_threads > 1 ? zt_pool_new(n_threads) : NULL;
    zt_kern_t kern;
    zt_kern_threaded(&kern, zt_simd_best(), pool);
    m.kern = &kern;
    uint32_t n_ctx = n + n_gen + 1;
    uint64_t sbytes = zt_model_state_bytes(&m, n_ctx, n_batch, ZT_KV_Q16);
    void *smem = alloc64(sbytes);
    zt_model_state_t s;
    if (!smem || zt_model_state_init(&s, &m, n_ctx, n_batch, ZT_KV_Q16, smem, sbytes) != 0)
        return 5;
    uint32_t nv = m.cfg.n_vocab;
    if (n_gen) {
        zt_fx *lg = malloc((size_t) nv * sizeof *lg);
        if (!lg || zt_model_eval(&m, &s, ids0, n, lg, 0) != 0) return 5;
        printf("golden.ids=");
        for (uint32_t i = 0; i < n_gen; i++) {
            int32_t t = zt_argmax(lg, nv);
            bool stop = false;
            for (const char *p = eos_list; *p;) {
                if (strtol(p, NULL, 10) == t) stop = true;
                p = strchr(p, ',');
                if (!p) break;
                p++;
            }
            if (stop) break;
            printf(i ? ",%d" : "%d", t);
            if (zt_model_decode(&m, &s, t, lg) != 0) return 5;
        }
        printf("\n");
    } else if (out_path) {
        zt_fx *lg = malloc((size_t) n * nv * sizeof *lg);
        float *row = malloc((size_t) nv * sizeof *row);
        if (!lg || !row || zt_model_eval(&m, &s, ids0, n, lg, ZT_EVAL_ALL_LOGITS) != 0) return 5;
        FILE *o = fopen(out_path, "wb");
        if (!o) return 3;
        for (uint32_t p = 0; p < n; p++) {
            for (uint32_t j = 0; j < nv; j++) row[j] = (float) lg[(uint64_t) p * nv + j] / 65536.0f;
            fwrite(row, sizeof *row, nv, o);
        }
        fclose(o);
    }
    printf("golden.run_s=%.2f\n", now_s() - t1);
    zt_pool_free(pool);
    return 0;
}
