#!/usr/bin/env python3
"""zxv_soundpack.py — the ZXV signature system sound set.

Design brief: futuristic, but rooted in nature — an African-tribal
timbral palette rendered with synthesis rather than samples. Nothing
here is a recording; every sound is generated from physical-ish models
so the whole set shares one voice and can be regenerated, retuned, or
re-scaled at any sample rate.

VOICES (the palette every cue is built from)
  mbira    plucked metal tine, inharmonic bar partials — the signature
           voice: organic attack, metallic shimmer, long clean decay
  membrane djembe/udu-like head: pitch-dropping body + noise transient
  balafon  wooden bar (marimba/balafon): woody knock + odd partial
  shaker   seed rattle: dense filtered-noise grains
  shimmer  ring-modulated high partials with a slow glide — the
           futuristic gloss laid over the natural voices

TUNING
  Minor pentatonic on A (A C D E G) — the scale most common across West
  African traditions and pleasantly consonant in any order, which makes
  the cues combinable without clashing.

Usage:
  zxv_soundpack.py <outdir> [--rate 48000]
"""
import argparse, os, wave
import numpy as np

SR = 48000

# --- A minor pentatonic ---------------------------------------------------
A3, C4, D4, E4, G4 = 220.00, 261.63, 293.66, 329.63, 392.00
A4, C5, D5, E5, G5 = 440.00, 523.25, 587.33, 659.25, 783.99
A5, C6, D6, E6     = 880.00, 1046.50, 1174.66, 1318.51


def lp(x, cutoff, sr=SR, poles=2):
    """One-pole lowpass, applied `poles` times. Noise transients in real
    wood/skin/seed sources are band-limited; unfiltered white noise is what
    makes synthetic percussion sound like hiss instead of material."""
    a = 1.0 - np.exp(-2*np.pi*cutoff/sr)
    y = x.astype(float).copy()
    for _ in range(poles):
        out = np.empty_like(y); acc = 0.0
        for i in range(len(y)):
            acc += a*(y[i]-acc); out[i] = acc
        y = out
    return y


def env(n, a=0.002, d=0.25, s=0.0, r=0.10, sr=SR, curve=3.0):
    """Percussive AD(S)R with an exponential tail (natural decay)."""
    e = np.zeros(n)
    ai, di, ri = int(a*sr), int(d*sr), int(r*sr)
    ai = max(1, min(ai, n)); di = max(1, min(di, n-ai)); ri = max(1, min(ri, n-ai-di))
    e[:ai] = np.linspace(0, 1, ai)
    e[ai:ai+di] = (1-s) * np.exp(-curve*np.linspace(0, 1, di)) + s
    tail = n - ai - di
    if tail > 0:
        start = e[ai+di-1]
        e[ai+di:] = start * np.exp(-curve*1.6*np.linspace(0, 1, tail))
    return e


def mbira(f, dur, sr=SR, bright=1.0, amp=1.0):
    """Plucked metal tine. Bar modes are inharmonic: ~1 : 2.76 : 5.40."""
    n = int(dur*sr); t = np.arange(n)/sr
    partials = [(1.00, 1.00, 3.0), (2.76, 0.42*bright, 5.5), (5.40, 0.18*bright, 8.0),
                (8.93, 0.07*bright, 11.0)]
    y = np.zeros(n)
    for mult, a, dec in partials:
        y += a*np.sin(2*np.pi*f*mult*t)*np.exp(-dec*t)
    # metallic strike transient
    k = int(0.004*sr)
    tr = np.random.RandomState(int(f) % 2**31).randn(k)*np.linspace(1, 0, k)
    y[:k] += 0.10*lp(tr, 2600, sr)
    return amp*y*env(n, a=0.001, d=dur*0.6, r=dur*0.35, sr=sr, curve=2.2)


def membrane(f, dur, sr=SR, amp=1.0, drop=0.35):
    """Hand drum: the head pitch falls as tension releases."""
    n = int(dur*sr); t = np.arange(n)/sr
    fenv = f*(1.0 + drop*np.exp(-14*t))            # pitch drop
    ph = 2*np.pi*np.cumsum(fenv)/sr
    body = np.sin(ph) + 0.30*np.sin(2*ph) + 0.12*np.sin(3.4*ph)
    rs = np.random.RandomState(1234)
    k = int(0.012*sr)
    slap = np.zeros(n); slap[:k] = lp(rs.randn(k)*np.exp(-40*t[:k]), 1800, sr)
    y = 0.95*body*np.exp(-9*t) + 0.30*slap
    return amp*y*env(n, a=0.0008, d=dur*0.45, r=dur*0.5, sr=sr, curve=2.6)


def balafon(f, dur, sr=SR, amp=1.0):
    """Wooden bar — strong fundamental, one woody odd partial, dry knock."""
    n = int(dur*sr); t = np.arange(n)/sr
    y = (np.sin(2*np.pi*f*t)*np.exp(-6*t)
         + 0.30*np.sin(2*np.pi*3.9*f*t)*np.exp(-16*t)
         + 0.10*np.sin(2*np.pi*10.5*f*t)*np.exp(-30*t))
    k = int(0.003*sr)
    rs = np.random.RandomState(7)
    y[:k] += 0.22*lp(rs.randn(k)*np.linspace(1, 0, k), 1400, sr)  # wood knock
    return amp*y*env(n, a=0.001, d=dur*0.5, r=dur*0.45, sr=sr, curve=3.2)


def shaker(dur, sr=SR, amp=1.0, tilt=1600.0, grains=0):
    """Seed rattle: high-passed noise, optionally grouped into grains."""
    n = int(dur*sr); t = np.arange(n)/sr
    rs = np.random.RandomState(99)
    x = rs.randn(n)
    # one-pole high-pass (tilt controls the 'seed' brightness)
    a = np.exp(-2*np.pi*tilt/sr)
    y = np.zeros(n); prev_x = 0.0; prev_y = 0.0
    for i in range(n):
        y[i] = a*(prev_y + x[i] - prev_x); prev_x = x[i]; prev_y = y[i]
    if grains:
        g = np.zeros(n)
        for i in range(grains):
            s = int(i*n/grains)
            ln = int(0.03*sr)
            seg = y[s:s+ln]
            g[s:s+len(seg)] += seg*np.exp(-30*np.arange(len(seg))/sr)*(1-0.6*i/grains)
        y = g
    y = lp(y, 5200, sr)   # seed pods have a top limit
    return amp*y*env(n, a=0.003, d=dur*0.5, r=dur*0.45, sr=sr, curve=2.5)


def shimmer(f, dur, sr=SR, amp=1.0, glide=1.0):
    """Ring-modulated high partials with a slow glide — the futuristic gloss."""
    n = int(dur*sr); t = np.arange(n)/sr
    fg = f*(glide + (1-glide)*np.exp(-3*t))
    ph = 2*np.pi*np.cumsum(fg)/sr
    car = np.sin(ph) + 0.5*np.sin(2.01*ph)
    mod = np.sin(2*np.pi*(f*0.503)*t)
    y = car*(0.6 + 0.4*mod)
    return amp*y*env(n, a=0.02, d=dur*0.55, r=dur*0.4, sr=sr, curve=2.0)


# --- assembly helpers -----------------------------------------------------
def lay(base, snd, at, sr=SR):
    i = int(at*sr); e = min(len(base), i+len(snd))
    if e > i: base[i:e] += snd[:e-i]
    return base

def buf(dur, sr=SR):
    return np.zeros(int(dur*sr))

def finish(y, sr=SR, peak=0.89, fade=0.01):
    y = 0.80*y + 0.20*lp(y, 3200, sr)   # warm tilt across the whole set
    m = np.max(np.abs(y)) or 1.0
    y = y/m*peak
    f = int(fade*sr)
    if len(y) > 2*f:
        y[:f] *= np.linspace(0, 1, f)
        y[-f:] *= np.linspace(1, 0, f)
    return y

def stereo(y, spread=0.12):
    """Gentle Haas-style width so cues feel spatial, still mono-safe."""
    d = int(spread*0.004*SR)
    L = y.copy(); R = np.concatenate([np.zeros(d), y[:len(y)-d]]) if d else y.copy()
    return np.stack([L, R], axis=1)

def write(path, y, sr=SR):
    st = stereo(finish(y, sr))
    pcm = (np.clip(st, -1, 1)*32767).astype('<i2')
    w = wave.open(path, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(pcm.tobytes()); w.close()
    return len(y)/sr


# --- the cue set ----------------------------------------------------------
def cues(sr=SR):
    C = {}

    # NOTIFY — soft ascending double tine. Gentle, non-urgent.
    y = buf(0.85, sr)
    lay(y, mbira(E5, 0.55, sr, amp=0.55), 0.00, sr)
    lay(y, mbira(G5, 0.60, sr, amp=0.50), 0.09, sr)
    lay(y, shimmer(E6, 0.45, sr, amp=0.10), 0.09, sr)
    C['notify'] = y

    # SUCCESS — rising pentatonic triplet, opens upward, shimmer halo.
    y = buf(1.10, sr)
    for i, f in enumerate((D5, G5, C6)):
        lay(y, mbira(f, 0.65, sr, amp=0.55-0.05*i), 0.00+0.085*i, sr)
    lay(y, membrane(A3*0.75, 0.40, sr, amp=0.30), 0.0, sr)
    lay(y, shimmer(E6, 0.70, sr, amp=0.13), 0.17, sr)
    C['success'] = y

    # ERROR — falling minor third on wood + dry drum. Blunt, not harsh.
    y = buf(0.95, sr)
    lay(y, membrane(A3*0.62, 0.55, sr, amp=0.75, drop=0.5), 0.0, sr)
    lay(y, balafon(D4, 0.50, sr, amp=0.55), 0.02, sr)
    lay(y, balafon(A3*0.945, 0.60, sr, amp=0.50), 0.16, sr)   # ~down a 3rd
    C['error'] = y

    # ALERT — attention: drum, then a firm rising tine pair.
    y = buf(1.05, sr)
    lay(y, membrane(A3*0.7, 0.45, sr, amp=0.70), 0.0, sr)
    lay(y, mbira(A4, 0.55, sr, amp=0.55, bright=1.2), 0.06, sr)
    lay(y, mbira(D5, 0.65, sr, amp=0.55, bright=1.2), 0.20, sr)
    lay(y, shaker(0.30, sr, amp=0.12, tilt=1500, grains=3), 0.04, sr)
    C['alert'] = y

    # WARNING — two detuned pulses; unsettled but not alarming.
    y = buf(1.05, sr)
    for i in range(2):
        t0 = 0.0 + 0.24*i
        lay(y, membrane(A3*0.66, 0.40, sr, amp=0.60), t0, sr)
        lay(y, balafon(C5*(1.0 if i == 0 else 0.988), 0.45, sr, amp=0.45), t0+0.01, sr)
    C['warning'] = y

    # QUESTION — rises and stops on the 5th: deliberately unresolved.
    y = buf(0.90, sr)
    lay(y, mbira(D5, 0.50, sr, amp=0.50), 0.00, sr)
    lay(y, mbira(E5, 0.65, sr, amp=0.55), 0.13, sr)
    lay(y, shimmer(E5*2, 0.55, sr, amp=0.10, glide=1.06), 0.13, sr)
    C['question'] = y

    # CONNECT — seed sweep up into a bright tine.
    y = buf(0.95, sr)
    lay(y, shaker(0.34, sr, amp=0.35, tilt=1500, grains=5), 0.0, sr)
    lay(y, mbira(G5, 0.60, sr, amp=0.55), 0.22, sr)
    lay(y, shimmer(G5*2, 0.55, sr, amp=0.12, glide=1.10), 0.22, sr)
    C['connect'] = y

    # DISCONNECT — the mirror: bright tine falling into the rattle.
    y = buf(0.95, sr)
    lay(y, mbira(G5, 0.45, sr, amp=0.50), 0.0, sr)
    lay(y, mbira(D5, 0.55, sr, amp=0.45), 0.12, sr)
    lay(y, shaker(0.34, sr, amp=0.28, tilt=650, grains=5), 0.16, sr)
    C['disconnect'] = y

    # UNLOCK — wood click, then an opening fifth.
    y = buf(0.85, sr)
    lay(y, balafon(A4, 0.22, sr, amp=0.45), 0.0, sr)
    lay(y, mbira(A5, 0.60, sr, amp=0.45), 0.07, sr)
    lay(y, shimmer(E6, 0.50, sr, amp=0.12), 0.07, sr)
    C['unlock'] = y

    # DELETE — a dry downward rush, like sand through the fingers.
    y = buf(0.75, sr)
    lay(y, shaker(0.55, sr, amp=0.55, tilt=380, grains=8), 0.0, sr)
    lay(y, membrane(A3*0.5, 0.45, sr, amp=0.40, drop=0.7), 0.03, sr)
    C['delete'] = y

    # TICK — the smallest UI grain: a single wood click.
    y = buf(0.16, sr)
    lay(y, balafon(D5, 0.14, sr, amp=0.55), 0.0, sr)
    C['tick'] = y

    # CAPTURE — quick shutter-ish transient + tine confirm.
    y = buf(0.60, sr)
    lay(y, shaker(0.10, sr, amp=0.42, tilt=1500), 0.0, sr)
    lay(y, mbira(C6, 0.42, sr, amp=0.42), 0.06, sr)
    C['capture'] = y

    # ATTACH/DETACH of a phase-tick sub terminal — a very short paired cue.
    y = buf(0.45, sr); lay(y, mbira(A5, 0.34, sr, amp=0.40), 0.0, sr)
    C['term_open'] = y
    y = buf(0.45, sr); lay(y, mbira(E5, 0.34, sr, amp=0.40), 0.0, sr)
    C['term_close'] = y

    return C


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("--rate", type=int, default=SR)
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    total = 0
    for name, y in cues(a.rate).items():
        p = os.path.join(a.outdir, f"zxv_{name}.wav")
        d = write(p, y, a.rate)
        total += 1
        print(f"  {name:12s} {d:5.2f}s  {p}")
    print(f"{total} cues written at {a.rate} Hz")


if __name__ == "__main__":
    main()
