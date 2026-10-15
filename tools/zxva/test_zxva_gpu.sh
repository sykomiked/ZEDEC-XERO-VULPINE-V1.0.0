#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxva_gpu.sh — the analyser's GPU backends (kernel/src/vaudio:
# zxva_cuda.c, zxva_vk.c), checked as far as this machine allows; each step
# says PASS, SKIP (and why) or FAIL. Honours CC, CXX; run from anywhere.
#
#   CUDA, no GPU needed: the host code builds with -Werror and no CUDA
#     toolkit; the kernel source compiles to PTX for sm_75, sm_86 (RTX
#     A6000) and sm_90 with clang; the kernel runs on the CPU (a block is
#     host threads, test_zxva_cuda_emu.cpp) word for word against the C
#     reference. With an NVIDIA driver + NVRTC: test_zxva_cuda on the GPU.
#   Vulkan: the stub build; SPIR-V from zxva_vk.comp (glslangValidator) and
#     spirv-val; with any device with shaderInt64 (a GPU, or lavapipe):
#     test_zxva_vk word for word and whole analyses byte for byte.
# ZXVA_GPU_REQUIRE=cuda / vulkan / all turns the matching SKIPs into FAILs.
set -e
R=$(cd "$(dirname "$0")/../.." && pwd)
cd "$R/kernel"
CC=${CC:-cc}
CXX=${CXX:-c++}
O=${TMPDIR:-/tmp}/zxva_gpu.$$
mkdir -p "$O"
trap 'rm -rf "$O"' EXIT
F="-std=c11 -O2 -Wall -Wextra -Werror -DTEST_HOST -Iinclude -Isrc/modbind -Isrc/vaudio"
B="src/vaudio/zxva_analyse.c src/vaudio/zxva.c src/robin_debanks/sha256.c"
need=${ZXVA_GPU_REQUIRE:-}
skip() { # what, why
    echo "[SKIP] $1 ($2)"
    case "$need" in all | "$3") echo "[FAIL] ZXVA_GPU_REQUIRE=$need"; exit 1 ;; esac
}

# ---- CUDA ----
# shellcheck disable=SC2086
$CC $F src/vaudio/test_zxva_cuda.c src/vaudio/zxva_cuda.c $B -ldl -o "$O/test_zxva_cuda" 2>"$O/b.log" || { cat "$O/b.log"; echo "[FAIL] zxva_cuda build"; exit 1; }
"$O/test_zxva_cuda" --source >"$O/zxva_cuda_gen.cu"
echo "[PASS] zxva_cuda builds with no CUDA toolkit"
if echo 'int main(){}' | clang++ -x cuda --cuda-device-only --cuda-gpu-arch=sm_86 -nocudainc -nocudalib -S - -o /dev/null 2>/dev/null; then
    cat src/vaudio/test_zxva_cuda_ptx.h "$O/zxva_cuda_gen.cu" >"$O/ptx.cu"
    for a in sm_75 sm_86 sm_90; do
        clang++ -x cuda --cuda-device-only --cuda-gpu-arch=$a -nocudainc -nocudalib -std=c++14 -O2 -Wall -Werror \
            -Wno-unused-function -S "$O/ptx.cu" -o "$O/zxva_$a.ptx" || { echo "[FAIL] zxva_cuda kernel to PTX ($a)"; exit 1; }
        grep -q '^.visible .entry zxva_stft_k' "$O/zxva_$a.ptx" || { echo "[FAIL] no zxva_stft_k in $a PTX"; exit 1; }
    done
    echo "[PASS] zxva_cuda kernel compiles to PTX (sm_75, sm_86, sm_90)"
else
    skip "zxva_cuda PTX" "clang without the NVPTX target" cuda
fi
for f in zxva_analyse zxva; do
    $CC -std=c11 -O2 -DTEST_HOST -Iinclude -Isrc/modbind -Isrc/vaudio -c src/vaudio/$f.c -o "$O/$f.o" 2>/dev/null
done
$CC -std=c11 -O2 -DTEST_HOST -Iinclude -Isrc/modbind -c src/robin_debanks/sha256.c -o "$O/sha256.o" 2>/dev/null
if $CXX -std=c++20 -O1 -Wall -Werror -Wextra -Wno-unused-function -I"$O" -Isrc/vaudio src/vaudio/test_zxva_cuda_emu.cpp \
    "$O/zxva_analyse.o" "$O/zxva.o" "$O/sha256.o" -lpthread -o "$O/emu" 2>"$O/e.log"; then
    "$O/emu" >"$O/emu.out" 2>&1 || { cat "$O/emu.out"; echo "[FAIL] test_zxva_cuda_emu"; exit 1; }
    echo "[PASS] test_zxva_cuda_emu ($(tail -1 "$O/emu.out"))"
else
    skip "test_zxva_cuda_emu" "needs a C++20 compiler with <barrier>: $(head -1 "$O/e.log")" cuda
fi
"$O/test_zxva_cuda" >"$O/cuda.out" 2>&1 || { cat "$O/cuda.out"; echo "[FAIL] test_zxva_cuda"; exit 1; }
if grep -q '^\[SKIP\]' "$O/cuda.out"; then
    skip "test_zxva_cuda on a GPU" "$(sed -n 's/^\[SKIP\] //p' "$O/cuda.out")" cuda
else
    grep '^\[INFO\]' "$O/cuda.out" || true
    echo "[PASS] test_zxva_cuda ($(tail -1 "$O/cuda.out"))"
fi

# ---- Vulkan ----
# shellcheck disable=SC2086
$CC $F -c src/vaudio/zxva_vk.c -o "$O/zxva_vk_stub.o" || { echo "[FAIL] zxva_vk stub build"; exit 1; }
echo "[PASS] zxva_vk builds without Vulkan (stubs)"
if ! command -v glslangValidator >/dev/null 2>&1; then
    skip "zxva_vk kernel" "needs glslangValidator: apt glslang-tools" vulkan
    exit 0
fi
sh src/vaudio/gen_zxva_vk_spv.sh "$O/zxva_vk_spv.h" "$O/zxva_vk.spv" || { echo "[FAIL] zxva_vk.comp"; exit 1; }
echo "[PASS] zxva_vk.comp builds to SPIR-V (shared source included)"
if command -v spirv-val >/dev/null 2>&1; then
    spirv-val --target-env vulkan1.1 "$O/zxva_vk.spv" || { echo "[FAIL] spirv-val"; exit 1; }
    echo "[PASS] spirv-val: valid for Vulkan 1.1"
else
    skip "spirv-val" "apt spirv-tools" vulkan
fi
if ! echo '#include <vulkan/vulkan.h>' | $CC -E -x c - >/dev/null 2>&1; then
    skip "test_zxva_vk" "needs the Vulkan headers: apt libvulkan-dev" vulkan
    exit 0
fi
# shellcheck disable=SC2086
$CC $F -DZXVA_VK_SPV_H="\"$O/zxva_vk_spv.h\"" src/vaudio/test_zxva_vk.c src/vaudio/zxva_vk.c $B -ldl -o "$O/test_zxva_vk" 2>"$O/b.log" || { cat "$O/b.log"; echo "[FAIL] test_zxva_vk build"; exit 1; }
"$O/test_zxva_vk" >"$O/vk.out" 2>&1 || { cat "$O/vk.out"; echo "[FAIL] test_zxva_vk"; exit 1; }
if grep -q '^\[SKIP\]' "$O/vk.out"; then
    skip "test_zxva_vk on a device" "$(sed -n 's/^\[SKIP\] //p' "$O/vk.out")" vulkan
else
    grep '^\[INFO\]' "$O/vk.out" || true
    echo "[PASS] test_zxva_vk ($(tail -1 "$O/vk.out"))"
fi
