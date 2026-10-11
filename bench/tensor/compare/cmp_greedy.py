#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Greedy answers: ZXV (zt_bench JSON from real_model_test.py) vs llama.cpp (llama_ref gen),
plus llama.cpp at 1 and 4 threads. Env: ZXV_CMP_DIR (work dir), ZXV_MODEL, ZXV_X86_JSON."""
import json, os, subprocess, sys
D = os.environ.get('ZXV_CMP_DIR', '/tmp/zxv_cmp')
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from real_model_test import PROMPTS, chat
M = os.environ.get('ZXV_MODEL', os.path.expanduser('~/.cache/zxv/models/qwen2.5-1.5b-instruct-q4_k_m.gguf'))
zx = json.load(open(os.environ.get('ZXV_X86_JSON', '/tmp/zxv_bench/qwen_results.json')))
def ref(prompt, n, th):
    p = subprocess.run([D + '/llama_ref', 'gen', M, prompt, str(n), str(th)], capture_output=True, text=True)
    kv = dict(l[4:].split('=', 1) for l in p.stdout.splitlines() if l.startswith('ref.'))
    return kv
out = []
for (q, exp), r in zip(PROMPTS, zx['results']):
    z = next(x for x in r['runs'] if x['kern'] == 'avx512')
    a = ref(chat(q), 64, 4); b = ref(chat(q), 64, 1)
    row = dict(prompt=q, zxv_ids=z['ids'], ref_ids_t4=a['ids'], ref_ids_t1=b['ids'],
               same_answer=z['ids'] == a['ids'], ref_thread_stable=a['ids'] == b['ids'],
               zxv_n_prompt=int(z['n_prompt']), ref_n_prompt=int(a['n_prompt']),
               zxv_prefill_tps=float(z['prefill_tps']), ref_prefill_tps=float(a['prefill_tps']),
               zxv_gen_tps=float(z['gen_tps']), ref_gen_tps=float(a['gen_tps']))
    out.append(row); print(json.dumps(row))
json.dump(out, open(D + '/greedy_cmp.json', 'w'), indent=1)
