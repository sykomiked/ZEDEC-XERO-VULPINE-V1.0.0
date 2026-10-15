/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxva_backend.h — the checks every GPU backend of the analyser's heavy
 * stage must pass, word for word against the C reference (zxva_stft_cpu).
 * Included by test_zxva_cuda.c and test_zxva_vk.c (one translation unit
 * each; everything here is static).
 *
 *   1. random plans: window 16..4096, FFT 2^L >= n (zero padding 1x..8x),
 *      hops given as num/den (integer and fractional, rounded half to even),
 *      centres before the start, inside and past the end, 64-bit frame
 *      numbers, signals from silence to full-scale Q23 noise, thresholds
 *      that mask nothing and everything, batches of 1..300 frames: re, im,
 *      mag, mask and top identical;
 *   2. whole analyses of the fixtures (tools/zxva/gen_analyse_fixtures.py)
 *      with the backend: the same ZXVA bytes as with the CPU. */
#ifndef TEST_ZXVA_BACKEND_H
#define TEST_ZXVA_BACKEND_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxva_analyse.h"

#include "test/zxva_analyse_fixtures.inc"

static uint64_t tb_rng = 0x7a78766162656eull;
static uint32_t tb_rnd(void)
{
    tb_rng ^= tb_rng << 13;
    tb_rng ^= tb_rng >> 7;
    tb_rng ^= tb_rng << 17;
    return (uint32_t) tb_rng;
}

/* 1: random plans; returns the number of mismatching cases */
static int tb_plans(zxva_backend_t *be, int cases, int *words_out)
{
    int bad = 0;
    long long words = 0;
    for (int it = 0; it < cases; it++) {
        uint32_t L = 4 + tb_rnd() % 9; /* N 16 .. 4096 */
        uint32_t N = 1u << L, pad = 1u << (tb_rnd() % 4), n = N / pad;
        if (n < 4) n = 4;
        if (tb_rnd() % 3 == 0 && n > 8) n -= 2 * (tb_rnd() % (n / 4)); /* not a power of two */
        uint32_t H = N / 2 + 1, mw = zxva_mask_words(N);
        uint32_t nf = it % 7 == 0 ? 1 + tb_rnd() % 300 : 1 + tb_rnd() % 12;
        uint64_t len = 1 + tb_rnd() % (4 * N + 50);
        int32_t *x = malloc((size_t) len * 4), *win = malloc((size_t) n * 4),
                *tw = malloc((size_t) N * 4);
        uint32_t kind = tb_rnd() % 5;
        for (uint64_t i = 0; i < len; i++) {
            int32_t v;
            switch (kind) {
            case 0:
                v = 0;
                break;
            case 1:
                v = (int32_t) (tb_rnd() % (1u << 24)) - (1 << 23);
                break;
            case 2:
                v = (i & 1) ? (1 << 23) - 1 : -(1 << 23);
                break;
            case 3:
                v = (int32_t) (tb_rnd() % 2001) - 1000;
                break;
            default:
                v = zxva_sin_q30((uint64_t) i * (0x0123456789ull + tb_rnd() % 7)) >> 7;
                break;
            }
            x[i] = v;
        }
        if (tb_rnd() & 1)
            zxva_tables_bh(win, n);
        else
            zxva_tables_hann(win, n);
        zxva_tables_tw(tw, N);
        zxva_stft_plan_t p;
        memset(&p, 0, sizeof p);
        p.n = n;
        p.N = N;
        p.L = L;
        p.num = 1 + tb_rnd() % (n / 2 + 3);
        p.den = tb_rnd() % 3 == 0 ? 1 + tb_rnd() % 7 : 1;
        p.c0 = (int64_t) (tb_rnd() % (2 * N)) - (int64_t) N;
        p.win = win;
        p.tw = tw;
        p.x = x;
        p.len = len;
        p.floor_mag = tb_rnd() % 4 == 0 ? 0xFFFFFFFFu : tb_rnd() % 1000;
        p.rel = 1 + tb_rnd() % 100000;
        uint64_t k0 = tb_rnd() % 5 == 0 ? (uint64_t) 0 : tb_rnd() % (len / (p.num ? p.num : 1) + 3);
        if (it % 11 ==
            5) { /* a 64-bit frame number with a fractional hop that lands back in range */
            p.num = 1;
            p.den = 1u << 31;
            k0 = ((uint64_t) 1 << 33) + tb_rnd() % 7;
            p.c0 = (int64_t) (tb_rnd() % (uint32_t) len);
        }
        size_t fw = (size_t) nf * H, mwn = (size_t) nf * mw;
        int32_t *re0 = malloc(fw * 4), *im0 = malloc(fw * 4), *re1 = malloc(fw * 4),
                *im1 = malloc(fw * 4);
        uint32_t *m0 = malloc(fw * 4), *m1 = malloc(fw * 4), *k0m = malloc(mwn * 4),
                 *k1m = malloc(mwn * 4);
        uint32_t *t0 = malloc((size_t) nf * 4), *t1 = malloc((size_t) nf * 4);
        int32_t *scr = malloc((size_t) 2 * N * 4);
        zxva_backend_t cpu;
        zxva_backend_cpu(&cpu);
        cpu.scratch = scr;
        cpu.scratch_words = 2 * N;
        memset(re1, 0x5a, fw * 4);
        memset(k1m, 0x5a, mwn * 4);
        int r0 = cpu.stft(&cpu, &p, k0, nf, re0, im0, m0, k0m, t0);
        int r1 = be->stft(be, &p, k0, nf, re1, im1, m1, k1m, t1);
        int same = r0 == 0 && r1 == 0 && !memcmp(re0, re1, fw * 4) && !memcmp(im0, im1, fw * 4) &&
                   !memcmp(m0, m1, fw * 4) && !memcmp(k0m, k1m, mwn * 4) &&
                   !memcmp(t0, t1, (size_t) nf * 4);
        words += (long long) (fw * 3 + mwn + nf);
        if (!same) {
            if (bad < 5)
                printf("    case %d: n %u N %u num %u den %u c0 %lld k0 %llu nf %u len %llu rc "
                       "%d/%d differs\n",
                       it, n, N, p.num, p.den, (long long) p.c0, (unsigned long long) k0, nf,
                       (unsigned long long) len, r0, r1);
            bad++;
        }
        free(re0), free(im0), free(re1), free(im1), free(m0), free(m1), free(k0m), free(k1m),
            free(t0), free(t1);
        free(scr), free(x), free(win), free(tw);
    }
    if (words_out) *words_out = (int) (words > 2000000000 ? 2000000000 : words);
    return bad;
}

/* 2: whole analyses; returns mismatches */
static int tb_analyses(zxva_backend_t *be, int *files)
{
    int bad = 0;
    *files = 0;
    for (size_t i = 0; i < sizeof AN_FX / sizeof AN_FX[0]; i++) {
        const an_fx_t *fx = &AN_FX[i];
        zxva_pcm_t p = {fx->pcm, fx->frames, fx->nch, AN_RATE, fx->bits};
        zxva_an_cfg_t c0, c1;
        memset(&c0, 0, sizeof c0);
        c0.residual = true;
        c1 = c0;
        c1.backend = be;
        uint64_t w0 = zxva_analyse_work_size(&p, &c0), w1 = zxva_analyse_work_size(&p, &c1);
        uint64_t ob = zxva_analyse_out_bound(&p, &c0);
        void *a0 = malloc((size_t) w0), *a1 = malloc((size_t) w1);
        uint8_t *o0 = malloc((size_t) ob), *o1 = malloc((size_t) ob);
        zxva_an_report_t r0, r1;
        int64_t n0 = zxva_analyse(&p, &c0, a0, w0, o0, ob, &r0);
        int64_t n1 = zxva_analyse(&p, &c1, a1, w1, o1, ob, &r1);
        if (n0 <= 0 || n0 != n1 || memcmp(o0, o1, (size_t) n0) != 0 ||
            r0.stft_frames != r1.stft_frames) {
            if (bad < 5)
                printf("    %s: cpu %lld bytes, %s %lld bytes differ\n", fx->name, (long long) n0,
                       be->name, (long long) n1);
            bad++;
        }
        (*files)++;
        free(a0), free(a1), free(o0), free(o1);
    }
    return bad;
}

#endif
