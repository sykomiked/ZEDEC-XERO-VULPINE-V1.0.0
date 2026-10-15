<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# Platform coverage of the integer engine

Which chips the forward pass (`kernel/src/tensor`) runs on, and how each claim
was checked. Every backend must return the C reference's bits (rule R1); a
platform is listed as working only where that was run on it (rule R9).

Three levels, and nothing in between:

- **Tested**: run on that hardware, bit-exact against the CPU reference.
- **Emulated**: the same code run on a stand-in (qemu, Mesa's lavapipe, the
  thread-level CUDA emulator), bit-exact there. The real chip has not run it.
- **Untested**: built only, or not built.

## GPUs

| Platform | Backend | Level | Evidence |
|---|---|---|---|
| Apple M-series (macOS) | Metal (`zt_metal.m`) | Tested | M3 Max: products and whole pass bit-exact (`test_zt_metal`, `zt_bench --gpu-resident`), see [GPU_STRATEGY.md](GPU_STRATEGY.md) |
| NVIDIA (Linux, Windows) | CUDA via NVRTC (`zt_cuda.c`) | Tested on the RTX A6000 (Linux, driver 580, sm_86) | 2026-10-15, commit 4a951060 (`tools/gpu/run_cuda_tests.sh`): Qwen2.5-1.5B through `zt_bench` `--gpu`, `--gpu-decode` and `--gpu-resident` gave the CPU's logits (fnv 4cc3218a8c0eea45); resident 151.8 tokens/s reading the prompt and 74.0 generating, against 32.8 and 13.7 on that server's CPU. `test_zt_cuda` itself did not run there: the server's clang 14 cannot target sm_90, which stopped `test-cuda-offline` first. Offline: 21 kernels compile to PTX for sm_75, sm_86, sm_90; the warp emulator matches the CPU on 48/48 products. Windows not run |
| Any Vulkan GPU: AMD Radeon, Intel Arc and integrated, NVIDIA | Vulkan compute (`zt_vk.c`, `zt_vk.comp`) | Tested on the RTX A6000; emulated for the rest | A6000: `test_zt_vk` 29/29 with the state imported and mirrored; Qwen2.5-1.5B `--gpu` and `--gpu-resident` gave the CPU's logits (fnv 4cc3218a8c0eea45), resident 32.2 tokens/s reading and 27.4 generating (CUDA is faster there, so the determiner takes CUDA). `make -C kernel test-vk` on lavapipe: 29/29 with the state imported and mirrored; Qwen2.5-1.5B `--gpu` and `--gpu-resident` logits identical to the CPU (fnv 27aad77f5095ad86, d489964cb01c7b5a) |
| Arm Mali, Qualcomm Adreno, other phone GPUs | Vulkan compute, 32-bit-word kernels (loaded when the GPU lacks `shaderInt64`) | Emulated | lavapipe with `ZT_VK_FORCE_I32=1`: `test_zt_vk` 29/29 (state imported and mirrored); no 32-bit module declares Int64 (`spirv-dis`); Qwen2.5-1.5B `--gpu-resident` ids and logits identical to the CPU (fnv d489964cb01c7b5a). No phone GPU has run it |
| AMD Instinct and Radeon under ROCm | HIP via hiprtc (`zt_hip.c`) | Emulated | `make -C kernel test-hip-offline`: 21 kernels compile for gfx906, gfx90a, gfx1030 (wave32) and gfx1100 (wave32 and wave64); the wavefront emulator matches the CPU on 96/96 products at 32 and 64 lanes. Radeon cards also run the Vulkan backend. No AMD GPU has run it; `make -C kernel test-hip` on one is the deciding check |
| Any GPU through a native WebGPU library (wgpu-native, Dawn): Linux, Windows, macOS | WebGPU (`zt_wgpu.c`, `zt_wgpu.wgsl`) | Emulated | `make -C kernel test-wgpu` with wgpu-native (pip `wgpu` 0.32.0) on lavapipe: 29/29 at three size sets (2x32x4 from the limits, 4x32x8, 1x8x2 with 4 MiB bindings and 64 KiB weight buffers); naga 30.0.1 validates the WGSL; the app's models 19/19; Qwen2.5-1.5B `--gpu` and `--gpu-resident` ids 576,1172,1616 and logits fnv d489964cb01c7b5a, identical to the CPU. No GPU has run it |
| Browsers (Chrome, Edge, Firefox, Safari), phones included | WebGPU through Emscripten (`zt_wgpu.c` with emdawnwebgpu, `-sASYNCIFY` or `-sJSPI`) | Emulated | built with emcc 6.0.12 (emdawnwebgpu, `-Werror`); `test_zt_wgpu` linked for the browser and run in Deno 2.9.7's WebGPU (wgpu on lavapipe): 29/29. No browser and no phone has run it |
| Intel GPUs under oneAPI / Level Zero | none | Not built, by choice | Intel Arc and Intel integrated GPUs ship Vulkan drivers with 64-bit integer support, so the Vulkan backend covers them with the same kernels. A Level Zero backend is added only if a measured Intel run shows Vulkan cannot hold the bits or the speed |

## CPUs

| Platform | Kernel set | Level | Evidence |
|---|---|---|---|
| x86-64, any | C reference | Tested | every hosted test |
| x86-64 AVX2 | `avx2` (`zt_simd_x86.c`) | Tested | `test_zt_simd` on an Intel Xeon (AVX2, AVX-512 F/BW/VL, VNNI): 72/72 checks, 3 SIMD sets |
| x86-64 AVX-512 | `avx512` | Tested | same run |
| x86-64 AVX-512 VNNI | `avx512vnni` | Tested | same run |
| AArch64 NEON (Apple M-series, Linux and Android ARM) | `neon` (`zt_simd_neon.c`) | Tested on Apple M3 Max; emulated for Linux ARM | M3 Max: Qwen2.5-1.5B through `zt_bench` natively (17 ms a token, [GPU_STRATEGY.md](GPU_STRATEGY.md)); `test_zt_simd`, `test_zt_lora` and `test_zt_vision` under `qemu-aarch64` in `make -C kernel verify-all` |
| AArch64 SVE / SVE2 (Graviton 3/4, Neoverse V, A64FX, recent phones) | `sve` (`zt_simd_sve.c`, base SVE; Linux, GCC 13+ or clang 18+) | Emulated | `test_zt_simd` under `qemu-aarch64 -cpu max` at 128, 256, 384, 512, 1024 and 2048-bit vectors: 53/53 each; with SVE off the set is not offered. No SVE hardware has run it |
| RISC-V RVV 1.0 | `rvv` (`zt_simd_rvv.c`, built with `-march=rv64gcv`; Linux) | Emulated | `test_zt_simd` under `qemu-riscv64` at VLEN 128, 256 and 1024: 34/34 each; with V off the C reference is used. No RISC-V hardware has run it |

## In the app

The app (zxv-host and zxvd) picks what runs each model by measuring, not by
device name (`kernel/src/tensor/zt_choose.h`, used by
`kernel/arch/hosted/zxv_zt_glue.c` G7). When a model loads, it is timed on
every CPU kernel set above that this CPU has (each with its thread pool, and
the fastest at half the threads) and on every GPU backend that opens and
takes the weights (Metal on a Mac; CUDA, HIP, Vulkan and WebGPU elsewhere;
WebGPU alone in an Emscripten build). Every engine must return the same
logits, bit for bit, or it is refused. The fastest CPU engine at one-token
steps runs the model, and the fastest GPU takes prompts, steps or both
wherever it measured faster. Each choice is one line (`zxv_zt_choice`,
`ZT_CHOICE_LOG=FILE`) that `ZT_CHOICE` replays. `zt_choose_host` reports the
processor, cores, memory, kernel sets and GPUs for a swarm planner before
anything loads.

The probe runs on the first launch only. Its choice is kept in a small file
per machine and model (`ZT_CHOICE_CACHE`, else the OS's cache directory;
none in a browser tab), and each later load runs the probe once on the kept
choice and keeps it only if the logits are the ones measured then, bit for
bit, and its GPU still opens; otherwise it measures again.
`ZT_CHOICE=measure` forces a new measurement.

So "GPU first" means the GPU wins wherever it is faster, and the CPU takes
the rest. In the app on the M3 Max (GPU_STRATEGY.md), Metal read prompts at
97.6 tokens/s against the CPU's 87.4 and generated at 48.4 against 63.3,
which is the split the determiner makes there: prompts on Metal, steps on
NEON. The probe reads a 64-token prompt, so a GPU that only wins on much
longer prompts than that is not chosen for them.

Each build gets what its platform can load:

| Build | GPU backends in it |
|---|---|
| Mac app built on a Mac (`build_desktop.sh`, `build_zxvd.sh`) | Metal |
| zxvd on Linux and the edge packages (`build_zxvd.sh`) | CUDA and HIP; Vulkan too when the build machine has glslangValidator and the Vulkan headers; WebGPU is compiled as stubs (that build passes no `webgpu.h`; `make -C kernel test-wgpu` builds the app path with it) |
| Windows `zxv-swarm.exe` (zig) | CUDA and HIP (no Vulkan headers in that build) |
| Linux `zxv-swarm` tarballs (zig, static musl) | none in practice: a static binary cannot load the GPU drivers, so it runs on the CPU |
| Mac binary cross-built off a Mac (zig) | none: no Metal framework there |

`test_zxv_zt_gpu` checks the app path: on lavapipe 17 agents on one base, a
LoRA agent, recycled caches and the single model slot all reply exactly as
on the CPU.

## What would move a row

- Emulated to tested: the platform's own hardware runs the same check. The
  A6000 server moved NVIDIA CUDA and NVIDIA Vulkan; an AMD card, an Intel
  Arc card and an Android phone would cover the rest. WebGPU moves when the
  browser build runs in a real browser (and on a phone's), and the native
  build on a real GPU.
- Untested to emulated: the backend lands with its emulator or qemu check in
  `make -C kernel verify-all`.
