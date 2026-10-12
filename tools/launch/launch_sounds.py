# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# ZXV app launch sounds: three cues per app, synthesised from scratch (no samples), deterministic.
#
#   begin   plays as the launch screen appears                (the app waking)
#   middle  one short pitched tick each time a part lights     (tracks real loading: never looped)
#   end     plays as the reveal opens onto the app             (the app ready)
#
# Each app has a key and a timbre family so the three cues read as one phrase, and every app sounds
# like itself. The OS boot keeps E; apps never use the gong, so the boot stays the biggest moment.
import numpy as np, wave

SR = 48000

def _t(dur): return np.arange(int(SR * dur)) / SR

def env(t, att=0.004, dec=0.4, st=0.0):
    tt = t - st; e = np.where(tt >= 0, np.minimum(np.maximum(tt, 0) / att, 1) * np.exp(-np.maximum(tt, 0) / dec), 0)
    return e

def pan(s, p):
    p = max(-1.0, min(1.0, p)); g = (p + 1) * np.pi / 4; return np.stack([s * np.cos(g), s * np.sin(g)], 1)

def note(n):  # 'E5' -> Hz
    names = {'C':-9,'C#':-8,'Db':-8,'D':-7,'D#':-6,'Eb':-6,'E':-5,'F':-4,'F#':-3,'Gb':-3,'G':-2,'G#':-1,'Ab':-1,'A':0,'A#':1,'Bb':1,'B':2}
    nm, oc = n[:-1], int(n[-1]); return 440.0 * 2 ** ((names[nm] + 12 * (oc - 4)) / 12)

def rng(seed): return np.random.default_rng(seed)

# ---- timbres: f, t -> mono signal (amplitude envelope applied by caller unless noted) ----------
def glass(f, t, st, dec=0.9):
    e = env(t, 0.005, dec, st)
    return (np.sin(2*np.pi*f*t) + 0.18*np.sin(2*np.pi*f*3*t)*env(t,0.005,dec*0.3,st)
            + 0.08*np.sin(2*np.pi*f*4.2*t)*env(t,0.003,0.1,st)) * e

def marimba(f, t, st, dec=0.35):
    e = env(t, 0.002, dec, st)
    return (np.sin(2*np.pi*f*t) + 0.35*np.sin(2*np.pi*f*3.93*t)*env(t,0.002,dec*0.25,st)
            + 0.12*np.sin(2*np.pi*f*9.2*t)*env(t,0.001,0.02,st)) * e

def chip(f, t, st, dec=0.18):     # soft square: odd harmonics, rolled off
    e = env(t, 0.002, dec, st); s = 0
    for k in (1, 3, 5, 7): s = s + np.sin(2*np.pi*f*k*t) / (k*k*0.6)
    return s * e

def flute(f, t, st, dur=0.6, seed=7):
    tt = t - st; a = np.clip(tt/0.06, 0, 1) * np.clip((dur - tt)/0.25, 0, 1) * (tt >= 0)
    br = rng(seed).standard_normal(len(t)); br = np.convolve(br, np.ones(30)/30, 'same')
    vib = 1 + 0.006*np.sin(2*np.pi*5.2*t)
    return (np.sin(2*np.pi*f*np.cumsum(vib)/SR) + 0.25*np.sin(4*np.pi*f*t) + 0.5*br) * a

def bell(f, t, st, dec=1.4):
    e = env(t, 0.002, dec, st); s = 0
    for r, a, d in [(1,1,1),(2.0,0.5,0.6),(2.76,0.35,0.4),(5.4,0.2,0.2),(8.9,0.1,0.08)]:
        s = s + a*np.sin(2*np.pi*f*r*t)*env(t,0.002,dec*d,st)
    return s

def coin(f, t, st, seed=3):
    r = rng(seed); s = 0
    for ratio in (1.0, 1.47, 2.09, 2.56, 3.41):
        s = s + np.sin(2*np.pi*f*ratio*(1+r.uniform(-.004,.004))*t) * env(t, 0.0008, 0.09 + 0.12/ratio, st)
    return s

def pluck(f, t, st, dec=0.5):     # kalimba-ish: tine + a quick bright partial
    e = env(t, 0.001, dec, st)
    return (np.sin(2*np.pi*f*t) + 0.4*np.sin(2*np.pi*f*5.95*t)*env(t,0.001,0.04,st)) * e

def strings(f, t, st, dur=1.2):
    tt = t - st; a = np.clip(tt/0.35, 0, 1) * np.clip((dur - tt)/0.5, 0, 1) * (tt >= 0)
    s = 0
    for d in (-0.004, 0, 0.0035):   # three detuned saws, rolled off
        for k in range(1, 9): s = s + np.sin(2*np.pi*f*(1+d)*k*t) / k**1.6
    return s * a

def noise(t, st, dec, seed, smooth=1, hp=False):
    n = rng(seed).standard_normal(len(t))
    if smooth > 1: n = np.convolve(n, np.ones(smooth)/smooth, 'same')
    if hp: n = np.convolve(np.diff(n, prepend=0), np.ones(5)/5, 'same')   # a click, with the hiss taken off
    return n * env(t, 0.001, dec, st)

def sweep(t, f0, f1, st, dur, amp=1.0):
    tt = np.clip(t - st, 0, dur); f = f0 * (f1/f0) ** (tt/dur); ph = 2*np.pi*np.cumsum(f)/SR
    a = (t >= st) * (t <= st + dur) * np.sin(np.pi * tt/dur)
    return np.sin(ph) * a * amp

# ---- finishing: echo, fade, normalise, write ----------------------------------------------------
def finish(x, peak=0.85, echo=None, fade=0.15):
    if echo:
        D, g = int(echo[0]*SR), echo[1]; y = x.copy()
        for k in (1, 2, 3):
            if D*k < len(x):
                src = x[:-D*k][:, ::-1] if k % 2 else x[:-D*k]
                y[D*k:] += src * g**k
        x = y
    n = len(x); fl = int(fade*SR)
    if fl: x[-fl:] *= np.linspace(1, 0, fl)[:, None]**2
    m = np.abs(x).max()
    return x / m * peak if m > 0 else x

def write(path, x):
    with wave.open(path, 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((np.clip(x, -1, 1)*32767).astype('<i2').tobytes())

# ---- the six themed apps --------------------------------------------------------------------------
# Each recipe returns (begin, tick(i, n), end) as stereo arrays. tick(i, n) is the middle cue for the
# i-th of n parts: it climbs the app's scale so the gathering itself plays a short rising phrase.

def burrow():           # Files: earth and wood. D, marimba, a hollow knock, the tunnel opening.
    sc = ['D4','E4','F#4','A4','B4','D5','E5']
    def begin():
        t = _t(1.3); s = 0.8*noise(t, 0, 0.05, 11, smooth=40)          # a soft knock on wood
        s += 0.9*marimba(note('D3'), t, 0.02, 0.5) + 0.7*marimba(note('A3'), t, 0.20, 0.5)
        s += 0.6*marimba(note('D4'), t, 0.38, 0.7)
        return finish(pan(s, -0.15) + pan(0.5*marimba(note('A4'), t, 0.38, 0.6), 0.4), 0.7, (0.14, 0.25))
    def tick(i, n):
        t = _t(0.45); f = note(sc[min(i, len(sc)-1)])
        return finish(pan(marimba(f, t, 0, 0.25), -0.6 + 1.2*i/max(n-1, 1)), 0.45, None, 0.05)
    def end():
        t = _t(1.8); air = sweep(t, 90, 40, 0, 0.5, 0.9) + 0.25*noise(t, 0, 0.35, 12, smooth=80)   # the earth gives way
        ch = sum(marimba(note(n), t, 0.08+0.025*k, 0.9) for k, n in enumerate(['D3','A3','D4','F#4','A4']))
        return finish(pan(air, 0) + pan(0.6*ch, -0.1) + pan(0.4*glass(note('D6'), t, 0.2, 0.6), 0.5), 0.85, (0.16, 0.22), 0.4)
    return begin, tick, end

def flint():            # Terminal: a struck spark, CRT ignition. A, chip square.
    sc = ['A4','B4','C#5','E5','F#5','A5','B5']
    def begin():
        t = _t(1.0); s = 0.45*noise(t, 0.0, 0.012, 21, hp=True) + 0.35*noise(t, 0.05, 0.02, 22, hp=True)   # flint on steel
        s += 0.8*glass(note('A6'), t, 0.05, 0.3) + 0.5*glass(note('E7'), t, 0.07, 0.2)
        s += 0.7*chip(note('A3'), t, 0.18, 0.45) + 0.4*chip(note('E4'), t, 0.24, 0.4)
        return finish(pan(s, 0.2), 0.7, (0.09, 0.3))
    def tick(i, n):
        t = _t(0.25); f = note(sc[min(i, len(sc)-1)])
        s = 0.25*noise(t, 0, 0.004, 30+i, hp=True) + chip(f, t, 0.002, 0.07)        # a keystroke that plays a note
        return finish(pan(s, -0.5 + i/max(n-1, 1)), 0.4, None, 0.03)
    def end():
        t = _t(1.4); s = 1.0*sweep(t, 60, 15000, 0, 0.12, 0.5)                    # the tube warms: a fast whine
        s += 0.9*noise(t, 0.12, 0.03, 23, smooth=6)                                   # the thunk
        s += sum(0.5*chip(note(n), t, 0.14, 0.6) for n in ['A3','E4','A4','C#5'])
        s += 0.3*glass(note('A5'), t, 0.16, 0.8)
        return finish(pan(s, 0), 0.85, (0.11, 0.2), 0.3)
    return begin, tick, end

def murmuration():      # The swarm: wings and breath. E (the OS key: the swarm is its heart), flute.
    sc = ['E5','F#5','G#5','B5','C#6','E6','F#6']
    def begin():
        t = _t(1.5); s = 0
        for k in range(9):                                    # a flutter of wings, left to right
            s = s + pan(0.5*noise(t, 0.03*k, 0.025, 40+k, smooth=12), -0.8 + 0.2*k)
        s = s + pan(0.6*flute(note('E5'), t, 0.25, 0.9), -0.2) + pan(0.45*flute(note('B5'), t, 0.45, 0.8, 8), 0.3)
        return finish(s, 0.7, (0.2, 0.25))
    def tick(i, n):
        t = _t(0.35); f = note(sc[min(i, len(sc)-1)])
        s = glass(f, t, 0, 0.12) * (1 + 0.5*np.sin(2*np.pi*f*0.5*t)) + 0.6*glass(f*1.5, t, 0.04, 0.08)   # a chirp
        return finish(pan(s, np.sin(i*2.1)*0.7), 0.4, None, 0.04)
    def end():
        t = _t(1.9); s = 0
        for k in range(14):                                  # the flock lifts off: wings spreading outward
            s = s + pan(0.4*noise(t, 0.02*k, 0.04, 60+k, smooth=10), (-1)**k * (0.2 + 0.05*k))
        s = s + pan(sum(0.35*flute(note(n), t, 0.12, 1.5, 9+j) for j, n in enumerate(['E4','B4','E5','G#5'])), 0)
        s = s + pan(0.3*glass(note('E6'), t, 0.3, 1.0), 0.4)
        return finish(s, 0.85, (0.22, 0.25), 0.5)
    return begin, tick, end

def coffer():           # Wallet: a key, tumblers, coins, the lid. G, coins and bell (kin to the boot's vault).
    sc = ['G5','A5','B5','D6','E6','G6','A6']
    def begin():
        t = _t(1.2); s = 0
        for k, st in enumerate((0.0, 0.07, 0.13, 0.22)):      # a key turning through the tumblers
            s = s + 0.25*noise(t, st, 0.006, 70+k, hp=True) + 0.3*marimba(1800+300*k, t, st, 0.03)
        s = s + 0.7*bell(note('G4'), t, 0.32, 0.8) + 0.4*bell(note('D5'), t, 0.38, 0.6)
        return finish(pan(s, 0.1), 0.7, (0.12, 0.25))
    def tick(i, n):
        t = _t(0.4); f = note(sc[min(i, len(sc)-1)])
        return finish(pan(coin(f, t, 0, 80+i), -0.5 + i/max(n-1, 1)), 0.6, None, 0.05)
    def end():
        t = _t(2.0); s = 1.0*noise(t, 0, 0.05, 90, smooth=30) + sweep(t, 70, 45, 0, 0.25, 1.0)   # the latch
        s += 0.6*bell(note('G5'), t, 0.1, 1.2) + 0.45*bell(note('D6'), t, 0.14, 1.0) + 0.35*bell(note('B5'), t, 0.18, 1.0)
        for k in range(5): s += 0.25*coin(note(sc[k]), t, 0.25 + 0.06*k, 95+k)               # coins settle
        return finish(pan(s, 0), 0.85, (0.15, 0.22), 0.5)
    return begin, tick, end

def cutwater():         # Video editing: the projector, a clapper, a cinematic swell. C, strings.
    sc = ['C5','D5','E5','G5','A5','C6','D6']
    def begin():
        t = _t(1.6); s = 0
        for k in range(14):                                   # the reel's clatter spinning up
            st = 0.4*(1 - (1 - k/14)**1.8); s = s + 0.16*noise(t, st, 0.008, 100+k, hp=True)
        s = s + 0.7*strings(note('C3'), t, 0.1, 1.5) + 0.5*strings(note('G3'), t, 0.25, 1.35)
        return finish(pan(s, 0), 0.7, (0.1, 0.18))
    def tick(i, n):
        t = _t(0.35); f = note(sc[min(i, len(sc)-1)])
        s = 0.22*noise(t, 0, 0.005, 110+i, hp=True) + glass(f, t, 0.003, 0.15)    # a frame clicking through the gate
        return finish(pan(s, -0.5 + i/max(n-1, 1)), 0.4, None, 0.04)
    def end():
        t = _t(2.1); s = 0.5*noise(t, 0, 0.02, 120, hp=True) + 0.4*noise(t, 0, 0.06, 121, smooth=8)   # the clapper
        s += sweep(t, 55, 38, 0.02, 0.6, 1.0)                                                         # a low hit
        s += sum(0.45*strings(note(n), t, 0.03, 1.9) for n in ['C3','G3','C4','E4','G4'])
        s += 0.3*glass(note('C6'), t, 0.3, 1.0)
        return finish(pan(s, 0), 0.85, (0.18, 0.2), 0.6)
    return begin, tick, end

def commons():          # Chat: a call and an answer. F, kalimba.
    sc = ['F5','G5','A5','C6','D6','F6','G6']
    def begin():
        t = _t(1.3)
        call = pluck(note('F5'), t, 0.0) + 0.8*pluck(note('C6'), t, 0.12)          # "hel-lo"
        ans = 0.8*pluck(note('A5'), t, 0.42) + 0.8*pluck(note('F5'), t, 0.54)      # and the answer, from the other side
        return finish(pan(call, -0.55) + pan(ans, 0.55) + pan(0.3*glass(note('F3'), t, 0, 1.0), 0), 0.7, (0.13, 0.2))
    def tick(i, n):
        t = _t(0.4); f = note(sc[min(i, len(sc)-1)])
        return finish(pan(pluck(f, t, 0, 0.25), (-1)**i * 0.5), 0.45, None, 0.05)
    def end():
        t = _t(1.8); s = 0.8*sweep(t, 300, 1200, 0, 0.06, 0.6)                    # a bubble pops open
        s += sum(0.5*pluck(note(n), t, 0.06 + 0.04*k, 1.0) for k, n in enumerate(['F4','A4','C5','F5','A5']))
        s += 0.4*glass(note('F6'), t, 0.25, 0.8) + 0.4*strings(note('F3'), t, 0.05, 1.6)
        return finish(pan(s, 0), 0.85, (0.15, 0.22), 0.5)
    return begin, tick, end

RECIPES = dict(burrow=burrow, flint=flint, murmuration=murmuration, coffer=coffer, cutwater=cutwater, commons=commons)

if __name__ == '__main__':
    import sys, os
    out = sys.argv[1]; os.makedirs(out, exist_ok=True)
    for name, fn in RECIPES.items():
        b, tk, e = fn()
        write(f'{out}/{name}-1-begin.wav', b()); write(f'{out}/{name}-3-end.wav', e())
        # the middle cue as heard during a 5-part load, for review
        n = 5; gap = int(0.22*SR); x = np.zeros((gap*n + int(0.5*SR), 2))
        for i in range(n):
            c = tk(i, n); x[i*gap:i*gap+len(c)] += c
        write(f'{out}/{name}-2-middle.wav', finish(x, 0.6, None, 0.1))
        print('wrote', name)
