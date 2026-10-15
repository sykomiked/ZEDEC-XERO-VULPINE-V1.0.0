#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""wav2zxva.py - analyse a recording into ZXVA vector audio.

    wav2zxva.py in.wav out.zxva [--residual] [--no-noise] [--no-grains]
                [--max-tracks N] [--render BIN] [--quiet]

Reads 16/24/32-bit integer or 32/64-bit float PCM WAV (any rate, 1..8
channels). Per channel:
  1. transients: onsets from a short/long energy ratio; each is fitted with a
     tone grain (windowed sinusoid, least squares over a small grid of
     frequency and decay) and kept only if it removes energy;
  2. sinusoids: STFT (Blackman-Harris for peak picking, 4x zero padding,
     parabolic interpolation), a tonality test against the window's own
     main lobe, joint least-squares amplitude/phase per frame (Hann weights),
     partial tracking with birth/death ramps, phase-locked breakpoints;
  3. noise: what the partials and grains leave, as band power on 24 ERB-spaced
     bands every 10 ms;
  4. --residual: render the parametric file with the kernel renderer at the
     native rate and depth (no dither) and store source - render losslessly,
     so the file reproduces the recording sample-exactly at its native rate.
The kernel renderer (zxvarender, built from tools/zxva/zxvarender.c and
kernel/src/vaudio/zxva.c) is used to compute exact residuals; it is compiled
with `cc` into a temporary directory if --render is not given.

Needs numpy.
"""
import math
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import zxvafmt as Z  # noqa: E402

WIN_S = 0.085   # analysis window, seconds (rounded to a power of two)
TOL_DB = 60.0   # breakpoint simplification: error below the loudest partial
PHASE_TOL = 0.125  # tracking: largest phase surprise over one hop, turns
COHERENCE = 0.16   # tracking: RMS phase surprise across one window, turns
SALIENCE = 12.0    # tracking: median peak height over the local median, dB
MIN_FRAMES = 9     # a partial must last one analysis window (8 hops): with
                   # 7/8 overlap, noise peaks stay phase-coherent for a few hops
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))


# ---- WAV I/O ------------------------------------------------------------
def read_wav(path):
    """-> (rate, bits, is_float, int_or_float array [frames, ch])"""
    d = open(path, "rb").read()
    if d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        raise SystemExit("wav2zxva: not a RIFF/WAVE file")
    p, fmt, data = 12, None, None
    while p + 8 <= len(d):
        cid, ln = d[p:p + 4], struct.unpack("<I", d[p + 4:p + 8])[0]
        body = d[p + 8:p + 8 + ln]
        if cid == b"fmt ":
            fmt = body
        elif cid == b"data":
            data = body
        p += 8 + ln + (ln & 1)
    if fmt is None or data is None:
        raise SystemExit("wav2zxva: missing fmt or data chunk")
    tag, nch, rate, _, _, bits = struct.unpack("<HHIIHH", fmt[:16])
    if tag == 0xFFFE and len(fmt) >= 26:
        tag = struct.unpack("<H", fmt[24:26])[0]
    if tag == 1 and bits in (16, 24, 32):
        if bits == 24:
            a = np.frombuffer(data[:len(data) // 3 * 3], np.uint8).reshape(-1, 3).astype(np.int32)
            v = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
            x = np.where(v >= 1 << 23, v - (1 << 24), v)
        else:
            x = np.frombuffer(data, "<i2" if bits == 16 else "<i4").astype(np.int64)
        return rate, bits, False, x[:len(x) // nch * nch].reshape(-1, nch).astype(np.int64)
    if tag == 3 and bits in (32, 64):
        x = np.frombuffer(data, "<f4" if bits == 32 else "<f8").astype(np.float64)
        return rate, 32, True, x[:len(x) // nch * nch].reshape(-1, nch)
    raise SystemExit("wav2zxva: unsupported WAV format (tag %d, %d bit)" % (tag, bits))


def write_wav(path, rate, bits, x):
    """x: int array [frames, ch] at `bits`"""
    x = np.asarray(x, np.int64)
    nch = x.shape[1]
    if bits == 16:
        raw = x.astype("<i2").tobytes()
    elif bits == 32:
        raw = x.astype("<i4").tobytes()
    else:
        v = (x.reshape(-1) & 0xFFFFFF).astype(np.uint32)
        raw = np.stack([v & 255, (v >> 8) & 255, v >> 16], 1).astype(np.uint8).tobytes()
    bps = bits // 8
    hdr = b"RIFF" + struct.pack("<I", 36 + len(raw)) + b"WAVEfmt " + struct.pack(
        "<IHHIIHH", 16, 1, nch, rate, rate * nch * bps, nch * bps, bits) + b"data" + struct.pack(
        "<I", len(raw))
    open(path, "wb").write(hdr + raw)


# ---- the kernel renderer -------------------------------------------------
_RENDER = [None]


def renderer(path=None):
    if path:
        _RENDER[0] = path
    if _RENDER[0] is None:
        out = os.path.join(tempfile.mkdtemp(prefix="zxva"), "zxvarender")
        subprocess.run(["cc", "-std=c11", "-O2", "-I" + os.path.join(ROOT, "kernel/src/vaudio"),
                        os.path.join(HERE, "zxvarender.c"),
                        os.path.join(ROOT, "kernel/src/vaudio/zxva.c"), "-o", out], check=True)
        _RENDER[0] = out
    return _RENDER[0]


def render(data, rate, bits, layers=31, residual=False):
    """render ZXVA bytes with the kernel renderer -> int array [frames, ch]"""
    d = tempfile.mkdtemp(prefix="zxvar")
    zp, wp = os.path.join(d, "a.zxva"), os.path.join(d, "a.wav")
    open(zp, "wb").write(data)
    cmd = [renderer(), zp, wp, "--rate", str(rate), "--bits", str(bits), "--layers", str(layers)]
    if residual:
        cmd.append("--residual")
    subprocess.run(cmd, check=True)
    _, _, _, x = read_wav(wp)
    os.remove(zp)
    os.remove(wp)
    os.rmdir(d)
    return x


# ---- analysis helpers ----------------------------------------------------
def bh_window(n):
    k = np.arange(n) / n
    return (0.35875 - 0.48829 * np.cos(2 * np.pi * k) + 0.14128 * np.cos(4 * np.pi * k)
            - 0.01168 * np.cos(6 * np.pi * k))


def erb_edges(top, nb=24, lo=40.0):
    def erb(f):
        return 21.4 * math.log10(1 + 0.00437 * f)

    def inv(e):
        return (10 ** (e / 21.4) - 1) / 0.00437
    a, b = erb(lo), erb(top)
    e = [int(round(inv(a + (b - a) * i / nb))) for i in range(nb + 1)]
    out = []
    for v in e:
        out.append(max(v, out[-1] + 1) if out else max(1, v))
    return out


def grain_env(t, dur, att, half):
    """float model of zxva.c grain_span's envelope; t in us"""
    env = np.ones_like(t)
    if att > 0:
        m = t < att
        env[m] *= np.sin(np.pi / 2 * t[m] / att) ** 2
    if half > 0:
        env *= 2.0 ** (-np.maximum(0, t - att) / half)
    fl = dur // 8
    if fl:
        m = t > dur - fl
        env[m] *= np.sin(np.pi / 2 * np.clip(dur - t[m], 0, None) / fl) ** 2
    env[(t < 0) | (t >= dur)] = 0
    return env


def detect_transients(x, rate, quiet=True):
    """onset sample indices"""
    e = x * x
    s = max(1, rate // 2000)            # 0.5 ms
    c = np.concatenate([[0.0], np.cumsum(e)])

    def win(a, b):
        a, b = np.clip(a, 0, len(x)), np.clip(b, 0, len(x))
        return (c[b] - c[a]) / np.maximum(1, b - a)
    n = np.arange(0, len(x), s)
    short = win(n, n + 2 * s)
    long_ = win(n - 20 * s, n)
    on = np.where((short > 10 * (long_ + 1e-12)) & (short > 1e-6))[0]
    out, last = [], -10 ** 9
    for i in on:
        p = int(n[i])
        if p - last > 10 * s:
            out.append(p)
            last = p
    return out


def fit_grains(x, rate, w, ch, pan, quiet):
    """fit tone grains at onsets, add them to the writer, return the float model"""
    model = np.zeros_like(x)
    count = 0
    for on in detect_transients(x, rate):
        a = max(0, on - rate // 2000)
        b = min(len(x), on + rate // 50)
        seg = x[a:b] - model[a:b]
        if len(seg) < 16:
            continue
        # start the grain where the transient really starts
        a += max(0, int(np.argmax(np.abs(seg) > 0.1 * np.max(np.abs(seg)))) - 2)
        seg = x[a:b] - model[a:b]
        if len(seg) < 16:
            continue
        energy = float(np.sum(seg ** 2))
        spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 4 * len(seg))) ** 2
        fr = np.fft.rfftfreq(4 * len(seg), 1.0 / rate)
        cen = float(np.sum(fr * spec) / max(1e-30, np.sum(spec)))
        pk = float(fr[np.argmax(spec)])
        t_us = (np.arange(a, b) - a) / rate * 1e6
        best = None
        for f in {cen, pk, cen * 0.5, cen * 0.75, cen * 1.5}:
            if not 20 < f < 0.45 * rate:
                continue
            for dur in (1000, 3000, 8000, 20000):
                dur = min(dur, int((b - a) / rate * 1e6))
                if dur < 100:
                    continue
                for half in (100, 400, 1500, 0):
                    env = grain_env(t_us, dur, 50, half)
                    ph = 2 * np.pi * f * t_us / 1e6
                    A = np.stack([env * np.sin(ph), env * np.cos(ph)], 1)
                    coef, *_ = np.linalg.lstsq(A, seg, rcond=None)
                    r = float(np.sum((seg - A @ coef) ** 2))
                    if best is None or r < best[0]:
                        best = (r, f, dur, half, coef, A)
        if best is None or best[0] > energy * 0.8:   # must remove > 1 dB
            continue
        r, f, dur, half, coef, A = best
        amp = math.hypot(coef[0], coef[1])
        phase = math.atan2(coef[1], coef[0]) / (2 * math.pi)
        if amp > 1.0:
            continue
        t0 = int(round(a / rate * 1e6))
        w.grain(t0, dur, f, amp, attack=50, half=half, phase=phase % 1.0, ch=ch, pan=pan)
        model[a:b] += A @ coef
        count += 1
    if not quiet:
        print("  grains: %d" % count)
    return model


class Peaks:
    """STFT peak picking with a main-lobe tonality test"""

    def __init__(self, n, pad=4, sim=0.95, over=30.0):
        self.n, self.pad, self.sim, self.over = n, pad, sim, over
        self.win = bh_window(n)
        k = np.abs(np.fft.rfft(self.win, n * 64))
        self.kern = k / k[0]            # main lobe, 64 points per bin
        self.sal = {}                   # frequency -> dB above the local median

    def lobe(self, off):
        idx = np.abs(off) * 64 / self.pad
        i = np.clip(idx.astype(int), 0, len(self.kern) - 2)
        fr = idx - i
        return self.kern[i] * (1 - fr) + self.kern[i + 1] * fr

    def find(self, frame, rate, floor_db=-110.0, rel_db=-90.0):
        X = np.abs(np.fft.rfft(frame * self.win, self.n * self.pad)) / (np.sum(self.win) / 2)
        L = 20 * np.log10(X + 1e-20)
        top = L.max()
        thr = max(floor_db, top + rel_db)
        k = np.where((L[1:-1] > L[:-2]) & (L[1:-1] >= L[2:]) & (L[1:-1] > thr))[0] + 1
        out = []
        for i in k:
            a, b, c = L[i - 1], L[i], L[i + 1]
            den = a - 2 * b + c
            d = 0.5 * (a - c) / den if den < 0 else 0.0
            if abs(d) > 0.5:
                continue
            # tonality: the neighbourhood must look like the window's main lobe
            j = np.arange(max(1, i - 2 * self.pad), min(len(X) - 1, i + 2 * self.pad + 1))
            ref = self.lobe(j - (i + d))
            meas = X[j]
            sim = float(np.dot(ref, meas) / (np.linalg.norm(ref) * np.linalg.norm(meas) + 1e-30))
            # or: far above the local floor (a lobe distorted by a neighbour)
            fl = np.median(X[max(1, i - 16 * self.pad):i + 16 * self.pad + 1])
            if sim < self.sim and X[i] < fl * 10 ** (self.over / 20):
                continue
            f = (i + d) * rate / (self.n * self.pad)
            if 15.0 < f < 0.45 * rate:
                out.append(f)
                self.sal[f] = 20 * math.log10(X[i] / (fl + 1e-30) + 1e-30)
        return out


def lsq(A, y):
    """least squares by regularised normal equations (fast; a tiny ridge keeps
    near-coincident frequencies from blowing up)"""
    G = A.T @ A
    G[np.diag_indices_from(G)] += 1e-9 * (np.trace(G) / len(G) + 1e-30)
    return np.linalg.solve(G, A.T @ y)


def simplify(bps, tol):
    """drop breakpoints while the renderer's interpolation (linear amplitude,
    linear frequency, phase = integral + constant correction) stays within
    `tol` (absolute amplitude error, phase error weighted by amplitude) of
    every dropped breakpoint. Greedy, with a galloping search."""
    B = np.array(bps, dtype=np.float64)
    T, F, Am, P = B[:, 0], B[:, 1], B[:, 2], B[:, 3]

    def ok(i, j):
        d = (T[j] - T[i]) / 1e6
        if T[j] - T[i] > 1 << 22:
            return False
        if j == i + 1:
            return True
        corr = (P[j] - P[i] - (F[i] + F[j]) / 2 * d + 0.5) % 1.0 - 0.5
        tau = (T[i + 1:j] - T[i]) / 1e6
        a_ = Am[i] + (Am[j] - Am[i]) * tau / d
        ph = P[i] + F[i] * tau + (F[j] - F[i]) * tau * tau / (2 * d) + corr * tau / d
        dp = (ph - P[i + 1:j] + 0.5) % 1.0 - 0.5
        am = Am[i + 1:j]
        return bool(np.all((a_ - am) ** 2 + (2 * np.pi * dp * am) ** 2 <= tol * tol))
    out, i, last = [bps[0]], 0, len(bps) - 1
    while i < last:
        step, good = 1, i + 1
        while good + step <= last and ok(i, good + step):
            good += step
            step *= 2
        lo, hi = good, min(last, good + step)          # ok(lo), maybe not ok(hi)
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if ok(i, mid):
                lo = mid
            else:
                hi = mid
        out.append(bps[lo])
        i = lo
    return out


def synth_bps(bps, rate, nframes):
    """float rendering of one partial's breakpoints, the renderer's model"""
    out = np.zeros(nframes)
    for (ti, fi, ai, pi), (tj, fj, aj, pj) in zip(bps, bps[1:]):
        n0, n1 = int(math.ceil(ti * rate / 1e6)), min(nframes, int(math.ceil(tj * rate / 1e6)))
        if n1 <= n0:
            continue
        d = (tj - ti) / 1e6
        corr = (pj - pi - (fi + fj) / 2 * d + 0.5) % 1.0 - 0.5
        tau = np.arange(n0, n1) / rate - ti / 1e6
        ph = pi + fi * tau + (fj - fi) * tau * tau / (2 * d) + corr * tau / d
        out[n0:n1] += (ai + (aj - ai) * tau / d) * np.sin(2 * np.pi * ph)
    return out


def sharpen_onsets(x, rate, all_bps):
    """At each onset, scale every partial by one common gain curve g(t)
    (0.5 ms grid) so the model's short-time energy follows the source's: the
    analysis hop cannot resolve a 3 ms attack, but a shared envelope can."""
    if not all_bps:
        return all_bps
    model = sum(synth_bps(b, rate, len(x)) for b in all_bps)
    step = max(1, rate // 2000)
    grid_t = {}
    for on in detect_transients(x, rate):
        a, b = max(0, on - 8 * step), min(len(x), on + 60 * step)
        for c in range(a, b, step):
            lo, hi = max(0, c - 2 * step), min(len(x), c + 2 * step)
            mm = model[lo:hi]
            em = float(np.dot(mm, mm))
            # least-squares gain over 2 ms: robust to where the waveform is
            g = min(1.5, max(0.0, float(np.dot(x[lo:hi], mm)) / em)) if em > 1e-12 else 1.0
            grid_t[int(round(c / rate * 1e6))] = g
    if not grid_t:
        return all_bps
    times = np.array(sorted(grid_t))
    gains = np.array([grid_t[t] for t in times])

    def gain_at(t):  # g between grid points; 1 outside onset regions
        i = np.searchsorted(times, t)
        if i < len(times) and times[i] == t:
            return gains[i]
        if i == 0 or i == len(times) or times[i] - times[i - 1] > 600:
            return 1.0
        f = (t - times[i - 1]) / (times[i] - times[i - 1])
        return gains[i - 1] * (1 - f) + gains[i] * f
    out = []
    for bps in all_bps:
        t0, t1 = bps[0][0], bps[-1][0]
        merged = list(bps)
        for t in times:
            if not t0 < t < t1:
                continue
            j = max(i for i, b in enumerate(merged) if b[0] <= t)
            (ti, fi, ai, pi), (tj, fj, aj, pj) = merged[j], merged[j + 1]
            if t - ti < 100 or tj - t < 100:
                continue
            d = (tj - ti) / 1e6
            tau = (t - ti) / 1e6
            corr = (pj - pi - (fi + fj) / 2 * d + 0.5) % 1.0 - 0.5
            ph = pi + fi * tau + (fj - fi) * tau * tau / (2 * d) + corr * tau / d
            merged.insert(j + 1, (t, fi + (fj - fi) * tau / d, ai + (aj - ai) * tau / d, ph % 1.0))
        out.append([(t, f, a * gain_at(t), p) for t, f, a, p in merged])
    return out


def chirp_refit(x, rate, n, hop, tracks):
    """Refit every frame's amplitudes and phases with each partial modelled as
    a linear chirp across the window (slope from its track's neighbours), so
    vibrato and glides do not smear a long analysis window."""
    hann = np.hanning(n + 1)[:-1]
    per = {}
    for ti, tr in enumerate(tracks):
        for j, (k, f, a, p) in enumerate(tr):
            fp = tr[max(0, j - 1)]
            fn = tr[min(len(tr) - 1, j + 1)]
            slope = (fn[1] - fp[1]) / (max(1, fn[0] - fp[0]) * hop / rate)
            per.setdefault(k, []).append((ti, j, f, slope))
    out = [list(tr) for tr in tracks]
    for k, lst in per.items():
        c = k * hop
        idx = np.arange(c - n // 2, c + n // 2)
        valid = (idx >= 0) & (idx < len(x))
        seg = np.zeros(n)
        seg[valid] = x[idx[valid]]
        wt = hann * valid
        tau = (idx - c) / rate
        f = np.array([e[2] for e in lst])
        sl = np.array([e[3] for e in lst])
        ang = 2 * np.pi * (np.outer(tau, f) + 0.5 * np.outer(tau * tau, sl))
        A = np.concatenate([np.sin(ang), np.cos(ang)], 1) * wt[:, None]
        coef = lsq(A, seg * wt)
        m = len(lst)
        for (ti, j, fr, _), a_, b_ in zip(lst, coef[:m], coef[m:]):
            out[ti][j] = (k, fr, float(np.hypot(a_, b_)), float(np.arctan2(b_, a_) / (2 * np.pi)))
    return out


def analyse_partials(x, rate, w, ch, pan, dur_us, max_tracks, quiet, tol_db=TOL_DB):
    n = 1 << int(round(math.log2(rate * WIN_S)))      # ~85 ms
    hop = n // 8
    pk = Peaks(n)
    hann = np.hanning(n + 1)[:-1]
    nfr = len(x) // hop + 1
    binw = rate / n

    def fit(k, fs):
        c = k * hop
        idx = np.arange(c - n // 2, c + n // 2)
        valid = (idx >= 0) & (idx < len(x))
        if not len(fs) or not valid.any():
            return [[f, 0.0, 0.0] for f in fs]
        seg = np.zeros(n)
        seg[valid] = x[idx[valid]]
        wt = hann * valid
        ang = 2 * np.pi * np.outer((idx - c) / rate, np.asarray(fs))
        A = np.concatenate([np.sin(ang), np.cos(ang)], 1) * wt[:, None]
        coef = lsq(A, seg * wt)
        s_, co = coef[:len(fs)], coef[len(fs):]
        return [[float(f), float(np.hypot(a_, b_)), float(np.arctan2(b_, a_) / (2 * np.pi))]
                for f, a_, b_ in zip(fs, s_, co)]

    fits, sal = [], []
    for k in range(nfr):
        c = k * hop
        idx = np.arange(c - n // 2, c + n // 2)
        valid = (idx >= 0) & (idx < len(x))
        seg = np.zeros(n)
        seg[valid] = x[idx[valid]]
        pk.sal = {}
        fs = sorted(pk.find(seg, rate)) if valid.any() else []
        fits.append(fit(k, fs) if fs else [])
        sal.append([pk.sal[f] for f in fs])
    # tracking: a track is a list of (frame, index into fits[frame])
    tracks, active = [], []
    claimed = [set() for _ in range(nfr)]
    for k in range(nfr):
        peaks = fits[k]
        cand = []
        for ti, tr in enumerate(active):
            fl, _, pl = fits[tr[-1][0]][tr[-1][1]]
            for pi, (f, a_, p_) in enumerate(peaks):
                if a_ > 1e-6 and abs(f - fl) < max(15.0, 0.03 * fl):
                    # phase coherence: a sinusoid arrives where its frequency
                    # integral says; a noise peak arrives anywhere
                    dp = (p_ - pl - (f + fl) / 2 * hop / rate + 0.5) % 1.0 - 0.5
                    if abs(dp) < PHASE_TOL:
                        cand.append((abs(f - fl), ti, pi))
        cand.sort()
        used_t, used_p, nxt = set(), set(), []
        for _, ti, pi in cand:
            if ti in used_t or pi in used_p:
                continue
            used_t.add(ti)
            used_p.add(pi)
            active[ti].append((k, pi))
            nxt.append(active[ti])
        tracks += [tr for ti, tr in enumerate(active) if ti not in used_t]
        nxt += [[(k, pi)] for pi in range(len(peaks)) if pi not in used_p and peaks[pi][1] > 1e-6]
        active = nxt
    tracks += active
    def coherent(tr):
        """Phase prediction across one whole window (8 hops, so the two fits
        share no samples): a sinusoid lands where its integrated frequency
        says; a noise peak, which overlapping fits make look locally
        coherent, lands anywhere."""
        v = [fits[k][i] for k, i in tr]
        e = []
        for j in range(0, len(v) - 8):
            ph = v[j][2] + sum((v[m][0] + v[m + 1][0]) / 2 for m in range(j, j + 8)) * hop / rate
            e.append((v[j + 8][2] - ph + 0.5) % 1.0 - 0.5)
        return bool(e) and math.sqrt(sum(d * d for d in e) / len(e)) < COHERENCE
    def salient(tr):
        """a partial stands well above the local spectral median for most
        of its life; a peak of a noise spectrum stands a few dB above it"""
        return float(np.median([sal[k][i] for k, i in tr])) >= SALIENCE
    tracks = [tr for tr in tracks if len(tr) >= MIN_FRAMES and coherent(tr) and salient(tr)]
    for tr in tracks:
        for k, i in tr:
            claimed[k].add(i)
    # extension: grow each track a frame at a time at both ends while the
    # least-squares refit with the track's frequency still finds it (this
    # recovers onsets the peak picker sees late, and tails it loses)
    open_s = [True] * len(tracks)
    open_e = [True] * len(tracks)
    for _ in range(12):
        prop = {}
        for ti, tr in enumerate(tracks):
            for side, k, (kk, ii) in (("s", tr[0][0] - 1, tr[0]), ("e", tr[-1][0] + 1, tr[-1])):
                if (open_s if side == "s" else open_e)[ti]:
                    if 0 <= k < nfr:
                        prop.setdefault(k, []).append((ti, side, fits[kk][ii][0], fits[kk][ii][1]))
                    else:
                        (open_s if side == "s" else open_e)[ti] = False
        if not prop:
            break
        for k, lst in prop.items():
            fs = [e[0] for e in fits[k]]
            slots = []
            for ti, side, f, aref in lst:
                if any(abs(f - fs[i]) < 0.5 * binw for i in claimed[k]):
                    slots.append(None)
                    continue
                free = [i for i in range(len(fs)) if i not in claimed[k] and abs(f - fs[i]) < 0.5 * binw]
                if free:
                    slots.append(free[0])
                    fs[free[0]] = f
                else:
                    fs.append(f)
                    slots.append(len(fs) - 1)
                claimed[k].add(slots[-1])
            fits[k] = fit(k, fs)
            for (ti, side, f, aref), sl in zip(lst, slots):
                if sl is not None and fits[k][sl][1] >= 0.1 * aref:
                    if side == "s":
                        tracks[ti].insert(0, (k, sl))
                    else:
                        tracks[ti].append((k, sl))
                else:
                    if sl is not None:
                        claimed[k].discard(sl)
                    (open_s if side == "s" else open_e)[ti] = False
    tracks = [[(k,) + tuple(fits[k][i]) for k, i in tr] for tr in tracks if len(tr) >= MIN_FRAMES]
    tracks = chirp_refit(x, rate, n, hop, tracks)
    tracks.sort(key=lambda tr: -sum(a * a for _, _, a, _ in tr))
    tracks = tracks[:max_tracks]
    aref = max([a for tr in tracks for _, _, a, _ in tr] + [1e-9])
    tol = aref * 10 ** (-tol_db / 20)
    hop_s = hop / rate

    def bp(fi, f, a, p):
        ts = fi * hop / rate
        tu = int(round(ts * 1e6))
        # breakpoints sit on whole microseconds: move the phase with them
        return (tu, f, a, (p + f * (tu / 1e6 - ts)) % 1.0)
    nbp = 0
    all_bps = []
    for tr in tracks:
        k0, f0, a0, p0 = tr[0]
        bps = [bp(k0 - 1, f0, 0.0, p0 - f0 * hop_s)] if k0 > 0 else []
        bps += [bp(k, f, a, p) for k, f, a, p in tr]
        k1, f1, a1, p1 = tr[-1]
        if (k1 + 1) * hop / rate * 1e6 <= dur_us:
            bps.append(bp(k1 + 1, f1, 0.0, p1 + f1 * hop_s))
        elif dur_us - bps[-1][0] >= 100:
            dt = dur_us / 1e6 - bps[-1][0] / 1e6
            bps.append((dur_us, f1, a1, (bps[-1][3] + f1 * dt) % 1.0))
        all_bps.append(bps)
    for bps in sharpen_onsets(x, rate, all_bps):
        bps = simplify(bps, tol)
        if len(bps) >= 2:
            w.partial(bps, ch=ch, pan=pan, locked=True)
            nbp += len(bps)
    if not quiet:
        print("  partial tracks: %d, %d breakpoints (frame %d, hop %d samples)"
              % (len(tracks), nbp, n, hop))
    return len(tracks)


def analyse_noise(r, rate, w, ch, pan, dur_us, quiet):
    hop_us = 10000
    n = 1 << int(round(math.log2(rate * 0.021)))
    win = np.hanning(n + 1)[:-1]
    sw2 = float(np.sum(win ** 2))
    top = min(20000, int(0.45 * rate))
    edges = erb_edges(top)
    fr = np.fft.rfftfreq(n, 1.0 / rate)
    bands = [(fr >= edges[b]) & (fr < edges[b + 1]) for b in range(len(edges) - 1)]
    nfr = dur_us // hop_us + 1
    frames = []
    for k in range(nfr):
        c = int(round(k * hop_us * rate / 1e6))
        a = c - n // 2
        seg = np.zeros(n)
        lo, hi = max(0, a), min(len(r), a + n)
        if hi > lo:
            seg[lo - a:hi - a] = r[lo:hi]
        P = np.abs(np.fft.rfft(seg * win)) ** 2 * 2 / (n * sw2)
        amps = []
        for m in bands:
            pb = float(np.sum(P[m]))
            a_ = math.sqrt(2 * pb)
            amps.append(a_ if a_ > 3e-5 else 0.0)        # -90 dBFS gate
        frames.append(amps)
    if not any(any(f) for f in frames):
        return False
    w.noise(0, hop_us, edges, frames, ch=ch, pan=pan)
    if not quiet:
        print("  noise: %d bands x %d frames" % (len(edges) - 1, nfr))
    return True


def to_float(x, bits, is_float):
    return x if is_float else x / float(1 << (bits - 1))


def analyse(path, residual=False, noise=True, grains=True, max_tracks=1000, quiet=True):
    rate, bits, is_float, x = read_wav(path)
    nch = x.shape[1]
    dur_us = int(math.ceil(x.shape[0] * 1e6 / rate))
    while (dur_us * rate + 999999) // 1000000 > x.shape[0]:
        dur_us -= 1
    xf = to_float(x, bits, is_float)
    w = Z.Writer(nch, dur_us, seed=0x5A58)
    per = max(1, max_tracks // nch)
    def place(c):
        if nch == 1:
            return 0, 0
        if nch == 2:
            return (0, 0) if c == 0 else (0, 65536)
        return c, 0
    for c in range(nch):
        if not quiet:
            print("channel %d:" % c)
        analyse_partials(xf[:, c], rate, w, ch=place(c)[0], pan=place(c)[1], dur_us=dur_us,
                         max_tracks=per, quiet=quiet)
    if grains or noise:
        sofar = render(w.bytes(), rate, 32) / float(1 << 31)
        n = min(len(sofar), len(xf))
        for c in range(nch):
            r = xf[:n, c] - sofar[:n, c]
            if grains:
                r = r - fit_grains(r, rate, w, place(c)[0], place(c)[1], quiet)
            if noise:
                analyse_noise(r, rate, w, place(c)[0], place(c)[1], dur_us, quiet)
    if residual:
        src = x if not is_float else np.clip(np.round(x * 2 ** 31), -2 ** 31, 2 ** 31 - 1).astype(
            np.int64)
        param = render(w.bytes(), rate, bits)
        nf = param.shape[0]
        srcn = np.zeros((nf, nch), np.int64)
        srcn[:min(nf, len(src))] = src[:nf]
        res = srcn - param
        w.residual(rate, bits, [res[:, c].tolist() for c in range(nch)])
    return w, rate, bits


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    opts = [a for a in argv if a.startswith("--")]
    if len(args) < 2:
        sys.stderr.write(__doc__)
        return 2
    mt = 1000
    if "--max-tracks" in argv:
        mt = int(argv[argv.index("--max-tracks") + 1])
        args = [a for a in args if a != str(mt)]
    if "--render" in argv:
        rb = argv[argv.index("--render") + 1]
        renderer(rb)
        args = [a for a in args if a != rb]
    w, rate, bits = analyse(args[0], residual="--residual" in opts, noise="--no-noise" not in opts,
                            grains="--no-grains" not in opts, max_tracks=mt,
                            quiet="--quiet" in opts)
    data = w.bytes()
    open(args[1], "wb").write(data)
    if "--quiet" not in opts:
        print("wrote %s: %d bytes (parametric %d)" % (args[1], len(data),
                                                     len(w.parametric_bytes()) + 1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
