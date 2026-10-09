<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# VRAM Blocks: an IPFS block store in device memory

`kernel/src/vblock/` is a content-addressed block layer for machines with
GPUs. It keeps IPFS blocks in device memory as well as host memory. Each block
is keyed by its real CIDv1 from `src/ipfs_node`. Where the hardware allows it,
blocks move between CPU and GPU, between GPUs, and out to the network without
a needless trip through host RAM.

The owner's ask was: *"on advanced server cases, run IPFS nodes in VRAM as a
quick data transfer protocol for the VRAM. And use the CPU and GPU effectively
without running into the bus shuttle bottlenecks in data transmission."*

This module covers the data half of that ask. A block in VRAM is the same
block, under the same CID, that a Kubo node or a ZXV peer names. A peer can ask
for it by CID and be served straight from device memory. The store places
copies and routes transfers so that compute is not left waiting on the PCIe
bus. It is **not** a libp2p peer running on a GPU. The networking itself stays
in `ipfs_node` and Carracho. vblock is where the blocks live and how their
bytes move.

Status: built 2026-10-09. `test_vblock.c` (113 checks) passes on the CPU
reference backend and on a simulated GPU backend. **No real GPU backend exists
yet.** Every speedup below is a model result, not a measurement (§8).

---

## 1. Files and API

| File | Contents |
|---|---|
| `vblock.h` | The public API, the backend contract, and rules V1 to V9 |
| `vblock.c` | The store: CID index, pool sub-allocator, routing, dedup, zero-copy, batching, eviction, pipeline, planner, outputs, network landing and serving, `ipfs_node` glue |
| `vblock_cpu.c` | CPU reference backend: host and pinned arenas, memcpy, synchronous fences |
| `vblock_sim.c` | Simulated GPU backend: modelled lanes, bandwidth, latency and a hazard detector |
| `test_vblock.c` | Host tests and the speedup report |

The code is freestanding C11: integer only, no libc, and no allocation (the
caller supplies the entry table, each pool's range table and a scratch
buffer). The only 64-bit division is `zt_udiv64` from `src/tensor/zt.c`, and
there is no `__int128`. It compiles cleanly for `clang --target=aarch64-none-elf
-ffreestanding` at `-O0`, `-O2` and `-Os`, for `armv7-none-eabi` and for
`riscv64-linux-gnu-gcc -ffreestanding`. The only symbols it needs from
outside the module are `sha256`, `zt_udiv64` and three `ipfsn_cid_*`
functions. It does not use `memcpy`, `memset` or `__aeabi_*`.

## 2. Tiers

| Tier | Meaning | Example |
|---|---|---|
| `VB_TIER_HOST` | Pageable host RAM | `malloc`, a file mapping |
| `VB_TIER_PINNED` | Page-locked host RAM that DMA engines and NICs can read | `cudaHostAlloc`, `hipHostMalloc`, an `ibv_reg_mr` region |
| `VB_TIER_VRAM` | Device-local memory | `cudaMalloc`, `MTLStorageModePrivate`, a `DEVICE_LOCAL` Vulkan heap |
| `VB_TIER_UNIFIED` | Memory the CPU and GPU both read in place | Apple Silicon `MTLStorageModeShared`, Grace Hopper coherent memory |

A peer GPU is simply `VB_TIER_VRAM` on another device. The store asks the
backend for one arena per pool when it starts, and then sub-allocates blocks
inside that arena with 256-byte alignment. The driver's slow allocator
therefore runs once per pool, never once per block.

## 3. How it avoids the bus bottleneck (V1 to V9)

**V1. Dedup by CID.** A block that is already resident where it is needed is
never copied again. Two consumers of one CID share one copy, and also one
in-flight transfer: the second consumer waits on the first one's fence. The
same rule applies to repeated CIDs within one batch, to network receives (an
already-resident block returns `VB_HAVE`, so nothing is fetched) and to device
outputs (an identical tensor frees its new memory).

**V2. Zero-copy.** If the consuming device can read an existing copy at local
speed (`backend.access(pool, dev)`), that copy is used where it is and nothing
moves. On Apple Silicon this means a block loaded by the CPU into shared
storage is read by the GPU directly. The naive path would blit it into private
storage first.

**V3. Routing.** Transfers follow the cheapest path through the backend's link
graph. This is Dijkstra over at most 16 pools, where each hop costs
`latency + bytes / bandwidth`.

- A direct peer link (NVLink, or PCIe P2P) beats bouncing through host RAM.
- When no peer path exists, the route goes through pinned memory, not
  pageable memory.
- For pageable sources the store picks the cheaper of two options: a direct
  pageable DMA, or a memcpy into pinned memory followed by a full-speed DMA.
  The test checks that choice for sizes from 64 B to 64 MiB.
- Every intermediate hop becomes an ordinary resident copy. A block is
  therefore staged once and reused, not staged again for every consumer.

**V4. Batching.** Small blocks (64 KiB or less by default) bound for one device
are gathered into one pinned extent by host memcpy. The extent then moves to
the device in **one** DMA, so it pays one transfer latency instead of one per
block.

- An extent holds at most 168 blocks (`VB_EXTENT_BLOCKS`, the row count of one
  freight matrix) and at most `extent_bytes` (4 MiB by default).
- On the device each block keeps its own range inside the extent, so blocks
  are later evicted one at a time.
- If pinned memory has no room for the extent, the store falls back to one DMA
  per block rather than failing.
- Freight's erasure coding is not used here. A transfer inside one machine has
  nothing to recover from.

**V5. Asynchronous prefetch pipeline.** `vb_run(ops, depth)` keeps `depth` ops
in flight.

- `depth` 1 is synchronous, 2 is double buffering and 3 is triple buffering.
  While op *i* computes, the inputs of ops *i+1 … i+depth−1* are being copied.
- Every copy returns a fence, and an op's compute waits on the join of its
  inputs' fences.
- When memory is freed, its range is *retired* with the fence of its last
  reader or writer. A later allocation of overlapping bytes waits on that
  fence, so memory is never overwritten while a kernel or a DMA still uses it.

**V6. Cost-aware eviction.** When a pool is full the victim is the copy with
the lowest GreedyDual-Size value:

    H = L + refetch_ns(bytes, cheapest surviving source) / KiB

Here `refetch_ns` is "bytes × re-fetch cost", the time to bring that many
bytes back over the cheapest remaining path. Dividing by size gives the cost
per byte of space freed. `L` is an inflation value that ages out copies that
are not used again.

- A block whose other copy sits one NVLink hop away goes before a block that
  only lives behind PCIe.
- `VB_OPT_COSTEVICT` off gives LRU, for comparison.
- Copies are never evicted while they are pinned (`vb_pin`, for active model
  weights) or held (refs > 0).
- The **last** copy of a block is never evicted unless a backing source (an
  `ipfs_node`) can fetch it again. The pool reports `VB_ERR_NOMEM` instead of
  losing data.

**V7. Placement.** `vb_plan` assigns each op to a device by greedy earliest
finish:

- An op can start on a device once the device's queue is free and every
  dependency has finished, plus the time to hand off that dependency's output
  tensor.
- The op then pays the transfer of each input not already on that device,
  plus its compute time.

So ops run where their blocks and tensors already are. `vb_plan_cost` scores
any other assignment with the same model. `vb_affinity` with `vb_place_affine`
takes an access trace instead. It moves each block to the device that reads it
most often, and pins it there if asked.

**V8. Network.**

- *Serving.* `vb_serve` hands the NIC an RDMA descriptor (GPUDirect RDMA style)
  when the backend can export the device memory, so the NIC reads VRAM
  directly. Otherwise the block is staged **once** into pinned memory, where
  it stays, so the next serve copies nothing.
- *Receiving.* `vb_recv_begin` lands incoming bytes directly in the consuming
  GPU's memory when the backend allows it, and in pinned memory otherwise.
- *Privacy.* Blocks marked private (`VB_F_PRIVATE`) are never served.
- *Wire format.* Host-staged bytes can go out in the UBH-168 wire syntax
  through `ipfsn_wire_encode_block`.

**V9. Integrity.** Every block is checked against its CID before anything can
use it.

- `vb_put` hashes on the CPU.
- A network landing or a device output is hashed by the backend's device SHA-256
  hook when it sits in VRAM. Without that hook it is staged once and hashed on
  the CPU.
- A mismatch frees the landing, never registers the block, and counts
  `rejected`.
- A DMA copy between two pools inside this machine inherits the source copy's
  verification.

## 4. The backend contract

A host plugs in CUDA, Metal, Vulkan or ROCm by filling `vb_backend_t`:

| Op | Must do | CUDA | Metal | Vulkan | ROCm |
|---|---|---|---|---|---|
| `alloc` / `free` | One arena per (device, tier) | `cudaMalloc`, `cudaHostAlloc` | `newBufferWithLength:` (Private / Shared) | `vkAllocateMemory` (DEVICE_LOCAL / HOST_VISIBLE) | `hipMalloc`, `hipHostMalloc` |
| `copy_h2h/h2d/d2h/d2d/peer` | Async copy, start after `wait`, return a fence | `cudaMemcpyAsync`, `cudaMemcpyPeerAsync` on per-direction streams | `MTLBlitCommandEncoder` | `vkCmdCopyBuffer` on a transfer queue | `hipMemcpyAsync`, `hipMemcpyPeerAsync` |
| `fence_wait/done/join` | Host wait, poll, and combine two fences | `cudaEvent*`, `cudaStreamWaitEvent` | `MTLSharedEvent` | timeline semaphores | `hipEvent*` |
| `hash` (optional) | SHA-256 of device memory, computed on the device | a SHA-256 kernel | a compute shader | a compute shader | a HIP kernel |
| `map` | CPU address of host-visible pools, NULL for VRAM | host pointer | `buffer.contents` (Shared) | `vkMapMemory` | host pointer |
| `link` | Direct DMA path and its bandwidth and latency | from `cudaDeviceCanAccessPeer` and the topology | blit throughput | queue family | XGMI or PCIe |
| `access` | Can a device read a pool in place at local speed? | own VRAM; coherent memory on Grace Hopper | Shared storage: yes | own heap | own VRAM |
| `rdma_export` (optional) | Register device memory for the NIC | GPUDirect RDMA (`nvidia-peermem` or dma-buf + `ibv_reg_mr`) | none | `VK_KHR_external_memory_fd` → dma-buf → NIC | ROCm PeerDirect / dma-buf |

**GPUDirect Storage** (`cuFile`, NVMe → VRAM with no host bounce) fits behind
the backing-source hook (`vb_set_backing`). No cuFile backend is written yet.
A GDS read would need its own landing path, as network receives have, and
would still be verified by CID.

The CPU backend shows the minimum: host and pinned tiers, memcpy, fences that
are always complete. Any accelerated backend must give the same bytes for the
same calls.

## 5. Configuration

```c
vb_cfg_t cfg = {
    .be = &backend,
    .table = entries, .table_cap = 4096,   /* power of two, caller memory */
    .pools = pools, .npools = 4,           /* {dev, tier, bytes, ranges, nranges} */
    .opts = VB_OPT_ALL,                    /* or any subset; VB_OPT_NONE = naive */
    .batch_max_block = 65536,              /* V4 threshold (0 = 64 KiB) */
    .extent_bytes = 4u << 20,              /* V4 extent cap (0 = 4 MiB) */
    .scratch = buf, .scratch_cap = IPFSN_BLOCK_MAX,  /* backing fetches */
};
```

- Each device's **home** pool, where its working copies go, is its first VRAM
  pool. If it has none, the home is its unified pool, then pinned, then host.
- The first pinned pool is the **stage**. It is used for host bounces,
  batching, serving and backing fetches.
- Option bits: `VB_OPT_DEDUP`, `VB_OPT_ZEROCOPY`, `VB_OPT_P2P`,
  `VB_OPT_BATCH`, `VB_OPT_ASYNC`, `VB_OPT_COSTEVICT`. Turning them off one at a
  time is how the tests attribute each speedup.

## 6. Use with ipfs_node

- `vb_set_backing(s, ipfsn_bs_source, &bs, cost)`: a miss is fetched from an
  `ipfs_node` blockstore, or from `ipfsn_node_source` (local, then peers, then
  gateway). It is verified, put in pinned memory, and then moved. A source that
  lies is caught and the block never enters.
- `vb_ipfs_source` is an `ipfsn_get_fn` over the store. `ipfsn_cat` can
  therefore walk a UnixFS DAG whose blocks live only in VRAM. The test does
  exactly that. Each block is staged once, and a second walk copies nothing.
- `vb_output_alloc` / `vb_output_commit`: a tensor written by a kernel is
  hashed on the device and named with a CIDv1. The CID matches what Kubo
  computes for the same bytes. This is how tensors become IPFS blocks without
  leaving the GPU.

## 7. Tests

`test_vblock.c` makes 113 checks and runs in about 2 s at `-O2`. It runs clean
under `-fsanitize=address,undefined`.

What it covers:

- Both backends: put, verify, dedup, zero-copy and eviction.
- UnixFS round trips through VRAM, and the backing fetch, including a tampered
  backing block.
- Routing choices, dedup, zero-copy and batching. The batching tests cover the
  168-block extent cap and the per-block fallback.
- The pipeline at depths 1, 2 and 3. Every op checks that it read the right
  bytes on its own device.
- Eviction: cost-aware against LRU, pinning, and last copies that must not be
  lost.
- The planner and affinity placement.
- Serving: RDMA and staged, the UBH-168 envelope, and the private refusal.
- Receiving: RDMA landing with a device hash, a single flipped bit rejected,
  and pinned landing.
- Device outputs.
- A self-test proving that the hazard detector catches a missing fence.
- A stress run: 3 topologies × 400 random steps. It checks that every resident
  copy is byte-identical, that the allocator stays consistent, that no block
  is lost and that there are no hazards.

The simulator found one real bug during development. The RDMA serve path
handed out a descriptor before the copy's fence had completed, and the NIC is
not ordered by GPU fences. `vb_serve` now waits for the copy on the host
before it exports.

### Simulated speedups

The baseline is the naive host bounce, `VB_OPT_NONE` with depth 1:

- data starts in pageable host memory;
- every consumer gets its own copy;
- the host waits after every copy;
- GPU-to-GPU traffic goes through host memory;
- every small block gets its own DMA;
- eviction is LRU.

Model parameters (`test_vblock.c`):

| Link | Bandwidth | Latency |
|---|---|---|
| PCIe, pinned | 24 GB/s | 5 µs |
| PCIe, pageable | 9 GB/s | 10 µs |
| Host memcpy | 16 GB/s | 0.3 µs |
| NVLink-class | 200 GB/s | 3 µs |
| Apple shared → private blit | 200 GB/s | 3 µs |
| Device SHA-256 | 40 GB/s | 4 µs |

| Scenario | Naive | Optimised | Speedup |
|---|---|---|---|
| End to end: 2 passes, 16 layers, 2 GPUs (4 × 256 KiB + 32 × 4 KiB per layer) | 11957 µs | 2167 µs | **5.51×** |
| 1024 × 4 KiB blocks to a GPU (batched extents) | 5294 µs | 784 µs | 6.75× |
| GPU1 → GPU2, 4 MiB: peer link against host bounce | 271 µs | 69 µs | 3.92× |
| 16-layer weight streaming, 5 MiB VRAM: depth 3 against naive | 3047 µs | 1079 µs | 2.82× |
| … depth 3 against depth 1, same optimisations | 1979 µs | 1079 µs | 1.83× |
| 12-block cyclic trace, 8-block VRAM: cost-aware against LRU | 1334 µs | 474 µs | 2.81× (PCIe traffic 15 MiB → 1.5 MiB) |
| 16 ops on 2 GPUs: planner against round robin | 1032 µs | 816 µs | 1.26× (planned traffic 3008 KiB → 64 KiB) |
| Apple unified memory, 16 ops × 256 KiB: zero-copy | 389 µs | 320 µs | 1.21× (4 MiB → 0 bytes moved) |
| 16-layer pass, warm against cold (weights resident) | 1079 µs | 960 µs | 1.12× |

How to read these numbers:

- The optimised end-to-end run reaches 2167 µs. The compute floor is 32 ops ×
  60 µs = 1920 µs, so the transfers are almost entirely hidden.
- In the streaming case depth 3 equals depth 2, because the workload is
  copy-bound: the h2d lane is busy for 1019 µs and compute for 960 µs.
- Zero-copy gains little *time* on Apple, because the blit is fast. Its real
  value is that it moves zero bytes and needs no second copy of the memory.

## 8. Honest limits

- **No real GPU backend runs in this repository or its CI.** The CUDA, Metal,
  Vulkan and ROCm columns in §4 describe what a backend would call. None is
  written. The speedups come from a model with assumed parameters. Real
  numbers need hardware, and they will differ. The model leaves out driver
  submission overhead, PCIe protocol efficiency, NUMA placement, IOMMU costs,
  copy-engine counts, NVSwitch topologies and contention with other processes.
- **CPU work is partly unmodelled.** The CPU SHA-256 in `vb_put` and in
  host-memory landings is not charged to the simulated clock, and neither is
  the store's own bookkeeping. The host gather memcpy *is* charged, on the
  host memcpy lane.
- **Activations are the caller's.** `vb_run` moves CID-addressed inputs. The
  ordering of and hand-off between ops' output tensors is done by the compute
  callback; the test models it there. The tensor engine has no forward pass or
  op graph yet (`docs/TENSOR_ENGINE.md`), so the op list is supplied by hand.
- **The planner is greedy.** It is not optimal, it ignores memory capacity, and
  it handles at most 256 ops.
- **Eviction is GreedyDual-Size.** It does not count reuse frequency (GDSF
  would).
- **The allocator is first-fit over a sorted range table.** It does not
  compact. Fragmentation can force more eviction than strictly needed, and
  every operation is O(ranges).
- **One thread.** The store has no locks; one host thread drives it.
- **Copies inside the machine are trusted.** A copy made by DMA from a verified
  copy is not re-hashed. There is no paranoid mode that re-verifies after every
  transfer.
- **Device hashing is SHA-256 only.** Identity-multihash CIDs land in host
  memory and are checked there.
- **Privacy only gates serving.** `VB_F_PRIVATE` stops a block from being
  served. Device memory holds plaintext: `ipfs_node`'s sealed-at-rest
  encryption is not applied in VRAM. A private-CID-mode file's blocks should
  not be placed on a GPU that is shared with untrusted tenants.
- **No transport.** The RDMA descriptor is a hook, and there is no ibverbs
  code in the repository. RDMA serving waits on the host for the copy, because
  the NIC is not ordered by GPU fences. Stream-ordered or GPU-initiated
  networking would remove that wait.
- **Not wired in.** The module is not linked into the ARM64 image or the
  desktop app. The `verify-all` line is at the top of the test file.
