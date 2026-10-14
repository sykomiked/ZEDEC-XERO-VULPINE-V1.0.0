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

## 2. Where it stands (2026-10-14)

| Part | Status | Where |
|---|---|---|
| Matrix products on an Apple GPU, bit-exact | ✅ merged (#85) | `kernel/src/tensor/zt_metal.m`, `zt_kern_t.mm_full` |
| The whole forward pass on the GPU: every layer's RMS norm, activation quantiser, products and bias, RoPE, KV store, attention, residual and SiLU, then the output norm and logits, in one command buffer | 🧩 built, proven in C; Metal run pending on the M3 Max | `zt_gpu.h`, `zt_gpu.c`, `zt_kern_metal_resident`, `zt_bench --gpu-resident` |
| The same op list through a C backend | ✅ in CI | `test_zt_gpu` (fixtures, edge cases, saturating runs); `zt_bench --gpu-sim` on a real model |
| Blocks kept in VRAM under their IPFS CIDs | 🧩 data layer only, simulated GPU | [VRAM_BLOCKS.md](VRAM_BLOCKS.md) |
| Accelerator tier between two integer tiers | 🧩 design and integer tiers; no GPU behind tier 2 yet | [COPROCESSOR_TIERS.md](COPROCESSOR_TIERS.md) |
| Vulkan (Linux, Windows, Android) and CUDA (NVIDIA servers) | 📋 next | the same op list (G4) |

### What was measured

Products only, on an M3 Max with Qwen2.5-1.5B Q4_K_M (#85): bit-exact, prompt
reading 120 → 217 tokens/s, but token-by-token generation all on the GPU was
32 tokens/s against 57 on the CPU, because each of the ~197 products a token
needs was its own round trip (~110 µs of encode, commit and wait). That is the
bus shuttle G3 forbids, and it is why generation was left on the CPU.

The whole-pass op list for the same model is 957 kernel launches in one
command buffer per token: one hand-off instead of ~197. Run through the C
backend on Linux it gives the CPU's exact logits (`zt.logits_fnv` identical,
6- and 30-token prompts, batches of 8). Its speed on the GPU has not been
measured yet.

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

1. Run `make -C kernel test-metal` and `zt_bench --gpu-resident` on the M3 Max:
   bit-exactness on the GPU, then tokens per second against the CPU.
2. Fewer launches: fuse norm + quantise, bias into the product, the six
   attention steps into two; one threadgroup per row with simd reductions
   instead of one thread per row for norms and softmax.
3. Faster products for one token (the kernels still decode weights byte by
   byte).
4. Keep the residual stream and logits on the GPU across tokens; sample on the
   GPU (greedy and top-k are integer).
5. Vulkan backend for the same op list (Linux, Windows, Android), then CUDA
   for NVIDIA servers.
6. Wire `vblock` (blocks in VRAM) and `cotier` tier 2 to a real backend.
7. Q5_0 and Q8_0-mixed files on the GPU (Qwen2.5-0.5B's Q4_K_M uses them, so
   its passes run on the CPU today).
