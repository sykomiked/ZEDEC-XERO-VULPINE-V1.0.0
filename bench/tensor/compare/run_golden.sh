#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# Runs every zt_golden variant on ids.bin (from llama_ref dump) in $ZXV_CMP_DIR.
M=${ZXV_MODEL:-$HOME/.cache/zxv/models/qwen2.5-1.5b-instruct-q4_k_m.gguf}
cd "${ZXV_CMP_DIR:-/tmp/zxv_cmp}"
for mode in holo10 requant phi phi16 holo9 e8down int2down e8 int2; do
  ${ZXV_BENCH_OUT:-/tmp/zxv_bench}/zt_golden $M ids.bin $mode --out g_$mode.f32 --threads 4 > g_$mode.log 2>&1
  echo "$mode rc=$?"
done
echo ALLDONE
