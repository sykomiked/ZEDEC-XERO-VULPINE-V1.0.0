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
| NVIDIA (Linux, Windows) | CUDA via NVRTC (`zt_cuda.c`) | Emulated | `make -C kernel test-cuda-offline`: 21 kernels compile to PTX for sm_75, sm_86, sm_90; the warp emulator matches the CPU on 48/48 products. First real run: the A6000 server (`tools/gpu/run_cuda_tests.sh`) |
| Any Vulkan GPU: AMD Radeon, Intel Arc and integrated, NVIDIA | Vulkan compute (`zt_vk.c`, `zt_vk.comp`) | Emulated | `make -C kernel test-vk` on lavapipe: 29/29 with the state imported and mirrored; Qwen2.5-1.5B `--gpu` and `--gpu-resident` logits identical to the CPU (fnv 27aad77f5095ad86, d489964cb01c7b5a) |
| Arm Mali, Qualcomm Adreno, other phone GPUs | Vulkan compute, 32-bit-word kernels (loaded when the GPU lacks `shaderInt64`) | Emulated | lavapipe with `ZT_VK_FORCE_I32=1`: `test_zt_vk` 29/29 (state imported and mirrored); no 32-bit module declares Int64 (`spirv-dis`); Qwen2.5-1.5B `--gpu-resident` ids and logits identical to the CPU (fnv d489964cb01c7b5a). No phone GPU has run it |
| AMD Instinct and Radeon under ROCm | HIP | Untested | Backend in progress. Radeon cards are also reachable through Vulkan today (emulated row above) |
| Intel GPUs under oneAPI / Level Zero | none | Not built, by choice | Intel Arc and Intel integrated GPUs ship Vulkan drivers with 64-bit integer support, so the Vulkan backend covers them with the same kernels. A Level Zero backend is added only if a measured Intel run shows Vulkan cannot hold the bits or the speed |

## CPUs

| Platform | Kernel set | Level | Evidence |
|---|---|---|---|
| x86-64, any | C reference | Tested | every hosted test |
| x86-64 AVX2 | `avx2` (`zt_simd_x86.c`) | Tested | `test_zt_simd` on an Intel Xeon (AVX2, AVX-512 F/BW/VL, VNNI): 72/72 checks, 3 SIMD sets |
| x86-64 AVX-512 | `avx512` | Tested | same run |
| x86-64 AVX-512 VNNI | `avx512vnni` | Tested | same run |
| AArch64 NEON (Apple M-series, Linux and Android ARM) | `neon` (`zt_simd_neon.c`) | Tested on Apple M3 Max; emulated for Linux ARM | M3 Max: Qwen2.5-1.5B through `zt_bench` natively (17 ms a token, [GPU_STRATEGY.md](GPU_STRATEGY.md)); `test_zt_simd`, `test_zt_lora` and `test_zt_vision` under `qemu-aarch64` in `make -C kernel verify-all` |
| AArch64 SVE / SVE2 (Graviton 3/4, Neoverse V, recent phones) | SVE | Untested | Kernel set in progress, to be checked under qemu at several vector lengths |
| RISC-V RVV 1.0 | RVV | Untested | Kernel set in progress, to be checked under qemu at several vector lengths |

## What would move a row

- Emulated to tested: the platform's own hardware runs the same check. The
  A6000 server covers NVIDIA CUDA and NVIDIA Vulkan; an AMD card, an Intel
  Arc card and an Android phone would cover the rest.
- Untested to emulated: the backend lands with its emulator or qemu check in
  `make -C kernel verify-all`.
