#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# run_cuda_tests.sh — first run of ZXV's NVIDIA (zt_cuda.c) and Vulkan
# (zt_vk.c) backends on a real GPU. Read-only toward GitHub: it clones the
# public repo, checks out the branch under test (or adds the commits from a git
# bundle), builds, tests and benchmarks. Everything it writes stays in $WORK
# (default ~/zxv-cuda). It never pushes, and never touches other disks.
#
#   bash run_cuda_tests.sh                     (branch claude/project-thread-3cts9k-cuda)
#   bash run_cuda_tests.sh BRANCH              (another branch)
#   bash run_cuda_tests.sh /path/to/X.bundle   (commits from a bundle)
#
# Needs: git, gcc (11+), g++, make, curl, the NVIDIA driver (libcuda.so.1)
# and NVRTC (libnvrtc.so*, from any CUDA 11.2+ toolkit or the
# nvidia-cuda-nvrtc pip wheel; set ZT_NVRTC_LIB=/full/path/libnvrtc.so.12
# if it is not on the library path). No nvcc needed. For the Vulkan half:
# the NVIDIA driver's Vulkan ICD (normally installed with the driver), the
# Vulkan headers and glslangValidator (apt libvulkan-dev glslang-tools);
# without them the Vulkan steps say SKIP.
set -u
SRC=${1:-claude/project-thread-3cts9k-cuda}
[ -f "$SRC" ] && SRC=$(readlink -f "$SRC")
WORK=${WORK:-$HOME/zxv-cuda}
REPO_URL=https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0.git
BASE=4b9e2ae49ed900bf2aacca3228ebfa4fcd23fd8a
HEAD_REF=refs/heads/claude/project-thread-3cts9k
MODEL_URL=https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf
N=${N:-64}
THREADS=${THREADS:-$(nproc)}
PROMPT='<|im_start|>user
Explain in three sentences why integer arithmetic gives the same answer on every machine.<|im_end|>
<|im_start|>assistant
'
mkdir -p "$WORK/results" "$WORK/models" "$WORK/tmp"
R="$WORK/results"
exec > >(tee "$R/run.log") 2>&1
echo "=== machine ==="
date -u; uname -a
nvidia-smi --query-gpu=name,driver_version,memory.total,compute_cap --format=csv || echo "nvidia-smi not found"
ldconfig -p | grep -E "libcuda\.so|libnvrtc" || echo "(libcuda / libnvrtc not in ldconfig; set ZT_CUDA_LIB / ZT_NVRTC_LIB)"
gcc --version | head -1

echo "=== source ==="
cd "$WORK"
[ -d repo/.git ] || git clone --quiet "$REPO_URL" repo || { echo "FAIL clone"; exit 1; }
cd repo
git fetch --quiet origin main
if [ -f "$SRC" ]; then
  git cat-file -e "$BASE^{commit}" || { echo "FAIL: base $BASE not in the clone"; exit 1; }
  git bundle verify "$SRC" || { echo "FAIL bundle verify"; exit 1; }
  git fetch --quiet "$SRC" "$HEAD_REF:zxv-cuda-test" --force || { echo "FAIL bundle fetch"; exit 1; }
else
  git fetch --quiet origin "refs/heads/$SRC:zxv-cuda-test" --force || { echo "FAIL: cannot fetch branch $SRC"; exit 1; }
fi
git checkout --quiet --force zxv-cuda-test
git log --oneline -3

echo "=== 1. kernel tests (offline checks, then on the GPU) ==="
ZT_CUDA_REQUIRE=1 make -C kernel --no-print-directory ZXV_TMP="$WORK/tmp" test-cuda 2>&1 | tee "$R/test-cuda.log"
TEST_RC=${PIPESTATUS[0]}
cp -f "$WORK/tmp/test_zt_cuda.log" "$R/test_zt_cuda.full.log" 2>/dev/null

echo "=== 1b. Vulkan tests (SKIP if the headers, glslang or a Vulkan device are missing) ==="
make -C kernel --no-print-directory ZXV_TMP="$WORK/tmp" test-vk 2>&1 | tee "$R/test-vk.log"
VK_RC=${PIPESTATUS[0]}
command -v vulkaninfo >/dev/null && vulkaninfo --summary > "$R/vulkaninfo.txt" 2>&1

echo "=== 2. bench build ==="
make -C bench --no-print-directory OUT="$WORK/bench" "$WORK/bench/zt_bench" > "$R/bench-build.log" 2>&1 || { cat "$R/bench-build.log"; echo "FAIL bench build"; exit 1; }
M="$WORK/models/qwen2.5-1.5b-instruct-q4_k_m.gguf"
if [ ! -s "$M" ]; then
  echo "downloading Qwen2.5-1.5B-Instruct Q4_K_M (Apache-2.0, ~1 GB)"
  curl -fL --retry 3 -o "$M.part" "$MODEL_URL" && mv "$M.part" "$M" || { echo "FAIL model download"; exit 1; }
fi

echo "=== 3. bench: CPU, then CUDA (three modes) and Vulkan (two), same prompt ==="
B="$WORK/bench/zt_bench"
run() { # name, extra flags
  local name=$1; shift
  "$B" "$M" --special --prompt "$PROMPT" -n "$N" --threads "$THREADS" "$@" > "$R/bench-$name.txt" 2>"$R/bench-$name.err"
  echo "$name rc=$? $(grep -E '^zt\.(prefill_tps|gen_tps|logits_fnv|gpu_products|gpu_passes|gpu_passes_declined|gpu_s)=' "$R/bench-$name.txt" | tr '\n' ' ')"
}
run cpu
export ZT_GPU=cuda
run gpu --gpu
run gpu-decode --gpu --gpu-decode
run gpu-resident --gpu --gpu-decode --gpu-resident
export ZT_GPU=vulkan
run vk-gpu --gpu
run vk-gpu-resident --gpu --gpu-decode --gpu-resident
unset ZT_GPU

echo "=== 4. bit-exact check against the CPU ==="
ok=1
ref_fnv=$(grep '^zt.logits_fnv=' "$R/bench-cpu.txt"); ref_ids=$(grep '^zt.ids=' "$R/bench-cpu.txt")
for m in gpu gpu-decode gpu-resident vk-gpu vk-gpu-resident; do
  if grep -q "build has no GPU\|vulkan: " "$R/bench-$m.err" 2>/dev/null && [ "${m#vk}" != "$m" ]; then echo "[SKIP] $m: $(head -1 "$R/bench-$m.err")"; continue; fi
  f=$(grep '^zt.logits_fnv=' "$R/bench-$m.txt"); i=$(grep '^zt.ids=' "$R/bench-$m.txt")
  if [ -n "$ref_fnv" ] && [ "$f" = "$ref_fnv" ] && [ "$i" = "$ref_ids" ]; then echo "[PASS] $m: logits and token ids identical to the CPU ($f)"
  else echo "[FAIL] $m: CPU $ref_fnv vs GPU ${f:-none}"; ok=0; fi
done

echo "=== summary ==="
[ "$TEST_RC" = 0 ] && echo "[PASS] make test-cuda" || echo "[FAIL] make test-cuda (rc $TEST_RC)"
[ "$VK_RC" = 0 ] && echo "[PASS] make test-vk" || echo "[FAIL] make test-vk (rc $VK_RC)"
[ "$ok" = 1 ] && echo "[PASS] bench bit-exact in every GPU mode that ran" || echo "[FAIL] bench bit-exact"
echo "results in $R (send back run.log and the bench-*.txt files)"
[ "$TEST_RC" = 0 ] && [ "$ok" = 1 ]
