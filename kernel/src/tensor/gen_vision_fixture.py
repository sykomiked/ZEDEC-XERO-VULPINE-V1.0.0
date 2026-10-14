# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# Generate test_vision_fixture.h: a tiny random SmolVLM-style vision projector
# (SigLIP encoder + Idefics3 pixel shuffle) written as a GGUF file by gguf-py,
# a random image, and the float64 numpy forward pass of llama.cpp's semantics
# on the dequantised weights (Q16, rounded). The image is 16 x 16 with 4 x 4
# patches, so the patch matrix rows are 48 values long: not a multiple of 32,
# which exercises zt_nn's padding. Matrices are Q8_0 (used in place) except the
# patch matrix (F32) and fc2 of layer 1 (F16), which zt_nn converts.
# Also: zt_vision_resize's integer area average re-done in Python on a 37 x 23
# image to 16 x 16 (the test checks it byte for byte).
# Usage: python3 -m pip install gguf numpy; python3 gen_vision_fixture.py test_vision_fixture.h
import sys, os, tempfile
import numpy as np
import gguf
from gguf import GGMLQuantizationType as T
from gguf.quants import quantize, dequantize

rng = np.random.default_rng(20261012)
S, P, NE, NF, NH, NL, SF, PD = 16, 4, 64, 128, 4, 2, 2, 48
side = S // P; NP = side * side; HD = NE // NH; EPS = 1e-6

path = os.path.join(tempfile.mkdtemp(), 'vis.gguf')
w = gguf.GGUFWriter(path, 'clip')
w.add_string('clip.projector_type', 'idefics3')
w.add_bool('clip.has_vision_encoder', True)
w.add_uint32('clip.vision.image_size', S)
w.add_uint32('clip.vision.patch_size', P)
w.add_uint32('clip.vision.embedding_length', NE)
w.add_uint32('clip.vision.feed_forward_length', NF)
w.add_uint32('clip.vision.block_count', NL)
w.add_uint32('clip.vision.attention.head_count', NH)
w.add_uint32('clip.vision.projection_dim', PD)
w.add_uint32('clip.vision.projector.scale_factor', SF)
w.add_float32('clip.vision.attention.layer_norm_epsilon', EPS)
w.add_array('clip.vision.image_mean', [0.5, 0.5, 0.5])
w.add_array('clip.vision.image_std', [0.5, 0.5, 0.5])
vals = {}

def q8(name, rows, cols, scale):
    a = (rng.standard_normal((rows, cols)) * scale).astype(np.float32)
    raw = quantize(a, T.Q8_0)
    w.add_tensor(name, raw, raw_dtype=T.Q8_0)
    vals[name] = dequantize(raw, T.Q8_0).astype(np.float64).reshape(rows, cols)

def f32(name, shape, scale, offset=0.0):
    a = (offset + rng.standard_normal(shape) * scale).astype(np.float32)
    w.add_tensor(name, a)
    vals[name] = a.astype(np.float64)

def f16(name, rows, cols, scale):
    a = (rng.standard_normal((rows, cols)) * scale).astype(np.float16)
    w.add_tensor(name, a)
    vals[name] = a.astype(np.float64)

f32('v.patch_embd.weight', (NE, 3, P, P), 0.15)
f32('v.patch_embd.bias', (NE,), 0.1)
f32('v.position_embd.weight', (NP, NE), 0.3)
for l in range(NL):
    p = 'v.blk.%d.' % l
    for m in ('attn_q', 'attn_k', 'attn_v', 'attn_out'):
        q8(p + m + '.weight', NE, NE, 0.12)
        f32(p + m + '.bias', (NE,), 0.05)
    # llama.cpp's converter's naming for SmolVLM: fc1 stored as ffn_down
    q8(p + 'ffn_down.weight', NF, NE, 0.12)
    f32(p + 'ffn_down.bias', (NF,), 0.05)
    if l == 1:
        f16(p + 'ffn_up.weight', NE, NF, 0.09)
    else:
        q8(p + 'ffn_up.weight', NE, NF, 0.09)
    f32(p + 'ffn_up.bias', (NE,), 0.05)
    for n in ('ln1', 'ln2'):
        f32(p + n + '.weight', (NE,), 0.2, 1.0)
        f32(p + n + '.bias', (NE,), 0.1)
f32('v.post_ln.weight', (NE,), 0.2, 1.0)
f32('v.post_ln.bias', (NE,), 0.1)
q8('mm.model.fc.weight', PD, NE * SF * SF, 0.06)
w.write_header_to_file(); w.write_kv_data_to_file(); w.write_tensors_to_file(); w.close()
blob = open(path, 'rb').read()

img = rng.integers(0, 256, (S, S, 3), dtype=np.uint8)
x = (img.astype(np.float64) / 255.0 - 0.5) / 0.5
patches = x.reshape(side, P, side, P, 3).transpose(0, 2, 4, 1, 3).reshape(NP, 3 * P * P)
h = patches @ vals['v.patch_embd.weight'].reshape(NE, -1).T + vals['v.patch_embd.bias'] + vals['v.position_embd.weight']
def ln(v, g, b):
    c = v - v.mean(-1, keepdims=True)
    return c / np.sqrt((c * c).mean(-1, keepdims=True) + EPS) * g + b
def gelu(v): return 0.5 * v * (1 + np.tanh(np.sqrt(2 / np.pi) * (v + 0.044715 * v ** 3)))
for l in range(NL):
    p = 'v.blk.%d.' % l; V = lambda n: vals[p + n]
    a = ln(h, V('ln1.weight'), V('ln1.bias'))
    q = (a @ V('attn_q.weight').T + V('attn_q.bias')).reshape(NP, NH, HD).transpose(1, 0, 2)
    k = (a @ V('attn_k.weight').T + V('attn_k.bias')).reshape(NP, NH, HD).transpose(1, 0, 2)
    v = (a @ V('attn_v.weight').T + V('attn_v.bias')).reshape(NP, NH, HD).transpose(1, 0, 2)
    s = q @ k.transpose(0, 2, 1) / np.sqrt(HD)
    s = np.exp(s - s.max(-1, keepdims=True)); s /= s.sum(-1, keepdims=True)
    o = (s @ v).transpose(1, 0, 2).reshape(NP, NE)
    h = h + o @ V('attn_out.weight').T + V('attn_out.bias')
    a = ln(h, V('ln2.weight'), V('ln2.bias'))
    m = gelu(a @ V('ffn_down.weight').T + V('ffn_down.bias'))
    h = h + m @ V('ffn_up.weight').T + V('ffn_up.bias')
h = ln(h, vals['v.post_ln.weight'], vals['v.post_ln.bias'])
ob = side // SF
sh = h.reshape(ob, SF, ob, SF, NE).transpose(0, 2, 1, 3, 4).reshape(ob * ob, SF * SF * NE)
out = sh @ vals['mm.model.fc.weight'].T
expect = np.round(out * 65536).astype(np.int64)

# resize reference: the same integer area average as zt_vision_resize
RW, RH = 37, 23
src = rng.integers(0, 256, (RH, RW, 3), dtype=np.uint8)
dst = np.zeros((S, S, 3), dtype=np.uint8)
for oy in range(S):
    y0, y1 = oy * RH, oy * RH + RH
    for ox in range(S):
        x0, x1 = ox * RW, ox * RW + RW
        acc = np.zeros(3, dtype=np.int64)
        sy = y0 // S
        while sy * S < y1 and sy < RH:
            wy = min((sy + 1) * S, y1) - max(sy * S, y0)
            sx = x0 // S
            while sx * S < x1 and sx < RW:
                wx = min((sx + 1) * S, x1) - max(sx * S, x0)
                acc += wy * wx * src[sy, sx].astype(np.int64)
                sx += 1
            sy += 1
        tot = RW * RH
        dst[oy, ox] = (acc + tot // 2) // tot

def arr(name, ctype, a, per=16):
    a = list(a)
    s = ['static const %s %s[%d] = {' % (ctype, name, len(a))]
    for i in range(0, len(a), per): s.append('    ' + ', '.join(str(int(v)) for v in a[i:i + per]) + ',')
    s.append('};')
    return '\n'.join(s)
out_h = ['/* test_vision_fixture.h -- generated by gen_vision_fixture.py with gguf-py and numpy %s.' % np.__version__,
         ' * A tiny random SigLIP + Idefics3 projector GGUF, an image, and float64 reference',
         ' * output (Q16, rounded) of llama.cpp semantics; plus a resize case. Do not edit. */',
         '#include <stdint.h>',
         '#define VF_S %d' % S, '#define VF_NOUT %d' % (ob * ob), '#define VF_PD %d' % PD,
         '#define VF_RW %d' % RW, '#define VF_RH %d' % RH,
         arr('VF_GGUF', 'uint8_t', blob, 24), arr('VF_IMG', 'uint8_t', img.reshape(-1), 24),
         arr('VF_EXPECT', 'int32_t', expect.reshape(-1), 8),
         arr('VF_RSRC', 'uint8_t', src.reshape(-1), 24), arr('VF_RDST', 'uint8_t', dst.reshape(-1), 24)]
open(sys.argv[1], 'w').write('\n'.join(out_h) + '\n')
print('gguf bytes', len(blob), 'out rms', np.sqrt((out ** 2).mean()))
