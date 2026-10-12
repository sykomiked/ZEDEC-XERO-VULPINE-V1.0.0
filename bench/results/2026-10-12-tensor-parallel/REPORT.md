<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# Tensor-parallel inference, first slice (2026-10-12)

The integer engine can now run one model as several processes (ranks) that
split every matrix product by output rows and every attention layer by query
head, then share the pieces with an all-gather (`kernel/src/tensor/zt_model.h`
T23, transports in `kernel/src/tensor/dist/`). Design and next steps:
[docs/HPC_DESIGN.md](../../../docs/HPC_DESIGN.md).

## Results

Model: Qwen2.5-1.5B-Instruct Q4_K_M (sha256 6a1a2eb6…, 1,117,320,736 bytes).
Machine: 4-core Intel Xeon @ 2.10 GHz (AVX-512 VNNI), 15 GB, gcc 13.3, `-O2`,
kernel set avx512vnni. Prompt: a 52-token chat prompt, then 64 greedy tokens.
All ranks ran on the same machine, joined by socketpairs. Each row is the
median of three runs. Script: [logs/tp_bench.sh](logs/tp_bench.sh); output:
[logs/tp_bench.log](logs/tp_bench.log).

| ranks x threads | prefill tok/s | generate tok/s | peak memory per rank | sent per rank |
|---|---|---|---|---|
| 1 x 1 | 18.0 | 5.4 | 968 MB | - |
| 2 x 1 | 30.6 | 9.4 | 515 MB | 178 MB |
| 4 x 1 | 39.1 | 13.5 | 281-287 MB | 267 MB |
| 1 x 2 | 33.7 | 9.6 | 968 MB | - |
| 2 x 2 | 58.5 | 15.1 | 515 MB | 178 MB |
| 1 x 4 | 60.5 | 15.2 | 968 MB | - |

- **Bits.** Every run, of every shape, printed the same logits hash
  (`zt.logits_fnv=f0e7ad334d4b4051`, FNV-1a over every logit of the prefill and
  of all 63 decode steps) and the same 64 tokens. Two ranks over TCP on
  loopback gave the same hash too ([logs/tcp_loopback.txt](logs/tcp_loopback.txt)).
- **Memory.** Each rank maps the file and reads only its rows, so its peak
  resident memory falls with the rank count: 968 MB alone, 515 MB with two,
  about 284 MB with four. This is what lets a model larger than one machine's
  memory run across several.
- **Speed.** On one 4-core machine, splitting across processes is about as
  fast as splitting across threads (2 x 2 and 1 x 4 both generate about 15
  tokens/s). It is not faster, because the cores and memory are the same.
  These numbers say the split costs little on one box. They say nothing yet
  about speed across machines: no multi-machine run has been measured.
- **Traffic.** A decode step gathers about 3.4 MB of activations (9 gathers a
  layer, 28 layers, plus the 151,936 logits), so each rank sends about
  1.7 MB a token with two ranks and 2.5 MB with four. Over a 1 Gbit/s link that
  alone caps generation near 70 tokens/s with two ranks, before latency
  (225 gathers a token). Cutting this is the next slice (docs/HPC_DESIGN.md).

## Tests

`make -C kernel verify-all` runs `test_zt_dist` (plain and under
ASan/UBSan), and the MPI build when Open MPI is installed:

- the three fixture models (qwen2, qwen3, llama), Q16 and Q8 KV caches,
  1 to 5 ranks (5 is more ranks than query heads), C reference and fastest
  SIMD with 2 threads: every rank's logits equal one process's, bit for bit;
  the ranks' multiply-add meters add up to the single-process total;
- the same over TCP on 127.0.0.1, and over MPI with 2, 3 and 4 ranks;
- models forced to saturate: the saturation count agrees on every rank;
- a rank that exits: the others return `ZT_MODEL_EDIST` instead of hanging;
- the ring all-gather alone, 2 to 7 ranks, uneven parts, parts larger than a
  socket buffer.

Mutations checked by hand: gathering attention by value instead of by head
(fails 30 checks), dropping one head per rank (fails 56), and not summing the
saturation count (fails 9).
