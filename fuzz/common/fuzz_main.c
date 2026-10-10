/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_main.c — the replay driver. Linked into every harness when it is
 * built WITHOUT libFuzzer (plain gcc), so the committed corpora can be
 * replayed by `make -C kernel verify-all` on a machine with no clang.
 *
 *   harness FILE|DIR ...              run every file (a directory: every
 *                                     regular file in it) once
 *   harness --random SEED ITERS MAX   run ITERS inputs of 0..MAX bytes drawn
 *                                     from splitmix64(SEED): the seeded
 *                                     property mode of the economic harnesses
 *
 * A failing input aborts (the harnesses call abort() on a broken invariant;
 * the sanitizers abort on memory or UB errors), so the exit status is the
 * verdict. On success it prints how many inputs ran. */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "fuzz_in.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static unsigned long g_runs;

/* The input being run, so a failure in --random mode can be saved and
 * replayed: written to ./crash-random-<seed>-<iteration> by the property
 * check (fz_abort_at) or by the sanitizer's death callback. */
static const uint8_t *g_cur;
static size_t g_cur_n;
static uint64_t g_seed;
static unsigned long g_iter;
static int g_random_mode;

static void save_current(void)
{
    if (!g_random_mode || !g_cur) return;
    char name[96];
    snprintf(name, sizeof name, "crash-random-%llu-%lu", (unsigned long long) g_seed, g_iter);
    FILE *f = fopen(name, "wb");
    if (!f) return;
    if (g_cur_n) fwrite(g_cur, 1, g_cur_n, f);
    fclose(f);
    fprintf(stderr, "replay: failing input saved to %s (%zu bytes)\n", name, g_cur_n);
}
void (*fz_on_failure)(void) = save_current;

/* Present only when a sanitizer runtime is linked (weak, so a plain or
 * coverage-only build links without it). */
extern void __sanitizer_set_death_callback(void (*cb)(void)) __attribute__((weak));

static int run_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "replay: cannot open %s\n", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    uint8_t *buf = (uint8_t *) malloc((size_t) n);
    if (!buf && n) {
        fclose(f);
        return -1;
    }
    if (n && fread(buf, 1, (size_t) n, f) != (size_t) n) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    if (getenv("FUZZ_REPLAY_VERBOSE")) fprintf(stderr, "replay: %s (%ld bytes)\n", path, n);
    LLVMFuzzerTestOneInput(buf, (size_t) n);
    free(buf);
    g_runs++;
    return 0;
}

static int run_path(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "replay: no such path %s\n", path);
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) return run_file(path);
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *e;
    int rc = 0;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue; /* ., .., and hidden files */
        char sub[4096];
        int k = snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
        if (k < 0 || (size_t) k >= sizeof sub) continue;
        struct stat s2;
        if (stat(sub, &s2) != 0 || !S_ISREG(s2.st_mode)) continue;
        if (run_file(sub) != 0) rc = -1;
    }
    closedir(d);
    return rc;
}

static int run_random(uint64_t seed, unsigned long iters, size_t max)
{
    uint8_t *buf = (uint8_t *) malloc(max ? max : 1u);
    if (!buf) return -1;
    uint64_t s = seed;
    int save_each = getenv("FUZZ_SAVE_EACH") != NULL;
    for (unsigned long i = 0; i < iters; i++) {
        size_t n = max ? (size_t) (fz_splitmix64(&s) % (max + 1u)) : 0u;
        for (size_t j = 0; j < n; j += 8) {
            uint64_t r = fz_splitmix64(&s);
            size_t k = n - j < 8 ? n - j : 8;
            memcpy(buf + j, &r, k);
        }
        /* exact-size copy so ASan bounds the input */
        uint8_t *in = fz_dup(buf, n);
        g_cur = in;
        g_cur_n = n;
        g_iter = i;
        /* FUZZ_SAVE_EACH: write every input before running it, for failures
         * that end the process without a callback (some UBSan builds) */
        if (save_each) {
            FILE *f = fopen("crash-random-current", "wb");
            if (f) {
                if (n) fwrite(in, 1, n, f);
                fclose(f);
            }
        }
        LLVMFuzzerTestOneInput(in, n);
        g_cur = NULL;
        free(in);
        g_runs++;
    }
    free(buf);
    if (save_each) remove("crash-random-current");
    return 0;
}

int main(int argc, char **argv)
{
    int rc = 0;
    if (argc >= 5 && strcmp(argv[1], "--random") == 0) {
        uint64_t seed = strtoull(argv[2], NULL, 0);
        unsigned long iters = strtoul(argv[3], NULL, 0);
        size_t max = (size_t) strtoul(argv[4], NULL, 0);
        g_seed = seed;
        g_random_mode = 1;
        if (__sanitizer_set_death_callback) __sanitizer_set_death_callback(save_current);
        rc = run_random(seed, iters, max);
        printf("%s: %lu random inputs (seed %llu, <= %zu bytes) OK\n", argv[0], g_runs,
               (unsigned long long) seed, max);
        return rc ? 1 : 0;
    }
    if (argc < 2) {
        fprintf(stderr, "usage: %s FILE|DIR ... | --random SEED ITERS MAXLEN\n", argv[0]);
        return 2;
    }
    for (int i = 1; i < argc; i++)
        if (run_path(argv[i]) != 0) rc = 1;
    printf("%s: replayed %lu inputs%s\n", argv[0], g_runs, rc ? " (with errors)" : " OK");
    return rc;
}
