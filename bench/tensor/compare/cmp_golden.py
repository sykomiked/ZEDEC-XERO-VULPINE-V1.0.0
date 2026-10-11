#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Golden-ratio variants (zt_golden --out) vs llama.cpp and vs the plain ZXV engine.
KL and top-1 skip position 0 (see REPORT.md, finding F1). Env: ZXV_CMP_DIR. Args: modes."""
import numpy as np, json, os, sys
D = os.environ.get('ZXV_CMP_DIR', '/tmp/zxv_cmp')
V=151936
ids=np.fromfile(D + '/ids.bin',dtype=np.int32); tgt=ids[1:]; n=len(tgt)
def load(p): return np.fromfile(p,dtype=np.float32).reshape(-1,V).astype(np.float64)
def lsm(x): m=x.max(1,keepdims=True); return x-m-np.log(np.exp(x-m).sum(1,keepdims=True))
ref=lsm(load(D + '/ref_t4.f32')); base=lsm(load(D + '/zxv.f32'))
def st(la, lb):  # la reference
    kl=(np.exp(la)*(la-lb)).sum(1)[1:n]   # skip position 0 (see report)
    return dict(kl_mean=float(kl.mean()), top1=float((la[1:n].argmax(1)==lb[1:n].argmax(1)).mean()))
def ppl(l): return float(np.exp(-l[np.arange(n),tgt].mean()))
rows=[dict(mode='llama.cpp', ppl=ppl(ref)), dict(mode='zxv baseline', ppl=ppl(base), vs_llama=st(ref,base))]
for m in sys.argv[1:]:
    p=fD + '/g_{m}.f32'
    if not os.path.exists(p): continue
    l=lsm(load(p)); rows.append(dict(mode=m, ppl=ppl(l), vs_llama=st(ref,l), vs_zxv=st(base,l)))
for r in rows: print(json.dumps(r))
json.dump(rows, open(D + '/golden_cmp.json','w'), indent=1)
