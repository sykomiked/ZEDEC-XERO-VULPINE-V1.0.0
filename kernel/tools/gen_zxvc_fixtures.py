#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Small GGUF and safetensors files for test_zxvc.sh (standard library only).

Writes into OUTDIR:
  model.gguf            GGUF v3, general.license apache-2.0: F32, F16, BF16,
                        Q8_0 and Q4_K tensors, F16 subnormals, -0.0, +-65504
  nolicence.gguf        the same tensors with no general.license
  llama.gguf            general.license "llama3" (not on the allow-list)
  model.safetensors     F32, F16, BF16 tensors, __metadata__ license mit
  shard1.safetensors    a model split in two shards (no licence metadata)
  shard2.safetensors
  nan.safetensors       an F16 tensor holding a NaN (must be refused)
  wide.safetensors      F32 1.0 beside 2^-149 in one block (must be refused)
"""
import json
import os
import random
import struct
import sys


def f16_bits(rng, n):
    out = []
    for i in range(n):
        b = rng.getrandbits(16)
        while (b >> 10) & 0x1F == 0x1F:
            b = rng.getrandbits(16)
        out.append(b)
    return out


def bf16_weights(rng, n):
    out = []
    for _ in range(n):
        f = rng.gauss(0.0, 0.02)
        out.append(struct.unpack("<I", struct.pack("<f", f))[0] >> 16)
    return out


def tensors(rng):
    """(name, ggml type, dims in GGUF order, raw bytes)."""
    t = []
    f32 = [1.0 + rng.gauss(0, 0.1) for _ in range(64)] + [0.0, -0.0, 3.4e38, -3.0e38]
    t.append(("output_norm.weight", 0, [68], struct.pack("<%df" % len(f32), *f32)))
    h = f16_bits(rng, 32 * 40)
    h[:8] = [0x0001, 0x8001, 0x03FF, 0x8000, 0x7BFF, 0xFBFF, 0x0000, 0x3C00]
    t.append(("blk.0.attn_q.weight", 1, [32, 40], struct.pack("<%dH" % len(h), *h)))
    b = bf16_weights(rng, 48 * 7)
    t.append(("blk.0.ffn_up.weight", 30, [48, 7], struct.pack("<%dH" % len(b), *b)))
    q8 = bytes(rng.getrandbits(8) for _ in range(34 * 2 * 3))
    t.append(("blk.0.ffn_down.weight", 8, [64, 3], q8))
    q4k = bytes(rng.getrandbits(8) for _ in range(144 * 2))
    t.append(("blk.0.attn_v.weight", 12, [256, 2], q4k))
    return t


def gguf_str(s):
    b = s.encode()
    return struct.pack("<Q", len(b)) + b


def write_gguf(path, ts, licence):
    kv = [("general.architecture", "qwen2"), ("general.name", "zxvc fixture")]
    if licence is not None:
        kv.append(("general.license", licence))
    out = bytearray(b"GGUF" + struct.pack("<IQQ", 3, len(ts), len(kv)))
    for k, v in kv:
        out += gguf_str(k) + struct.pack("<I", 8) + gguf_str(v)
    off = 0
    infos, offs = bytearray(), []
    for name, typ, dims, raw in ts:
        infos += gguf_str(name) + struct.pack("<I", len(dims))
        infos += struct.pack("<%dQ" % len(dims), *dims) + struct.pack("<IQ", typ, off)
        offs.append(off)
        off = (off + len(raw) + 31) & ~31
    out += infos
    out += b"\0" * ((-len(out)) % 32)
    base = len(out)
    for (name, typ, dims, raw), o in zip(ts, offs):
        out += b"\0" * (base + o - len(out))
        out += raw
    with open(path, "wb") as f:
        f.write(out)


ST_DTYPE = {0: "F32", 1: "F16", 30: "BF16"}


def write_st(path, ts, meta):
    hdr, data = {}, bytearray()
    if meta:
        hdr["__metadata__"] = meta
    for name, typ, dims, raw in ts:
        hdr[name] = {"dtype": ST_DTYPE[typ], "shape": list(reversed(dims)),
                     "data_offsets": [len(data), len(data) + len(raw)]}
        data += raw
    h = json.dumps(hdr).encode()
    h += b" " * ((-len(h)) % 8)
    with open(path, "wb") as f:
        f.write(struct.pack("<Q", len(h)) + h + data)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    rng = random.Random(20261012)
    ts = tensors(rng)
    write_gguf(os.path.join(out, "model.gguf"), ts, "apache-2.0")
    write_gguf(os.path.join(out, "nolicence.gguf"), ts, None)
    write_gguf(os.path.join(out, "llama.gguf"), ts, "llama3")
    fl = [t for t in ts if t[1] in ST_DTYPE]
    # safetensors names, as Hugging Face checkpoints spell them
    st = [("model.norm.weight",) + fl[0][1:], ("model.layers.0.self_attn.q_proj.weight",) +
          fl[1][1:], ("model.layers.0.mlp.up_proj.weight",) + fl[2][1:]]
    write_st(os.path.join(out, "model.safetensors"), st, {"format": "pt", "license": "mit"})
    write_st(os.path.join(out, "shard1.safetensors"), st[:2], {"format": "pt"})
    write_st(os.path.join(out, "shard2.safetensors"), st[2:], None)
    nan = struct.pack("<4H", 0x3C00, 0x7E00, 0x3C00, 0x3C00)
    write_st(os.path.join(out, "nan.safetensors"), [("w", 1, [4], nan)], {"license": "mit"})
    wide = struct.pack("<2I", 0x3F800000, 0x00000001)
    write_st(os.path.join(out, "wide.safetensors"), [("w", 0, [2], wide)], {"license": "mit"})


if __name__ == "__main__":
    main()
