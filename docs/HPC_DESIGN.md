<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# Supercomputer (HPC) edition: design note

Status: first slice built (tensor-parallel inference across processes and
machines, `kernel/src/tensor/zt_model.h` T23). Everything below "Next slices"
is a plan, not code.

## Goal

Run one model, or many copies of one, across many nodes (a lab cluster, a
university supercomputer, or a group of ZXV desktops on the mesh) with the
same rule the local edition keeps: **integer arithmetic, and the same bits on
every machine**. A cluster run must print exactly the logits one laptop would
print. That makes every node's work checkable by any other node, which is what
lets the swarm pay for shared compute without trusting anyone.

## The invariant, and how the split keeps it

A matrix product in the engine turns each output into one exact 64-bit integer
sum, then rounds it once (`zt_kern.h`). There are two classic ways to split a
product across nodes:

1. **Split the input (reduce).** Each node sums part of every output, and the
   partial sums are added across nodes. Adding rounded Q16 partial results
   would change the bits. It works only if the nodes exchange the exact 64-bit
   sums, which doubles the bytes and couples the wire format to the kernel.
2. **Split the outputs (gather).** Each node computes whole outputs, every
   one with the same kernel and the same full input as a single process, and
   the outputs are copied to everyone. Nothing is ever added across nodes.

ZXV uses (2). Rank r of P computes rows `[n r / P, n (r + 1) / P)` of every
matrix, and query heads in the same proportion for attention. After each
product an all-gather gives every rank the full vector. Norms, RoPE, the KV
cache, SiLU and residuals are repeated on every rank, so ranks stay identical
without a broadcast. The only value summed across ranks is the saturation
count, which is an integer count.

Consequence: any rank count, transport, kernel set (C, AVX2, AVX-512, NEON)
and thread count gives the same logits. Tested for 1 to 5 ranks on three
architectures, over socketpairs, TCP and MPI
(`kernel/src/tensor/dist/test_zt_dist.c`), and measured on Qwen2.5-1.5B
(`bench/results/2026-10-12-tensor-parallel/REPORT.md`).

## What is built (slice 1)

| Piece | Where | What it does |
|---|---|---|
| Model hook | `zt_model.h` T23, `zt_model_t.dist` | splits products by rows and attention by heads, calls `allgather` after each; freestanding (kernel images can carry it, but never link a transport) |
| Ring transport | `tensor/dist/zt_dist.h`, `zt_dist.c` | ring all-gather over any pair of stream fds; `zt_dist_fork_ring` (one machine), `zt_dist_tcp_ring` (one rank per host) |
| MPI transport | `tensor/dist/zt_dist_mpi.h`, `zt_dist_mpi.c` | `MPI_Allgatherv` plus an exact integer `MPI_Allreduce`; build with `mpicc` |
| Driver | `bench/tensor/zt_bench.c` | `--ranks P` on one machine, `--tcp RANK HOSTS PORT` across machines |
| Meter | `zt_model_state_t.dist_macs` | exact multiply-adds each rank computed: the unit for settling shared compute |

Measured on a 4-core Xeon (same machine, so no network): identical logits for
every shape; peak memory per rank 968 MB alone, 515 MB with two ranks, about
284 MB with four; two ranks of two threads generate about as fast as one
process of four threads (15.1 against 15.2 tokens/s).

## Transports: MPI or the PQ mesh

They serve different networks, so ZXV keeps both behind the same
`zt_dist_t` interface:

- **MPI** for a real supercomputer: the fabric (InfiniBand, Slingshot) and the
  scheduler (Slurm `srun`) are there already, and MPI's collectives are tuned
  for them. Nodes trust each other; the job is speed.
- **Plain TCP ring** for a trusted LAN of desktops, with no MPI to install.
- **PQ mesh** for nodes that do not trust each other (the swarm on Vinea).
  Each ring link becomes a Vinea session (`vna_session.h`: X25519 + ML-KEM-1024
  key exchange, ML-DSA-87 identities, ChaCha20-Poly1305 records), the same
  handshake the two-node demo uses (`kernel/arch/hosted/zxv_peer_host.h`).
  Encryption costs CPU on every gathered byte, so tensor parallelism over the
  mesh only makes sense on a fast LAN; across the internet, pipeline
  parallelism (below) moves far fewer bytes.

## Netting: paying for shared compute

Because results are deterministic, a run can be settled exactly:

1. Each rank's `dist_macs` is an exact integer, and the partition is fixed by
   (rows, rank count), so every rank can compute every other rank's meter
   itself. Nobody has to trust a reported number.
2. Each node signs a receipt per job: job id, model hash, prompt hash, logits
   hash, its meter. Any node can rerun any slice and compare hashes, since a
   correct node always produces the same bits. A wrong result is provable.
3. At the end of an epoch the obligations between nodes are netted
   multilaterally (each node's net = what it is owed minus what it owes, which
   sums to zero across the group) and only the net amounts are posted, with
   the 0.08889% assurance fee and its 50/25/15/10 split, through the same
   `pay_ledger_pay_with_fee` path the two-node demo uses.

None of this netting is built yet. The meter is.

## Honest limits of slice 1

- The KV cache, norms and other per-token steps are repeated on every rank,
  so only weight memory is split.
- Traffic is high: 9 all-gathers a layer (Q, K, V, attention, output, gate,
  up, down) plus the logits, about 3.4 MB a token for Qwen2.5-1.5B, in 225
  round trips. Fine inside one box or over InfiniBand; slow over Ethernet.
- No multi-machine run has been measured yet. Bit-identity across machines
  rests on the engine's existing cross-architecture result (x86 and arm64 give
  the same bits) and the wire format being explicit little-endian, but a mixed
  Mac + Linux cluster has not been tried.
- The TCP ring has no encryption or authentication: trusted networks only.
- MPI was tested with Open MPI on one machine, not on a cluster.

## Next slices

1. **Cut the traffic.** Split Q, K and V by head so each rank keeps its own
   heads' Q and its share of the KV cache (no Q gather, KV memory split too);
   apply SiLU to each rank's own gate/up rows before gathering (half the
   gate/up bytes); gather Q/K/V once and gate/up once. About 4 gathers a layer
   instead of 9.
2. **Pipeline parallelism.** Give each node a band of layers; only the 1536
   residual values a token cross a node boundary (6 KB). Right for the mesh
   and for models bigger than a whole cluster's per-node memory budget.
   Combine with (1) inside each node or rack.
3. **Batch serving.** Many sequences at once (data parallelism on top), the
   way a supercomputer would actually be used for a swarm.
4. **PQ mesh transport** as above, plus a `zt_dist` over Vinea sessions.
5. **Netting and receipts** as above.
6. **Launchers.** A Slurm script and an `mpirun` driver beside `zt_bench`, and
   a `zxvd` RPC so the desktop app can join or host a group.
