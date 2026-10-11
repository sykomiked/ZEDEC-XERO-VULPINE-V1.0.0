#!/bin/bash
M=~/.cache/zxv/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
LB=~/llama.cpp/build/bin/llama-bench
for P in logs/prompt128.txt logs/prompt470.txt; do
  for cfg in "base /tmp/zb_base/zt_bench --batch 32" "new /tmp/zxv_bench/zt_bench --batch 32" "new /tmp/zxv_bench/zt_bench --batch 128" "new-avx2 /tmp/zxv_bench/zt_bench --batch 128 --kern avx2" "new-c /tmp/zxv_bench/zt_bench --batch 128 --kern c"; do
    set -- $cfg; name=$1; bin=$2; shift 2
    best=0; for r in 1 2; do
      o=$($bin $M --prompt "$(cat $P)" -n 16 --threads 4 "$@")
      t=$(echo "$o" | sed -n 's/zt.prefill_tps=//p'); g=$(echo "$o" | sed -n 's/zt.gen_tps=//p'); np=$(echo "$o" | sed -n 's/zt.n_prompt=//p'); f=$(echo "$o" | sed -n 's/zt.logits_fnv=//p')
      best=$(python3 -c "print(max($best,$t))")
    done
    echo "$(basename $P) $name $* n_prompt=$np prefill=$best gen=$g fnv=$f"
  done
  $LB -m $M -p $np -n 16 -ngl 0 -t 4 -r 2 -o md 2>/dev/null | grep -E "pp|tg"
done
