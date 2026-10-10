#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Write a tiny random qwen2 GGUF for an offline end-to-end run of the benchmark.

The tokenizer (a 1200-token qwen2 byte-level BPE vocabulary) is copied from a
tokenizer-only GGUF (kernel/src/tensor's test fixture, written by
kernel/arch/hosted/test_write_gguf.c); the weights are seeded random F32 values.
The model is meaningless: it exists so `make -C bench smoke` can run zt_bench
(and llama.cpp, when present) through prompt -> prefill -> greedy generation
without downloading anything. Speeds measured on it say nothing about real
models.

Usage: make_tiny_model.py TOKENIZER_ONLY.gguf OUT.gguf   (needs gguf-py, numpy)
"""
import sys

import gguf
import numpy as np


def main(src, dst):
    r = gguf.GGUFReader(src)

    def field(name):
        return r.fields[name]

    def strs(name):
        f = field(name)
        return [bytes(f.parts[i]).decode("utf-8") for i in f.data]

    def scalar(name):
        f = field(name)
        return f.parts[f.data[0]][0]

    tokens = strs("tokenizer.ggml.tokens")
    V = len(tokens)
    ne, nf, nh, nkv, nl = 64, 128, 4, 2, 2
    hd = ne // nh
    w = gguf.GGUFWriter(dst, "qwen2")
    w.add_block_count(nl)
    w.add_context_length(256)
    w.add_embedding_length(ne)
    w.add_feed_forward_length(nf)
    w.add_head_count(nh)
    w.add_head_count_kv(nkv)
    w.add_layer_norm_rms_eps(1e-6)
    w.add_rope_freq_base(10000.0)
    w.add_file_type(gguf.LlamaFileType.ALL_F32)
    w.add_tokenizer_model(bytes(field("tokenizer.ggml.model").parts[-1]).decode())
    w.add_tokenizer_pre(bytes(field("tokenizer.ggml.pre").parts[-1]).decode())
    w.add_token_list(tokens)
    w.add_token_merges(strs("tokenizer.ggml.merges"))
    tt = field("tokenizer.ggml.token_type")
    w.add_token_types([int(tt.parts[i][0]) for i in tt.data])
    w.add_bos_token_id(int(scalar("tokenizer.ggml.bos_token_id")))
    w.add_eos_token_id(int(scalar("tokenizer.ggml.eos_token_id")))

    rng = np.random.default_rng(20261010)

    def add(name, shape, sd):
        a = (rng.standard_normal(shape) * sd).astype(np.float32) if sd else np.ones(shape, np.float32)
        w.add_tensor(name, a)

    add("token_embd.weight", (V, ne), 0.5)
    for layer in range(nl):
        b = "blk.%d." % layer
        add(b + "attn_norm.weight", (ne,), 0)
        add(b + "attn_q.weight", (nh * hd, ne), ne ** -0.5)
        add(b + "attn_k.weight", (nkv * hd, ne), ne ** -0.5)
        add(b + "attn_v.weight", (nkv * hd, ne), ne ** -0.5)
        add(b + "attn_q.bias", (nh * hd,), 0.02)
        add(b + "attn_k.bias", (nkv * hd,), 0.02)
        add(b + "attn_v.bias", (nkv * hd,), 0.02)
        add(b + "attn_output.weight", (ne, nh * hd), (nh * hd) ** -0.5)
        add(b + "ffn_norm.weight", (ne,), 0)
        add(b + "ffn_gate.weight", (nf, ne), ne ** -0.5)
        add(b + "ffn_up.weight", (nf, ne), ne ** -0.5)
        add(b + "ffn_down.weight", (ne, nf), nf ** -0.5)
    add("output_norm.weight", (ne,), 0)
    add("output.weight", (V, ne), 0.5)
    w.write_header_to_file()
    w.write_kv_data_to_file()
    w.write_tensors_to_file()
    w.close()
    print("wrote %s: random qwen2, %d layers, n_embd %d, vocab %d (meaningless weights)"
          % (dst, nl, ne, V))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
