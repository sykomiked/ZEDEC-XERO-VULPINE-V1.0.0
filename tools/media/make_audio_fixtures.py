#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""make_audio_fixtures.py - test recordings for kernel/src/media/zui_audio.c.

    python3 tools/media/make_audio_fixtures.py [--out kernel/src/media/test/fx_audio.inc]

Writes WAV, AIFF/AIFC and FLAC files as C arrays, each with the FNV-1a hash
of the int32 samples a correct decoder must produce (computed here from the
integer source, independently of the decoder). FLAC comes from two places:
  * the reference encoder (`flac`, apt package flac; needed to regenerate),
    for realistic streams: LPC, mid/side, 8/16/24/32 bit, 6 channels, odd
    block sizes, constant and verbatim subframes;
  * fl_encode() below, a small FLAC writer that forces every rare path the
    reference encoder seldom or never takes: variable block size, every
    stereo decorrelation, escape-coded partitions, wasted bits, LPC order 32
    at precision 15, 5-bit Rice parameters, and every frame-header code for
    block size and sample rate. Each such stream is checked with `flac -t`
    (the reference decoder, MD5 included) before it is written.
Standard library only (plus the flac binary).
"""
import hashlib
import math
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "kernel", "src", "media", "test", "fx_audio.inc")


def fnv(samples):
    h = 2166136261
    for v in samples:
        u = v & 0xFFFFFFFF
        for k in range(4):
            h = ((h ^ ((u >> (8 * k)) & 0xFF)) * 16777619) & 0xFFFFFFFF
    return h


def signal(n, nch, bits, seed, kind="tone"):
    """interleaved ints in the signed `bits` range"""
    rnd = random.Random(seed)
    top = (1 << (bits - 1)) - 1
    out = []
    for i in range(n):
        for c in range(nch):
            if kind == "noise":
                v = rnd.randint(-top - 1, top)
            elif kind == "silence":
                v = 0
            else:
                x = 0.5 * math.sin(2 * math.pi * (220 * (c + 1)) * i / 8000.0)
                x += 0.2 * math.sin(2 * math.pi * 1375 * i / 8000.0 + c)
                x += 0.01 * (rnd.random() - 0.5)
                v = int(round(x * top))
            out.append(max(-top - 1, min(top, v)))
    return out


# ---- WAV / AIFF writers ----------------------------------------------------
def pack_int(v, nbytes, big=False, unsigned8=False):
    if unsigned8:
        return bytes([(v + 128) & 0xFF])
    u = v & ((1 << (8 * nbytes)) - 1)
    b = u.to_bytes(nbytes, "big" if big else "little")
    return b


def wav(samples, nch, rate, bits, tag=1, ext=False, valid=None, raw=None):
    nb = bits // 8
    if raw is None:
        raw = b"".join(pack_int(v, nb, unsigned8=(bits == 8)) for v in samples)
    if ext:
        sub = struct.pack("<H", tag) + bytes([0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71])
        fmt = struct.pack("<HHIIHHHHI", 0xFFFE, nch, rate, rate * nch * nb, nch * nb, bits, 22,
                          valid or bits, 0) + sub
    else:
        fmt = struct.pack("<HHIIHH", tag, nch, rate, rate * nch * nb, nch * nb, bits)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
    body += b"LIST" + struct.pack("<I", 5) + b"INFOx" + b"\0"   # odd chunk + pad byte
    body += b"data" + struct.pack("<I", len(raw)) + raw
    if len(raw) & 1:
        body += b"\0"
    return b"RIFF" + struct.pack("<I", len(body)) + body


def ext80(rate):
    e = rate.bit_length() - 1
    m = rate << (63 - e)
    return struct.pack(">HQ", 16383 + e, m)


def aiff(samples, nch, rate, bits, comp=None, raw=None):
    nb = (bits + 7) // 8
    frames = len(samples) // nch if raw is None else len(raw) // (nch * nb)
    if raw is None:
        sh = nb * 8 - bits
        if comp == b"sowt":
            raw = b"".join(pack_int(v << sh, nb) for v in samples)
        else:
            raw = b"".join(pack_int(v << sh, nb, big=True) for v in samples)
    comm = struct.pack(">hIh", nch, frames, bits) + ext80(rate)
    form = b"AIFF"
    if comp is not None:
        comm += comp + b"\x04none\0"   # pstring name, padded to even
        form = b"AIFC"
    body = form + b"COMM" + struct.pack(">I", len(comm)) + comm
    if form == b"AIFC":
        body += b"FVER" + struct.pack(">II", 4, 0xA2805140)
    body += b"SSND" + struct.pack(">III", len(raw) + 8, 0, 0) + raw
    return b"FORM" + struct.pack(">I", len(body)) + body


def f32bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def f_to_i32(x):
    """the conversion zui_audio promises: round half to even, saturate"""
    if x != x:
        return 0
    v = x * 2.0 ** 31
    if v >= 2 ** 31 - 1:
        return 2 ** 31 - 1
    if v <= -2 ** 31:
        return -2 ** 31
    return int(round(v))   # Python's round is half to even


# ---- FLAC writer (rare paths) -----------------------------------------------
class Bits:
    def __init__(self):
        self.acc, self.n, self.out = 0, 0, bytearray()

    def put(self, v, n):
        if n == 0:
            return
        self.acc = (self.acc << n) | (v & ((1 << n) - 1))
        self.n += n
        while self.n >= 8:
            self.n -= 8
            self.out.append((self.acc >> self.n) & 0xFF)
        self.acc &= (1 << self.n) - 1

    def align(self):
        if self.n:
            self.put(0, 8 - self.n)


def crc8(b):
    c = 0
    for x in b:
        c ^= x
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


def crc16(b):
    c = 0
    for x in b:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x8005) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def utf8num(v):
    if v < 0x80:
        return bytes([v])
    n = 2
    while v >= 1 << (5 * n + 1):
        n += 1
    out = []
    for _ in range(n - 1):
        out.append(0x80 | (v & 0x3F))
        v >>= 6
    lead = ((0xFF << (8 - n)) & 0xFF) | v
    return bytes([lead] + out[::-1])


def residual(bw, res, order, bs, porder=0, method=0, escape=False):
    bw.put(method, 2)
    bw.put(porder, 4)
    parts = 1 << porder
    pbits, esc = (5, 31) if method else (4, 15)
    i = 0
    for p in range(parts):
        cnt = bs // parts - (order if p == 0 else 0)
        chunk = res[i:i + cnt]
        i += cnt
        if escape:
            nb = max([abs(v).bit_length() + 1 for v in chunk] + [1])
            bw.put(esc, pbits)
            bw.put(nb, 5)
            for v in chunk:
                bw.put(v, nb)
            continue
        best = None
        for k in range(esc):
            cost = sum(((v << 1) ^ (v >> 63)) >> k for v in chunk) + len(chunk) * (k + 1)
            if best is None or cost < best[0]:
                best = (cost, k)
        k = best[1]
        bw.put(k, pbits)
        for v in chunk:
            u = (v << 1) if v >= 0 else ((-v) << 1) - 1
            q = u >> k
            bw.put(0, q)
            bw.put(1, 1)
            bw.put(u & ((1 << k) - 1), k)


def subframe(bw, x, bits, kind, **o):
    """x: one channel's samples; kind: constant|verbatim|fixed|lpc"""
    bs = len(x)
    wasted = o.get("wasted", 0)
    if wasted:
        assert all(v % (1 << wasted) == 0 for v in x)
        x = [v >> wasted for v in x]
        bits -= wasted
    bw.put(0, 1)
    if kind == "constant":
        bw.put(0, 6)
    elif kind == "verbatim":
        bw.put(1, 6)
    elif kind == "fixed":
        bw.put(8 + o["order"], 6)
    else:
        bw.put(31 + o["order"], 6)
    if wasted:
        bw.put(1, 1)
        bw.put(0, wasted - 1)
        bw.put(1, 1)
    else:
        bw.put(0, 1)
    if kind == "constant":
        assert len(set(x)) == 1
        bw.put(x[0], bits)
    elif kind == "verbatim":
        for v in x:
            bw.put(v, bits)
    elif kind == "fixed":
        order = o["order"]
        for v in x[:order]:
            bw.put(v, bits)
        co = [[], [1], [2, -1], [3, -3, 1], [4, -6, 4, -1]][order]
        res = [x[i] - sum(c * x[i - 1 - j] for j, c in enumerate(co)) for i in range(order, bs)]
        residual(bw, res, order, bs, o.get("porder", 0), o.get("method", 0), o.get("escape", False))
    else:
        order, prec, shift, co = o["order"], o["prec"], o["shift"], o["coefs"]
        for v in x[:order]:
            bw.put(v, bits)
        bw.put(prec - 1, 4)
        bw.put(shift, 5)
        for c in co:
            bw.put(c, prec)
        res = [x[i] - (sum(c * x[i - 1 - j] for j, c in enumerate(co)) >> shift)
               for i in range(order, bs)]
        residual(bw, res, order, bs, o.get("porder", 0), o.get("method", 0), o.get("escape", False))


BSC = {192: 1, 576: 2, 1152: 3, 2304: 4, 4608: 5, 256: 8, 512: 9, 1024: 10, 2048: 11, 4096: 12}
RATEC = {88200: 1, 176400: 2, 192000: 3, 8000: 4, 16000: 5, 22050: 6, 24000: 7, 32000: 8,
         44100: 9, 48000: 10, 96000: 11}
SSC = {8: 1, 12: 2, 16: 4, 20: 5, 24: 6, 32: 7}


def frame(chans, bits, rate, num, ca, subs, rc=None, ss=None, variable=False, force_bsc=None):
    bs = len(chans[0])
    hb = Bits()
    hb.put(0x7FFC, 15)
    hb.put(1 if variable else 0, 1)
    bsc = force_bsc if force_bsc is not None else BSC.get(bs, 6 if bs <= 256 else 7)
    if rc is None:
        rc = 0
    hb.put(bsc, 4)
    hb.put(rc, 4)
    hb.put(ca, 4)
    hb.put(SSC[bits] if ss else 0, 3)
    hb.put(0, 1)
    head = bytes(hb.out) + utf8num(num)
    if bsc == 6:
        head += bytes([bs - 1])
    elif bsc == 7:
        head += struct.pack(">H", bs - 1)
    if rc == 12:
        head += bytes([rate // 1000])
    elif rc == 13:
        head += struct.pack(">H", rate)
    elif rc == 14:
        head += struct.pack(">H", rate // 10)
    head += bytes([crc8(head)])
    bw = Bits()
    # decorrelate
    L, R = chans[0], chans[1] if len(chans) > 1 else None
    if ca == 8:
        cs = [L, [l - r for l, r in zip(L, R)]]
    elif ca == 9:
        cs = [[l - r for l, r in zip(L, R)], R]
    elif ca == 10:
        cs = [[(l + r) >> 1 for l, r in zip(L, R)], [l - r for l, r in zip(L, R)]]
    else:
        cs = chans
    for c, (x, (kind, o)) in enumerate(zip(cs, subs)):
        sb = bits + (1 if (ca == 8 and c == 1) or (ca == 9 and c == 0) or (ca == 10 and c == 1) else 0)
        subframe(bw, x, sb, kind, **o)
    bw.align()
    body = head + bytes(bw.out)
    return body + struct.pack(">H", crc16(body))


def md5_of(chans, bits):
    nb = (bits + 7) // 8
    h = hashlib.md5()
    for i in range(len(chans[0])):
        for ch in chans:
            h.update((ch[i] & ((1 << (8 * nb)) - 1)).to_bytes(nb, "little"))
    return h.digest()


def flac_stream(frames, chans, bits, rate, minbs, maxbs):
    nch = len(chans)
    total = len(chans[0])
    si = struct.pack(">HH", minbs, maxbs) + b"\0\0\0\0\0\0"
    v = (rate << 44) | ((nch - 1) << 41) | ((bits - 1) << 36) | total
    si += v.to_bytes(8, "big") + md5_of(chans, bits)
    hdr = b"fLaC" + bytes([0x80, 0, 0, 34]) + si
    return hdr + b"".join(frames)


def interleave(chans):
    return [chans[c][i] for i in range(len(chans[0])) for c in range(len(chans))]


def exotic_stereo():
    """variable block size, every decorrelation, every rare subframe path"""
    rate, bits = 32000, 16
    sizes = [192, 576, 100, 300, 256, 17]
    n = sum(sizes)
    sig = signal(n, 2, 15, 7)
    L = [sig[2 * i] for i in range(n)]
    R = [sig[2 * i + 1] for i in range(n)]
    for i in range(192 + 576 + 100, 192 + 576 + 100 + 300):  # wasted bits: multiples of 8
        L[i] = (L[i] >> 3) << 3
    for i in range(192, 192 + 576):  # a constant side channel in frame 1 (right/side)
        L[i] = R[i] + 5
    frames, at = [], 0
    lpc32 = [0] * 32
    lpc32[0], lpc32[1], lpc32[31] = 16000, -7000, 3
    specs = [
        (8, [("fixed", {"order": 2}), ("lpc", {"order": 3, "prec": 12, "shift": 9,
                                               "coefs": [700, -300, 60], "porder": 1})], 0, None),
        (9, [("constant", {}), ("verbatim", {})], 8, 4),
        (10, [("fixed", {"order": 4, "porder": 2, "escape": True}),
              ("fixed", {"order": 0, "porder": 2, "escape": True})], 12, None),
        (1, [("lpc", {"order": 32, "prec": 15, "shift": 14, "coefs": lpc32, "method": 1,
                      "wasted": 3}),
             ("fixed", {"order": 3, "method": 1, "porder": 2})], 13, 4),
        (1, [("fixed", {"order": 1, "porder": 8}), ("fixed", {"order": 1, "porder": 3})], 14, None),
        (8, [("verbatim", {}), ("fixed", {"order": 2})], 0, None),
    ]
    for bs, (ca, subs, rc, ss) in zip(sizes, specs):
        frames.append(frame([L[at:at + bs], R[at:at + bs]], bits, rate, at, ca, subs, rc=rc, ss=ss,
                            variable=True))
        at += bs
    return flac_stream(frames, [L, R], bits, rate, 16, 576), [L, R], rate, bits


def exotic_mono24():
    """24-bit mono, fixed block size 1024 (bsc 10), 5-bit Rice and LPC; rate code 13"""
    rate, bits = 11025, 24
    n = 2048 + 1000
    sig = signal(n, 1, 24, 11)
    frames = []
    co = [3500, -1800]
    for k, at in enumerate(range(0, n, 1024)):
        x = sig[at:at + 1024]
        subs = [("lpc", {"order": 2, "prec": 13, "shift": 11, "coefs": co, "method": 1, "porder": 0})]
        if len(x) < 1024:
            subs = [("fixed", {"order": 2, "method": 1, "porder": 3})]
        frames.append(frame([x], bits, rate, k, 0, subs, rc=13, ss=1))
    return flac_stream(frames, [sig], bits, rate, 1024, 1024), [sig], rate, bits


def flac_check(data):
    exe = shutil.which("flac")
    if not exe:
        sys.exit("make_audio_fixtures: the reference encoder `flac` is needed")
    d = tempfile.mkdtemp()
    p = os.path.join(d, "a.flac")
    open(p, "wb").write(data)
    r = subprocess.run([exe, "-t", "-s", p], capture_output=True)
    shutil.rmtree(d)
    if r.returncode:
        sys.exit("make_audio_fixtures: flac -t refused a generated stream:\n" + r.stderr.decode())


def flac_ref(samples, nch, bits, rate, args):
    """encode with the reference encoder from raw PCM"""
    exe = shutil.which("flac")
    d = tempfile.mkdtemp()
    raw = os.path.join(d, "a.raw")
    nb = bits // 8
    open(raw, "wb").write(b"".join(pack_int(v, nb) for v in samples))
    out = os.path.join(d, "a.flac")
    subprocess.run([exe, "-s", "-f", "--force-raw-format", "--endian=little",
                    "--sign=signed", "--channels=%d" % nch, "--bps=%d" % bits,
                    "--sample-rate=%d" % rate, "--no-padding", "--no-seektable"] + args +
                   ["-o", out, raw], check=True)
    data = open(out, "rb").read()
    shutil.rmtree(d)
    # the vorbis comment block names the encoder version; keep only STREAMINFO
    p, blocks, last = 4, [], False
    while not last:
        last = data[p] & 0x80
        ln = int.from_bytes(data[p + 1:p + 4], "big")
        if data[p] & 0x7F == 0:
            blocks.append(data[p + 4:p + 4 + ln])
        p += 4 + ln
    return b"fLaC" + bytes([0x80, 0, 0, 34]) + blocks[0] + data[p:]


def c_bytes(name, data):
    lines = ["static const uint8_t %s[%d] = {" % (name, len(data))]
    for i in range(0, len(data), 24):
        lines.append("    " + ",".join(str(b) for b in data[i:i + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    out = OUT
    if "--out" in sys.argv:
        out = sys.argv[sys.argv.index("--out") + 1]
    fx = []   # (name, bytes, kind, rate, nch, bits, frames, fnv)

    def add(name, data, kind, rate, nch, bits, samples):
        fx.append((name, data, kind, rate, nch, bits, len(samples) // nch, fnv(samples)))

    s16 = signal(300, 2, 16, 1)
    add("wav16s", wav(s16, 2, 8000, 16), 1, 8000, 2, 16, s16)
    s24 = signal(257, 1, 24, 2)
    add("wav24m", wav(s24, 1, 44100, 24), 1, 44100, 1, 24, s24)
    s8 = signal(99, 1, 8, 3)
    add("wav8m", wav(s8, 1, 11025, 8), 1, 11025, 1, 8, s8)
    s32 = signal(64, 3, 32, 4, "noise")
    add("wav32x3", wav(s32, 3, 96000, 32), 1, 96000, 3, 32, s32)
    # extensible: 24 valid bits in a 32-bit container (low byte zero)
    se = [v << 8 for v in signal(50, 2, 24, 5)]
    add("wavext", wav(se, 2, 48000, 32, ext=True, valid=24), 1, 48000, 2, 32, se)
    # float32: edge values, halves, tiny, out of range, inf, NaN
    fl = [0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -3.0, 1e-10, -1e-10, 0.25 + 2 ** -33,
          2 ** -32 * 3, 2 ** -32 * 5, float("inf"), float("-inf"), 0.999999, -0.999999]
    fl += [0.3 * math.sin(i / 3.0) for i in range(48)]
    raw = b"".join(struct.pack("<f", x) for x in fl)
    ref = [f_to_i32(struct.unpack("<f", struct.pack("<f", x))[0]) for x in fl]
    add("wavf32", wav(None, 1, 22050, 32, tag=3, raw=raw), 1, 22050, 1, 32, ref)
    nanraw = struct.pack("<I", 0x7FC00000)
    add("wavnan", wav(None, 1, 8000, 32, tag=3, raw=nanraw), 1, 8000, 1, 32, [0])
    fd = [0.1 * math.cos(i / 5.0) for i in range(40)] + [1.5, -1.0, 2 ** -31 * 1.5, 2 ** -31 * 2.5]
    raw = b"".join(struct.pack("<d", x) for x in fd)
    add("wavf64", wav(None, 2, 16000, 64, tag=3, raw=raw), 1, 16000, 2, 32, [f_to_i32(x) for x in fd])
    raw = b"".join(struct.pack("<f", x) for x in fl[:20])
    add("wavextf", wav(None, 2, 8000, 32, tag=3, ext=True, raw=raw), 1, 8000, 2, 32,
        [f_to_i32(struct.unpack("<f", struct.pack("<f", x))[0]) for x in fl[:20]])
    a16 = signal(200, 2, 16, 6)
    add("aiff16s", aiff(a16, 2, 44100, 16), 2, 44100, 2, 16, a16)
    a12 = signal(77, 1, 12, 7)
    add("aiff12m", aiff(a12, 1, 8000, 12), 2, 8000, 1, 12, a12)
    a24 = signal(60, 2, 24, 8)
    add("aifcsowt", aiff(a24, 2, 48000, 24, comp=b"sowt"), 2, 48000, 2, 24, a24)
    raw = b"".join(struct.pack(">f", x) for x in fl)
    add("aifcfl32", aiff(None, 1, 32000, 32, comp=b"fl32", raw=raw), 2, 32000, 1, 32, ref)
    # the reference encoder
    r16 = signal(5000, 2, 16, 9)
    add("flac16s", flac_ref(r16, 2, 16, 44100, ["-8"]), 3, 44100, 2, 16, r16)
    r24 = signal(3000, 1, 24, 10)
    add("flac24m", flac_ref(r24, 1, 24, 96000, ["-5", "-b", "1000"]), 3, 96000, 1, 24, r24)
    r8 = signal(2000, 1, 8, 12)
    add("flac8m", flac_ref(r8, 1, 8, 8000, ["-0"]), 3, 8000, 1, 8, r8)
    r32 = signal(1500, 1, 32, 13)
    add("flac32m", flac_ref(r32, 1, 32, 48000, ["-5"]), 3, 48000, 1, 32, r32)
    r6 = signal(1200, 6, 16, 14)
    add("flac6ch", flac_ref(r6, 6, 16, 22050, ["-5", "-b", "1152"]), 3, 22050, 6, 16, r6)
    rn = signal(600, 2, 16, 15, "noise") + signal(600, 2, 16, 15, "silence")
    add("flacnoise", flac_ref(rn, 2, 16, 16000, ["-8", "-b", "600"]), 3, 16000, 2, 16, rn)
    # forced rare paths
    data, chans, rate, bits = exotic_stereo()
    flac_check(data)
    add("flacexo", data, 3, rate, 2, bits, interleave(chans))
    data, chans, rate, bits = exotic_mono24()
    flac_check(data)
    add("flacexo24", data, 3, rate, 1, bits, interleave(chans))

    body = ["/* Generated by tools/media/make_audio_fixtures.py; do not edit. */",
            "/* SPDX-License-Identifier: Apache-2.0 */"]
    for name, data, *_ in fx:
        body.append(c_bytes("fx_" + name, data))
    body.append("typedef struct {\n    const char *name;\n    const uint8_t *d;\n    uint32_t n, kind, "
                "rate, nch, bits;\n    uint32_t frames, fnv;\n} fx_audio_t;")
    body.append("static const fx_audio_t FX_AUDIO[] = {")
    for name, data, kind, rate, nch, bits, frames, h in fx:
        body.append("    {\"%s\", fx_%s, %d, %d, %d, %d, %d, %d, 0x%08xu}," %
                    (name, name, len(data), kind, rate, nch, bits, frames, h))
    body.append("};")
    open(out, "w").write("\n".join(body) + "\n")
    print("wrote %s: %d fixtures, %d bytes" % (out, len(fx), sum(len(f[1]) for f in fx)))


if __name__ == "__main__":
    main()
