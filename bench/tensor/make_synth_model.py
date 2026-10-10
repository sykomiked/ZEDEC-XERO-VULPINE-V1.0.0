#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Write a synthetic qwen2 GGUF with random quantized weights, for speed tests.

The weights are random bytes in the chosen block format, so the file has the
size and the memory traffic of a real model of that shape but produces
meaningless text. Tokens/s measured on it are SYNTHETIC: they show what the
engine's kernels do on this CPU, not what a real model does on any other.

The tokenizer is copied from a tokenizer-only GGUF (kernel/arch/hosted/
test_write_gguf.c writes one); with --vocab larger than its vocabulary the
list is padded with unused control tokens so the output matrix has the real
model's row count.

Shapes:
  tiny     2 layers, n_embd 64, n_ff 128            (seconds to write)
  qwen1.5b 28 layers, n_embd 1536, n_ff 8960, 12 heads, 2 KV heads,
           head_dim 128, tied output (the published Qwen2.5-1.5B shape)

Types: q8_0 (every matrix Q8_0, used in place by the engine) or q4_k (every
matrix Q4_K except attn_v and ffn_down in Q6_K, roughly a Q4_K_M mix; the
engine converts these to 8-bit blocks at load).

Usage: make_synth_model.py TOKENIZER_ONLY.gguf OUT.gguf [--shape S] [--type T]
       [--vocab N]                                     (needs gguf-py, numpy)
"""
import argparse

import gguf
import numpy as np

SHAPES = {
    "tiny": dict(ne=64, nf=128, nh=4, nkv=2, hd=16, nl=2),
    "qwen1.5b": dict(ne=1536, nf=8960, nh=12, nkv=2, hd=128, nl=28),
}
Q = gguf.GGMLQuantizationType


def f16_bytes(v, n):
    return np.full(n, v, dtype=np.float16).view(np.uint8).reshape(n, 2)


def rand_q8_0(rng, rows, cols):
    nb = rows * cols // 32
    blk = np.empty((nb, 34), np.uint8)
    blk[:, 0:2] = f16_bytes(1.0 / 2048, nb)
    blk[:, 2:] = rng.integers(0, 256, (nb, 32), dtype=np.uint8)
    return blk.reshape(rows, cols // 32 * 34), Q.Q8_0


def rand_q4_k(rng, rows, cols):
    nb = rows * cols // 256
    blk = rng.integers(0, 256, (nb, 144), dtype=np.uint8)
    blk[:, 0:2] = f16_bytes(1.0 / 4096, nb)
    blk[:, 2:4] = f16_bytes(1.0 / 8192, nb)
    return blk.reshape(rows, cols // 256 * 144), Q.Q4_K


def rand_q6_k(rng, rows, cols):
    nb = rows * cols // 256
    blk = rng.integers(0, 256, (nb, 210), dtype=np.uint8)
    blk[:, 192:208] = rng.integers(1, 64, (nb, 16), dtype=np.uint8)  # int8 scales
    blk[:, 208:210] = f16_bytes(1.0 / 8192, nb)
    return blk.reshape(rows, cols // 256 * 210), Q.Q6_K


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("tokenizer")
    ap.add_argument("out")
    ap.add_argument("--shape", choices=sorted(SHAPES), default="tiny")
    ap.add_argument("--type", choices=["q8_0", "q4_k"], default="q8_0")
    ap.add_argument("--vocab", type=int, default=0, help="pad the vocabulary to N tokens")
    ap.add_argument("--seed", type=int, default=20261010)
    a = ap.parse_args()
    s = SHAPES[a.shape]
    ne, nf, nh, nkv, hd, nl = s["ne"], s["nf"], s["nh"], s["nkv"], s["hd"], s["nl"]

    r = gguf.GGUFReader(a.tokenizer)

    def strs(name):
        f = r.fields[name]
        return [bytes(f.parts[i]).decode("utf-8") for i in f.data]

    def scalar(name):
        f = r.fields[name]
        return f.parts[f.data[0]][0]

    tokens = strs("tokenizer.ggml.tokens")
    tt = r.fields["tokenizer.ggml.token_type"]
    types = [int(tt.parts[i][0]) for i in tt.data]
    while len(tokens) < a.vocab:
        tokens.append("<|synthetic_pad_%d|>" % len(tokens))
        types.append(3)  # control: never produced by encoding text
    V = len(tokens)

    w = gguf.GGUFWriter(a.out, "qwen2")
    w.add_block_count(nl)
    w.add_context_length(4096)
    w.add_embedding_length(ne)
    w.add_feed_forward_length(nf)
    w.add_head_count(nh)
    w.add_head_count_kv(nkv)
    w.add_layer_norm_rms_eps(1e-6)
    w.add_rope_freq_base(1000000.0)
    w.add_tokenizer_model(bytes(r.fields["tokenizer.ggml.model"].parts[-1]).decode())
    w.add_tokenizer_pre(bytes(r.fields["tokenizer.ggml.pre"].parts[-1]).decode())
    w.add_token_list(tokens)
    w.add_token_merges(strs("tokenizer.ggml.merges"))
    w.add_token_types(types)
    w.add_bos_token_id(int(scalar("tokenizer.ggml.bos_token_id")))
    w.add_eos_token_id(int(scalar("tokenizer.ggml.eos_token_id")))

    rng = np.random.default_rng(a.seed)
    if a.type == "q4_k" and (ne % 256 or nf % 256):
        ap.error("q4_k needs n_embd and n_ff to be multiples of 256 (use --shape qwen1.5b)")

    def mat(name, rows, cols, kind="main"):
        if a.type == "q8_0":
            data, t = rand_q8_0(rng, rows, cols)
        elif kind == "v":
            data, t = rand_q6_k(rng, rows, cols)
        else:
            data, t = rand_q4_k(rng, rows, cols)
        w.add_tensor(name, data, raw_dtype=t)

    def vec(name, n, sd):
        v = (rng.standard_normal(n) * sd).astype(np.float32) if sd else np.ones(n, np.float32)
        w.add_tensor(name, v)

    mat("token_embd.weight", V, ne)
    for layer in range(nl):
        b = "blk.%d." % layer
        vec(b + "attn_norm.weight", ne, 0)
        mat(b + "attn_q.weight", nh * hd, ne)
        mat(b + "attn_k.weight", nkv * hd, ne)
        mat(b + "attn_v.weight", nkv * hd, ne, "v")
        vec(b + "attn_q.bias", nh * hd, 0.02)
        vec(b + "attn_k.bias", nkv * hd, 0.02)
        vec(b + "attn_v.bias", nkv * hd, 0.02)
        mat(b + "attn_output.weight", ne, nh * hd)
        vec(b + "ffn_norm.weight", ne, 0)
        mat(b + "ffn_gate.weight", nf, ne)
        mat(b + "ffn_up.weight", nf, ne)
        mat(b + "ffn_down.weight", ne, nf, "v")
    vec("output_norm.weight", ne, 0)
    w.write_header_to_file()
    w.write_kv_data_to_file()
    w.write_tensors_to_file()
    w.close()
    print("wrote %s: SYNTHETIC qwen2 (%s, %s), %d layers, n_embd %d, n_ff %d, vocab %d, "
          "tied output, random weights" % (a.out, a.shape, a.type, nl, ne, nf, V))


if __name__ == "__main__":
    main()
