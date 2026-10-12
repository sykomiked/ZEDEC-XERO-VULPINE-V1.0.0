#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# Float64 reference of the SigLIP image encoder + Idefics3 projector (SmolVLM mmproj GGUF),
# compared with the integer engine's output (zt_vlm --dump-rgb / --dump-embd).
# Weights are dequantized exactly from the file (gguf-py); all arithmetic is float64.
# Pixels are the same resized bytes zt_vlm used, so only the encoder is compared.
# usage: ref_vision.py MMPROJ.gguf RGB.bin EMBD.bin
# Prints max |error|, RMS error relative to the output's RMS, and the cosine of each
# output vector with the reference (minimum and mean).
import sys, numpy as np
from gguf import GGUFReader
from gguf.quants import dequantize

M, RGB, EMBD = sys.argv[1], sys.argv[2], sys.argv[3]
r = GGUFReader(M)
def kv(k):
    f = r.fields[k]
    v = [f.parts[i][0] for i in f.data]
    return v[0] if len(v) == 1 else v
T = {t.name: t for t in r.tensors}
def W(name):
    t = T[name]
    a = dequantize(t.data, t.tensor_type).astype(np.float64)
    return a.reshape([int(x) for x in reversed(t.shape)])
S = int(kv('clip.vision.image_size')); P = int(kv('clip.vision.patch_size'))
ne = int(kv('clip.vision.embedding_length')); nh = int(kv('clip.vision.attention.head_count'))
nl = int(kv('clip.vision.block_count')); sf = int(kv('clip.vision.projector.scale_factor'))
eps = float(kv('clip.vision.attention.layer_norm_epsilon'))
mean = np.array(kv('clip.vision.image_mean'), dtype=np.float64)
std = np.array(kv('clip.vision.image_std'), dtype=np.float64)
side = S // P; NP = side * side; hd = ne // nh

img = np.fromfile(RGB, dtype=np.uint8).reshape(S, S, 3).astype(np.float64)
x = (img / 255.0 - mean) / std                       # [S, S, 3]
patches = x.reshape(side, P, side, P, 3).transpose(0, 2, 4, 1, 3).reshape(NP, 3 * P * P)
pw = W('v.patch_embd.weight').reshape(ne, 3 * P * P)  # [768][3][16][16] -> rows
h = patches @ pw.T + W('v.patch_embd.bias') + W('v.position_embd.weight')

def ln(v, g, b):
    mu = v.mean(-1, keepdims=True); c = v - mu
    return c / np.sqrt((c * c).mean(-1, keepdims=True) + eps) * g + b
def gelu_tanh(v): return 0.5 * v * (1 + np.tanh(np.sqrt(2 / np.pi) * (v + 0.044715 * v ** 3)))
for l in range(nl):
    p = 'v.blk.%d.' % l
    swapped = T[p + 'ffn_down.weight'].shape[0] == ne
    f1, f2 = ('ffn_down', 'ffn_up') if swapped else ('ffn_up', 'ffn_down')
    a = ln(h, W(p + 'ln1.weight'), W(p + 'ln1.bias'))
    q = a @ W(p + 'attn_q.weight').T + W(p + 'attn_q.bias')
    k = a @ W(p + 'attn_k.weight').T + W(p + 'attn_k.bias')
    v = a @ W(p + 'attn_v.weight').T + W(p + 'attn_v.bias')
    q = q.reshape(NP, nh, hd).transpose(1, 0, 2); k = k.reshape(NP, nh, hd).transpose(1, 0, 2)
    v = v.reshape(NP, nh, hd).transpose(1, 0, 2)
    s = q @ k.transpose(0, 2, 1) / np.sqrt(hd)
    s = np.exp(s - s.max(-1, keepdims=True)); s /= s.sum(-1, keepdims=True)
    o = (s @ v).transpose(1, 0, 2).reshape(NP, ne)
    h = h + o @ W(p + 'attn_out.weight').T + W(p + 'attn_out.bias')
    a = ln(h, W(p + 'ln2.weight'), W(p + 'ln2.bias'))
    m = gelu_tanh(a @ W(p + f1 + '.weight').T + W(p + f1 + '.bias'))
    h = h + m @ W(p + f2 + '.weight').T + W(p + f2 + '.bias')
h = ln(h, W('v.post_ln.weight'), W('v.post_ln.bias'))
ob = side // sf
sh = h.reshape(ob, sf, ob, sf, ne).transpose(0, 2, 1, 3, 4).reshape(ob * ob, sf * sf * ne)
ref = sh @ W('mm.model.fc.weight').T
zt = np.fromfile(EMBD, dtype='<i4').astype(np.float64).reshape(ref.shape) / 65536.0
err = zt - ref
cos = (zt * ref).sum(1) / (np.linalg.norm(zt, axis=1) * np.linalg.norm(ref, axis=1))
print('vision.out_shape=%dx%d' % ref.shape)
print('vision.ref_rms=%.6g' % np.sqrt((ref ** 2).mean()))
print('vision.max_abs_err=%.6g' % np.abs(err).max())
print('vision.rel_rms_err=%.6g' % (np.sqrt((err ** 2).mean()) / np.sqrt((ref ** 2).mean())))
print('vision.min_cos=%.8f' % cos.min())
print('vision.mean_cos=%.8f' % cos.mean())
