#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""KL(ref64 || x), top-1 agreement against the float64 reference (ref_f64.py).
usage: cmp_ref64.py N REF64.f32 LOGITS.f32 [LOGITS.f32 ...]"""
import numpy as np, sys
V=151936; N=int(sys.argv[1]); ref=sys.argv[2]
def ld(p,n): return np.fromfile(p,dtype=np.float32,count=n*V).reshape(n,V).astype(np.float64)
def lsm(x): m=x.max(1,keepdims=True); return x-m-np.log(np.exp(x-m).sum(1,keepdims=True))
R=lsm(ld(ref,N))
for name in sys.argv[3:]:
    L=lsm(ld(name,N)); kl=(np.exp(R)*(R-L)).sum(1)
    print(f'{name:28s} KL(ref64||x) pos0 {kl[0]:.4f}  pos1.. mean {kl[1:].mean():.5f} max {kl[1:].max():.4f}  top1 {(R.argmax(1)==L.argmax(1)).mean():.3f}')
