<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# Task: first run of the NVIDIA (CUDA) and Vulkan backends on a GPU server

From the "GPU-first architecture" thread, 2026-10-15. Nothing here pushes to
GitHub or starts CI.

## What it is

ZXV's integer inference engine now has two new GPU backends, both bit for bit
the same as the CPU:
- NVIDIA, through CUDA (`kernel/src/tensor/zt_cuda.c`). Checked so far only
  without a GPU: the kernels compile to PTX and run correctly in a CPU
  emulator.
- Any Vulkan GPU (`zt_vk.c`). It already passes everything on lavapipe, a
  CPU Vulkan driver, including real Qwen2.5-1.5B.

This is the first run of both on real GPU hardware. An NVIDIA driver also
provides Vulkan.

The code is the branch `claude/project-thread-3cts9k-cuda` (main `4b9e2ae4`
plus the token yield and GPU commits, plus this folder). It has no pull
request, so it starts no CI.

## Steps

1. Get the script onto the server. It clones the repository itself, so this
   one file is enough:

       mkdir -p ~/zxv-cuda-in && curl -fsSL -o ~/zxv-cuda-in/run_cuda_tests.sh \
         https://raw.githubusercontent.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/claude/project-thread-3cts9k-cuda/tools/gpu/run_cuda_tests.sh

2. Check the server has the NVIDIA driver and NVRTC:
   - `nvidia-smi`;
   - `ldconfig -p | grep -E 'libcuda|libnvrtc'`.

   No nvcc is needed. If NVRTC is missing, the least invasive fix is a
   user-level pip wheel, `pip install --user nvidia-cuda-nvrtc-cu12`, then
   set `ZT_NVRTC_LIB` to the `libnvrtc.so.12` it installed.

   For the Vulkan half, also check that `vulkaninfo --summary` lists the GPU
   and that `glslangValidator` and the Vulkan headers are present (apt
   `vulkan-tools glslang-tools libvulkan-dev`). Without them the Vulkan steps
   say SKIP and the CUDA half still runs.

   Ask the user before installing anything system-wide.
3. Run it. It takes about 10 to 20 minutes and downloads the 1 GB
   Qwen2.5-1.5B Q4_K_M GGUF (Apache-2.0) into `~/zxv-cuda/models` unless it
   is already there:

       bash ~/zxv-cuda-in/run_cuda_tests.sh

   It only writes under `~/zxv-cuda` (set `WORK=` to move it). It does the
   following:
   - clones the public repo and checks out the branch;
   - runs `make -C kernel test-cuda` with the GPU required, then
     `make -C kernel test-vk`;
   - builds `zt_bench`;
   - benchmarks the same prompt on the CPU, then on CUDA with `--gpu`,
     `--gpu-decode` and `--gpu-resident`, then on Vulkan with `--gpu` and
     `--gpu-resident`;
   - checks that the GPU runs give the CPU's exact logits (`zt.logits_fnv`)
     and token ids.
4. Send back `~/zxv-cuda/results/`: `run.log`, `test-cuda.log`,
   `test_zt_cuda.full.log`, `test-vk.log`, `vulkaninfo.txt`, `bench-*.txt`
   and `bench-*.err`. If files can't be attached, paste `run.log` in full.

## What counts as a pass

- `[PASS] make test-cuda` and `[PASS] make test-vk`: every product and whole
  pass matches the C reference bit for bit.
- `[PASS] bench bit-exact in every GPU mode that ran`.
- The tokens per second in `bench-*.txt` are the first measured NVIDIA and
  Vulkan GPU numbers. Report them as measured; nothing was promised.

A failure is useful too. Send the logs back as they are.

## Rules

- No secrets in any file that comes back. Never copy SSH keys.
- Don't touch the server's other disks, and don't wipe anything.
- No pushes.
