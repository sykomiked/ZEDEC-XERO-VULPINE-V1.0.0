/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vblock.c — host tests for the device-memory block layer.
 *
 * Correctness on the CPU reference backend and the simulated GPU backend,
 * then each bottleneck-avoidance algorithm measured on the simulator against
 * a naive host-bounce baseline. The simulator's bandwidths and latencies are
 * model parameters (set in rig_pcie / rig_apple below), not measurements.
 *
 *   gcc -std=c11 -O2 -Wall -Werror -Wextra -DTEST_HOST -Isrc/vblock -Isrc/ipfs_node \
 *       -Isrc/robin_debanks src/vblock/test_vblock.c src/vblock/vblock.c \
 *       src/vblock/vblock_cpu.c src/vblock/vblock_sim.c src/ipfs_node/ipfsn_multiformats.c \
 *       src/ipfs_node/ipfsn_unixfs.c src/ipfs_node/ipfsn_store.c src/ipfs_node/ipfsn_net.c \
 *       src/ipfs_node/ipfsn_ubh.c src/robin_debanks/sha256.c src/tls/aead.c src/tls/hkdf.c \
 *       src/ubh/ubh.c src/event_space/event_envelope.c src/tensor/zt.c \
 *       -o /tmp/test_vblock && /tmp/test_vblock
 *
 * (run from kernel/)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vblock.h"
#include "ipfs_node.h"
#include "sha256.h"

static int failures = 0, checks = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)
static int quiet_fail = 0;
#define QCHECK(c, m)                                                                               \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            if (quiet_fail++ < 10) printf("  [fail] %s (line %d)\n", m, __LINE__);                 \
        }                                                                                          \
    } while (0)

static uint64_t rng_s = 0x56424C4F434B2121ull;
static uint64_t rnd(void)
{
    uint64_t z = (rng_s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

#define KiB   1024u
#define MiB   (1024u * 1024u)
#define BLK   (256u * KiB) /* Kubo's default chunk: an IPFS-native block */
#define ARENA (20u * MiB)
#define SMALL 4096u
#define NBIG  64u
#define NSML  1024u

static uint8_t arena[4][ARENA];
static vb_range_t ranges[4][4096];
static vb_entry_t table[4096];
static uint8_t scratch[IPFSN_BLOCK_MAX];
static uint8_t big[NBIG][BLK];
static ipfsn_cid_t bigc[NBIG];
static uint8_t sml[NSML][SMALL];
static ipfsn_cid_t smlc[NSML];

typedef struct {
    vb_sim_t sim;
    vb_cpu_t cpu;
    vb_backend_t be;
    vb_store_t s;
    vb_pool_cfg_t pc[4];
    int H, P, V1, V2, U, V; /* store pool indices */
} rig_t;
static rig_t R;

/* ---- model parameters ------------------------------------------------------
 * PCIe 4.0 x16 with pinned memory: 24 GB/s, 5 us per transfer.
 * Pageable host memory over PCIe (the driver bounces it): 9 GB/s, 10 us.
 * Host memcpy (pageable <-> pinned, gather): 16 GB/s, 0.3 us.
 * NVLink-class peer link: 200 GB/s, 3 us.
 * Apple unified memory blit (shared -> private): 200 GB/s, 3 us.
 * Device SHA-256: 40 GB/s aggregate, 4 us.                                  */
#define PCIE_MBPS 24000u
#define PCIE_LAT  5000u
#define PAGE_MBPS 9000u
#define PAGE_LAT  10000u
#define MEMC_MBPS 16000u
#define MEMC_LAT  300u
#define NVL_MBPS  200000u
#define NVL_LAT   3000u

static int rig_store(uint32_t opts, uint32_t npools)
{
    vb_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.be = &R.be;
    cfg.table = table;
    cfg.table_cap = 4096;
    cfg.pools = R.pc;
    cfg.npools = npools;
    cfg.opts = opts;
    cfg.scratch = scratch;
    cfg.scratch_cap = sizeof(scratch);
    return vb_init(&R.s, &cfg);
}

static void pool_cfg(uint32_t i, uint32_t dev, uint32_t tier, uint64_t bytes)
{
    R.pc[i].dev = dev;
    R.pc[i].tier = tier;
    R.pc[i].bytes = bytes;
    R.pc[i].ranges = ranges[i];
    R.pc[i].nranges = 4096;
}

/* dev 0 = CPU (host + pinned), devs 1..2 = discrete GPUs. */
static int rig_pcie(uint32_t opts, bool nvlink, bool rdma, uint64_t vram, uint64_t pinned)
{
    vb_sim_t *m = &R.sim;
    vb_sim_init(m);
    vb_sim_dev(m, 0, VB_DEVK_CPU);
    int h = vb_sim_region(m, 0, VB_TIER_HOST, arena[0], ARENA);
    int p = vb_sim_region(m, 0, VB_TIER_PINNED, arena[1], ARENA);
    vb_sim_link(m, (uint32_t) h, (uint32_t) p, MEMC_MBPS, MEMC_LAT, 0);
    vb_sim_link(m, (uint32_t) p, (uint32_t) h, MEMC_MBPS, MEMC_LAT, 0);
    vb_sim_link(m, (uint32_t) p, (uint32_t) p, MEMC_MBPS, MEMC_LAT, 0);
    vb_sim_rdma(m, (uint32_t) p, true); /* NICs DMA pinned host memory */
    int v[2];
    for (uint32_t g = 0; g < 2; g++) {
        vb_sim_dev(m, g + 1, VB_DEVK_GPU);
        v[g] = vb_sim_region(m, g + 1, VB_TIER_VRAM, arena[2 + g], ARENA);
        uint32_t up = 1 + 2 * g, dn = 2 + 2 * g;
        vb_sim_link(m, (uint32_t) p, (uint32_t) v[g], PCIE_MBPS, PCIE_LAT, up);
        vb_sim_link(m, (uint32_t) v[g], (uint32_t) p, PCIE_MBPS, PCIE_LAT, dn);
        vb_sim_link(m, (uint32_t) h, (uint32_t) v[g], PAGE_MBPS, PAGE_LAT, up);
        vb_sim_link(m, (uint32_t) v[g], (uint32_t) h, PAGE_MBPS, PAGE_LAT, dn);
        vb_sim_hash_rate(m, g + 1, 40000, 4000);
        vb_sim_rdma(m, (uint32_t) v[g], rdma);
    }
    if (nvlink) {
        vb_sim_link(m, (uint32_t) v[0], (uint32_t) v[1], NVL_MBPS, NVL_LAT, 10);
        vb_sim_link(m, (uint32_t) v[1], (uint32_t) v[0], NVL_MBPS, NVL_LAT, 11);
    }
    vb_sim_backend(m, &R.be);
    pool_cfg(0, 0, VB_TIER_HOST, ARENA);
    pool_cfg(1, 0, VB_TIER_PINNED, pinned);
    pool_cfg(2, 1, VB_TIER_VRAM, vram);
    pool_cfg(3, 2, VB_TIER_VRAM, vram);
    int rc = rig_store(opts, 4);
    R.H = vb_pool_index(&R.s, 0, VB_TIER_HOST);
    R.P = vb_pool_index(&R.s, 0, VB_TIER_PINNED);
    R.V1 = vb_pool_index(&R.s, 1, VB_TIER_VRAM);
    R.V2 = vb_pool_index(&R.s, 2, VB_TIER_VRAM);
    return rc;
}

/* Apple Silicon: dev 1 is an integrated GPU. Shared storage is unified
 * memory both the CPU and GPU read in place; private storage is GPU-only. */
static int rig_apple(uint32_t opts)
{
    vb_sim_t *m = &R.sim;
    vb_sim_init(m);
    vb_sim_dev(m, 0, VB_DEVK_CPU);
    vb_sim_dev(m, 1, VB_DEVK_UNIFIED);
    int u = vb_sim_region(m, 1, VB_TIER_UNIFIED, arena[0], ARENA);
    int v = vb_sim_region(m, 1, VB_TIER_VRAM, arena[2], ARENA);
    vb_sim_access(m, (uint32_t) u, 0, true);
    vb_sim_link(m, (uint32_t) u, (uint32_t) v, 200000, 3000, 1);
    vb_sim_link(m, (uint32_t) v, (uint32_t) u, 200000, 3000, 2);
    vb_sim_hash_rate(m, 1, 40000, 4000);
    vb_sim_backend(m, &R.be);
    pool_cfg(0, 1, VB_TIER_UNIFIED, ARENA);
    pool_cfg(1, 1, VB_TIER_VRAM, ARENA);
    int rc = rig_store(opts, 2);
    R.U = vb_pool_index(&R.s, 1, VB_TIER_UNIFIED);
    R.V = vb_pool_index(&R.s, 1, VB_TIER_VRAM);
    return rc;
}

static int rig_cpu(uint32_t opts)
{
    vb_cpu_init(&R.cpu);
    vb_cpu_add_arena(&R.cpu, VB_TIER_HOST, arena[0], ARENA);
    vb_cpu_add_arena(&R.cpu, VB_TIER_PINNED, arena[1], ARENA);
    vb_cpu_backend(&R.cpu, &R.be);
    pool_cfg(0, 0, VB_TIER_HOST, ARENA);
    pool_cfg(1, 0, VB_TIER_PINNED, 2 * MiB);
    int rc = rig_store(opts, 2);
    R.H = 0;
    R.P = 1;
    return rc;
}

static uint64_t bytes_moved(void)
{
    uint64_t b = 0;
    for (uint32_t k = 0; k < VB_KINDS; k++) b += R.s.st.bytes[k];
    return b;
}

static uint64_t xfers(void)
{
    uint64_t n = 0;
    for (uint32_t k = 0; k < VB_KINDS; k++) n += R.s.st.xfers[k];
    return n;
}

/* ---- speedup report ---- */
static char summary[32][160];
static uint32_t nsum = 0;
static void report(const char *what, uint64_t naive_ns, uint64_t opt_ns)
{
    uint64_t x100 = opt_ns ? naive_ns * 100u / opt_ns : 0;
    snprintf(summary[nsum % 32], sizeof(summary[0]),
             "%-46s naive %9.1f us  optimised %9.1f us  %3llu.%02llux", what, naive_ns / 1000.0,
             opt_ns / 1000.0, (unsigned long long) (x100 / 100), (unsigned long long) (x100 % 100));
    printf("  SPEEDUP %s\n", summary[nsum % 32]);
    nsum++;
}

/* Is every byte of every resident copy equal to the original? */
static const uint8_t *orig_of(const ipfsn_cid_t *c, uint32_t *len)
{
    for (uint32_t i = 0; i < NBIG; i++)
        if (ipfsn_cid_equal(c, &bigc[i])) {
            *len = BLK;
            return big[i];
        }
    for (uint32_t i = 0; i < NSML; i++)
        if (ipfsn_cid_equal(c, &smlc[i])) {
            *len = SMALL;
            return sml[i];
        }
    return 0;
}

static bool store_consistent(void)
{
    vb_store_t *s = &R.s;
    for (uint32_t p = 0; p < s->npools; p++) {
        uint64_t used = 0, prev = 0;
        for (uint32_t i = 0; i < s->pool[p].nr; i++) {
            const vb_range_t *r = &s->pool[p].r[i];
            if (r->off < prev || r->off + r->len > s->pool[p].cap) return false;
            prev = r->off + r->len;
            used += r->len;
            if (r->entry != 0xFFFFFFFFu) {
                const vb_copy_t *c = &s->tab[r->entry].c[r->slot];
                if (c->pool != p || c->off != r->off) return false;
            }
        }
        if (used != s->pool[p].used) return false;
    }
    for (uint32_t i = 0; i < s->cap; i++) {
        const vb_entry_t *e = &s->tab[i];
        if (e->state != 1) continue;
        uint32_t len = 0;
        const uint8_t *o = orig_of(&e->cid, &len);
        if (!o || len != e->len) return false;
        for (uint32_t k = 0; k < VB_MAX_COPIES; k++) {
            const vb_copy_t *c = &e->c[k];
            if (c->pool == VB_NONE) continue;
            if (R.be.fence_wait) R.be.fence_wait(R.be.ctx, c->ready);
            const uint8_t *mem = R.sim.mem[s->pool[c->pool].h] + c->off;
            if (memcmp(mem, o, len) != 0) return false;
        }
    }
    return true;
}

/* ---- compute callback for vb_run ---- */
typedef struct {
    const vb_op_t *ops;
    uint32_t bad_inputs;
    vb_fence_t done[256];
    uint8_t dev[256];
} cctx_t;

static int compute_cb(void *ctx, uint32_t op, uint32_t dev, const vb_ref_t *refs, uint32_t n,
                      vb_fence_t ready, vb_fence_t *done)
{
    cctx_t *c = (cctx_t *) ctx;
    for (uint32_t i = 0; i < n; i++) { /* the right bytes, on this device */
        uint32_t len = 0;
        const uint8_t *o = orig_of(&c->ops[op].in[i], &len);
        const uint8_t *mem = R.sim.mem[refs[i].backend_pool] + refs[i].off;
        if (!o || refs[i].len != len || memcmp(mem, o, len) != 0) c->bad_inputs++;
        if (!R.sim.acc[refs[i].backend_pool][dev]) c->bad_inputs++;
    }
    /* Activations are the tensor engine's: an op waits for the ops it
     * depends on, plus the modelled hand-off when they ran elsewhere. */
    vb_fence_t wait = ready;
    for (uint32_t k = 0; k < c->ops[op].ndeps; k++) {
        uint32_t p = c->ops[op].deps[k];
        vb_fence_t f = c->done[p];
        if (c->dev[p] != dev) {
            uint64_t ns = 0;
            vb_route(&R.s, R.s.home[c->dev[p]], R.s.home[dev], c->ops[p].out_len, 0, 0, &ns);
            f += ns;
        }
        if (f > wait) wait = f;
    }
    int rc = vb_sim_compute(&R.sim, dev, wait, c->ops[op].cost_ns, refs, n, done);
    c->done[op] = *done;
    c->dev[op] = (uint8_t) dev;
    return rc;
}

static uint64_t run_ops(const vb_op_t *ops, uint32_t n, const uint8_t *devs, uint32_t depth,
                        uint32_t *bad)
{
    static vb_ref_t rs[8 * 256];
    static cctx_t c;
    memset(&c, 0, sizeof(c));
    c.ops = ops;
    vb_fence_t last = 0;
    uint64_t t0 = R.sim.now;
    int rc = vb_run(&R.s, ops, n, devs, depth, rs, 256, compute_cb, &c, &last);
    R.be.fence_wait(R.be.ctx, last);
    if (bad) *bad = c.bad_inputs + (rc != VB_OK);
    return rc == VB_OK ? R.sim.now - t0 : 0;
}

static void put_all(const ipfsn_cid_t *c, uint8_t (*d)[BLK], uint32_t n, int pool)
{
    for (uint32_t i = 0; i < n; i++) vb_put(&R.s, &c[i], d[i], BLK, (uint32_t) pool, 0);
}

/* ======================================================================= */

static void test_cpu_backend(void)
{
    printf("--- CPU reference backend ---\n");
    CHECK(rig_cpu(VB_OPT_ALL) == VB_OK, "cpu: store init");
    CHECK(vb_put(&R.s, &smlc[0], sml[0], SMALL, (uint32_t) R.P, 0) == VB_OK, "cpu: put");
    CHECK(vb_put(&R.s, &smlc[0], sml[1], SMALL, (uint32_t) R.P, 0) == VB_ERR_HASH,
          "cpu: put of bytes that do not match the CID is refused");
    CHECK(vb_put(&R.s, &smlc[0], sml[0], SMALL, (uint32_t) R.P, 0) == VB_OK &&
              R.s.st.dedup_hits == 1,
          "cpu: second put of the same CID is a dedup hit");
    vb_ref_t r;
    CHECK(vb_acquire(&R.s, &smlc[0], 0, &r) == VB_OK && r.pool == R.P && xfers() == 0,
          "cpu: acquire on the CPU uses the resident copy");
    CHECK(memcmp(R.cpu.mem[r.backend_pool] + r.off, sml[0], SMALL) == 0, "cpu: bytes intact");
    vb_release(&R.s, &r, 0);
    CHECK(vb_put(&R.s, &smlc[1], sml[1], SMALL, (uint32_t) R.H, 0) == VB_OK, "cpu: put to host");
    CHECK(vb_acquire(&R.s, &smlc[1], 0, &r) == VB_OK && R.s.st.zero_copy_hits == 1 && xfers() == 0,
          "cpu: host copy is read in place (zero-copy)");
    vb_release(&R.s, &r, 0);
    ipfsn_cid_t missing;
    ipfsn_cid_sha256(IPFSN_MC_RAW, (const uint8_t *) "nope", 4, &missing);
    CHECK(vb_acquire(&R.s, &missing, 0, &r) == VB_ERR_NOTFOUND, "cpu: unknown CID not found");

    /* Without dedup: the baseline copies even when resident (in pinned). */
    rig_cpu(VB_OPT_NONE);
    vb_put(&R.s, &smlc[0], sml[0], SMALL, (uint32_t) R.H, 0);
    vb_acquire(&R.s, &smlc[0], 0, &r);
    CHECK(R.s.st.xfers[VB_K_H2H] == 1 && r.pool == R.P, "cpu: naive mode copies host->pinned");
    vb_release(&R.s, &r, 0);
    CHECK(R.s.pool[R.P].nr == 0, "cpu: naive temp copy freed on release");

    /* Eviction under a 2 MiB pinned pool: blocks with another copy go. */
    rig_cpu(VB_OPT_ALL);
    for (uint32_t i = 0; i < 64; i++) vb_put(&R.s, &smlc[i], sml[i], SMALL, (uint32_t) R.H, 0);
    for (uint32_t i = 0; i < 64; i++) vb_put(&R.s, &smlc[i], sml[i], SMALL, (uint32_t) R.P, 0);
    CHECK(R.s.pool[R.P].used <= 2 * MiB && R.s.st.evictions == 0, "cpu: 64 x 4 KiB fit in pinned");
    for (uint32_t i = 64; i < 640; i++) vb_put(&R.s, &smlc[i], sml[i], SMALL, (uint32_t) R.H, 0);
    for (uint32_t i = 64; i < 640; i++) vb_put(&R.s, &smlc[i], sml[i], SMALL, (uint32_t) R.P, 0);
    CHECK(R.s.st.evictions > 0 && R.s.pool[R.P].used <= 2 * MiB,
          "cpu: pinned pool evicts copies that also live in host RAM");
    bool all = true;
    for (uint32_t i = 0; i < 640; i++) all &= vb_has(&R.s, &smlc[i]);
    CHECK(all, "cpu: no block lost to eviction (each kept its host copy)");
}

/* UnixFS through the store: blocks in, ipfsn_cat out. */
static int sink_put(void *ctx, const ipfsn_cid_t *cid, const uint8_t *b, uint32_t len)
{
    (void) ctx;
    return vb_put(&R.s, cid, b, len, (uint32_t) R.P, 0) == VB_OK ? 0 : -1;
}
static uint8_t file[96 * KiB], cat_out[96 * KiB];
static uint32_t cat_len;
static int cat_write(void *ctx, const uint8_t *d, uint32_t len)
{
    (void) ctx;
    if (cat_len + len > sizeof(cat_out)) return -1;
    memcpy(cat_out + cat_len, d, len);
    cat_len += len;
    return 0;
}
static ipfsn_ufs_t ufs;
static ipfsn_walk_t walk;
static uint8_t chunkbuf[1024];

static void test_ipfs_glue(void)
{
    printf("--- ipfs_node glue: UnixFS DAG in VRAM, walked by ipfsn_cat ---\n");
    for (uint32_t i = 0; i < sizeof(file); i++) file[i] = (uint8_t) rnd();
    rig_pcie(VB_OPT_ALL, true, false, 8 * MiB, 8 * MiB);
    ipfsn_cid_t root;
    uint64_t fsz = 0;
    int rc = ipfsn_ufs_init(&ufs, chunkbuf, sizeof(chunkbuf), 0, sink_put, 0);
    if (rc == IPFSN_OK) rc = ipfsn_ufs_update(&ufs, file, sizeof(file));
    if (rc == IPFSN_OK) rc = ipfsn_ufs_final(&ufs, &root, &fsz);
    CHECK(rc == IPFSN_OK && fsz == sizeof(file), "glue: UnixFS build sinks into the store");
    uint32_t nblocks = R.s.count;
    CHECK(nblocks == 96 + 1, "glue: 96 leaves + 1 root resident");
    /* Move every block into GPU 1's VRAM, then drop the host copies. */
    uint32_t moved = 0;
    for (uint32_t i = 0; i < R.s.cap; i++) {
        if (R.s.tab[i].state != 1) continue;
        ipfsn_cid_t c;
        memcpy(&c, &R.s.tab[i].cid, sizeof(c));
        vb_ref_t r;
        if (vb_acquire(&R.s, &c, 1, &r) == VB_OK) moved++;
        vb_release(&R.s, &r, 0);
    }
    uint32_t dropped = vb_drop_pool(&R.s, (uint32_t) R.P);
    CHECK(moved == nblocks && dropped == nblocks && R.s.pool[R.P].nr == 0,
          "glue: DAG now lives only in VRAM");
    cat_len = 0;
    walk.leaf = scratch;
    walk.leaf_cap = sizeof(scratch);
    uint64_t sz = 0;
    rc = ipfsn_cat(&walk, &root, vb_ipfs_source, &R.s, cat_write, 0, &sz);
    CHECK(rc == IPFSN_OK && sz == sizeof(file) && memcmp(cat_out, file, sizeof(file)) == 0,
          "glue: ipfsn_cat rebuilds the file from VRAM blocks (every block re-hashed)");
    CHECK(R.s.st.staged_serves == nblocks, "glue: each block staged to pinned exactly once");
    uint64_t d2h = R.s.st.bytes[VB_K_D2H];
    cat_len = 0;
    rc = ipfsn_cat(&walk, &root, vb_ipfs_source, &R.s, cat_write, 0, &sz);
    CHECK(rc == IPFSN_OK && R.s.st.bytes[VB_K_D2H] == d2h,
          "glue: second walk copies nothing (staged copies are resident)");
    CHECK(R.sim.hazards == 0, "glue: no fence hazards");

    /* Backing source: an ipfs_node blockstore fills misses. */
    static uint8_t medium[4 * MiB];
    static ipfsn_bs_entry_t bst[256];
    static uint8_t bscratch[IPFSN_SCRATCH_MIN];
    ipfsn_memstore_t ms = {medium, sizeof(medium), 0};
    ipfsn_storage_ops_t ops;
    ipfsn_memstore_ops(&ms, &ops);
    ipfsn_bs_t bs;
    ipfsn_bs_init(&bs, &ops, bst, 256, bscratch, sizeof(bscratch));
    for (uint32_t i = 0; i < 8; i++) ipfsn_bs_put(&bs, &bigc[i], big[i], BLK, IPFSN_VIS_PUBLIC, 0);
    rig_pcie(VB_OPT_ALL, true, false, 8 * MiB, 8 * MiB);
    vb_link_t net = {1000, 200000};
    vb_set_backing(&R.s, ipfsn_bs_source, &bs, net);
    vb_ref_t r;
    rc = vb_acquire(&R.s, &bigc[3], 2, &r);
    CHECK(rc == VB_OK && R.s.st.backing_fetches == 1 && r.pool == R.V2,
          "glue: miss fetched from the ipfs_node blockstore, verified, moved to GPU 2");
    CHECK(memcmp(R.sim.mem[r.backend_pool] + r.off, big[3], BLK) == 0,
          "glue: fetched bytes intact");
    vb_release(&R.s, &r, 0);
    /* A backing source that lies. */
    memcpy(medium + 64, "tampered", 8); /* inside the first stored block */
    rc = vb_acquire(&R.s, &bigc[0], 1, &r);
    CHECK(rc != VB_OK && !vb_has(&R.s, &bigc[0]), "glue: tampered backing block never enters");
}

static void test_routing(void)
{
    printf("--- V3 routing: peer paths beat host bounce ---\n");
    uint8_t hops[VB_MAX_POOLS];
    uint32_t n = 0;
    uint64_t c1 = 0, c2 = 0;
    rig_pcie(VB_OPT_ALL, true, false, 8 * MiB, 8 * MiB);
    CHECK(vb_route(&R.s, (uint32_t) R.V1, (uint32_t) R.V2, BLK, hops, &n, &c1) == VB_OK && n == 2,
          "route: NVLink is a direct GPU1->GPU2 hop");
    rig_pcie(VB_OPT_ALL & ~VB_OPT_P2P, true, false, 8 * MiB, 8 * MiB);
    CHECK(vb_route(&R.s, (uint32_t) R.V1, (uint32_t) R.V2, BLK, hops, &n, &c2) == VB_OK && n == 3 &&
              hops[1] == R.P,
          "route: without P2P the path bounces through pinned host memory");
    CHECK(c1 < c2, "route: peer path is cheaper");
    rig_pcie(VB_OPT_ALL, false, false, 8 * MiB, 8 * MiB);
    CHECK(vb_route(&R.s, (uint32_t) R.V1, (uint32_t) R.V2, BLK, hops, &n, &c2) == VB_OK && n == 3 &&
              hops[1] == R.P,
          "route: no peer link -> staged through pinned, not pageable");
    CHECK(vb_route(&R.s, (uint32_t) R.H, (uint32_t) R.V1, 64 * MiB, hops, &n, 0) == VB_OK &&
              n == 3 && hops[1] == R.P,
          "route: big pageable->VRAM transfer goes via pinned (memcpy + fast DMA)");
    {
        vb_link_t d = {PAGE_MBPS, PAGE_LAT}, m = {MEMC_MBPS, MEMC_LAT}, p = {PCIE_MBPS, PCIE_LAT};
        bool okc = true;
        for (uint64_t len = 64; len <= 64 * MiB; len *= 4) {
            uint64_t direct = vb_link_ns(d, len), via = vb_link_ns(m, len) + vb_link_ns(p, len);
            uint64_t c = 0;
            vb_route(&R.s, (uint32_t) R.H, (uint32_t) R.V1, len, hops, &n, &c);
            okc &= c == (direct < via ? direct : via) && n == (direct < via ? 2u : 3u);
        }
        CHECK(okc, "route: pageable->VRAM picks min(direct, memcpy+pinned DMA), 64 B..64 MiB");
    }

    /* Measure: move 16 x 256 KiB from GPU 1 to GPU 2. */
    uint64_t t[2];
    for (uint32_t mode = 0; mode < 2; mode++) {
        rig_pcie(mode ? VB_OPT_ALL : (VB_OPT_ALL & ~VB_OPT_P2P), true, false, 8 * MiB, 8 * MiB);
        put_all(bigc, big, 16, R.P);
        vb_ref_t rs[16];
        vb_acquire_many(&R.s, bigc, 16, 1, rs);
        for (uint32_t i = 0; i < 16; i++) vb_release(&R.s, &rs[i], 0);
        vb_drop_pool(&R.s, (uint32_t) R.P); /* only VRAM copies remain */
        R.be.fence_wait(R.be.ctx, R.sim.lane_free[1]);
        uint64_t t0 = R.sim.now;
        vb_fence_t last = 0;
        vb_acquire_many(&R.s, bigc, 16, 2, rs);
        for (uint32_t i = 0; i < 16; i++) {
            if (rs[i].ready > last) last = rs[i].ready;
            vb_release(&R.s, &rs[i], 0);
        }
        R.be.fence_wait(R.be.ctx, last);
        t[mode] = R.sim.now - t0;
        if (mode) {
            CHECK(R.s.st.bytes[VB_K_PEER] == 16ull * BLK && R.s.st.bytes[VB_K_D2H] == 0,
                  "p2p: GPU1->GPU2 bytes moved over the peer link only");
        } else {
            CHECK(R.s.st.bytes[VB_K_PEER] == 0 && R.s.st.multi_hop == 16,
                  "p2p off: every block bounced through host memory");
        }
        CHECK(store_consistent() && R.sim.hazards == 0, "p2p: copies intact, no hazards");
    }
    report("GPU1->GPU2 4 MiB: peer vs host bounce", t[0], t[1]);
}

static void test_dedup(void)
{
    printf("--- V1 dedup by CID ---\n");
    rig_pcie(VB_OPT_ALL, true, false, 8 * MiB, 8 * MiB);
    put_all(bigc, big, 4, R.P);
    vb_ref_t a, b;
    vb_acquire(&R.s, &bigc[0], 1, &a);
    uint64_t x = xfers();
    vb_acquire(&R.s, &bigc[0], 1, &b);
    CHECK(x == 1 && xfers() == 1 && a.off == b.off && R.s.st.dedup_hits == 1,
          "dedup: second consumer shares the resident copy, no transfer");
    vb_release(&R.s, &a, 0);
    vb_release(&R.s, &b, 0);
    ipfsn_cid_t two[2] = {bigc[1], bigc[1]};
    vb_ref_t rr[2];
    vb_acquire_many(&R.s, two, 2, 1, rr);
    CHECK(xfers() == 2 && rr[0].off == rr[1].off, "dedup: duplicate CID in one batch moves once");
    vb_release(&R.s, &rr[0], 0);
    vb_release(&R.s, &rr[1], 0);
    vb_landing_t l;
    CHECK(vb_recv_begin(&R.s, &bigc[1], BLK, 2, &l) == VB_HAVE,
          "dedup: a block already resident is never fetched from the network");
    rig_pcie(VB_OPT_ALL & ~VB_OPT_DEDUP, true, false, 8 * MiB, 8 * MiB);
    put_all(bigc, big, 1, R.P);
    vb_acquire(&R.s, &bigc[0], 1, &a);
    vb_acquire(&R.s, &bigc[0], 1, &b);
    CHECK(xfers() == 2 && a.off != b.off, "no dedup: each consumer gets its own copy");
    vb_release(&R.s, &a, 0);
    vb_release(&R.s, &b, 0);
    CHECK(R.s.pool[R.V1].nr == 0, "no dedup: temporary copies freed");
}

static void test_zero_copy(void)
{
    printf("--- V2 zero-copy on unified memory ---\n");
    static vb_op_t ops[16];
    for (uint32_t i = 0; i < 16; i++) {
        memset(&ops[i], 0, sizeof(ops[i]));
        ops[i].in = &bigc[i];
        ops[i].nin = 1;
        ops[i].dev = 1;
        ops[i].cost_ns = 20000;
    }
    uint64_t t[2], b[2];
    uint32_t bad = 0;
    for (uint32_t mode = 0; mode < 2; mode++) {
        CHECK(rig_apple(mode ? VB_OPT_ALL : VB_OPT_NONE) == VB_OK, "apple: store init");
        put_all(bigc, big, 16, R.U);
        t[mode] = run_ops(ops, 16, 0, mode ? 2 : 1, &bad);
        b[mode] = bytes_moved();
        CHECK(bad == 0 && R.sim.hazards == 0, "apple: inputs correct, no hazards");
    }
    CHECK(b[1] == 0 && R.s.st.zero_copy_hits == 16,
          "apple: GPU reads shared (unified) memory in place: 0 bytes moved");
    CHECK(b[0] == 16ull * BLK, "apple baseline: blits every block to private storage");
    report("Apple unified 16 ops x 256 KiB (zero-copy)", t[0], t[1]);
}

static void test_batching(void)
{
    printf("--- V4 batching small blocks into extents ---\n");
    uint64_t t[2], nx[2];
    for (uint32_t mode = 0; mode < 2; mode++) {
        rig_pcie(mode ? VB_OPT_ALL : (VB_OPT_ALL & ~VB_OPT_BATCH), true, false, 8 * MiB, 8 * MiB);
        for (uint32_t i = 0; i < NSML; i++)
            vb_put(&R.s, &smlc[i], sml[i], SMALL, (uint32_t) R.P, 0);
        static vb_ref_t rs[NSML];
        uint64_t t0 = R.sim.now;
        int rc = VB_OK;
        for (uint32_t g = 0; g < NSML / 128 && rc == VB_OK; g++)
            rc = vb_acquire_many(&R.s, &smlc[g * 128], 128, 1, &rs[g * 128]);
        vb_fence_t last = 0;
        for (uint32_t i = 0; i < NSML; i++) {
            if (rs[i].ready > last) last = rs[i].ready;
            vb_release(&R.s, &rs[i], 0);
        }
        R.be.fence_wait(R.be.ctx, last);
        t[mode] = R.sim.now - t0;
        nx[mode] = R.s.st.xfers[VB_K_H2D];
        CHECK(rc == VB_OK && store_consistent() && R.sim.hazards == 0,
              "batch: all 1024 blocks resident and intact, no hazards");
    }
    CHECK(nx[0] == NSML && nx[1] == NSML / 128 && R.s.st.batched_blocks == NSML,
          "batch: 1024 DMAs become 8 (one per 128-block extent)");
    report("1024 x 4 KiB to GPU (batched extents)", t[0], t[1]);

    /* An extent never exceeds 168 blocks (one freight's rows). */
    rig_pcie(VB_OPT_ALL, true, false, 8 * MiB, 8 * MiB);
    for (uint32_t i = 0; i < 400; i++) vb_put(&R.s, &smlc[i], sml[i], SMALL, (uint32_t) R.P, 0);
    static vb_ref_t rs[400];
    int rc = vb_acquire_many(&R.s, smlc, 400, 1, rs);
    CHECK(rc == VB_OK && R.s.st.batches == 3 && R.s.st.xfers[VB_K_H2D] == 3,
          "batch: 400 blocks -> extents of 168 + 168 + 64");
    for (uint32_t i = 0; i < 400; i++) vb_release(&R.s, &rs[i], 0);

    /* Pinned memory full of last copies: no room for a staging extent, so
     * the batch falls back to one DMA per block instead of failing. */
    rig_pcie(VB_OPT_ALL, true, false, 8 * MiB, 64 * SMALL);
    for (uint32_t i = 0; i < 64; i++) vb_put(&R.s, &smlc[i], sml[i], SMALL, (uint32_t) R.P, 0);
    rc = vb_acquire_many(&R.s, smlc, 64, 1, rs);
    CHECK(rc == VB_OK && R.s.st.batches == 0 && R.s.st.xfers[VB_K_H2D] == 64 && store_consistent(),
          "batch: no staging room -> per-block fallback, all 64 resident and intact");
    for (uint32_t i = 0; i < 64; i++) vb_release(&R.s, &rs[i], 0);
}

static void test_pipeline(void)
{
    printf("--- V5 async prefetch pipeline: 16 layers x 1 MiB of weights ---\n");
    static vb_op_t ops[16];
    for (uint32_t l = 0; l < 16; l++) {
        memset(&ops[l], 0, sizeof(ops[l]));
        ops[l].in = &bigc[4 * l];
        ops[l].nin = 4;
        ops[l].dev = 1;
        ops[l].cost_ns = 60000;
    }
    struct {
        const char *name;
        uint32_t opts, depth;
        bool pinned;
    } runs[] = {
        {"naive", VB_OPT_NONE, 1, false},
        {"all, depth 1", VB_OPT_ALL, 1, true},
        {"all, depth 2", VB_OPT_ALL, 2, true},
        {"all, depth 3", VB_OPT_ALL, 3, true},
    };
    uint64_t t[4];
    for (uint32_t k = 0; k < 4; k++) {
        /* VRAM holds 5 MiB: weights must stream (no warm cache). */
        rig_pcie(runs[k].opts, true, false, 5 * MiB, ARENA);
        put_all(bigc, big, 64, runs[k].pinned ? R.P : R.H);
        uint32_t bad = 0;
        t[k] = run_ops(ops, 16, 0, runs[k].depth, &bad);
        printf("  pipeline %-14s %9.1f us  (h2d lane busy %9.1f us, compute busy %9.1f us)\n",
               runs[k].name, t[k] / 1000.0, R.sim.lane_busy[1] / 1000.0,
               R.sim.lane_busy[VB_SIM_LANE_COMP + 1] / 1000.0);
        CHECK(t[k] && bad == 0 && R.sim.hazards == 0,
              "pipeline: every op read the right bytes after their fence, no hazards");
    }
    CHECK(t[2] < t[1] && t[3] <= t[2], "pipeline: double and triple buffering overlap transfer");
    uint64_t copy_ns = R.sim.lane_busy[1], comp_ns = R.sim.lane_busy[VB_SIM_LANE_COMP + 1];
    uint64_t bound = copy_ns > comp_ns ? copy_ns : comp_ns;
    CHECK(t[3] < bound + bound / 8, "pipeline: depth 3 is within 12.5% of the busier engine");
    report("16-layer weight streaming: depth 3 vs naive", t[0], t[3]);
    report("16-layer weight streaming: depth 3 vs depth 1", t[1], t[3]);

    /* Warm: everything fits, second pass moves nothing. */
    rig_pcie(VB_OPT_ALL, true, false, 18 * MiB, ARENA);
    put_all(bigc, big, 64, R.P);
    uint32_t bad = 0;
    uint64_t cold = run_ops(ops, 16, 0, 3, &bad);
    uint64_t b0 = bytes_moved();
    uint64_t warm = run_ops(ops, 16, 0, 3, &bad);
    CHECK(bad == 0 && bytes_moved() == b0, "pipeline: warm pass is all dedup hits, 0 bytes");
    report("16-layer pass, weights resident (dedup)", cold, warm);
}

static void test_eviction(void)
{
    printf("--- V6 cost-aware eviction vs LRU ---\n");
    uint64_t t[2], h2d[2];
    for (uint32_t mode = 0; mode < 2; mode++) {
        uint32_t opts = mode ? VB_OPT_ALL : (VB_OPT_ALL & ~VB_OPT_COSTEVICT);
        rig_pcie(opts, true, false, 8 * BLK, ARENA);
        put_all(bigc, big, 12, R.P);
        /* A = blocks 6..11 also live on GPU 2 (NVLink refetch is cheap);
         * B = blocks 0..5 only in host memory (PCIe refetch). */
        for (uint32_t i = 6; i < 12; i++) {
            vb_ref_t r;
            vb_acquire(&R.s, &bigc[i], 2, &r);
            vb_release(&R.s, &r, 0);
        }
        R.be.fence_wait(R.be.ctx, R.sim.lane_free[3]);
        uint64_t h0 = R.s.st.bytes[VB_K_H2D], t0 = R.sim.now;
        for (uint32_t round = 0; round < 10; round++)
            for (uint32_t i = 0; i < 12; i++) {
                vb_ref_t r;
                vb_acquire(&R.s, &bigc[i], 1, &r);
                vb_fence_t d = 0;
                vb_sim_compute(&R.sim, 1, r.ready, 1000, &r, 1, &d);
                vb_release(&R.s, &r, d);
                R.be.fence_wait(R.be.ctx, d);
            }
        t[mode] = R.sim.now - t0;
        h2d[mode] = R.s.st.bytes[VB_K_H2D] - h0;
        CHECK(store_consistent() && R.sim.hazards == 0, "evict: copies intact, no hazards");
    }
    printf("  LRU moved %llu KiB over PCIe, cost-aware %llu KiB\n",
           (unsigned long long) (h2d[0] / KiB), (unsigned long long) (h2d[1] / KiB));
    CHECK(h2d[1] * 4 < h2d[0],
          "evict: cost-aware keeps the PCIe-only blocks, refetches over NVLink");
    report("12-block cyclic trace, 8-block VRAM (eviction)", t[0], t[1]);

    /* Pinned weights survive pressure; last copies are never dropped. */
    rig_pcie(VB_OPT_ALL, true, false, 4 * BLK, ARENA);
    put_all(bigc, big, 16, R.P);
    CHECK(vb_pin(&R.s, &bigc[0], 1) == VB_OK, "pin: weights pinned on GPU 1");
    for (uint32_t i = 1; i < 16; i++) {
        vb_ref_t r;
        vb_acquire(&R.s, &bigc[i], 1, &r);
        vb_release(&R.s, &r, 0);
    }
    bool in = false;
    vb_copies(&R.s, &bigc[0], (uint32_t) R.V1, &in);
    CHECK(in && R.s.st.evictions >= 12, "pin: pinned copy survived 15 competing blocks");
    CHECK(vb_unpin(&R.s, &bigc[0], 1) == VB_OK, "pin: unpin");
    vb_drop_pool(&R.s, (uint32_t) R.V1);
    vb_copies(&R.s, &bigc[0], (uint32_t) R.V1, &in);
    CHECK(!in, "pin: unpinned copy can go");
    /* A device output is a last copy: never evicted, the pool reports NOMEM. */
    rig_pcie(VB_OPT_ALL, true, false, 2 * BLK, ARENA);
    vb_landing_t l;
    ipfsn_cid_t oc[3];
    int rc = VB_OK;
    for (uint32_t i = 0; i < 2 && rc == VB_OK; i++) {
        rc = vb_output_alloc(&R.s, 1, BLK, &l);
        memcpy(R.sim.mem[l.rdma.pool] + l.off, big[40 + i], BLK);
        if (rc == VB_OK) rc = vb_output_commit(&R.s, &l, IPFSN_MC_RAW, 0, &oc[i]);
    }
    CHECK(rc == VB_OK, "last copy: two device outputs fill the pool");
    CHECK(vb_output_alloc(&R.s, 1, BLK, &l) == VB_ERR_NOMEM,
          "last copy: a third output gets NOMEM instead of destroying data");
}

static void test_placement(void)
{
    printf("--- V7 placement: ops go where their data is ---\n");
    static vb_op_t ops[16];
    static uint32_t dep[16];
    for (uint32_t i = 0; i < 16; i++) {
        memset(&ops[i], 0, sizeof(ops[i]));
        ops[i].in = &bigc[i];
        ops[i].nin = 1;
        ops[i].dev = VB_DEV_ANY;
        ops[i].cost_ns = 50000;
        dep[i] = i ? i - 1 : 0;
        ops[i].deps = &dep[i];
        ops[i].ndeps = i ? 1 : 0;
        ops[i].out_len = 64 * KiB;
    }
    uint8_t rr[16], pl[16];
    for (uint32_t i = 0; i < 16; i++) rr[i] = (uint8_t) (1 + (i & 1));
    uint64_t t[2], pb[2];
    for (uint32_t mode = 0; mode < 2; mode++) {
        rig_pcie(VB_OPT_ALL, false, false, 8 * MiB, ARENA);
        put_all(bigc, big, 16, R.P);
        for (uint32_t i = 0; i < 16; i++) { /* layers 0..7 on GPU 1, 8..15 on GPU 2 */
            vb_ref_t r;
            vb_acquire(&R.s, &bigc[i], i < 8 ? 1 : 2, &r);
            vb_release(&R.s, &r, 0);
        }
        vb_drop_pool(&R.s, (uint32_t) R.P);
        R.be.fence_wait(R.be.ctx, R.sim.lane_free[3]);
        R.be.fence_wait(R.be.ctx, R.sim.lane_free[1]);
        uint64_t ns = 0;
        if (mode == 0) {
            vb_plan_cost(&R.s, ops, 16, rr, &pb[0], &ns);
        } else {
            CHECK(vb_plan(&R.s, ops, 16, 0x6, pl, &pb[1], &ns) == VB_OK, "plan: ok");
            bool home = true;
            for (uint32_t i = 0; i < 16; i++) home &= pl[i] == (i < 8 ? 1 : 2);
            CHECK(home, "plan: ops 0-7 on GPU 1, 8-15 on GPU 2 (with their weights)");
        }
        uint32_t bad = 0;
        t[mode] = run_ops(ops, 16, mode ? pl : rr, 2, &bad);
        CHECK(bad == 0 && R.sim.hazards == 0, "plan: run correct, no hazards");
    }
    printf("  planned transfer bytes: round-robin %llu KiB, planner %llu KiB\n",
           (unsigned long long) (pb[0] / KiB), (unsigned long long) (pb[1] / KiB));
    CHECK(pb[1] == 64 * KiB && pb[0] > 2 * MiB, "plan: one 64 KiB hand-off instead of 2+ MiB");
    report("16 ops, 2 GPUs: planner vs round-robin", t[0], t[1]);

    /* Affinity from an access trace. */
    rig_pcie(VB_OPT_ALL, true, false, 8 * MiB, ARENA);
    put_all(bigc, big, 4, R.P);
    vb_access_t tr[8] = {{&bigc[0], 2}, {&bigc[0], 2}, {&bigc[0], 1}, {&bigc[1], 1},
                         {&bigc[1], 1}, {&bigc[2], 0}, {&bigc[0], 2}, {&bigc[3], 2}};
    CHECK(vb_affinity(&R.s, tr, 8) == VB_OK, "affinity: trace counted");
    int placed = vb_place_affine(&R.s, true);
    bool a = false, b = false, c = false, d = false;
    vb_copies(&R.s, &bigc[0], (uint32_t) R.V2, &a);
    vb_copies(&R.s, &bigc[1], (uint32_t) R.V1, &b);
    vb_copies(&R.s, &bigc[2], (uint32_t) R.P, &c);
    vb_copies(&R.s, &bigc[3], (uint32_t) R.V2, &d);
    CHECK(placed == 4 && a && b && c && d, "affinity: each block placed on its main consumer");
    vb_drop_pool(&R.s, (uint32_t) R.V2);
    vb_copies(&R.s, &bigc[0], (uint32_t) R.V2, &a);
    CHECK(a, "affinity: placed copies are pinned");
}

static void test_network(void)
{
    printf("--- V8 network: serve from VRAM, land in VRAM, verify by CID ---\n");
    for (uint32_t rd = 0; rd < 2; rd++) {
        rig_pcie(VB_OPT_ALL, true, rd == 1, 8 * MiB, 8 * MiB);
        put_all(bigc, big, 2, R.P);
        vb_ref_t r;
        vb_acquire(&R.s, &bigc[0], 1, &r);
        vb_release(&R.s, &r, 0);
        vb_drop_pool(&R.s, (uint32_t) R.P);
        uint64_t d2h = R.s.st.bytes[VB_K_D2H];
        vb_serve_t sv;
        CHECK(vb_serve(&R.s, &bigc[0], &sv) == VB_OK, "serve: ok");
        if (rd) {
            CHECK(sv.kind == VB_SERVE_RDMA && sv.rdma.pool == (uint32_t) R.s.pool[R.V1].h &&
                      R.s.st.bytes[VB_K_D2H] == d2h,
                  "serve: GPUDirect-style descriptor, NIC reads VRAM, no staging copy");
            vb_fence_t sent =
                vb_sim_nic(&R.sim, sv.rdma.pool, sv.rdma.off, sv.len, false, 12500, 2000, 30);
            CHECK(memcmp(R.sim.mem[sv.rdma.pool] + sv.rdma.off, big[0], BLK) == 0,
                  "serve: exported region holds the block");
            vb_serve_done(&R.s, &sv, sent);
        } else {
            CHECK(sv.kind == VB_SERVE_HOST && R.s.st.bytes[VB_K_D2H] == d2h + BLK &&
                      memcmp(sv.ptr, big[0], BLK) == 0,
                  "serve: no RDMA -> staged once through pinned memory");
            static uint8_t wire[BLK + 4096];
            uint32_t wl = 0;
            const uint8_t *blk = 0;
            uint32_t bl = 0;
            int rc = ipfsn_wire_encode_block(IPFSN_WIRE_UBH168, &bigc[0], sv.ptr, sv.len, wire,
                                             sizeof(wire), &wl);
            if (rc == IPFSN_OK)
                rc = ipfsn_wire_decode_block(IPFSN_WIRE_UBH168, &bigc[0], wire, wl, &blk, &bl);
            CHECK(rc == IPFSN_OK && bl == BLK, "serve: staged bytes go out as a UBH-168 envelope");
            vb_serve_done(&R.s, &sv, 0);
            CHECK(vb_serve(&R.s, &bigc[0], &sv) == VB_OK && sv.kind == VB_SERVE_HOST &&
                      R.s.st.bytes[VB_K_D2H] == d2h + BLK && R.s.st.host_serves == 1,
                  "serve: second serve copies nothing (staged copy resident)");
            vb_serve_done(&R.s, &sv, 0);
        }
        CHECK(vb_put(&R.s, &bigc[5], big[5], BLK, (uint32_t) R.P, VB_F_PRIVATE) == VB_OK &&
                  vb_serve(&R.s, &bigc[5], &sv) == VB_ERR_PRIVATE,
              "serve: private block refused");

        /* Receive. */
        for (uint32_t tamper = 0; tamper < 2; tamper++) {
            vb_landing_t l;
            uint32_t idx = 10 + tamper;
            int rc = vb_recv_begin(&R.s, &bigc[idx], BLK, 1, &l);
            CHECK(rc == VB_OK && l.kind == (rd ? VB_LAND_RDMA : VB_LAND_HOST),
                  rd ? "recv: lands directly in GPU 1's VRAM" : "recv: lands in pinned memory");
            uint8_t *dst = R.sim.mem[l.rdma.pool] + l.off;
            memcpy(dst, big[idx], BLK);
            if (tamper) dst[BLK / 2] ^= 0x40;
            vb_fence_t arr = vb_sim_nic(&R.sim, l.rdma.pool, l.off, BLK, true, 12500, 2000, 30);
            uint64_t hd = R.s.st.hashed_dev_bytes;
            rc = vb_recv_end(&R.s, &l, arr);
            if (tamper) {
                CHECK(rc == VB_ERR_HASH && !vb_has(&R.s, &bigc[idx]) && R.s.st.rejected == 1,
                      "recv: one flipped bit -> rejected, never resident");
            } else {
                CHECK(rc == VB_OK && vb_has(&R.s, &bigc[idx]), "recv: verified and resident");
                if (rd)
                    CHECK(R.s.st.hashed_dev_bytes == hd + BLK,
                          "recv: hashed on the GPU, bytes never touched host RAM");
            }
        }
        CHECK(store_consistent() && R.sim.hazards == 0, "network: intact, no hazards");
    }

    /* Device outputs are named on the device. */
    rig_pcie(VB_OPT_ALL, true, false, 8 * MiB, 8 * MiB);
    vb_landing_t l;
    ipfsn_cid_t c1, c2;
    vb_output_alloc(&R.s, 1, BLK, &l);
    memcpy(R.sim.mem[l.rdma.pool] + l.off, big[20], BLK);
    CHECK(vb_output_commit(&R.s, &l, IPFSN_MC_RAW, 0, &c1) == VB_OK &&
              ipfsn_cid_equal(&c1, &bigc[20]) && R.s.st.hashed_dev_bytes == BLK,
          "output: device-hashed CID equals the IPFS CID of the bytes");
    vb_output_alloc(&R.s, 1, BLK, &l);
    memcpy(R.sim.mem[l.rdma.pool] + l.off, big[20], BLK);
    uint64_t nr = R.s.pool[R.V1].nr;
    CHECK(vb_output_commit(&R.s, &l, IPFSN_MC_RAW, 0, &c2) == VB_OK &&
              R.s.pool[R.V1].nr == nr - 1 && R.s.st.dedup_hits == 1,
          "output: identical second output is deduplicated, memory freed");
}

static void test_hazard_detector(void)
{
    printf("--- simulator self-test: a missing fence is caught ---\n");
    rig_pcie(VB_OPT_ALL, true, false, 8 * MiB, 8 * MiB);
    put_all(bigc, big, 1, R.P);
    vb_ref_t r;
    vb_acquire(&R.s, &bigc[0], 1, &r);
    vb_fence_t d;
    vb_sim_compute(&R.sim, 1, 0 /* should be r.ready */, 1000, &r, 1, &d);
    CHECK(R.sim.hazards == 1, "hazard: compute before its input copy finished is detected");
    vb_release(&R.s, &r, d);
}

static void test_stress(void)
{
    printf("--- stress: random puts, acquires, releases, serves, drops ---\n");
    for (uint32_t seed = 0; seed < 3; seed++) {
        rig_pcie(seed == 2 ? (VB_OPT_ALL & ~VB_OPT_P2P) : VB_OPT_ALL, seed != 1, seed == 0,
                 3 * BLK + 64 * SMALL, 4 * BLK + 64 * SMALL);
        for (uint32_t i = 0; i < 12; i++) vb_put(&R.s, &bigc[i], big[i], BLK, (uint32_t) R.H, 0);
        for (uint32_t i = 0; i < 48; i++) vb_put(&R.s, &smlc[i], sml[i], SMALL, (uint32_t) R.H, 0);
        bool ok = true;
        uint32_t nok = 0;
        for (uint32_t step = 0; step < 400; step++) {
            uint32_t op = (uint32_t) (rnd() % 10);
            if (op < 6) {
                ipfsn_cid_t c[6];
                uint32_t n = 1 + (uint32_t) (rnd() % 6);
                for (uint32_t k = 0; k < n; k++) {
                    uint32_t j = (uint32_t) (rnd() % 60);
                    c[k] = j < 12 ? bigc[j] : smlc[j - 12];
                }
                vb_ref_t rs[6];
                uint32_t dev = 1 + (uint32_t) (rnd() % 2);
                if (vb_acquire_many(&R.s, c, n, dev, rs) == VB_OK) {
                    vb_fence_t rd = 0, d = 0;
                    for (uint32_t k = 0; k < n; k++) rd = rs[k].ready > rd ? rs[k].ready : rd;
                    vb_sim_compute(&R.sim, dev, rd, 2000 + rnd() % 20000, rs, n, &d);
                    for (uint32_t k = 0; k < n; k++) vb_release(&R.s, &rs[k], d);
                    nok++;
                }
            } else if (op < 8) {
                vb_serve_t sv;
                uint32_t j = (uint32_t) (rnd() % 12);
                if (vb_serve(&R.s, &bigc[j], &sv) == VB_OK) {
                    vb_fence_t f = vb_sim_nic(&R.sim, sv.ref.backend_pool, sv.ref.off, sv.len,
                                              false, 12500, 2000, 30);
                    vb_serve_done(&R.s, &sv, f);
                }
            } else if (op == 8) {
                vb_drop_pool(&R.s, 2 + (uint32_t) (rnd() % 2));
            } else {
                R.be.fence_wait(R.be.ctx, R.sim.now + rnd() % 50000);
            }
            if (step % 25 == 24) ok &= store_consistent();
        }
        bool all = true;
        for (uint32_t i = 0; i < 12; i++) all &= vb_has(&R.s, &bigc[i]);
        for (uint32_t i = 0; i < 48; i++) all &= vb_has(&R.s, &smlc[i]);
        QCHECK(ok, "stress: every copy byte-identical, allocator consistent");
        QCHECK(R.sim.hazards == 0, "stress: no hazards");
        QCHECK(all, "stress: no block lost");
        QCHECK(nok > 100, "stress: most acquires succeed under pressure");
        printf("  seed %u: %u acquires ok, %llu evictions, %llu batches, hazards %llu\n", seed, nok,
               (unsigned long long) R.s.st.evictions, (unsigned long long) R.s.st.batches,
               (unsigned long long) R.sim.hazards);
    }
    CHECK(quiet_fail == 0, "stress: 3 topologies x 400 random steps");
}

static void test_end_to_end(void)
{
    printf("--- end to end: 2 passes of a 16-layer model split over 2 GPUs ---\n");
    /* Each layer reads 4 x 256 KiB weight blocks and 32 x 4 KiB small blocks
     * (norms, biases, KV pages) and hands a 64 KiB activation to the next. */
    static vb_op_t ops[32];
    static ipfsn_cid_t in[16][36];
    static uint32_t dep[32];
    static uint8_t devs[32];
    for (uint32_t l = 0; l < 16; l++) {
        for (uint32_t k = 0; k < 4; k++) in[l][k] = bigc[4 * l + k];
        for (uint32_t k = 0; k < 32; k++) in[l][4 + k] = smlc[32 * l + k];
    }
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t l = i % 16;
        memset(&ops[i], 0, sizeof(ops[i]));
        ops[i].in = in[l];
        ops[i].nin = 36;
        ops[i].dev = l < 8 ? 1 : 2;
        ops[i].cost_ns = 60000;
        dep[i] = i ? i - 1 : 0;
        ops[i].deps = &dep[i];
        ops[i].ndeps = i ? 1 : 0;
        ops[i].out_len = 64 * KiB;
        devs[i] = (uint8_t) ops[i].dev;
    }
    uint64_t t[2], b[2];
    for (uint32_t mode = 0; mode < 2; mode++) {
        rig_pcie(mode ? VB_OPT_ALL : VB_OPT_NONE, true, false, 12 * MiB, ARENA);
        int pool = mode ? R.P : R.H;
        put_all(bigc, big, 64, pool);
        for (uint32_t i = 0; i < 512; i++)
            vb_put(&R.s, &smlc[i], sml[i], SMALL, (uint32_t) pool, 0);
        uint32_t bad = 0;
        t[mode] = run_ops(ops, 32, devs, mode ? 3 : 1, &bad);
        b[mode] = bytes_moved();
        CHECK(t[mode] && bad == 0 && R.sim.hazards == 0, "e2e: correct inputs, no hazards");
    }
    printf("  bytes moved: naive %llu KiB, optimised %llu KiB\n", (unsigned long long) (b[0] / KiB),
           (unsigned long long) (b[1] / KiB));
    CHECK(b[1] * 2 <= b[0], "e2e: second pass reuses resident blocks (half the bytes or less)");
    report("end to end: 2 passes, 16 layers, 2 GPUs", t[0], t[1]);
}

int main(void)
{
    for (uint32_t i = 0; i < NBIG; i++) {
        for (uint32_t j = 0; j < BLK; j += 8) {
            uint64_t v = rnd();
            memcpy(&big[i][j], &v, 8);
        }
        ipfsn_cid_sha256(IPFSN_MC_RAW, big[i], BLK, &bigc[i]);
    }
    for (uint32_t i = 0; i < NSML; i++) {
        for (uint32_t j = 0; j < SMALL; j += 8) {
            uint64_t v = rnd();
            memcpy(&sml[i][j], &v, 8);
        }
        ipfsn_cid_sha256(IPFSN_MC_RAW, sml[i], SMALL, &smlc[i]);
    }
    test_cpu_backend();
    test_ipfs_glue();
    test_routing();
    test_dedup();
    test_zero_copy();
    test_batching();
    test_pipeline();
    test_eviction();
    test_placement();
    test_network();
    test_end_to_end();
    test_hazard_detector();
    test_stress();
    printf("\n=== simulated speedups (model parameters, not hardware measurements) ===\n");
    for (uint32_t i = 0; i < nsum && i < 32; i++) printf("  %s\n", summary[i]);
    printf("\ntest_vblock: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
