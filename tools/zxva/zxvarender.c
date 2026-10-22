/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvarender.c — render a ZXVA vector sound to a WAV file with the kernel
 * renderer (kernel/src/vaudio/zxva.c), at any rate and depth.
 *
 *   zxvarender in.zxva out.wav [--rate R] [--bits 16|24|32] [--dither]
 *              [--residual] [--layers MASK] [--info]
 *
 * --residual adds the lossless residual (only at the file's native rate and
 * depth, which become the defaults when the file has one). --layers takes the
 * ZXVA_L_* mask (1 partials, 2 noise, 4 grains, 8 voices, 16 frame voices). Build:
 *   cc -std=c11 -O2 -Ikernel/src/vaudio tools/zxva/zxvarender.c \
 *      kernel/src/vaudio/zxva.c -o zxvarender
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zxva.h"

static zxva_work_t W;
static int32_t BUF[ZXVA_BLOCK * 8 * ZXVA_MAX_CH];

static void le(FILE *f, uint32_t v, int n)
{
    for (int i = 0; i < n; i++) fputc((int) ((v >> (8 * i)) & 0xFFu), f);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: zxvarender in.zxva out.wav [--rate R] [--bits B] [--dither] "
                        "[--residual] [--layers MASK] [--info]\n");
        return 2;
    }
    uint32_t rate = 0, bits = 0, flags = 0, layers = ZXVA_L_ALL;
    int info = 0;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--rate") && i + 1 < argc)
            rate = (uint32_t) strtoul(argv[++i], 0, 10);
        else if (!strcmp(argv[i], "--bits") && i + 1 < argc)
            bits = (uint32_t) strtoul(argv[++i], 0, 10);
        else if (!strcmp(argv[i], "--layers") && i + 1 < argc)
            layers = (uint32_t) strtoul(argv[++i], 0, 0) & ZXVA_L_ALL;
        else if (!strcmp(argv[i], "--dither"))
            flags |= ZXVA_F_DITHER;
        else if (!strcmp(argv[i], "--residual"))
            flags |= ZXVA_F_RESIDUAL;
        else if (!strcmp(argv[i], "--info"))
            info = 1;
        else {
            fprintf(stderr, "zxvarender: unknown option %s\n", argv[i]);
            return 2;
        }
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    static uint8_t data[1 << 26];
    size_t n = fread(data, 1, sizeof data, f);
    fclose(f);
    zxva_info_t in;
    int rc = zxva_check(data, (uint32_t) n, &in);
    if (rc < 0) {
        fprintf(stderr, "zxvarender: %s: invalid ZXVA (%d)\n", argv[1], rc);
        return 1;
    }
    if (!rate) rate = in.res_rate ? in.res_rate : 48000;
    if (!bits) bits = in.res_bits ? in.res_bits : 24;
    if (info)
        printf("channels %u, %u us, partials %u, noise %u, grains %u, voices %u, fvoices %u, "
               "residual %u Hz/%u bit\n",
               in.nch, in.dur_us, in.npartial, in.nnoise, in.ngrain, in.nvoice, in.nfvoice,
               in.res_rate, in.res_bits);
    rc = zxva_open(&W, data, (uint32_t) n, rate, bits, flags | layers);
    if (rc < 0) {
        fprintf(stderr, "zxvarender: cannot render at %u Hz / %u bit with these flags (%d)\n", rate,
                bits, rc);
        return 1;
    }
    FILE *o = fopen(argv[2], "wb");
    if (!o) {
        perror(argv[2]);
        return 1;
    }
    uint64_t frames = zxva_frames(&W);
    uint32_t nch = in.nch, bps = bits / 8, data_len = (uint32_t) (frames * nch * bps);
    fwrite("RIFF", 1, 4, o);
    le(o, 36 + data_len, 4);
    fwrite("WAVEfmt ", 1, 8, o);
    le(o, 16, 4);
    le(o, 1, 2);
    le(o, nch, 2);
    le(o, rate, 4);
    le(o, rate * nch * bps, 4);
    le(o, nch * bps, 2);
    le(o, bits, 2);
    fwrite("data", 1, 4, o);
    le(o, data_len, 4);
    uint32_t blk = ZXVA_BLOCK * 8;
    for (uint64_t at = 0; at < frames; at += blk) {
        uint32_t m = frames - at < blk ? (uint32_t) (frames - at) : blk;
        zxva_render(&W, at, BUF, m);
        for (uint32_t i = 0; i < m * nch; i++) le(o, (uint32_t) BUF[i], (int) bps);
    }
    fclose(o);
    return 0;
}
