#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# build.sh OUT — build the model engine for the browser into OUT:
#   zxv_ai.js           what a page imports (openAI)
#   zxv_ai_worker.js    the model's own thread
#   zxv_ai_engine.js    the engine's loader (an ES module)
#   zxv_ai_engine.wasm  the app's integer engine: the CPU kernels and WebGPU
# Needs emcc (emsdk, with its emdawnwebgpu port). EMCC overrides the
# compiler.
set -eu
[ $# -eq 1 ] || { echo "usage: build.sh OUT" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
kernel=$(cd "$here/../../kernel" && pwd)
mkdir -p "$1"
out=$(cd "$1" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
EMCC=${EMCC:-emcc}

sh "$kernel/src/tensor/gen_zt_wgpu_wgsl.sh" "$tmp/zt_wgpu_wgsl.h"
T=$kernel/src/tensor
cd "$kernel"
"$EMCC" --use-port=emdawnwebgpu -std=c11 -O2 -Wall -Werror -Wextra -I"$T" \
    -DZT_WGPU_WGSL_H="\"$tmp/zt_wgpu_wgsl.h\"" \
    -sDEFAULT_TO_CXX -sASYNCIFY=1 -sALLOW_MEMORY_GROWTH=1 \
    -sINITIAL_MEMORY=67108864 -sMAXIMUM_MEMORY=4294967296 -sSTACK_SIZE=1048576 \
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=zxvAIEngine -sENVIRONMENT=web,worker,node \
    -sEXPORTED_FUNCTIONS=_zw_load,_zw_error,_zw_device,_zw_record,_zw_start,_zw_next,_zw_text,_zw_stop,_zw_n_gen,_zw_digest,_zw_free,_malloc,_free \
    -sEXPORTED_RUNTIME_METHODS=ccall,UTF8ToString,ENV,FS,HEAPU8 \
    "$here/zxv_ai_engine.c" "$T/zt_choose.c" "$T/zt_accel.c" "$T/zt_wgpu.c" "$T/zt_gpu.c" \
    "$T/zt_simd.c" "$T/zt_simd_x86.c" "$T/zt_simd_sve.c" "$T/zt_simd_neon.c" "$T/zt_simd_rvv.c" \
    "$T/zt_model.c" "$T/zt_tok.c" "$T/zt_rope.c" "$T/zt_gguf.c" "$T/zt.c" \
    -o "$out/zxv_ai_engine.js"
cp "$here/zxv_ai.js" "$here/zxv_ai_worker.js" "$out/"
