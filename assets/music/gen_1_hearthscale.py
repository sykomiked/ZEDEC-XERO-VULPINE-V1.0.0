#!/usr/bin/env python3
# -*- coding: ascii -*-
"""
TOL VOVINA UPAAH LOT -- music slice 1: the HEARTHSCALE hold and its rest point.

Generates three Standard MIDI Files (SMF format 1, division 480):

    hearthscale_hub.mid       HEARTHSCALE          -- the town-hub theme (day)
    hearthscale_ashlight.mid  HEARTHSCALE, ASHLIGHT-- the night/rain variant
    coilrest_rest.mid         COILREST             -- the save / rest point

Python 3 standard library only. No pip, no external modules. Deterministic:
the same script always emits byte-identical files.

ref: the melodic and harmonic bearing is aimed at the bright, hook-forward
     adventure-town register of Mega Man Legends / Mega Man 64, and the
     percussion-forward, open-air treatment at Stewart Copeland's Spyro
     scores. Those are STYLE references only. Every pitch, phrase, bassline
     and progression below was written for this project and quotes nothing.
     Per [[proprietary naming]] these names appear here, in a ref: comment,
     and nowhere else -- not in a filename, a track name, or any shipped
     string inside the .mid files.

THE SYSTEM'S OWN MATHEMATICS, used audibly (see PROVENANCE/TVUL_MUSIC_HEARTHSCALE.md):

  * Form is Fibonacci, not square. The hub loop is 34 bars = F(9), split
    21 + 13, and the 21 is itself split 13 + 8. Nothing is 16 + 16.
  * The bass is a THIRTEEN-BEAT ostinato of THIRTEEN notes under 4/4. Its
    first step lands on beat-of-bar 0,1,2,3,0,1,2,3,... -- it realigns with
    the barline only every lcm(13,4) = 52 beats = 13 bars. Measured, not
    asserted: 28 of the 34 bars carry a (chord, bass figure) pairing unique
    in the loop; the 6 that repeat are precisely the 13-bar realignment
    pairs (4/17, 8/21, 9/22, 20/33, 21/34) plus bar 1's own return. A
    triangle is struck on beats 0, 52 and 104 -- exactly those realignment
    moments -- so the 13-cycle is audible rather than merely claimed.
  * 136 beats is not a multiple of 13, so the loop point cuts the ostinato
    mid-cycle: the SPIRAL SEAM named in TVUL_MUSIC_FORMAT.md 4.2. The cut
    is not a defect. Cycle 11 lands its 7th step on beat 135 over the A
    chord, degree = third = C#, a quarter note that ends exactly on the
    loop point and resolves to D at bar 1. The 13-cycle supplies its own
    leading tone into the loop.
  * The marimba runs a 21-EIGHTH cycle (F(8)) whose seven strikes sit at
    Fibonacci eighth-indices 0,2,3,5,8,13,16. 21 eighths against 8 eighths
    per bar realigns only every 21 bars, i.e. never inside this loop.
  * PHI relates the two tempos. Day = 110 BPM, night = 68 BPM.
    110/68 = 1.617647; phi = 1.618034. Error 0.024%. The night is the day
    slowed by the golden ratio.
  * COILREST is 13 bars of 4/4 = 52 beats = lcm(13,4) exactly: the whole
    rest-point loop is one complete spiral seam of the hub's ostinato, and
    the hub's 13-beat figure runs through it exactly four times.
  * SPIRAL, NOT CIRCLE. Material returns transformed, never copy-pasted:
    the hub's opening cell returns in the consequent displaced by an eighth,
    octave-shifted, reharmonised, and with its signature rising minor sixth
    inverted to a falling one. The night version keeps bars 1-21 in D major
    and reharmonises ONLY the 13-bar consequent to the relative minor.
    COILREST's 5-bar tail is its own 8-bar statement contour-inverted.

TUNING: written in standard pitch. MIDI note numbers are tuning-agnostic.
A=432 IS a supported system tuning -- kernel/src/audiogenomics_pro/
audiogenomics_pro.h:76 declares `bool retune_432` and audiogenomics_pro.c:33
defines AGP_432_RATIO (432.0/440.0) -- but that is the DNA-sonification
module, not a game-audio path, and retune is a playback decision. Do not
bake it into the source assets.
"""

import os
import struct
import sys

DIV = 480                     # ticks per quarter note
BEAT = DIV
BAR = 4 * BEAT                # 4/4 only in this slice
OUT = os.path.dirname(os.path.abspath(__file__))

PHI = (1 + 5 ** 0.5) / 2

# ----------------------------------------------------------------------------
# SMF primitives
# ----------------------------------------------------------------------------


def varint(n):
    if n < 0:
        raise ValueError("negative delta %d" % n)
    out = bytearray([n & 0x7F])
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


def meta(kind, payload):
    return b"\xff" + bytes([kind]) + varint(len(payload)) + payload


def text_meta(kind, s):
    return meta(kind, s.encode("ascii"))


def tempo_meta(bpm):
    uspq = int(round(60000000.0 / bpm))
    return meta(0x51, struct.pack(">I", uspq)[1:]), uspq


def timesig_meta(num, den_pow2, clocks=24, thirty2nds=8):
    return meta(0x58, bytes([num, den_pow2, clocks, thirty2nds]))


def keysig_meta(sharps, minor=0):
    return meta(0x59, bytes([sharps & 0xFF, minor]))


END_OF_TRACK = meta(0x2F, b"")


class Track(object):
    """An event accumulator. Events are (tick, order, bytes)."""

    ORDER_META = 0
    ORDER_CTRL = 1
    ORDER_OFF = 2
    ORDER_ON = 3

    def __init__(self, name, channel=None, program=None):
        self.name = name
        self.channel = channel
        self.events = []
        self._seq = 0
        self.add(0, self.ORDER_META, text_meta(0x03, name))
        if channel is not None and program is not None:
            self.add(0, self.ORDER_CTRL, bytes([0xC0 | channel, program]))

    def add(self, tick, order, data):
        self.events.append((int(round(tick)), order, self._seq, data))
        self._seq += 1

    def cc(self, tick, ctrl, value, channel=None):
        ch = self.channel if channel is None else channel
        self.add(tick, self.ORDER_CTRL, bytes([0xB0 | ch, ctrl, value]))

    def note(self, tick, pitch, vel, dur, channel=None, gate=0.94, limit=None):
        """Emit a matched note-on / note-off pair.

        `gate` shortens the note so consecutive notes never overlap and the
        final note never runs past the loop point. Real 0x8n note-offs are
        emitted -- TVUL_MUSIC_FORMAT.md 4.3 is explicit that a velocity-0
        note-on is NOT an alias for note-off in this system.
        """
        ch = self.channel if channel is None else channel
        t0 = int(round(tick))
        d = int(round(dur * gate))
        if d < 4:
            d = max(1, int(round(dur)))
        t1 = t0 + d
        if limit is not None:
            if t0 >= limit:
                return
            if t1 > limit:
                t1 = limit
        if t1 <= t0:
            return
        p = int(pitch)
        if p < 0 or p > 127:
            raise ValueError("pitch out of range: %d" % p)
        v = max(1, min(127, int(vel)))
        self.add(t0, self.ORDER_ON, bytes([0x90 | ch, p, v]))
        self.add(t1, self.ORDER_OFF, bytes([0x80 | ch, p, 64]))

    def serialize(self, total_ticks):
        evs = sorted(self.events, key=lambda e: (e[0], e[1], e[2]))
        body = bytearray()
        last = 0
        for tick, _order, _seq, data in evs:
            if tick > total_ticks:
                raise ValueError("%s: event at %d past end %d"
                                 % (self.name, tick, total_ticks))
            body += varint(tick - last)
            body += data
            last = tick
        body += varint(total_ticks - last)
        body += END_OF_TRACK
        return b"MTrk" + struct.pack(">I", len(body)) + bytes(body)


def write_smf(path, tracks, total_ticks):
    head = b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), DIV)
    chunks = b"".join(t.serialize(total_ticks) for t in tracks)
    with open(path, "wb") as fh:
        fh.write(head + chunks)
    return len(head + chunks)


# ----------------------------------------------------------------------------
# Harmony
# ----------------------------------------------------------------------------

PC = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6,
      "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}

# name -> (root pitch-class, intervals from root, bass pitch-class)
CHORDS = {
    "D":       (PC["D"],  [0, 4, 7],      PC["D"]),
    "Dsus2":   (PC["D"],  [0, 2, 7],      PC["D"]),
    "Bm":      (PC["B"],  [0, 3, 7],      PC["B"]),
    "Bm7":     (PC["B"],  [0, 3, 7, 10],  PC["B"]),
    "G":       (PC["G"],  [0, 4, 7],      PC["G"]),
    "Gadd9":   (PC["G"],  [0, 4, 7, 14],  PC["G"]),
    "A":       (PC["A"],  [0, 4, 7],      PC["A"]),
    "Asus4":   (PC["A"],  [0, 5, 7],      PC["A"]),
    "A7sus4":  (PC["A"],  [0, 5, 7, 10],  PC["A"]),
    "Em7":     (PC["E"],  [0, 3, 7, 10],  PC["E"]),
    "F#m":     (PC["F#"], [0, 3, 7],      PC["F#"]),
    "C":       (PC["C"],  [0, 4, 7],      PC["C"]),
    "D/F#":    (PC["D"],  [0, 4, 7],      PC["F#"]),
    "A/C#":    (PC["A"],  [0, 4, 7],      PC["C#"]),
    "Bm/F#":   (PC["B"],  [0, 3, 7],      PC["F#"]),
    "F#m/A":   (PC["F#"], [0, 3, 7],      PC["A"]),
    "G/D":     (PC["G"],  [0, 4, 7],      PC["D"]),
    "Gadd9/D": (PC["G"],  [0, 4, 7, 14],  PC["D"]),
    "Em7/D":   (PC["E"],  [0, 3, 7, 10],  PC["D"]),
}


def pick(pc, lo, hi, near):
    """Lowest-cost octave placement of pitch-class `pc` inside [lo,hi]."""
    pc %= 12
    best = None
    p = lo + ((pc - lo) % 12)
    while p <= hi:
        if best is None or abs(p - near) < abs(best - near):
            best = p
        p += 12
    if best is None:
        raise ValueError("no placement for pc %d in [%d,%d]" % (pc, lo, hi))
    return best


def tone(chord_name, idx, lo, hi, near):
    """idx-th chord tone. idx wraps through the chord and climbs octaves."""
    root, ivs, _bass = CHORDS[chord_name]
    n = len(ivs)
    pc = (root + ivs[idx % n]) % 12
    oct_up = idx // n
    p = pick(pc, lo, hi, near + 12 * oct_up)
    return p


def bass_tone(chord_name, deg, lo=31, hi=57):
    """deg 0 root (the SLASH bass if there is one), 1 third, 2 fifth, 3 octave."""
    root, ivs, bass = CHORDS[chord_name]
    if deg == 0:
        return pick(bass, lo, hi, 38)
    if deg == 1:
        return pick((root + ivs[1]) % 12, lo, hi, 42)
    if deg == 2:
        return pick((root + ivs[2]) % 12, lo, hi, 45)
    if deg == 3:
        return pick(bass, lo, hi, 50)
    raise ValueError(deg)


def chord_at(chart, beat):
    """chart is a list of chord names, one per bar (1-based bars)."""
    bar = int(beat // 4)
    if bar >= len(chart):
        bar = len(chart) - 1
    return chart[bar]


# ----------------------------------------------------------------------------
# The shared structural material
# ----------------------------------------------------------------------------

# The THIRTEEN-BEAT ostinato: thirteen notes in thirteen beats.
# (offset in beats within the cycle, chord degree, duration in beats)
OSTINATO_13 = [
    (0.0,  0, 1.0),
    (1.0,  2, 0.5),
    (1.5,  0, 0.5),
    (2.5,  3, 0.5),
    (3.0,  2, 1.0),
    (4.5,  0, 0.5),
    (5.0,  1, 1.0),
    (6.5,  2, 0.5),
    (7.0,  0, 1.0),
    (8.5,  3, 0.5),
    (9.0,  2, 1.5),
    (11.0, 1, 0.5),
    (11.5, 0, 1.5),
]
OSTINATO_LEN = 13.0

# The MARIMBA 21-eighth cycle. Seven strikes at Fibonacci eighth-indices.
# (eighth index within the 21-eighth cycle, chord tone index, duration in eighths)
MARIMBA_21 = [
    (0,  2, 2),
    (2,  3, 1),
    (3,  4, 1),
    (5,  1, 2),
    (8,  3, 2),
    (13, 2, 1),
    (16, 0, 3),
]
MARIMBA_LEN = 21  # eighths

# Clean-guitar comping cells, selected by a 13-long selector so that no two
# bars of the 34-bar loop wear the same combination of chord and figure twice.
GTR_CELLS = [
    [(0.0, 0, 0.5), (0.5, 2, 0.5), (1.5, 1, 1.0),
     (2.5, 3, 0.5), (3.0, 2, 0.5), (3.5, 1, 0.5)],
    [(0.0, 2, 1.0), (1.0, 3, 0.5), (2.0, 1, 1.0),
     (3.0, 0, 0.5), (3.5, 2, 0.5)],
    [(0.5, 3, 0.5), (1.0, 2, 0.5), (2.0, 0, 1.0), (3.5, 1, 0.5)],
]
GTR_SEL13 = [0, 2, 1, 0, 1, 2, 0, 1, 0, 2, 1, 0, 1]

# --- HEARTHSCALE, the hub -----------------------------------------------------
#
# 34 bars = F(9). Antecedent 21 (= 13 + 8), consequent 13.
#
HUB_CHART = [
    # A-phrase, 13 bars: bars 1-13. Lands on Bm, not D -- the deceptive
    # landing is what makes the phrase 13 bars long instead of 12.
    "D", "Bm", "G", "A", "D", "Bm", "Em7", "A", "D", "F#m", "G", "A", "Bm",
    # B-phrase, 8 bars: bars 14-21. The flat-seventh (C) colour; the lead
    # sits out for three bars of it so the percussion bed can be heard.
    "G", "D/F#", "Em7", "A", "G", "C", "G", "A",
    # C-phrase, 13 bars: bars 22-34. The return, transformed.
    "D", "A/C#", "Bm", "G", "D/F#", "Em7", "A",
    "D", "G", "A", "Bm", "G", "A",
]
assert len(HUB_CHART) == 34

# The hook. (bar, beat within bar 1.0-4.75, duration in beats, MIDI pitch)
# Signature: a stepwise three-note climb, then a RISING MINOR SIXTH to the
# top note, then a dotted-quarter push down. The leap and the dotted push
# are what make it hummable; the sixth is inverted in the consequent.
HUB_MELODY = [
    # -- A-phrase ------------------------------------------------------------
    (1, 1.0, 0.5, 62), (1, 1.5, 0.5, 64), (1, 2.0, 1.0, 66),
    (1, 3.0, 1.0, 74), (1, 4.0, 0.5, 73), (1, 4.5, 0.5, 71),      # F#4 -> D5
    (2, 1.0, 1.5, 69), (2, 2.5, 0.5, 71), (2, 3.0, 2.0, 66),
    (3, 1.0, 0.5, 67), (3, 1.5, 0.5, 69), (3, 2.0, 1.0, 71),
    (3, 3.0, 1.0, 79), (3, 4.0, 0.5, 78), (3, 4.5, 0.5, 76),      # B4 -> G5
    (4, 1.0, 1.5, 74), (4, 2.5, 0.5, 73), (4, 3.0, 2.0, 69),
    (5, 1.0, 1.0, 66), (5, 2.0, 1.0, 69), (5, 3.0, 0.5, 74),
    (5, 3.5, 0.5, 76), (5, 4.0, 1.0, 78),
    (6, 1.0, 1.5, 76), (6, 2.5, 0.5, 74), (6, 3.0, 1.0, 71),
    (6, 4.0, 1.0, 73),
    (7, 1.0, 1.0, 74), (7, 2.0, 0.5, 71), (7, 2.5, 0.5, 67),
    (7, 3.0, 2.0, 69),
    (8, 1.0, 0.5, 71), (8, 1.5, 0.5, 73), (8, 2.0, 1.0, 74),
    (8, 3.0, 2.0, 69),
    (9, 1.0, 1.0, 74), (9, 2.0, 0.5, 73), (9, 2.5, 0.5, 71),
    (9, 3.0, 1.0, 69), (9, 4.0, 1.0, 66),
    (10, 1.0, 1.5, 69), (10, 2.5, 0.5, 71), (10, 3.0, 2.0, 73),
    (11, 1.0, 1.0, 71), (11, 2.0, 0.5, 69), (11, 2.5, 1.5, 67),
    (11, 4.0, 1.0, 69),
    (12, 1.0, 1.5, 71), (12, 2.5, 0.5, 73), (12, 3.0, 2.0, 76),
    (13, 1.0, 2.0, 78), (13, 3.0, 2.0, 74),
    # -- B-phrase: three bars of air, then the flat-seventh turn -------------
    (17, 3.0, 0.5, 64), (17, 3.5, 0.5, 66),
    (18, 1.0, 1.0, 67), (18, 2.0, 1.0, 71), (18, 3.0, 1.0, 74),
    (18, 4.0, 0.5, 71), (18, 4.5, 0.5, 72),                        # C natural
    (19, 1.0, 1.5, 72), (19, 2.5, 0.5, 71), (19, 3.0, 2.0, 67),
    (20, 1.0, 1.0, 69), (20, 2.0, 1.0, 71), (20, 3.0, 2.0, 74),
    (21, 1.0, 1.0, 73), (21, 2.0, 1.0, 71), (21, 3.0, 2.0, 69),
    # -- C-phrase: the opening cell displaced by an eighth, up an octave,
    #    reharmonised, and its rising sixth turned into a falling one -------
    (22, 1.5, 0.5, 74), (22, 2.0, 0.5, 76), (22, 2.5, 1.0, 78),
    (22, 3.5, 1.5, 69),                                            # F#5 -> A4
    (23, 1.0, 0.5, 73), (23, 1.5, 0.5, 71), (23, 2.0, 1.5, 69),
    (23, 3.5, 0.5, 71), (23, 4.0, 1.0, 73),
    (24, 1.0, 2.0, 74), (24, 3.0, 1.0, 78), (24, 4.0, 1.0, 76),
    (25, 1.0, 1.5, 74), (25, 2.5, 0.5, 71), (25, 3.0, 2.0, 67),
    (26, 1.0, 1.0, 66), (26, 2.0, 1.0, 69), (26, 3.0, 2.0, 74),
    (27, 1.0, 1.0, 76), (27, 2.0, 0.5, 74), (27, 2.5, 0.5, 71),
    (27, 3.0, 2.0, 67),
    (28, 1.0, 0.5, 69), (28, 1.5, 0.5, 71), (28, 2.0, 1.0, 73),
    (28, 3.0, 2.0, 76),
    (29, 1.0, 1.5, 78), (29, 2.5, 0.5, 76), (29, 3.0, 2.0, 74),
    (30, 1.0, 1.0, 71), (30, 2.0, 1.0, 74), (30, 3.0, 2.0, 79),
    (31, 1.0, 1.5, 78), (31, 2.5, 0.5, 76), (31, 3.0, 2.0, 73),
    (32, 1.0, 1.0, 74), (32, 2.0, 1.0, 71), (32, 3.0, 2.0, 66),
    (33, 1.0, 0.5, 67), (33, 1.5, 0.5, 69), (33, 2.0, 1.0, 71),
    (33, 3.0, 2.0, 74),
    (34, 1.0, 1.0, 71), (34, 2.0, 1.0, 69), (34, 3.0, 1.0, 66),
    (34, 4.5, 0.5, 61),                     # C#4 leading tone into bar 1's D4
]

# --- HEARTHSCALE, ASHLIGHT ----------------------------------------------------
# Bars 1-21 are the hub's own chords, unchanged: this is the same place, and
# the ear must recognise it inside two bars. ONLY the 13-bar consequent is
# reharmonised, to the relative minor. B minor borrows no accidentals from
# D major, so the day melody sits on the night harmony without a single
# altered note -- which is exactly why the reharmonisation reads as light
# changing rather than as a different tune.
ASH_CHART = HUB_CHART[:21] + [
    "Bm", "F#m/A", "G", "D", "Bm/F#", "Em7", "F#m",
    "Bm", "G", "A", "Bm", "G", "A",
]
assert len(ASH_CHART) == 34

# --- COILREST -----------------------------------------------------------------
# 13 bars of 4/4 = 52 beats = lcm(13,4). The entire rest-point loop is one
# complete realignment period of the hub's ostinato, so the hub's 13-beat
# figure runs through it exactly four times and lands home on the last tick.
# Structure 8 + 5. Plagal throughout; no dominant push, nothing to resolve.
REST_CHART = [
    "D", "Gadd9/D", "D", "Em7/D", "G/D", "D", "Bm7", "G",
    "D/F#", "Em7", "A7sus4", "D", "G/D",
]
assert len(REST_CHART) == 13

# Glockenspiel. Bars 1-8 state the hub's climb slowed to one note per bar and
# then the hub's rising minor sixth PLAYED BACKWARDS (D5 down to F#4, the same
# eight semitones). Bars 9-13 are that contour inverted and lifted an octave.
REST_GLOCK = [
    (1, 3.0, 2.0, 74),
    (2, 3.0, 2.0, 76),
    (3, 1.0, 3.0, 78),
    (5, 1.0, 2.0, 74), (5, 3.0, 2.0, 66),     # descending minor sixth
    (6, 1.0, 2.0, 67), (6, 3.0, 2.0, 69),
    (7, 1.0, 4.0, 71),
    (8, 1.0, 2.0, 74), (8, 3.0, 2.0, 71),
    (9, 1.0, 2.0, 78), (9, 3.0, 2.0, 81),
    (10, 1.0, 2.0, 79), (10, 3.0, 2.0, 76),
    (11, 1.0, 4.0, 74),
    (12, 1.0, 3.0, 66), (12, 4.0, 1.0, 64),
    (13, 1.0, 4.0, 62),
]

# ----------------------------------------------------------------------------
# Shared builders
# ----------------------------------------------------------------------------


def conductor(title, bpm, bars, ts=(4, 2)):
    t = Track("%s CONDUCTOR" % title)
    tm, uspq = tempo_meta(bpm)
    t.add(0, Track.ORDER_META, tm)
    t.add(0, Track.ORDER_META, timesig_meta(ts[0], ts[1]))
    t.add(0, Track.ORDER_META, keysig_meta(2, 0))          # D major, 2 sharps
    t.add(0, Track.ORDER_META, text_meta(0x06, "LOOP_START"))
    t.add(0, Track.ORDER_META, text_meta(0x01, "loop start tick 0"))
    t.cc(0, 111, 0, channel=0)                              # the loop marker
    total = bars * BAR
    t.add(total, Track.ORDER_META, text_meta(0x06, "LOOP_END"))
    return t, uspq, total


def build_ostinato(track, chart, total_beats, total_ticks,
                   keep=None, vel_fn=None, gate=0.90, octave=0):
    """Lay the 13-beat ostinato continuously across the whole loop.

    `keep` optionally restricts which of the thirteen steps sound (this is how
    the night version thins the same figure without changing it). The final
    cycle is truncated by the loop point -- the spiral seam -- and every note
    is clamped so nothing sounds past it.
    """
    cyc = 0
    placed = []
    while cyc * OSTINATO_LEN < total_beats:
        base = cyc * OSTINATO_LEN
        for i, (off, deg, dur) in enumerate(OSTINATO_13):
            if keep is not None and i not in keep:
                continue
            b = base + off
            if b >= total_beats:
                continue
            ch = chord_at(chart, b)
            p = bass_tone(ch, deg) + 12 * octave
            v = vel_fn(cyc, i, b) if vel_fn else 84
            track.note(b * BEAT, p, v, dur * BEAT, gate=gate, limit=total_ticks)
            placed.append((b, p, deg))
        cyc += 1
    return placed


# ----------------------------------------------------------------------------
# HEARTHSCALE -- the hub
# ----------------------------------------------------------------------------


def build_hub(path):
    TITLE = "HEARTHSCALE"
    BARS = 34
    BPM = 110
    cond, uspq, TOTAL = conductor(TITLE, BPM, BARS)
    TOTAL_BEATS = BARS * 4

    lead = Track("HEARTHSCALE LEAD", channel=0, program=61)    # Brass Section
    gtr = Track("HEARTHSCALE COUNTER", channel=1, program=27)  # Clean guitar
    mar = Track("HEARTHSCALE TUNED PERC", channel=2, program=12)  # Marimba
    bas = Track("HEARTHSCALE BASS", channel=3, program=33)     # Finger bass
    prc = Track("HEARTHSCALE PERCUSSION", channel=9)           # GM drum map

    lead.cc(0, 111, 0)
    lead.cc(0, 7, 100)
    gtr.cc(0, 7, 84)
    mar.cc(0, 7, 76)
    bas.cc(0, 7, 96)
    prc.cc(0, 7, 88, channel=9)

    # -- lead ---------------------------------------------------------------
    phrase_starts = {1, 14, 22}
    for bar, beat, dur, pitch in HUB_MELODY:
        t = (bar - 1) * BAR + (beat - 1.0) * BEAT
        v = 92
        if bar in phrase_starts and beat <= 1.5:
            v = 106
        elif beat == 1.0:
            v = 100
        elif beat in (2.5, 3.5, 4.5):
            v = 84
        lead.note(t, pitch, v, dur * BEAT, gate=0.93, limit=TOTAL)

    # -- clean-guitar comping, cell chosen by the 13-long selector ----------
    for bar in range(1, BARS + 1):
        cell = GTR_CELLS[GTR_SEL13[(bar - 1) % 13]]
        ch = HUB_CHART[bar - 1]
        base = (bar - 1) * 4.0
        for k, (off, idx, dur) in enumerate(cell):
            b = base + off
            p = tone(ch, idx, 52, 76, 59)
            v = 74 if off == 0.0 else (62 if k % 2 else 68)
            gtr.note(b * BEAT, p, v, dur * BEAT, gate=0.88, limit=TOTAL)

    # -- marimba: 21-eighth cycle, seven strikes at Fibonacci indices -------
    cyc = 0
    while cyc * MARIMBA_LEN < TOTAL_BEATS * 2:
        base_e = cyc * MARIMBA_LEN
        for (ei, idx, de) in MARIMBA_21:
            e = base_e + ei
            b = e / 2.0
            if b >= TOTAL_BEATS:
                continue
            if b < 8.0:                    # two bars of air before it enters
                continue
            ch = chord_at(HUB_CHART, b)
            p = tone(ch, idx, 67, 91, 76)
            v = 70 if ei == 0 else 56
            mar.note(b * BEAT, p, v, (de / 2.0) * BEAT, gate=0.85, limit=TOTAL)
        cyc += 1

    # -- bass: the 13-beat ostinato ----------------------------------------
    def bvel(cyc_i, step_i, b):
        if step_i == 0:
            return 104
        if step_i in (4, 8, 10):
            return 92
        return 80

    build_ostinato(bas, HUB_CHART, TOTAL_BEATS, TOTAL, vel_fn=bvel, gate=0.90)

    # -- percussion: hands, not a kit. Air is composed, not left over. ------
    CONGA_LO, CONGA_MID, CONGA_HI = 64, 62, 63
    FRAME, SHAKER, TAMB, TRI = 41, 82, 54, 81
    groove8 = [(0.0, CONGA_LO, 100), (0.5, CONGA_MID, 58),
               (1.5, CONGA_HI, 84), (2.0, CONGA_LO, 70),
               (3.0, CONGA_MID, 66), (3.5, CONGA_HI, 90),
               (4.0, CONGA_LO, 104), (5.5, CONGA_HI, 80),
               (6.0, CONGA_MID, 62), (7.0, CONGA_LO, 76),
               (7.5, CONGA_HI, 94)]
    frame8 = [(0.0, FRAME, 62), (4.5, FRAME, 50)]

    c = 0
    while c * 8.0 < TOTAL_BEATS:
        base = c * 8.0
        for off, n, v in groove8:
            b = base + off
            if b >= TOTAL_BEATS:
                continue
            bar = int(b // 4) + 1
            if bar <= 4 and v < 90:
                continue                    # thin the first four bars
            if bar >= 33 and v < 70:
                continue                    # taper into the loop point
            prc.note(b * BEAT, n, v, 24, channel=9, gate=1.0, limit=TOTAL)
        for off, n, v in frame8:
            b = base + off
            if b >= TOTAL_BEATS:
                continue
            bar = int(b // 4) + 1
            if bar < 22:
                continue                    # the frame drum joins the return
            prc.note(b * BEAT, n, v, 48, channel=9, gate=1.0, limit=TOTAL)
        c += 1

    # shaker: offbeat eighths from the B-phrase on, with holes punched by a
    # 13-step index so the hi-hat-substitute never settles into a loop.
    e = 0
    while e / 2.0 < TOTAL_BEATS:
        b = e / 2.0
        if e % 2 == 1 and b >= 52.0:
            if (e % 13) not in (1, 4, 9):
                v = 40 if (e % 4 == 1) else 32
                prc.note(b * BEAT, SHAKER, v, 20, channel=9, gate=1.0,
                         limit=TOTAL)
        e += 1

    # tambourine: a FIVE-beat cycle against the 13 and the 4. 5 and 13 do not
    # realign inside the loop at all.
    c = 0
    while c * 5.0 < TOTAL_BEATS:
        for off in (0.0, 3.0):
            b = c * 5.0 + off
            if b >= TOTAL_BEATS or b < 84.0:
                continue
            prc.note(b * BEAT, TAMB, 48, 24, channel=9, gate=1.0, limit=TOTAL)
        c += 1

    # triangle: struck on beats 0, 52 and 104 -- the three moments inside the
    # loop where the 13-beat ostinato is back on a downbeat. This is what
    # makes the 13-cycle audible rather than merely asserted.
    for b in (0.0, 52.0, 104.0):
        prc.note(b * BEAT, TRI, 72, 240, channel=9, gate=1.0, limit=TOTAL)

    size = write_smf(path, [cond, lead, gtr, mar, bas, prc], TOTAL)
    return dict(path=path, bars=BARS, bpm=BPM, total=TOTAL, uspq=uspq,
                size=size, tracks=6)


# ----------------------------------------------------------------------------
# HEARTHSCALE, ASHLIGHT -- the night / rain variant
# ----------------------------------------------------------------------------


def thin_melody(mel, bars):
    """The night melody is the day melody thinned and stretched, not rewritten.

    Keep every note of a beat or longer, plus whatever begins a bar; then hold
    each survivor until the next one starts. Same contour, same leaps, half
    the events, none of the bustle.
    """
    abs_notes = []
    for bar, beat, dur, pitch in mel:
        t = (bar - 1) * 4.0 + (beat - 1.0)
        abs_notes.append((t, dur, pitch, beat))
    abs_notes.sort(key=lambda n: n[0])
    kept = [n for n in abs_notes if n[1] >= 1.0 or n[3] == 1.0]
    out = []
    for i, (t, dur, pitch, _beat) in enumerate(kept):
        if i + 1 < len(kept):
            span = kept[i + 1][0] - t
        else:
            span = bars * 4.0 - t
        out.append((t, max(dur, min(span, 4.0)), pitch))
    return out


def build_ashlight(path):
    TITLE = "HEARTHSCALE ASHLIGHT"
    BARS = 34
    BPM = 68                       # 110 / 68 = 1.617647 ~= phi (0.024% error)
    cond, uspq, TOTAL = conductor(TITLE, BPM, BARS)
    TOTAL_BEATS = BARS * 4

    fl = Track("ASHLIGHT LEAD", channel=0, program=73)     # Flute
    pad = Track("ASHLIGHT PAD", channel=1, program=89)     # Warm Pad
    cel = Track("ASHLIGHT RAIN", channel=2, program=8)     # Celesta
    bas = Track("ASHLIGHT BASS", channel=3, program=35)    # Fretless bass
    prc = Track("ASHLIGHT PERCUSSION", channel=9)

    fl.cc(0, 111, 0)
    fl.cc(0, 7, 88)
    fl.cc(0, 91, 110)              # reverb depth: the night is a wet room
    pad.cc(0, 7, 70)
    pad.cc(0, 91, 120)
    cel.cc(0, 7, 62)
    cel.cc(0, 91, 127)
    bas.cc(0, 7, 84)
    prc.cc(0, 7, 66, channel=9)
    prc.cc(0, 91, 100, channel=9)

    # -- flute: the same tune, thinned; long notes snapped to a chord tone --
    for t, dur, pitch in thin_melody(HUB_MELODY, BARS):
        ch = chord_at(ASH_CHART, t)
        p = pitch
        if dur >= 2.0:
            root, ivs, _b = CHORDS[ch]
            cand = [(root + iv) % 12 for iv in ivs]
            best = p
            bestd = 99
            for d in (0, -1, 1, -2, 2):
                if (p + d) % 12 in cand and abs(d) < bestd:
                    best, bestd = p + d, abs(d)
            p = best
        v = 76 if dur >= 2.0 else 68
        fl.note(t * BEAT, p, v, dur * BEAT, gate=0.90, limit=TOTAL)

    # -- pad: the harmonic bones, one sustained voicing per bar -------------
    for bar in range(1, BARS + 1):
        ch = ASH_CHART[bar - 1]
        t = (bar - 1) * BAR
        for k, idx in enumerate((0, 1, 2)):
            p = tone(ch, idx, 47, 71, 55 + 5 * k)
            pad.note(t, p, 54 + 4 * k, BAR, gate=0.985, limit=TOTAL)
        pad.note(t, bass_tone(ch, 0, 31, 47), 50, BAR, gate=0.985, limit=TOTAL)

    # -- celesta: rain off the eaves. A 13-EIGHTH cycle, struck at Fibonacci
    #    sub-indices 0, 3 and 8, so the drips fall in a pattern that does not
    #    repeat against the bar until bar 14.
    e = 0
    while e / 2.0 < TOTAL_BEATS:
        if (e % 13) in (0, 3, 8):
            b = e / 2.0
            bar = int(b // 4) + 1
            if 5 <= bar <= 13 or bar >= 22:
                ch = chord_at(ASH_CHART, b)
                idx = {0: 4, 3: 2, 8: 3}[e % 13] + (e // 26) % 2
                p = tone(ch, idx, 79, 100, 88)
                v = 52 if (e % 13) == 0 else 40
                cel.note(b * BEAT, p, v, BEAT * 0.5, gate=0.8, limit=TOTAL)
        e += 1

    # -- bass: the SAME 13-beat ostinato, five of its thirteen steps ---------
    build_ostinato(bas, ASH_CHART, TOTAL_BEATS, TOTAL,
                   keep={0, 3, 5, 8, 12},
                   vel_fn=lambda c, i, b: 78 if i == 0 else 62,
                   gate=0.95)

    # -- percussion: rainstick and brushed frame, no kit --------------------
    CABASA, MARACAS, SHAKER = 69, 70, 82
    SIDESTICK, FLOORTOM, TRI = 37, 41, 81
    e = 0
    while e / 2.0 < TOTAL_BEATS:
        b = e / 2.0
        bar = int(b // 4) + 1
        if bar >= 3 and (e % 13) not in (2, 6, 11):
            n = CABASA if (e % 2 == 0) else MARACAS
            v = 34 if (e % 4 == 0) else 24
            prc.note(b * BEAT, n, v, 16, channel=9, gate=1.0, limit=TOTAL)
        e += 1
    for bar in range(1, BARS + 1):
        t = (bar - 1) * BAR
        if bar % 2 == 1:
            prc.note(t, FLOORTOM, 52, 60, channel=9, gate=1.0, limit=TOTAL)
        if bar % 4 == 3:
            prc.note(t + 2 * BEAT, SIDESTICK, 40, 24, channel=9, gate=1.0,
                     limit=TOTAL)
        if bar >= 22 and bar % 2 == 0:
            prc.note(t + 3 * BEAT, SHAKER, 30, 20, channel=9, gate=1.0,
                     limit=TOTAL)
    for b in (0.0, 52.0, 104.0):
        prc.note(b * BEAT, TRI, 56, 360, channel=9, gate=1.0, limit=TOTAL)

    size = write_smf(path, [cond, fl, pad, cel, bas, prc], TOTAL)
    return dict(path=path, bars=BARS, bpm=BPM, total=TOTAL, uspq=uspq,
                size=size, tracks=6)


# ----------------------------------------------------------------------------
# COILREST -- the save / rest point
# ----------------------------------------------------------------------------


def build_rest(path):
    TITLE = "COILREST"
    BARS = 13
    BPM = 89                                   # F(11)
    cond, uspq, TOTAL = conductor(TITLE, BPM, BARS)
    TOTAL_BEATS = BARS * 4                     # 52 = lcm(13, 4)

    glk = Track("COILREST LEAD", channel=0, program=9)     # Glockenspiel
    ep = Track("COILREST CYCLE", channel=1, program=4)     # Electric Piano
    pad = Track("COILREST PAD", channel=2, program=89)     # Warm Pad
    bas = Track("COILREST BASS", channel=3, program=32)    # Acoustic Bass

    glk.cc(0, 111, 0)
    glk.cc(0, 7, 92)
    glk.cc(0, 91, 100)
    ep.cc(0, 7, 72)
    pad.cc(0, 7, 64)
    pad.cc(0, 91, 110)
    bas.cc(0, 7, 80)

    for bar, beat, dur, pitch in REST_GLOCK:
        t = (bar - 1) * BAR + (beat - 1.0) * BEAT
        v = 84 if beat == 1.0 else 72
        glk.note(t, pitch, v, dur * BEAT, gate=0.94, limit=TOTAL)

    # The hub's own 13-beat ostinato, transposed up two octaves onto the
    # electric piano, running exactly four times through the 52-beat loop and
    # closing on the last tick. This is the thread that ties the rest point
    # to the town.
    build_ostinato(ep, REST_CHART, TOTAL_BEATS, TOTAL,
                   vel_fn=lambda c, i, b: 66 if i == 0 else 52,
                   gate=0.92, octave=2)

    for bar in range(1, BARS + 1):
        ch = REST_CHART[bar - 1]
        t = (bar - 1) * BAR
        for k, idx in enumerate((0, 1, 2)):
            p = tone(ch, idx, 50, 72, 57 + 5 * k)
            pad.note(t, p, 48 + 3 * k, BAR, gate=0.985, limit=TOTAL)
        bas.note(t, bass_tone(ch, 0, 33, 50), 68, BAR, gate=0.96, limit=TOTAL)

    size = write_smf(path, [cond, glk, ep, pad, bas], TOTAL)
    return dict(path=path, bars=BARS, bpm=BPM, total=TOTAL, uspq=uspq,
                size=size, tracks=5)


# ----------------------------------------------------------------------------
# Verification -- re-parse the bytes we just wrote
# ----------------------------------------------------------------------------


def read_varint(buf, i):
    v = 0
    while True:
        b = buf[i]
        i += 1
        v = (v << 7) | (b & 0x7F)
        if not (b & 0x80):
            return v, i


def verify(path, expect_ticks, expect_tracks, expect_uspq, bpm):
    with open(path, "rb") as fh:
        data = fh.read()
    errs = []
    if data[:4] != b"MThd":
        errs.append("bad MThd magic")
    hlen, fmt, ntrks, div = struct.unpack(">IHHH", data[4:14])
    if hlen != 6:
        errs.append("MThd length %d != 6" % hlen)
    if fmt != 1:
        errs.append("format %d != 1" % fmt)
    if div != 480:
        errs.append("division %d != 480" % div)
    if ntrks != expect_tracks:
        errs.append("ntrks %d != %d" % (ntrks, expect_tracks))

    pos = 14
    stats = []
    saw_tempo = saw_ts = saw_cc111 = False
    tempo_val = None
    for tno in range(ntrks):
        if data[pos:pos + 4] != b"MTrk":
            errs.append("track %d: bad MTrk magic" % tno)
            break
        tlen = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + tlen]
        pos += 8 + tlen
        i = 0
        tick = 0
        running = None
        open_notes = {}
        n_on = n_off = 0
        progs = []
        ended = False
        last_meta = None
        while i < len(body):
            d, i = read_varint(body, i)
            tick += d
            st = body[i]
            if st < 0x80:
                if running is None:
                    errs.append("track %d: data byte with no running status" % tno)
                    break
                st = running
            else:
                i += 1
                if st < 0xF0:
                    running = st
            if st == 0xFF:
                mt = body[i]
                i += 1
                ln, i = read_varint(body, i)
                payload = body[i:i + ln]
                i += ln
                last_meta = mt
                if mt == 0x2F:
                    ended = True
                    if i != len(body):
                        errs.append("track %d: trailing bytes after FF 2F 00" % tno)
                    break
                if mt == 0x51:
                    saw_tempo = True
                    tempo_val = (payload[0] << 16) | (payload[1] << 8) | payload[2]
                if mt == 0x58:
                    saw_ts = True
                    if tuple(payload[:2]) != (4, 2):
                        errs.append("track %d: time sig not 4/4" % tno)
                continue
            if st in (0xF0, 0xF7):
                ln, i = read_varint(body, i)
                i += ln
                continue
            hi = st & 0xF0
            ch = st & 0x0F
            if hi in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                d0, d1 = body[i], body[i + 1]
                i += 2
                if hi == 0x90:
                    if d1 == 0:
                        errs.append("track %d: velocity-0 note-on at %d"
                                    % (tno, tick))
                    key = (ch, d0)
                    open_notes[key] = open_notes.get(key, 0) + 1
                    n_on += 1
                elif hi == 0x80:
                    key = (ch, d0)
                    if open_notes.get(key, 0) <= 0:
                        errs.append("track %d: note-off with no note-on "
                                    "(ch %d note %d) at %d" % (tno, ch, d0, tick))
                    else:
                        open_notes[key] -= 1
                    n_off += 1
                elif hi == 0xB0 and d0 == 111:
                    saw_cc111 = True
            elif hi in (0xC0, 0xD0):
                d0 = body[i]
                i += 1
                if hi == 0xC0:
                    progs.append(d0)
            else:
                errs.append("track %d: unknown status 0x%02X" % (tno, st))
                break
        if not ended:
            errs.append("track %d: does not end with FF 2F 00" % tno)
        dangling = sum(v for v in open_notes.values() if v > 0)
        if dangling:
            errs.append("track %d: %d dangling note-on(s)" % (tno, dangling))
        if tick != expect_ticks:
            errs.append("track %d: ends at tick %d, expected %d"
                        % (tno, tick, expect_ticks))
        if tno > 0 and n_on == 0:
            errs.append("track %d: empty (no notes)" % tno)
        stats.append((tno, n_on, n_off, progs, tick))
    if pos != len(data):
        errs.append("%d trailing bytes after last track" % (len(data) - pos))
    if not saw_tempo:
        errs.append("no FF 51 03 tempo meta")
    if not saw_ts:
        errs.append("no FF 58 04 time-signature meta")
    if not saw_cc111:
        errs.append("no CC#111 loop marker")
    if tempo_val != expect_uspq:
        errs.append("tempo %s != expected %s" % (tempo_val, expect_uspq))
    secs = expect_ticks / 480.0 * 60.0 / bpm
    return errs, stats, secs


def main():
    results = []
    results.append(("HEARTHSCALE / hub",
                    build_hub(os.path.join(OUT, "hearthscale_hub.mid"))))
    results.append(("HEARTHSCALE, ASHLIGHT / night",
                    build_ashlight(os.path.join(OUT, "hearthscale_ashlight.mid"))))
    results.append(("COILREST / rest",
                    build_rest(os.path.join(OUT, "coilrest_rest.mid"))))

    print("phi = %.6f   day/night tempo ratio = %.6f   error = %.4f%%"
          % (PHI, 110.0 / 68.0, abs(110.0 / 68.0 - PHI) / PHI * 100.0))
    print("")
    ok = True
    for label, info in results:
        errs, stats, secs = verify(info["path"], info["total"], info["tracks"],
                                   info["uspq"], info["bpm"])
        print("=== %s -> %s" % (label, os.path.basename(info["path"])))
        print("    %d bytes, %d tracks, %d bars, %d BPM, %d ticks, %.2f s"
              % (info["size"], info["tracks"], info["bars"], info["bpm"],
                 info["total"], secs))
        for tno, n_on, n_off, progs, tick in stats:
            print("    track %d: %3d note-on / %3d note-off  programs=%s  "
                  "end tick=%d" % (tno, n_on, n_off, progs, tick))
        if errs:
            ok = False
            for e in errs:
                print("    FAIL: %s" % e)
        else:
            print("    PASS: header ok, all tracks end FF 2F 00, "
                  "all note-ons matched, no track empty, length as intended")
        print("")
    print("RESULT: %s" % ("ALL PASS" if ok else "FAILURES PRESENT"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
