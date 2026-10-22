#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""fidelity.py - measure ZXVA analysis fidelity on synthetic signals.

    fidelity.py [outdir]

Generates five 48 kHz / 16-bit test signals (piano-like tone with
inharmonicity, vowel-like formant source with vibrato, white-noise burst,
click train, chord), analyses each with wav2zxva (with the lossless
residual), renders with the kernel renderer and prints a Markdown table:
sizes against 16-bit PCM, waveform SNR of the parametric render (all layers,
and tonal layers only), band-envelope error, residual size, and whether
"ZXVA + residual" reproduces the source sample-exactly. The numbers in
kernel/src/vaudio/ZXVA.md come from this script. Needs numpy.
"""
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import wav2zxva as A  # noqa: E402
import zxvafmt as Z  # noqa: E402

RATE = 48000


def norm16(x, peak=0.8):
    x = x / np.max(np.abs(x)) * peak
    return np.clip(np.round(x * 32767), -32768, 32767).astype(np.int64)


def piano(dur=1.5):
    t = np.arange(int(dur * RATE)) / RATE
    f0, B, x = 220.0, 0.0004, np.zeros_like(t)
    for k in range(1, 31):
        fk = k * f0 * math.sqrt(1 + B * k * k)
        if fk > 20000:
            break
        env = np.minimum(1, t / 0.003) * np.exp(-t * (0.7 + 0.25 * k))
        x += k ** -1.2 * env * np.sin(2 * np.pi * fk * t + 0.3 * k)
    return norm16(x)


def vowel(dur=1.5):
    t = np.arange(int(dur * RATE)) / RATE
    f0 = 130 * (1 + 0.01 * np.sin(2 * np.pi * 5 * t))
    ph = 2 * np.pi * np.cumsum(f0) / RATE
    x = np.zeros_like(t)
    forms = [(700, 130, 1.0), (1220, 70, 0.5), (2600, 160, 0.25)]
    for k in range(1, int(5000 / 130)):
        fk = k * f0
        g = sum(a / np.sqrt((1 - (fk / F) ** 2) ** 2 + (fk * bw / F ** 2) ** 2) for F, bw, a in forms)
        x += g / k * np.sin(k * ph)
    env = np.minimum(1, t / 0.02) * np.minimum(1, (dur - t) / 0.05)
    return norm16(x * env)


def noise_burst(dur=1.0):
    rng = np.random.default_rng(1)
    n = int(dur * RATE)
    x = np.zeros(n)
    a, b = int(0.25 * RATE), int(0.75 * RATE)
    r = int(0.01 * RATE)
    env = np.ones(b - a)
    env[:r] = np.linspace(0, 1, r)
    env[-r:] = np.linspace(1, 0, r)
    x[a:b] = rng.standard_normal(b - a) * env
    return norm16(x, 0.5)


def clicks(dur=1.0):
    n = int(dur * RATE)
    x = np.zeros(n)
    t = np.arange(int(0.004 * RATE)) / RATE
    click = np.exp(-t / 0.0005) * np.sin(2 * np.pi * 3000 * t)
    click[0] += 0.5
    for i in range(10):
        p = int((0.05 + 0.1 * i) * RATE)
        x[p:p + len(click)] += click * (1 - 0.05 * i)
    return norm16(x)


def chord(dur=1.5):
    t = np.arange(int(dur * RATE)) / RATE
    x = np.zeros_like(t)
    for j, f in enumerate((261.63, 329.63, 392.0, 523.25)):
        for k in range(1, 7):
            x += (1.0 / k) * np.exp(-t * (0.5 + 0.2 * k)) * np.sin(2 * np.pi * f * k * t + j + k)
    return norm16(x * np.minimum(1, t / 0.01))


def snr(ref, x):
    n = min(len(ref), len(x))
    e = np.sum((x[:n] - ref[:n]) ** 2)
    return 10 * math.log10(np.sum(ref[:n] ** 2) / e) if e > 0 else float("inf")


def band_err(ref, x):
    """RMS over (10 ms frame, ERB band) of the level difference in dB, for
    cells within 60 dB of the loudest cell of the reference."""
    n = 1024
    edges = A.erb_edges(20000)
    fr = np.fft.rfftfreq(n, 1.0 / RATE)
    win = np.hanning(n)

    def cells(s):
        out = []
        for c in range(0, len(s) - n, RATE // 100):
            P = np.abs(np.fft.rfft(s[c:c + n] * win)) ** 2
            out.append([np.sum(P[(fr >= edges[b]) & (fr < edges[b + 1])]) for b in range(24)])
        return 10 * np.log10(np.array(out) + 1e-20)
    m = min(len(ref), len(x))
    R, X = cells(ref[:m]), cells(x[:m])
    keep = R > R.max() - 60
    return float(np.sqrt(np.mean((R[keep] - X[keep]) ** 2)))


def main(argv):
    out = argv[0] if argv else os.path.join(os.getcwd(), "zxva_fidelity")
    os.makedirs(out, exist_ok=True)
    rows = []
    for name, fn in (("piano-like tone (inharmonic, decaying)", piano),
                     ("vowel-like formant source (vibrato)", vowel),
                     ("white-noise burst", noise_burst), ("click train", clicks),
                     ("chord (4 notes x 6 harmonics)", chord)):
        src = fn()
        wav = os.path.join(out, fn.__name__ + ".wav")
        A.write_wav(wav, RATE, 16, src[:, None])
        w, _, _ = A.analyse(wav, residual=True)
        full = w.bytes()
        par = w.parametric_bytes() + b"\x00"
        open(os.path.join(out, fn.__name__ + ".zxva"), "wb").write(full)
        r_all = A.render(par, RATE, 16)[:, 0].astype(float)
        r_ton = A.render(par, RATE, 16, layers=1 | 4 | 8 | 16)[:, 0].astype(float)
        exact = A.render(full, RATE, 16, residual=True)[:, 0]
        s = src.astype(float)
        plain = len(Z.residual_chunk(RATE, 16, [src.tolist()], 1))
        rows.append((name, 2 * len(src), len(par), snr(s, r_all), snr(s, r_ton), band_err(s, r_all),
                     len(full) - len(par) + 1, len(full), plain,
                     bool(len(exact) == len(src) and np.array_equal(exact, src))))
        print("done", name, file=sys.stderr)
    print("| signal | PCM 16/48k | ZXVA parametric | ratio | SNR all layers | SNR tonal layers "
          "| band error | residual | ZXVA+residual | same coder on PCM | sample-exact |")
    print("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|:---:|")
    for n, pcm, par, sa, st, be, res, full, plain, ok in rows:
        print("| %s | %d B | %d B | %.0f:1 | %.1f dB | %.1f dB | %.2f dB | %d B | %d B | %d B | %s |"
              % (n, pcm, par, pcm / par, sa, st, be, res, full, plain, "yes" if ok else "NO"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
