#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Score zt_golden --scan outputs against the float64 reference (ref_f64.py).
usage: cmp_scan.py N REF64.f32 IDS.bin SCAN_DIR [LABEL ...]
For each LABEL.f32 (default: every .f32 in SCAN_DIR but the reference): mean KL(ref64 || x)
over positions 1..N-1, top-1 agreement with the reference, and perplexity on the ids.
Prints one tab-separated row per label, sorted by KL."""
import glob, os, sys
import numpy as np
V = 151936
N, REF, IDS, D = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
def ld(p): return np.fromfile(p, dtype=np.float32, count=N * V).reshape(N, V).astype(np.float64)
def lsm(x): m = x.max(1, keepdims=True); return x - m - np.log(np.exp(x - m).sum(1, keepdims=True))
R = lsm(ld(REF)); ids = np.fromfile(IDS, dtype=np.int32)[:N]
labels = sys.argv[5:] or sorted(os.path.basename(p)[:-4] for p in glob.glob(D + '/*.f32')
                               if os.path.abspath(p) != os.path.abspath(REF))
rows = []
for lb in labels:
    L = lsm(ld(f'{D}/{lb}.f32'))
    kl = (np.exp(R) * (R - L)).sum(1)[1:]
    top1 = (R.argmax(1) == L.argmax(1))[1:].mean()
    ppl = np.exp(-L[np.arange(N - 1), ids[1:]].mean())
    rows.append((kl.mean(), lb, top1, ppl))
print('label\tkl_vs_float64\ttop1\tppl')
for kl, lb, t, p in sorted(rows):
    print(f'{lb}\t{kl:.6f}\t{t:.3f}\t{p:.4f}')
