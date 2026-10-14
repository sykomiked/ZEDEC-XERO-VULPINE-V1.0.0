#!/bin/bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# Tensor-parallel benchmark: ranks x threads on one machine, 3 runs each.
# Needs bench/ built (make -C bench zt_bench) and the Qwen2.5-1.5B Q4_K_M file
# fetched by make -C bench qwen (or bench/tensor/real_model_test.py).
M=~/.cache/zxv/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
P=$'<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\nExplain in three short paragraphs why the sky is blue, how sunsets get their colour, and why clouds look white. Use simple words a child would understand.<|im_end|>\n<|im_start|>assistant\n'
for cfg in "1 1" "2 1" "4 1" "1 2" "2 2" "1 4"; do set -- $cfg
  for rep in 1 2 3; do
    if [ $1 = 1 ]; then extra="--mmap"; else extra="--ranks $1"; fi
    out=$(/tmp/zxv_bench/zt_bench $M --special --prompt "$P" -n 64 --threads $2 $extra)
    echo "ranks=$1 threads=$2 rep=$rep $(echo "$out" | grep -E "n_prompt|prefill_tps|n_gen=|gen_tps|logits_fnv|maxrss|rank0.sent" | tr '\n' ' ')"
  done
done
