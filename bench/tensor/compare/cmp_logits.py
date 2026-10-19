#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Every-position logits on the same token ids: llama.cpp vs ZXV, and llama.cpp at two batch
sizes. Perplexity, top-1 agreement, KL(llama || other), logit differences. Env: ZXV_CMP_DIR."""
import numpy as np, json, os, sys
D = os.environ.get('ZXV_CMP_DIR', '/tmp/zxv_cmp')
V = 151936
ids = np.fromfile(D + '/ids.bin', dtype=np.int32)
def load(p): return np.fromfile(p, dtype=np.float32).reshape(-1, V).astype(np.float64)
def lsm(x): m = x.max(1, keepdims=True); return x - m - np.log(np.exp(x - m).sum(1, keepdims=True))
def stats(a, b, name):
    la, lb = lsm(a), lsm(b)
    tgt = ids[1:]; n = len(tgt)
    nll_a = -la[np.arange(n), tgt]; nll_b = -lb[np.arange(n), tgt]
    pa = np.exp(la)
    kl = (pa * (la - lb)).sum(1)
    d = np.abs(a - b)
    return dict(name=name, n_scored=int(n),
        ppl_ref=float(np.exp(nll_a[:n].mean())), ppl_other=float(np.exp(nll_b[:n].mean())),
        top1_agree=float((a[:n].argmax(1) == b[:n].argmax(1)).mean()),
        kl_mean=float(kl[:n].mean()), kl_p99=float(np.percentile(kl[:n], 99)), kl_max=float(kl[:n].max()),
        logit_absdiff_mean=float(d.mean()), logit_absdiff_max=float(d.max()),
        logit_std_ref=float(a.std()))
ref = load(D + '/ref_t4.f32')
res = [stats(ref, load(D + '/zxv.f32'), 'llama.cpp (float) vs ZXV (integer)')]
ub = load(D + '/ref_ub7.f32')
res.append(stats(ref, ub, 'llama.cpp ubatch 512 vs llama.cpp ubatch 7'))
for r in res: print(json.dumps(r, indent=1))
json.dump(res, open(D + '/logit_cmp.json', 'w'), indent=1)
