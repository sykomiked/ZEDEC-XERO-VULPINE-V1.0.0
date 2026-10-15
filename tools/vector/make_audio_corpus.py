#!/usr/bin/env python3
# make_audio_corpus.py: synthetic audio corpus for the vector benchmarks.
#
# Writes WAV files (standard library only) covering the cases ZXVA is
# measured on: a piano-like tone with inharmonic partials, a vowel with
# vibrato, a stereo chord, noise bursts and a click train, at 44.1/48/96 kHz
# and 16/24-bit. When `flac` is on PATH it also writes FLAC copies. The same
# seed gives the same bytes on every machine.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
import math
import os
import random
import shutil
import subprocess
import sys
import wave


def write_wav(path, chans, rate, bits):
    n = len(chans[0])
    peak = (1 << (bits - 1)) - 1
    frames = bytearray()
    for i in range(n):
        for c in chans:
            v = int(round(max(-1.0, min(1.0, c[i])) * peak))
            frames += v.to_bytes(bits // 8, "little", signed=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(len(chans))
        w.setsampwidth(bits // 8)
        w.setframerate(rate)
        w.writeframes(bytes(frames))


def piano(rate, secs, f0=220.0):
    n = int(rate * secs)
    out = [0.0] * n
    for k in range(1, 13):
        f = f0 * k * math.sqrt(1 + 0.0004 * k * k)
        if f > rate / 2 - 500:
            break
        a, d = 0.5 / k, 1.5 + 0.6 * k
        for i in range(n):
            t = i / rate
            out[i] += a * math.exp(-d * t) * math.sin(2 * math.pi * f * t)
    return [0.6 * s for s in out]


def vowel(rate, secs, f0=140.0):
    n = int(rate * secs)
    out = [0.0] * n
    formants = [(700, 130), (1220, 70), (2600, 160)]
    ph = 0.0
    for i in range(n):
        t = i / rate
        f = f0 * (1 + 0.02 * math.sin(2 * math.pi * 5.5 * t))
        ph += 2 * math.pi * f / rate
        s = 0.0
        for k in range(1, int((rate / 2 - 500) / f0)):
            fk = k * f
            g = sum(1.0 / (1 + ((fk - fc) / bw) ** 2) for fc, bw in formants)
            s += g / k * math.sin(k * ph)
        out[i] = s
    m = max(abs(x) for x in out) or 1.0
    return [0.7 * x / m for x in out]


def chord(rate, secs):
    n = int(rate * secs)
    l, r = [0.0] * n, [0.0] * n
    for j, f0 in enumerate((261.63, 329.63, 392.0, 523.25)):
        pan = j / 3.0
        for k in range(1, 7):
            f = f0 * k
            if f > rate / 2 - 500:
                break
            for i in range(n):
                t = i / rate
                v = 0.12 / k * math.exp(-0.8 * t) * math.sin(2 * math.pi * f * t)
                l[i] += v * (1 - pan)
                r[i] += v * pan
    return l, r


def noise_bursts(rate, secs, rng):
    n = int(rate * secs)
    out = [0.0] * n
    for b in range(4):
        s = int(rate * (0.1 + 0.45 * b))
        for i in range(s, min(n, s + int(rate * 0.2))):
            out[i] = 0.5 * (rng.random() * 2 - 1) * math.exp(-8 * (i - s) / rate)
    return out


def clicks(rate, secs):
    n = int(rate * secs)
    out = [0.0] * n
    for s in range(0, n, rate // 8):
        for j in range(min(48, n - s)):
            out[s + j] = 0.8 * math.exp(-j / 6.0) * (1 if j % 2 == 0 else -1)
    return out


def main():
    dst = sys.argv[1] if len(sys.argv) > 1 else "corpus/audio"
    os.makedirs(dst, exist_ok=True)
    rng = random.Random(20261015)
    jobs = [
        ("piano_44k16", [piano(44100, 2.0)], 44100, 16),
        ("piano_96k24", [piano(96000, 2.0)], 96000, 24),
        ("vowel_48k16", [vowel(48000, 1.5)], 48000, 16),
        ("chord_stereo_48k24", list(chord(48000, 2.0)), 48000, 24),
        ("noise_bursts_44k16", [noise_bursts(44100, 2.0, rng)], 44100, 16),
        ("clicks_44k16", [clicks(44100, 2.0)], 44100, 16),
    ]
    flac = shutil.which("flac")
    for name, chans, rate, bits in jobs:
        p = os.path.join(dst, name + ".wav")
        write_wav(p, chans, rate, bits)
        if flac:
            subprocess.run([flac, "-s", "-f", "-8", p, "-o", p[:-4] + ".flac"], check=True)
        print(p)


if __name__ == "__main__":
    main()
