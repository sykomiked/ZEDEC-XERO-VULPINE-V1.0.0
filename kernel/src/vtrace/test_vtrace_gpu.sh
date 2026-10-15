#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_vtrace_gpu.sh — the vectoriser's GPU backends (zvt_gpu.h), from the
# kernel directory:  sh src/vtrace/test_vtrace_gpu.sh
#   1 builds with no GPU SDK at all (stubs; CUDA is dlopen'd anyway)
#   2 the CUDA source compiles to PTX for sm_75, sm_86, sm_90 (clang, stub
#     built-ins, no CUDA toolkit)                      [SKIP without NVPTX]
#   3 the CUDA kernels run on the CPU against the C reference (emulator)
#   4 zvt_cuda.c's whole host path against a stand-in libcuda/libnvrtc
#   5 zvt_vk.comp to SPIR-V, spirv-val               [SKIP without glslang]
#   6 test_zvt_gpu with Vulkan built in: every real device present (CUDA
#     via libcuda + libnvrtc, Vulkan via the loader, lavapipe counts) must
#     match the CPU bit for bit. ZVT_GPU_REQUIRE=cuda,vulkan turns a
#     missing backend into a failure (for the GPU server).
set -e
cd "$(dirname "$0")/../.."
T=${ZXV_TMP:-$(mktemp -d)}/vtrace_gpu
mkdir -p "$T"
Z=src/zui
INC="-Iinclude -Isrc/zui -Isrc/font -Isrc/media -Isrc/robin_debanks -Isrc/vtrace -Isrc/emu -Isrc/lpres -Isrc/event_space -Isrc/modbind -Isrc/surplus"
SRC="src/vtrace/zui_vtrace.c src/vtrace/zui_vtrace_video.c src/vtrace/zvt_cuda.c src/vtrace/zvt_vk.c src/robin_debanks/sha256.c $Z/zui_vec.c $Z/zui_canvas.c src/media/zx_image.c src/media/zui_png.c src/media/zui_jpeg.c src/media/zx_inflate.c src/media/zui_video.c src/font/truetype.c $Z/zui_capture.c $Z/zui_wm.c $Z/zui_dock.c $Z/zui_motion.c $Z/zui_layout.c $Z/zui_progress.c"
CF="-std=c11 -O2 -Wall -Wextra -Werror -DTEST_HOST"

# 1
gcc $CF $INC src/vtrace/test_zvt_gpu.c $SRC -ldl -o "$T/test_zvt_gpu_stub" || { echo "[FAIL] vtrace GPU stub build"; exit 1; }
# (no Vulkan in this build by design: ZVT_GPU_REQUIRE is for step 6's binary)
env -u ZVT_GPU_REQUIRE "$T/test_zvt_gpu_stub" > "$T/stub.log" 2>&1 || { cat "$T/stub.log"; echo "[FAIL] test_zvt_gpu (stubs)"; exit 1; }
echo "[PASS] vtrace GPU backends build with no GPU SDK ($(tail -1 "$T/stub.log" | sed 's/^\[PASS\] //'))"
"$T/test_zvt_gpu_stub" --source > "$T/zvt_cuda_gen.cu"

# 2
if echo 'int main(){}' | clang++ -x cuda --cuda-device-only --cuda-gpu-arch=sm_86 -nocudainc -nocudalib -S - -o /dev/null 2>/dev/null; then
    cat src/vtrace/test_zvt_cuda_ptx.h "$T/zvt_cuda_gen.cu" > "$T/zvt_cuda_ptx.cu"
    for a in sm_75 sm_86 sm_90; do
        clang++ -x cuda --cuda-device-only --cuda-gpu-arch=$a -nocudainc -nocudalib -std=c++14 -O2 -Wall -Werror \
            -Wno-unused-function -S "$T/zvt_cuda_ptx.cu" -o "$T/zvt_cuda_$a.ptx" || { echo "[FAIL] zvt_cuda to PTX ($a)"; exit 1; }
        test "$(grep -c '^.visible .entry' "$T/zvt_cuda_$a.ptx")" = 6 || { echo "[FAIL] zvt_cuda: 6 kernels expected ($a)"; exit 1; }
    done
    echo "[PASS] zvt_cuda kernels compile to PTX (sm_75, sm_86, sm_90: 6 kernels each)"
else
    echo "[SKIP] zvt_cuda PTX (clang without the NVPTX target)"
fi

# 3
for f in src/vtrace/zui_vtrace.c src/robin_debanks/sha256.c $Z/zui_vec.c $Z/zui_canvas.c src/font/truetype.c; do
    gcc $CF $INC -fPIC -c "$f" -o "$T/$(basename "$f" .c).o"
done
REF="$T/zui_vtrace.o $T/sha256.o $T/zui_vec.o $T/zui_canvas.o $T/truetype.o"
g++ -std=c++14 -O1 -Wall -Wextra -Werror -Wno-unused-function -I"$T" -Isrc/vtrace src/vtrace/test_zvt_cuda_emu.cpp $REF \
    -o "$T/test_zvt_cuda_emu" || { echo "[FAIL] test_zvt_cuda_emu build"; exit 1; }
"$T/test_zvt_cuda_emu" > "$T/emu.log" 2>&1 || { cat "$T/emu.log"; echo "[FAIL] test_zvt_cuda_emu"; exit 1; }
echo "[PASS] $(tail -1 "$T/emu.log" | sed 's/^\[PASS\] //')"

# 4
{ echo 'const char zvt_fake_expected[] = {'; od -An -tu1 -v "$T/zvt_cuda_gen.cu" | tr -s ' ' ',' | sed 's/^,//; s/$/,/'; echo '0};'; } > "$T/zvt_cuda_gen.c"
gcc -fPIC -c "$T/zvt_cuda_gen.c" -o "$T/zvt_cuda_gen.o"
g++ -std=c++14 -O1 -shared -fPIC -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -I"$T" \
    src/vtrace/test_zvt_fake_cuda.cpp "$T/zvt_cuda_gen.o" -o "$T/libzvt_fake_cuda.so" || { echo "[FAIL] fake libcuda build"; exit 1; }
ZVT_CUDA_LIB="$T/libzvt_fake_cuda.so" ZVT_NVRTC_LIB="$T/libzvt_fake_cuda.so" ZVT_GPU_REQUIRE=cuda "$T/test_zvt_gpu_stub" > "$T/fake.log" 2>&1 ||
    { cat "$T/fake.log"; echo "[FAIL] zvt_cuda host path (stand-in libcuda)"; exit 1; }
echo "[PASS] zvt_cuda host path on a stand-in libcuda/libnvrtc (CPU): $(grep 'cuda stages' "$T/fake.log" | sed 's/^\[PASS\] cuda stages: //; s/ runs (.*//') stage runs, $(grep 'cuda whole' "$T/fake.log" | sed 's/^\[PASS\] cuda whole traces: //; s/ (.*//') traces + the film identical"

# 5
if ! command -v glslangValidator >/dev/null 2>&1; then echo "[SKIP] zvt_vk kernels (needs glslangValidator: apt glslang-tools)"; exit 0; fi
sh src/vtrace/gen_zvt_vk_spv.sh "$T/zvt_vk_spv.h" || { echo "[FAIL] zvt_vk.comp"; exit 1; }
if command -v spirv-val >/dev/null 2>&1; then
    for k in 0 1 2 3 4 5; do
        glslangValidator -S comp --target-env vulkan1.1 -DZK=$k -o "$T/zvt_vk_$k.spv" src/vtrace/zvt_vk.comp >/dev/null &&
            spirv-val --target-env vulkan1.1 "$T/zvt_vk_$k.spv" || { echo "[FAIL] spirv-val stage $k"; exit 1; }
    done
    echo "[PASS] zvt_vk.comp: 6 stages to SPIR-V, spirv-val clean (Vulkan 1.1)"
else
    echo "[PASS] zvt_vk.comp: 6 stages to SPIR-V ([SKIP] spirv-val: apt spirv-tools)"
fi

# 6
if ! echo '#include <vulkan/vulkan.h>' | gcc -E -x c - >/dev/null 2>&1; then echo "[SKIP] test_zvt_gpu with Vulkan (needs libvulkan-dev)"; exit 0; fi
gcc $CF $INC -DZVT_VK_SPV_H="\"$T/zvt_vk_spv.h\"" src/vtrace/test_zvt_gpu.c $SRC -ldl -o "$T/test_zvt_gpu" || { echo "[FAIL] test_zvt_gpu build"; exit 1; }
"$T/test_zvt_gpu" > "$T/gpu.log" 2>&1 || { cat "$T/gpu.log"; echo "[FAIL] test_zvt_gpu"; exit 1; }
grep -E "^\[(SKIP|INFO|PASS)\]" "$T/gpu.log" | sed '$d'
echo "[PASS] $(tail -1 "$T/gpu.log" | sed 's/^\[PASS\] //')"
echo "       binary: $T/test_zvt_gpu"
