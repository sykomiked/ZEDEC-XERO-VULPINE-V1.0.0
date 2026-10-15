<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# GPU strategy: GPU-first, integer-exact

ZXV is not a pure-CPU architecture. The GPU does the heavy arithmetic, and the
CPU coordinates. Every GPU kernel computes the same integers as the CPU's C
reference, so an answer is still bit for bit the same on every device and can
still be signed and re-checked (rule R1).

The owner's words:

> "the GPU strategy, to use the GPU and not have a pure CPU architecture is
> critical" (2026-10-14)

> "on advanced server cases, run IPFS nodes in VRAM as a quick data transfer
> protocol for the VRAM. And use the CPU and GPU effectively without running
> into the bus shuttle bottlenecks in data transmission." (quoted in
> [VRAM_BLOCKS.md](VRAM_BLOCKS.md))

## 1. The rules

| # | Rule | Why |
|---|---|---|
| G1 | **The GPU is the default compute device for model work** wherever one exists. The CPU path is the reference and the fallback, not the target | The owner's ask; a model's arithmetic is mostly dense products, which GPUs do best |
| G2 | **Integer-exact on every backend.** A GPU kernel is a port of the C reference's integer steps, shared as source where it can be (`zt_metal_math.h` is compiled both as C and as Metal) | Same bits on CPU and GPU, so R1 (bit-identical results) and signed answers survive the move |
| G3 | **No bus shuttle.** Data stays on the device for the whole job; the CPU hands over inputs once and reads results once. Round trips per product are a defect, not a design | The owner's "bus shuttle bottlenecks" |
| G4 | **One op list, many backends.** The forward pass is described once (`zt_gpu.c`) as a list of kernel launches; each backend (Metal, then Vulkan, CUDA) only allocates memory and runs the list | New GPUs cost a backend, not a rewrite |
| G5 | **Proven without a GPU.** Every op list also runs through a C backend in CI, against the CPU reference, so a GPU-only bug is a backend bug | CI runners have no GPU |
| G6 | **Measured, never promised** (R9). Speed numbers come from a real run on named hardware | |

The kernel images themselves stay integer-only and CPU-only (R1): the GPU
backends are hosted code (the Mac app, bench, servers).

## 2. Where it stands (2026-10-15)

| Part | Status | Where |
|---|---|---|
| Matrix products on an Apple GPU, bit-exact | ✅ merged (#85) | `kernel/src/tensor/zt_metal.m`, `zt_kern_t.mm_full` |
| The whole forward pass on the GPU: every layer's RMS norm, activation quantiser, products and bias, RoPE, KV store, attention, residual and SiLU, then the output norm and logits, in one command buffer | 🧩 built; bit-exact on the M3 Max; prompt reading 2.3× the CPU, generation not yet faster | `zt_gpu.h`, `zt_gpu.c`, `zt_kern_metal_resident`, `zt_bench --gpu-resident` |
| The same op list through a C backend | ✅ in CI | `test_zt_gpu` (fixtures, edge cases, saturating runs); `zt_bench --gpu-sim` on a real model |
| The app's models on whatever this machine runs fastest, measured: the model slot and every swarm base in zxv-host and zxvd are timed on each CPU kernel set (with its threads) and each GPU (Metal, CUDA, HIP, Vulkan); an engine whose logits differ in any bit is refused; the GPU takes prompts, one-token steps or both where it measured faster. One replayable line records each choice | 🧩 built; on lavapipe 17 agents, a LoRA agent and the single slot reply exactly as on the CPU, and the determiner picks the fastest measured engine; not yet run in the app on a real GPU | `zt_choose.h`, `zt_accel.h`, `zxv_zt_glue.c` G7, `test_zxv_zt_gpu` (in `test_hosted.sh` and `test-vk`) |
| Blocks kept in VRAM under their IPFS CIDs | 🧩 data layer only, simulated GPU | [VRAM_BLOCKS.md](VRAM_BLOCKS.md) |
| Accelerator tier between two integer tiers | 🧩 design and integer tiers; no GPU behind tier 2 yet | [COPROCESSOR_TIERS.md](COPROCESSOR_TIERS.md) |
| NVIDIA GPUs (CUDA): the products and the whole pass, the same kernels | 🧩 built; offline checks in CI; not yet run on a GPU (first run: the A6000 test server) | `zt_cuda.c`, `zt_kern_cuda_resident`, `make -C kernel test-cuda`, `zt_bench --gpu*` on Linux |
| AMD GPUs under ROCm (HIP): the products and the whole pass, the same kernels | 🧩 built; offline checks in CI (gfx906, gfx90a, gfx1030, gfx1100; wave32 and wave64 emulated); not yet run on an AMD GPU | `zt_hip.c`, `zt_kern_hip_resident`, `make -C kernel test-hip`, `zt_bench --gpu*` (`ZT_GPU=hip`) |
| Any Vulkan GPU (AMD Radeon, Arm Mali, Qualcomm Adreno, Intel, NVIDIA): the products and the whole pass | 🧩 built; bit-exact on lavapipe (Mesa's Vulkan on the CPU), including real Qwen2.5-1.5B; not yet run on a Vulkan GPU | `zt_vk.comp`, `zt_vk.c`, `zt_kern_vk_resident`, `make -C kernel test-vk`, `zt_bench --gpu*` (`ZT_GPU=vulkan`) |

### NVIDIA (CUDA)

The owner (2026-10-15): "It can't only be compatible with Apple chips." The
CUDA backend runs the same `zt_metal_math.h` kernels as Metal, so every logit
is bit for bit the CPU's.

- **No toolkit to build.** It loads the driver (`libcuda`) and the run-time
  compiler (NVRTC) with `dlopen` when it starts, and compiles the kernels for
  the GPU it finds (a cubin for `sm_XY`, else PTX the driver finishes). One
  binary runs on any NVIDIA GPU from Turing on, and on machines with none it
  says so and stays on the CPU.
- **Memory.** A discrete GPU has its own memory. The weights are copied to
  VRAM once. The KV cache is page-locked and shared in place, because the CPU
  path reads it too. The pass's work memory lives on the GPU and crosses the
  bus twice a pass (inputs in; logits and hidden state out).
- **Products.** One warp (32 threads) per row, four rows a block, tiles of 8
  tokens when there are several, sums with `__shfl_down_sync`: integers, so
  the order cannot change the bits.
- **Checked without a GPU (CI, `verify-all`).** The host code builds with
  `-Werror`; the kernel source compiles to PTX for Turing, Ampere and Hopper
  (clang, 21 kernels each); and the warp product kernels run on the CPU, 32
  threads a warp, bit for bit against the C reference (48 random products,
  every quant type, partial blocks and tiles). A broken warp sum fails it.
- **Checked on a GPU.** `make -C kernel test-cuda` runs `test_zt_metal.c`'s
  checks on CUDA (random products, Qwen2.5-1.5B's shapes, whole fixture passes
  and resident passes, saturating gains). Until it has run on a real GPU, the
  NVIDIA path is unmeasured (R9): no speed is claimed.

### Radeon, Mali, Adreno and the rest (Vulkan)

Vulkan compute reaches AMD, Arm, Qualcomm, Intel and NVIDIA GPUs on Linux,
Windows and Android. `zt_vk.comp` is `zt_metal_math.h` in GLSL, line for
line, built to SPIR-V (20 kernels) by `glslangValidator` at build time, so
end-user devices need no shader compiler.

- **Written for phones as well as desktops.** Buffers are read as 32-bit
  words, so no 8- or 16-bit storage support is needed; byte writes are word
  atomics, so neighbouring threads never clobber each other. Products sum
  through shared memory, so any subgroup width works (8 on lavapipe, 16 to
  128 elsewhere). A product whose weights exceed what one binding may see
  (`maxStorageBufferRange`, 128 MB on some phones and on lavapipe) is split
  by rows: Qwen2.5-1.5B's 191 MB output matrix takes two dispatches.
- **Memory.** Weights go to VRAM once (or stay in unified memory). The KV
  cache is imported in place where the driver allows it
  (`VK_EXT_external_memory_host`), else mirrored and copied in and out around
  each pass (slower, the same bits).
- **With or without 64-bit integers in shaders.** Desktop Radeon, Intel and
  NVIDIA have `shaderInt64`; many Mali and Adreno parts do not. Every kernel
  is built twice: with 64-bit integers, and with none (`-DZV_I32=1`, no
  `Int64` capability in the SPIR-V), where each 64-bit value is a pair of
  32-bit words and each 64-bit step (carries, signed compares, shifts,
  multiplies, division) is done exactly on the pair. A GPU without
  `shaderInt64` gets the second set; `ZT_VK_FORCE_I32=1` takes it on any GPU.
  Checked on lavapipe with that knob: `test-vk` runs all 29 checks on both
  sets (state imported and mirrored), `spirv-dis` finds no 64-bit integer in
  the second set, and Qwen2.5-1.5B Q4_K_M with `--gpu-resident` gives the
  CPU's exact token ids and `zt.logits_fnv`. Not yet run on a real Mali or
  Adreno.
- **Checked (2026-10-15, lavapipe).** `make -C kernel test-vk` (in
  `verify-all`) builds both ways with `-Werror` and runs `spirv-val` on all
  20 kernels. It then runs `test_zt_metal.c`'s checks: random products,
  Qwen2.5-1.5B's shapes, whole fixture passes and resident passes with
  saturating gains. All 29 pass with the state imported, and again with it
  mirrored. With products sliced (a 400 KB limit forced), every product that
  ran was exact; the one too large for that limit was handed back to the CPU,
  as designed. On the real Qwen2.5-1.5B Q4_K_M, `zt_bench --gpu` and
  `--gpu-resident` gave the CPU's exact token ids and `zt.logits_fnv` (the
  whole pass: 957 dispatches a token, none declined). Lavapipe runs on the
  CPU, so these runs say nothing about speed; no Vulkan speed is claimed until
  it runs on a GPU (R9).

### What was measured

Products only, on an M3 Max with Qwen2.5-1.5B Q4_K_M (#85): bit-exact, prompt
reading 120 → 217 tokens/s, but token-by-token generation all on the GPU was
32 tokens/s against 57 on the CPU, because each of the ~197 products a token
needs was its own round trip (~110 µs of encode, commit and wait). That is the
bus shuttle G3 forbids, and it is why generation was left on the CPU.

The whole pass (draft #138), measured on the same M3 Max, Qwen2.5-1.5B
Q4_K_M, a 462-token prompt and 64 generated tokens, 8 threads. All four
modes gave the same tokens and the same `zt.logits_fnv` (7e55278052c5dd8a):

| Mode | Prompt reading (tok/s) | Generation (tok/s) |
|---|---|---|
| CPU (NEON) | 126 | 60 |
| `--gpu` (products on the GPU for prompts) | 242 | 61 |
| `--gpu-decode` (every product on the GPU) | 235 | 31 |
| `--gpu-resident` (the whole pass, one command buffer) | 289 | 38 |

The resident pass is the fastest way to read a prompt (2.3× the CPU) and
beats per-product GPU generation, but generation is still slower than the
CPU: 957 launches a token, and the norm, softmax and single-token product
kernels are naive (one thread loops over a whole row). Section 4 is the plan.
`make -C kernel test-metal` passes 30/30 on the GPU, including the resident
passes with forced saturation.

### Where these numbers ran, and what bare metal would change

Every number above is `zt_bench` as an ordinary native macOS process on the
M3 Max (Apple's own Metal driver, no virtual machine, no emulator, and no ZXV
OS underneath). The ZXV bare-metal images were not involved; today they boot
only in QEMU and do not run models on a GPU.

The owner asked for a bare-metal estimate from the hardware itself
(2026-10-15). The arithmetic, for Qwen2.5-1.5B Q4_K_M on the M3 Max:

| Quantity | Value | Source |
|---|---|---|
| Weights read for one generated token | ~1.01 GB (980 MB of layer and output weights, ~29 MB of Q16 KV cache at 500 positions; the embedding is one row) | the GGUF's tensor table |
| Memory bandwidth (M3 Max, 40-core GPU) | ~400 GB/s, shared by CPU and GPU | Apple's spec |
| **Floor per token, one stream** | 1.01 GB / 400 GB/s = **~2.5 ms, so ~400 tokens/s at most** | arithmetic |
| Measured, `--gpu-resident` | 26 ms a token (38 tokens/s): ~10% of the bandwidth | table above |
| Measured, CPU (NEON) | 17 ms a token (60 tokens/s) | table above |
| macOS's own share (command buffer commit and wait, ~0.1 ms a pass; scheduling) | under ~0.5 ms a token, a few percent | inferred from the pass count and Metal's usual commit latency |
| Compute for prompt reading | ~3.1 billion integer multiply-adds per token; at the several trillion integer operations a second the 40-core GPU can sustain, a ceiling in the low thousands of tokens/s (measured 289) | arithmetic, inferred GPU rate |

So removing the operating system would buy a few percent. The other ~90% of
the gap between 38 and ~400 tokens/s is our own kernels: 957 launches a
token, one thread looping over a whole row for norms and softmax, and weight
decoding byte by byte. A well-tuned pass usually reaches 50-75% of the
bandwidth, which would put **one stream at roughly 200-300 tokens/s** on this
machine, bare metal or not (inferred, not measured; R9).

**The owner's freight-packet design applied to this pass.** His long message
(2026-10-14) describes the CPU packing a uniform, pointer-free grid of jobs
into one contiguous block, coupling packets into trains, double buffering so
transfer and compute overlap, and probing the hardware to size packets
("gauge"). It also names the limit itself: work that reads fresh data once
with little arithmetic per byte stays bound by the wire speed. Mapped onto
the forward pass:

| Freight idea | In the pass |
|---|---|
| One packet, one hand-off | Done: the whole pass is one command buffer, and weights and KV are read in place, zero-copy (unified memory has no PCIe hop) |
| Coupling cars into a train | Fuse launches (957 a token toward ~150), and batch several swarm agents' tokens into one pass |
| Double-buffered ring | Encode agent B's pass while agent A's runs; keep the residual stream and logits on the GPU across tokens; sample on the GPU |
| Gauge profiler | Probe bandwidth, threadgroup width and cache tiles at start-up and pick tile sizes from them |
| The wire-speed limit | One stream decoding one token is exactly that case (each weight byte used for about two operations), so its ceiling is the ~400 tokens/s above |

Batching is where the train pays most: B agents' tokens share one read of
the weights, so the floor per pass stays ~2.5 ms while it carries B tokens.
Eight agents in one train would have an aggregate ceiling near the compute
limit, in the low thousands of tokens/s (inferred). Prompt reading already
shows the effect: 289 tokens/s against 38 for one-at-a-time generation.

Token denominations (TOKEN_DENOMINATIONS.md) are how the swarm counts and
settles tokens, not how fast a model makes them: a 144-face super token is
144 model tokens. Their speed gain is on the bookkeeping side (a million
tokens settle as 17 pieces at RACING instead of a million), and the
long message's BPE-to-Zeckendorf lookup costs one table read per token, so
neither slows the pass.

For other hardware the same floor applies: a GPU with ~1 TB/s of VRAM
bandwidth has a one-stream ceiling near ~1,000 tokens/s for this model, and
on a discrete card only ~6 KB of hidden state and (until sampling moves to
the GPU) ~0.6 MB of logits cross PCIe per token, once the weights are loaded.

## 3. How the whole pass works

1. The CPU looks up the token rows (embedding) and the RoPE angles for the
   batch's positions, and writes them into the GPU's work memory.
2. `zg_forward` builds the op list: for each layer, about 35 launches (norm,
   quantise, three products with their bias, q/k norms when the model has
   them, RoPE, KV store, six attention steps, the output product, residual,
   norm, quantise, gate and up products, SiLU, quantise, down product,
   residual), then the output norm and the logits product.
3. The backend encodes the list into one serial compute encoder and commits
   it once. Weights are read where they are (the mapped GGUF file); the KV
   cache is the model state's own memory, shared with the GPU in place (on
   Apple silicon the memory is unified, so nothing is copied).
4. The CPU reads back the logits, the last hidden state and the saturation
   count. `zt_model.c` updates the position and the counters as it always does.

A pass the GPU cannot take falls back, never fails: a Q8 KV cache, a LoRA
adapter, a hidden-state tap, a distributed model or weights converted to
`zt_q8_t` rows go to the per-product path and then the CPU, with the same bits.

## 4. Next steps, in order

1. Done: bit-exact on the M3 Max (table above).
2. Fewer launches (coupling the train): fuse norm + quantise, bias into the
   product, the six attention steps into two; one threadgroup per row with
   simd reductions instead of one thread per row for norms and softmax.
3. Faster products for one token (the kernels still decode weights byte by
   byte).
4. Keep the residual stream and logits on the GPU across tokens; sample on the
   GPU (greedy and top-k are integer).
5. Batch several agents' tokens into one pass (the freight train), and a
   start-up gauge probe that sizes tiles from the measured bandwidth.
6. CUDA: run `test-cuda` and the bench on the A6000 server. Vulkan: run
   `test-vk` and the bench on a Radeon, a Mali and an Adreno device (both
   kernel sets). HIP: run `test-hip` and the bench on an AMD GPU. Status per
   chip: [PLATFORM_COVERAGE.md](PLATFORM_COVERAGE.md).
7. Wire `vblock` (blocks in VRAM) and `cotier` tier 2 to a real backend.
8. Q5_0 and Q8_0-mixed files on the GPU (Qwen2.5-0.5B's Q4_K_M uses them, so
   its passes run on the CPU today).
