/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vblock.h — a content-addressed block layer for device memory.
 *
 * WHAT THIS IS
 * ------------
 * An IPFS-compatible block store whose blocks live in memory TIERS: pageable
 * host RAM, pinned (page-locked, DMA-able) host RAM, GPU VRAM, unified memory
 * that the CPU and GPU both address (Apple Silicon), and the VRAM of peer
 * GPUs. A block's key is its real CIDv1 (src/ipfs_node), so a block in VRAM is
 * the same block a Kubo node or a ZXV peer names, and a peer can ask for it by
 * CID and be served straight out of device memory.
 *
 * The store decides WHERE a block's copies live and HOW bytes move, so that
 * CPU and GPU work does not stall on the host bus:
 *
 *   V1 DEDUP BY CID.  A block already resident where it is needed is never
 *      copied again; two consumers of one CID share one copy (and one
 *      in-flight transfer).
 *   V2 ZERO-COPY.  If the consuming device can read a copy at local speed
 *      (unified memory, MTLStorageModeShared), it is used in place: no
 *      transfer at all.
 *   V3 ROUTING.  Transfers take the cheapest path through the backend's link
 *      graph (Dijkstra over pools, cost = latency + bytes / bandwidth). A
 *      direct peer path (NVLink, PCIe P2P) wins over bouncing through host
 *      RAM; intermediate hops become ordinary resident copies, so a block is
 *      staged once and reused.
 *   V4 BATCHING.  Small blocks bound for one device are gathered into one
 *      pinned extent (at most VB_EXTENT_BLOCKS = 168 blocks, the freight row
 *      count, or extent_bytes) and moved in ONE DMA, paying one transfer
 *      latency instead of one per block.
 *   V5 ASYNC PIPELINE.  vb_run keeps `depth` ops in flight (2 = double
 *      buffering, 3 = triple): inputs for op i+depth-1 are copied while op i
 *      computes, with fences ordering every read after its write and every
 *      reuse of memory after its last reader.
 *   V6 COST EVICTION.  When a pool is full the victim is the copy with the
 *      lowest GreedyDual-Size value: re-fetch time (latency + bytes / the
 *      bandwidth of the cheapest surviving source) per byte freed, aged by
 *      an inflation value. Pinned copies (active model weights) and copies in
 *      use are never evicted; the last copy of a block is only evicted when a
 *      backing source (an ipfs_node) can fetch it again.
 *   V7 PLACEMENT.  vb_plan assigns ops to devices so that ops run where the
 *      blocks and tensors they read already are (greedy list scheduling with
 *      communication cost); vb_affinity / vb_place_affine place blocks on the
 *      device a supplied access trace says consumes them most.
 *   V8 NETWORK.  vb_serve hands a peer a GPUDirect-RDMA style descriptor when
 *      the backend can export the device memory to the NIC; otherwise the
 *      block is staged ONCE into pinned host memory (and kept there). Blocks
 *      arriving from the network land directly in VRAM when the backend
 *      allows it, and are hashed (on the device if the backend can) and
 *      checked against their CID before anything can use them.
 *   V9 INTEGRITY.  Every copy is verified against its CID before it is
 *      usable: by the CPU for host-visible memory, by the backend's device
 *      hash hook (or one staging copy) for VRAM. DMA between pools inside
 *      this machine inherits the source copy's verification.
 *
 * BACKENDS
 * --------
 * vb_backend_t is a function-pointer table (arena alloc/free, copies
 * h2h/h2d/d2h/d2d/peer with fences, fence wait/poll/join, an optional device
 * hash, link and access topology, optional RDMA export). CUDA, Metal, Vulkan
 * or ROCm hosts plug in there. This directory ships two:
 *   - vb_cpu_*: the CPU reference backend (host and pinned tiers, memcpy,
 *     synchronous fences);
 *   - vb_sim_*: a simulated GPU backend. Device memory is caller RAM, copies
 *     really move the bytes, and time is a modelled clock with per-lane
 *     PCIe / NVLink / unified-memory bandwidth and latency. It also detects
 *     read-before-write and write-while-reading hazards, so tests prove the
 *     fences are right, not just fast.
 * No real GPU backend runs in this repository's CI; simulated speedups are
 * model results, and real numbers need hardware (docs/VRAM_BLOCKS.md).
 *
 * RULES. Freestanding C11: integer only, no libc, no allocation (every table
 * is supplied by the caller), no 64-bit division except through zt_udiv64
 * (src/tensor/zt.c), no __int128.
 */
#ifndef ZXV_VBLOCK_H
#define ZXV_VBLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include "../ipfs_node/ipfs_node.h"

/* ===== Results ============================================================ */

enum {
    VB_OK = 0,
    VB_HAVE = 1,            /* vb_recv_begin: already resident, do not fetch   */
    VB_ERR_ARG = -1,        /* bad argument                                    */
    VB_ERR_NOTFOUND = -2,   /* no such block (and no backing source has it)    */
    VB_ERR_HASH = -3,       /* bytes do not match the CID                      */
    VB_ERR_FULL = -4,       /* the caller's entry or range table is full       */
    VB_ERR_NOMEM = -5,      /* pool full and nothing can be evicted            */
    VB_ERR_NOROUTE = -6,    /* no path between the pools                       */
    VB_ERR_BACKEND = -7,    /* a backend operation failed                      */
    VB_ERR_PRIVATE = -8,    /* refused: the block is private                   */
    VB_ERR_UNSUPP = -9,     /* not supported by this backend or CID            */
    VB_ERR_UNVERIFIED = -10 /* a copy exists but has not been verified         */
};

/* ===== Geometry =========================================================== */

#define VB_MAX_POOLS     16u
#define VB_MAX_DEVS      8u
#define VB_MAX_COPIES    6u
#define VB_ALIGN         256u /* placement alignment inside a pool (GPU-friendly) */
#define VB_EXTENT_BLOCKS 168u /* blocks per batched extent: one freight's rows   */
#define VB_RETIRED       32u  /* freed ranges per pool still waiting on a fence  */
#define VB_DEV_ANY       0xFFu
#define VB_NONE          0xFFu

/* Tiers. "Peer GPU" is VB_TIER_VRAM on another device. */
#define VB_TIER_HOST    0u /* pageable host RAM                                 */
#define VB_TIER_PINNED  1u /* page-locked host RAM (cudaHostAlloc, DMA source)   */
#define VB_TIER_VRAM    2u /* device-local memory (cudaMalloc, MTLStorageModePrivate) */
#define VB_TIER_UNIFIED 3u /* CPU and GPU both address it (MTLStorageModeShared) */
#define VB_TIERS        4u

/* Optimisations; VB_OPT_NONE is the naive host-bounce baseline. */
#define VB_OPT_DEDUP     0x01u /* V1 */
#define VB_OPT_ZEROCOPY  0x02u /* V2 */
#define VB_OPT_P2P       0x04u /* V3: allow device-to-device edges            */
#define VB_OPT_BATCH     0x08u /* V4 */
#define VB_OPT_ASYNC     0x10u /* V5: no host wait after each transfer        */
#define VB_OPT_COSTEVICT 0x20u /* V6 (otherwise LRU)                          */
#define VB_OPT_NONE      0x00u
#define VB_OPT_ALL       0x3Fu

/* Transfer kinds, for stats and for picking the backend op. */
#define VB_K_H2H  0u
#define VB_K_H2D  1u
#define VB_K_D2H  2u
#define VB_K_D2D  3u
#define VB_K_PEER 4u
#define VB_KINDS  5u

/* An opaque completion token. 0 is always complete. The store only waits on,
 * polls and joins fences through the backend. */
typedef uint64_t vb_fence_t;

/* ===== Backend ============================================================ */

typedef struct {
    uint32_t mbps;   /* sustained bandwidth, 10^6 bytes per second */
    uint32_t lat_ns; /* fixed cost per transfer (launch + DMA setup) */
} vb_link_t;

/* One transfer between two backend pool handles. */
typedef struct {
    uint32_t src, dst; /* backend pool handles */
    uint64_t src_off, dst_off, len;
    vb_fence_t wait; /* may not start before this fence */
} vb_xfer_t;

/* A device-memory region exported to a NIC (GPUDirect RDMA style): the NIC
 * reads or writes it without the bytes touching host RAM. */
typedef struct {
    uint64_t addr; /* bus / IOVA address the NIC uses */
    uint32_t key;  /* registration key (ibverbs rkey, or backend-defined) */
    uint32_t pool; /* backend pool handle */
    uint64_t off;
    uint64_t len;
} vb_rdma_t;

typedef int (*vb_copy_fn)(void *ctx, const vb_xfer_t *x, vb_fence_t *done);

typedef struct {
    void *ctx;
    const char *name;
    /* Reserve one arena of `bytes` in (dev, tier); the store sub-allocates
     * blocks inside it, so the (slow) driver allocator runs once per pool. */
    int (*alloc)(void *ctx, uint32_t dev, uint32_t tier, uint64_t bytes, uint32_t *pool);
    void (*free)(void *ctx, uint32_t pool);
    /* Asynchronous copies; *done completes when the bytes are in place. The
     * store picks the op from the two pools' tiers and devices. */
    vb_copy_fn copy_h2h, copy_h2d, copy_d2h, copy_d2d, copy_peer;
    /* Optional: SHA-256 of device memory, computed on the device. The digest
     * is valid after *done. NULL: the store stages to host and hashes there. */
    int (*hash)(void *ctx, uint32_t pool, uint64_t off, uint64_t len, vb_fence_t wait,
                uint8_t digest[32], vb_fence_t *done);
    int (*fence_wait)(void *ctx, vb_fence_t f);
    bool (*fence_done)(void *ctx, vb_fence_t f);
    vb_fence_t (*fence_join)(void *ctx, vb_fence_t a, vb_fence_t b);
    /* CPU address of a host-visible pool (host, pinned, unified), NULL else. */
    uint8_t *(*map)(void *ctx, uint32_t pool);
    /* Is there a direct DMA path src -> dst? VB_OK with its cost, or
     * VB_ERR_NOROUTE. */
    int (*link)(void *ctx, uint32_t src, uint32_t dst, vb_link_t *out);
    /* Can `dev` read `pool` in place at local speed? (zero-copy) */
    bool (*access)(void *ctx, uint32_t pool, uint32_t dev);
    /* Optional: export a region to the NIC. NULL or an error: not supported. */
    int (*rdma_export)(void *ctx, uint32_t pool, uint64_t off, uint64_t len, vb_rdma_t *out);
    uint64_t (*now_ns)(void *ctx); /* for reporting only */
} vb_backend_t;

/* ===== Store ============================================================== */

/* Range record: one allocation inside a pool, kept sorted by offset. */
typedef struct {
    uint64_t off, len;
    uint32_t entry;
    uint8_t slot;
    uint8_t pad[3];
} vb_range_t;

typedef struct {
    uint64_t off, len;
    vb_fence_t fence; /* the range may be reused once this completes */
} vb_retired_t;

#define VB_C_VERIFIED 0x01u
#define VB_C_PINNED   0x02u
#define VB_C_TEMP     0x04u /* no-dedup baseline copy, freed on release */

typedef struct {
    uint64_t off;
    vb_fence_t ready; /* data valid here after this fence */
    vb_fence_t busy;  /* last reader done after this fence */
    uint64_t prio;    /* GreedyDual-Size value, or LRU tick */
    uint16_t refs;    /* acquired and not yet released */
    uint8_t pool;     /* store pool index; VB_NONE = empty slot */
    uint8_t flags;    /* VB_C_* */
} vb_copy_t;

#define VB_F_PRIVATE 0x01u /* never served to peers */

typedef struct {
    ipfsn_cid_t cid; /* normalised to v1 */
    uint32_t len;
    uint8_t state; /* 0 empty, 1 used, 2 tombstone */
    uint8_t flags; /* VB_F_* */
    uint8_t plan;  /* vb_plan scratch: bit d = will be resident on dev d */
    uint8_t pad;
    uint16_t acc[VB_MAX_DEVS]; /* vb_affinity counts */
    vb_copy_t c[VB_MAX_COPIES];
} vb_entry_t;

typedef struct {
    uint32_t dev, tier;
    uint64_t bytes;
    vb_range_t *ranges; /* caller table for this pool's allocations */
    uint32_t nranges;
} vb_pool_cfg_t;

typedef struct {
    const vb_backend_t *be;
    vb_entry_t *table;
    uint32_t table_cap; /* power of two */
    const vb_pool_cfg_t *pools;
    uint32_t npools;
    uint32_t opts;            /* VB_OPT_* */
    uint32_t batch_max_block; /* blocks up to this size are batched; 0 -> 65536 */
    uint64_t extent_bytes;    /* bytes per batched transfer; 0 -> 4 MiB */
    uint8_t *scratch;         /* host buffer for backing fetches, >= IPFSN_BLOCK_MAX */
    uint32_t scratch_cap;
} vb_cfg_t;

typedef struct {
    uint32_t h; /* backend handle */
    uint8_t dev, tier;
    uint8_t *cpu; /* map() or NULL */
    uint64_t cap, used;
    vb_range_t *r;
    uint32_t nr, rcap;
    vb_retired_t ret[VB_RETIRED];
    uint32_t nret;
    uint64_t L; /* GreedyDual inflation */
} vb_pool_t;

typedef struct {
    uint64_t bytes[VB_KINDS]; /* bytes moved per transfer kind */
    uint64_t xfers[VB_KINDS]; /* transfers per kind */
    uint64_t dedup_hits;      /* resident copy reused (V1) */
    uint64_t zero_copy_hits;  /* used in place on another pool (V2) */
    uint64_t multi_hop;       /* transfers that needed a staging hop */
    uint64_t batches, batched_blocks;
    uint64_t evictions, evicted_bytes;
    uint64_t backing_fetches;
    uint64_t hashed_dev_bytes, hashed_cpu_bytes;
    uint64_t rdma_serves, staged_serves, host_serves;
    uint64_t rdma_landings, rejected;
    uint64_t host_waits;
} vb_stats_t;

typedef struct {
    const vb_backend_t *be;
    vb_entry_t *tab;
    uint32_t cap, count;
    vb_pool_t pool[VB_MAX_POOLS];
    uint32_t npools;
    uint8_t home[VB_MAX_DEVS]; /* where a device's working copies go */
    uint8_t stage;             /* pinned staging pool, VB_NONE if none */
    uint8_t ndevs;             /* 1 + highest device id */
    uint8_t lok[VB_MAX_POOLS][VB_MAX_POOLS];
    vb_link_t lnk[VB_MAX_POOLS][VB_MAX_POOLS];
    uint8_t acc[VB_MAX_POOLS][VB_MAX_DEVS];
    uint32_t opts;
    uint32_t batch_max_block;
    uint64_t extent_bytes;
    uint64_t tick;
    ipfsn_get_fn backing;
    void *backing_ctx;
    vb_link_t backing_cost;
    uint8_t *scratch;
    uint32_t scratch_cap;
    vb_stats_t st;
} vb_store_t;

/* A held block: valid to read at `pool`/`off` once `ready` completes. */
typedef struct {
    uint32_t entry;
    uint8_t slot;
    uint8_t pool; /* store pool index */
    uint8_t pad[2];
    uint32_t backend_pool;
    uint64_t off;
    uint32_t len;
    vb_fence_t ready;
} vb_ref_t;

int vb_init(vb_store_t *s, const vb_cfg_t *cfg);
void vb_fini(vb_store_t *s);
/* Index of the first pool on (dev, tier), or -1. */
int vb_pool_index(const vb_store_t *s, uint32_t dev, uint32_t tier);
/* Optional source for blocks that are nowhere in the store (an ipfs_node
 * blockstore, ipfsn_node_source, a gateway). `cost` models how long a fetch
 * takes, for eviction. Fetched blocks are verified and land in pinned memory. */
void vb_set_backing(vb_store_t *s, ipfsn_get_fn get, void *ctx, vb_link_t cost);

/* Store a block from host memory into a host-visible pool. The bytes are
 * checked against the CID first. A block already in that pool is not copied
 * again (VB_OK). flags: VB_F_PRIVATE. */
int vb_put(vb_store_t *s, const ipfsn_cid_t *cid, const uint8_t *data, uint32_t len, uint32_t pool,
           uint32_t flags);
bool vb_has(vb_store_t *s, const ipfsn_cid_t *cid);
/* Number of copies, and whether one is in `pool` (pool may be VB_NONE). */
uint32_t vb_copies(vb_store_t *s, const ipfsn_cid_t *cid, uint32_t pool, bool *in_pool);

/* Make the block readable by `dev` and hold it. Applies V1-V3 per opts.
 * Release with the fence after which the reader is done. */
int vb_acquire(vb_store_t *s, const ipfsn_cid_t *cid, uint32_t dev, vb_ref_t *ref);
/* The same for n blocks, batching small ones (V4). On error none are held. */
int vb_acquire_many(vb_store_t *s, const ipfsn_cid_t *cids, uint32_t n, uint32_t dev,
                    vb_ref_t *refs);
void vb_release(vb_store_t *s, vb_ref_t *ref, vb_fence_t busy);

/* Pin a copy on dev (active model weights): never evicted until unpinned. */
int vb_pin(vb_store_t *s, const ipfsn_cid_t *cid, uint32_t dev);
int vb_unpin(vb_store_t *s, const ipfsn_cid_t *cid, uint32_t dev);
/* Drop every unpinned, unused copy in a pool (tests, memory pressure). */
uint32_t vb_drop_pool(vb_store_t *s, uint32_t pool);

/* Cheapest path for `len` bytes from pool a to pool b under the store's
 * opts (V3). hops[0] = a ... hops[n-1] = b. Returns VB_OK. */
int vb_route(const vb_store_t *s, uint32_t a, uint32_t b, uint64_t len, uint8_t *hops,
             uint32_t *nhops, uint64_t *cost_ns);
/* Transfer time of one hop under the link model. */
uint64_t vb_link_ns(vb_link_t l, uint64_t len);

/* ===== Pipeline (V5) and placement (V7) =================================== */

typedef struct {
    const ipfsn_cid_t *in; /* blocks the op reads */
    uint32_t nin;
    uint32_t dev;         /* device, or VB_DEV_ANY for vb_plan to choose */
    uint64_t cost_ns;     /* compute estimate, for vb_plan */
    const uint32_t *deps; /* ops whose output tensor this op reads */
    uint32_t ndeps;
    uint32_t out_len; /* bytes of this op's output tensor */
} vb_op_t;

/* Run op `op` on `dev` reading `refs` once `ready` completes; set *done to
 * the fence after which it has finished. On a real backend this launches a
 * kernel; in tests it calls vb_sim_compute. */
typedef int (*vb_compute_fn)(void *ctx, uint32_t op, uint32_t dev, const vb_ref_t *refs,
                             uint32_t nrefs, vb_fence_t ready, vb_fence_t *done);

/* Execute ops in order with `depth` ops in flight (1 = synchronous, 2 =
 * double buffered, 3 = triple). devs[i] overrides ops[i].dev when non-NULL.
 * scratch holds depth * max_in refs. *last = the final op's done fence. */
int vb_run(vb_store_t *s, const vb_op_t *ops, uint32_t nops, const uint8_t *devs, uint32_t depth,
           vb_ref_t *scratch, uint32_t max_in, vb_compute_fn fn, void *ctx, vb_fence_t *last);

/* Assign a device to every op (fixed ops keep theirs): greedy earliest
 * finish. An op on a candidate device starts after that device's queue and
 * after each dependency's finish plus the hand-off of its output tensor; it
 * then pays the transfers of every input not already there and its compute.
 * dev_mask bit d allows dev d. out[i] = device. *xfer_bytes = bytes the plan
 * moves. Device memory capacity is not modelled. At most VB_PLAN_MAX ops. */
#define VB_PLAN_MAX 256u
int vb_plan(vb_store_t *s, const vb_op_t *ops, uint32_t nops, uint32_t dev_mask, uint8_t *out,
            uint64_t *xfer_bytes, uint64_t *xfer_ns);
/* Bytes and modelled transfer time of a given assignment (same cost model). */
int vb_plan_cost(vb_store_t *s, const vb_op_t *ops, uint32_t nops, const uint8_t *devs,
                 uint64_t *xfer_bytes, uint64_t *xfer_ns);

typedef struct {
    const ipfsn_cid_t *cid;
    uint32_t dev;
} vb_access_t;

/* Count accesses per device from a trace. */
int vb_affinity(vb_store_t *s, const vb_access_t *trace, uint32_t n);
/* Move every traced block to its most frequent consumer's home pool (pin if
 * asked) and clear the counts. Returns blocks placed, or negative. */
int vb_place_affine(vb_store_t *s, bool pin);

/* ===== Device outputs, network (V8), ipfs_node glue ======================= */

#define VB_LAND_HOST 1u /* landed in pinned host memory: write via ptr */
#define VB_LAND_RDMA 2u /* landed in device memory: the NIC writes via rdma */
#define VB_LAND_DEV  3u /* a kernel writes it in device memory (outputs) */

typedef struct {
    ipfsn_cid_t cid; /* receive: the CID expected */
    uint8_t pool;    /* store pool index */
    uint8_t kind;    /* VB_LAND_* */
    uint8_t pad[2];
    uint32_t len;
    uint64_t off;
    vb_fence_t wait; /* the writer (NIC or kernel) must start after this */
    uint8_t *ptr;    /* VB_LAND_HOST: CPU address */
    vb_rdma_t rdma;  /* where the NIC or kernel writes; addr/key set for RDMA */
} vb_landing_t;

/* Reserve device memory for a tensor a kernel will write on `dev`. */
int vb_output_alloc(vb_store_t *s, uint32_t dev, uint32_t len, vb_landing_t *l);
/* After the kernel (done after `written`): hash on the device, name the block
 * with a CIDv1 of `codec`, and register it. If the CID is already resident in
 * that pool the new memory is freed (dedup). */
int vb_output_commit(vb_store_t *s, vb_landing_t *l, uint32_t codec, vb_fence_t written,
                     ipfsn_cid_t *cid);

/* Begin receiving a block from a peer for `dev`. VB_HAVE: already resident,
 * nothing to fetch. Otherwise the landing says where the bytes go: straight
 * into dev's memory by RDMA when the backend can, else pinned host memory. */
int vb_recv_begin(vb_store_t *s, const ipfsn_cid_t *cid, uint32_t len, uint32_t dev,
                  vb_landing_t *l);
/* The bytes are in place after `arrived`. Verified against the CID (device
 * hash, or CPU for host memory); a mismatch frees the landing, VB_ERR_HASH. */
int vb_recv_end(vb_store_t *s, vb_landing_t *l, vb_fence_t arrived);
void vb_recv_abort(vb_store_t *s, vb_landing_t *l);

#define VB_SERVE_HOST 1u /* ptr/len in host-visible memory */
#define VB_SERVE_RDMA 2u /* the NIC reads device memory through `rdma` */

typedef struct {
    uint32_t kind;
    const uint8_t *ptr;
    uint32_t len;
    vb_rdma_t rdma;
    vb_ref_t ref;
} vb_serve_t;

/* Serve a block to a peer. Private blocks are refused. Device memory goes
 * out by RDMA when the backend exports it; otherwise it is staged once into
 * pinned memory, which stays resident so the next serve copies nothing. */
int vb_serve(vb_store_t *s, const ipfsn_cid_t *cid, vb_serve_t *out);
/* The NIC is done reading after `sent`. */
void vb_serve_done(vb_store_t *s, vb_serve_t *sv, vb_fence_t sent);

/* ipfsn_get_fn over the store (ctx = vb_store_t *): a DAG whose blocks live
 * in VRAM can be walked by ipfsn_cat. */
int vb_ipfs_source(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len);

/* ===== Reference backends ================================================= */

/* CPU reference backend: host and pinned tiers on device 0, memcpy copies,
 * synchronous fences. Arenas come from caller memory. */
#define VB_CPU_ARENAS 8u
typedef struct {
    uint8_t *mem[VB_CPU_ARENAS];
    uint64_t cap[VB_CPU_ARENAS];
    uint8_t tier[VB_CPU_ARENAS];
    uint8_t taken[VB_CPU_ARENAS];
    uint32_t n;
} vb_cpu_t;

void vb_cpu_init(vb_cpu_t *c);
int vb_cpu_add_arena(vb_cpu_t *c, uint32_t tier, uint8_t *mem, uint64_t cap);
void vb_cpu_backend(vb_cpu_t *c, vb_backend_t *be);

/* Simulated GPU backend. Each region is caller RAM standing in for a pool on
 * (dev, tier). Links have bandwidth, latency and a LANE: transfers on the
 * same lane queue behind each other (one PCIe direction, one NVLink
 * direction, the host memcpy engine); different lanes overlap. Compute and
 * device hashing have their own lanes per device. Time is a modelled clock in
 * nanoseconds; a fence is the completion time. */
#define VB_SIM_LANES     64u
#define VB_SIM_LANE_COMP 48u /* + dev: compute lane */
#define VB_SIM_LANE_HASH 56u /* + dev: device hash lane */
#define VB_SIM_TRACK     512u

#define VB_DEVK_CPU     0u
#define VB_DEVK_GPU     1u
#define VB_DEVK_UNIFIED 2u /* integrated GPU sharing memory with the CPU */

typedef struct {
    uint64_t off, len, t0, t1;
    uint8_t pool, write;
} vb_sim_ev_t;

typedef struct {
    uint8_t *mem[VB_MAX_POOLS];
    uint64_t cap[VB_MAX_POOLS];
    uint8_t dev[VB_MAX_POOLS], tier[VB_MAX_POOLS], taken[VB_MAX_POOLS];
    uint8_t rdma[VB_MAX_POOLS];
    uint32_t n;
    uint8_t devkind[VB_MAX_DEVS];
    uint8_t lok[VB_MAX_POOLS][VB_MAX_POOLS];
    vb_link_t lnk[VB_MAX_POOLS][VB_MAX_POOLS];
    uint8_t lane[VB_MAX_POOLS][VB_MAX_POOLS];
    uint8_t acc[VB_MAX_POOLS][VB_MAX_DEVS];
    vb_link_t hash_rate[VB_MAX_DEVS]; /* device SHA-256 throughput; 0 mbps = none */
    uint64_t lane_free[VB_SIM_LANES];
    uint64_t lane_busy[VB_SIM_LANES]; /* accumulated busy ns per lane */
    uint64_t now;
    vb_sim_ev_t ev[VB_SIM_TRACK]; /* recent reads and writes, for hazards */
    uint32_t nev;
    uint64_t hazards;
    uint64_t copies;
} vb_sim_t;

void vb_sim_init(vb_sim_t *m);
void vb_sim_dev(vb_sim_t *m, uint32_t dev, uint32_t kind);
/* Register memory for (dev, tier). Returns the region index or negative. */
int vb_sim_region(vb_sim_t *m, uint32_t dev, uint32_t tier, uint8_t *mem, uint64_t cap);
/* Directed link between regions a and b on `lane` (0..47). */
void vb_sim_link(vb_sim_t *m, uint32_t a, uint32_t b, uint32_t mbps, uint32_t lat_ns,
                 uint32_t lane);
void vb_sim_access(vb_sim_t *m, uint32_t region, uint32_t dev, bool yes);
void vb_sim_rdma(vb_sim_t *m, uint32_t region, bool yes);
void vb_sim_hash_rate(vb_sim_t *m, uint32_t dev, uint32_t mbps, uint32_t lat_ns);
void vb_sim_backend(vb_sim_t *m, vb_backend_t *be);
/* Model a kernel on dev: starts at max(now, wait, compute lane free), runs
 * cost_ns, reads `refs` (hazard-checked against pending writes). */
int vb_sim_compute(vb_sim_t *m, uint32_t dev, vb_fence_t wait, uint64_t cost_ns,
                   const vb_ref_t *refs, uint32_t nrefs, vb_fence_t *done);
/* Model a NIC reading or writing a region (for serve / receive tests). */
vb_fence_t vb_sim_nic(vb_sim_t *m, uint32_t region, uint64_t off, uint64_t len, bool write,
                      uint32_t mbps, uint32_t lat_ns, uint32_t lane);

#endif /* ZXV_VBLOCK_H */
