/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_dist.c — tensor-parallel forward passes (zt_model.h T23) against one
 * process, bit for bit:
 *   1. the three fixture models (test_model_fixture.h), Q16 and Q8 KV
 *      caches, split over 1 to 5 ranks (5 is more ranks than the models have
 *      query heads, so some ranks own no head): every rank's logits for a
 *      batched prefill and 8 greedy decode steps equal the single-process
 *      logits, with the C reference and with the fastest SIMD set + threads;
 *      the saturation counts agree; the ranks' multiply-add meters
 *      (dist_macs) add up to the single-process total;
 *   2. the same over TCP on 127.0.0.1 (zt_dist_tcp_ring), 3 ranks;
 *   2b. models made to saturate (an oversized bias and output gain): the
 *      saturation count each rank reports is the single-process count;
 *   3. a rank that dies: the others get ZT_MODEL_EDIST, not a hang;
 *   4. the ring all-gather alone: 2 to 7 ranks, uneven parts (n not a
 *      multiple of the rank count, units of 1 and 7, ranks with no part),
 *      B vectors with a stride: every rank ends with every value, and the
 *      counts add up.
 * Built with -DZT_TEST_MPI by mpicc and run under mpirun instead, it runs
 * check 1 over MPI (zt_dist_mpi.h) with the communicator's size.
 *
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -DTEST_HOST -Isrc/tensor -Isrc/tensor/dist \
 *       src/tensor/dist/test_zt_dist.c src/tensor/dist/zt_dist.c src/tensor/zt_simd.c \
 *       src/tensor/zt_simd_x86.c src/tensor/zt_simd_sve.c src/tensor/zt_simd_neon.c \
 *       src/tensor/zt_simd_rvv.c src/tensor/zt_model.c \
 *       src/tensor/zt_tok.c src/tensor/zt_rope.c src/tensor/zt_gguf.c src/tensor/zt.c \
 *       -lpthread -o /tmp/test_zt_dist && /tmp/test_zt_dist */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test_model_fixture.h"
#include "zt_dist.h"
#include "zt_model.h"
#include "zt_simd.h"
#ifdef ZT_TEST_MPI
#    include "zt_dist_mpi.h"
#endif

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("[FAIL] ");                                                                     \
        } else                                                                                     \
            printf("[PASS] ");                                                                     \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
        fflush(stdout);                                                                            \
    } while (0)

/* ---- the fixture files (as test_zt_simd.c) ---- */

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

typedef struct {
    int32_t rc;
    uint32_t n_sat;
    uint64_t macs, n;
    zt_fx *logits;
} run_t;

/* all logits of the first sequence (batch 4), then 8 greedy decode steps */
static run_t run_model(const zt_model_t *m, const mf_variant_t *v, uint32_t kv)
{
    run_t r = {0, 0, 0, 0, 0};
    uint32_t V = v->n_vocab, n0 = v->seq_len[0], steps = 8;
    uint64_t need = zt_model_state_bytes(m, 64, 4, kv);
    void *mem = malloc(need);
    zt_model_state_t s;
    r.n = (uint64_t) V * (n0 + steps);
    r.logits = calloc((size_t) r.n, 4);
    zt_model_state_init(&s, m, 64, 4, kv, mem, need);
    r.rc = zt_model_eval(m, &s, v->seq, n0, r.logits, ZT_EVAL_ALL_LOGITS);
    zt_fx *lg = r.logits + (uint64_t) (n0 - 1) * V;
    for (uint32_t i = 0; i < steps && !r.rc; i++) {
        int32_t id = zt_argmax(lg, V);
        lg += V;
        r.rc = zt_model_decode(m, &s, id, lg);
    }
    r.n_sat = s.n_sat;
    r.macs = s.dist_macs;
    free(mem);
    return r;
}

typedef struct {
    zt_model_t m;
    uint8_t *file;
    void *arena;
} loaded_t;

/* With g_sat set, layer 0's Q projection gets a bias near the Q16 limit
 * and the output norm's gain is multiplied by up to 4096, so matrix
 * products saturate and the saturation count must add up across ranks. */
static bool g_sat;
static zt_fx g_big[4096];

static int load(const mf_variant_t *v, loaded_t *L)
{
    zt_gguf_t g;
    zt_model_err_t err;
    L->file = rebuild(v);
    if (zt_gguf_open(&g, L->file, v->file_len)) return -1;
    uint64_t ab = zt_model_arena_bytes(&g);
    L->arena = malloc(ab);
    if (zt_model_load(&L->m, &g, L->arena, ab, &err)) return -1;
    if (g_sat) {
        for (uint32_t i = 0; i < 4096; i++) g_big[i] = (i & 1) ? 0x7FFF0000 : -0x7FFF0000;
        L->m.layers[0].bq = g_big;
        L->m.out_norm.frac = L->m.out_norm.frac > 12 ? L->m.out_norm.frac - 12 : 0;
    }
    return 0;
}

static void unload(loaded_t *L)
{
    free(L->arena);
    free(L->file);
}

/* What each rank reports to rank 0 through a pipe. */
typedef struct {
    uint32_t rank, same, n_sat;
    int32_t rc;
    uint64_t macs;
} report_t;

static int write_all(int fd, const void *b, size_t n)
{
    return write(fd, b, n) == (ssize_t) n ? 0 : -1;
}

/* A GPU stand-in (zt_kern_t.mm_full, as zt_metal provides): it only counts
 * and declines, so the CPU computes the product. zt_model uses mm_full only
 * on an undistributed model (whole matrices would bypass each rank's rows and
 * dist_macs); every distributed run below has it set and must never call it. */
static int mm_full_trap(void *fctx, const zt_mmf_t *jobs, uint32_t n)
{
    (void) jobs;
    (void) n;
    (*(uint32_t *) fctx)++;
    return 1; /* declined (zt_kern.h): the caller computes all n itself */
}

/* ---- 1 and 2: every rank count, kernel set and transport ---- */

static void group_case(const mf_variant_t *v, uint32_t kv, uint32_t P, bool fast, bool tcp,
                       const run_t *ref, uint16_t port)
{
    int pfd[2];
    if (pipe(pfd)) {
        CHECK(0, "pipe");
        return;
    }
    int fdn = -1, fdp = -1;
    pid_t pids[8];
    int rank;
    if (tcp) {
        rank = 0;
        for (uint32_t c = 1; c < P; c++) {
            pid_t p = fork();
            if (p == 0) {
                rank = (int) c;
                break;
            }
            pids[c - 1] = p;
        }
        const char *hosts[8] = {"127.0.0.1", "127.0.0.1", "127.0.0.1", "127.0.0.1",
                                "127.0.0.1", "127.0.0.1", "127.0.0.1", "127.0.0.1"};
        if (zt_dist_tcp_ring((uint32_t) rank, P, hosts, port, 10000, &fdn, &fdp)) rank = -rank - 1;
    } else {
        rank = zt_dist_fork_ring(P, &fdn, &fdp, pids);
    }
    report_t rep = {0, 0, 0, -1, 0};
    if (rank >= 0) {
        loaded_t L;
        if (!load(v, &L)) {
            zt_dist_ring_t *ring = zt_dist_ring_new((uint32_t) rank, P, fdn, fdp, 10000);
            zt_pool_t *pool = fast ? zt_pool_new(2) : NULL;
            zt_kern_t k;
            zt_kern_threaded(&k, fast ? zt_simd_best() : zt_simd_find("c"), pool);
            uint32_t gpu_calls = 0;
            k.mm_full = mm_full_trap;
            k.fctx = &gpu_calls;
            L.m.kern = &k;
            L.m.dist = zt_dist_ring_dist(ring);
            run_t r = run_model(&L.m, v, kv);
            rep.rank = (uint32_t) rank;
            rep.rc = r.rc;
            rep.n_sat = r.n_sat;
            rep.macs = r.macs;
            rep.same = r.n == ref->n && !memcmp(r.logits, ref->logits, ref->n * 4) &&
                       gpu_calls == 0; /* mm_full is never used under dist */
            free(r.logits);
            zt_pool_free(pool);
            zt_dist_ring_free(ring);
            unload(&L);
        }
    } else {
        rep.rank = (uint32_t) (-rank - 1);
    }
    if (rep.rank != 0) { /* a child: report and leave */
        write_all(pfd[1], &rep, sizeof rep);
        _exit(0);
    }
    close(pfd[1]);
    uint32_t all_same = rep.same && rep.rc == 0, sat_ok = rep.n_sat == ref->n_sat;
    uint64_t macs = rep.macs;
    for (uint32_t c = 1; c < P; c++) {
        report_t o;
        if (read(pfd[0], &o, sizeof o) != (ssize_t) sizeof o) {
            all_same = 0;
            continue;
        }
        all_same &= o.same && o.rc == 0;
        sat_ok &= o.n_sat == ref->n_sat;
        macs += o.macs;
    }
    close(pfd[0]);
    for (uint32_t c = 1; c < P; c++) waitpid(pids[c - 1], NULL, 0);
    CHECK(all_same && sat_ok && macs == ref->macs,
          "%s, KV %s, %u rank%s over %s, %s: every rank's %llu logits identical to one process; "
          "n_sat %u on every rank; rank meters sum to %llu multiply-adds",
          v->name, kv ? "Q8" : "Q16", P, P > 1 ? "s" : "", tcp ? "TCP" : "socketpairs",
          fast ? "fastest SIMD + 2 threads" : "C reference", (unsigned long long) ref->n,
          ref->n_sat, (unsigned long long) ref->macs);
}

/* ---- 3: a rank that dies ---- */

static void dead_peer(const mf_variant_t *v)
{
    int fdn, fdp;
    pid_t pid;
    int rank = zt_dist_fork_ring(2, &fdn, &fdp, &pid);
    if (rank == 1) _exit(0); /* closes its links without a word */
    loaded_t L;
    if (rank < 0 || load(v, &L)) {
        CHECK(0, "dead peer setup");
        return;
    }
    zt_dist_ring_t *ring = zt_dist_ring_new(0, 2, fdn, fdp, 5000);
    L.m.dist = zt_dist_ring_dist(ring);
    run_t r = run_model(&L.m, v, ZT_KV_Q16);
    waitpid(pid, NULL, 0);
    CHECK(r.rc == ZT_MODEL_EDIST, "a rank that exits: the other gets ZT_MODEL_EDIST (%d), no hang",
          r.rc);
    free(r.logits);
    zt_dist_ring_free(ring);
    unload(&L);
}

/* ---- 4: the all-gather alone ---- */

static zt_fx val(uint32_t b, uint32_t i)
{
    return (zt_fx) (uint32_t) mix(b * 7919u + 3, i);
}

static void gather_case(uint32_t P, uint32_t B, uint32_t n, uint32_t unit)
{
    int pfd[2], fdn, fdp;
    pid_t pids[8];
    if (pipe(pfd)) return;
    int rank = zt_dist_fork_ring(P, &fdn, &fdp, pids);
    uint32_t ok = 0, stride = n + 3;
    if (rank >= 0) {
        zt_dist_ring_t *ring = zt_dist_ring_new((uint32_t) rank, P, fdn, fdp, 10000);
        const zt_dist_t *d = zt_dist_ring_dist(ring);
        zt_fx *y = malloc((size_t) B * stride * 4);
        uint32_t u0, u1, cnt = (uint32_t) rank + 1;
        zt_dist_part(n / unit, P, (uint32_t) rank, &u0, &u1);
        for (uint32_t b = 0; b < B; b++)
            for (uint32_t i = 0; i < stride; i++)
                y[b * stride + i] =
                    (i >= u0 * unit && i < u1 * unit) ? val(b, i) : (i < n ? -1 : 7);
        ok = d->allgather(d->ctx, y, stride, B, n, unit, &cnt) == 0 && cnt == P * (P + 1) / 2;
        for (uint32_t b = 0; b < B; b++)
            for (uint32_t i = 0; i < stride; i++)
                ok &= y[b * stride + i] == (i < n ? val(b, i) : 7);
        free(y);
        zt_dist_ring_free(ring);
    }
    if (rank != 0) {
        write_all(pfd[1], &ok, sizeof ok);
        _exit(0);
    }
    close(pfd[1]);
    for (uint32_t c = 1; c < P; c++) {
        uint32_t o = 0;
        if (read(pfd[0], &o, sizeof o) != (ssize_t) sizeof o) o = 0;
        ok &= o;
        waitpid(pids[c - 1], NULL, 0);
    }
    close(pfd[0]);
    CHECK(ok,
          "ring all-gather, %u ranks, %u vectors of %u values in units of %u: every rank holds "
          "every value, padding untouched, counts sum to %u",
          P, B, n, unit, P * (P + 1) / 2);
}

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;
#ifdef ZT_TEST_MPI
    (void) group_case; /* the fork and TCP checks are for the plain build */
    (void) dead_peer;
    (void) gather_case;
    MPI_Init(&argc, &argv);
    int me, np;
    MPI_Comm_rank(MPI_COMM_WORLD, &me);
    MPI_Comm_size(MPI_COMM_WORLD, &np);
    zt_dist_mpi_t *dm = zt_dist_mpi_new(MPI_COMM_WORLD);
    for (uint32_t vi = 0; vi < MF_N_VARIANTS; vi++)
        for (uint32_t kv = 0; kv < 2; kv++) {
            const mf_variant_t *v = &MF_VARIANTS[vi];
            loaded_t L;
            load(v, &L);
            run_t ref = run_model(&L.m, v, kv);
            L.m.kern = zt_simd_best();
            L.m.dist = zt_dist_mpi_dist(dm);
            run_t r = run_model(&L.m, v, kv);
            unsigned int same = r.rc == 0 && r.n == ref.n &&
                                !memcmp(r.logits, ref.logits, r.n * 4) && r.n_sat == ref.n_sat,
                         all = 0;
            unsigned long long macs = r.macs, msum = 0;
            MPI_Allreduce(&same, &all, 1, MPI_UNSIGNED, MPI_SUM, MPI_COMM_WORLD);
            MPI_Allreduce(&macs, &msum, 1, MPI_UNSIGNED_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
            if (me == 0)
                CHECK(all == (unsigned) np && msum == ref.macs,
                      "%s, KV %s, %d MPI ranks: every rank's %llu logits identical to one process; "
                      "meters sum to %llu",
                      v->name, kv ? "Q8" : "Q16", np, (unsigned long long) ref.n, msum);
            free(ref.logits);
            free(r.logits);
            unload(&L);
        }
    zt_dist_mpi_free(dm);
    MPI_Finalize();
    if (me == 0) printf("%d/%d checks passed (MPI, %d ranks)\n", checks - failures, checks, np);
    return failures ? 1 : 0;
#else
    /* below Linux's ephemeral range (32768 and up), so no outgoing connection holds one */
    uint16_t port = (uint16_t) (20000 + (getpid() % 1500) * 8);
    for (uint32_t vi = 0; vi < MF_N_VARIANTS; vi++) {
        const mf_variant_t *v = &MF_VARIANTS[vi];
        loaded_t L;
        if (load(v, &L)) {
            CHECK(0, "%s: fixture loads", v->name);
            continue;
        }
        for (uint32_t kv = 0; kv < 2; kv++) {
            run_t ref = run_model(&L.m, v, kv); /* one process, C reference */
            CHECK(ref.rc == 0, "%s, KV %s: single-process run", v->name, kv ? "Q8" : "Q16");
            { /* the trap is live: without dist, zt_model offers it every product */
                zt_kern_t k = *zt_simd_find("c");
                uint32_t gpu_calls = 0;
                k.mm_full = mm_full_trap;
                k.fctx = &gpu_calls;
                L.m.kern = &k;
                run_t g = run_model(&L.m, v, kv);
                L.m.kern = NULL;
                CHECK(g.rc == 0 && gpu_calls > 0 && g.n == ref.n &&
                          !memcmp(g.logits, ref.logits, ref.n * 4),
                      "%s, KV %s: one process offers mm_full %u products; declined, the logits "
                      "are the CPU's",
                      v->name, kv ? "Q8" : "Q16", gpu_calls);
                free(g.logits);
            }
            for (uint32_t P = 1; P <= 5; P++)
                for (int fast = 0; fast < 2; fast++) group_case(v, kv, P, fast, false, &ref, 0);
            group_case(v, kv, 3, true, true, &ref, port);
            port = (uint16_t) (port + 4);
            free(ref.logits);
        }
        unload(&L);
    }
    /* saturation: the count must add up across ranks */
    g_sat = true;
    for (uint32_t vi = 0; vi < MF_N_VARIANTS; vi++) {
        const mf_variant_t *v = &MF_VARIANTS[vi];
        loaded_t L;
        if (load(v, &L)) continue;
        run_t ref = run_model(&L.m, v, ZT_KV_Q16);
        CHECK(ref.rc == 0 && ref.n_sat > 0,
              "%s with an oversized bias and gain: %u values saturate", v->name, ref.n_sat);
        for (uint32_t P = 2; P <= 4; P++) group_case(v, ZT_KV_Q16, P, P & 1, false, &ref, 0);
        free(ref.logits);
        unload(&L);
    }
    g_sat = false;
    dead_peer(&MF_VARIANTS[0]);
    for (uint32_t P = 2; P <= 7; P++) {
        gather_case(P, 1, 5, 1);      /* fewer values than ranks */
        gather_case(P, 3, 1001, 1);   /* uneven parts */
        gather_case(P, 2, 7 * 37, 7); /* head-sized units */
        gather_case(P, 5, 300000, 1); /* parts larger than a socket buffer */
    }
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
#endif
}
