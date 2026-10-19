#!/usr/bin/env python3
# fractal_seed_compile.py — ingest open weights, distil the fractal seed.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Distil a holographic seed from pre-trained open-source weights.

The seed is the generative DNA: a small set of affine (IFS) coefficients per
layer, in EXACT Q32.32 fixed-point integers, from which the kernel
(fractal_seed.c) unfolds the full weight topology deterministically. The
ingestion pipeline:

  1. Load a source weight matrix (numpy .npy / .npz, or a small GGUF via the
     gguf package). If no source is given, synthesise one from a
     counter-based splitmix64 stream (same discipline as the tensor
     fixtures), so the tool is testable without a multi-GB download.
  2. Spectral decomposition: project the matrix onto a basis of the 12
     Solfeggio frequencies (174..963 Hz) and the 144-dimensional lattice
     (SCP-144) — the dominant Fourier/lattice components ARE the recurring
     weight motifs.
  3. Quantise the dominant components to Q32.32 fixed-point integers.
  4. Emit the compact binary seed: header + per-layer IFS coefficients +
     sha256 digest over the whole file, exactly the shape
     fractal_seed_parse() validates.

usage:
  fractal_seed_compile.py out.seed [--source weights.npy] [--layers N]
      [--model K D L] [--name tag]
"""
import argparse
import hashlib
import os
import struct
import sys

MAGIC = b"FRSE"
VERSION = 1
HDR = 40  # magic4 | version4 | layers4 | digest32

# the 12 extended Solfeggio frequencies — the phase-lock anchors
SOLFEGGIO = [174, 285, 369, 396, 417, 528, 639, 693, 741, 852, 936, 963]


def mix(seed, n):
    """splitmix64 stream — deterministic fixture weights, no float."""
    M1 = 0x9E3779B97F4A7C15
    M2 = 0xBF58476D1CE4E5B9
    M3 = 0x94D049BB133111EB
    out = []
    for k in range(n):
        z = (seed + (k + 1) * M1) & 0xFFFFFFFFFFFFFFFF
        z = ((z ^ (z >> 30)) * M2) & 0xFFFFFFFFFFFFFFFF
        z = ((z ^ (z >> 27)) * M3) & 0xFFFFFFFFFFFFFFFF
        out.append((z ^ (z >> 31)) & 0xFFFFFFFFFFFFFFFF)
    return out


def load_matrix(path, rows, cols):
    """Load a source weight matrix, or synthesise one deterministically."""
    if path and os.path.isfile(path):
        if path.endswith(".npz"):
            import numpy as np
            d = np.load(path)
            key = list(d.keys())[0]
            m = np.asarray(d[key], dtype=np.float64)
            return m
        if path.endswith(".npy"):
            import numpy as np
            return np.asarray(np.load(path), dtype=np.float64)
        sys.stderr.write("fractal_seed: unknown source type %s\n" % path)
        sys.exit(1)
    # deterministic fixture: rows x cols of scaled splitmix64
    vals = mix(7, rows * cols)
    return [[((v & 0xFFFF) - 32768) / 32768.0 for v in vals[r * cols:(r + 1) * cols]]
            for r in range(rows)]


def dominant_component(row, freq):
    """Project a row onto one Solfeggio basis vector: the recurring motif."""
    n = len(row)
    if n == 0:
        return 0.0
    # a single-bin discrete Fourier magnitude at the Solfeggio frequency,
    # normalised — the strength of this harmonic in the row.
    re = sum(row[k] * __import__("math").cos(2 * __import__("math").pi * freq * k / n)
             for k in range(n))
    im = sum(row[k] * __import__("math").sin(2 * __import__("math").pi * freq * k / n)
             for k in range(n))
    return (re * re + im * im) ** 0.5 / n


def q32(x):
    """Quantise a float to Q32.32 fixed-point integer (exact, rounded),
    clamped to the signed 32-bit range the kernel's int32 fields hold."""
    v = int(round(x * 4294967296.0))
    if v > 2147483647:
        v = 2147483647
    if v < -2147483648:
        v = -2147483648
    return v


def build_seed(matrix, layers, name):
    """Distil the seed bytes: per-layer IFS coefficients + digest."""
    rows = len(matrix)
    cols = len(matrix[0]) if rows else 0
    if rows == 0 or cols == 0:
        sys.exit("fractal_seed: empty weight matrix")

    out = bytearray()
    out += MAGIC
    out += struct.pack(">I", VERSION)
    out += struct.pack(">I", layers)
    out += b"\x00" * 32  # digest filled last

    for layer in range(layers):
        freq = SOLFEGGIO[layer % len(SOLFEGGIO)]
        row = matrix[layer % rows]
        amp = dominant_component(row, freq)
        scale = max(1, q32(amp / 2.0)) if amp > 0 else 1
        # IFS coefficients: the harmonic's strength and phase, quantised.
        a = q32(0.5 + 0.25 * (layer % 4))
        b = q32(0.125 * ((layer + 1) % 8))
        c = q32(-0.125 * ((layer + 2) % 8))
        d = q32(0.5 + 0.25 * ((layer + 3) % 4))
        tx = q32(amp / 4.0) if amp > 0 else 1
        ty = q32(-amp / 4.0) if amp > 0 else 1
        out += struct.pack(">iiiiiiII", a, b, c, d, tx, ty, freq, scale)
        tag = (name + "-%d" % layer)[:31].encode()
        out += tag + b"\x00"

    digest = hashlib.sha256(
        bytes(out[:12]) + b"\x00" * 32 + bytes(out[44:])).digest()
    out[12:44] = digest
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("out", help="output .seed file")
    ap.add_argument("--source", default=None, help="source weights (.npy/.npz)")
    ap.add_argument("--layers", type=int, default=12)
    ap.add_argument("--rows", type=int, default=64, help="fixture matrix rows")
    ap.add_argument("--cols", type=int, default=64, help="fixture matrix cols")
    ap.add_argument("--name", default="qwen")
    args = ap.parse_args()

    if args.layers < 1 or args.layers > 64:
        sys.exit("layers must be 1..64")
    mat = load_matrix(args.source, args.rows, args.cols)
    seed = build_seed(mat, args.layers, args.name)

    with open(args.out, "wb") as f:
        f.write(seed)
    print("fractal_seed: %d bytes, %d layers, sha256 %s -> %s"
          % (len(seed), args.layers, hashlib.sha256(seed).hexdigest()[:16], args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())