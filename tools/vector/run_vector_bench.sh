#!/usr/bin/env bash
# run_vector_bench.sh: GPU proofs and corpus benchmarks for the vectorisers
# (pictures and films -> ZXVG/ZXVM, audio -> ZXVA, any file -> ZXVS) on a
# Linux GPU server such as the RTX A6000. See tools/vector/TASK.md.
#
#   bash tools/vector/run_vector_bench.sh [EXTRA_CORPUS_DIR]
#
# Run from a checkout of the branch. Writes only under $WORK (default
# ~/zxv-vector): corpus/, build/, results/. Every result is JSON or a log.
# Nothing is pushed or installed. Exit status is nonzero if any step failed.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-$HOME/zxv-vector}
EXTRA=${1:-}
C=$WORK/corpus
B=$WORK/build
R=$WORK/results
mkdir -p "$C/visual" "$C/audio" "$C/data" "$B" "$R"
CC=${CC:-cc}
FAIL=0
STEPS="$R/steps.jsonl"
: >"$STEPS"

step() { # name, command...
    name=$1
    shift
    t0=$(date +%s%N)
    "$@" >"$R/$name.log" 2>&1
    rc=$?
    ms=$((($(date +%s%N) - t0) / 1000000))
    printf '{"step":"%s","rc":%d,"ms":%d,"log":"%s.log"}\n' "$name" "$rc" "$ms" "$name" >>"$STEPS"
    if [ $rc -eq 0 ]; then echo "[PASS] $name (${ms} ms)"; else echo "[FAIL] $name (rc $rc, see $R/$name.log)"; FAIL=1; fi
}

{
    echo "== host"
    uname -a
    git -C "$REPO" log --oneline -1
    nvidia-smi 2>&1 || echo "no nvidia-smi"
    ldconfig -p 2>/dev/null | grep -E 'libcuda|libnvrtc|libvulkan' || echo "no libcuda/libnvrtc/libvulkan in ldconfig"
    vulkaninfo --summary 2>&1 | head -40 || echo "no vulkaninfo"
    for v in ZT_NVRTC_LIB ZVT_NVRTC_LIB ZXVA_NVRTC_LIB ZVT_CUDA_LIB ZXVA_CUDA_LIB; do
        eval "echo \"$v=\${$v:-<unset>}\""
    done
    command -v glslangValidator || echo "no glslangValidator (Vulkan kernels will be unavailable)"
    $CC --version | head -1
} >"$R/host.txt" 2>&1

# 1. Corpora: generated fixtures plus files already in the repository.
step corpus_visual python3 "$REPO/tools/vtrace/make_fixtures.py" --corpus "$C/visual"
cp "$REPO/kernel/boot/zede_logo.png" "$C/visual/" 2>/dev/null
cp "$REPO/mobile/companion/ios/Runner/Assets.xcassets/AppIcon.appiconset/Icon-App-1024x1024@1x.png" "$C/visual/" 2>/dev/null
step corpus_audio python3 "$REPO/tools/vector/make_audio_corpus.py" "$C/audio"
for f in cards/cards.tsv bench/results/2026-10-11-russell-gating/logs/energy_balance_float64.tsv \
         api/zxvd.openrpc.json .zenodo.json kernel/src/vaudio/zxva.c CITATION.cff; do
    [ -f "$REPO/$f" ] && cp "$REPO/$f" "$C/data/"
done
if [ -n "$EXTRA" ] && [ -d "$EXTRA" ]; then
    echo "extra corpus: $EXTRA" >>"$R/host.txt"
fi

# 2. Unit suites (CPU) and the GPU proofs with every backend required.
step toolkit sh -c "cd '$REPO/kernel' && sh src/zui/test_zui_toolkit.sh"
step gpu_vtrace sh -c "cd '$REPO/kernel' && ZXV_TMP='$B/vtrace' ZVT_GPU_REQUIRE=cuda,vulkan sh src/vtrace/test_vtrace_gpu.sh"
step gpu_zxva sh -c "cd '$REPO' && ZXVA_GPU_REQUIRE=all sh tools/zxva/test_zxva_gpu.sh"

# 3. Benchmark tools.
K=$REPO/kernel
step build_vtrace sh -c "cd '$K' && sh src/vtrace/gen_zvt_vk_spv.sh '$B/zvt_vk_spv.h' && \
  $CC -std=c11 -O2 -DTEST_HOST -Iinclude -Isrc/zui -Isrc/font -Isrc/media -Isrc/robin_debanks -Isrc/vtrace \
  -Isrc/emu -Isrc/lpres -Isrc/event_space -Isrc/modbind -Isrc/surplus -DZVT_VK_SPV_H='\"$B/zvt_vk_spv.h\"' \
  ../tools/vtrace/vtrace.c src/vtrace/zui_vtrace.c src/vtrace/zui_vtrace_video.c src/vtrace/zvt_cuda.c \
  src/vtrace/zvt_vk.c src/robin_debanks/sha256.c src/zui/zui_vec.c src/zui/zui_canvas.c src/media/zx_image.c \
  src/media/zui_png.c src/media/zui_jpeg.c src/media/zx_inflate.c src/media/zui_video.c src/font/truetype.c \
  src/zui/zui_capture.c src/zui/zui_wm.c src/zui/zui_dock.c src/zui/zui_motion.c src/zui/zui_layout.c \
  src/zui/zui_progress.c -ldl -o '$B/vtrace_cli'"
step build_zxva sh -c "cd '$REPO' && sh kernel/src/vaudio/gen_zxva_vk_spv.sh '$B/zxva_vk_spv.h' '$B/zxva_vk.spv' && \
  $CC -std=c11 -O2 -DTEST_HOST -Ikernel/include -Ikernel/src/modbind -Ikernel/src/vaudio -Ikernel/src/media \
  -Ikernel/src/vshape -DZXVA_VK_SPV_H='\"$B/zxva_vk_spv.h\"' tools/zxva/zxva_bench.c kernel/src/vaudio/zxva_analyse.c \
  kernel/src/vaudio/zxva.c kernel/src/vaudio/zxva_cuda.c kernel/src/vaudio/zxva_vk.c kernel/src/media/zui_audio.c \
  kernel/src/vshape/zxvs.c kernel/src/robin_debanks/sha256.c -ldl -o '$B/zxva_bench'"

# 4. Corpus runs: every backend, every preset, JSON out.
step bench_visual "$B/vtrace_cli" --bench "$C/visual" --presets all --backends all \
    --jsonl "$R/visual.jsonl" --summary "$R/visual_summary.json"
step bench_audio "$B/zxva_bench" --backends cpu,cuda,vulkan --jsonl "$R/audio.jsonl" \
    --summary "$R/audio_summary.json" --out "$B/audio_out/" "$C/audio"
step bench_data "$B/zxva_bench" --mode data --jsonl "$R/data.jsonl" --summary "$R/data_summary.json" "$C/data"
step bench_data_1pct "$B/zxva_bench" --mode data --tol 655 --jsonl "$R/data_1pct.jsonl" "$C/data"
if [ -n "$EXTRA" ] && [ -d "$EXTRA" ]; then
    step extra_visual "$B/vtrace_cli" --bench "$EXTRA" --presets all --backends all \
        --jsonl "$R/extra_visual.jsonl" --summary "$R/extra_visual_summary.json"
    step extra_audio_data "$B/zxva_bench" --backends cpu,cuda,vulkan --jsonl "$R/extra_audio.jsonl" \
        --summary "$R/extra_audio_summary.json" "$EXTRA"
fi

printf '{"repo_head":"%s","work":"%s","failed":%s}\n' "$(git -C "$REPO" rev-parse HEAD)" "$WORK" \
    "$([ $FAIL -eq 0 ] && echo false || echo true)" >"$R/run.json"
echo "results: $R"
exit $FAIL
