# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# Generate test_q5_fixture.h: GGUF Q5_0 and Q5_1 blocks and their expected
# dequantised values in the tensor engine's Q16 format. Pure Python, no
# dependencies. Usage:
#   python3 gen_q5_fixture.py test_q5_fixture.h
#
# The decoder below is a line-for-line transcription of ggml's reference
# dequantize_row_q5_0 / dequantize_row_q5_1 (ggml-quants.c):
#   xh_0 = ((qh >> (j + 0)) << 4) & 0x10;
#   xh_1 = ((qh >> (j + 12))     ) & 0x10;
#   x0 = ((qs[j] & 0x0F) | xh_0) [- 16 for Q5_0];
#   x1 = ((qs[j] >>   4) | xh_1) [- 16 for Q5_0];
#   y[j] = x0*d [+ m];  y[j + 16] = x1*d [+ m];
# Two expectations are written per value:
#   EXACT: round(value * 2^16) of the exact rational value, rounding half
#          away from zero and saturating to int32 (what zt_gguf_dequant
#          computes; the C test requires bit-exact agreement);
#   GGML:  the same rounding applied to the f32 value ggml produces (ggml
#          computes x*d + m in f32; x*d is exact in f32, so Q5_0 equals
#          EXACT and Q5_1 may differ by the one f32 rounding of the add).
# When gguf-py happens to be installed, its dequantize is used as an extra
# cross-check of the f32 values (optional; not needed to regenerate).
import random
import struct
import sys
from fractions import Fraction


def f16_value(bits):
    """Exact value of an IEEE half as a Fraction (None for inf/NaN)."""
    s = -1 if bits >> 15 else 1
    ex = (bits >> 10) & 0x1F
    mant = bits & 0x3FF
    if ex == 0x1F:
        return None
    if ex == 0:
        return s * Fraction(mant, 1 << 24)
    return s * Fraction(mant | 0x400, 1) * Fraction(2) ** (ex - 25)


def f32_round(x):
    """Fraction -> nearest normal f32 (ties to even), exactly, as a Fraction.
    (Every value here is a multiple of 2^-24 below 2^22, so no f32
    subnormals or overflow arise.)"""
    if x == 0:
        return Fraction(0)
    s = -1 if x < 0 else 1
    a = abs(x)
    e = a.numerator.bit_length() - a.denominator.bit_length()
    while Fraction(2) ** e > a:
        e -= 1
    while Fraction(2) ** (e + 1) <= a:
        e += 1
    # a in [2^e, 2^(e+1)): keep 24 significant bits
    scaled = a / Fraction(2) ** (e - 23)
    q, r = divmod(scaled.numerator, scaled.denominator)
    if 2 * r > scaled.denominator or (2 * r == scaled.denominator and q & 1):
        q += 1
    return s * q * Fraction(2) ** (e - 23)


def q16(x):
    """round(x * 2^16), half away from zero, saturating to int32."""
    v = x * 65536
    mag = abs(v)
    r = int(mag + Fraction(1, 2))  # floor(mag + 1/2)
    if v < 0:
        return -min(r, 1 << 31)
    return min(r, (1 << 31) - 1)


def ggml_q5(block, with_min):
    """ggml's reference decoder: returns 32 exact Fractions and 32 f32s."""
    d = f16_value(block[0] | block[1] << 8)
    off = 2
    m = Fraction(0)
    if with_min:
        m = f16_value(block[2] | block[3] << 8)
        off = 4
    qh = struct.unpack('<I', bytes(block[off:off + 4]))[0]
    qs = block[off + 4:off + 20]
    exact = [None] * 32
    for j in range(16):
        xh_0 = ((qh >> (j + 0)) << 4) & 0x10
        xh_1 = ((qh >> (j + 12))) & 0x10
        x0 = ((qs[j] & 0x0F) | xh_0)
        x1 = ((qs[j] >> 4) | xh_1)
        if not with_min:
            x0 -= 16
            x1 -= 16
        exact[j] = x0 * d + m
        exact[j + 16] = x1 * d + m
    # f32: the product is exact (5-bit times 11-bit), the add rounds once
    f32 = [f32_round(v) for v in exact]
    return exact, f32


def f16_bits(x):
    return struct.unpack('<H', struct.pack('<e', x))[0]


def le16(v):
    return [v & 0xFF, v >> 8]


def le32(v):
    return list(struct.pack('<I', v))


rng = random.Random(20261010)


def rnd_qs():
    return [rng.randrange(256) for _ in range(16)]


def q5_0_block(d_bits, qh, qs):
    return le16(d_bits) + le32(qh) + qs


def q5_1_block(d_bits, m_bits, qh, qs):
    return le16(d_bits) + le16(m_bits) + le32(qh) + qs


ramp = [(i & 0xF) | ((15 - (i & 0xF)) << 4) for i in range(16)]
q5_0 = [
    # layout probes: one high bit at a time, low half then high half
    q5_0_block(f16_bits(1.0), 0x00000001, [0] * 16),
    q5_0_block(f16_bits(1.0), 0x00010000, [0] * 16),
    q5_0_block(f16_bits(1.0), 0x80008000, [0xFF] * 16),
    q5_0_block(f16_bits(0.5), 0xFFFFFFFF, ramp),
    q5_0_block(f16_bits(0.25), 0x00000000, ramp),
    q5_0_block(f16_bits(-0.03125), 0xA5A50F0F, rnd_qs()),  # negative d
    q5_0_block(0x0001, 0x12345678, rnd_qs()),  # smallest subnormal d
    q5_0_block(0x03FF, 0xDEADBEEF, rnd_qs()),  # largest subnormal d
    q5_0_block(0x0000, 0xFFFFFFFF, rnd_qs()),  # d = 0
    q5_0_block(0x7BFF, 0x0000FFFF, ramp),  # d = 65504: saturates
    q5_0_block(f16_bits(4096.0), 0xF0F0F0F0, rnd_qs()),  # near the Q16 limit
]
for _ in range(13):
    d = f16_bits(rng.uniform(1e-4, 4e-2))
    q5_0.append(q5_0_block(d, rng.getrandbits(32), rnd_qs()))

q5_1 = [
    q5_1_block(f16_bits(1.0), f16_bits(0.0), 0x00000001, [0] * 16),
    q5_1_block(f16_bits(1.0), f16_bits(0.0), 0x00010000, [0] * 16),
    q5_1_block(f16_bits(1.0), f16_bits(-16.0), 0x80008000, [0xFF] * 16),
    q5_1_block(f16_bits(0.5), f16_bits(-7.75), 0xFFFFFFFF, ramp),
    # exponents far apart: the f32 add rounds, so EXACT and GGML differ
    q5_1_block(f16_bits(1024.0), f16_bits(0.7509765625), 0x5A5A5A5A, rnd_qs()),
    q5_1_block(0x0001, f16_bits(-0.0009765625), 0x0F0F0F0F, rnd_qs()),
    q5_1_block(f16_bits(-0.015625), f16_bits(0.25), 0x33333333, rnd_qs()),  # negative d
    q5_1_block(0x0000, f16_bits(-3.0), 0xFFFFFFFF, rnd_qs()),  # d = 0
    q5_1_block(0x7BFF, 0x7BFF, 0xFFFFFFFF, ramp),  # saturates
    q5_1_block(0x7BFF, 0xFBFF, 0x00000000, [0x00] * 16),  # d*0 - 65504: saturates negative
    q5_1_block(f16_bits(1000.0), f16_bits(-31000.0), 0xFFFF0000, ramp),
]
for _ in range(13):
    d = f16_bits(rng.uniform(1e-4, 4e-2))
    m = f16_bits(-rng.uniform(0, 0.6))
    q5_1.append(q5_1_block(d, m, rng.getrandbits(32), rnd_qs()))


def decode_all(blocks, with_min):
    ex, fl = [], []
    for b in blocks:
        e, f = ggml_q5(b, with_min)
        ex += e
        fl += f
    return ex, fl


q50_exact, q50_f32 = decode_all(q5_0, False)
q51_exact, q51_f32 = decode_all(q5_1, True)

# optional cross-check against gguf-py's dequantiser
try:
    import numpy as np
    from gguf import quants, GGMLQuantizationType as T
    for blocks, typ, ref in ((q5_0, T.Q5_0, q50_f32), (q5_1, T.Q5_1, q51_f32)):
        raw = np.array([x for b in blocks for x in b], dtype=np.uint8)
        got = quants.dequantize(raw, typ).astype(np.float32).reshape(-1)
        for i, v in enumerate(got):
            if np.isfinite(v):
                assert Fraction(float(v)) == ref[i], (typ, i, float(v), float(ref[i]))
    print('gguf-py cross-check: agrees')
except ImportError:
    print('gguf-py not installed: cross-check skipped')

out = [
    '/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC',
    ' * SPDX-License-Identifier: Apache-2.0 */',
    '/* test_q5_fixture.h - generated by gen_q5_fixture.py. Do not edit.',
    ' * GGUF Q5_0 (22-byte) and Q5_1 (24-byte) blocks, and per element the',
    ' * Q16 rounding of the exact value (EXACT) and of the f32 value ggml\'s',
    ' * reference dequantize_row_q5_0/q5_1 produces (GGML). */',
    '#include <stdint.h>',
]


def emit_bytes(name, blocks):
    flat = [x for b in blocks for x in b]
    out.append('static const uint8_t %s[%d] = {' % (name, len(flat)))
    for i in range(0, len(flat), 16):
        out.append('    ' + ', '.join('0x%02x' % x for x in flat[i:i + 16]) + ',')
    out.append('};')


def emit_i32(name, vals):
    out.append('static const int32_t %s[%d] = {' % (name, len(vals)))
    for i in range(0, len(vals), 8):
        out.append('    ' + ', '.join(str(v) for v in vals[i:i + 8]) + ',')
    out.append('};')


out.append('#define Q5_0_NBLK %d' % len(q5_0))
out.append('#define Q5_1_NBLK %d' % len(q5_1))
emit_bytes('Q5_0_BLOCKS', q5_0)
emit_i32('Q5_0_EXACT', [q16(v) for v in q50_exact])
emit_i32('Q5_0_GGML', [q16(v) for v in q50_f32])
emit_bytes('Q5_1_BLOCKS', q5_1)
emit_i32('Q5_1_EXACT', [q16(v) for v in q51_exact])
emit_i32('Q5_1_GGML', [q16(v) for v in q51_f32])
open(sys.argv[1], 'w').write('\n'.join(out) + '\n')
ndiff = sum(q16(a) != q16(b) for a, b in zip(q51_exact, q51_f32))
print('%d Q5_0 + %d Q5_1 blocks; %d Q5_1 values where f32 rounding moves the Q16 result'
      % (len(q5_0), len(q5_1), ndiff))
