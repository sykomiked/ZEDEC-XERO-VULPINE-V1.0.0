# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# Generate test_tts_fixture.h for test_zt_tts.c (T26):
#   1. a tiny random WavTokenizer-shaped decoder ("wavtokenizer-dec" GGUF written by
#      gguf-py: 64 codes of 32 features, 32 channels, 4 GroupNorm groups, the six PosNet
#      blocks, two ConvNeXt blocks of 64, and the real 1282-wide output), F16 weights
#      like the real file except the output matrix (Q8_0, used in place), with the
#      float64 numpy forward pass of llama.cpp's graph on 7 codes: the spectrogram
#      (Q16, rounded) and the PCM of llama.cpp's iSTFT (x 32767 x 256, rounded);
#   2. an iSTFT-only case: a random 5-frame spectrogram given in Q16 and its float64
#      PCM (x 32767 x 256, rounded), with some bins above ln 100 (the clip).
# Usage: python3 -m pip install gguf numpy; python3 gen_tts_fixture.py test_tts_fixture.h
import sys, os, tempfile
import numpy as np
import gguf
from gguf import GGMLQuantizationType as T
from gguf.quants import quantize, dequantize

rng = np.random.default_rng(20261012)
NV, NFEAT, E, NFF, G, NCNX, NOUT, NFFT, HOP, NBIN = 64, 32, 32, 64, 4, 2, 1282, 1280, 320, 641
EPS = 1e-6
CODES = [3, 17, 60, 0, 42, 42, 9]

path = os.path.join(tempfile.mkdtemp(), 'wt.gguf')
w = gguf.GGUFWriter(path, 'wavtokenizer-dec')
P = 'wavtokenizer-dec.'
w.add_uint32(P + 'block_count', 12)
w.add_uint32(P + 'context_length', 8192)
w.add_uint32(P + 'embedding_length', NOUT)
w.add_uint32(P + 'attention.head_count', 1)
w.add_float32(P + 'attention.layer_norm_epsilon', EPS)
w.add_uint32(P + 'vocab_size', NV)
w.add_uint32(P + 'features_length', NFEAT)
w.add_uint32(P + 'feed_forward_length', NFF)
w.add_float32(P + 'attention.group_norm_epsilon', EPS)
w.add_uint32(P + 'attention.group_norm_groups', G)
w.add_uint32(P + 'posnet.embedding_length', E)
w.add_uint32(P + 'posnet.block_count', 6)
w.add_uint32(P + 'convnext.embedding_length', E)
w.add_uint32(P + 'convnext.block_count', NCNX)
V = {}


def f16(name, shape, scale, offset=0.0):  # shape in numpy order (GGUF dims reversed)
    a = (offset + rng.standard_normal(shape) * scale).astype(np.float16)
    w.add_tensor(name, a)
    V[name] = a.astype(np.float64)


def f32(name, shape, scale, offset=0.0):
    a = (offset + rng.standard_normal(shape) * scale).astype(np.float32)
    w.add_tensor(name, a)
    V[name] = a.astype(np.float64)


def q8(name, rows, cols, scale):
    a = (rng.standard_normal((rows, cols)) * scale).astype(np.float32)
    raw = quantize(a, T.Q8_0)
    w.add_tensor(name, raw, raw_dtype=T.Q8_0)
    V[name] = dequantize(raw, T.Q8_0).astype(np.float64).reshape(rows, cols)


f16('token_embd.weight', (NV, NFEAT), 1.0)
f16('conv1d.weight', (E, NFEAT, 7), 1 / np.sqrt(7 * NFEAT))
f32('conv1d.bias', (E, 1), 0.1)
for i in (0, 1, 3, 4):
    p = 'posnet.%d.' % i
    for n in ('norm1', 'norm2'):
        f32(p + n + '.weight', (E, 1), 0.2, 1.0)
        f32(p + n + '.bias', (E, 1), 0.1)
    for n in ('conv1', 'conv2'):
        f16(p + n + '.weight', (E, E, 3), 1 / np.sqrt(3 * E))
        f32(p + n + '.bias', (E, 1), 0.1)
p = 'posnet.2.'
f32(p + 'attn_norm.weight', (E, 1), 0.2, 1.0)
f32(p + 'attn_norm.bias', (E, 1), 0.1)
for n in ('attn_q', 'attn_k', 'attn_v', 'attn_output'):
    f16(p + n + '.weight', (E, E, 1), 1.5 / np.sqrt(E))
    f32(p + n + '.bias', (E, 1), 0.1)
f32('posnet.5.attn_norm.weight', (E, 1), 0.2, 1.0)
f32('posnet.5.attn_norm.bias', (E, 1), 0.1)
f32('token_embd_norm.weight', (E,), 0.2, 1.0)
f32('token_embd_norm.bias', (E,), 0.1)
for l in range(NCNX):
    p = 'convnext.%d.' % l
    f16(p + 'dw.weight', (E, 1, 7), 0.4)
    f32(p + 'dw.bias', (E, 1), 0.1)
    f32(p + 'norm.weight', (E,), 0.2, 1.0)
    f32(p + 'norm.bias', (E,), 0.1)
    f16(p + 'pw1.weight', (NFF, E), 1 / np.sqrt(E))
    f32(p + 'pw1.bias', (NFF,), 0.1)
    f16(p + 'pw2.weight', (E, NFF), 1 / np.sqrt(NFF))
    f32(p + 'pw2.bias', (E,), 0.1)
    f32(p + 'gamma.weight', (E,), 0.3, 0.5)
f32('output_norm.weight', (E,), 0.2, 1.0)
f32('output_norm.bias', (E,), 0.1)
q8('output.weight', NOUT, E, 0.3)
ob = np.concatenate([rng.standard_normal(NBIN) * 0.5 - 2.5, rng.standard_normal(NBIN) * 2.0])
ob = ob.astype(np.float32)
w.add_tensor('output.bias', ob)
V['output.bias'] = ob.astype(np.float64)
w.write_header_to_file(); w.write_kv_data_to_file(); w.write_tensors_to_file(); w.close()
blob = open(path, 'rb').read()


def W(n): return V[n]


def gelu(v): return 0.5 * v * (1 + np.tanh(np.sqrt(2 / np.pi) * (v + 0.044715 * v ** 3)))
def silu(v): return v / (1 + np.exp(-v))


def conv(x, name, K):
    wt = W(name + '.weight'); b = W(name + '.bias').reshape(-1)
    n, C = x.shape; p = K // 2
    xp = np.concatenate([np.zeros((p, C)), x, np.zeros((p, C))])
    return sum(xp[j:j + n] @ wt[:, :, j].T for j in range(K)) + b


def gn(x, name):
    g = W(name + '.weight').reshape(-1); b = W(name + '.bias').reshape(-1)
    n, C = x.shape; xg = x.reshape(n, G, C // G)
    mu = xg.mean(axis=(0, 2), keepdims=True); var = ((xg - mu) ** 2).mean(axis=(0, 2), keepdims=True)
    return ((xg - mu) / np.sqrt(var + EPS)).reshape(n, C) * g + b


def ln(x, name):
    g = W(name + '.weight').reshape(-1); b = W(name + '.bias').reshape(-1)
    c = x - x.mean(-1, keepdims=True)
    return c / np.sqrt((c * c).mean(-1, keepdims=True) + EPS) * g + b


x = conv(W('token_embd.weight')[CODES], 'conv1d', 7)
for i in range(6):
    p = 'posnet.%d.' % i
    if i in (0, 1, 3, 4):
        h = conv(silu(gn(x, p + 'norm1')), p + 'conv1', 3)
        x = x + conv(silu(gn(h, p + 'norm2')), p + 'conv2', 3)
    elif i == 2:
        h = gn(x, p + 'attn_norm')
        q, k, v = (conv(h, p + n, 1) for n in ('attn_q', 'attn_k', 'attn_v'))
        s = q @ k.T / np.sqrt(E)
        s = np.exp(s - s.max(-1, keepdims=True)); s /= s.sum(-1, keepdims=True)
        x = x + conv(s @ v, p + 'attn_output', 1)
    else:
        x = gn(x, p + 'attn_norm')
x = ln(x, 'token_embd_norm')
for l in range(NCNX):
    p = 'convnext.%d.' % l
    dw = W(p + 'dw.weight').reshape(E, 7); n = x.shape[0]
    xp = np.concatenate([np.zeros((3, E)), x, np.zeros((3, E))])
    h = sum(xp[j:j + n] * dw[:, j] for j in range(7)) + W(p + 'dw.bias').reshape(-1)
    h = gelu(ln(h, p + 'norm') @ W(p + 'pw1.weight').T + W(p + 'pw1.bias'))
    x = x + (h @ W(p + 'pw2.weight').T + W(p + 'pw2.bias')) * W(p + 'gamma.weight')
spec = ln(x, 'output_norm') @ W('output.weight').T + W('output.bias')


def istft(sp):
    n = sp.shape[0]
    X = np.minimum(np.exp(sp[:, :NBIN]), 100.0) * np.exp(1j * sp[:, NBIN:])
    k = np.arange(NFFT)[:, None]; m = np.arange(NBIN)[None, :]
    y = (X @ np.exp(2j * np.pi * ((k * m) % NFFT) / NFFT).T).real / NBIN
    hann = 0.5 * (1 - np.cos(2 * np.pi * np.arange(NFFT) / NFFT))
    L = (n - 1) * HOP + NFFT; acc = np.zeros(L); env = np.zeros(L)
    for f in range(n):
        acc[f * HOP:f * HOP + NFFT] += y[f] * hann; env[f * HOP:f * HOP + NFFT] += hann * hann
    pad = (NFFT - HOP) // 2
    return acc[pad:L - pad] / env[pad:L - pad]


pcm = istft(spec)
# iSTFT-only case: Q16 inputs given exactly
NI = 5
sq = np.concatenate([rng.uniform(-8, 1.2, (NI, NBIN)), rng.uniform(-20, 20, (NI, NBIN))], axis=1)
sq[:, [5, 100, 400]] = 5.0  # above ln 100: clipped to 100
sq = np.round(sq * 65536).astype(np.int64)
ipcm = istft(sq / 65536.0)


def arr(name, ctype, a, per=16):
    a = list(a)
    s = ['static const %s %s[%d] = {' % (ctype, name, len(a))]
    for i in range(0, len(a), per): s.append('    ' + ', '.join(str(int(v)) for v in a[i:i + per]) + ',')
    s.append('};')
    return '\n'.join(s)


out_h = ['/* test_tts_fixture.h -- generated by gen_tts_fixture.py with gguf-py and numpy %s.' % np.__version__,
         ' * A tiny random WavTokenizer-shaped decoder GGUF, codes, and the float64 reference of',
         " * llama.cpp's graph and iSTFT: spectrogram in Q16, PCM x 32767 x 256 (both rounded);",
         ' * plus an iSTFT-only case. Do not edit. */',
         '#include <stdint.h>',
         '#define TF_NCODES %d' % len(CODES), '#define TF_NOUT %d' % NOUT, '#define TF_NI %d' % NI,
         arr('TF_GGUF', 'uint8_t', blob, 24), arr('TF_CODES', 'int32_t', CODES),
         arr('TF_SPEC', 'int32_t', np.round(spec * 65536).astype(np.int64).reshape(-1), 8),
         arr('TF_PCM', 'int32_t', np.round(pcm * 32767 * 256).astype(np.int64), 8),
         arr('TF_ISPEC', 'int32_t', sq.reshape(-1), 8),
         arr('TF_IPCM', 'int32_t', np.round(np.clip(ipcm * 32767, -40000, 40000) * 256).astype(np.int64), 8)]
open(sys.argv[1], 'w').write('\n'.join(out_h) + '\n')
print('gguf bytes', len(blob), 'spec rms', np.sqrt((spec ** 2).mean()), 'logmag max', spec[:, :NBIN].max(),
      'pcm rms', np.sqrt((pcm ** 2).mean()), 'pcm max', np.abs(pcm).max(), 'ipcm max', np.abs(ipcm).max())
