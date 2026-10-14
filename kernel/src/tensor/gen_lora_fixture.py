# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# Generate test_lora_fixture.h: LoRA adapters for the three tiny models of
# test_model_fixture.h, written by gguf-py the way llama.cpp's
# convert_lora_to_gguf.py writes them (general.type "adapter", adapter.type
# "lora", adapter.lora.alpha, "blk.<l>.<matrix>.weight.lora_a/_b"), and
# float64 reference logits of each base model with its adapter MERGED
# (W + scale * B @ A, scale = alpha / rank * adapter_scale, llama.cpp's rule).
#
#   qwen2  rank 8 (not a multiple of 32), F16, all seven matrices, alpha 16
#   qwen3  rank 32, Q8_0, attn_q / attn_v / ffn_down only, no alpha,
#          adapter scale 0.5
#   llama  rank 4, F32 lora_a and BF16 lora_b, all seven matrices, alpha 2,
#          adapter scale 1.5
#
# As in gen_model_fixture.py, tensor data comes from a counter-based
# splitmix64 stream: the fixture stores the header and a generation table,
# and the test rebuilds the file and checks an FNV-1a hash of it. Usage:
#   python3 -m pip install gguf numpy
#   python3 gen_lora_fixture.py test_lora_fixture.h
import os, sys, tempfile
import numpy as np
import gguf
from gguf import GGMLQuantizationType as T

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_model_fixture import (VARIANTS, build, forward, mix, gen_bytes, f16_exp_for, cstr, ints,
                               fnv, U, K_Q8, K_F16, HEAD)

K_LF32, K_LBF16 = 6, 7  # f32 (z % 2049 - 1024) * 2^-param; bf16 (z % 257 - 128) * 2^-param
MATS = ['attn_q', 'attn_k', 'attn_v', 'attn_output', 'ffn_gate', 'ffn_up', 'ffn_down']

LORAS = {
    'qwen2': dict(rank=8, a=K_F16, b=K_F16, mats=MATS, alpha=16.0, scale=1.0),
    'qwen3': dict(rank=32, a=K_Q8, b=K_Q8, mats=['attn_q', 'attn_v', 'ffn_down'], alpha=None, scale=0.5),
    'llama': dict(rank=4, a=K_LF32, b=K_LBF16, mats=MATS, alpha=2.0, scale=1.5),
}


def lora_bytes(kind, n, param, seed):
    if kind in (K_Q8, K_F16):
        return gen_bytes(kind, n, param, seed)
    z = mix(seed, n)
    if kind == K_LF32:
        v = (((z % U(2049)).astype(np.int64) - 1024) * 2.0 ** -param).astype(np.float32)
        return v.view(np.uint8), v.astype(np.float64)
    v = (((z % U(257)).astype(np.int64) - 128) * 2.0 ** -param).astype(np.float32)
    bits = (v.view(np.uint32) >> np.uint32(16)).astype(np.uint16)  # exact: 8 significant bits
    return bits.view(np.uint8), v.astype(np.float64)


def lora_param(kind, sigma):
    if kind in (K_Q8, K_F16):
        return f16_exp_for(sigma, kind)
    sd = 591.6 if kind == K_LF32 else 74.2
    return int(round(np.log2(sd / sigma)))


def mat_dims(cfg, m):
    ne, nf, qd, kvd = cfg['ne'], cfg['nf'], cfg['nh'] * cfg['hd'], cfg['nkv'] * cfg['hd']
    return {'attn_q': (qd, ne), 'attn_k': (kvd, ne), 'attn_v': (kvd, ne), 'attn_output': (ne, qd),
            'ffn_gate': (nf, ne), 'ffn_up': (nf, ne), 'ffn_down': (ne, nf)}[m]


def build_lora(name, cfg, lc, seed, path):
    w = gguf.GGUFWriter(path, cfg['arch'])
    w.add_string('general.type', 'adapter')
    w.add_string('adapter.type', 'lora')
    if lc['alpha'] is not None:
        w.add_float32('adapter.lora.alpha', lc['alpha'])
    r = lc['rank']
    scale = (lc['alpha'] / r if lc['alpha'] else 1.0) * lc['scale']
    plan, delta = [], {}

    def add(tname, kind, shape, sigma):
        n = int(np.prod(shape))
        param = lora_param(kind, sigma)
        s = (seed * 1000003 + len(plan)) & ((1 << 64) - 1)
        raw, val = lora_bytes(kind, n, param, s)
        if kind == K_Q8:
            w.add_tensor(tname, raw.reshape(shape[0], -1), raw_dtype=T.Q8_0)
        elif kind == K_F16:
            w.add_tensor(tname, raw.view(np.float16).reshape(shape))
        elif kind == K_LF32:
            w.add_tensor(tname, raw.view(np.float32).reshape(shape))
        else:
            w.add_tensor(tname, raw.reshape(shape[0], -1), raw_dtype=T.BF16)
        plan.append((tname, kind, n, param, s, raw))
        return val.reshape(shape)

    for l in range(cfg['nl']):
        for m in lc['mats']:
            rows, cols = mat_dims(cfg, m)
            base = 'blk.%d.%s.weight' % (l, m)
            A = add(base + '.lora_a', lc['a'], (r, cols), 1 / np.sqrt(cols))
            B = add(base + '.lora_b', lc['b'], (rows, r), 0.4 / (scale * np.sqrt(r)))
            delta[base] = scale * (B @ A)
    w.write_header_to_file()
    w.write_kv_data_to_file()
    w.write_tensors_to_file()
    w.close()
    blob = open(path, 'rb').read()
    rd = gguf.GGUFReader(path)
    offs = {t.name: int(t.data_offset) for t in rd.tensors}
    data_off = min(offs.values())
    rebuilt = bytearray(blob[:data_off]) + bytearray(len(blob) - data_off)
    gens = []
    for tname, kind, n, param, s, raw in plan:
        o = offs[tname]
        rebuilt[o:o + raw.size] = raw.tobytes()
        gens.append((o - data_off, n, kind, param, s))
    assert bytes(rebuilt) == blob, name + ': the generation table does not reproduce the adapter'
    return blob, data_off, gens, delta


def main(outp):
    tmp = tempfile.mkdtemp()
    rng = np.random.default_rng(20261012)
    out = ['/* test_lora_fixture.h -- generated by gen_lora_fixture.py with gguf-py and numpy %s.' % np.__version__,
           ' * LoRA adapters for the three models of test_model_fixture.h (headers +',
           ' * splitmix64 generation tables; FNV-1a of the file gguf-py wrote) and float64',
           ' * reference logits (Q16, rounded) of each base with its adapter merged. Do not edit. */',
           '#include <stdint.h>',
           '#define LF_HEAD %d' % HEAD]
    table = []
    for vi, (name, cfg, seed) in enumerate(VARIANTS):
        _, _, _, W = build(name, cfg['arch'], cfg, seed, os.path.join(tmp, name + '.gguf'))
        lc = LORAS[name]
        blob, data_off, gens, delta = build_lora(name, cfg, lc, seed + 1000, os.path.join(tmp, name + '-lora.gguf'))
        Wm = dict(W)
        for k, d in delta.items():
            Wm[k] = W[k] + d
        V = cfg['V']
        seqs = [[int(t) for t in rng.integers(0, V, n)] for n in (9, 5)]
        last, head, amax, gap, moved = [], [], [], [], 0.0
        for s in seqs:
            L = forward(cfg, Wm, s)
            L0 = forward(cfg, W, s)
            moved = max(moved, float(np.max(np.abs(L - L0))))
            for row in L:
                head += list(np.rint(row[:HEAD] * 65536))
                o = np.argsort(-row, kind='stable')
                amax.append(o[0])
                gap.append(np.rint((row[o[0]] - row[o[1]]) * 65536))
            last += list(np.rint(L[-1] * 65536))
        c = name.upper()
        scale_q16 = int(round(lc['scale'] * 65536))
        out.append('#define LF_%s_HDR_LEN %du' % (c, data_off))
        out.append('static const char LF_%s_HDR[LF_%s_HDR_LEN + 1] =' % (c, c))
        out += cstr(blob[:data_off])
        out[-1] += ';'
        out.append('static const mf_gen_t LF_%s_GEN[%d] = {' % (c, len(gens)))
        for o, n, kind, param, s in gens:
            out.append('    {%du, %du, %d, %d, 0x%016xull},' % (o, n, kind, param, s))
        out.append('};')
        out += ints('LF_%s_SEQ' % c, [t for s in seqs for t in s])
        out += ints('LF_%s_SEQLEN' % c, [len(s) for s in seqs], 'uint32_t')
        out += ints('LF_%s_LAST' % c, last)
        out += ints('LF_%s_HEADL' % c, head)
        out += ints('LF_%s_ARGMAX' % c, amax)
        out += ints('LF_%s_GAP' % c, gap)
        table.append('    {"%s", %d, LF_%s_HDR, LF_%s_HDR_LEN, %du, 0x%016xull, LF_%s_GEN, %d, %du, %du, %d, %d, '
                     'LF_%s_SEQ, LF_%s_SEQLEN, LF_%s_LAST, LF_%s_HEADL, LF_%s_ARGMAX, LF_%s_GAP},'
                     % (name, vi, c, c, len(blob), fnv(blob), c, len(gens), lc['rank'], scale_q16,
                        len(lc['mats']), len(seqs), c, c, c, c, c, c))
        print('%s: adapter %d bytes (header %d), %d tensors, rank %d; merged logits move up to %.3f '
              'from the base; min top-2 gap %.4f' % (name, len(blob), data_off, len(gens), lc['rank'],
                                                     moved, min(gap) / 65536))
    out.append('typedef struct {')
    out.append('    const char *name; uint32_t base; const char *hdr; uint32_t hdr_len; uint64_t file_len, fnv;')
    out.append('    const mf_gen_t *gen; uint32_t n_gen, rank, scale_q16, n_mats, n_seq;')
    out.append('    const int32_t *seq; const uint32_t *seq_len;')
    out.append('    const int32_t *last, *head, *argmax, *gap;')
    out.append('} lf_variant_t;')
    out.append('static const lf_variant_t LF_VARIANTS[%d] = {' % len(table))
    out += table
    out.append('};')
    out.append('#define LF_N_VARIANTS %du' % len(table))
    open(outp, 'w').write('\n'.join(out) + '\n')
    print(outp, os.path.getsize(outp), 'bytes')


if __name__ == '__main__':
    main(sys.argv[1])
