#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TOL VOVINA UPAAH LOT -- score generator, slice 4 of 4.

Produces four Standard MIDI Files (format 1, division 480) for three holds of the
hoard plus one variant:

    dragonmark_title.mid          -- the title / menu face of the world  (SEED)
    dragonmark_select.mid         -- the same 13 bars, stripped, for companion-select
    wingwide_open_sky.mid         -- THE WINGWIDE, the open-sky / dragon-flight hold
    chiglets_roost_companion.mid  -- CHIGLET'S ROOST, the AI companion's own space

Python 3 standard library only.  No pip, no external modules.  Deterministic:
the same script always emits byte-identical files, which is what
PROVENANCE/TVUL_MUSIC_FORMAT.md 7.2 requires of anything feeding midi2song.py.

---------------------------------------------------------------------------
ORIGINALITY
---------------------------------------------------------------------------
Every melody, bassline, chord progression and motif below was written for this
score.  Nothing is transcribed, quoted, paraphrased or reconstructed from any
existing work.  Style only is borrowed, and the influences are recorded here as
`ref:` comments and nowhere else -- never in a filename, a track title, or any
shipped string, per [[proprietary naming]]:

    ref: Mega Man Legends / Mega Man 64  -- melodic and harmonic bearing:
         bright mid-tempo hooks, clear melody over simple functional harmony,
         brass / organ / clean timbres, jaunty frontier warmth.
    ref: Stewart Copeland's Spyro scores -- percussion-forward, layered tuned and
         untuned hand percussion, ambient beds, odd accents, air left in.

---------------------------------------------------------------------------
THE SIGNATURE INTERVAL -- "the golden leap"
---------------------------------------------------------------------------
No composer-1 report existed when this slice was written, so the seed is defined
here and MUST propagate to the rest of the score.

phi = 1.6180339887...  As a *frequency ratio* that is 12 * log2(phi) = 8.3313
semitones.  The nearest equal-tempered interval is 8 semitones -- the ascending
minor sixth -- and 8 is itself F(6).  So the golden ratio, read as pitch, IS the
minor sixth.  That is the signature interval of TOL VOVINA UPAAH LOT.

It is never played bare.  It is always *arpeggiated through Fibonacci steps*:

        GOLDEN CELL:   x  ->  x+3  ->  x+8        (m3 then P4; 3 and 5 are F(4), F(5))

3, 5 and 8 are consecutive Fibonacci numbers, so the cell is a Fibonacci sum that
lands on the golden interval.  In a major key the cell is diatonic from exactly
three degrees -- 3^ (-> tonic), 6^ (-> subdominant), 7^ (-> dominant) -- which is
what makes it singable rather than chromatic.

The whole melodic vocabulary of this slice is restricted to Fibonacci intervals:
1 (semitone), 2 (whole tone), 3 (minor third), 5 (perfect fourth), 8 (minor
sixth), 13 (minor ninth).  A leap of 4, 6, 7, 9, 10 or 11 semitones is deliberately
rare.  This gives the score an audible accent of its own without being a quotation
of anything.

CONVERGENCE WITH SLICE 1, AND THE DELIBERATE DIVERGENCE FROM IT
---------------------------------------------------------------
gen_1_hearthscale.py reached the rising minor sixth independently
("Signature: a stepwise three-note climb, then a RISING MINOR SIXTH to the top
note"), in the same key, on the same two leaps: F#4->D5 and B4->G5.  Two composers
converging on the interval is exactly the identity the score wants, so DRAGONMARK
keeps D major and keeps those leaps.

What DRAGONMARK does NOT keep is slice 1's handling of them, because a title and a
town theme must not be the same tune:

    HEARTHSCALE  approaches the sixth by STEPWISE CLIMB and leaves it by
                 STEPWISE FALL      (62 64 66 -> 74 -> 73 71)
    DRAGONMARK   approaches the sixth by ARPEGGIATING the golden cell and leaves
                 it by DROPPING A FOURTH and STEPPING BACK UP
                                    (66 69 74 -> 69 -> 71)

Same interval, opposite gesture.  DRAGONMARK's rhythmic signature is also its own:
the peak lands on the AND OF TWO and is held across the beat, where the hub theme
peaks squarely on beat three.  The relationship is herald-to-home, not copy.

---------------------------------------------------------------------------
SPIRAL, NOT CIRCLE
---------------------------------------------------------------------------
Per TVUL_MUSIC_FORMAT.md 3: sections are Fibonacci-length, ostinati are 13-long,
and material returns transformed.  Recorded per piece in the ZXV MATH block above
each builder function.
"""

import os
import struct

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
DIV = 480                       # ticks per quarter note
E = DIV // 2                    # ticks per eighth note ("slot" unit throughout)

# ---------------------------------------------------------------------------
# Standard MIDI File primitives
# ---------------------------------------------------------------------------

def vlq(n):
    """Variable-length quantity, as SMF delta times require."""
    if n < 0:
        raise ValueError("negative delta")
    out = bytearray([n & 0x7F])
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)


# event ordering at an identical tick: metas first, then programme/CC, then
# note-offs, then note-ons.  Off-before-on at the same tick means a repeated
# pitch is retriggered rather than silently truncated.
ORD_META = 0
ORD_CTRL = 1
ORD_OFF = 2
ORD_ON = 3


class Track:
    def __init__(self, name):
        self.name = name
        self.ev = []            # (tick, order, seq, bytes)
        self._seq = 0
        self.meta(0, 0x03, name.encode("ascii"))

    def _add(self, tick, order, data):
        self.ev.append((tick, order, self._seq, bytes(data)))
        self._seq += 1

    def meta(self, tick, kind, payload):
        self._add(tick, ORD_META, bytes([0xFF, kind]) + vlq(len(payload)) + payload)

    def text(self, tick, s):
        self.meta(tick, 0x01, s.encode("ascii"))

    def marker(self, tick, s):
        self.meta(tick, 0x06, s.encode("ascii"))

    def tempo(self, tick, usec_per_quarter):
        self.meta(tick, 0x51, struct.pack(">I", usec_per_quarter)[1:])

    def timesig(self, tick, num, den_pow2, cc=24, bb=8):
        self.meta(tick, 0x58, bytes([num, den_pow2, cc, bb]))

    def keysig(self, tick, sharps, minor=0):
        self.meta(tick, 0x59, bytes([sharps & 0xFF, minor]))

    def program(self, tick, chan, prog):
        self._add(tick, ORD_CTRL, [0xC0 | chan, prog])

    def cc(self, tick, chan, num, val):
        self._add(tick, ORD_CTRL, [0xB0 | chan, num, val])

    def note(self, tick, chan, pitch, dur, vel):
        if dur <= 0:
            raise ValueError("zero-length note")
        if not (0 <= pitch <= 127):
            raise ValueError("pitch out of range: %d" % pitch)
        vel = max(1, min(127, int(vel)))
        self._add(tick, ORD_ON, [0x90 | chan, pitch, vel])
        self._add(tick + dur, ORD_OFF, [0x80 | chan, pitch, 64])

    def serialise(self):
        self.ev.sort(key=lambda e: (e[0], e[1], e[2]))
        end = self.ev[-1][0] if self.ev else 0
        body = bytearray()
        prev = 0
        for tick, _o, _s, data in self.ev:
            body += vlq(tick - prev)
            body += data
            prev = tick
        body += vlq(max(0, end - prev)) + b"\xFF\x2F\x00"
        return b"MTrk" + struct.pack(">I", len(body)) + bytes(body)

    def last_tick(self):
        return max((e[0] for e in self.ev), default=0)


def write_smf(path, tracks):
    hdr = b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), DIV)
    with open(path, "wb") as f:
        f.write(hdr)
        for t in tracks:
            f.write(t.serialise())
    return path


# ---------------------------------------------------------------------------
# bar grid helper (handles mixed meter)
# ---------------------------------------------------------------------------

class Grid:
    """meters is a list of (numerator, denominator) per bar, 1-indexed access."""

    def __init__(self, meters):
        self.meters = meters
        self.starts = [0]
        for num, den in meters:
            self.starts.append(self.starts[-1] + num * (4 * DIV // den))
        self.total = self.starts[-1]

    def bar(self, n):                       # 1-indexed bar start tick
        return self.starts[n - 1]

    def slots(self, n):                     # eighth-note slots in bar n
        num, den = self.meters[n - 1]
        return num * (4 * DIV // den) // E

    def at(self, n, slot):
        return self.bar(n) + slot * E


def lay(track, grid, chan, data, vel_scale=1.0, transpose=0):
    """data: {bar: [(slot, pitch, dur_in_eighths, vel), ...]}"""
    for bar, notes in data.items():
        for slot, pitch, dur, vel in notes:
            track.note(grid.at(bar, slot), chan, pitch + transpose,
                       dur * E, vel * vel_scale)


# ===========================================================================
# PIECE 1 + 2 -- DRAGONMARK  (title, and the stripped companion-select variant)
# ===========================================================================
#
# ZXV MATH
# --------
#   phi:  the loop is 13 bars, divided 8 + 5.  The 8-bar hook divides 5 + 3.
#         Every division is a Fibonacci pair, so the form is self-similar at
#         three scales (13 = 8+5, 8 = 5+3, 5 = 3+2 in the melodic cells).
#   13:   the bass ostinato is 13 QUARTER BEATS long (26 eighths).  Under 4/4 it
#         realigns with the barline after lcm(13,4) = 52 beats = exactly 13 bars.
#         The loop is 13 bars.  So the loop length IS the realignment length: the
#         ostinato plays exactly four cycles per pass, each starting on a
#         different beat of the bar (bar 1 beat 1, bar 4 beat 2, bar 7 beat 3,
#         bar 10 beat 4), and arrives home precisely at the loop seam.  The
#         tambourine marks each cycle head, so the drift is audible, not
#         theoretical.
#   spiral: bars 9-13 are the hook returning TRANSFORMED -- the golden cell is
#         contour-INVERTED (x, x-3, x-8 instead of x, x+3, x+8), reharmonised
#         from D major onto its relative minor (Bm-Em-G-A7-D), and reorchestrated
#         (trumpet drops out, brass section takes it).  Not one bar is copied.
#
# Key D major.  120 BPM so the GLINTFALL stinger (also 120) can fire over it.

TITLE_BPM = 120
TITLE_BARS = 13
TITLE_CHORDS = ["D", "Bm", "G", "Em", "A", "Bm", "G", "D",
                "Bm", "Em", "G", "A7", "D"]

# bass-register chord tones, indexed 0=root 1=third 2=fifth 3=octave(or 7th)
BASS_TONES = {
    "D":  [38, 42, 45, 50],
    "Bm": [35, 38, 42, 47],
    "G":  [31, 35, 38, 43],
    "A":  [33, 37, 40, 45],
    "A7": [33, 37, 40, 43],     # the 7th (G) replaces the octave -- dominant colour
    "Em": [40, 43, 47, 52],
}

# organ pad voicings, mid register
PAD_VOICE = {
    "D":  [50, 54, 57],
    "Bm": [47, 50, 54],
    "G":  [43, 47, 50],
    "A":  [45, 49, 52],
    "A7": [45, 49, 52, 55],
    "Em": [52, 55, 59],
}

# THE OSTINATO.  13 quarter beats == 26 eighth slots.
# (slot, chord-tone index, duration in eighths).  Rests at 6, 15, 22 keep air in.
OSTINATO = [
    (0, 0, 2), (2, 0, 1), (3, 2, 1), (4, 3, 2),
    (7, 1, 1), (8, 0, 3), (11, 2, 1), (12, 0, 2),
    (14, 3, 1), (16, 0, 2), (18, 1, 1), (19, 2, 3),
    (23, 0, 1), (24, 0, 2),
]
OSTINATO_LEN = 26               # eighths

# --- THE HOOK.
# Every odd bar opens with the GOLDEN CELL -- x, x+3, x+8 -- arpeggiating the
# signature minor sixth, peak landing on the AND OF TWO and held across the beat.
# Every peak then leaves by the FLAG GESTURE: drop a fourth (5), step back up (2).
# Even bars answer by restating the peak squarely on beat 1 and walking it home.
#   bar 1  F#4 A4 D5   -> A4 B4        (3^ -> 1^)
#   bar 3  B4  D5 G5   -> D5 E5        (6^ -> 4^)
#   bar 5  C#5 E5 A5   -> E5 F#5       (7^ -> 5^)   -- all three diatonic sixths
TITLE_LEAD = {
    1:  [(0, 66, 1, 100), (1, 69, 1, 98), (2, 74, 3, 112), (5, 69, 1, 94), (6, 71, 2, 100)],
    # even bars restate the peak on beat 1, then DOUBLE the flag gesture on the
    # way down (-5 +2, -5 -2).  Deliberately not a stepwise walk: a stepwise
    # descent from the peak is slice 1's hub gesture, and these two themes must
    # not converge on the same six notes in the same key.
    2:  [(0, 74, 2, 104), (2, 69, 1, 94), (3, 71, 1, 94), (4, 66, 3, 98), (7, 64, 1, 88)],
    3:  [(0, 71, 1, 102), (1, 74, 1, 100), (2, 79, 3, 114), (5, 74, 1, 96), (6, 76, 2, 102)],
    4:  [(0, 79, 2, 106), (2, 74, 1, 96), (3, 76, 1, 96), (4, 71, 3, 100), (7, 74, 1, 92)],
    5:  [(0, 73, 1, 106), (1, 76, 1, 104), (2, 81, 4, 120), (6, 76, 1, 100), (7, 78, 1, 102)],
    6:  [(0, 81, 2, 112), (2, 76, 1, 100), (3, 78, 1, 98), (4, 73, 3, 102), (7, 71, 1, 92)],
    7:  [(0, 69, 2, 100), (2, 74, 1, 98), (3, 71, 1, 94), (4, 69, 2, 96), (6, 66, 2, 92)],
    8:  [(0, 66, 1, 98), (1, 69, 1, 98), (2, 74, 4, 108)],
    # 9-11 tacet: the brass section carries the inverted return
    12: [(0, 73, 2, 106), (2, 76, 2, 108), (4, 81, 4, 120)],   # golden cell in augmentation
    13: [(0, 76, 2, 104), (2, 74, 4, 108)],                    # flag gesture: -5 then -2
}

# brass section: fanfare stabs, then the INVERTED golden cell in bars 9-11.
# Inversion is total: the cell descends (x, x-3, x-8) and the flag gesture flips
# with it -- rise a fourth, step back DOWN.
TITLE_BRASS = {
    1:  [(0, 62, 1, 108), (0, 66, 1, 106), (0, 69, 1, 106)],
    3:  [(0, 55, 1, 104), (0, 59, 1, 102), (0, 62, 1, 102)],
    5:  [(0, 57, 1, 106), (0, 61, 1, 104), (0, 64, 1, 104)],
    8:  [(6, 59, 1, 92), (6, 62, 1, 90), (7, 62, 1, 94), (7, 66, 1, 92)],
    9:  [(0, 74, 1, 96), (1, 71, 1, 94), (2, 66, 3, 100), (5, 71, 1, 90), (6, 69, 2, 94)],
    10: [(0, 79, 1, 98), (1, 76, 1, 96), (2, 71, 3, 100), (5, 76, 1, 92), (6, 74, 2, 96)],
    11: [(0, 71, 3, 98), (3, 69, 1, 92), (4, 67, 1, 92), (5, 66, 3, 96)],
    12: [(0, 61, 8, 84), (0, 64, 8, 82), (0, 67, 8, 80)],
    13: [(0, 62, 4, 88), (0, 66, 4, 86), (0, 69, 4, 86)],
}


def build_dragonmark(select_variant=False):
    grid = Grid([(4, 4)] * TITLE_BARS)
    name = "DRAGONMARK (select)" if select_variant else "DRAGONMARK"

    t0 = Track("conductor")
    t0.tempo(0, int(round(60_000_000 / TITLE_BPM)))
    t0.timesig(0, 4, 2, 24, 8)
    t0.keysig(0, 2, 0)                                  # D major
    t0.text(0, name)
    t0.text(0, "LOOP_START")
    t0.marker(0, "loopStart")
    t0.cc(0, 0, 111, 0)                                 # CC#111 loop point
    t0.text(grid.total, "LOOP_END")
    t0.marker(grid.total, "loopEnd")

    tracks = [t0]

    if not select_variant:
        lead = Track("lead trumpet")
        lead.program(0, 0, 56)                          # GM 56 Trumpet
        lead.cc(0, 0, 7, 108)
        lead.cc(0, 0, 10, 64)
        lay(lead, grid, 0, TITLE_LEAD)

        brass = Track("brass section")
        brass.program(0, 1, 61)                         # GM 61 Brass Section
        brass.cc(0, 1, 7, 92)
        brass.cc(0, 1, 10, 46)
        lay(brass, grid, 1, TITLE_BRASS)

        organ = Track("church organ")
        organ.program(0, 2, 19)                         # GM 19 Church Organ
        organ.cc(0, 2, 7, 74)
        organ.cc(0, 2, 10, 82)
        for b in range(1, TITLE_BARS + 1):
            dur = 8 if b < TITLE_BARS else 5
            for p in PAD_VOICE[TITLE_CHORDS[b - 1]]:
                organ.note(grid.at(b, 0), 2, p, dur * E, 66)
        tracks += [lead, brass, organ]
    else:
        # the same 13 bars, stripped to one marimba voice and a whisper of pad:
        # only the golden cells survive, in augmentation, with the air left in.
        mar = Track("marimba")
        mar.program(0, 0, 12)                           # GM 12 Marimba
        mar.cc(0, 0, 7, 88)
        mar.cc(0, 0, 10, 64)
        sel = {
            1:  [(0, 66, 2, 84), (2, 69, 2, 84), (4, 74, 4, 92)],
            3:  [(0, 71, 2, 84), (2, 74, 2, 84), (4, 79, 4, 92)],
            5:  [(0, 73, 2, 86), (2, 76, 2, 86), (4, 81, 4, 94)],
            7:  [(0, 69, 4, 80), (4, 74, 2, 76), (6, 71, 2, 74)],   # flag gesture bare
            8:  [(0, 74, 4, 82)],
            9:  [(0, 74, 2, 80), (2, 71, 2, 80), (4, 66, 4, 86)],   # inverted cell
            11: [(0, 71, 4, 78), (4, 66, 4, 74)],
            12: [(0, 73, 2, 82), (2, 76, 2, 82), (4, 81, 4, 90)],
            13: [(0, 76, 2, 78), (2, 74, 4, 80)],
        }
        lay(mar, grid, 0, sel)

        pad = Track("warm pad")
        pad.program(0, 2, 89)                           # GM 89 Pad 2 (warm)
        pad.cc(0, 2, 7, 52)
        pad.cc(0, 2, 10, 70)
        for b in range(1, TITLE_BARS + 1):
            dur = 8 if b < TITLE_BARS else 5
            for p in PAD_VOICE[TITLE_CHORDS[b - 1]][:3]:
                pad.note(grid.at(b, 0), 2, p - 12, dur * E, 44)
        tracks += [mar, pad]

    # ---- bass: the 13-beat ostinato, four cycles, ending exactly at the seam
    bass = Track("bass ostinato 13")
    bass.program(0, 3, 33)                              # GM 33 Electric Bass (finger)
    bass.cc(0, 3, 7, 96 if not select_variant else 76)
    bass.cc(0, 3, 10, 64)
    total_eighths = TITLE_BARS * 8                      # 104
    cycles = total_eighths // OSTINATO_LEN              # 4, exactly
    bvel = 1.0 if not select_variant else 0.72
    for c in range(cycles):
        base = c * OSTINATO_LEN
        for slot, tone, dur in OSTINATO:
            pos = base + slot
            chord = TITLE_CHORDS[pos // 8]
            pitch = BASS_TONES[chord][tone]
            # accent the head of each 13-beat cycle
            vel = 100 if slot == 0 else (86 if tone == 0 else 78)
            bass.note(pos * E, 3, pitch, dur * E, vel * bvel)
    tracks.append(bass)

    # ---- percussion (channel 10 == index 9)
    perc = Track("percussion")
    perc.cc(0, 9, 7, 100 if not select_variant else 78)
    pv = 1.0 if not select_variant else 0.66
    for b in range(1, TITLE_BARS + 1):
        t = lambda s: grid.at(b, s)
        for s in (0, 2, 4, 6):
            perc.note(t(s), 9, 82, E, 52 * pv)          # shaker
        for s in (3, 7):
            perc.note(t(s), 9, 82, E, 36 * pv)
        if b in (5, 6, 7, 8, 12, 13):
            for s in (1, 5):
                perc.note(t(s), 9, 69, E, 40 * pv)      # cabasa lift
        if not select_variant:
            perc.note(t(0), 9, 36, E, 100)              # kick
            perc.note(t(5), 9, 36, E, 76)               # push on the and-of-3
            perc.note(t(4), 9, 64, E, 82)               # low conga on 3
        else:
            perc.note(t(0), 9, 64, E, 62)
    # tambourine on each 13-beat ostinato head -- the drift made audible
    for c in range(cycles):
        perc.note(c * OSTINATO_LEN * E, 9, 54, E, 104 * pv)
    if not select_variant:
        perc.note(grid.at(1, 0), 9, 49, 4 * E, 108)     # crash
        perc.note(grid.at(9, 0), 9, 49, 4 * E, 96)
        for i, (s, n) in enumerate(((6, 47), (7, 45))):
            perc.note(grid.at(8, s), 9, n, E, 92 - i * 4)
        for i, (s, n) in enumerate(((4, 47), (5, 45), (6, 43), (7, 41))):
            perc.note(grid.at(13, s), 9, n, E, 88 + i * 4)
    else:
        perc.note(grid.at(13, 6), 9, 76, E, 58)
    tracks.append(perc)

    fn = "dragonmark_select.mid" if select_variant else "dragonmark_title.mid"
    return write_smf(os.path.join(OUT_DIR, fn), tracks), grid.total


# ===========================================================================
# PIECE 3 -- THE WINGWIDE  (open sky / high places)
# ===========================================================================
#
# ZXV MATH
# --------
#   phi:  loop = 55 bars, divided 21 + 13 + 21.  The first 21 divides 13 + 8
#         (a 13-bar phrase answered by an 8-bar consequent); the closing 21
#         divides 13 + 8 the same way.  55, 21, 13, 8 are all Fibonacci, and the
#         13-bar phrase length means the melody never cadences where a 12- or
#         16-bar ear expects it -- that is what makes the piece feel open-ended.
#   13:   an open triangle strikes every 13 EIGHTHS against 6/8 (6 eighths per
#         bar).  It therefore lands on a different eighth of the bar each time
#         and its bar-position pattern only repeats after lcm(13,6) = 78 eighths
#         = 13 bars.  Across the 55-bar loop that is 4 full traversals plus 3
#         bars -- so the bell's relationship to the phrase is different in every
#         section.  25 whole periods are laid (325 eighths); the remaining 5
#         eighths of bar 55 are left as air so nothing crosses the loop seam.
#   spiral: section III is section I returning transformed, three ways at once --
#         the flute takes the phrase an OCTAVE UP, the horn plays the original
#         register in CONTRARY MOTION beneath it (rising while the flute falls,
#         crossing at bar 52), and the harmony is tilted onto the relative minor
#         (F#m-D-Bm-E) before resolving back to A.  Section II strips the horn
#         out entirely -- the same material at altitude, with nothing under it.
#
#   Range: horn A2 (45) to flute E6 (88) = 43 semitones of melodic range, the
#   widest in the set.  With the contrabass at A1 (33) the full texture spans
#   33..88 = 55 semitones, which is F(10) -- a coincidence, but a welcome one.
#
# Key A major with a mixolydian G natural (the bVII), which is what supplies the
# "open and cold" colour INTERSPACE is specified to have.

SKY_BPM_DOTTED = 92                                     # dotted quarter
SKY_INTRO = 5
SKY_LOOP = 55
SKY_BARS = SKY_INTRO + SKY_LOOP                         # 60

SKY_CHORDS = (
    ["A", "A", "D", "E", "E"] +                                            # intro 5
    # --- section I : 21 bars = 13-bar phrase + 8-bar answer
    ["A", "A", "E", "E", "D", "D", "G", "G", "A", "F#m", "D", "E", "A"] +
    ["D", "E", "F#m", "D", "Bm", "E", "A", "A"] +
    # --- section II : 13 bars, the high plateau
    ["D", "A", "G", "D", "A", "E", "F#m", "D", "G", "A", "E", "E", "A"] +
    # --- section III : 21 bars = the 13-bar phrase transformed + 8-bar descent
    ["F#m", "D", "Bm", "E", "F#m", "D", "A", "E", "D", "Bm", "E", "E", "A"] +
    ["A", "G", "D", "A", "E", "D", "A", "E"]
)

SKY_PAD = {                                             # wide voicings: root, 5th, 3rd+8ve
    "A":   [57, 64, 73],
    "E":   [52, 59, 68],
    "D":   [50, 57, 66],
    "G":   [55, 62, 71],
    "F#m": [54, 61, 69],
    "Bm":  [47, 54, 62],
}
SKY_BASS_ROOT = {"A": 33, "E": 40, "D": 38, "G": 43, "F#m": 42, "Bm": 35}

SKY_FLUTE = {
    13: [(0, 81, 6, 88)],
    14: [(0, 79, 3, 84), (3, 76, 3, 84)],
    15: [(0, 78, 6, 86)],
    16: [(0, 74, 3, 82), (3, 71, 3, 82)],
    17: [(0, 73, 9, 84)],
    19: [(0, 76, 1, 80), (1, 78, 1, 82), (2, 81, 4, 92)],
    20: [(0, 83, 3, 94), (3, 81, 3, 90)],
    21: [(0, 78, 6, 88)],
    22: [(0, 76, 1, 84), (1, 74, 1, 84), (2, 71, 4, 86)],
    23: [(0, 73, 3, 84), (3, 76, 3, 86)],
    24: [(0, 78, 9, 90)],
    25: [(3, 81, 3, 92)],
    26: [(0, 76, 4, 84)],
    # --- II : the plateau, flute alone over pad
    27: [(0, 86, 3, 92), (3, 88, 3, 96)],
    28: [(0, 85, 6, 90)],
    29: [(0, 81, 3, 86), (3, 83, 3, 88)],
    30: [(0, 78, 9, 86)],
    31: [(3, 76, 3, 84)],
    32: [(0, 74, 3, 82), (3, 79, 3, 86)],
    33: [(0, 81, 6, 88)],
    34: [(0, 83, 3, 90), (3, 86, 3, 92)],
    35: [(0, 88, 6, 98)],
    36: [(0, 85, 3, 90), (3, 81, 3, 86)],
    37: [(0, 78, 6, 84)],
    38: [(0, 76, 9, 82)],
    # --- III : phrase A an octave up, then falling against the rising horn
    40: [(0, 69, 3, 84), (3, 76, 9, 88)],
    42: [(0, 78, 3, 88), (3, 81, 3, 90)],
    43: [(0, 88, 6, 96)],
    44: [(0, 86, 3, 92), (3, 83, 3, 90)],
    45: [(0, 81, 9, 88)],
    46: [(3, 78, 3, 86)],
    47: [(0, 81, 6, 88)],
    48: [(0, 79, 3, 86), (3, 76, 3, 84)],
    49: [(0, 74, 6, 82)],
    50: [(0, 71, 3, 80), (3, 69, 3, 80)],
    51: [(0, 66, 9, 78)],
    53: [(0, 81, 3, 76), (3, 78, 3, 74)],
    54: [(0, 76, 6, 74)],
    55: [(0, 74, 3, 72), (3, 71, 3, 70)],
    56: [(0, 69, 6, 68)],
}

SKY_HORN = {
    4:  [(0, 45, 5, 52)],
    5:  [(0, 50, 3, 58), (3, 52, 3, 62)],
    # --- I : the 13-bar phrase, stated low.  Bar 6 opens A3 -> E4, a perfect
    #         fifth; bar 8-9 F#4 -> A4 -> E5 opens the register out.
    6:  [(0, 57, 3, 80), (3, 64, 9, 84)],
    8:  [(0, 66, 3, 82), (3, 69, 3, 84)],
    9:  [(0, 76, 6, 88)],
    10: [(0, 74, 3, 84), (3, 71, 3, 82)],
    11: [(0, 69, 9, 84)],
    12: [(3, 66, 3, 80)],
    13: [(0, 57, 5, 66)], 14: [(0, 55, 5, 64)], 15: [(0, 54, 5, 64)],
    16: [(0, 50, 5, 62)], 17: [(0, 52, 5, 62)], 18: [(0, 57, 5, 64)],
    19: [(0, 50, 5, 64)], 20: [(0, 52, 5, 64)], 21: [(0, 54, 5, 64)],
    22: [(0, 50, 5, 62)], 23: [(0, 47, 5, 62)], 24: [(0, 52, 5, 64)],
    25: [(0, 57, 5, 66)], 26: [(0, 57, 5, 64)],
    # 27-39 tacet -- the plateau has nothing underneath it
    # --- III : contrary motion, rising through the falling flute
    40: [(0, 50, 5, 70)], 41: [(0, 52, 5, 70)], 42: [(0, 54, 5, 72)],
    43: [(0, 57, 5, 74)], 44: [(0, 59, 5, 76)], 45: [(0, 61, 5, 78)],
    46: [(0, 62, 5, 80)], 47: [(0, 64, 5, 82)], 48: [(0, 66, 5, 84)],
    49: [(0, 69, 5, 86)], 50: [(0, 71, 5, 88)], 51: [(0, 73, 5, 90)],
    52: [(0, 76, 5, 92)],
    53: [(0, 64, 5, 80)], 54: [(0, 62, 5, 78)], 55: [(0, 59, 5, 76)],
    56: [(0, 57, 5, 74)], 57: [(0, 61, 5, 72)], 58: [(0, 59, 5, 70)],
    59: [(0, 57, 5, 68)], 60: [(0, 52, 3, 64)],
}


def build_wingwide():
    grid = Grid([(6, 8)] * SKY_BARS)
    loop_tick = grid.bar(SKY_INTRO + 1)

    t0 = Track("conductor")
    t0.tempo(0, int(round(60_000_000 / (SKY_BPM_DOTTED * 1.5))))
    t0.timesig(0, 6, 3, 36, 8)
    t0.keysig(0, 3, 0)                                  # A major
    t0.text(0, "THE WINGWIDE")
    t0.text(loop_tick, "LOOP_START")
    t0.marker(loop_tick, "loopStart")
    t0.cc(loop_tick, 0, 111, 0)
    t0.text(grid.total, "LOOP_END")
    t0.marker(grid.total, "loopEnd")

    flute = Track("flute lead")
    flute.program(0, 0, 73)                             # GM 73 Flute
    flute.cc(0, 0, 7, 104)
    flute.cc(0, 0, 10, 58)
    lay(flute, grid, 0, SKY_FLUTE)

    horn = Track("french horn")
    horn.program(0, 1, 60)                              # GM 60 French Horn
    horn.cc(0, 1, 7, 92)
    horn.cc(0, 1, 10, 76)
    lay(horn, grid, 1, SKY_HORN)

    pad = Track("string pad")
    pad.program(0, 2, 48)                               # GM 48 String Ensemble 1
    pad.cc(0, 2, 7, 70)
    pad.cc(0, 2, 10, 64)
    for b in range(1, SKY_BARS + 1):
        ch = SKY_CHORDS[b - 1]
        # intro swells in; last bar releases early so nothing crosses the seam
        vel = 34 + min(26, (b - 1) * 6) if b <= SKY_INTRO else 58
        dur = 6 if b < SKY_BARS else 4
        for i, p in enumerate(SKY_PAD[ch]):
            pad.note(grid.at(b, 0), 2, p, dur * E, vel - i * 3)

    bass = Track("contrabass")
    bass.program(0, 3, 43)                              # GM 43 Contrabass
    bass.cc(0, 3, 7, 84)
    bass.cc(0, 3, 10, 64)
    for b in range(4, SKY_BARS + 1):
        ch = SKY_CHORDS[b - 1]
        r = SKY_BASS_ROOT[ch]
        i = b - (SKY_INTRO + 1)                         # 0-based inside the loop
        if 27 <= b <= 39 and b % 2 == 0:
            continue                                    # plateau: thin the floor out
        if i >= 0 and i % 8 == 7:
            continue                                    # a bar of air every eighth bar
        bass.note(grid.at(b, 0), 3, r, 3 * E, 74)
        if i >= 0 and i % 5 == 3:
            bass.note(grid.at(b, 3), 3, r + 7, 2 * E, 62)

    perc = Track("percussion")
    perc.cc(0, 9, 7, 88)
    # soft dotted-quarter pulse, dropped every fourth bar so the air stays in
    for b in range(4, SKY_BARS + 1):
        i = b - (SKY_INTRO + 1)
        if i >= 0 and i % 4 == 3:
            continue
        if 27 <= b <= 39:
            if b % 2 == 0:
                continue
            perc.note(grid.at(b, 0), 9, 69, E, 26)
            continue
        perc.note(grid.at(b, 0), 9, 69, E, 34)
        perc.note(grid.at(b, 3), 9, 69, E, 26)
        if 40 <= b <= 52:
            perc.note(grid.at(b, 3), 9, 64, E, 30)      # low conga on the return
        if 19 <= b <= 26:
            perc.note(grid.at(b, 2), 9, 82, E, 28)      # shaker in the consequent
            perc.note(grid.at(b, 5), 9, 82, E, 24)
    for b in (1, 6, 19, 27, 40, 53):                    # section markers
        perc.note(grid.at(b, 0), 9, 41, 2 * E, 66)
    # --- the 13-eighth triangle, 25 whole periods from the loop point
    for k in range(26):
        tick = loop_tick + k * 13 * E
        perc.note(tick, 9, 81, E, 58 if k % 2 == 0 else 46)

    tracks = [t0, flute, horn, pad, bass, perc]
    return write_smf(os.path.join(OUT_DIR, "wingwide_open_sky.mid"), tracks), grid.total, loop_tick


# ===========================================================================
# PIECE 4 -- CHIGLET'S ROOST  (the AI companion's own space)
# ===========================================================================
#
# ZXV MATH
# --------
#   phi:  the loop is 34 bars, divided 5 + 8 + 13 + 8.  Those are four
#         consecutive Fibonacci lengths and the first three ASCEND -- the
#         companion's utterances literally get longer as it learns to finish your
#         sentences.  The closing 8 is the duet, where it stops growing and
#         interlocks with you instead.
#   13:   claves strike every 13 EIGHTHS from the loop point.  Because the METER
#         itself shifts underneath (bars of 3/4 and 5/4 are dropped in where a
#         phrase finishes early), the claves never land on the same beat of the
#         same bar twice inside the loop.  20 whole periods (260 eighths) are laid
#         inside a 270-eighth loop, leaving 10 eighths of air at the seam.
#   spiral: phase 3 is phases 1 and 2 returning with the ROLES SWAPPED -- the
#         acoustic bass takes the melody two octaves down while the pizzicato,
#         which had the tune, is demoted to sparse high answers.  Phase 4 states
#         the golden cell in AUGMENTATION (halves instead of eighths) with the
#         two voices in complementary rhythm: pizz on the downbeats, bass filling
#         the gaps, neither ever doubling the other.  That is the Concord
#         complementary-pairing principle written as counterpoint.
#
#   Warmth is the brief, so: F major throughout, no minor cadence anywhere, no
#   diminished chord, plucked and struck timbres only, nothing sustained or
#   breathy that could read as surveillance.
#
# Meter map: 4/4 except bar 8 (3/4), 16 (5/4), 24 (3/4), 29 (5/4), 37 (3/4).

ROOST_BPM = 104
ROOST_METERS = ([(4, 4)] * 3 +                                   # 1-3   intro
                [(4, 4)] * 4 + [(3, 4)] +                        # 4-8   phase 1 (5)
                [(4, 4)] * 7 + [(5, 4)] +                        # 9-16  phase 2 (8)
                [(4, 4)] * 7 + [(3, 4)] + [(4, 4)] * 4 + [(5, 4)] +   # 17-29 phase 3 (13)
                [(4, 4)] * 7 + [(3, 4)])                         # 30-37 phase 4 (8)

ROOST_CHORDS = (["F", "Bb", "C"] +
                ["F", "Dm", "F", "Bb", "C"] +
                ["C", "Am", "Bb", "F", "Dm", "Gm", "C", "C"] +
                ["F", "Dm", "F", "Bb", "C", "Am", "Bb", "Gm", "F", "Dm", "C", "Bb", "C"] +
                ["F", "Dm", "Bb", "F", "F", "Am", "Bb", "C"])

ROOST_MAR = {"F": [53, 60], "Dm": [50, 57], "Bb": [46, 53],
             "C": [48, 55], "Am": [45, 52], "Gm": [43, 50]}

# phase 1+2 melody.  Bar 6 slot 0 is the golden cell in F: 57 -> 60 -> 65
# (A3 +3 +5 = F4) -- the same shape that opens DRAGONMARK, in the companion's key.
ROOST_PIZZ = {
    3:  [(6, 60, 1, 70), (7, 62, 1, 72)],
    4:  [(0, 65, 1, 88), (1, 69, 1, 84), (2, 65, 1, 80), (4, 72, 2, 90), (7, 70, 1, 78)],
    5:  [(0, 69, 3, 86), (3, 67, 1, 78), (4, 65, 2, 82), (7, 64, 1, 74)],
    6:  [(0, 57, 1, 82), (1, 60, 1, 84), (2, 65, 4, 92), (7, 67, 1, 78)],
    7:  [(0, 69, 2, 88), (2, 72, 2, 90), (4, 70, 1, 80), (5, 69, 3, 84)],
    8:  [(0, 67, 2, 82), (2, 65, 4, 86)],
    9:  [(0, 64, 1, 84), (1, 67, 1, 86), (2, 72, 3, 94), (5, 70, 1, 82), (6, 69, 2, 86)],
    10: [(0, 67, 3, 84), (3, 69, 1, 80), (4, 67, 1, 80), (5, 64, 3, 82)],
    11: [(0, 62, 1, 84), (1, 65, 1, 86), (2, 70, 3, 92), (5, 69, 1, 82), (6, 67, 2, 84)],
    12: [(0, 65, 3, 84), (3, 64, 1, 78), (4, 62, 1, 78), (5, 60, 3, 80)],
    13: [(0, 69, 2, 86), (2, 72, 2, 88), (4, 74, 4, 94)],
    14: [(0, 72, 1, 86), (1, 70, 1, 82), (2, 69, 2, 84), (5, 67, 3, 82)],
    15: [(0, 64, 1, 84), (1, 67, 1, 86), (2, 72, 4, 94), (6, 74, 2, 88)],
    16: [(0, 72, 3, 88), (3, 70, 1, 82), (4, 69, 2, 84), (6, 67, 1, 78), (7, 65, 3, 82)],
    # phase 3: demoted to sparse high answers while the bass sings
    17: [(6, 72, 2, 66)],
    18: [(4, 69, 1, 64), (5, 67, 1, 62)],
    19: [(6, 65, 2, 66)],
    20: [(2, 72, 1, 64), (3, 74, 1, 66)],
    22: [(4, 72, 2, 66)],
    23: [(6, 69, 2, 64)],
    24: [(3, 67, 3, 66)],
    25: [(4, 74, 2, 68)],
    27: [(6, 72, 2, 66)],
    28: [(4, 77, 4, 70)],
    29: [(0, 76, 2, 68), (4, 72, 2, 66), (8, 74, 2, 64)],
    # phase 4: the golden cell in augmentation, interlocking with the bass
    30: [(0, 64, 2, 84), (2, 67, 2, 84), (4, 72, 4, 92)],
    31: [(0, 74, 2, 86), (2, 72, 2, 82), (4, 69, 4, 84)],
    32: [(0, 62, 2, 84), (2, 65, 2, 84), (4, 70, 4, 90)],
    33: [(0, 69, 2, 84), (2, 67, 2, 80), (4, 65, 4, 82)],
    34: [(0, 57, 1, 82), (1, 60, 1, 84), (2, 65, 3, 90), (6, 67, 2, 80)],
    35: [(0, 69, 3, 86), (3, 72, 1, 82), (4, 74, 2, 88), (6, 72, 2, 84)],
    36: [(0, 70, 2, 84), (2, 69, 2, 82), (4, 67, 2, 80), (6, 65, 2, 78)],
    37: [(0, 67, 2, 80), (2, 64, 2, 76)],
}

# phases 1, 2 and 4: the bass ANSWERS, filling the melody's gaps.
# phase 3 (bars 17-29): the bass has the melody, two octaves down.
ROOST_BASS = {
    4:  [(3, 41, 1, 72), (7, 36, 1, 70)],
    5:  [(5, 38, 1, 72), (6, 45, 1, 68)],
    6:  [(4, 41, 2, 74)],
    7:  [(3, 34, 1, 72)],
    8:  [(2, 36, 1, 70), (4, 36, 2, 66)],
    9:  [(3, 36, 1, 74), (7, 40, 1, 70)],
    10: [(1, 45, 1, 70), (6, 43, 1, 72)],
    11: [(3, 34, 1, 74), (7, 38, 1, 70)],
    12: [(1, 41, 1, 72), (6, 36, 1, 70)],
    13: [(1, 38, 1, 72), (3, 45, 1, 70)],
    14: [(3, 43, 1, 72), (7, 41, 1, 70)],
    15: [(3, 36, 1, 74), (7, 40, 1, 70)],
    16: [(2, 36, 1, 72), (5, 40, 1, 70), (8, 36, 2, 68)],
    # --- phase 3: role swap
    17: [(0, 41, 1, 84), (1, 45, 1, 82), (2, 41, 1, 78), (4, 48, 2, 86), (7, 46, 1, 76)],
    18: [(0, 45, 3, 84), (3, 43, 1, 76), (4, 41, 2, 80), (7, 40, 1, 72)],
    19: [(0, 33, 1, 80), (1, 36, 1, 82), (2, 41, 4, 88), (7, 43, 1, 76)],
    20: [(0, 45, 2, 84), (2, 48, 2, 86), (4, 46, 1, 78), (5, 45, 3, 82)],
    21: [(0, 40, 1, 82), (1, 43, 1, 84), (2, 48, 3, 90), (5, 46, 1, 80), (6, 45, 2, 84)],
    22: [(0, 43, 3, 82), (3, 45, 1, 78), (4, 43, 1, 78), (5, 40, 3, 80)],
    23: [(0, 38, 1, 82), (1, 41, 1, 84), (2, 46, 3, 90), (5, 45, 1, 80), (6, 43, 2, 82)],
    24: [(0, 41, 3, 82), (3, 40, 3, 78)],
    25: [(0, 45, 2, 84), (2, 48, 2, 86), (4, 50, 4, 90)],
    26: [(0, 48, 1, 84), (1, 46, 1, 80), (2, 45, 2, 82), (5, 43, 3, 80)],
    27: [(0, 40, 1, 82), (1, 43, 1, 84), (2, 48, 4, 90), (6, 50, 2, 86)],
    28: [(0, 48, 3, 84), (3, 46, 1, 80), (4, 45, 4, 84)],
    29: [(0, 43, 2, 82), (2, 41, 2, 80), (4, 40, 2, 78), (6, 41, 1, 74), (7, 43, 3, 80)],
    # --- phase 4: interlock.  bass on the downbeats, pizz answers over the top
    30: [(0, 36, 2, 78), (4, 43, 2, 74)],
    31: [(0, 41, 2, 78), (4, 45, 2, 74)],
    32: [(0, 34, 2, 78), (4, 41, 2, 74)],
    33: [(0, 41, 2, 78), (4, 36, 2, 74)],
    34: [(0, 41, 2, 78), (4, 45, 2, 74)],
    35: [(0, 38, 2, 76), (4, 45, 2, 72)],
    36: [(0, 34, 2, 76), (4, 41, 2, 72)],
    37: [(0, 36, 2, 78), (2, 36, 2, 72)],
}

ROOST_GLOCK = {
    2:  [(4, 77, 2, 56)],
    8:  [(4, 84, 2, 70)],
    13: [(4, 81, 2, 64)],
    16: [(8, 89, 2, 72)],
    21: [(0, 77, 2, 60)],
    29: [(8, 86, 2, 70)],
    34: [(6, 84, 2, 68)],
    37: [(0, 84, 2, 66)],
}


def build_roost():
    grid = Grid(ROOST_METERS)
    loop_tick = grid.bar(4)

    t0 = Track("conductor")
    t0.tempo(0, int(round(60_000_000 / ROOST_BPM)))
    t0.keysig(0, -1 & 0xFF, 0)                          # F major
    for i, (num, den) in enumerate(ROOST_METERS):
        if i == 0 or ROOST_METERS[i - 1] != (num, den):
            t0.timesig(grid.bar(i + 1), num, {4: 2, 8: 3}[den], 24, 8)
    t0.text(0, "CHIGLET'S ROOST")
    t0.text(loop_tick, "LOOP_START")
    t0.marker(loop_tick, "loopStart")
    t0.cc(loop_tick, 0, 111, 0)
    t0.text(grid.total, "LOOP_END")
    t0.marker(grid.total, "loopEnd")

    pizz = Track("pizzicato lead")
    pizz.program(0, 0, 45)                              # GM 45 Pizzicato Strings
    pizz.cc(0, 0, 7, 102)
    pizz.cc(0, 0, 10, 54)
    lay(pizz, grid, 0, ROOST_PIZZ)

    mar = Track("marimba")
    mar.program(0, 1, 12)                               # GM 12 Marimba
    mar.cc(0, 1, 7, 82)
    mar.cc(0, 1, 10, 78)
    for b in range(2, len(ROOST_METERS) + 1):
        dy = ROOST_MAR[ROOST_CHORDS[b - 1]]
        long_phase = 17 <= b <= 29                      # holds the harmony during the swap
        dur = 4 if long_phase else 2
        for i, p in enumerate(dy):
            mar.note(grid.at(b, 0), 1, p, dur * E, 58 - i * 4)
        if long_phase or b % 3 == 0:
            if grid.slots(b) > 4:
                for i, p in enumerate(dy):
                    mar.note(grid.at(b, 4), 1, p, dur * E, 48 - i * 4)

    glock = Track("glockenspiel")
    glock.program(0, 2, 9)                              # GM 9 Glockenspiel
    glock.cc(0, 2, 7, 74)
    glock.cc(0, 2, 10, 88)
    lay(glock, grid, 2, ROOST_GLOCK)

    bass = Track("acoustic bass")
    bass.program(0, 3, 32)                              # GM 32 Acoustic Bass
    bass.cc(0, 3, 7, 94)
    bass.cc(0, 3, 10, 64)
    lay(bass, grid, 3, ROOST_BASS)

    perc = Track("percussion")
    perc.cc(0, 9, 7, 92)
    for b in range(1, len(ROOST_METERS) + 1):
        n = grid.slots(b)
        for s in (1, 5):
            if s < n:
                perc.note(grid.at(b, s), 9, 69, E, 40)          # cabasa, offbeats only
        if b % 2 == 1 and 2 < n:
            perc.note(grid.at(b, 2), 9, 76, E, 58)              # hi woodblock
        if b % 2 == 0 and 6 < n:
            perc.note(grid.at(b, 6), 9, 77, E, 54)              # lo woodblock
        if b % 2 == 1:
            perc.note(grid.at(b, 0), 9, 64, E, 62)              # low conga grounds it
        if 9 <= b <= 16 or 30 <= b <= 37:
            if 3 < n:
                perc.note(grid.at(b, 3), 9, 60, E, 52)          # hi bongo
            if 7 < n:
                perc.note(grid.at(b, 7), 9, 61, E, 48)          # lo bongo
    for b in (8, 16, 29, 37):
        perc.note(grid.at(b, 0), 9, 81, 2 * E, 60)              # triangle at phrase ends
    # --- the 13-eighth claves, 20 whole periods from the loop point
    loop_eighths = (grid.total - loop_tick) // E                # 270
    for k in range(loop_eighths // 13):
        perc.note(loop_tick + k * 13 * E, 9, 75, E, 88 if k % 2 == 0 else 74)

    tracks = [t0, pizz, mar, glock, bass, perc]
    return write_smf(os.path.join(OUT_DIR, "chiglets_roost_companion.mid"), tracks), grid.total, loop_tick


# ===========================================================================
# VERIFICATION -- re-parse every emitted file from the bytes on disk
# ===========================================================================

def parse_smf(path):
    data = open(path, "rb").read()
    assert data[:4] == b"MThd", "bad MThd magic"
    hlen, fmt, ntrks, div = struct.unpack(">IHHH", data[4:14])
    assert hlen == 6, "bad header length %d" % hlen
    assert fmt == 1, "format is %d, expected 1" % fmt
    assert div == DIV, "division is %d, expected %d" % (div, DIV)
    pos = 8 + hlen
    tracks = []
    for _ in range(ntrks):
        assert data[pos:pos + 4] == b"MTrk", "bad MTrk magic at %d" % pos
        tlen = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        tracks.append(data[pos + 8:pos + 8 + tlen])
        pos += 8 + tlen
    assert pos == len(data), "trailing bytes: %d of %d consumed" % (pos, len(data))
    return fmt, ntrks, div, tracks


def check(path):
    fmt, ntrks, div, chunks = parse_smf(path)
    report = {"file": os.path.basename(path), "format": fmt, "tracks": ntrks,
              "division": div, "errors": [], "loop_cc111": None, "loop_text": False,
              "tempo_us": None, "timesigs": [], "notes": 0, "max_tick": 0,
              "per_track": []}
    for ti, buf in enumerate(chunks):
        p = 0
        tick = 0
        running = None
        open_notes = {}
        n_on = 0
        eot = False
        eot_tick = None
        tname = ""
        while p < len(buf):
            # delta
            d = 0
            while True:
                b = buf[p]; p += 1
                d = (d << 7) | (b & 0x7F)
                if not (b & 0x80):
                    break
            tick += d
            st = buf[p]
            if st & 0x80:
                running = st
                p += 1
            else:
                st = running
                if st is None:
                    report["errors"].append("trk%d: data byte with no running status" % ti)
                    break
            if st == 0xFF:
                kind = buf[p]; p += 1
                ln = 0
                while True:
                    b = buf[p]; p += 1
                    ln = (ln << 7) | (b & 0x7F)
                    if not (b & 0x80):
                        break
                payload = buf[p:p + ln]; p += ln
                if kind == 0x2F:
                    eot = True
                    eot_tick = tick
                    if p != len(buf):
                        report["errors"].append("trk%d: bytes after FF 2F 00" % ti)
                    break
                if kind == 0x51:
                    report["tempo_us"] = (payload[0] << 16) | (payload[1] << 8) | payload[2]
                if kind == 0x58:
                    report["timesigs"].append((tick, payload[0], 2 ** payload[1]))
                if kind == 0x03:
                    tname = payload.decode("ascii", "replace")
                if kind == 0x01 and payload == b"LOOP_START":
                    report["loop_text"] = True
            elif st in (0xF0, 0xF7):
                ln = 0
                while True:
                    b = buf[p]; p += 1
                    ln = (ln << 7) | (b & 0x7F)
                    if not (b & 0x80):
                        break
                p += ln
            else:
                hi = st & 0xF0
                nb = 1 if hi in (0xC0, 0xD0) else 2
                d1 = buf[p]; d2 = buf[p + 1] if nb == 2 else 0
                p += nb
                ch = st & 0x0F
                if hi == 0xB0 and d1 == 111:
                    report["loop_cc111"] = tick
                if hi == 0x90 and d2 > 0:
                    open_notes.setdefault((ch, d1), []).append(tick)
                    n_on += 1
                elif hi == 0x80 or (hi == 0x90 and d2 == 0):
                    q = open_notes.get((ch, d1))
                    if not q:
                        report["errors"].append(
                            "trk%d(%s): note-off with no note-on ch%d pitch%d @%d"
                            % (ti, tname, ch, d1, tick))
                    else:
                        q.pop(0)
                        if not q:
                            del open_notes[(ch, d1)]
        if not eot:
            report["errors"].append("trk%d(%s): missing FF 2F 00" % (ti, tname))
        for (ch, pi), q in open_notes.items():
            report["errors"].append(
                "trk%d(%s): DANGLING note-on ch%d pitch%d x%d" % (ti, tname, ch, pi, len(q)))
        if n_on == 0 and ti != 0:
            report["errors"].append("trk%d(%s): EMPTY -- no notes" % (ti, tname))
        report["notes"] += n_on
        report["max_tick"] = max(report["max_tick"], eot_tick or tick)
        report["per_track"].append((tname, n_on, eot_tick))
    return report


def main():
    made = []
    p, tot = build_dragonmark(False)
    made.append((p, tot, 0, "13 bars 4/4 @120"))
    p, tot = build_dragonmark(True)
    made.append((p, tot, 0, "13 bars 4/4 @120"))
    p, tot, lp = build_wingwide()
    made.append((p, tot, lp, "60 bars 6/8 @92 dotted (5 intro + 55 loop)"))
    p, tot, lp = build_roost()
    made.append((p, tot, lp, "37 bars mixed @104 (3 intro + 34 loop)"))

    ok = True
    print("=" * 74)
    for path, expect_tot, expect_loop, desc in made:
        r = check(path)
        us = r["tempo_us"]
        secs = None
        # duration: sum over the timesig-free tick span at the file tempo
        if us:
            secs = r["max_tick"] / DIV * us / 1e6
        print("%-30s fmt=%d trks=%d div=%d notes=%d" %
              (r["file"], r["format"], r["tracks"], r["division"], r["notes"]))
        print("    %s" % desc)
        print("    end tick %d (expected %d)  duration %.1fs" %
              (r["max_tick"], expect_tot, secs or 0))
        print("    loop: CC#111 @%s  text-meta=%s  (expected @%d)" %
              (r["loop_cc111"], r["loop_text"], expect_loop))
        print("    timesigs: %s" % (r["timesigs"][:6] +
                                    (["...(%d total)" % len(r["timesigs"])]
                                     if len(r["timesigs"]) > 6 else []),))
        print("    per-track notes: %s" % ", ".join(
            "%s=%d" % (n or "?", c) for n, c, _ in r["per_track"]))
        if r["max_tick"] != expect_tot:
            r["errors"].append("end tick %d != expected %d" % (r["max_tick"], expect_tot))
        if r["loop_cc111"] != expect_loop:
            r["errors"].append("CC#111 at %s, expected %d" % (r["loop_cc111"], expect_loop))
        if not r["loop_text"]:
            r["errors"].append("missing LOOP_START text meta")
        if r["errors"]:
            ok = False
            for e in r["errors"]:
                print("    !! %s" % e)
        else:
            print("    OK -- header valid, every track ends FF 2F 00, "
                  "every note-on matched, no track empty")
        print("-" * 74)
    print("RESULT: %s" % ("ALL PASS" if ok else "FAILURES PRESENT"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
