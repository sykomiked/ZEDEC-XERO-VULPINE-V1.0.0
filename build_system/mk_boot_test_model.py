#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""mk_boot_test_model.py -- a tiny real GGUF language model for the boot path.

    python3 build_system/mk_boot_test_model.py OUT.gguf

Writes a qwen2 model (2 layers, 64 wide, 4 query heads over 2 KV heads, QKV
biases, Q8_0 matrices) with a byte-level BPE tokenizer: the 256 GPT-2 byte
tokens, a few merges and the three Qwen chat specials, <|im_end|> as the end
token. Every weight comes from a splitmix64 stream, so the file is the same
bytes on every run and every machine; nothing is downloaded and no weights
enter git.

It is NOT a trained model: its answer is noise. It exists so the boot path
(EFI stub -> kernel -> zt_model_generate, swarm_bootmodel.h) can be tested in
CI with the real loader, tokenizer and forward pass on a file small enough to
boot in seconds; test_swarm_bootmodel runs the same file on the host and the
bare-metal [AI_OK] line must match it digest for digest. The golden disc's
real boot model is Qwen2.5-0.5B-Instruct (build_system/golden.models).

Pure Python 3, no third-party packages: the GGUF v3 writer is below.
"""
import struct
import sys

N_EMBD, N_HEAD, N_HEAD_KV, N_FF, N_LAYER, N_CTX = 64, 4, 2, 128, 2, 256
HEAD_DIM = N_EMBD // N_HEAD
SEED = 0x5A58564D4F44454C  # "ZXVMODEL"
M64 = (1 << 64) - 1

# GGUF value types and tensor types
T_U32, T_I32, T_F32, T_BOOL, T_STR, T_ARR = 4, 5, 6, 7, 8, 9
G_F32, G_Q8_0 = 0, 8


def splitmix(seed, i):
    z = (seed + (i + 1) * 0x9E3779B97F4A7C15) & M64
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
    return z ^ (z >> 31)


def bytes_to_unicode():
    """GPT-2's byte alphabet: byte -> the character that stands for it."""
    bs = list(range(ord('!'), ord('~') + 1)) + list(range(0xA1, 0xAD)) + list(range(0xAE, 0x100))
    cs = bs[:]
    n = 0
    for b in range(256):
        if b not in bs:
            bs.append(b)
            cs.append(256 + n)
            n += 1
    return {b: chr(c) for b, c in zip(bs, cs)}


def vocab():
    b2u = bytes_to_unicode()
    toks = [b2u[b] for b in range(256)]
    merges = []
    for a, b in [(' ', 'h'), ('e', 'l'), ('l', 'o'), (' h', 'el'), ('Z', 'X'), ('ZX', 'V')]:
        ua = ''.join(b2u[c] for c in a.encode())
        ub = ''.join(b2u[c] for c in b.encode())
        merges.append(ua + ' ' + ub)
        toks.append(ua + ub)
    specials = ['<|endoftext|>', '<|im_start|>', '<|im_end|>']
    types = [1] * len(toks) + [3] * len(specials)
    toks += specials
    return toks, types, merges


def q8_rows(rows, cols, seed, scale_bits=0x2147):
    """rows x cols Q8_0: per 32 weights an f16 scale (about 0.01) and 32 int8."""
    out = bytearray()
    nb = rows * cols // 32
    for k in range(nb):
        out += struct.pack('<H', scale_bits ^ (splitmix(seed, k * 33) & 0x3F))
        out += bytes(((splitmix(seed, k * 33 + 1 + i) % 255) - 127) & 0xFF for i in range(32))
    return bytes(out)


def f32s(n, seed, base, spread):
    return b''.join(
        struct.pack('<f', base + ((splitmix(seed, i) % 1025) - 512) / 512.0 * spread)
        for i in range(n))


class Writer:
    def __init__(self):
        self.kv = []
        self.tensors = []

    def s(self, v):
        b = v.encode('utf-8')
        return struct.pack('<Q', len(b)) + b

    def add(self, key, typ, val):
        self.kv.append(self.s(key) + struct.pack('<I', typ) + self.val(typ, val))

    def val(self, typ, v):
        if typ == T_U32:
            return struct.pack('<I', v)
        if typ == T_I32:
            return struct.pack('<i', v)
        if typ == T_F32:
            return struct.pack('<f', v)
        if typ == T_BOOL:
            return struct.pack('<B', 1 if v else 0)
        if typ == T_STR:
            return self.s(v)
        if typ == T_ARR:
            et, items = v
            return struct.pack('<IQ', et, len(items)) + b''.join(self.val(et, x) for x in items)
        raise ValueError(typ)

    def tensor(self, name, dims, gtype, data):
        self.tensors.append((name, dims, gtype, data))

    def write(self, path):
        head = b'GGUF' + struct.pack('<IQQ', 3, len(self.tensors), len(self.kv)) + b''.join(self.kv)
        infos, blobs, off = b'', [], 0
        for name, dims, gtype, data in self.tensors:
            infos += self.s(name) + struct.pack('<I', len(dims))
            infos += b''.join(struct.pack('<Q', d) for d in dims)
            infos += struct.pack('<IQ', gtype, off)
            pad = (-len(data)) % 32
            blobs.append(data + bytes(pad))
            off += len(data) + pad
        out = head + infos
        out += bytes((-len(out)) % 32)
        with open(path, 'wb') as f:
            f.write(out + b''.join(blobs))


def main(path):
    toks, types, merges = vocab()
    n_vocab = len(toks)
    w = Writer()
    w.add('general.architecture', T_STR, 'qwen2')
    w.add('general.name', T_STR, 'zxv-boot-test')
    w.add('general.alignment', T_U32, 32)
    w.add('qwen2.block_count', T_U32, N_LAYER)
    w.add('qwen2.context_length', T_U32, N_CTX)
    w.add('qwen2.embedding_length', T_U32, N_EMBD)
    w.add('qwen2.feed_forward_length', T_U32, N_FF)
    w.add('qwen2.attention.head_count', T_U32, N_HEAD)
    w.add('qwen2.attention.head_count_kv', T_U32, N_HEAD_KV)
    w.add('qwen2.attention.layer_norm_rms_epsilon', T_F32, 1e-6)
    w.add('qwen2.rope.freq_base', T_F32, 1000000.0)
    w.add('tokenizer.ggml.model', T_STR, 'gpt2')
    w.add('tokenizer.ggml.pre', T_STR, 'qwen2')
    w.add('tokenizer.ggml.tokens', T_ARR, (T_STR, toks))
    w.add('tokenizer.ggml.token_type', T_ARR, (T_I32, types))
    w.add('tokenizer.ggml.merges', T_ARR, (T_STR, merges))
    w.add('tokenizer.ggml.eos_token_id', T_U32, toks.index('<|im_end|>'))
    w.add('tokenizer.ggml.add_bos_token', T_BOOL, False)

    seed = [SEED]

    def nxt():
        seed[0] = splitmix(seed[0], 0)
        return seed[0]

    kv_dim = N_HEAD_KV * HEAD_DIM
    w.tensor('token_embd.weight', [N_EMBD, n_vocab], G_Q8_0, q8_rows(n_vocab, N_EMBD, nxt(), 0x2E66))
    for l in range(N_LAYER):
        p = 'blk.%d.' % l
        w.tensor(p + 'attn_norm.weight', [N_EMBD], G_F32, f32s(N_EMBD, nxt(), 1.0, 0.25))
        w.tensor(p + 'attn_q.weight', [N_EMBD, N_EMBD], G_Q8_0, q8_rows(N_EMBD, N_EMBD, nxt()))
        w.tensor(p + 'attn_k.weight', [N_EMBD, kv_dim], G_Q8_0, q8_rows(kv_dim, N_EMBD, nxt()))
        w.tensor(p + 'attn_v.weight', [N_EMBD, kv_dim], G_Q8_0, q8_rows(kv_dim, N_EMBD, nxt()))
        w.tensor(p + 'attn_q.bias', [N_EMBD], G_F32, f32s(N_EMBD, nxt(), 0.0, 0.25))
        w.tensor(p + 'attn_k.bias', [kv_dim], G_F32, f32s(kv_dim, nxt(), 0.0, 0.25))
        w.tensor(p + 'attn_v.bias', [kv_dim], G_F32, f32s(kv_dim, nxt(), 0.0, 0.25))
        w.tensor(p + 'attn_output.weight', [N_EMBD, N_EMBD], G_Q8_0, q8_rows(N_EMBD, N_EMBD, nxt()))
        w.tensor(p + 'ffn_norm.weight', [N_EMBD], G_F32, f32s(N_EMBD, nxt(), 1.0, 0.25))
        w.tensor(p + 'ffn_gate.weight', [N_EMBD, N_FF], G_Q8_0, q8_rows(N_FF, N_EMBD, nxt()))
        w.tensor(p + 'ffn_up.weight', [N_EMBD, N_FF], G_Q8_0, q8_rows(N_FF, N_EMBD, nxt()))
        w.tensor(p + 'ffn_down.weight', [N_FF, N_EMBD], G_Q8_0, q8_rows(N_EMBD, N_FF, nxt()))
    w.tensor('output_norm.weight', [N_EMBD], G_F32, f32s(N_EMBD, nxt(), 1.0, 0.25))
    w.tensor('output.weight', [N_EMBD, n_vocab], G_Q8_0, q8_rows(n_vocab, N_EMBD, nxt(), 0x2E66))
    w.write(path)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('usage: mk_boot_test_model.py OUT.gguf')
    main(sys.argv[1])
