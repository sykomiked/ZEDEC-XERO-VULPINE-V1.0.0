#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_2_working_holds.py -- TOL VOVINA UPAAH LOT : the WORKING HOLDS music slice.

Composer slice 2 of 4.  Three holds, three original Standard MIDI Files:

    bellowsworks_anvil_thirteen.mid       THE BELLOWSWORKS   (workshop / forge)
    gildmaw_exchange_open_outcry.mid      GILDMAW EXCHANGE   (market / trading floor)
    vellumscale_archive_slow_accretion.mid VELLUMSCALE ARCHIVE (archive / library)

Vocabulary is the one already fixed by PROVENANCE/TVUL_ROM_FORMAT.md:10-11 --
a **hoard** is the world graph, a **hold** is an enterable place (and each hold IS
an application), a **gate** is an edge.  These are three holds in the hoard.

Format rules come from PROVENANCE/TVUL_MUSIC_FORMAT.md sec.3 and sec.7:
Python 3 stdlib only; SMF format 1; division 480; General MIDI programmes so the
files audition anywhere; section/loop lengths are Fibonacci bar counts; the
ostinato period is 13; the realignment length is lcm(13,4) = 52.

    ref: Mega Man Legends / Mega Man 64 -- melodic and harmonic bearing (bright,
         mid-tempo, functional harmony, one strong singable hook per hold).
    ref: Stewart Copeland's Spyro scores -- percussion-forward, layered tuned and
         untuned percussion, atmosphere and air rather than a rock kit.
    Influence only.  Every melody, bassline, progression and motif below was
    written for this score and quotes nothing.

ORIGINALITY DISCIPLINE.  Each hold has exactly one seed motif, stated once and
thereafter only transformed (inverted / reharmonised / displaced / reorchestrated
/ transposed).  Derived material cannot be a quotation of anything external,
which is the structural defence, not a promise.

WHY IT IS SPIRAL AND NOT CIRCULAR.  Material always returns changed:
  * Bellowsworks: the antecedent returns reorchestrated (marimba -> brass),
    displaced by two beats, and reharmonised from D DORIAN into D AEOLIAN --
    one note changes, B natural -> B flat, and the whole return re-colours.
  * Gildmaw: the two voices literally swap parts on the return, and the call is
    restated a fourth higher in a lower register.
  * Vellumscale: four passes of one 13-bar figure; each pass ADDS a voice and
    SUBTRACTS a note (16 -> 15 -> 14 -> 13), the harp shadows it in canon a
    fifth below entering five bars in, and the fourth pass inverts the figure
    about its own tonic.  A dorian is the palindromic mode -- reflect it about
    the tonic and it maps onto itself -- so the inversion stays diatonic without
    a single accidental.  That is a property of the mode, not a contrivance.

TUNING.  Written in standard pitch.  MIDI note numbers are tuning-agnostic and
retune is a playback decision; the system already owns the ratio constant
(AGP_432_RATIO = 432.0/440.0, kernel/src/audiogenomics_pro/audiogenomics_pro.c:33)
and defaults that module to 432, but that is the audiogenomics path, not a world
audio path.  See PROVENANCE/TVUL_MUSIC_FORMAT.md sec.1.6.

No existing game title appears in any track title, filename, marker, or text meta
event inside the produced binaries -- only in this source comment, per the
project's proprietary-naming rule.
"""

import os
import struct
import sys

DIV = 480                      # ticks per quarter note
OUT = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------------------
# Standard MIDI File writer (stdlib only)
# ---------------------------------------------------------------------------


def vlq(n):
    """MIDI variable-length quantity."""
    if n < 0:
        raise ValueError("negative delta time")
    out = bytearray([n & 0x7F])
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


class Track(object):
    """One MTrk chunk.  Notes are held separately so overlaps can be resolved."""

    ORD_META = 0
    ORD_OFF = 1
    ORD_ON = 2

    def __init__(self, name):
        self.name = name
        self.other = []          # (tick, order, seq, bytes)
        self.notes = []          # (ch, pitch, start, dur, vel)
        self._seq = 0
        self.min_len = 0

    # -- meta / channel-voice ------------------------------------------------
    def _push(self, tick, order, data):
        self.other.append((int(tick), order, self._seq, bytes(data)))
        self._seq += 1

    def meta(self, tick, mtype, payload):
        self._push(tick, self.ORD_META, b"\xff" + bytes([mtype]) + vlq(len(payload)) + payload)

    def text(self, tick, s):
        self.meta(tick, 0x01, s.encode("ascii"))

    def marker(self, tick, s):
        self.meta(tick, 0x06, s.encode("ascii"))

    def track_name(self, s):
        self.meta(0, 0x03, s.encode("ascii"))

    def tempo(self, tick, bpm):
        us = int(round(60000000.0 / bpm))
        self.meta(tick, 0x51, struct.pack(">I", us)[1:])

    def timesig(self, tick, num, den, clocks=24, n32=8):
        dd = {1: 0, 2: 1, 4: 2, 8: 3, 16: 4}[den]
        self.meta(tick, 0x58, bytes([num, dd, clocks, n32]))

    def keysig(self, tick, sf, minor=0):
        self.meta(tick, 0x59, bytes([sf & 0xFF, minor]))

    def prog(self, tick, ch, program):
        self._push(tick, self.ORD_META, bytes([0xC0 | ch, program]))

    def cc(self, tick, ch, ctrl, val):
        self._push(tick, self.ORD_META, bytes([0xB0 | ch, ctrl, int(val) & 0x7F]))

    def note(self, ch, tick, dur, pitch, vel):
        pitch = int(round(pitch))
        if not 0 <= pitch <= 127:
            raise ValueError("pitch %d out of range in %s" % (pitch, self.name))
        dur = int(round(dur))
        if dur <= 0:
            raise ValueError("zero-length note in %s" % self.name)
        self.notes.append((ch, pitch, int(tick), dur, max(1, min(127, int(vel)))))

    # -- serialise -----------------------------------------------------------
    def _resolved_notes(self):
        """Truncate any same-channel same-pitch overlap so every note-on has
        exactly one unambiguous note-off."""
        by_key = {}
        for n in self.notes:
            by_key.setdefault((n[0], n[1]), []).append(n)
        out = []
        for key in by_key:
            seq = sorted(by_key[key], key=lambda n: n[2])
            for i, n in enumerate(seq):
                ch, pitch, start, dur, vel = n
                end = start + dur
                if i + 1 < len(seq):
                    nxt = seq[i + 1][2]
                    if end > nxt - 4:
                        end = nxt - 4
                if end <= start:
                    continue
                out.append((ch, pitch, start, end - start, vel))
        return out

    def chunk(self):
        ev = list(self.other)
        for ch, pitch, start, dur, vel in self._resolved_notes():
            ev.append((start, self.ORD_ON, -1, bytes([0x90 | ch, pitch, vel])))
            ev.append((start + dur, self.ORD_OFF, -1, bytes([0x80 | ch, pitch, 64])))
        ev.sort(key=lambda e: (e[0], e[1], e[2]))
        body = bytearray()
        last = 0
        for tick, _o, _s, data in ev:
            body += vlq(tick - last)
            body += data
            last = tick
        end = max(last, self.min_len)
        body += vlq(end - last) + b"\xff\x2f\x00"
        return b"MTrk" + struct.pack(">I", len(body)) + bytes(body)

    def last_note_off(self):
        m = 0
        for ch, pitch, start, dur, vel in self._resolved_notes():
            m = max(m, start + dur)
        return m


def write_smf(path, tracks):
    hdr = b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), DIV)
    with open(path, "wb") as fh:
        fh.write(hdr)
        for t in tracks:
            fh.write(t.chunk())
    return os.path.getsize(path)


# ---------------------------------------------------------------------------
# timing helpers
# ---------------------------------------------------------------------------

BAR = 4 * DIV          # all three pieces are in 4/4


def tb(bar, beat=0.0):
    """bar is 1-indexed, beat is 0-indexed within the bar."""
    return int(round(((bar - 1) * 4.0 + beat) * DIV))


def swing(t, amount=40):
    """Push the 2nd and 4th sixteenth of every quarter late -> swung 16ths."""
    pos = t % DIV
    if pos == 120 or pos == 360:
        return t + amount
    return t


def sw_note(trk, ch, bar, beat, dur, pitch, vel):
    a = swing(tb(bar, beat))
    b = swing(tb(bar, beat + dur))
    trk.note(ch, a, b - a, pitch, vel)


def conductor(name, bpm, sf, title_text, total_bars, extra_meta=()):
    t = Track("conductor")
    t.track_name("conductor")
    t.tempo(0, bpm)
    t.timesig(0, 4, 4)
    t.keysig(0, sf, 0)
    t.text(0, title_text)
    t.text(0, "loop start")
    t.marker(0, "LOOP_START")
    for tick, label in extra_meta:
        t.marker(tick, label)
    t.marker(tb(total_bars + 1), "LOOP_END")
    t.min_len = tb(total_bars + 1)
    return t


def loop_marker_cc(trk, ch=0):
    """CC#111 == the widely used loop-point convention."""
    trk.cc(0, ch, 111, 0)


# ===========================================================================
# HOLD 1 -- THE BELLOWSWORKS : "ANVIL THIRTEEN"
# ===========================================================================
#
# D dorian, 132 BPM, 4/4, 34 bars (F(9)).  Form 34 = 21 + 13, and the 21 divides
# 8 + 5 + 8, and the 13 divides 5 + 8.  Every division is Fibonacci.
#
# The machinery is a 13-EIGHTH-NOTE ostinato on tuned metal.  4/4 gives 8 eighths
# a bar, so lcm(13,8) = 104 eighths = 13 bars: the anvil returns to the downbeat
# only at bars 1, 14 and 27 -- which are exactly the bars where the consequent
# and the transformed reprise begin.  In between it walks one eighth further off
# the barline every bar.  That drift is the whole point of putting a forge in 13.
#
# Seed motif: rising MINOR SEVENTH (D4 -> C5) taken on a dotted-eighth + sixteenth
# "hammer and ring" rhythm.  It is sequenced up a fourth (G4 -> F5) in the
# consequent and inverted (D5 -> E4, a seventh DOWN) in the reprise.

BW_BPM = 132
BW_BARS = 34
BW_TOTAL = tb(BW_BARS + 1)

BW_CHORD = {
    1: "Dm", 2: "Dm", 3: "Dm", 4: "Dm", 5: "C", 6: "C", 7: "G", 8: "G",
    9: "Dm", 10: "F", 11: "C", 12: "G", 13: "Dm",
    14: "Dm", 15: "Bb", 16: "C", 17: "Dm", 18: "F", 19: "C", 20: "G", 21: "Am",
    22: "Bb", 23: "F", 24: "Gm", 25: "Dm", 26: "Am",
    27: "Dm", 28: "Dm", 29: "Bb", 30: "C", 31: "Dm", 32: "Gm", 33: "A", 34: "D5",
}
BW_ROOT = {"Dm": 38, "C": 36, "G": 43, "F": 41, "Bb": 46, "Am": 45,
           "Gm": 43, "A": 45, "D5": 38}

# (bar, beat, dur_beats, midi) -- the antecedent, bars 9..13, five bars.
BW_CELL_A = [
    (9, 0.00, 0.75, 62), (9, 0.75, 0.25, 72), (9, 1.00, 1.00, 74),
    (9, 2.00, 0.50, 69), (9, 3.00, 1.00, 67),
    (10, 0.00, 1.50, 65), (10, 1.50, 0.50, 67), (10, 2.00, 1.00, 69),
    (11, 0.00, 0.75, 67), (11, 0.75, 0.25, 64), (11, 1.00, 1.00, 67),
    (11, 2.00, 0.50, 72), (11, 3.00, 1.00, 71),
    (12, 0.00, 0.75, 69), (12, 0.75, 0.25, 71), (12, 1.00, 1.50, 74),
    (12, 3.00, 1.00, 72),
    (13, 0.00, 2.00, 74), (13, 2.00, 1.00, 69),
]

# the consequent, bars 14..21, eight bars -- the seed sequenced up a fourth
BW_CELL_B = [
    (14, 0.00, 0.75, 62), (14, 0.75, 0.25, 72), (14, 1.00, 1.00, 74),
    (14, 2.00, 0.50, 77), (14, 3.00, 1.00, 76),
    (15, 0.00, 0.75, 74), (15, 0.75, 0.25, 72), (15, 1.00, 1.50, 70),
    (15, 3.00, 1.00, 74),
    (16, 0.00, 0.75, 76), (16, 0.75, 0.25, 74), (16, 1.00, 1.50, 72),
    (16, 3.00, 1.00, 67),
    (17, 0.00, 0.75, 67), (17, 0.75, 0.25, 77), (17, 1.00, 1.00, 79),
    (17, 2.00, 0.50, 74), (17, 3.00, 1.00, 72),
    (18, 0.00, 1.00, 69), (18, 1.00, 1.00, 72), (18, 2.00, 1.50, 77),
    (19, 0.00, 0.75, 76), (19, 0.75, 0.25, 74), (19, 1.00, 1.00, 72),
    (19, 2.00, 1.00, 67),
    (20, 0.00, 0.75, 67), (20, 0.75, 0.25, 69), (20, 1.00, 1.00, 71),
    (20, 2.00, 1.00, 74),
    (21, 0.00, 0.75, 69), (21, 0.75, 0.25, 71), (21, 1.00, 1.00, 72),
    (21, 2.00, 1.90, 76),
]

# the reprise, bars 27..33 -- contour inverted, then the seed once more upright
BW_CELL_Bp = [
    (27, 0.00, 0.75, 74), (27, 0.75, 0.25, 64), (27, 1.00, 1.00, 62),
    (27, 2.00, 0.50, 65), (27, 3.00, 1.00, 67),
    (28, 0.00, 0.75, 69), (28, 0.75, 0.25, 67), (28, 1.00, 1.50, 65),
    (28, 3.00, 1.00, 64),
    (29, 0.00, 0.75, 62), (29, 0.75, 0.25, 65), (29, 1.00, 1.00, 70),
    (29, 2.00, 1.00, 69),
    (30, 0.00, 0.75, 67), (30, 0.75, 0.25, 64), (30, 1.00, 1.50, 60),
    (30, 3.00, 1.00, 67),
    (31, 0.00, 0.75, 62), (31, 0.75, 0.25, 72), (31, 1.00, 1.00, 74),
    (31, 2.00, 0.50, 69), (31, 3.00, 1.00, 65),
    (32, 0.00, 1.00, 67), (32, 1.00, 1.00, 70), (32, 2.00, 1.00, 74),
    (33, 0.00, 0.75, 69), (33, 0.75, 0.25, 73), (33, 1.00, 1.00, 76),
    (33, 2.00, 1.00, 69),
]

# the 13-eighth machine cycle: hits on steps 0 2 4 7 9 11  (2+2+3+2+2+2)
BW_ANVIL = {0: (74, 100), 2: (69, 74), 4: (74, 80), 7: (77, 100),
            9: (69, 72), 11: (72, 76)}

BW_BASS_FORGE = [(0.00, 0.75, 0), (1.00, 0.50, 0), (1.75, 0.25, 12),
                 (2.50, 0.50, 0), (3.00, 0.50, 7), (3.50, 0.50, 0)]
BW_BASS_DRIVE = [(0.00, 0.50, 0), (0.50, 0.50, 0), (1.50, 0.50, 7),
                 (2.00, 0.50, 0), (2.50, 0.50, 12), (3.50, 0.50, 7)]
BW_BASS_FILL = [(0.00, 0.50, 0), (1.00, 0.50, 2), (2.00, 0.50, 3), (3.00, 0.50, 7)]


def build_bellowsworks():
    marks = [(tb(9), "ANTECEDENT 5"), (tb(14), "CONSEQUENT 8"),
             (tb(22), "REPRISE AEOLIAN 5"), (tb(27), "INVERSION 8")]
    t0 = conductor("conductor", BW_BPM, 0,
                   "TOL VOVINA UPAAH LOT - hold THE BELLOWSWORKS - ANVIL THIRTEEN",
                   BW_BARS, marks)
    t0.text(0, "D dorian; 34 bars = 21+13; anvil ostinato = 13 eighths, realigns every 13 bars")
    loop_marker_cc(t0, 0)

    lead = Track("LEAD marimba")           # ch 0
    lead.track_name("LEAD marimba")
    lead.prog(0, 0, 12)                    # GM 13 Marimba
    lead.cc(0, 0, 7, 104)
    lead.cc(0, 0, 10, 58)

    horn = Track("HORN brass")             # ch 1
    horn.track_name("HORN brass")
    horn.prog(0, 1, 61)                    # GM 62 Brass Section
    horn.cc(0, 1, 7, 92)
    horn.cc(0, 1, 10, 72)

    anvil = Track("ANVIL tuned metal")     # ch 2
    anvil.track_name("ANVIL tuned metal")
    anvil.prog(0, 2, 14)                   # GM 15 Tubular Bells
    anvil.cc(0, 2, 7, 88)
    anvil.cc(0, 2, 10, 80)

    bass = Track("BASS")                   # ch 3
    bass.track_name("BASS")
    bass.prog(0, 3, 33)                    # GM 34 Electric Bass (finger)
    bass.cc(0, 3, 7, 108)
    bass.cc(0, 3, 10, 64)

    drums = Track("FLOOR percussion")      # ch 9
    drums.track_name("FLOOR percussion")
    drums.cc(0, 9, 7, 100)
    drums.cc(0, 9, 10, 64)

    # -- lead ---------------------------------------------------------------
    for bar, beat, dur, p in BW_CELL_A:
        lead.note(0, tb(bar, beat), int(dur * DIV) - 6, p, 96 if dur >= 0.5 else 82)
    for bar, beat, dur, p in BW_CELL_B:
        lead.note(0, tb(bar, beat), int(dur * DIV) - 6, p, 100 if dur >= 0.5 else 86)
    for bar, beat, dur, p in BW_CELL_Bp:
        lead.note(0, tb(bar, beat), int(dur * DIV) - 6, p, 104 if dur >= 0.5 else 88)

    # -- horn: low open fifths under the machinery --------------------------
    for start, span, pitches in ((1, 2, (50, 57)), (3, 2, (50, 60)),
                                 (5, 2, (48, 55)), (7, 2, (55, 62))):
        for p in pitches:
            horn.note(1, tb(start), span * BAR - 30, p, 46)
    # bar 21 swell into the reprise
    for beat, p in ((0.0, 57), (1.0, 62), (2.0, 64), (3.0, 69)):
        horn.note(1, tb(21, beat), DIV - 20, p, 54 + int(beat) * 8)

    # -- horn: the antecedent returns.  Three transformations at once:
    #    reorchestrated (marimba -> brass), displaced +2 beats, and
    #    reharmonised out of D dorian into D aeolian (B natural -> B flat).
    a_origin = (9 - 1) * 4.0                       # abs beat of CELL_A bar 9 beat 0
    for bar, beat, dur, p in BW_CELL_A:
        rel = (bar - 1) * 4.0 + beat - a_origin
        if p == 71:
            p = 70                                  # <- the one note that recolours it
        abs_beat = (22 - 1) * 4.0 + 2.0 + rel
        horn.note(1, int(round(abs_beat * DIV)), int(dur * DIV) - 10, p,
                  92 if dur >= 0.5 else 78)
    # horn weight under the last upright statement
    for bar, beat, dur, p in BW_CELL_Bp:
        if bar >= 31:
            horn.note(1, tb(bar, beat), int(dur * DIV) - 10, p - 12, 74)

    # -- anvil: the 13-eighth cycle, all 34 bars ----------------------------
    eighths = BW_BARS * 8
    for i in range(eighths):
        step = i % 13
        if step not in BW_ANVIL:
            continue
        pitch, vel = BW_ANVIL[step]
        bar = i // 8 + 1
        if 9 <= bar <= 21:
            vel = int(vel * 0.74)        # the machinery ducks for the tune
        elif bar >= 27:
            vel = int(vel * 1.0)
        else:
            vel = int(vel * 0.92)
        anvil.note(2, i * (DIV // 2), 200, pitch, vel)

    # -- bass ---------------------------------------------------------------
    for bar in range(1, BW_BARS + 1):
        root = BW_ROOT[BW_CHORD[bar]]
        if bar == BW_BARS:
            pat = BW_BASS_FILL
        elif 9 <= bar <= 13 or 22 <= bar <= 26:
            pat = BW_BASS_DRIVE
        else:
            pat = BW_BASS_FORGE
        for beat, dur, off in pat:
            v = 104 if beat == 0.0 else 86
            bass.note(3, tb(bar, beat), int(dur * DIV) - 10, root + off, v)

    # -- percussion: sparse at first, and the big tom rides the 13 cycle ----
    for bar in range(1, BW_BARS + 1):
        drums.note(9, tb(bar, 0.0), 60, 36, 108)
        drums.note(9, tb(bar, 2.5), 60, 36, 92)
        if bar >= 9:
            drums.note(9, tb(bar, 1.5), 60, 36, 78)
        if bar >= 22:
            drums.note(9, tb(bar, 0.75), 60, 36, 70)
            drums.note(9, tb(bar, 3.5), 60, 36, 74)
        if 9 <= bar <= 21 or bar >= 27:
            for k in range(8):
                drums.note(9, tb(bar, k * 0.5), 60, 82, 62 if k % 2 == 0 else 40)
    for i in range(eighths):
        step = i % 13
        tick = i * (DIV // 2)
        bar = i // 8 + 1
        if step == 7:
            drums.note(9, tick, 60, 41, 104)          # low floor tom, drifting
        if step in (2, 9):
            drums.note(9, tick, 60, 76, 74)           # hi wood block
        if step == 0 and (i // 13) % 2 == 0:
            drums.note(9, tick, 60, 81, 66)           # open triangle, every other cycle
        if step == 4 and bar >= 22:
            drums.note(9, tick, 60, 56, 70)           # cowbell joins for the reprise
    for bar in (22, 27):
        drums.note(9, tb(bar, 0.0), 60, 49, 100)
    for beat, note in ((2.0, 45), (2.5, 47), (3.0, 50), (3.5, 47)):
        drums.note(9, tb(34, beat), 60, note, 96)

    for t in (t0, lead, horn, anvil, bass, drums):
        t.min_len = BW_TOTAL
    return [t0, lead, horn, anvil, bass, drums]


# ===========================================================================
# HOLD 2 -- GILDMAW EXCHANGE : "OPEN OUTCRY"
# ===========================================================================
#
# G mixolydian, 138 BPM, 4/4, swung sixteenths, 55 bars (F(10)).
# Form 55 = 21 + 13 + 21.  The 21 divides 8 + 5 + 8.
#
# Seed motif: a rising OCTAVE off a short pickup -- the shout across the floor.
# Voice A (muted trumpet) calls it; voice B (percussive organ) answers it
# INVERTED, the octave taken downward.  On the reprise the two voices SWAP
# parts outright and the call is restated a fourth higher in the lower register.
#
# 13 lives in the cowbell -- the floor bell -- which runs a 13-BEAT cycle with
# strikes on steps 0, 5 and 8 (Fibonacci positions inside the cycle).  Against
# 4/4 it realigns with the downbeat only every lcm(13,4) = 52 beats = 13 bars,
# at bars 1, 14, 27, 40 and 53.  The sections begin at 1, 22 and 35, so the bell
# never agrees with the form: it walks through it.

GE_BPM = 138
GE_BARS = 55
GE_TOTAL = tb(GE_BARS + 1)

GE_CHORD = {
    1: "G", 2: "F", 3: "C", 4: "G", 5: "G", 6: "Bb", 7: "C", 8: "D",
    9: "G", 10: "Am", 11: "C", 12: "F", 13: "G",
    14: "Em", 15: "C", 16: "G", 17: "D", 18: "Em", 19: "Bb", 20: "F", 21: "D",
    22: "G", 23: "G", 24: "G", 25: "G", 26: "G", 27: "G", 28: "G", 29: "G",
    30: "C", 31: "F", 32: "Bb", 33: "D", 34: "D",
    35: "C", 36: "Bb", 37: "F", 38: "C", 39: "C", 40: "Eb", 41: "F", 42: "G",
    43: "C", 44: "Dm", 45: "F", 46: "Bb", 47: "C",
    48: "Am", 49: "F", 50: "G", 51: "D", 52: "Em", 53: "C", 54: "Dm", 55: "D7",
}
GE_PC = {"G": 7, "F": 5, "C": 0, "Bb": 10, "D": 2, "D7": 2, "Am": 9,
         "Em": 4, "Dm": 2, "Eb": 3}
GE_Q = {"G": "maj", "F": "maj", "C": "maj", "Bb": "maj", "D": "maj",
        "D7": "dom", "Am": "min", "Em": "min", "Dm": "min", "Eb": "maj"}
GE_VOICING = {
    "G": (67, 71, 74), "F": (65, 69, 72), "C": (64, 67, 72), "Bb": (65, 70, 74),
    "D": (66, 69, 74), "D7": (62, 66, 69, 72), "Am": (64, 69, 72),
    "Em": (64, 67, 71), "Dm": (65, 69, 74), "Eb": (67, 70, 75),
}

GE_CALL = [           # voice A, bars 1..4 -- the outcry
    (1, 0.00, 0.50, 67), (1, 0.50, 1.00, 79), (1, 1.50, 0.50, 77),
    (1, 2.00, 0.50, 76), (1, 2.50, 1.00, 74), (1, 3.50, 0.50, 71),
    (2, 0.00, 0.75, 72), (2, 0.75, 0.25, 69), (2, 1.00, 1.00, 65),
    (2, 3.00, 0.50, 69), (2, 3.50, 0.50, 72),
    (3, 0.00, 0.50, 76), (3, 0.50, 0.50, 79), (3, 1.00, 0.50, 76),
    (3, 1.50, 1.00, 72), (3, 3.00, 1.00, 74),
    (4, 0.00, 0.50, 71), (4, 0.50, 0.50, 74), (4, 1.00, 1.50, 79),
    (4, 3.00, 0.50, 77), (4, 3.50, 0.50, 74),
]
GE_ANSWER = [         # voice B, bars 5..8 -- the same shout, octave inverted
    (5, 0.00, 0.50, 67), (5, 0.50, 1.00, 55), (5, 1.50, 0.50, 57),
    (5, 2.00, 0.50, 59), (5, 2.50, 1.00, 60), (5, 3.50, 0.50, 62),
    (6, 0.00, 0.75, 65), (6, 0.75, 0.25, 62), (6, 1.00, 1.00, 58),
    (6, 3.00, 0.50, 62), (6, 3.50, 0.50, 65),
    (7, 0.00, 0.50, 67), (7, 0.50, 0.50, 64), (7, 1.00, 0.50, 67),
    (7, 1.50, 1.00, 72), (7, 3.00, 1.00, 71),
    (8, 0.00, 0.50, 69), (8, 0.50, 0.50, 66), (8, 1.00, 1.50, 62),
    (8, 3.00, 0.50, 69), (8, 3.50, 0.50, 72),
]
GE_TOGETHER_HI = [    # bars 9..13, five bars, upper line
    (9, 0.00, 0.50, 74), (9, 0.50, 1.00, 79), (9, 2.00, 0.50, 77),
    (9, 2.50, 0.50, 76), (9, 3.00, 1.00, 74),
    (10, 0.00, 0.75, 72), (10, 0.75, 0.25, 74), (10, 1.00, 1.00, 76),
    (10, 2.50, 0.50, 72), (10, 3.00, 1.00, 69),
    (11, 0.00, 0.50, 72), (11, 0.50, 0.50, 76), (11, 1.00, 1.00, 79),
    (11, 2.50, 0.50, 76), (11, 3.00, 1.00, 72),
    (12, 0.00, 0.75, 77), (12, 0.75, 0.25, 76), (12, 1.00, 1.50, 72),
    (12, 3.00, 1.00, 69),
    (13, 0.00, 0.50, 71), (13, 0.50, 0.50, 74), (13, 1.00, 2.00, 79),
    (13, 3.50, 0.50, 77),
]
GE_TOGETHER_LO = [    # bars 9..13, the offbeat answering shout
    (9, 0.75, 0.25, 62), (9, 1.75, 0.25, 59), (9, 2.75, 0.50, 55),
    (10, 0.75, 0.25, 60), (10, 1.75, 0.25, 57), (10, 2.75, 0.50, 64),
    (11, 0.75, 0.25, 64), (11, 1.75, 0.25, 60), (11, 2.75, 0.50, 55),
    (12, 0.75, 0.25, 65), (12, 1.75, 0.25, 60), (12, 2.75, 0.50, 57),
    (13, 0.75, 0.25, 62), (13, 1.75, 0.25, 59), (13, 2.75, 0.50, 62),
]
GE_DEV = [            # bars 14..21, eight bars of development
    (14, 0.00, 0.50, 69), (14, 0.50, 1.00, 81), (14, 1.50, 0.50, 79),
    (14, 2.00, 1.00, 76), (14, 3.00, 1.00, 74),
    (15, 0.00, 0.75, 72), (15, 0.75, 0.25, 76), (15, 1.00, 1.00, 79),
    (15, 2.50, 0.50, 76), (15, 3.00, 1.00, 72),
    (16, 0.00, 0.50, 67), (16, 0.50, 1.00, 79), (16, 1.50, 0.50, 77),
    (16, 2.00, 0.50, 76), (16, 2.50, 1.50, 74),
    (17, 0.00, 0.50, 69), (17, 0.50, 0.50, 74), (17, 1.00, 1.00, 78),
    (17, 2.00, 1.00, 81), (17, 3.00, 1.00, 74),
    (18, 0.00, 0.50, 71), (18, 0.50, 0.50, 76), (18, 1.00, 1.00, 79),
    (18, 2.00, 0.50, 76), (18, 2.50, 0.50, 74), (18, 3.00, 1.00, 71),
    (19, 0.00, 0.75, 70), (19, 0.75, 0.25, 74), (19, 1.00, 1.50, 77),
    (19, 3.00, 1.00, 74),
    (20, 0.00, 0.75, 72), (20, 0.75, 0.25, 69), (20, 1.00, 1.50, 65),
    (20, 3.00, 1.00, 69),
    (21, 0.00, 0.50, 74), (21, 0.50, 0.50, 78), (21, 1.00, 1.00, 81),
    (21, 2.00, 1.50, 74),
]
GE_CLEARING = [       # bars 22..29 -- the floor drops to one voice
    (22, 0.00, 2.00, 67), (22, 3.00, 1.00, 65),
    (23, 0.00, 3.00, 62),
    (24, 0.00, 1.00, 60), (24, 2.00, 2.00, 62),
    (25, 0.00, 3.75, 55),
    (26, 0.00, 2.00, 67), (26, 2.50, 0.50, 71), (26, 3.00, 1.00, 72),
    (27, 0.00, 3.00, 74),
    (28, 0.00, 1.00, 72), (28, 1.50, 0.50, 71), (28, 2.00, 2.00, 69),
    (29, 0.00, 3.75, 67),
]
GE_REOPEN = [         # bars 30..34 -- the floor comes back in
    (30, 2.00, 0.50, 72), (30, 2.50, 0.50, 76), (30, 3.00, 1.00, 79),
    (31, 0.00, 0.50, 77), (31, 1.00, 0.50, 72), (31, 2.00, 1.00, 69),
    (32, 0.00, 0.50, 70), (32, 0.50, 1.00, 82), (32, 2.00, 0.50, 77),
    (32, 2.50, 1.00, 74),
    (33, 0.00, 0.50, 74), (33, 0.50, 0.50, 78), (33, 1.00, 1.00, 81),
    (33, 2.00, 2.00, 74),
    (34, 0.00, 0.50, 69), (34, 1.00, 0.50, 72), (34, 2.00, 1.90, 74),
]

GE_COMP_A = [(0.50, 0.25), (1.50, 0.25), (2.50, 0.25), (3.00, 0.25), (3.75, 0.25)]
GE_COMP_B = [(0.00, 0.25), (0.75, 0.25), (1.50, 0.50), (2.75, 0.25), (3.50, 0.25)]
# thinned chop for the bars where both voices are singing at once -- air matters
GE_COMP_S = [(1.50, 0.25), (3.00, 0.25)]
GE_SPARSE_BARS = set(range(9, 14)) | set(range(30, 33)) | set(range(43, 48))


def ge_shift(events, bars, semis):
    return [(b + bars, beat, dur, p + semis) for (b, beat, dur, p) in events]


def ge_bass_pitch(pc, lo=40):
    return lo + ((pc - lo) % 12)


def ge_walk(bar):
    ch = GE_CHORD[bar]
    nxt = GE_CHORD[bar + 1] if bar < GE_BARS else GE_CHORD[1]
    r = ge_bass_pitch(GE_PC[ch])
    third = r + (3 if GE_Q[ch] == "min" else 4)
    fifth = r + 7
    nr = ge_bass_pitch(GE_PC[nxt])
    appr = nr - 1 if nr - 1 > r else nr + 1
    # the octave note goes UP when there is room and DOWN when there is not, so
    # it is always a real octave leap and never collapses back onto the root
    octv = r + 12 if r + 12 <= 57 else r - 12
    shape = bar % 3
    if shape == 0:
        seq = [r, third, fifth, appr]
    elif shape == 1:
        seq = [r, octv, fifth, appr]
    else:
        seq = [r, fifth, third + 12, appr]
    return [p - 12 if p > 57 else p for p in seq]


def build_gildmaw():
    marks = [(tb(9), "TUTTI 5"), (tb(14), "DEVELOPMENT 8"),
             (tb(22), "THE CLEARING 13"), (tb(35), "REPRISE - VOICES SWAPPED 21"),
             (tb(48), "FULL FLOOR 8")]
    t0 = conductor("conductor", GE_BPM, 0,
                   "TOL VOVINA UPAAH LOT - hold GILDMAW EXCHANGE - OPEN OUTCRY",
                   GE_BARS, marks)
    t0.text(0, "G mixolydian; 55 bars = 21+13+21; floor bell = 13-beat cycle, realigns every 52 beats")
    loop_marker_cc(t0, 0)

    va = Track("VOICE A muted trumpet")     # ch 0
    va.track_name("VOICE A muted trumpet")
    va.prog(0, 0, 59)                       # GM 60 Muted Trumpet
    va.cc(0, 0, 7, 106)
    va.cc(0, 0, 10, 48)

    vb = Track("VOICE B percussive organ")  # ch 1
    vb.track_name("VOICE B percussive organ")
    vb.prog(0, 1, 17)                       # GM 18 Percussive Organ
    vb.cc(0, 1, 7, 96)
    vb.cc(0, 1, 10, 80)

    gtr = Track("COMP clean guitar")        # ch 2
    gtr.track_name("COMP clean guitar")
    gtr.prog(0, 2, 27)                      # GM 28 Electric Guitar (clean)
    gtr.cc(0, 2, 7, 84)
    gtr.cc(0, 2, 10, 90)

    bs = Track("WALKING BASS")              # ch 3
    bs.track_name("WALKING BASS")
    bs.prog(0, 3, 32)                       # GM 33 Acoustic Bass
    bs.cc(0, 3, 7, 108)
    bs.cc(0, 3, 10, 64)

    dr = Track("FLOOR percussion")          # ch 9
    dr.track_name("FLOOR percussion")
    dr.cc(0, 9, 7, 100)
    dr.cc(0, 9, 10, 64)

    def put(trk, ch, events, vel_hi, vel_lo):
        for bar, beat, dur, p in events:
            if not 1 <= bar <= GE_BARS:
                continue
            sw_note(trk, ch, bar, beat, dur - 0.02, p, vel_hi if dur >= 0.5 else vel_lo)

    # --- exposition ---------------------------------------------------------
    put(va, 0, GE_CALL, 100, 86)
    put(vb, 1, GE_ANSWER, 96, 82)
    put(va, 0, GE_TOGETHER_HI, 102, 88)
    put(vb, 1, GE_TOGETHER_LO, 90, 84)
    put(va, 0, GE_DEV, 104, 90)
    # organ pads through the development
    for bar in range(14, 22):
        v = GE_VOICING[GE_CHORD[bar]]
        for p in (v[0] - 12, v[1] - 12):
            vb.note(1, tb(bar, 0.0), 2 * DIV - 20, p, 62)

    # --- the clearing: one voice, thirteen bars ------------------------------
    put(vb, 1, GE_CLEARING, 84, 76)
    put(va, 0, GE_REOPEN, 100, 88)

    # --- reprise: the two voices trade places -------------------------------
    # organ takes the trumpet's call, a fourth up and an octave down (+5-12)
    put(vb, 1, ge_shift(GE_CALL, 34, -7), 96, 84)
    # trumpet takes the organ's answer, a fourth up (+5)
    put(va, 0, ge_shift(GE_ANSWER, 34, 5), 100, 86)
    # and the tutti swaps too: organ on the upper line, trumpet on the shout
    put(vb, 1, ge_shift(GE_TOGETHER_HI, 34, -7), 94, 84)
    put(va, 0, ge_shift(GE_TOGETHER_LO, 34, 17), 98, 88)
    # the development returns at pitch but reharmonised (see GE_CHORD 48..55)
    put(va, 0, ge_shift(GE_DEV, 34, 0), 106, 92)
    for bar, beat, dur, p in ge_shift(GE_DEV, 34, 0):
        if bar >= 52:
            sw_note(vb, 1, bar, beat, dur - 0.02, p - 12, 88 if dur >= 0.5 else 78)

    # --- comping guitar -----------------------------------------------------
    for bar in range(1, GE_BARS + 1):
        if 22 <= bar <= 29:
            continue                        # the floor is empty
        v = GE_VOICING[GE_CHORD[bar]]
        if bar in GE_SPARSE_BARS:
            pat = GE_COMP_S
        else:
            pat = GE_COMP_A if (bar // 4) % 2 == 0 else GE_COMP_B
        for beat, dur in pat:
            for i, p in enumerate(v):
                sw_note(gtr, 2, bar, beat, dur, p, 72 - i * 4 if beat != 0.0 else 80)

    # --- bass ---------------------------------------------------------------
    for bar in range(1, GE_BARS + 1):
        if 22 <= bar <= 25:
            bs.note(3, tb(bar, 0.0), BAR - 24, 43, 74)
            continue
        if 26 <= bar <= 29:
            for beat, p in ((0.0, 43), (2.0, 45 if bar % 2 else 50)):
                bs.note(3, tb(bar, beat), 2 * DIV - 24, p, 76)
            continue
        for i, p in enumerate(ge_walk(bar)):
            if bar == GE_BARS and i == 3:
                p = 42                       # F# leading tone home to G
            bs.note(3, tb(bar, i * 1.0), DIV - 24, p, 100 if i == 0 else 88)

    # --- percussion ---------------------------------------------------------
    for bar in range(1, GE_BARS + 1):
        quiet = 22 <= bar <= 29
        dr.note(9, tb(bar, 0.0), 60, 36, 106)
        dr.note(9, tb(bar, 2.5), 60, 36, 90)
        if not quiet:
            dr.note(9, tb(bar, 1.0), 60, 38, 100)
            dr.note(9, tb(bar, 3.0), 60, 38, 98)
            for k in range(8):
                t = swing(tb(bar, k * 0.5))
                dr.note(9, t, 60, 42, 70 if k % 2 == 0 else 50)
            if bar % 4 == 0:
                dr.note(9, tb(bar, 3.5), 60, 46, 78)
        # tambourine leaves beat 2.5 to the kick -- a lopsided floor shuffle
        if not 22 <= bar <= 25:
            for beat in (0.5, 1.5, 3.5):
                dr.note(9, swing(tb(bar, beat)), 60, 54, 52 if quiet else 64)
    # the floor bell: 13-beat cycle, strikes on Fibonacci steps 0, 5, 8
    total_beats = GE_BARS * 4
    for b in range(total_beats):
        step = b % 13
        if step in (0, 5, 8):
            dr.note(9, int(b * DIV), 60, 56, 88 if step == 0 else 70)
    for bar in (1, 22, 35, 48):
        dr.note(9, tb(bar, 0.0), 60, 49, 104)

    for t in (t0, va, vb, gtr, bs, dr):
        t.min_len = GE_TOTAL
    return [t0, va, vb, gtr, bs, dr]


# ===========================================================================
# HOLD 3 -- VELLUMSCALE ARCHIVE : "SLOW ACCRETION"
# ===========================================================================
#
# A dorian, 60 BPM, 4/4, 55 bars (F(10)) = four 13-bar passes plus a three-bar
# hush.  13 + 13 + 13 + 13 + 3 -- 52 is the realignment length, 3 is F(4), 55 is
# F(10).  Nothing here climaxes and nothing here fills.
#
# One figure of sixteen notes across thirteen bars.  Each pass ADDS a voice and
# SUBTRACTS a note: 16, 15, 14, 13.  The harp shadows the figure in canon a
# diatonic fifth below, entering five bars into the pass and running eight --
# 5 + 8 = 13, so each shadow is contained inside its own pass.  The canon always
# draws from the FULL sixteen notes, so it keeps exactly what the lead is
# shedding, and by the fourth pass the figure is buried inside its own harmony.
#
# Pass 4 inverts the figure about A4.  A dorian is the palindromic mode: reflect
# it about its own tonic and you land on the same seven pitch classes, so the
# inversion needs no accidental and no adjustment.  Not a trick -- a property.
#
# The only percussion is one triangle on a 13-BEAT cycle from pass 3, which
# against 4/4 drifts and closes exactly one pass later.

VA_BPM = 60
VA_BARS = 55
VA_TOTAL = tb(VA_BARS + 1)

# (local bar 0..12, beat, dur in beats, midi)
VA_FIGURE = [
    (0, 0.0, 3.0, 69), (0, 3.0, 1.0, 71),
    (1, 0.0, 4.0, 72),
    (2, 0.0, 2.0, 76), (2, 2.0, 2.0, 74),
    (3, 0.0, 4.0, 71),
    (4, 0.0, 3.0, 69),
    (5, 2.0, 2.0, 78),
    (6, 0.0, 4.0, 76),
    (7, 0.0, 3.0, 74),
    (8, 0.0, 2.0, 72), (8, 2.0, 2.0, 71),
    (9, 0.0, 4.0, 69),
    (10, 0.0, 3.0, 67),
    (11, 0.0, 5.0, 69),
    (12, 2.0, 2.0, 64),
]
# indices dropped on passes 2, 3, 4 -- 16 -> 15 -> 14 -> 13 notes
VA_DROP = {1: (), 2: (4,), 3: (4, 11), 4: (4, 11, 13)}

# every pad and choir pitch below is drawn from A dorian -- A B C D E F# G -- so
# nothing under the figure ever contradicts the mode
VA_PAD = [                      # (bar, span_bars, (pitches...))
    (1, 13, (45, 52)),                                              # A  E
    (14, 4, (45, 52)), (18, 4, (40, 47)), (22, 3, (43, 50)),        # A E / E B / G D
    (25, 2, (45, 52)),
    (27, 4, (50, 57)), (31, 4, (45, 52)), (35, 3, (43, 50)),        # D A / A E / G D
    (38, 8, (45, 52)), (46, 4, (47, 54)), (50, 5, (45, 52)),        # A E / B F# / A E
]
# the choir breathes: each chord is followed by a bar of nothing
VA_CHOIR = [
    (27, 2, (57, 64)), (30, 2, (60, 67)), (33, 2, (59, 66)), (36, 3, (57, 64)),
    (40, 2, (62, 69)), (43, 2, (60, 67)), (46, 3, (59, 64)), (50, 3, (57, 64)),
]

# A dorian as scale degrees, so pass 3 can be transposed DIATONICALLY.  A
# chromatic +5 would drag the figure out of the mode and grind against the pad;
# up two scale degrees keeps every note in A dorian and still lifts the pass.
VA_SCALE = [9, 11, 0, 2, 4, 6, 7]      # A B C D E F# G


def va_degree(p):
    """MIDI pitch -> absolute scale-degree index, counting A as degree 0."""
    pc = p % 12
    if pc not in VA_SCALE:
        raise ValueError("pitch %d (%d) is not in A dorian" % (p, pc))
    return 7 * ((p - 9) // 12) + VA_SCALE.index(pc)


def va_pitch(d):
    """Absolute scale-degree index -> MIDI pitch."""
    pc = VA_SCALE[d % 7]
    return 9 + 12 * (d // 7) + (pc - 9) % 12


def va_diatonic(p, degrees):
    """Transpose within A dorian.  Works in both directions -- which the octave
    clamp it replaced did not, and a canon below the lead needs it to."""
    return va_pitch(va_degree(p) + degrees)


def va_pass_notes(pass_no, origin_bar, degrees=0, octave=0, invert=False, full=False):
    drop = () if full else VA_DROP[pass_no]
    out = []
    for i, (lb, beat, dur, p) in enumerate(VA_FIGURE):
        if i in drop:
            continue
        q = (138 - p) if invert else p     # A dorian is the palindromic mode:
        if degrees:                        # reflect it about A and it maps onto
            q = va_diatonic(q, degrees)    # itself, so no accidental appears
        out.append((origin_bar + lb, beat, dur, q + 12 * octave))
    return out


def build_vellumscale():
    marks = [(tb(1), "PASS 1 - 16 NOTES"), (tb(14), "PASS 2 - 15 NOTES + HARP CANON"),
             (tb(27), "PASS 3 - 14 NOTES + CHOIR"),
             (tb(40), "PASS 4 - 13 NOTES INVERTED"), (tb(53), "HUSH 3")]
    t0 = conductor("conductor", VA_BPM, 1,
                   "TOL VOVINA UPAAH LOT - hold VELLUMSCALE ARCHIVE - SLOW ACCRETION",
                   VA_BARS, marks)
    t0.text(0, "A dorian; 55 bars = 13+13+13+13+3; figure sheds one note per pass, canon at 5 bars")
    loop_marker_cc(t0, 0)

    cel = Track("FIGURE celesta")      # ch 0
    cel.track_name("FIGURE celesta")
    cel.prog(0, 0, 8)                  # GM 9 Celesta
    cel.cc(0, 0, 7, 88)
    cel.cc(0, 0, 10, 64)

    hrp = Track("CANON harp")          # ch 1
    hrp.track_name("CANON harp")
    hrp.prog(0, 1, 46)                 # GM 47 Orchestral Harp
    hrp.cc(0, 1, 7, 72)
    hrp.cc(0, 1, 10, 44)

    pad = Track("PAD room tone")       # ch 2
    pad.track_name("PAD room tone")
    pad.prog(0, 2, 89)                 # GM 90 Pad 2 (warm)
    pad.cc(0, 2, 7, 54)
    pad.cc(0, 2, 10, 84)

    cho = Track("CHOIR")               # ch 3
    cho.track_name("CHOIR")
    cho.prog(0, 3, 52)                 # GM 53 Choir Aahs
    cho.cc(0, 3, 7, 48)
    cho.cc(0, 3, 10, 64)

    per = Track("TRIANGLE")            # ch 9
    per.track_name("TRIANGLE")
    per.cc(0, 9, 7, 44)
    per.cc(0, 9, 10, 54)

    LAST = tb(VA_BARS, 3.0)            # everything is silent for the final beat

    def lay(trk, ch, events, vel, cap=None, until=None):
        for bar, beat, dur, p in events:
            if until is not None and bar >= until:
                continue
            if cap is not None:
                dur = min(dur, cap)
            start = tb(bar, beat)
            end = min(tb(bar, beat + dur) - 24, LAST)
            if end <= start:
                continue
            trk.note(ch, start, end - start, p, vel)

    # The harp is plucked, not sustained: capping the canon at a bar and a half
    # is what stops the accretion from becoming a wall.  The archive must keep
    # its silences -- they are as composed as the notes.
    HARP_CAP = 1.5
    # the canon answers a diatonic FIFTH below the lead (-4 degrees), which is
    # the classical answer and keeps the two voices consonant where a canon at
    # the octave would grind seconds against a piece that has nowhere to hide
    CANON = -4
    # each canon enters 5 bars into its pass and runs 8 bars, so it is contained
    # inside its own 13-bar pass: 5 + 8 = 13.  The Fibonacci division of the pass
    # is what stops one pass's shadow from colliding with the next pass's lead.

    # pass 1: celesta alone over a single held fifth
    lay(cel, 0, va_pass_notes(1, 1), 64)
    # pass 2: one note gone, the pad starts to move, the harp canon opens
    lay(cel, 0, va_pass_notes(2, 14), 66)
    lay(hrp, 1, va_pass_notes(2, 19, degrees=CANON, full=True), 46, HARP_CAP, 27)
    # pass 3: figure up a diatonic third, two notes gone, choir enters
    lay(cel, 0, va_pass_notes(3, 27, degrees=2), 62)
    lay(hrp, 1, va_pass_notes(3, 32, degrees=2 + CANON, full=True), 44, HARP_CAP, 40)
    # pass 4: thirteen notes, inverted about the tonic; the canon stays upright,
    # so lead and shadow are mirror images of one another
    lay(cel, 0, va_pass_notes(4, 40, invert=True), 60)
    lay(hrp, 1, va_pass_notes(4, 45, degrees=CANON, full=True), 42, HARP_CAP, 53)
    # the hush: one last unhurried statement of the head, then nothing
    lay(cel, 0, [(53, 0.0, 4.0, 69), (54, 0.0, 3.0, 67), (55, 0.0, 2.0, 64)], 48)

    for bar, span, pitches in VA_PAD:
        start = tb(bar)
        end = min(tb(bar + span) - 8, LAST)
        for p in pitches:
            if end > start:
                pad.note(2, start, end - start, p, 34 if bar < 27 else 40)
    for bar, span, pitches in VA_CHOIR:
        start = tb(bar)
        end = min(tb(bar + span) - 24, LAST)
        for p in pitches:
            if end > start:
                cho.note(3, start, end - start, p, 36)

    # one soft triangle every 13 beats, from pass 3 onward
    for b in range(VA_BARS * 4):
        if b % 13 == 0 and tb(27) <= b * DIV <= LAST:
            per.note(9, b * DIV, 120, 81, 30)

    for t in (t0, cel, hrp, pad, cho, per):
        t.min_len = VA_TOTAL
    return [t0, cel, hrp, pad, cho, per]


# ===========================================================================

PIECES = [
    ("bellowsworks_anvil_thirteen.mid", build_bellowsworks, BW_TOTAL, BW_BPM),
    ("gildmaw_exchange_open_outcry.mid", build_gildmaw, GE_TOTAL, GE_BPM),
    ("vellumscale_archive_slow_accretion.mid", build_vellumscale, VA_TOTAL, VA_BPM),
]


def main():
    for fname, builder, total, bpm in PIECES:
        tracks = builder()
        path = os.path.join(OUT, fname)
        # hard guarantee: nothing may still be sounding at the loop point
        for t in tracks:
            end = t.last_note_off()
            if end > total:
                raise SystemExit("%s: %s sounds past the loop point (%d > %d)"
                                 % (fname, t.name, end, total))
        size = write_smf(path, tracks)
        secs = total / float(DIV) * 60.0 / bpm
        print("wrote %-42s %6d bytes  %2d tracks  %6.2f s  %d ticks"
              % (fname, size, len(tracks), secs, total))


if __name__ == "__main__":
    main()
