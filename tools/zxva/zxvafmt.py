#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""zxvafmt.py - write ZXVA, the native vector audio format.

The byte layout is specified in kernel/src/vaudio/ZXVA.md and read by
kernel/src/vaudio/zxva.c; this module must agree with both. Used by
wav2zxva.py (analysis) and author.py (hand-written sounds). Standard library
only; numpy is used for the residual coder when it is importable.
"""
import math
import struct

MAXCODE = 6144          # level code of 0 dBFS; 256 codes per octave (6.02 dB)
FMAX = (1 << 31) - 1    # frequencies in Q16 Hz are below 2^31 (32768 Hz)
US = 1000000
RES_BLOCK = 4096

T_END, T_PARTIAL, T_NOISE, T_GRAINS, T_VOICE, T_RESIDUAL, T_FVOICE, T_NORM = range(8)


def uv(v):
    v = int(v)
    if v < 0 or v >= 1 << 64:
        raise ValueError("uv out of range: %d" % v)
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def uv32(v):
    if not 0 <= int(v) < 1 << 32:
        raise ValueError("uv32 out of range: %d" % v)
    return uv(v)


def zz(v):
    v = int(v)
    if not -(1 << 31) <= v < 1 << 31:
        raise ValueError("zz out of range: %d" % v)
    return uv32(v << 1 if v >= 0 else ((-v) << 1) - 1)


def zz64(v):
    v = int(v)
    return uv((-v << 1) - 1 if v < 0 else v << 1)


def level_code(a):
    """linear amplitude (1.0 = full scale) -> level code (0 = silence)."""
    if a <= 2.0 ** -24:
        return 0
    c = int(round(MAXCODE + 256.0 * math.log2(a)))
    return max(1, min(MAXCODE, c))


def code_level(c):
    return 0.0 if c <= 0 else 2.0 ** ((c - MAXCODE) / 256.0)


def hz16(f):
    return max(0, min(FMAX, int(round(f * 65536.0))))


def nat_q64(fa, fb, d_us):
    """exact phase advance of a segment, Q64 turns (zxva.c pt_next)."""
    m = 2 * US * 65536
    return (((fa + fb) * d_us) % m << 64) // m


class Writer:
    def __init__(self, nch, dur_us, seed=0):
        if not 1 <= nch <= 8:
            raise ValueError("1..8 channels")
        self.nch, self.dur, self.seed = nch, int(dur_us), int(seed) & 0xFFFFFFFF
        self.chunks = []
        self.res = None
        self.grain_list = []

    def _out(self, ch, pan):
        b = bytes([ch])
        return b + uv32(pan)

    # PARTIAL. bps: [(t_us, f_hz, amp_linear, phase_turns or None)], times
    # ascending, gaps 100..2^22 us. locked=True stores the phase at every
    # breakpoint (as a correction to the integral of frequency).
    def partial(self, bps, ch=0, pan=0, locked=False):
        if len(bps) < 2:
            raise ValueError("a partial needs 2 breakpoints")
        t0, f0, a0, p0 = bps[0]
        fa, ca = hz16(f0), level_code(a0)
        ph0 = int(round((p0 or 0.0) * 65536)) & 0xFFFF
        out = bytearray([T_PARTIAL, ch]) + uv32(pan) + bytes([1 if locked else 0])
        out += uv32(int(t0)) + uv32(len(bps)) + uv32(fa) + uv32(ca) + struct.pack("<H", ph0)
        phi = ph0 << 48
        ta = int(t0)
        for t, f, a, p in bps[1:]:
            t = int(t)
            fb, cb = hz16(f), level_code(a)
            d = t - ta
            if not 100 <= d <= 1 << 22:
                raise ValueError("breakpoint gap %d us out of 100..2^22" % d)
            out += uv32(d) + zz(fb - fa) + zz(cb - ca)
            pred = (phi + nat_q64(fa, fb, d)) & ((1 << 64) - 1)
            dph = 0
            if locked:
                tgt = int(round((p or 0.0) * 2.0 ** 64)) & ((1 << 64) - 1)
                diff = (tgt - pred) & ((1 << 64) - 1)
                if diff >= 1 << 63:
                    diff -= 1 << 64
                dph = max(-32768, min(32767, int(round(diff / 2.0 ** 48))))
                out += zz(dph)
            phi = (pred + (dph << 48)) & ((1 << 64) - 1)
            ta, fa, ca = t, fb, cb
        if ta > self.dur:
            raise ValueError("partial ends after the sound")
        self.chunks.append(bytes(out))

    # NOISE. edges: nb+1 ascending Hz (int); frames: list of [amp per band],
    # one every hop_us starting at t0.
    def noise(self, t0, hop, edges, frames, ch=0, pan=0):
        nb = len(edges) - 1
        out = bytearray([T_NOISE, ch]) + uv32(pan) + uv32(t0) + uv32(hop) + uv32(len(frames))
        out += bytes([nb]) + b"".join(uv32(e) for e in edges)
        prev = None
        for fr in frames:
            codes = [level_code(a) for a in fr]
            if prev is None:
                out += b"".join(uv32(c) for c in codes)
            else:
                out += b"".join(zz(c - p) for c, p in zip(codes, prev))
            prev = codes
        self.chunks.append(bytes(out))

    # GRAINS (collected; written as one chunk sorted by start)
    def grain(self, t0, dur, f, amp, kind=0, bw=0.0, attack=0, half=0, phase=0.0, ch=0, pan=0):
        self.grain_list.append((int(t0), kind, ch, pan, int(dur), int(attack), int(half),
                                hz16(f), hz16(bw), level_code(amp),
                                int(round(phase * 65536)) & 0xFFFF))

    def _grains_chunk(self):
        g = sorted(self.grain_list, key=lambda x: x[0])
        out = bytearray([T_GRAINS]) + uv32(len(g))
        prev = 0
        for t0, kind, ch, pan, dur, att, half, f, bw, code, ph in g:
            out += uv32(t0 - prev) + bytes([kind, ch]) + uv32(pan) + uv32(dur) + uv32(att)
            out += uv32(half) + uv32(f) + uv32(bw) + uv32(code) + struct.pack("<H", ph)
            prev = t0
        return bytes(out)

    # VOICE. env: [(t_us from voice start, amp)], first t = 0.
    def _voice_head(self, kind, t0, f, env, ch, pan):
        out = bytearray([T_VOICE, kind, ch]) + uv32(pan) + uv32(int(t0)) + uv32(hz16(f))
        out += bytes([len(env)])
        pt = 0
        for t, a in env:
            out += uv32(int(t) - pt) + uv32(level_code(a))
            pt = int(t)
        return out

    def voice_modal(self, t0, f, env, partials, ch=0, pan=0):
        """partials: [(ratio, amp, half_life_us or 0)]"""
        out = self._voice_head(0, t0, f, env, ch, pan) + bytes([len(partials)])
        for r, a, h in partials:
            out += uv32(int(round(r * 65536))) + uv32(level_code(a)) + uv32(int(h))
        self.chunks.append(bytes(out))

    def voice_fm(self, t0, f, env, ratio, index0, index1, ch=0, pan=0):
        out = self._voice_head(1, t0, f, env, ch, pan)
        out += uv32(int(round(ratio * 65536))) + uv32(int(round(index0 * 65536)))
        out += uv32(int(round(index1 * 65536)))
        self.chunks.append(bytes(out))

    def voice_pluck(self, t0, f, env, half1_us, damp, pos, ch=0, pan=0):
        """damp 0..1 (high-harmonic damping), pos 0..0.5 (pluck point)"""
        out = self._voice_head(2, t0, f, env, ch, pan)
        out += uv32(int(half1_us)) + uv32(int(round(damp * 65536)))
        out += uv32(max(1, int(round(pos * 65536))))
        self.chunks.append(bytes(out))

    # FVOICE: frame recipe at native rate `rate` (see zxva.h FVOICE semantics)
    def fvoice(self, rate, start, length, kind, gains, env=(0, 0, 0), echo=None, ch=0,
               partials=None, noise=None, sweep=None):
        out = bytearray([T_FVOICE]) + uv32(rate) + uv32(start) + uv32(length)
        out += bytes([kind, ch]) + zz64(gains[0]) + zz64(gains[1])
        out += bytes([env[0]]) + uv32(env[1]) + uv32(env[2])
        if echo:
            n, delay, gain, mirror = echo
            out += bytes([n]) + uv32(delay) + uv32(gain) + bytes([1 if mirror else 0])
        else:
            out += bytes([0])
        if kind == 0:
            out += bytes([len(partials)])
            for f_mhz, amp_q14, att, dec_q30 in partials:
                out += uv32(f_mhz) + uv32(amp_q14) + uv32(att) + uv32(dec_q30)
        elif kind == 1:
            seed, mul, add, alp, ahp, amp = noise
            out += b"".join(uv32(x) for x in (seed, mul, add, alp, ahp, amp))
        else:
            f0, glide, amp = sweep
            out += uv32(f0) + uv32(glide) + uv32(amp)
        self.chunks.append(bytes(out))

    def norm(self, peak_q40, target_q40):
        self.chunks.append(bytes([T_NORM]) + uv(peak_q40) + uv(target_q40))

    def residual(self, rate, bits, chans):
        """chans: per channel, a list/array of int residual samples (same
        length, = ceil(dur * rate / 1e6))."""
        self.res = (rate, bits, chans)

    def parametric_bytes(self):
        out = bytearray(b"ZXVA\x01\x00") + uv32(self.nch) + uv32(self.dur) + uv32(self.seed)
        for c in self.chunks:
            out += c
        if self.grain_list:
            out += self._grains_chunk()
        return bytes(out)

    def bytes(self):
        out = bytearray(self.parametric_bytes())
        if self.res:
            out += residual_chunk(*self.res, nch=self.nch)
        out.append(T_END)
        return bytes(out)


# ---- residual coder: fixed predictor (order 0..3) + Rice, per block ------
class _Bits:
    def __init__(self):
        self.acc, self.n, self.out = 0, 0, bytearray()

    def put(self, v, n):
        self.acc = (self.acc << n) | (v & ((1 << n) - 1))
        self.n += n
        while self.n >= 8:
            self.n -= 8
            self.out.append((self.acc >> self.n) & 0xFF)
        self.acc &= (1 << self.n) - 1

    def done(self):
        if self.n:
            self.out.append((self.acc << (8 - self.n)) & 0xFF)
        return bytes(self.out)


def _pred_err(x, order):
    e = []
    for i, s in enumerate(x):
        o = min(i, order)
        if o == 0:
            p = 0
        elif o == 1:
            p = x[i - 1]
        elif o == 2:
            p = 2 * x[i - 1] - x[i - 2]
        else:
            p = 3 * x[i - 1] - 3 * x[i - 2] + x[i - 3]
        e.append(s - p)
    return e


def _rice_cost(us, k):
    return sum(40 + 32 if (u >> k) >= 32 else (u >> k) + 1 + k for u in us)


def encode_block(x):
    best = None
    for order in range(4):
        us = [(e << 1) if e >= 0 else ((-e) << 1) - 1 for e in _pred_err(x, order)]
        mean = sum(us) / max(1, len(us))
        k0 = max(0, min(40, int(math.log2(mean + 1)) if mean > 0 else 0))
        for k in range(max(0, k0 - 2), min(40, k0 + 2) + 1):
            c = _rice_cost(us, k)
            if best is None or c < best[0]:
                best = (c, order, k, us)
    _, order, k, us = best
    b = _Bits()
    b.put(order, 2)
    b.put(k, 6)
    for u in us:
        q = u >> k
        if q >= 32:
            b.put((1 << 32) - 1, 32)
            b.put(u, 40)
        else:
            b.put(((1 << q) - 1) << 1, q + 1)
            if k:
                b.put(u, k)
    return b.done()


def residual_chunk(rate, bits, chans, nch):
    nf = len(chans[0])
    out = bytearray([T_RESIDUAL]) + uv32(rate) + bytes([bits]) + uv(nf)
    for b0 in range(0, nf, RES_BLOCK):
        for c in range(nch):
            blk = encode_block([int(v) for v in chans[c][b0:b0 + RES_BLOCK]])
            out += uv32(len(blk)) + blk
    return bytes(out)


def c_array(data, name, comment):
    lines = ["/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */",
             "/* SPDX-License-Identifier: Apache-2.0 */",
             "/* Generated by kernel/src/vaudio/test/gen_fixtures.py; do not edit. */",
             "/* %s */" % comment,
             "static const uint8_t %s[%d] = {" % (name, len(data))]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines) + "\n"
