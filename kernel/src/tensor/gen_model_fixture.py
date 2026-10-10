# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# Generate test_model_fixture.h: three tiny random language models written as
# GGUF files by gguf-py (llama.cpp's own Python package) -- a qwen2 (GQA, QKV
# biases, NEOX RoPE), a qwen3 (per-head Q/K norm, key_length != n_embd/n_head,
# tied output) and a llama (interleaved RoPE, rope_freqs divisors, F16
# matrices that the engine must convert) -- and a float64 numpy forward pass
# that follows llama.cpp's semantics, run on the dequantised weights.
#
# The weights are drawn from a counter-based splitmix64 stream, so the fixture
# stores only each file's header plus a generation table; the test rebuilds the
# tensor data with the same integer recipe and checks the whole file against
# an FNV-1a hash of the bytes gguf-py wrote. Usage:
#   python3 -m pip install gguf numpy
#   python3 gen_model_fixture.py test_model_fixture.h
import os, re, sys, tempfile
import numpy as np
import gguf
from gguf import GGMLQuantizationType as T

HERE = os.path.dirname(os.path.abspath(__file__))
U = np.uint64
GOLD, M1, M2 = U(0x9E3779B97F4A7C15), U(0xBF58476D1CE4E5B9), U(0x94D049BB133111EB)
K_Q8, K_F16, K_NORM, K_BIAS, K_ROPEF = 1, 2, 3, 4, 5
HEAD = 32  # logits stored at every position for the first HEAD ids


def mix(seed, n):
    """z_i = splitmix64 finaliser of seed + (i + 1) * golden, i < n"""
    with np.errstate(over='ignore'):
        z = U(seed) + np.arange(1, n + 1, dtype=U) * GOLD
        z = (z ^ (z >> U(30))) * M1
        z = (z ^ (z >> U(27))) * M2
        return z ^ (z >> U(31))


def gen_bytes(kind, n, param, seed):
    """raw tensor bytes and the float64 values they stand for"""
    if kind == K_Q8:
        nb = n // 32
        z = mix(seed, nb * 33).reshape(nb, 33)
        d = ((U(param) << U(10)) | (z[:, 0] & U(0x3FF))).astype(np.uint16)
        q = ((z[:, 1:] % U(255)).astype(np.int64) - 127).astype(np.int8)
        raw = np.concatenate([d.view(np.uint8).reshape(nb, 2), q.view(np.uint8)], axis=1)
        val = d.view(np.float16).astype(np.float64)[:, None] * q.astype(np.float64)
        return raw.reshape(-1), val.reshape(-1)
    if kind == K_F16:
        z = mix(seed, n)
        bits = (((z >> U(20)) & U(1)) << U(15)) | ((U(param) - ((z >> U(16)) & U(1))) << U(10)) | (z & U(0x3FF))
        h = bits.astype(np.uint16)
        return h.view(np.uint8), h.view(np.float16).astype(np.float64)
    if kind == K_NORM:
        z = mix(seed, n)
        v = (1.0 + ((z % U(1025)).astype(np.int64) - 512) / 2048.0).astype(np.float32)
    elif kind == K_BIAS:
        z = mix(seed, n)
        v = (((z % U(2049)).astype(np.int64) - 1024) / 4096.0).astype(np.float32)
    elif kind == K_ROPEF:
        v = (1.0 + np.arange(n) / 8.0).astype(np.float32)
    return v.view(np.uint8), v.astype(np.float64)


def f16_exp_for(sigma, kind):
    """exponent field giving weights of roughly this standard deviation"""
    if kind == K_Q8:  # q ~ U[-127,127] (sd 73.6), d mantissa in [1, 2)
        return int(round(np.log2(sigma / (73.6 * 1.5)))) + 15
    return int(round(np.log2(sigma / 1.1))) + 15


def build(name, arch, cfg, seed, path):
    ne, nf, nh, nkv, hd, nl, V = (cfg[k] for k in ('ne', 'nf', 'nh', 'nkv', 'hd', 'nl', 'V'))
    w = gguf.GGUFWriter(path, arch)
    w.add_block_count(nl)
    w.add_context_length(256)
    w.add_embedding_length(ne)
    w.add_feed_forward_length(nf)
    w.add_head_count(nh)
    w.add_head_count_kv(nkv)
    w.add_layer_norm_rms_eps(1e-6)
    w.add_rope_freq_base(cfg['base'])
    if hd * nh != ne:
        w.add_key_length(hd)
        w.add_value_length(hd)
    w.add_rope_dimension_count(hd)
    w.add_file_type(gguf.LlamaFileType.MOSTLY_Q8_0)
    plan, vals = [], {}

    def add(tname, kind, shape, sigma=None):
        n = int(np.prod(shape))
        param = f16_exp_for(sigma, kind) if kind in (K_Q8, K_F16) else 0
        s = (seed * 1000003 + len(plan)) & ((1 << 64) - 1)
        raw, val = gen_bytes(kind, n, param, s)
        if kind == K_Q8:
            w.add_tensor(tname, raw.reshape(shape[0], -1), raw_dtype=T.Q8_0)
        elif kind == K_F16:
            w.add_tensor(tname, raw.view(np.float16).reshape(shape))
        else:
            w.add_tensor(tname, raw.view(np.float32).reshape(shape))
        plan.append((tname, kind, n, param, s, raw))
        vals[tname] = val.reshape(shape)

    mt = cfg.get('mat', {})  # per-matrix kind overrides
    qd, kvd = nh * hd, nkv * hd
    add('token_embd.weight', mt.get('token_embd', K_Q8), (V, ne), 0.5)
    for l in range(nl):
        b = 'blk.%d.' % l
        add(b + 'attn_norm.weight', K_NORM, (ne,))
        add(b + 'attn_q.weight', mt.get('attn', K_Q8), (qd, ne), 1 / np.sqrt(ne))
        add(b + 'attn_k.weight', mt.get('attn', K_Q8), (kvd, ne), 1 / np.sqrt(ne))
        add(b + 'attn_v.weight', mt.get('attn', K_Q8), (kvd, ne), 1 / np.sqrt(ne))
        if cfg.get('bias'):
            add(b + 'attn_q.bias', K_BIAS, (qd,))
            add(b + 'attn_k.bias', K_BIAS, (kvd,))
            add(b + 'attn_v.bias', K_BIAS, (kvd,))
        if cfg.get('qknorm'):
            add(b + 'attn_q_norm.weight', K_NORM, (hd,))
            add(b + 'attn_k_norm.weight', K_NORM, (hd,))
        add(b + 'attn_output.weight', mt.get('attn', K_Q8), (ne, qd), 1 / np.sqrt(qd))
        add(b + 'ffn_norm.weight', K_NORM, (ne,))
        add(b + 'ffn_gate.weight', mt.get('ffn', K_Q8), (nf, ne), 1 / np.sqrt(ne))
        add(b + 'ffn_up.weight', mt.get('ffn', K_Q8), (nf, ne), 1 / np.sqrt(ne))
        add(b + 'ffn_down.weight', mt.get('ffn', K_Q8), (ne, nf), 1 / np.sqrt(nf))
    add('output_norm.weight', K_NORM, (ne,))
    if not cfg.get('tied'):
        add('output.weight', mt.get('output', K_Q8), (V, ne), 0.5)
    if cfg.get('ropef'):
        add('rope_freqs.weight', K_ROPEF, (hd // 2,))
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
    assert bytes(rebuilt) == blob, name + ': the generation table does not reproduce the file'
    return blob, data_off, gens, vals


def forward(cfg, W, tokens):
    """float64 reference, llama.cpp semantics; logits at every position"""
    ne, nh, nkv, hd, nl = (cfg[k] for k in ('ne', 'nh', 'nkv', 'hd', 'nl'))
    eps = float(np.float32(1e-6))
    base = float(np.float32(cfg['base']))
    neox = cfg['arch'] != 'llama'
    half = hd // 2
    ff = W['rope_freqs.weight'] if cfg.get('ropef') else np.ones(half)
    inv = base ** (-2.0 * np.arange(half) / hd) / ff

    def norm(x, g):
        return x / np.sqrt(np.mean(x * x) + eps) * g

    def rope(x, pos):  # x: (heads, hd)
        th = pos * inv
        c, s = np.cos(th), np.sin(th)
        y = x.copy()
        ia, ib = (np.arange(half), np.arange(half) + half) if neox else (2 * np.arange(half), 2 * np.arange(half) + 1)
        a, b = x[:, ia], x[:, ib]
        y[:, ia] = a * c - b * s
        y[:, ib] = a * s + b * c
        return y

    out_w = W['token_embd.weight'] if cfg.get('tied') else W['output.weight']
    Ks = [[] for _ in range(nl)]
    Vs = [[] for _ in range(nl)]
    logits = []
    for pos, tok in enumerate(tokens):
        x = W['token_embd.weight'][tok].copy()
        for l in range(nl):
            b = 'blk.%d.' % l
            h = norm(x, W[b + 'attn_norm.weight'])
            q, k, v = W[b + 'attn_q.weight'] @ h, W[b + 'attn_k.weight'] @ h, W[b + 'attn_v.weight'] @ h
            if cfg.get('bias'):
                q, k, v = q + W[b + 'attn_q.bias'], k + W[b + 'attn_k.bias'], v + W[b + 'attn_v.bias']
            q, k, v = q.reshape(nh, hd), k.reshape(nkv, hd), v.reshape(nkv, hd)
            if cfg.get('qknorm'):
                q = np.stack([norm(r, W[b + 'attn_q_norm.weight']) for r in q])
                k = np.stack([norm(r, W[b + 'attn_k_norm.weight']) for r in k])
            q, k = rope(q, pos), rope(k, pos)
            Ks[l].append(k)
            Vs[l].append(v)
            K, Vv = np.stack(Ks[l]), np.stack(Vs[l])  # (pos+1, nkv, hd)
            att = np.zeros((nh, hd))
            for hh in range(nh):
                kv = hh // (nh // nkv)
                sc = K[:, kv, :] @ q[hh] / np.sqrt(hd)
                p = np.exp(sc - sc.max())
                p /= p.sum()
                att[hh] = p @ Vv[:, kv, :]
            x = x + W[b + 'attn_output.weight'] @ att.reshape(-1)
            h = norm(x, W[b + 'ffn_norm.weight'])
            g, u = W[b + 'ffn_gate.weight'] @ h, W[b + 'ffn_up.weight'] @ h
            x = x + W[b + 'ffn_down.weight'] @ (g / (1 + np.exp(-g)) * u)
        logits.append(out_w @ norm(x, W['output_norm.weight']))
    return np.array(logits)


def tok_prompt(index):
    """the qwen2 reference ids of TOK_TEXTS[index] from test_tok_fixture.h"""
    src = open(os.path.join(HERE, 'test_tok_fixture.h')).read()
    ids = [int(v) for v in re.search(r'TOK_IDS_QWEN2\[\d+\] = \{(.*?)\};', src, re.S).group(1).replace('\n', ' ').split(',') if v.strip()]
    nids = [int(v) for v in re.search(r'TOK_NIDS_QWEN2\[\d+\] = \{(.*?)\};', src, re.S).group(1).split(',')]
    off = sum(nids[:index])
    return ids[off:off + nids[index]]


def cstr(blob):
    lines, cur = [], ''
    for b in blob:
        ch = chr(b)
        cur += ch if (32 <= b < 127 and ch not in '"\\?') else '\\%03o' % b
        if len(cur) >= 92:
            lines.append('    "%s"' % cur)
            cur = ''
    if cur:
        lines.append('    "%s"' % cur)
    return lines


def ints(name, vals, ctype='int32_t', per=12):
    vals = list(vals) or [0]
    out = ['static const %s %s[%d] = {' % (ctype, name, len(vals))]
    for i in range(0, len(vals), per):
        out.append('    ' + ', '.join(str(int(v)) for v in vals[i:i + per]) + ',')
    out.append('};')
    return out


def fnv(blob):
    h = 0xcbf29ce484222325
    for b in blob:
        h = ((h ^ b) * 0x100000001b3) & ((1 << 64) - 1)
    return h


VARIANTS = [
    ('qwen2', dict(arch='qwen2', ne=64, nf=128, nh=4, nkv=2, hd=16, nl=2, V=1200, base=1e6, bias=True), 11),
    ('qwen3', dict(arch='qwen3', ne=64, nf=128, nh=4, nkv=2, hd=32, nl=2, V=256, base=1e6, qknorm=True, tied=True), 22),
    ('llama', dict(arch='llama', ne=64, nf=128, nh=4, nkv=4, hd=16, nl=2, V=256, base=5e5, ropef=True,
                   mat={'ffn': K_F16, 'output': K_F16, 'token_embd': K_F16}), 33),
]
N_GREEDY = 16
EOS = 2  # <|im_end|> in the tokenizer fixture

def main(outp):
    tmp = tempfile.mkdtemp()
    rng = np.random.default_rng(20261009)
    out = ['/* test_model_fixture.h -- generated by gen_model_fixture.py with gguf-py and numpy %s.' % np.__version__,
           ' * Three tiny random models (headers + splitmix64 generation tables; the test',
           ' * rebuilds the data and checks FNV-1a of the file gguf-py wrote) and float64',
           ' * reference logits (Q16, rounded) of llama.cpp semantics. Do not edit. */',
           '#include <stdint.h>',
           '#define MF_HEAD %d' % HEAD,
           'typedef struct { uint64_t off, n; uint32_t kind, param; uint64_t seed; } mf_gen_t;']
    table = []
    for name, cfg, seed in VARIANTS:
        path = os.path.join(tmp, name + '.gguf')
        blob, data_off, gens, W = build(name, cfg['arch'], cfg, seed, path)
        V = cfg['V']
        if name == 'qwen2':
            seqs = [tok_prompt(12), list(rng.integers(0, V, 9)), [5]]
        else:
            seqs = [list(rng.integers(0, V, 10)), list(rng.integers(0, V, 7)), [3]]
        seqs = [[int(t) for t in s] for s in seqs]
        last, head, amax, gap = [], [], [], []
        for s in seqs:
            L = forward(cfg, W, s)
            for row in L:
                head += list(np.rint(row[:HEAD] * 65536))
                o = np.argsort(-row, kind='stable')
                amax.append(o[0])
                gap.append(np.rint((row[o[0]] - row[o[1]]) * 65536))
            last += list(np.rint(L[-1] * 65536))
        # the greedy chain from sequence 0 (stops at EOS, which is not stored)
        chain, cur = [], list(seqs[0])
        while len(chain) < N_GREEDY:
            t = int(np.argmax(forward(cfg, W, cur)[-1]))
            if name == 'qwen2' and t == EOS:
                break
            chain.append(t)
            cur.append(t)
        mingap = min(gap) / 65536
        c = name.upper()
        out.append('#define MF_%s_HDR_LEN %du' % (c, data_off))
        out.append('static const char MF_%s_HDR[MF_%s_HDR_LEN + 1] =' % (c, c))
        out += cstr(blob[:data_off])
        out[-1] += ';'
        out.append('static const mf_gen_t MF_%s_GEN[%d] = {' % (c, len(gens)))
        for o, n, kind, param, s in gens:
            out.append('    {%du, %du, %d, %d, 0x%016xull},' % (o, n, kind, param, s))
        out.append('};')
        out += ints('MF_%s_SEQ' % c, [t for s in seqs for t in s])
        out += ints('MF_%s_SEQLEN' % c, [len(s) for s in seqs], 'uint32_t')
        out += ints('MF_%s_LAST' % c, last)
        out += ints('MF_%s_HEADL' % c, head)
        out += ints('MF_%s_ARGMAX' % c, amax)
        out += ints('MF_%s_GAP' % c, gap)
        out += ints('MF_%s_GREEDY' % c, chain)
        table.append('    {"%s", MF_%s_HDR, MF_%s_HDR_LEN, %du, 0x%016xull, MF_%s_GEN, %d, %d, %d, MF_%s_SEQ, MF_%s_SEQLEN, '
                     'MF_%s_LAST, MF_%s_HEADL, MF_%s_ARGMAX, MF_%s_GAP, MF_%s_GREEDY, %d},'
                     % (name, c, c, len(blob), fnv(blob), c, len(gens), V, len(seqs), c, c, c, c, c, c, c, len(chain)))
        print('%s: file %d bytes (header %d), %d tensors, min top-2 gap %.4f, greedy %s' %
              (name, len(blob), data_off, len(gens), mingap, chain))
    out.append('typedef struct {')
    out.append('    const char *name; const char *hdr; uint32_t hdr_len; uint64_t file_len, fnv;')
    out.append('    const mf_gen_t *gen; uint32_t n_gen, n_vocab, n_seq;')
    out.append('    const int32_t *seq; const uint32_t *seq_len;')
    out.append('    const int32_t *last, *head, *argmax, *gap, *greedy; uint32_t n_greedy;')
    out.append('} mf_variant_t;')
    out.append('static const mf_variant_t MF_VARIANTS[%d] = {' % len(table))
    out += table
    out.append('};')
    out.append('#define MF_N_VARIANTS %du' % len(table))
    open(outp, 'w').write('\n'.join(out) + '\n')
    print(outp, os.path.getsize(outp), 'bytes')


if __name__ == '__main__':
    main(sys.argv[1])
