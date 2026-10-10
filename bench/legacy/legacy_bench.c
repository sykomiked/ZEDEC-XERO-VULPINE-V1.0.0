/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* legacy_bench.c -- bit-exactness check and throughput figure for the legacy
 * transcoders (kernel/src/legacy cobol/fortran, kernel/src/lightningrod).
 *
 * Reads golden traces (bench/legacy/golden_legacy.tsv: published COBOL and
 * IBM HFP examples and hand-worked values, sources in the file), runs each
 * row through the transcoder and compares the result bit for bit. Then, with
 * --iters N, runs the whole trace N times and prints conversions per second.
 * The timing is a property of the machine it ran on; no figure is claimed.
 *
 * Usage: legacy_bench GOLDEN.tsv [--iters N]
 * Exit 0 when every row matches, 1 on any mismatch, 2 on a malformed file. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cobol.h"
#include "fortran.h"
#include "lightningrod.h"

#define MAXROWS 256
#define MAXHEX  32

typedef struct {
    char kind[16], in[64], arg[16], want[64];
    int line;
} row_t;

static row_t rows[MAXROWS];
static int nrows;

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Hex string -> bytes; returns byte count or -1. */
static int unhex(const char *s, uint8_t *out, int cap)
{
    int n = (int) strlen(s);
    if (n % 2 || n / 2 > cap) return -1;
    for (int i = 0; i < n / 2; i++) {
        int h = hexval(s[2 * i]), l = hexval(s[2 * i + 1]);
        if (h < 0 || l < 0) return -1;
        out[i] = (uint8_t) (h << 4 | l);
    }
    return n / 2;
}

static void tohex(const uint8_t *b, int n, char *out)
{
    static const char d[] = "0123456789ABCDEF";
    for (int i = 0; i < n; i++) {
        out[2 * i] = d[b[i] >> 4];
        out[2 * i + 1] = d[b[i] & 15];
    }
    out[2 * n] = 0;
}

static uint64_t hexu64(const char *s)
{
    return strtoull(s, NULL, 16);
}

/* Run one row; write the produced value as text into got. Returns 1 if it
 * matches the expected text, 0 if not, -1 if the row is malformed. */
static int run_row(const row_t *r, char *got, size_t cap)
{
    uint8_t buf[MAXHEX], out[MAXHEX];
    int64_t v = 0;
    const char *k = r->kind;
    if (!strcmp(k, "comp3_dec") || !strcmp(k, "zoned_dec") || !strcmp(k, "comp_dec")) {
        int n = unhex(r->in, buf, MAXHEX);
        if (n <= 0) return -1;
        int ok = !strcmp(k, "comp3_dec")   ? comp3_decode(buf, (uint32_t) n, &v)
                 : !strcmp(k, "zoned_dec") ? zoned_decode(buf, (uint32_t) n, &v)
                                           : comp_decode(buf, (uint32_t) n, r->arg[0] == 's', &v);
        if (ok)
            snprintf(got, cap, "%lld", (long long) v);
        else
            snprintf(got, cap, "REFUSE");
    } else if (!strcmp(k, "comp3_enc") || !strcmp(k, "zoned_enc") || !strcmp(k, "comp_enc")) {
        v = strtoll(r->in, NULL, 10);
        uint32_t n = (uint32_t) strtoul(r->arg, NULL, 10);
        const char *comma = strchr(r->arg, ',');
        int sgn = comma && comma[1] == 's';
        if (n == 0 || n > MAXHEX) return -1;
        memset(out, 0, sizeof out);
        int ok = !strcmp(k, "comp3_enc")   ? comp3_encode(v, out, n)
                 : !strcmp(k, "zoned_enc") ? zoned_encode(v, out, n, sgn)
                                           : comp_encode(v, out, n, sgn);
        if (ok)
            tohex(out, (int) n, got);
        else
            snprintf(got, cap, "REFUSE");
    } else if (!strcmp(k, "lr_packed") || !strcmp(k, "lr_zoned")) {
        int n = unhex(r->in, buf, MAXHEX);
        if (n <= 0) return -1;
        rat_t q;
        uint32_t scale = (uint32_t) strtoul(r->arg, NULL, 10);
        lr_result_t res = !strcmp(k, "lr_packed") ? lr_packed_to_rat(buf, (uint32_t) n, scale, &q)
                                                  : lr_zoned_to_rat(buf, (uint32_t) n, scale, &q);
        if (res.ok && res.exact && q.valid)
            snprintf(got, cap, "%lld/%lld", (long long) q.num, (long long) q.den);
        else
            snprintf(got, cap, "REFUSE");
    } else if (!strcmp(k, "hfp32_ieee") || !strcmp(k, "ieee32_hfp")) {
        uint32_t x = (uint32_t) hexu64(r->in);
        uint32_t y = !strcmp(k, "hfp32_ieee") ? hfp32_to_ieee32(x) : ieee32_to_hfp32(x);
        snprintf(got, cap, "%08X", y);
    } else if (!strcmp(k, "hfp64_ieee") || !strcmp(k, "ieee64_hfp")) {
        uint64_t x = hexu64(r->in);
        uint64_t y = !strcmp(k, "hfp64_ieee") ? hfp64_to_ieee64(x) : ieee64_to_hfp64(x);
        snprintf(got, cap, "%016llX", (unsigned long long) y);
    } else {
        return -1;
    }
    return strcmp(got, r->want) == 0;
}

static int load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[512];
    int ln = 0;
    while (fgets(line, sizeof line, f)) {
        ln++;
        if (line[0] == '#' || line[0] == '\n') continue;
        if (nrows == MAXROWS) break;
        row_t *r = &rows[nrows];
        line[strcspn(line, "\r\n")] = 0;
        if (sscanf(line, "%15[^\t]\t%63[^\t]\t%15[^\t]\t%63s", r->kind, r->in, r->arg, r->want) !=
            4) {
            fprintf(stderr, "line %d: expected 4 tab-separated columns\n", ln);
            fclose(f);
            return -1;
        }
        r->line = ln;
        nrows++;
    }
    fclose(f);
    return nrows;
}

int main(int argc, char **argv)
{
    long iters = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: %s GOLDEN.tsv [--iters N]\n", argv[0]);
        return 2;
    }
    if (argc == 4 && !strcmp(argv[2], "--iters")) iters = strtol(argv[3], NULL, 10);
    if (load(argv[1]) <= 0) {
        fprintf(stderr, "cannot read golden traces from %s\n", argv[1]);
        return 2;
    }
    int fails = 0;
    char got[64];
    for (int i = 0; i < nrows; i++) {
        int m = run_row(&rows[i], got, sizeof got);
        if (m < 0) {
            fprintf(stderr, "line %d: malformed row\n", rows[i].line);
            return 2;
        }
        if (!m) {
            printf("[FAIL] line %d %s %s (%s): got %s want %s\n", rows[i].line, rows[i].kind,
                   rows[i].in, rows[i].arg, got, rows[i].want);
            fails++;
        }
    }
    printf("legacy golden traces: %d rows, %d bit-exact, %d mismatched\n", nrows, nrows - fails,
           fails);
    if (iters > 0) {
        struct timespec a, b;
        volatile int sink = 0;
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (long it = 0; it < iters; it++)
            for (int i = 0; i < nrows; i++) sink += run_row(&rows[i], got, sizeof got);
        clock_gettime(CLOCK_MONOTONIC, &b);
        double s = (double) (b.tv_sec - a.tv_sec) + (double) (b.tv_nsec - a.tv_nsec) * 1e-9;
        (void) sink;
        printf("throughput: %ld x %d conversions in %.3f s = %.0f conversions/s "
               "(includes parsing and formatting of each row; this machine only)\n",
               iters, nrows, s, s > 0 ? (double) iters * nrows / s : 0.0);
    }
    if (fails) {
        printf("[FAIL] legacy_bench\n");
        return 1;
    }
    printf("[PASS] legacy_bench\n");
    return 0;
}
