#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TOL VOVINA UPAAH LOT -- score slice 3 of 4: the deep hold, the tribunal, the find.

Generates three Standard MIDI Files, by hand, stdlib only:

  rootwyrm_deep_stonebreath.mid       "Stonebreath"     ROOTWYRM DEEP (deep ruins hold)
  wyrmgate_threshold_sixfold_verdict.mid "Sixfold Verdict" THE WYRMGATE THRESHOLD (confrontation)
  glintfall_first_gleam.mid           "First Gleam"     GLINTFALL (discovery one-shot)

Vocabulary is the tree's own (PROVENANCE/TVUL_ROM_FORMAT.md:10-11): the world
graph is a HOARD, an enterable place is a HOLD, an edge is a GATE. Not
level/world/exit.

ORIGINALITY
  Every pitch here was written for this score. Nothing is transcribed, quoted,
  paraphrased or reconstructed from any existing work.
  (ref: the influences are the Legends-style melodic sensibility and Stewart
  Copeland's percussion-forward, air-first approach -- style only, per
  [[proprietary naming]]. No franchise name appears in any title, filename or
  shipped string.)

COMPOSED AGAINST THE SYSTEM'S OWN MATHEMATICS
  phi   -- phi^2 = phi + 1, phi^k = F(k)*phi + F(k-1). Used as SECTION
           proportion and phrase length, never as decoration.
           Stonebreath: 55 bars split 34 + 21   (34/21 = 1.6190, 55/34 = 1.6176)
           Stonebreath pad segment lengths:  8 5 8 13 8 5 8  -> 55, all Fibonacci
           Sixfold Verdict: 5 + (8+5+3) + 8 + 21 + 5 = 55 bars, and the melodic
           statement CONTRACTS down the Fibonacci ladder 8 -> 5 -> 3 as pressure
           rises, then releases into 21.
  13    -- the system's native arity (cyc13_t, the 13 l13_phase_t phases,
           MIXMAT_MAX 13, the 13 lattice spaces at kernel/src/desktop/zxv_shell.c:236).
           Stonebreath: a 13-BEAT tuned-percussion ostinato under 5/4. lcm(13,5)
           = 65 beats, so it realigns with the downbeat only every 13 bars, and
           exactly 21 statements (273 beats) fit inside the 275-beat piece --
           leaving a 2-beat void at the seam. Fibonacci count of a 13-cycle
           inside a phi-proportioned form, and the void is what makes the loop
           click-free.
  Sixfold Verdict inverts the relationship: the METER is 13/8 (grouped
           3+3+3+2+2) and the bass ostinato is 8 eighths, so lcm(8,13) = 104
           eighths = exactly 8 bars. The bass therefore lands on a different
           accent in each of 8 consecutive bars and re-agrees with the bar line
           at bars 5, 13, 21, 29, 37, 45, 53 -- which are the section seams.
  SPIRAL, NOT CIRCLE
           No material is ever copy-pasted. The Stonebreath ostinato thins,
           octave-displaces and finally runs in retrograde; the Deep Cell is
           stated, then transposed +5 and displaced by one beat, then reversed,
           then dissolved to single notes. The Verdict hook returns transposed
           up a fourth, then in true chromatic inversion sounding SIMULTANEOUSLY
           against itself (the six-fold judgment: assertion and its mirror), then
           restated in D with a rising tail it never had before.

TUNING NOTE (A=432 vs A=440)
  A=432 IS a supported system tuning and is the default in the one module with an
  opinion: kernel/src/audiogenomics_pro/audiogenomics_pro.h:76 declares
  `bool retune_432` in agp_freq_map_t, and audiogenomics_pro.c applies it as the
  flat scalar AGP_432_RATIO (432.0/440.0). Two honest caveats: that is the
  audiogenomics (DNA/RNA sonification) path, not a world-audio path, and there is
  no wiring between it and the hoard today; and its fields are `double`, i.e.
  host-side -- on target it would need the surplus_real_t treatment.
  These files are therefore written in STANDARD PITCH. MIDI note numbers are
  tuning-agnostic; retune is a playback decision and the ratio constant already
  exists to make it.

FILE DISCIPLINE
  This script writes only into assets/music/. It touches no .c, no .h, no Makefile.
"""

import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DIV = 480                      # ticks per quarter note
Q = DIV                        # one quarter
E = DIV // 2                   # one eighth

# ---------------------------------------------------------------- SMF plumbing

def vlq(n):
    """Standard MIDI variable-length quantity."""
    if n < 0:
        raise ValueError("negative delta time")
    out = bytearray([n & 0x7F])
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


# event ordering at an identical tick: meta/ctrl first, then note-offs, then
# note-ons. Off-before-on is what stops a repeated pitch from being cut short.
ORD_CTRL = 0
ORD_OFF = 1
ORD_ON = 2


class Track:
    def __init__(self, name, channel=0, program=None):
        self.name = name
        self.ch = channel
        self.ev = []
        self.seq = 0
        self._meta(0, 0x03, name.encode("ascii"))
        if program is not None:
            self.add(0, ORD_CTRL, bytes([0xC0 | channel, program & 0x7F]))

    def add(self, tick, order, data):
        self.ev.append((int(round(tick)), order, self.seq, bytes(data)))
        self.seq += 1

    def _meta(self, tick, mtype, payload, order=ORD_CTRL):
        self.add(tick, order, bytes([0xFF, mtype]) + vlq(len(payload)) + payload)

    def text(self, tick, s):
        self._meta(tick, 0x01, s.encode("ascii"))

    def marker(self, tick, s):
        self._meta(tick, 0x06, s.encode("ascii"))

    def tempo(self, tick, bpm):
        us = int(round(60000000.0 / bpm))
        self._meta(tick, 0x51, struct.pack(">I", us)[1:])

    def timesig(self, tick, num, den, clocks=24, n32=8):
        dd = 0
        d = den
        while d > 1:
            d >>= 1
            dd += 1
        self._meta(tick, 0x58, bytes([num, dd, clocks, n32]))

    def keysig(self, tick, sf, minor):
        self._meta(tick, 0x59, bytes([sf & 0xFF, 1 if minor else 0]))

    def cc(self, tick, num, val):
        v = max(0, min(127, int(round(val))))
        self.add(tick, ORD_CTRL, bytes([0xB0 | self.ch, num & 0x7F, v]))

    def loop_start(self, tick, label="LOOP_START"):
        # CC#111 is the widely-used loop-point convention; the text + marker make
        # it human-findable in any sequencer.
        self.cc(tick, 111, 0)
        self.text(tick, label)
        self.marker(tick, label)

    def note(self, tick, pitch, dur, vel):
        p = int(pitch)
        if not (0 <= p <= 127):
            raise ValueError("pitch out of range: %d" % p)
        v = max(1, min(127, int(round(vel))))
        d = int(round(dur))
        if d <= 0:
            raise ValueError("non-positive duration")
        self.add(tick, ORD_ON, bytes([0x90 | self.ch, p, v]))
        self.add(tick + d, ORD_OFF, bytes([0x80 | self.ch, p, 0x40]))

    def swell(self, t0, t1, lo, hi, steps=16):
        """CC#11 expression ramp, lo -> hi across [t0, t1]."""
        for i in range(steps + 1):
            f = i / float(steps)
            self.cc(t0 + (t1 - t0) * f, 11, lo + (hi - lo) * f)

    def arch(self, t0, t1, lo, hi, steps=24):
        """CC#11 rise-then-fall across [t0, t1]."""
        mid = (t0 + t1) / 2.0
        self.swell(t0, mid, lo, hi, steps // 2)
        self.swell(mid, t1, hi, lo, steps // 2)

    def build(self, end_tick):
        self._meta(end_tick, 0x2F, b"", order=3)   # FF 2F 00, last thing at the end
        self.ev.sort(key=lambda e: (e[0], e[1], e[2]))
        out = bytearray()
        last = 0
        for tick, _order, _seq, data in self.ev:
            out += vlq(tick - last)
            out += data
            last = tick
        return b"MTrk" + struct.pack(">I", len(out)) + bytes(out)


def write_smf(path, tracks, end_tick):
    hdr = b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), DIV)
    body = b"".join(t.build(end_tick) for t in tracks)
    with open(path, "wb") as f:
        f.write(hdr + body)
    return len(hdr + body)


# ------------------------------------------------------------ shared material
#
# THE DEEP CELL -- the melodic seed of this slice. D Phrygian.
# Contour: D, up a MINOR SIXTH to Bb, then a slow collapse A - G - F - Eb,
# hanging on the flat second and never touching the tonic again.
# The leap up and the refusal to come home are the signature; the stinger's whole
# job is to take that same leap and make it MAJOR.
DEEP_CELL = [62, 70, 69, 67, 65, 63]        # D4 Bb4 A4 G4 F4 Eb4

# rhythm of the cell across 4 bars of 5/4, as (bar, beat, dur_beats)
DEEP_RHYTHM = [(0, 0, 3), (0, 3, 2), (1, 0, 2), (1, 2, 1), (2, 0, 2), (2, 2, 3)]


# =============================================================================
# 1.  ROOTWYRM DEEP  --  "Stonebreath"
# =============================================================================
def build_stonebreath(path):
    BPM = 72
    BEAT = Q
    BAR = 5 * BEAT                       # 5/4
    BARS = 55                            # 34 + 21, phi-proportioned
    END = BARS * BAR                     # 132000 ticks = 275 beats

    def T(bar, beat=0.0):
        return int(round(bar * BAR + beat * BEAT))

    t0 = Track("Stonebreath -- conductor")
    t0.tempo(0, BPM)
    t0.timesig(0, 5, 4)
    t0.keysig(0, -1, True)
    t0.text(0, "ROOTWYRM DEEP -- the hold beneath the shell")
    t0.text(0, "phi: 55 bars = 34 atmosphere + 21 forming. 13-beat ostinato under 5/4.")
    t0.marker(0, "LOOP_START")
    t0.marker(T(34), "SECTION 21 -- the fragment forms")
    t0.marker(T(48), "dissolve")

    pad = Track("Deep Pad", channel=0, program=88)        # GM 89 Pad 1 (new age)
    perc = Track("Stone Marimba (13-beat)", channel=1, program=12)  # GM 13 Marimba
    horn = Track("Horizon Brass", channel=2, program=57)  # GM 58 Trombone
    frag = Track("Deep Cell -- pan flute", channel=3, program=75)   # GM 76 Pan Flute
    drum = Track("Root Percussion", channel=9)            # channel 10

    for t, lvl in ((pad, 82), (perc, 96), (horn, 78), (frag, 104), (drum, 74)):
        t.cc(0, 7, lvl)          # channel volume: a sane audition mix
        t.loop_start(0)

    # ---- pad: 7 segments, every length a Fibonacci number, summing to 55.
    # Quartal / added-second stacks. No segment contains a functional leading
    # tone, so nothing ever cadences.
    segs = [
        (0,  8,  [38, 45]),          # D2 A2            -- bare fifth, no third
        (8,  5,  [38, 45, 51]),      # + Eb3            -- the flat second rubs
        (13, 8,  [38, 43, 48]),      # D2 G2 C3         -- quartal, suspended
        (21, 13, [39, 46, 50]),      # Eb2 Bb2 D3       -- the floor shifts
        (34, 8,  [38, 45, 53]),      # D2 A2 F3
        (42, 5,  [36, 43, 46]),      # C2 G2 Bb2
        (47, 8,  [39, 46, 50]),      # Eb2 Bb2 D3       -- returns changed
    ]
    assert sum(s[1] for s in segs) == BARS
    for i, (b, ln, chord) in enumerate(segs):
        st = T(b)
        en = T(b + ln)
        if i == len(segs) - 1:
            en -= BEAT                     # one beat of air before the seam
        for j, p in enumerate(chord):
            pad.note(st, p, en - st, 44 + 4 * j)
        pad.arch(st, en, 52, 96)
    pad.cc(END, 11, 100)

    # ---- the 13-beat ostinato. 21 statements of 13 beats = 273 of 275 beats.
    CYC = 13 * BEAT
    fig = [(0, 50), (3, 57), (5, 58), (8, 55), (11, 63)]      # D3 A3 Bb3 G3 Eb4
    retro = [(0, 63), (3, 55), (5, 58), (8, 57), (11, 50)]    # pitch order reversed
    for c in range(21):
        base = c * CYC
        if c <= 1:
            picks, src, oct_ = [0, 3], fig, 0
        elif c <= 3:
            picks, src, oct_ = [0, 2, 3], fig, 0
        elif c <= 7:
            picks, src, oct_ = [0, 1, 2, 3, 4], fig, 0
        elif c <= 12:
            picks, src, oct_ = [0, 1, 2, 3, 4], fig, 0        # top note lifted below
        elif c <= 16:
            picks, src, oct_ = [0, 1, 2, 3, 4], retro, 0      # SPIRAL: retrograde
        elif c <= 19:
            picks, src, oct_ = [0, 1, 3, 4], retro, 12        # thinner, an octave up
        else:
            picks, src, oct_ = [0, 4], retro, 12              # dissolve
        # velocity contour: grows to statement 13, then recedes
        vel = 40 + int(38 * (c / 13.0)) if c <= 13 else 78 - int(46 * ((c - 13) / 7.0))
        for k, idx in enumerate(picks):
            off, pit = src[idx]
            p = pit + oct_
            if 8 <= c <= 12 and idx == 4:
                p += 12                                       # the top note lifts
            dur = BEAT if idx == picks[-1] else BEAT // 2
            perc.note(base + off * BEAT, p, dur, vel - 6 * (k % 2))

    # ---- horizon brass: four swells, at bars 8 / 21 / 34 / 47.
    for b, ln, p, v in ((8, 3, 45, 46), (21, 3, 43, 50), (34, 2, 46, 56), (47, 4, 51, 42)):
        st, en = T(b), T(b + ln)
        if en > END - BEAT:
            en = END - BEAT
        horn.note(st, p, en - st, v)
        horn.swell(st, st + (en - st) * 0.45, 20, 92, 12)
        horn.swell(st + (en - st) * 0.45, en, 92, 14, 12)
    horn.cc(END, 11, 100)

    # ---- the Deep Cell: three transformations, then dissolution.
    def place(bar0, pitches, beat_shift=0.0, vel=70, octave=0):
        for (bb, be, bd), p in zip(DEEP_RHYTHM, pitches):
            frag.note(T(bar0 + bb, be + beat_shift), p + octave, bd * BEAT, vel)

    place(34, DEEP_CELL, 0.0, 72)                                    # statement
    place(39, [p + 5 for p in DEEP_CELL], 1.0, 64)                   # +5, displaced 1 beat
    place(44, list(reversed(DEEP_CELL)), 0.0, 58)                    # retrograde
    frag.note(T(49, 0), 70, 4 * BEAT, 48)                            # dissolve: Bb4
    frag.note(T(51, 2), 69, 3 * BEAT, 40)                            #           A4
    frag.note(T(53, 0), 63, 4 * BEAT, 32)                            #           Eb4, hanging
    frag.arch(T(34), T(48), 70, 105, 16)
    frag.swell(T(48), T(54), 90, 30, 12)

    # ---- percussion: the 13 made audible, and a lot of air.
    for c in range(21):
        base = c * CYC
        drum.note(base, 41, 120, 52 + (10 if c % 3 == 0 else 0))     # low floor tom
        if 4 <= c <= 18:
            drum.note(base + 5 * BEAT, 82, 90, 30)                   # shaker
            drum.note(base + 8 * BEAT, 82, 90, 26)
        if base >= T(34):
            drum.note(base, 36, 120, 46)                             # kick joins at bar 34
    drum.note(T(21), 52, 240, 38)                                    # chinese cymbal, horizon
    drum.note(T(47), 52, 240, 34)

    n = write_smf(path, [t0, pad, perc, horn, frag, drum], END)
    return dict(path=path, bytes=n, end=END, bars=BARS, bpm=BPM,
                seconds=END / float(DIV) * 60.0 / BPM)


# =============================================================================
# 2.  THE WYRMGATE THRESHOLD  --  "Sixfold Verdict"
# =============================================================================
def build_verdict(path):
    BPM = 156                            # quarter = 156, so the eighth is the pulse
    BAR = 13 * E                         # 13/8 = 3120 ticks
    BARS = 55
    END = BARS * BAR
    ACC = [0, 3, 6, 9, 11]               # 3+3+3+2+2

    def T(bar, eighth=0.0):
        return int(round(bar * BAR + eighth * E))

    t0 = Track("Sixfold Verdict -- conductor")
    t0.tempo(0, BPM)
    t0.timesig(0, 13, 8)
    t0.keysig(0, -1, True)
    t0.text(0, "THE WYRMGATE THRESHOLD -- tri-space judgment, S+ / S0 / S-")
    t0.text(0, "13/8 as 3+3+3+2+2. 8-eighth bass under 13/8: realigns every 8 bars.")
    t0.marker(0, "INTRO (plays once)")
    t0.marker(T(5), "LOOP_START -- S+ ASSERTION (8)")
    t0.marker(T(13), "S+ contracted (5)")
    t0.marker(T(18), "S+ compressed (3)")
    t0.marker(T(21), "S0 SUSPENSION (8)")
    t0.marker(T(29), "S- RESOLUTION (21)")
    t0.marker(T(50), "turnaround (5) -> loop")

    brass = Track("Tribunal Brass", channel=0, program=61)   # GM 62 Brass Section
    organ = Track("Threshold Organ", channel=1, program=19)  # GM 20 Church Organ
    bass = Track("Gate Bass (8-eighth cycle)", channel=2, program=38)  # GM 39 Synth Bass 1
    timp = Track("Verdict Timpani", channel=3, program=47)   # GM 48 Timpani
    drum = Track("Threshold Percussion", channel=9)

    LOOP = T(5)
    for t, lvl in ((brass, 104), (organ, 88), (bass, 100), (timp, 92), (drum, 96)):
        t.cc(0, 7, lvl)
        t.cc(0, 11, 100)
        t.loop_start(LOOP, "LOOP_START")

    # ---- THE VERDICT HOOK. D Phrygian. (off_eighths, semitone-from-D, dur_eighths)
    # D4 F4 Bb4 - A4 - Eb4 D4 : rise by fourths, then the signature TRITONE DROP
    # A -> Eb, then a flat slam home. Angular, obsessive, hummable.
    HOOK = [(0, 0, 2), (3, 3, 2), (6, 8, 1), (8, 7, 1), (9, 1, 1), (11, 0, 2)]
    D4 = 62

    def hook(track, bar, root=D4, shift=0.0, vel=100, oct_=0, invert=False, tail=None):
        for off, iv, dur in HOOK:
            # a displaced statement is truncated at the bar line rather than
            # spilling into the next bar's downbeat -- otherwise the tail note
            # doubles the next hook's first note on the same pitch and channel.
            if off + shift + dur > 13:
                continue
            step = -iv if invert else iv
            track.note(T(bar, off + shift), root + step + oct_, dur * E,
                       vel - (8 if off in (8, 9) else 0))
        if tail:
            for off, p, dur, v in tail:
                track.note(T(bar, off), p, dur * E, v)

    # ---- S+ ASSERTION, 8 bars (5..12)
    for b in (5, 6, 9, 10):
        hook(brass, b, vel=104)
    hook(brass, 7, vel=100)
    brass.note(T(7, 12), 63, E, 88)                           # Eb jab, anticipating bar 8
    hook(brass, 8, root=D4 + 5, vel=96)                       # +4th: the Ab bites
    hook(brass, 11, shift=2.0, vel=98)                        # displaced 2 eighths
    hook(brass, 12, vel=110,
         tail=[(0, 50, 1, 100)])                              # octave-doubled downbeat
    for off, p in ((6, 65), (8, 70), (10, 72), (11, 74)):     # rising push into the 5
        brass.note(T(12, off), p, E, 96 + off)
    for b in range(5, 13):
        organ.note(T(b, 0), 38, 13 * E, 58)                   # D2 pedal
        organ.note(T(b, 0), 45, 13 * E, 50)

    # ---- contracted to 5 bars (13..17): head of the hook, twice a bar
    for i, b in enumerate(range(13, 18)):
        up = 3 if b == 15 else (0 if b != 17 else 1)
        for base_off in (0, 6):
            for k, (o, iv) in enumerate(((0, 0), (1, 3), (2, 8))):
                brass.note(T(b, base_off + o), D4 + iv + up, E, 100 + 3 * i)
        brass.note(T(b, 11), D4 + 1 + up, 2 * E, 92 + 3 * i)
        organ.note(T(b, 0), 38 + up, 13 * E, 56)
    # ---- compressed to 3 bars (18..20): two pitches, hammered on every accent
    for i, b in enumerate(range(18, 21)):
        for k, off in enumerate(ACC):
            brass.note(T(b, off), D4 if k % 2 == 0 else D4 + 1, E, 96 + 6 * i + 2 * k)
        organ.note(T(b, 0), 38, 13 * E, 62 + 6 * i)
        organ.note(T(b, 0), 39, 13 * E, 40 + 6 * i)           # D + Eb, grinding

    # ---- S0 SUSPENSION, 8 bars (21..28): one held note and the drums.
    organ.note(T(21), 38, 8 * 13 * E, 70)
    organ.note(T(21), 50, 8 * 13 * E, 48)
    organ.swell(T(21), T(25), 96, 34, 12)
    organ.swell(T(25), T(29), 34, 108, 16)
    for b in range(25, 29):                                   # timpani starts counting
        for off in (0, 9):
            timp.note(T(b, off), 38, 2 * E, 48 + 8 * (b - 25))

    # ---- S- RESOLUTION, 21 bars (29..49) = 8 + 5 + 8
    for b in range(29, 37):                                   # 8: hook up a fourth
        hook(brass, b, root=D4 + 5, vel=104)
        organ.note(T(b, 0), 43, 13 * E, 60)
        organ.note(T(b, 0), 50, 13 * E, 52)
        for off in ACC:
            timp.note(T(b, off), 43 if off in (0, 6) else 38, E, 62)
    for b in range(37, 42):                                   # 5: assertion vs its mirror
        hook(brass, b, vel=104)
        hook(organ, b, root=D4, invert=True, oct_=-12, vel=76)
        for off in (0, 9):
            timp.note(T(b, off), 38, 2 * E, 70)
    for i, b in enumerate(range(42, 50)):                     # 8: restated in D, rising tail
        hook(brass, b, vel=100 + 2 * i)
        organ.note(T(b, 0), 38, 13 * E, 66)
        organ.note(T(b, 0), 45, 13 * E, 58)
        organ.note(T(b, 0), 53, 13 * E, 46)
        for off in ACC:
            timp.note(T(b, off), 38 if off != 9 else 41, E, 66 + 2 * i)
        if b >= 46:                                           # the tail it never had
            for k, (off, p) in enumerate(((10, 70), (11, 72), (12, 74))):
                brass.note(T(b, off), p, E, 104 + 2 * i + k)

    # ---- turnaround, 5 bars (50..54): thin to bass + drums, hand back to bar 5
    organ.note(T(50), 38, 5 * 13 * E - E, 60)
    organ.swell(T(50), T(54, 12), 92, 44, 14)
    for b in (50, 52):
        hook(brass, b, vel=84 - 8 * ((b - 50) // 2), oct_=-12)
    timp.note(T(54, 9), 38, 2 * E, 74)
    timp.note(T(54, 11), 38, 2 * E, 84)

    # ---- BASS: an 8-eighth cell under 13/8 bars. lcm(8,13) = 104 eighths = 8 bars.
    # (off_eighths, semitone-from-D2, dur_eighths). Offset 4 is deliberately empty.
    CELL = [(0, 0, 1), (1, 0, 1), (2, 3, 1), (3, 0, 1), (5, -2, 1), (6, 1, 1), (7, 0, 1)]
    D2 = 38
    TOTAL_E = BARS * 13
    LOOP_E = 5 * 13                                           # = 65 = 1 + 8*8
    starts = list(range(1, LOOP_E, 8)) + list(range(LOOP_E, TOTAL_E, 8))
    for s in starts:
        for off, iv, dur in CELL:
            pos = s + off
            if pos >= TOTAL_E:
                break                                         # truncate: nothing crosses the seam
            if pos + dur > TOTAL_E:
                dur = TOTAL_E - pos
            bar_i = pos // 13
            quiet = 21 <= bar_i <= 24                         # S0: bass drops out
            if quiet:
                continue
            v = 96
            if 25 <= bar_i <= 28:
                v = 62 + 6 * (bar_i - 25)                     # crawls back in
            elif bar_i >= 50:
                v = 84
            bass.note(pos * E, D2 + iv, dur * E, v)

    # ---- DRUMS. The 3+3+3+2+2 made physical, restyled per section.
    for b in range(BARS):
        if b < 5:
            style = "intro"
        elif b < 21:
            style = "drive"
        elif b < 29:
            style = "suspend"
        elif b < 50:
            style = "drive"
        else:
            style = "turn"
        if style in ("intro", "drive", "turn"):
            for off in range(13):
                acc = off in ACC
                if style == "intro" and off not in ACC and off % 2:
                    continue
                drum.note(T(b, off), 42, E // 2, (98 if acc else 58) - (14 if style == "turn" else 0))
            for off in (0, 6, 9):
                drum.note(T(b, off), 36, E, 104 if off == 0 else 92)
            for off in (3, 11):
                drum.note(T(b, off), 38, E, 100)
            if b % 8 == 4:                                    # fill on the bass realignment
                for k, off in enumerate((9, 10, 11, 12)):
                    drum.note(T(b, off), (48, 47, 45, 41)[k], E // 2, 88 + 4 * k)
            if b == 12 or b == 17 or b == 20:
                drum.note(T(b, 11), 46, 2 * E, 96)            # open hat lifts the seam
        else:                                                 # S0: air and dread
            for off in (0, 6, 9):
                drum.note(T(b, off), 41, E, 60 + 4 * (b - 21))
            if b % 2 == 1:
                drum.note(T(b, 11), 51, 2 * E, 44)            # ride, distant
        if b in (5, 21, 29, 42):
            drum.note(T(b, 0), 49, 2 * E, 110)                # crash on the seams

    n = write_smf(path, [t0, brass, organ, bass, timp, drum], END)
    return dict(path=path, bytes=n, end=END, bars=BARS, bpm=BPM, loop=LOOP,
                seconds=END / float(DIV) * 60.0 / BPM)


# =============================================================================
# 3.  GLINTFALL  --  "First Gleam"   (one-shot, no loop)
# =============================================================================
def build_gleam(path):
    BPM = 120                            # matches the title/hub tempo so it fires over anything
    BAR = 4 * Q
    BARS = 3
    END = BARS * BAR

    def T(bar, beat=0.0):
        return int(round(bar * BAR + beat * Q))

    t0 = Track("First Gleam -- conductor")
    t0.tempo(0, BPM)
    t0.timesig(0, 4, 4)
    t0.keysig(0, 2, False)               # D major
    t0.text(0, "GLINTFALL -- one-shot. NO loop marker by design.")
    t0.text(0, "Deep Cell inverted: the minor 6th becomes MAJOR and climbs.")
    t0.marker(0, "ONE_SHOT")

    glock = Track("Gleam", channel=0, program=9)       # GM 10 Glockenspiel
    brass = Track("Gleam Brass", channel=1, program=61)
    strg = Track("Gleam Strings", channel=2, program=48)   # GM 49 String Ensemble 1
    drum = Track("Gleam Cymbal", channel=9)

    # THE TRANSFORMATION, note for note:
    #   Deep Cell : D  -> Bb (MINOR 6th up) -> A -> G -> F -> Eb   descending, hanging on b2
    #   First Gleam: D -> B  (MAJOR 6th up) -> A -> B -> D         ascending, landing home
    # and the Eb that hung unresolved in the hold returns as E natural, rising
    # through F# -- the exact pitch the ruins refused to brighten.
    fig = [(0, 0.0, 74, 0.5, 96),        # D5
           (0, 0.5, 83, 1.0, 108),       # B5   <- the leap, now MAJOR
           (0, 1.5, 81, 0.5, 100),       # A5
           (0, 2.0, 79, 0.5, 98),        # G5
           (0, 2.5, 83, 0.5, 104),       # B5
           (0, 3.0, 86, 1.0, 112),       # D6
           (1, 0.0, 76, 0.5, 100),       # E5   <- the old flat second, freed
           (1, 0.5, 78, 0.5, 104),       # F#5
           (1, 1.0, 81, 0.5, 108),       # A5
           (1, 1.5, 86, 0.5, 112),       # D6
           (1, 2.0, 90, 0.5, 116),       # F#6
           (1, 2.5, 93, 1.5, 120)]       # A6
    for b, be, p, d, v in fig:
        glock.note(T(b, be), p, int(d * Q), v)

    # the landing: an open D major, hard-stopped on the final tick
    LAND = T(2, 0)
    CHORD_LEN = 3 * Q + Q // 2           # rings most of bar 3, then a half-beat of tail
    for p, v in ((50, 96), (57, 90), (62, 94), (66, 92), (69, 88), (74, 98)):
        strg.note(LAND, p, CHORD_LEN, v)
        brass.note(LAND, p, Q, v - 6)
    for k, p in enumerate((86, 90, 93, 98)):        # sparkle over the top
        glock.note(LAND + k * (Q // 4), p, Q, 118 - 4 * k)
    strg.swell(T(0), LAND, 60, 118, 12)
    strg.swell(LAND, LAND + CHORD_LEN, 118, 62, 10)   # decay, not a hard cut

    # rising cymbal swell into the landing, then a single bright crash
    for k in range(8):
        drum.note(T(1, k * 0.25), 42, 60, 40 + 8 * k)         # closed hat crescendo
    drum.note(T(1, 2.0), 55, 2 * Q, 84)                       # splash, rising
    drum.note(LAND, 49, CHORD_LEN, 118)                       # crash on the landing
    drum.note(LAND, 35, Q, 100)

    n = write_smf(path, [t0, glock, brass, strg, drum], END)
    return dict(path=path, bytes=n, end=END, bars=BARS, bpm=BPM,
                seconds=END / float(DIV) * 60.0 / BPM)


# =============================================================================
#  VERIFIER -- re-parses the bytes off disk. No trust in the writer above.
# =============================================================================
def read_vlq(buf, i):
    n = 0
    while True:
        b = buf[i]
        i += 1
        n = (n << 7) | (b & 0x7F)
        if not (b & 0x80):
            return n, i


def verify(path, expect_end=None, expect_tracks=None, expect_loop=True):
    errs = []
    info = {}
    data = open(path, "rb").read()
    if data[:4] != b"MThd":
        errs.append("no MThd")
        return errs, info
    hlen, fmt, ntrks, div = struct.unpack(">IHHH", data[4:14])
    info["format"], info["ntrks"], info["division"] = fmt, ntrks, div
    if hlen != 6:
        errs.append("MThd length %d != 6" % hlen)
    if fmt != 1:
        errs.append("format %d != 1" % fmt)
    if div != 480:
        errs.append("division %d != 480" % div)
    if expect_tracks is not None and ntrks != expect_tracks:
        errs.append("ntrks %d != %d" % (ntrks, expect_tracks))

    pos = 8 + hlen
    stacked = []
    seen_tempo = seen_ts = seen_loopcc = False
    seen_texts = 0
    max_tick = 0
    total_notes = 0
    tracks = []
    for ti in range(ntrks):
        if data[pos:pos + 4] != b"MTrk":
            errs.append("track %d: no MTrk at %d" % (ti, pos))
            break
        tlen = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + tlen]
        pos += 8 + tlen
        i = 0
        tick = 0
        running = None
        open_notes = {}
        eot = None
        nnotes = 0
        nprog = 0
        while i < len(body):
            dt, i = read_vlq(body, i)
            tick += dt
            if eot is not None:
                errs.append("track %d: events after FF 2F 00" % ti)
                break
            st = body[i]
            if st & 0x80:
                i += 1
                running = st if st < 0xF0 else None
            else:
                if running is None:
                    errs.append("track %d: data byte with no running status" % ti)
                    break
                st = running
            if st == 0xFF:
                mt = body[i]
                i += 1
                ln, i = read_vlq(body, i)
                payload = body[i:i + ln]
                i += ln
                if mt == 0x2F:
                    if ln != 0:
                        errs.append("track %d: FF 2F length %d != 0" % (ti, ln))
                    eot = tick
                elif mt == 0x51:
                    seen_tempo = True
                elif mt == 0x58:
                    seen_ts = True
                elif mt in (0x01, 0x06, 0x03):
                    seen_texts += 1
            elif st in (0xF0, 0xF7):
                ln, i = read_vlq(body, i)
                i += ln
            else:
                hi = st & 0xF0
                ch = st & 0x0F
                if hi in (0xC0, 0xD0):
                    i += 1
                    if hi == 0xC0:
                        nprog += 1
                else:
                    d1 = body[i]
                    d2 = body[i + 1]
                    i += 2
                    if hi == 0xB0 and d1 == 111:
                        seen_loopcc = True
                    if hi == 0x90 and d2 > 0:
                        k = (ch, d1)
                        if open_notes.get(k):
                            # legal MIDI, but a real hazard: two note-ons for the
                            # same channel+pitch sounding at once means one
                            # note-off can strand the other on a hardware synth.
                            stacked.append((ti, ch, d1, tick))
                        open_notes.setdefault(k, []).append(tick)
                        nnotes += 1
                    elif hi == 0x80 or (hi == 0x90 and d2 == 0):
                        k = (ch, d1)
                        if not open_notes.get(k):
                            errs.append("track %d: note-off with no note-on ch%d p%d @%d"
                                        % (ti, ch, d1, tick))
                        else:
                            open_notes[k].pop()
                            if not open_notes[k]:
                                del open_notes[k]
        if eot is None:
            errs.append("track %d: missing FF 2F 00" % ti)
        if open_notes:
            errs.append("track %d: DANGLING note-ons: %r" % (ti, sorted(open_notes)))
        if ti > 0 and nnotes == 0:
            errs.append("track %d: EMPTY (no notes)" % ti)
        if ti > 0 and nprog == 0 and ti != ntrks - 1:
            errs.append("track %d: no program change" % ti)
        max_tick = max(max_tick, eot or tick)
        total_notes += nnotes
        tracks.append((ti, nnotes, eot))
    if not seen_tempo:
        errs.append("no FF 51 tempo")
    if not seen_ts:
        errs.append("no FF 58 time signature")
    if expect_loop and not seen_loopcc:
        errs.append("no CC#111 loop marker")
    if (not expect_loop) and seen_loopcc:
        errs.append("CC#111 present on a one-shot")
    if seen_texts == 0:
        errs.append("no text/marker meta")
    if stacked:
        errs.append("stacked duplicate note-ons (%d): %r" % (len(stacked), stacked[:6]))
    if expect_end is not None and max_tick != expect_end:
        errs.append("end tick %d != expected %d" % (max_tick, expect_end))
    ends = set(e for _, _, e in tracks if e is not None)
    if len(ends) > 1:
        errs.append("tracks end at differing ticks: %r" % sorted(ends))
    info["notes"] = total_notes
    info["end_tick"] = max_tick
    info["per_track_notes"] = [n for _, n, _ in tracks]
    return errs, info


def main():
    outdir = HERE
    jobs = [
        ("rootwyrm_deep_stonebreath.mid", build_stonebreath, 6, True),
        ("wyrmgate_threshold_sixfold_verdict.mid", build_verdict, 6, True),
        ("glintfall_first_gleam.mid", build_gleam, 5, False),
    ]
    ok = True
    for fname, fn, ntrk, loop in jobs:
        p = os.path.join(outdir, fname)
        meta = fn(p)
        errs, info = verify(p, expect_end=meta["end"], expect_tracks=ntrk, expect_loop=loop)
        print("=" * 74)
        print("%s  (%d bytes)" % (fname, meta["bytes"]))
        print("  bars=%d  bpm=%d  end_tick=%d  duration=%.1fs"
              % (meta["bars"], meta["bpm"], meta["end"], meta["seconds"]))
        print("  parsed: format=%d ntrks=%d division=%d notes=%d per-track=%r"
              % (info.get("format", -1), info.get("ntrks", -1), info.get("division", -1),
                 info.get("notes", -1), info.get("per_track_notes")))
        if errs:
            ok = False
            for e in errs:
                print("  FAIL: %s" % e)
        else:
            print("  PASS: header ok, every track ends FF 2F 00, all note-ons matched, "
                  "no empty track, duration as intended.")
    print("=" * 74)
    print("RESULT: %s" % ("ALL PASS" if ok else "FAILURES PRESENT"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
